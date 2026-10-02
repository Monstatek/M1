/* See COPYING.txt for license details. */

/*
 * m1_sdcard_provision.h
 *
 * Single source of truth for the M1's canonical top-level SD-card folder
 * structure, and the provisioning entry point that creates it once storage
 * is confirmed mounted and owned by firmware.
 *
 * M1 Project
 */

#ifndef M1_SDCARD_PROVISION_H_
#define M1_SDCARD_PROVISION_H_

#include <stdbool.h>

/* ---- Canonical paths: the ONE definition of each folder's location ----
 * Every production caller that creates or opens one of these folders must
 * reference these constants (directly, or through an existing per-feature
 * macro redefined in terms of them) rather than its own literal, so the
 * spelling and location can never drift between callers.
 *
 * apps/ and apps/data/ are deliberately NOT defined here yet. They are
 * deferred until the App Loader ships -- provisioning them earlier would
 * show users an empty folder for a feature that does not exist yet. */
#define M1_SD_DIR_NFC            "0:/nfc"
#define M1_SD_DIR_NFC_SYSTEM     "0:/nfc/system"
#define M1_SD_DIR_NFC_RECOVER    "0:/nfc/recover"
#define M1_SD_DIR_RFID           "0:/rfid"
#define M1_SD_DIR_SUBGHZ         "0:/subghz"
#define M1_SD_DIR_WIFI           "0:/wifi"
#define M1_SD_DIR_BLE            "0:/ble"
#define M1_SD_DIR_INFRARED       "0:/infrared"
#define M1_SD_DIR_INFRARED_DB    "0:/infrared/db"

/* Per-folder outcome, exposed for callers/tests that want more than a
 * single pass/fail bit. */
typedef enum
{
    M1_SDPROV_OK = 0,      /* already existed as a directory, or created now */
    M1_SDPROV_COLLISION,   /* path occupied by a regular file -- left untouched */
    M1_SDPROV_ERROR,       /* filesystem error (read-only media, disk error, missing parent, etc.) */
} m1_sdcard_provision_outcome_t;

/* Provision the full canonical tree.
 *
 * Safe to call ONLY once the caller has already confirmed storage is
 * mounted, owned by firmware (not USB mass storage), and the filesystem is
 * usable -- this function does not itself re-check card presence, USB MSC
 * ownership, or mount state; m1_sdcard_mount() gates all of that and is the
 * function's only production caller.
 *
 * Idempotent: safe to call on every successful mount, including repeatedly
 * on an already-provisioned card -- an existing directory (any FAT case)
 * is left untouched and counted as success, never recreated or duplicated.
 *
 * Never blocks and never aborts partway: every path in the table is
 * attempted regardless of an earlier path's outcome, so one folder's
 * failure (including a parent directory colliding with a file) can never
 * prevent the remaining, unrelated folders from being checked. Never
 * deletes, moves, or overwrites anything that already exists.
 *
 * Logs each non-OK outcome once per call. This function is only ever
 * invoked from a mount-transition point, never from a polling loop, so
 * "once per call" is also "once per mount/provisioning attempt."
 *
 * Returns true iff every path ended in M1_SDPROV_OK. A false return means
 * at least one folder needs attention (collision or error); it does not
 * mean storage is unusable, and the caller must not treat it as a mount
 * failure. */
bool m1_sdcard_provision_canonical(void);

#endif /* M1_SDCARD_PROVISION_H_ */
