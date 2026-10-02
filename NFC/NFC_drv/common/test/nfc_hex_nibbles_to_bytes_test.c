/* Host test for nfc_hex_utils.c's nfc_hex_nibbles_to_bytes() -- the correct
 * contiguous-hex-to-bytes conversion for buffers produced by
 * m1_vkbs_get_hexkey() (no separators, exactly 2*len hex chars).
 *
 * Links and drives the REAL production nfc_hex_utils.c (compiled directly
 * into this binary below), not a transcription -- it has no HAL/RTOS
 * dependency of its own (pure string/strtol logic), so it was extracted out
 * of m1_nfc.c (which cannot be host-compiled: FreeRTOS/u8g2/HAL throughout)
 * into this small, dedicated, host-linkable module specifically so it could
 * be tested this way instead.
 *
 * This test exists because three call sites (nfc_pwd_keys_do_add(),
 * nfc_ulc_keys_do_add(), nfc_add_enter_uid(), all in m1_nfc.c) were found
 * still calling the WRONG conversion function, m1_strtob_with_base() --
 * which space-tokenizes and therefore treats an entire contiguous hex
 * string as one (overflowing, truncated) value, making Add always fail
 * with "Invalid password"/"Invalid key"/"Invalid Length" regardless of what
 * was actually typed. All three now call nfc_hex_nibbles_to_bytes(),
 * exactly like the one call site (nfc_unlock_do_enter_password()) that was
 * already correct.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../nfc_hex_utils.c nfc_hex_nibbles_to_bytes_test.c -o /tmp/hntb && /tmp/hntb
 */
#include "../nfc_hex_utils.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* 1) 4-byte password conversion (nfc_pwd_keys_do_add / nfc_unlock_do_enter_password shape). */
static void test_4byte_password(void)
{
    uint8_t out[4] = {0};
    CHECK(nfc_hex_nibbles_to_bytes("A1B2C3D4", out, 4), "8 contiguous hex chars convert to 4 bytes");
    uint8_t expect[4] = {0xA1, 0xB2, 0xC3, 0xD4};
    CHECK(memcmp(out, expect, 4) == 0, "converted bytes are exactly A1 B2 C3 D4");
}

/* 2) 16-byte ULC key conversion (nfc_ulc_keys_do_add shape: 32 hex -> 16 bytes). */
static void test_16byte_ulc_key(void)
{
    const char *hex = "0102030405060708090A0B0C0D0E0F10";
    uint8_t out[16] = {0};
    CHECK(nfc_hex_nibbles_to_bytes(hex, out, 16), "32 contiguous hex chars convert to 16 bytes");
    for (int i = 0; i < 16; i++) {
        CHECK(out[i] == (uint8_t)(i + 1), "byte i has the expected sequential value");
    }
}

/* 3) UID conversion at both real M1 UID lengths (nfc_add_enter_uid shape). */
static void test_uid_4_and_7_bytes(void)
{
    uint8_t out4[4] = {0};
    CHECK(nfc_hex_nibbles_to_bytes("01020304", out4, 4), "8 hex chars -> 4-byte UID");
    uint8_t expect4[4] = {0x01, 0x02, 0x03, 0x04};
    CHECK(memcmp(out4, expect4, 4) == 0, "4-byte UID bytes are exact -- proves the original bug (which\n"
          "        always reported \"Invalid Length\" for this exact input) is fixed");

    uint8_t out7[7] = {0};
    CHECK(nfc_hex_nibbles_to_bytes("0A0B0C0D0E0F10", out7, 7), "14 hex chars -> 7-byte UID");
    uint8_t expect7[7] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    CHECK(memcmp(out7, expect7, 7) == 0, "7-byte UID bytes are exact");
}

/* 4) Lowercase (and mixed-case) hex is accepted -- strtol's base-16 parsing
 * is case-insensitive, so nfc_hex_nibbles_to_bytes() inherits that for free. */
static void test_lowercase_accepted(void)
{
    uint8_t out[4] = {0};
    CHECK(nfc_hex_nibbles_to_bytes("a1b2c3d4", out, 4), "lowercase hex is accepted");
    uint8_t expect[4] = {0xA1, 0xB2, 0xC3, 0xD4};
    CHECK(memcmp(out, expect, 4) == 0, "lowercase converts to the identical byte values as uppercase");

    uint8_t out2[4] = {0};
    CHECK(nfc_hex_nibbles_to_bytes("A1b2C3d4", out2, 4), "mixed-case hex is accepted");
    CHECK(memcmp(out2, expect, 4) == 0, "mixed-case converts identically");
}

/* 5) A non-hex-digit character is rejected -- the exact failure mode the
 * production call sites now check via the boolean return. Reachable input:
 * the hex-only editor (m1_vkbs_get_hexkey) can never itself produce a
 * non-hex-digit character, so this exercises the defensive path only. */
static void test_non_hex_rejected(void)
{
    uint8_t out[4] = {0};
    CHECK(!nfc_hex_nibbles_to_bytes("G1B2C3D4", out, 4), "a non-hex letter ('G') is rejected");
    CHECK(!nfc_hex_nibbles_to_bytes("--------", out, 4), "an all-non-hex string is rejected");
    CHECK(!nfc_hex_nibbles_to_bytes("A1B2C3Dz", out, 4), "a trailing non-hex character is rejected");
}

int main(void)
{
    test_4byte_password();
    test_16byte_ulc_key();
    test_uid_4_and_7_bytes();
    test_lowercase_accepted();
    test_non_hex_rejected();

    printf("nfc_hex_nibbles_to_bytes_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
