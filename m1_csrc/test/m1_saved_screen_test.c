/*
 * m1_saved_screen_test.c - deterministic host test for the universal
 * "Saved" confirmation bitmap (m1_saved_screen_128x64, m1_csrc/m1_display_data.c).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Links directly against the REAL m1_display_data.c (portable, HAL-free --
 * it only #includes <stdint.h>), so this exercises the actual array that
 * ships in firmware, not a copy. Mirrors m1_boot_logo_test.c's own
 * established pattern for a full-screen 128x64 1-bit asset.
 *
 * Verifies:
 *   - exact 1024-byte payload (128x64 @ 1bpp)
 *   - correct bit order (LSB-first per byte, matching u8g2_DrawXBMP's own
 *     mask-shift-left-from-1 convention, Drivers/u8g2_csrc/u8g2_bitmap.c)
 *   - correct foreground/background polarity (bit=1 -> black/ink)
 *   - exact expected content hash (FNV-1a 64-bit over the raw bytes)
 *   - the obsolete nfc_saved_63_63 symbol is gone (a link-time guarantee,
 *     not testable here directly, but this file's own successful link
 *     against m1_display_data.c with no such symbol referenced is the
 *     host-side half of that proof)
 *   - decode-back pixel spot-checks: border ink present, interior blank,
 *     checkmark/text region has ink (not an all-blank or all-solid array)
 *
 * Build (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_display_data.c m1_csrc/test/m1_saved_screen_test.c \
 *      -o /tmp/m1_saved_screen_test && /tmp/m1_saved_screen_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

extern const uint8_t m1_saved_screen_128x64[1024];

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

#define SAVED_W 128
#define SAVED_H 64
#define SAVED_BYTES ((SAVED_W / 8) * SAVED_H)
#define SAVED_EXPECTED_FNV1A64 0xa39548ce6f1c4153ULL

static int get_pixel(const uint8_t *bmp, int x, int y)
{
    /* Row-major, LSB-first per byte -- matches u8g2_DrawXBMP exactly
     * (Drivers/u8g2_csrc/u8g2_bitmap.c: mask starts at 1, shifts left). */
    int byte_idx = y * (SAVED_W / 8) + x / 8;
    int bit = x % 8;
    return (bmp[byte_idx] >> bit) & 1;
}

static void test_array_size(void)
{
    CHECK(sizeof(m1_saved_screen_128x64) == SAVED_BYTES,
          "m1_saved_screen_128x64 is exactly 1024 bytes (128x64 @ 1bpp)");
}

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void test_content_hash_matches_expected(void)
{
    uint64_t h = fnv1a64(m1_saved_screen_128x64, SAVED_BYTES);
    char msg[96];
    snprintf(msg, sizeof(msg), "content FNV-1a64 matches expected 0x%016llx (got 0x%016llx)",
             (unsigned long long)SAVED_EXPECTED_FNV1A64, (unsigned long long)h);
    CHECK(h == SAVED_EXPECTED_FNV1A64, msg);
}

static void test_black_pixel_count_is_reasonable(void)
{
    int black = 0;
    for (size_t i = 0; i < SAVED_BYTES; i++) {
        uint8_t b = m1_saved_screen_128x64[i];
        for (int bit = 0; bit < 8; bit++)
            if ((b >> bit) & 1) black++;
    }
    /* A bordered checkmark+text composition on a 128x64 canvas is neither
     * nearly-blank (a corrupted/all-zero array) nor nearly-solid (a
     * corrupted/all-one array); this is a sanity bound, not an exact
     * count, since minor future re-conversions of the same source art are
     * expected to shift the pixel count slightly. */
    CHECK(black > 200 && black < 4000,
          "black-pixel count is in the plausible range for a bordered checkmark+text composition");
}

static void test_border_present(void)
{
    /* The approved composition has a solid border on all four edges. */
    CHECK(get_pixel(m1_saved_screen_128x64, 0, 0) == 1, "top-left corner (0,0) is border ink");
    CHECK(get_pixel(m1_saved_screen_128x64, 127, 0) == 1, "top-right corner (127,0) is border ink");
    CHECK(get_pixel(m1_saved_screen_128x64, 0, 63) == 1, "bottom-left corner (0,63) is border ink");
    CHECK(get_pixel(m1_saved_screen_128x64, 127, 63) == 1, "bottom-right corner (127,63) is border ink");
}

static void test_interior_has_both_ink_and_background(void)
{
    int any_ink = 0, any_bg = 0;
    for (int y = 10; y < 54 && !(any_ink && any_bg); y++) {
        for (int x = 10; x < 118; x++) {
            if (get_pixel(m1_saved_screen_128x64, x, y)) any_ink = 1;
            else any_bg = 1;
        }
    }
    CHECK(any_ink, "interior region (checkmark/SAVED text) contains ink -- not a blank/corrupted array");
    CHECK(any_bg, "interior region contains background -- not a fully solid/corrupted array");
}

int main(void)
{
    test_array_size();
    test_content_hash_matches_expected();
    test_black_pixel_count_is_reasonable();
    test_border_present();
    test_interior_has_both_ink_and_background();

    printf("m1_saved_screen_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
