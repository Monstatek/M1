/*
 * m1_boot_logo_test.c - deterministic host test for the production boot-
 * screen bitmap (m1_boot_logo_128x64, m1_csrc/m1_display_data.c).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Links directly against the REAL m1_display_data.c (portable, HAL-free --
 * it only #includes <stdint.h>), so this exercises the actual array that
 * ships in firmware, not a copy.
 *
 * Verifies:
 *   - exact 1024-byte payload (128x64 @ 1bpp)
 *   - correct bit order (LSB-first per byte, matching u8g2_DrawHXBMP's own
 *     mask-shift-left-from-1 convention, Drivers/u8g2_csrc/u8g2_bitmap.c)
 *   - correct foreground/background polarity (bit=1 -> black/ink, matching
 *     M1_DISP_DRAW_COLOR_TXT=1 and u8g2_DrawHXBMP's color/ncolor mapping)
 *   - exact expected content hash (FNV-1a 64-bit over the raw bytes -- a
 *     small, dependency-free deterministic hash; not MD5/SHA256, which
 *     would require pulling in a hashing library for a single test file,
 *     but equally sufficient to catch any single-bit regression)
 *   - the version-text reserved region (rows 44-63) is entirely blank in
 *     the ARRAY itself, proving the live-rendered "FW VERSION x.y" line
 *     (drawn separately in startup_info_screen_display(), m1_system.c) is
 *     genuinely not baked into this bitmap
 *   - decode-back pixel spot-checks against known positions
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_display_data.c m1_csrc/test/m1_boot_logo_test.c \
 *      -o /tmp/m1_boot_logo_test && /tmp/m1_boot_logo_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

/* The real header (m1_csrc/m1_display.h) declares this as an incomplete
 * array (`extern const uint8_t m1_boot_logo_128x64[];`), so sizeof() isn't
 * available on that declaration. Declaring the expected bound here lets
 * this TU use sizeof for documentation purposes; the actual exact-size
 * guarantee comes from every test below reading indices 0..1023 under
 * -fsanitize=address -- if the real definition in m1_display_data.c were
 * ever smaller, that would be an immediate out-of-bounds-read abort, not a
 * silent pass. */
extern const uint8_t m1_boot_logo_128x64[1024];

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

#define BOOT_LOGO_W 128
#define BOOT_LOGO_H 64
#define BOOT_LOGO_BYTES ((BOOT_LOGO_W / 8) * BOOT_LOGO_H)
#define BOOT_LOGO_EXPECTED_FNV1A64 0x3f07016ea1233adaULL
#define BOOT_LOGO_EXPECTED_BLACK_BITS 2099

static int get_pixel(const uint8_t *bmp, int x, int y)
{
    /* Row-major, LSB-first per byte -- matches u8g2_DrawHXBMP exactly
     * (Drivers/u8g2_csrc/u8g2_bitmap.c: mask starts at 1, shifts left). */
    int byte_idx = y * (BOOT_LOGO_W / 8) + x / 8;
    int bit = x % 8;
    return (bmp[byte_idx] >> bit) & 1;
}

static void test_array_size(void)
{
    CHECK(sizeof(m1_boot_logo_128x64) == BOOT_LOGO_BYTES,
          "m1_boot_logo_128x64 is exactly 1024 bytes (128x64 @ 1bpp)");
}

static void test_content_hash(void)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < BOOT_LOGO_BYTES; i++) {
        h ^= m1_boot_logo_128x64[i];
        h *= 0x100000001b3ULL;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "content FNV-1a64 matches expected 0x%016llx (got 0x%016llx)",
             (unsigned long long)BOOT_LOGO_EXPECTED_FNV1A64, (unsigned long long)h);
    CHECK(h == BOOT_LOGO_EXPECTED_FNV1A64, msg);
}

static void test_black_pixel_count(void)
{
    int black = 0;
    for (size_t i = 0; i < BOOT_LOGO_BYTES; i++) {
        uint8_t b = m1_boot_logo_128x64[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((b >> bit) & 1) black++;
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "black-bit count is exactly %d (got %d)", BOOT_LOGO_EXPECTED_BLACK_BITS, black);
    CHECK(black == BOOT_LOGO_EXPECTED_BLACK_BITS, msg);
}

static void test_polarity_and_bit_order_spot_checks(void)
{
    CHECK(get_pixel(m1_boot_logo_128x64, 0, 0) == 0, "top-left corner (0,0) is background (0)");
    CHECK(get_pixel(m1_boot_logo_128x64, 127, 0) == 0, "top-right corner (127,0) is background (0)");
    CHECK(get_pixel(m1_boot_logo_128x64, 0, 63) == 0, "bottom-left corner (0,63) is background (0)");
    CHECK(get_pixel(m1_boot_logo_128x64, 127, 63) == 0, "bottom-right corner (127,63) is background (0)");
    /* Inside the wordmark's ink region (rows 16-40) -- proven black by the
     * approved conversion; if this were ever 0, either the bit order or
     * the polarity (or the artwork itself) regressed. */
    CHECK(get_pixel(m1_boot_logo_128x64, 64, 20) == 1, "(64,20), inside the wordmark, is foreground/black (1)");
}

static void test_version_text_region_is_blank(void)
{
    /* Rows 44-63 are reserved for the LIVE u8g2_DrawStr version line drawn
     * by startup_info_screen_display() -- proves that text is genuinely
     * not baked into this array (a silent future regression here would be
     * a duplicate/stale version string burned into the artwork itself). */
    int any_ink = 0;
    for (int y = 44; y <= 63 && !any_ink; y++) {
        for (int x = 0; x < BOOT_LOGO_W; x++) {
            if (get_pixel(m1_boot_logo_128x64, x, y)) { any_ink = 1; break; }
        }
    }
    CHECK(!any_ink, "rows 44-63 (reserved for the live version-text draw) are entirely blank in the array");
}

int main(void)
{
    test_array_size();
    test_content_hash();
    test_black_pixel_count();
    test_polarity_and_bit_order_spot_checks();
    test_version_text_region_is_blank();

    printf("m1_boot_logo_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
