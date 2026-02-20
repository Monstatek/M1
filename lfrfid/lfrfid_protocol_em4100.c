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
#include <stddef.h>
#include <math.h>

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "uiview.h"

#include "lfrfid.h"

/*************************** D E F I N E S ************************************/
#define EMUL_EM4100_CORR	(3)

#define OUTPUT_INVERT	0

#define M1_LOGDB_TAG	"RFID"

// --- 헬퍼 매크로 ---
//HALF_LOW  ---- HALF ---- HALF_HIGH (= MID) ---- FULL ---- FULL_HIGH
//                  \                     /
//                   \                   /
//                      MID_REFERENCE

#define HALF_TOLERANCE_RATIO (1.0f - 0.75f)	// 30%
#define FULL_TOLERANCE_RATIO (1.0f + 0.30f)	// 30%
#define MID_TOLERANCE_RATIO  (1.0f + 0.60f) // 30%
#define MID_TOLERANCE_RATIOx (1.0f + 0.20f) // 30%

#define MID_REFERENCE_VALUE(t) 	(t * MID_TOLERANCE_RATIO)		// t*1.6
#define HALF_LOWER_LIMIT(t)		(t * HALF_TOLERANCE_RATIO)		// t*0.7
#define FULL_UPPER_LIMIT(t)		((2 * t) * FULL_TOLERANCE_RATIO)// 2t*1.3
#define MID_REFERENCE_VALUEx(t) (t * MID_TOLERANCE_RATIOx)		// t*1.6

#define IS_HALF_BIT(t_us, base_us) \
		((t_us) > HALF_LOWER_LIMIT(base_us)) && ((t_us) < MID_REFERENCE_VALUE(base_us)) // ((t_us) > (base_us * HALF_TOLERANCE_RATIO)) && ((t_us) < (base_us / HALF_TOLERANCE_RATIO))

#define IS_FULL_BIT(t_us, base_us) \
		((t_us) > MID_REFERENCE_VALUE(base_us)) && ((t_us) < FULL_UPPER_LIMIT(base_us))//((t_us) > ((2 * base_us) * FULL_TOLERANCE_RATIO)) && ((t_us) < ((2 * base_us) / FULL_TOLERANCE_RATIO))

#define IS_FULL_BITx(t_us, base_us) \
		((t_us) > MID_REFERENCE_VALUEx(base_us)) && ((t_us) < FULL_UPPER_LIMIT(base_us))//((t_us) > ((2 * base_us) * FULL_TOLERANCE_RATIO)) && ((t_us) < ((2 * base_us) / FULL_TOLERANCE_RATIO))


#define EM4100_MAX_STEPS   (64 * 2)

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

typedef enum {
    DEC_RESET_PARTIAL = 0,
    DEC_RESET_FULL = 1 << 0,
    DEC_RESET_KEEP_TIMING = 1 << 1,
} dec_reset_mode_t;

/***************************** V A R I A B L E S ******************************/

static EM4100_Decoder_t g_em4100_dec;
static EM4100_Decoder_t g_em4100_32_dec;
static EM4100_Decoder_t g_em4100_16_dec;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool Check_Even_Parity(const uint8_t* data_bits, uint8_t length);
static uint8_t GetBitFromFrame(EM4100_Decoder_t* dec, uint8_t bit_index);
static bool em4100_decoder_execute(void* proto, uint16_t size, void* dec);

static uint8_t* protocol_em4100_get_data(void* proto);
void protocol_em4100_decoder_begin(void* proto);
bool protocol_em4100_decoder_execute(void* proto, uint16_t size);
bool protocol_em4100_encoder_begin(void* proto);
void protocol_em4100_encoder_send(void* proto);
void protocol_em4100_write_begin(void* protocol, void *data);
void protocol_em4100_write_send(void* proto);
void protocol_em4100_render_data(void* protocol, char *result);
void protocol_em4100_32_decoder_begin(void* proto);
bool protocol_em4100_32_decoder_execute(void* proto, uint16_t size);
void protocol_em4100_16_decoder_begin(void* proto);
bool protocol_em4100_16_decoder_execute(void* proto, uint16_t size);

//************************** C O N S T A N T **********************************/

const LFRFIDProtocolBase protocol_em4100 = {
    .name = "EM4100",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK,
    .get_data = (lfrfidProtocolGetData)protocol_em4100_get_data,
    .decoder =
    {
        .begin = (lfrfidProtocolDecoderBegin)protocol_em4100_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)protocol_em4100_decoder_execute,
    },
    .encoder =
    {
        .begin = (lfrfidProtocolEncoderBegin)protocol_em4100_encoder_begin,
        .send = (lfrfidProtocolEncoderSend)protocol_em4100_encoder_send,
    },
    .write =
    {
        .begin = (lfrfidProtocolWriteBegin)protocol_em4100_write_begin,
        .send = (lfrfidProtocolWriteSend)protocol_em4100_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)protocol_em4100_render_data,
    //.write_data = (lfrfidProtocolWriteData)protocol_em4100_write_data,
};

const LFRFIDProtocolBase protocol_em4100_32 = {
    .name = "EM4100/32",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK,
    .get_data = (lfrfidProtocolGetData)protocol_em4100_get_data,
    .decoder =
    {
       .begin = (lfrfidProtocolDecoderBegin)protocol_em4100_32_decoder_begin,
       .execute = (lfrfidProtocolDecoderExecute)protocol_em4100_32_decoder_execute,
    },
    .encoder =
    {
        .begin = (lfrfidProtocolEncoderBegin)protocol_em4100_encoder_begin,
        .send = (lfrfidProtocolEncoderSend)protocol_em4100_encoder_send,
    },
    .write =
    {
        .begin = (lfrfidProtocolWriteBegin)protocol_em4100_write_begin,
        .send = (lfrfidProtocolWriteSend)protocol_em4100_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)protocol_em4100_render_data,
    //.write_data = (lfrfidProtocolWriteData)protocol_em4100_write_data,
};

const LFRFIDProtocolBase protocol_em4100_16 = {
    .name = "EM4100/16",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK,
    .get_data = (lfrfidProtocolGetData)protocol_em4100_get_data,
    .decoder =
    {
        .begin = (lfrfidProtocolDecoderBegin)protocol_em4100_16_decoder_begin,
        .execute = (lfrfidProtocolDecoderExecute)protocol_em4100_16_decoder_execute,
    },
    .encoder =
    {
        .begin = (lfrfidProtocolEncoderBegin)protocol_em4100_encoder_begin,
        .send = (lfrfidProtocolEncoderSend)protocol_em4100_encoder_send,
    },
    .write =
    {
        .begin = (lfrfidProtocolWriteBegin)protocol_em4100_write_begin,
        .send = (lfrfidProtocolWriteSend)protocol_em4100_write_send,
    },
    .render_data = (lfrfidProtocolRenderData)protocol_em4100_render_data,
    //.write_data = (lfrfidProtocolWriteData)protocol_em4100_write_data,
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

#if 0
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void setEm4100_bitrate(int bitrate)
{
	//g_em4100_dec.detected_half_bit_us = bitrate;
}
#endif


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void EM4100_Decoder_Init_Full(EM4100_Decoder_t* dec)
{
	memset(dec->frame_buffer, 0, FRAME_BUFFER_BYTES);
	dec->bit_count = 0;
	//dec->state = DECODER_STATE_IDLE;
    //g_decoder.sync_bit_count = 0;
	//dec->bit_test = 0;
	dec->edge_count = 0;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void EM4100_Decoder_Init_Partial(EM4100_Decoder_t* dec)
{
	memset(dec->frame_buffer, 0, FRAME_BUFFER_BYTES);
	dec->bit_count = 0;
	//dec->state = DECODER_STATE_IDLE;
    //g_decoder.sync_bit_count = 0;
	//dec->bit_test = 0;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool em4100_extract_fields(EM4100_Decoder_t* dec)
{
    // TODO:
	bool valid = true;
	uint8_t temp_bits[11];

	if (valid) {
	    // EM4100 Frame successfully decoded and verified.
	    // TODO:
#if 1	// data parsing
	  	memset(temp_bits, 0, 10);

        for (uint8_t col = 0; col < 10; col++) {
            for (uint8_t row = 0; row < 4; row++) {
              	temp_bits[col] |= GetBitFromFrame(dec, 9 + (col * 5) + row)<<(3-row); // temp_bits[0] ~ temp_bits[3]
            }
        }

        for(int i = 0; i<5; i++)
        {
         	lfrfid_tag_info.uid[i] = MAKEBYTE(temp_bits[i<<1], temp_bits[(i<<1)+1]);
        }

	  	lfrfid_tag_info.bitrate = dec->detected_half_bit_us/4;
#endif


	 } else {
	        // EM4100 Frame verification failed.

	 }
	return valid;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t manchester_symbol_feed(lfrfid_evt_t* stream, lfrfid_evt_t* stream2,uint8_t count, uint16_t Th_us)
{
    uint8_t output_count = 0;

    for (uint8_t i = 0; i < count; ++i) {
        lfrfid_evt_t current_evt = stream2[i];

           if (IS_FULL_BIT(current_evt.t_us, Th_us)) {

        	stream[output_count].t_us = Th_us;
        	stream[output_count].edge = current_evt.edge;
            output_count++;

            stream[output_count].t_us = Th_us;
            stream[output_count].edge = current_evt.edge;
            output_count++;

        } else {

            //if (i != output_count) {
            	stream[output_count] = current_evt;
            //}
            output_count++;
        }
    }
    return output_count;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void manchester_bit_feed(EM4100_Decoder_t* dec, lfrfid_evt_t* e1, lfrfid_evt_t* e2, uint8_t* bit)
{
	uint16_t T_h = dec->detected_half_bit_us;
    //uint8_t bit = 2;

    bool is_valid_pattern = IS_HALF_BIT(e1->t_us, T_h) && IS_HALF_BIT(e2->t_us, T_h);

    if (is_valid_pattern) {
        if (e1->edge == 0 && e2->edge == 1) {
        	if(IS_FULL_BITx((e1->t_us+e2->t_us), T_h))
        		*bit = 1;
        }
        else if (e1->edge == 1 && e2->edge == 0) {
        	if(IS_FULL_BITx((e1->t_us+e2->t_us), T_h))
        		*bit = 0;
        }
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void bit_stream_push(EM4100_Decoder_t* dec, uint8_t bit)
{
    uint8_t carry = 0;
    uint8_t new_carry;

    for (int i = 7; i >= 0; i--) {
        new_carry = (dec->frame_buffer[i] >> 7) & 1;
        dec->frame_buffer[i] <<= 1;
        dec->frame_buffer[i] |= carry;
        carry = new_carry;
    }

    dec->frame_buffer[7] &= 0xFE;
    dec->frame_buffer[7] |= bit;

    if (dec->bit_count < FRAME_BITS)
        dec->bit_count++;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t bit_stream_get(uint8_t *buf, uint16_t index)
{
    return (buf[index / 8] >> (7 - (index % 8))) & 1;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

static inline bool decoder_is_full(const EM4100_Decoder_t *dec) {
    return dec && (dec->bit_count >= FRAME_BITS);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool em4100_is_valid(const EM4100_Decoder_t *dec)
{
	uint8_t* frame = dec->frame_buffer;

    if (!frame) return false;

    //const uint8_t *raw = (const uint8_t *)frame;
    const uint16_t preamble =  ((uint16_t)frame[0]<<1) + ((frame[1]>>7) & 1);

    if (preamble != 0b111111111) return false;

    // parity check
    // TODO:
	bool valid = true;
	uint8_t temp_bits[11];

	for (uint8_t row = 0; row < 4; row++) {

	    for (uint8_t col = 0; col < 11; col++) {

	        uint8_t bit_index = 9 + (col * 5) + row;
	        temp_bits[col] = GetBitFromFrame(dec, bit_index);
	    }

	    if (Check_Even_Parity(temp_bits, 11)) {
	    	valid = false;
	        break;
	    }
	}

	if (valid) {
	    for (uint8_t col = 0; col < 10; col++) {

	        memset(temp_bits, 0, 5);

	        for (uint8_t row = 0; row < 5; row++) {

	            temp_bits[row] = GetBitFromFrame(dec, 9 + (col * 5) + row); // temp_bits[0] ~ temp_bits[3]
	        }

	        if (Check_Even_Parity(temp_bits, 5)) {
	        	valid = false;
	            break;
	        }
	    }
	}

	if (valid) {
	   if (GetBitFromFrame(dec, 63) != 0) {
	      valid = false;
	   }
	}

    return valid;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool em4100_decoder_execute(void* proto, uint16_t size, void* dec)
{
	lfrfid_evt_t temp_stream[FRAME_CHUNK_SIZE];
    uint8_t normalized_count;
    lfrfid_evt_t* new_stream = (lfrfid_evt_t*)proto;
    EM4100_Decoder_t *pdec = (EM4100_Decoder_t*)dec;

    //if (g_decoder.state != DECODER_STATE_IDLE && g_decoder.detected_half_bit_us != 0) {
    normalized_count = manchester_symbol_feed(temp_stream, new_stream, size, pdec->detected_half_bit_us);
    //}
    //p = &temp_stream[0];

    if (pdec->edge_count + normalized_count > sizeof(pdec->edge_buffer) / sizeof(lfrfid_evt_t)) {
        EM4100_Decoder_Init_Full(pdec);
        return false;
    }

    //if(g_decoder.edge_count > 1)
    //	M1_LOG_I(M1_LOGDB_TAG,"g_decoder.edge_count=%d, %d\r\n",g_decoder.edge_count ,normalized_count);
#if 1
    memcpy(&pdec->edge_buffer[pdec->edge_count], temp_stream, normalized_count * sizeof(lfrfid_evt_t));
    pdec->edge_count += normalized_count;
#endif

    uint8_t consumed_idx = 0;

    while (pdec->edge_count - consumed_idx >= 2)
    {
        lfrfid_evt_t* e1 = &pdec->edge_buffer[consumed_idx];
        lfrfid_evt_t* e2 = &pdec->edge_buffer[consumed_idx + 1];
        consumed_idx += 2;

       	uint8_t bit = 2;
       	manchester_bit_feed(pdec, e1, e2, &bit);

        if (bit != 2) {
           	bit_stream_push(pdec,bit);

           	if(decoder_is_full(pdec))
          	{
           		if(em4100_is_valid(pdec))
           		{
           			em4100_extract_fields(pdec);
           			return true;
           			//m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_LFRFID_TAG_DETECTED);
           		}
           	}
        } else {

          	//EM4100_Decoder_Init_Partial();

            //if(bit == 2 && is_valid_pattern)
           	if(bit == 2)
           	{
               	consumed_idx -= 1;
               	EM4100_Decoder_Init_Partial(pdec);
            }
        }
    } // end while

#if 1
    uint8_t remaining_edges;
    if(pdec->edge_count && (pdec->edge_count >= consumed_idx))
    {
    	remaining_edges = pdec->edge_count - consumed_idx;

		if (consumed_idx > 0) {

			if((FRAME_CHUNK_SIZE-consumed_idx) > remaining_edges)
			{
				//M1_LOG_I(M1_LOGDB_TAG, "1->edge_count=%d, consumed_idx=%d, remaining_edges=%d normalized_count=%d\r\n", g_decoder.edge_count,consumed_idx,remaining_edges,normalized_count);

				memmove(pdec->edge_buffer, &pdec->edge_buffer[consumed_idx], remaining_edges * sizeof(lfrfid_evt_t));
				pdec->edge_count = remaining_edges;
			}
		}
	}
    else
    {
    	//M1_LOG_I(M1_LOGDB_TAG, "2->edge_count=%d, consumed_idx=%d, remaining_edges=%d normalized_count=%d\r\n", g_decoder.edge_count,consumed_idx,remaining_edges,normalized_count);
    }
#endif
    return false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static bool Check_Even_Parity(const uint8_t* data_bits, uint8_t length)
{
    uint8_t parity = data_bits[0];

    for (uint8_t i = 1; i < length; i++) {
        parity = parity ^ data_bits[i];
    }

    return (parity);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t GetBitFromFrame(EM4100_Decoder_t*dec, uint8_t bit_index)
{
    uint8_t byte_idx = bit_index / 8;

    uint8_t bit_idx = bit_index % 8;

    if (dec->frame_buffer[byte_idx] & (1 << (7 - bit_idx))) {
        return 1;
    } else {
        return 0;
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void em4100_uid_bytes_to_nibbles(const uint8_t *uid_bytes,
                                 uint8_t uid_len,
                                 uint8_t out_nibs[10])
{
    memset(out_nibs, 0, 10);

    if (uid_bytes == NULL || uid_len == 0)
        return;

    if (uid_len > 5)
        uid_len = 5;

    int nib_index = 9;
    for (int i = uid_len - 1; i >= 0 && nib_index >= 1; i--)
    {
        uint8_t b = uid_bytes[i];

        out_nibs[nib_index--] = (b & 0x0F);       // low nibble
        out_nibs[nib_index--] = (b >> 4) & 0x0F;  // high nibble
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static inline void set_bit(uint8_t frame[8], uint16_t bitpos, uint8_t bit)
{
    uint16_t byte = bitpos >> 3;      // bitpos / 8
    uint8_t  bpos = 7 - (bitpos & 7); // MSB first

    if (bit)
        frame[byte] |=  (1U << bpos);
    else
        frame[byte] &= ~(1U << bpos);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void em4100_build_frame8(uint8_t frame[8], const uint8_t uid_nibs[10])
{
    uint8_t col_ones[4] = {0};
    uint16_t bitpos = 0;

    memset(frame, 0, 8);

    for (int i = 0; i < 9; i++)
        set_bit(frame, bitpos++, 1);

    for (int row = 0; row < 10; row++)
    {
        uint8_t nib = uid_nibs[row] & 0x0F;
        uint8_t row_ones = 0;

        for (int b = 0; b < 4; b++)
        {
            uint8_t bit = (nib >> (3 - b)) & 1;
            set_bit(frame, bitpos++, bit);

            if (bit)
            {
                row_ones++;
                col_ones[b]++;
            }
        }

        //uint8_t p = (row_ones & 1) ? 0 : 1; // row odd parity
        uint8_t p = (row_ones & 1);	// row even parity
        set_bit(frame, bitpos++, p);
    }

    for (int c = 0; c < 4; c++)
    {
        //uint8_t p = (col_ones[c] & 1) ? 0 : 1;
    	uint8_t p = (col_ones[c] & 1);
        set_bit(frame, bitpos++, p);
    }

    set_bit(frame, bitpos++, 0);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void em4100_build_frame8_from_uid(uint8_t frame8[8],
                                  const uint8_t *uid_bytes,
                                  uint8_t uid_len)
{
    uint8_t nibs[10];
    em4100_uid_bytes_to_nibbles(uid_bytes, uid_len, nibs);
    em4100_build_frame8(frame8, nibs);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

static uint8_t em4100_frame_get_bit(const uint8_t frame[8], uint16_t bit_index)
{
    uint16_t byte = bit_index >> 3;      // /8
    uint8_t  bpos = 7 - (bit_index & 7); // MSB-first
    return (frame[byte] >> bpos) & 0x01;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint16_t em4100_build_manchester_wave(
        const uint8_t frame8[8],
		Encoded_Data_t *steps,
        uint16_t max_steps,
        uint16_t half_bit_us,
        uint8_t  gpio_pin,
        uint8_t  start_level)
{
    if (!frame8 || !steps || max_steps == 0 || half_bit_us == 0 || gpio_pin > 15)
        return 0;

    const uint32_t bsrr_set   = (1U << gpio_pin);         // GPIOx_BSRR SET
    const uint32_t bsrr_reset = (1U << (gpio_pin + 16U)); // GPIOx_BSRR RESET

    uint16_t step_idx = 0;
    uint8_t level;

     level = start_level;

    for (uint16_t bit = 0; bit < 64; bit++)
    {
        uint8_t v = em4100_frame_get_bit(frame8, bit);

        // bit=1 → [HIGH, LOW]
        // bit=0 → [LOW, HIGH]
#if OUTPUT_INVERT
        uint8_t first  = (v ? 0U : 1U);
        uint8_t second = (v ? 1U : 0U);
#else
        uint8_t first  = (v ? 1U : 0U);
        uint8_t second = (v ? 0U : 1U);
#endif
        if (step_idx >= max_steps) break;
        level = first;
        steps[step_idx].bsrr    = level ? bsrr_set : bsrr_reset;
        steps[step_idx].time_us = half_bit_us;
        step_idx++;

        if (step_idx >= max_steps) break;
        level = second;
        steps[step_idx].bsrr    = level ? bsrr_set : bsrr_reset;
        steps[step_idx].time_us = half_bit_us;
        step_idx++;
    }

    return step_idx;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint32_t protocol_em4100_get_t5577_bitrate(int bitrate) {
    switch(bitrate) {
    case 64:
        return T5577_BITRATE_RF_64;
    case 32:
        return T5577_BITRATE_RF_32;
    case 16:
    	return T5577_BITRATE_RF_16;
    default:
        return T5577_BITRATE_RF_64;
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_em4100_decoder_begin(void* proto)
{
	EM4100_Decoder_Init_Full(&g_em4100_dec);
	g_em4100_dec.detected_half_bit_us = T_256_US;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_em4100_32_decoder_begin(void* proto)
{
	EM4100_Decoder_Init_Full(&g_em4100_32_dec);
	g_em4100_32_dec.detected_half_bit_us = T_128_US;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_em4100_16_decoder_begin(void* proto)
{
	EM4100_Decoder_Init_Full(&g_em4100_16_dec);
	g_em4100_16_dec.detected_half_bit_us = T_64_US;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool protocol_em4100_decoder_execute(void* proto, uint16_t size)
{
	lfrfid_evt_t* p = (lfrfid_evt_t*)proto;

   //if(lfrfidProtocolManager((const lfrfid_evt_t*)p, size) != LFRFIDStateActive)
   // 	return false;

	return em4100_decoder_execute(p, size, &g_em4100_dec);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool protocol_em4100_32_decoder_execute(void* proto, uint16_t size)
{
	lfrfid_evt_t* p = (lfrfid_evt_t*)proto;

	return em4100_decoder_execute(p, size, &g_em4100_32_dec);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool protocol_em4100_16_decoder_execute(void* proto, uint16_t size)
{
	lfrfid_evt_t* p = (lfrfid_evt_t*)proto;

	return em4100_decoder_execute(p, size, &g_em4100_16_dec);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_em4100_render_data(void* protocol, char *result)
{

	char* data = (char*)protocol_em4100_get_data(NULL);

    sprintf(
        result,
		"Hex: %02X %02X %02X %02X %02X\n"
        "FC: %03u\n"
		"Card: %05hu (RF/%02hu)",
		data[0],data[1],data[2],data[3],data[4],
        data[2],
       	MAKEWORD(data[4],data[3]),
		lfrfid_tag_info.bitrate);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t* protocol_em4100_get_data(void* proto)
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
bool protocol_em4100_encoder_begin(void* proto)
{
	LFRFID_TAG_INFO* tag_data = (LFRFID_TAG_INFO*)proto;
	uint8_t frame[8];

	em4100_build_frame8_from_uid(frame,tag_data->uid,5);

	uint16_t half_bit_us = (tag_data->bitrate*4);

	uint16_t nsteps = em4100_build_manchester_wave(
	    frame,
		lfrfid_encoded_data.data,
	    EM4100_MAX_STEPS,
	    half_bit_us-EMUL_EM4100_CORR,
	    /* gpio_pin   */ 2,
	    /* start_level*/ 0
	);

    if (nsteps == 0)
        return false;
#if 0
    WaveTx_Data_t data = {
        .steps  = gEncoded_data,
        .length = nsteps,
    };
#endif
    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_em4100_encoder_send(void* proto)
{
	lfrfid_encoded_data.index = 0;
	lfrfid_encoded_data.length = 128;
	lfrfid_emul_hw_init();
#if 0
	App_WaveTx_Init();

    if (!App_WaveTx_Start())
    {
        // BUSY or ERROR
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
void protocol_em4100_write_begin(void* protocol, void *data)
{
	LFRFID_TAG_INFO* tag_data = (LFRFID_TAG_INFO*)protocol;
	LFRFIDProgram* write = (LFRFIDProgram*)data;
	uint8_t encoded_data[8];

	// encoder begin
	em4100_build_frame8_from_uid(encoded_data,tag_data->uid,5);

#if 1
	if(write){
		if(write->type == LFRFIDProgramTypeT5577) {
			write->t5577.block_data[0] = (T5577_MOD_MANCHESTER | protocol_em4100_get_t5577_bitrate(tag_data->bitrate) | T5577_TRANS_BL_1_2);

			bytes_to_u32_array(BIT_ORDER_MSB_FIRST, encoded_data, &write->t5577.block_data[1], 2);
			write->t5577.max_blocks = 3;
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
void protocol_em4100_write_send(void* proto)
{

	t5577_execute_write(lfrfid_program, 0);
}

