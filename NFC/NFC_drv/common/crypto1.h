/*
 * crypto1.h - MIFARE Classic Crypto1 stream cipher (Phase A)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Crypto1 algorithm itself is from Proxmark3 (RfidResearchGroup):
 *   https://github.com/RfidResearchGroup/proxmark3.git
 *
 * The implementation uses a caller-owned static-state Crypto1 structure and
 * byte-level stream helpers. See COPYING.txt for the project license.
 */
#ifndef CRYPTO1_H
#define CRYPTO1_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Crypto1 LFSR cipher state (public, matching the reference layout). */
typedef struct {
    uint32_t odd;
    uint32_t even;
} Crypto1;

/* Load the 48-bit MIFARE key into the cipher state. */
void     crypto1_init(Crypto1 *c, uint64_t key);

/* Core cipher clocking (faithful to the reference). */
uint8_t  crypto1_bit (Crypto1 *c, uint8_t in, int is_encrypted);
uint8_t  crypto1_byte(Crypto1 *c, uint8_t in, int is_encrypted);
uint32_t crypto1_word(Crypto1 *c, uint32_t in, int is_encrypted);

/* MIFARE Classic weak-PRNG nonce successor (suc^n(x)). */
uint32_t crypto1_prng_successor(uint32_t x, uint32_t n);

/*
 * LFSR rollback primitives — the exact inverse of crypto1_bit/byte/word.
 *
 * These live here (not in crypto1_recover.c) because they must share the
 * private crypto1_filter() and the odd/even LFSR layout owned by the forward
 * cipher; they are the only additions this module needs for the card-only
 * Crypto1 recovery foundation (crypto1_recover.c). They do NOT change any
 * existing forward-cipher behaviour used by the poller.
 *
 * Uses the Proxmark3 crapto1 algorithm.
 *
 *   c  : cipher state advanced N steps forward; rolled back in place
 *   in : the same feed word/byte/bit supplied to the matching forward call
 *        (bit ordering: word/byte use BEBIT/C1_BIT exactly as the forward path)
 *   fb : non-zero to include the cipher-feedback term (matches is_encrypted)
 * @retval the keystream bit(s) produced at the rolled-back step(s).
 */
uint8_t  crypto1_lfsr_rollback_bit (Crypto1 *c, uint32_t in, int fb);
uint8_t  crypto1_lfsr_rollback_byte(Crypto1 *c, uint8_t  in, int fb);
uint32_t crypto1_lfsr_rollback_word(Crypto1 *c, uint32_t in, int fb);

/*
 * Phase-A authentication and stream helpers over plain buffers.
 *
 * crypto1_reader_answer: build the 8-byte encrypted reader answer {Nr}{ar}
 * plus its 8 encrypted (odd) parity bits. Initialises the cipher with `key`.
 *   key      : 48-bit sector key
 *   cuid     : 4-byte card UID as a big-endian uint32
 *   nt[4]    : tag nonce as received — plaintext for a first auth, or the
 *              ENCRYPTED nonce for a nested auth (is_nested=true)
 *   nr[4]    : reader nonce (plaintext)
 *   out[8]   : encrypted {Nr}{ar}
 *   out_par[8]: encrypted odd-parity bit per output byte (0/1)
 *   is_nested: false = first authentication (plaintext nonce);
 *              true  = nested authentication (the cipher is re-keyed and the
 *              plaintext nonce is recovered from the encrypted nt.
 * @retval the recovered PLAINTEXT tag nonce (for suc^96(nt) validation).
 */
uint32_t crypto1_reader_answer(Crypto1 *c, uint64_t key, uint32_t cuid,
                               const uint8_t nt[4], const uint8_t nr[4],
                               uint8_t out[8], uint8_t out_par[8], bool is_nested);

/*
 * After a successful auth, per-byte encrypted stream I/O for the READ
 * command/response.
 *   crypto1_encrypt_byte: returns the cipher byte for `plain`; *enc_par gets
 *     the encrypted odd-parity bit to transmit for that byte.
 *   crypto1_decrypt_byte: returns the plaintext for `cipher`; *ks_par gets the
 *     keystream bit at the parity position. The caller verifies parity as
 *       odd_parity(plain) == (received_parity_bit ^ *ks_par).
 */
uint8_t  crypto1_encrypt_byte(Crypto1 *c, uint8_t plain,  uint8_t *enc_par);
uint8_t  crypto1_decrypt_byte(Crypto1 *c, uint8_t cipher, uint8_t *ks_par);

/* ISO14443A odd parity of a byte (1 when popcount is even). Exposed so the
 * poller can verify received parity bits consistently with the cipher. */
uint8_t  crypto1_odd_parity8(uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO1_H */
