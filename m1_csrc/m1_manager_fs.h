/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fs.h
*
*  M1CP "Add Files" — safe Manager-driven file transfer to the SD card
*  (filesystem/content namespace 0x40..0x4F). Lifecycle:
*    FS_BEGIN -> FS_DATA* -> FS_COMMIT   (or FS_ABORT / timeout)
*  Data is streamed to a temporary file, f_sync'd, CRC-32 verified, then
*  atomically renamed into its final path under the SD card root. The browser
*  supplies a validated relative path (a leaf filename, optionally preceded by
*  one or more subdirectory components, e.g. "infrared/db/tv.ir") that is
*  always resolved under the root; every component is validated the same way
*  a bare leaf name always was (charset [A-Za-z0-9._-], no "."/".."/all-dots,
*  no empty component, no leading/trailing separator), so escaping the SD
*  card via this path is structurally impossible even though it may be more
*  than one segment long. The top-level component may not be "WIFI"
*  (case-insensitive) - that name is reserved for the M1's own Wi-Fi capture
*  storage (DRIVE0_WIFI in m1_wifi.c) and must never be written to or
*  overwritten by a Manager-driven transfer.
*
*  The transfer state machine, filename validation, chunk ordering, integrity
*  check and duplicate/replacement policy are storage-backend-agnostic (a small
*  vtable, see m1cp_fs_backend_t), so they are exercised by the host test harness
*  without FatFs. The firmware provides a FatFs-backed backend.
*
*  See documentation/M1CP_web_manager.md section 4.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_FS_H_
#define M1_MANAGER_FS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* filesystem/content message types (0x40..0x4F) */
#define M1CP_CMD_FS_BEGIN     0x40U
#define M1CP_CMD_FS_DATA      0x41U
#define M1CP_CMD_FS_COMMIT    0x42U
#define M1CP_CMD_FS_ABORT     0x43U
#define M1CP_CMD_FS_STAT      0x44U
#define M1CP_CMD_FS_DELETE    0x46U

/* Manager destination root and temp suffix. Deliberately the SD card's own
 * root ("0:") - see the file-level comment above for why this is still safe
 * (path validation + the WIFI reserved-name guard), and m1_wifi.c's
 * DRIVE0_WIFI for the one name this must never collide with. */
#define M1CP_FS_DIR           "0:"
#define M1CP_FS_TMP_SUFFIX    ".part"
#define M1CP_FS_NAME_MAX      64U           /* max relative-path length (may include '/' subdirs) */
#define M1CP_FS_PATH_MAX      96U           /* dir + '/' + name + suffix + NUL    */
#define M1CP_FS_CHUNK_MAX     480U          /* max data bytes per FS_DATA payload */

/* Storage backend result codes (backend-neutral; mapped to M1CP errors). */
typedef enum
{
    M1CP_FSB_OK = 0,
    M1CP_FSB_ERR_IO,          /* generic I/O failure                            */
    M1CP_FSB_ERR_NOT_FOUND,   /* file not found                                 */
    M1CP_FSB_ERR_NO_SPACE     /* out of space                                   */
} m1cp_fsb_rc_t;

/*
 * Storage backend vtable. All paths are absolute ("0:/...") and produced
 * internally from a validated leaf name; the backend never parses client input.
 * A single file handle is open at a time (the active transfer's temp file).
 */
typedef struct
{
    /* SD readiness/ownership. Returns true when the device may write: card
     * present, filesystem healthy, and NOT owned by the USB-MSC host. When it
     * returns false, *host_owned is set to 1 if the reason is host ownership
     * (so the caller can distinguish "busy elsewhere" from "no card"). */
    bool          (*ready)(uint8_t *host_owned);

    /* Free space in bytes on the volume (best-effort; 0 if unknown). */
    uint64_t      (*free_bytes)(void);

    /* Ensure ONE directory level exists (mkdir if absent; not recursive - the
     * caller walks a nested path level by level, so each call's parent is
     * already known to exist). */
    m1cp_fsb_rc_t (*ensure_dir)(const char *dir_path);

    /* Create/truncate `path` for writing and keep it open as the active file. */
    m1cp_fsb_rc_t (*open_w)(const char *path);
    /* Append `len` bytes to the active file; *written gets the count written. */
    m1cp_fsb_rc_t (*write)(const void *data, uint32_t len, uint32_t *written);
    /* Flush the active file to media. */
    m1cp_fsb_rc_t (*sync)(void);
    /* Close the active file (no-op if none open). */
    m1cp_fsb_rc_t (*close)(void);

    /* CRC-32 (ISO-HDLC / zlib: poly 0xEDB88320, init/xorout 0xFFFFFFFF,
     * reflected) over the whole file at `path`. */
    m1cp_fsb_rc_t (*crc32_file)(const char *path, uint32_t *crc);

    /* Stat: set *exists and, if it exists, *size. */
    m1cp_fsb_rc_t (*stat)(const char *path, uint8_t *exists, uint32_t *size);
    /* Delete `path`. */
    m1cp_fsb_rc_t (*remove)(const char *path);
    /* Rename `from` -> `to` (atomic replace; caller removes an existing `to`
     * first when overwrite is permitted). */
    m1cp_fsb_rc_t (*rename)(const char *from, const char *to);
} m1cp_fs_backend_t;

/* Install the storage backend (call once at init, before any FS command). */
void m1cp_fs_init(const m1cp_fs_backend_t *backend);

/*
 * Handle one filesystem-namespace command. `payload`/`plen` are the request
 * payload. On success writes any response payload into `out` (capacity must be
 * >= 8) and sets *out_len, returning M1CP_ERR_NONE (the caller sends a RESPONSE,
 * or RESPONSE|ACK when *out_len == 0). On failure returns a non-zero M1CP error
 * code (the caller sends RESPONSE|NACK with it) and sets *out_len = 0.
 * Task context only.
 */
uint8_t m1cp_fs_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                       uint8_t *out, uint16_t *out_len);

/* Abort any in-progress transfer and release the temp file (called on session
 * end / link-down so no orphan .part file survives). */
void m1cp_fs_reset(void);

/* True while a transfer is in progress (for GET_STATUS transfer_active flag). */
bool m1cp_fs_busy(void);

/* CRC-32 (ISO-HDLC/zlib) helper, exposed for the FatFs backend and host tests. */
uint32_t m1cp_fs_crc32(uint32_t crc, const uint8_t *data, uint32_t len);

/* Validate a client relative path against the Add-Files rules: length 1..64,
 * one or more '/'-separated components, each charset [A-Za-z0-9._-] and not
 * "."/".."/all-dots; no empty component and no leading/trailing '/'. Returns
 * true if acceptable. Exposed for host tests. */
bool m1cp_fs_name_valid(const uint8_t *name, uint16_t len);

/* Install the firmware FatFs-backed backend (defined in the firmware build). */
#ifndef M1CP_HOST_TEST
void m1cp_fs_install_fatfs_backend(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_FS_H_ */
