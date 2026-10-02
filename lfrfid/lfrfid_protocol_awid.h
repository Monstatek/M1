/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — AWID protocol
 *
 * The M1 vtable adaptation preserves the full 9-byte decoded payload.
 * Truncating it into uid[5] would lose credential data.
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

#ifndef LFRFID_PROTOCOL_AWID_H_
#define LFRFID_PROTOCOL_AWID_H_

/* Decoded credential: the 66-bit de-parity'd AWID payload, MSB-first, packed
 * into 9 bytes (uid[8] uses only bits 64..65).
 *   uid[0]      = format length (26 / 34 / 36 / 37 / 50)
 *   for fmt 26: facility = payload bits 9..16, card = payload bits 17..32
 *   for 34/36/37/50: raw payload (not field-parsed). */
#define AWID_DECODED_SIZE  (9)

/* Encoded frame: 96 bits. Buffer is 13 bytes (96/8 + 1);
 * byte 12 holds the next frame's preamble in a looped signal. */
#define AWID_ENCODED_SIZE  (13)
#define AWID_ENCODED_BITS  (96)

extern const LFRFIDProtocolBase protocol_awid;

#endif /* LFRFID_PROTOCOL_AWID_H_ */
