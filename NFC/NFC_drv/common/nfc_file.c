/* See COPYING.txt for license details. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_sdcard_man.h"
#include "m1_file_browser.h"
#include "m1_file_util.h"
#include "m1_virtual_kb.h"
#include "m1_storage.h"
#include "nfc_file.h"
#include "nfc_storage.h"
#include "nfc_ctx.h"
#include "uiView.h"
#include "privateprofilestring.h"
#include "m1_nfc.h"
#include "logger.h"
#include "Res_String.h"

#define DRIVE0_NFC     "0:/NFC"
#define NFC_FILE_EXTENSION_TMP		"nfc"
#define NFC_FILE_PREFIX			"nfc_"
#define NFC_FILE_EXTENSION		".nfc"

#define CONCAT_FILEPATH_FILENAME(fpath, fname) fpath fname

// External dump buffers (defined in m1_nfc.c)
extern uint8_t g_nfc_dump_buf[];
extern uint8_t g_nfc_valid_bits[];

/*============================================================================*/
/**
 * @brief Load NFC profile from file
 * 
 * Validates file extension and loads NFC card data from file.
 * Uses nfc_storage_load_file() internally to parse the file.
 * 
 * @param f File info structure from storage_browse()
 * @param ext File extension to validate (e.g., "nfc")
 * @return true on success, false on failure
 */
/*============================================================================*/
bool nfc_profile_load(const S_M1_file_info *f, const char* ext)
{
	char file_path[128];
	nfc_storage_result_t nfc_ret;
	//BaseType_t ret;

	if(IsValidFileSpec(f, ext))
	{
		fu_path_combine(file_path, sizeof(file_path), f->dir_name, f->file_name);

		// Load file using nfc_storage_load_file
		nfc_ret = nfc_storage_load_file(file_path, g_nfc_dump_buf, sizeof(g_nfc_dump_buf),
										g_nfc_valid_bits, sizeof(g_nfc_valid_bits));

		if (nfc_ret == NFC_STORAGE_OK)
		{
			// Save file path to context
			nfc_run_ctx_t* c = nfc_ctx_get();
			if (c) {
				strncpy(c->file.path, file_path, sizeof(c->file.path) - 1);
				c->file.path[sizeof(c->file.path) - 1] = '\0';
			}
			return true;
		}
		else
		{
			platformLog("nfc_storage_load_file('%s') failed: %d\r\n", file_path, nfc_ret);
		}
	}

	return false;
}

/*============================================================================*/
/**
 * @brief Save NFC profile to file
 * 
 * Saves NFC card context data to file in M1 NFC device format.
 * 
 * @param fp Full file path to save to
 * @param ctx NFC context containing card data
 * @return true on success, false on failure
 */
/*============================================================================*/
bool nfc_profile_save(const char *fp, PCNFC_RUN_CTX ctx)
{
	FIL nfc_file;
	char line[128];
	uint8_t ret;

	if (!fp || !ctx) {
		return false;
	}

	ret = m1_fb_open_new_file(&nfc_file, fp);
	if (ret) {
		platformLog("nfc_profile_save: Error creating file '%s'\r\n", fp);
		return false;
	}

	// Write header
	strcpy(line, "Filetype: M1 NFC device\r\n");
	m1_fb_write_to_file(&nfc_file, line, strlen(line));
	strcpy(line, "Version: 4\r\n");
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// Device type
	const char* devtype = "NFC";
	switch (ctx->head.tech) {
	case NFC_TX_A:
		if (ctx->head.a.ats_len > 0) devtype = "ISO14443-4A";
		else if (ctx->head.family == M1NFC_FAM_CLASSIC) devtype = "Classic";
		else if (ctx->head.family == M1NFC_FAM_ULTRALIGHT) devtype = "Ultralight/NTAG";
		else if (ctx->head.family == M1NFC_FAM_DESFIRE) devtype = "DESFire";
		else devtype = "ISO14443A";
		break;
	case NFC_TX_B:  devtype = "ISO14443B";   break;
	case NFC_TX_F:  devtype = "Felica";      break;
	case NFC_TX_V:  devtype = "ISO15693";    break;
	default:        devtype = "NFC";         break;
	}
	snprintf(line, sizeof(line), "Device type: %s\r\n", devtype);
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// Format UID with spaces (like RFID does) for proper parsing
	char uid_str[32];
	int pos = 0;
	for (uint8_t i = 0; i < ctx->head.uid_len && pos < (int)sizeof(uid_str) - 3; i++) {
		pos += snprintf(uid_str + pos, sizeof(uid_str) - pos,
						(i + 1 < ctx->head.uid_len) ? "%02X " : "%02X",
						ctx->head.uid[i]);
	}
	snprintf(line, sizeof(line), "UID: %s\r\n", uid_str);
	m1_fb_write_to_file(&nfc_file, line, strlen(line));

	// ATQA and SAK (for Tech A)
	if (ctx->head.tech == NFC_TX_A) {
		snprintf(line, sizeof(line), "ATQA: %02X %02X\r\n",
				ctx->head.a.atqa[0], ctx->head.a.atqa[1]);
		m1_fb_write_to_file(&nfc_file, line, strlen(line));

		snprintf(line, sizeof(line), "SAK: %02X\r\n", ctx->head.a.sak);
		m1_fb_write_to_file(&nfc_file, line, strlen(line));
	}

	// Save Ultralight (NTAG) page dumps if available
	if ((ctx->head.tech == NFC_TX_A) && (ctx->head.family == M1NFC_FAM_ULTRALIGHT) &&
		ctx->dump.has_dump && ctx->dump.data != NULL &&
		ctx->dump.unit_size == 4 && ctx->dump.unit_count > 0)
	{
		uint32_t page_cnt  = ctx->dump.unit_count;
		uint32_t page_base = ctx->dump.origin;
		const uint8_t *dump = ctx->dump.data;
		const uint8_t *valid = ctx->dump.valid_bits;

		snprintf(line, sizeof(line), "Pages: %lu\r\n", (unsigned long)page_cnt);
		m1_fb_write_to_file(&nfc_file, line, strlen(line));

		for (uint32_t i = 0; i < page_cnt; i++)
		{
			uint8_t is_valid = 1;
			if (valid != NULL)
			{
				uint32_t byte_idx = (i >> 3);        // i / 8
				uint8_t  mask     = (1u << (i & 0x07)); // i % 8
				if ((valid[byte_idx] & mask) == 0)
				{
					is_valid = 0;
				}
			}

			if (!is_valid)
			{
				continue;
			}

			const uint8_t *page = &dump[i * ctx->dump.unit_size];
			uint32_t page_no = page_base + i;

			snprintf(line, sizeof(line),
					"Page %03lu: %02X %02X %02X %02X\r\n",
					(unsigned long)page_no,
					page[0], page[1], page[2], page[3]);
			m1_fb_write_to_file(&nfc_file, line, strlen(line));
		}
	}

	m1_fb_close_file(&nfc_file);
	return true;
}

/*============================================================================*/
/**
 * @brief Get filename from user and create full file path
 * 
 * Prompts user for filename using virtual keyboard, validates
 * SD card space, creates directory if needed, and checks for
 * duplicate filenames.
 * 
 * @param filepath Output buffer for full file path (can be NULL)
 * @return 0 on success
 * @return 1 Limited space available on SD card
 * @return 2 Error creating directory on SD card
 * @return 3 User escaped (cancelled)
 */
/*============================================================================*/
uint8_t nfc_save_file_keyboard(char *filepath)
{
	uint8_t fname[50], dname[50];
	uint8_t ret, error;

	do {
		error = 0;
		m1_sdcard_get_info();
		if ( m1_sdcard_get_free_capacity() < 4 ) // 4096 bytes
		{
			error = 1;
			break;
		}

		if(fs_directory_ensure(DRIVE0_NFC) != FR_OK)
		{
			error = 2;
			break;
		}

		srand(HAL_GetTick());
		while(1)
		{
			sprintf((char*)dname, NFC_FILE_PREFIX"%05u", rand() % 0xFFFFF);
			ret = m1_vkb_get_filename((char*)res_string(IDS_ENTER_FILENAME), (char*)dname, (char*)fname);
			if (!ret )
			{
				error = 3; // user escapes
				break;
			}
			strcpy((char*)dname, CONCAT_FILEPATH_FILENAME(DRIVE0_NFC, "/"));
			strcat((char*)dname, (char*)fname);
			strcat((char*)dname, NFC_FILE_EXTENSION);

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
}

