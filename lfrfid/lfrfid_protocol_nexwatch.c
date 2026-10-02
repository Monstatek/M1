/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Honeywell NexWatch protocol
 *
 * PSK1 modulation, 255us/bit, 96-bit frame: an 8-bit preamble (0b01010110),
 * a mandatory 32-bit all-zero reserved field, a 32-bit scrambled ID, a
 * 4-bit mode, a 4-bit parity (checked), and a 16-bit trailing field
 * (mode/parity+checksum region, not checked by can_be_decoded -- matches
 * the required behavior). The scrambled ID is a fixed bit permutation
 * (nexwatch_descramble) preserved from the licensed source, not derivable
 * from first principles.
 *
 * Same raw-level, four-hypothesis decode strategy as lfrfid_protocol_keri.c
 * (see that file's header for the full rationale) -- byte-for-byte with
 * the protocol decoder, including its
 * duration thresholds and +-120us phase-correction retry.
 *
 * Every bit-level operation (preamble check, reserved-field/parity
 * validation, ID/mode/checksum field extraction, the descramble table and
 * magic-type table) was re-derived and checked against that source. Read,
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

#if defined(LFRFID_NEXWATCH_ENABLED)

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "uiView.h"

#include "lfrfid.h"
#include "lfrfid_psk1_wave.h"

/*************************** D E F I N E S ************************************/

#define NEXWATCH_US_PER_BIT  (255U)
#define NEXWATCH_EMUL_GPIO_PIN (2U) /* RFID_PULL / PA2 */

/***************************** V A R I A B L E S ******************************/

/* Four parallel decode hypotheses -- see lfrfid_protocol_keri.c's header
 * for why. Persistent across execute() calls. */
static uint8_t g_nw_buf_pos[NEXWATCH_ENCODED_SIZE];
static uint8_t g_nw_buf_neg[NEXWATCH_ENCODED_SIZE];
static uint8_t g_nw_buf_corr_pos[NEXWATCH_ENCODED_SIZE];
static uint8_t g_nw_buf_corr_neg[NEXWATCH_ENCODED_SIZE];
static uint8_t g_nw_decoded[NEXWATCH_DECODED_SIZE];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *nw_get_data(void *proto);
static void     nw_decoder_begin(void *proto);
static bool     nw_decoder_execute(void *proto, uint16_t size);
static bool     nw_encoder_begin(void *proto);
static void     nw_encoder_send(void *proto);
static void     nw_write_begin(void *protocol, void *data);
static void     nw_write_send(void *proto);
static void     nw_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first primitives, same semantics as every other protocol in this port. */

static uint8_t nw_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8] >> (7 - (position % 8))) & 1u);
}

static void nw_set_bit(uint8_t *data, size_t position, uint8_t bit)
{
    if(bit & 1u)
        data[position / 8] |= (uint8_t)(1u << (7 - (position % 8)));
    else
        data[position / 8] &= (uint8_t)~(1u << (7 - (position % 8)));
}

static uint32_t nw_get_bits(const uint8_t *data, size_t position, int nbits)
{
    uint32_t v = 0;
    for(int b = 0; b < nbits; b++)
        v = (v << 1) | nw_get_bit(data, position + b);
    return v;
}

/* Shift a new bit into the MSB-first buffer (oldest bits fall off byte 0). */
static void nw_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*========================= D E C O D E ======================================*/
/*
 * Preamble: bits[0:8) == 0b01010110. Reserved: bits[8:40) == 0 (32 bits,
 * mandatory). Parity: bits[76:80) must equal the parity computed over the
 * 5 "hex" bytes at bits[40:80) (the last of which is masked to its high
 * nibble only -- the mode field. The
 * checksum (bits[80:96)) is deliberately NOT checked here -- byte-for-byte
 * with frame validation, which only validates it
 * for display (magic-type lookup), not for frame acceptance.
 */
static bool nw_check_preamble(const uint8_t *data)
{
    return nw_get_bits(data, 0, 8) == 0x56u; /* 0b01010110 */
}

static uint8_t nw_parity_swap(uint8_t parity)
{
    uint8_t a = (parity >> 3) & 1u;
    a |= ((parity >> 1) & 1u) << 1;
    a |= ((parity >> 2) & 1u) << 2;
    a |= (parity & 1u) << 3;
    return a;
}

static uint8_t nw_parity(const uint8_t hexid[5])
{
    uint8_t p = 0;
    for(int i = 0; i < 5; i++) {
        p ^= (hexid[i] & 0xF0u) >> 4;
        p ^= (hexid[i] & 0x0Fu);
    }
    return nw_parity_swap(p);
}

static bool nw_can_be_decoded(const uint8_t *data)
{
    if(!nw_check_preamble(data))
        return false;
    if(nw_get_bits(data, 8, 32) != 0)  /* reserved field must be all-zero */
        return false;

    uint8_t hex[5];
    for(int i = 0; i < 5; i++)
        hex[i] = (uint8_t)nw_get_bits(data, 40 + (i * 8), 8);
    hex[4] &= 0xF0u; /* only the mode nibble participates in parity */

    uint8_t parity = (uint8_t)nw_get_bits(data, 76, 4);
    return nw_parity(hex) == parity;
}

/* Feed `bit_count` copies of `polarity` into `data`, checking after every
 * single push
 * protocol_nexwatch_decoder_feed_internal. */
static bool nw_feed_internal(bool polarity, uint32_t time_us, uint8_t *data)
{
    time_us += (NEXWATCH_US_PER_BIT / 2);
    uint32_t bit_count = time_us / NEXWATCH_US_PER_BIT;

    if(bit_count >= NEXWATCH_ENCODED_BITS)
        return false;

    for(uint32_t i = 0; i < bit_count; i++) {
        nw_push_bit(data, NEXWATCH_ENCODED_SIZE, polarity ? 1u : 0u);
        if(nw_can_be_decoded(data))
            return true;
    }
    return false;
}

/* Byte layout uses the canonical decoder-save representation,
 * including byte[0] always being 0 (the low byte of `id` is shifted fully
 * out by four `>>= 8` steps before the last assignment in the original --
 * kept as-is for interop rather than "corrected"). */
static void nw_save_from(const uint8_t *data)
{
    uint32_t id = nw_get_bits(data, 40, 32);
    g_nw_decoded[4] = (uint8_t)id;
    g_nw_decoded[3] = (uint8_t)(id >>= 8);
    g_nw_decoded[2] = (uint8_t)(id >>= 8);
    g_nw_decoded[1] = (uint8_t)(id >>= 8);
    g_nw_decoded[0] = (uint8_t)(id >>= 8);

    uint32_t check = nw_get_bits(data, 72, 24);
    g_nw_decoded[7] = (uint8_t)check;
    g_nw_decoded[6] = (uint8_t)(check >>= 8);
    g_nw_decoded[5] = (uint8_t)(check >>= 8);
}

static void nw_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_nw_buf_pos, 0, sizeof(g_nw_buf_pos));
    memset(g_nw_buf_neg, 0, sizeof(g_nw_buf_neg));
    memset(g_nw_buf_corr_pos, 0, sizeof(g_nw_buf_corr_pos));
    memset(g_nw_buf_corr_neg, 0, sizeof(g_nw_buf_corr_neg));
}

static bool nw_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        bool level = (evt[i].edge != 0);
        uint32_t duration = evt[i].t_us;

        if(duration > (NEXWATCH_US_PER_BIT / 2)) {
            if(nw_feed_internal(level, duration, g_nw_buf_pos)) {
                nw_save_from(g_nw_buf_pos);
                memcpy(lfrfid_tag_info.uid, g_nw_decoded, NEXWATCH_DECODED_SIZE);
                return true;
            }
            if(nw_feed_internal(!level, duration, g_nw_buf_neg)) {
                nw_save_from(g_nw_buf_neg);
                memcpy(lfrfid_tag_info.uid, g_nw_decoded, NEXWATCH_DECODED_SIZE);
                return true;
            }
        }

        if(duration > (NEXWATCH_US_PER_BIT / 4)) {
            uint32_t corrected = duration;
            if(level) {
                corrected += 120U;
            } else if(corrected > 120U) {
                corrected -= 120U;
            }

            if(nw_feed_internal(level, corrected, g_nw_buf_corr_pos)) {
                nw_save_from(g_nw_buf_corr_pos);
                memcpy(lfrfid_tag_info.uid, g_nw_decoded, NEXWATCH_DECODED_SIZE);
                return true;
            }
            if(nw_feed_internal(!level, corrected, g_nw_buf_corr_neg)) {
                nw_save_from(g_nw_buf_corr_neg);
                memcpy(lfrfid_tag_info.uid, g_nw_decoded, NEXWATCH_DECODED_SIZE);
                return true;
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
static uint8_t *nw_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

/* ID descramble table -- preserved from the licensed source
 * protocol_nexwatch_descramble (hex_2_id[]): destination bit position (in
 * the final ID) that source bit n of the raw scrambled field maps to. */
static void nw_descramble(uint32_t *id, uint32_t scrambled)
{
    static const uint8_t hex_2_id[] = {
        31, 27, 23, 19, 15, 11, 7, 3, 30, 26, 22, 18, 14, 10, 6, 2,
        29, 25, 21, 17, 13, 9,  5, 1, 28, 24, 20, 16, 12, 8,  4, 0
    };

    *id = 0;
    for(uint8_t idx = 0; idx < 32; idx++) {
        bool bit = (scrambled >> hex_2_id[idx]) & 1u;
        *id |= ((uint32_t)bit << (31 - idx));
    }
}

/* Checksum + magic-byte type table -- preserved from the licensed source
 * protocol_nexwatch_checksum / magic_items[]. Used only to label the card
 * type on the details screen; not part of frame acceptance. */
static uint8_t nw_checksum(uint8_t magic, uint32_t id, uint8_t parity)
{
    uint8_t a = (uint8_t)((id >> 24) & 0xFFu);
    a -= (uint8_t)((id >> 16) & 0xFFu);
    a -= (uint8_t)((id >> 8) & 0xFFu);
    a -= (uint8_t)(id & 0xFFu);
    a -= magic;

    uint8_t p_hi_nibble = (uint8_t)(parity >> 4); /* bit_lib_reverse_8_fast(parity) >> 4, parity is 4 bits so reverse-then->>4 == 0 when parity<16 in practice; kept literal below */
    (void)p_hi_nibble;
    /* a -= (bit_lib_reverse_8_fast(parity) >> 4). Reversing an
     * 8-bit value that only ever holds a 4-bit parity nibble (0..15) in
     * its low bits, then shifting right by 4, reproduces the standard
     * bit-reversal of that nibble into the high nibble's mirror -- done
     * directly here via a 4-bit reverse table rather than an 8-bit
     * bit_lib call this port does not have. */
    static const uint8_t nibble_rev[16] = {
        0x0,0x8,0x4,0xC,0x2,0xA,0x6,0xE,0x1,0x9,0x5,0xD,0x3,0xB,0x7,0xF
    };
    a -= nibble_rev[parity & 0xFu];

    /* bit_lib_reverse_8_fast(a) -- full 8-bit reversal. */
    uint8_t r = 0;
    for(int b = 0; b < 8; b++)
        r |= (uint8_t)(((a >> b) & 1u) << (7 - b));
    return r;
}

static void nw_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = nw_get_data(NULL);

    uint32_t scrambled = nw_get_bits(d, 0, 32); /* bytes[0..3] = 0,id_hi,id_mid,id_lo3 -- see below */
    /* g_nw_decoded[0] is always 0 (see nw_save_from), so
     * the 32-bit scrambled ID is actually bytes[1..4]; re-read precisely: */
    scrambled = ((uint32_t)d[1] << 24) | ((uint32_t)d[2] << 16) |
                ((uint32_t)d[3] << 8) | (uint32_t)d[4];

    uint32_t id = 0;
    nw_descramble(&id, scrambled);

    uint8_t mode   = (uint8_t)((d[5] >> 4) & 0x0Fu);
    uint8_t parity = (uint8_t)(d[5] & 0x0Fu);
    uint8_t chk    = d[6];

    static const struct { uint8_t magic; const char *desc; } magic_items[] = {
        {0xBE, "Quadrakey"}, {0x88, "Nexkey"}, {0x86, "Honeywell"}
    };
    const char *type = "Unknown";
    for(size_t m = 0; m < sizeof(magic_items)/sizeof(magic_items[0]); m++) {
        if(nw_checksum(magic_items[m].magic, id, parity) == chk) {
            type = magic_items[m].desc;
            break;
        }
    }

    sprintf(result,
            "ID: %lu\n"
            "Mode: %u\n"
            "Type: %s",
            (unsigned long)id, (unsigned)mode, type);
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * Build the 96-bit wire frame (preamble + reserved-zero + ID/mode/parity,
 * checksum tail left as stored) from the stored 8-byte credential --
 * using the required frame layout
 * construction (the GPIO-wave-yield half is intentionally not ported; see
 * the header).
 */
static void nw_encode(const uint8_t *decoded, uint8_t *encoded)
{
    memset(encoded, 0, NEXWATCH_ENCODED_SIZE);
    /* preamble (bits 0-7) + reserved (bits 8-39, stays 0) */
    nw_set_bit(encoded, 0, 0); nw_set_bit(encoded, 1, 1);
    nw_set_bit(encoded, 2, 0); nw_set_bit(encoded, 3, 1);
    nw_set_bit(encoded, 4, 0); nw_set_bit(encoded, 5, 1);
    nw_set_bit(encoded, 6, 1); nw_set_bit(encoded, 7, 0);

    for(int b = 0; b < 32; b++) /* bits 40-71: 32-bit scrambled ID */
        nw_set_bit(encoded, 40 + b, nw_get_bit(decoded, 8 + b));
    for(int b = 0; b < 24; b++) /* bits 72-95: mode+parity+tail */
        nw_set_bit(encoded, 72 + b, nw_get_bit(decoded, 40 + b));
}

/*========================= E M U L A T E  (PSK1 RF/2) ======================*/
static bool nw_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t encoded[NEXWATCH_ENCODED_SIZE];
    size_t step_count = 0;

    nw_encode(lfrfid_tag_info.uid, encoded);
    if(lfrfid_psk1_encoded_to_wave(encoded, NEXWATCH_ENCODED_BITS,
                                   NEXWATCH_EMUL_GPIO_PIN,
                                   lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                   &step_count) != 0) {
        lfrfid_encoded_data.length = 0;
        return false;
    }

    lfrfid_encoded_data.length = (uint16_t)step_count;
    return true;
}

static void nw_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*
 * PSK1, RF/32, T5577 blocks 1..3 (96-bit frame, MSB-first) -- byte-for-byte
 * with the required T5577 configuration word.
 */
static void nw_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t          encoded[NEXWATCH_ENCODED_SIZE];

    nw_encode(tag_data->uid, encoded);

    if(lfrfid_tag_info_back)
        memcpy(lfrfid_tag_info_back->uid, tag_data->uid, NEXWATCH_DECODED_SIZE);

    if(write && write->type == LFRFIDProgramTypeT5577) {
        write->t5577.block_data[0] =
            T5577_MOD_PSK1 | T5577_BITRATE_RF_32 | T5577_TRANS_BL_1_3;
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded, &write->t5577.block_data[1], 3);
        write->t5577.max_blocks = 4;
    }
}

static void nw_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_nexwatch = {
    .name         = "Nexwatch",
    .manufacturer = "Honeywell",
    .data_size    = NEXWATCH_DECODED_SIZE,
    .features     = LFRFIDFeaturePSK,
    .get_data     = (lfrfidProtocolGetData)nw_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)nw_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)nw_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)nw_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)nw_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)nw_write_begin,
        .send  = (lfrfidProtocolWriteSend)nw_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)nw_render_data,
};

#endif /* LFRFID_NEXWATCH_ENABLED */
