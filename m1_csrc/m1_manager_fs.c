/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fs.c
*
*  M1CP "Add Files" transfer to the SD card root (namespace 0x40..0x4F). See
*  m1_manager_fs.h and documentation/M1CP_web_manager.md section 4.
*
*  Safety model:
*    - a single active transfer at a time; the browser supplies only a validated
*      relative path (a leaf filename optionally preceded by '/'-separated
*      subdirectory components), and because every component is validated the
*      same way a bare leaf name always was (no "."/".."/all-dots/empty
*      component, no leading/trailing separator), escaping the SD card via
*      this path is structurally impossible;
*    - the top-level path component may not be "WIFI" (case-insensitive) -
*      reserved for the M1's own Wi-Fi capture storage (DRIVE0_WIFI in
*      m1_wifi.c), so a Manager transfer can never write into or over it;
*    - every op requires a device-owned, healthy card (refused while USB-MSC owns
*      the SD);
*    - data goes to a temporary <leaf>.part file; FS_COMMIT f_sync's, CRC-32
*      verifies, then atomically renames onto the final name. No success is
*      reported before data is synchronized and verified;
*    - any error / abort / session-end closes and unlinks the temp file, so no
*      orphan .part survives.
*
*  The state machine is storage-backend-agnostic (m1cp_fs_backend_t) so it is
*  exercised on the host without FatFs.
*
* M1 Project
*
*/

#include <string.h>
#include "m1_manager_fs.h"
#include "m1_manager_protocol.h"   /* error codes, status-publish API, op ids */

/*************************** S T A T E ***************************************/

static const m1cp_fs_backend_t *s_be = NULL;

static uint8_t   s_active     = 0U;      /* transfer in progress               */
static uint16_t  s_session    = 0U;      /* active session id (nonzero)        */
static uint16_t  s_session_ctr = 0U;     /* monotonic session id source        */
static uint32_t  s_total      = 0U;      /* declared total size                */
static uint32_t  s_received   = 0U;      /* bytes written so far               */
static uint32_t  s_crc_expect = 0U;      /* declared CRC-32                    */
static uint8_t   s_overwrite  = 0U;      /* overwrite existing final allowed   */
static char      s_name[M1CP_FS_NAME_MAX + 1];
static char      s_tmp_path[M1CP_FS_PATH_MAX];
static char      s_final_path[M1CP_FS_PATH_MAX];

/*************************** C R C - 3 2 ************************************/

/* CRC-32/ISO-HDLC (zlib): poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF.
 * Composable: crc32(0, a, na) then crc32(crc, b, nb) equals crc32 over a||b. */
uint32_t m1cp_fs_crc32(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t  bit;

    crc = crc ^ 0xFFFFFFFFUL;
    for (i = 0U; i < len; i++)
    {
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/*************************** H E L P E R S **********************************/

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Validate a client relative path: one or more '/'-separated components, each
 * validated exactly like a bare leaf name always was (charset [A-Za-z0-9._-],
 * not all-dots, no ".." anywhere in the component). No leading/trailing '/'
 * and no empty ("//") component, so there is no way to spell an absolute path
 * or a parent reference - escaping the SD card via this path is structurally
 * impossible. The top-level component may also not be "WIFI" (case-
 * insensitive): that name is reserved for the M1's own Wi-Fi capture storage
 * (DRIVE0_WIFI in m1_wifi.c), which a Manager transfer must never touch. */
bool m1cp_fs_name_valid(const uint8_t *name, uint16_t len)
{
    uint16_t i;
    uint16_t comp_start = 0U;
    uint8_t  comp_nondot = 0U;

    if ((name == NULL) || (len == 0U) || (len > M1CP_FS_NAME_MAX))
    {
        return false;
    }
    if ((name[0] == '/') || (name[len - 1U] == '/'))
    {
        return false;       /* no leading/trailing separator */
    }
    for (i = 0U; i < len; i++)
    {
        uint8_t c = name[i];
        if (c == '/')
        {
            if (i == comp_start)   { return false; }  /* "//" empty component */
            if (comp_nondot == 0U) { return false; }   /* component was all dots */
            comp_start = (uint16_t)(i + 1U);
            comp_nondot = 0U;
            continue;
        }
        bool ok = ((c >= 'A') && (c <= 'Z')) ||
                  ((c >= 'a') && (c <= 'z')) ||
                  ((c >= '0') && (c <= '9')) ||
                  (c == '.') || (c == '_') || (c == '-');
        if (!ok)
        {
            return false;   /* rejects '\\', ':', spaces, control, etc. */
        }
        if (c != '.') { comp_nondot = 1U; }
        /* reject any ".." adjacent pair within the SAME component defensively
         * (belt and suspenders; comp_start bounds this to one component) */
        if ((c == '.') && (i > comp_start) && (name[i - 1U] == '.'))
        {
            return false;
        }
    }
    if (comp_nondot == 0U)
    {
        return false;       /* final component was "", ".", "..", "..." etc. */
    }

    /* Reserved: the top-level component may not be "WIFI" (case-insensitive) -
     * see the function comment above. */
    {
        uint16_t first_len = 0U;
        while ((first_len < len) && (name[first_len] != '/')) { first_len++; }
        if ((first_len == 4U) &&
            ((name[0] == 'W') || (name[0] == 'w')) &&
            ((name[1] == 'I') || (name[1] == 'i')) &&
            ((name[2] == 'F') || (name[2] == 'f')) &&
            ((name[3] == 'I') || (name[3] == 'i')))
        {
            return false;
        }
    }
    return true;
}

/* Build <name> and <name>.part (both under the SD root) into the static buffers. */
static void fs_build_paths(void)
{
    size_t dlen = strlen(M1CP_FS_DIR);
    size_t nlen = strlen(s_name);
    size_t o = 0U;

    memcpy(s_final_path, M1CP_FS_DIR, dlen);       o = dlen;
    s_final_path[o++] = '/';
    memcpy(&s_final_path[o], s_name, nlen);        o += nlen;
    s_final_path[o] = '\0';

    memcpy(s_tmp_path, s_final_path, o);
    memcpy(&s_tmp_path[o], M1CP_FS_TMP_SUFFIX, strlen(M1CP_FS_TMP_SUFFIX) + 1U);
}

/* Ensure every NAMED directory level between M1CP_FS_DIR and the leaf of
 * `final_path` exists, one ensure_dir() call per level (each call's parent is
 * already known to exist, since we walk left to right). `final_path` is
 * always M1CP_FS_DIR + '/' + (validated relative path), so this only ever
 * creates directories under the single permitted tree.
 *
 * Deliberately does NOT call ensure_dir() on the bare M1CP_FS_DIR root itself:
 * a mounted drive's root always exists, and FatFs's f_stat() on a bare root
 * path (e.g. "0:", no name component beneath it) returns FR_INVALID_NAME (see
 * f_stat -> follow_path -> NS_NONAME in ff.c), not FR_OK - fs_directory_ensure
 * doesn't special-case that, so calling ensure_dir(M1CP_FS_DIR) unconditionally
 * turned into a guaranteed M1CP_ERR_IO on every single call once M1CP_FS_DIR
 * became the bare root ("0:") instead of a real subdirectory name
 * ("0:/MONSTA") - confirmed on-device (every FS_STAT/FS_BEGIN failed with IO
 * immediately, before any real file/directory work happened). */
static m1cp_fsb_rc_t fs_ensure_dirs_for(const char *final_path)
{
    char         buf[M1CP_FS_PATH_MAX];
    size_t       dlen = strlen(M1CP_FS_DIR);
    size_t       i;
    m1cp_fsb_rc_t rc;

    for (i = dlen + 1U; final_path[i] != '\0'; i++)
    {
        if (final_path[i] == '/')
        {
            memcpy(buf, final_path, i);
            buf[i] = '\0';
            rc = s_be->ensure_dir(buf);
            if (rc != M1CP_FSB_OK) { return rc; }
        }
    }
    return M1CP_FSB_OK;
}

/* Close + unlink the temp file, clear transfer state, and mark the op failed. */
static void fs_fail_cleanup(uint8_t err)
{
    if (s_be != NULL)
    {
        (void)s_be->close();
        (void)s_be->remove(s_tmp_path);
    }
    s_active = 0U;
    s_session = 0U;
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, false);
    m1cp_status_set_error(err);
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_FAILED, 0U);
}

/* Copy+validate the leaf name from a name_len|name request prefix.
 * Returns the number of prefix bytes consumed (1 + name_len), or 0 on error. */
static uint16_t fs_take_name(const uint8_t *payload, uint16_t plen)
{
    uint16_t nlen;
    if (plen < 1U) { return 0U; }
    nlen = payload[0];
    if ((nlen == 0U) || ((uint16_t)(1U + nlen) > plen)) { return 0U; }
    if (!m1cp_fs_name_valid(&payload[1], nlen)) { return 0U; }
    memcpy(s_name, &payload[1], nlen);
    s_name[nlen] = '\0';
    return (uint16_t)(1U + nlen);
}

/*************************** C O M M A N D S ********************************/

/* FS_BEGIN: name_len(1)|name|total(4)|crc32(4)|flags(1) */
static uint8_t fs_begin(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    uint16_t consumed;
    uint8_t  host_owned = 0U;
    uint8_t  exists = 0U;
    uint32_t dummy = 0U;
    uint64_t freeb;
    uint8_t  flags;

    if (s_active) { return M1CP_ERR_BUSY; }

    consumed = fs_take_name(payload, plen);
    if (consumed == 0U) { return M1CP_ERR_BAD_ARG; }
    if ((uint16_t)(consumed + 9U) != plen) { return M1CP_ERR_BAD_PAYLOAD; }

    s_total      = rd_u32(&payload[consumed]);
    s_crc_expect = rd_u32(&payload[consumed + 4U]);
    flags        = payload[consumed + 8U];
    s_overwrite  = (uint8_t)(flags & 0x01U);

    if ((s_be == NULL) || (!s_be->ready(&host_owned))) { return M1CP_ERR_SD_UNAVAILABLE; }

    fs_build_paths();
    if (fs_ensure_dirs_for(s_final_path) != M1CP_FSB_OK) { return M1CP_ERR_IO; }

    /* duplicate/replacement policy */
    if (s_be->stat(s_final_path, &exists, &dummy) == M1CP_FSB_OK)
    {
        if (exists && (s_overwrite == 0U)) { return M1CP_ERR_BAD_STATE; }
    }

    /* capacity precheck (best-effort: 0 means unknown -> proceed) */
    freeb = s_be->free_bytes();
    if ((freeb != 0U) && ((uint64_t)s_total > freeb)) { return M1CP_ERR_NO_SPACE; }

    if (s_be->open_w(s_tmp_path) != M1CP_FSB_OK) { return M1CP_ERR_IO; }

    s_received = 0U;
    if (++s_session_ctr == 0U) { s_session_ctr = 1U; }
    s_session  = s_session_ctr;
    s_active   = 1U;

    m1cp_status_set_error(M1CP_ERR_NONE);
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, true);
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_RECEIVING, 0U);

    wr_u16(&out[0], (uint16_t)M1CP_FS_CHUNK_MAX);
    wr_u16(&out[2], s_session);
    *olen = 4U;
    return M1CP_ERR_NONE;
}

/* FS_DATA: session(2)|offset(4)|data(N) */
static uint8_t fs_data(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    uint16_t session;
    uint32_t offset;
    uint16_t n;
    uint32_t written = 0U;
    m1cp_fsb_rc_t rc;

    if (!s_active) { return M1CP_ERR_BAD_STATE; }
    if (plen < 6U) { return M1CP_ERR_BAD_PAYLOAD; }

    session = rd_u16(&payload[0]);
    offset  = rd_u32(&payload[2]);
    n       = (uint16_t)(plen - 6U);

    if (session != s_session)          { return M1CP_ERR_BAD_ARG; }
    if (n > M1CP_FS_CHUNK_MAX)         { return M1CP_ERR_BAD_ARG; }
    if (offset != s_received)          { return M1CP_ERR_BAD_ARG; }   /* strict order */
    if (((uint64_t)s_received + n) > s_total) { return M1CP_ERR_BAD_ARG; } /* overrun */

    rc = s_be->write(&payload[6], n, &written);
    if (rc == M1CP_FSB_ERR_NO_SPACE) { fs_fail_cleanup(M1CP_ERR_NO_SPACE); return M1CP_ERR_NO_SPACE; }
    if ((rc != M1CP_FSB_OK) || (written != n)) { fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO; }

    s_received += n;
    {
        uint8_t pct = (s_total != 0U)
                    ? (uint8_t)(((uint64_t)s_received * 100U) / s_total) : 0U;
        m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_RECEIVING, pct);
    }

    wr_u32(&out[0], s_received);
    *olen = 4U;
    return M1CP_ERR_NONE;
}

/* FS_COMMIT: session(2) */
static uint8_t fs_commit(const uint8_t *payload, uint16_t plen)
{
    uint16_t session;
    uint32_t got = 0U;
    uint8_t  exists = 0U;
    uint32_t dummy = 0U;

    if (!s_active) { return M1CP_ERR_BAD_STATE; }
    if (plen < 2U) { return M1CP_ERR_BAD_PAYLOAD; }
    session = rd_u16(&payload[0]);
    if (session != s_session) { return M1CP_ERR_BAD_ARG; }

    if (s_received != s_total) { fs_fail_cleanup(M1CP_ERR_BAD_STATE); return M1CP_ERR_BAD_STATE; }

    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_VERIFYING, 100U);

    if (s_be->sync()  != M1CP_FSB_OK) { fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO; }
    if (s_be->close() != M1CP_FSB_OK) { fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO; }

    if (s_be->crc32_file(s_tmp_path, &got) != M1CP_FSB_OK)
    {
        fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO;
    }
    if (got != s_crc_expect) { fs_fail_cleanup(M1CP_ERR_INTEGRITY); return M1CP_ERR_INTEGRITY; }

    /* atomic replace */
    if (s_be->stat(s_final_path, &exists, &dummy) == M1CP_FSB_OK)
    {
        if (exists)
        {
            if (s_overwrite == 0U) { fs_fail_cleanup(M1CP_ERR_BAD_STATE); return M1CP_ERR_BAD_STATE; }
            if (s_be->remove(s_final_path) != M1CP_FSB_OK) { fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO; }
        }
    }
    if (s_be->rename(s_tmp_path, s_final_path) != M1CP_FSB_OK)
    {
        fs_fail_cleanup(M1CP_ERR_IO); return M1CP_ERR_IO;
    }

    s_active  = 0U;
    s_session = 0U;
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, false);
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_COMPLETE, 100U);
    return M1CP_ERR_NONE;   /* ACK (no payload) */
}

/* FS_ABORT: clean cancel (temp discarded, running data untouched). */
static uint8_t fs_abort(void)
{
    if (s_active)
    {
        if (s_be != NULL)
        {
            (void)s_be->close();
            (void)s_be->remove(s_tmp_path);
        }
        s_active  = 0U;
        s_session = 0U;
    }
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, false);
    m1cp_status_set_error(M1CP_ERR_ABORTED);
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_IDLE, 0U);
    return M1CP_ERR_NONE;   /* ACK */
}

/* FS_STAT: name_len(1)|name -> exists(1)|size(4) */
static uint8_t fs_stat(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    uint16_t consumed;
    uint8_t  host_owned = 0U;
    uint8_t  exists = 0U;
    uint32_t size = 0U;

    consumed = fs_take_name(payload, plen);
    if (consumed == 0U) { return M1CP_ERR_BAD_ARG; }
    if (consumed != plen) { return M1CP_ERR_BAD_PAYLOAD; }
    if ((s_be == NULL) || (!s_be->ready(&host_owned))) { return M1CP_ERR_SD_UNAVAILABLE; }

    fs_build_paths();
    if (s_be->stat(s_final_path, &exists, &size) != M1CP_FSB_OK) { return M1CP_ERR_IO; }

    out[0] = exists ? 1U : 0U;
    wr_u32(&out[1], exists ? size : 0U);
    *olen = 5U;
    return M1CP_ERR_NONE;
}

/* FS_DELETE: name_len(1)|name -> ACK / NACK NOT_FOUND */
static uint8_t fs_delete(const uint8_t *payload, uint16_t plen)
{
    uint16_t consumed;
    uint8_t  host_owned = 0U;
    uint8_t  exists = 0U;
    uint32_t size = 0U;

    consumed = fs_take_name(payload, plen);
    if (consumed == 0U) { return M1CP_ERR_BAD_ARG; }
    if (consumed != plen) { return M1CP_ERR_BAD_PAYLOAD; }
    if ((s_be == NULL) || (!s_be->ready(&host_owned))) { return M1CP_ERR_SD_UNAVAILABLE; }

    fs_build_paths();
    if (s_be->stat(s_final_path, &exists, &size) != M1CP_FSB_OK) { return M1CP_ERR_IO; }
    if (!exists) { return M1CP_ERR_NOT_FOUND; }
    if (s_be->remove(s_final_path) != M1CP_FSB_OK) { return M1CP_ERR_IO; }
    return M1CP_ERR_NONE;   /* ACK */
}

/*************************** P U B L I C ************************************/

void m1cp_fs_init(const m1cp_fs_backend_t *backend)
{
    s_be = backend;
    s_active = 0U;
    s_session = 0U;
    s_session_ctr = 0U;
}

bool m1cp_fs_busy(void) { return s_active != 0U; }

void m1cp_fs_reset(void)
{
    if (s_active && (s_be != NULL))
    {
        (void)s_be->close();
        (void)s_be->remove(s_tmp_path);
    }
    s_active = 0U;
    s_session = 0U;
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, false);
}

uint8_t m1cp_fs_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                       uint8_t *out, uint16_t *out_len)
{
    uint8_t err;
    *out_len = 0U;

    switch (msg_type)
    {
        case M1CP_CMD_FS_BEGIN:  err = fs_begin(payload, plen, out, out_len); break;
        case M1CP_CMD_FS_DATA:   err = fs_data(payload, plen, out, out_len);  break;
        case M1CP_CMD_FS_COMMIT: err = fs_commit(payload, plen);              break;
        case M1CP_CMD_FS_ABORT:  err = fs_abort();                            break;
        case M1CP_CMD_FS_STAT:   err = fs_stat(payload, plen, out, out_len);  break;
        case M1CP_CMD_FS_DELETE: err = fs_delete(payload, plen);              break;
        default:                 err = M1CP_ERR_NOT_IMPLEMENTED;              break;
    }
    if (err != M1CP_ERR_NONE) { *out_len = 0U; }
    return err;
}

/*************************** F A T F S   B A C K E N D **********************/

#ifndef M1CP_HOST_TEST
#include "ff.h"
#include "m1_sdcard.h"     /* m1_sd_detected, m1_sdcard_get_status, SD_access_* */
#include "m1_storage.h"    /* usbmsc_sd_enable                                  */
#include "m1_file_util.h"  /* fs_directory_ensure, fs_get_free_space           */

static FIL     s_fatfs_file;
static uint8_t s_fatfs_open = 0U;

static m1cp_fsb_rc_t fatfs_map(FRESULT fr)
{
    switch (fr)
    {
        case FR_OK:        return M1CP_FSB_OK;
        case FR_NO_FILE:
        case FR_NO_PATH:   return M1CP_FSB_ERR_NOT_FOUND;
        case FR_DENIED:    return M1CP_FSB_ERR_NO_SPACE;
        default:           return M1CP_FSB_ERR_IO;
    }
}

static bool fatfs_ready(uint8_t *host_owned)
{
    uint8_t host = (usbmsc_sd_enable != 0U) ? 1U : 0U;
    if (host_owned != NULL) { *host_owned = host; }
    return (m1_sd_detected() != 0U) &&
           (m1_sdcard_get_status() == SD_access_OK) &&
           (host == 0U);
}

static uint64_t fatfs_free_bytes(void)
{
    uint64_t freeb = 0U;
    if (fs_get_free_space(&freeb) != FR_OK) { return 0U; }
    return freeb;
}

static m1cp_fsb_rc_t fatfs_ensure_dir(const char *dir_path)
{
    return fatfs_map(fs_directory_ensure(dir_path));
}

static m1cp_fsb_rc_t fatfs_open_w(const char *path)
{
    FRESULT fr;
    if (s_fatfs_open) { (void)f_close(&s_fatfs_file); s_fatfs_open = 0U; }
    fr = f_open(&s_fatfs_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK) { s_fatfs_open = 1U; }
    return fatfs_map(fr);
}

static m1cp_fsb_rc_t fatfs_write(const void *data, uint32_t len, uint32_t *written)
{
    UINT bw = 0U;
    FRESULT fr;
    if (!s_fatfs_open) { return M1CP_FSB_ERR_IO; }
    fr = f_write(&s_fatfs_file, data, (UINT)len, &bw);
    if (written != NULL) { *written = (uint32_t)bw; }
    if ((fr == FR_OK) && (bw != len)) { return M1CP_FSB_ERR_NO_SPACE; }
    return fatfs_map(fr);
}

static m1cp_fsb_rc_t fatfs_sync(void)
{
    if (!s_fatfs_open) { return M1CP_FSB_ERR_IO; }
    return fatfs_map(f_sync(&s_fatfs_file));
}

static m1cp_fsb_rc_t fatfs_close(void)
{
    FRESULT fr = FR_OK;
    if (s_fatfs_open) { fr = f_close(&s_fatfs_file); s_fatfs_open = 0U; }
    return fatfs_map(fr);
}

static m1cp_fsb_rc_t fatfs_crc32_file(const char *path, uint32_t *crc)
{
    FIL      f;
    FRESULT  fr;
    uint8_t  buf[512];
    UINT     br = 0U;
    uint32_t acc = 0U;

    fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) { return fatfs_map(fr); }
    for (;;)
    {
        fr = f_read(&f, buf, sizeof(buf), &br);
        if (fr != FR_OK) { (void)f_close(&f); return M1CP_FSB_ERR_IO; }
        if (br == 0U) { break; }
        acc = m1cp_fs_crc32(acc, buf, (uint32_t)br);
    }
    (void)f_close(&f);
    if (crc != NULL) { *crc = acc; }
    return M1CP_FSB_OK;
}

static m1cp_fsb_rc_t fatfs_stat(const char *path, uint8_t *exists, uint32_t *size)
{
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    if (fr == FR_OK)
    {
        if (exists != NULL) { *exists = 1U; }
        if (size != NULL)   { *size = (uint32_t)fno.fsize; }
        return M1CP_FSB_OK;
    }
    if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH))
    {
        if (exists != NULL) { *exists = 0U; }
        if (size != NULL)   { *size = 0U; }
        return M1CP_FSB_OK;   /* "does not exist" is a valid stat outcome */
    }
    return M1CP_FSB_ERR_IO;
}

static m1cp_fsb_rc_t fatfs_remove(const char *path)
{
    FRESULT fr = f_unlink(path);
    if ((fr == FR_OK) || (fr == FR_NO_FILE) || (fr == FR_NO_PATH)) { return M1CP_FSB_OK; }
    return M1CP_FSB_ERR_IO;
}

static m1cp_fsb_rc_t fatfs_rename(const char *from, const char *to)
{
    return fatfs_map(f_rename(from, to));
}

static const m1cp_fs_backend_t s_fatfs_backend =
{
    fatfs_ready, fatfs_free_bytes, fatfs_ensure_dir,
    fatfs_open_w, fatfs_write, fatfs_sync, fatfs_close,
    fatfs_crc32_file, fatfs_stat, fatfs_remove, fatfs_rename
};

void m1cp_fs_install_fatfs_backend(void)
{
    m1cp_fs_init(&s_fatfs_backend);
}
#endif /* !M1CP_HOST_TEST */
