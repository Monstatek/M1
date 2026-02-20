/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) implementation
 *
 * Portions of the data structure definitions and table-driven
 * architecture were adapted from the Flipper Zero firmware project.
 *
 * Original project:
 * https://github.com/flipperdevices/flipperzero-firmware
 *
 * Licensed under the GNU General Public License v3.0 (GPLv3).
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
#include <math.h>

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "uiview.h"

#include "lfrfid.h"


/*************************** D E F I N E S ************************************/
#define H10301_DECODED_DATA_SIZE     (3)

static uint8_t* protocol_h10301_get_data(void* proto);
static bool h10301_decoder_execute(void* proto, uint16_t size, void* dec);
static bool protocol_h10301_decoder_execute(void* proto, uint16_t size);

/* ============================================================
 * HID H10301
 * ============================================================ */
#define OUTPUT_INVERT	0
/*(80/64) */
#define PERIOD_ONE_US     80U   // (1,40)+(0,40)
#define PERIOD_ZERO_US    64U   // (1,32)+(0,32)
#define PERIOD_TOL_PCT    20U   // ±20%
#define HALF_TOL_PCT      70U   // ±30% (half sanity)

#define EMUL_HALF_ONE_CORR	(0)
#define EMUL_HALF_ZERO_CORR	(0)
#define EMUL_HALF_ONE_US  (40 - EMUL_HALF_ONE_CORR)
#define EMUL_HALF_ZERO_US (32 - EMUL_HALF_ZERO_CORR)
#define EMUL_PERIOD_ONE_US  (80-2) //(80)
#define EMUL_PERIOD_ZERO_US (64-2) //(64)

/* ───────── FSK half → symbol  ───────── */
#define HALF_TOLERANCE_RATIO (1.0f - 0.35f)	// 30%
#define FULL_TOLERANCE_RATIO (1.0f + 0.35f)	// 30%

#define MID_REFERENCE_VALUE() 	(72)		// t*1.6
#define HALF_LOWER_LIMIT(t)		(t * HALF_TOLERANCE_RATIO)
#define FULL_UPPER_LIMIT(t)		(t * FULL_TOLERANCE_RATIO)

#define ZERO_VALUE	(64)
#define ONE_VALUE	(80)

#define IS_FULL_BIT(t_us) \
		((t_us) > HALF_LOWER_LIMIT(ZERO_VALUE)) && ((t_us) < FULL_UPPER_LIMIT(ONE_VALUE))

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static h10301_Decoder_t g_h10301_dec;
static fsk_symbol_state_t sym_st;
static fsk_bit_state_t    bit_st;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t* protocol_h10301_get_data(void* proto);
void protocol_h10301_decoder_begin(void* proto);
bool protocol_h10301_decoder_execute(void* proto, uint16_t size);
bool protocol_h10301_encoder_begin(void* proto);
void protocol_h10301encoder_send(void* proto);
void protocol_h10301_write_begin(void* protocol, void *data);
void protocol_h10301_write_send(void* proto);
void protocol_h10301_render_data(void* protocol, char* result);

//************************** C O N S T A N T **********************************/

const LFRFIDProtocolBase protocol_h10301 = {
    .name = "H10301",
    .manufacturer = "HID",
    .data_size = H10301_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK,
    .get_data = (lfrfidProtocolGetData)protocol_h10301_get_data,
    .decoder =
    {
        .begin = (lfrfidProtocolDecoderBegin)protocol_h10301_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)protocol_h10301_decoder_execute,
    },
    .encoder =
    {
        .begin = (lfrfidProtocolEncoderBegin)protocol_h10301_encoder_begin,
        .send = (lfrfidProtocolEncoderSend)protocol_h10301encoder_send,
    },
    .write =
    {
        .begin = (lfrfidProtocolWriteBegin)protocol_h10301_write_begin,
        .send = (lfrfidProtocolWriteSend)protocol_h10301_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)protocol_h10301_render_data,
    //.write_data = (lfrfidProtocolWriteData)protocol_h10301_write_data,
};


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief Decoder 구조 초기화
  * @param
  * @retval
  */
/*============================================================================*/
void h10301_decoder_init(h10301_Decoder_t *dec)
{
    if (!dec) return;
    dec->word[0] = dec->word[1] = dec->word[2] = 0;
    dec->bit_count = 0;
    //dec->bit_test = 0;
}


/*============================================================================*/
/**
  * @brief Decoder에 1bit push (MSB→LSB shift)
  * @param
  * @retval
  */
/*============================================================================*/
void h10301_decoder_push_bit(h10301_Decoder_t *dec, uint8_t bit)
{
    if (!dec) return;

    bit &= 1U;

    dec->word[0] = (dec->word[0] << 1) | ((dec->word[1] >> 31) & 1);
    dec->word[1] = (dec->word[1] << 1) | ((dec->word[2] >> 31) & 1);
    dec->word[2] = (dec->word[2] << 1) | bit;

    //dec->bit_test++;
    if (dec->bit_count < H10301_DECODER_BITS)
        dec->bit_count++;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t abs_u16(int32_t v)
{
    return (v < 0) ? -v : v;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool within_pct(uint16_t v, uint16_t target, uint16_t pct)
{
    uint16_t tol = target * pct / 100U;
    return abs_u16((int32_t)v - target) <= tol;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void fsk_symbol_state_init(fsk_symbol_state_t *st)
{
    if (!st) return;
    st->has_prev = false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool fsk_symbol_feed(fsk_symbol_state_t *st,
                     const lfrfid_evt_t *half,
                     uint8_t *out_symbol)
{
    if (!st || !half || !out_symbol) return false;

    if (!st->has_prev) {

        st->prev_half = *half;
        st->has_prev = true;
        return false;
    }

    lfrfid_evt_t h0 = st->prev_half;
    lfrfid_evt_t h1 = *half;

    if (h0.edge == h1.edge) {
         st->prev_half = *half;
        st->has_prev = true;
        return false;
    }

    uint16_t dt0 = h0.t_us;
    uint16_t dt1 = h1.t_us;
    uint16_t period = (uint16_t)(dt0 + dt1);
#if 1
    bool near_zero = within_pct(period, PERIOD_ZERO_US, PERIOD_TOL_PCT);
    bool near_one  = within_pct(period, PERIOD_ONE_US,  PERIOD_TOL_PCT);

    if (!near_zero && !near_one) {
        st->prev_half = *half;
        st->has_prev  = true;
        return false;
    }

    if (near_zero && near_one) {
        uint16_t d0 = abs_u16(period - PERIOD_ZERO_US);
        uint16_t d1 = abs_u16(period - PERIOD_ONE_US);
        near_zero = (d0 <= d1);
        near_one  = !near_zero;
    }

    *out_symbol = near_one ? 1 : 0;
#else
    st->prev_half = *half;

    if(IS_FULL_BIT(period))
    {
    	if(period>MID_REFERENCE_VALUE())
    		 *out_symbol = 1;
    	else
    		 *out_symbol = 0;
    }
#endif

    st->has_prev = false;

    return true;
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void fsk_bit_state_init(fsk_bit_state_t *st)
{
    if (!st) return;
    st->count = 0;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool fsk_bit_feed(fsk_bit_state_t *st,
                  uint8_t symbol,
                  uint8_t *out_bit)
{
    if (!st || !out_bit) return false;

    if (st->count >= sizeof(st->buf)) {
        st->count = 0;
    }

    st->buf[st->count++] = (symbol ? 1 : 0);

    uint8_t cnt1 = 0;
    uint8_t cnt0 = 0;
    for (uint8_t i = 0; i < st->count; i++) {
        if (st->buf[i] == 1) cnt1++;
        else                 cnt0++;
    }

    uint8_t total = st->count;

    if (cnt1 >= 5 ||
        (total >= 5 && cnt1 >= 4 && cnt0 <= 1))
    {
        *out_bit = 1;
        st->count = 0;
        return true;
    }

    if (cnt0 >= 6 ||
        (total >= 6 && cnt0 >= 5 && cnt1 <= 1))
    {
        *out_bit = 0;
        st->count = 0;
        return true;
    }

    return false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool decode_symbol_pair(uint8_t p, uint8_t *out)
{
    if (p == 0b01) { *out = 0; return true; }
    if (p == 0b10) { *out = 1; return true; }
    return false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool decode_payload_26bits(const uint32_t *frame, uint32_t *out)
{
    uint32_t r = 0;

    for (int i = 9; i >= 0; i--) {
        uint8_t p = (frame[1] >> (2 * i)) & 3;
        uint8_t b;
        if (!decode_symbol_pair(p, &b)) return false;
        r = (r << 1) | b;
    }
    for (int i = 15; i >= 0; i--) {
        uint8_t p = (frame[2] >> (2 * i)) & 3;
        uint8_t b;
        if (!decode_symbol_pair(p, &b)) return false;
        r = (r << 1) | b;
    }

    *out = r;
    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool h10301_parity_check(uint32_t raw26)
{
    uint8_t p = 0;

    for (int i = 0; i < 13; i++)
        if ((raw26 >> i) & 1) p++;
    if ((p & 1) != 1) return false;

    p = 0;
    for (int i = 13; i < 26; i++)
        if ((raw26 >> i) & 1) p++;
    if ((p & 1) == 1) return false;

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool h10301_is_valid(const uint32_t *frame)
{
    if (!frame) return false;

    const uint8_t *raw = (const uint8_t *)frame;

    if (raw[3] != 0x1D) return false;

    if (((frame[0] >> 10) & 0x3FFF) != 0x1556) return false;

    uint32_t fmt = ((frame[0] & 0x3FF) << 12) |
                   ((frame[1] >> 20) & 0xFFF);
    if (fmt != 0x155556) return false;

    uint32_t raw26;
    if (!decode_payload_26bits(frame, &raw26)) return false;
    if (!h10301_parity_check(raw26)) return false;

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool h10301_extract_raw26(const uint32_t *frame, uint32_t *out_raw26)
{
    if (!frame || !out_raw26) return false;
    if (!h10301_is_valid(frame)) return false;

    uint32_t raw26;
    if (!decode_payload_26bits(frame, &raw26)) return false;
    //if (!h10301_parity_check(raw26)) return false;

    *out_raw26 = raw26;
    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool h10301_extract_fields(uint32_t raw26,
                           uint8_t *fc,
                           uint16_t *cn)
{
    if (!h10301_parity_check(raw26)) return false;

    if (fc)
        *fc = (raw26 >> 17) & 0xFF;

    if (cn)
        *cn = (raw26 >> 1) & 0xFFFF;

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool h10301_decoder_execute(void* proto, uint16_t size, void* dec)
{
	lfrfid_evt_t* evt = (lfrfid_evt_t*)proto;

    for (int i = 0; i < size; i++) {
        uint8_t symbol;

        if (fsk_symbol_feed(&sym_st, &evt[i], &symbol)) {

            uint8_t bit;
            if (fsk_bit_feed(&bit_st, symbol, &bit)) {

            	h10301_decoder_push_bit(dec, bit);

                 if (h10301_decoder_is_full(dec)) {
                    const uint32_t *frame = h10301_decoder_frame(dec);

                    if (h10301_is_valid(frame)) {
                        uint32_t raw26;

                        if (h10301_extract_raw26(frame, &raw26)) {
                            uint8_t  facility;
                            uint16_t card;

                            if (h10301_extract_fields(raw26, &facility, &card)) {

#if 1
                                //M1_LOG_I("RFID","HID H10301: FC=%u, Card=%u\n", facility, card);

                                lfrfid_tag_info.uid[0] = facility;
                                lfrfid_tag_info.uid[1] = HIBYTE(card);
                                lfrfid_tag_info.uid[2] = LOBYTE(card);

                        	  	//lfrfid_tag_info.bitrate = g_decoder.detected_half_bit_us/4;
                                return true;
                          	   	//m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_LFRFID_TAG_DETECTED);
#endif
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}


/*============================================================================*/
/*
 * This function handles h10301 encoder
 */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t get_bit(const uint8_t *buf, int bit_index)
{
    int byte_pos    = bit_index >> 3;       // /8
    int bit_in_byte = 7 - (bit_index & 7);  // MSB-first
    return (buf[byte_pos] >> bit_in_byte) & 1u;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void set_bit(uint8_t *buf, int bit_index, uint8_t val)
{
    int byte_pos    = bit_index >> 3;
    int bit_in_byte = 7 - (bit_index & 7);

    if (val)
        buf[byte_pos] |= (uint8_t)(1u << bit_in_byte);
    else
        buf[byte_pos] &= (uint8_t)~(1u << bit_in_byte);
}


/*============================================================================*/
/**
  * @brief 12bit parity
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t parity_even12(uint16_t v12)
{
    uint8_t p = 0;
    for (int i = 0; i < 12; i++)
        p ^= (v12 >> i) & 1u;
    return p;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t parity_odd12(uint16_t v12)
{
    return (uint8_t)(parity_even12(v12) ^ 1u);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void uid24_to_hid26_bytes(const uint8_t *uid_bytes, int uid_len,
                                 uint8_t out26[4])
{
    memset(out26, 0, 4);

    uint32_t b0 = (uid_len > 0 ? uid_bytes[0] : 0);
    uint32_t b1 = (uid_len > 1 ? uid_bytes[1] : 0);
    uint32_t b2 = (uid_len > 2 ? uid_bytes[2] : 0);

    uint16_t uid_high =
          ((b0 << 4) | (b1 >> 4))
        & 0x0FFF;

    uint16_t uid_low =
          (((b1 & 0x0F) << 8) | b2)
        & 0x0FFF;

    uint8_t ep = parity_even12(uid_high);
    uint8_t op = parity_odd12(uid_low);

    uint32_t w26 = 0;

    w26 |= (uint32_t)ep         << 25;
    w26 |= (uint32_t)uid_high   << 13;
    w26 |= (uint32_t)uid_low    <<  1;
    w26 |= (uint32_t)op;

    uint32_t tmp = w26 << 6;

    out26[0] = (tmp >> 24) & 0xFF;
    out26[1] = (tmp >> 16) & 0xFF;
    out26[2] = (tmp >>  8) & 0xFF;
    out26[3] =  tmp        & 0xFF;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void make_header44_bytes(uint8_t preamble8,
                         uint16_t company14,
                         uint32_t cardfmt22,
                         uint8_t header44[6])
{
    memset(header44, 0, 6);

    company14 &= 0x3FFF;   // 14bit
    cardfmt22 &= 0x3FFFFF; // 22bit

    // bit43..bit36 : preamble8
    header44[0] = preamble8;

    // bit35..bit22 : company14 (14bit)
    //  - header44[1] = company[13:6]
    //  - header44[2] upper 6bit = company[5:0]
    header44[1] = (uint8_t)(company14 >> 6);          // company[13:6]
    header44[2] = (uint8_t)((company14 & 0x3F) << 2); // company[5:0] << 2

    // bit21..bit0 : cardfmt22 (22bit)
    //   header44[2] lower 2bit = cardfmt[21:20]
    //   header44[3] = cardfmt[19:12]
    //   header44[4] = cardfmt[11:4]
    //   header44[5] upper 4bit = cardfmt[3:0], lower 4bit = 0
    header44[2] |= (uint8_t)(cardfmt22 >> 20);          // cardfmt[21:20]

    header44[3] = (uint8_t)((cardfmt22 >> 12) & 0xFF);  // cardfmt[19:12]
    header44[4] = (uint8_t)((cardfmt22 >>  4) & 0xFF);  // cardfmt[11:4]

    header44[5] = (uint8_t)((cardfmt22 & 0x0F) << 4);   // cardfmt[3:0] → upper 4bit
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void manchester_encode_bits(const uint8_t *in, int in_bits,
                            uint8_t *out, int out_size)
{
    memset(out, 0, out_size);

    int out_bit = 0;

    for (int i = 0; i < in_bits; ++i) {
        uint8_t b  = get_bit(in, i);
        uint8_t m0 = b ? 1 : 0;
        uint8_t m1 = b ? 0 : 1;

        set_bit(out, out_bit++, m0);
        set_bit(out, out_bit++, m1);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void combine_header_and_credential(const uint8_t header44[6],
                                   const uint8_t man52[7],
                                   uint8_t out12[12])
{
    memset(out12, 0, 12);

    // 1) Header 44bit
    for (int i = 0; i < 44; ++i) {
        uint8_t bit = get_bit(header44, i);
        set_bit(out12, i, bit);
    }

    // 2) Manchester 52bit
    int out_bit = 44;
    for (int i = 0; i < 52; ++i) {
        uint8_t bit = get_bit(man52, i);
        set_bit(out12, out_bit++, bit);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void uid_bytes_to_h10301_raw96_bytes(const uint8_t *uid_bytes, int uid_len,
                                     uint8_t preamble8,
                                     uint16_t company14,
                                     uint32_t format22,
                                     uint8_t raw96_12[12])
{
    uint8_t data26[4];
    uid24_to_hid26_bytes(uid_bytes, uid_len, data26);

    uint8_t header44[6];
    make_header44_bytes(preamble8, company14, format22, header44);

    uint8_t man52[7];
    manchester_encode_bits(data26, 26, man52, sizeof(man52));

    combine_header_and_credential(header44, man52, raw96_12);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t get_bit_msb(const uint8_t *buf, int bit_index)
{
    int byte_pos    = bit_index >> 3;       // bit_index / 8
    int bit_in_byte = 7 - (bit_index & 7);  // MSB-first
    return (buf[byte_pos] >> bit_in_byte) & 1u;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
int h10301_raw96_to_wave(const uint8_t raw96[12],
                         uint8_t gpio_pin,           /* GPIO pin number (0~15) */
						 Encoded_Data_t *steps,
                         size_t max_steps,
                         size_t *out_step_count)
{
    size_t idx = 0;

    uint32_t bsrr_set   = 1u << gpio_pin;        // HIGH
    uint32_t bsrr_reset = 1u << (gpio_pin + 16); // LOW

    for (int bit = 0; bit < 96; ++bit)
    {
        uint8_t b = get_bit_msb(raw96, bit);

        uint16_t half_us,period_us;

        int repeat;

        if (b == 0) {
        	half_us = EMUL_HALF_ZERO_US;
        	period_us = (EMUL_PERIOD_ZERO_US - half_us);  // (LOW 32, HIGH 32) × 6
            repeat  = 6;   // 12 step
        } else {
        	half_us = EMUL_HALF_ONE_US;
        	period_us = (EMUL_PERIOD_ONE_US - half_us);  // (LOW 32, HIGH 32) × 6
            repeat  = 5;   // 10 step
        }

        for (int i = 0; i < repeat; ++i)
        {
#if OUTPUT_INVERT	// invert
            /* LOW  */
            if (idx >= max_steps) goto overflow;
            steps[idx].bsrr    = bsrr_reset;
            steps[idx].time_us = half_us;
            idx++;

            /* HIGH  */
            if (idx >= max_steps) goto overflow;
            steps[idx].bsrr    = bsrr_set;
            steps[idx].time_us = period_us;
            idx++;
#else
            /* LOW  */
            if (idx >= max_steps) goto overflow;
            steps[idx].bsrr    = bsrr_set;
            steps[idx].time_us = half_us;
            idx++;

            /* HIGH  */
            if (idx >= max_steps) goto overflow;
            steps[idx].bsrr    = bsrr_reset;
            steps[idx].time_us = period_us;
            idx++;
#endif
        }
    }

    if (out_step_count) *out_step_count = idx;
    return 0;

overflow:
    if (out_step_count) *out_step_count = idx;
    return -1;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_h10301_decoder_begin(void* proto)
{
	h10301_decoder_init(&g_h10301_dec);

    fsk_symbol_state_init(&sym_st);
    fsk_bit_state_init(&bit_st);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool protocol_h10301_decoder_execute(void* proto, uint16_t size)
{
	lfrfid_evt_t* p = (lfrfid_evt_t*)proto;
	return h10301_decoder_execute(p, size, &g_h10301_dec);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool protocol_h10301_encoder_begin(void* proto)
{
    uint8_t output_raw[12];

    uint8_t uid[3] = { 0, };
    int     uid_len = 3;
    memcpy(uid,lfrfid_tag_info.uid,sizeof(uid));

    uint8_t  Preamble  = 0x1D;      	// Preamble(8bit)
    uint16_t Company = 0x1556;    		// Company(14bit )
    uint32_t CardFormat  = 0x155556;	// CardFormat(22bit )

    uid_bytes_to_h10301_raw96_bytes(uid, uid_len, Preamble, Company, CardFormat, output_raw);

    h10301_raw96_to_wave(output_raw, 2, lfrfid_encoded_data.data, 1200, (size_t*)&lfrfid_encoded_data.length);

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_h10301encoder_send(void* proto)
{
	lfrfid_encoded_data.index = 0;
	//lfrfid_emul_length = 128;	// modify
	lfrfid_emul_hw_init();

}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_h10301_render_data(void* protocol, char* result)
{
	uint8_t* data = protocol_h10301_get_data(NULL);

    sprintf(
        result,
		"Hex: %02X %02X %02X\n"
        "FC: %03u\n"
		"Card: %05hu",
		data[0],data[1],data[2],
        data[0],
       	MAKEWORD(data[2],data[1]));
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t* protocol_h10301_get_data(void* proto)
{
    return lfrfid_tag_info.uid;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_h10301_write_begin(void* protocol, void *data)
{
	LFRFID_TAG_INFO* tag_data = (LFRFID_TAG_INFO*)protocol;
	LFRFIDProgram* write = (LFRFIDProgram*)data;

	uint8_t encoded_data[12];
    uint8_t uid[3] = { 0, };
    int     uid_len = 3;
    memcpy(uid,tag_data->uid,sizeof(uid));

    uint8_t  Preamble  = 0x1D;      	// Preamble(8bit)
    uint16_t Company = 0x1556;    		// Company(14bit )
    uint32_t CardFormat  = 0x155556;	// CardFormat(22bit )

    uid_bytes_to_h10301_raw96_bytes(uid, uid_len, Preamble, Company, CardFormat, encoded_data);

#if 1
	if(write){
		if(write->type == LFRFIDProgramTypeT5577) {
			write->t5577.block_data[0] = (T5577_MOD_FSK2a | T5577_BITRATE_RF_50| T5577_TRANS_BL_1_3);
			bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded_data, &write->t5577.block_data[1], 3);
			write->t5577.max_blocks = 4;
			//result = true;
		}
	}
#endif

}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_h10301_write_send(void* proto)
{

	t5577_execute_write(lfrfid_program, 0);
}
