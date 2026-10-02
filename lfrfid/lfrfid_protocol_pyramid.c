/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Pyramid (Farpointe) protocol
 *
 * Farpointe Pyramid, 26-bit format only. FSK2a modulation, RF/50 bit rate,
 * 128-bit frame: leading preamble (0x00 0x01 0x01), 13 data bytes carrying a
 * Wiegand-parity payload with an odd column-parity bit inserted every 8 bits,
 * an 8-bit CRC (poly 0x31, reflected in/out), and a trailing preamble.
 *
 * The self-contained bit helpers use MSB-first bit order. The Wiegand parity
 * helper takes a bit offset; passing a byte pointer in its place would read
 * out of bounds.
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

#define PYRAMID_FORMAT_26   (26)

/* FSK2a emulation timing (RF/50) — same field-clock counts the proven H10301
 * emulator uses: bit 0 -> fc/8 (64 us carrier), bit 1 -> fc/10 (80 us carrier).
 * A 125 kHz field clock is 8 us; a data bit spans 50 field clocks (~400 us):
 *   fc/8  = 8 field clocks = 64 us -> 50/8  ~= 6 carrier cycles
 *   fc/10 = 10 field clocks = 80 us -> 50/10 = 5 carrier cycles                */
#define PYR_OUTPUT_INVERT     (0)
/* True nominal FSK2a half/period values. The "-2" that used to sit on the
 * PERIOD constants compensated for the old per-edge ISR path writing
 * TIM5->ARR = time_us with no inclusive-counter "-1" correction
 * (rfid_emul_handler(), lfrfid_hal.c, now removed) -- see lfrfid_dma_tx.c's
 * header comment for the full derivation. The new GPDMA2-driven transport
 * performs that correction itself, once, from the real measured TIM5
 * kernel clock -- so it must not be duplicated here. */
#define PYR_EMUL_HALF_ONE_US  (40)          /* fc/10 half period */
#define PYR_EMUL_HALF_ZERO_US (32)          /* fc/8  half period */
#define PYR_EMUL_PERIOD_ONE_US  (80)
#define PYR_EMUL_PERIOD_ZERO_US (64)
#define PYR_EMUL_CYCLES_ONE   (5)           /* fc/10 -> 5 cycles per bit */
#define PYR_EMUL_CYCLES_ZERO  (6)           /* fc/8  -> 6 cycles per bit */
#define PYR_EMUL_GPIO_PIN     (2)           /* RFID_OUT pin on GPIOA (as H10301) */

/***************************** V A R I A B L E S ******************************/

static uint8_t g_pyr_encoded[PYRAMID_ENCODED_SIZE];   /* 19-byte shift register */
static uint8_t g_pyr_decoded[PYRAMID_DECODED_SIZE];   /* 4-byte credential      */
static fsk_symbol_state_t g_pyr_sym_st;
static fsk_bit_state_t    g_pyr_bit_st;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *pyramid_get_data(void *proto);
static void     pyramid_decoder_begin(void *proto);
static bool     pyramid_decoder_execute(void *proto, uint16_t size);
static bool     pyramid_encoder_begin(void *proto);
static void     pyramid_encoder_send(void *proto);
static void     pyramid_write_begin(void *protocol, void *data);
static void     pyramid_write_send(void *proto);
static void     pyramid_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives
 * this protocol depends on (bit_lib_set_bit/get_bit/copy_bits). */

static uint8_t pyr_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8] >> (7 - (position % 8))) & 1u);
}

static void pyr_set_bit(uint8_t *data, size_t position, uint8_t bit)
{
    if(bit & 1u)
        data[position / 8] |= (uint8_t)(1u << (7 - (position % 8)));
    else
        data[position / 8] &= (uint8_t)~(1u << (7 - (position % 8)));
}

/* Read 8 MSB-first bits at a byte-aligned or arbitrary position. */
static uint8_t pyr_get_byte(const uint8_t *data, size_t position)
{
    uint8_t v = 0;
    for(int b = 0; b < 8; b++)
        v = (uint8_t)((v << 1) | pyr_get_bit(data, position + b));
    return v;
}

/* Read 16 MSB-first bits (big-endian) at an arbitrary position. */
static uint16_t pyr_get_16(const uint8_t *data, size_t position)
{
    uint16_t v = 0;
    for(int b = 0; b < 16; b++)
        v = (uint16_t)((v << 1) | pyr_get_bit(data, position + b));
    return v;
}

/* Write the low `length` bits (<=8) of `byte`, MSB-first, at `position`.
 * Mirrors bit_lib_set_bits(). */
static void pyr_set_bits(uint8_t *data, size_t position, uint8_t byte, uint8_t length)
{
    for(uint8_t i = 0; i < length; i++) {
        uint8_t shift = (uint8_t)((length - 1) - i);
        pyr_set_bit(data, position + i, (byte >> shift) & 1u);
    }
}

/* Copy `length` bits from source@src_pos to dst@dst_pos. Mirrors
 * bit_lib_copy_bits(). */
static void pyr_copy_bits(uint8_t *dst, size_t dst_pos, size_t length,
                          const uint8_t *src, size_t src_pos)
{
    for(size_t i = 0; i < length; i++)
        pyr_set_bit(dst, dst_pos + i, pyr_get_bit(src, src_pos + i));
}

/* Shift a whole bit into a big-endian byte buffer from the right (LSB end),
 * dropping the leftmost bit. Mirrors bit_lib_push_bit(). */
static void pyr_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*============================== C R C - 8 ==================================*/
/* poly=0x31, init=0x00, ref_in=true, ref_out=true, xor_out=0x00 */

static uint8_t pyr_reflect_byte(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

static uint8_t pyr_crc8(const uint8_t *data, size_t size)
{
    uint8_t crc = 0x00;
    for(size_t i = 0; i < size; i++) {
        crc ^= pyr_reflect_byte(data[i]);   /* ref_in */
        for(int j = 0; j < 8; j++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return pyr_reflect_byte(crc);           /* ref_out */
}

/*========================= D E C O D E ======================================*/
/*
 * Frame validation:
 *   - leading preamble  : bits 0..15 == 0x0001 AND bits 16..23 == 0x01
 *   - trailing preamble : bits 128..143 == 0x0001 AND bits 136..143 == 0x01
 *     (the bit-136 byte check is implied by the 16-bit test and retained)
 *   - CRC-8 of the 13 bytes at bits 16..119 == byte at bits 120..127
 *   - after removing every 8th (column-parity) bit, format length must be 26
 */
static bool pyramid_can_be_decoded(const uint8_t *data)
{
    /* Leading preamble */
    if(pyr_get_16(data, 0) != 0x0001)
        return false;
    if(pyr_get_byte(data, 16) != 0x01)
        return false;

    /* Trailing preamble checks */
    if(pyr_get_16(data, 128) != 0x0001)
        return false;
    if(pyr_get_byte(data, 136) != 0x01)
        return false;

    /* CRC-8 over the 13 data bytes (bits 16..119); checksum at bits 120..127 */
    uint8_t checksum = pyr_get_byte(data, 120);
    uint8_t checksum_data[13];
    for(int i = 0; i < 13; i++)
        checksum_data[i] = pyr_get_byte(data, 16 + (i * 8));
    if(checksum != pyr_crc8(checksum_data, 13))
        return false;

    /* Remove every 8th bit starting at bit 8 over 120 bits -> 105 data bits.
     * Compaction begins at bit 8 (byte 0 preamble preserved), preserving the
     * in-place bit_lib_remove_bit_every_nth(data, 8, 15*8, 8). */
    uint8_t temp[PYRAMID_ENCODED_SIZE];
    memcpy(temp, data, PYRAMID_ENCODED_SIZE);

    int dst_bit = 8;
    for(int i = 0; i < 120; i++) {
        if(((i + 1) % 8) == 0)
            continue;                       /* skip column-parity bit */
        pyr_set_bit(temp, dst_bit, pyr_get_bit(data, 8 + i));
        dst_bit++;
    }

    /* Leading zeros in the 105-bit field determine the format length. */
    int j;
    for(j = 0; j < 105; j++) {
        if(pyr_get_bit(temp, j))
            break;
    }
    if((uint8_t)(105 - j) != PYRAMID_FORMAT_26)
        return false;

    return true;
}

/*
 * Extract format / facility / card into the 4-byte decoded buffer.
 * Compaction here starts at bit 0 (dst_bit=0); the resulting field positions
 * (FC at bit 73, card at bit 81) use the compacted-buffer positions
 * (81, 89) offset by the 8-bit preamble, i.e. identical credential bits.
 */
static void pyramid_decode(const uint8_t *encoded, uint8_t *decoded)
{
    memset(decoded, 0, PYRAMID_DECODED_SIZE);

    uint8_t temp[PYRAMID_ENCODED_SIZE];
    memset(temp, 0, sizeof(temp));

    int dst_bit = 0;
    for(int i = 0; i < 120; i++) {
        if(((i + 1) % 8) == 0)
            continue;
        pyr_set_bit(temp, dst_bit, pyr_get_bit(encoded, 8 + i));
        dst_bit++;
    }
    /* temp now holds 105 compacted credential bits starting at bit 0 */

    decoded[0] = PYRAMID_FORMAT_26;

    uint8_t fc = 0;
    for(int b = 0; b < 8; b++)
        fc = (uint8_t)((fc << 1) | pyr_get_bit(temp, 73 + b));

    uint16_t cn = 0;
    for(int b = 0; b < 16; b++)
        cn = (uint16_t)((cn << 1) | pyr_get_bit(temp, 81 + b));

    decoded[1] = fc;
    decoded[2] = (uint8_t)(cn >> 8);
    decoded[3] = (uint8_t)(cn & 0xFF);
}

/*========================= D E C O D E R  I / F =============================*/

static void pyramid_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_pyr_encoded, 0, sizeof(g_pyr_encoded));
    memset(g_pyr_decoded, 0, sizeof(g_pyr_decoded));
    fsk_symbol_state_init(&g_pyr_sym_st);
    fsk_bit_state_init(&g_pyr_bit_st);
}

static bool pyramid_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint8_t symbol;
        if(fsk_symbol_feed(&g_pyr_sym_st, &evt[i], &symbol)) {
            uint8_t bit;
            if(fsk_bit_feed(&g_pyr_bit_st, symbol, &bit)) {
                pyr_push_bit(g_pyr_encoded, PYRAMID_ENCODED_SIZE, bit);

                if(pyramid_can_be_decoded(g_pyr_encoded)) {
                    pyramid_decode(g_pyr_encoded, g_pyr_decoded);
                    memcpy(lfrfid_tag_info.uid, g_pyr_decoded,
                           min(sizeof(lfrfid_tag_info.uid), (size_t)PYRAMID_DECODED_SIZE));
                    return true;
                }
            }
        }
    }
    return false;
}

/* Canonical credential source is lfrfid_tag_info.uid (populated by a live read
 * AND by file load / Add Manually / reload), matching EM4100 and H10301. The
 * decoder-private g_pyr_decoded is only a scratch target during a live decode;
 * get_data / render must NOT read it, or loaded credentials render stale. */
static uint8_t *pyramid_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void pyramid_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = pyramid_get_data(NULL);

    if(d[0] == PYRAMID_FORMAT_26) {
        sprintf(result,
                "Format: %u\n"
                "FC: %03u\n"
                "Card: %05hu",
                d[0],
                d[1],
                MAKEWORD(d[3], d[2]));
    } else {
        sprintf(result, "Format: %u\nData: Unknown", d[0]);
    }
}

/*========================= E N C O D E ======================================*/
/*
 * Builds a 128-bit Pyramid frame
 * from the 4-byte decoded credential (data[1]=FC, data[2..3]=card):
 *   - 24-bit Wiegand payload = FC(8) | card(16)
 *   - even/odd Wiegand parity wraps it (26 bits) placed after the format
 *     start bit at pre bit 79
 *   - odd column parity added every 8 bits (bit_lib_add_parity) -> preamble
 *     bytes 0x00 0x01 0x01 fall out naturally from parity-of-zeros
 *   - CRC-8 over the 13 payload bytes appended at bits 120..127
 */

/* Odd-parity test of a bit word. */
static uint8_t pyr_test_parity_odd(uint32_t bits)
{
    return (uint8_t)(!__builtin_parity(bits));
}

/* Compute parity by summing `length` bits from `bits`
 * starting at `position`, mod 2, XOR `type`. */
static uint8_t pyramid_get_parity(const uint8_t *bits, size_t position,
                                  uint8_t type, int length)
{
    int x = 0;
    for(; length > 0; --length)
        x += pyr_get_bit(bits, position + length - 1);
    x %= 2;
    return (uint8_t)(x ^ type);
}

/* Wiegand parity — even parity over the first
 * half, the payload, then odd parity over the second half.
 * NOTE: `position` here is a BIT offset within `source` (12).
 * Passing `source + length/2` would be a 12-BYTE pointer, out of bounds. */
static void pyramid_add_wiegand_parity(uint8_t *target, uint8_t target_position,
                                       const uint8_t *source, uint8_t length)
{
    pyr_set_bit(target, target_position,
                pyramid_get_parity(source, 0, 0 /* even */, length / 2));
    pyr_copy_bits(target, target_position + 1, length, source, 0);
    pyr_set_bit(target, (size_t)(target_position + length + 1),
                pyramid_get_parity(source, length / 2, 1 /* odd */, length / 2));
}

/* Add parity for the (source_length=102, chunk=8, odd) case
 * used by Pyramid: copy 7 bits then append an odd column-parity bit, per byte. */
static void pyramid_add_column_parity(const uint8_t *data, size_t position,
                                      uint8_t *dest, size_t dest_position,
                                      uint8_t source_length, uint8_t parity_length)
{
    uint32_t parity_word = 0;
    size_t j = 0;
    for(int word = 0; word < source_length; word += parity_length - 1) {
        for(int bit = 0; bit < parity_length - 1; bit++) {
            uint8_t b = pyr_get_bit(data, position + word + bit);
            parity_word = (parity_word << 1) | b;
            pyr_set_bit(dest, dest_position + j++, b);
        }
        /* (test_parity(pw, Odd) ^ parity) ^ 1, parity == Odd(1) */
        pyr_set_bit(dest, dest_position + j++,
                    (uint8_t)((pyr_test_parity_odd(parity_word) ^ 1) ^ 1));
        parity_word = 0;
    }
}

static void pyramid_encode(const uint8_t *data, uint8_t *encoded)
{
    memset(encoded, 0, PYRAMID_ENCODED_SIZE);

    uint8_t pre[16];
    memset(pre, 0, sizeof(pre));

    /* Format start bit */
    pyr_set_bit(pre, 79, 1);

    uint8_t wiegand[3];
    memset(wiegand, 0, sizeof(wiegand));

    /* FC (8 bits) then card number (16 bits) */
    pyr_copy_bits(wiegand, 0, 8, data, 8);
    pyr_copy_bits(wiegand, 8, 16, data, 16);

    /* Wiegand parity -> pre bits 80..105 */
    pyramid_add_wiegand_parity(pre, 80, wiegand, 24);

    /* Odd column parity every 8 bits -> encoded bits 8..127 */
    pyramid_add_column_parity(pre, 8, encoded, 8, 102, 8);

    /* CRC-8 over the 13 payload bytes (bits 16..119) -> bits 120..127 */
    uint8_t checksum_buffer[13];
    for(uint8_t i = 0; i < 13; i++)
        checksum_buffer[i] = pyr_get_byte(encoded, 16 + (i * 8));

    uint8_t crc = pyr_crc8(checksum_buffer, 13);
    pyr_set_bits(encoded, 120, crc, 8);
}

/*========================= E M U L A T E  (FSK2a wave) ======================*/
/*
 * Turn the 128-bit encoded frame into GPIO BSRR step pairs, FSK2a at RF/50.
 * Same wave shape as the H10301 emulator, extended from 96 to 128 bits.
 * Worst case (all bit-0) = 128 * 6 cycles * 2 steps = 1536 steps, which fits
 * the ENCODED_DATA_MAX buffer (see lfrfid_hal.h). Returns 0 on success,
 * -1 if the step buffer overflowed (frame truncated -> not emulated).
 */
static int pyramid_encoded_to_wave(const uint8_t *encoded, uint8_t gpio_pin,
                                   Encoded_Data_t *steps, size_t max_steps,
                                   size_t *out_step_count)
{
    size_t idx = 0;
    uint32_t bsrr_set   = 1u << gpio_pin;         /* HIGH */
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);  /* LOW  */

    for(int bit = 0; bit < PYRAMID_ENCODED_BITS; bit++) {
        uint8_t b = pyr_get_bit(encoded, bit);

        uint16_t half_us, period_us;
        int repeat;

        if(b == 0) {
            half_us   = PYR_EMUL_HALF_ZERO_US;
            period_us = (uint16_t)(PYR_EMUL_PERIOD_ZERO_US - half_us);
            repeat    = PYR_EMUL_CYCLES_ZERO;
        } else {
            half_us   = PYR_EMUL_HALF_ONE_US;
            period_us = (uint16_t)(PYR_EMUL_PERIOD_ONE_US - half_us);
            repeat    = PYR_EMUL_CYCLES_ONE;
        }

        for(int i = 0; i < repeat; i++) {
#if PYR_OUTPUT_INVERT
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

static bool pyramid_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t encoded[PYRAMID_ENCODED_SIZE];
    size_t  step_count = 0;

    pyramid_encode(lfrfid_tag_info.uid, encoded);

    if(pyramid_encoded_to_wave(encoded, PYR_EMUL_GPIO_PIN,
                               lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                               &step_count) != 0) {
        /* Overflow guard: refuse to emit a truncated (undecodable) frame. */
        lfrfid_encoded_data.length = 0;
        return false;
    }

    lfrfid_encoded_data.length = (uint16_t)step_count;
    return true;
}

static void pyramid_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * FSK2a, RF/50, 4 data blocks (128-bit frame). Config in block 0, the frame
 * in blocks 1..4 (MSB-first), five blocks written total.
 * protocol_pyramid_write_data (MAXBLOCK=4, blocks_to_write=5).
 */
static void pyramid_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t          encoded[PYRAMID_ENCODED_SIZE];

    pyramid_encode(tag_data->uid, encoded);

    if(write && write->type == LFRFIDProgramTypeT5577) {
        write->t5577.block_data[0] =
            (T5577_MOD_FSK2a | T5577_BITRATE_RF_50 | T5577_TRANS_BL_1_4);
        /* Blocks 1..4 = encoded frame bytes 0..15 (128 bits), MSB-first. */
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded, &write->t5577.block_data[1], 4);
        write->t5577.max_blocks = 5;
    }
}

static void pyramid_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_pyramid = {
    .name         = "Pyramid",
    .manufacturer = "Farpointe",
    .data_size    = PYRAMID_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)pyramid_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)pyramid_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)pyramid_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)pyramid_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)pyramid_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)pyramid_write_begin,
        .send  = (lfrfidProtocolWriteSend)pyramid_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)pyramid_render_data,
};
