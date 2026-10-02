/*
 * nfc_add_image.c - Blank-template image builders for manual card creation.
 *                   See nfc_add_image.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authoritative sources (geometry + factory/reset default values). All layouts
 * are derived from the NXP product datasheets; no card memory is copied from any
 * physical card, and no code was copied from third-party firmware for these
 * templates (the geometry is datasheet-derived, not reverse-engineered):
 *   - MIFARE Classic 1K : NXP MF1S50yyX (16 sectors, 64 blocks; block-0 layout;
 *                         sector-trailer access-condition tables).
 *   - MIFARE Classic 4K : NXP MF1S70yyX (40 sectors, 256 blocks; sectors 32-39
 *                         are 16-block with grouped access conditions).
 *     Transport-configuration trailer FF FF FF FF FF FF FF 07 80 69 FF FF FF FF
 *     FF FF (Key A / access 011+trailer 001 / GPB 0x69 / Key B) is the NXP
 *     delivery/transport default; keys here are TEMPLATE-configured, never
 *     represented as authenticated against a card.
 *   - MIFARE Ultralight : NXP MF0ICU1 (16 pages; UID/BCC0/BCC1; OTP page 3).
 *   - NTAG213/215/216   : NXP NTAG21x (NT3H1101/NT3H1201) -- 45/135/231 pages;
 *                         CC E1 10 {12,3E,6D} 00; config pages (dynamic lock,
 *                         CFG0 AUTH0=0xFF, CFG1, PWD=FFFFFFFF, PACK=0000) are the
 *                         documented factory/reset defaults, generated here (no
 *                         password is invented and none is read from a card).
 */
#include "nfc_add_image.h"
#include <stddef.h>
#include <string.h>

/* T2T sub-variant codes -- mirror M1NFC_T2TVAR_* in nfc_ctx.h (kept numeric so
 * this module stays host-testable without the full ctx header). */
#define ADD_T2TVAR_UL       1
#define ADD_T2TVAR_NTAG213  6
#define ADD_T2TVAR_NTAG215  7
#define ADD_T2TVAR_NTAG216  8

/* Classic sector geometry -- mirrors nfc_ctx.h m1nfc_mfc_sector_first_block /
 * m1nfc_mfc_sector_blocks (sectors 0-31 have 4 blocks, 32-39 have 16). */
static uint8_t  mfc_sector_blocks(uint8_t sector)
{
    return (sector < 32U) ? 4U : 16U;
}
static uint16_t mfc_sector_first_block(uint8_t sector)
{
    return (sector < 32U) ? (uint16_t)(sector * 4U)
                          : (uint16_t)(128U + (uint16_t)(sector - 32U) * 16U);
}

static const nfc_add_geom_t s_geom[NFC_ADD_TYPE_COUNT] = {
    /* type              label        devtype             family              uid unit cnt   size  sak    atqa        var               cc3   uf  ul   */
    { NFC_ADD_MFC_1K,    "MFC 1K",    "Classic",          NFC_ADD_FAM_CLASSIC, 4, 16,  64,   1024, 0x08, {0x04,0x00}, 0,                0x00,  1, 63 },
    { NFC_ADD_MFC_4K,    "MFC 4K",    "Classic",          NFC_ADD_FAM_CLASSIC, 4, 16, 256,   4096, 0x18, {0x02,0x00}, 0,                0x00,  1,255 },
    { NFC_ADD_ULTRALIGHT,"Ultralight","Ultralight/NTAG",  NFC_ADD_FAM_T2T,     7,  4,  16,     64, 0x00, {0x44,0x00}, ADD_T2TVAR_UL,     0x00,  4, 15 },
    { NFC_ADD_NTAG213,   "NTAG213",   "Ultralight/NTAG",  NFC_ADD_FAM_T2T,     7,  4,  45,    180, 0x00, {0x44,0x00}, ADD_T2TVAR_NTAG213,0x12,  4, 39 },
    { NFC_ADD_NTAG215,   "NTAG215",   "Ultralight/NTAG",  NFC_ADD_FAM_T2T,     7,  4, 135,    540, 0x00, {0x44,0x00}, ADD_T2TVAR_NTAG215,0x3E,  4,129 },
    { NFC_ADD_NTAG216,   "NTAG216",   "Ultralight/NTAG",  NFC_ADD_FAM_T2T,     7,  4, 231,    924, 0x00, {0x44,0x00}, ADD_T2TVAR_NTAG216,0x6D,  4,225 },
};

const nfc_add_geom_t *nfc_add_geometry(nfc_add_type_t type)
{
    /* unsigned compare catches both negatives (wrap) and out-of-range. */
    if ((unsigned)type >= (unsigned)NFC_ADD_TYPE_COUNT) return NULL;
    return &s_geom[type];
}

uint8_t nfc_add_uid_len(nfc_add_type_t type)
{
    const nfc_add_geom_t *g = nfc_add_geometry(type);
    return g ? g->uid_len : 0U;
}

uint8_t nfc_add_bcc_mfc(const uint8_t uid[4])
{
    return (uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
}
uint8_t nfc_add_bcc0_t2t(const uint8_t uid[7])
{
    return (uint8_t)(0x88U ^ uid[0] ^ uid[1] ^ uid[2]);   /* CT=0x88 for 7-byte UID */
}
uint8_t nfc_add_bcc1_t2t(const uint8_t uid[7])
{
    return (uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
}

/* --- MIFARE Classic 1K/4K blank image --- */
static void build_mfc(const nfc_add_geom_t *g, const uint8_t uid[4], uint8_t *out)
{
    (void)memset(out, 0x00, g->image_size);

    /* Block 0: UID(4) | BCC | SAK | ATQA(2) | manufacturer filler(8 = template 0). */
    out[0] = uid[0]; out[1] = uid[1]; out[2] = uid[2]; out[3] = uid[3];
    out[4] = nfc_add_bcc_mfc(uid);
    out[5] = g->sak;
    out[6] = g->atqa[0];
    out[7] = g->atqa[1];
    /* out[8..15] left 0 -- template filler, not claimed as real manufacturer data. */

    /* Sector trailers: default keys FFFFFFFFFFFF, transport access bits FF 07 80,
     * GPB 0x69, Key B FFFFFFFFFFFF. Access bytes are structurally valid (011/001
     * per NXP), i.e. the standard blank/transport configuration. */
    uint8_t sectors = (g->type == NFC_ADD_MFC_4K) ? 40U : 16U;
    for (uint8_t s = 0; s < sectors; s++) {
        uint16_t tb  = (uint16_t)(mfc_sector_first_block(s) + mfc_sector_blocks(s) - 1U);
        uint32_t off = (uint32_t)tb * 16U;
        (void)memset(&out[off], 0xFF, 6);          /* Key A */
        out[off + 6] = 0xFF; out[off + 7] = 0x07; out[off + 8] = 0x80;  /* access bits */
        out[off + 9] = 0x69;                       /* GPB (default) */
        (void)memset(&out[off + 10], 0xFF, 6);     /* Key B */
    }
}

/* --- Type-2 (Ultralight / NTAG) blank image --- */
static void build_t2t(const nfc_add_geom_t *g, const uint8_t uid[7], uint8_t *out)
{
    (void)memset(out, 0x00, g->image_size);

    /* Page 0: UID0-2 | BCC0 */
    out[0] = uid[0]; out[1] = uid[1]; out[2] = uid[2]; out[3] = nfc_add_bcc0_t2t(uid);
    /* Page 1: UID3-6 */
    out[4] = uid[3]; out[5] = uid[4]; out[6] = uid[5]; out[7] = uid[6];
    /* Page 2: BCC1 | Internal(0x48) | Lock0 | Lock1 (locks 0 = unlocked) */
    out[8] = nfc_add_bcc1_t2t(uid); out[9] = 0x48; out[10] = 0x00; out[11] = 0x00;

    if (g->cc3 != 0x00U) {
        /* NTAG: Page 3 = Capability Container E1 10 <size> 00. */
        out[12] = 0xE1; out[13] = 0x10; out[14] = g->cc3; out[15] = 0x00;

        /* Config = last 5 pages: dynamic lock | CFG0 | CFG1 | PWD | PACK. */
        uint32_t last  = (uint32_t)g->unit_count - 1U;
        uint32_t lock  = (last - 4U) * 4U;   /* dynamic lock bytes */
        uint32_t cfg0  = (last - 3U) * 4U;
        uint32_t cfg1  = (last - 2U) * 4U;
        uint32_t pwd   = (last - 1U) * 4U;
        uint32_t pack  = (last - 0U) * 4U;
        /* dynamic lock: unlocked (0) -- already zeroed. */
        (void)lock;
        out[cfg0 + 0] = 0x04; out[cfg0 + 1] = 0x00; out[cfg0 + 2] = 0x00; out[cfg0 + 3] = 0xFF; /* AUTH0=0xFF: no pages protected */
        out[cfg1 + 0] = 0x00; out[cfg1 + 1] = 0x05; out[cfg1 + 2] = 0x00; out[cfg1 + 3] = 0x00;
        out[pwd  + 0] = 0xFF; out[pwd  + 1] = 0xFF; out[pwd  + 2] = 0xFF; out[pwd  + 3] = 0xFF; /* factory default PWD */
        out[pack + 0] = 0x00; out[pack + 1] = 0x00; out[pack + 2] = 0x00; out[pack + 3] = 0x00; /* PACK 0000 */
    } else {
        /* Original Ultralight: Page 3 = OTP (factory 0). No CC / config pages. */
        /* out[12..15] left 0. */
    }
}

uint32_t nfc_add_build_image(nfc_add_type_t type, const uint8_t *uid, uint8_t uid_len,
                             uint8_t *out, uint32_t out_cap)
{
    const nfc_add_geom_t *g = nfc_add_geometry(type);
    if (g == NULL || uid == NULL || out == NULL) return 0U;
    if (uid_len != g->uid_len) return 0U;
    if (out_cap < g->image_size) return 0U;

    if (g->family == NFC_ADD_FAM_CLASSIC) build_mfc(g, uid, out);
    else                                  build_t2t(g, uid, out);
    return g->image_size;
}
