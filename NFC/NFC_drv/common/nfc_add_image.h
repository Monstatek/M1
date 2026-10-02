/*
 * nfc_add_image.h - Blank-template image builders for manual NFC card creation
 *                   ("Add"). Pure logic, no RF, no allocation, no I/O.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Produces a structurally valid blank card image for each supported type from a
 * user-entered UID, using the project's blank-template policy. Unspecified
 * bytes are template defaults (zeros / factory config), never invented data
 * claimed to have been read from a physical card. No passwords are generated:
 * the NTAG PWD field is left at its factory default (FF FF FF FF) and PACK 0000.
 *
 * Geometry references:
 *   MIFARE Classic 1K : 16 sectors, 64 blocks, 16 B/block, 1024 B.
 *   MIFARE Classic 4K : 40 sectors, 256 blocks (sec 0-31 x4, 32-39 x16), 4096 B.
 *   MIFARE Ultralight : original MF0ICU1, 16 pages, 4 B/page, 64 B.
 *   NTAG213/215/216   : 45 / 135 / 231 pages, 4 B/page, 180 / 540 / 924 B.
 */
#ifndef NFC_DRV_NFC_ADD_IMAGE_H_
#define NFC_DRV_NFC_ADD_IMAGE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NFC_ADD_MFC_1K = 0,
    NFC_ADD_MFC_4K,
    NFC_ADD_ULTRALIGHT,   /* original MIFARE Ultralight (MF0ICU1) */
    NFC_ADD_NTAG213,
    NFC_ADD_NTAG215,
    NFC_ADD_NTAG216,
    NFC_ADD_TYPE_COUNT
} nfc_add_type_t;

typedef enum {
    NFC_ADD_FAM_CLASSIC = 0,   /* 16-byte blocks */
    NFC_ADD_FAM_T2T            /* 4-byte pages   */
} nfc_add_family_t;

typedef struct {
    nfc_add_type_t   type;
    const char      *label;        /* menu / summary label, e.g. "MFC 1K"      */
    const char      *devtype_str;  /* saved "Device type:" value               */
    nfc_add_family_t family;
    uint8_t          uid_len;      /* 4 (Classic) or 7 (T2T)                   */
    uint16_t         unit_size;    /* 16 (Classic block) or 4 (T2T page)       */
    uint16_t         unit_count;   /* blocks or pages                          */
    uint32_t         image_size;   /* unit_size * unit_count                   */
    uint8_t          sak;          /* Classic 0x08/0x18; 0 for T2T             */
    uint8_t          atqa[2];      /* [0]=LSB (anticollision), [1]=MSB         */
    uint8_t          t2t_variant;  /* M1NFC_T2TVAR_* for T2T; 0 for Classic    */
    uint8_t          cc3;          /* NTAG CC byte 2 (size); 0 for UL/Classic  */
    uint16_t         user_first;   /* first user unit (T2T page / not used MFC)*/
    uint16_t         user_last;    /* last user unit (inclusive)               */
} nfc_add_geom_t;

/* Geometry descriptor for a type, or NULL if the type is invalid. */
const nfc_add_geom_t *nfc_add_geometry(nfc_add_type_t type);

/* Required UID length (bytes) for a type: 4 (Classic) or 7 (T2T). 0 if invalid. */
uint8_t nfc_add_uid_len(nfc_add_type_t type);

/* BCC helpers (exposed for tests / block-0 construction). */
uint8_t nfc_add_bcc_mfc(const uint8_t uid[4]);   /* Classic block 0: xor of 4 UID bytes   */
uint8_t nfc_add_bcc0_t2t(const uint8_t uid[7]);  /* T2T page 0 byte 3: 0x88 ^ u0 ^ u1 ^ u2 */
uint8_t nfc_add_bcc1_t2t(const uint8_t uid[7]);  /* T2T page 2 byte 0: u3 ^ u4 ^ u5 ^ u6   */

/* Build the full blank image for `type` from `uid` (uid_len bytes) into `out`
 * (capacity out_cap). Returns the image size in bytes on success, or 0 on any
 * error (bad type, wrong uid_len, or out_cap < image_size). The buffer is fully
 * initialised (template defaults) before card-specific fields are written. */
uint32_t nfc_add_build_image(nfc_add_type_t type, const uint8_t *uid, uint8_t uid_len,
                             uint8_t *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_NFC_ADD_IMAGE_H_ */
