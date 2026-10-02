/* Regression guard + fix proof for the hardware finding: the NFC Tools menu
 * item "MIFARE Ultralight Keys" ran off the 128x64 display.
 *
 * m1_csrc/m1_menu.c (the label's source) and m1_csrc/m1_display.c (the
 * submenu renderer, m1_gui_submenu_update()) cannot be host-linked
 * (FreeRTOS/u8g2-hardware-driver/HAL throughout their surrounding TUs), so
 * this test does NOT reimplement or approximate the renderer. Instead it
 * reproduces the EXACT drawing call m1_gui_submenu_update() makes for a
 * submenu item -- same real font constants, same real left-margin and
 * scrollbar-column constants, same u8g2_DrawStr() call shape -- against the
 * REAL, linked u8g2 library and the REAL 13-font selected catalog
 * (u8g2_fonts_selected.c), on a REAL 128x64 tile framebuffer, then scans the
 * REAL rendered pixels. This is real production text layout, not font-
 * metric math or an illustration: u8g2_GetStrWidth()/u8g2_DrawStr() read the
 * exact same glyph-width tables the firmware links.
 *
 * The label text itself, and the two layout constants the real renderer
 * uses, are extracted from the real committed source (bounded string
 * search / literal #define parsing) rather than hand-copied into this
 * file, so this test cannot silently drift from what m1_menu.c and
 * m1_display.c actually say.
 *
 * Real constants reproduced here (m1_csrc/m1_display.c):
 *   SUB_MENU_TXT_LEFT_POS_X = 4    (left margin every submenu label draws at)
 *   MENU_SCROLLBAR_POS_X    = 124  (scrollbar column start -- drawn
 *                                    UNCONDITIONALLY for every submenu
 *                                    render, regardless of item count)
 *   M1_DISP_SUB_MENU_FONT_N = u8g2_font_resoledmedium_tr  (unselected)
 *   M1_DISP_SUB_MENU_FONT_B = u8g2_font_squeezed_b7_tr    (selected/bold)
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra -I Drivers/u8g2_csrc \
 *     NFC/NFC_drv/common/test/nfc_menu_label_width_test.c \
 *     $(find Drivers/u8g2_csrc -maxdepth 1 -name 'u8g2_*.c' ! -name 'u8g2_fonts.c') \
 *     Drivers/u8g2_csrc/u8x8_*.c \
 *     -o /tmp/nfc_menu_label_width_test -lm && /tmp/nfc_menu_label_width_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u8g2.h"

#define DISP_W 128
#define DISP_H 64

static int pass_count;
static int fail_count;
#define CHECK(c, m) do { \
    if (c) { pass_count++; } \
    else { fail_count++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } \
} while (0)

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = malloc((size_t)n + 1U);
    if (s == NULL) { fclose(f); return NULL; }
    if (fread(s, 1U, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = '\0';
    fclose(f);
    return s;
}

/* Strips C block comments (replacing them with spaces, preserving offsets)
 * so a quote appearing inside an explanatory comment -- e.g. this fix's own
 * commit-message-style comment quoting the OLD overflowing label for
 * context -- can never be mistaken for the real string literal. */
static void strip_block_comments(char *s)
{
    for (char *p = s; *p != '\0'; ) {
        if (p[0] == '/' && p[1] == '*') {
            char *end = strstr(p + 2, "*/");
            char *stop = end ? end + 2 : (p + strlen(p));
            for (char *q = p; q < stop; q++) *q = ' ';
            p = stop;
        } else {
            p++;
        }
    }
}

/* Extracts the quoted label string that is the FIRST field of the
 * menu_NFC_ULC_Keys struct literal, e.g. from:
 *   S_M1_Menu_t menu_NFC_ULC_Keys =
 *   {
 *       ...
 *       "Ultralight Keys", nfc_ulc_keys, ...
 *   };
 * -- so the test proves what the real struct initializer says, not a
 * hand-copied guess. Comments between the opening brace and the field are
 * stripped first (see strip_block_comments()) so this can't be fooled by a
 * quoted string inside a comment. */
static int extract_menu_label(const char *src, char *out, size_t out_sz)
{
    const char *anchor = strstr(src, "menu_NFC_ULC_Keys");
    if (anchor == NULL) return 0;
    const char *brace = strchr(anchor, '{');
    if (brace == NULL) return 0;

    size_t remaining = strlen(brace);
    char *scratch = malloc(remaining + 1U);
    if (scratch == NULL) return 0;
    memcpy(scratch, brace, remaining + 1U);
    strip_block_comments(scratch);

    const char *quote1 = strchr(scratch, '"');
    const char *quote2 = (quote1 != NULL) ? strchr(quote1 + 1, '"') : NULL;
    int ok = 0;
    if (quote1 != NULL && quote2 != NULL) {
        size_t len = (size_t)(quote2 - (quote1 + 1));
        if (len < out_sz) {
            memcpy(out, quote1 + 1, len);
            out[len] = '\0';
            ok = 1;
        }
    }
    free(scratch);
    return ok;
}

/* Extracts the integer value of a simple "#define NAME <int>" macro. */
static int extract_define_int(const char *src, const char *name, int *out)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "#define %s", name);
    const char *p = strstr(src, needle);
    if (p == NULL) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

static int get_pixel(u8g2_t *u, int x, int y)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) return 0;
    uint8_t *buf = u8g2_GetBufferPtr(u);
    int tile_width = (int)u8g2_GetBufferTileWidth(u);
    int band = y >> 3;
    long byte_off = (long)band * tile_width * 8 + x;
    int bit = y & 7;
    return (buf[byte_off] >> bit) & 1;
}

/* Real render of one label at the real left margin, using the real font --
 * exactly the m1_gui_submenu_update() DrawStr call shape (font, x, label).
 * Returns u8g2_GetStrWidth() (the real glyph-metric width) and, via
 * out_max_ink_x, the rightmost column with any set pixel actually drawn
 * (real pixel readback, not just the metric). */
static int render_label(u8g2_t *u, const uint8_t *font, int left_x, const char *label, int *out_max_ink_x)
{
    u8g2_ClearBuffer(u);
    u8g2_FirstPage(u);
    int w = 0;
    do {
        u8g2_SetFont(u, font);
        w = u8g2_GetStrWidth(u, label);
        u8g2_DrawStr(u, left_x, 30, label);
    } while (u8g2_NextPage(u));

    int max_ink_x = -1;
    for (int y = 0; y < DISP_H; y++)
        for (int x = 0; x < DISP_W; x++)
            if (get_pixel(u, x, y)) { if (x > max_ink_x) max_ink_x = x; }
    *out_max_ink_x = max_ink_x;
    return w;
}

int main(void)
{
    char *menu_src = read_all("m1_csrc/m1_menu.c");
    CHECK(menu_src != NULL, "m1_csrc/m1_menu.c readable");
    char *disp_src = read_all("m1_csrc/m1_display.c");
    CHECK(disp_src != NULL, "m1_csrc/m1_display.c readable");
    if (menu_src == NULL || disp_src == NULL) {
        printf("nfc_menu_label_width_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }

    char label[64];
    CHECK(extract_menu_label(menu_src, label, sizeof(label)),
          "menu_NFC_ULC_Keys label extracted from the real source");
    CHECK(strcmp(label, "Ultralight Keys") == 0,
          "the real committed label is exactly \"Ultralight Keys\" (the required fix)");

    int left_x = 0, scrollbar_x = 0;
    CHECK(extract_define_int(disp_src, "SUB_MENU_TXT_LEFT_POS_X", &left_x),
          "SUB_MENU_TXT_LEFT_POS_X extracted from the real m1_display.c");
    CHECK(extract_define_int(disp_src, "MENU_SCROLLBAR_POS_X", &scrollbar_x),
          "MENU_SCROLLBAR_POS_X extracted from the real m1_display.c");

    u8g2_t u;
    u8g2_Setup_st7567_enh_dg128064i_f(&u, U8G2_R0, u8x8_dummy_cb, u8x8_dummy_cb);
    u8g2_InitDisplay(&u);

    /* The two real fonts the submenu renderer actually uses for this row:
     * unselected (resoledmedium) and selected/bold (squeezed_b7) -- the
     * bolder font is generally wider, so both are checked, not just one. */
    struct { const char *name; const uint8_t *font; } fonts[] = {
        { "M1_DISP_SUB_MENU_FONT_N (u8g2_font_resoledmedium_tr, unselected)", u8g2_font_resoledmedium_tr },
        { "M1_DISP_SUB_MENU_FONT_B (u8g2_font_squeezed_b7_tr, selected/bold)", u8g2_font_squeezed_b7_tr },
    };

    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        int max_ink_x;
        int w = render_label(&u, fonts[i].font, left_x, label, &max_ink_x);
        char msg[256];

        snprintf(msg, sizeof(msg),
                 "\"%s\" real glyph width (%d px) + left margin (%d) stays left of the scrollbar column (x=%d) in %s",
                 label, w, left_x, scrollbar_x, fonts[i].name);
        CHECK(left_x + w <= scrollbar_x, msg);

        snprintf(msg, sizeof(msg),
                 "\"%s\" real rendered pixels never reach the scrollbar column (x=%d) in %s (rightmost ink column: %d)",
                 label, scrollbar_x, fonts[i].name, max_ink_x);
        CHECK(max_ink_x < scrollbar_x, msg);

        snprintf(msg, sizeof(msg),
                 "\"%s\" real rendered pixels never run off the 128px display in %s (rightmost ink column: %d)",
                 label, fonts[i].name, max_ink_x);
        CHECK(max_ink_x < DISP_W, msg);
    }

    /* Proves the test is meaningful, not vacuously true: the ORIGINAL,
     * pre-fix label really does overflow under the same real font/layout
     * constants, in at least one of the two real states this row is
     * actually drawn in (unselected or selected/bold) -- reproducing the
     * hardware finding, not just asserting the new label happens to fit.
     * Checked against both real fonts individually (reported per-font)
     * rather than requiring both to overflow: in practice only the
     * unselected font (the row's normal, non-highlighted state -- how it
     * is drawn for every row except whichever one currently has the
     * cursor) actually overflows here, which is already sufficient to
     * reproduce "runs off the display" as reported. */
    {
        const char *old_label = "MIFARE Ultralight Keys";
        int any_overflow = 0;
        for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
            int max_ink_x;
            int w = render_label(&u, fonts[i].font, left_x, old_label, &max_ink_x);
            int overflowed = (left_x + w > scrollbar_x) || (max_ink_x >= scrollbar_x);
            printf("  (control info) \"%s\" in %s: width=%d max_ink_x=%d budget=%d -> %s\n",
                   old_label, fonts[i].name, w, max_ink_x, scrollbar_x - left_x,
                   overflowed ? "OVERFLOWS" : "fits");
            any_overflow = any_overflow || overflowed;
        }
        CHECK(any_overflow,
              "control: the ORIGINAL \"MIFARE Ultralight Keys\" label really did overflow past the scrollbar column in at least one real submenu font -- confirms this test would have caught the hardware finding");
    }

    free(menu_src);
    free(disp_src);
    printf("nfc_menu_label_width_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
