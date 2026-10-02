/* See COPYING.txt for license details. */

#ifndef NFC_DRV_NFC_FILEIO_H_
#define NFC_DRV_NFC_FILEIO_H_

#include <stdint.h>
#include <stddef.h>

#include "main.h"
#include "m1_nfc.h"
#include "m1_sdcard.h"


/* ===== Configuration (can be redefined in project if needed) ===== */
#ifndef NFCFIO_RBUFSZ
#define NFCFIO_RBUFSZ   1024    /* Read block size (bytes read from SD at once) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Line-based I/O context (static buffer, no heap usage)
 */
typedef struct {
    FIL      fh;                /* FatFs file handle */
    /* read state */
    uint8_t  rbuf[NFCFIO_RBUFSZ + 1]; /* +1: null terminator space */
    uint32_t rlen;              /* Current data length in rbuf */
    uint32_t rpos;              /* Read position in rbuf */
    uint8_t  eof;               /* EOF flag */
    /* write state */
    uint8_t  eol_crlf;          /* 1: \r\n, 0: \n */
} nfcfio_t;

/*============================================================================*/
/* Open/Close */
/*============================================================================*/
/**
 * @brief Open a file for reading
 * @param io Pointer to the nfcfio context (will be initialized)
 * @param path File path to open
 * @return 1 on success, 0 on failure
 */
int  nfcfio_open_read (nfcfio_t* io, const char* path);

/**
 * @brief Open a file for writing (creates new file)
 * @param io Pointer to the nfcfio context (will be initialized)
 * @param path File path to create/open
 * @return 1 on success, 0 on failure
 */
int  nfcfio_open_write(nfcfio_t* io, const char* path);

/**
 * @brief Close the file
 * @param io Pointer to the nfcfio context
 */
void nfcfio_close     (nfcfio_t* io);

/*============================================================================*/
/* Options */
/*============================================================================*/
/**
 * @brief Set CRLF mode for EOL (End of Line)
 * @param io Pointer to the nfcfio context
 * @param on 1 for CRLF (\r\n), 0 for LF (\n)
 */
static inline void nfcfio_set_crlf(nfcfio_t* io, int on) { if (io) io->eol_crlf = (on != 0); }

/*============================================================================*/
/* Line I/O */
/*============================================================================*/
/**
 * @brief Read a line from file (includes '\n' in output)
 * @param io Pointer to the nfcfio context
 * @param out Output buffer to store the line
 * @param outsz Size of the output buffer
 * @return >=0: number of bytes read, -1: EOF (no more data), -2: error
 */
int  nfcfio_getline(nfcfio_t* io, char* out, size_t outsz);

/*============================================================================*/
/* Resume support (optional -- only needed by a caller that wants to seek
 * back into a large file across two separate open/read sessions, e.g. the
 * MIFARE Classic dictionary-acquisition resume cursor). */
/*============================================================================*/
/**
 * @brief Byte offset of the next line getline() would return -- pass this to
 *        nfcfio_seek() in a LATER session to resume exactly here.
 * @param io Pointer to the nfcfio context
 * @return The offset, or -1 if io is NULL.
 */
long nfcfio_tell_line_start(nfcfio_t* io);

/**
 * @brief Move to a byte offset previously reported by nfcfio_tell_line_start(),
 *        discarding any buffered-but-unconsumed data.
 * @param io Pointer to the nfcfio context
 * @param offset Byte offset to seek to
 * @return 1 on success, 0 on failure
 */
int  nfcfio_seek(nfcfio_t* io, uint32_t offset);

/**
 * @brief Write a string to file (no newline appended)
 * @param io Pointer to the nfcfio context
 * @param s String to write
 * @return 1 on success, 0 on failure
 */
int  nfcfio_puts   (nfcfio_t* io, const char* s);

/**
 * @brief Write a line to file (EOL automatically appended)
 * @param io Pointer to the nfcfio context
 * @param s String to write
 * @return 1 on success, 0 on failure
 */
int  nfcfio_putline(nfcfio_t* io, const char* s);

#ifdef __cplusplus
}
#endif


#endif /* NFC_DRV_NFC_FILEIO_H_ */
