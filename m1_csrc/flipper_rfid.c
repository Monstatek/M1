/* See COPYING.txt for license details. */

/*
 * flipper_rfid.c
 *
 * Flipper Zero .rfid file format parser
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
#include "flipper_rfid.h"

/*************************** D E F I N E S ************************************/

#define FLIPPER_RFID_FILETYPE       "Flipper RFID key"
#define FLIPPER_RFID_MIN_VERSION    1

//************************** S T R U C T U R E S *******************************

/**
 * @brief  RFID protocol mapping entry
 */
typedef struct {
	const char     *flipper_name;
	LFRFIDProtocol  protocol;
} rfid_proto_map_t;

/***************************** V A R I A B L E S ******************************/

/**
 * Protocol mapping table between Flipper RFID protocol names and M1 enum values.
 * Values from lfrfid_protocol.h: LFRFIDProtocolEM4100, LFRFIDProtocolH10301, etc.
 */
static const rfid_proto_map_t rfid_proto_table[] = {
	{ "EM4100",    LFRFIDProtocolEM4100 },
	{ "EM-Marin",  LFRFIDProtocolEM4100 },         /* Alias used by some Flipper files */
	{ "EM4100_32", LFRFIDProtocolEM4100_32 },
	{ "EM4100_16", LFRFIDProtocolEM4100_16 },
	{ "H10301",    LFRFIDProtocolH10301 },
	{ "HID26",     LFRFIDProtocolH10301 },          /* HID 26-bit = H10301 */
	{ NULL,        LFRFIDProtocolMax }              /* Sentinel / unknown */
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static int rfid_strcasecmp(const char *a, const char *b);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Case-insensitive string comparison (portable)
 */
static int rfid_strcasecmp(const char *a, const char *b)
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
 * @brief  Map Flipper RFID protocol name to M1 LFRFIDProtocol enum
 * @param  name  protocol name from .rfid file (e.g., "EM4100", "H10301")
 * @return LFRFIDProtocol enum value, or LFRFIDProtocolMax if not recognized
 */
LFRFIDProtocol flipper_rfid_parse_protocol(const char *name)
{
	const rfid_proto_map_t *entry;

	if (name == NULL)
		return LFRFIDProtocolMax;

	for (entry = rfid_proto_table; entry->flipper_name != NULL; entry++)
	{
		if (rfid_strcasecmp(name, entry->flipper_name) == 0)
			return entry->protocol;
	}

	return LFRFIDProtocolMax;
}

/*============================================================================*/
/**
 * @brief  Get Flipper protocol name from M1 LFRFIDProtocol enum
 * @param  protocol  M1 protocol enum value
 * @return Flipper name string, or "Unknown"
 */
static const char *rfid_protocol_to_name(LFRFIDProtocol protocol)
{
	const rfid_proto_map_t *entry;

	for (entry = rfid_proto_table; entry->flipper_name != NULL; entry++)
	{
		if (entry->protocol == protocol)
			return entry->flipper_name;
	}

	return "Unknown";
}

/*============================================================================*/
/**
 * @brief  Load a .rfid file
 *
 *         File format:
 *           Filetype: Flipper RFID key
 *           Version: 1
 *           Key type: EM4100
 *           Data: 01 23 45 67 89
 *
 * @param  path  file path on FatFs filesystem
 * @param  out   output tag structure
 * @return true on success
 */
bool flipper_rfid_load(const char *path, flipper_rfid_tag_t *out)
{
	flipper_file_t ff;

	if (path == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(flipper_rfid_tag_t));
	out->protocol = LFRFIDProtocolMax;  /* Mark as unknown initially */

	if (!ff_open(&ff, path))
		return false;

	/* Validate header */
	if (!ff_validate_header(&ff, FLIPPER_RFID_FILETYPE, FLIPPER_RFID_MIN_VERSION))
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

		if (rfid_strcasecmp(ff_get_key(&ff), "Key type") == 0)
		{
			strncpy(out->protocol_name, ff_get_value(&ff),
			        sizeof(out->protocol_name) - 1);
			out->protocol_name[sizeof(out->protocol_name) - 1] = '\0';
			out->protocol = flipper_rfid_parse_protocol(ff_get_value(&ff));
		}
		else if (rfid_strcasecmp(ff_get_key(&ff), "Data") == 0)
		{
			out->data_len = ff_parse_hex_bytes(ff_get_value(&ff),
			                                    out->data,
			                                    (uint8_t)sizeof(out->data));
		}
	}

	ff_close(&ff);

	/* Minimal validation: must have data and a recognized protocol */
	return (out->data_len > 0 && out->protocol < LFRFIDProtocolMax);
}

/*============================================================================*/
/**
 * @brief  Save a .rfid file
 * @param  path  file path on FatFs filesystem
 * @param  tag   tag structure to save
 * @return true on success
 */
bool flipper_rfid_save(const char *path, const flipper_rfid_tag_t *tag)
{
	flipper_file_t ff;
	bool result = true;
	const char *proto_name;

	if (path == NULL || tag == NULL)
		return false;

	if (!ff_open_write(&ff, path))
		return false;

	/* Write header */
	if (result)
		result = ff_write_kv_str(&ff, "Filetype", FLIPPER_RFID_FILETYPE);
	if (result)
		result = ff_write_kv_uint32(&ff, "Version", 1);

	/* Write key type */
	if (result)
	{
		if (tag->protocol_name[0] != '\0')
			proto_name = tag->protocol_name;
		else
			proto_name = rfid_protocol_to_name(tag->protocol);

		result = ff_write_kv_str(&ff, "Key type", proto_name);
	}

	/* Write data */
	if (result && tag->data_len > 0)
		result = ff_write_kv_hex(&ff, "Data", tag->data, tag->data_len);

	ff_close(&ff);
	return result;
}
