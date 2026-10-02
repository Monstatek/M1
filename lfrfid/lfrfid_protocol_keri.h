/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Keri Systems protocol
 *
 * PSK1 modulation, RF/2 bit rate. Keri is the first PSK protocol in this
 * port -- every other lfrfid_protocol_*.c here is ASK/FSK. The decoder
 * (raw-level sampling at a fixed 255us bit clock, with parallel
 * positive/negative/corrupted-phase hypothesis buffers to resolve antenna
 * phase ambiguity) and the descramble table are re-derived line-by-line
 * for byte-for-byte interoperability.
 *
 * Read (decoder), RF emulation (encoder), and T5577 clone (write) are
 * implemented. The emulator uses the shared PSK1 RF/2 DMA wave builder;
 * its timing still requires acceptance testing against physical readers.
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

#ifndef LFRFID_PROTOCOL_KERI_H_
#define LFRFID_PROTOCOL_KERI_H_

/* Decoded credential: the 32-bit raw ID field, MSB-first, packed into 4
 * bytes in the canonical Keri decoded
 * representation (internal_id/fc/cn are all derived from this on render,
 * not stored separately). */
#define KERI_DECODED_SIZE  (4)

/* Encoded frame: 64 bits, buffer sized with the same +5-byte margin as
 * KERI_ENCODED_DATA_SIZE so a 96-bit two-preamble window
 * (offset 0 and offset 64) always fits during decode. */
#define KERI_ENCODED_SIZE  (13)
#define KERI_ENCODED_BITS  (64)

extern const LFRFIDProtocolBase protocol_keri;

#endif /* LFRFID_PROTOCOL_KERI_H_ */
