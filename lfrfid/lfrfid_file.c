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
#include "m1_file_browser.h"
#include "m1_file_util.h"
#include "m1_virtual_kb.h"
#include "lfrfid.h"
#include "uiView.h"
#include "res_string.h"
#include "privateprofilestring.h"
#include "lfrfid_file.h"

/*************************** D E F I N E S ************************************/

#define DRIVE0_RFID     "0:/RFID"
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
bool lfrfid_profile_load(const S_M1_file_info *f, const char* ext)
{
	char file_path[64];
	char buf[200];

	ParsedValue data;

	if(IsValidFileSpec(f, ext))
	{
		fu_path_combine(file_path, sizeof(file_path), f->dir_name, f->file_name);

		data.buf = buf;
		data.max_len = sizeof(buf);

		if(!isValidHeaderField(&data, RFID_DATAFILE_FILETYPE, RFID_DATAFILE_VERSION, file_path))
			return false;

		GetPrivateProfileString(&data,RFID_DATAFILE_PACKTYPE_KEYWORD, file_path);
		lfrfid_tag_info.protocol = lfrfid_get_protocol_by_name(data.buf);

		if(lfrfid_tag_info.protocol == (uint8_t)PROTOCOL_NO)
			return false;

		GetPrivateProfileHex(&data,RFID_DATAFILE_DATA_KEYWORD, file_path);

		memcpy(lfrfid_tag_info.uid, data.buf, data.v.hex.out_len);

		if(lfrfid_tag_info.protocol == 0)
			lfrfid_tag_info.bitrate = 64;
		else if(lfrfid_tag_info.protocol == 1)
			lfrfid_tag_info.bitrate = 32;
		else if(lfrfid_tag_info.protocol == 2)
			lfrfid_tag_info.bitrate = 16;

		//fu_get_filename_without_ext(file_path, lfrfid_tag_info.filename, sizeof(lfrfid_tag_info.filename));

		return true;
	}

	return false;
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
    char szString[32];

    int uid_size = protocol_get_data_size(data->protocol);
    const char *protocol = protocol_get_name(data->protocol);
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
        if(strcmp(name, protocol_get_name(i)) == 0) {
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
uint8_t lfrfid_save_file_keyboard(char *filepath)
{
	uint8_t fname[50], dname[50];
	uint8_t ret, error;

	do {
		error = 0;
	    m1_sdcard_get_info();
	    if ( m1_sdcard_get_free_capacity() < 4 ) // 4096 bytes
	    {
	    	//M1_LOG_E(M1_LOGDB_TAG, "Limited space available on SD card!");
	    	error = 1;
	    	break;
	    }

	    if(fs_directory_ensure(RFID_FILEPATH) != FR_OK)
	    {
	    	error = 2;
	    	break;
	    }

	    srand(HAL_GetTick());
	    while(1)
	    {
	    	sprintf((char*)dname, RFID_FILE_PREFIX"%05u", rand() % 0xFFFFF);
	    	ret = m1_vkb_get_filename((char*)res_string(IDS_ENTER_FILENAME), (char*)dname, (char*)fname);
	    	if (!ret )
	    	{
	    		error = 3; // user escapes
	    		break;
	    	}
	    	strcpy((char*)dname, CONCAT_FILEPATH_FILENAME(DRIVE0_RFID, "/"));
	    	strcat((char*)dname, (char*)fname);
	    	strcat((char*)dname, RFID_FILE_EXTENSION);

	    	if(fs_file_exists((char*)dname) == 1)
	    	{
	    		m1_message_box(&m1_u8g2, res_string(IDS_DUPLICATE_FILE),NULL," ", res_string(IDS_BACK));
	    	}
	    	else
	    		break;

	    }

	    if ( error==3 ) // user escaped?
	    	break;

	} while (0);

	if(error == 0)
	{
		if(filepath)
			strcpy(filepath, (char*)dname);
	}

	return error;
} // static uint8_t rfid_read_more_options_save(void)

