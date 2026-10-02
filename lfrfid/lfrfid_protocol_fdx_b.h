/* See COPYING.txt for license details. */

/*
 * LF RFID — FDX-B / ISO 11784-11785 animal identification (Pet Tag)
 *
 * NOTE ON CARRIER: FDX-B operates at 134.2 kHz. The M1 shared LF read path
 * currently generates a 125 kHz carrier (lfrfid_hal.c: lfrfid_RFIDOut_Init(125000)),
 * so this protocol decoder is software-complete but physical reads require the
 * separate 134.2 kHz carrier-support increment. The bi-phase data timing
 * (128/256 us) is carrier-independent and passes the shared RX filter.
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

#ifndef LFRFID_PROTOCOL_FDX_B_H_
#define LFRFID_PROTOCOL_FDX_B_H_

/* Decoded canonical payload: 11 bytes (88 bits), containing the de-framed
 * record (38-bit national + 10-bit country + 16-bit block/status + 24-bit extra
 * data), with the 11-bit header, control bits and 16-bit CRC removed. */
#define FDXB_DECODED_SIZE  (11)

/* Emulated / T5577-written frame: 128 bits. Decoder shift-register window is
 * 144 bits / 18 bytes (128-bit frame + the next frame's 11-bit header). */
#define FDXB_ENCODED_BITS  (128)
#define FDXB_ENCODED_SIZE  (18)

extern const LFRFIDProtocolBase protocol_fdx_b;

#endif /* LFRFID_PROTOCOL_FDX_B_H_ */
