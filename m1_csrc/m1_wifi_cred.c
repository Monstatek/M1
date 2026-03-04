/* See COPYING.txt for license details. */

/*
*
*  m1_wifi_cred.c
*
*  Encrypted WiFi credential storage on SD card.
*  Uses AES-256-CBC encryption from m1_crypto for secure storage.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "m1_wifi_cred.h"
#include "m1_crypto.h"
#include "ff.h"

/*************************** D E F I N E S ************************************/

/*
 * File format (binary):
 *   [magic (4 bytes)] [version (1 byte)] [record_count (1 byte)]
 *   [record 0] [record 1] ... [record N-1]
 *
 * Each record:
 *   [encrypted_len (4 bytes, little-endian)]
 *   [encrypted_data (variable length)]
 *
 * Encrypted data format (before encryption):
 *   [SSID\0] [PASSWORD\0]
 *   (two null-terminated strings concatenated)
 *
 * After encryption:
 *   [IV (16 bytes)] [ciphertext with PKCS7 padding]
 */

#define WIFI_CRED_FILE_MAGIC       0x57494649  /* "WIFI" in little-endian */
#define WIFI_CRED_FILE_VERSION     1
#define WIFI_CRED_HEADER_SIZE      6  /* magic(4) + version(1) + count(1) */

/* Maximum plaintext size for a single credential:
 * SSID (33 bytes max including null) + Password (65 bytes max including null) = 98 bytes
 * After encryption with PKCS7: need room for IV(16) + padded(112) = 128 bytes max */
#define WIFI_CRED_PLAIN_MAX_SIZE   (WIFI_CRED_SSID_MAX_LEN + WIFI_CRED_PASS_MAX_LEN)
#define WIFI_CRED_ENC_MAX_SIZE     (M1_CRYPTO_IV_SIZE + ((WIFI_CRED_PLAIN_MAX_SIZE / M1_CRYPTO_AES_BLOCK_SIZE) + 1) * M1_CRYPTO_AES_BLOCK_SIZE)

/* Static work buffer for encryption/decryption (no heap!) */
#define WIFI_CRED_WORK_BUF_SIZE    256

//************************** S T R U C T U R E S *******************************

//****************************** V A R I A B L E S *****************************/

static uint8_t s_work_buf[WIFI_CRED_WORK_BUF_SIZE];

/* In-memory credential cache for save/delete operations */
static wifi_credential_t s_cred_cache[WIFI_CRED_MAX_STORED];
static uint8_t s_cred_cache_count;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool write_all_credentials(void);
static bool ensure_system_dir(void);
static bool read_file_header(FIL *file, uint8_t *record_count);
static bool write_file_header(FIL *file, uint8_t record_count);
static bool read_record(FIL *file, wifi_credential_t *cred);
static bool write_record(FIL *file, const wifi_credential_t *cred);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/



/*============================================================================*/
/*
 * Ensure 0:/System/ directory exists
 */
/*============================================================================*/
static bool ensure_system_dir(void)
{
	FILINFO fno;
	FRESULT res;

	res = f_stat("0:/System", &fno);
	if (res != FR_OK)
	{
		res = f_mkdir("0:/System");
		if (res != FR_OK)
			return false;
	}
	return true;
} // static bool ensure_system_dir(void)



/*============================================================================*/
/*
 * Read and validate the file header
 */
/*============================================================================*/
static bool read_file_header(FIL *file, uint8_t *record_count)
{
	uint8_t header[WIFI_CRED_HEADER_SIZE];
	UINT br;
	FRESULT res;
	uint32_t magic;

	res = f_read(file, header, WIFI_CRED_HEADER_SIZE, &br);
	if (res != FR_OK || br != WIFI_CRED_HEADER_SIZE)
		return false;

	/* Check magic */
	magic = (uint32_t)header[0] |
	        ((uint32_t)header[1] << 8) |
	        ((uint32_t)header[2] << 16) |
	        ((uint32_t)header[3] << 24);

	if (magic != WIFI_CRED_FILE_MAGIC)
		return false;

	/* Check version */
	if (header[4] != WIFI_CRED_FILE_VERSION)
		return false;

	*record_count = header[5];
	if (*record_count > WIFI_CRED_MAX_STORED)
		*record_count = WIFI_CRED_MAX_STORED;

	return true;
} // static bool read_file_header(...)



/*============================================================================*/
/*
 * Write the file header
 */
/*============================================================================*/
static bool write_file_header(FIL *file, uint8_t record_count)
{
	uint8_t header[WIFI_CRED_HEADER_SIZE];
	UINT bw;
	FRESULT res;

	header[0] = (uint8_t)(WIFI_CRED_FILE_MAGIC & 0xFF);
	header[1] = (uint8_t)((WIFI_CRED_FILE_MAGIC >> 8) & 0xFF);
	header[2] = (uint8_t)((WIFI_CRED_FILE_MAGIC >> 16) & 0xFF);
	header[3] = (uint8_t)((WIFI_CRED_FILE_MAGIC >> 24) & 0xFF);
	header[4] = WIFI_CRED_FILE_VERSION;
	header[5] = record_count;

	res = f_write(file, header, WIFI_CRED_HEADER_SIZE, &bw);
	return (res == FR_OK && bw == WIFI_CRED_HEADER_SIZE);
} // static bool write_file_header(...)



/*============================================================================*/
/*
 * Read a single encrypted record from file and decrypt it
 */
/*============================================================================*/
static bool read_record(FIL *file, wifi_credential_t *cred)
{
	uint8_t len_buf[4];
	uint32_t enc_len;
	uint32_t plain_len;
	UINT br;
	FRESULT res;
	const char *ssid_ptr;
	const char *pass_ptr;
	size_t ssid_len;

	memset(cred, 0, sizeof(wifi_credential_t));

	/* Read encrypted data length (4 bytes, little-endian) */
	res = f_read(file, len_buf, 4, &br);
	if (res != FR_OK || br != 4)
		return false;

	enc_len = (uint32_t)len_buf[0] |
	          ((uint32_t)len_buf[1] << 8) |
	          ((uint32_t)len_buf[2] << 16) |
	          ((uint32_t)len_buf[3] << 24);

	if (enc_len == 0 || enc_len > WIFI_CRED_WORK_BUF_SIZE)
		return false;

	/* Read encrypted data */
	res = f_read(file, s_work_buf, enc_len, &br);
	if (res != FR_OK || br != enc_len)
		return false;

	/* Decrypt */
	plain_len = m1_crypto_decrypt(s_work_buf, enc_len);
	if (plain_len == 0)
		return false;

	/* Parse: SSID\0PASSWORD\0 */
	ssid_ptr = (const char *)s_work_buf;
	ssid_len = strlen(ssid_ptr);

	if (ssid_len == 0 || ssid_len >= WIFI_CRED_SSID_MAX_LEN)
		return false;

	if (ssid_len + 1 >= plain_len)
		return false; /* No room for password */

	pass_ptr = ssid_ptr + ssid_len + 1;

	strncpy(cred->ssid, ssid_ptr, WIFI_CRED_SSID_MAX_LEN - 1);
	cred->ssid[WIFI_CRED_SSID_MAX_LEN - 1] = '\0';

	strncpy(cred->password, pass_ptr, WIFI_CRED_PASS_MAX_LEN - 1);
	cred->password[WIFI_CRED_PASS_MAX_LEN - 1] = '\0';

	cred->valid = true;

	/* Clear work buffer */
	memset(s_work_buf, 0, WIFI_CRED_WORK_BUF_SIZE);

	return true;
} // static bool read_record(...)



/*============================================================================*/
/*
 * Write a single credential as an encrypted record to file
 */
/*============================================================================*/
static bool write_record(FIL *file, const wifi_credential_t *cred)
{
	uint32_t plain_len;
	uint32_t enc_len;
	uint8_t len_buf[4];
	UINT bw;
	FRESULT res;
	size_t ssid_len;
	size_t pass_len;

	if (cred == NULL || !cred->valid)
		return false;

	/* Build plaintext: SSID\0PASSWORD\0 */
	ssid_len = strlen(cred->ssid);
	pass_len = strlen(cred->password);
	plain_len = ssid_len + 1 + pass_len + 1; /* Both null terminators included */

	if (plain_len > WIFI_CRED_PLAIN_MAX_SIZE)
		return false;

	memset(s_work_buf, 0, WIFI_CRED_WORK_BUF_SIZE);
	memcpy(s_work_buf, cred->ssid, ssid_len);
	s_work_buf[ssid_len] = '\0';
	memcpy(&s_work_buf[ssid_len + 1], cred->password, pass_len);
	s_work_buf[ssid_len + 1 + pass_len] = '\0';

	/* Encrypt in-place */
	enc_len = m1_crypto_encrypt(s_work_buf, plain_len, WIFI_CRED_WORK_BUF_SIZE);
	if (enc_len == 0)
		return false;

	/* Write encrypted length (4 bytes, little-endian) */
	len_buf[0] = (uint8_t)(enc_len & 0xFF);
	len_buf[1] = (uint8_t)((enc_len >> 8) & 0xFF);
	len_buf[2] = (uint8_t)((enc_len >> 16) & 0xFF);
	len_buf[3] = (uint8_t)((enc_len >> 24) & 0xFF);

	res = f_write(file, len_buf, 4, &bw);
	if (res != FR_OK || bw != 4)
		return false;

	/* Write encrypted data */
	res = f_write(file, s_work_buf, enc_len, &bw);
	if (res != FR_OK || bw != enc_len)
		return false;

	/* Clear work buffer */
	memset(s_work_buf, 0, WIFI_CRED_WORK_BUF_SIZE);

	return true;
} // static bool write_record(...)



/*============================================================================*/
/*
 * Write all credentials from the in-memory cache to file.
 * Rewrites the entire file.
 */
/*============================================================================*/
static bool write_all_credentials(void)
{
	FIL file;
	FRESULT res;
	uint8_t i;
	uint8_t valid_count = 0;

	if (!ensure_system_dir())
		return false;

	/* Count valid credentials */
	for (i = 0; i < s_cred_cache_count; i++)
	{
		if (s_cred_cache[i].valid)
			valid_count++;
	}

	res = f_open(&file, WIFI_CRED_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return false;

	/* Write header */
	if (!write_file_header(&file, valid_count))
	{
		f_close(&file);
		return false;
	}

	/* Write each valid credential */
	for (i = 0; i < s_cred_cache_count; i++)
	{
		if (s_cred_cache[i].valid)
		{
			if (!write_record(&file, &s_cred_cache[i]))
			{
				f_close(&file);
				return false;
			}
		}
	}

	f_close(&file);
	return true;
} // static bool write_all_credentials(void)



/*============================================================================*/
/*
 * Load all saved credentials from SD card.
 * Decrypts each record automatically.
 * Returns the number of valid credentials loaded.
 */
/*============================================================================*/
uint8_t wifi_cred_load_all(wifi_credential_t *creds, uint8_t max_count)
{
	FIL file;
	FRESULT res;
	uint8_t record_count = 0;
	uint8_t loaded = 0;
	uint8_t i;

	if (creds == NULL || max_count == 0)
		return 0;

	memset(creds, 0, sizeof(wifi_credential_t) * max_count);

	res = f_open(&file, WIFI_CRED_FILE_PATH, FA_READ);
	if (res != FR_OK)
		return 0;

	if (!read_file_header(&file, &record_count))
	{
		f_close(&file);
		return 0;
	}

	for (i = 0; i < record_count && loaded < max_count; i++)
	{
		if (read_record(&file, &creds[loaded]))
		{
			loaded++;
		}
		else
		{
			/* Skip corrupted records - try to continue */
			break; /* Can't reliably skip variable-length records */
		}
	}

	f_close(&file);

	/* Update the internal cache as well */
	s_cred_cache_count = loaded;
	if (loaded > 0)
	{
		memcpy(s_cred_cache, creds,
		       sizeof(wifi_credential_t) * ((loaded < WIFI_CRED_MAX_STORED) ? loaded : WIFI_CRED_MAX_STORED));
	}

	return loaded;
} // uint8_t wifi_cred_load_all(...)



/*============================================================================*/
/*
 * Save a new credential. If the SSID already exists, update the password.
 * Encrypts and writes to SD card.
 * Returns true on success.
 */
/*============================================================================*/
bool wifi_cred_save(const char *ssid, const char *password)
{
	uint8_t i;
	int existing_idx = -1;
	wifi_credential_t temp_creds[WIFI_CRED_MAX_STORED];

	if (ssid == NULL || password == NULL)
		return false;

	if (strlen(ssid) == 0 || strlen(ssid) >= WIFI_CRED_SSID_MAX_LEN)
		return false;

	if (strlen(password) >= WIFI_CRED_PASS_MAX_LEN)
		return false;

	/* Load current credentials into cache if not already loaded */
	if (s_cred_cache_count == 0)
	{
		s_cred_cache_count = wifi_cred_load_all(temp_creds, WIFI_CRED_MAX_STORED);
		if (s_cred_cache_count > 0)
			memcpy(s_cred_cache, temp_creds, sizeof(wifi_credential_t) * s_cred_cache_count);
	}

	/* Check if SSID already exists */
	for (i = 0; i < s_cred_cache_count; i++)
	{
		if (s_cred_cache[i].valid && strcmp(s_cred_cache[i].ssid, ssid) == 0)
		{
			existing_idx = i;
			break;
		}
	}

	if (existing_idx >= 0)
	{
		/* Update existing credential */
		strncpy(s_cred_cache[existing_idx].password, password, WIFI_CRED_PASS_MAX_LEN - 1);
		s_cred_cache[existing_idx].password[WIFI_CRED_PASS_MAX_LEN - 1] = '\0';
	}
	else
	{
		/* Add new credential */
		if (s_cred_cache_count >= WIFI_CRED_MAX_STORED)
			return false; /* Storage full */

		strncpy(s_cred_cache[s_cred_cache_count].ssid, ssid, WIFI_CRED_SSID_MAX_LEN - 1);
		s_cred_cache[s_cred_cache_count].ssid[WIFI_CRED_SSID_MAX_LEN - 1] = '\0';
		strncpy(s_cred_cache[s_cred_cache_count].password, password, WIFI_CRED_PASS_MAX_LEN - 1);
		s_cred_cache[s_cred_cache_count].password[WIFI_CRED_PASS_MAX_LEN - 1] = '\0';
		s_cred_cache[s_cred_cache_count].valid = true;
		s_cred_cache_count++;
	}

	/* Rewrite the entire file */
	return write_all_credentials();
} // bool wifi_cred_save(...)



/*============================================================================*/
/*
 * Delete a credential by SSID.
 * Rewrites the file without the deleted credential.
 * Returns true if the credential was found and deleted.
 */
/*============================================================================*/
bool wifi_cred_delete(const char *ssid)
{
	uint8_t i;
	bool found = false;
	wifi_credential_t temp_creds[WIFI_CRED_MAX_STORED];

	if (ssid == NULL || strlen(ssid) == 0)
		return false;

	/* Load current credentials into cache if not already loaded */
	if (s_cred_cache_count == 0)
	{
		s_cred_cache_count = wifi_cred_load_all(temp_creds, WIFI_CRED_MAX_STORED);
		if (s_cred_cache_count > 0)
			memcpy(s_cred_cache, temp_creds, sizeof(wifi_credential_t) * s_cred_cache_count);
	}

	/* Find and mark the credential as invalid */
	for (i = 0; i < s_cred_cache_count; i++)
	{
		if (s_cred_cache[i].valid && strcmp(s_cred_cache[i].ssid, ssid) == 0)
		{
			/* Shift remaining entries down */
			uint8_t j;
			for (j = i; j < s_cred_cache_count - 1; j++)
			{
				memcpy(&s_cred_cache[j], &s_cred_cache[j + 1], sizeof(wifi_credential_t));
			}

			/* Clear the last slot */
			memset(&s_cred_cache[s_cred_cache_count - 1], 0, sizeof(wifi_credential_t));
			s_cred_cache_count--;
			found = true;
			break;
		}
	}

	if (!found)
		return false;

	/* Rewrite the file without the deleted credential */
	return write_all_credentials();
} // bool wifi_cred_delete(...)



/*============================================================================*/
/*
 * Find a credential by SSID.
 * Returns true if found, with the credential data in *out.
 */
/*============================================================================*/
bool wifi_cred_find(const char *ssid, wifi_credential_t *out)
{
	wifi_credential_t temp_creds[WIFI_CRED_MAX_STORED];
	uint8_t count;
	uint8_t i;

	if (ssid == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(wifi_credential_t));

	/* Try the cache first */
	if (s_cred_cache_count > 0)
	{
		for (i = 0; i < s_cred_cache_count; i++)
		{
			if (s_cred_cache[i].valid && strcmp(s_cred_cache[i].ssid, ssid) == 0)
			{
				memcpy(out, &s_cred_cache[i], sizeof(wifi_credential_t));
				return true;
			}
		}
	}

	/* Cache miss or empty - load from file */
	count = wifi_cred_load_all(temp_creds, WIFI_CRED_MAX_STORED);

	for (i = 0; i < count; i++)
	{
		if (temp_creds[i].valid && strcmp(temp_creds[i].ssid, ssid) == 0)
		{
			memcpy(out, &temp_creds[i], sizeof(wifi_credential_t));

			/* Clear temporary buffer containing passwords */
			memset(temp_creds, 0, sizeof(temp_creds));
			return true;
		}
	}

	/* Clear temporary buffer */
	memset(temp_creds, 0, sizeof(temp_creds));
	return false;
} // bool wifi_cred_find(...)
