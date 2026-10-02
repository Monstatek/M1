/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Honeywell NexWatch protocol
 *
 * PSK1 modulation, 255us/bit, same raw-level/4-hypothesis decode strategy
 * as lfrfid_protocol_keri.c (see that file's header for why four parallel
 * buffers are needed). Every bit-level operation (preamble check,
 * reserved-field/parity validation, ID/mode/checksum field extraction, the
 * descramble table and magic-type table) was re-derived and checked
 * for byte-for-byte interoperability.
 *
 * Read (decoder), RF emulation (encoder), and T5577 clone (write) are
 * implemented. The emulator uses the shared PSK1 RF/2 DMA wave builder;
 * its timing still requires acceptance testing against physical readers.
 *
 * Honeywell NexWatch is a DIFFERENT protocol from Pyramid/Farpointe
 * (lfrfid_protocol_pyramid.c) -- different manufacturer, different
 * modulation (PSK1 vs FSK2a), different frame layout. It must never be
 * aliased to or classified as Pyramid.
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

#ifndef LFRFID_PROTOCOL_NEXWATCH_H_
#define LFRFID_PROTOCOL_NEXWATCH_H_

/* Decoded credential in the canonical
 * NEXWATCH_DECODED_DATA_SIZE=8 -- byte[0] is always 0 (an artifact of how
 * decoder_save shifts the 32-bit ID out and is kept exactly as-is
 * for interop rather than "improved"), bytes[1..4] are the 32-bit raw
 * (still-scrambled) ID field MSB-first, bytes[5..7] are the 24-bit
 * mode+parity+tail trailing field. */
#define NEXWATCH_DECODED_SIZE  (8)

/* Encoded frame: 96 bits, 12 bytes exactly -- unlike Keri, can_be_decoded
 * only checks a single preamble occurrence (offset 0), so no extra margin
 * for a second repeat window is needed. */
#define NEXWATCH_ENCODED_SIZE  (12)
#define NEXWATCH_ENCODED_BITS  (96)

extern const LFRFIDProtocolBase protocol_nexwatch;

#endif /* LFRFID_PROTOCOL_NEXWATCH_H_ */
