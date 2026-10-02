/*
 * ce_mfc.h - MIFARE Classic card-emulation Crypto1 session engine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Card (listener) side of the MIFARE Classic Crypto1 handshake and encrypted
 * data exchange. Pure logic operating on byte buffers; the RF transport
 * (raw-parity TX/RX in listen mode) is the caller's job (see ce_mfc_xport.h and
 * the nfc_listener glue). Reuses the committed crypto1.c primitives.
 *
 * Two-state model (idle / auth-complete) with crypto1 authentication (nt sent
 * plaintext on first auth, {Nr}{ar} verified against prng_successor(nt,64),
 * {at}=prng_successor(nt,96) returned encrypted with custom parity). The crypto
 * core is the existing, separately attributed crypto1.c.
 */
#ifndef NFC_DRV_CE_MFC_H_
#define NFC_DRV_CE_MFC_H_

#include <stdint.h>
#include <stdbool.h>
#include "crypto1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CE_MFC_IDLE = 0,      /* no active authentication            */
    CE_MFC_AUTHED         /* a sector is authenticated (session) */
} ce_mfc_state_t;

typedef struct {
    Crypto1        cipher;      /* running crypto1 state              */
    uint32_t       cuid;        /* card UID (big-endian last 4 bytes) */
    ce_mfc_state_t state;
    uint8_t        key_type;    /* 0x60 = Key A, 0x61 = Key B         */
    uint8_t        auth_block;  /* block the current session authed   */
    uint32_t       nt;          /* current tag nonce                  */
} ce_mfc_t;

/* Reset the session to IDLE for a card with the given cuid. */
void ce_mfc_init(ce_mfc_t *s, uint32_t cuid);

/* ISO/IEC 14443-A CRC_A (poly 0x8408, preload 0x6363) over d[0..n-1].
 * CRC[0] (low byte) = crc & 0xFF, CRC[1] (high byte) = crc >> 8 -- matching
 * how a reader appends CRC_A to an on-air command and how ce_mfc_read_cmd_
 * nopar()/ce_mfc_build_read_resp() (this file) already use it. Exposed (was
 * file-local) so callers validating a raw on-air frame's trailing CRC_A --
 * e.g. m1_mfc_session_nonce_gate(), m1_mfc_session.c -- reuse this single
 * implementation instead of a second copy. */
uint16_t ce_crc_a(const uint8_t *d, uint8_t n);

/*
 * AUTH first part: the reader sent { key_type(0x60/0x61), block }. The card
 * selects the sector key (caller supplies the 48-bit `key` from the image),
 * uses `nt_rand` as the tag nonce (device: HW RNG; tests: fixed), initialises
 * the cipher, and produces the 4-byte nonce to transmit.
 *
 *   out_nt[4]  : the nonce bytes to send (big-endian).
 *   out_par[4] : parity bits for each nonce byte.
 *   *plaintext : true  -> send with STANDARD parity (first auth, nonce is clear)
 *                false -> send with CUSTOM parity   (nested auth, nonce encrypted)
 *
 * Returns true (first-auth nonce generation cannot fail). The caller must then
 * transmit out_nt (plaintext via standard tx, or encrypted via custom-parity tx)
 * and wait for the reader's {Nr}{ar}.
 */
bool ce_mfc_auth1(ce_mfc_t *s, uint8_t key_type, uint8_t block, uint64_t key,
                  uint32_t nt_rand, uint8_t out_nt[4], uint8_t out_par[4],
                  bool *plaintext);

/*
 * AUTH second part: the reader replied with 8 bytes {Nr}{ar} (encrypted). The
 * card feeds Nr, verifies ar == prng_successor(nt,64), and on success returns
 * {at} = prng_successor(nt,96) encrypted with custom parity, moving to AUTHED.
 *
 *   nr_ar[8]   : received {Nr}{ar}.
 *   out_at[4]  : encrypted {at} to transmit (custom parity).
 *   out_par[4] : parity bits for {at}.
 *
 * Returns true on successful authentication; false means the reader failed the
 * check and the card must stay silent (send nothing), matching a real card.
 */
bool ce_mfc_auth2(ce_mfc_t *s, const uint8_t nr_ar[8],
                  uint8_t out_at[4], uint8_t out_par[4]);

/*
 * POST-AUTH (session must be CE_MFC_AUTHED). Decrypt an N-byte encrypted reader
 * command in the CONTINUING cipher, verifying each byte's parity in on-air order.
 *   enc[n]/par[n] : received ciphertext bytes and their parity bits.
 *   plain_out[n]  : recovered plaintext (written for all N regardless of parity).
 * Returns true iff the session is AUTHED and EVERY parity bit is valid. The
 * cipher advances by N bytes either way (a real card clocks the received bytes),
 * so a caller that rejects must reset the session (ce_mfc_init) before reuse.
 */
bool ce_mfc_decrypt_cmd(ce_mfc_t *s, const uint8_t *enc, const uint8_t *par,
                        uint8_t n, uint8_t *plain_out);

/*
 * POST-AUTH: encrypt an N-byte response in the continuing cipher, producing the
 * ciphertext and its N encrypted (custom) parity bits, in on-air order. Advances
 * the cipher by N bytes. No-op (returns false) unless the session is AUTHED.
 */
bool ce_mfc_encrypt_resp(ce_mfc_t *s, const uint8_t *plain, uint8_t n,
                         uint8_t *enc_out, uint8_t *par_out);

/* Deterministic POSTAUTH-1 card image: block b -> { b*16+i } for i in 0..15. */
bool ce_mfc_block_data(uint8_t block, uint8_t out[16]);

/*
 * POSTAUTH-1 target-mode READ (parity-unavailable). The ST25R3916 listener does
 * NOT expose received parity, so the encrypted READ arrives as 4 ciphertext bytes.
 * Decrypt on a TEMPORARY copy of the continuing cipher and validate opcode 0x30,
 * plaintext CRC_A, and that the block is in the AUTHED sector. Commit the advanced
 * cipher ONLY if valid.
 *   return 1  : valid READ; cipher committed; *out_block set.
 *   return 0  : rejected; cipher NOT advanced (caller must reset the session).
 *   return -1 : not AUTHED / bad args.
 */
int ce_mfc_read_cmd_nopar(ce_mfc_t *s, const uint8_t enc[4], uint8_t *out_block);

/*
 * Build + encrypt the 18-byte READ response (16 deterministic data bytes + CRC_A)
 * for `block` on the committed continuing cipher. Produces enc_out[18]+par_out[18]
 * (18 encrypted custom-parity bits). Returns false unless AUTHED.
 */
bool ce_mfc_build_read_resp(ce_mfc_t *s, uint8_t block,
                            uint8_t enc_out[18], uint8_t par_out[18]);

/*
 * Same as ce_mfc_build_read_resp(), but encrypts the caller-supplied 16
 * bytes of real block data instead of ce_mfc_block_data()'s deterministic
 * test image. Same CRC_A + encrypt tail, same Crypto1 state advancement --
 * this is only a different source for the 16 plaintext bytes.
 */
bool ce_mfc_build_read_resp_real(ce_mfc_t *s, const uint8_t block_data[16],
                                 uint8_t enc_out[18], uint8_t par_out[18]);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_CE_MFC_H_ */
