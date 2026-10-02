/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Securakey "Radio Key" protocol
 *
 * ASK + Manchester, RF/40, three auto-detected formats sharing one protocol:
 *   RKKT 26-bit, RKKT 32-bit, RKKTH plaintext. A 96-bit (RKKT) or 64-bit
 *   (RKKTH) frame carries a 10+9-bit preamble/length field, 9-bit groups with a
 *   trailing spacer 0, Wiegand even/odd parity, and two opaque checksum bytes.
 *
 * The checksum bytes are retained verbatim; this implementation neither
 * computes nor validates them. Decoding distinguishes all three formats.
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

/* RF/40 Manchester timing. Long = 1e6 / (125000/40) = 320 us; short = 160 us;
 * 40% jitter tolerance. */
#define SK_LONG_US        (320)
#define SK_SHORT_US       (160)
#define SK_JITTER_US      (64)          /* 40% of short */
#define SK_SHORT_LOW      (SK_SHORT_US - SK_JITTER_US)  /* 96  */
#define SK_SHORT_HIGH     (SK_SHORT_US + SK_JITTER_US)  /* 224 */
#define SK_LONG_LOW       (SK_LONG_US - SK_JITTER_US)   /* 256 */
#define SK_LONG_HIGH      (SK_LONG_US + SK_JITTER_US)   /* 384 */

/* Manchester emulation: half-bit = RF/40/2 = 20 field clocks = 160 us. */
#define SK_EMUL_HALF_US   (160)
#define SK_EMUL_GPIO_PIN  (2)           /* RFID_PULL (PA2) load-mod, as EM4100 */

/* 19-bit preamble + format-length patterns. */
#define SK_PREAMBLE_PLAINTEXT  (0x3FE00u >> 0) /* 0b0111111111000000000 */
#define SK_PREAMBLE_26         (0b0111111111001011010u)
#define SK_PREAMBLE_32         (0b0111111111001100000u)
#define SK_PREAMBLE_0          (0b0111111111000000000u)

/* Manchester decoder. */
typedef enum {
    SK_MANCH_ShortLow  = 0,
    SK_MANCH_ShortHigh = 2,
    SK_MANCH_LongLow   = 4,
    SK_MANCH_LongHigh  = 6,
    SK_MANCH_Reset     = 8,
} sk_manch_event_t;

typedef enum {
    SK_MANCH_Start1 = 0,
    SK_MANCH_Mid1   = 1,
    SK_MANCH_Mid0   = 2,
    SK_MANCH_Start0 = 3,
} sk_manch_state_t;

/* Parity kinds for sk_test_parity (subset of bit_lib). */
#define SK_PARITY_ODD     (1)
#define SK_PARITY_ALWAYS0 (2)

/***************************** V A R I A B L E S ******************************/

static uint8_t g_sk_rkkt[SECURAKEY_RKKT_ENCODED_SIZE];   /* 12-byte shift reg */
static uint8_t g_sk_data[SECURAKEY_DECODED_SIZE];        /* 6-byte credential */
static sk_manch_state_t g_sk_manch;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *sk_get_data(void *proto);
static void     sk_decoder_begin(void *proto);
static bool     sk_decoder_execute(void *proto, uint16_t size);
static bool     sk_encoder_begin(void *proto);
static void     sk_encoder_send(void *proto);
static void     sk_write_begin(void *protocol, void *data);
static void     sk_write_send(void *proto);
static void     sk_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives. */

static uint8_t sk_get_bit(const uint8_t *data, size_t pos)
{
    return (uint8_t)((data[pos / 8] >> (7 - (pos % 8))) & 1u);
}

static void sk_set_bit(uint8_t *data, size_t pos, uint8_t bit)
{
    if(bit & 1u)
        data[pos / 8] |= (uint8_t)(1u << (7 - (pos % 8)));
    else
        data[pos / 8] &= (uint8_t)~(1u << (7 - (pos % 8)));
}

/* Read up to 32 MSB-first bits. */
static uint32_t sk_get_bits(const uint8_t *data, size_t pos, uint8_t len)
{
    uint32_t v = 0;
    for(uint8_t i = 0; i < len; i++)
        v = (v << 1) | sk_get_bit(data, pos + i);
    return v;
}

/* Read up to 64 MSB-first bits (for the 48-bit plaintext card). */
static uint64_t sk_get_bits_64(const uint8_t *data, size_t pos, uint8_t len)
{
    uint64_t v = 0;
    for(uint8_t i = 0; i < len; i++)
        v = (v << 1) | (uint64_t)sk_get_bit(data, pos + i);
    return v;
}

/* Write the low `len` bits (<=32) of `val`, MSB-first, at `pos`. */
static void sk_set_bits(uint8_t *data, size_t pos, uint32_t val, uint8_t len)
{
    for(uint8_t i = 0; i < len; i++)
        sk_set_bit(data, pos + i, (uint8_t)((val >> (len - 1 - i)) & 1u));
}

static void sk_copy_bits(uint8_t *dst, size_t dpos, size_t len,
                         const uint8_t *src, size_t spos)
{
    for(size_t i = 0; i < len; i++)
        sk_set_bit(dst, dpos + i, sk_get_bit(src, spos + i));
}

static void sk_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/* Faithful port of bit_lib_test_parity for the ODD and ALWAYS0 cases used here.
 * Returns true when every `plen`-bit block satisfies the rule (ALWAYS0: the last
 * bit of each block is 0; ODD: bit_lib_test_parity_32 == !__builtin_parity). */
static bool sk_test_parity(const uint8_t *bits, size_t pos, uint8_t len,
                           int parity, uint8_t plen)
{
    bool result = true;
    size_t blocks = len / plen;
    for(size_t i = 0; i < blocks; i++) {
        if(parity == SK_PARITY_ALWAYS0) {
            if(sk_get_bit(bits, pos + i * plen + plen - 1))
                result = false;
        } else { /* ODD */
            uint32_t blk = sk_get_bits(bits, pos + i * plen, plen);
            if(__builtin_parity(blk))   /* !test_parity_32(blk, Odd) */
                result = false;
        }
        if(!result) break;
    }
    return result;
}

/*==================== M A N C H E S T E R   S M ============================*/
/* Advance the Manchester decoder state. */
static bool sk_manch_advance(sk_manch_state_t state, sk_manch_event_t event,
                             sk_manch_state_t *next_state, bool *data)
{
    static const uint8_t transitions[] = {0b00000001, 0b10010001, 0b10011011, 0b11111011};
    bool result = false;
    sk_manch_state_t new_state;

    if(event == SK_MANCH_Reset) {
        new_state = SK_MANCH_Mid1;
    } else {
        new_state = (sk_manch_state_t)((transitions[state] >> event) & 0x3);
        if(new_state == state) {
            new_state = SK_MANCH_Mid1;
        } else if(new_state == SK_MANCH_Mid0) {
            if(data) *data = false;
            result = true;
        } else if(new_state == SK_MANCH_Mid1) {
            if(data) *data = true;
            result = true;
        }
    }
    *next_state = new_state;
    return result;
}

/*==================== u64 -> D E C I M A L ==================================*/
/* Newlib-nano may lack %llu; format the 48-bit plaintext card safely. */
static void sk_u64_to_dec(uint64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    if(v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while(v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while(n > 0) out[j++] = tmp[--n];
    out[j] = '\0';
}

/*========================= D E C O D E ======================================*/
static bool sk_can_be_decoded(const uint8_t *rkkt)
{
    uint32_t pre = sk_get_bits(rkkt, 0, 19);
    if(pre == SK_PREAMBLE_0) {
        return sk_test_parity(rkkt, 2, 54, SK_PARITY_ALWAYS0, 9);
    } else if(pre == SK_PREAMBLE_26) {
        return sk_test_parity(rkkt, 2, 90, SK_PARITY_ALWAYS0, 9);
    } else if(pre == SK_PREAMBLE_32) {
        return sk_test_parity(rkkt, 2, 90, SK_PARITY_ALWAYS0, 9);
    }
    return false;
}

/* Format-aware extraction into the six-byte canonical layout. */
static void sk_decode(const uint8_t *rkkt, uint8_t *data)
{
    memset(data, 0, SECURAKEY_DECODED_SIZE);

    if(sk_get_bits(rkkt, 13, 6) == 0) {
        /* RKKTH plaintext: 32-bit card in bytes 2..5 */
        sk_copy_bits(data, 16, 8, rkkt, 29);
        sk_copy_bits(data, 24, 8, rkkt, 38);
        sk_copy_bits(data, 32, 8, rkkt, 47);
        sk_copy_bits(data, 40, 8, rkkt, 56);
        return;
    }

    if(sk_get_bits(rkkt, 13, 6) == 26) {
        sk_copy_bits(data, 8, 1, rkkt, 36);        /* facility (8-bit) */
        sk_copy_bits(data, 9, 7, rkkt, 38);
    } else if(sk_get_bits(rkkt, 13, 6) == 32) {
        sk_copy_bits(data, 2, 7, rkkt, 30);        /* facility (14-bit, 2-bit pad) */
        sk_copy_bits(data, 9, 7, rkkt, 38);
    }
    /* card number (16-bit) */
    sk_copy_bits(data, 16, 1, rkkt, 45);
    sk_copy_bits(data, 17, 8, rkkt, 47);
    sk_copy_bits(data, 25, 7, rkkt, 56);
    /* checksum bytes (opaque, retained) */
    sk_copy_bits(data, 32, 8, rkkt, 65);           /* CS1 */
    sk_copy_bits(data, 40, 8, rkkt, 74);           /* CS2 */
}

/*========================= D E C O D E R  I / F =============================*/

static void sk_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_sk_rkkt, 0, sizeof(g_sk_rkkt));
    memset(g_sk_data, 0, sizeof(g_sk_data));
    sk_manch_advance(g_sk_manch, SK_MANCH_Reset, &g_sk_manch, NULL);
}

static bool sk_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        bool     level    = (evt[i].edge != 0);
        uint32_t duration = evt[i].t_us;
        sk_manch_event_t event = SK_MANCH_Reset;

        if(duration > SK_SHORT_LOW && duration < SK_SHORT_HIGH) {
            event = level ? SK_MANCH_ShortLow : SK_MANCH_ShortHigh;
        } else if(duration > SK_LONG_LOW && duration < SK_LONG_HIGH) {
            event = level ? SK_MANCH_LongLow : SK_MANCH_LongHigh;
        }

        if(event != SK_MANCH_Reset) {
            bool data;
            if(sk_manch_advance(g_sk_manch, event, &g_sk_manch, &data)) {
                sk_push_bit(g_sk_rkkt, SECURAKEY_RKKT_ENCODED_SIZE, data ? 1 : 0);
                if(sk_can_be_decoded(g_sk_rkkt)) {
                    sk_decode(g_sk_rkkt, g_sk_data);
                    memcpy(lfrfid_tag_info.uid, g_sk_data,
                           min(sizeof(lfrfid_tag_info.uid), (size_t)SECURAKEY_DECODED_SIZE));
                    return true;
                }
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (live read AND file/Add-Manually). */
static uint8_t *sk_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void sk_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = sk_get_data(NULL);

    if(sk_get_bits(d, 0, 16) == 0) {
        char dec[24];
        sk_u64_to_dec(sk_get_bits_64(d, 0, 48), dec);
        sprintf(result, "RKKTH Plaintext\nCard number: %s", dec);
    } else {
        unsigned fmt = (sk_get_bits(d, 0, 8) == 0) ? 26 : 32;
        sprintf(result,
                "RKKT %u-bit format\n"
                "Facility code: %u\n"
                "Card number: %u",
                fmt,
                (unsigned)sk_get_bits(d, 0, 16),
                (unsigned)sk_get_bits(d, 16, 16));
    }
}

/*========================= E N C O D E ======================================*/
/* Build the RKKT (96-bit) or RKKTH (64-bit) frame from the canonical data.
 * Builds the Securakey frame. Returns true if
 * the RKKTH (plaintext) frame was produced (into rkkth), false for RKKT. */
static bool sk_encode(const uint8_t *data, uint8_t *rkkt, uint8_t *rkkth)
{
    memset(rkkt, 0, SECURAKEY_RKKT_ENCODED_SIZE);
    memset(rkkth, 0, SECURAKEY_RKKTH_ENCODED_SIZE);

    if(sk_get_bits(data, 0, 16) == 0) {
        /* RKKTH plaintext */
        sk_set_bits(rkkth, 0, 0x7F, 8);            /* 01111111 */
        sk_set_bits(rkkth, 8, 0x6, 3);             /* 110      */
        sk_copy_bits(rkkth, 29, 8, data, 16);
        sk_copy_bits(rkkth, 38, 8, data, 24);
        sk_copy_bits(rkkth, 47, 8, data, 32);
        sk_copy_bits(rkkth, 56, 8, data, 40);
        return true;
    }

    /* RKKT 26/32-bit */
    sk_set_bits(rkkt, 0, 0x7F, 8);                 /* 01111111 */
    sk_set_bits(rkkt, 8, 0x19, 5);                 /* 11001    */
    if(sk_get_bits(data, 0, 8) == 0) {
        sk_set_bits(rkkt, 13, 26, 6);
        if(!sk_test_parity(data, 8, 12, SK_PARITY_ODD, 12))
            sk_set_bit(rkkt, 35, 1);
        if(sk_test_parity(data, 20, 12, SK_PARITY_ODD, 12))
            sk_set_bit(rkkt, 63, 1);
        sk_copy_bits(rkkt, 36, 1, data, 8);
        sk_copy_bits(rkkt, 38, 7, data, 9);
    } else {
        sk_set_bits(rkkt, 13, 32, 6);
        if(!sk_test_parity(data, 2, 15, SK_PARITY_ODD, 15))
            sk_set_bit(rkkt, 29, 1);
        if(sk_test_parity(data, 17, 15, SK_PARITY_ODD, 15))
            sk_set_bit(rkkt, 63, 1);
        sk_copy_bits(rkkt, 30, 7, data, 2);
        sk_copy_bits(rkkt, 38, 7, data, 9);
    }
    sk_copy_bits(rkkt, 45, 1, data, 16);
    sk_copy_bits(rkkt, 47, 8, data, 17);
    sk_copy_bits(rkkt, 56, 7, data, 25);
    sk_copy_bits(rkkt, 65, 8, data, 32);           /* CS1 */
    sk_copy_bits(rkkt, 74, 8, data, 40);           /* CS2 */
    return false;
}

/*========================= E M U L A T E  (ASK Manchester) ==================*/
/* frame -> Manchester half-bit BSRR steps: bit -> [bit, !bit], each half_us.
 * Worst case RKKT 96 bits * 2 = 192 steps <= ENCODED_DATA_MAX. */
static uint16_t sk_build_manchester_wave(const uint8_t *frame, uint16_t nbits,
                                         Encoded_Data_t *steps, uint16_t max_steps,
                                         uint16_t half_us, uint8_t gpio_pin)
{
    uint32_t bsrr_set   = 1u << gpio_pin;
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);
    uint16_t idx = 0;

    for(uint16_t b = 0; b < nbits; b++) {
        uint8_t v = sk_get_bit(frame, b);
        uint8_t first  = v ? 1 : 0;
        uint8_t second = v ? 0 : 1;

        if(idx >= max_steps) break;
        steps[idx].bsrr = first ? bsrr_set : bsrr_reset;
        steps[idx].time_us = half_us; idx++;

        if(idx >= max_steps) break;
        steps[idx].bsrr = second ? bsrr_set : bsrr_reset;
        steps[idx].time_us = half_us; idx++;
    }
    return idx;
}

static bool sk_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t rkkt[SECURAKEY_RKKT_ENCODED_SIZE];
    uint8_t rkkth[SECURAKEY_RKKTH_ENCODED_SIZE];
    bool    is_rkkth = sk_encode(lfrfid_tag_info.uid, rkkt, rkkth);

    uint16_t n = is_rkkth
        ? sk_build_manchester_wave(rkkth, SECURAKEY_RKKTH_ENCODED_BITS,
                                   lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                   SK_EMUL_HALF_US, SK_EMUL_GPIO_PIN)
        : sk_build_manchester_wave(rkkt, SECURAKEY_RKKT_ENCODED_BITS,
                                   lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                   SK_EMUL_HALF_US, SK_EMUL_GPIO_PIN);

    if(n == 0) { lfrfid_encoded_data.length = 0; return false; }
    lfrfid_encoded_data.length = n;
    return true;
}

static void sk_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * MANCHESTER, RF/40. RKKT: 3 data blocks (BL_1_3), 4 total. RKKTH: 2 data
 * blocks (BL_1_2), three total.
 */
static void sk_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t rkkt[SECURAKEY_RKKT_ENCODED_SIZE];
    uint8_t rkkth[SECURAKEY_RKKTH_ENCODED_SIZE];
    bool    is_rkkth = sk_encode(tag_data->uid, rkkt, rkkth);

    if(!write || write->type != LFRFIDProgramTypeT5577)
        return;

    if(is_rkkth) {
        write->t5577.block_data[0] =
            (T5577_MOD_MANCHESTER | T5577_BITRATE_RF_40 | T5577_TRANS_BL_1_2);
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, rkkth, &write->t5577.block_data[1], 2);
        write->t5577.max_blocks = 3;
    } else {
        write->t5577.block_data[0] =
            (T5577_MOD_MANCHESTER | T5577_BITRATE_RF_40 | T5577_TRANS_BL_1_3);
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, rkkt, &write->t5577.block_data[1], 3);
        write->t5577.max_blocks = 4;
    }
}

static void sk_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_radiokey = {
    .name         = "Radio Key",
    .manufacturer = "Securakey",
    .data_size    = SECURAKEY_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)sk_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)sk_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)sk_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)sk_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)sk_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)sk_write_begin,
        .send  = (lfrfidProtocolWriteSend)sk_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)sk_render_data,
};
