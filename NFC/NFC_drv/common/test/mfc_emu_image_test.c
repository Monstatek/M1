/* Host tests for m1_mfc_emu_image_build() -- compiled against the REAL,
 * unmodified m1_mfc_emu_image.c, using a structurally-faithful nfc_ctx.h
 * stub (stub/nfc_ctx.h + stub/nfc_ctx_stub.c) in place of the real header,
 * which transitively pulls the full RFAL/HAL vendor tree (impractical to
 * host-compile for testing pure snapshot logic that never touches RF
 * hardware -- see stub/nfc_ctx.h's own header comment).
 *
 * C quoted #include resolves relative to the including file's OWN directory
 * first, so compiling m1_mfc_emu_image.c in place would still pick up the
 * real NFC/NFC_drv/common/nfc_ctx.h regardless of -I order. Copy the real
 * .c/.h next to the stub immediately before compiling (never commit that
 * copy -- it must always be freshly taken from the current source, never a
 * stale duplicate):
 *
 *   cp NFC/NFC_drv/common/m1_mfc_emu_image.c NFC/NFC_drv/common/m1_mfc_emu_image.h \
 *      NFC/NFC_drv/common/test/stub/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common/test/stub \
 *      NFC/NFC_drv/common/test/stub/m1_mfc_emu_image.c NFC/NFC_drv/common/test/stub/nfc_ctx_stub.c \
 *      NFC/NFC_drv/common/test/mfc_emu_image_test.c -o /tmp/emuimg && /tmp/emuimg
 *   rm NFC/NFC_drv/common/test/stub/m1_mfc_emu_image.c NFC/NFC_drv/common/test/stub/m1_mfc_emu_image.h
 */
#include "m1_mfc_emu_image.h"
#include "nfc_ctx.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static const uint8_t UID[4] = { 0x11, 0x22, 0x33, 0x44 };
static uint8_t bcc(const uint8_t u[4]) { return (uint8_t)(u[0]^u[1]^u[2]^u[3]); }

/* Build a fully-complete, eligible 1K card: real UID/ATQA/SAK, all 64 blocks
 * (block 0 = UID+BCC+SAK+ATQA+mfg, matching a real card), all 32 keys. */
static void make_complete_card(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    c->head.family = M1NFC_FAM_CLASSIC;
    memcpy(c->head.uid, UID, 4);
    c->head.uid_len = 4;
    c->head.a.has_atqa = true; c->head.a.atqa[0] = 0x04; c->head.a.atqa[1] = 0x00;
    c->head.a.has_sak  = true; c->head.a.sak = 0x08;

    nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    mfc->valid = true;
    mfc->type  = M1NFC_MFCTYPE_1K;
    mfc->sectors_total = 16;

    for (int b = 0; b < 64; b++) {
        uint8_t data[16];
        memset(data, (uint8_t)(0xA0 + b), 16);
        if (b == 0) {
            data[0]=UID[0]; data[1]=UID[1]; data[2]=UID[2]; data[3]=UID[3];
            data[4] = bcc(UID);
        }
        nfc_ctx_mfc_store_block((uint16_t)b, data);
    }
    for (int s = 0; s < 16; s++) {
        nfc_mfc_sector_t *sc = &mfc->sec[s];
        sc->key_a_found = true; memset(sc->key_a, (uint8_t)(0x10 + s), 6);
        sc->key_b_found = true; memset(sc->key_b, (uint8_t)(0x20 + s), 6);
    }
}

/* ---- 8. incomplete block dump ------------------------------------------ */
static void test_incomplete_blocks(void)
{
    make_complete_card();
    /* Un-mark block 40 as read by rebuilding without it. */
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    c->head.family = M1NFC_FAM_CLASSIC;
    memcpy(c->head.uid, UID, 4); c->head.uid_len = 4;
    c->head.a.has_atqa = true; c->head.a.atqa[0]=0x04; c->head.a.atqa[1]=0x00;
    c->head.a.has_sak  = true; c->head.a.sak = 0x08;
    nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    mfc->valid = true; mfc->type = M1NFC_MFCTYPE_1K; mfc->sectors_total = 16;
    for (int b = 0; b < 64; b++) {
        if (b == 40) continue;   /* leave this one unread */
        uint8_t data[16]; memset(data, 0xAA, 16);
        if (b == 0) { data[0]=UID[0]; data[1]=UID[1]; data[2]=UID[2]; data[3]=UID[3]; data[4]=bcc(UID); }
        nfc_ctx_mfc_store_block((uint16_t)b, data);
    }
    for (int s = 0; s < 16; s++) {
        mfc->sec[s].key_a_found = true; memset(mfc->sec[s].key_a, 0x11, 6);
        mfc->sec[s].key_b_found = true; memset(mfc->sec[s].key_b, 0x22, 6);
    }

    m1_mfc_emu_image_t img;
    m1_mfc_emu_status_t st = m1_mfc_emu_image_build(&img);
    CHECK(st == M1_MFC_EMU_INCOMPLETE_BLOCKS, "one unread block -> INCOMPLETE_BLOCKS");
    uint8_t zero[sizeof(img)]; memset(zero, 0, sizeof(zero));
    CHECK(memcmp(&img, zero, sizeof(img)) == 0, "failed build leaves *out zeroed, no partial fill");
}

/* ---- 7. missing key ------------------------------------------------------ */
static void test_missing_key(void)
{
    make_complete_card();
    nfc_ctx_get_mfc_info()->sec[5].key_b_found = false;   /* sector 5 Key B unknown */

    m1_mfc_emu_image_t img;
    m1_mfc_emu_status_t st = m1_mfc_emu_image_build(&img);
    CHECK(st == M1_MFC_EMU_INCOMPLETE_KEYS, "one missing key -> INCOMPLETE_KEYS, not fabricated");
}

/* ---- 9. reject 4K and 7-byte UID ---------------------------------------- */
static void test_reject_4k_and_7byte_uid(void)
{
    make_complete_card();
    nfc_ctx_get_mfc_info()->type = M1NFC_MFCTYPE_4K;
    m1_mfc_emu_image_t img;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_UNSUPPORTED_4K, "MFC 4K explicitly rejected");

    make_complete_card();
    nfc_ctx_get()->head.uid_len = 7;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_UNSUPPORTED_UID_LEN, "7-byte UID explicitly rejected");
}

/* ---- 10. inconsistent UID / block 0 / BCC ------------------------------- */
static void test_identity_inconsistent(void)
{
    make_complete_card();
    /* Corrupt block 0's UID copy so it disagrees with the header UID. */
    uint8_t bad[16]; memcpy(bad, nfc_ctx_mfc_block(0), 16);
    bad[0] ^= 0xFF;
    nfc_ctx_mfc_store_block(0, bad);
    m1_mfc_emu_image_t img;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_IDENTITY_INCONSISTENT,
          "block0 UID mismatch -> IDENTITY_INCONSISTENT");

    make_complete_card();
    memcpy(bad, nfc_ctx_mfc_block(0), 16);
    bad[4] ^= 0xFF;   /* corrupt BCC only */
    nfc_ctx_mfc_store_block(0, bad);
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_IDENTITY_INCONSISTENT,
          "block0 BCC mismatch -> IDENTITY_INCONSISTENT");
}

/* ---- 4/5. exact snapshot of UID/ATQA/SAK/64 blocks/validity/keys -------- */
static void test_exact_snapshot(void)
{
    make_complete_card();
    m1_mfc_emu_image_t img;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_OK, "complete card -> OK");

    CHECK(memcmp(img.uid, UID, 4) == 0, "UID copied exactly");
    CHECK(img.atqa[0] == 0x04 && img.atqa[1] == 0x00, "ATQA copied exactly");
    CHECK(img.sak == 0x08, "SAK copied exactly");

    int blocks_ok = 1, valid_ok = 1;
    for (int b = 0; b < 64; b++) {
        uint8_t expect[16]; memset(expect, (uint8_t)(0xA0 + b), 16);
        if (b == 0) { expect[0]=UID[0]; expect[1]=UID[1]; expect[2]=UID[2]; expect[3]=UID[3]; expect[4]=bcc(UID); }
        if (memcmp(img.block[b], expect, 16) != 0) blocks_ok = 0;
        if (!img.block_valid[b]) valid_ok = 0;
    }
    CHECK(blocks_ok, "all 64 blocks copied exactly");
    CHECK(valid_ok, "all 64 block_valid flags true");

    int keys_ok = 1, key_valid_ok = 1;
    for (int s = 0; s < 16; s++) {
        uint8_t ea[6]; memset(ea, (uint8_t)(0x10 + s), 6);
        uint8_t eb[6]; memset(eb, (uint8_t)(0x20 + s), 6);
        if (memcmp(img.key_a[s], ea, 6) != 0) keys_ok = 0;
        if (memcmp(img.key_b[s], eb, 6) != 0) keys_ok = 0;
        if (!img.key_a_valid[s] || !img.key_b_valid[s]) key_valid_ok = 0;
    }
    CHECK(keys_ok, "all 32 keys (16 sectors x A/B) copied exactly");
    CHECK(key_valid_ok, "all 32 key-validity flags true");
}

/* ---- 11. snapshot independence from later nfc_ctx changes --------------- */
static void test_snapshot_independence(void)
{
    make_complete_card();
    m1_mfc_emu_image_t img;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_OK, "build OK");
    uint8_t saved_uid[4]; memcpy(saved_uid, img.uid, 4);
    uint8_t saved_block0[16]; memcpy(saved_block0, img.block[0], 16);
    uint8_t saved_key_a0[6]; memcpy(saved_key_a0, img.key_a[0], 6);

    /* Mutate nfc_ctx as if a different card were loaded/read afterward. */
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    c->head.family = M1NFC_FAM_CLASSIC;
    c->head.uid[0] = 0xEE; c->head.uid_len = 4;
    nfc_ctx_get_mfc_info()->sec[0].key_a[0] = 0xEE;
    uint8_t junk[16]; memset(junk, 0xEE, 16);
    nfc_ctx_mfc_store_block(0, junk);

    CHECK(memcmp(img.uid, saved_uid, 4) == 0, "snapshot UID unaffected by later nfc_ctx mutation");
    CHECK(memcmp(img.block[0], saved_block0, 16) == 0, "snapshot block0 unaffected by later nfc_ctx mutation");
    CHECK(memcmp(img.key_a[0], saved_key_a0, 6) == 0, "snapshot key_a[0] unaffected by later nfc_ctx mutation");
}

/* ---- 12. cleanup then build a different image, no stale data ------------ */
static void test_no_stale_data_across_cards(void)
{
    make_complete_card();
    m1_mfc_emu_image_t img1;
    CHECK(m1_mfc_emu_image_build(&img1) == M1_MFC_EMU_OK, "card 1 builds OK");

    /* A second, different complete card. */
    test_nfc_ctx_reset();
    static const uint8_t UID2[4] = { 0x99, 0x88, 0x77, 0x66 };
    nfc_run_ctx_t *c = nfc_ctx_get();
    c->head.family = M1NFC_FAM_CLASSIC;
    memcpy(c->head.uid, UID2, 4); c->head.uid_len = 4;
    c->head.a.has_atqa = true; c->head.a.atqa[0]=0x04; c->head.a.atqa[1]=0x00;
    c->head.a.has_sak  = true; c->head.a.sak = 0x08;
    nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    mfc->valid = true; mfc->type = M1NFC_MFCTYPE_1K; mfc->sectors_total = 16;
    for (int b = 0; b < 64; b++) {
        uint8_t data[16]; memset(data, (uint8_t)(0x50 + b), 16);
        if (b == 0) { data[0]=UID2[0]; data[1]=UID2[1]; data[2]=UID2[2]; data[3]=UID2[3]; data[4]=bcc(UID2); }
        nfc_ctx_mfc_store_block((uint16_t)b, data);
    }
    for (int s = 0; s < 16; s++) {
        mfc->sec[s].key_a_found = true; memset(mfc->sec[s].key_a, (uint8_t)(0x30+s), 6);
        mfc->sec[s].key_b_found = true; memset(mfc->sec[s].key_b, (uint8_t)(0x40+s), 6);
    }

    m1_mfc_emu_image_t img2;
    CHECK(m1_mfc_emu_image_build(&img2) == M1_MFC_EMU_OK, "card 2 builds OK");
    CHECK(memcmp(img2.uid, UID2, 4) == 0, "card 2 image has card 2's real UID");
    CHECK(memcmp(img1.uid, UID, 4) == 0, "card 1's already-built image is untouched by building card 2");
    CHECK(memcmp(img1.uid, img2.uid, 4) != 0, "card 1 and card 2 images do not collide");
}

/* ---- 13. production build can never reach 01020304 / fabricated data ---- */
static void test_never_fabricates(void)
{
    make_complete_card();
    m1_mfc_emu_image_t img;
    CHECK(m1_mfc_emu_image_build(&img) == M1_MFC_EMU_OK, "build OK");
    uint8_t fixed_uid[4] = { 0x01, 0x02, 0x03, 0x04 };
    CHECK(memcmp(img.uid, fixed_uid, 4) != 0, "built image UID is never the fixed test UID");

    /* An eligible card whose real content HAPPENS to look like the test
     * pattern is fine (that's real data, not fabrication) -- the guarantee
     * under test is that the FUNCTION doesn't fabricate/default when data is
     * missing, proven by every incomplete/no-source case above returning
     * zeroed output rather than the test image. */
    test_nfc_ctx_reset();
    m1_mfc_emu_status_t st = m1_mfc_emu_image_build(&img);
    CHECK(st == M1_MFC_EMU_NO_SOURCE, "empty ctx -> NO_SOURCE, not the fixed fallback identity");
    uint8_t zero[4] = {0,0,0,0};
    CHECK(memcmp(img.uid, zero, 4) == 0, "no source -> UID is zeroed, never 01020304");
}

int main(void)
{
    test_exact_snapshot();
    test_incomplete_blocks();
    test_missing_key();
    test_reject_4k_and_7byte_uid();
    test_identity_inconsistent();
    test_snapshot_independence();
    test_no_stale_data_across_cards();
    test_never_fabricates();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
