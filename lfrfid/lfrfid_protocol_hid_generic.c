/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Generic HID Proximity (HIDProx)
 *
 * ASK/FSK2a (HID physical layer, RF/50). A 96-bit frame = 8-bit 0x1D preamble
 * + 88 Manchester bits encoding a 44-bit HID record. The record carries a
 * leading-1 length sentinel; the raw 44 bits are preserved as the 6-byte
 * canonical credential (any HID bit length, not just 26-bit H10301).
 *
 * Reuses the shared M1 FSK demodulator (fsk_symbol_feed / fsk_bit_feed, from
 * lfrfid_protocol_h10301) for acquisition and the shared 96-bit FSK emulate
 * wave builder (h10301_raw96_to_wave) — no second FSK engine.
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

#include "lfrfid.h"   /* pulls in fsk_symbol_feed/fsk_bit_feed via h10301.h */

/* Shared 96-bit FSK emulate wave builder (defined in lfrfid_protocol_h10301.c). */
extern int h10301_raw96_to_wave(const uint8_t raw96[12], uint8_t gpio_pin,
                                Encoded_Data_t *steps, size_t max_steps,
                                size_t *out_step_count);

/*************************** D E F I N E S ************************************/

#define HID_PREAMBLE       (0x1D)
#define HID_DECODED_BITS   (44)
#define HID_EMUL_GPIO_PIN  (2)

/***************************** V A R I A B L E S ******************************/

static fsk_symbol_state_t g_hid_sym;
static fsk_bit_state_t    g_hid_bit;
static uint8_t g_hid_encoded[HIDPROX_ENCODED_SIZE];   /* 13-byte shift window */
static uint8_t g_hid_data[HIDPROX_DECODED_SIZE];       /* 6-byte credential */

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *hid_get_data(void *proto);
static void     hid_decoder_begin(void *proto);
static bool     hid_decoder_execute(void *proto, uint16_t size);
static bool     hid_encoder_begin(void *proto);
static void     hid_encoder_send(void *proto);
static void     hid_write_begin(void *protocol, void *data);
static void     hid_write_send(void *proto);
static void     hid_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
static uint8_t hid_get_bit(const uint8_t *data, size_t pos)
{
    return (uint8_t)((data[pos / 8] >> (7 - (pos % 8))) & 1u);
}

static void hid_set_bit(uint8_t *data, size_t pos, uint8_t bit)
{
    if(bit & 1u)
        data[pos / 8] |= (uint8_t)(1u << (7 - (pos % 8)));
    else
        data[pos / 8] &= (uint8_t)~(1u << (7 - (pos % 8)));
}

static uint32_t hid_get_bits(const uint8_t *data, size_t pos, uint8_t len)
{
    uint32_t v = 0;
    for(uint8_t i = 0; i < len; i++)
        v = (v << 1) | hid_get_bit(data, pos + i);
    return v;
}

static void hid_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*========================= D E C O D E ======================================*/
/* Preamble (0x1D) at byte 0 AND byte 12 (next frame), and every 2-bit
 * Manchester pair in the 11 data bytes must be 01 or 10. */
static bool hid_can_be_decoded(const uint8_t *enc)
{
    if(enc[0] != HID_PREAMBLE || enc[1 + 11] != HID_PREAMBLE) return false;

    for(size_t i = 1; i < (1 + 11); i++) {
        for(size_t n = 0; n < 4; n++) {
            uint8_t bit_pair = (uint8_t)((enc[i] >> (n * 2)) & 0x3u);
            if(bit_pair == 0x3u || bit_pair == 0x0u) return false;
        }
    }
    return true;
}

/* Manchester-decode the 88 data bits into the 44-bit record (01 -> 0, 10 -> 1). */
static void hid_decode(const uint8_t *from, uint8_t *to)
{
    size_t bit_index = 0;
    memset(to, 0, HIDPROX_DECODED_SIZE);
    for(size_t i = 1; i < (1 + 11); i++) {
        for(size_t n = 0; n < 4; n++) {
            uint8_t bit_pair = (uint8_t)((from[i] >> (6 - (n * 2))) & 0x3u);
            if(bit_pair == 0x1u)      hid_set_bit(to, bit_index, 0);
            else if(bit_pair == 0x2u) hid_set_bit(to, bit_index, 1);
            bit_index++;
        }
    }
}

/* HID Proximity length from the leading-1 sentinel. Returns 0
 * (unknown) for a header implying < 26 bits. */
static uint8_t hid_protocol_size(const uint8_t *data)
{
    for(size_t bit_index = 0; bit_index < 6; bit_index++)
        if(hid_get_bit(data, bit_index))
            return (uint8_t)(HID_DECODED_BITS - bit_index - 1);

    if(!hid_get_bit(data, 6)) return 37;

    size_t bit_index = 7;
    uint8_t size = 36;
    while(!hid_get_bit(data, bit_index) && size >= 26) { size--; bit_index++; }
    return size < 26 ? 0 : size;
}

/*========================= D E C O D E R  I / F =============================*/

static void hid_decoder_begin(void *proto)
{
    (void)proto;
    fsk_symbol_state_init(&g_hid_sym);
    fsk_bit_state_init(&g_hid_bit);
    memset(g_hid_encoded, 0, sizeof(g_hid_encoded));
    memset(g_hid_data, 0, sizeof(g_hid_data));
}

static bool hid_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint8_t symbol;
        if(fsk_symbol_feed(&g_hid_sym, &evt[i], &symbol)) {
            uint8_t bit;
            if(fsk_bit_feed(&g_hid_bit, symbol, &bit)) {
                hid_push_bit(g_hid_encoded, HIDPROX_ENCODED_SIZE, bit);
                if(hid_can_be_decoded(g_hid_encoded)) {
                    hid_decode(g_hid_encoded, g_hid_data);

                    /* Coexistence with H10301: the dedicated H10301 decoder
                     * (registered earlier, validate_count 3) owns standard
                     * 26-bit HID Prox. HIDProx ABSTAINS on 26-bit frames so it
                     * can never steal an H10301 credential or oscillate the
                     * shared detection candidate against it. HIDProx covers
                     * every OTHER HID Prox bit length (33/34/35/36/37/...),
                     * which H10301's strict format+parity check rejects. */
                    if(hid_protocol_size(g_hid_data) == 26)
                        return false;

                    memcpy(lfrfid_tag_info.uid, g_hid_data, HIDPROX_DECODED_SIZE);
                    return true;
                }
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
static uint8_t *hid_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

/* Append the `protocol_size`-bit key as hexadecimal text. */
static void hid_cat_protocol_bits(const uint8_t *data, uint8_t protocol_size, char *out)
{
    const uint8_t hex_character_count = (uint8_t)((protocol_size + 3) / 4);
    const uint8_t protocol_bit_index  = (uint8_t)(HID_DECODED_BITS - protocol_size);
    int p = 0;

    for(uint8_t i = 0; i < hex_character_count; i++) {
        uint8_t nibble = (i == 0)
            ? (uint8_t)hid_get_bits(data, protocol_bit_index,
                                    (uint8_t)(protocol_size % 4 == 0 ? 4 : protocol_size % 4))
            : (uint8_t)hid_get_bits(data, protocol_bit_index + i * 4, 4);
        p += sprintf(out + p, "%X", nibble & 0xF);
    }
}

static void hid_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = hid_get_data(NULL);
    uint8_t protocol_size = hid_protocol_size(d);

    if(protocol_size == 0) {
        sprintf(result,
                "Generic HID Proximity\n"
                "Data: %02X%02X%02X%02X%02X%X",
                d[0], d[1], d[2], d[3], d[4], d[5] >> 4);
    } else {
        char key[20];
        hid_cat_protocol_bits(d, protocol_size, key);
        sprintf(result, "%u-bit HID Proximity\nData: %s", (unsigned)protocol_size, key);
    }
}

/*========================= E N C O D E ======================================*/
/* Build the 96-bit frame: 0x1D preamble + Manchester of the 44-bit record
 * (bit 1 -> pair 10, bit 0 -> pair 01). */
static void hid_encode(const uint8_t *data, uint8_t *enc)
{
    memset(enc, 0, HIDPROX_ENCODED_SIZE);
    enc[0] = HID_PREAMBLE;
    size_t bit_index = 0;
    for(size_t i = 0; i < HID_DECODED_BITS; i++) {
        if(hid_get_bit(data, i)) {
            hid_set_bit(enc, 8 + bit_index, 1);
            hid_set_bit(enc, 8 + bit_index + 1, 0);
        } else {
            hid_set_bit(enc, 8 + bit_index, 0);
            hid_set_bit(enc, 8 + bit_index + 1, 1);
        }
        bit_index += 2;
    }
}

/*========================= E M U L A T E ====================================*/
static bool hid_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t frame[HIDPROX_ENCODED_SIZE];
    size_t  n = 0;

    hid_encode(lfrfid_tag_info.uid, frame);
    /* Reuse the shared 96-bit FSK wave builder (bytes 0..11 = the 96-bit frame). */
    if(h10301_raw96_to_wave(frame, HID_EMUL_GPIO_PIN,
                            lfrfid_encoded_data.data, ENCODED_DATA_MAX, &n) != 0)
    {
        lfrfid_encoded_data.length = 0;
        return false;
    }
    lfrfid_encoded_data.length = (uint16_t)n;
    return true;
}

static void hid_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/* FSK2a, RF/50, 3 data blocks (BL_1_3), 4 total — same as H10301's HID FSK. */
static void hid_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t frame[HIDPROX_ENCODED_SIZE];

    hid_encode(tag_data->uid, frame);

    if(!write || write->type != LFRFIDProgramTypeT5577)
        return;

    write->t5577.block_data[0] =
        (T5577_MOD_FSK2a | T5577_BITRATE_RF_50 | T5577_TRANS_BL_1_3);
    bytes_to_u32_array(BIT_ORDER_MSB_FIRST, frame, &write->t5577.block_data[1], 3);
    write->t5577.max_blocks = 4;
}

static void hid_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_hid_generic = {
    .name         = "HIDProx",
    .manufacturer = "Generic",
    .data_size    = HIDPROX_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)hid_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)hid_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)hid_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)hid_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)hid_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)hid_write_begin,
        .send  = (lfrfidProtocolWriteSend)hid_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)hid_render_data,
};
