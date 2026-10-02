/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fw.h
*
*  M1CP STM32 firmware-update namespace (0x20..0x2F) — READ-ONLY half.
*
*  This milestone implements ONLY FW_VALIDATE (0x20): a read-only parse and
*  validation of a staged `.m1pkg` firmware+resource package on the SD card. It
*  NEVER writes, erases, swaps, or activates flash. The flash-touching opcodes
*  FW_APPLY (0x21), FW_ACTIVATE (0x22) and FW_ABORT (0x23) are deliberately NOT
*  implemented (they answer ERR_NOT_IMPLEMENTED) and the STM32_UPDATE capability
*  is NOT advertised, until the programming/activation path can be validated on
*  physical M1 hardware. Validation success reports the package versions and sets
*  the "package staged" status flag; it never implies installation or activation.
*
*  The parser/validator (m1cp_pkg_validate) is storage-agnostic (it reads through
*  a small m1cp_pkg_reader_t), so it is exercised on the host without FatFs.
*
*  See documentation/M1CP_web_manager.md section 5 for the package format.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_FW_H_
#define M1_MANAGER_FW_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "m1_manager_fs.h"    /* m1cp_fsb_rc_t, m1cp_fs_crc32, m1cp_fs_name_valid */

#ifdef __cplusplus
extern "C" {
#endif

/* STM32 firmware-update message types (0x20..0x2F).
 * 0x20 = FW_VALIDATE (read-only .m1pkg validator, this module).
 * 0x21..0x25 = the streamed CRC32-only update flow, defined in
 * m1_manager_fwupdate.h (FW_UPDATE_BEGIN/DATA/FINISH/ACTIVATE/ABORT). */
#define M1CP_CMD_FW_VALIDATE   0x20U   /* implemented: read-only validate       */

/* ---- .m1pkg container header (little-endian), 48 bytes ---------------------
 *  off size field              notes
 *  0   4    magic              'M','1','P','K' = 0x4D 0x31 0x50 0x4B
 *  4   1    format_version     M1PKG_FORMAT_VERSION (0x01)
 *  5   3    reserved           must be present; not interpreted
 *  8   2    target_devid       STM32 DBGMCU device id (H573 = 0x0484)
 *  10  2    reserved2          alignment padding; not interpreted
 *  12  4    fw_version          major,minor,build,rc
 *  16  4    resource_version    major,minor,build,rc
 *  20  4    fw_offset          byte offset of the STM32 image within the file
 *  24  4    fw_size            image length (bytes), 4-byte aligned, > 0
 *  28  4    fw_crc32           CRC-32/ISO-HDLC over the image bytes
 *  32  4    res_offset         byte offset of the resource blob
 *  36  4    res_size           resource length (bytes), > 0
 *  40  4    res_crc32          CRC-32/ISO-HDLC over the resource bytes
 *  44  4    header_crc32       CRC-32/ISO-HDLC over bytes [0..43]
 *
 * The header is 48 bytes so the (4-byte-aligned) STM32 image can start
 * immediately after it. The two components must tile the file exactly: the
 * first begins at offset 48, they are adjacent (no gap, no overlap), and the
 * second ends exactly at end-of-file (no trailing data, no truncation).
 * ------------------------------------------------------------------------- */
#define M1PKG_MAGIC0           0x4DU  /* 'M' */
#define M1PKG_MAGIC1           0x31U  /* '1' */
#define M1PKG_MAGIC2           0x50U  /* 'P' */
#define M1PKG_MAGIC3           0x4BU  /* 'K' */
#define M1PKG_FORMAT_VERSION   0x01U
#define M1PKG_HEADER_LEN       48U
#define M1PKG_HEADER_CRC_COVER 44U    /* bytes [0..43] covered by header_crc32  */

/* Read-only package source. Both callbacks return M1CP_FSB_OK on success. The
 * validator reads bounded slices; the backend keeps whatever handle it needs in
 * `ctx`. */
typedef m1cp_fsb_rc_t (*m1cp_pkg_size_fn)(void *ctx, uint32_t *out_size);
typedef m1cp_fsb_rc_t (*m1cp_pkg_read_fn)(void *ctx, uint32_t offset,
                                          void *buf, uint32_t len, uint32_t *out_read);
typedef struct
{
    m1cp_pkg_size_fn size;   /* total staged file size in bytes                */
    m1cp_pkg_read_fn read;   /* read `len` bytes at `offset`; *out_read == len  */
    void            *ctx;
} m1cp_pkg_reader_t;

/* Target/environment the package is validated against. */
typedef struct
{
    uint16_t devid;             /* expected STM32 device id (HAL_GetDEVID)      */
    uint32_t flash_bank_size;   /* max STM32 image size (one bank)              */
} m1cp_fw_target_t;

/* Parsed/validated package descriptor (filled on success). */
typedef struct
{
    uint16_t target_devid;
    uint8_t  fw_version[4];
    uint8_t  resource_version[4];
    uint32_t fw_offset, fw_size, fw_crc32;
    uint32_t res_offset, res_size, res_crc32;
    uint32_t total_size;
} m1cp_pkg_info_t;

/*
 * Read-only validation of a staged `.m1pkg`. Returns M1CP_ERR_NONE and fills
 * `out` on success; otherwise a specific M1CP error code (nothing is written and
 * no state implies installation). Guarantees checked: container magic + format
 * version, header length + header CRC, required-component presence, overflow-safe
 * bounds, alignment, non-overlap, no-truncation, no-trailing-data, target-devid
 * match, image flash-size bounds, fw<->resource version compatibility, and
 * component CRC-32 integrity. See the .c for the exact error mapping.
 */
uint8_t m1cp_pkg_validate(const m1cp_pkg_reader_t *rd,
                          const m1cp_fw_target_t *tgt,
                          m1cp_pkg_info_t *out);

/* Firmware/resource version compatibility rule (exposed for tests): the resource
 * package must match the firmware major.minor line. */
bool m1cp_fw_versions_compatible(const uint8_t fw_ver[4], const uint8_t res_ver[4]);

/*
 * Package-source factory: resolve a validated leaf name to an opened reader and
 * the current target. Returns M1CP_ERR_NONE on success (reader/target filled),
 * or an M1CP error (NOT_FOUND / SD_UNAVAILABLE / IO). Paired with a close fn.
 * The firmware sets a FatFs-backed source at init; host tests inject their own.
 */
typedef uint8_t (*m1cp_fw_source_open_fn)(const char *name,
                                          m1cp_pkg_reader_t *rd,
                                          m1cp_fw_target_t *tgt);
typedef void    (*m1cp_fw_source_close_fn)(void);
void m1cp_fw_set_source(m1cp_fw_source_open_fn open_fn, m1cp_fw_source_close_fn close_fn);

/*
 * Handle a firmware-namespace (0x20..0x2F) command. FW_VALIDATE performs the
 * read-only validation and, on success, writes fw_version(4)|resource_version(4)
 * into `out` (capacity >= 8) and sets *out_len = 8. Every other opcode in the
 * namespace returns ERR_NOT_IMPLEMENTED (flash operations are not built). Task
 * context only.
 */
uint8_t m1cp_fw_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                       uint8_t *out, uint16_t *out_len);

/* Install the firmware FatFs-backed package source (firmware build only). */
#ifndef M1CP_HOST_TEST
void m1cp_fw_install_fatfs_source(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_FW_H_ */
