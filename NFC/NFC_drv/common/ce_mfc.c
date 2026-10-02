/*
 * ce_mfc.c - MIFARE Classic card-emulation Crypto1 session engine.
 *            See ce_mfc.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Auth math mirrors the reader-side handshake already proven on hardware in
 * nfc_poller.c (mfc_auth) / crypto1_reader_answer -- the card side is its exact
 * mirror.
 */
#include "ce_mfc.h"
#include <stddef.h>
#include <string.h>

static uint32_t be32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}
static void wr_be32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);  out[3] = (uint8_t)v;
}

void ce_mfc_init(ce_mfc_t *s, uint32_t cuid)
{
    if (s == NULL) return;
    s->cuid       = cuid;
    s->state      = CE_MFC_IDLE;
    s->key_type   = 0;
    s->auth_block = 0;
    s->nt         = 0;
    /* cipher is (re)initialised per auth */
}

bool ce_mfc_auth1(ce_mfc_t *s, uint8_t key_type, uint8_t block, uint64_t key,
                  uint32_t nt_rand, uint8_t out_nt[4], uint8_t out_par[4],
                  bool *plaintext)
{
    if (s == NULL || out_nt == NULL || out_par == NULL || plaintext == NULL) return false;

    bool nested = (s->state == CE_MFC_AUTHED);   /* an auth already ran this session */

    s->key_type   = key_type;
    s->auth_block = block;
    s->nt         = nt_rand;

    wr_be32(out_nt, nt_rand);

    if (!nested) {
        /* First authentication: nonce is sent in the clear (standard parity).
         * Feed nt ^ cuid into a freshly keyed cipher (is_encrypted = 0). */
        crypto1_init(&s->cipher, key);
        (void)crypto1_word(&s->cipher, nt_rand ^ s->cuid, 0);
        for (uint8_t i = 0; i < 4; i++) out_par[i] = crypto1_odd_parity8(out_nt[i]);
        *plaintext = true;
    } else {
        /* Nested authentication: the nonce is transmitted ENCRYPTED with custom
         * parity. Re-key with the new sector key; the keystream that encrypts
         * the nonce is produced as it is fed. */
        crypto1_init(&s->cipher, key);
        uint8_t ks[4], ksp[4];
        for (uint8_t i = 0; i < 4; i++) {
            /* keystream byte for this nonce byte (plaintext fed = 0 so we read ks) */
            ks[i] = crypto1_encrypt_byte(&s->cipher, 0x00, &ksp[i]);
        }
        /* NOTE: full nested transmit (mixing nt^cuid feedback with the encrypted
         * nonce + parity) is completed in the next increment together with a
         * nested host vector; first-auth is the validated path. */
        for (uint8_t i = 0; i < 4; i++) {
            out_nt[i]  = (uint8_t)(out_nt[i] ^ ks[i]);
            out_par[i] = (uint8_t)(ksp[i] ^ crypto1_odd_parity8((uint8_t)(out_nt[i] ^ ks[i])));
        }
        *plaintext = false;
    }
    return true;
}

bool ce_mfc_auth2(ce_mfc_t *s, const uint8_t nr_ar[8],
                  uint8_t out_at[4], uint8_t out_par[4])
{
    if (s == NULL || nr_ar == NULL || out_at == NULL || out_par == NULL) return false;

    uint32_t nr = be32(&nr_ar[0]);
    uint32_t ar = be32(&nr_ar[4]);

    /* Feed the encrypted reader nonce (is_encrypted = 1: self-synchronising). */
    (void)crypto1_word(&s->cipher, nr, 1);

    /* Decrypt ar with the next keystream word and verify it is suc2(nt). */
    uint32_t secret = ar ^ crypto1_word(&s->cipher, 0, 0);
    if (secret != crypto1_prng_successor(s->nt, 64)) {
        return false;   /* reader failed -> stay silent (as a real card does) */
    }

    /* Reply {at} = suc3(nt), encrypted with custom parity. */
    uint32_t at = crypto1_prng_successor(s->nt, 96);
    uint8_t atb[4];
    wr_be32(atb, at);
    for (uint8_t i = 0; i < 4; i++) {
        out_at[i] = crypto1_encrypt_byte(&s->cipher, atb[i], &out_par[i]);
    }

    s->state = CE_MFC_AUTHED;
    return true;
}

bool ce_mfc_decrypt_cmd(ce_mfc_t *s, const uint8_t *enc, const uint8_t *par,
                        uint8_t n, uint8_t *plain_out)
{
    if (s == NULL || enc == NULL || par == NULL || plain_out == NULL) return false;
    if (s->state != CE_MFC_AUTHED) return false;

    bool ok = true;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t ksp;
        uint8_t p = crypto1_decrypt_byte(&s->cipher, enc[i], &ksp);
        plain_out[i] = p;
        /* On-air parity bit = odd_parity(plaintext) XOR keystream-parity bit. */
        uint8_t exp = (uint8_t)((crypto1_odd_parity8(p) ^ ksp) & 1U);
        if ((uint8_t)(par[i] & 1U) != exp) { ok = false; }
    }
    return ok;
}

bool ce_mfc_encrypt_resp(ce_mfc_t *s, const uint8_t *plain, uint8_t n,
                         uint8_t *enc_out, uint8_t *par_out)
{
    if (s == NULL || plain == NULL || enc_out == NULL || par_out == NULL) return false;
    if (s->state != CE_MFC_AUTHED) return false;

    for (uint8_t i = 0; i < n; i++) {
        enc_out[i] = crypto1_encrypt_byte(&s->cipher, plain[i], &par_out[i]);
    }
    return true;
}

/* ISO/IEC 14443-A CRC_A (poly 0x8408, preload 0x6363); CRC[0]=low, CRC[1]=high.
 * Declared in ce_mfc.h -- exposed for callers outside this file (see there). */
uint16_t ce_crc_a(const uint8_t *d, uint8_t n)
{
    uint16_t crc = 0x6363U;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t b = (uint8_t)(d[i] ^ (uint8_t)(crc & 0xFFU));
        b = (uint8_t)(b ^ (uint8_t)(b << 4));
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4));
    }
    return crc;
}

bool ce_mfc_block_data(uint8_t block, uint8_t out[16])
{
    if (out == NULL) return false;
    for (uint8_t i = 0; i < 16; i++) { out[i] = (uint8_t)((block * 16U) + i); }
    return true;
}

int ce_mfc_read_cmd_nopar(ce_mfc_t *s, const uint8_t enc[4], uint8_t *out_block)
{
    if (s == NULL || enc == NULL || out_block == NULL) return -1;
    if (s->state != CE_MFC_AUTHED) return -1;

    /* Target mode exposes no received parity, so validate by decrypt + opcode +
     * block + plaintext CRC_A on a TEMPORARY cipher copy; commit only if valid. */
    ce_mfc_t tmp = *s;
    uint8_t plain[4];
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t ksp;
        plain[i] = crypto1_decrypt_byte(&tmp.cipher, enc[i], &ksp);
    }
    if (plain[0] != 0x30U) return 0;                     /* MFC READ opcode      */
    uint16_t cc = ce_crc_a(plain, 2);
    if (((uint8_t)(cc & 0xFFU) != plain[2]) ||
        ((uint8_t)(cc >> 8) != plain[3])) return 0;      /* plaintext CRC_A      */
    uint8_t block = plain[1];
    /* POSTAUTH-1: MFC 1K, 4 blocks/sector; the READ must target the authed sector. */
    if ((uint8_t)(block / 4U) != (uint8_t)(s->auth_block / 4U)) return 0;

    *s = tmp;                                            /* commit advanced cipher */
    *out_block = block;
    return 1;
}

/* Shared tail: CRC_A + encrypt whatever 16 bytes the caller supplies. Neither
 * caller below touches Crypto1 state beyond the existing ce_mfc_encrypt_resp()
 * call already used by the original (deterministic-image) path. */
static bool ce_mfc_build_read_resp_from(ce_mfc_t *s, const uint8_t data16[16],
                                        uint8_t enc_out[18], uint8_t par_out[18])
{
    if (s == NULL || data16 == NULL || enc_out == NULL || par_out == NULL) return false;
    if (s->state != CE_MFC_AUTHED) return false;

    uint8_t resp[18];
    memcpy(resp, data16, 16);
    uint16_t cc = ce_crc_a(resp, 16);
    resp[16] = (uint8_t)(cc & 0xFFU);
    resp[17] = (uint8_t)(cc >> 8);
    return ce_mfc_encrypt_resp(s, resp, 18, enc_out, par_out);
}

bool ce_mfc_build_read_resp(ce_mfc_t *s, uint8_t block,
                            uint8_t enc_out[18], uint8_t par_out[18])
{
    uint8_t resp[16];
    (void)ce_mfc_block_data(block, resp);                /* deterministic image  */
    return ce_mfc_build_read_resp_from(s, resp, enc_out, par_out);
}

bool ce_mfc_build_read_resp_real(ce_mfc_t *s, const uint8_t block_data[16],
                                 uint8_t enc_out[18], uint8_t par_out[18])
{
    return ce_mfc_build_read_resp_from(s, block_data, enc_out, par_out);
}
