/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — AWID protocol
 *
 * AWID, FSK2a modulation, RF/50 bit rate, 96-bit frame supporting format
 * lengths 26, 34, 36, 37 and 50. The 96-bit frame is an 8-bit preamble (0x01)
 * followed by 22 four-bit groups, each carrying 3 payload bits plus an odd
 * parity bit. Removing the parity bits yields the 66-bit payload; its first 8
 * bits are the format length. The decoded credential stores the full 66-bit
 * payload (9 bytes) without truncation.
 *
 * The parity gate and payload extraction preserve the supported on-air
 * representation; the full decoded payload remains available for encoding.
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

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "uiView.h"

#include "lfrfid.h"

/*************************** D E F I N E S ************************************/

#define AWID_PARITY_GROUPS  (22)     /* 88 payload+parity bits / 4 */
#define AWID_PAYLOAD_BITS   (66)     /* 22 * 3 data bits           */

/* FSK2a emulation timing (RF/50) — identical to the proven Pyramid emulator
 * (fc/8 = 64 us -> 6 cycles per 0-bit, fc/10 = 80 us -> 5 cycles per 1-bit). */
/* True nominal FSK2a half/period values. The "-2" that used to sit on the
 * PERIOD constants compensated for the old per-edge ISR path writing
 * TIM5->ARR = time_us with no inclusive-counter "-1" correction
 * (rfid_emul_handler(), lfrfid_hal.c, now removed) -- see lfrfid_dma_tx.c's
 * header comment for the full derivation. The new GPDMA2-driven transport
 * performs that correction itself, once, from the real measured TIM5
 * kernel clock -- so it must not be duplicated here. */
#define AWID_OUTPUT_INVERT     (0)
#define AWID_EMUL_HALF_ONE_US  (40)          /* fc/10 half period */
#define AWID_EMUL_HALF_ZERO_US (32)          /* fc/8  half period */
#define AWID_EMUL_PERIOD_ONE_US  (80)
#define AWID_EMUL_PERIOD_ZERO_US (64)
#define AWID_EMUL_CYCLES_ONE   (5)           /* fc/10 -> 5 cycles */
#define AWID_EMUL_CYCLES_ZERO  (6)           /* fc/8  -> 6 cycles */
#define AWID_EMUL_GPIO_PIN     (2)           /* RFID_OUT pin on GPIOA */

/***************************** V A R I A B L E S ******************************/

static uint8_t g_awid_encoded[AWID_ENCODED_SIZE];   /* 13-byte shift register */
static uint8_t g_awid_decoded[AWID_DECODED_SIZE];   /* 9-byte credential      */
static fsk_symbol_state_t g_awid_sym_st;            /* reused RF/50, read-only */
static fsk_bit_state_t    g_awid_bit_st;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *awid_get_data(void *proto);
static void     awid_decoder_begin(void *proto);
static bool     awid_decoder_execute(void *proto, uint16_t size);
static bool     awid_encoder_begin(void *proto);
static void     awid_encoder_send(void *proto);
static void     awid_write_begin(void *protocol, void *data);
static void     awid_write_send(void *proto);
static void     awid_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives. */

static uint8_t awid_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8] >> (7 - (position % 8))) & 1u);
}

static void awid_set_bit(uint8_t *data, size_t position, uint8_t bit)
{
    if(bit & 1u)
        data[position / 8] |= (uint8_t)(1u << (7 - (position % 8)));
    else
        data[position / 8] &= (uint8_t)~(1u << (7 - (position % 8)));
}

static uint8_t awid_get_byte(const uint8_t *data, size_t position)
{
    uint8_t v = 0;
    for(int b = 0; b < 8; b++)
        v = (uint8_t)((v << 1) | awid_get_bit(data, position + b));
    return v;
}

/* Read `length` (<=8) MSB-first bits at an arbitrary position. */
static uint8_t awid_get_bits(const uint8_t *data, size_t position, uint8_t length)
{
    uint8_t v = 0;
    for(uint8_t b = 0; b < length; b++)
        v = (uint8_t)((v << 1) | awid_get_bit(data, position + b));
    return v;
}

/* Write the low `length` bits (<=8) of `byte`, MSB-first, at `position`. */
static void awid_set_bits(uint8_t *data, size_t position, uint8_t byte, uint8_t length)
{
    for(uint8_t i = 0; i < length; i++) {
        uint8_t shift = (uint8_t)((length - 1) - i);
        awid_set_bit(data, position + i, (byte >> shift) & 1u);
    }
}

static void awid_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/* Extract the 66-bit de-parity'd payload: keep the first 3 bits of each of the
 * 22 four-bit groups starting at bit 8,
 * position 8, length 88, n 4). Result packed MSB-first into decoded[0..8]. */
static void awid_extract_payload(const uint8_t *encoded, uint8_t *decoded)
{
    memset(decoded, 0, AWID_DECODED_SIZE);
    int dst = 0;
    for(int g = 0; g < AWID_PARITY_GROUPS; g++) {
        int base = 8 + g * 4;
        for(int b = 0; b < 3; b++) {
            if(awid_get_bit(encoded, base + b))
                decoded[dst / 8] |= (uint8_t)(1u << (7 - (dst % 8)));
            dst++;
        }
    }
    /* dst == 66 */
}

/*========================= D E C O D E ======================================*/
/*
 * Frame validation:
 *   - leading preamble  : byte 0  == 0x01
 *   - trailing preamble : byte 12 == 0x01 (next frame's preamble when looped)
 *   - odd-4-bit parity gate over bits 8..95 (see below)
 *   - format length (first payload byte) in {26,34,36,37,50}
 *
 * Parity gate computes
 *     parity_error = bit_lib_test_parity(data, 8, 88, BitLibParityOdd, 4)
 * and rejects when parity_error is true. bit_lib_test_parity returns true only
 * when EVERY 4-bit group has EVEN parity (it stops at the first odd-parity
 * group). A valid AWID frame has all groups odd, so parity_error is false and
 * the frame is accepted. This weak-by-design semantics is reproduced exactly
 * so validation accepts only the intended frames.
 */
static bool awid_parity_gate_all_even(const uint8_t *data)
{
    bool result = true; /* mirrors bit_lib_test_parity's local `result` */
    for(int i = 0; i < AWID_PARITY_GROUPS; i++) {
        uint8_t block = awid_get_bits(data, 8 + i * 4, 4);
        /* bit_lib_test_parity_32(block, Odd) == !parity(block) */
        int odd = __builtin_parity(block);          /* 1 if odd # of 1s */
        bool tp = !odd;                              /* test_parity_32(Odd) */
        if(!tp) result = false;
        if(!result) break;
    }
    return result; /* true => all groups even => reject */
}

static bool awid_can_be_decoded(const uint8_t *data)
{
    if(data[0] != 0x01)
        return false;
    if(data[AWID_ENCODED_SIZE - 1] != 0x01)          /* byte 12: next preamble */
        return false;

    if(awid_parity_gate_all_even(data))              /* parity error */
        return false;

    uint8_t payload[AWID_DECODED_SIZE];
    awid_extract_payload(data, payload);
    uint8_t len = payload[0];
    if(len != 26 && len != 34 && len != 36 && len != 37 && len != 50)
        return false;

    return true;
}

static void awid_decode(const uint8_t *encoded, uint8_t *decoded)
{
    awid_extract_payload(encoded, decoded);
}

/*========================= D E C O D E R  I / F =============================*/

static void awid_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_awid_encoded, 0, sizeof(g_awid_encoded));
    memset(g_awid_decoded, 0, sizeof(g_awid_decoded));
    fsk_symbol_state_init(&g_awid_sym_st);
    fsk_bit_state_init(&g_awid_bit_st);
}

static bool awid_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint8_t symbol;
        /* Reused, read-only RF/50 sub-carrier classification + bit grouping,
         * identical to the validated Pyramid decoder. */
        if(fsk_symbol_feed(&g_awid_sym_st, &evt[i], &symbol)) {
            uint8_t bit;
            if(fsk_bit_feed(&g_awid_bit_st, symbol, &bit)) {
                awid_push_bit(g_awid_encoded, AWID_ENCODED_SIZE, bit);

                if(awid_can_be_decoded(g_awid_encoded)) {
                    awid_decode(g_awid_encoded, g_awid_decoded);
                    memcpy(lfrfid_tag_info.uid, g_awid_decoded,
                           min(sizeof(lfrfid_tag_info.uid), (size_t)AWID_DECODED_SIZE));
                    return true;
                }
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (populated by live read AND by file
 * load / Add Manually / reload), matching the other protocols. Never read the
 * decoder-private scratch (g_awid_decoded). */
static uint8_t *awid_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void awid_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = awid_get_data(NULL);
    uint8_t fmt = d[0];

    if(fmt == 26) {
        uint8_t  facility = awid_get_byte(d, 9);
        uint16_t card = (uint16_t)((awid_get_byte(d, 17) << 8) | awid_get_byte(d, 25));
        sprintf(result,
                "Format: 26\n"
                "FC: %u\n"
                "Card: %hu",
                facility, card);
    } else {
        /* 34/36/37/50: display format + full raw 9-byte payload. */
        sprintf(result,
                "Format: %u\n"
                "Data: %02X%02X%02X%02X%02X%02X%02X%02X%02X",
                fmt, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
    }
}

/*========================= E N C O D E ======================================*/
/* Encodes the preamble followed by 22 groups of
 * (3 payload bits << 1 | odd parity). */
static void awid_encode(const uint8_t *decoded, uint8_t *encoded)
{
    memset(encoded, 0, AWID_ENCODED_SIZE);
    awid_set_bits(encoded, 0, 0x01, 8);              /* preamble */

    for(int i = 0; i < AWID_PARITY_GROUPS; i++) {
        uint8_t value = (uint8_t)(awid_get_bits(decoded, i * 3, 3) << 1);
        value |= (uint8_t)(!__builtin_parity(value)); /* odd parity in bit 0 */
        awid_set_bits(encoded, 8 + i * 4, value, 4);
    }
}

/* Normalize a working credential before write-data generation
 * does before building the frame: coerce an invalid format length to 26, then
 * round-trip through encode / de-parity / decode so the stored payload equals
 * what the emitted frame decodes to. Operates on a caller-owned copy — the
 * canonical lfrfid_tag_info.uid is never mutated (see awid_write_begin). */
static void awid_normalize(uint8_t *work, uint8_t *encoded)
{
    if(work[0] != 26 && work[0] != 50 && work[0] != 37 && work[0] != 34 && work[0] != 36)
        work[0] = 26;

    awid_encode(work, encoded);
    awid_extract_payload(encoded, work);   /* de-parity back into work */
    awid_encode(work, encoded);            /* final frame */
}

/*========================= E M U L A T E  (FSK2a wave) ======================*/
/*
 * 96-bit frame -> GPIO BSRR step pairs, FSK2a at RF/50 (same wave shape and
 * polarity as the Pyramid emulator). Worst case (all bit-0) = 96 * 6 * 2 =
 * 1152 <= ENCODED_DATA_MAX. Returns 0 on success, -1 on overflow.
 */
static int awid_encoded_to_wave(const uint8_t *encoded, uint8_t gpio_pin,
                                Encoded_Data_t *steps, size_t max_steps,
                                size_t *out_step_count)
{
    size_t idx = 0;
    uint32_t bsrr_set   = 1u << gpio_pin;
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);

    for(int bit = 0; bit < AWID_ENCODED_BITS; bit++) {
        uint8_t b = awid_get_bit(encoded, bit);

        uint16_t half_us, period_us;
        int repeat;

        if(b == 0) {
            half_us   = AWID_EMUL_HALF_ZERO_US;
            period_us = (uint16_t)(AWID_EMUL_PERIOD_ZERO_US - half_us);
            repeat    = AWID_EMUL_CYCLES_ZERO;
        } else {
            half_us   = AWID_EMUL_HALF_ONE_US;
            period_us = (uint16_t)(AWID_EMUL_PERIOD_ONE_US - half_us);
            repeat    = AWID_EMUL_CYCLES_ONE;
        }

        for(int i = 0; i < repeat; i++) {
#if AWID_OUTPUT_INVERT
            if(idx >= max_steps) goto overflow;
            steps[idx].bsrr = bsrr_reset; steps[idx].time_us = half_us;   idx++;
            if(idx >= max_steps) goto overflow;
            steps[idx].bsrr = bsrr_set;   steps[idx].time_us = period_us; idx++;
#else
            if(idx >= max_steps) goto overflow;
            steps[idx].bsrr = bsrr_set;   steps[idx].time_us = half_us;   idx++;
            if(idx >= max_steps) goto overflow;
            steps[idx].bsrr = bsrr_reset; steps[idx].time_us = period_us; idx++;
#endif
        }
    }

    if(out_step_count) *out_step_count = idx;
    return 0;

overflow:
    if(out_step_count) *out_step_count = idx;
    return -1;
}

static bool awid_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t work[AWID_DECODED_SIZE];
    uint8_t encoded[AWID_ENCODED_SIZE];
    size_t  step_count = 0;

    memcpy(work, lfrfid_tag_info.uid, AWID_DECODED_SIZE);
    awid_normalize(work, encoded);          /* normalized local copy */

    if(awid_encoded_to_wave(encoded, AWID_EMUL_GPIO_PIN,
                            lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                            &step_count) != 0) {
        lfrfid_encoded_data.length = 0;
        return false;
    }

    lfrfid_encoded_data.length = (uint16_t)step_count;
    return true;
}

static void awid_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * FSK2a, RF/50, 3 data blocks (96-bit frame). Config in block 0, frame in
 * blocks 1..3 (MSB-first), 4 blocks total.
 * protocol_awid_write_data (MAXBLOCK=3, blocks_to_write=4), including the
 * invalid-format coercion + normalization — here on a local working copy, so
 * the stored credential is not mutated.
 */
static void awid_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t          work[AWID_DECODED_SIZE];
    uint8_t          encoded[AWID_ENCODED_SIZE];

    memcpy(work, tag_data->uid, AWID_DECODED_SIZE);
    awid_normalize(work, encoded);

    /* The built-in write verification compares the read-back credential against
     * lfrfid_tag_info_back. Because an invalid format is coerced to 26 and
     * round-trips the payload before emitting the tag, the read-back credential
     * equals `work` (the normalized data), NOT the pre-coercion snapshot. Point
     * the verify reference at `work` so a correctly written tag verifies. This
     * touches only the write-verify reference, never the canonical
     * lfrfid_tag_info.uid; for a valid credential `work` is byte-identical to the
     * original, so the reference is unchanged. */
    if(lfrfid_tag_info_back)
        memcpy(lfrfid_tag_info_back->uid, work, AWID_DECODED_SIZE);

    if(write && write->type == LFRFIDProgramTypeT5577) {
        write->t5577.block_data[0] =
            (T5577_MOD_FSK2a | T5577_BITRATE_RF_50 | T5577_TRANS_BL_1_3);
        /* Blocks 1..3 = encoded frame bytes 0..11 (96 bits), MSB-first. */
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded, &write->t5577.block_data[1], 3);
        write->t5577.max_blocks = 4;
    }
}

static void awid_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_awid = {
    .name         = "AWID",
    .manufacturer = "AWID",
    .data_size    = AWID_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)awid_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)awid_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)awid_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)awid_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)awid_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)awid_write_begin,
        .send  = (lfrfidProtocolWriteSend)awid_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)awid_render_data,
};
