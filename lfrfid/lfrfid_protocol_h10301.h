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
#ifndef LFRFID_PROTOCOL_H10301_H_
#define LFRFID_PROTOCOL_H10301_H_



typedef struct {
    lfrfid_evt_t prev_half;
    bool         has_prev;
} fsk_symbol_state_t;

void fsk_symbol_state_init(fsk_symbol_state_t *st);

bool fsk_symbol_feed(fsk_symbol_state_t *st,
                     const lfrfid_evt_t *half,
                     uint8_t *out_symbol);

typedef struct {
    uint8_t buf[64];
    uint8_t count;
} fsk_bit_state_t;

void fsk_bit_state_init(fsk_bit_state_t *st);

bool fsk_bit_feed(fsk_bit_state_t *st,
                  uint8_t symbol,
                  uint8_t *out_bit);



/* ============================================================
 * H10301 Decoder
 *  - 96bit shift register buffer
 *  - bit_count:
 * ============================================================ */
#define H10301_DECODER_BITS   96U
#define H10301_DECODER_WORDS  3U

typedef struct {
    uint32_t word[H10301_DECODER_WORDS];  // [0] = MSB, [2] = LSB
    uint8_t  bit_count;                    // 0 ~ 96
    //uint32_t bit_test;
} h10301_Decoder_t;

void h10301_decoder_begin(h10301_Decoder_t *dec);

void h10301_decoder_push_bit(h10301_Decoder_t *dec, uint8_t bit);

static inline bool h10301_decoder_is_full(const h10301_Decoder_t *dec) {
    return dec && (dec->bit_count >= H10301_DECODER_BITS);
}

static inline const uint32_t* h10301_decoder_frame(const h10301_Decoder_t *dec) {
    return dec ? dec->word : 0;
}

/* ============================================================
 * RAW → 심볼(0/1) → decoder push
 * ============================================================ */
void h10301_insert_raw_events(const lfrfid_evt_t *events,
                              int event_count,
                              h10301_Decoder_t *dec);

/* ============================================================
 * Decoder frame이 HID H10301
 * ============================================================ */
bool h10301_is_valid(const uint32_t *frame96);

/* ============================================================
 * 96bit frame → 26bit raw
 * ============================================================ */
bool h10301_extract_raw26(const uint32_t *frame96, uint32_t *out_raw26);

/* ============================================================
 * 26bit raw → facility/card number
 * ============================================================ */
bool h10301_extract_fields(uint32_t raw26,
                           uint8_t *facility_code,
                           uint16_t *card_number);

extern const LFRFIDProtocolBase protocol_h10301;


#endif /* LFRFID_PROTOCOL_H10301_H_ */
