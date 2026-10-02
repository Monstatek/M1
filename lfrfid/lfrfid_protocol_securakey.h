/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) — Securakey "Radio Key" protocol
 *
 * Uses the "Radio Key" protocol name, format-aware Manchester decoding,
 * parity and spacer framing, and a canonical 6-byte credential. Rendering
 * reads the stored credential rather than decoder scratch. The encoder and
 * T5577 configuration account for the RKKTH format.
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

#ifndef LFRFID_PROTOCOL_SECURAKEY_H_
#define LFRFID_PROTOCOL_SECURAKEY_H_

/* Decoded credential: six bytes (48 bits), canonical layout.
 *   RKKT 26-bit: [00][facility 8][card hi][card lo][CS1][CS2]
 *   RKKT 32-bit: [pad2+facility hi][facility lo][card hi][card lo][CS1][CS2]
 *   RKKTH plaintext: [00][00][32-bit card across bytes 2..5]
 * Format is inferred from the data bytes at render time;
 * CS1/CS2 are retained verbatim and are not computed or validated. */
#define SECURAKEY_DECODED_SIZE       (6)

/* Encoded frames: RKKT (26/32-bit) = 96 bits / 12 bytes; RKKTH = 64 bits / 8 B. */
#define SECURAKEY_RKKT_ENCODED_SIZE  (12)
#define SECURAKEY_RKKT_ENCODED_BITS  (96)
#define SECURAKEY_RKKTH_ENCODED_SIZE (8)
#define SECURAKEY_RKKTH_ENCODED_BITS (64)

extern const LFRFIDProtocolBase protocol_radiokey;

#endif /* LFRFID_PROTOCOL_SECURAKEY_H_ */
