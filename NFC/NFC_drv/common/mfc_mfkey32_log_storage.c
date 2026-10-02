/*
 * mfc_mfkey32_log_storage.c - FatFs/SD binding for mfc_mfkey32_log.c
 *                             (Extract Keys export, Phase 4).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See mfc_mfkey32_log_storage.h. Device-only; uses the project's FatFs
 * wrappers directly (no existing m1_file_browser helper opens in append
 * mode). No dynamic allocation. No RF/radio dependency.
 */
#include "mfc_mfkey32_log_storage.h"
#include "m1_file_util.h"      /* fs_directory_ensure */
#include "m1_sdcard.h"         /* m1_sdcard_get_info / _get_free_capacity */

#define MFLOGS_NFC_DIR      "0:/NFC"
#define MFLOGS_RECOVER_DIR  "0:/NFC/recover"
#define MFLOGS_PATH         "0:/NFC/recover/mfkey32.log"
#define MFLOGS_MIN_FREE     4096u     /* bytes of free space required to start */

int mfc_mfkey32_log_storage_start(mfc_mfkey32_log_storage_t *st)
{
    if (st == NULL) {
        return MFC_MFLOGS_ERR_SD;
    }
    st->open = 0;

    (void)m1_sdcard_get_info();
    if (m1_sdcard_get_free_capacity() < MFLOGS_MIN_FREE) {
        return MFC_MFLOGS_ERR_SD;
    }

    if (fs_directory_ensure(MFLOGS_NFC_DIR) != FR_OK) {
        return MFC_MFLOGS_ERR_DIR;
    }
    if (fs_directory_ensure(MFLOGS_RECOVER_DIR) != FR_OK) {
        return MFC_MFLOGS_ERR_DIR;
    }

    if (f_open(&st->file, MFLOGS_PATH, FA_WRITE | FA_OPEN_APPEND) != FR_OK) {
        return MFC_MFLOGS_ERR_OPEN;
    }
    st->open = 1;
    return MFC_MFLOGS_OK;
}

bool mfc_mfkey32_log_storage_write(void *ctx, const uint8_t *bytes, size_t len)
{
    mfc_mfkey32_log_storage_t *st = (mfc_mfkey32_log_storage_t *)ctx;
    if ((st == NULL) || (st->open == 0) || (bytes == NULL)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (len > 0xFFFFu) {
        return false;
    }
    UINT written = 0;
    if (f_write(&st->file, bytes, (UINT)len, &written) != FR_OK) {
        return false;
    }
    if (written != (UINT)len) {
        return false;   /* short write (e.g. card full mid-line) */
    }
    if (f_sync(&st->file) != FR_OK) {
        return false;   /* line accepted by f_write but not durable: treat as failed */
    }
    return true;
}

int mfc_mfkey32_log_storage_close(mfc_mfkey32_log_storage_t *st)
{
    if (st == NULL) {
        return MFC_MFLOGS_ERR_SD;
    }
    if (st->open) {
        (void)f_close(&st->file);
        st->open = 0;
    }
    return MFC_MFLOGS_OK;
}
