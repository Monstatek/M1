/* See COPYING.txt for license details. */

/*
 * flipper_nfc.h
 *
 * Flipper Zero .nfc file format parser
 *
 * M1 Project
 */

#ifndef FLIPPER_NFC_H_
#define FLIPPER_NFC_H_

#include "flipper_file.h"

#define FLIPPER_NFC_UID_MAX_LEN    10
#define FLIPPER_NFC_ATQA_LEN       2

typedef enum {
	FLIPPER_NFC_TYPE_ISO14443_3A = 0,
	FLIPPER_NFC_TYPE_ISO14443_3B,
	FLIPPER_NFC_TYPE_ISO14443_4A,
	FLIPPER_NFC_TYPE_NTAG,
	FLIPPER_NFC_TYPE_MIFARE_CLASSIC,
	FLIPPER_NFC_TYPE_MIFARE_DESFIRE,
	FLIPPER_NFC_TYPE_UNKNOWN
} flipper_nfc_type_t;

typedef struct {
	flipper_nfc_type_t type;
	uint8_t  uid[FLIPPER_NFC_UID_MAX_LEN];
	uint8_t  uid_len;
	uint8_t  atqa[FLIPPER_NFC_ATQA_LEN];
	uint8_t  sak;
	char     device_type[32];
} flipper_nfc_card_t;

/* Load a .nfc file */
bool flipper_nfc_load(const char *path, flipper_nfc_card_t *out);

/* Save a .nfc file */
bool flipper_nfc_save(const char *path, const flipper_nfc_card_t *card);

/* Map Flipper NFC device type string to enum */
flipper_nfc_type_t flipper_nfc_parse_type(const char *type_str);

#endif /* FLIPPER_NFC_H_ */
