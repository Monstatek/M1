/* Host test for mfc_block_render.c's mfc_block_format_rows() -- the real
 * production formatter behind the MFC Data screen's two-byte-group hex
 * layout ("0102 0304 0506 0708" / "090A 0B0C 0D0E 0F10").
 *
 * Links and drives the REAL production mfc_block_render.c (compiled
 * directly into this binary below), not a transcription -- it has no
 * HAL/RTOS/nfc_ctx dependency of its own (pure array-in/array-out logic), so
 * it was extracted out of m1_nfc.c's mfc_block_rows() (which cannot be
 * host-compiled: FreeRTOS/u8g2/HAL throughout) into this small, dedicated,
 * host-linkable module specifically so it could be tested this way, per the
 * explicit requirement that Data-formatting tests link/call the real
 * production formatter rather than copy its implementation into the test.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../mfc_block_render.c mfc_block_render_test.c -o /tmp/mbr && /tmp/mbr
 */
#include "../mfc_block_render.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* 1) The exact example from the spec: a 16-byte block 01..10 formats as
 * "0102 0304 0506 0708" / "090A 0B0C 0D0E 0F10". */
static void test_spec_example_block(void)
{
    const uint8_t block[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    };
    char rows[2][24];

    mfc_block_format_rows(block, rows);

    CHECK(strcmp(rows[0], "0102 0304 0506 0708") == 0, "row 0 matches the spec example exactly");
    CHECK(strcmp(rows[1], "090A 0B0C 0D0E 0F10") == 0, "row 1 matches the spec example exactly");
}

/* 2) An unread block (NULL) renders both rows with the same four-group
 * geometry, using "????" per group -- not a single collapsed placeholder. */
static void test_unread_block(void)
{
    char rows[2][24];

    mfc_block_format_rows(NULL, rows);

    CHECK(strcmp(rows[0], "???? ???? ???? ????") == 0, "unread row 0 is four ???? groups");
    CHECK(strcmp(rows[1], "???? ???? ???? ????") == 0, "unread row 1 is four ???? groups");
}

/* 3) Byte order is preserved exactly -- no reversal/swap within a group or
 * across groups, including a sector-trailer-shaped block (masked Key A). */
static void test_byte_order_preserved(void)
{
    const uint8_t trailer[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* Key A, masked to 00 */
        0xFF, 0x07, 0x80, 0x69,                /* access bits + GPB */
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   /* Key B */
    };
    char rows[2][24];

    mfc_block_format_rows(trailer, rows);

    CHECK(strcmp(rows[0], "0000 0000 0000 FF07") == 0, "trailer row 0 keeps raw byte order, no reinterpretation");
    CHECK(strcmp(rows[1], "8069 FFFF FFFF FFFF") == 0, "trailer row 1 keeps raw byte order, no reinterpretation");
}

/* 4) Every hex digit is uppercase, even for low-nibble values that would
 * otherwise render as lowercase under some libc/format combinations. */
static void test_uppercase_hex(void)
{
    const uint8_t block[16] = {
        0xab, 0xcd, 0xef, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    char rows[2][24];

    mfc_block_format_rows(block, rows);

    CHECK(strcmp(rows[0], "ABCD EF00 0000 0000") == 0, "lowercase input bytes render as uppercase hex");
}

int main(void)
{
    test_spec_example_block();
    test_unread_block();
    test_byte_order_preserved();
    test_uppercase_hex();

    printf("mfc_block_render_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
