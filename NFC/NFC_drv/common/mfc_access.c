/*
 * mfc_access.c - MIFARE Classic access-condition decoder. See mfc_access.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mfc_access.h"
#include <stddef.h>   /* NULL */

/*
 * Access-byte bit layout (NXP MF1S50yyX), blocks 0..3, bit i selects block i:
 *
 *   byte6: [7:4] = ~C2[3:0]   [3:0] = ~C1[3:0]
 *   byte7: [7:4] =  C1[3:0]   [3:0] = ~C3[3:0]
 *   byte8: [7:4] =  C3[3:0]   [3:0] =  C2[3:0]
 *
 * Integrity: each non-inverted nibble must be the 4-bit complement of its
 * inverted twin.
 */
bool mfc_access_decode(const uint8_t trailer[16], mfc_access_t *out)
{
    if (trailer == NULL || out == NULL) return false;

    uint8_t b6 = trailer[6], b7 = trailer[7], b8 = trailer[8];

    uint8_t inv_c1 = (uint8_t)(b6 & 0x0Fu);
    uint8_t inv_c2 = (uint8_t)(b6 >> 4);
    uint8_t inv_c3 = (uint8_t)(b7 & 0x0Fu);
    uint8_t c1     = (uint8_t)(b7 >> 4);
    uint8_t c2     = (uint8_t)(b8 & 0x0Fu);
    uint8_t c3     = (uint8_t)(b8 >> 4);

    /* Non-inverted nibble must equal ~inverted (masked to 4 bits). */
    if (c1 != ((uint8_t)(~inv_c1) & 0x0Fu)) return false;
    if (c2 != ((uint8_t)(~inv_c2) & 0x0Fu)) return false;
    if (c3 != ((uint8_t)(~inv_c3) & 0x0Fu)) return false;

    for (uint8_t i = 0; i < 4; i++) {
        out->c1[i] = (uint8_t)((c1 >> i) & 1u);
        out->c2[i] = (uint8_t)((c2 >> i) & 1u);
        out->c3[i] = (uint8_t)((c3 >> i) & 1u);
    }
    return true;
}

bool mfc_access_valid(const uint8_t trailer[16])
{
    mfc_access_t tmp;
    return mfc_access_decode(trailer, &tmp);
}

/* Pack a block's triple into 0bC1C2C3 (0..7) for table lookup. */
static uint8_t triple(const mfc_access_t *a, uint8_t blk)
{
    return (uint8_t)((a->c1[blk] << 2) | (a->c2[blk] << 1) | a->c3[blk]);
}

uint8_t mfc_access_group(uint8_t block_in_sector, uint8_t sector_blocks)
{
    if (sector_blocks <= 4U) {
        return (block_in_sector < 4U) ? block_in_sector : 3U;   /* 0,1,2 data; 3 trailer */
    }
    /* Big (16-block) sector: three 5-block data groups, then the trailer. */
    if (block_in_sector < 5U)  return 0U;
    if (block_in_sector < 10U) return 1U;
    if (block_in_sector < 15U) return 2U;
    return 3U;   /* block 15 = trailer */
}

/*
 * Data-block WRITE permission (NXP data-block access table), indexed by the
 * 3-bit condition C1C2C3:
 *   000 -> A|B     001 -> never   010 -> never   011 -> key B
 *   100 -> key B   101 -> never   110 -> key B   111 -> never
 */
bool mfc_access_can_write_data(const mfc_access_t *a, uint8_t block_in_sector,
                               uint8_t sector_blocks, mfc_acc_key_t key)
{
    if (a == NULL) return false;
    uint8_t g = mfc_access_group(block_in_sector, sector_blocks);
    if (g > 2U) return false;   /* not a data block (trailer) */
    switch (triple(a, g)) {
        case 0x0: return true;                       /* A or B          */
        case 0x3: /* fallthrough */
        case 0x4: /* fallthrough */
        case 0x6: return (key == MFC_ACC_KEY_B);     /* key B only      */
        default:  return false;                      /* 001,101,111,010 */
    }
}

/*
 * Sector-trailer access table (NXP). To write the WHOLE trailer in one block
 * write the key must be permitted to write Key A, the access bytes, AND Key B.
 * Working that column intersection through the table leaves exactly:
 *   001 -> key A may write the full trailer
 *   011 -> key B may write the full trailer
 * every other condition forbids a full trailer rewrite.
 */
bool mfc_access_can_write_trailer(const mfc_access_t *a, mfc_acc_key_t key)
{
    if (a == NULL) return false;
    switch (triple(a, 3)) {
        case 0x1: return (key == MFC_ACC_KEY_A);
        case 0x3: return (key == MFC_ACC_KEY_B);
        default:  return false;
    }
}

/*
 * Trailer access-byte READ permission (NXP). Access bytes are readable with:
 *   000,001,010 -> key A only        100,101,110,111 -> key A or B
 */
bool mfc_access_can_read_trailer(const mfc_access_t *a, mfc_acc_key_t key)
{
    if (a == NULL) return false;
    switch (triple(a, 3)) {
        case 0x0: case 0x1: case 0x2:
            return (key == MFC_ACC_KEY_A);
        default:
            return true;                             /* A or B */
    }
}
