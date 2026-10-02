/*
 * mfc_access_test.c - host unit tests for the MIFARE Classic access decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/mfc_access.c \
 *      NFC/NFC_drv/common/test/mfc_access_test.c -o /tmp/mfc_access_test && /tmp/mfc_access_test
 */
#include "mfc_access.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* Build a 16-byte trailer with the three access bytes set. */
static void trailer_with(uint8_t b6, uint8_t b7, uint8_t b8, uint8_t out[16])
{
    memset(out, 0, 16);
    out[6] = b6; out[7] = b7; out[8] = b8;
}

int main(void)
{
    uint8_t t[16];
    mfc_access_t a;

    /* [1] Transport/blank default FF 07 80: data blocks 000, trailer 001. */
    trailer_with(0xFF, 0x07, 0x80, t);
    CHECK(mfc_access_decode(t, &a), "FF0780 decodes");
    for (uint8_t b = 0; b < 3; b++) {
        CHECK(mfc_access_can_write_data(&a, b, 4, MFC_ACC_KEY_A), "FF0780 data write A");
        CHECK(mfc_access_can_write_data(&a, b, 4, MFC_ACC_KEY_B), "FF0780 data write B");
    }
    CHECK(mfc_access_can_write_trailer(&a, MFC_ACC_KEY_A),  "FF0780 trailer write A (001)");
    CHECK(!mfc_access_can_write_trailer(&a, MFC_ACC_KEY_B), "FF0780 trailer NOT write B");
    CHECK(mfc_access_can_read_trailer(&a, MFC_ACC_KEY_A),   "FF0780 access read A");
    CHECK(!mfc_access_can_read_trailer(&a, MFC_ACC_KEY_B),  "FF0780 access NOT read B (001)");

    /* [2] All blocks 011 (0F 00 FF): data writable by B only, trailer by B. */
    trailer_with(0x0F, 0x00, 0xFF, t);
    CHECK(mfc_access_decode(t, &a), "0F00FF decodes");
    CHECK(!mfc_access_can_write_data(&a, 0, 4, MFC_ACC_KEY_A), "011 data NOT write A");
    CHECK(mfc_access_can_write_data(&a, 0, 4, MFC_ACC_KEY_B),  "011 data write B");
    CHECK(!mfc_access_can_write_trailer(&a, MFC_ACC_KEY_A), "011 trailer NOT write A");
    CHECK(mfc_access_can_write_trailer(&a, MFC_ACC_KEY_B),  "011 trailer write B");
    CHECK(mfc_access_can_read_trailer(&a, MFC_ACC_KEY_B),   "011 access read B");

    /* [3] All blocks 111 (00 F0 FF): nothing writable. */
    trailer_with(0x00, 0xF0, 0xFF, t);
    CHECK(mfc_access_decode(t, &a), "00F0FF decodes");
    CHECK(!mfc_access_can_write_data(&a, 0, 4, MFC_ACC_KEY_A), "111 data NOT write A");
    CHECK(!mfc_access_can_write_data(&a, 2, 4, MFC_ACC_KEY_B), "111 data NOT write B");
    CHECK(!mfc_access_can_write_trailer(&a, MFC_ACC_KEY_A), "111 trailer NOT write A");
    CHECK(!mfc_access_can_write_trailer(&a, MFC_ACC_KEY_B), "111 trailer NOT write B");

    /* [4] Mixed per-block (B9 67 84): b0=000 b1=100 b2=110 trailer=001. */
    trailer_with(0xB9, 0x67, 0x84, t);
    CHECK(mfc_access_decode(t, &a), "B96784 decodes");
    CHECK(mfc_access_can_write_data(&a, 0, 4, MFC_ACC_KEY_A),  "mixed b0 000 write A");
    CHECK(mfc_access_can_write_data(&a, 0, 4, MFC_ACC_KEY_B),  "mixed b0 000 write B");
    CHECK(!mfc_access_can_write_data(&a, 1, 4, MFC_ACC_KEY_A), "mixed b1 100 NOT write A");
    CHECK(mfc_access_can_write_data(&a, 1, 4, MFC_ACC_KEY_B),  "mixed b1 100 write B");
    CHECK(!mfc_access_can_write_data(&a, 2, 4, MFC_ACC_KEY_A), "mixed b2 110 NOT write A");
    CHECK(mfc_access_can_write_data(&a, 2, 4, MFC_ACC_KEY_B),  "mixed b2 110 write B");
    CHECK(mfc_access_can_write_trailer(&a, MFC_ACC_KEY_A),  "mixed trailer 001 write A");

    /* [5] Integrity rejection: FF0780 with byte7 corrupted breaks ~C3 twin. */
    trailer_with(0xFF, 0x08, 0x80, t);
    CHECK(!mfc_access_decode(t, &a), "corrupt byte7 rejected");
    CHECK(!mfc_access_valid(t),      "corrupt byte7 invalid");

    /* [6] Integrity rejection: all-zero access bytes are inconsistent. */
    trailer_with(0x00, 0x00, 0x00, t);
    CHECK(!mfc_access_valid(t), "all-zero access bytes rejected");

    /* [7] NULL / bounds hardening. */
    CHECK(!mfc_access_decode(NULL, &a), "NULL trailer rejected");
    CHECK(!mfc_access_decode(t, NULL),  "NULL out rejected");
    trailer_with(0xFF, 0x07, 0x80, t);
    (void)mfc_access_decode(t, &a);
    CHECK(!mfc_access_can_write_data(NULL, 0, 4, MFC_ACC_KEY_A), "NULL access rejected");

    /* [8] Big (16-block) 4K sector group mapping + permissions.
     * BD 23 C4: group0=000, group1=100, group2=011, trailer=001. */
    CHECK(mfc_access_group(0, 4)  == 0, "small: block0 -> group0");
    CHECK(mfc_access_group(2, 4)  == 2, "small: block2 -> group2");
    CHECK(mfc_access_group(3, 4)  == 3, "small: block3 -> trailer group");
    CHECK(mfc_access_group(0, 16) == 0, "big: block0 -> group0");
    CHECK(mfc_access_group(4, 16) == 0, "big: block4 -> group0");
    CHECK(mfc_access_group(5, 16) == 1, "big: block5 -> group1");
    CHECK(mfc_access_group(9, 16) == 1, "big: block9 -> group1");
    CHECK(mfc_access_group(10, 16) == 2, "big: block10 -> group2");
    CHECK(mfc_access_group(14, 16) == 2, "big: block14 -> group2");
    CHECK(mfc_access_group(15, 16) == 3, "big: block15 -> trailer group");

    trailer_with(0xBD, 0x23, 0xC4, t);
    CHECK(mfc_access_decode(t, &a), "BD23C4 decodes");
    CHECK(mfc_access_can_write_data(&a, 2,  16, MFC_ACC_KEY_A),  "big grp0 000 write A");
    CHECK(mfc_access_can_write_data(&a, 4,  16, MFC_ACC_KEY_B),  "big grp0 000 write B");
    CHECK(!mfc_access_can_write_data(&a, 7,  16, MFC_ACC_KEY_A), "big grp1 100 NOT write A");
    CHECK(mfc_access_can_write_data(&a, 7,  16, MFC_ACC_KEY_B),  "big grp1 100 write B");
    CHECK(!mfc_access_can_write_data(&a, 12, 16, MFC_ACC_KEY_A), "big grp2 011 NOT write A");
    CHECK(mfc_access_can_write_data(&a, 12, 16, MFC_ACC_KEY_B),  "big grp2 011 write B");
    CHECK(mfc_access_can_write_trailer(&a, MFC_ACC_KEY_A),       "big trailer 001 write A");

    printf("\nmfc_access_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
