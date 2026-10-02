/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Jablotron protocol
 *
 * Uses a 64-bit frame, bi-phase (DIPHASE) line coding, RF/64 timing,
 * checksum (sum ^ 0x3A), and a canonical 5-byte representation. The
 * "Card: %llX" display decodes BCD and prints hexadecimal from the stored
 * credential, so file-load and Add-Manually do not depend on decoder scratch.
 * The encoder supports live Emulate.
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

#ifndef LFRFID_PROTOCOL_JABLOTRON_H_
#define LFRFID_PROTOCOL_JABLOTRON_H_

/* Decoded credential: 5 bytes (40 bits), canonical layout — the
 * raw 40-bit payload (frame bits 16..55), preamble and checksum stripped. */
#define JABLOTRON_DECODED_SIZE  (5)

/* Decoder shift-register window: 80 bits / 10 bytes. A valid frame requires the
 * 16-bit preamble at bit 0 AND the next frame's preamble at bit 64. */
#define JABLOTRON_ENCODED_SIZE  (10)

/* Emulated / T5577-written frame: 64 bits (16-bit preamble + 40-bit payload +
 * 8-bit checksum). */
#define JABLOTRON_ENCODED_BITS  (64)

extern const LFRFIDProtocolBase protocol_jablotron;

#endif /* LFRFID_PROTOCOL_JABLOTRON_H_ */
