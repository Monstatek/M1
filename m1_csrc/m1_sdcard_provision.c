/* See COPYING.txt for license details. */

/*
 * m1_sdcard_provision.c
 *
 * Implementation of the canonical SD-card folder table and the
 * mount-triggered provisioning pass. See m1_sdcard_provision.h for the
 * path constants and the full safety contract.
 *
 * M1 Project
 */

#include <stdint.h>
#include <stddef.h>
#include "ff.h"
#include "m1_sdcard_provision.h"
#include "m1_file_util.h"    /* fs_directory_ensure() -- the existing, already-
                               * idempotent stat-then-mkdir helper this reuses
                               * rather than duplicating its FatFs logic. */
#include "m1_log_debug.h"

#define M1_LOGDB_TAG "SDProv"

typedef struct
{
    const char *path;
    const char *subsystem;   /* for the log line only */
} m1_sdcard_provision_entry_t;

/* Ordered so every parent is provisioned before its children. FatFs'
 * f_mkdir() (what fs_directory_ensure() calls for a missing path) requires
 * the parent directory to already exist -- it does not create intermediate
 * directories itself -- so nfc/ must precede nfc/system/ and nfc/recover/,
 * and infrared/ must precede infrared/db/. */
static const m1_sdcard_provision_entry_t s_canonical[] =
{
    { M1_SD_DIR_NFC,         "NFC"      },
    { M1_SD_DIR_NFC_SYSTEM,  "NFC"      },
    { M1_SD_DIR_NFC_RECOVER, "NFC"      },
    { M1_SD_DIR_RFID,        "RFID"     },
    { M1_SD_DIR_SUBGHZ,      "SUBGHZ"   },
    { M1_SD_DIR_WIFI,        "WIFI"     },
    { M1_SD_DIR_BLE,         "BLE"      },
    { M1_SD_DIR_INFRARED,    "INFRARED" },
    { M1_SD_DIR_INFRARED_DB, "INFRARED" },
};
#define M1_SDPROV_COUNT ((uint8_t)(sizeof(s_canonical) / sizeof(s_canonical[0])))

static m1_sdcard_provision_outcome_t provision_one(const char *path)
{
    /* fs_directory_ensure() already implements exactly the required
     * per-folder contract: f_stat() first -- an existing directory (any
     * FAT case) returns FR_OK untouched; an existing regular file (the
     * collision case) returns FR_EXIST without deleting or overwriting
     * it; a missing path is created with f_mkdir(); any other FatFs
     * result (read-only media, disk error, missing parent, ...) passes
     * straight through. Reused here rather than reimplemented so there is
     * exactly one place that decides what "already provisioned" means. */
    FRESULT fr = fs_directory_ensure(path);

    if (fr == FR_OK)    { return M1_SDPROV_OK; }
    if (fr == FR_EXIST) { return M1_SDPROV_COLLISION; }
    return M1_SDPROV_ERROR;
}

bool m1_sdcard_provision_canonical(void)
{
    bool all_ok = true;
    uint8_t i;

    for (i = 0U; i < M1_SDPROV_COUNT; i++)
    {
        m1_sdcard_provision_outcome_t r = provision_one(s_canonical[i].path);

        /* Every entry is attempted unconditionally, regardless of any
         * earlier entry's outcome -- a parent collision (e.g. nfc/ itself
         * occupied by a file) makes its own children fail too (FatFs
         * cannot create a directory under a non-directory), but must
         * never stop rfid/, subghz/, wifi/, ble/, or infrared/ -- the
         * unrelated, unaffected folders -- from still being checked. */
        switch (r)
        {
            case M1_SDPROV_OK:
                break;

            case M1_SDPROV_COLLISION:
                all_ok = false;
                M1_LOG_E(M1_LOGDB_TAG, "%s: %s is occupied by a file -- left untouched\r\n",
                         s_canonical[i].subsystem, s_canonical[i].path);
                break;

            case M1_SDPROV_ERROR:
            default:
                all_ok = false;
                M1_LOG_E(M1_LOGDB_TAG, "%s: could not provision %s\r\n",
                         s_canonical[i].subsystem, s_canonical[i].path);
                break;
        }
    }

    return all_ok;
} /* bool m1_sdcard_provision_canonical(void) */
