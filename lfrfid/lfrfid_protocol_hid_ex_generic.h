/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Generic HID Extended (HIDExt)
 *
 * Extended HID Prox coverage: recognizes long-format HID-framed FSK
 * credentials (a 92-bit record inside a 192-bit encoded frame) that the
 * 96-bit HIDProx / H10301 decoders cannot represent. It does NOT replace
 * H10301 or HIDProx; those keep priority for the 96-bit frame lengths, and
 * HIDExt only matches the longer 192-bit frame (the two are mutually
 * exclusive because a 0x1D preamble byte is not a valid Manchester pair).
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

#ifndef LFRFID_PROTOCOL_HID_EX_GENERIC_H_
#define LFRFID_PROTOCOL_HID_EX_GENERIC_H_

/* Decoded canonical: 12 bytes (holds the 92-bit HID Extended record). */
#define HIDEXT_DECODED_SIZE   (12)
#define HIDEXT_DECODED_BITS   (92)

/* Encoded frame: 192 bits (0x1D preamble byte + 184 Manchester bits). Decoder
 * shift-register window is 25 bytes (frame + the next frame's 0x1D preamble). */
#define HIDEXT_ENCODED_BITS   (192)
#define HIDEXT_ENCODED_SIZE   (25)

extern const LFRFIDProtocolBase protocol_hid_ex_generic;

#endif /* LFRFID_PROTOCOL_HID_EX_GENERIC_H_ */
