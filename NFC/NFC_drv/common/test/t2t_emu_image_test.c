/* Host tests for m1_t2t_emu_image_build() / page_writable() / write_page()
 * -- compiled against the REAL, unmodified m1_t2t_emu_image.c, using a
 * structurally-faithful nfc_ctx.h stub (stub_t2t/nfc_ctx.h + nfc_ctx_stub.c)
 * in place of the real header, which transitively pulls the full RFAL/HAL
 * vendor tree. See mfc_emu_image_test.c's header comment for the rationale
 * (same pattern, separate stub directory so the MFC tests stay untouched).
 * m1_t2t_emu_image_save() is NOT covered here -- it needs real FatFS file
 * I/O and is left to hardware/on-target verification.
 *
 *   cp NFC/NFC_drv/common/m1_t2t_emu_image.c NFC/NFC_drv/common/m1_t2t_emu_image.h \
 *      NFC/NFC_drv/common/test/stub_t2t/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common/test/stub_t2t \
 *      NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.c \
 *      NFC/NFC_drv/common/test/stub_t2t/nfc_ctx_stub.c \
 *      NFC/NFC_drv/common/test/stub_t2t/m1_file_browser_stub.c \
 *      NFC/NFC_drv/common/test/t2t_emu_image_test.c -o /tmp/t2temu && /tmp/t2temu
 *   rm NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.c NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.h
 *
 * NOTE: m1_t2t_emu_image.c calls m1_fb_open_new_file()/m1_fb_write_to_file()/
 * m1_fb_close_file()/m1_fb_delete_file()/f_rename() (real firmware file-
 * browser/FatFS API) only inside m1_t2t_emu_image_save(), which this test
 * suite never calls -- but the linker still needs every symbol referenced
 * anywhere in a linked translation unit resolved, called or not, so
 * m1_file_browser_stub.c (no-op stand-ins) must be linked in regardless.
 * m1_t2t_emu_image_save() itself is not exercised here; it needs real FatFS
 * file I/O and is left to hardware/on-target verification.
 */
#include "m1_t2t_emu_image.h"
#include "nfc_ctx.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static const uint8_t UID7[7] = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t TEST_SIG[32] = {
    0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
    0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
};

/* Fill pages 0..2 (UID/BCC/internal/lock) + page3 (CC) consistently, then
 * pages [user_first..user_last] with a recognizable pattern, leaving any
 * config/lock/pwd/pack pages for the caller to fill per-variant. */
static void fill_identity_pages(uint16_t user_first, uint16_t user_last)
{
    uint8_t bcc0 = (uint8_t)(0x88U ^ UID7[0] ^ UID7[1] ^ UID7[2]);
    uint8_t bcc1 = (uint8_t)(UID7[3] ^ UID7[4] ^ UID7[5] ^ UID7[6]);

    uint8_t p0[4] = { UID7[0], UID7[1], UID7[2], bcc0 };
    uint8_t p1[4] = { UID7[3], UID7[4], UID7[5], UID7[6] };
    uint8_t p2[4] = { bcc1, 0x48, 0x00, 0x00 };   /* internal=0x48, no static locks set */
    uint8_t p3[4] = { 0xE1, 0x10, 0x00, 0x00 };   /* CC */
    test_t2t_store_page(0, p0);
    test_t2t_store_page(1, p1);
    test_t2t_store_page(2, p2);
    test_t2t_store_page(3, p3);

    for (uint16_t p = user_first; p <= user_last; p++) {
        uint8_t data[4] = { (uint8_t)p, (uint8_t)p, (uint8_t)p, (uint8_t)p };
        test_t2t_store_page(p, data);
    }
}

static void set_head_ul_common(nfc_run_ctx_t *c)
{
    c->head.tech   = M1NFC_TECH_A;
    c->head.family = M1NFC_FAM_ULTRALIGHT;
    memcpy(c->head.uid, UID7, 7);
    c->head.uid_len = 7;
    c->head.a.has_atqa = true; c->head.a.atqa[0] = 0x44; c->head.a.atqa[1] = 0x00;
    c->head.a.has_sak  = true; c->head.a.sak = 0x00;
    strncpy(c->file.path, "0:/nfc/test_card.nfc", sizeof(c->file.path) - 1);
}

/* ---- Plain Ultralight: 16 pages, no config/lock tail --------------------- */
static void make_ul_card(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UL);
    test_t2t_set_capacity(16);
    fill_identity_pages(4, 15);
}

static void test_ul_ok(void)
{
    make_ul_card();
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "plain UL: complete card builds OK");
    CHECK(img.page_count == 16, "plain UL: page_count == 16");
    CHECK(img.user_first == 4 && img.user_last == 15, "plain UL: user bounds 4..15");
    CHECK(!img.has_dynamic_lock, "plain UL: no dynamic lock");
    CHECK(!img.protected_tag, "plain UL: not protected");
    CHECK(memcmp(img.uid, UID7, 7) == 0, "plain UL: UID copied");
    CHECK(strcmp(img.source_path, "0:/nfc/test_card.nfc") == 0, "plain UL: source_path copied");
    CHECK(!img.requires_signature, "plain UL: predates READ_SIGNATURE, never required");
    CHECK(!img.signature_valid, "plain UL: no signature captured (none attempted -- correctly still OK)");
}

/* ---- NTAG213: 45 pages with dynamic lock + config tail -------------------
 * layout: 0-2 identity, 3 CC, 4-39 user, 40 dynlock, 41 CFG0, 42 CFG1, 43 PWD, 44 PACK */
static void make_ntag213_card(uint8_t auth0, uint16_t dyn_lock_bits)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_NTAG213);
    test_t2t_set_capacity(45);
    fill_identity_pages(4, 39);

    uint8_t dlp[4]  = { (uint8_t)(dyn_lock_bits & 0xFF), (uint8_t)(dyn_lock_bits >> 8), 0x00, 0x00 };
    uint8_t cfg0[4] = { 0x00, 0x00, 0x00, auth0 };
    uint8_t cfg1[4] = { 0x00, 0x05, 0x00, 0x00 };
    uint8_t pwd[4]  = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t pack[4] = { 0x00, 0x00, 0x00, 0x00 };
    test_t2t_store_page(40, dlp);
    test_t2t_store_page(41, cfg0);
    test_t2t_store_page(42, cfg1);
    test_t2t_store_page(43, pwd);
    test_t2t_store_page(44, pack);
    nfc_ctx_set_t2t_signature(TEST_SIG);
}

static void test_ntag213_ok(void)
{
    make_ntag213_card(0xFF /* AUTH0=0xFF: never protected */, 0x0000);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213: complete unprotected card builds OK");
    CHECK(img.page_count == 45, "NTAG213: page_count == 45");
    CHECK(img.has_dynamic_lock, "NTAG213: has dynamic lock");
    CHECK(img.dynamic_lock_granularity == 2, "NTAG213: granularity 2");
    CHECK(img.user_first == 4 && img.user_last == 39, "NTAG213: user bounds 4..39");
    CHECK(img.requires_signature, "NTAG213: requires_signature true");
    CHECK(img.signature_valid, "NTAG213: signature captured and copied in");
    CHECK(memcmp(img.signature, TEST_SIG, 32) == 0, "NTAG213: signature bytes match capture exactly");
    CHECK(img.cfg0_page == 41, "NTAG213: cfg0_page == 41 (PWD=43, PACK=44)");
}

static void test_ntag213_missing_signature_refused(void)
{
    /* Same complete, unprotected card as test_ntag213_ok, but the READ_
     * SIGNATURE capture never succeeded (e.g. the physical read was
     * interrupted, or an older save predates this task). Must be refused,
     * never emulated with a fabricated signature. */
    make_ntag213_card(0xFF, 0x0000);
    nfc_ctx_clear_t2t_signature();
    m1_t2t_emu_image_t img;
    memset(&img, 0xAA, sizeof(img));
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_MISSING_SIGNATURE, "NTAG213: missing signature refused as its OWN dedicated status, not generic INCOMPLETE_PAGES");
    CHECK(img.page_count == 0, "NTAG213: image left zeroed when signature missing");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Signature missing / Read tag again") == 0,
          "NTAG213: exact, specific refusal message when signature missing -- never the generic page-read one");
}

static void test_counters_and_tearing_captured_honestly(void)
{
    /* NTAG213 (SingleCounter): only index 2 is ever captured/asked for.
     * Index 0/1 stay invalid -- must NOT be silently zero-filled. */
    make_ntag213_card(0xFF, 0x0000);
    uint8_t cnt2[3] = { 0x01, 0x02, 0x03 };
    nfc_ctx_set_t2t_counter(2, cnt2);
    nfc_ctx_set_t2t_tearing(2, 0xBC);
    /* Tearing index 0 deliberately left uncaptured -- NTAG21x genuinely NAKs
     * CHECK_TEARING for the indices it doesn't use. */

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213 counters/tearing: card builds OK");
    CHECK(!img.counter_valid[0], "NTAG213 counters: index 0 stays invalid (never captured)");
    CHECK(!img.counter_valid[1], "NTAG213 counters: index 1 stays invalid (never captured)");
    CHECK(img.counter_valid[2], "NTAG213 counters: index 2 captured");
    CHECK(memcmp(img.counter[2], cnt2, 3) == 0, "NTAG213 counters: index 2 bytes match capture exactly");
    CHECK(!img.tearing_valid[0], "NTAG213 tearing: index 0 stays invalid (never captured)");
    CHECK(img.tearing_valid[2], "NTAG213 tearing: index 2 captured");
    CHECK(img.tearing[2] == 0xBC, "NTAG213 tearing: index 2 value matches capture exactly");
}

/* Unlock feature (NFC-T2T-Unlock): a protected tag is no longer refused
 * outright -- see m1_t2t_emu_image.c's build() header comment. PROT=0
 * (write-only protection, the default make_ntag213_card() writes into
 * CFG1) never gates READ, so a genuinely complete capture (every page,
 * including 4..39, actually present) builds OK even though protected_tag
 * is true -- matching real NXP silicon behavior
 * behavior. */
static void test_ntag213_prot0_protected_still_builds_ok(void)
{
    make_ntag213_card(0x04 /* AUTH0=4: protection active from page 4 */, 0x0000);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213 PROT=0: protected-but-complete card builds OK (Unlock feature)");
    CHECK(img.protected_tag, "NTAG213 PROT=0: protected_tag correctly true");
    CHECK(!img.prot, "NTAG213 PROT=0: prot bit correctly false (write-only protection)");
    CHECK(img.auth0 == 4, "NTAG213 PROT=0: auth0 correctly captured");
    CHECK(!img.credential_valid, "NTAG213 PROT=0: no credential captured -- none was ever set");
}

/* PROT=1 (read+write protection): a real tag genuinely refuses READ past
 * AUTH0 while unauthenticated, so pages 4..39 would never have been
 * captured live -- modeled here by simply never storing them (the
 * existing per-page completeness gate refuses this honestly, exactly as
 * it already does for any other incomplete read; no special-casing). */
static void test_ntag213_prot1_incomplete_refused(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_NTAG213);
    test_t2t_set_capacity(45);
    /* Only identity (0-3) captured -- pages 4-39 (past AUTH0) were never
     * readable live under PROT=1, matching real hardware. */
    uint8_t bcc0 = (uint8_t)(0x88U ^ UID7[0] ^ UID7[1] ^ UID7[2]);
    uint8_t bcc1 = (uint8_t)(UID7[3] ^ UID7[4] ^ UID7[5] ^ UID7[6]);
    uint8_t p0[4] = { UID7[0], UID7[1], UID7[2], bcc0 };
    uint8_t p1[4] = { UID7[3], UID7[4], UID7[5], UID7[6] };
    uint8_t p2[4] = { bcc1, 0x48, 0x00, 0x00 };
    uint8_t p3[4] = { 0xE1, 0x10, 0x00, 0x00 };
    test_t2t_store_page(0, p0); test_t2t_store_page(1, p1);
    test_t2t_store_page(2, p2); test_t2t_store_page(3, p3);
    /* CFG0/CFG1 ARE readable regardless of PROT (real silicon always
     * allows discovering protection state pre-auth) -- AUTH0=4, PROT=1. */
    uint8_t cfg0[4] = { 0x00, 0x00, 0x00, 0x04 };
    uint8_t cfg1[4] = { 0x80, 0x05, 0x00, 0x00 };   /* bit7=PROT=1 */
    test_t2t_store_page(41, cfg0);
    test_t2t_store_page(42, cfg1);

    m1_t2t_emu_image_t img;
    memset(&img, 0xAA, sizeof(img));
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_INCOMPLETE_PAGES, "NTAG213 PROT=1: unauthenticated incomplete capture refused honestly");
    CHECK(img.page_count == 0, "NTAG213 PROT=1: image left zeroed on refusal");
}

/* Genuine, verified credential (as if Unlock had already run and the
 * physical tag accepted the password): build() must copy pwd/pack/
 * credential_valid straight from nfc_ctx, never fabricate or omit them. */
static void test_ntag213_credential_populated(void)
{
    make_ntag213_card(0xFF /* unprotected -- credential presence is independent of AUTH0 */, 0x0000);
    uint8_t pwd[4]  = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t pack[2] = { 0xAB, 0xCD };
    nfc_ctx_set_t2t_credential(pwd, pack);

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213 credential: card still builds OK");
    CHECK(img.credential_valid, "NTAG213 credential: credential_valid true");
    CHECK(memcmp(img.pwd, pwd, 4) == 0, "NTAG213 credential: pwd bytes match exactly");
    CHECK(memcmp(img.pack, pack, 2) == 0, "NTAG213 credential: pack bytes match exactly");
}

static void test_ntag213_no_credential_left_invalid(void)
{
    make_ntag213_card(0xFF, 0x0000);
    /* No nfc_ctx_set_t2t_credential() call -- must never be fabricated. */
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213 no credential: card still builds OK");
    CHECK(!img.credential_valid, "NTAG213 no credential: credential_valid stays false, never fabricated");
}

/* ---- UL11 (MIFARE Ultralight EV1, 48-byte): 20 pages -------------------
 * layout: 0-2 identity, 3 CC, 4-15 user, 16 CFG0, 17 CFG1, 18 PWD, 19 PACK.
 * NO dynamic-lock page (unlike NTAG21x) -- static lock alone covers 4-15. */
static void make_ul11_card(uint8_t auth0)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UL11);
    test_t2t_set_capacity(20);
    fill_identity_pages(4, 15);

    uint8_t cfg0[4] = { 0x00, 0x00, 0x00, auth0 };
    uint8_t cfg1[4] = { 0x00, 0x05, 0x00, 0x00 };
    uint8_t pwd[4]  = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t pack[4] = { 0x00, 0x00, 0x00, 0x00 };
    test_t2t_store_page(16, cfg0);
    test_t2t_store_page(17, cfg1);
    test_t2t_store_page(18, pwd);
    test_t2t_store_page(19, pack);
    nfc_ctx_set_t2t_signature(TEST_SIG);
}

static void test_ul11_ok(void)
{
    make_ul11_card(0xFF /* AUTH0=0xFF: never protected */);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "UL11: complete unprotected card builds OK");
    CHECK(img.page_count == 20, "UL11: page_count == 20");
    CHECK(!img.has_dynamic_lock, "UL11: no dynamic lock (static lock covers the whole user range)");
    CHECK(img.has_config_pages, "UL11: has config pages (password-protectable)");
    CHECK(img.user_first == 4 && img.user_last == 15, "UL11: user bounds 4..15");
    /* Every user page still writable (unlocked) despite no dynamic lock. */
    CHECK(m1_t2t_emu_image_page_writable(&img, 15), "UL11: page 15 (last user page) writable");
    CHECK(!m1_t2t_emu_image_page_writable(&img, 16), "UL11: CFG0 page never writable");
    CHECK(img.requires_signature, "UL11: requires_signature true");
    CHECK(img.signature_valid, "UL11: signature captured and copied in");
    CHECK(memcmp(img.signature, TEST_SIG, 32) == 0, "UL11: signature bytes match capture exactly");
    CHECK(img.cfg0_page == 16, "UL11: cfg0_page == 16 (PWD=18, PACK=19)");
}

static void test_ul11_missing_signature_refused(void)
{
    make_ul11_card(0xFF);
    nfc_ctx_clear_t2t_signature();
    m1_t2t_emu_image_t img;
    memset(&img, 0xAA, sizeof(img));
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_MISSING_SIGNATURE, "UL11: missing signature refused as its OWN dedicated status, not generic INCOMPLETE_PAGES");
    CHECK(img.page_count == 0, "UL11: image left zeroed when signature missing");
}

static void test_ul11_prot0_protected_still_builds_ok(void)
{
    /* Same Unlock-feature behavior as NTAG213: PROT=0 (write-only) never
     * gates READ, so a genuinely complete capture builds OK. */
    make_ul11_card(0x04 /* AUTH0=4: protection active from page 4 */);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "UL11 PROT=0: protected-but-complete card builds OK (Unlock feature)");
    CHECK(img.protected_tag, "UL11 PROT=0: protected_tag correctly true");
    CHECK(img.auth0 == 4, "UL11 PROT=0: auth0 correctly captured");
}

static void test_incomplete_pages_refused(void)
{
    make_ul_card();
    /* Rebuild without page 10 -- one gap inside an otherwise-complete dump. */
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UL);
    test_t2t_set_capacity(16);
    uint8_t bcc0 = (uint8_t)(0x88U ^ UID7[0] ^ UID7[1] ^ UID7[2]);
    uint8_t bcc1 = (uint8_t)(UID7[3] ^ UID7[4] ^ UID7[5] ^ UID7[6]);
    uint8_t p0[4] = { UID7[0], UID7[1], UID7[2], bcc0 };
    uint8_t p1[4] = { UID7[3], UID7[4], UID7[5], UID7[6] };
    uint8_t p2[4] = { bcc1, 0x48, 0x00, 0x00 };
    uint8_t p3[4] = { 0xE1, 0x10, 0x00, 0x00 };
    test_t2t_store_page(0, p0); test_t2t_store_page(1, p1);
    test_t2t_store_page(2, p2); test_t2t_store_page(3, p3);
    for (uint16_t p = 4; p <= 15; p++) {
        if (p == 10) continue;   /* gap */
        uint8_t data[4] = { (uint8_t)p, (uint8_t)p, (uint8_t)p, (uint8_t)p };
        test_t2t_store_page(p, data);
    }
    /* Force max_seen_unit up to 15 despite the gap by storing page 15 last
     * (already done above) -- the gap is a hole, not a truncation. */
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_INCOMPLETE_PAGES, "UL: mid-dump gap refused as INCOMPLETE_PAGES");
}

static void test_truncated_pages_refused(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UL);
    test_t2t_set_capacity(16);
    fill_identity_pages(4, 10);   /* only read up through page 10, not 15 */
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_INCOMPLETE_PAGES, "UL: truncated dump (11/16 pages) refused");
}

static void test_identity_inconsistent_refused(void)
{
    make_ul_card();
    /* Corrupt BCC0 in page 0 directly via a second store. */
    uint8_t bad_p0[4] = { UID7[0], UID7[1], UID7[2], (uint8_t)(0x00) };
    test_t2t_store_page(0, bad_p0);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_IDENTITY_INCONSISTENT, "UL: bad BCC0 refused as IDENTITY_INCONSISTENT");
}

static void test_unsupported_variant_refused(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_ULC);   /* not a launch variant */
    test_t2t_set_capacity(48);
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_UNSUPPORTED_VARIANT, "ULC: refused as UNSUPPORTED_VARIANT");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Tag type not supported") == 0,
          "ULC: exact refusal message for UNSUPPORTED_VARIANT");
}

static void test_no_source_refused(void)
{
    test_nfc_ctx_reset();
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_NO_SOURCE, "empty ctx: refused as NO_SOURCE");
}

static void test_static_lock_enforcement(void)
{
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UL);
    test_t2t_set_capacity(16);
    fill_identity_pages(4, 15);
    /* Set static lock bit for page 5 (bit 5 of the 16-bit static lock value
       at page2 bytes[2:4]). */
    uint16_t lockval = (uint16_t)(1U << 5);
    uint8_t bcc1 = (uint8_t)(UID7[3] ^ UID7[4] ^ UID7[5] ^ UID7[6]);
    uint8_t p2[4] = { bcc1, 0x48, (uint8_t)(lockval & 0xFF), (uint8_t)(lockval >> 8) };
    test_t2t_store_page(2, p2);

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "static lock: card still builds OK");
    CHECK(!m1_t2t_emu_image_page_writable(&img, 5), "static lock: page 5 not writable");
    CHECK(m1_t2t_emu_image_page_writable(&img, 6), "static lock: page 6 (unlocked) writable");

    uint8_t data[4] = { 0x99, 0x99, 0x99, 0x99 };
    CHECK(!m1_t2t_emu_image_write_page(&img, 5, data), "static lock: write to locked page rejected");
    CHECK(!img.dirty, "static lock: rejected write does not set dirty");
    CHECK(m1_t2t_emu_image_write_page(&img, 6, data), "static lock: write to unlocked page accepted");
    CHECK(img.dirty, "static lock: accepted write sets dirty");
    CHECK(memcmp(img.page[6], data, 4) == 0, "static lock: shadow actually mutated");
}

static void test_dynamic_lock_enforcement(void)
{
    /* granularity=2 for NTAG213: dynamic-lock bit 0 covers pages 16-17. */
    make_ntag213_card(0xFF, (uint16_t)(1U << 0));
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "dynamic lock: card builds OK");
    CHECK(!m1_t2t_emu_image_page_writable(&img, 16), "dynamic lock: page 16 locked (bit 0)");
    CHECK(!m1_t2t_emu_image_page_writable(&img, 17), "dynamic lock: page 17 locked (bit 0, same granularity pair)");
    CHECK(m1_t2t_emu_image_page_writable(&img, 18), "dynamic lock: page 18 unlocked (bit 1 not set)");
}

static void test_manufacturer_pages_never_writable(void)
{
    make_ul_card();
    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "manufacturer pages: card builds OK");
    for (uint16_t p = 0; p <= 3; p++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "manufacturer pages: page %u never writable", (unsigned)p);
        CHECK(!m1_t2t_emu_image_page_writable(&img, p), msg);
    }
    uint8_t data[4] = { 0, 0, 0, 0 };
    CHECK(!m1_t2t_emu_image_write_page(&img, 0, data), "manufacturer pages: write to UID page rejected");
}

static void test_get_version(void)
{
    uint8_t ver[8];
    CHECK(!m1_t2t_emu_image_get_version(M1NFC_T2TVAR_UL, ver), "UL: GET_VERSION not supported");
    CHECK(m1_t2t_emu_image_get_version(M1NFC_T2TVAR_UL11, ver), "UL11: GET_VERSION supported");
    CHECK(ver[6] == 0x0B, "UL11: storage-size byte 0x0B");
    CHECK(m1_t2t_emu_image_get_version(M1NFC_T2TVAR_NTAG213, ver), "NTAG213: GET_VERSION supported");
    CHECK(ver[6] == 0x0F, "NTAG213: storage-size byte 0x0F");
    CHECK(m1_t2t_emu_image_get_version(M1NFC_T2TVAR_NTAG215, ver), "NTAG215: GET_VERSION supported");
    CHECK(ver[6] == 0x11, "NTAG215: storage-size byte 0x11");
    CHECK(m1_t2t_emu_image_get_version(M1NFC_T2TVAR_NTAG216, ver), "NTAG216: GET_VERSION supported");
    CHECK(ver[6] == 0x13, "NTAG216: storage-size byte 0x13");
}

/* ---- Persistence-correction task: expected-vs-actual geometry, and the
 * dedicated GEOMETRY_INCONSISTENT status. m1_t2t_emu_image_build() itself
 * never talks to nfc_storage.c directly -- it only reads nfc_ctx's variant/
 * expected-pages/geometry-corrupt fields, which is exactly what the real
 * parser's t2t_resolve_and_validate_geometry() sets after its own cross-
 * checks. These tests drive those same nfc_ctx entries directly, proving
 * build()'s side of the contract without needing real FatFS file I/O
 * (nfc_storage.c/nfc_file.c's own format-string and cross-check logic are
 * covered separately by nfc_t2t_persistence_test.c's source-bound checks --
 * the two together cover the full parser->gate chain). ------------------- */

static void test_partial_ntag216_stays_ntag216_incomplete(void)
{
    /* The exact hardware scenario this task was created for: a genuinely-
     * identified NTAG216 whose read stopped at page 42 (0-41 valid). Must
     * reload/rebuild as "NTAG216, expected 231, valid 42, incomplete" --
     * NEVER as a complete-looking 42-page NTAG203. */
    test_nfc_ctx_reset();
    nfc_run_ctx_t *c = nfc_ctx_get();
    set_head_ul_common(c);
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_NTAG216);   /* explicit, not guessed from page count */
    test_t2t_set_capacity(231);                       /* full NTAG216 buffer capacity */
    test_t2t_set_expected_pages(231);                 /* declared/expected geometry */
    fill_identity_pages(4, 41);                        /* only 0-41 actually captured */

    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG216,
          "partial NTAG216: variant stays NTAG216, never reinterpreted as NTAG203");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 231,
          "partial NTAG216: expected pages == 231");
    CHECK(nfc_ctx_get_t2t_page_count() == 42,
          "partial NTAG216: valid/actually-present pages == 42");

    m1_t2t_emu_image_t img;
    memset(&img, 0xAA, sizeof(img));
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_INCOMPLETE_PAGES,
          "partial NTAG216: refused as INCOMPLETE_PAGES (honest), never OK, never UNSUPPORTED_VARIANT (which NTAG203 would trigger)");
    CHECK(img.page_count == 0, "partial NTAG216: image left zeroed on refusal, no fabricated pages 42-230");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Complete tag read required") == 0,
          "partial NTAG216: exact refusal message");
}

static void test_geometry_inconsistent_refused(void)
{
    /* A file nfc_storage.c's own cross-check flagged corrupt (e.g.
     * "T2T Variant: NTAG216" but "Pages: 45", or a version tuple that
     * disagrees with the declared name). Must be refused as its own
     * distinct status -- never silently treated as OK, INCOMPLETE_PAGES,
     * or UNSUPPORTED_VARIANT -- and checked before every other gate, even
     * on a card that would otherwise pass every other check. */
    make_ntag213_card(0xFF, 0x0000);   /* otherwise a fully valid, complete card */
    nfc_ctx_set_t2t_geometry_corrupt(true);

    m1_t2t_emu_image_t img;
    memset(&img, 0xAA, sizeof(img));
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_GEOMETRY_INCONSISTENT,
          "geometry corrupt: refused as GEOMETRY_INCONSISTENT even though every other check would pass");
    CHECK(img.page_count == 0, "geometry corrupt: image left zeroed on refusal");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Invalid saved tag data") == 0,
          "geometry corrupt: exact refusal message");
}

static void test_status_message_never_null(void)
{
    m1_t2t_emu_status_t all[] = {
        M1_T2T_EMU_OK, M1_T2T_EMU_NO_SOURCE, M1_T2T_EMU_UNSUPPORTED_VARIANT,
        M1_T2T_EMU_INCOMPLETE_PAGES, M1_T2T_EMU_MISSING_SIGNATURE, M1_T2T_EMU_GEOMETRY_INCONSISTENT,
        M1_T2T_EMU_IDENTITY_INCONSISTENT,
    };
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) {
        CHECK(m1_t2t_emu_status_message(all[i]) != NULL, "status message never NULL");
    }
}

int main(void)
{
    test_ul_ok();
    test_ntag213_ok();
    test_ntag213_missing_signature_refused();
    test_ntag213_prot0_protected_still_builds_ok();
    test_ntag213_prot1_incomplete_refused();
    test_ntag213_credential_populated();
    test_ntag213_no_credential_left_invalid();
    test_counters_and_tearing_captured_honestly();
    test_ul11_ok();
    test_ul11_missing_signature_refused();
    test_ul11_prot0_protected_still_builds_ok();
    test_incomplete_pages_refused();
    test_truncated_pages_refused();
    test_identity_inconsistent_refused();
    test_unsupported_variant_refused();
    test_no_source_refused();
    test_static_lock_enforcement();
    test_dynamic_lock_enforcement();
    test_manufacturer_pages_never_writable();
    test_get_version();
    test_partial_ntag216_stays_ntag216_incomplete();
    test_geometry_inconsistent_refused();
    test_status_message_never_null();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
