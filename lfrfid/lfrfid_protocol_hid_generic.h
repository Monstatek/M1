/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Generic HID Proximity (HIDProx)
 *
 * Broad HID Prox coverage: recognizes HID-framed FSK credentials of any
 * supported bit length and preserves the raw 44-bit record. It does NOT replace
 * the existing 26-bit H10301 decoder; H10301 keeps priority for standard 26-bit
 * credentials (registered earlier + blocks HIDProx on the same frame).
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

#ifndef LFRFID_PROTOCOL_HID_GENERIC_H_
#define LFRFID_PROTOCOL_HID_GENERIC_H_

/* Decoded canonical form: six bytes holding the 44-bit HID record. */
#define HIDPROX_DECODED_SIZE   (6)

/* Encoded frame: 96 bits (0x1D preamble byte + 88 Manchester bits). Decoder
 * shift-register window is 13 bytes (frame + the next frame's 0x1D preamble). */
#define HIDPROX_ENCODED_BITS   (96)
#define HIDPROX_ENCODED_SIZE   (13)

extern const LFRFIDProtocolBase protocol_hid_generic;

#endif /* LFRFID_PROTOCOL_HID_GENERIC_H_ */
