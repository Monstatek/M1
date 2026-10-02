/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Kantech ioProx XSF protocol
 *
 * The M1 vtable adaptation retains the RF/64 FSK grouping and field order:
 * facility precedes version, and the checksum includes the fixed 0xF0 word.
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

#ifndef LFRFID_PROTOCOL_IOPROX_H_
#define LFRFID_PROTOCOL_IOPROX_H_

/* Decoded credential (4 bytes), ioProx XSF layout:
 *   [0] = Facility code   (bits 18..25)
 *   [1] = Version         (bits 27..34)
 *   [2] = Card/code high  (bits 36..43)
 *   [3] = Card/code low   (bits 45..52)
 *   Card number = (uid[2] << 8) | uid[3]. */
#define IOPROX_DECODED_SIZE  (4)

/* Encoded frame: 8 bytes / 64 bits. */
#define IOPROX_ENCODED_SIZE  (8)
#define IOPROX_ENCODED_BITS  (64)

extern const LFRFIDProtocolBase protocol_ioprox;

#endif /* LFRFID_PROTOCOL_IOPROX_H_ */
