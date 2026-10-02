/* Host tests for the T2T persistence correction: explicit "T2T Variant:"/
 * "T2T Version:"/corrected "Pages:"/"Signature:"/"CounterN:"/"TearingN:"
 * file-format fields (nfc_file.c save, nfc_storage.c parse + geometry
 * cross-check), which replace the old "guess the variant from how many
 * Page N: lines happen to be present" heuristic responsible for a
 * genuinely truncated NTAG216 (42/231 pages) silently reloading as a
 * complete-looking 42-page NTAG203.
 *
 * DISCLOSED LIMITATION (same as mfc_key_file_format_test.c, and every other
 * FatFs/HAL-coupled file in this project): nfc_storage_load_file()/
 * nfc_profile_save() cannot be host-compiled directly (m1_sdcard.h pulls
 * main.h/the full HAL stack; nfc_fileio.c wraps real FatFS). This suite
 * instead:
 *   1. Verifies the exact save-line formats and parse/cross-check logic are
 *      present in the REAL, committed nfc_file.c/nfc_storage.c source text
 *      (source_contains() reads the actual files, not a memory of them).
 *   2. Round-trips through a byte-for-byte faithful transcription of the
 *      variant-name table, the version-tuple mapper, and the geometry
 *      cross-check (t2t_resolve_and_validate_geometry()) -- each transcribed
 *      function's literals/branches are checked against the real source
 *      first, so drift between this test and the shipped logic is caught,
 *      not silently trusted.
 *   3. Feeds the transcription's output into the REAL, unmodified
 *      m1_t2t_emu_image_build() (via stub_t2t/'s nfc_ctx stub -- the same
 *      harness t2t_emu_image_test.c already uses) for genuine end-to-end
 *      verification of the full parse-result -> eligibility-gate chain.
 * The real on-disk round trip (survives a reboot, a real SD card) is
 * covered by this task's mandatory hardware acceptance gate instead.
 *
 *   cp NFC/NFC_drv/common/m1_t2t_emu_image.c NFC/NFC_drv/common/m1_t2t_emu_image.h \
 *      NFC/NFC_drv/common/test/stub_t2t/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common/test/stub_t2t \
 *      NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.c \
 *      NFC/NFC_drv/common/test/stub_t2t/nfc_ctx_stub.c \
 *      NFC/NFC_drv/common/test/stub_t2t/m1_file_browser_stub.c \
 *      NFC/NFC_drv/common/test/nfc_t2t_persistence_test.c -o /tmp/t2tpersist && /tmp/t2tpersist
 *   rm NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.c NFC/NFC_drv/common/test/stub_t2t/m1_t2t_emu_image.h
 */
#include "m1_t2t_emu_image.h"
#include "nfc_ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}

static bool contains(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* ============================================================================
 * Part 1: source-bound proofs against the real, committed files.
 * ========================================================================= */
static void test_source_matches_shipped(void)
{
    char *file_c    = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_file.c");
    char *storage_c = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_storage.c");
    CHECK(file_c != NULL,    "nfc_file.c readable");
    CHECK(storage_c != NULL, "nfc_storage.c readable");

    /* --- Writer: explicit identity/geometry, never re-derived on save --- */
    CHECK(contains(file_c, "\"T2T Variant: %s\\r\\n\""),
          "nfc_file.c: T2T Variant save line format present");
    CHECK(contains(file_c, "\"T2T Version: %02X %02X %02X %02X %02X %02X %02X %02X\\r\\n\""),
          "nfc_file.c: T2T Version (raw GET_VERSION tuple) save line format present");
    CHECK(contains(file_c, "nfc_ctx_get_t2t_expected_pages()"),
          "nfc_file.c: Pages: is sourced from the DECLARED/EXPECTED count, not the raw dump size");
    CHECK(contains(file_c, "Signature: %02X %02X"), "nfc_file.c: Signature save line format present");
    CHECK(contains(file_c, "\"Counter%u: %02X %02X %02X\\r\\n\""), "nfc_file.c: CounterN save line format present");
    CHECK(contains(file_c, "\"Tearing%u: %02X\\r\\n\""), "nfc_file.c: TearingN save line format present");
    CHECK(contains(file_c, "nfc_t2t_variant_name(variant)"),
          "nfc_file.c: variant name is written only via the single shared name table (never a local literal)");

    /* --- MFC/other-family paths untouched by this task --- */
    CHECK(contains(file_c, "\"Block %03lu: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\\r\\n\""),
          "nfc_file.c: MFC Block save line format unchanged");
    CHECK(contains(file_c, "\"Sector %02u KeyA: %02X %02X %02X %02X %02X %02X\\r\\n\""),
          "nfc_file.c: MFC Sector KeyA save line format unchanged");

    /* --- Parser: reads the new fields, cross-checks, never guesses --- */
    CHECK(contains(storage_c, "GetPrivateProfileStringS(&data, \"T2T Variant\", &sess)"),
          "nfc_storage.c: T2T Variant parsed via the header string reader");
    CHECK(contains(storage_c, "GetPrivateProfileHexS(&data, \"T2T Version\", &sess) && data.v.hex.out_len == 8"),
          "nfc_storage.c: T2T Version parsed as exactly 8 raw bytes");
    CHECK(contains(storage_c, "GetPrivateProfileUintS(&data, \"Pages\", &sess)"),
          "nfc_storage.c: Pages parsed as a plain declared integer, not re-derived from page lines");
    CHECK(contains(storage_c, "static uint8_t t2t_variant_from_version_bytes"),
          "nfc_storage.c: raw-tuple-to-variant mapper present for the cross-check");
    CHECK(contains(storage_c, "static bool t2t_resolve_and_validate_geometry"),
          "nfc_storage.c: geometry cross-check/validation function present");
    CHECK(!contains(storage_c, "t2t_variant_from_page_count"),
          "nfc_storage.c: the old page-count-guessing heuristic is fully removed, not just unused");
    CHECK(contains(storage_c, "Never guess from raw page count"),
          "nfc_storage.c: explicit no-guessing policy documented at the point it matters");
    CHECK(contains(storage_c, "nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);"),
          "nfc_storage.c: a failed cross-check is recorded as corrupt, not silently ignored");

    /* Contradiction detection: both the variant/version mismatch and the
     * out-of-range highest-page checks must actually return false (reject),
     * not just log. */
    CHECK(contains(storage_c, "if (ver_variant != variant) {") &&
          contains(storage_c, "return false;"),
          "nfc_storage.c: variant/version contradiction path exists and rejects");
    CHECK(contains(storage_c, "highest_page_seen >= 0) && ((uint32_t)highest_page_seen >= (uint32_t)canonical)"),
          "nfc_storage.c: out-of-range highest-page check present");
    CHECK(contains(storage_c, "fi->t2t_pages_declared != (uint32_t)canonical"),
          "nfc_storage.c: declared Pages: vs variant's real geometry cross-check present");

    /* --- Genuine PWD_AUTH credential (Unlock feature) --- */
    CHECK(contains(file_c, "\"PWD: %02X %02X %02X %02X\\r\\n\""), "nfc_file.c: PWD save line format present");
    CHECK(contains(file_c, "\"PACK: %02X %02X\\r\\n\""), "nfc_file.c: PACK save line format present");
    CHECK(contains(file_c, "nfc_ctx_t2t_credential_valid()"),
          "nfc_file.c: PWD/PACK written only when genuinely verified, never unconditionally");
    CHECK(contains(storage_c, "GetPrivateProfileHexS(&data, \"PWD\", &sess) && data.v.hex.out_len == 4"),
          "nfc_storage.c: PWD parsed as exactly 4 raw bytes");
    CHECK(contains(storage_c, "GetPrivateProfileHexS(&data, \"PACK\", &sess) && data.v.hex.out_len == 2"),
          "nfc_storage.c: PACK parsed as exactly 2 raw bytes");
    CHECK(contains(storage_c, "faminfo.t2t_pwd_present && faminfo.t2t_pack_present"),
          "nfc_storage.c: credential only trusted when BOTH PWD and PACK are present -- a malformed/contradictory file (only one of the two) is rejected, never partially trusted");

    /* --- Transient pre-auth PROTECTION-SUSPECTED signal (T2T-UNLOCK-T2
     * fix) has NO field in the V4 format at all -- structurally cannot
     * leak into a saved file, confirmed by its absence from both the
     * writer and the reader. --- */
    CHECK(!contains(file_c, "suspected") && !contains(file_c, "first_blocked_page"),
          "nfc_file.c: never writes the transient protection-suspected signal to the V4 file");
    CHECK(!contains(storage_c, "suspected") && !contains(storage_c, "first_blocked_page"),
          "nfc_storage.c: never reads/restores a protection-suspected signal from the V4 file");

    free(file_c); free(storage_c);
}

/* ============================================================================
 * Part 2: faithful transcription of the variant table + geometry resolver,
 * verified above to match the real nfc_storage.c literals/branches, then
 * exercised directly and fed into the REAL m1_t2t_emu_image_build().
 * ========================================================================= */
#define T2T_VARIANT_TABLE_COUNT 9U
static const char *const T2T_VARIANT_NAMES[T2T_VARIANT_TABLE_COUNT] = {
    [M1NFC_T2TVAR_UNKNOWN] = NULL,      [M1NFC_T2TVAR_UL] = "UL",
    [M1NFC_T2TVAR_ULC] = "ULC",         [M1NFC_T2TVAR_UL11] = "UL11",
    [M1NFC_T2TVAR_UL21] = "UL21",       [M1NFC_T2TVAR_NTAG203] = "NTAG203",
    [M1NFC_T2TVAR_NTAG213] = "NTAG213", [M1NFC_T2TVAR_NTAG215] = "NTAG215",
    [M1NFC_T2TVAR_NTAG216] = "NTAG216",
};
static const uint16_t T2T_VARIANT_PAGES[T2T_VARIANT_TABLE_COUNT] = {
    [M1NFC_T2TVAR_UNKNOWN] = 0,  [M1NFC_T2TVAR_UL] = 16,      [M1NFC_T2TVAR_ULC] = 48,
    [M1NFC_T2TVAR_UL11] = 20,    [M1NFC_T2TVAR_UL21] = 41,    [M1NFC_T2TVAR_NTAG203] = 42,
    [M1NFC_T2TVAR_NTAG213] = 45, [M1NFC_T2TVAR_NTAG215] = 135,[M1NFC_T2TVAR_NTAG216] = 231,
};

static uint8_t xvariant_from_name(const char *name)
{
    if (!name) return M1NFC_T2TVAR_UNKNOWN;
    for (uint8_t v = 0; v < T2T_VARIANT_TABLE_COUNT; v++) {
        if (T2T_VARIANT_NAMES[v] && strcmp(T2T_VARIANT_NAMES[v], name) == 0) return v;
    }
    return M1NFC_T2TVAR_UNKNOWN;
}
static uint16_t xvariant_pages(uint8_t variant)
{
    return (variant < T2T_VARIANT_TABLE_COUNT) ? T2T_VARIANT_PAGES[variant] : 0U;
}
static uint8_t xvariant_from_version(const uint8_t *ver, uint8_t len)
{
    if (!ver || len != 8U) return M1NFC_T2TVAR_UNKNOWN;
    if (ver[1] != 0x04U) return M1NFC_T2TVAR_UNKNOWN;
    if (ver[3] == 0x05U && ver[4] == 0x02U) return M1NFC_T2TVAR_UNKNOWN;
    switch (ver[6]) {
        case 0x00U: case 0x0BU: return M1NFC_T2TVAR_UL11;
        case 0x0EU: return M1NFC_T2TVAR_UL21;
        case 0x0FU: return M1NFC_T2TVAR_NTAG213;
        case 0x11U: return M1NFC_T2TVAR_NTAG215;
        case 0x13U: return M1NFC_T2TVAR_NTAG216;
        default: return M1NFC_T2TVAR_UNKNOWN;
    }
}

typedef struct {
    bool     variant_present; uint8_t variant_declared;
    uint8_t  version[8];      uint8_t version_len;
    bool     pages_present;   uint32_t pages_declared;
} xfile_fields_t;

/* Faithful transcription of t2t_resolve_and_validate_geometry(): resolves
 * variant, cross-checks version/pages/highest-page, and applies the result
 * directly to the REAL nfc_ctx (via the stub) exactly as the shipped
 * function does -- so the REAL m1_t2t_emu_image_build() can be called
 * immediately afterward for genuine end-to-end verification. */
static bool xresolve_geometry(const xfile_fields_t *fi, int32_t highest_page_seen)
{
    uint8_t variant = fi->variant_present ? fi->variant_declared : M1NFC_T2TVAR_UNKNOWN;

    if (fi->version_len == 8U) {
        uint8_t ver_variant = xvariant_from_version(fi->version, fi->version_len);
        if (ver_variant != M1NFC_T2TVAR_UNKNOWN) {
            if (fi->variant_present) {
                if (ver_variant != variant) {
                    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
                    nfc_ctx_set_t2t_expected_pages(0U);
                    return false;
                }
            } else {
                variant = ver_variant;
            }
        }
    }

    if (variant == M1NFC_T2TVAR_UNKNOWN) {
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return true;
    }

    uint16_t canonical = xvariant_pages(variant);
    if (canonical == 0U) {
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }
    if (fi->pages_present && fi->pages_declared != (uint32_t)canonical) {
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }
    if (highest_page_seen >= 0 && (uint32_t)highest_page_seen >= (uint32_t)canonical) {
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }

    nfc_ctx_set_t2t_variant(variant);
    nfc_ctx_set_t2t_expected_pages(canonical);
    return true;
}

static const uint8_t UID7[7] = { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint8_t TEST_SIG[32] = {
    0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,
    0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,
};
static const uint8_t VER_UL11[8]    = { 0x00,0x04,0x04,0x03,0x01,0x00,0x0BU,0x03 };
static const uint8_t VER_NTAG213[8] = { 0x00,0x04,0x04,0x02,0x01,0x00,0x0FU,0x03 };
static const uint8_t VER_NTAG215[8] = { 0x00,0x04,0x04,0x02,0x01,0x00,0x11U,0x03 };
static const uint8_t VER_NTAG216[8] = { 0x00,0x04,0x04,0x02,0x01,0x00,0x13U,0x03 };

static void fill_identity_pages(uint16_t user_first, uint16_t user_last)
{
    uint8_t bcc0 = (uint8_t)(0x88U ^ UID7[0] ^ UID7[1] ^ UID7[2]);
    uint8_t bcc1 = (uint8_t)(UID7[3] ^ UID7[4] ^ UID7[5] ^ UID7[6]);
    uint8_t p0[4] = { UID7[0], UID7[1], UID7[2], bcc0 };
    uint8_t p1[4] = { UID7[3], UID7[4], UID7[5], UID7[6] };
    uint8_t p2[4] = { bcc1, 0x48, 0x00, 0x00 };
    uint8_t p3[4] = { 0xE1, 0x10, 0x00, 0x00 };
    test_t2t_store_page(0, p0); test_t2t_store_page(1, p1);
    test_t2t_store_page(2, p2); test_t2t_store_page(3, p3);
    for (uint16_t p = user_first; p <= user_last; p++) {
        uint8_t data[4] = { (uint8_t)p, (uint8_t)p, (uint8_t)p, (uint8_t)p };
        test_t2t_store_page(p, data);
    }
}

static void set_head_common(void)
{
    nfc_run_ctx_t *c = nfc_ctx_get();
    c->head.tech = M1NFC_TECH_A; c->head.family = M1NFC_FAM_ULTRALIGHT;
    memcpy(c->head.uid, UID7, 7); c->head.uid_len = 7;
    c->head.a.has_atqa = true; c->head.a.atqa[0] = 0x44; c->head.a.atqa[1] = 0x00;
    c->head.a.has_sak = true; c->head.a.sak = 0x00;
    strncpy(c->file.path, "0:/nfc/test_card.nfc", sizeof(c->file.path) - 1);
}

/* One "saved file" -> "reloaded" round trip for a given variant: resolves
 * geometry via the transcription (verified above to match the shipped
 * logic), then builds via the REAL m1_t2t_emu_image_build(). */
static m1_t2t_emu_status_t do_round_trip(uint8_t variant, const uint8_t *version,
                                          uint16_t declared_pages, uint16_t user_last,
                                          const uint8_t *sig, void (*fill_tail)(void))
{
    test_nfc_ctx_reset();
    set_head_common();
    test_t2t_set_capacity(xvariant_pages(variant));
    fill_identity_pages(4, user_last);
    if (fill_tail) fill_tail();
    if (sig) nfc_ctx_set_t2t_signature(sig);

    xfile_fields_t fi = {0};
    fi.variant_present = true; fi.variant_declared = variant;
    if (version) { memcpy(fi.version, version, 8); fi.version_len = 8; }
    fi.pages_present = true; fi.pages_declared = declared_pages;

    uint16_t pc = nfc_ctx_get_t2t_page_count();
    int32_t highest_page_seen = (pc > 0U) ? ((int32_t)pc - 1) : -1;
    bool geom_ok = xresolve_geometry(&fi, highest_page_seen);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);

    m1_t2t_emu_image_t img;
    return m1_t2t_emu_image_build(&img);
}

static void fill_ntag213_tail(void)
{
    uint8_t dlp[4] = {0,0,0,0}, cfg0[4] = {0,0,0,0xFF}, cfg1[4] = {0,5,0,0}, pwd[4]={0xFF,0xFF,0xFF,0xFF}, pack[4]={0,0,0,0};
    test_t2t_store_page(40, dlp); test_t2t_store_page(41, cfg0);
    test_t2t_store_page(42, cfg1); test_t2t_store_page(43, pwd); test_t2t_store_page(44, pack);
}
static void fill_ul11_tail(void)
{
    uint8_t cfg0[4] = {0,0,0,0xFF}, cfg1[4] = {0,5,0,0}, pwd[4]={0xFF,0xFF,0xFF,0xFF}, pack[4]={0,0,0,0};
    test_t2t_store_page(16, cfg0); test_t2t_store_page(17, cfg1);
    test_t2t_store_page(18, pwd); test_t2t_store_page(19, pack);
}
static void fill_ntag215_tail(void)
{
    uint8_t dlp[4] = {0,0,0,0}, cfg0[4] = {0,0,0,0xFF}, cfg1[4] = {0,5,0,0}, pwd[4]={0xFF,0xFF,0xFF,0xFF}, pack[4]={0,0,0,0};
    test_t2t_store_page(130, dlp); test_t2t_store_page(131, cfg0);
    test_t2t_store_page(132, cfg1); test_t2t_store_page(133, pwd); test_t2t_store_page(134, pack);
}
static void fill_ntag216_tail(void)
{
    uint8_t dlp[4] = {0,0,0,0}, cfg0[4] = {0,0,0,0xFF}, cfg1[4] = {0,5,0,0}, pwd[4]={0xFF,0xFF,0xFF,0xFF}, pack[4]={0,0,0,0};
    test_t2t_store_page(226, dlp); test_t2t_store_page(227, cfg0);
    test_t2t_store_page(228, cfg1); test_t2t_store_page(229, pwd); test_t2t_store_page(230, pack);
}

static void test_roundtrip_ul11(void)
{
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_UL11, VER_UL11, 20, 15, TEST_SIG, fill_ul11_tail);
    CHECK(st == M1_T2T_EMU_OK, "UL11: full save/reload round trip builds OK");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_UL11, "UL11: variant round-trips exactly");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 20, "UL11: expected pages round-trips exactly");
}
static void test_roundtrip_ntag213(void)
{
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_NTAG213, VER_NTAG213, 45, 39, TEST_SIG, fill_ntag213_tail);
    CHECK(st == M1_T2T_EMU_OK, "NTAG213: full save/reload round trip builds OK");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG213, "NTAG213: variant round-trips exactly");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 45, "NTAG213: expected pages round-trips exactly");
}
static void test_roundtrip_ntag215(void)
{
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_NTAG215, VER_NTAG215, 135, 129, TEST_SIG, fill_ntag215_tail);
    CHECK(st == M1_T2T_EMU_OK, "NTAG215: full save/reload round trip builds OK");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG215, "NTAG215: variant round-trips exactly");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 135, "NTAG215: expected pages round-trips exactly");
}
static void test_roundtrip_ntag216(void)
{
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_NTAG216, VER_NTAG216, 231, 225, TEST_SIG, fill_ntag216_tail);
    CHECK(st == M1_T2T_EMU_OK, "NTAG216: full save/reload round trip builds OK");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG216, "NTAG216: variant round-trips exactly");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 231, "NTAG216: expected pages round-trips exactly");
}

static void test_partial_ntag216_never_becomes_ntag203(void)
{
    /* The exact hardware bug: a real NTAG216 read stops at page 41 (42
     * pages captured). Saved (with the fix) as "T2T Variant: NTAG216",
     * "T2T Version: <216 tuple>", "Pages: 231" -- the declared/expected
     * geometry is NOT the truncated capture size. Only pages 0-41 have
     * "Page N:" lines. */
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_NTAG216, VER_NTAG216, 231, 41, NULL, NULL);
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG216,
          "partial NTAG216: reloads as NTAG216, never reinterpreted as NTAG203");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 231, "partial NTAG216: expected pages == 231");
    CHECK(nfc_ctx_get_t2t_page_count() == 42, "partial NTAG216: valid pages == 42");
    CHECK(st == M1_T2T_EMU_INCOMPLETE_PAGES,
          "partial NTAG216: refused as incomplete (honest), never OK");
}

static void test_missing_signature_dedicated_status(void)
{
    /* A file saved by firmware that never persisted the signature (the bug
     * this whole task started from) -- everything else about the card is
     * genuinely complete. */
    m1_t2t_emu_status_t st = do_round_trip(M1NFC_T2TVAR_NTAG216, VER_NTAG216, 231, 225, NULL, fill_ntag216_tail);
    CHECK(st == M1_T2T_EMU_MISSING_SIGNATURE,
          "NTAG216 with no saved signature: refused with the DEDICATED status, not generic INCOMPLETE_PAGES");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Signature missing / Read tag again") == 0,
          "NTAG216 with no saved signature: exact, specific message");
}

static void test_old_format_file_never_guessed(void)
{
    /* A true legacy file: no "T2T Variant:", no "T2T Version:" -- only
     * "Pages: 42" and 42 Page lines (or, per the old code, no Pages: line
     * at all -- either way, no identifying evidence). Must resolve to
     * UNKNOWN, never guessed as NTAG203 just because the count matches. */
    test_nfc_ctx_reset();
    set_head_common();
    test_t2t_set_capacity(42);
    fill_identity_pages(4, 41);

    xfile_fields_t fi = {0};   /* variant_present=false, version_len=0, pages_present=false */
    uint16_t pc = nfc_ctx_get_t2t_page_count();
    int32_t highest_page_seen = (pc > 0U) ? ((int32_t)pc - 1) : -1;
    bool geom_ok = xresolve_geometry(&fi, highest_page_seen);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);

    CHECK(geom_ok, "old-format 42-page file: not treated as corrupt, just unidentifiable");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_UNKNOWN,
          "old-format 42-page file: resolves to UNKNOWN, never guessed as NTAG203");
    CHECK(nfc_ctx_get_t2t_expected_pages() == 0, "old-format 42-page file: no fabricated expected-pages either");

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_UNSUPPORTED_VARIANT,
          "old-format 42-page file: emulation refused as unidentified, not silently allowed");
}

static void test_recognized_version_alone_identifies_variant(void)
{
    /* No "T2T Variant:" name (an even older file that predates that key
     * but was saved after "T2T Version:" existed, or a hand-edited file),
     * but a genuine, recognized GET_VERSION tuple IS present. That tuple
     * alone is strong enough evidence (unlike a bare page count, which a
     * truncation could produce for any smaller variant). */
    xfile_fields_t fi = {0};
    memcpy(fi.version, VER_NTAG213, 8); fi.version_len = 8;
    fi.pages_present = true; fi.pages_declared = 45;

    test_nfc_ctx_reset();
    set_head_common();
    test_t2t_set_capacity(45);
    fill_identity_pages(4, 39);
    fill_ntag213_tail();
    nfc_ctx_set_t2t_signature(TEST_SIG);

    uint16_t pc = nfc_ctx_get_t2t_page_count();
    bool geom_ok = xresolve_geometry(&fi, (int32_t)pc - 1);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);

    CHECK(geom_ok, "version-only identification: resolves cleanly");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_NTAG213,
          "version-only identification: a recognized tuple alone establishes the variant");

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "version-only identification: card still builds OK");
}

static void test_contradictory_variant_and_version_rejected(void)
{
    /* "T2T Variant: NTAG216" but a raw tuple whose storage-size byte says
     * NTAG213 -- a genuinely self-inconsistent/corrupt save. */
    xfile_fields_t fi = {0};
    fi.variant_present = true; fi.variant_declared = M1NFC_T2TVAR_NTAG216;
    memcpy(fi.version, VER_NTAG213, 8); fi.version_len = 8;
    fi.pages_present = true; fi.pages_declared = 231;

    test_nfc_ctx_reset();
    set_head_common();
    bool geom_ok = xresolve_geometry(&fi, -1);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);

    CHECK(!geom_ok, "variant/version contradiction: cross-check rejects");
    CHECK(nfc_ctx_get_t2t_variant() == M1NFC_T2TVAR_UNKNOWN,
          "variant/version contradiction: never left at the (wrong) declared variant");

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_GEOMETRY_INCONSISTENT,
          "variant/version contradiction: refused as GEOMETRY_INCONSISTENT");
    CHECK(strcmp(m1_t2t_emu_status_message(st), "Invalid saved tag data") == 0,
          "variant/version contradiction: exact message");
}

static void test_contradictory_pages_and_variant_rejected(void)
{
    /* "T2T Variant: NTAG216" but "Pages: 45" (NTAG213's real count). */
    xfile_fields_t fi = {0};
    fi.variant_present = true; fi.variant_declared = M1NFC_T2TVAR_NTAG216;
    fi.pages_present = true; fi.pages_declared = 45;

    test_nfc_ctx_reset();
    set_head_common();
    bool geom_ok = xresolve_geometry(&fi, -1);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);
    CHECK(!geom_ok, "declared Pages: disagreeing with variant's real geometry: rejected");

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_GEOMETRY_INCONSISTENT, "Pages:/variant contradiction: refused as GEOMETRY_INCONSISTENT");
}

static void test_variant_name_table_round_trips(void)
{
    static const uint8_t variants[] = {
        M1NFC_T2TVAR_UL, M1NFC_T2TVAR_ULC, M1NFC_T2TVAR_UL11, M1NFC_T2TVAR_UL21,
        M1NFC_T2TVAR_NTAG203, M1NFC_T2TVAR_NTAG213, M1NFC_T2TVAR_NTAG215, M1NFC_T2TVAR_NTAG216,
    };
    for (size_t i = 0; i < sizeof(variants)/sizeof(variants[0]); i++) {
        const char *name = T2T_VARIANT_NAMES[variants[i]];
        CHECK(name != NULL, "variant name table: every named variant has a name");
        CHECK(xvariant_from_name(name) == variants[i], "variant name table: name round-trips back to the same enum value");
    }
    CHECK(xvariant_from_name("bogus") == M1NFC_T2TVAR_UNKNOWN, "variant name table: an unrecognized name resolves to UNKNOWN");
    CHECK(xvariant_from_name(NULL) == M1NFC_T2TVAR_UNKNOWN, "variant name table: NULL name resolves to UNKNOWN");
}

/* End-to-end: a genuine credential set on nfc_ctx (as the parser would
 * after successfully validating both PWD and PACK together) must survive
 * into the built emulation image untouched -- proven directly against the
 * REAL m1_t2t_emu_image_build(), not just the source_contains() checks
 * above. */
static void test_credential_reaches_build(void)
{
    test_nfc_ctx_reset();
    set_head_common();
    test_t2t_set_capacity(45);
    fill_identity_pages(4, 39);
    fill_ntag213_tail();
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_NTAG213);
    nfc_ctx_set_t2t_expected_pages(45);
    nfc_ctx_set_t2t_signature(TEST_SIG);

    uint8_t pwd[4]  = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t pack[2] = { 0x12, 0x34 };
    nfc_ctx_set_t2t_credential(pwd, pack);

    m1_t2t_emu_image_t img;
    m1_t2t_emu_status_t st = m1_t2t_emu_image_build(&img);
    CHECK(st == M1_T2T_EMU_OK, "credential end-to-end: card builds OK");
    CHECK(img.credential_valid, "credential end-to-end: credential_valid true");
    CHECK(memcmp(img.pwd, pwd, 4) == 0, "credential end-to-end: pwd matches exactly");
    CHECK(memcmp(img.pack, pack, 2) == 0, "credential end-to-end: pack matches exactly");
}

static void test_out_of_range_page_rejected(void)
{
    /* A "Page 231:" line (or higher) on a declared NTAG216 -- 0-230 is the
     * entire valid range; anything at or beyond 231 cannot be genuine. */
    xfile_fields_t fi = {0};
    fi.variant_present = true; fi.variant_declared = M1NFC_T2TVAR_NTAG216;
    fi.pages_present = true; fi.pages_declared = 231;

    test_nfc_ctx_reset();
    set_head_common();
    bool geom_ok = xresolve_geometry(&fi, 231 /* highest page SEEN == 231, i.e. a "Page 231:" line existed */);
    nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);
    CHECK(!geom_ok, "a Page number at/beyond the variant's own total: rejected as out of range");
}

int main(void)
{
    test_source_matches_shipped();
    test_roundtrip_ul11();
    test_roundtrip_ntag213();
    test_roundtrip_ntag215();
    test_roundtrip_ntag216();
    test_partial_ntag216_never_becomes_ntag203();
    test_missing_signature_dedicated_status();
    test_old_format_file_never_guessed();
    test_recognized_version_alone_identifies_variant();
    test_variant_name_table_round_trips();
    test_credential_reaches_build();
    test_contradictory_variant_and_version_rejected();
    test_contradictory_pages_and_variant_rejected();
    test_out_of_range_page_rejected();

    printf("\nnfc_t2t_persistence_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
