/* See COPYING.txt for license details. */

/*
 * flipper_subghz.c
 *
 * Flipper Zero .sub file format parser for Sub-GHz signals
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
#include "flipper_subghz.h"
#include "m1_sub_ghz.h"

/*************************** D E F I N E S ************************************/

#define FLIPPER_SUBGHZ_RAW_FILETYPE      "Flipper SubGhz RAW File"
#define FLIPPER_SUBGHZ_KEY_FILETYPE      "Flipper SubGhz Key File"
#define FLIPPER_SUBGHZ_MIN_VERSION       1

/* Maximum number of int32 samples to parse from a single RAW_Data line */
#define SUBGHZ_LINE_SAMPLES_MAX          256

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static int subghz_strcasecmp(const char *a, const char *b);
static bool subghz_parse_raw(flipper_file_t *ctx, flipper_subghz_signal_t *out);
static bool subghz_parse_key(flipper_file_t *ctx, flipper_subghz_signal_t *out);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Case-insensitive string comparison (portable)
 */
static int subghz_strcasecmp(const char *a, const char *b)
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
 * @brief  Parse a raw Sub-GHz file body after header validation
 */
static bool subghz_parse_raw(flipper_file_t *ctx, flipper_subghz_signal_t *out)
{
	int32_t line_samples[SUBGHZ_LINE_SAMPLES_MAX];
	uint16_t line_count;
	uint16_t i;

	out->type = FLIPPER_SUBGHZ_TYPE_RAW;
	out->raw_count = 0;

	while (ff_read_line(ctx))
	{
		if (ff_is_separator(ctx))
			continue;

		if (!ff_parse_kv(ctx))
			continue;

		if (subghz_strcasecmp(ff_get_key(ctx), "Frequency") == 0)
		{
			out->frequency = (uint32_t)strtoul(ff_get_value(ctx), NULL, 10);
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Preset") == 0)
		{
			strncpy(out->preset, ff_get_value(ctx), FLIPPER_SUBGHZ_PRESET_MAX_LEN - 1);
			out->preset[FLIPPER_SUBGHZ_PRESET_MAX_LEN - 1] = '\0';
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Protocol") == 0)
		{
			strncpy(out->protocol, ff_get_value(ctx), FLIPPER_SUBGHZ_PROTO_MAX_LEN - 1);
			out->protocol[FLIPPER_SUBGHZ_PROTO_MAX_LEN - 1] = '\0';
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "RAW_Data") == 0)
		{
			/* Parse the int32 samples from this line and append to raw_data */
			line_count = ff_parse_int32_array(ff_get_value(ctx),
			                                  line_samples,
			                                  SUBGHZ_LINE_SAMPLES_MAX);

			for (i = 0; i < line_count && out->raw_count < FLIPPER_SUBGHZ_RAW_MAX_SAMPLES; i++)
			{
				/* Clamp int32 values to int16 range */
				int32_t val = line_samples[i];
				if (val > 32767)
					val = 32767;
				else if (val < -32768)
					val = -32768;

				out->raw_data[out->raw_count++] = (int16_t)val;
			}
		}
	}

	return (out->raw_count > 0);
}

/*============================================================================*/
/**
 * @brief  Parse a key/parsed Sub-GHz file body after header validation
 */
static bool subghz_parse_key(flipper_file_t *ctx, flipper_subghz_signal_t *out)
{
	out->type = FLIPPER_SUBGHZ_TYPE_PARSED;

	while (ff_read_line(ctx))
	{
		if (ff_is_separator(ctx))
			continue;

		if (!ff_parse_kv(ctx))
			continue;

		if (subghz_strcasecmp(ff_get_key(ctx), "Frequency") == 0)
		{
			out->frequency = (uint32_t)strtoul(ff_get_value(ctx), NULL, 10);
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Preset") == 0)
		{
			strncpy(out->preset, ff_get_value(ctx), FLIPPER_SUBGHZ_PRESET_MAX_LEN - 1);
			out->preset[FLIPPER_SUBGHZ_PRESET_MAX_LEN - 1] = '\0';
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Protocol") == 0)
		{
			strncpy(out->protocol, ff_get_value(ctx), FLIPPER_SUBGHZ_PROTO_MAX_LEN - 1);
			out->protocol[FLIPPER_SUBGHZ_PROTO_MAX_LEN - 1] = '\0';
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Bit") == 0)
		{
			out->bit_count = (uint32_t)strtoul(ff_get_value(ctx), NULL, 10);
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "Key") == 0)
		{
			/* Key is in format "00 00 00 00 07 04 D0 03" (8 hex bytes, big-endian) */
			uint8_t key_bytes[8];
			uint8_t count;
			uint8_t i;

			memset(key_bytes, 0, sizeof(key_bytes));
			count = ff_parse_hex_bytes(ff_get_value(ctx), key_bytes, 8);

			out->key = 0;
			for (i = 0; i < count && i < 8; i++)
			{
				out->key = (out->key << 8) | key_bytes[i];
			}
		}
		else if (subghz_strcasecmp(ff_get_key(ctx), "TE") == 0)
		{
			out->te = (uint32_t)strtoul(ff_get_value(ctx), NULL, 10);
		}
	}

	return (out->frequency > 0 && out->protocol[0] != '\0');
}

/*============================================================================*/
/**
 * @brief  Load a .sub file (either RAW or Key format)
 * @param  path  file path on FatFs filesystem
 * @param  out   output signal structure
 * @return true on success
 */
bool flipper_subghz_load(const char *path, flipper_subghz_signal_t *out)
{
	flipper_file_t ff;
	bool result = false;

	if (path == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(flipper_subghz_signal_t));

	if (!ff_open(&ff, path))
		return false;

	/* Read and check Filetype line */
	if (!ff_read_line(&ff) || !ff_parse_kv(&ff))
	{
		ff_close(&ff);
		return false;
	}

	if (strcmp(ff_get_key(&ff), "Filetype") != 0)
	{
		ff_close(&ff);
		return false;
	}

	/* Check Version line */
	if (!ff_read_line(&ff) || !ff_parse_kv(&ff))
	{
		ff_close(&ff);
		return false;
	}

	if (strcmp(ff_get_key(&ff), "Version") != 0)
	{
		ff_close(&ff);
		return false;
	}

	/* Determine file type and parse accordingly */
	if (strcmp(ff.value, "1") == 0 || strcmp(ff.value, "2") == 0)
	{
		/* Need to peek at Filetype to determine RAW vs Key */
		/* We stored the filetype value earlier but need to check it */
		/* Re-open and check filetype */
		ff_close(&ff);

		if (!ff_open(&ff, path))
			return false;

		if (!ff_read_line(&ff) || !ff_parse_kv(&ff))
		{
			ff_close(&ff);
			return false;
		}

		if (strstr(ff_get_value(&ff), "RAW") != NULL)
		{
			/* Skip Version line */
			if (!ff_read_line(&ff) || !ff_parse_kv(&ff))
			{
				ff_close(&ff);
				return false;
			}
			result = subghz_parse_raw(&ff, out);
		}
		else
		{
			/* Skip Version line */
			if (!ff_read_line(&ff) || !ff_parse_kv(&ff))
			{
				ff_close(&ff);
				return false;
			}
			result = subghz_parse_key(&ff, out);
		}
	}

	ff_close(&ff);
	return result;
}

/*============================================================================*/
/**
 * @brief  Save a .sub file
 * @param  path  file path on FatFs filesystem
 * @param  sig   signal structure to save
 * @return true on success
 */
bool flipper_subghz_save(const char *path, const flipper_subghz_signal_t *sig)
{
	flipper_file_t ff;
	bool result = true;

	if (path == NULL || sig == NULL)
		return false;

	if (!ff_open_write(&ff, path))
		return false;

	/* Write header */
	if (sig->type == FLIPPER_SUBGHZ_TYPE_RAW)
	{
		result = ff_write_kv_str(&ff, "Filetype", FLIPPER_SUBGHZ_RAW_FILETYPE);
	}
	else
	{
		result = ff_write_kv_str(&ff, "Filetype", FLIPPER_SUBGHZ_KEY_FILETYPE);
	}

	if (result)
		result = ff_write_kv_uint32(&ff, "Version", 1);

	if (result)
		result = ff_write_kv_uint32(&ff, "Frequency", sig->frequency);

	if (result && sig->preset[0] != '\0')
		result = ff_write_kv_str(&ff, "Preset", sig->preset);

	if (result && sig->protocol[0] != '\0')
		result = ff_write_kv_str(&ff, "Protocol", sig->protocol);

	if (result && sig->type == FLIPPER_SUBGHZ_TYPE_RAW)
	{
		/* Write RAW_Data lines, splitting at reasonable line lengths */
		uint16_t i = 0;
		char buf[FF_VALUE_MAX_LEN];

		while (i < sig->raw_count && result)
		{
			int pos = 0;
			uint16_t line_start = i;

			while (i < sig->raw_count)
			{
				int written = snprintf(&buf[pos], sizeof(buf) - (size_t)pos,
				                       "%s%d",
				                       (i > line_start) ? " " : "",
				                       (int)sig->raw_data[i]);

				if (written < 0 || (pos + written) >= (int)(sizeof(buf) - 8))
					break;

				pos += written;
				i++;
			}
			buf[pos] = '\0';

			result = ff_write_kv_str(&ff, "RAW_Data", buf);
		}
	}
	else if (result && sig->type == FLIPPER_SUBGHZ_TYPE_PARSED)
	{
		char key_str[32];

		if (result)
			result = ff_write_kv_uint32(&ff, "Bit", sig->bit_count);

		/* Write Key as 8-byte big-endian hex */
		if (result)
		{
			snprintf(key_str, sizeof(key_str),
			         "%02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)((sig->key >> 56) & 0xFF),
			         (unsigned int)((sig->key >> 48) & 0xFF),
			         (unsigned int)((sig->key >> 40) & 0xFF),
			         (unsigned int)((sig->key >> 32) & 0xFF),
			         (unsigned int)((sig->key >> 24) & 0xFF),
			         (unsigned int)((sig->key >> 16) & 0xFF),
			         (unsigned int)((sig->key >> 8) & 0xFF),
			         (unsigned int)(sig->key & 0xFF));
			result = ff_write_kv_str(&ff, "Key", key_str);
		}

		if (result && sig->te > 0)
			result = ff_write_kv_uint32(&ff, "TE", sig->te);
	}

	ff_close(&ff);
	return result;
}

/*============================================================================*/
/**
 * @brief  Map Flipper preset name to M1 modulation type
 * @param  preset  Flipper preset string (e.g., "FuriHalSubGhzPresetOok650Async")
 * @return M1 modulation type (S_M1_SubGHz_Modulation enum value)
 */
uint8_t flipper_subghz_preset_to_modulation(const char *preset)
{
	if (preset == NULL)
		return (uint8_t)MODULATION_UNKNOWN;

	/* OOK presets */
	if (strstr(preset, "Ook") != NULL || strstr(preset, "OOK") != NULL)
		return (uint8_t)MODULATION_OOK;

	/* FSK presets */
	if (strstr(preset, "2FSK") != NULL || strstr(preset, "FSK") != NULL)
		return (uint8_t)MODULATION_FSK;

	return (uint8_t)MODULATION_UNKNOWN;
}

/*============================================================================*/
/**
 * @brief  Convert frequency in Hz to closest M1 SI4463 band enum
 * @param  freq_hz  frequency in Hz (e.g., 433920000)
 * @return M1 Sub-GHz band enum value (S_M1_SubGHz_Band)
 */
uint8_t flipper_subghz_freq_to_band(uint32_t freq_hz)
{
	/* Convert to MHz for comparison */
	uint32_t freq_mhz = freq_hz / 1000000;

	if (freq_mhz <= 305)
		return (uint8_t)SUB_GHZ_BAND_300;
	else if (freq_mhz <= 312)
		return (uint8_t)SUB_GHZ_BAND_310;
	else if (freq_mhz <= 320)
		return (uint8_t)SUB_GHZ_BAND_315;
	else if (freq_mhz <= 358)
		return (uint8_t)SUB_GHZ_BAND_345;
	else if (freq_mhz <= 381)
		return (uint8_t)SUB_GHZ_BAND_372;
	else if (freq_mhz <= 412)
		return (uint8_t)SUB_GHZ_BAND_390;
	else if (freq_mhz <= 433)
		return (uint8_t)SUB_GHZ_BAND_433;
	else if (freq_mhz <= 500)
		return (uint8_t)SUB_GHZ_BAND_433_92;
	else
		return (uint8_t)SUB_GHZ_BAND_915;
}
