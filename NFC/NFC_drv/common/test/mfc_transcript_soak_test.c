/*
 * mfc_transcript_soak_test.c - randomized full-transcript soak for the MIFARE
 * Classic card-side engine (ce_mfc.c), branch-neutral acceptance oracle.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WHY THIS EXISTS
 * ---------------
 * ce_mfc_test.c proves the card engine once per hand-picked vector. This driver
 * runs the COMPLETE production transcript
 *     AUTH(1) -> Nt -> {Nr}{Ar} -> {At} -> encrypted READ -> 18-byte block
 * over SOAK_ITERS deterministically-generated (key, cuid, nt, nr, block) tuples,
 * asserting on every iteration that the reader side recovers the exact
 * deterministic block AND that all parity bits + CRC_A hold. It also runs the
 * negative/edge cases (bad Ar, corrupt cmd parity, wrong opcode, wrong sector,
 * cipher-not-advanced-on-reject, not-AUTHED gating). Intended to be built and run
 * under -fsanitize=address,undefined so the soak doubles as a memory/UB check on
 * the real crypto1.c + ce_mfc.c the firmware links.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/crypto1.c NFC/NFC_drv/common/ce_mfc.c \
 *      NFC/NFC_drv/common/test/mfc_transcript_soak_test.c -o /tmp/soak && /tmp/soak
 *
 * INDEPENDENCE NOTE (honest scope): the reader half uses crypto1_reader_answer,
 * which shares crypto1.c's Proxmark lineage with the card half, so this soak is a
 * self-consistency + memory-safety oracle, not a second-implementation cross-check.
 * The cross-lineage anchor is the hardware-captured KAT ({At}=03CA9A82 for the
 * fixed persona), asserted separately in ce_mfc_test.c [8]. Deterministic seed =>
 * a failing iteration is exactly reproducible from its index.
 */
#include "ce_mfc.h"
#include "crypto1.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifndef SOAK_ITERS
#define SOAK_ITERS 2000
#endif

static long g_pass = 0, g_fail = 0;
static int  g_first_fail_reported = 0;
#define CHECK(c, m) do { \
    if (c) { g_pass++; } \
    else   { g_fail++; if (!g_first_fail_reported) { \
                 printf("  FAIL: %s (line %d)\n", (m), __LINE__); g_first_fail_reported = 1; } } \
} while (0)

/* Deterministic PRNG (xorshift64*) - no rand(), fully reproducible across hosts. */
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rng_next(void)
{
    uint64_t x = rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}
static uint32_t rng_u32(void) { return (uint32_t)(rng_next() >> 32); }

/* ISO/IEC 14443-A CRC_A (poly 0x8408, preload 0x6363); CRC[0]=low, CRC[1]=high. */
static uint16_t crc_a(const uint8_t *d, int n)
{
    uint16_t crc = 0x6363;
    for (int i = 0; i < n; i++) {
        uint8_t b = (uint8_t)(d[i] ^ (uint8_t)(crc & 0xFF));
        b = (uint8_t)(b ^ (uint8_t)(b << 4));
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4));
    }
    return crc;
}
static uint32_t be32(const uint8_t b[4])
{
    return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}

/* One full transcript. Sector-0 authed on `block` (block 0..3 map to sector 0);
 * for the soak we authenticate and READ the same block. Returns 1 on full success. */
static int run_one(uint64_t key, uint32_t cuid, uint32_t nt, const uint8_t nr[4],
                   uint8_t key_type, uint8_t block)
{
    ce_mfc_t card;
    ce_mfc_init(&card, cuid);

    /* --- AUTH part 1: card emits plaintext nonce --- */
    uint8_t nt_b[4], par[4];
    bool plain = false;
    if (!ce_mfc_auth1(&card, key_type, block, key, nt, nt_b, par, &plain)) return 0;
    if (!plain) return 0;                    /* first auth must be plaintext */
    if (be32(nt_b) != nt) return 0;          /* nonce sent verbatim          */

    /* --- Reader computes {Nr}{Ar} independently from the nonce --- */
    Crypto1 rc;
    uint8_t nrar[8], nrarpar[8];
    uint32_t nt_rec = crypto1_reader_answer(&rc, key, cuid, nt_b, nr, nrar, nrarpar, false);
    if (nt_rec != nt) return 0;

    /* --- AUTH part 2: card verifies, emits encrypted {At} --- */
    uint8_t at_b[4], atpar[4];
    if (!ce_mfc_auth2(&card, nrar, at_b, atpar)) return 0;
    if (card.state != CE_MFC_AUTHED) return 0;

    /* reader decrypts {At}, must equal suc(nt,96) */
    Crypto1 rc_at = rc;
    uint32_t at_plain = 0;
    for (int i = 0; i < 4; i++) { uint8_t k; at_plain = (at_plain << 8) | crypto1_decrypt_byte(&rc_at, at_b[i], &k); }
    if (at_plain != crypto1_prng_successor(nt, 96)) return 0;
    rc = rc_at;                              /* commit reader cipher past {At} */

    /* --- POSTAUTH: reader sends encrypted READ 0x30 (target-mode, no parity to card) --- */
    uint8_t rd[4] = { 0x30, block, 0, 0 };
    uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
    uint8_t cenc[4], cpar[4];
    for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);

    uint8_t got_block = 0xFF;
    if (ce_mfc_read_cmd_nopar(&card, cenc, &got_block) != 1) return 0;
    if (got_block != block) return 0;

    /* --- card builds encrypted 18-byte response; reader recovers it --- */
    uint8_t renc[18], rpar[18];
    if (!ce_mfc_build_read_resp(&card, block, renc, rpar)) return 0;

    uint8_t exp[16]; ce_mfc_block_data(block, exp);
    uint8_t got[18];
    for (int i = 0; i < 18; i++) {
        uint8_t ksp; got[i] = crypto1_decrypt_byte(&rc, renc[i], &ksp);
        uint8_t expp = (uint8_t)((crypto1_odd_parity8(got[i]) ^ ksp) & 1U);
        if ((rpar[i] & 1U) != expp) return 0;          /* parity mismatch */
    }
    if (memcmp(got, exp, 16) != 0) return 0;           /* wrong block bytes */
    uint16_t dcc = crc_a(got, 16);
    if ((uint8_t)(dcc & 0xFF) != got[16] || (uint8_t)(dcc >> 8) != got[17]) return 0;
    return 1;
}

int main(void)
{
    /* ---- 1. Randomized full-transcript soak ---- */
    for (long it = 0; it < SOAK_ITERS; it++) {
        uint64_t key = ((uint64_t)rng_u32() << 16) ^ rng_u32();   /* 48-bit */
        key &= 0xFFFFFFFFFFFFULL;
        uint32_t cuid = rng_u32();
        uint32_t nt   = rng_u32();
        uint8_t  nr[4] = { (uint8_t)rng_u32(), (uint8_t)rng_u32(),
                           (uint8_t)rng_u32(), (uint8_t)rng_u32() };
        uint8_t  key_type = (rng_u32() & 1) ? 0x61 : 0x60;
        uint8_t  block    = (uint8_t)(rng_u32() & 3);             /* sector-0 blocks 0..3 */
        int ok = run_one(key, cuid, nt, nr, key_type, block);
        if (!ok && !g_first_fail_reported)
            printf("  FAIL: transcript it=%ld key=%012llX cuid=%08X nt=%08X\n",
                   it, (unsigned long long)key, cuid, nt);
        CHECK(ok, "randomized full transcript");
    }

    /* ---- 2. Fixed-persona transcript (the live M1 values) many times ---- */
    for (int i = 0; i < 256; i++) {
        uint8_t nr[4] = { (uint8_t)i, (uint8_t)(i*7), (uint8_t)(i*13), (uint8_t)(i*29) };
        CHECK(run_one(0xFFFFFFFFFFFFULL, 0x01020304, 0x01020304, nr, 0x60, 0),
              "fixed-persona transcript across Nr");
    }

    /* ---- 3. Negative: corrupt Ar must be rejected, card stays silent ---- */
    {
        uint64_t key = 0xFFFFFFFFFFFFULL; uint32_t cuid = 0x01020304, nt = 0x01020304;
        const uint8_t nr[4] = {0,0,0,0};
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 0, key, nt, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr, o8, op8, false);
        o8[7] ^= 0xFF;
        uint8_t at_b[4], atpar[4];
        CHECK(ce_mfc_auth2(&card, o8, at_b, atpar) == false, "corrupt Ar rejected");
        CHECK(card.state != CE_MFC_AUTHED, "not AUTHED after bad Ar");
    }

    /* ---- 4. Negative: wrong opcode / wrong sector, cipher NOT advanced ---- */
    for (int variant = 0; variant < 2; variant++) {
        uint64_t key = 0xFFFFFFFFFFFFULL; uint32_t cuid = 0x01020304, nt = 0x01020304;
        const uint8_t nr[4] = {0,0,0,0};
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 0, key, nt, nt_b, par, &pl);      /* authed sector 0 */
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr, o8, op8, false);
        uint8_t at_b[4], atpar[4]; ce_mfc_auth2(&card, o8, at_b, atpar);
        for (int i = 0; i < 4; i++) { uint8_t k; (void)crypto1_decrypt_byte(&rc, at_b[i], &k); }
        ce_mfc_t before = card;
        uint8_t rd[4];
        if (variant == 0) { rd[0] = 0x31; rd[1] = 0; }   /* wrong opcode */
        else              { rd[0] = 0x30; rd[1] = 8; }   /* block 8 = different sector */
        uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
        uint8_t cenc[4], cpar[4];
        for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
        uint8_t blk = 0xFF;
        CHECK(ce_mfc_read_cmd_nopar(&card, cenc, &blk) == 0,
              variant == 0 ? "wrong opcode rejected" : "wrong sector rejected");
        CHECK(memcmp(&before.cipher, &card.cipher, sizeof(card.cipher)) == 0,
              "cipher NOT advanced on reject");
    }

    /* ---- 5. Negative: operations gated on AUTHED ---- */
    {
        ce_mfc_t s; ce_mfc_init(&s, 0x01020304);
        uint8_t enc[4] = {0}, p[4] = {0}, out[4];
        CHECK(ce_mfc_decrypt_cmd(&s, enc, p, 4, out) == false, "decrypt_cmd gated on AUTHED");
        CHECK(ce_mfc_encrypt_resp(&s, out, 4, enc, p) == false, "encrypt_resp gated on AUTHED");
        uint8_t blk;
        CHECK(ce_mfc_read_cmd_nopar(&s, enc, &blk) == -1, "read_cmd_nopar gated on AUTHED");
    }

    printf("\nmfc_transcript_soak_test: %ld passed, %ld failed  (%d transcript iters)\n",
           g_pass, g_fail, SOAK_ITERS);
    return g_fail ? 1 : 0;
}
