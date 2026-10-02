/*
 * mfc_mfkey32_log_storage.h - FatFs/SD binding for mfc_mfkey32_log.c
 *                             (Extract Keys export, Phase 4).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DEVICE-ONLY glue between the hardware-agnostic line formatter
 * (mfc_mfkey32_log.*) and the microSD card via the project's FatFs
 * wrappers. Opens (creating if absent) a SINGLE FIXED filename,
 * 0:/NFC/recover/mfkey32.log, and always appends -- matching Flipper's own
 * mfkey32_logger.c exactly (single fixed name, FSOM_OPEN_APPEND, never
 * truncated). No RF/radio dependency. Not host-testable (requires FatFs +
 * an SD card); verified by the ARM build.
 */
#ifndef MFC_MFKEY32_LOG_STORAGE_H
#define MFC_MFKEY32_LOG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ff.h"          /* FIL */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MFC_MFLOGS_OK       =  0,
    MFC_MFLOGS_ERR_SD   = -1,   /* SD absent or insufficient free space */
    MFC_MFLOGS_ERR_DIR  = -2,   /* directory create/ensure failed       */
    MFC_MFLOGS_ERR_OPEN = -3,   /* f_open failed                        */
} mfc_mfkey32_log_storage_result_t;

typedef struct {
    FIL     file;
    uint8_t open;
} mfc_mfkey32_log_storage_t;

/*
 * Ensure 0:/NFC and 0:/NFC/recover exist, then open
 *   0:/NFC/recover/mfkey32.log
 * for append (created if absent, existing content preserved and seeked to
 * EOF otherwise). Returns MFC_MFLOGS_OK, or a negative result on
 * SD/dir/open failure.
 */
int mfc_mfkey32_log_storage_start(mfc_mfkey32_log_storage_t *st);

/*
 * Write callback (matches mfc_mfkey32_log_write_fn): write `len` bytes and
 * fsync so a line already accepted here survives an immediate power loss.
 * ctx must be the mfc_mfkey32_log_storage_t*. Returns true iff all bytes
 * were written and synced; any failure returns false.
 */
bool mfc_mfkey32_log_storage_write(void *ctx, const uint8_t *bytes, size_t len);

/* Close the file (safe to call once; idempotent). */
int mfc_mfkey32_log_storage_close(mfc_mfkey32_log_storage_t *st);

#ifdef __cplusplus
}
#endif

#endif /* MFC_MFKEY32_LOG_STORAGE_H */
