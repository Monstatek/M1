/*
 * mfc_mfkey32_log.h - Extract Keys nonce-pair export (Phase 4).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Persists COMPLETED mfc_capture.h pairs (never incomplete ones) as plain
 * text lines byte-for-byte compatible with Flipper Zero's own on-device
 * capture log, confirmed directly against Flipper source this session:
 *   _reference/flipperzero-firmware-dev/applications/main/nfc/helpers/mfkey32_logger.c
 * Flipper's format is deliberately simple: one line per completed pair,
 * built with
 *   furi_string_printf("Sec %d key %c cuid %08lx nt0 %08lx nr0 %08lx "
 *                       "ar0 %08lx nt1 %08lx nr1 %08lx ar1 %08lx\n", ...)
 * (mfkey32_logger.c:128-139), no header, no version marker, no per-record
 * framing, appended forever to one fixed filename
 * (`FSOM_OPEN_APPEND`, mfkey32_logger.c:120) -- this is exactly what the
 * established desktop/companion mfkey32 tooling (referenced in Flipper's
 * own UI as "r.flipper.net/nfc-tools") already parses. M1 reproduces that
 * exact line syntax so a copied-off mfkey32.log needs no reformatting.
 * Flipper itself performs no deduplication, no session/card isolation, and
 * no on-device QR encoding of this data (confirmed: no qrcode/QR code path
 * exists anywhere in its nfc app -- its "QR" is a single static icon
 * pointing at a companion-app download URL, not a per-capture export). M1
 * adds three things beyond Flipper's own behavior, all layered on top
 * without altering the emitted line syntax:
 *   - per-export-session dedup (a pair is written at most once per
 *     mfc_mfkey32_log_begin()..close, tracked by mfc_capture's own stable
 *     pair index -- see mfc_mfkey32_log_flush_new())
 *   - wrong-card / stale-session isolation: flush_new() refuses to write
 *     anything once mfc_capture's own cuid/generation (mfc_capture.h) has
 *     moved on from what this export session was opened for
 *   - a failed write never marks its pair written, so a later retry can
 *     still export it, and the failure is latched so no further writes are
 *     attempted until a fresh mfc_mfkey32_log_begin()
 * QR encoding and any on-device solving remain out of scope here (Phase 5
 * / not-yet-linked solver, respectively) -- this module only ever produces
 * the same plain-text line Flipper already produces.
 *
 * Host-testable core: no FatFs/HAL dependency, a caller-supplied write
 * callback (mirrors mfc_harvest.h's mfc_harvest_flush_fn). Must only ever
 * be called from worker/UI context, never from the RF-critical frame path
 * (mfc_detect_service_frame() and friends) or an ISR.
 */
#ifndef MFC_MFKEY32_LOG_H
#define MFC_MFKEY32_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mfc_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "Sec 255 key B cuid ffffffff nt0 ffffffff nr0 ffffffff ar0 ffffffff
 *  nt1 ffffffff nr1 ffffffff ar1 ffffffff\n" = 106 chars + NUL; rounded up. */
#define MFC_MFKEY32_LOG_LINE_MAX 128U

typedef bool (*mfc_mfkey32_log_write_fn)(void *ctx, const uint8_t *bytes, size_t len);

typedef enum {
    MFC_MFLOG_OK = 0,
    MFC_MFLOG_NOT_OPEN,      /* flush_new() called before begin()              */
    MFC_MFLOG_WRONG_CARD,    /* mfc_capture's cuid no longer matches this session*/
    MFC_MFLOG_STALE_SESSION, /* mfc_capture's generation moved on (reset/invalidate) */
    MFC_MFLOG_FORMAT_FAILED, /* line formatting failed (unrecoverable, defensive)*/
    MFC_MFLOG_WRITE_FAILED,  /* write callback failed; sticky until re-begin()   */
} mfc_mfkey32_log_status_t;

typedef struct {
    mfc_mfkey32_log_write_fn write;
    void    *write_ctx;
    uint32_t cuid;
    uint32_t gen;
    bool     is_open;
    bool     written[MFC_CAPTURE_MAX_RECORDS];
    uint32_t written_count;
    mfc_mfkey32_log_status_t status;
} mfc_mfkey32_log_t;

/* Bind an export session to mfc_capture's CURRENT cuid/generation (read by
 * the caller via mfc_capture_session_cuid()/mfc_capture_session_gen() at
 * the moment it starts exporting). Clears all dedup/status state. Performs
 * no I/O. */
void mfc_mfkey32_log_begin(mfc_mfkey32_log_t *log,
                           mfc_mfkey32_log_write_fn write, void *write_ctx,
                           uint32_t cuid, uint32_t gen);

/* Writes one Flipper-format text line for every completed pair
 * (mfc_capture_get_completed_pairs()) not yet written by THIS session.
 * Refuses to write anything (returns a negative status, no I/O attempted)
 * if mfc_capture has since moved to a different card or generation than
 * this session was opened for -- see mfc_mfkey32_log.h's file header.
 * On a write failure the failing pair is left unwritten (retryable later)
 * and the failure is latched; every subsequent call returns it immediately
 * until a fresh mfc_mfkey32_log_begin(). Returns the count of NEW lines
 * written (>= 0) on success. */
int mfc_mfkey32_log_flush_new(mfc_mfkey32_log_t *log);

uint32_t mfc_mfkey32_log_written_count(const mfc_mfkey32_log_t *log);
mfc_mfkey32_log_status_t mfc_mfkey32_log_status(const mfc_mfkey32_log_t *log);

/* Formats one completed pair into buf as the exact Flipper-compatible line
 * (including trailing '\n' and NUL), with no I/O or dedup side effects --
 * used directly by format-compatibility tests and internally by
 * flush_new(). Returns the formatted length excluding the NUL, or 0 if
 * buf is too small, pair is NULL, or pair->is_filled is false (an
 * incomplete pair is never formatted -- there is nothing valid to emit). */
size_t mfc_mfkey32_log_format_line(char *buf, size_t buflen, const mfc_pair_t *pair);

#ifdef __cplusplus
}
#endif

#endif /* MFC_MFKEY32_LOG_H */
