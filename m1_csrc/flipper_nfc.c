/* See COPYING.txt for license details. */

/*
 * flipper_nfc.c
 *
 * Flipper Zero .nfc file format parser
 *
 * M1 Project
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "flipper_nfc.h"

/*************************** D E F I N E S ************************************/

#define FLIPPER_NFC_FILETYPE        "Flipper NFC device"
#define FLIPPER_NFC_MIN_VERSION     2

//************************** S T R U C T U R E S *******************************

/**
 * @brief  NFC type mapping entry
 */
typedef struct {
	const char          *flipper_name;
	flipper_nfc_type_t   type;
} nfc_type_map_t;

/***************************** V A R I A B L E S ******************************/

static const nfc_type_map_t nfc_type_table[] = {
	{ "ISO14443-3A",       FLIPPER_NFC_TYPE_ISO14443_3A },
	{ "ISO14443-3B",       FLIPPER_NFC_TYPE_ISO14443_3B },
	{ "ISO14443-4A",       FLIPPER_NFC_TYPE_ISO14443_4A },
	{ "NTAG",              FLIPPER_NFC_TYPE_NTAG },
	{ "NTAG203",           FLIPPER_NFC_TYPE_NTAG },
	{ "NTAG213",           FLIPPER_NFC_TYPE_NTAG },
	{ "NTAG215",           FLIPPER_NFC_TYPE_NTAG },
	{ "NTAG216",           FLIPPER_NFC_TYPE_NTAG },
	{ "NTAGI2C1K",         FLIPPER_NFC_TYPE_NTAG },
	{ "NTAGI2C2K",         FLIPPER_NFC_TYPE_NTAG },
	{ "Mifare Classic",    FLIPPER_NFC_TYPE_MIFARE_CLASSIC },
	{ "Mifare Classic 1K", FLIPPER_NFC_TYPE_MIFARE_CLASSIC },
	{ "Mifare Classic 4K", FLIPPER_NFC_TYPE_MIFARE_CLASSIC },
	{ "Mifare DESFire",    FLIPPER_NFC_TYPE_MIFARE_DESFIRE },
	{ NULL,                FLIPPER_NFC_TYPE_UNKNOWN }
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static int nfc_strcasecmp(const char *a, const char *b);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Case-insensitive string comparison (portable)
 */
static int nfc_strcasecmp(const char *a, const char *b)
{
	while (*a && *b)
	{
		int ca = tolower((unsigned char)*a);
		int cb = tolower((unsigned char)*b);
		if (ca != cb)
			return ca - cb;
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

/*============================================================================*/
/**
 * @brief  Map Flipper NFC device type string to enum
 * @param  type_str  device type string from .nfc file
 * @return flipper_nfc_type_t enum value
 */
flipper_nfc_type_t flipper_nfc_parse_type(const char *type_str)
{
	const nfc_type_map_t *entry;

	if (type_str == NULL)
		return FLIPPER_NFC_TYPE_UNKNOWN;

	for (entry = nfc_type_table; entry->flipper_name != NULL; entry++)
	{
		if (nfc_strcasecmp(type_str, entry->flipper_name) == 0)
			return entry->type;
	}

	return FLIPPER_NFC_TYPE_UNKNOWN;
}

/*============================================================================*/
/**
 * @brief  Load a .nfc file
 *
 *         File format:
 *           Filetype: Flipper NFC device
 *           Version: 4
 *           Device type: ISO14443-3A
 *           UID: 04 68 95 71 FA 5C 64
 *           ATQA: 44 00
 *           SAK: 00
 *
 * @param  path  file path on FatFs filesystem
 * @param  out   output card structure
 * @return true on success
 */
bool flipper_nfc_load(const char *path, flipper_nfc_card_t *out)
{
	flipper_file_t ff;

	if (path == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(flipper_nfc_card_t));
	out->type = FLIPPER_NFC_TYPE_UNKNOWN;

	if (!ff_open(&ff, path))
		return false;

	/* Validate header */
	if (!ff_validate_header(&ff, FLIPPER_NFC_FILETYPE, FLIPPER_NFC_MIN_VERSION))
	{
		ff_close(&ff);
		return false;
	}

	/* Parse remaining key-value pairs */
	while (ff_read_line(&ff))
	{
		if (ff_is_separator(&ff))
			continue;

		if (!ff_parse_kv(&ff))
			continue;

		if (nfc_strcasecmp(ff_get_key(&ff), "Device type") == 0)
		{
			strncpy(out->device_type, ff_get_value(&ff), sizeof(out->device_type) - 1);
			out->device_type[sizeof(out->device_type) - 1] = '\0';
			out->type = flipper_nfc_parse_type(ff_get_value(&ff));
		}
		else if (nfc_strcasecmp(ff_get_key(&ff), "UID") == 0)
		{
			out->uid_len = ff_parse_hex_bytes(ff_get_value(&ff),
			                                   out->uid,
			                                   FLIPPER_NFC_UID_MAX_LEN);
		}
		else if (nfc_strcasecmp(ff_get_key(&ff), "ATQA") == 0)
		{
			ff_parse_hex_bytes(ff_get_value(&ff), out->atqa, FLIPPER_NFC_ATQA_LEN);
		}
		else if (nfc_strcasecmp(ff_get_key(&ff), "SAK") == 0)
		{
			uint8_t sak_byte;
			if (ff_parse_hex_bytes(ff_get_value(&ff), &sak_byte, 1) == 1)
				out->sak = sak_byte;
		}
	}

	ff_close(&ff);

	/* Minimal validation: must have at least a UID */
	return (out->uid_len > 0);
}

/*============================================================================*/
/**
 * @brief  Save a .nfc file
 * @param  path  file path on FatFs filesystem
 * @param  card  card structure to save
 * @return true on success
 */
bool flipper_nfc_save(const char *path, const flipper_nfc_card_t *card)
{
	flipper_file_t ff;
	bool result = true;
	uint8_t sak_byte;

	if (path == NULL || card == NULL)
		return false;

	if (!ff_open_write(&ff, path))
		return false;

	/* Write header */
	if (result)
		result = ff_write_kv_str(&ff, "Filetype", FLIPPER_NFC_FILETYPE);
	if (result)
		result = ff_write_kv_uint32(&ff, "Version", 4);

	/* Write device type */
	if (result && card->device_type[0] != '\0')
		result = ff_write_kv_str(&ff, "Device type", card->device_type);

	/* Write UID */
	if (result && card->uid_len > 0)
		result = ff_write_kv_hex(&ff, "UID", card->uid, card->uid_len);

	/* Write ATQA */
	if (result)
		result = ff_write_kv_hex(&ff, "ATQA", card->atqa, FLIPPER_NFC_ATQA_LEN);

	/* Write SAK */
	if (result)
	{
		sak_byte = card->sak;
		result = ff_write_kv_hex(&ff, "SAK", &sak_byte, 1);
	}

	ff_close(&ff);
	return result;
}
