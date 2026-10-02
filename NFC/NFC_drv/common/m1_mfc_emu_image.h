/*
 * m1_mfc_emu_image.h - stable, session-owned MIFARE Classic 1K emulation image.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bridges the currently-selected/loaded card in nfc_ctx (mutable, singleton,
 * shared with UI/file-parser/live-read) to the raw MFC emulation engine
 * (m1_mfc_raw_listener.c / m1_mfc_session.c / ce_mfc.c), which must not touch
 * nfc_ctx after RF emulation starts.
 *
 * m1_mfc_emu_image_build() performs the full production eligibility gate --
 * MFC 1K only, 4-byte UID only, all 64 blocks known, all 32 keys known,
 * UID/block-0/BCC internally consistent -- and either fills a self-contained
 * snapshot or fails with a specific, user-reportable reason. It never
 * fabricates missing content and never partially fills *out on failure.
 */
#ifndef M1_MFC_EMU_IMAGE_H_
#define M1_MFC_EMU_IMAGE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M1_MFC_EMU_SECTORS  16U
#define M1_MFC_EMU_BLOCKS   64U

typedef enum {
    M1_MFC_EMU_OK = 0,
    M1_MFC_EMU_NO_SOURCE,             /* no valid MIFARE Classic card loaded       */
    M1_MFC_EMU_UNSUPPORTED_4K,        /* MFC 4K -- this engine is 1K-only          */
    M1_MFC_EMU_UNSUPPORTED_UID_LEN,   /* 7-byte UID -- this engine is 4-byte-only  */
    M1_MFC_EMU_INCOMPLETE_BLOCKS,     /* one or more of the 64 blocks not read     */
    M1_MFC_EMU_INCOMPLETE_KEYS,       /* one or more of the 32 keys not known      */
    M1_MFC_EMU_IDENTITY_INCONSISTENT, /* header UID / block 0 / BCC disagree       */
    M1_MFC_EMU_START_FAILED,          /* RAWOWN/DMA session did not confirm start  */
} m1_mfc_emu_status_t;

typedef struct {
    uint8_t uid[4];
    uint8_t atqa[2];
    uint8_t sak;

    uint8_t block[M1_MFC_EMU_BLOCKS][16];
    bool    block_valid[M1_MFC_EMU_BLOCKS];

    uint8_t key_a[M1_MFC_EMU_SECTORS][6];
    bool    key_a_valid[M1_MFC_EMU_SECTORS];
    uint8_t key_b[M1_MFC_EMU_SECTORS][6];
    bool    key_b_valid[M1_MFC_EMU_SECTORS];
} m1_mfc_emu_image_t;

/* Build a self-contained snapshot from the currently-selected card (nfc_ctx).
 * On M1_MFC_EMU_OK, *out is fully populated and independent of nfc_ctx from
 * that point on. On any other status, *out is left zeroed -- never a
 * partially-filled or fabricated image. */
m1_mfc_emu_status_t m1_mfc_emu_image_build(m1_mfc_emu_image_t *out);

/* Look up the sector key for an incoming AUTH (key_type 0x60=A/0x61=B, block
 * 0..63). Returns false (no fallback) if the block is out of range or that
 * key was not marked valid in the snapshot. */
bool m1_mfc_emu_image_key(const m1_mfc_emu_image_t *img, uint8_t key_type,
                          uint8_t block, uint64_t *out_key);

/* Short, user-facing refusal message for a given status. Never NULL. */
const char *m1_mfc_emu_status_message(m1_mfc_emu_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* M1_MFC_EMU_IMAGE_H_ */
