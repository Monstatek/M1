/* See COPYING.txt for license details. */

#include "nfc_fileio.h"
#include "ff.h"   /* f_tell/f_lseek -- same direct-FIL-access pattern as mfc_key_source_sd.c */
#include <string.h>

/*============================================================================*/
/**
 * @brief Refill the read buffer
 * @param io Pointer to the nfcfio context
 * @return 1 on success, 0 on EOF or error
 */
/*============================================================================*/
static int nfcfio_refill(nfcfio_t* io) {
    if (io->eof) return 0;
    io->rlen = m1_fb_read_from_file(&io->fh, (char*)io->rbuf, (uint16_t)NFCFIO_RBUFSZ);
    io->rpos = 0;
    if (io->rlen == 0) { io->eof = 1; return 0; }
    io->rbuf[io->rlen] = 0; /* sentinel */
    return 1;
}

/*============================================================================*/
/**
 * @brief Open a file for reading
 * @param io Pointer to the nfcfio context (will be initialized)
 * @param path File path to open
 * @return 1 on success, 0 on failure
 * @note m1_fb_open_file returns 0 on success, 1 on failure
 */
/*============================================================================*/
int nfcfio_open_read(nfcfio_t* io, const char* path) {
    if (!io || !path) return 0;
    memset(io, 0, sizeof(*io));
    /* m1_fb_open_file: success=0, failure=1 */
    if (m1_fb_open_file(&io->fh, path)) return 0;
    io->eol_crlf = 0;
    return 1;
}

/*============================================================================*/
/**
 * @brief Open a file for writing (creates new file)
 * @param io Pointer to the nfcfio context (will be initialized)
 * @param path File path to create/open
 * @return 1 on success, 0 on failure
 * @note m1_fb_open_new_file returns 0 on success, 1 on failure
 */
/*============================================================================*/
int nfcfio_open_write(nfcfio_t* io, const char* path) {
    if (!io || !path) return 0;
    memset(io, 0, sizeof(*io));
    /* m1_fb_open_new_file: success=0, failure=1 */
    if (m1_fb_open_new_file(&io->fh, path)) return 0;
    io->eol_crlf = 0;
    return 1;
}

/*============================================================================*/
/**
 * @brief Close the file
 * @param io Pointer to the nfcfio context
 * @note m1_fb_close_file wraps FatFs f_close
 */
/*============================================================================*/
void nfcfio_close(nfcfio_t* io) {
    if (!io) return;
    (void)m1_fb_close_file(&io->fh);
}

/*============================================================================*/
/**
 * @brief Read a line from file (includes '\n' in output)
 * @param io Pointer to the nfcfio context
 * @param out Output buffer to store the line
 * @param outsz Size of the output buffer
 * @return >=0: number of bytes read, -1: EOF (no more data), -2: error
 * @note - Returns even if buffer is full (truncated) before newline
 * @note - Returns last fragment line without newline at EOF
 */
/*============================================================================*/
int nfcfio_getline(nfcfio_t* io, char* out, size_t outsz) {
    if (!io || !out || outsz == 0) return -2;
    size_t w = 0;

    while (1) {
        /* Refill buffer when exhausted */
        if (io->rpos >= io->rlen) {
            if (!nfcfio_refill(io)) {
                if (w == 0) return -1; /* no more data */
                out[w] = '\0';
                return (int)w;         /* last fragment line */
            }
        }

        /* Copy until newline */
        while (io->rpos < io->rlen && w + 1 < outsz) {
            char c = (char)io->rbuf[io->rpos++];
            out[w++] = c;
            if (c == '\n') { out[w] = '\0'; return (int)w; }
        }

        /* Output buffer full, return immediately (truncated) */
        if (w + 1 >= outsz) { out[w] = '\0'; return (int)w; }
        /* Input buffer ended without newline, continue to refill */
    }
}

/*============================================================================*/
/**
 * @brief   Byte offset of the next unconsumed line -- the exact position a
 *          later nfcfio_seek() must be given to resume streaming exactly
 *          where a prior getline() session left off, skipping nothing and
 *          re-reading nothing.
 * @param   io Pointer to the nfcfio context (a file opened for reading)
 * @return  The offset, or -1 if io is NULL.
 * @note    f_tell() alone is NOT this value: it reports the position after
 *          the last physical refill from SD, which is ahead of whatever
 *          getline() has actually returned to the caller by however many
 *          bytes remain buffered (rlen-rpos) but not yet consumed.
 */
/*============================================================================*/
long nfcfio_tell_line_start(nfcfio_t* io) {
    if (!io) return -1;
    FSIZE_t  pos        = f_tell(&io->fh);
    uint32_t unconsumed = io->rlen - io->rpos;
    if ((FSIZE_t)unconsumed > pos) return -1;   /* defensive: should never happen */
    return (long)(pos - (FSIZE_t)unconsumed);
}

/*============================================================================*/
/**
 * @brief   Move the read position to a byte offset previously reported by
 *          nfcfio_tell_line_start(), discarding any buffered-but-unconsumed
 *          data so the next getline() reads fresh from that exact position.
 * @param   io Pointer to the nfcfio context (a file opened for reading)
 * @param   offset Byte offset to seek to
 * @return  1 on success, 0 on failure (io NULL, or the underlying seek failed --
 *          the caller's own fallback is simply to stream from the beginning
 *          instead, never to trust a partially-applied seek)
 */
/*============================================================================*/
int nfcfio_seek(nfcfio_t* io, uint32_t offset) {
    if (!io) return 0;
    if (f_lseek(&io->fh, (FSIZE_t)offset) != FR_OK) return 0;
    io->rlen = 0;
    io->rpos = 0;
    io->eof  = 0;
    return 1;
}

/*============================================================================*/
/**
 * @brief Write a string to file (no newline appended)
 * @param io Pointer to the nfcfio context
 * @param s String to write
 * @return 1 on success, 0 on failure
 * @note m1_fb_write_to_file returns number of bytes written (0 on error)
 */
/*============================================================================*/
int nfcfio_puts(nfcfio_t* io, const char* s) {
    if (!io || !s) return 0;
    size_t n = strlen(s);
    return (m1_fb_write_to_file(&io->fh, s, (uint16_t)n) == (uint16_t)n);
}

/*============================================================================*/
/**
 * @brief Write a line to file (EOL automatically appended)
 * @param io Pointer to the nfcfio context
 * @param s String to write
 * @return 1 on success, 0 on failure
 * @note EOL format depends on eol_crlf setting (\r\n or \n)
 */
/*============================================================================*/
int nfcfio_putline(nfcfio_t* io, const char* s) {
    if (!nfcfio_puts(io, s)) return 0;
    const char* eol = io->eol_crlf ? "\r\n" : "\n";
    return nfcfio_puts(io, eol);
}


