/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Keri Systems protocol
 *
 * Keri, PSK1 modulation, RF/2 bit rate (255us/bit), 64-bit frame: a fixed
 * 33-bit preamble (0xE0 000000 then a forced '1' marker bit) followed by
 * a 31-bit internal ID. The internal ID is itself a fixed bit-scramble of
 * a facility code and card number (see keri_descramble) -- reverse
 * engineered by the RFID community, not derivable from first principles;
 * the mapping tables are preserved verbatim from the licensed source.
 *
 * Unlike every other protocol in this port (all ASK/FSK, decoded via the
 * shared fsk_symbol_feed/fsk_bit_feed helpers), PSK1 has no sub-carrier to
 * classify -- the raw signal LEVEL sampled once per bit-clock tick IS the
 * data. The decoder uses this directly: for every incoming edge
 * it samples the just-ended level across however many 255us bit-periods
 * it spanned and pushes that many copies into an MSB-first shift-register
 * buffer, checking the fixed preamble pattern after every single push.
 * Because it cannot know a priori which absolute level the antenna's
 * analog front-end calls "high", it runs this in FOUR buffers at once --
 * the level as given, its complement, and both again with the duration
 * nudged +-120us to catch a slightly mis-synced first edge -- and accepts
 * whichever one's preamble matches first. That redundancy is why this
 * port doesn't need to resolve the exact level/edge polarity convention
 * of M1's own lfrfid_evt_t stream: whichever convention
 * is used, one of the four hypothesis buffers ends up correct.
 *
 * Every bit-level operation (preamble check, ID field extraction, the
 * descramble table) was re-derived and checked against that source. Read,
 * RF emulation, and T5577 clone/write are implemented.
 *
 * The functional implementation and modifications were
 * independently developed by Monstatek.
 *
 * Copyright (C) 2026 Monstatek
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 */
/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "uiView.h"

#include "lfrfid.h"
#include "lfrfid_psk1_wave.h"

/*************************** D E F I N E S ************************************/

#define KERI_US_PER_BIT  (255U)
#define KERI_EMUL_GPIO_PIN (2U) /* RFID_PULL / PA2 */

/***************************** V A R I A B L E S ******************************/

/* Four parallel decode hypotheses: as-given
 * level, complemented level, and both again with the duration corrected
 * for a slightly mis-synced first edge. Persistent across execute() calls
 * -- a frame can span many batches. */
static uint8_t g_keri_buf_pos[KERI_ENCODED_SIZE];
static uint8_t g_keri_buf_neg[KERI_ENCODED_SIZE];
static uint8_t g_keri_buf_corr_pos[KERI_ENCODED_SIZE];
static uint8_t g_keri_buf_corr_neg[KERI_ENCODED_SIZE];
static uint8_t g_keri_decoded[KERI_DECODED_SIZE];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *keri_get_data(void *proto);
static void     keri_decoder_begin(void *proto);
static bool     keri_decoder_execute(void *proto, uint16_t size);
static bool     keri_encoder_begin(void *proto);
static void     keri_encoder_send(void *proto);
static void     keri_write_begin(void *protocol, void *data);
static void     keri_write_send(void *proto);
static void     keri_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first primitives, same semantics as every other protocol in this port. */

static uint8_t keri_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8] >> (7 - (position % 8))) & 1u);
}

static void keri_set_bit(uint8_t *data, size_t position, uint8_t bit)
{
    if(bit & 1u)
        data[position / 8] |= (uint8_t)(1u << (7 - (position % 8)));
    else
        data[position / 8] &= (uint8_t)~(1u << (7 - (position % 8)));
}

/* Read 32 MSB-first bits at an arbitrary position into one uint32_t. */
static uint32_t keri_get_bits32(const uint8_t *data, size_t position)
{
    uint32_t v = 0;
    for(int b = 0; b < 32; b++)
        v = (v << 1) | keri_get_bit(data, position + b);
    return v;
}

/* Shift a new bit into the MSB-first buffer (oldest bits fall off byte 0). */
static void keri_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*========================= D E C O D E ======================================*/
/*
 * Preamble: bits [0..31] == 0xE0 0x00 0x00 0x00, bit 32 == 1. Checked at
 * bit offset 0 (this frame) and offset 64 (the next repeat, immediately
 * following in a continuously-transmitted signal) before accepting --
 * using the required frame validation.
 */
static bool keri_check_preamble(const uint8_t *data, size_t bit_index)
{
    size_t byte0 = bit_index / 8;
    if(data[byte0] != 0xE0 || data[byte0 + 1] != 0x00 ||
       data[byte0 + 2] != 0x00 || data[byte0 + 3] != 0x00)
        return false;
    if(keri_get_bit(data, bit_index + 32) != 1)
        return false;
    return true;
}

static bool keri_can_be_decoded(const uint8_t *data)
{
    return keri_check_preamble(data, 0) && keri_check_preamble(data, 64);
}

/* Feed `bit_count` copies of `polarity` (the level held for this many
 * 255us bit-periods) into `data`, checking after every single push --
 * using the required decoder state transitions. Returns
 * true (and leaves `data` holding the just-completed frame) the moment a
 * valid preamble is found. */
static bool keri_feed_internal(bool polarity, uint32_t time_us, uint8_t *data)
{
    time_us += (KERI_US_PER_BIT / 2);
    uint32_t bit_count = time_us / KERI_US_PER_BIT;

    if(bit_count >= KERI_ENCODED_BITS)
        return false;

    for(uint32_t i = 0; i < bit_count; i++) {
        keri_push_bit(data, KERI_ENCODED_SIZE, polarity ? 1u : 0u);
        if(keri_can_be_decoded(data))
            return true;
    }
    return false;
}

static void keri_save_from(const uint8_t *data)
{
    uint32_t id = keri_get_bits32(data, 32);
    g_keri_decoded[3] = (uint8_t)id;
    g_keri_decoded[2] = (uint8_t)(id >>= 8);
    g_keri_decoded[1] = (uint8_t)(id >>= 8);
    g_keri_decoded[0] = (uint8_t)(id >>= 8);
}

static void keri_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_keri_buf_pos, 0, sizeof(g_keri_buf_pos));
    memset(g_keri_buf_neg, 0, sizeof(g_keri_buf_neg));
    memset(g_keri_buf_corr_pos, 0, sizeof(g_keri_buf_corr_pos));
    memset(g_keri_buf_corr_neg, 0, sizeof(g_keri_buf_corr_neg));
}

static bool keri_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        bool level = (evt[i].edge != 0);
        uint32_t duration = evt[i].t_us;

        if(duration > (KERI_US_PER_BIT / 2)) {
            if(keri_feed_internal(level, duration, g_keri_buf_pos)) {
                keri_save_from(g_keri_buf_pos);
                memcpy(lfrfid_tag_info.uid, g_keri_decoded, KERI_DECODED_SIZE);
                return true;
            }
            if(keri_feed_internal(!level, duration, g_keri_buf_neg)) {
                keri_save_from(g_keri_buf_neg);
                memcpy(lfrfid_tag_info.uid, g_keri_decoded, KERI_DECODED_SIZE);
                return true;
            }
        }

        if(duration > (KERI_US_PER_BIT / 4)) {
            /* Wrong-phase-sync recovery: nudge the duration the same way
             * required before retrying, in a separate pair of buffers
             * so it never corrupts the primary hypothesis. */
            uint32_t corrected = duration;
            if(level) {
                corrected += 120U;
            } else if(corrected > 120U) {
                corrected -= 120U;
            }

            if(keri_feed_internal(level, corrected, g_keri_buf_corr_pos)) {
                keri_save_from(g_keri_buf_corr_pos);
                memcpy(lfrfid_tag_info.uid, g_keri_decoded, KERI_DECODED_SIZE);
                return true;
            }
            if(keri_feed_internal(!level, corrected, g_keri_buf_corr_neg)) {
                keri_save_from(g_keri_buf_corr_neg);
                memcpy(lfrfid_tag_info.uid, g_keri_decoded, KERI_DECODED_SIZE);
                return true;
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (populated by live read AND by
 * file load / Add Manually / reload), matching the other protocols. */
static uint8_t *keri_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

/* Facility code / card number descramble table -- Keri's internal ID is a
 * fixed bit permutation of these two fields, reverse-engineered by the
 * RFID community. card_to_id[n]/card_to_fc[n] give the destination bit
 * position (in cn/fc respectively) that source bit n maps to; 255 means
 * "not part of this field". Preserved from the licensed source
 * protocol_keri_descramble -- there is no derivation, this table IS the
 * protocol. */
static void keri_descramble(uint32_t *fc, uint32_t *cn, uint32_t internal_id)
{
    static const uint8_t card_to_id[] = {
        255, 255, 255, 255, 13, 12, 20, 5,   16,  6,  21,
        17,  8,   255, 0,   7,  10, 15, 255, 11,  4,  1,
        255, 18,  255, 19,  2,  14, 3,  9,   255, 255
    };
    static const uint8_t card_to_fc[] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 0,   255, 255, 255, 255, 2,   255, 255, 255,
        3,   255, 4,   255, 255, 255, 255, 255, 1,   255
    };

    *fc = 0;
    *cn = 0;
    for(uint8_t card_idx = 0; card_idx < 32; card_idx++) {
        bool bit = (internal_id >> card_idx) & 1u;
        if(card_to_id[card_idx] < 32)
            *cn |= (bit << card_to_id[card_idx]);
        if(card_to_fc[card_idx] < 32)
            *fc |= (bit << card_to_fc[card_idx]);
    }
}

static void keri_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = keri_get_data(NULL);
    uint32_t data = keri_get_bits32(d, 0);
    uint32_t internal_id = data & 0x7FFFFFFFU;
    uint32_t fc = 0, cn = 0;

    keri_descramble(&fc, &cn, data);

    sprintf(result,
            "Internal ID: %lu\n"
            "FC: %lu\n"
            "Card: %lu",
            (unsigned long)internal_id, (unsigned long)fc, (unsigned long)cn);
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * Build the 64-bit wire frame (preamble + forced marker bit + 31-bit ID)
 * from the stored four-byte credential
 * protocol_keri_encoder_start's frame construction (the GPIO-wave-yield
 * half of that function is intentionally not ported; see the header).
 */
static void keri_encode(const uint8_t *decoded, uint8_t *encoded)
{
    memset(encoded, 0, KERI_ENCODED_SIZE);
    encoded[0] = 0xE0; /* preamble; bytes 1..3 stay 0 */
    for(int b = 0; b < 32; b++)
        keri_set_bit(encoded, 32 + b, keri_get_bit(decoded, b));
    keri_set_bit(encoded, 32, 1); /* forced marker bit, overwrites bit 31 of the stored id */
}

/*========================= E M U L A T E  (PSK1 RF/2) ======================*/
static bool keri_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t encoded[KERI_ENCODED_SIZE];
    size_t step_count = 0;

    keri_encode(lfrfid_tag_info.uid, encoded);
    if(lfrfid_psk1_encoded_to_wave(encoded, KERI_ENCODED_BITS,
                                   KERI_EMUL_GPIO_PIN,
                                   lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                   &step_count) != 0) {
        lfrfid_encoded_data.length = 0;
        return false;
    }

    lfrfid_encoded_data.length = (uint16_t)step_count;
    return true;
}

static void keri_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*
 * PSK1, RF/2, T5577 blocks 1..2 (64-bit frame, MSB-first). Config value in
 * block 0 uses the required literal: most
 * fields map onto M1's named T5577_* constants one-for-one, but the
 * TESTMODE_DISABLED and PSK1 bit-rate (0xF) values have no equivalent
 * named constant in t5577.h -- kept as a literal rather
 * than guessed at, since T5577 config words are not something to improve
 * on without a reason to believe the improvement is correct.
 */
static void keri_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t          encoded[KERI_ENCODED_SIZE];

    keri_encode(tag_data->uid, encoded);

    if(lfrfid_tag_info_back)
        memcpy(lfrfid_tag_info_back->uid, tag_data->uid, KERI_DECODED_SIZE);

    if(write && write->type == LFRFIDProgramTypeT5577) {
        write->t5577.block_data[0] =
            0x60000000UL                 /* TESTMODE_DISABLED */
            | T5577_X_MODE
            | T5577_MOD_PSK1
            | T5577_PSKCF_RF_2
            | T5577_TRANS_BL_1_2
            | (0xFUL << 18);             /* PSK1 bit-rate field */
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded, &write->t5577.block_data[1], 2);
        write->t5577.max_blocks = 3;
    }
}

static void keri_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_keri = {
    .name         = "Keri",
    .manufacturer = "Keri",
    .data_size    = KERI_DECODED_SIZE,
    .features     = LFRFIDFeaturePSK,
    .get_data     = (lfrfidProtocolGetData)keri_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)keri_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)keri_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)keri_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)keri_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)keri_write_begin,
        .send  = (lfrfidProtocolWriteSend)keri_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)keri_render_data,
};
