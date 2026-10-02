/*
 * crypto1.c - MIFARE Classic Crypto1 stream cipher (Phase A)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Crypto1 algorithm itself is from Proxmark3 (RfidResearchGroup):
 *   https://github.com/RfidResearchGroup/proxmark3.git
 *
 * The implementation uses a caller-owned static-state Crypto1 structure and
 * byte-level stream helpers. See COPYING.txt for the project license.
 */
#include "crypto1.h"

/* --- parity helpers --------------------------------------------------------
 * even_parity32(x) = XOR of all bits of x (the crapto1 LFSR feedback bit).
 * odd_parity8(b)   = ISO14443A odd parity bit: 1 when popcount(b) is even. */
static inline uint8_t even_parity32(uint32_t x) {
    return (uint8_t)(__builtin_parity(x) & 1U);
}
static inline uint8_t odd_parity8_i(uint8_t b) {
    return (uint8_t)((__builtin_parity((unsigned)b) ^ 1U) & 1U);
}

uint8_t crypto1_odd_parity8(uint8_t b) {
    return odd_parity8_i(b);
}

#define LF_POLY_ODD   (0x29CE5CUL)
#define LF_POLY_EVEN  (0x870804UL)
#define C1_BIT(x, n)  ((uint8_t)(((x) >> (n)) & 1U))
#define BEBIT(x, n)   C1_BIT((x), (n) ^ 24)
#define SWAPENDIAN(x) \
    ((x) = ((x) >> 8 & 0xff00ffUL) | ((x) & 0xff00ffUL) << 8, (x) = (x) >> 16 | (x) << 16)

void crypto1_init(Crypto1 *c, uint64_t key) {
    c->even = 0;
    c->odd  = 0;
    for (int8_t i = 47; i > 0; i -= 2) {
        c->odd  = c->odd  << 1 | C1_BIT(key, (i - 1) ^ 7);
        c->even = c->even << 1 | C1_BIT(key, i ^ 7);
    }
}

static uint32_t crypto1_filter(uint32_t in) {
    uint32_t out = 0;
    out  = 0xf22c0UL >> (in & 0xf) & 16;
    out |= 0x6c9c0UL >> (in >> 4 & 0xf) & 8;
    out |= 0x3c8b0UL >> (in >> 8 & 0xf) & 4;
    out |= 0x1e458UL >> (in >> 12 & 0xf) & 2;
    out |= 0x0d938UL >> (in >> 16 & 0xf) & 1;
    return C1_BIT(0xEC57E80AUL, out);
}

uint8_t crypto1_bit(Crypto1 *c, uint8_t in, int is_encrypted) {
    uint8_t  out  = (uint8_t)crypto1_filter(c->odd);
    uint32_t feed = out & (uint32_t)(!!is_encrypted);
    feed ^= (uint32_t)(!!in);
    feed ^= LF_POLY_ODD  & c->odd;
    feed ^= LF_POLY_EVEN & c->even;
    c->even = c->even << 1 | even_parity32(feed);
    /* Swap odd/even halves. */
    uint32_t t = c->odd;
    c->odd  = c->even;
    c->even = t;
    return out;
}

uint8_t crypto1_byte(Crypto1 *c, uint8_t in, int is_encrypted) {
    uint8_t out = 0;
    for (uint8_t i = 0; i < 8; i++) {
        out |= (uint8_t)(crypto1_bit(c, C1_BIT(in, i), is_encrypted) << i);
    }
    return out;
}

uint32_t crypto1_word(Crypto1 *c, uint32_t in, int is_encrypted) {
    uint32_t out = 0;
    for (uint8_t i = 0; i < 32; i++) {
        out |= (uint32_t)crypto1_bit(c, BEBIT(in, i), is_encrypted) << (24 ^ i);
    }
    return out;
}

/* ---- LFSR rollback primitives ---------------------------------------------
 * Exact inverse of one crypto1_bit()/crypto1_byte()/crypto1_word() step.
 * Uses the Proxmark3 crapto1 algorithm. These require
 * the static crypto1_filter() and the odd/even layout owned by this file, so
 * they are defined here rather than in crypto1_recover.c. */
uint8_t crypto1_lfsr_rollback_bit(Crypto1 *c, uint32_t in, int fb) {
    int out;
    uint8_t  ret;
    uint32_t t;

    c->odd &= 0xffffffUL;
    t       = c->odd;
    c->odd  = c->even;
    c->even = t;

    out  = (int)(c->even & 1U);
    out ^= (int)(LF_POLY_EVEN & (c->even >>= 1));
    out ^= (int)(LF_POLY_ODD  & c->odd);
    out ^= (int)(!!in);
    out ^= (int)((ret = (uint8_t)crypto1_filter(c->odd)) & (!!fb));

    c->even |= (uint32_t)even_parity32((uint32_t)out) << 23;
    return ret;
}

uint8_t crypto1_lfsr_rollback_byte(Crypto1 *c, uint8_t in, int fb) {
    uint8_t ret = 0;
    for (int i = 7; i >= 0; i--) {
        ret |= (uint8_t)(crypto1_lfsr_rollback_bit(c, C1_BIT(in, i), fb) << i);
    }
    return ret;
}

uint32_t crypto1_lfsr_rollback_word(Crypto1 *c, uint32_t in, int fb) {
    uint32_t ret = 0;
    for (int i = 31; i >= 0; i--) {
        ret |= (uint32_t)crypto1_lfsr_rollback_bit(c, BEBIT(in, i), fb) << (24 ^ i);
    }
    return ret;
}

uint32_t crypto1_prng_successor(uint32_t x, uint32_t n) {
    SWAPENDIAN(x);
    while (n--) {
        x = x >> 1 | (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) << 31;
    }
    return SWAPENDIAN(x);
}

static uint32_t be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

uint32_t crypto1_reader_answer(Crypto1 *c, uint64_t key, uint32_t cuid,
                               const uint8_t nt[4], const uint8_t nr[4],
                               uint8_t out[8], uint8_t out_par[8], bool is_nested) {
    uint32_t nt_num = be32(nt);

    crypto1_init(c, key);
    if (is_nested) {
        /* Nested auth: nt is the ENCRYPTED nonce; recover the plaintext nonce
         * and set the cipher state in one step. */
        nt_num = crypto1_word(c, nt_num ^ cuid, 1) ^ nt_num;
    } else {
        crypto1_word(c, nt_num ^ cuid, 0);   /* first (non-nested) auth */
    }

    /* {Nr}: reader nonce, encrypted; the plaintext nonce is fed into the LFSR */
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t enc = (uint8_t)(crypto1_byte(c, nr[i], 0) ^ nr[i]);
        out[i]     = enc;
        out_par[i] = (uint8_t)((crypto1_filter(c->odd) ^ odd_parity8_i(nr[i])) & 1U);
    }

    /* Capture the plaintext nonce before the {ar} prng advances it, so the
     * caller can validate the tag's {at} == suc^96(nt). */
    uint32_t nt_plain = nt_num;

    /* {ar}: answer to tag = suc^64(nt), encrypted with keystream only */
    nt_num = crypto1_prng_successor(nt_num, 32);
    for (uint8_t i = 4; i < 8; i++) {
        nt_num = crypto1_prng_successor(nt_num, 8);
        uint8_t p   = (uint8_t)nt_num;
        uint8_t enc = (uint8_t)(crypto1_byte(c, 0, 0) ^ p);
        out[i]     = enc;
        out_par[i] = (uint8_t)((crypto1_filter(c->odd) ^ odd_parity8_i(p)) & 1U);
    }

    return nt_plain;
}

uint8_t crypto1_encrypt_byte(Crypto1 *c, uint8_t plain, uint8_t *enc_par) {
    uint8_t enc = (uint8_t)(crypto1_byte(c, 0, 0) ^ plain);
    if (enc_par) {
        *enc_par = (uint8_t)((crypto1_filter(c->odd) ^ odd_parity8_i(plain)) & 1U);
    }
    return enc;
}

uint8_t crypto1_decrypt_byte(Crypto1 *c, uint8_t cipher, uint8_t *ks_par) {
    uint8_t plain = (uint8_t)(crypto1_byte(c, 0, 0) ^ cipher);
    if (ks_par) {
        *ks_par = (uint8_t)(crypto1_filter(c->odd) & 1U);
    }
    return plain;
}
