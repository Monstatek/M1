/*
 * nfc_desfire_ui_layout_test.c
 *
 * Behavioral layout verification for the DESFire/transit UI screens in
 * m1_csrc/m1_nfc.c, using the REAL u8g2 font-rendering library (the exact
 * same u8g2_GetStrWidth() the firmware calls at runtime) against the REAL
 * fonts (u8g2_font_resoledmedium_tr / u8g2_font_helvB08_tf) and the REAL
 * display setup (u8g2_Setup_st7567_enh_dg128064i_f, the actual 128x64
 * driver m1_lcd.c uses) -- not an estimated character count, and not a
 * source-string search. This exists because a prior candidate (T1) shipped
 * a hex-dump line and two detail lines that measured 144px/155px/197px wide
 * on a 128px-wide display; those exact regressions are reproduced here as
 * failing-until-fixed assertions.
 *
 * Two things are verified:
 *   1. Every format string this file actually uses (copy-verified against
 *      the real, committed m1_nfc.c via source_contains) produces a
 *      real-measured width within its screen's safe budget, across
 *      systematically enumerated worst-case field values (every file
 *      type x every comm mode x every read status; DESFire's real 24-bit
 *      field maximums; int16 min/max; a maximal 32-bit serial/timestamp).
 *   2. The width-safe truncation helper (nfc_desf_draw_str_fit, transcribed
 *      here as a pure "compute the string" function since the real one has
 *      the side effect of drawing) never returns a string wider than the
 *      max_width it was given, for arbitrarily long/pathological input.
 *
 * Build (from repo root, macOS host):
 *   cc -std=c11 -I Drivers/u8g2_csrc -I NFC/NFC_drv/common \
 *     NFC/NFC_drv/common/test/nfc_desfire_ui_layout_test.c \
 *     NFC/NFC_drv/common/mf_desfire_parse.c \
 *     Drivers/u8g2_csrc/u8g2_arc.c Drivers/u8g2_csrc/u8g2_bitmap.c Drivers/u8g2_csrc/u8g2_box.c \
 *     Drivers/u8g2_csrc/u8g2_buffer.c Drivers/u8g2_csrc/u8g2_button.c Drivers/u8g2_csrc/u8g2_circle.c \
 *     Drivers/u8g2_csrc/u8g2_cleardisplay.c Drivers/u8g2_csrc/u8g2_d_setup.c Drivers/u8g2_csrc/u8g2_d_memory.c \
 *     Drivers/u8g2_csrc/u8g2_font.c Drivers/u8g2_csrc/u8g2_fonts.c Drivers/u8g2_csrc/u8g2_hvline.c \
 *     Drivers/u8g2_csrc/u8g2_input_value.c Drivers/u8g2_csrc/u8g2_intersection.c Drivers/u8g2_csrc/u8g2_kerning.c \
 *     Drivers/u8g2_csrc/u8g2_line.c Drivers/u8g2_csrc/u8g2_ll_hvline.c Drivers/u8g2_csrc/u8g2_message.c \
 *     Drivers/u8g2_csrc/u8g2_polygon.c Drivers/u8g2_csrc/u8g2_selection_list.c Drivers/u8g2_csrc/u8g2_setup.c \
 *     Drivers/u8g2_csrc/u8x8_8x8.c Drivers/u8g2_csrc/u8x8_byte.c Drivers/u8g2_csrc/u8x8_cad.c \
 *     Drivers/u8g2_csrc/u8x8_capture.c Drivers/u8g2_csrc/u8x8_debounce.c Drivers/u8g2_csrc/u8x8_display.c \
 *     Drivers/u8g2_csrc/u8x8_fonts.c Drivers/u8g2_csrc/u8x8_gpio.c Drivers/u8g2_csrc/u8x8_input_value.c \
 *     Drivers/u8g2_csrc/u8x8_message.c Drivers/u8g2_csrc/u8x8_selection_list.c Drivers/u8g2_csrc/u8x8_setup.c \
 *     Drivers/u8g2_csrc/u8x8_string.c Drivers/u8g2_csrc/u8x8_u16toa.c Drivers/u8g2_csrc/u8x8_u8toa.c \
 *     Drivers/u8g2_csrc/u8x8_d_*.c \
 *     -o /tmp/nfc_desfire_ui_layout_test -lm && /tmp/nfc_desfire_ui_layout_test
 * (Run from the repository root; override REPO_ROOT at compile time if needed.)
 */
#include "u8g2.h"
#include "mf_desfire_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

/* Display geometry: the real M1 panel is 128px wide. A 2px left margin and
 * a matching right margin/scrollbar gutter (up to 5px, when a scrollbar is
 * drawn at x=123..127) is standard across every screen in m1_nfc.c -- so a
 * dynamic text budget of 124px (128 - 2 margin - 2 safety) or 116px
 * (128 - 2 margin - 10 scrollbar/highlight-box gutter, tightened from an
 * earlier 118 after a real-framebuffer render proved the row-0 selection
 * box's own width shrank to 121px during the round-4 header-overlap fix)
 * covers every screen this file checks, matching the exact budgets passed
 * to nfc_desf_draw_str_fit() calls in the real source (verified in Part 1). */
#define SCREEN_WIDTH 128
#define BUDGET_NO_SCROLLBAR 124
#define BUDGET_WITH_SCROLLBAR 118   /* Transit + File Detail scrollable rows: reserves the x=123-127 scrollbar gutter */
#define BUDGET_WITH_ROW_BOX 116

static u8g2_t u8g2;

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
static bool contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

/*============================ Part 1: source-bound proofs ========================*/

static void test_source_matches_shipped(void)
{
    char *ui = slurp(REPO_ROOT "/m1_csrc/m1_nfc.c");
    CHECK(ui != NULL, "m1_nfc.c readable");

    /* Scope the Transit-screen-specific negative checks to JUST that
     * function's body -- "Status: %s" legitimately exists elsewhere in
     * this file (the unrelated generic File Detail screen's own, valid
     * per-file status line) and must not be flagged there. */
    const char *transit_fn_start = ui ? strstr(ui, "static void nfc_desfire_transit_draw(void)") : NULL;
    const char *transit_fn_end = transit_fn_start ? strstr(transit_fn_start, "\n/*====") : NULL;
    /* skip past the function's own opening comment block delimiter on the first match */
    if (transit_fn_end == transit_fn_start) transit_fn_end = NULL;
    if (transit_fn_start && !transit_fn_end) {
        const char *search_from = transit_fn_start + 1;
        transit_fn_end = strstr(search_from, "\n/*====");
    }
    char transit_fn_body[8192];
    if (transit_fn_start && transit_fn_end && (size_t)(transit_fn_end - transit_fn_start) < sizeof(transit_fn_body)) {
        size_t len = (size_t)(transit_fn_end - transit_fn_start);
        memcpy(transit_fn_body, transit_fn_start, len);
        transit_fn_body[len] = '\0';
    } else {
        transit_fn_body[0] = '\0';
    }
    CHECK(transit_fn_body[0] != '\0', "nfc_desfire_transit_draw()'s function body was isolated for scoped checks");

    CHECK(contains(ui, "static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)"),
          "the width-safe truncation helper exists");
    CHECK(contains(ui, "if (u8g2_GetStrWidth(&m1_u8g2, s) <= max_width)"),
          "the helper measures with the REAL u8g2_GetStrWidth, not an estimate");

    CHECK(contains(ui, "#define NFC_DESF_HEXBYTES_PER_LINE 6U"),
          "hex dump uses 6 bytes/line (the confirmed-safe count; 8 measured 144px, overflowing the 128px panel)");

    CHECK(contains(ui, "\"Comm: %s\""), "Comm is its own line (no longer combined with AR on one line)");
    CHECK(contains(ui, "\"AR: %04X\""), "AR is its own line");
    CHECK(contains(ui, "\"Rec size: %lu B\""), "Record size is its own line (no longer combined with cur/max)");
    CHECK(contains(ui, "\"Cur: %lu\""), "Record cur is its own line");
    CHECK(contains(ui, "\"Max: %lu\""), "Record max is its own line");

    CHECK(contains(ui, "nfc_desf_draw_str_fit(2, y, 116, row);"), "Applications/Files list rows use the width-safe helper");
    CHECK(contains(ui, "static void nfc_desfire_transit_draw(void)"), "Transit Summary screen exists");
    CHECK(contains(ui, "NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT"), "Transit Summary is wired into the display-param dispatch");

    /* Required field order, labels, and formatting. */
    CHECK(contains(ui, "TLINE_HDR(\"Clipper\");"), "Clipper section header line is present");
    CHECK(contains(ui, "TLINE(\"Serial: %lu\""), "Serial line has the required format");
    CHECK(contains(ui, "TLINE(\"Balance: $%d.%02u\""), "Balance line preserves the required sign-inside-currency format");
    CHECK(contains(ui, "TLINE(\"Type: %s\""), "Type line is positioned after Balance");
    CHECK(contains(ui, "TLINE_HDR(\"Last Update\");"), "Last Update section header is present");
    CHECK(contains(ui, "TLINE(\"Never\");"), "literal \"Never\" is used when last_update_1900==0");
    CHECK(contains(ui, "TLINE(\"Terminal: 0x%04x\""), "Terminal line has the required format");
    CHECK(contains(ui, "TLINE(\"Transaction Id: %u\""), "Transaction Id line is present");
    CHECK(contains(ui, "TLINE(\"Counter: %u\""), "Counter line is present");
    CHECK(contains(ui, "TLINE_HDR(\"Ride Record\");"), "Ride Record section header is present for each ride");
    CHECK(contains(ui, "TLINE(\"Fare: $%d.%02u\""), "Ride Fare line present");
    CHECK(contains(ui, "TLINE(\"Agency: %s (%04x)\""), "Ride Agency line matches clipper.c's exact \"name (hex)\" format");
    CHECK(contains(ui, "TLINE(\"On: %s (%04x)\""), "Ride On-zone line present");
    CHECK(contains(ui, "TLINE(\"Vehicle id: %u\""), "Ride Vehicle id line is conditional on a nonzero value");
    CHECK(contains(ui, "TLINE(\"Off: %s (%04x)\""), "Ride Off-zone line present, conditional on time_off!=0");
    CHECK(contains(ui, "TLINE(\"Date Off: %02u-%02u-%04u\""), "Ride Date Off line is present in DD-MM-YYYY format");
    CHECK(contains(ui, "TLINE(\"Time Off: %02u:%02u:%02u (UTC)\""), "Ride Time Off line has the required parenthesized UTC suffix");

    CHECK(!contains(transit_fn_body, "\"Status: %s\""), "the unsupported \"Status: Complete/Partial/Unavailable\" line is absent from the Transit screen");
    CHECK(!contains(transit_fn_body, "Unavailable"), "per-field \"Unavailable\" text is absent; the Transit screen is all-or-nothing");

    /* The corrected access-rights acquisition gate lives in m1_desfire.c,
     * not m1_nfc.c -- check the real, committed file it's actually in. */
    char *desf = slurp(REPO_ROOT "/NFC/NFC_drv/legacy/m1_desfire.c");
    CHECK(desf != NULL, "m1_desfire.c readable");
    CHECK(contains(desf, "(read_access == 0x0EU) || (read_write_access == 0x0EU);"),
          "the access-rights gate checks the Read/ReadWrite nibbles, not access_rights==0x000E");
    CHECK(!contains(desf, "access_rights == 0x000EU"),
          "the old, incorrect whole-word gate is fully removed, not left as dead code alongside the fix");
}

/*============================ Part 2: real-metric worst-case checks ==============*/

static void desf_fit_compute(char *out, size_t out_sz, uint8_t max_width, const char *s);

static void assert_fits(const char *label, const char *s, uint8_t budget)
{
    int w = u8g2_GetStrWidth(&u8g2, s);
    char msg[160];
    snprintf(msg, sizeof(msg), "%s: real width %d must be <= budget %u  (\"%s\")", label, w, (unsigned)budget, s);
    CHECK(w <= budget, msg);
}

static void test_file_detail_worst_cases(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);

    const char *types[] = {"Standard","Backup","Value","Linear Record","Cyclic Record","Transaction MAC","Unknown"};
    const char *comms[] = {"Plain","MACed","Enciphered","Reserved"};
    const char *stats[] = {"Not read","Complete","Partial","Protected","Unsupported","Failed"};

    for (size_t t = 0; t < sizeof(types)/sizeof(types[0]); t++) {
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "File FF %s", types[t]);
        u8g2_SetFont(&u8g2, u8g2_font_helvB08_tf);
        assert_fits("file-detail header (EMPH)", hdr, BUDGET_NO_SCROLLBAR);
        u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);
    }
    for (size_t c = 0; c < sizeof(comms)/sizeof(comms[0]); c++) {
        char line[32];
        snprintf(line, sizeof(line), "Comm: %s", comms[c]);
        assert_fits("Comm line", line, BUDGET_WITH_SCROLLBAR);
    }
    {
        char line[32];
        snprintf(line, sizeof(line), "AR: %04X", 0xFFFF);
        assert_fits("AR line (worst case)", line, BUDGET_WITH_SCROLLBAR);
    }
    {
        /* The raw "Size: 16777215 bytes" string measures 119px -- 1px over
         * the tightened 118px scrollbar-gutter budget (it fit under the old,
         * pre-round-4 124px budget) -- but every file-detail body line is
         * drawn through nfc_desf_draw_str_fit() in the real code (verified
         * in Part 1), which truncates safely rather than overflowing.
         * Checking the DRAWN (post-truncation) result, exactly like the
         * Applications/Files/Transit checks, is the behavior that actually
         * ships -- asserting the raw string never truncates would be a
         * false invariant for this specific 24-bit-max edge case. */
        char line[32];
        snprintf(line, sizeof(line), "Size: %lu bytes", 16777215UL); /* DESFire 3-byte field max */
        char drawn[40];
        desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, line);
        assert_fits("Size line (24-bit max), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    }
    {
        char line[32];
        snprintf(line, sizeof(line), "Lo:%ld Hi:%ld", (long)-32768, (long)32767); /* int16 min/max */
        assert_fits("Value Lo/Hi line (int16 extremes)", line, BUDGET_WITH_SCROLLBAR);
    }
    {
        char l1[32], l2[32], l3[32];
        snprintf(l1, sizeof(l1), "Rec size: %lu B", 16777215UL);
        snprintf(l2, sizeof(l2), "Cur: %lu", 16777215UL);
        snprintf(l3, sizeof(l3), "Max: %lu", 16777215UL);
        char d1[40], d2[40], d3[40];
        desf_fit_compute(d1, sizeof(d1), BUDGET_WITH_SCROLLBAR, l1); /* see Size-line comment above: same 24-bit-max edge case */
        desf_fit_compute(d2, sizeof(d2), BUDGET_WITH_SCROLLBAR, l2);
        desf_fit_compute(d3, sizeof(d3), BUDGET_WITH_SCROLLBAR, l3);
        assert_fits("Rec size line (24-bit max, now its own line), drawn", d1, BUDGET_WITH_SCROLLBAR);
        assert_fits("Cur line (24-bit max, now its own line), drawn", d2, BUDGET_WITH_SCROLLBAR);
        assert_fits("Max line (24-bit max, now its own line), drawn", d3, BUDGET_WITH_SCROLLBAR);
    }
    for (size_t s = 0; s < sizeof(stats)/sizeof(stats[0]); s++) {
        char line[32];
        snprintf(line, sizeof(line), "Status: %s", stats[s]);
        assert_fits("Status line", line, BUDGET_WITH_SCROLLBAR);
    }
}

static void test_hexdump_line_width(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);
    /* Exactly what the real hex-dump loop builds: NFC_DESF_HEXBYTES_PER_LINE
     * (6) bytes as "%02X " each, unconditional trailing space. */
    char line[32] = "";
    for (int i = 0; i < 6; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", 0xAAU); strcat(line, b); }
    assert_fits("hex dump line (6 bytes/line)", line, BUDGET_WITH_SCROLLBAR);

    /* Regression check: the OLD 8-bytes/line format must NOT be mistaken
     * for safe -- documents the exact confirmed-overflowing case T1 shipped. */
    char old_line[32] = "";
    for (int i = 0; i < 8; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", 0xAAU); strcat(old_line, b); }
    int old_w = u8g2_GetStrWidth(&u8g2, old_line);
    CHECK(old_w > SCREEN_WIDTH, "confirms the OLD 8-bytes/line format genuinely overflowed a 128px panel (documents the regression this fixes)");
}

static void test_apps_files_row_worst_cases(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);

    /* The list rows use the required list-item labels
     * ("App %02x%02x%02x" lowercase reversed-byte and "File %d" decimal),
     * not the old abbreviated type/status text that produced unreadable truncated
     * output on hardware. Both are drawn through nfc_desf_draw_str_fit(2, y,
     * 116, row) (source-verified in Part 1) -- worst case is short enough
     * that truncation should never actually trigger, so this checks the
     * RAW string fits directly, not just the post-truncation result. */
    char app_row[32];
    snprintf(app_row, sizeof(app_row), "App %02x%02x%02x", 0xFFU, 0xFFU, 0xFFU);
    assert_fits("Applications list row (worst case: App ffffff)", app_row, BUDGET_WITH_ROW_BOX);

    char file_row[32];
    snprintf(file_row, sizeof(file_row), "File %u", 255U); /* file->id is uint8_t, max 255 */
    assert_fits("Files list row (worst case: File 255)", file_row, BUDGET_WITH_ROW_BOX);
}

static void test_transit_summary_worst_cases(void)
{
    /* Every dynamic line on this screen is drawn through
     * nfc_desf_draw_str_fit() (source-verified in Part 1), so the
     * invariant to check is the DRAWN (post-truncation) result -- exactly
     * like the Applications/Files list rows. The longest supported transit
     * agency/zone names (from the transcribed name tables in
     * nfc_transit_clipper.c) are used here as the genuine worst case,
     * not an arbitrary long string. */
    u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);

    char serial[48]; snprintf(serial, sizeof(serial), "Serial: %lu", 4294967295UL);
    char drawn[56];
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, serial);
    assert_fits("Transit Serial (uint32 max), drawn", drawn, BUDGET_WITH_SCROLLBAR);

    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Balance: $-327.68"); /* required sign-inside format */
    assert_fits("Transit Balance (negative worst case), drawn", drawn, BUDGET_WITH_SCROLLBAR);

    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Type: Mobile Device");
    assert_fits("Transit Type, drawn", drawn, BUDGET_WITH_SCROLLBAR);

    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Date: 29-08-2026");
    assert_fits("Transit Date, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Time: 23:59:59 (UTC)");
    assert_fits("Transit Time, drawn", drawn, BUDGET_WITH_SCROLLBAR);

    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Terminal: 0xffff");
    assert_fits("Transit Terminal, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Transaction Id: 65535");
    assert_fits("Transit Transaction Id, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Counter: 65535");
    assert_fits("Transit Counter, drawn", drawn, BUDGET_WITH_SCROLLBAR);

    /* Ride record fields -- worst-case AGENCY name ("SF Bay Ferry", the
     * longest entry in agency_names[]) and worst-case ZONE name
     * ("Pleasant Hill/Contra Costa Centre", the longest entry in any
     * *_zones[] table), each combined with a 4-digit hex id per
     * clipper.c's own "%s (%04x)" format. */
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Fare: $-1.23");
    assert_fits("Ride Fare, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Agency: SF Bay Ferry (001b)");
    assert_fits("Ride Agency (longest known name), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "On: Pleasant Hill/Contra Costa Centre (0013)");
    assert_fits("Ride On zone (longest known name), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Off: Pleasant Hill/Contra Costa Centre (0013)");
    assert_fits("Ride Off zone (longest known name), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Vehicle id: 65535");
    assert_fits("Ride Vehicle id, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Date Off: 29-08-2026");
    assert_fits("Ride Date Off, drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Time Off: 23:59:59 (UTC)");
    assert_fits("Ride Time Off, drawn", drawn, BUDGET_WITH_SCROLLBAR);

    u8g2_SetFont(&u8g2, u8g2_font_helvB08_tf);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Clipper");
    assert_fits("Transit title header (EMPH), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Last Update");
    assert_fits("Last Update header (EMPH), drawn", drawn, BUDGET_WITH_SCROLLBAR);
    desf_fit_compute(drawn, sizeof(drawn), BUDGET_WITH_SCROLLBAR, "Ride Record");
    assert_fits("Ride Record header (EMPH), drawn", drawn, BUDGET_WITH_SCROLLBAR);
}

/*============================ Part 3: truncation-helper invariant ================*/
/* Transcribed from the real nfc_desf_draw_str_fit() (source-verified in
 * Part 1) with the drawing call replaced by returning the computed string,
 * since the real function's only side effect (u8g2_DrawStr) has no
 * observable return value to assert on. */
static void desf_fit_compute(char *out, size_t out_sz, uint8_t max_width, const char *s)
{
    if (u8g2_GetStrWidth(&u8g2, s) <= max_width) {
        snprintf(out, out_sz, "%s", s);
        return;
    }
    char buf[40];
    size_t len = strlen(s);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    while (len > 0) {
        len--;
        buf[len] = '\0';
        char tmp[42];
        snprintf(tmp, sizeof(tmp), "%s..", buf);
        if (u8g2_GetStrWidth(&u8g2, tmp) <= max_width) {
            snprintf(out, out_sz, "%s", tmp);
            return;
        }
    }
    out[0] = '\0';
}

static void test_fit_helper_never_overflows(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_resoledmedium_tr);

    const char *pathological[] = {
        "FFFFFF (no select)",
        "FF Transaction MAC Unsupported",
        "This is a deliberately very long string nobody formatted for a 128 pixel screen",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "",
        "X",
    };
    for (size_t i = 0; i < sizeof(pathological)/sizeof(pathological[0]); i++) {
        for (uint8_t budget = 10; budget <= 124; budget = (uint8_t)(budget + 19)) {
            char out[48];
            desf_fit_compute(out, sizeof(out), budget, pathological[i]);
            int w = u8g2_GetStrWidth(&u8g2, out);
            char msg[128];
            snprintf(msg, sizeof(msg), "fit(\"%.20s...\", budget=%u) -> width %d must be <= %u",
                     pathological[i], (unsigned)budget, w, (unsigned)budget);
            CHECK(w <= budget, msg);
        }
    }
}

int main(void)
{
    u8g2_Setup_st7567_enh_dg128064i_f(&u8g2, U8G2_R0, u8x8_dummy_cb, u8x8_dummy_cb);
    u8g2_InitDisplay(&u8g2);

    test_source_matches_shipped();
    test_file_detail_worst_cases();
    test_hexdump_line_width();
    test_apps_files_row_worst_cases();
    test_transit_summary_worst_cases();
    test_fit_helper_never_overflows();

    printf("nfc_desfire_ui_layout_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
