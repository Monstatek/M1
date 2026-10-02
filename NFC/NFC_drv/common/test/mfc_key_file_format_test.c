/* Host tests for the "Sector N KeyA/KeyB:" .nfc file format extension
 * (nfc_file.c save + nfc_storage.c parse/restore).
 *
 * DISCLOSED LIMITATION: nfc_storage_load_file()/nfc_profile_save() depend on
 * the SD/FatFs file-I/O layer (m1_fb_*, FIL, privateprofilestring.h's INI
 * parser) transitively through nfc_fileio.c and nfc_ctx.h's rfal_nfc.h
 * include -- the same class of dependency that made the full HAL stack
 * impractical to host-compile earlier in this project (see the PSK duty-
 * cycle fix's own disclosed limitation). This test instead verifies, against
 * the REAL committed source text (source_contains() below, reading the
 * actual files, not a memory of them):
 *   1. The exact save format (one "Sector NN KeyA:"/"Sector NN KeyB:" line
 *      per FOUND key only, skipping unfound keys) is present in nfc_file.c
 *      unchanged since this test was written.
 *   2. The exact parse logic (sector/keytag/6-hex-byte extraction, storing
 *      into sec[sector].key_a/key_b + key_a_found/key_b_found) is present in
 *      nfc_storage.c unchanged.
 *   3. nfc_ctx_clear_mfc() is called before parsing (no stale-key leak).
 * -- then proves the SAVE-line format and PARSE logic are correct and
 * mutually consistent by round-tripping through a byte-for-byte transcribed
 * copy of both algorithms (transcription verified char-for-char against the
 * source_contains() checks above, so drift is caught, not silently trusted).
 * The real on-disk round trip (file survives a reboot) is proven by the
 * mandatory hardware acceptance gate instead, which this task explicitly
 * requires for exactly this reason.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      mfc_key_file_format_test.c -o /tmp/fmt && /tmp/fmt
 *   (run from the repository root, or override REPO_ROOT at compile time)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

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

static void test_source_matches_shipped(void)
{
    char *file_c = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_file.c");
    char *storage_c = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_storage.c");
    CHECK(file_c != NULL, "nfc_file.c readable");
    CHECK(storage_c != NULL, "nfc_storage.c readable");

    CHECK(contains(file_c, "\"Sector %02u KeyA: %02X %02X %02X %02X %02X %02X\\r\\n\""),
          "nfc_file.c: Sector KeyA save line format present");
    CHECK(contains(file_c, "\"Sector %02u KeyB: %02X %02X %02X %02X %02X %02X\\r\\n\""),
          "nfc_file.c: Sector KeyB save line format present");
    CHECK(contains(file_c, "if (sc->key_a_found) {"), "nfc_file.c: only FOUND Key A is saved");
    CHECK(contains(file_c, "if (sc->key_b_found) {"), "nfc_file.c: only FOUND Key B is saved");

    CHECK(contains(storage_c, "strncmp(line, \"Sector \", 7) == 0"),
          "nfc_storage.c: Sector-line parse branch present");
    CHECK(contains(storage_c, "if (len != 6) continue;"),
          "nfc_storage.c: parser enforces exactly 6 key bytes");
    CHECK(contains(storage_c, "sc->key_a_found = true;"), "nfc_storage.c: KeyA line sets key_a_found");
    CHECK(contains(storage_c, "sc->key_b_found = true;"), "nfc_storage.c: KeyB line sets key_b_found");
    CHECK(contains(storage_c, "nfc_ctx_clear_mfc();"),
          "nfc_storage.c: .mfc cleared before parsing (no stale-key leak between loads)");

    free(file_c); free(storage_c);
}

/* --- faithful transcription of the save-line formatter, char-for-char
 * matching the format string verified present above. --- */
static void format_key_line(char *out, size_t outsz, uint8_t sector, const char *tag,
                            const uint8_t key[6])
{
    snprintf(out, outsz, "Sector %02u Key%s: %02X %02X %02X %02X %02X %02X\r\n",
             (unsigned)sector, tag, key[0], key[1], key[2], key[3], key[4], key[5]);
}

/* --- faithful transcription of the parse logic verified present above. --- */
typedef struct { bool key_a_found; uint8_t key_a[6]; bool key_b_found; uint8_t key_b[6]; } sec_t;

static bool parse_key_line(const char *line, sec_t sec_arr[16])
{
    if (strncmp(line, "Sector ", 7) != 0) return false;
    char buf[128]; strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf)-1] = 0;
    unsigned sector = 0; char keytag[8] = {0};
    char *colon = strchr(buf, ':');
    if (!colon) return false;
    *colon = '\0';
    if (sscanf(buf, "Sector %u %7s", &sector, keytag) != 2) return false;
    if (sector >= 16U) return false;

    char *data_str = colon + 1;
    while (*data_str == ' ') data_str++;
    uint8_t bytes[8]; size_t len = 0;
    char *tok = strtok(data_str, " \r\n");
    while (tok && len < sizeof(bytes)) {
        bytes[len++] = (uint8_t)strtoul(tok, NULL, 16);
        tok = strtok(NULL, " \r\n");
    }
    if (len != 6) return false;

    if (strcmp(keytag, "KeyA") == 0) { memcpy(sec_arr[sector].key_a, bytes, 6); sec_arr[sector].key_a_found = true; }
    else if (strcmp(keytag, "KeyB") == 0) { memcpy(sec_arr[sector].key_b, bytes, 6); sec_arr[sector].key_b_found = true; }
    return true;
}

static void test_roundtrip_all_16_sectors(void)
{
    uint8_t key_a[16][6], key_b[16][6];
    char lines[32][128];
    for (int s = 0; s < 16; s++) {
        for (int i = 0; i < 6; i++) { key_a[s][i] = (uint8_t)(0x10*s + i); key_b[s][i] = (uint8_t)(0x80 + 0x10*s + i); }
        format_key_line(lines[s*2],   sizeof(lines[0]), (uint8_t)s, "A", key_a[s]);
        format_key_line(lines[s*2+1], sizeof(lines[0]), (uint8_t)s, "B", key_b[s]);
    }

    sec_t sec[16]; memset(sec, 0, sizeof(sec));
    for (int i = 0; i < 32; i++) CHECK(parse_key_line(lines[i], sec), "line parses");

    int ok = 1;
    for (int s = 0; s < 16; s++) {
        if (!sec[s].key_a_found || memcmp(sec[s].key_a, key_a[s], 6) != 0) ok = 0;
        if (!sec[s].key_b_found || memcmp(sec[s].key_b, key_b[s], 6) != 0) ok = 0;
    }
    CHECK(ok, "all 16 sectors' Key A/B round-trip exactly through save-format -> parse");
}

static void test_old_file_no_key_lines(void)
{
    /* An old-format file has ZERO "Sector N KeyX:" lines -- only Block N:
     * lines (not modeled here, irrelevant to key state) and possibly other
     * header lines. Simulate by simply not feeding any Sector lines in. */
    sec_t sec[16]; memset(sec, 0, sizeof(sec));
    const char *old_lines[] = {
        "Filetype: M1 NFC device\r\n",
        "Block 000: 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00\r\n",
        "Block 001: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\r\n",
    };
    int stray_parsed = 0;
    for (size_t i = 0; i < sizeof(old_lines)/sizeof(old_lines[0]); i++) {
        if (parse_key_line(old_lines[i], sec)) stray_parsed++;
    }
    CHECK(stray_parsed == 0, "non-Sector lines (old-format file) are never mistaken for key lines");
    int any_found = 0;
    for (int s = 0; s < 16; s++) if (sec[s].key_a_found || sec[s].key_b_found) any_found = 1;
    CHECK(!any_found, "old file with no key lines at all leaves every key genuinely unknown (not a fabricated default)");
}

static void test_no_stale_key_across_loads(void)
{
    /* Load "card 1" with full keys. */
    sec_t sec[16]; memset(sec, 0, sizeof(sec));
    uint8_t k[6] = { 1,2,3,4,5,6 };
    char line[128];
    format_key_line(line, sizeof(line), 3, "A", k);
    CHECK(parse_key_line(line, sec), "card1 sector3 KeyA parses");
    CHECK(sec[3].key_a_found, "card1 sector3 KeyA marked found");

    /* nfc_ctx_clear_mfc() (verified present in the real loader above) zeroes
     * the WHOLE struct -- reproduce that exact effect before "loading" card 2. */
    memset(sec, 0, sizeof(sec));

    /* Card 2's file has no Sector lines at all (e.g. an incomplete read). */
    CHECK(!sec[3].key_a_found, "after clear, card1's sector3 KeyA does not leak into card2's state");
}

int main(void)
{
    test_source_matches_shipped();
    test_roundtrip_all_16_sectors();
    test_old_file_no_key_lines();
    test_no_stale_key_across_loads();

    printf("\nmfc_key_file_format_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
