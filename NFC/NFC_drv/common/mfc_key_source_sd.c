/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_key_source_sd.c
 * @brief   See mfc_key_source_sd.h.
 */
/*============================================================================*/
#include "mfc_key_source_sd.h"
#include "nfc_fileio.h"
#include "ff.h"   /* f_open/f_close/FRESULT: distinguish absent vs read-error */

/* One open file at a time (see mfc_key_source_sd.h's lifetime note) -- a
 * static instance, never the stack, matching this codebase's convention for
 * SD/file contexts that outlive a single call. */
static nfcfio_t s_sd_io;

mfc_path_probe_t mfc_key_source_sd_probe(const char *path, void *io_ctx)
{
    (void)io_ctx;
    if (path == NULL) return MFC_PATH_ABSENT;

    FIL     f;
    FRESULT r = f_open(&f, path, FA_READ);
    if (r == FR_OK) { f_close(&f); return MFC_PATH_PRESENT; }
    if ((r == FR_NO_FILE) || (r == FR_NO_PATH)) return MFC_PATH_ABSENT;
    return MFC_PATH_READ_ERROR;
}

static int sd_next_line(void *ctx, char *buf, size_t bufsz)
{
    nfcfio_t *io = (nfcfio_t *)ctx;
    return (nfcfio_getline(io, buf, bufsz) >= 0) ? 1 : 0;
}

static void sd_close(void *ctx)
{
    nfcfio_close((nfcfio_t *)ctx);
}

static long sd_tell(void *ctx)
{
    return nfcfio_tell_line_start((nfcfio_t *)ctx);
}

static bool sd_seek(void *ctx, uint32_t offset)
{
    return nfcfio_seek((nfcfio_t *)ctx, offset) == 1;
}

int mfc_key_source_sd_open(mfc_line_source_t *out, const char *path, void *io_ctx)
{
    (void)io_ctx;
    if ((out == NULL) || (path == NULL)) return 0;
    if (nfcfio_open_read(&s_sd_io, path) != 1) return 0;

    out->next_line = sd_next_line;
    out->close     = sd_close;
    out->tell      = sd_tell;
    out->seek      = sd_seek;
    out->ctx       = &s_sd_io;
    return 1;
}

int mfc_key_source_sd_stat(const char *path, uint32_t *size, uint16_t *fdate, uint16_t *ftime)
{
    if ((path == NULL) || (size == NULL) || (fdate == NULL) || (ftime == NULL)) return 0;
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return 0;
    *size  = (uint32_t)fno.fsize;
    *fdate = fno.fdate;
    *ftime = fno.ftime;
    return 1;
}
