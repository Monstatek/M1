/* See COPYING.txt for license details. */


#ifndef NFC_DRV_NFC_STORAGE_H_
#define NFC_DRV_NFC_STORAGE_H_

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------- File parsing result codes ------- */
typedef enum {
    NFC_STORAGE_OK            = 0,  /* Success */
    NFC_STORAGE_ERR_IO        = 1,  /* File open/read failure */
    NFC_STORAGE_ERR_FORMAT    = 2,  /* Format error (header/body parsing failure) */
    NFC_STORAGE_ERR_UNSUP     = 3,  /* Unsupported Device type */
    NFC_STORAGE_ERR_NO_BUFFER = 4   /* Dump buffer insufficient/NULL */
} nfc_storage_result_t;


/*============================================================================*/
/**
 * @brief Parse .nfc text file to fill nfc_ctx
 * 
 * Parses .nfc file format:
 * - Header: Filetype, Version, Device type, UID, ATQA, SAK, ATS, etc.
 * - Body: "Page N:", "Block N:" lines → stored in dump buffer
 *
 * @param path Full path on SD card (e.g., "/nfc/card1.nfc")
 * @param dump_buf Workspace pointer to store dump data
 * @param dump_buf_bytes Total size of dump_buf (in bytes)
 * @param valid_bits Unit validity bitmap (optional, NULL allowed)
 * @param valid_bits_bytes Size of valid_bits buffer (bytes, 1 byte per 8 units)
 *
 * @return NFC_STORAGE_OK on success
 * @return NFC_STORAGE_ERR_IO on file open/read failure
 * @return NFC_STORAGE_ERR_FORMAT on missing required header, HEX parsing failure, etc.
 * @return NFC_STORAGE_ERR_UNSUP on unsupported Device type
 * @return NFC_STORAGE_ERR_NO_BUFFER on NULL buffer or insufficient capacity
 */
/*============================================================================*/
nfc_storage_result_t nfc_storage_load_file(
        const char* path,
        uint8_t*    dump_buf,
        uint32_t    dump_buf_bytes,
        uint8_t*    valid_bits,
        uint32_t    valid_bits_bytes);

/* Canonical file-format name for a T2T variant ("UL", "NTAG216", ...), or
 * NULL for M1NFC_T2TVAR_UNKNOWN / an out-of-range value. Used by
 * nfc_file.c's writer for the "T2T Variant:" line and internally by the
 * parser's name->enum lookup -- the single source of truth for both
 * directions, so the name a file is saved with is always exactly the name
 * that reloads back to the same variant. */
const char *nfc_t2t_variant_name(uint8_t variant);



/*  Storage file text line format (M1 NFC device)
 *
 *  Filetype: M1 NFC device
 *  Version: 4
 *  Device type: <Classic|Ultralight/NTAG|DESFire|ISO14443-4A|Felica|ISO15693>
 *  UID: 04 A2 BC 12 34 56 78
 *  ATQA: 44 00          # If Tech A
 *  SAK: 00              # If Tech A
 *  ATS:  78 77 ...      # If 4A
 *
 *  # Type 2 (Ultralight/NTAG) -- "Version:" above is this FILE FORMAT's own
 *  # version (always "4"); "T2T Version:" below is the tag's raw GET_VERSION
 *  # reply -- the two must never be confused.
 *  T2T Variant: NTAG216            # explicit, canonical name (see
 *                                  # nfc_t2t_variant_name()); absent for a
 *                                  # tag that was never identified
 *  T2T Version: 00 04 04 02 01 00 13 03   # raw 8-byte GET_VERSION tuple;
 *                                  # absent if never captured (UL/ULC/
 *                                  # NTAG203 predate this command, or the
 *                                  # tag NAK'd it)
 *  Pages: 231                      # DECLARED/EXPECTED total for the
 *                                  # variant above -- NOT re-derived from
 *                                  # how many Page lines follow
 *  Signature: <64 hex chars>       # genuine READ_SIGNATURE bytes; absent
 *                                  # if never captured
 *  Counter0: <6 hex chars>         # genuine READ_CNT bytes, per index
 *  Counter1: ...
 *  Counter2: ...
 *  Tearing0: <2 hex chars>         # genuine CHECK_TEARING byte, per index
 *  Tearing1: ...
 *  Tearing2: ...
 *  Page 0:  xx xx xx xx            # present only for an actually-read
 *  Page 1:  xx xx xx xx            # page -- a gap in the numbering means
 *  ...                             # that page was never captured, never
 *                                  # a zero-filled placeholder
 *
 *  # Classic
 *  Mifare Classic type: 1K
 *  Data format version: 2
 *  Block 0:  xx xx ... (16B)
 *  Block 1:  ...
 *  ...
 *
 *  # Felica
 *  IDm:  xx .. (8B)
 *  PMm:  xx .. (8B)
 *  System code: 12 34
 *
 *  # ISO15693
 *  DSFID:  xx
 *  AFI:    xx
 *  Block 0: xx .. (block_size B)
 *  ...
 */



#endif /* NFC_DRV_NFC_STORAGE_H_ */
