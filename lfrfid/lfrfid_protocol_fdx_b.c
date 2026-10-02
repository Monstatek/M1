/* See COPYING.txt for license details. */

/*
 * LF RFID — FDX-B / ISO 11784-11785 animal identification (Pet Tag)
 *
 * ASK, bi-phase Manchester, 134.2 kHz, RF/32. A 128-bit frame carries an 11-bit
 * header (10000000000), then 13 nine-bit groups each ending in an Always-1
 * control bit: 8 bytes of payload (national + country + block/status), a 16-bit
 * CRC-16 (poly 0x1021, init 0x0000), and 24 bits of extra data. De-framing
 * (strip control bits + header) yields the 11-byte canonical payload (CRC
 * removed). National/country codes are stored bit-reversed.
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

/* RF/32 @ 134.2 kHz bi-phase timing. Two
 * shorts (128 us) -> bit 0, one long (256 us) -> bit 1; jitter +/-60. Longest
 * legit interval = 316 us < the shared 1000 us RX filter. */
#define FDXB_SHORT_US    (128)
#define FDXB_LONG_US     (256)
#define FDXB_JITTER_US   (60)
#define FDXB_SHORT_LOW   (FDXB_SHORT_US - FDXB_JITTER_US)  /* 68  */
#define FDXB_SHORT_HIGH  (FDXB_SHORT_US + FDXB_JITTER_US)  /* 188 */
#define FDXB_LONG_LOW    (FDXB_LONG_US - FDXB_JITTER_US)   /* 196 */
#define FDXB_LONG_HIGH   (FDXB_LONG_US + FDXB_JITTER_US)   /* 316 */

#define FDXB_EMUL_GPIO_PIN  (2)          /* RFID_PULL (PA2) load-mod, as EM4100 */

/* T5577 DIPHASE modulation — module-local (0x00018000 = MANCHESTER|BIPHASE). */
#define FDXB_T5577_MOD_DIPHASE  (0x00018000u)

/***************************** V A R I A B L E S ******************************/

static uint8_t g_fdxb_frame[FDXB_ENCODED_SIZE];   /* 144-bit shift register */
static uint8_t g_fdxb_data[FDXB_DECODED_SIZE];     /* 11-byte credential */
static bool    g_fdxb_last_short;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *fdxb_get_data(void *proto);
static void     fdxb_decoder_begin(void *proto);
static bool     fdxb_decoder_execute(void *proto, uint16_t size);
static bool     fdxb_encoder_begin(void *proto);
static void     fdxb_encoder_send(void *proto);
static void     fdxb_write_begin(void *protocol, void *data);
static void     fdxb_write_send(void *proto);
static void     fdxb_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives. */

static uint8_t fdxb_get_bit(const uint8_t *data, size_t pos)
{
    return (uint8_t)((data[pos / 8] >> (7 - (pos % 8))) & 1u);
}

static void fdxb_set_bit(uint8_t *data, size_t pos, uint8_t bit)
{
    if(bit & 1u)
        data[pos / 8] |= (uint8_t)(1u << (7 - (pos % 8)));
    else
        data[pos / 8] &= (uint8_t)~(1u << (7 - (pos % 8)));
}

static uint32_t fdxb_get_bits(const uint8_t *data, size_t pos, uint8_t len)
{
    uint32_t v = 0;
    for(uint8_t i = 0; i < len; i++)
        v = (v << 1) | fdxb_get_bit(data, pos + i);
    return v;
}

static void fdxb_copy_bits(uint8_t *dst, size_t dpos, size_t len,
                           const uint8_t *src, size_t spos)
{
    for(size_t i = 0; i < len; i++)
        fdxb_set_bit(dst, dpos + i, fdxb_get_bit(src, spos + i));
}

static void fdxb_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/* Reverse the bit order within [pos, pos+len) (bit_lib_reverse_bits). */
static void fdxb_reverse_bits(uint8_t *data, size_t pos, size_t len)
{
    size_t i = pos, j = pos + len - 1;
    while(i < j) {
        uint8_t bi = fdxb_get_bit(data, i);
        uint8_t bj = fdxb_get_bit(data, j);
        fdxb_set_bit(data, i, bj);
        fdxb_set_bit(data, j, bi);
        i++; j--;
    }
}

/* Control bits: the last bit of each 9-bit group over [pos,pos+len) must be 1
 * (bit_lib_test_parity Always1, plen 9). */
static bool fdxb_test_always1_every9(const uint8_t *data, size_t pos, size_t len)
{
    for(size_t g = 0; g + 9 <= len; g += 9)
        if(fdxb_get_bit(data, pos + g + 8) != 1)
            return false;
    return true;
}

/* In-place compaction removing every nth bit over [pos,pos+len)
 * (bit_lib_remove_bit_every_nth). Safe in place: write index <= read index. */
static void fdxb_remove_every_nth(uint8_t *data, size_t pos, size_t len, uint8_t n)
{
    size_t rc = 0;
    for(size_t i = 0; i < len; i++) {
        if(((i + 1) % n) != 0) {
            fdxb_set_bit(data, pos + rc, fdxb_get_bit(data, pos + i));
            rc++;
        }
    }
}

/* CRC-16 MSB-first (bit_lib_crc16 with ref_in=ref_out=false, xor_out=0). */
static uint16_t fdxb_crc16(const uint8_t *data, size_t len, uint16_t poly, uint16_t init)
{
    uint16_t crc = init;
    for(size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for(size_t j = 0; j < 8; j++) {
            uint8_t c15 = (uint8_t)((crc >> 15) & 1u);
            uint8_t bit = (uint8_t)((byte >> (7 - j)) & 1u);
            crc = (uint16_t)(crc << 1);
            if(c15 ^ bit) crc ^= poly;
        }
    }
    return crc;
}

/*==================== u64 -> zero-padded decimal ============================*/
/* Newlib-nano may lack %llu; format the 38-bit national code as 12 digits. */
static void fdxb_u64_to_dec12(uint64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    if(v == 0) { tmp[n++] = '0'; }
    while(v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    for(int pad = n; pad < 12; pad++) out[j++] = '0';   /* zero-pad to 12 */
    while(n > 0) out[j++] = tmp[--n];
    out[j] = '\0';
}

/*==================== F I E L D   E X T R A C T I O N =======================*/
/* National code: 38-bit, stored bit-reversed. */
static uint64_t fdxb_national_code(const uint8_t *data)
{
    uint64_t n = fdxb_get_bits(data, 0, 32);
    n <<= 32;
    n |= (uint64_t)fdxb_get_bits(data, 32, 6) << (32 - 6);
    fdxb_reverse_bits((uint8_t *)&n, 0, 64);
    return n;
}

/* Country code: 10-bit, stored bit-reversed. */
static uint16_t fdxb_country_code(const uint8_t *data)
{
    uint16_t c = (uint16_t)(fdxb_get_bits(data, 38, 10) << 6);
    fdxb_reverse_bits((uint8_t *)&c, 0, 16);
    return c;
}

/*========================= D E C O D E ======================================*/
static bool fdxb_can_be_decoded(const uint8_t *frame)
{
    /* 11-bit header 10000000000 at bit 0 AND at bit 128 (next frame's header). */
    if(fdxb_get_bits(frame, 0, 11) != 0x400u) return false;   /* 0b10000000000 */
    if(fdxb_get_bits(frame, 128, 11) != 0x400u) return false;
    /* control bits: Always-1 every 9th over 13*9 bits starting at bit 3 */
    if(!fdxb_test_always1_every9(frame, 3, 13 * 9)) return false;

    /* CRC-16 over the 8 payload bytes assembled from the 9-bit groups */
    uint8_t crc_data[8];
    for(size_t i = 0; i < 8; i++)
        fdxb_copy_bits(crc_data, i * 8, 8, frame, 12 + 9 * i);
    uint16_t crc_res = fdxb_crc16(crc_data, 8, 0x1021, 0x0000);

    uint16_t crc_ex = 0;
    fdxb_copy_bits((uint8_t *)&crc_ex, 8, 8, frame, 84);
    fdxb_copy_bits((uint8_t *)&crc_ex, 0, 8, frame, 93);

    return crc_res == crc_ex;
}

/* De-frame the 128-bit frame into the 11-byte canonical payload (CRC removed).
 * Works on a local copy so the shift register is left intact. */
static void fdxb_decode(const uint8_t *frame, uint8_t *data)
{
    uint8_t work[FDXB_ENCODED_SIZE];
    memcpy(work, frame, FDXB_ENCODED_SIZE);

    fdxb_remove_every_nth(work, 3, 14 * 9, 9);          /* strip control bits */
    for(size_t i = 0; i < 11; i++)                      /* strip 11-bit header */
        fdxb_push_bit(work, FDXB_ENCODED_SIZE, 0);

    memset(data, 0, FDXB_DECODED_SIZE);
    fdxb_copy_bits(data, 0, 64, work, 0);               /* national+country+block */
    fdxb_copy_bits(data, 64, 24, work, 80);             /* extra data (skip CRC)  */
}

/*========================= D E C O D E R  I / F =============================*/

static void fdxb_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_fdxb_frame, 0, sizeof(g_fdxb_frame));
    memset(g_fdxb_data, 0, sizeof(g_fdxb_data));
    g_fdxb_last_short = false;
}

/* Bi-phase decoder: duration-only; two
 * shorts -> 0, one long -> 1; long-while-awaiting-short or out-of-window ->
 * reset. On a valid frame the 11-byte payload commits to uid. */
static bool fdxb_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint32_t dur = evt[i].t_us;
        bool pushed = false;
        uint8_t bit = 0;

        if(dur >= FDXB_SHORT_LOW && dur <= FDXB_SHORT_HIGH) {
            if(g_fdxb_last_short == false) {
                g_fdxb_last_short = true;
            } else {
                pushed = true; bit = 0;
                g_fdxb_last_short = false;
            }
        } else if(dur >= FDXB_LONG_LOW && dur <= FDXB_LONG_HIGH) {
            if(g_fdxb_last_short == false) {
                pushed = true; bit = 1;
            } else {
                g_fdxb_last_short = false;   /* reset */
            }
        } else {
            g_fdxb_last_short = false;       /* reset */
        }

        if(pushed) {
            fdxb_push_bit(g_fdxb_frame, FDXB_ENCODED_SIZE, bit);
            if(fdxb_can_be_decoded(g_fdxb_frame)) {
                fdxb_decode(g_fdxb_frame, g_fdxb_data);
                memcpy(lfrfid_tag_info.uid, g_fdxb_data,
                       min(sizeof(lfrfid_tag_info.uid), (size_t)FDXB_DECODED_SIZE));
                lfrfid_tag_info.bitrate = 32;
                return true;
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (the 11-byte de-framed payload). */
static uint8_t *fdxb_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void fdxb_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = fdxb_get_data(NULL);
    char ndec[16];

    fdxb_u64_to_dec12(fdxb_national_code(d), ndec);
    sprintf(result,
            "ID: %03u-%s\n"
            "Country: %u\n"
            "Animal: %s",
            (unsigned)fdxb_country_code(d),
            ndec,
            (unsigned)fdxb_country_code(d),
            fdxb_get_bit(d, 63) ? "Yes" : "No");
}

/*========================= E N C O D E ======================================*/
/* Build the 128-bit frame from the 11-byte payload. */
static void fdxb_encode(const uint8_t *data, uint8_t *frame)
{
    memset(frame, 0, FDXB_ENCODED_SIZE);
    fdxb_set_bit(frame, 0, 1);                          /* header first bit */
    for(size_t i = 0; i < 13; i++) {
        fdxb_set_bit(frame, 11 + 9 * i, 1);             /* control bit */
        if(i == 8 || i == 9) continue;                  /* CRC slots */
        if(i < 8)
            fdxb_copy_bits(frame, 12 + 9 * i, 8, data, i * 8);
        else
            fdxb_copy_bits(frame, 12 + 9 * i, 8, data, (i - 2) * 8);
    }
    uint16_t crc = fdxb_crc16(data, 8, 0x1021, 0x0000);
    fdxb_copy_bits(frame, 84, 8, (uint8_t *)&crc, 8);   /* crc high byte */
    fdxb_copy_bits(frame, 93, 8, (uint8_t *)&crc, 0);   /* crc low byte  */
}

/*========================= E M U L A T E  (ASK bi-phase) ====================*/
/* bit 1 -> one long (256 us) segment, bit 0 -> two short (128 us) segments;
 * output level toggles per segment. Worst case (all 0) = 256 entries. */
static uint16_t fdxb_build_wave(const uint8_t *frame, uint16_t nbits,
                                Encoded_Data_t *steps, uint16_t max_steps,
                                uint8_t gpio_pin)
{
    uint32_t bsrr_set   = 1u << gpio_pin;
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);
    uint16_t idx = 0;
    bool level = false;

    for(uint16_t b = 0; b < nbits; b++) {
        if(fdxb_get_bit(frame, b)) {                    /* bit 1 -> one long */
            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = FDXB_LONG_US; idx++;
        } else {                                        /* bit 0 -> two shorts */
            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = FDXB_SHORT_US; idx++;

            level = !level;
            if(idx >= max_steps) break;
            steps[idx].bsrr = level ? bsrr_set : bsrr_reset;
            steps[idx].time_us = FDXB_SHORT_US; idx++;
        }
    }
    return idx;
}

static bool fdxb_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t frame[FDXB_ENCODED_SIZE];
    fdxb_encode(lfrfid_tag_info.uid, frame);

    uint16_t n = fdxb_build_wave(frame, FDXB_ENCODED_BITS,
                                 lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                                 FDXB_EMUL_GPIO_PIN);
    if(n == 0) { lfrfid_encoded_data.length = 0; return false; }
    lfrfid_encoded_data.length = n;
    return true;
}

static void fdxb_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * DIPHASE, RF/32, four data blocks, five total.
 * protocol_fdx_b_write_data. The 128-bit frame occupies the first 16 bytes;
 * blocks 1..4 = frame bits 0..127 (MSB-first).
 */
static void fdxb_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t frame[FDXB_ENCODED_SIZE];

    fdxb_encode(tag_data->uid, frame);

    if(!write || write->type != LFRFIDProgramTypeT5577)
        return;

    write->t5577.block_data[0] =
        (FDXB_T5577_MOD_DIPHASE | T5577_BITRATE_RF_32 | T5577_TRANS_BL_1_4);
    bytes_to_u32_array(BIT_ORDER_MSB_FIRST, frame, &write->t5577.block_data[1], 4);
    write->t5577.max_blocks = 5;
}

static void fdxb_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_fdx_b = {
    .name         = "FDX-B",
    .manufacturer = "ISO",
    .data_size    = FDXB_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)fdxb_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)fdxb_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)fdxb_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)fdxb_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)fdxb_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)fdxb_write_begin,
        .send  = (lfrfidProtocolWriteSend)fdxb_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)fdxb_render_data,
};
