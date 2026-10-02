/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Generic HID Extended (HIDExt)
 *
 * ASK/FSK2a (HID physical layer, RF/50). A 192-bit frame = 8-bit 0x1D preamble
 * + 184 Manchester bits encoding a 92-bit HID Extended record. The raw 92 bits
 * are preserved verbatim as the 12-byte canonical credential (rendered
 * no parser for this format yet).
 *
 * Reuses the shared M1 FSK demodulator (fsk_symbol_feed / fsk_bit_feed, from
 * lfrfid_protocol_h10301) for acquisition and the shared 96-bit FSK emulate
 * wave builder (h10301_raw96_to_wave), invoked twice to emit the two 96-bit
 * halves of the 192-bit frame — no second FSK engine, no new wave code.
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

#define HID_PREAMBLE        (0x1D)
#define HIDEXT_DATA_BYTES   (23)   /* Manchester data bytes between the preambles */
#define HIDEXT_EMUL_GPIO_PIN (2)

/***************************** V A R I A B L E S ******************************/

static fsk_symbol_state_t g_hidext_sym;
static fsk_bit_state_t    g_hidext_bit;
static uint8_t g_hidext_encoded[HIDEXT_ENCODED_SIZE];   /* 25-byte shift window */
static uint8_t g_hidext_data[HIDEXT_DECODED_SIZE];       /* 12-byte credential */

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
static uint8_t hidext_get_bit(const uint8_t *data, size_t pos)
{
    return (uint8_t)((data[pos / 8] >> (7 - (pos % 8))) & 1u);
}

static void hidext_set_bit(uint8_t *data, size_t pos, uint8_t bit)
{
    if(bit & 1u)
        data[pos / 8] |= (uint8_t)(1u << (7 - (pos % 8)));
    else
        data[pos / 8] &= (uint8_t)~(1u << (7 - (pos % 8)));
}

static void hidext_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*========================= D E C O D E ======================================*/
/* Preamble (0x1D) at byte 0 AND byte 24 (next frame), and every 2-bit
 * Manchester pair in the 23 data bytes must be 01 or 10. */
static bool hidext_can_be_decoded(const uint8_t *enc)
{
    if(enc[0] != HID_PREAMBLE || enc[1 + HIDEXT_DATA_BYTES] != HID_PREAMBLE) return false;

    for(size_t i = 1; i < (1 + HIDEXT_DATA_BYTES); i++) {
        for(size_t n = 0; n < 4; n++) {
            uint8_t bit_pair = (uint8_t)((enc[i] >> (n * 2)) & 0x3u);
            if(bit_pair == 0x3u || bit_pair == 0x0u) return false;
        }
    }
    return true;
}

/* Manchester-decode the 184 data bits into the 92-bit record (01 -> 0, 10 -> 1). */
static void hidext_decode(const uint8_t *from, uint8_t *to)
{
    size_t bit_index = 0;
    memset(to, 0, HIDEXT_DECODED_SIZE);
    for(size_t i = 1; i < (1 + HIDEXT_DATA_BYTES); i++) {
        for(size_t n = 0; n < 4; n++) {
            uint8_t bit_pair = (uint8_t)((from[i] >> (6 - (n * 2))) & 0x3u);
            if(bit_pair == 0x1u)      hidext_set_bit(to, bit_index, 0);
            else if(bit_pair == 0x2u) hidext_set_bit(to, bit_index, 1);
            bit_index++;
        }
    }
}

/*========================= D E C O D E R  I / F =============================*/

static void hidext_decoder_begin(void *proto)
{
    (void)proto;
    fsk_symbol_state_init(&g_hidext_sym);
    fsk_bit_state_init(&g_hidext_bit);
    memset(g_hidext_encoded, 0, sizeof(g_hidext_encoded));
    memset(g_hidext_data, 0, sizeof(g_hidext_data));
}

static bool hidext_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint8_t symbol;
        if(fsk_symbol_feed(&g_hidext_sym, &evt[i], &symbol)) {
            uint8_t bit;
            if(fsk_bit_feed(&g_hidext_bit, symbol, &bit)) {
                hidext_push_bit(g_hidext_encoded, HIDEXT_ENCODED_SIZE, bit);
                if(hidext_can_be_decoded(g_hidext_encoded)) {
                    /* No abstain needed: HIDExt's 192-bit frame is disjoint from
                     * the 96-bit H10301/HIDProx frames — a mid-stream 0x1D
                     * preamble is not a valid Manchester pair, so those shorter
                     * decoders never accept this frame and vice-versa. */
                    hidext_decode(g_hidext_encoded, g_hidext_data);
                    memcpy(lfrfid_tag_info.uid, g_hidext_data, HIDEXT_DECODED_SIZE);
                    return true;
                }
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
static uint8_t *hidext_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

/* This format has no field parser ("Data: Unknown"); M1 preserves and
 * shows the full 12-byte canonical record instead of a placeholder. */
static void hidext_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = hidext_get_data(NULL);
    sprintf(result,
            "Generic HID Extended\n"
            "Data: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            d[0], d[1], d[2], d[3], d[4], d[5],
            d[6], d[7], d[8], d[9], d[10], d[11]);
}

/*========================= E N C O D E ======================================*/
/* Build the 192-bit frame: 0x1D preamble + Manchester of the 92-bit record
 * (bit 1 -> pair 10, bit 0 -> pair 01). */
static void hidext_encode(const uint8_t *data, uint8_t *enc)
{
    memset(enc, 0, HIDEXT_ENCODED_SIZE);
    enc[0] = HID_PREAMBLE;
    size_t bit_index = 0;
    for(size_t i = 0; i < HIDEXT_DECODED_BITS; i++) {
        if(hidext_get_bit(data, i)) {
            hidext_set_bit(enc, 8 + bit_index, 1);
            hidext_set_bit(enc, 8 + bit_index + 1, 0);
        } else {
            hidext_set_bit(enc, 8 + bit_index, 0);
            hidext_set_bit(enc, 8 + bit_index + 1, 1);
        }
        bit_index += 2;
    }
}

/*========================= E M U L A T E ====================================*/
static bool hidext_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t frame[HIDEXT_ENCODED_SIZE];
    size_t  n1 = 0, n2 = 0;

    hidext_encode(lfrfid_tag_info.uid, frame);

    /* Emit the 192-bit frame as two 96-bit halves via the shared wave builder:
     * bytes 0..11 = bits 0..95, bytes 12..23 = bits 96..191. Byte 24 is the
     * trailing preamble (decoder window only) and is not transmitted. */
    if(h10301_raw96_to_wave(frame, HIDEXT_EMUL_GPIO_PIN,
                            lfrfid_encoded_data.data, ENCODED_DATA_MAX, &n1) != 0) {
        lfrfid_encoded_data.length = 0;
        return false;
    }
    if(h10301_raw96_to_wave(frame + 12, HIDEXT_EMUL_GPIO_PIN,
                            lfrfid_encoded_data.data + n1, ENCODED_DATA_MAX - n1, &n2) != 0) {
        lfrfid_encoded_data.length = 0;
        return false;
    }
    lfrfid_encoded_data.length = (uint16_t)(n1 + n2);
    return true;
}

static void hidext_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/* FSK2a, RF/50, six data blocks (BL_1_6), seven total. */
static void hidext_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t frame[HIDEXT_ENCODED_SIZE];

    hidext_encode(tag_data->uid, frame);

    if(!write || write->type != LFRFIDProgramTypeT5577)
        return;

    write->t5577.block_data[0] =
        (T5577_MOD_FSK2a | T5577_BITRATE_RF_50 | T5577_TRANS_BL_1_6);
    bytes_to_u32_array(BIT_ORDER_MSB_FIRST, frame, &write->t5577.block_data[1], 6);
    write->t5577.max_blocks = 7;
}

static void hidext_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_hid_ex_generic = {
    .name         = "HIDExt",
    .manufacturer = "Generic",
    .data_size    = HIDEXT_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)hidext_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)hidext_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)hidext_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)hidext_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)hidext_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)hidext_write_begin,
        .send  = (lfrfidProtocolWriteSend)hidext_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)hidext_render_data,
};
