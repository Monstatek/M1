/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Kantech ioProx XSF protocol
 *
 * Kantech ioProx / XSF, FSK2a modulation, RF/64 bit rate, 64-bit frame:
 * preamble (byte 0 = 0x00, bit 8 = 0), a fixed 0xF0 second word, six 8-bit
 * fields each followed by a framing '1' (facility, version, code-hi, code-lo,
 * checksum), and trailing framing bits. The checksum is a byte-sum complement
 * over the fixed word plus the four data bytes.
 *
 * The self-contained bit helpers use MSB-first bit order. The credential
 * field order and checksum include the fixed second word described above.
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

#define IOPROX_FIXED_WORD   (0xF0)   /* fixed second word at bits 9..16 */

/* RF/64 FSK sub-carrier cycles per data bit:
 * a '0' bit is fc/8 (64 field clocks -> 8 sub-carrier cycles), a '1' bit is
 * fc/10 (64 field clocks -> ~6 cycles). Used by both decoder grouping and the
 * emulate wave generator. */
#define IOPROX_PULSES_FC8   (8)      /* symbol 0 -> data bit 0 */
#define IOPROX_PULSES_FC10  (6)      /* symbol 1 -> data bit 1 */

/* FSK2a emulation timing (RF/64) — same sub-carrier periods as the proven
 * H10301 / Pyramid emulators (fc/8 = 64 us, fc/10 = 80 us); only the cycle
 * COUNT per bit differs (8 / 6 for RF/64 vs 6 / 5 for RF/50). */
#define IOPROX_OUTPUT_INVERT     (0)
/* True nominal FSK2a half/period values. The "-2" that used to sit on the
 * PERIOD constants compensated for the old per-edge ISR path writing
 * TIM5->ARR = time_us with no inclusive-counter "-1" correction
 * (rfid_emul_handler(), lfrfid_hal.c, now removed) -- see lfrfid_dma_tx.c's
 * header comment for the full derivation. The new GPDMA2-driven transport
 * performs that correction itself, once, from the real measured TIM5
 * kernel clock -- so it must not be duplicated here. */
#define IOPROX_EMUL_HALF_ONE_US  (40)          /* fc/10 half period */
#define IOPROX_EMUL_HALF_ZERO_US (32)          /* fc/8  half period */
#define IOPROX_EMUL_PERIOD_ONE_US  (80)
#define IOPROX_EMUL_PERIOD_ZERO_US (64)
#define IOPROX_EMUL_CYCLES_ONE   IOPROX_PULSES_FC10   /* fc/10 -> 6 cycles */
#define IOPROX_EMUL_CYCLES_ZERO  IOPROX_PULSES_FC8    /* fc/8  -> 8 cycles */
#define IOPROX_EMUL_GPIO_PIN     (2)           /* RFID_OUT pin on GPIOA */

/***************************** V A R I A B L E S ******************************/

static uint8_t g_ioprox_encoded[IOPROX_ENCODED_SIZE];   /* 8-byte shift register */
static uint8_t g_ioprox_decoded[IOPROX_DECODED_SIZE];   /* 4-byte credential     */
static fsk_symbol_state_t g_ioprox_sym_st;              /* reused, read-only     */

/* RF/64 run-length grouping state on the M1
 * sub-carrier symbol stream). last_sym mirrors fsk_demod's last_pulse (init 0). */
static uint32_t g_ioprox_rl_count;
static uint8_t  g_ioprox_rl_last;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint8_t *ioprox_get_data(void *proto);
static void     ioprox_decoder_begin(void *proto);
static bool     ioprox_decoder_execute(void *proto, uint16_t size);
static bool     ioprox_encoder_begin(void *proto);
static void     ioprox_encoder_send(void *proto);
static void     ioprox_write_begin(void *protocol, void *data);
static void     ioprox_write_send(void *proto);
static void     ioprox_render_data(void *protocol, char *result);

/*==================== S E L F - C O N T A I N E D   B I T S ==================*/
/* MSB-first bit primitives. */

static uint8_t ioprox_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8] >> (7 - (position % 8))) & 1u);
}

static void ioprox_set_bit(uint8_t *data, size_t position, uint8_t bit)
{
    if(bit & 1u)
        data[position / 8] |= (uint8_t)(1u << (7 - (position % 8)));
    else
        data[position / 8] &= (uint8_t)~(1u << (7 - (position % 8)));
}

/* Read 8 MSB-first bits at an arbitrary position. */
static uint8_t ioprox_get_byte(const uint8_t *data, size_t position)
{
    uint8_t v = 0;
    for(int b = 0; b < 8; b++)
        v = (uint8_t)((v << 1) | ioprox_get_bit(data, position + b));
    return v;
}

/* Write the low `length` bits (<=8) of `byte`, MSB-first, at `position`.
 * Mirrors bit_lib_set_bits(). */
static void ioprox_set_bits(uint8_t *data, size_t position, uint8_t byte, uint8_t length)
{
    for(uint8_t i = 0; i < length; i++) {
        uint8_t shift = (uint8_t)((length - 1) - i);
        ioprox_set_bit(data, position + i, (byte >> shift) & 1u);
    }
}

/* Shift a bit into a big-endian byte buffer from the LSB end (mirrors
 * bit_lib_push_bit()). */
static void ioprox_push_bit(uint8_t *data, size_t data_size, uint8_t bit)
{
    size_t last = data_size - 1;
    for(size_t i = 0; i < last; i++)
        data[i] = (uint8_t)((data[i] << 1) | ((data[i + 1] >> 7) & 1u));
    data[last] = (uint8_t)((data[last] << 1) | (bit & 1u));
}

/*========================= C H E C K S U M ==================================*/
/* ioProx XSF checksum:
 *   checksum = 0xFF - (byte@9 + byte@18 + byte@27 + byte@36 + byte@45)
 *            = 0xFF - (0xF0 + FC + Version + CodeHi + CodeLo)
 * 8-bit wrap-around on the accumulation. */
static uint8_t ioprox_compute_checksum(const uint8_t *encoded)
{
    uint8_t sum = 0;
    for(int i = 1; i <= 5; i++)
        sum = (uint8_t)(sum + ioprox_get_byte(encoded, 9 * i));
    return (uint8_t)(0xFF - sum);
}

/*========================= D E C O D E ======================================*/
/*
 * Frame validation:
 *   byte0 == 0x00                                    (preamble bits 0..7)
 *   (byte1 >> 6) == 0b01                             (bit 8 = 0, bit 9 = 1)
 *   framing '1' at bits 17, 26, 35, 44, 53, 62, 63
 *   checksum(byte@54) == 0xFF - (0xF0+FC+V+CHi+CLo)
 * The fixed 0xF0 word (bits 10..16) is validated via the checksum, exactly as
 * no separate literal 0xF0 equality test is added because it would
 * false-reject valid frames.
 */
static bool ioprox_can_be_decoded(const uint8_t *d)
{
    if(d[0] != 0x00)
        return false;
    if((d[1] >> 6) != 0x01)          /* bit 8 == 0 AND bit 9 == 1 */
        return false;

    if(!ioprox_get_bit(d, 17)) return false;
    if(!ioprox_get_bit(d, 26)) return false;
    if(!ioprox_get_bit(d, 35)) return false;
    if(!ioprox_get_bit(d, 44)) return false;
    if(!ioprox_get_bit(d, 53)) return false;
    if(!ioprox_get_bit(d, 62)) return false;
    if(!ioprox_get_bit(d, 63)) return false;

    if(ioprox_compute_checksum(d) != ioprox_get_byte(d, 54))
        return false;

    return true;
}

/* Extract the four credential bytes. */
static void ioprox_decode(const uint8_t *encoded, uint8_t *decoded)
{
    decoded[0] = ioprox_get_byte(encoded, 18);   /* Facility code */
    decoded[1] = ioprox_get_byte(encoded, 27);   /* Version       */
    decoded[2] = ioprox_get_byte(encoded, 36);   /* Code high     */
    decoded[3] = ioprox_get_byte(encoded, 45);   /* Code low      */
}

/*========================= D E C O D E R  I / F =============================*/

static void ioprox_decoder_begin(void *proto)
{
    (void)proto;
    memset(g_ioprox_encoded, 0, sizeof(g_ioprox_encoded));
    memset(g_ioprox_decoded, 0, sizeof(g_ioprox_decoded));
    fsk_symbol_state_init(&g_ioprox_sym_st);
    g_ioprox_rl_count = 0;
    g_ioprox_rl_last  = 0;
}

/* Push one grouped data bit and test for a complete valid frame. */
static bool ioprox_push_and_check(uint8_t value)
{
    ioprox_push_bit(g_ioprox_encoded, IOPROX_ENCODED_SIZE, value);
    if(ioprox_can_be_decoded(g_ioprox_encoded)) {
        ioprox_decode(g_ioprox_encoded, g_ioprox_decoded);
        memcpy(lfrfid_tag_info.uid, g_ioprox_decoded,
               min(sizeof(lfrfid_tag_info.uid), (size_t)IOPROX_DECODED_SIZE));
        return true;
    }
    return false;
}

static bool ioprox_decoder_execute(void *proto, uint16_t size)
{
    lfrfid_evt_t *evt = (lfrfid_evt_t *)proto;

    for(int i = 0; i < size; i++) {
        uint8_t symbol;
        /* Reused, read-only: classify one sub-carrier period as 0 (fc/8, 64us)
         * or 1 (fc/10, 80us). RF-independent, no H10301/Pyramid impact. */
        if(!fsk_symbol_feed(&g_ioprox_sym_st, &evt[i], &symbol))
            continue;

        /* RF/64 run-length grouping: a run of same-type
         * sub-carrier periods yields (run+1)/pulses_per_bit data bits equal to
         * that run's symbol value. */
        g_ioprox_rl_count++;
        if(g_ioprox_rl_last != symbol) {
            uint32_t n = g_ioprox_rl_count + 1;
            n /= (g_ioprox_rl_last ? IOPROX_PULSES_FC10 : IOPROX_PULSES_FC8);
            uint8_t value = g_ioprox_rl_last;

            g_ioprox_rl_count = 0;
            g_ioprox_rl_last  = symbol;

            for(uint32_t b = 0; b < n; b++) {
                if(ioprox_push_and_check(value))
                    return true;
            }
        }
    }
    return false;
}

/*========================= D E T A I L S ====================================*/
/* Canonical source is lfrfid_tag_info.uid (populated by live read AND by file
 * load / Add Manually / reload), matching EM4100/H10301/Pyramid. Never read the
 * decoder-private scratch (g_ioprox_decoded). */
static uint8_t *ioprox_get_data(void *proto)
{
    (void)proto;
    return lfrfid_tag_info.uid;
}

static void ioprox_render_data(void *protocol, char *result)
{
    (void)protocol;
    uint8_t *d = ioprox_get_data(NULL);

    sprintf(result,
            "FC: %u\n"
            "V: %u\n"
            "Card: %hu",
            d[0],
            d[1],
            (uint16_t)((d[2] << 8) | d[3]));
}

/*========================= E N C O D E ======================================*/
/* Encode the ioProx XSF frame. */
static void ioprox_encode(const uint8_t *decoded, uint8_t *encoded)
{
    memset(encoded, 0, IOPROX_ENCODED_SIZE);

    /* Preamble: byte 0 = 0x00 and bit 8 = 0 come from memset. */
    ioprox_set_bits(encoded, 9, IOPROX_FIXED_WORD, 8);   /* fixed 0xF0 word */
    ioprox_set_bit(encoded, 17, 1);

    ioprox_set_bits(encoded, 18, decoded[0], 8);         /* Facility code */
    ioprox_set_bit(encoded, 26, 1);

    ioprox_set_bits(encoded, 27, decoded[1], 8);         /* Version */
    ioprox_set_bit(encoded, 35, 1);

    ioprox_set_bits(encoded, 36, decoded[2], 8);         /* Code high */
    ioprox_set_bit(encoded, 44, 1);

    ioprox_set_bits(encoded, 45, decoded[3], 8);         /* Code low */
    ioprox_set_bit(encoded, 53, 1);

    ioprox_set_bits(encoded, 54, ioprox_compute_checksum(encoded), 8);
    ioprox_set_bit(encoded, 62, 1);
    ioprox_set_bit(encoded, 63, 1);
}

/*========================= E M U L A T E  (FSK2a wave) ======================*/
/*
 * Turn the 64-bit encoded frame into GPIO BSRR step pairs, FSK2a at RF/64.
 * Same wave shape and polarity as the H10301 / Pyramid emulators; only the
 * per-bit cycle count differs (8 for a fc/8 bit, 6 for a fc/10 bit).
 * Worst case (all bit-0) = 64 * 8 cycles * 2 steps = 1024 <= ENCODED_DATA_MAX.
 * Returns 0 on success, -1 on step-buffer overflow (frame not emulated).
 */
static int ioprox_encoded_to_wave(const uint8_t *encoded, uint8_t gpio_pin,
                                  Encoded_Data_t *steps, size_t max_steps,
                                  size_t *out_step_count)
{
    size_t idx = 0;
    uint32_t bsrr_set   = 1u << gpio_pin;         /* HIGH */
    uint32_t bsrr_reset = 1u << (gpio_pin + 16);  /* LOW  */

    for(int bit = 0; bit < IOPROX_ENCODED_BITS; bit++) {
        uint8_t b = ioprox_get_bit(encoded, bit);

        uint16_t half_us, period_us;
        int repeat;

        if(b == 0) {
            half_us   = IOPROX_EMUL_HALF_ZERO_US;
            period_us = (uint16_t)(IOPROX_EMUL_PERIOD_ZERO_US - half_us);
            repeat    = IOPROX_EMUL_CYCLES_ZERO;
        } else {
            half_us   = IOPROX_EMUL_HALF_ONE_US;
            period_us = (uint16_t)(IOPROX_EMUL_PERIOD_ONE_US - half_us);
            repeat    = IOPROX_EMUL_CYCLES_ONE;
        }

        for(int i = 0; i < repeat; i++) {
#if IOPROX_OUTPUT_INVERT
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

static bool ioprox_encoder_begin(void *proto)
{
    (void)proto;
    uint8_t encoded[IOPROX_ENCODED_SIZE];
    size_t  step_count = 0;

    ioprox_encode(lfrfid_tag_info.uid, encoded);

    if(ioprox_encoded_to_wave(encoded, IOPROX_EMUL_GPIO_PIN,
                              lfrfid_encoded_data.data, ENCODED_DATA_MAX,
                              &step_count) != 0) {
        lfrfid_encoded_data.length = 0;   /* refuse a truncated frame */
        return false;
    }

    lfrfid_encoded_data.length = (uint16_t)step_count;
    return true;
}

static void ioprox_encoder_send(void *proto)
{
    (void)proto;
    lfrfid_encoded_data.index = 0;
    lfrfid_emul_hw_init();
}

/*========================= T 5 5 7 7   W R I T E ============================*/
/*
 * FSK2a, RF/64, 2 data blocks (64-bit frame). Config in block 0, frame in
 * blocks 1..2 (MSB-first), 3 blocks written total.
 * protocol_io_prox_xsf_write_data (MAXBLOCK=2, blocks_to_write=3).
 */
static void ioprox_write_begin(void *protocol, void *data)
{
    LFRFID_TAG_INFO *tag_data = (LFRFID_TAG_INFO *)protocol;
    LFRFIDProgram   *write    = (LFRFIDProgram *)data;
    uint8_t          encoded[IOPROX_ENCODED_SIZE];

    ioprox_encode(tag_data->uid, encoded);

    if(write && write->type == LFRFIDProgramTypeT5577) {
        write->t5577.block_data[0] =
            (T5577_MOD_FSK2a | T5577_BITRATE_RF_64 | T5577_TRANS_BL_1_2);
        /* Blocks 1..2 = encoded frame bytes 0..7 (64 bits), MSB-first. */
        bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded, &write->t5577.block_data[1], 2);
        write->t5577.max_blocks = 3;
    }
}

static void ioprox_write_send(void *proto)
{
    (void)proto;
    t5577_execute_write(lfrfid_program, 0);
}

/*============================================================================*/
const LFRFIDProtocolBase protocol_ioprox = {
    .name         = "IoProxXSF",
    .manufacturer = "Kantech",
    .data_size    = IOPROX_DECODED_SIZE,
    .features     = LFRFIDFeatureASK,
    .get_data     = (lfrfidProtocolGetData)ioprox_get_data,
    .decoder = {
        .begin   = (lfrfidProtocolDecoderBegin)ioprox_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)ioprox_decoder_execute,
    },
    .encoder = {
        .begin = (lfrfidProtocolEncoderBegin)ioprox_encoder_begin,
        .send  = (lfrfidProtocolEncoderSend)ioprox_encoder_send,
    },
    .write = {
        .begin = (lfrfidProtocolWriteBegin)ioprox_write_begin,
        .send  = (lfrfidProtocolWriteSend)ioprox_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)ioprox_render_data,
};
