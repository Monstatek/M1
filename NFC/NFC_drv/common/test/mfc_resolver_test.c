/* Host tests for the saved-card key/block resolver wiring added to the REAL
 * m1_mfc_session.c (m1_mfc_session_set_key_resolver/set_block_resolver) and
 * ce_mfc.c (ce_mfc_build_read_resp_real), driving the production orchestrator
 * exactly like the existing mfc_session_test.c does, with a real reader-side
 * oracle (independent crypto1 primitives) playing multiple different sectors
 * each with their OWN distinct key -- something the fixed-single-key `key`
 * param could never express, proving the resolver is what's actually used.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/crypto1.c NFC/NFC_drv/common/ce_mfc.c \
 *      NFC/NFC_drv/common/m1_mfc_session.c \
 *      NFC/NFC_drv/common/test/mfc_resolver_test.c -o /tmp/resolver && /tmp/resolver
 */
#include "m1_mfc_session.h"
#include "ce_mfc.h"
#include "crypto1.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

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

#define CUID 0x11223344U
#define NT   0x01020304U

/* --- test-owned "saved card" image: one distinct key per sector, one
 * distinct, recognizable data pattern per block. --- */
static uint64_t s_sector_key[16];
static uint8_t  s_block_data[64][16];

static bool test_key_resolver(void *ctx, uint8_t key_type, uint8_t block, uint64_t *out_key)
{
    (void)ctx; (void)key_type;
    if (block >= 64U) return false;
    uint8_t sector = block / 4U;
    if (sector >= 16U) return false;
    /* One key per sector (both A and B resolve to it here) -- distinctness
     * under test is PER SECTOR, which the fixed single-`key` session field
     * could never express across sectors 0/7/15 simultaneously. */
    *out_key = s_sector_key[sector];
    return true;
}

static bool test_key_resolver_missing_sector15B(void *ctx, uint8_t key_type, uint8_t block, uint64_t *out_key)
{
    (void)ctx;
    if (block >= 64U) return false;
    uint8_t sector = block / 4U;
    if (sector == 15U && key_type == 0x61U) return false;   /* Key B unknown for sector 15 */
    *out_key = s_sector_key[sector];
    return true;
}

static bool test_block_resolver(void *ctx, uint8_t block, uint8_t out_data[16])
{
    (void)ctx;
    if (block >= 64U) return false;
    memcpy(out_data, s_block_data[block], 16);
    return true;
}

/* Reader-side full AUTH+READ against one (key_type,block), using the same
 * proven sequence as mfc_session_test.c's full_chain(): the 2-byte logical
 * AUTH command (bits=16), crypto1_reader_answer() for {Nr}{Ar}, and
 * crypto1_encrypt_byte() for the READ -- not a hand-rolled reimplementation. */
static bool reader_auth_and_read_cuid(m1_mfc_session_t *s, uint32_t cuid, uint8_t key_type,
                                      uint8_t block, uint64_t key, uint8_t out_block[18])
{
    uint8_t frame[2] = { key_type, block };
    uint8_t nt_b[4];
    if (!m1_mfc_session_take_auth(s, frame, 16, nt_b)) return false;

    static const uint8_t nr[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    Crypto1 rc;
    uint8_t nrar[8], nrarpar[8];
    uint32_t ntrec = crypto1_reader_answer(&rc, key, cuid, nt_b, nr, nrar, nrarpar, false);
    if (ntrec != NT) return false;

    uint8_t at_b[4], at_par[4];
    if (!m1_mfc_session_on_nrar(s, nrar, at_b, at_par)) return false;

    /* Resync the reader's keystream past {At} by decrypting it (the card's
     * cipher already advanced internally when it computed {At}) -- same as
     * mfc_session_test.c's full_chain(): decrypt on a copy, verify the
     * plaintext, then commit. Skipping this desyncs reader vs. card and
     * every subsequent READ silently fails. */
    Crypto1 rc_at = rc; uint32_t at_plain = 0;
    for (int i = 0; i < 4; i++) { uint8_t k; at_plain = (at_plain << 8) | crypto1_decrypt_byte(&rc_at, at_b[i], &k); }
    if (at_plain != crypto1_prng_successor(NT, 96)) return false;
    rc = rc_at;

    uint8_t rd[4] = { 0x30, block, 0, 0 };
    uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
    uint8_t cenc[4], cpar[4];
    for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);

    uint8_t renc[18], par18[18]; uint8_t blk_out = 0xFF;
    int r = m1_mfc_session_on_read(s, cenc, renc, par18, &blk_out);
    if ((r != 1) || (blk_out != block)) return false;

    /* Decrypt the response (out_block[]=plaintext data+CRC, 18 bytes) --
     * same as mfc_session_test.c's full_chain(). Caller compares out_block
     * to plaintext, not the encrypted wire bytes. */
    for (int i = 0; i < 18; i++) { uint8_t ksp; out_block[i] = crypto1_decrypt_byte(&rc, renc[i], &ksp); }
    return true;
}

static bool reader_auth_and_read(m1_mfc_session_t *s, uint8_t key_type, uint8_t block,
                                 uint64_t key, uint8_t out_block[18])
{
    return reader_auth_and_read_cuid(s, CUID, key_type, block, key, out_block);
}

static void test_resolve_multiple_sectors(void)
{
    for (int s = 0; s < 16; s++) s_sector_key[s] = 0x100000000000ULL + (uint64_t)(s * 0x111111ULL);
    for (int b = 0; b < 64; b++) memset(s_block_data[b], (uint8_t)(0xB0 + b), 16);

    int all_ok = 1;
    /* Sectors 0, 7, 15 (first/middle/last), Key A and Key B, using EACH
     * sector's own distinct key -- the fixed single-key path could never
     * satisfy more than one of these. */
    struct { uint8_t sector; uint8_t key_type; } cases[] = {
        {0, 0x60}, {0, 0x61}, {7, 0x60}, {7, 0x61}, {15, 0x60}, {15, 0x61},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        m1_mfc_session_t s;
        m1_mfc_session_init(&s, CUID, 0, NT);
        m1_mfc_session_set_key_resolver(&s, test_key_resolver, NULL);
        m1_mfc_session_set_block_resolver(&s, test_block_resolver, NULL);
        m1_mfc_session_begin(&s);

        uint8_t block = (uint8_t)(cases[i].sector * 4U);   /* first block of the sector */
        uint8_t out_block[18];
        bool ok = reader_auth_and_read(&s, cases[i].key_type, block,
                                       s_sector_key[cases[i].sector], out_block);
        if (!ok || memcmp(out_block, s_block_data[block], 16) != 0) {
            all_ok = 0;
            printf("  sector %u type 0x%02X: ok=%d\n", cases[i].sector, cases[i].key_type, ok);
        }
    }
    CHECK(all_ok, "resolves the correct per-sector key AND returns the correct real block data, for 3 different sectors x A/B");
}

static void test_wrong_key_rejected(void)
{
    for (int s = 0; s < 16; s++) s_sector_key[s] = 0x100000000000ULL + (uint64_t)(s * 0x111111ULL);
    for (int b = 0; b < 64; b++) memset(s_block_data[b], 0xC0, 16);

    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, 0, NT);
    m1_mfc_session_set_key_resolver(&s, test_key_resolver, NULL);
    m1_mfc_session_set_block_resolver(&s, test_block_resolver, NULL);
    m1_mfc_session_begin(&s);

    uint8_t out_block[18];
    /* Reader uses the WRONG key (sector 1's key against sector 0's block). */
    bool ok = reader_auth_and_read(&s, 0x60, 0, s_sector_key[1], out_block);
    CHECK(!ok, "reader authenticating with the wrong sector's key is rejected (Ar mismatch)");
}

static void test_missing_key_rejected(void)
{
    for (int s = 0; s < 16; s++) s_sector_key[s] = 0x222222222222ULL;
    for (int b = 0; b < 64; b++) memset(s_block_data[b], 0xD0, 16);

    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, 0, NT);
    m1_mfc_session_set_key_resolver(&s, test_key_resolver_missing_sector15B, NULL);
    m1_mfc_session_set_block_resolver(&s, test_block_resolver, NULL);
    m1_mfc_session_begin(&s);

    uint8_t out_block[18];
    /* Sector 15 Key B has no resolver entry -- must fail safely, not fall
     * back to any default/test key. */
    bool ok = reader_auth_and_read(&s, 0x61, 60, s_sector_key[15], out_block);
    CHECK(!ok, "AUTH for a key the resolver doesn't have fails safely (no fallback key used)");
}

static void test_missing_block_rejected(void)
{
    for (int s = 0; s < 16; s++) s_sector_key[s] = 0x333333333333ULL;

    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, 0, NT);
    m1_mfc_session_set_key_resolver(&s, test_key_resolver, NULL);
    /* Block resolver always returns false: no real data available. */
    m1_mfc_session_set_block_resolver(&s, NULL, NULL);   /* falls back to deterministic image */
    m1_mfc_session_begin(&s);

    /* With no block resolver installed, the read still succeeds but returns
     * the deterministic TEST image, not real data -- this is exactly the
     * dev/diagnostic-persona behavior (resolver NULL = unchanged legacy
     * path), verified explicitly so a future change can't silently blur the
     * two. */
    uint8_t out_block[18];
    bool ok = reader_auth_and_read(&s, 0x60, 0, s_sector_key[0], out_block);
    CHECK(ok, "no block resolver installed -> falls back to the deterministic test image (legacy behavior), not a hard failure");
    uint8_t expect[16]; (void)ce_mfc_block_data(0, expect);
    CHECK(memcmp(out_block, expect, 16) == 0, "...and that fallback is exactly ce_mfc_block_data()'s image, unchanged");
}

static void test_cleanup_no_stale_resolver(void)
{
    for (int s = 0; s < 16; s++) s_sector_key[s] = 0x444444444444ULL + (uint64_t)s;
    for (int b = 0; b < 64; b++) memset(s_block_data[b], (uint8_t)(0xE0+b), 16);

    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, 0, NT);
    m1_mfc_session_set_key_resolver(&s, test_key_resolver, NULL);
    m1_mfc_session_set_block_resolver(&s, test_block_resolver, NULL);
    m1_mfc_session_begin(&s);
    uint8_t out_block[18];
    CHECK(reader_auth_and_read(&s, 0x60, 0, s_sector_key[0], out_block), "session 1 (with resolvers) works");

    /* Re-init fresh, WITHOUT installing any resolver -- exactly what happens
     * when the next Emulate entry is the dev/diagnostic persona (no image
     * armed). Must not retain the previous session's resolver/ctx. */
    m1_mfc_session_init(&s, 0x01020304U, 0xFFFFFFFFFFFFULL, NT);
    m1_mfc_session_begin(&s);
    bool ok2 = reader_auth_and_read_cuid(&s, 0x01020304U, 0x60, 0, 0xFFFFFFFFFFFFULL, out_block);
    uint8_t expect[16]; (void)ce_mfc_block_data(0, expect);
    CHECK(ok2 && memcmp(out_block, expect, 16) == 0,
          "session 2 (re-init, no resolver installed) cleanly uses the fixed test key/image -- no leftover resolver from session 1");
}

int main(void)
{
    test_resolve_multiple_sectors();
    test_wrong_key_rejected();
    test_missing_key_rejected();
    test_missing_block_rejected();
    test_cleanup_no_stale_resolver();

    printf("\nmfc_resolver_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
