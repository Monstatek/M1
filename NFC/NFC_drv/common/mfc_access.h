/*
 * mfc_access.h - MIFARE Classic sector-trailer access-condition decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pure logic (no RF, no allocation). Decodes the 3 access bytes of a sector
 * trailer (bytes 6..8) into the per-block (C1,C2,C3) triples, validates the
 * inverted-nibble integrity, and answers "may key A/B WRITE this block?" for
 * both data blocks and the trailer -- the permission model MIFARE Classic
 * enforces on-card. Used by the 1K writer to (a) skip blocks the authenticated
 * key is not allowed to write and (b) refuse to write a trailer whose new
 * access bits are malformed (brick prevention).
 *
 * Reference: NXP MF1S50yyX sector-trailer access-condition tables.
 */
#ifndef NFC_DRV_MFC_ACCESS_H_
#define NFC_DRV_MFC_ACCESS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Key type used for an authentication. Matches the low bit of the MIFARE
 * AUTH command (0x60 = A, 0x61 = B); here 0 = A, 1 = B. */
typedef enum {
    MFC_ACC_KEY_A = 0,
    MFC_ACC_KEY_B = 1
} mfc_acc_key_t;

/* Decoded access conditions for one 4-block sector (1K layout: blocks 0..2
 * data, block 3 trailer). c1/c2/c3[i] is the access-bit triple for block i. */
typedef struct {
    uint8_t c1[4];
    uint8_t c2[4];
    uint8_t c3[4];
} mfc_access_t;

/* Decode the access bytes of a 16-byte sector trailer (bytes [6],[7],[8]).
 * Returns true and fills *out only when the inverted-nibble integrity holds
 * (each non-inverted nibble is the bit-complement of its inverted twin). A
 * trailer that fails this check has corrupt/forged access bits and must never
 * be trusted or written back. */
bool mfc_access_decode(const uint8_t trailer[16], mfc_access_t *out);

/* Same integrity check as above without keeping the decoded result. */
bool mfc_access_valid(const uint8_t trailer[16]);

/* Map a block within a sector to its access-condition group (0..3, where 3 is
 * the trailer group). Small sectors (<=4 blocks, 1K and 4K sectors 0..31): the
 * block index IS its group (0,1,2 data; 3 trailer). Big sectors (16 blocks, 4K
 * sectors 32..39): blocks 0..4 -> group 0, 5..9 -> 1, 10..14 -> 2, 15 -> trailer.
 */
uint8_t mfc_access_group(uint8_t block_in_sector, uint8_t sector_blocks);

/* May `key` WRITE data block `block_in_sector` of a sector with `sector_blocks`
 * blocks (4 or 16)? The block is mapped to its access group first, so this is
 * correct for both 1K/small sectors and 4K big sectors. */
bool mfc_access_can_write_data(const mfc_access_t *a, uint8_t block_in_sector,
                               uint8_t sector_blocks, mfc_acc_key_t key);

/* May `key` WRITE the sector trailer (block 3) -- i.e. rewrite Key A, the
 * access bytes, and Key B in one block write? True only when the authenticated
 * key is permitted to write ALL three trailer fields, which is what the card
 * requires to accept a trailer write. */
bool mfc_access_can_write_trailer(const mfc_access_t *a, mfc_acc_key_t key);

/* May `key` READ the trailer's access bytes? (Key bytes always read back as 0;
 * this governs whether the access bytes we read are trustworthy.) */
bool mfc_access_can_read_trailer(const mfc_access_t *a, mfc_acc_key_t key);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_ACCESS_H_ */
