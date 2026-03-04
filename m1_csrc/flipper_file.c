/* See COPYING.txt for license details. */

/*
 * flipper_file.c
 *
 * Generic Flipper Zero file format parser (key-value line based)
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
#include "flipper_file.h"

/*************************** D E F I N E S ************************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void ff_strip_trailing_whitespace(char *str);
static bool ff_line_is_comment(const char *line);
static bool ff_line_is_empty(const char *line);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Strip trailing whitespace (CR, LF, spaces, tabs) from a string
 * @param  str  string to modify in place
 */
static void ff_strip_trailing_whitespace(char *str)
{
	int len = (int)strlen(str);

	while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' ||
	                    str[len - 1] == ' '  || str[len - 1] == '\t'))
	{
		str[--len] = '\0';
	}
}

/*============================================================================*/
/**
 * @brief  Check if a line is a comment (starts with '#')
 */
static bool ff_line_is_comment(const char *line)
{
	const char *p = line;

	/* skip leading whitespace */
	while (*p == ' ' || *p == '\t')
		p++;

	return (*p == '#');
}

/*============================================================================*/
/**
 * @brief  Check if a line is empty (only whitespace)
 */
static bool ff_line_is_empty(const char *line)
{
	const char *p = line;

	while (*p != '\0')
	{
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return false;
		p++;
	}
	return true;
}

/*============================================================================*/
/**
 * @brief  Open a Flipper file for reading
 * @param  ctx   flipper file context
 * @param  path  file path on FatFs filesystem
 * @return true on success
 */
bool ff_open(flipper_file_t *ctx, const char *path)
{
	FRESULT res;

	if (ctx == NULL || path == NULL)
		return false;

	memset(ctx, 0, sizeof(flipper_file_t));

	res = f_open(&ctx->fh, path, FA_READ | FA_OPEN_EXISTING);
	if (res != FR_OK)
		return false;

	ctx->is_open = true;
	ctx->eof = false;
	return true;
}

/*============================================================================*/
/**
 * @brief  Open a Flipper file for writing (create or overwrite)
 * @param  ctx   flipper file context
 * @param  path  file path on FatFs filesystem
 * @return true on success
 */
bool ff_open_write(flipper_file_t *ctx, const char *path)
{
	FRESULT res;

	if (ctx == NULL || path == NULL)
		return false;

	memset(ctx, 0, sizeof(flipper_file_t));

	res = f_open(&ctx->fh, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return false;

	ctx->is_open = true;
	ctx->eof = false;
	return true;
}

/*============================================================================*/
/**
 * @brief  Close an open Flipper file
 * @param  ctx  flipper file context
 */
void ff_close(flipper_file_t *ctx)
{
	if (ctx == NULL)
		return;

	if (ctx->is_open)
	{
		f_close(&ctx->fh);
		ctx->is_open = false;
	}
}

/*============================================================================*/
/**
 * @brief  Read the next non-empty, non-comment line from the file.
 *         Comment lines starting with '#' are detected and cause
 *         ff_is_separator() to return true. The line is stored in ctx->line.
 * @param  ctx  flipper file context
 * @return true if a line was read, false at EOF or error
 */
bool ff_read_line(flipper_file_t *ctx)
{
	if (ctx == NULL || !ctx->is_open || ctx->eof)
		return false;

	for (;;)
	{
		if (f_gets(ctx->line, FF_LINE_BUF_LEN, &ctx->fh) == NULL)
		{
			ctx->eof = true;
			return false;
		}

		ff_strip_trailing_whitespace(ctx->line);

		/* Skip completely empty lines */
		if (ff_line_is_empty(ctx->line))
			continue;

		/* If it is a comment/separator line, keep it (for ff_is_separator) and return */
		if (ff_line_is_comment(ctx->line))
			return true;

		/* Non-empty, non-comment line */
		return true;
	}
}

/*============================================================================*/
/**
 * @brief  Parse the current line as "key: value" format.
 *         The key is stored in ctx->key and the value in ctx->value.
 * @param  ctx  flipper file context
 * @return true if the line was successfully parsed as key-value
 */
bool ff_parse_kv(flipper_file_t *ctx)
{
	char *sep;
	char *k_start;
	char *k_end;
	char *v_start;
	size_t k_len;
	size_t v_len;

	if (ctx == NULL)
		return false;

	ctx->key[0] = '\0';
	ctx->value[0] = '\0';

	/* Find the first ": " separator */
	sep = strstr(ctx->line, ": ");
	if (sep == NULL)
		return false;

	/* Extract key: trim leading/trailing whitespace */
	k_start = ctx->line;
	while (*k_start == ' ' || *k_start == '\t')
		k_start++;

	k_end = sep;
	while (k_end > k_start && (*(k_end - 1) == ' ' || *(k_end - 1) == '\t'))
		k_end--;

	k_len = (size_t)(k_end - k_start);
	if (k_len == 0 || k_len >= FF_KEY_MAX_LEN)
		return false;

	memcpy(ctx->key, k_start, k_len);
	ctx->key[k_len] = '\0';

	/* Extract value: skip past ": " and trim leading whitespace */
	v_start = sep + 2;
	while (*v_start == ' ' || *v_start == '\t')
		v_start++;

	v_len = strlen(v_start);
	if (v_len >= FF_VALUE_MAX_LEN)
		v_len = FF_VALUE_MAX_LEN - 1;

	memcpy(ctx->value, v_start, v_len);
	ctx->value[v_len] = '\0';

	return true;
}

/*============================================================================*/
/**
 * @brief  Get the key string from the last parsed key-value line
 */
const char *ff_get_key(const flipper_file_t *ctx)
{
	if (ctx == NULL)
		return "";
	return ctx->key;
}

/*============================================================================*/
/**
 * @brief  Get the value string from the last parsed key-value line
 */
const char *ff_get_value(const flipper_file_t *ctx)
{
	if (ctx == NULL)
		return "";
	return ctx->value;
}

/*============================================================================*/
/**
 * @brief  Check if the current line is a separator comment (starts with '#')
 */
bool ff_is_separator(const flipper_file_t *ctx)
{
	if (ctx == NULL)
		return false;
	return ff_line_is_comment(ctx->line);
}

/*============================================================================*/
/**
 * @brief  Validate the Flipper file header (Filetype and Version lines).
 *         Reads the first two key-value lines and checks them.
 * @param  ctx               flipper file context (must be open for reading)
 * @param  expected_filetype expected value of the "Filetype" field
 * @param  min_version       minimum acceptable version number
 * @return true if header is valid
 */
bool ff_validate_header(flipper_file_t *ctx, const char *expected_filetype, uint8_t min_version)
{
	uint32_t version;

	if (ctx == NULL || expected_filetype == NULL)
		return false;

	/* Read and check Filetype line */
	if (!ff_read_line(ctx))
		return false;
	if (!ff_parse_kv(ctx))
		return false;
	if (strcmp(ctx->key, "Filetype") != 0)
		return false;
	if (strcmp(ctx->value, expected_filetype) != 0)
		return false;

	/* Read and check Version line */
	if (!ff_read_line(ctx))
		return false;
	if (!ff_parse_kv(ctx))
		return false;
	if (strcmp(ctx->key, "Version") != 0)
		return false;

	version = (uint32_t)strtoul(ctx->value, NULL, 10);
	if (version < min_version)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a key-value pair with a string value
 */
bool ff_write_kv_str(flipper_file_t *ctx, const char *key, const char *value)
{
	if (ctx == NULL || !ctx->is_open || key == NULL || value == NULL)
		return false;

	if (f_printf(&ctx->fh, "%s: %s\n", key, value) < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a key-value pair with a uint32 value
 */
bool ff_write_kv_uint32(flipper_file_t *ctx, const char *key, uint32_t val)
{
	if (ctx == NULL || !ctx->is_open || key == NULL)
		return false;

	if (f_printf(&ctx->fh, "%s: %lu\n", key, (unsigned long)val) < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a key-value pair with a float value (2 decimal places)
 */
bool ff_write_kv_float(flipper_file_t *ctx, const char *key, float val)
{
	char buf[32];
	int whole;
	int frac;

	if (ctx == NULL || !ctx->is_open || key == NULL)
		return false;

	/* Manual float formatting since embedded printf may not support %f */
	whole = (int)val;
	frac = (int)((val - (float)whole) * 100.0f);
	if (frac < 0)
		frac = -frac;

	snprintf(buf, sizeof(buf), "%d.%02d", whole, frac);

	if (f_printf(&ctx->fh, "%s: %s\n", key, buf) < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a key-value pair with hex byte data (e.g., "07 00 00 00")
 */
bool ff_write_kv_hex(flipper_file_t *ctx, const char *key, const uint8_t *data, uint8_t len)
{
	char buf[FF_VALUE_MAX_LEN];
	uint8_t i;
	int pos = 0;

	if (ctx == NULL || !ctx->is_open || key == NULL || data == NULL)
		return false;

	for (i = 0; i < len && pos < (int)(sizeof(buf) - 4); i++)
	{
		if (i > 0)
			buf[pos++] = ' ';

		pos += snprintf(&buf[pos], sizeof(buf) - (size_t)pos, "%02X", data[i]);
	}
	buf[pos] = '\0';

	if (f_printf(&ctx->fh, "%s: %s\n", key, buf) < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a separator comment line ('#')
 */
bool ff_write_separator(flipper_file_t *ctx)
{
	if (ctx == NULL || !ctx->is_open)
		return false;

	if (f_printf(&ctx->fh, "#\n") < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Write a comment line
 */
bool ff_write_comment(flipper_file_t *ctx, const char *comment)
{
	if (ctx == NULL || !ctx->is_open || comment == NULL)
		return false;

	if (f_printf(&ctx->fh, "# %s\n", comment) < 0)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Parse hex bytes from a space-separated string like "07 00 00 00"
 * @param  str      input string
 * @param  out      output buffer for parsed bytes
 * @param  max_len  maximum number of bytes to parse
 * @return number of bytes parsed
 */
uint8_t ff_parse_hex_bytes(const char *str, uint8_t *out, uint8_t max_len)
{
	uint8_t count = 0;
	const char *p = str;

	if (str == NULL || out == NULL || max_len == 0)
		return 0;

	while (*p != '\0' && count < max_len)
	{
		/* Skip whitespace */
		while (*p == ' ' || *p == '\t')
			p++;

		if (*p == '\0')
			break;

		/* Parse two hex digits */
		if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]))
		{
			unsigned int val = 0;
			char hex[3] = { p[0], p[1], '\0' };
			val = (unsigned int)strtoul(hex, NULL, 16);
			out[count++] = (uint8_t)val;
			p += 2;
		}
		else if (isxdigit((unsigned char)p[0]))
		{
			/* Single hex digit */
			unsigned int val = 0;
			char hex[2] = { p[0], '\0' };
			val = (unsigned int)strtoul(hex, NULL, 16);
			out[count++] = (uint8_t)val;
			p += 1;
		}
		else
		{
			/* Skip invalid character */
			p++;
		}
	}

	return count;
}

/*============================================================================*/
/**
 * @brief  Parse space-separated int32 values from a string.
 *         Handles positive and negative values like "9024 -4512 579 -552"
 * @param  str        input string
 * @param  out        output buffer for parsed values
 * @param  max_count  maximum number of values to parse
 * @return number of values parsed
 */
uint16_t ff_parse_int32_array(const char *str, int32_t *out, uint16_t max_count)
{
	uint16_t count = 0;
	const char *p = str;
	char *end;

	if (str == NULL || out == NULL || max_count == 0)
		return 0;

	while (*p != '\0' && count < max_count)
	{
		/* Skip whitespace */
		while (*p == ' ' || *p == '\t')
			p++;

		if (*p == '\0')
			break;

		/* Parse an integer (strtol handles negative signs) */
		out[count] = (int32_t)strtol(p, &end, 10);

		/* Check if any characters were consumed */
		if (end == p)
		{
			/* No valid number found, skip this character */
			p++;
			continue;
		}

		count++;
		p = end;
	}

	return count;
}
