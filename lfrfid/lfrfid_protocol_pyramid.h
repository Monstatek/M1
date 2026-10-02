/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Pyramid (Farpointe) protocol
 *
 * The M1 vtable adaptation retains the frame and CRC layout. Its
 * Wiegand-parity helper uses bit offsets within the source buffer.
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

#ifndef LFRFID_PROTOCOL_PYRAMID_H_
#define LFRFID_PROTOCOL_PYRAMID_H_

/* Decoded credential: [0]=format length (26), [1]=facility code,
 * [2]=card number high byte, [3]=card number low byte.
 * Canonical Pyramid decoded layout. */
#define PYRAMID_DECODED_SIZE  (4)

/* Encoded shift-register / frame buffer.
 * PYRAMID_ENCODED_DATA_SIZE = (3 preamble + 13 data + 3 preamble) = 19.
 * The decoder needs bytes up to index 17 (trailing preamble at bits 128..143). */
#define PYRAMID_ENCODED_SIZE  (19)

/* Bits actually transmitted per frame before it repeats.
 * PYRAMID_ENCODED_BIT_SIZE = (3 + 13) * 8 = 128. */
#define PYRAMID_ENCODED_BITS  (128)

extern const LFRFIDProtocolBase protocol_pyramid;

#endif /* LFRFID_PROTOCOL_PYRAMID_H_ */
