/*
 * m1_mfc_emu_image.c - see m1_mfc_emu_image.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "m1_mfc_emu_image.h"
#include "nfc_ctx.h"
#include <string.h>

m1_mfc_emu_status_t m1_mfc_emu_image_build(m1_mfc_emu_image_t *out)
{
    if (out == NULL) { return M1_MFC_EMU_NO_SOURCE; }
    memset(out, 0, sizeof(*out));

    nfc_run_ctx_t *c = nfc_ctx_get();
    const nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    if ((c == NULL) || (mfc == NULL) || !mfc->valid) {
        return M1_MFC_EMU_NO_SOURCE;
    }

    if (mfc->type == M1NFC_MFCTYPE_4K) {
        return M1_MFC_EMU_UNSUPPORTED_4K;
    }
    if (mfc->type != M1NFC_MFCTYPE_1K) {
        return M1_MFC_EMU_NO_SOURCE;   /* Mini / Unknown -- not a supported source either */
    }

    if (c->head.uid_len != 4U) {
        return M1_MFC_EMU_UNSUPPORTED_UID_LEN;
    }
    if (!c->head.a.has_atqa || !c->head.a.has_sak) {
        return M1_MFC_EMU_NO_SOURCE;
    }

    /* Every one of the 64 blocks must have been actually read. */
    for (uint16_t b = 0; b < M1_MFC_EMU_BLOCKS; b++) {
        if (!nfc_ctx_mfc_block_valid(b)) {
            return M1_MFC_EMU_INCOMPLETE_BLOCKS;
        }
    }

    /* Every one of the 32 keys (16 sectors x A/B) must be known. Key A can
     * never be read back from a real card's trailer, so this is the only
     * source of truth -- never derived from raw trailer bytes here. */
    for (uint8_t s = 0; s < M1_MFC_EMU_SECTORS; s++) {
        if (!mfc->sec[s].key_a_found || !mfc->sec[s].key_b_found) {
            return M1_MFC_EMU_INCOMPLETE_KEYS;
        }
    }

    /* Identity consistency: the universally-defined part of MFC block 0 is
     * UID(4) + BCC(1) = XOR of the UID bytes. SAK/ATQA are NOT re-derived
     * from block 0 bytes 5-7 here -- manufacturer block layout for those
     * bytes is not uniformly standardized across real cards, so checking
     * them would risk false rejections of legitimate cards; UID+BCC is the
     * one invariant guaranteed by the MIFARE spec itself. */
    const uint8_t *block0 = nfc_ctx_mfc_block(0);
    if (block0 == NULL) { return M1_MFC_EMU_INCOMPLETE_BLOCKS; }
    if (memcmp(block0, c->head.uid, 4U) != 0) {
        return M1_MFC_EMU_IDENTITY_INCONSISTENT;
    }
    uint8_t bcc = (uint8_t)(c->head.uid[0] ^ c->head.uid[1] ^ c->head.uid[2] ^ c->head.uid[3]);
    if (block0[4] != bcc) {
        return M1_MFC_EMU_IDENTITY_INCONSISTENT;
    }

    /* All checks passed -- copy everything into the self-contained image. */
    memcpy(out->uid, c->head.uid, 4U);
    out->atqa[0] = c->head.a.atqa[0];
    out->atqa[1] = c->head.a.atqa[1];
    out->sak     = c->head.a.sak;

    for (uint16_t b = 0; b < M1_MFC_EMU_BLOCKS; b++) {
        const uint8_t *blk = nfc_ctx_mfc_block(b);
        memcpy(out->block[b], blk, 16U);
        out->block_valid[b] = true;
    }

    for (uint8_t s = 0; s < M1_MFC_EMU_SECTORS; s++) {
        memcpy(out->key_a[s], mfc->sec[s].key_a, 6U);
        out->key_a_valid[s] = true;
        memcpy(out->key_b[s], mfc->sec[s].key_b, 6U);
        out->key_b_valid[s] = true;
    }

    return M1_MFC_EMU_OK;
}

bool m1_mfc_emu_image_key(const m1_mfc_emu_image_t *img, uint8_t key_type,
                          uint8_t block, uint64_t *out_key)
{
    if ((img == NULL) || (out_key == NULL)) { return false; }
    if (block >= M1_MFC_EMU_BLOCKS) { return false; }
    uint8_t sector = block / 4U;   /* MFC 1K: 4 blocks per sector, uniform */
    if (sector >= M1_MFC_EMU_SECTORS) { return false; }

    const uint8_t *key;
    bool valid;
    if (key_type == 0x60U) {
        key   = img->key_a[sector];
        valid = img->key_a_valid[sector];
    } else if (key_type == 0x61U) {
        key   = img->key_b[sector];
        valid = img->key_b_valid[sector];
    } else {
        return false;
    }
    if (!valid) { return false; }

    uint64_t k = 0;
    for (uint8_t i = 0; i < 6U; i++) { k = (k << 8) | key[i]; }
    *out_key = k;
    return true;
}

const char *m1_mfc_emu_status_message(m1_mfc_emu_status_t status)
{
    switch (status) {
        case M1_MFC_EMU_OK:                     return "OK";
        case M1_MFC_EMU_NO_SOURCE:               return "No MFC 1K card loaded";
        case M1_MFC_EMU_UNSUPPORTED_4K:           return "MFC 4K not supported";
        case M1_MFC_EMU_UNSUPPORTED_UID_LEN:      return "7-byte UID not supported";
        case M1_MFC_EMU_INCOMPLETE_BLOCKS:        return "Complete MFC 1K read required";
        case M1_MFC_EMU_INCOMPLETE_KEYS:          return "Complete MFC 1K read required";
        case M1_MFC_EMU_IDENTITY_INCONSISTENT:    return "Card identity inconsistent";
        case M1_MFC_EMU_START_FAILED:              return "Emulator failed to start";
        default:                                  return "Emulation unavailable";
    }
}
