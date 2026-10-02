/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Jablotron protocol
 *
 * ASK, bi-phase (DIPHASE) line coding, RF/64 (bit period 512 us). A 64-bit
 * frame carries a 16-bit 0xFFFF preamble, a 40-bit payload (5 bytes) and an
 * 8-bit checksum (sum of the 5 payload bytes ^ 0x3A). No per-bit parity.
 *
 * The "Card: %llX" render decodes the BCD-per-nibble decimal identifier and
 * prints it in hexadecimal from the stored credential, independent of
 * decoder scratch. The encoder supports live emulation.
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

/* RF/64 bi-phase timing. Long = 512 us
 * (one pulse -> bit 1); short = 256 us (two pulses -> bit 0); jitter +/-120. */
#define JAB_SHORT_US       (256)
#define JAB_LONG_US        (512)
#define JAB_JITTER_US      (120)
#define JAB_SHORT_LOW      (JAB_SHORT_US - JAB_JITTER_US)  /* 136 */
#define JAB_SHORT_HIGH     (JAB_SHORT_US + JAB_JITTER_US)  /* 376 */
#define JAB_LONG_LOW       (JAB_LONG_US - JAB_JITTER_US)   /* 392 */
#define JAB_LONG_HIGH      (JAB_LONG_US + JAB_JITTER_US)   /* 632 */

#define JAB_EMUL_GPIO_PIN  (2)          /* RFID_PULL (PA2) load-mod, as EM4100 */

/* T5577 DIPHASE modulation — module-local (M1 t5577.h has no DIPHASE constant).
 * Numeric value is the ATA5577 diphase modulation value:
 * MANCHESTER(0x8000) | BIPHASE(0x10000) = 0x18000 (mod field 5 bits = 24<<12).
 * Distinct from M1's T5577_MOD_MANCHESTER (0x8000); no shared-header change. */
#define JAB_T5577_MOD_DIPHASE  (0x00018000u)

/***************************** V A R I A B L E S ******************************/

static uint8_t g_jab_encoded[JABLOTRON_ENCODED_SIZE];   /* 80-bit shift reg */
static uint8_t g_jab_data[JABLOTRON_DECODED_SIZE];       /* 5-byte credential */
static bool    g_jab_last_short;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *jab_get_data(void *proto);
static void     jab_decoder_begin(void *proto);
static bool     jab_decoder_execute(void *proto, uint16_t size);
static bool     jab_encoder_begin(void *proto);
static void     jab_encoder_send(void *proto);
static void     jab_write_begin(void *protocol, void *data);
static void     jab_write_send(void *proto);
static void     jab_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives. */

static uint8_t jab_get_bit(const uint8_t *data, size_t pos)
{
    return (uint8_t)((data[pos / 8] >> (7 - (pos % 8))) & 1u);
}

static void jab_set_bit(uint8_t *data, size_t pos, uint8_t bit)
{
    if(bit & 1u)
        data[pos / 8] |= (uint8_t)(1u << (7 - (pos % 8)));
    else
        data[pos / 8] &= (uint8_t)~(1u << (7 - (pos % 8)));
}

/* Read up to 32 MSB-first bits. */
static uint32_t jab_get_bits(const uint8_t *data, size_t pos, uint8_t len)
{
    uint32_t v = 0;
    for(uint8_t i = 0; i < len; i++)
        v = (v << 1) | jab_get_bit(data, pos + i);
    return v;
}

/* Write the low `len` bits (<=32) of `val`, MSB-first, at `pos`. */
static void jab_set_bits(uint8_t *data, size_t pos, uint32_t val, uint8_t len)
{
    for(uint8_t i = 0; i < len; i++)
        jab_set_bit(data, pos + i, (uint8_t)((val >> (len - 1 - i)) & 1u));
}

static void jab_copy_bits(uint8_t *dst, size_t dpos, size_t len,
                          const uint8_t *src, size_t spos)
{
    for(size_t i = 0; i < len; i++)
        jab_set_bit(dst, dpos + i, jab_get_bit(src, spos + i));
}

static void jab_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*==================== u64 -> H E X ==========================================*/
/* Newlib-nano may lack %llX; format the card id (up to 64-bit) safely, matching
 * printf("%llX"): uppercase, no leading zeros, "0" for zero. */
static void jab_u64_to_hex(uint64_t v, char *out)
{
    static const char HEX[] = "0123456789ABCDEF";
    char tmp[20];
    int n = 0;
    if(v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while(v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = HEX[v & 0xF]; v >>= 4; }
    int j = 0;
    while(n > 0) out[j++] = tmp[--n];
    out[j] = '\0';
}

/*========================= C H E C K S U M / I D ============================*/
/* Sum of the five payload bytes (frame bits 16..55), XOR 0x3A. */
static uint8_t jab_checksum(const uint8_t *frame)
{
    uint8_t chk = 0;
    for(uint8_t i = 16; i < 56; i += 8)
        chk += (uint8_t)jab_get_bits(frame, i, 8);
    chk ^= 0x3A;
    return chk;
}

/* BCD-per-nibble decimal identifier. Note the
 * high nibble is multiplied by 10 unconditionally, so non-BCD nibbles are
 * accepted verbatim — replicated exactly. */
static uint64_t jab_card_id(const uint8_t *data)
{
    uint64_t id = 0;
    for(int i = 0; i < 5; i++) {
        id *= 100;
        id += (uint64_t)(((data[i] & 0xF0) >> 4) * 10 + (data[i] & 0x0F));
    }
    return id;
}

/*========================= D E C O D E ======================================*/
static bool jab_can_be_decoded(const uint8_t *frame)
{
    /* 16-bit preamble at bit 0 AND at bit 64 (next frame's preamble). */
    if(jab_get_bits(frame, 0, 16) != 0xFFFFu) return false;
    if(jab_get_bits(frame, 64, 16) != 0xFFFFu) return false;
    /* checksum byte at bits 56..63 */
    if((uint8_t)jab_get_bits(frame, 56, 8) != jab_checksum(frame)) return false;
    return true;
}

/* Canonical 5-byte payload = frame bits 16..55. */
static void jab_decode(const uint8_t *frame, uint8_t *data)
{
    jab_copy_bits(data, 0, 40, frame, 16);
}

/*========================= D E C O D E R  I / F =============================*/

static void jab_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_jab_encoded, 0, sizeof(g_jab_encoded));
    memset(g_jab_data, 0, sizeof(g_jab_data));
    g_jab_last_short = false;
}

/* Bi-phase decoder. Edge level is
 * ignored; only the interval duration matters. Two consecutive shorts push a
 * 0; a lone long pushes a 1; a long while awaiting the 2nd short, or any
 * out-of-window interval, resets. */
static bool jab_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint32_t dur = evt[i].t_us;
        bool pushed = false;
        uint8_t bit = 0;

        if(dur >= JAB_SHORT_LOW && dur <= JAB_SHORT_HIGH) {
            if(g_jab_last_short == false) {
                g_jab_last_short = true;
            } else {
                pushed = true; bit = 0;
                g_jab_last_short = false;
            }
        } else if(dur >= JAB_LONG_LOW && dur <= JAB_LONG_HIGH) {
            if(g_jab_last_short == false) {
                pushed = true; bit = 1;
            } else {
                g_jab_last_short = false;   /* reset */
            }
        } else {
            g_jab_last_short = false;       /* reset */
        }

        if(pushed) {
            jab_push_bit(g_jab_encoded, JABLOTRON_ENCODED_SIZE, bit);
            if(jab_can_be_decoded(g_jab_encoded)) {
                jab_decode(g_jab_encoded, g_jab_data);
                memcpy(lfrfid_tag_info.uid, g_jab_data,
                       min(sizeof(lfrfid_tag_info.uid), (size_t)JABLOTRON_DECODED_SIZE));
                lfrfid_tag_info.bitrate = 64;
                return true;
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (live read AND file/Add-Manually). */
static uint8_t *jab_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void jab_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = jab_get_data(NULL);
    char hex[20];
    jab_u64_to_hex(jab_card_id(d), hex);
    sprintf(result, "Card: %s", hex);
}

/*========================= E N C O D E ======================================*/
/* Build the 64-bit frame from the canonical five-byte payload
 * encoder_start): 16-bit 0xFFFF preamble, 40-bit payload, 8-bit checksum. */
static void jab_encode(const uint8_t *data, uint8_t *frame)
{
    memset(frame, 0, JABLOTRON_ENCODED_SIZE);
    jab_set_bits(frame, 0, 0xFF, 8);            /* preamble 0xFFFF */
    jab_set_bits(frame, 8, 0xFF, 8);
    jab_copy_bits(frame, 16, 40, data, 0);      /* 5-byte payload  */
    jab_set_bits(frame, 56, jab_checksum(frame), 8);
}

/*========================= E M U L A T E  (ASK bi-phase) ====================*/
/* Bi-phase wave: the output level toggles on
 * every emitted segment; bit 1 -> one long (512 us) segment, bit 0 -> two short
 * (256 us) segments. Worst case (all 0) = 128 entries for 64 bits. */
static uint16_t jab_build_biphase_wave(const uint8_t *frame, uint16_t nbits,
                                       Encoded_Data_t *steps, uint16_t max_steps,
                                       uint8_t gpio_pin)
{
    uint32_t bsrr_set   = 1u << gpio_pin;
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);
    uint16_t idx = 0;
    bool level = false;

    for(uint16_t b = 0; b < nbits; b++) {
        if(jab_get_bit(frame, b)) {
            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = JAB_LONG_US; idx++;
        } else {
            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = JAB_SHORT_US; idx++;

            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = JAB_SHORT_US; idx++;
        }
    }
    return idx;
}

static bool jab_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t frame[JABLOTRON_ENCODED_SIZE];
    jab_encode(lfrfid_tag_info.uid, frame);

    uint16_t n = jab_build_biphase_wave(frame, JABLOTRON_ENCODED_BITS,
                                        lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                        JAB_EMUL_GPIO_PIN);
    if(n == 0) { lfrfid_encoded_data.length = 0; return false; }
    lfrfid_encoded_data.length = n;
    return true;
}

static void jab_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * DIPHASE, RF/64, two data blocks (BL_1_2), three total.
 * protocol_jablotron_write_data. The 64-bit frame occupies the first 8 bytes;
 * block[1] = frame bits 0..31, block[2] = bits 32..63.
 */
static void jab_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t frame[JABLOTRON_ENCODED_SIZE];

    jab_encode(tag_data->uid, frame);

    if(!write || write->type != LFRFIDProgramTypeT5577)
        return;

    write->t5577.block_data[0] =
        (JAB_T5577_MOD_DIPHASE | T5577_BITRATE_RF_64 | T5577_TRANS_BL_1_2);
    bytes_to_u32_array(BIT_ORDER_MSB_FIRST, frame, &write->t5577.block_data[1], 2);
    write->t5577.max_blocks = 3;
}

static void jab_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_jablotron = {
    .name         = "Jablotron",
    .manufacturer = "Jablotron",
    .data_size    = JABLOTRON_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)jab_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)jab_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)jab_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)jab_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)jab_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)jab_write_begin,
        .send  = (lfrfidProtocolWriteSend)jab_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)jab_render_data,
};
