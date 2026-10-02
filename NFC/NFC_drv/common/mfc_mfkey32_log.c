/*
 * mfc_mfkey32_log.c - Extract Keys nonce-pair export (Phase 4).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See mfc_mfkey32_log.h. Pure/host-testable: no FatFs/HAL dependency.
 */
#include "mfc_mfkey32_log.h"
#include <string.h>
#include <stdio.h>

size_t mfc_mfkey32_log_format_line(char *buf, size_t buflen, const mfc_pair_t *pair)
{
    if ((buf == NULL) || (buflen == 0U) || (pair == NULL) || !pair->is_filled) {
        return 0U;
    }
    char key_ch = (pair->key_type == MFC_KEY_B) ? 'B' : 'A';
    int n = snprintf(buf, buflen,
                     "Sec %u key %c cuid %08x nt0 %08x nr0 %08x ar0 %08x "
                     "nt1 %08x nr1 %08x ar1 %08x\n",
                     (unsigned)pair->sector, key_ch, (unsigned)pair->cuid,
                     (unsigned)pair->nt0, (unsigned)pair->nr0, (unsigned)pair->ar0,
                     (unsigned)pair->nt1, (unsigned)pair->nr1, (unsigned)pair->ar1);
    if ((n <= 0) || ((size_t)n >= buflen)) {
        return 0U;   /* truncated/encoding error: never emit a partial line */
    }
    return (size_t)n;
}

void mfc_mfkey32_log_begin(mfc_mfkey32_log_t *log,
                           mfc_mfkey32_log_write_fn write, void *write_ctx,
                           uint32_t cuid, uint32_t gen)
{
    if (log == NULL) {
        return;
    }
    (void)memset(log, 0, sizeof(*log));
    log->write     = write;
    log->write_ctx = write_ctx;
    log->cuid      = cuid;
    log->gen       = gen;
    log->is_open   = true;
    log->status    = MFC_MFLOG_OK;
}

int mfc_mfkey32_log_flush_new(mfc_mfkey32_log_t *log)
{
    if ((log == NULL) || !log->is_open) {
        return -(int)MFC_MFLOG_NOT_OPEN;
    }
    if (log->status != MFC_MFLOG_OK) {
        return -(int)log->status;   /* sticky: a prior write failure blocks further attempts */
    }
    if (mfc_capture_session_cuid() != log->cuid) {
        log->status = MFC_MFLOG_WRONG_CARD;
        return -(int)log->status;
    }
    if (mfc_capture_session_gen() != log->gen) {
        log->status = MFC_MFLOG_STALE_SESSION;
        return -(int)log->status;
    }

    uint32_t total = mfc_capture_get_completed_pairs();
    if (total > MFC_CAPTURE_MAX_RECORDS) {
        total = MFC_CAPTURE_MAX_RECORDS;   /* defensive; store never actually exceeds this */
    }

    int newly_written = 0;
    for (uint32_t i = 0U; i < total; i++) {
        if (log->written[i]) {
            continue;
        }
        mfc_pair_t pair;
        if (!mfc_capture_get_pair(i, &pair)) {
            continue;   /* defensive: index no longer resolves to a filled pair */
        }
        char line[MFC_MFKEY32_LOG_LINE_MAX];
        size_t len = mfc_mfkey32_log_format_line(line, sizeof(line), &pair);
        if (len == 0U) {
            log->status = MFC_MFLOG_FORMAT_FAILED;
            return -(int)log->status;
        }
        if (!log->write(log->write_ctx, (const uint8_t *)line, len)) {
            log->status = MFC_MFLOG_WRITE_FAILED;
            return -(int)log->status;   /* this pair NOT marked written -> retryable later */
        }
        log->written[i] = true;
        log->written_count++;
        newly_written++;
    }
    return newly_written;
}

uint32_t mfc_mfkey32_log_written_count(const mfc_mfkey32_log_t *log)
{
    return (log != NULL) ? log->written_count : 0U;
}

mfc_mfkey32_log_status_t mfc_mfkey32_log_status(const mfc_mfkey32_log_t *log)
{
    if ((log == NULL) || !log->is_open) {
        return MFC_MFLOG_NOT_OPEN;
    }
    return log->status;
}
