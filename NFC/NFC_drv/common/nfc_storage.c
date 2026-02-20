/* See COPYING.txt for license details. */


#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "nfc_storage.h"
#include "m1_sdcard.h"
#include "nfc_fileio.h"
#include "nfc_ctx.h"
#include "m1_nfc.h"     /* M1NFC_FAM_*, M1NFC_TECH_* */
#include "privateprofilestring.h"  /* INI style parsing for header */
#include "logger.h"     /* platformLog */

#define NFC_STORAGE_MIN_DUMP_UNITS  1

typedef struct {
    uint8_t  tech;      /* M1NFC_TECH_* */
    uint8_t  family;    /* M1NFC_FAM_*  */
    uint16_t unit_size; /* Page/block size */
} nfc_family_info_t;


/*============================================================================*/
/**
 * @brief Remove leading whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_ltrim(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/*============================================================================*/
/**
 * @brief Remove trailing whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_rtrim(char* s)
{
    size_t len = strlen(s);
    while (len > 0 &&
           (s[len-1] == ' ' || s[len-1] == '\t' ||
            s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

/*============================================================================*/
/**
 * @brief Remove leading and trailing whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_trim(char* s)
{
    return str_ltrim(str_rtrim(s));
}

/*============================================================================*/
/**
 * @brief Convert a single character to 0~15 hex value
 * @param c Character to convert
 * @return Hex value (0-15) on success, -1 on failure
 */
/*============================================================================*/
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/*============================================================================*/
/**
 * @brief Parse HEX byte sequence from string (supports "AA BB CC" or "AABBCC" format)
 * @param s Input string containing hex bytes
 * @param out Output buffer for parsed bytes
 * @param max Maximum number of bytes to parse
 * @param out_len Pointer to store actual parsed length
 * @return 0 on success, 1 if second digit missing, 2 if invalid char/hex, 3 if buffer full
 */
/*============================================================================*/
static int parse_hex_bytes(char *s, uint8_t *out, size_t max, size_t *out_len)
{
    size_t      n        = 0;
    int         have_hi  = 0;
    uint8_t     hi_nib   = 0;
    unsigned char *p     = (unsigned char *)str_trim(s);

    while (*p)
    {
        /* Skip all whitespace */
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
            continue;
        }

        /* Non-hex character is treated as error */
        if (!isxdigit(*p)) {
            return 2;   // invalid char
        }

        int v = hex_nibble(*p);
        if (v < 0) {
            return 2;   // invalid hex
        }

        if (!have_hi) {
            /* Store upper nibble */
            hi_nib  = (uint8_t)v;
            have_hi = 1;
        } else {
            /* Combine with lower nibble to complete 1 byte */
            if (n >= max) {
                return 3;   // out buffer full
            }
            out[n++] = (uint8_t)((hi_nib << 4) | (uint8_t)v);
            have_hi  = 0;
        }

        p++;
    }

    /* Error if hex string has odd number of characters */
    if (have_hi) {
        return 1;   // second digit missing
    }

    if (out_len) {
        *out_len = n;
    }
    return 0;
}


/*============================================================================*/
/**
 * @brief Mark unit as valid in valid_bits bitmap
 * @param valid_bits Pointer to valid bits bitmap
 * @param valid_bytes Size of valid_bits buffer in bytes
 * @param idx Unit index to mark as valid
 */
/*============================================================================*/
static void mark_unit_valid(uint8_t* valid_bits, uint32_t valid_bytes, uint32_t idx)
{
    if (!valid_bits) return;
    uint32_t byte_idx = idx >> 3;
    uint8_t  bit_mask = (uint8_t)(1u << (idx & 7));
    if (byte_idx >= valid_bytes) return;
    valid_bits[byte_idx] |= bit_mask;
}


/*============================================================================*/
/**
 * @brief Parse device type string and fill family info structure
 * @param s Device type string (e.g., "Classic", "Ultralight/NTAG")
 * @param out Pointer to output structure to fill
 * @return 0 on success, -1 on failure
 */
/*============================================================================*/
static int parse_device_type(const char* s, nfc_family_info_t* out)
{
    if (!s || !out) return -1;

    if (strncmp(s, "Classic", 7) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_CLASSIC;
        out->unit_size = 16;
        return 0;
    }

    //if (strncmp(s, "Mifare Classic", 14) == 0) {
    //    out->tech      = M1NFC_TECH_A;
    //    out->family    = M1NFC_FAM_CLASSIC;
    //    out->unit_size = 16;
    //    return 0;
    //}

    if (strncmp(s, "Ultralight/NTAG", 15) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_ULTRALIGHT;
        out->unit_size = 4;   /* Type2 page */
        return 0;
    }

#ifdef M1NFC_FAM_DESFIRE
    if (strncmp(s, "DESFire", 7) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_DESFIRE;
        out->unit_size = 16;  /* Arbitrary, adjust as needed */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_T4T
    if (strncmp(s, "ISO14443-4A", 11) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_T4T;
        out->unit_size = 4;   /* Logical block unit (arbitrary) */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_FELICA
    if (strncmp(s, "Felica", 6) == 0) {
        out->tech      = M1NFC_TECH_F;
        out->family    = M1NFC_FAM_FELICA;
        out->unit_size = 16;  /* Service block size (arbitrary) */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_15693
    if (strncmp(s, "ISO15693", 8) == 0) {
        out->tech      = M1NFC_TECH_V;
        out->family    = M1NFC_FAM_15693;
        out->unit_size = 4;   /* block_size: can be adjusted per project */
        return 0;
    }
#endif

    return -1;
}

/*============================================================================*/
/**
 * @brief Parse header section using INI style parsing (hybrid approach)
 * 
 * Uses privateprofilestring.c for header fields (Filetype, Version, Device type, UID, ATQA, SAK, ATS)
 * This provides consistency with RFID parsing and better maintainability.
 * 
 * @param c Pointer to NFC context
 * @param faminfo Pointer to output family info structure
 * @param file_path Full path to the NFC file
 * @return NFC_STORAGE_OK on success, error code on failure
 */
/*============================================================================*/
static nfc_storage_result_t nfc_storage_parse_header_ini(
        nfc_run_ctx_t* c,
        nfc_family_info_t* faminfo,
        const char* file_path)
{
    char buf[200];
    ParsedValue data;
    bool saw_filetype = false;
    bool saw_devtype  = false;

    memset(faminfo, 0, sizeof(*faminfo));

    /* 1) Validate Filetype and Version */
    data.buf = buf;
    data.max_len = sizeof(buf);
    if (!isValidHeaderField(&data, "M1 NFC device", "4", file_path)) {
        platformLog("Filetype/Version Parsing Fail\r\n");
        return NFC_STORAGE_ERR_FORMAT;
    }
    saw_filetype = true;

    /* 2) Parse Device type */
    data.buf = buf;
    data.max_len = sizeof(buf);
    data.type = VALUE_TYPE_STRING;
    if (!GetPrivateProfileString(&data, "Device type", file_path)) {
        platformLog("Device type not found\r\n");
        return NFC_STORAGE_ERR_FORMAT;
    }
    if (parse_device_type(data.buf, faminfo) != 0) {
        platformLog("Unsupported Device type: %s\r\n", data.buf);
        return NFC_STORAGE_ERR_UNSUP;
    }
    c->head.tech   = faminfo->tech;
    c->head.family = faminfo->family;
    saw_devtype    = true;

    /* 3) Parse UID */
    // Use temporary buffer like RFID does, then copy to c->head.uid
    uint8_t tmp_uid[10];
    memset(tmp_uid, 0, sizeof(tmp_uid));
    data.buf = tmp_uid;
    data.max_len = sizeof(tmp_uid);
    data.type = VALUE_TYPE_HEX_ARRAY;
    
    // Debug: Check what we're trying to parse
    platformLog("[NFC Storage] Parsing UID from file: %s\r\n", file_path);
    
    if (!GetPrivateProfileHex(&data, "UID", file_path)) {
        platformLog("UID not found\r\n");
        return NFC_STORAGE_ERR_FORMAT;
    }
    
    // Debug: Check parsed result
    //platformLog("[NFC Storage] UID parse result: out_len=%d\r\n", data.v.hex.out_len);
    //platformLog("[NFC Storage] UID tmp buffer: %s\r\n", hex2Str(tmp_uid, data.v.hex.out_len));
    
    c->head.uid_len = (uint8_t)data.v.hex.out_len;
    if (c->head.uid_len == 0 || c->head.uid_len > 10) {
        platformLog("Invalid UID length: %d\r\n", c->head.uid_len);
        return NFC_STORAGE_ERR_FORMAT;
    }
    // Copy parsed UID to context (like RFID does)
    memcpy(c->head.uid, tmp_uid, c->head.uid_len);
    platformLog("[NFC Storage] UID final: len=%d, bytes=%s\r\n", 
                c->head.uid_len, hex2Str(c->head.uid, c->head.uid_len));

    /* 4) Parse ATQA (optional, Tech A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        data.buf = c->head.a.atqa;
        data.max_len = 2;
        data.type = VALUE_TYPE_HEX_ARRAY;
        if (GetPrivateProfileHex(&data, "ATQA", file_path) && data.v.hex.out_len == 2) {
            c->head.a.has_atqa = true;
        }
    }

    /* 5) Parse SAK (optional, Tech A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        uint8_t tmp_sak[1];
        data.buf = tmp_sak;
        data.max_len = 1;
        data.type = VALUE_TYPE_HEX_ARRAY;
        if (GetPrivateProfileHex(&data, "SAK", file_path) && data.v.hex.out_len == 1) {
            c->head.a.sak = tmp_sak[0];
            c->head.a.has_sak = true;
        }
    }

    /* 6) Parse ATS (optional, ISO14443-4A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        data.buf = c->head.a.ats;
        data.max_len = sizeof(c->head.a.ats);
        data.type = VALUE_TYPE_HEX_ARRAY;
        if (GetPrivateProfileHex(&data, "ATS", file_path) && data.v.hex.out_len > 0) {
            c->head.a.ats_len = (uint8_t)data.v.hex.out_len;
        }
    }

    if (!saw_filetype || !saw_devtype) {
        return NFC_STORAGE_ERR_FORMAT;
    }

    return NFC_STORAGE_OK;
}

/*============================================================================*/
/**
 * @brief Store unit data from "Page N:" / "Block N:" line into dump buffer
 * @param dump_buf Dump buffer to store data
 * @param unit_size Size of each unit (page/block)
 * @param unit_count Total number of units
 * @param valid_bits Valid bits bitmap (optional)
 * @param valid_bits_bytes Size of valid_bits buffer
 * @param max_seen_unit Pointer to update maximum seen unit index
 * @param idx Unit index to store
 * @param src_bytes Source data bytes
 * @param src_len Length of source data
 */
/*============================================================================*/
static void nfc_storage_store_unit(
        uint8_t*    dump_buf,
        uint16_t    unit_size,
        uint32_t    unit_count,
        uint8_t*    valid_bits,
        uint32_t    valid_bits_bytes,
        uint32_t*   max_seen_unit,
        uint32_t    idx,
        const uint8_t* src_bytes,
        size_t      src_len)
{
    if (!dump_buf || unit_size == 0 || unit_count == 0) return;

    if (src_len < unit_size) {
        /* Pad remaining part with 0 if insufficient */
        uint8_t tmp[32] = {0};
        size_t copy = src_len;
        if (copy > sizeof(tmp)) copy = sizeof(tmp);
        memcpy(tmp, src_bytes, copy);

        if (idx < unit_count) {
            memcpy(dump_buf + idx * unit_size, tmp, unit_size);
            mark_unit_valid(valid_bits, valid_bits_bytes, idx);
            if (max_seen_unit && idx > *max_seen_unit) *max_seen_unit = idx;
        }
        return;
    }

    if (idx >= unit_count) return; /* Buffer overflow → clip */

    memcpy(dump_buf + idx * unit_size, src_bytes, unit_size);
    mark_unit_valid(valid_bits, valid_bits_bytes, idx);
    if (max_seen_unit && idx > *max_seen_unit) *max_seen_unit = idx;
}

/*============================================================================*/
/**
 * @brief Parse body section: "Page N:" for Type2, "Block N:" for Classic
 * @param c Pointer to NFC context
 * @param faminfo Pointer to family info structure
 * @param dump_buf Dump buffer to store parsed data
 * @param dump_buf_bytes Size of dump buffer
 * @param valid_bits Valid bits bitmap (optional)
 * @param valid_bits_bytes Size of valid_bits buffer
 * @return NFC_STORAGE_OK on success, error code on failure
 * @note Felica, 15693, etc. can parse header only for now
 */
/*============================================================================*/
static nfc_storage_result_t nfc_storage_parse_body(
        nfc_run_ctx_t*     c,
        const nfc_family_info_t* faminfo,
        uint8_t*           dump_buf,
        uint32_t           dump_buf_bytes,
        uint8_t*           valid_bits,
        uint32_t           valid_bits_bytes)
{
    if (!dump_buf || dump_buf_bytes == 0)
        return NFC_STORAGE_ERR_NO_BUFFER;

    nfc_parser_t* ps = &c->parser;

    uint16_t unit_size  = faminfo->unit_size;
    if (unit_size == 0) unit_size = 4;
    uint32_t unit_count = dump_buf_bytes / unit_size;
    if (unit_count < NFC_STORAGE_MIN_DUMP_UNITS)
        return NFC_STORAGE_ERR_NO_BUFFER;

    memset(dump_buf, 0, dump_buf_bytes);
    if (valid_bits && valid_bits_bytes > 0)
        memset(valid_bits, 0, valid_bits_bytes);

    uint32_t max_seen = 0;

    while (1) {
        int n = nfcfio_getline(&ps->io, ps->line, sizeof(ps->line));
        if (n == -1) break;          /* EOF */
        if (n < 0)  return NFC_STORAGE_ERR_IO;

        char* line = str_trim(ps->line);
        if (line[0] == '\0') continue;
        if (line[0] == '#')  continue;

        /* Classic: "Block N:" */
        if (strncmp(line, "Block ", 6) == 0) {
            unsigned long idx = 0;
            char* colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (sscanf(line, "Block %lu", &idx) != 1) continue;

            char* data_str = str_trim(colon + 1);
            uint8_t bytes[32];
            size_t  len = 0;
            uint8_t b = parse_hex_bytes(data_str, bytes, sizeof(bytes), &len);
            if (b != 0)
            {
                platformLog("Block Parsing Fail [%d]\r\n",b);
                return NFC_STORAGE_ERR_FORMAT;
            }
            nfc_storage_store_unit(dump_buf, unit_size, unit_count,
                                   valid_bits, valid_bits_bytes,
                                   &max_seen, (uint32_t)idx,
                                   bytes, len);
            continue;
        }

        /* Type2: "Page N:" */
        if (strncmp(line, "Page ", 5) == 0) {
            unsigned long idx = 0;
            char* colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (sscanf(line, "Page %lu", &idx) != 1) continue;

            char* data_str = str_trim(colon + 1);
            uint8_t bytes[32];
            size_t  len = 0;
            uint8_t b = parse_hex_bytes(data_str, bytes, sizeof(bytes), &len);
            if (b != 0)
            {
                platformLog("Page Parsing Fail [%d]\r\n",b);
                return NFC_STORAGE_ERR_FORMAT;
            }
            nfc_storage_store_unit(dump_buf, unit_size, unit_count,
                                   valid_bits, valid_bits_bytes,
                                   &max_seen, (uint32_t)idx,
                                   bytes, len);
            continue;
        }

        /* Felica/ISO15693, etc. can add separate processing here */
    }

    /* Parsing complete → set nfc_ctx.dump metadata */
    nfc_ctx_set_dump(unit_size,
                     unit_count,
                     0,             /* origin */
                     dump_buf,
                     valid_bits,
                     max_seen,
                     true); //dump set

    return NFC_STORAGE_OK;
}


/*============================================================================*/
/**
 * @brief Load and parse .nfc text file to fill nfc_ctx
 * 
 * Parses .nfc file format:
 * - Header: Filetype, Version, Device type, UID, ATQA, SAK, ATS, etc.
 * - Body: "Page N:" / "Block N:" lines → stored in dump buffer
 * 
 * @param path Full path on SD card (e.g., "/NFC/card1.nfc")
 * @param dump_buf Workspace pointer to store dump data
 * @param dump_buf_bytes Total size of dump_buf (in bytes)
 * @param valid_bits Unit validity bitmap (optional, NULL allowed)
 * @param valid_bits_bytes Size of valid_bits buffer (bytes, 1 byte per 8 units)
 * @return NFC_STORAGE_OK on success, error code on failure
 */
/*============================================================================*/
nfc_storage_result_t nfc_storage_load_file(
        const char* path,
        uint8_t*    dump_buf,
        uint32_t    dump_buf_bytes,
        uint8_t*    valid_bits,
        uint32_t    valid_bits_bytes)

{
    if (!path || !dump_buf || dump_buf_bytes == 0)
        return NFC_STORAGE_ERR_NO_BUFFER;

    nfc_run_ctx_t* c = nfc_ctx_get();
    nfc_family_info_t faminfo;
    nfc_storage_result_t ret;

    /* Start file session: set source_kind, path */
    nfc_ctx_begin_file(path);

    /* Initialize header/dump */
    memset(&c->head, 0, sizeof(c->head));
    nfc_ctx_clear_dump();
    c->parser.parse_error = 0;

    /* ---------- Phase 1: Parse header using INI style ---------- */
    ret = nfc_storage_parse_header_ini(c, &faminfo, path);

    if (ret != NFC_STORAGE_OK) {
        c->file.sys_error = 1;
        return ret;
    }

    /* ---------- Phase 2: Parse body (pages/blocks) ---------- */
    if (!nfcfio_open_read(&c->parser.io, path)) {
        c->file.sys_error = 1;
        return NFC_STORAGE_ERR_IO;
    }

    ret = nfc_storage_parse_body(c, &faminfo,
                                 dump_buf, dump_buf_bytes,
                                 valid_bits, valid_bits_bytes);

    nfcfio_close(&c->parser.io);

    if (ret == NFC_STORAGE_OK) {
        nfc_ctx_refresh_ui();
        c->file.sys_error = 0;
    } else {
        c->file.sys_error = 1;
    }

    return ret;
}        
