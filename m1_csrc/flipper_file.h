/* See COPYING.txt for license details. */

/*
 * flipper_file.h
 *
 * Generic Flipper Zero file format parser (key-value line based)
 *
 * M1 Project
 */

#ifndef FLIPPER_FILE_H_
#define FLIPPER_FILE_H_

#include "ff.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FF_LINE_BUF_LEN    512
#define FF_KEY_MAX_LEN     64
#define FF_VALUE_MAX_LEN   448

typedef struct {
	FIL    fh;
	char   line[FF_LINE_BUF_LEN];
	char   key[FF_KEY_MAX_LEN];
	char   value[FF_VALUE_MAX_LEN];
	bool   is_open;
	bool   eof;
} flipper_file_t;

/* Open a file for reading */
bool ff_open(flipper_file_t *ctx, const char *path);

/* Open a file for writing */
bool ff_open_write(flipper_file_t *ctx, const char *path);

/* Close file */
void ff_close(flipper_file_t *ctx);

/* Read next non-comment, non-empty line. Returns false at EOF */
bool ff_read_line(flipper_file_t *ctx);

/* Parse current line as "key: value". Returns false if not key-value format */
bool ff_parse_kv(flipper_file_t *ctx);

/* After ff_parse_kv(), get the key string */
const char *ff_get_key(const flipper_file_t *ctx);

/* After ff_parse_kv(), get the value string */
const char *ff_get_value(const flipper_file_t *ctx);

/* Check if current line is a separator (#) */
bool ff_is_separator(const flipper_file_t *ctx);

/* Validate header: reads first two lines and checks Filetype and Version */
bool ff_validate_header(flipper_file_t *ctx, const char *expected_filetype, uint8_t min_version);

/* Write functions */
bool ff_write_kv_str(flipper_file_t *ctx, const char *key, const char *value);
bool ff_write_kv_uint32(flipper_file_t *ctx, const char *key, uint32_t val);
bool ff_write_kv_float(flipper_file_t *ctx, const char *key, float val);
bool ff_write_kv_hex(flipper_file_t *ctx, const char *key, const uint8_t *data, uint8_t len);
bool ff_write_separator(flipper_file_t *ctx);
bool ff_write_comment(flipper_file_t *ctx, const char *comment);

/* Utility: parse hex bytes from a value string like "07 00 00 00" */
uint8_t ff_parse_hex_bytes(const char *str, uint8_t *out, uint8_t max_len);

/* Utility: parse space-separated int32 values from a data line */
uint16_t ff_parse_int32_array(const char *str, int32_t *out, uint16_t max_count);

#endif /* FLIPPER_FILE_H_ */
