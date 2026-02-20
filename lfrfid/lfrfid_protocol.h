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
 * Copyright (C) Flipper Devices Inc.
 * Licensed under the GNU General Public License v3.0 (GPLv3).
 *
 * Modifications and additional implementation:
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

#ifndef LFRFID_PROTOCOL_H_
#define LFRFID_PROTOCOL_H_

#define PROTOCOL_NO           (-1)

typedef enum {
    LFRFIDFeatureASK = 1 << 0, /** ASK Demodulation */
    LFRFIDFeaturePSK = 1 << 1, /** PSK Demodulation */
} LFRFIDFeature;

typedef enum {
    LFRFIDProtocolEM4100,
    LFRFIDProtocolEM4100_32,
    LFRFIDProtocolEM4100_16,
    LFRFIDProtocolH10301,
    LFRFIDProtocolMax,
} LFRFIDProtocol;

typedef void* (*ProtocolMemoryCreate)(void);
typedef void (*ProtocolMemoryDestory)(void* protocol);

typedef uint8_t* (*lfrfidProtocolGetData)(void* protocol);

typedef void (*lfrfidProtocolDecoderBegin)(void* protocol);
typedef bool (*lfrfidProtocolDecoderExecute)(void* protocol, int size);

typedef bool (*lfrfidProtocolEncoderBegin)(void* protocol);
typedef void (*lfrfidProtocolEncoderSend)(void* protocol);

typedef bool (*lfrfidProtocolWriteBegin)(void* protocol, void* data);
typedef void (*lfrfidProtocolWriteSend)(void* protocol);

typedef void (*lfrfidProtocolRenderData)(void* protocol, char* result);

typedef struct {
	lfrfidProtocolDecoderBegin begin;
	lfrfidProtocolDecoderExecute execute;
} LFRFIDProtocolDecoder;

typedef struct {
	lfrfidProtocolEncoderBegin begin;
    lfrfidProtocolEncoderSend send;
} LFRFIDProtocolEncoder;

typedef struct {
	lfrfidProtocolWriteBegin begin;
	lfrfidProtocolWriteSend send;
} LFRFIDProtocolWrite;

typedef struct {
	int min;
    int max;
} LFRFIDProtocolFilter;

typedef struct {
    const size_t data_size;
    const char* name;
    const char* manufacturer;
    const uint32_t features;

    lfrfidProtocolGetData get_data;
    LFRFIDProtocolDecoder decoder;
    LFRFIDProtocolEncoder encoder;
    LFRFIDProtocolWrite   write;
    lfrfidProtocolRenderData render_uid;
    lfrfidProtocolRenderData render_data;

} LFRFIDProtocolBase;

extern const LFRFIDProtocolBase* lfrfid_protocols[];

void lfrfid_decoder_begin(void);
bool lfrfid_decoder_execute(uint16_t protocol_index, const lfrfid_evt_t* new_stream, uint8_t stream_count);

bool lfrfid_encoder_begin(uint16_t protocol_index, void* proto);
void lfrfid_encoder_send(uint16_t protocol_index, void* proto);

void lfrfid_write_begin(uint16_t protocol_index, void* proto, void *data);
void lfrfid_write_send(uint16_t protocol_index, void* proto);
//void lfrfid_write_data(uint16_t protocol_index, void* proto, void *data);

const char* protocol_get_manufacturer(uint16_t protocol_index);
const char* protocol_get_name(uint16_t protocol_index);
uint16_t protocol_get_data_size(uint16_t protocol_index);
void protocol_get_data(uint16_t protocol_index, uint8_t* data, size_t data_size);
void protocol_render_data(uint16_t protocol_index, char* szstring);

void lfrfid_GetTagInfo(PLFRFID_TAG_INFO pTaginfo);
void lfrfid_tag_info_init(void);

void* protocol_get_pdata(uint16_t protocol_index);

bool lfrfid_write_verify(LFRFID_TAG_INFO* write, LFRFID_TAG_INFO* readback);
#endif /* LFRFID_PROTOCOL_H_ */
