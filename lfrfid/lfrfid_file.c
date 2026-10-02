/* See COPYING.txt for license details. */

/*
 * lfrfid_file.c
 */

/*************************** I N C L U D E S **********************************/
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_sdcard_man.h"
#include "m1_sdcard_provision.h"
#include "m1_file_browser.h"
#include "m1_file_util.h"
#include "m1_virtual_kb.h"
#include "m1_save_filename.h"
#include "lfrfid.h"
#include "uiView.h"
#include "res_string.h"
#include "privateprofilestring.h"
#include "lfrfid_file.h"

/*************************** D E F I N E S ************************************/

#define DRIVE0_RFID     M1_SD_DIR_RFID
#define RFID_FILE_EXTENSION_TMP				"rfid" // rfh for NFC
//#define RFID_DATAFILE_PACKET_FORMAT_N		3
//#define RFID_DATAFILE_FILETYPE_PACKET		"PACKET"
//#define RFID_DATAFILE_FILETYPE_KEYWORD	RFID_DATAFILE_FILETYPE_PACKET

#define RFID_DATAFILE_FILETYPE		"M1 RFID PACKET"
#define RFID_DATAFILE_VERSION		"0.8"

#define RFID_DATAFILE_FILETYPE_KEYWORD	"Filetype"
#define RFID_DATAFILE_VERSION_KEYWORD	"Version"
#define RFID_DATAFILE_PACKTYPE_KEYWORD	"Packettype"
#define RFID_DATAFILE_DATA_KEYWORD		"HData"

/* Compatible LF RFID key file (read-only interoperability). */
#define RFID_INTEROP_FILETYPE			"Flipper RFID key"
#define RFID_INTEROP_VERSION			"1"
#define RFID_INTEROP_KEYTYPE_KEYWORD	"Key type"
#define RFID_INTEROP_DATA_KEYWORD		"Data"

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/



/********************* F U N C T I O N   P R O T O T Y P E S ******************/



/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static inline void uid_to_string(char *dst, size_t dst_size,
                                 const uint8_t *uid, uint8_t uid_len)
{
    size_t pos = 0;

    for (uint8_t i = 0; i < uid_len; i++) {
        if (pos >= dst_size) break;

        pos += snprintf(dst + pos, dst_size - pos,
                        (i + 1 < uid_len) ? "%02X " : "%02X",
                        uid[i]);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief Validate a space-separated hex-byte string WITHOUT relying on the
  *        shared hex parser. Requires EXACTLY `expected` tokens, each exactly
  *        two hex digits (rejects invalid chars, odd/short/long tokens, and
  *        too few / too many bytes). Writes into out[] (size >= expected) but
  *        the result must be ignored unless this returns true.
  * @retval true only when the whole string validates exactly.
  */
/*============================================================================*/
static bool rfid_parse_exact_hex_bytes(const char *s, uint8_t *out, int expected)
{
	int count = 0;

	if(s == NULL || out == NULL || expected <= 0)
		return false;

	while(*s)
	{
		while(*s == ' ') s++;			/* skip separators */
		if(*s == '\0') break;

		int ndig = 0;
		unsigned int byte = 0;
		while(*s && *s != ' ')
		{
			char c = *s++;
			int d;
			if(c >= '0' && c <= '9') d = c - '0';
			else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false;			/* non-hex character */
			byte = (byte << 4) | (unsigned)d;
			if(++ndig > 2) return false;		/* token longer than one byte */
		}
		if(ndig != 2) return false;		/* odd / short digit count */
		if(count >= expected) return false;	/* too many bytes */
		out[count++] = (uint8_t)byte;
	}

	return (count == expected);			/* too few -> reject */
}


/* Supported saved-key formats share the same "Key: Value" line syntax,
 * protocol names and data layout. Loading is dual-format; saving remains M1. */
typedef struct {
	const char *filetype;    /* expected Filetype value      */
	const char *version;     /* expected Version value       */
	const char *proto_key;   /* protocol-name field name     */
	const char *data_key;    /* credential-data field name   */
} rfid_file_fmt_t;

static const rfid_file_fmt_t rfid_file_formats[] = {
	{ RFID_DATAFILE_FILETYPE, RFID_DATAFILE_VERSION,
	  RFID_DATAFILE_PACKTYPE_KEYWORD, RFID_DATAFILE_DATA_KEYWORD },   /* M1 */
	{ RFID_INTEROP_FILETYPE,  RFID_INTEROP_VERSION,
	  RFID_INTEROP_KEYTYPE_KEYWORD,   RFID_INTEROP_DATA_KEYWORD  },
};
#define RFID_FILE_FORMAT_COUNT (sizeof(rfid_file_formats)/sizeof(rfid_file_formats[0]))


bool lfrfid_profile_load(const S_M1_file_info *f, const char* ext)
{
	char file_path[64];
	char buf[200];
	ParsedValue data;
	uint8_t protocol;
	uint16_t expected;
	uint8_t parsed[sizeof(lfrfid_tag_info.uid)];
	const rfid_file_fmt_t *fmt = NULL;
	ProfileSession sess;
	bool ok = false;

	if(!IsValidFileSpec(f, ext))
		return false;

	fu_path_combine(file_path, sizeof(file_path), f->dir_name, f->file_name);

	/* Single open for all 4 header lookups below (previously 4 independent
	 * f_open/f_close cycles per selected file). */
	if(!profile_session_open(&sess, file_path))
		return false;

	data.buf = buf;
	data.max_len = sizeof(buf);

	/* Select exactly one format from the Filetype value (both formats share
	 * the "Filetype"/"Version" header keys). Unknown Filetype -> reject. */
	if(GetPrivateProfileStringS(&data, RFID_DATAFILE_FILETYPE_KEYWORD, &sess) != 1)
		goto done;

	for(size_t i = 0; i < RFID_FILE_FORMAT_COUNT; i++)
	{
		if(strcmp((const char *)data.buf, rfid_file_formats[i].filetype) == 0)
		{
			fmt = &rfid_file_formats[i];
			break;
		}
	}
	if(fmt == NULL)
		goto done;

	/* Validate this format's exact Version. */
	if(GetPrivateProfileStringS(&data, RFID_DATAFILE_VERSION_KEYWORD, &sess) != 1)
		goto done;
	if(strcmp((const char *)data.buf, fmt->version) != 0)
		goto done;

	/* Protocol name -> enum, kept in a LOCAL. Read ONLY the selected format's
	 * field; never fall back to the other format's field name (hybrid files
	 * are rejected). The global credential state is not touched until every
	 * field has validated. */
	if(GetPrivateProfileStringS(&data, fmt->proto_key, &sess) != 1)
		goto done;

	protocol = (uint8_t)lfrfid_get_protocol_by_name(data.buf);
	if(protocol == (uint8_t)PROTOCOL_NO)
		goto done;

	expected = protocol_get_data_size(protocol);
	if(expected == 0 || expected > sizeof(lfrfid_tag_info.uid))
		goto done;

	/* Read ONLY the selected format's data field and validate exactly (Phase 1
	 * hardening): exact token count and exactly two hex digits per byte. A
	 * missing key makes GetPrivateProfileStringS() return != 1 (buffer may be
	 * stale), rejected here without dereferencing stale data. */
	if(GetPrivateProfileStringS(&data, fmt->data_key, &sess) != 1)
		goto done;

	if(!rfid_parse_exact_hex_bytes((const char *)data.buf, parsed, (int)expected))
		goto done;

	/* All fields valid: commit to the global credential state. */
	lfrfid_tag_info.protocol = protocol;
	memcpy(lfrfid_tag_info.uid, parsed, expected);

	if(protocol == LFRFIDProtocolEM4100)
		lfrfid_tag_info.bitrate = 64;
	else if(protocol == LFRFIDProtocolEM4100_32)
		lfrfid_tag_info.bitrate = 32;
	else if(protocol == LFRFIDProtocolEM4100_16)
		lfrfid_tag_info.bitrate = 16;

	ok = true;

done:
	profile_session_close(&sess);
	return ok;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool lfrfid_profile_save(const char *fp, const PLFRFID_TAG_INFO data)
{
    char szString[64];   /* 12-byte spaced hex Data = 35 chars + NUL = 36; 64 for headroom */

    int uid_size = protocol_get_data_size(data->protocol);
    const char *protocol = protocol_get_name(data->protocol);
    if(protocol == NULL)
        return false; /* unregistered protocol index (e.g. compiled out) -- nothing to save */
    sprintf(szString, "%s", protocol);

    /* Filetype */
    if (write_private_profile_string(RFID_DATAFILE_FILETYPE_KEYWORD, RFID_DATAFILE_FILETYPE, fp) == 0)
        return false;

    /* Version */
    if (write_private_profile_string(RFID_DATAFILE_VERSION_KEYWORD, RFID_DATAFILE_VERSION, fp) == 0)
        return false;

    /* PackType (Protocol Name) */
    if (write_private_profile_string(RFID_DATAFILE_PACKTYPE_KEYWORD, szString, fp) == 0)
        return false;

    /* UID → String*/
    uid_to_string(szString, sizeof(szString), data->uid, uid_size);

    /* Data */
    if (write_private_profile_string(RFID_DATAFILE_DATA_KEYWORD, szString, fp) == 0)
        return false;

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
LFRFIDProtocol lfrfid_get_protocol_by_name(const char* name)
{
    for(size_t i = 0; i < LFRFIDProtocolMax; i++) {
        const char *candidate = protocol_get_name(i);
        if(candidate == NULL)
            continue; /* unregistered protocol index (e.g. compiled out) */
        if(strcmp(name, candidate) == 0) {
            return i;
        }
    }
    return PROTOCOL_NO;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
//static FIL rfid_file;
// return
// 0: success
// 1: Limited space available on SD card!
// 2: Error creating directory on SD card!
// 3: user escapes
uint8_t lfrfid_save_file_keyboard(char *filepath, size_t filepath_size)
{
    return m1_save_filename(filepath, filepath_size, RFID_FILEPATH, DRIVE0_RFID "/", RFID_FILE_PREFIX, RFID_FILE_EXTENSION);
}
