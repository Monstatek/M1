/*
 * crypto1_recover_test.c - host test for the Crypto1 card-only recovery
 *                          foundation (crypto1_recover.c + crypto1.c rollback).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This is a HOST test (not built into firmware). Build & run from the repo root:
 *
 *   cc -std=c11 -Wall -Wextra -O1 \
 *      NFC/NFC_drv/common/crypto1.c \
 *      NFC/NFC_drv/common/crypto1_recover.c \
 *      NFC/NFC_drv/common/test/crypto1_recover_test.c \
 *      -I NFC/NFC_drv/common -o /tmp/c1rec && /tmp/c1rec
 *
 * With sanitizers (recommended):
 *   cc -std=c11 -Wall -Wextra -O1 -fsanitize=address,undefined \
 *      -fno-sanitize-recover=all  ...same sources...  -o /tmp/c1rec && /tmp/c1rec
 *
 * Validation strategy (documented so nothing is a tautology):
 *   - Rollback bit/byte/word are checked by ROUND-TRIP invariants (forward then
 *     rollback restores the exact cipher state) — independent of any oracle.
 *   - crypto1_recover_decrypt_nt_enc (rollback path) is cross-checked against the
 *     PRE-EXISTING, hardware-validated forward path used by crypto1_reader_answer
 *     (nt = nt_enc ^ crypto1_word(init(key), nt_enc^cuid, 1)). Independent code.
 *   - crypto1_prng_successor is cross-checked against an INDEPENDENT array-based
 *     LFSR reference (ref_prng_successor) transcribed from the published Proxmark
 *     recurrence, plus the composition law suc^a(suc^b(x)) == suc^(a+b)(x).
 *   - Remaining functions: direct behavioural + error-path assertions.
 */
#include "crypto1.h"
#include "crypto1_recover.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) { g_pass++; }                                                 \
        else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

/* deterministic PRNG for test inputs (no rand(), fully reproducible) */
static uint32_t xs = 0x1234567u;
static uint32_t xorshift(void) {
    xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs;
}

/* ---- INDEPENDENT reference implementations (different structure) ---------- */
static uint32_t ref_swapendian(uint32_t x) {
    x = (x >> 8 & 0xff00ffu) | (x & 0xff00ffu) << 8;
    return x >> 16 | x << 16;
}
/* Array-based 32-bit shift register with 16-bit-spaced feedback taps
 * (bits 16,18,19,21) — the published MIFARE weak PRNG, coded differently from
 * crypto1_prng_successor so agreement is meaningful. */
static uint32_t ref_prng_successor(uint32_t nonce, uint32_t n) {
    int b[32];
    uint32_t x = ref_swapendian(nonce);
    for (int i = 0; i < 32; i++) b[i] = (int)((x >> i) & 1u);
    while (n--) {
        int fb = b[16] ^ b[18] ^ b[19] ^ b[21];
        for (int i = 0; i < 31; i++) b[i] = b[i + 1];
        b[31] = fb;
    }
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) r |= (uint32_t)b[i] << i;
    return ref_swapendian(r);
}

/* forward-path nested nonce recovery, as used by crypto1_reader_answer() */
static uint32_t forward_decrypt(uint32_t cuid, uint32_t nt_enc, uint64_t key) {
    Crypto1 c;
    crypto1_init(&c, key);
    return crypto1_word(&c, nt_enc ^ cuid, 1) ^ nt_enc;
}

static int state_eq(const Crypto1 *a, const Crypto1 *b) {
    return a->odd == b->odd && a->even == b->even;
}

/* ========================================================================== */
/* 1 & 2: forward/rollback round-trip, bit/byte/word, boundary inputs         */
static void test_rollback_roundtrip(void) {
    printf("[1/2] rollback round-trip (bit/byte/word) + boundaries\n");
    const uint64_t keys[] = {0x000000000000ull, 0xFFFFFFFFFFFFull,
                             0xA0A1A2A3A4A5ull, 0x123456789ABCull};
    const uint32_t words[] = {0x00000000u, 0xFFFFFFFFu, 0xA5A5A5A5u,
                              0x00000001u, 0x80000000u, 0xDEADBEEFu};
    for (unsigned ki = 0; ki < sizeof(keys) / sizeof(keys[0]); ki++) {
        for (unsigned wi = 0; wi < sizeof(words) / sizeof(words[0]); wi++) {
            for (int fb = 0; fb <= 1; fb++) {
                uint32_t in = words[wi];
                /* --- word --- */
                Crypto1 c0; crypto1_init(&c0, keys[ki]);
                Crypto1 c = c0;
                crypto1_word(&c, in, fb);
                crypto1_lfsr_rollback_word(&c, in, fb);
                CHECK(state_eq(&c, &c0), "word round-trip restores state");

                /* --- byte (low 8 bits of in) --- */
                c = c0;
                crypto1_byte(&c, (uint8_t)in, fb);
                crypto1_lfsr_rollback_byte(&c, (uint8_t)in, fb);
                CHECK(state_eq(&c, &c0), "byte round-trip restores state");

                /* --- bit --- */
                c = c0;
                uint8_t inb = (uint8_t)(in & 1u);
                crypto1_bit(&c, inb, fb);
                crypto1_lfsr_rollback_bit(&c, inb, fb);
                CHECK(state_eq(&c, &c0), "bit round-trip restores state");
            }
        }
    }
    /* multi-step word round-trip (state advanced several words then unwound) */
    Crypto1 base; crypto1_init(&base, 0x0123456789ABull);
    Crypto1 c = base;
    uint32_t seq[4] = {0xCAFEBABEu, 0x00000000u, 0xFFFFFFFFu, 0x5A5A5A5Au};
    for (int i = 0; i < 4; i++) crypto1_word(&c, seq[i], 1);
    for (int i = 3; i >= 0; i--) crypto1_lfsr_rollback_word(&c, seq[i], 1);
    CHECK(state_eq(&c, &base), "multi-word round-trip restores state");
}

/* 3: PRNG successor known values (independent oracle + composition law)       */
static void test_prng_successor(void) {
    printf("[3]   prng_successor vs independent LFSR + composition\n");
    for (int i = 0; i < 20000; i++) {
        uint32_t x = xorshift();
        uint32_t n = xorshift() % 200u;
        CHECK(crypto1_prng_successor(x, n) == ref_prng_successor(x, n),
              "prng_successor matches independent reference");
    }
    /* composition: suc^a(suc^b(x)) == suc^(a+b)(x) */
    for (int i = 0; i < 5000; i++) {
        uint32_t x = xorshift();
        uint32_t a = xorshift() % 97u, b = xorshift() % 97u;
        CHECK(crypto1_prng_successor(crypto1_prng_successor(x, b), a) ==
              crypto1_prng_successor(x, a + b),
              "prng_successor composition law");
    }
    /* n==0 is identity */
    CHECK(crypto1_prng_successor(0xDEADBEEFu, 0) == 0xDEADBEEFu,
          "prng_successor(x,0) == x");
}

/* 7: weak-PRNG classification                                                 */
static void test_weak_prng(void) {
    printf("[7]   weak-PRNG nonce classification\n");
    CHECK(!crypto1_recover_is_weak_prng_nonce(0), "nonce 0 is not weak");
    int weak_ok = 0, broken_ok = 0, trials = 3000;
    for (int i = 0; i < trials; i++) {
        uint32_t v = xorshift();
        /* after >=16 LFSR steps every bit is LFSR-consistent -> weak nonce */
        uint32_t w = crypto1_prng_successor(v, 32);
        if (crypto1_recover_is_weak_prng_nonce(w)) weak_ok++;
        /* perturb a low bit -> should (almost always) break weakness */
        uint32_t broken = w ^ (1u << (xorshift() % 16));
        if (broken != w && !crypto1_recover_is_weak_prng_nonce(broken)) broken_ok++;
    }
    CHECK(weak_ok == trials, "all suc^32(v) nonces classified weak");
    /* perturbation breaks weakness in the overwhelming majority of cases */
    CHECK(broken_ok > (trials * 9) / 10, "perturbed nonces classified non-weak");
}

/* 4: nonce-distance known values                                              */
static void test_nonce_distance(void) {
    printf("[4]   nonce distance\n");
    uint16_t d;
    for (int i = 0; i < 2000; i++) {
        uint32_t nt = crypto1_prng_successor(xorshift(), 32); /* weak */
        uint16_t want = (uint16_t)(xorshift() % 4096u);
        uint32_t to = crypto1_prng_successor(nt, want);
        crypto1_recover_status_t st = crypto1_recover_nonce_distance(nt, to, &d);
        CHECK(st == CRYPTO1_RECOVER_OK && d == want, "distance == known step count");
    }
    uint32_t ntw = crypto1_prng_successor(0x11111111u, 32);
    CHECK(crypto1_recover_nonce_distance(ntw, ntw, &d) == CRYPTO1_RECOVER_OK &&
          d == 0, "distance(nt,nt) == 0");
    /* invalid: NULL out, non-weak args */
    CHECK(crypto1_recover_nonce_distance(ntw, ntw, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "distance NULL out rejected");
    CHECK(crypto1_recover_nonce_distance(0, ntw, &d) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "distance non-weak arg rejected");
}

/* 5 & 6: state recovery vector (decrypt) + wrong key rejected                 */
static void test_decrypt_and_key_consistency(void) {
    printf("[5/6] decrypt_nt_enc cross-path + key acceptance/rejection\n");
    const uint64_t key = 0xA0B1C2D3E4F5ull;
    const uint64_t wrong_key = 0xA0B1C2D3E4F4ull;
    const uint32_t cuid = 0xCD7690A3u;

    /* decrypt (rollback path) must equal the pre-existing forward path */
    for (int i = 0; i < 20000; i++) {
        uint32_t nt_enc = xorshift();
        uint32_t got = crypto1_recover_decrypt_nt_enc(cuid, nt_enc, key);
        uint32_t want = forward_decrypt(cuid, nt_enc, key);
        CHECK(got == want, "decrypt_nt_enc matches forward reader_answer path");
    }

    /* find an nt_enc whose decrypt under `key` is a weak nonce, then build the
     * encrypted parity a real card would send, and assert acceptance. */
    int built = 0;
    for (uint32_t nt_enc = 0; nt_enc < 0x200000u && !built; nt_enc++) {
        uint32_t nt = crypto1_recover_decrypt_nt_enc(cuid, nt_enc, key);
        if (!crypto1_recover_is_weak_prng_nonce(nt)) continue;
        uint32_t ks = nt ^ nt_enc;
        /* consistent encrypted-parity: par_bit = even_parity(nt_byte) ^ ks_bit */
        uint8_t par = 0;
        par |= (uint8_t)((( (__builtin_parity((nt >> 24) & 0xFF)) ) ^ ((ks >> 16) & 1)) << 3);
        par |= (uint8_t)((( (__builtin_parity((nt >> 16) & 0xFF)) ) ^ ((ks >> 8)  & 1)) << 2);
        par |= (uint8_t)((( (__builtin_parity((nt >> 8)  & 0xFF)) ) ^ ((ks >> 0)  & 1)) << 1);

        CHECK(crypto1_recover_key_consistent(cuid, nt_enc, par, key),
              "correct key accepted by key_consistent");
        CHECK(!crypto1_recover_key_consistent(cuid, nt_enc, par, wrong_key),
              "wrong key rejected by key_consistent");
        /* corrupt parity -> correct key now rejected */
        CHECK(!crypto1_recover_key_consistent(cuid, nt_enc, par ^ 0x08, key),
              "corrupt parity rejects correct key");
        built = 1;
    }
    CHECK(built, "constructed a weak-nonce recovery vector");
}

/* 8 & 9: static-nonce classification                                          */
static void test_static_classification(void) {
    printf("[8/9] static-nonce classification\n");
    bool is_static;
    uint32_t same[5] = {0x01020304u, 0x01020304u, 0x01020304u, 0x01020304u,
                        0x01020304u};
    CHECK(crypto1_recover_classify_static(same, 5, &is_static) ==
          CRYPTO1_RECOVER_OK && is_static, "identical samples -> static");
    uint32_t varied[5] = {0x01020304u, 0x01020304u, 0x01020305u, 0x01020304u,
                          0x01020304u};
    CHECK(crypto1_recover_classify_static(varied, 5, &is_static) ==
          CRYPTO1_RECOVER_OK && !is_static, "one differing sample -> not static");
    uint32_t two_same[2] = {0xAAu, 0xAAu};
    CHECK(crypto1_recover_classify_static(two_same, 2, &is_static) ==
          CRYPTO1_RECOVER_OK && is_static, "two identical -> static");
    /* invalid: count < 2, NULL args */
    CHECK(crypto1_recover_classify_static(same, 1, &is_static) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "count<2 rejected");
    CHECK(crypto1_recover_classify_static(NULL, 5, &is_static) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL nonces rejected");
    CHECK(crypto1_recover_classify_static(same, 5, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL out rejected");
}

/* 10 & 11: error paths + capacity enforcement                                 */
static void test_capacity_and_errors(void) {
    printf("[10/11] capacity enforcement + error paths\n");
    /* three weak + two non-weak nonces */
    uint32_t w0 = crypto1_prng_successor(0x22221111u, 32);
    uint32_t w1 = crypto1_prng_successor(0x33334444u, 32);
    uint32_t w2 = crypto1_prng_successor(0x55556666u, 32);
    uint32_t nonces[5] = {w0, 0x00000000u /*non-weak*/, w1,
                          (w2 ^ 1u) /*likely non-weak*/, w2};
    size_t got = 0;
    uint32_t out[8];

    /* ample capacity */
    crypto1_recover_status_t st =
        crypto1_recover_select_weak_nonces(nonces, 5, out, 8, &got);
    CHECK(st == CRYPTO1_RECOVER_OK, "select ample capacity -> OK");
    CHECK(got >= 3, "found at least the 3 weak nonces");

    /* capacity of 1 -> INSUFFICIENT_CAPACITY, but out_count reflects true total */
    got = 999;
    st = crypto1_recover_select_weak_nonces(nonces, 5, out, 1, &got);
    CHECK(st == CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY, "small capacity flagged");
    CHECK(got >= 3, "out_count reports full weak total on overflow");

    /* zero-capacity, non-NULL out_count -> counts only, no write */
    got = 999;
    st = crypto1_recover_select_weak_nonces(nonces, 5, NULL, 0, &got);
    CHECK(st == CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY && got >= 3,
          "zero-capacity counts without writing");

    /* count 0 is valid */
    got = 999;
    st = crypto1_recover_select_weak_nonces(NULL, 0, out, 8, &got);
    CHECK(st == CRYPTO1_RECOVER_OK && got == 0, "count 0 -> OK, 0 found");

    /* NULL out_count -> INVALID_INPUT */
    CHECK(crypto1_recover_select_weak_nonces(nonces, 5, out, 8, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL out_count rejected");
    /* count>0 with NULL nonces -> INVALID_INPUT */
    CHECK(crypto1_recover_select_weak_nonces(NULL, 5, out, 8, &got) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL nonces w/ count>0 rejected");
}

/* 12: determinism (repeat gives identical results)                            */
static void test_determinism(void) {
    printf("[12]  determinism\n");
    const uint64_t key = 0x0102030405006ull;
    const uint32_t cuid = 0x11223344u, nt_enc = 0xA5A5F00Fu;
    uint32_t a = crypto1_recover_decrypt_nt_enc(cuid, nt_enc, key);
    uint32_t b = crypto1_recover_decrypt_nt_enc(cuid, nt_enc, key);
    CHECK(a == b, "decrypt deterministic");
    uint16_t d1, d2;
    uint32_t nt = crypto1_prng_successor(0x77778889u, 32);
    uint32_t to = crypto1_prng_successor(nt, 123);
    crypto1_recover_nonce_distance(nt, to, &d1);
    crypto1_recover_nonce_distance(nt, to, &d2);
    CHECK(d1 == d2 && d1 == 123, "distance deterministic");
}

/* ========================================================================== */
/* GENUINE STATE RECOVERY (Proxmark3 lfsr_recovery64) — coverage item 5.       */

/* 256 KiB caller workspace (file-scope to keep it off the stack). */
static uint32_t g_ws[CRYPTO1_RECOVER64_WORKSPACE_WORDS];

/* Reconstruct the 48-bit key from a recovered state, per Proxmark mfkey64
 * (no encrypted-data rollback, argc==6 path). */
static uint64_t recover_key_from_state(Crypto1 st, uint32_t uid, uint32_t nt,
                                       uint32_t nr_enc) {
    crypto1_lfsr_rollback_word(&st, 0, 0);
    crypto1_lfsr_rollback_word(&st, 0, 0);
    crypto1_lfsr_rollback_word(&st, nr_enc, 1);
    crypto1_lfsr_rollback_word(&st, uid ^ nt, 0);
    return crypto1_recover_state_to_key(&st);
}

/* true iff any recovered candidate reconstructs to `expect_key` */
static int any_candidate_yields(const Crypto1 *cand, size_t n, uint32_t uid,
                                uint32_t nt, uint32_t nr_enc, uint64_t expect_key) {
    for (size_t k = 0; k < n; k++)
        if (recover_key_from_state(cand[k], uid, nt, nr_enc) == expect_key) return 1;
    return 0;
}

static void test_state_recovery(void) {
    printf("[5/13/14] genuine state recovery (lfsr_recovery64)\n");
    Crypto1 cand[64];
    size_t n = 0;
    crypto1_recover_status_t st;

    /* --- Published Proxmark3 vector ---------------------------------------
     * Source: RfidResearchGroup/proxmark3 @ ee8b9ca74b10536db246286179183e0f1d89770e
     *   tools/mfc/card_reader/mfkey_examples.md  ("Communication decryption")
     *   uid 14579f69, nt ce844261, {nr} f8049ccb, {ar} 0525c84f, {at} 9431cc40
     *   -> ks2 73f18ec2, ks3 41c20836 -> Found Key [091e639cb715]           */
    const uint32_t P_uid = 0x14579F69u, P_nt = 0xCE844261u, P_nr = 0xF8049CCBu;
    const uint32_t P_ks2 = 0x73F18EC2u, P_ks3 = 0x41C20836u;
    const uint64_t P_key = 0x091E639CB715ull;

    /* cross-check the published PRNG successors with the M1 prng */
    CHECK(crypto1_prng_successor(P_nt, 64) == 0x76D4468Du, "published nt' matches");
    CHECK(crypto1_prng_successor(crypto1_prng_successor(P_nt, 64), 32) == 0xD5F3C476u,
          "published nt'' matches");

    st = crypto1_recover_lfsr64(P_ks2, P_ks3, g_ws,
                                CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n,
                                NULL, NULL);
    CHECK(st == CRYPTO1_RECOVER_OK, "recover64 OK on Proxmark vector");
    CHECK(n >= 1, "recover64 returned >=1 candidate");
    CHECK(any_candidate_yields(cand, n, P_uid, P_nt, P_nr, P_key),
          "recovered state resolves to published key 091e639cb715");

    /* altered keystream must NOT resolve to the published key */
    n = 0;
    st = crypto1_recover_lfsr64(P_ks2, P_ks3 ^ 1u, g_ws,
                                CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n,
                                NULL, NULL);
    CHECK(!(st == CRYPTO1_RECOVER_OK &&
            any_candidate_yields(cand, n, P_uid, P_nt, P_nr, P_key)),
          "altered keystream does not falsely recover the key");

    /* --- Fixed forward/recovery round trip -------------------------------- */
    const uint64_t F_key = 0xFFFFFFFFFFFFull;
    const uint32_t F_uid = 0xCD7690A3u, F_nt = 0x11223344u, F_nr = 0x55667788u;
    /* derive ks2/ks3 through the pre-existing forward cipher */
    Crypto1 c;
    crypto1_init(&c, F_key);
    crypto1_word(&c, F_uid ^ F_nt, 0);
    crypto1_word(&c, F_nr, 1);
    uint32_t F_ks2 = crypto1_word(&c, 0, 0);
    uint32_t F_ks3 = crypto1_word(&c, 0, 0);
    /* regression guard on the forward path (independently verified constants) */
    CHECK(F_ks2 == 0x69E37EE4u, "forward ks2 matches expected constant");
    CHECK(F_ks3 == 0x7450A433u, "forward ks3 matches expected constant");

    n = 0;
    st = crypto1_recover_lfsr64(F_ks2, F_ks3, g_ws,
                                CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n,
                                NULL, NULL);
    CHECK(st == CRYPTO1_RECOVER_OK && n >= 1, "recover64 OK on forward vector");
    CHECK(any_candidate_yields(cand, n, F_uid, F_nt, F_nr, F_key),
          "round-trip recovers the exact 48-bit key");

    /* determinism: identical count and first state on repeat */
    Crypto1 cand2[64];
    size_t n2 = 0;
    crypto1_recover_lfsr64(F_ks2, F_ks3, g_ws, CRYPTO1_RECOVER64_WORKSPACE_WORDS,
                           cand2, 64, &n2, NULL, NULL);
    CHECK(n2 == n && cand2[0].odd == cand[0].odd && cand2[0].even == cand[0].even,
          "recover64 deterministic across runs");

    /* --- error paths / bounds --------------------------------------------- */
    n = 123;
    CHECK(crypto1_recover_lfsr64(F_ks2, F_ks3, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, NULL, NULL, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL out_count rejected");
    CHECK(crypto1_recover_lfsr64(F_ks2, F_ks3, NULL,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n, NULL, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "NULL workspace rejected");
    CHECK(crypto1_recover_lfsr64(F_ks2, F_ks3, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS - 1, cand, 64, &n, NULL, NULL) ==
          CRYPTO1_RECOVER_INSUFFICIENT_WORKSPACE, "small workspace flagged");
    /* zero output capacity but candidates exist -> INSUFFICIENT_CAPACITY, counted */
    n = 0;
    st = crypto1_recover_lfsr64(F_ks2, F_ks3, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS, NULL, 0, &n, NULL, NULL);
    CHECK(st == CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY && n >= 1,
          "zero capacity counts candidates without writing");
}

/* Phase 2 item 4: end-to-end mfkey64 pipeline (transcript -> verified key).   */
static void test_mfkey64_pipeline(void) {
    printf("[P2.4] end-to-end mfkey64 pipeline (transcript -> verified key)\n");
    uint64_t key = 0;
    size_t   ncand = 0;
    crypto1_recover_status_t st;

    /* Published Proxmark3 vector (mfkey_examples.md @ ee8b9ca):
     *   ./mfkey64 14579f69 ce844261 f8049ccb 0525c84f 9431cc40 -> 091e639cb715 */
    st = crypto1_recover_mfkey64(0x14579F69u, 0xCE844261u, 0xF8049CCBu,
                                 0x0525C84Fu, 0x9431CC40u, g_ws,
                                 CRYPTO1_RECOVER64_WORKSPACE_WORDS, &key, &ncand,
                                 NULL, NULL);
    CHECK(st == CRYPTO1_RECOVER_OK, "mfkey64 pipeline OK on published vector");
    CHECK(key == 0x091E639CB715ull, "mfkey64 recovers verified key 091e639cb715");
    CHECK(ncand == 1, "mfkey64 reports a single verified key");

    /* Second published vector: ./mfkey64 52b0f519 5417d1f8 4d545ea7 e15ac8c2 5056e41b
     * (uid nt nr ar at). Key not published in the md, so assert the pipeline
     * yields a single verified key that reproduces the transcript keystream. */
    key = 0; ncand = 0;
    st = crypto1_recover_mfkey64(0x52B0F519u, 0x5417D1F8u, 0x4D545EA7u,
                                 0xE15AC8C2u, 0x5056E41Bu, g_ws,
                                 CRYPTO1_RECOVER64_WORKSPACE_WORDS, &key, &ncand,
                                 NULL, NULL);
    CHECK(st == CRYPTO1_RECOVER_OK && ncand == 1,
          "mfkey64 pipeline OK on second published transcript");
    {   /* independent re-encryption check of the returned key */
        Crypto1 c; crypto1_init(&c, key);
        crypto1_word(&c, 0x52B0F519u ^ 0x5417D1F8u, 0);
        crypto1_word(&c, 0x4D545EA7u, 1);
        uint32_t vks2 = crypto1_word(&c, 0, 0);
        uint32_t vks3 = crypto1_word(&c, 0, 0);
        uint32_t p64 = crypto1_prng_successor(0x5417D1F8u, 64);
        CHECK(vks2 == (0xE15AC8C2u ^ p64) &&
              vks3 == (0x5056E41Bu ^ crypto1_prng_successor(p64, 32)),
              "returned key reproduces the observed {ar}/{at} keystream");
    }

    /* error paths */
    CHECK(crypto1_recover_mfkey64(1, 2, 3, 4, 5, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS, NULL, &ncand, NULL, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "mfkey64 NULL out_key rejected");
    CHECK(crypto1_recover_mfkey64(1, 2, 3, 4, 5, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS, &key, NULL, NULL, NULL) ==
          CRYPTO1_RECOVER_INVALID_INPUT, "mfkey64 NULL count rejected");
    CHECK(crypto1_recover_mfkey64(0x14579F69u, 0xCE844261u, 0xF8049CCBu,
              0x0525C84Fu, 0x9431CC40u, g_ws,
              CRYPTO1_RECOVER64_WORKSPACE_WORDS - 1, &key, &ncand, NULL, NULL) ==
          CRYPTO1_RECOVER_INSUFFICIENT_WORKSPACE, "mfkey64 small workspace flagged");
}

/* Phase 2 item 3: cooperative yield/abort seam (watchdog-safe execution). */
static bool cb_count(void *ctx, uint32_t idx) {
    (void)idx; (*(int *)ctx)++; return true;      /* never abort */
}
static bool cb_abort_now(void *ctx, uint32_t idx) {
    (void)idx; (*(int *)ctx)++; return false;     /* abort on first check */
}
static void test_progress_abort(void) {
    printf("[P2.3] cooperative yield/abort callback\n");
    Crypto1 cand[64];
    size_t n = 0;
    int calls = 0;

    /* callback is invoked periodically and recovery still succeeds */
    crypto1_recover_status_t st =
        crypto1_recover_lfsr64(0x73F18EC2u, 0x41C20836u, g_ws,
                               CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n,
                               cb_count, &calls);
    CHECK(st == CRYPTO1_RECOVER_OK, "recovery OK with progress callback");
    CHECK(calls >= 32 && calls <= 96, "callback invoked ~64 times over the sweep");

    /* aborting callback stops recovery deterministically */
    calls = 0; n = 123;
    st = crypto1_recover_lfsr64(0x73F18EC2u, 0x41C20836u, g_ws,
                                CRYPTO1_RECOVER64_WORKSPACE_WORDS, cand, 64, &n,
                                cb_abort_now, &calls);
    CHECK(st == CRYPTO1_RECOVER_ABORTED, "abort callback yields _ABORTED");
    CHECK(calls == 1, "abort honored on the first check");

    /* mfkey64 propagates abort */
    uint64_t key = 0; size_t ncand = 0; calls = 0;
    st = crypto1_recover_mfkey64(0x14579F69u, 0xCE844261u, 0xF8049CCBu,
                                 0x0525C84Fu, 0x9431CC40u, g_ws,
                                 CRYPTO1_RECOVER64_WORKSPACE_WORDS, &key, &ncand,
                                 cb_abort_now, &calls);
    CHECK(st == CRYPTO1_RECOVER_ABORTED, "mfkey64 propagates abort");
}

/* Separate host-runtime measurement (NOT a Cortex-M33 measurement). */
static double ts_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1.0e6;
}
static int cmp_dbl(const void *x, const void *y) {
    double d = *(const double *)x - *(const double *)y;
    return (d > 0) - (d < 0);
}
static void measure_recovery_runtime(void) {
    const uint32_t ks2 = 0x73F18EC2u, ks3 = 0x41C20836u;
    Crypto1 cand[64];
    size_t n = 0;
    const int iters = 15;
    double t[15];
    for (int k = 0; k < iters; k++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        crypto1_recover_lfsr64(ks2, ks3, g_ws, CRYPTO1_RECOVER64_WORKSPACE_WORDS,
                               cand, 64, &n, NULL, NULL);
        clock_gettime(CLOCK_MONOTONIC, &b);
        t[k] = ts_ms(a, b);
    }
    qsort(t, iters, sizeof(double), cmp_dbl);
    printf("[timing] recover64 HOST runtime over %d runs: "
           "min=%.2f ms  median=%.2f ms  max=%.2f ms (NOT Cortex-M33 timing)\n",
           iters, t[0], t[iters / 2], t[iters - 1]);
}

int main(void) {
    printf("=== Crypto1 recovery foundation host tests ===\n");
    test_rollback_roundtrip();
    test_prng_successor();
    test_weak_prng();
    test_nonce_distance();
    test_decrypt_and_key_consistency();
    test_static_classification();
    test_capacity_and_errors();
    test_determinism();
    test_state_recovery();
    test_mfkey64_pipeline();
    test_progress_abort();
    measure_recovery_runtime();
    printf("=============================================\n");
    printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
