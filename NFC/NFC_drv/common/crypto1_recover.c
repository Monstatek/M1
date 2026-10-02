/*
 * crypto1_recover.c - MIFARE Classic Crypto1 card-only recovery FOUNDATION
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See crypto1_recover.h for provenance, scope and bit/byte conventions.
 * Not called from the live polling path in this phase.
 */
#include "crypto1_recover.h"
#include "crypto1.h"

/* even parity of a byte = XOR of its 8 bits (the ISO14443A "even" parity). */
static inline uint8_t even_parity_byte(uint8_t b) {
    return (uint8_t)(__builtin_parity((unsigned)b) & 1U);
}

/* single bit of a word */
static inline uint8_t bit_at(uint32_t x, unsigned n) {
    return (uint8_t)((x >> n) & 1U);
}

uint32_t crypto1_recover_decrypt_nt_enc(uint32_t cuid, uint32_t nt_enc,
                                        uint64_t known_key) {
    /* Mathematically identical to the forward-keystream recovery already used
     * by crypto1_reader_answer() for nested auth; both compute nt = nt_enc ^ ks.
     * Using the rollback form here exercises the shared rollback primitive. */
    Crypto1 t;
    crypto1_init(&t, known_key);
    crypto1_word(&t, nt_enc ^ cuid, 1);
    return nt_enc ^ crypto1_lfsr_rollback_word(&t, nt_enc ^ cuid, 1);
}

bool crypto1_recover_nonce_matches_parity(uint32_t nt, uint32_t ks,
                                          uint8_t nt_par_enc) {
    /* Validate the encrypted parity bits for the recovered nonce. */
    return (even_parity_byte((uint8_t)(nt >> 24)) ==
            (uint8_t)(((nt_par_enc >> 3) & 1U) ^ bit_at(ks, 16))) &&
           (even_parity_byte((uint8_t)(nt >> 16)) ==
            (uint8_t)(((nt_par_enc >> 2) & 1U) ^ bit_at(ks, 8))) &&
           (even_parity_byte((uint8_t)(nt >> 8)) ==
            (uint8_t)(((nt_par_enc >> 1) & 1U) ^ bit_at(ks, 0)));
}

bool crypto1_recover_is_weak_prng_nonce(uint32_t nonce) {
    /* Detect a weak-PRNG nonce. */
    if (nonce == 0) return false;
    uint16_t x = (uint16_t)(nonce >> 16);
    x = (uint16_t)((x & 0xff) << 8 | x >> 8);
    for (uint8_t i = 0; i < 16; i++) {
        x = (uint16_t)(x >> 1 | (x ^ x >> 2 ^ x >> 3 ^ x >> 5) << 15);
    }
    x = (uint16_t)((x & 0xff) << 8 | x >> 8);
    return x == (uint16_t)(nonce & 0xFFFF);
}

crypto1_recover_status_t crypto1_recover_nonce_distance(uint32_t nt_from,
                                                        uint32_t nt_to,
                                                        uint16_t *out_distance) {
    if (out_distance == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    if (!crypto1_recover_is_weak_prng_nonce(nt_from) ||
        !crypto1_recover_is_weak_prng_nonce(nt_to)) {
        return CRYPTO1_RECOVER_INVALID_INPUT;
    }
    /* The weak PRNG is a 16-bit LFSR with period 65535; bound the search to one
     * full period. suc^0 == identity is checked first so equal nonces give 0. */
    uint32_t x = nt_from;
    for (uint32_t d = 0; d < 65536U; d++) {
        if (x == nt_to) {
            *out_distance = (uint16_t)d;
            return CRYPTO1_RECOVER_OK;
        }
        x = crypto1_prng_successor(x, 1);
    }
    return CRYPTO1_RECOVER_NO_CANDIDATE;
}

crypto1_recover_status_t crypto1_recover_classify_static(const uint32_t *nonces,
                                                         size_t count,
                                                         bool *out_is_static) {
    if (nonces == NULL || out_is_static == NULL || count < 2) {
        return CRYPTO1_RECOVER_INVALID_INPUT;
    }
    bool all_equal = true;
    for (size_t i = 1; i < count; i++) {
        if (nonces[i] != nonces[0]) { all_equal = false; break; }
    }
    *out_is_static = all_equal;
    return CRYPTO1_RECOVER_OK;
}

crypto1_recover_status_t crypto1_recover_select_weak_nonces(const uint32_t *nonces,
                                                            size_t count,
                                                            uint32_t *out,
                                                            size_t out_capacity,
                                                            size_t *out_count) {
    if (out_count == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    if (count > 0 && nonces == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    if (out_capacity > 0 && out == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;

    size_t found = 0;
    bool overflowed = false;
    for (size_t i = 0; i < count; i++) {
        if (!crypto1_recover_is_weak_prng_nonce(nonces[i])) continue;
        if (found < out_capacity) {
            out[found] = nonces[i];
        } else {
            overflowed = true;
        }
        found++;
    }
    *out_count = found;
    return overflowed ? CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY
                      : CRYPTO1_RECOVER_OK;
}

bool crypto1_recover_key_consistent(uint32_t cuid, uint32_t nt_enc,
                                    uint8_t nt_par_enc, uint64_t candidate_key) {
    uint32_t nt = crypto1_recover_decrypt_nt_enc(cuid, nt_enc, candidate_key);
    uint32_t ks = nt ^ nt_enc;
    if (!crypto1_recover_is_weak_prng_nonce(nt)) return false;
    return crypto1_recover_nonce_matches_parity(nt, ks, nt_par_enc);
}

/* ===========================================================================
 * GENUINE STATE RECOVERY — direct port of Proxmark3 lfsr_recovery64.
 *
 *   RfidResearchGroup/proxmark3  commit ee8b9ca74b10536db246286179183e0f1d89770e
 *   common/crapto1/crapto1.c  (lfsr_recovery64, extend_table_simple, tables)
 *   common/crapto1/crypto1.c  (crypto1_get_lfsr)
 *   Copyright (C) 2008-2014 bla <blapost@gmail.com>; (C) Proxmark3 contributors.
 *   License: GPL-3.0-or-later.
 *
 * Adapted (see crypto1_recover.h): the 256 KiB on-stack table[1<<16] and the
 * calloc'd state list become a caller-supplied workspace and a caller-owned
 * output buffer with explicit workspace/capacity status; filter() is replicated
 * locally (identical to the published crapto1 filter and to crypto1.c's private
 * copy). Core algorithm, tables and bit ordering are unchanged from upstream.
 * ===========================================================================*/

#define REC_LF_POLY_ODD  (0x29CE5CUL)
#define REC_LF_POLY_EVEN (0x870804UL)
#define REC_BIT(x, n)    (((x) >> (n)) & 1U)
#define REC_BEBIT(x, n)  REC_BIT((x), (n) ^ 24)

/* published crapto1 non-linear filter (bit-identical to crypto1.c's copy) */
static inline uint32_t rec_filter(uint32_t x) {
    uint32_t f;
    f  = 0xf22c0UL >> (x       & 0xf) & 16;
    f |= 0x6c9c0UL >> (x >>  4 & 0xf) &  8;
    f |= 0x3c8b0UL >> (x >>  8 & 0xf) &  4;
    f |= 0x1e458UL >> (x >> 12 & 0xf) &  2;
    f |= 0x0d938UL >> (x >> 16 & 0xf) &  1;
    return (0xEC57E80AUL >> f) & 1U;
}
static inline uint8_t rec_evenparity32(uint32_t x) {
    return (uint8_t)(__builtin_parity(x) & 1U);
}

/* upstream extend_table_simple — in-place table growth, no alloc/recursion */
static inline void rec_extend_table_simple(uint32_t *tbl, uint32_t **end, int bit) {
    for (*tbl <<= 1; tbl <= *end; *++tbl <<= 1) {
        uint8_t tf = (uint8_t)rec_filter(*tbl);
        if (tf ^ (uint8_t)rec_filter(*tbl | 1)) {          /* replace */
            *tbl |= tf ^ (uint32_t)bit;
        } else if (tf == (uint8_t)bit) {                   /* insert  */
            *++*end = *++tbl;
            *tbl = tbl[-1] | 1;
        } else {                                           /* drop    */
            *tbl-- = *(*end)--;
        }
    }
}

/* upstream crapto1 recovery tables (common/crapto1/crapto1.c, verbatim) */
static const uint32_t REC_S1[] = {
    0x62141, 0x310A0, 0x18850, 0x0C428, 0x06214, 0x0310A, 0x85E30, 0xC69AD,
    0x634D6, 0xB5CDE, 0xDE8DA, 0x6F46D, 0xB3C83, 0x59E41, 0xA8995, 0xD027F,
    0x6813F, 0x3409F, 0x9E6FA
};
static const uint32_t REC_S2[] = {
    0x3A557B00, 0x5D2ABD80, 0x2E955EC0, 0x174AAF60, 0x0BA557B0, 0x05D2ABD8,
    0x0449DE68, 0x048464B0, 0x42423258, 0x278192A8, 0x156042D0, 0x0AB02168,
    0x43F89B30, 0x61FC4D98, 0x765EAD48, 0x7D8FDD20, 0x7EC7EE90, 0x7F63F748,
    0x79117020
};
static const uint32_t REC_T1[] = {
    0x4F37D, 0x279BE, 0x97A6A, 0x4BD35, 0x25E9A, 0x12F4D, 0x097A6, 0x80D66,
    0xC4006, 0x62003, 0xB56B4, 0x5AB5A, 0xA9318, 0xD0F39, 0x6879C, 0xB057B,
    0x582BD, 0x2C15E, 0x160AF, 0x8F6E2, 0xC3DC4, 0xE5857, 0x72C2B, 0x39615,
    0x98DBF, 0xC806A, 0xE0680, 0x70340, 0x381A0, 0x98665, 0x4C332, 0xA272C
};
static const uint32_t REC_T2[] = {
    0x3C88B810, 0x5E445C08, 0x2982A580, 0x14C152C0, 0x4A60A960, 0x253054B0,
    0x52982A58, 0x2FEC9EA8, 0x1156C4D0, 0x08AB6268, 0x42F53AB0, 0x217A9D58,
    0x161DC528, 0x0DAE6910, 0x46D73488, 0x25CB11C0, 0x52E588E0, 0x6972C470,
    0x34B96238, 0x5CFC3A98, 0x28DE96C8, 0x12CFC0E0, 0x4967E070, 0x64B3F038,
    0x74F97398, 0x7CDC3248, 0x38CE92A0, 0x1C674950, 0x0E33A4A8, 0x01B959D0,
    0x40DCACE8, 0x26CEDDF0
};
static const uint32_t REC_C1[] = { 0x846B5, 0x4235A, 0x211AD };
static const uint32_t REC_C2[] = { 0x1A822E0, 0x21A822E0, 0x21A822E0 };

crypto1_recover_status_t crypto1_recover_lfsr64(uint32_t ks2, uint32_t ks3,
                                                uint32_t *workspace,
                                                size_t workspace_words,
                                                Crypto1 *out_states,
                                                size_t out_capacity,
                                                size_t *out_count,
                                                crypto1_recover_progress_fn progress,
                                                void *progress_ctx) {
    if (out_count == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    *out_count = 0;
    if (workspace == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    if (out_capacity > 0 && out_states == NULL) return CRYPTO1_RECOVER_INVALID_INPUT;
    if (workspace_words < CRYPTO1_RECOVER64_WORKSPACE_WORDS) {
        return CRYPTO1_RECOVER_INSUFFICIENT_WORKSPACE;
    }

    uint32_t *table = workspace;      /* upstream table[1<<16] */
    uint32_t *tail;
    uint8_t  oks[32], eks[32], hi[32];
    uint32_t low = 0, win = 0;
    int i, j;
    size_t   found = 0;
    bool     overflow = false;

    for (i = 30; i >= 0; i -= 2) {
        oks[i >> 1]      = (uint8_t)REC_BEBIT(ks2, i);
        oks[16 + (i >> 1)] = (uint8_t)REC_BEBIT(ks3, i);
    }
    for (i = 31; i >= 0; i -= 2) {
        eks[i >> 1]      = (uint8_t)REC_BEBIT(ks2, i);
        eks[16 + (i >> 1)] = (uint8_t)REC_BEBIT(ks3, i);
    }

    for (i = 0xfffff; i >= 0; --i) {          /* exactly 2^20 iterations */
        /* cooperative yield/abort: ~64 checks over the full sweep, cheap and
         * deterministic; lets the on-device task refresh the IWDG / cancel. */
        if (progress != NULL && ((uint32_t)i & 0x3FFFu) == 0) {
            if (!progress(progress_ctx, (uint32_t)i)) {
                *out_count = found;
                return CRYPTO1_RECOVER_ABORTED;
            }
        }
        if (rec_filter((uint32_t)i) != oks[0]) continue;

        *(tail = table) = (uint32_t)i;
        for (j = 1; tail >= table && j < 29; ++j)
            rec_extend_table_simple(table, &tail, oks[j]);

        if (tail < table) continue;

        for (j = 0; j < 19; ++j)
            low = low << 1 | rec_evenparity32((uint32_t)i & REC_S1[j]);
        for (j = 0; j < 32; ++j)
            hi[j] = rec_evenparity32((uint32_t)i & REC_T1[j]);

        for (; tail >= table; --tail) {
            for (j = 0; j < 3; ++j) {
                *tail = *tail << 1;
                *tail |= rec_evenparity32(((uint32_t)i & REC_C1[j]) ^ (*tail & REC_C2[j]));
                if (rec_filter(*tail) != oks[29 + j]) goto continue2;
            }

            for (j = 0; j < 19; ++j)
                win = win << 1 | rec_evenparity32(*tail & REC_S2[j]);

            win ^= low;
            for (j = 0; j < 32; ++j) {
                win = win << 1 ^ hi[j] ^ rec_evenparity32(*tail & REC_T2[j]);
                if (rec_filter(win) != eks[j]) goto continue2;
            }

            {
                uint32_t t   = *tail << 1 | rec_evenparity32(REC_LF_POLY_EVEN & *tail);
                uint32_t odd = t ^ rec_evenparity32(REC_LF_POLY_ODD & win);
                if (found < out_capacity) {
                    out_states[found].odd  = odd;
                    out_states[found].even = win;
                } else {
                    overflow = true;
                }
                found++;
            }
continue2:
            ;
        }
    }

    *out_count = found;
    if (overflow) return CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY;
    if (found == 0) return CRYPTO1_RECOVER_NO_CANDIDATE;
    return CRYPTO1_RECOVER_OK;
}

uint64_t crypto1_recover_state_to_key(const Crypto1 *state) {
    uint64_t lfsr = 0;
    if (state == NULL) return 0;
    for (int i = 23; i >= 0; --i) {
        lfsr = lfsr << 1 | REC_BIT(state->odd,  i ^ 3);
        lfsr = lfsr << 1 | REC_BIT(state->even, i ^ 3);
    }
    return lfsr;
}

/* Forward-cipher keystream of one mfkey64 transcript under `key`, so a candidate
 * key can be VERIFIED against the observed (ks2, ks3). Mirrors the encrypt side
 * of the auth the rollback in crypto1_recover_mfkey64 inverts. */
static void mfkey64_forward_ks(uint64_t key, uint32_t uid, uint32_t nt,
                               uint32_t nr_enc, uint32_t *ks2, uint32_t *ks3) {
    Crypto1 c;
    crypto1_init(&c, key);
    crypto1_word(&c, uid ^ nt, 0);
    crypto1_word(&c, nr_enc, 1);
    *ks2 = crypto1_word(&c, 0, 0);
    *ks3 = crypto1_word(&c, 0, 0);
}

crypto1_recover_status_t crypto1_recover_mfkey64(uint32_t uid, uint32_t nt,
                                                 uint32_t nr_enc, uint32_t ar_enc,
                                                 uint32_t at_enc,
                                                 uint32_t *workspace,
                                                 size_t workspace_words,
                                                 uint64_t *out_key,
                                                 size_t *out_candidate_count,
                                                 crypto1_recover_progress_fn progress,
                                                 void *progress_ctx) {
    if (out_key == NULL || out_candidate_count == NULL) {
        return CRYPTO1_RECOVER_INVALID_INPUT;
    }
    *out_candidate_count = 0;

    /* Derive the 64 keystream bits from the transcript (Proxmark mfkey64). */
    uint32_t p64 = crypto1_prng_successor(nt, 64);
    uint32_t ks2 = ar_enc ^ p64;
    uint32_t ks3 = at_enc ^ crypto1_prng_successor(p64, 32);

    Crypto1 cand[CRYPTO1_RECOVER_MFKEY64_MAX_CANDIDATES];
    size_t  n = 0;
    crypto1_recover_status_t st =
        crypto1_recover_lfsr64(ks2, ks3, workspace, workspace_words,
                               cand, CRYPTO1_RECOVER_MFKEY64_MAX_CANDIDATES, &n,
                               progress, progress_ctx);
    if (st == CRYPTO1_RECOVER_INVALID_INPUT ||
        st == CRYPTO1_RECOVER_INSUFFICIENT_WORKSPACE ||
        st == CRYPTO1_RECOVER_ABORTED) {
        *out_candidate_count = n;
        return st;                       /* propagate workspace/NULL/abort     */
    }
    if (st == CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY) {
        *out_candidate_count = n;        /* more states than we can vet safely  */
        return CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY;
    }
    if (st != CRYPTO1_RECOVER_OK || n == 0) {
        return CRYPTO1_RECOVER_NO_CANDIDATE;
    }

    /* Roll each state back to a key and VERIFY it reproduces (ks2, ks3);
     * collect distinct verified keys. */
    uint64_t keys[CRYPTO1_RECOVER_MFKEY64_MAX_CANDIDATES];
    size_t   distinct = 0;
    for (size_t k = 0; k < n; k++) {
        Crypto1 s = cand[k];
        crypto1_lfsr_rollback_word(&s, 0, 0);
        crypto1_lfsr_rollback_word(&s, 0, 0);
        crypto1_lfsr_rollback_word(&s, nr_enc, 1);
        crypto1_lfsr_rollback_word(&s, uid ^ nt, 0);
        uint64_t key = crypto1_recover_state_to_key(&s);

        uint32_t vks2 = 0, vks3 = 0;
        mfkey64_forward_ks(key, uid, nt, nr_enc, &vks2, &vks3);
        if (vks2 != ks2 || vks3 != ks3) continue;   /* prune false positive    */

        bool seen = false;
        for (size_t d = 0; d < distinct; d++) {
            if (keys[d] == key) { seen = true; break; }
        }
        if (!seen) keys[distinct++] = key;
    }

    *out_candidate_count = distinct;
    if (distinct == 0) return CRYPTO1_RECOVER_NO_CANDIDATE;
    if (distinct > 1)  return CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY; /* ambiguous */
    *out_key = keys[0];
    return CRYPTO1_RECOVER_OK;
}
