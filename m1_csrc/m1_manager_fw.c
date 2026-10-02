/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fw.c
*
*  M1CP STM32 firmware-update namespace (0x20..0x2F) — READ-ONLY half.
*  Implements FW_VALIDATE only. See m1_manager_fw.h and
*  documentation/M1CP_web_manager.md section 5.
*
*  SAFETY: nothing in this file writes, erases, swaps, or activates flash. It
*  parses and validates a staged `.m1pkg` on the SD card and reports the result.
*  FW_APPLY / FW_ACTIVATE / FW_ABORT return ERR_NOT_IMPLEMENTED so no code path
*  can claim an install or activation occurred.
*
* M1 Project
*
*/

#include <string.h>
#include "m1_manager_fw.h"
#include "m1_manager_protocol.h"   /* error codes, status-publish API, op ids */

/*************************** H E L P E R S **********************************/

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Firmware<->resource compatibility: resource pack must match the firmware
 * major.minor line (build/rc may differ). */
bool m1cp_fw_versions_compatible(const uint8_t fw_ver[4], const uint8_t res_ver[4])
{
    return (fw_ver[0] == res_ver[0]) && (fw_ver[1] == res_ver[1]);
}

/* CRC-32/ISO-HDLC over [offset, offset+size) of the staged file. Returns
 * M1CP_ERR_NONE and *crc_out, or M1CP_ERR_IO on any read shortfall/failure
 * (which also covers a staged file that shrank/changed under us). */
static uint8_t crc_over_range(const m1cp_pkg_reader_t *rd, uint32_t offset,
                              uint32_t size, uint32_t *crc_out)
{
    uint8_t  buf[256];
    uint32_t crc = 0U;
    uint32_t pos = offset;
    uint32_t remaining = size;

    while (remaining > 0U)
    {
        uint32_t chunk = (remaining < sizeof(buf)) ? remaining : (uint32_t)sizeof(buf);
        uint32_t got = 0U;
        if (rd->read(rd->ctx, pos, buf, chunk, &got) != M1CP_FSB_OK) { return M1CP_ERR_IO; }
        if (got != chunk) { return M1CP_ERR_IO; }
        crc = m1cp_fs_crc32(crc, buf, chunk);
        pos += chunk;
        remaining -= chunk;
    }
    *crc_out = crc;
    return M1CP_ERR_NONE;
}

/*************************** V A L I D A T O R ******************************/

uint8_t m1cp_pkg_validate(const m1cp_pkg_reader_t *rd,
                          const m1cp_fw_target_t *tgt,
                          m1cp_pkg_info_t *out)
{
    uint8_t  hdr[M1PKG_HEADER_LEN];
    uint32_t file_size = 0U;
    uint32_t got = 0U;
    uint32_t hdr_crc_calc, hdr_crc_decl;
    uint16_t target_devid;
    uint32_t fw_off, fw_sz, fw_crc, res_off, res_sz, res_crc;
    uint64_t last_end;
    uint32_t crc_got;
    uint8_t  rc;

    if ((rd == NULL) || (rd->size == NULL) || (rd->read == NULL) ||
        (tgt == NULL) || (out == NULL))
    {
        return M1CP_ERR_BAD_ARG;
    }

    /* --- staged-file size + header read (I/O stability) --- */
    if (rd->size(rd->ctx, &file_size) != M1CP_FSB_OK) { return M1CP_ERR_IO; }
    if (file_size < M1PKG_HEADER_LEN) { return M1CP_ERR_BAD_PAYLOAD; }   /* incomplete */

    if (rd->read(rd->ctx, 0U, hdr, M1PKG_HEADER_LEN, &got) != M1CP_FSB_OK) { return M1CP_ERR_IO; }
    if (got != M1PKG_HEADER_LEN) { return M1CP_ERR_IO; }

    /* --- container magic + format version --- */
    if ((hdr[0] != M1PKG_MAGIC0) || (hdr[1] != M1PKG_MAGIC1) ||
        (hdr[2] != M1PKG_MAGIC2) || (hdr[3] != M1PKG_MAGIC3))
    {
        return M1CP_ERR_BAD_PAYLOAD;
    }
    if (hdr[4] != M1PKG_FORMAT_VERSION) { return M1CP_ERR_INCOMPATIBLE; }

    /* --- header integrity (before trusting any field) --- */
    hdr_crc_calc = m1cp_fs_crc32(0U, hdr, M1PKG_HEADER_CRC_COVER);
    hdr_crc_decl = rd_u32(&hdr[44]);
    if (hdr_crc_calc != hdr_crc_decl) { return M1CP_ERR_INTEGRITY; }

    /* --- parse fields --- */
    target_devid = rd_u16(&hdr[8]);
    fw_off  = rd_u32(&hdr[20]);
    fw_sz   = rd_u32(&hdr[24]);
    fw_crc  = rd_u32(&hdr[28]);
    res_off = rd_u32(&hdr[32]);
    res_sz  = rd_u32(&hdr[36]);
    res_crc = rd_u32(&hdr[40]);

    /* --- required-component presence (fw AND resource) --- */
    if (fw_sz == 0U)  { return M1CP_ERR_BAD_PAYLOAD; }   /* missing firmware   */
    if (res_sz == 0U) { return M1CP_ERR_BAD_PAYLOAD; }   /* missing resources  */

    /* --- STM32 image alignment (quad-word programming needs 4-byte) --- */
    if (((fw_off & 0x3U) != 0U) || ((fw_sz & 0x3U) != 0U)) { return M1CP_ERR_BAD_PAYLOAD; }

    /* --- strict tiling: the two components exactly cover [HEADER_LEN, EOF)
     * with no gap, no overlap (catches duplicate/aliased offsets), no leading
     * gap, no trailing data and no truncation. All arithmetic in 64-bit so a
     * malicious offset/size can never wrap. --- */
    {
        uint32_t a_off, a_sz, b_off, b_sz;   /* a = earlier component */
        uint64_t a_end;
        if (fw_off <= res_off) { a_off = fw_off; a_sz = fw_sz;  b_off = res_off; b_sz = res_sz; }
        else                   { a_off = res_off; a_sz = res_sz; b_off = fw_off;  b_sz = fw_sz;  }

        if (a_off != M1PKG_HEADER_LEN) { return M1CP_ERR_BAD_PAYLOAD; }   /* leading gap/overlap */
        a_end   = (uint64_t)a_off + (uint64_t)a_sz;
        if (a_end != (uint64_t)b_off) { return M1CP_ERR_BAD_PAYLOAD; }    /* gap or overlap      */
        last_end = (uint64_t)b_off + (uint64_t)b_sz;
        if (last_end != (uint64_t)file_size) { return M1CP_ERR_BAD_PAYLOAD; } /* trailing/trunc  */
    }

    /* --- M1 hardware/target compatibility --- */
    if (target_devid != tgt->devid) { return M1CP_ERR_INCOMPATIBLE; }

    /* --- STM32 image flash-size bounds --- */
    if (fw_sz > tgt->flash_bank_size) { return M1CP_ERR_NO_SPACE; }

    /* --- firmware/resource version compatibility --- */
    if (!m1cp_fw_versions_compatible(&hdr[12], &hdr[16])) { return M1CP_ERR_INCOMPATIBLE; }

    /* --- component integrity (CRC-32 over each range) --- */
    rc = crc_over_range(rd, fw_off, fw_sz, &crc_got);
    if (rc != M1CP_ERR_NONE) { return rc; }
    if (crc_got != fw_crc) { return M1CP_ERR_INTEGRITY; }

    rc = crc_over_range(rd, res_off, res_sz, &crc_got);
    if (rc != M1CP_ERR_NONE) { return rc; }
    if (crc_got != res_crc) { return M1CP_ERR_INTEGRITY; }

    /* --- success: fill descriptor (read-only; nothing installed) --- */
    out->target_devid = target_devid;
    memcpy(out->fw_version,       &hdr[12], 4);
    memcpy(out->resource_version, &hdr[16], 4);
    out->fw_offset  = fw_off;  out->fw_size  = fw_sz;  out->fw_crc32  = fw_crc;
    out->res_offset = res_off; out->res_size = res_sz; out->res_crc32 = res_crc;
    out->total_size = file_size;
    return M1CP_ERR_NONE;
}

/*************************** C O M M A N D **********************************/

static m1cp_fw_source_open_fn  s_src_open  = NULL;
static m1cp_fw_source_close_fn s_src_close = NULL;

void m1cp_fw_set_source(m1cp_fw_source_open_fn open_fn, m1cp_fw_source_close_fn close_fn)
{
    s_src_open  = open_fn;
    s_src_close = close_fn;
}

/* FW_VALIDATE: name_len(1)|name -> fw_version(4)|resource_version(4) */
static uint8_t fw_validate(const uint8_t *payload, uint16_t plen,
                           uint8_t *out, uint16_t *out_len)
{
    uint8_t  nlen;
    char     name[M1CP_FS_NAME_MAX + 1];
    m1cp_pkg_reader_t rd;
    m1cp_fw_target_t  tgt;
    m1cp_pkg_info_t   info;
    uint8_t  e;

    if (plen < 1U) { return M1CP_ERR_BAD_PAYLOAD; }
    nlen = payload[0];
    if ((nlen == 0U) || ((uint16_t)(1U + nlen) != plen)) { return M1CP_ERR_BAD_PAYLOAD; }
    if (!m1cp_fs_name_valid(&payload[1], nlen)) { return M1CP_ERR_BAD_ARG; }
    memcpy(name, &payload[1], nlen);
    name[nlen] = '\0';

    if (s_src_open == NULL) { return M1CP_ERR_BAD_STATE; }

    /* read-only: domain STM32, verifying; never programming/reboot */
    m1cp_status_set(M1CP_DOMAIN_STM32, M1CP_OP_VERIFYING, 0U);

    e = s_src_open(name, &rd, &tgt);
    if (e != M1CP_ERR_NONE)
    {
        if (s_src_close != NULL) { s_src_close(); }
        m1cp_status_set_flag(M1CP_STF_FW_PKG_STAGED, false);
        m1cp_status_set_error(e);
        m1cp_status_set(M1CP_DOMAIN_STM32, M1CP_OP_IDLE, 0U);
        return e;
    }

    e = m1cp_pkg_validate(&rd, &tgt, &info);
    if (s_src_close != NULL) { s_src_close(); }

    if (e != M1CP_ERR_NONE)
    {
        m1cp_status_set_flag(M1CP_STF_FW_PKG_STAGED, false);
        m1cp_status_set_error(e);
        m1cp_status_set(M1CP_DOMAIN_STM32, M1CP_OP_IDLE, 0U);
        return e;
    }

    /* success — read-only validation only; DOES NOT install or activate */
    m1cp_status_set_error(M1CP_ERR_NONE);
    m1cp_status_set_flag(M1CP_STF_FW_PKG_STAGED, true);
    m1cp_status_set(M1CP_DOMAIN_STM32, M1CP_OP_IDLE, 0U);

    memcpy(&out[0], info.fw_version, 4);
    memcpy(&out[4], info.resource_version, 4);
    *out_len = 8U;
    return M1CP_ERR_NONE;
}

uint8_t m1cp_fw_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                       uint8_t *out, uint16_t *out_len)
{
    *out_len = 0U;

    if (msg_type == M1CP_CMD_FW_VALIDATE)
    {
        uint8_t e = fw_validate(payload, plen, out, out_len);
        if (e != M1CP_ERR_NONE) { *out_len = 0U; }
        return e;
    }

    /* FW_APPLY / FW_ACTIVATE / FW_ABORT and any other 0x2x opcode: the
     * flash-touching operations are intentionally not built. Return the honest
     * "not implemented" so nothing can report a false install/activation. */
    return M1CP_ERR_NOT_IMPLEMENTED;
}

/*************************** F A T F S   S O U R C E ************************/

#ifndef M1CP_HOST_TEST
#include "ff.h"
#include "stm32h5xx_hal.h"       /* HAL_GetDEVID                              */
#include "m1_sdcard.h"           /* m1_sd_detected, m1_sdcard_get_status      */
#include "m1_storage.h"          /* usbmsc_sd_enable                          */
#include "m1_fw_update_bl.h"     /* M1_FLASH_BANK_SIZE                        */

static FIL     s_pkg_file;
static uint8_t s_pkg_open = 0U;

static m1cp_fsb_rc_t fatfs_pkg_size(void *ctx, uint32_t *out_size)
{
    (void)ctx;
    if (!s_pkg_open) { return M1CP_FSB_ERR_IO; }
    if (out_size != NULL) { *out_size = (uint32_t)f_size(&s_pkg_file); }
    return M1CP_FSB_OK;
}

static m1cp_fsb_rc_t fatfs_pkg_read(void *ctx, uint32_t offset,
                                    void *buf, uint32_t len, uint32_t *out_read)
{
    UINT    br = 0U;
    FRESULT fr;
    (void)ctx;
    if (!s_pkg_open) { return M1CP_FSB_ERR_IO; }
    fr = f_lseek(&s_pkg_file, (FSIZE_t)offset);
    if (fr != FR_OK) { return M1CP_FSB_ERR_IO; }
    fr = f_read(&s_pkg_file, buf, (UINT)len, &br);
    if (fr != FR_OK) { return M1CP_FSB_ERR_IO; }
    if (out_read != NULL) { *out_read = (uint32_t)br; }
    return M1CP_FSB_OK;
}

/* Build <M1CP_FS_DIR>/<name> into `path` (name already validated as a safe leaf). */
static void fatfs_pkg_path(const char *name, char *path)
{
    size_t dlen = strlen(M1CP_FS_DIR);
    size_t nlen = strlen(name);
    memcpy(path, M1CP_FS_DIR, dlen);
    path[dlen] = '/';
    memcpy(&path[dlen + 1U], name, nlen + 1U);
}

static uint8_t fatfs_source_open(const char *name, m1cp_pkg_reader_t *rd,
                                 m1cp_fw_target_t *tgt)
{
    char    path[M1CP_FS_PATH_MAX];
    FRESULT fr;

    /* refuse unless the device owns a healthy, mounted card */
    if ((usbmsc_sd_enable != 0U) ||
        (m1_sd_detected() == 0U) ||
        (m1_sdcard_get_status() != SD_access_OK))
    {
        return M1CP_ERR_SD_UNAVAILABLE;
    }

    fatfs_pkg_path(name, path);
    if (s_pkg_open) { (void)f_close(&s_pkg_file); s_pkg_open = 0U; }
    fr = f_open(&s_pkg_file, path, FA_READ);
    if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH)) { return M1CP_ERR_NOT_FOUND; }
    if (fr != FR_OK) { return M1CP_ERR_IO; }
    s_pkg_open = 1U;

    rd->size = fatfs_pkg_size;
    rd->read = fatfs_pkg_read;
    rd->ctx  = NULL;

    tgt->devid           = (uint16_t)HAL_GetDEVID();
    tgt->flash_bank_size = (uint32_t)M1_FLASH_BANK_SIZE;
    return M1CP_ERR_NONE;
}

static void fatfs_source_close(void)
{
    if (s_pkg_open) { (void)f_close(&s_pkg_file); s_pkg_open = 0U; }
}

void m1cp_fw_install_fatfs_source(void)
{
    m1cp_fw_set_source(fatfs_source_open, fatfs_source_close);
}
#endif /* !M1CP_HOST_TEST */
