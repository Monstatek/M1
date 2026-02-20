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
 * @param path Full path on SD card (e.g., "/NFC/card1.nfc")
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
 *  # Type 2 (Ultralight/NTAG)
 *  Page 0:  xx xx xx xx
 *  Page 1:  xx xx xx xx
 *  ...
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
