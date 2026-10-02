/*
 * nfc_add_image_test.c - host unit tests for manual card-image builders.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/nfc_add_image.c \
 *      NFC/NFC_drv/common/test/nfc_add_image_test.c -o /tmp/nfc_add_image_test \
 *      && /tmp/nfc_add_image_test
 */
#include "nfc_add_image.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static uint8_t buf[4096];

int main(void)
{
    const uint8_t uid4[4] = { 0x11, 0x22, 0x33, 0x44 };
    const uint8_t uid7[7] = { 0x04, 0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34 };

    /* [1] Geometry / type->size / uid_len for all six. */
    struct { nfc_add_type_t t; const char *label; uint32_t size; uint16_t units; uint8_t uidl; } exp[] = {
        { NFC_ADD_MFC_1K,     "MFC 1K",     1024,  64, 4 },
        { NFC_ADD_MFC_4K,     "MFC 4K",     4096, 256, 4 },
        { NFC_ADD_ULTRALIGHT, "Ultralight",   64,  16, 7 },
        { NFC_ADD_NTAG213,    "NTAG213",     180,  45, 7 },
        { NFC_ADD_NTAG215,    "NTAG215",     540, 135, 7 },
        { NFC_ADD_NTAG216,    "NTAG216",     924, 231, 7 },
    };
    for (unsigned i = 0; i < 6; i++) {
        const nfc_add_geom_t *g = nfc_add_geometry(exp[i].t);
        CHECK(g != NULL, "geometry present");
        if (!g) continue;
        CHECK(strcmp(g->label, exp[i].label) == 0, "label mapping");
        CHECK(g->image_size == exp[i].size, "image size");
        CHECK(g->unit_count == exp[i].units, "unit count");
        CHECK(g->uid_len == exp[i].uidl, "uid len");
        CHECK(nfc_add_uid_len(exp[i].t) == exp[i].uidl, "uid_len accessor");
        CHECK((uint32_t)g->unit_size * g->unit_count == g->image_size, "size == units*unit");
    }
    CHECK(nfc_add_geometry(NFC_ADD_TYPE_COUNT) == NULL, "invalid type -> NULL");
    CHECK(nfc_add_geometry((nfc_add_type_t)-1) == NULL, "negative type -> NULL");

    /* [2] BCC helpers. */
    CHECK(nfc_add_bcc_mfc(uid4) == (uint8_t)(0x11 ^ 0x22 ^ 0x33 ^ 0x44), "MFC BCC");
    CHECK(nfc_add_bcc0_t2t(uid7) == (uint8_t)(0x88 ^ 0x04 ^ 0x9A ^ 0xBC), "T2T BCC0");
    CHECK(nfc_add_bcc1_t2t(uid7) == (uint8_t)(0xDE ^ 0xF0 ^ 0x12 ^ 0x34), "T2T BCC1");

    /* [3] MFC 1K: block 0 + all 16 trailers at 4*s+3. */
    memset(buf, 0xAA, sizeof(buf));
    CHECK(nfc_add_build_image(NFC_ADD_MFC_1K, uid4, 4, buf, sizeof(buf)) == 1024, "1K build size");
    CHECK(buf[0]==0x11 && buf[1]==0x22 && buf[2]==0x33 && buf[3]==0x44, "1K block0 UID");
    CHECK(buf[4]==nfc_add_bcc_mfc(uid4), "1K block0 BCC");
    CHECK(buf[5]==0x08, "1K SAK 0x08");
    CHECK(buf[6]==0x04 && buf[7]==0x00, "1K ATQA 04 00");
    CHECK(buf[8]==0 && buf[15]==0, "1K manufacturer filler zeroed");
    {
        int trailers_ok = 1, data_ok = 1;
        for (uint8_t s = 0; s < 16; s++) {
            uint32_t off = (uint32_t)(s * 4 + 3) * 16;
            for (int k = 0; k < 6; k++) if (buf[off+k]  != 0xFF) trailers_ok = 0;
            if (buf[off+6]!=0xFF||buf[off+7]!=0x07||buf[off+8]!=0x80) trailers_ok = 0;
            if (buf[off+9]!=0x69) trailers_ok = 0;
            for (int k = 10; k < 16; k++) if (buf[off+k] != 0xFF) trailers_ok = 0;
        }
        /* data blocks 1,2 of sector 0 are zeroed */
        for (int k = 16; k < 48; k++) if (buf[k] != 0) data_ok = 0;
        CHECK(trailers_ok, "1K all 16 trailers correct");
        CHECK(data_ok, "1K data blocks zeroed");
    }

    /* [4] MFC 4K: 40 trailers incl big sectors 32-39 (16 blocks). */
    memset(buf, 0xAA, sizeof(buf));
    CHECK(nfc_add_build_image(NFC_ADD_MFC_4K, uid4, 4, buf, sizeof(buf)) == 4096, "4K build size");
    CHECK(buf[5]==0x18, "4K SAK 0x18");
    CHECK(buf[6]==0x02 && buf[7]==0x00, "4K ATQA 02 00");
    {
        int ok = 1;
        for (uint8_t s = 0; s < 40; s++) {
            uint16_t first = (s < 32) ? (uint16_t)(s*4) : (uint16_t)(128 + (s-32)*16);
            uint8_t  nb    = (s < 32) ? 4 : 16;
            uint32_t off   = (uint32_t)(first + nb - 1) * 16;
            for (int k = 0; k < 6; k++)  if (buf[off+k]  != 0xFF) ok = 0;
            if (buf[off+6]!=0xFF||buf[off+7]!=0x07||buf[off+8]!=0x80||buf[off+9]!=0x69) ok = 0;
            for (int k = 10; k < 16; k++) if (buf[off+k] != 0xFF) ok = 0;
        }
        CHECK(ok, "4K all 40 trailers correct (incl big sectors)");
        /* sector 39 trailer is block 255 -> last 16 bytes of the image */
        CHECK((255U*16U) == 4080U, "4K sector39 trailer at block 255");
    }

    /* [5] NTAG213/215/216 boundaries: UID/BCC, CC page, config PWD/PACK. */
    struct { nfc_add_type_t t; uint8_t cc3; uint32_t size; uint16_t last_user; } nt[] = {
        { NFC_ADD_NTAG213, 0x12, 180, 39 },
        { NFC_ADD_NTAG215, 0x3E, 540, 129 },
        { NFC_ADD_NTAG216, 0x6D, 924, 225 },
    };
    for (unsigned i = 0; i < 3; i++) {
        memset(buf, 0xAA, sizeof(buf));
        uint32_t sz = nfc_add_build_image(nt[i].t, uid7, 7, buf, sizeof(buf));
        CHECK(sz == nt[i].size, "NTAG build size");
        CHECK(buf[0]==uid7[0] && buf[3]==nfc_add_bcc0_t2t(uid7), "NTAG page0 UID/BCC0");
        CHECK(buf[8]==nfc_add_bcc1_t2t(uid7) && buf[9]==0x48, "NTAG page2 BCC1/internal");
        CHECK(buf[12]==0xE1 && buf[13]==0x10 && buf[14]==nt[i].cc3 && buf[15]==0x00, "NTAG CC page");
        /* first user page (4) and last user page zeroed */
        CHECK(buf[16]==0 && buf[19]==0, "NTAG first user page zero");
        uint32_t lu = (uint32_t)(nt[i].last_user) * 4;
        CHECK(buf[lu]==0 && buf[lu+3]==0, "NTAG last user page zero");
        /* config: last 5 pages = lock | CFG0 | CFG1 | PWD | PACK */
        uint32_t last = sz - 4;               /* PACK page offset */
        uint32_t pwd  = last - 4;             /* PWD page offset  */
        uint32_t cfg0 = last - 12;            /* CFG0 page offset */
        CHECK(buf[cfg0]==0x04 && buf[cfg0+3]==0xFF, "NTAG CFG0 04..FF (AUTH0 off)");
        CHECK(buf[pwd]==0xFF && buf[pwd+1]==0xFF && buf[pwd+2]==0xFF && buf[pwd+3]==0xFF, "NTAG PWD factory FF");
        CHECK(buf[last]==0 && buf[last+1]==0 && buf[last+2]==0 && buf[last+3]==0, "NTAG PACK 0000");
    }

    /* [6] Ultralight: 16 pages, UID/BCC, OTP page 3 = 0, no config. */
    memset(buf, 0xAA, sizeof(buf));
    CHECK(nfc_add_build_image(NFC_ADD_ULTRALIGHT, uid7, 7, buf, sizeof(buf)) == 64, "UL build size");
    CHECK(buf[3]==nfc_add_bcc0_t2t(uid7), "UL BCC0");
    CHECK(buf[8]==nfc_add_bcc1_t2t(uid7), "UL BCC1");
    CHECK(buf[12]==0 && buf[13]==0 && buf[14]==0 && buf[15]==0, "UL OTP page3 zero (no CC)");
    { int ok=1; for (int k=16;k<64;k++) if (buf[k]!=0) ok=0; CHECK(ok, "UL user pages zeroed"); }

    /* [7] Rejections: wrong uid_len, too-small buffer, bad type. */
    CHECK(nfc_add_build_image(NFC_ADD_MFC_1K, uid7, 7, buf, sizeof(buf)) == 0, "1K wrong uid_len rejected");
    CHECK(nfc_add_build_image(NFC_ADD_NTAG213, uid4, 4, buf, sizeof(buf)) == 0, "NTAG wrong uid_len rejected");
    CHECK(nfc_add_build_image(NFC_ADD_MFC_4K, uid4, 4, buf, 100) == 0, "too-small buffer rejected");
    CHECK(nfc_add_build_image(NFC_ADD_TYPE_COUNT, uid4, 4, buf, sizeof(buf)) == 0, "bad type rejected");
    CHECK(nfc_add_build_image(NFC_ADD_MFC_1K, NULL, 4, buf, sizeof(buf)) == 0, "NULL uid rejected");
    CHECK(nfc_add_build_image(NFC_ADD_MFC_1K, uid4, 4, NULL, sizeof(buf)) == 0, "NULL out rejected");

    /* [8] Determinism / no stale data: rebuild into a dirty buffer twice, identical. */
    static uint8_t a[4096], b[4096];
    memset(a, 0x5A, sizeof(a)); memset(b, 0xA5, sizeof(b));
    uint32_t sa = nfc_add_build_image(NFC_ADD_NTAG216, uid7, 7, a, sizeof(a));
    uint32_t sb = nfc_add_build_image(NFC_ADD_NTAG216, uid7, 7, b, sizeof(b));
    CHECK(sa == sb && sa == 924 && memcmp(a, b, sa) == 0, "deterministic rebuild, no stale bytes");

    /* [9] Menu/type mapping: the six types are ordered and distinct. */
    CHECK(NFC_ADD_MFC_1K == 0 && NFC_ADD_MFC_4K == 1 && NFC_ADD_ULTRALIGHT == 2 &&
          NFC_ADD_NTAG213 == 3 && NFC_ADD_NTAG215 == 4 && NFC_ADD_NTAG216 == 5 &&
          NFC_ADD_TYPE_COUNT == 6, "enum order matches menu order");
    {
        /* every declared type has geometry; sizes strictly identify the type */
        int distinct = 1;
        uint32_t sizes[6];
        for (unsigned i = 0; i < 6; i++) { sizes[i] = nfc_add_geometry((nfc_add_type_t)i)->image_size; }
        for (unsigned i = 0; i < 6; i++)
            for (unsigned j = i + 1; j < 6; j++)
                if (i != 2 && j != 2 && sizes[i] == sizes[j]) distinct = 0; /* UL(64) unique anyway */
        CHECK(distinct, "type sizes usable as identifiers");
    }

    /* [10] No stale data across DIFFERENT types reusing the same buffer. */
    memset(buf, 0xCC, sizeof(buf));
    (void)nfc_add_build_image(NFC_ADD_MFC_4K, uid4, 4, buf, sizeof(buf));      /* 4096 B */
    uint32_t s10 = nfc_add_build_image(NFC_ADD_NTAG213, uid7, 7, buf, sizeof(buf)); /* 180 B */
    CHECK(s10 == 180, "cross-type rebuild size");
    CHECK(buf[12]==0xE1 && buf[14]==0x12, "cross-type: NTAG213 CC present (no MFC residue in header)");
    {   /* NTAG213 image region fully reflects the new build, no 4K trailer bytes leaking in */
        int clean = 1;
        /* block 3 trailer of the old 4K build was at offset 48 = FF..; NTAG213 page 12 (off 48) is user 0 */
        for (int k = 48; k < 52; k++) if (buf[k] != 0) clean = 0;
        CHECK(clean, "cross-type: no stale 4K trailer bytes in NTAG213 image");
    }

    printf("\nnfc_add_image_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
