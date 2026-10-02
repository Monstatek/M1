/*
 * mfc_harvest_storage.h - FatFs/SD binding for the .m1h harvester
 *                         (Harvester Increment 2).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DEVICE-ONLY glue between the hardware-agnostic serializer (mfc_harvest.*) and
 * the microSD card via the project's FatFs wrappers (m1_fb_* / fs_*). It maps
 * the serializer's 512-byte flush callback to f_write, writing a .m1h file under
 * 0:/NFC/recover/. No RF/radio dependency (nfc_poller.c and the RFAL layer are
 * NOT touched). Not host-testable (requires FatFs + an SD card); verified by the
 * ARM build and the on-device mock trigger.
 */
#ifndef MFC_HARVEST_STORAGE_H
#define MFC_HARVEST_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ff.h"          /* FIL */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MFC_HS_OK        =  0,
    MFC_HS_ERR_ARG   = -1,   /* NULL / bad uid_len                            */
    MFC_HS_ERR_SD    = -2,   /* SD absent or insufficient free space          */
    MFC_HS_ERR_DIR   = -3,   /* directory create/ensure failed                */
    MFC_HS_ERR_OPEN  = -4,   /* f_open (new file) failed                      */
    MFC_HS_ERR_SERIAL = -5,  /* serializer reported a non-OK status           */
} mfc_hs_result_t;

/* Storage context (caller-allocated; holds the FatFs file handle + path). */
typedef struct {
    FIL     file;
    uint8_t open;
    char    path[64];
} mfc_harvest_storage_t;

/*
 * Ensure 0:/NFC and 0:/NFC/recover exist, build the capture path
 *   0:/NFC/recover/<uid-hex>_<ticks-hex>.m1h
 * and open it for writing. Returns MFC_HS_OK, or a negative mfc_hs_result_t on
 * SD/dir/open failure. On success the path is stored in st->path.
 */
int mfc_harvest_storage_start(mfc_harvest_storage_t *st, const uint8_t *uid,
                              uint8_t uid_len, uint32_t ticks);

/*
 * Flush callback (matches mfc_harvest_flush_fn): write `len` bytes via f_write.
 * ctx must be the mfc_harvest_storage_t*. Returns true iff all bytes were
 * written; a short write / error returns false so the serializer latches
 * FLUSH_FAILED and the capture aborts safely.
 */
bool mfc_harvest_storage_flush(void *ctx, const uint8_t *bytes, size_t len);

/* Close the file (safe to call once; idempotent). */
int mfc_harvest_storage_close(mfc_harvest_storage_t *st);

/*
 * Debug trigger (mock hardware): write a mock .m1h (one MFKey64 + one Nested
 * record) to SD, end to end, with NO radio. Returns 0 on success; writes a
 * human-readable status line into msg[msglen]. Wired to the CLI: `m1_mtest 91`.
 */
int mfc_harvest_mock_run(char *msg, size_t msglen);

#ifdef __cplusplus
}
#endif

#endif /* MFC_HARVEST_STORAGE_H */
