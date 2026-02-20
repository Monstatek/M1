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

#ifndef LFRFID_PROTOCOL_EM4100_H_
#define LFRFID_PROTOCOL_EM4100_H_


#define EM4100_DECODED_DATA_SIZE (5)

#define FRAME_BITS (64)
#define PREAMBLE_BITS (9)
#define FRAME_BUFFER_BYTES (8)

#define T_64_US (64)
#define T_128_US (128)
#define T_256_US (256)

/**
 *
 */
typedef enum {
    DECODER_STATE_IDLE,
    DECODER_STATE_SYNC,
	DECODER_STATE_VERIFY,
    DECODER_STATE_DATA
} DecoderState_t;

/**
 *
 */
typedef struct {
    //DecoderState_t state;
    uint16_t detected_half_bit_us;

    uint8_t frame_buffer[FRAME_BUFFER_BYTES];
    uint8_t bit_count;
   //uint32_t bit_test;

    lfrfid_evt_t edge_buffer[FRAME_CHUNK_SIZE];
    uint8_t edge_count;

    //uint8_t sync_bit_count;
    //uint8_t verify_edge_count;
} EM4100_Decoder_t;


//void setEm4100_bitrate(int bitrate);
void EM4100_Decoder_Init_Full(EM4100_Decoder_t* dec);
void EM4100_Decoder_Init_Partial(EM4100_Decoder_t* dec);

extern const LFRFIDProtocolBase protocol_em4100;
extern const LFRFIDProtocolBase protocol_em4100_32;
extern const LFRFIDProtocolBase protocol_em4100_16;

#endif /* LFRFID_PROTOCOL_EM4100_H_ */
