/*
 * ce_mfc_xport_test.c - host tests for the emulation transport framing.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/ce_mfc_xport.c \
 *      NFC/NFC_drv/common/test/ce_mfc_xport_test.c -o /tmp/ce_xport && /tmp/ce_xport
 */
#include "ce_mfc_xport.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

int main(void)
{
    /* [1] odd parity (MIFARE uses odd parity of the plaintext byte). */
    CHECK(ce_mfc_odd_parity(0x00) == 1, "odd_parity(00)=1 (0 ones -> odd bit 1)");
    CHECK(ce_mfc_odd_parity(0x01) == 0, "odd_parity(01)=0 (1 one)");
    CHECK(ce_mfc_odd_parity(0xFF) == 1, "odd_parity(FF)=1 (8 ones)");
    CHECK(ce_mfc_odd_parity(0x30) == 1, "odd_parity(30)=1 (2 ones)");
    CHECK(ce_mfc_odd_parity(0x07) == 0, "odd_parity(07)=0 (3 ones)");

    /* [2] pack/unpack round-trip for a multi-byte frame with mixed parity. */
    {
        const uint8_t data[4] = { 0x60, 0x00, 0xA1, 0xFF };
        const uint8_t par[4]  = { 1, 0, 1, 0 };
        uint8_t packed[8];
        uint16_t nb = ce_mfc_pack_bits(packed, data, par, 4);
        CHECK(nb == 36, "4 bytes -> 36 bits (4*9)");

        uint8_t d2[4], p2[4];
        uint8_t n = ce_mfc_unpack_bits(packed, nb, d2, p2, 4);
        CHECK(n == 4, "unpack 4 groups");
        CHECK(memcmp(d2, data, 4) == 0, "data round-trips");
        CHECK(memcmp(p2, par, 4) == 0, "parity round-trips");
    }

    /* [3] exact bit placement: byte 0x01 LSB-first -> bit0 set, then parity. */
    {
        const uint8_t data[1] = { 0x01 };
        const uint8_t par[1]  = { 1 };
        uint8_t packed[2];
        uint16_t nb = ce_mfc_pack_bits(packed, data, par, 1);
        CHECK(nb == 9, "1 byte -> 9 bits");
        CHECK((packed[0] & 0x01u) == 0x01u, "bit0 (LSB of 0x01) set");
        CHECK((packed[0] & 0x02u) == 0x00u, "bit1 clear");
        CHECK(((packed[0] >> 8) == 0), "no overflow");
        CHECK((packed[1] & 0x01u) == 0x01u, "parity bit (bit 8) set");
    }

    /* [4] 4-bit ACK framing (MIFARE ACK nibble 0xA). */
    {
        uint8_t out[1] = { 0xFF };
        uint16_t nb = ce_mfc_pack_ack4(out, 0x0A);
        CHECK(nb == 4, "ack4 -> 4 bits");
        CHECK(out[0] == 0x0A, "ack nibble 0xA in low nibble");
        nb = ce_mfc_pack_ack4(out, 0x14);          /* only low nibble kept */
        CHECK(out[0] == 0x04, "ack4 masks to low nibble");
    }

    /* [5] NULL hardening. */
    CHECK(ce_mfc_pack_bits(NULL, (uint8_t[]){0}, (uint8_t[]){0}, 1) == 0, "pack NULL out");
    { uint8_t o[2]; CHECK(ce_mfc_pack_bits(o, NULL, (uint8_t[]){0}, 1) == 0, "pack NULL data"); }
    { uint8_t d[1], p[1]; CHECK(ce_mfc_unpack_bits(NULL, 9, d, p, 1) == 0, "unpack NULL in"); }
    CHECK(ce_mfc_pack_ack4(NULL, 0x0A) == 0, "ack4 NULL out");

    /* [6] partial trailing bits are ignored (only complete 9-bit groups). */
    {
        uint8_t packed[2] = { 0xFF, 0x00 };
        uint8_t d[2], p[2];
        uint8_t n = ce_mfc_unpack_bits(packed, 8, d, p, 2);  /* only 8 bits -> 0 groups */
        CHECK(n == 0, "8 bits -> no complete 9-bit group");
    }

    printf("\nce_mfc_xport_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
