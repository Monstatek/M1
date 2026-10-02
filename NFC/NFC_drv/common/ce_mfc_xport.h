/*
 * ce_mfc_xport.h - MIFARE Classic card-emulation transport framing (bit-level).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pure logic (no RF, no allocation) shared by the emulation transport proof and
 * the eventual full Crypto1 listener. Packs/unpacks MIFARE frames at the bit
 * level -- 8 data bits (LSB first) + 1 parity bit per byte -- so the caller can
 * transmit with software-controlled parity (RFAL PAR_TX_NONE) and keep the
 * reader's parity on receive (PAR_RX_KEEP). Mirrors the initiator-side framing
 * already proven in nfc_poller.c (mfc_pack_bits / mfc_unpack_bits); kept in a
 * separate non-frozen module so the recovery/write path is untouched.
 *
 * Crypto1 parity: MIFARE's encrypted parity bit for a cipher byte is
 * (keystream_parity ^ odd_parity(plaintext)). The committed crypto1.c already
 * returns this via crypto1_encrypt_byte()'s enc_par output; this module only
 * does the bit transport framing around it.
 */
#ifndef NFC_DRV_CE_MFC_XPORT_H_
#define NFC_DRV_CE_MFC_XPORT_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Odd parity of one byte (MIFARE uses odd parity on the plaintext). */
uint8_t ce_mfc_odd_parity(uint8_t b);

/* Pack `n` (byte,parity) pairs into `out` as (8 data bits LSB-first + 1 parity)
 * each. Returns the total bit count written. `out` must hold >= (n*9+7)/8 bytes.
 * Identical bit order to the poller's initiator framing. */
uint16_t ce_mfc_pack_bits(uint8_t *out, const uint8_t *data, const uint8_t *par, uint8_t n);

/* Unpack a bit-packed rx buffer into up to `maxn` (byte,parity) pairs. Returns
 * the number of complete 9-bit groups decoded. */
uint8_t ce_mfc_unpack_bits(const uint8_t *in, uint16_t rxbits,
                           uint8_t *data, uint8_t *par, uint8_t maxn);

/* Pack an exact 4-bit frame (e.g. the MIFARE ACK nibble 0xA) into out[0] low
 * nibble, LSB first. Returns the bit count (always 4). No parity on 4-bit
 * frames. */
uint16_t ce_mfc_pack_ack4(uint8_t *out, uint8_t nibble);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_CE_MFC_XPORT_H_ */
