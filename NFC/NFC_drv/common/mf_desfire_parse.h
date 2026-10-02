/* See COPYING.txt for license details. */

/*
 * mf_desfire_parse.h
 *
 * MIFARE DESFire deeper-read data model and byte-level parsers.
 *
 * The implementation uses static allocation and bounded capacities for keys,
 * applications, files and captured data. The limits are chosen conservatively
 * for real-world card content and the M1 RAM budget. Every
 * enumeration is bounds-checked and reports truncation explicitly rather
 * than silently dropping data -- see mf_desfire_apps_truncated /
 * mf_desfire_file_t.read_status.
 *
 * The functional implementation and modifications were
 * independently developed by Monstatek.
 *
 * Copyright (C) 2026 Monstatek
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * DISCLOSED SCOPE: read-only. No authentication is ever attempted. No file
 * is ever read unless its access rights already grant free (0x0E), plain
 * (unencrypted) read to an unauthenticated reader. No writing, no
 * emulation, no key recovery.
 */
#ifndef MF_DESFIRE_PARSE_H_
#define MF_DESFIRE_PARSE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================= Native command bytes =========================*/
/* DESFire command byte values. */
#define MF_DESFIRE_CMD_GET_VERSION          0x60U
#define MF_DESFIRE_CMD_GET_FREE_MEMORY      0x6EU
#define MF_DESFIRE_CMD_GET_KEY_SETTINGS     0x45U
#define MF_DESFIRE_CMD_GET_KEY_VERSION      0x64U
#define MF_DESFIRE_CMD_GET_APPLICATION_IDS  0x6AU
#define MF_DESFIRE_CMD_SELECT_APPLICATION   0x5AU
#define MF_DESFIRE_CMD_GET_FILE_IDS         0x6FU
#define MF_DESFIRE_CMD_GET_FILE_SETTINGS    0xF5U
#define MF_DESFIRE_CMD_READ_DATA            0xBDU
#define MF_DESFIRE_CMD_GET_VALUE            0x6CU
#define MF_DESFIRE_CMD_READ_RECORDS         0xBBU
#define MF_DESFIRE_CMD_ADDITIONAL_FRAME     0xAFU

/*============================= Status codes ==================================*/
/* Full status-code table. Only a subset is distinguished into a specific
 * mf_desfire_status_t; everything else is a protocol error. */
#define MF_DESFIRE_STATUS_OPERATION_OK          0x00U
#define MF_DESFIRE_STATUS_NO_CHANGES            0x0CU
#define MF_DESFIRE_STATUS_OUT_OF_EEPROM_ERROR   0x0EU
#define MF_DESFIRE_STATUS_ILLEGAL_COMMAND_CODE  0x1CU
#define MF_DESFIRE_STATUS_INTEGRITY_ERROR       0x1EU
#define MF_DESFIRE_STATUS_NO_SUCH_KEY           0x40U
#define MF_DESFIRE_STATUS_LENGTH_ERROR          0x7EU
#define MF_DESFIRE_STATUS_PERMISSION_DENIED     0x9DU
#define MF_DESFIRE_STATUS_PARAMETER_ERROR       0x9EU
#define MF_DESFIRE_STATUS_APPLICATION_NOT_FOUND 0xA0U
#define MF_DESFIRE_STATUS_APPL_INTEGRITY_ERROR  0xA1U
#define MF_DESFIRE_STATUS_AUTHENTICATION_ERROR  0xAEU
#define MF_DESFIRE_STATUS_ADDITIONAL_FRAME      0xAFU
#define MF_DESFIRE_STATUS_BOUNDARY_ERROR        0xBEU
#define MF_DESFIRE_STATUS_PICC_INTEGRITY_ERROR  0xC1U
#define MF_DESFIRE_STATUS_COMMAND_ABORTED       0xCAU
#define MF_DESFIRE_STATUS_PICC_DISABLED_ERROR   0xCDU
#define MF_DESFIRE_STATUS_COUNT_ERROR           0xCEU
#define MF_DESFIRE_STATUS_DUPLICATE_ERROR       0xDEU
#define MF_DESFIRE_STATUS_EEPROM_ERROR          0xEEU
#define MF_DESFIRE_STATUS_FILE_NOT_FOUND        0xF0U
#define MF_DESFIRE_STATUS_FILE_INTEGRITY_ERROR  0xF1U

/* Classified outcome of a single DESFire status byte. This is the type that
 * enforces the "conclusive vs transient" distinction the retry rules
 * require: every value below except None/AdditionalFrame is a CONCLUSIVE
 * protocol-level answer and must never be retried or overwritten by a
 * later transport timeout. */
typedef enum {
    MfDesfireStatusOk = 0,             /* OPERATION_OK -- final frame, success */
    MfDesfireStatusAdditionalFrame,    /* 0xAF -- more data follows, not an error */
    MfDesfireStatusAuthRequired,       /* AUTHENTICATION_ERROR -- conclusive */
    MfDesfireStatusPermissionDenied,   /* PERMISSION_DENIED -- conclusive */
    MfDesfireStatusAppNotFound,        /* APPLICATION_NOT_FOUND -- conclusive */
    MfDesfireStatusFileNotFound,       /* FILE_NOT_FOUND -- conclusive */
    MfDesfireStatusIllegalCommand,     /* ILLEGAL_COMMAND_CODE -- command unsupported by this card, conclusive */
    MfDesfireStatusBoundaryError,      /* BOUNDARY_ERROR -- offset/length past end of file, conclusive */
    MfDesfireStatusLengthError,        /* LENGTH_ERROR -- conclusive */
    MfDesfireStatusProtocolError       /* any other named status code -- conclusive, generic protocol error */
} mf_desfire_status_t;

/* Classifies a raw DESFire status byte. Never returns a "maybe" -- every
 * defined status byte maps to a definite, conclusive outcome (or to OK/
 * AdditionalFrame, the two non-error outcomes). This is the single place
 * that decides "is this an error a caller may treat as transient", and the
 * answer is always no: only an RFAL/transport-level ReturnCode (a
 * completely separate type, never a DESFire status byte) can be transient. */
mf_desfire_status_t mf_desfire_classify_status(uint8_t status_byte);

/* True for status values treated as "not present" rather than fatal when they occur
 * at specific points (GetFreeMemory on an old card; GetApplicationIDs/
 * GetKeySettings under auth): AuthRequired, IllegalCommand. Callers still
 * record WHY the item is missing (mf_desfire_file_read_status_t /
 * app-level flags) -- this is never used to suppress or hide the
 * distinction between "complete" and "protected/unsupported". */
bool mf_desfire_status_is_non_fatal_for_enumeration(mf_desfire_status_t s);

/*============================= Size caps ======================================*/
/* Key and file caps reflect DESFire hardware limits. */
#define MF_DESFIRE_MAX_KEYS            14U
#define MF_DESFIRE_MAX_FILES_PER_APP   32U
#define MF_DESFIRE_UID_SIZE            7U
#define MF_DESFIRE_APP_ID_SIZE         3U

/* M1-specific bounded caps. Chosen conservatively:
 * real-world access/transit cards almost always have 1-3 applications with
 * a handful of files each; anything beyond these caps is reported as
 * truncated, never silently dropped. */
#define MF_DESFIRE_DEEP_MAX_APPS        4U
#define MF_DESFIRE_DEEP_FILE_DATA_CAP   64U   /* bytes of file content captured per file */

/*============================= Key settings ===================================*/
/* DESFire key-settings 1-byte bitfield. */
typedef struct {
    bool     valid;
    bool     change_key_id_present;   /* the "master key changeable by another key" concept exists on this card */
    uint8_t  change_key_id;           /* 4-bit field */
    bool     config_changeable;
    bool     free_create_delete;
    bool     free_directory_list;
    bool     key_changeable;
    uint8_t  max_keys;                /* 4-bit field, 0 means "card reports zero keys" -- see the
                                        * zero-key hardening note below, never used as a divisor/index bound
                                        * without an explicit >0 check */
    uint8_t  flags;                   /* 4-bit reserved/vendor field, preserved raw */
} mf_desfire_key_settings_t;

/* Parses the 1-byte GetKeySettings response body (buf[0], after the status
 * byte has already been stripped by the caller). Returns false only on a
 * NULL/zero-length input (a parse failure, distinct from "card has zero
 * keys", which is a valid parsed value: max_keys==0). */
bool mf_desfire_key_settings_parse(const uint8_t *buf, size_t len, mf_desfire_key_settings_t *out);

/*============================= File types / settings =========================*/
/* DESFire file types. TransactionMac is recognized for settings parsing only;
 * there is no dedicated data-read command for it. */
typedef enum {
    MfDesfireFileTypeStandard = 0,
    MfDesfireFileTypeBackup = 1,
    MfDesfireFileTypeValue = 2,
    MfDesfireFileTypeLinearRecord = 3,
    MfDesfireFileTypeCyclicRecord = 4,
    MfDesfireFileTypeTransactionMac = 5,
    MfDesfireFileTypeUnknown = 0xFF
} mf_desfire_file_type_t;

/* DESFire file communication settings. Value 2 is reserved on real cards and
 * is preserved as-is if encountered rather than
 * rejected. */
typedef enum {
    MfDesfireCommPlaintext = 0,
    MfDesfireCommAuthenticated = 1,
    MfDesfireCommReserved2 = 2,
    MfDesfireCommEnciphered = 3
} mf_desfire_comm_t;

typedef struct {
    bool     valid;
    mf_desfire_file_type_t type;
    mf_desfire_comm_t      comm;
    uint16_t access_rights;     /* raw 16-bit field: read/write/rw/change nibbles */
    /* Standard/Backup: */
    uint32_t size;              /* 3-byte LE field, max 0xFFFFFF */
    /* Value: */
    int32_t  value_lo_limit;
    int32_t  value_hi_limit;
    int32_t  value_limited_credit;
    bool     value_limited_credit_enabled;
    /* Linear/Cyclic record: */
    uint32_t record_size;
    uint32_t record_max;
    uint32_t record_cur;
    /* TransactionMac settings only; no data command. */
    uint8_t  tmac_key_option;
    uint8_t  tmac_key_version;
} mf_desfire_file_settings_t;

/* Parses a GetFileSettings response body (status byte already stripped).
 * Handles the type-tagged union, including the "additional access
 * rights" tail when (comm_byte & 0x80) is set (parsed for length-bounds
 * correctness but not otherwise surfaced -- no M1 UI currently shows
 * per-key access rights beyond the primary rights word).
 * Returns false on a malformed/truncated body (explicit parse failure, per
 * the milestone's error rules -- never guesses at missing bytes). */
bool mf_desfire_file_settings_parse(const uint8_t *buf, size_t len, mf_desfire_file_settings_t *out);

/*============================= Application/file IDs ===========================*/
typedef struct {
    uint8_t id[MF_DESFIRE_APP_ID_SIZE];
} mf_desfire_app_id_t;

/* Parses a GetApplicationIDs response body into 3-byte-aligned application
 * IDs, writing up to max_out entries into out[] and reporting the TRUE
 * total application count in *out_count (which may exceed max_out) so a
 * caller can tell "there were more than we could store" from "there were
 * exactly this many". *truncated is set true iff the body held more whole
 * IDs than max_out. A body length that isn't a multiple of 3 is a
 * malformed response (returns false), not silently rounded down. */
bool mf_desfire_app_ids_parse(const uint8_t *buf, size_t len,
                               mf_desfire_app_id_t *out, size_t max_out,
                               size_t *out_count, bool *truncated);

/* Parses a GetFileIDs response body (one byte per file ID) with the same
 * "report true count, flag truncation, never silently drop" contract. */
bool mf_desfire_file_ids_parse(const uint8_t *buf, size_t len,
                                uint8_t *out, size_t max_out,
                                size_t *out_count, bool *truncated);

/*============================= Free memory =====================================*/
/* Parse the 3-byte little-endian free-memory byte count. */
bool mf_desfire_free_memory_parse(const uint8_t *buf, size_t len, uint32_t *out_bytes);

/*============================= Per-file read result ============================*/
typedef enum {
    MfDesfireFileNotAttempted = 0, /* enumeration truncated before this file was reached */
    MfDesfireFileComplete,         /* full captured content, exactly the file's real size */
    MfDesfireFilePartial,          /* content captured up to MF_DESFIRE_DEEP_FILE_DATA_CAP,
                                     * real file is larger -- data[] holds a genuine PREFIX,
                                     * never padded/fabricated past what was actually read */
    MfDesfireFileProtected,        /* access rights require authentication -- no read attempted */
    MfDesfireFileUnsupported,      /* file type/comm mode this milestone does not read (e.g.
                                     * TransactionMac, or MACed/Enciphered comm) */
    MfDesfireFileFailed            /* a plain-access-eligible read was attempted and failed
                                     * (malformed response, unexpected conclusive error, or a
                                     * transient transport failure that exhausted its retry) */
} mf_desfire_file_read_status_t;

typedef struct {
    uint8_t  id;
    bool     settings_valid;
    mf_desfire_file_settings_t settings;
    mf_desfire_file_read_status_t read_status;
    uint8_t  data[MF_DESFIRE_DEEP_FILE_DATA_CAP];
    uint16_t data_len;          /* bytes actually captured in data[], <= the cap */
} mf_desfire_file_t;

/*============================= Per-application data ============================*/
typedef struct {
    mf_desfire_app_id_t id;
    bool     select_ok;                 /* SelectApplication succeeded */
    bool     key_settings_valid;
    mf_desfire_key_settings_t key_settings;
    uint8_t  key_versions[MF_DESFIRE_MAX_KEYS];
    uint8_t  key_version_count;         /* how many of key_versions[] are valid */
    bool     key_versions_truncated;    /* card reported more keys than MF_DESFIRE_MAX_KEYS */
    uint8_t  file_ids[MF_DESFIRE_MAX_FILES_PER_APP];
    uint8_t  file_count;                /* how many of file_ids[]/files[] are valid */
    bool     files_truncated;           /* card reported more files than MF_DESFIRE_MAX_FILES_PER_APP */
    mf_desfire_file_t files[MF_DESFIRE_MAX_FILES_PER_APP];
} mf_desfire_app_t;

/*============================= Top-level deep-read result ======================*/
typedef struct {
    bool     free_memory_valid;
    uint32_t free_memory_bytes;

    bool     master_key_settings_valid;
    mf_desfire_key_settings_t master_key_settings;

    mf_desfire_app_t apps[MF_DESFIRE_DEEP_MAX_APPS];
    uint8_t  app_count;              /* how many of apps[] are valid */
    bool     apps_truncated;         /* card reported more applications than MF_DESFIRE_DEEP_MAX_APPS */

    bool     apps_protected;         /* GetApplicationIDs itself required auth -- app_count stays 0,
                                       * this is NOT the same as "apps_truncated" */
} mf_desfire_deep_t;

/* Resets a deep-read result to its documented empty/not-attempted state.
 * Callers must call this before starting a new deep read (mirrors
 * nfc_ctx_clear_desfire()'s existing convention for the Tier-1 struct). */
void mf_desfire_deep_reset(mf_desfire_deep_t *deep);

/*============================= Display names ==================================*/
/* Human-readable names for UI/log display. Never return NULL. */
const char *mf_desfire_file_type_name(mf_desfire_file_type_t type);
const char *mf_desfire_comm_name(mf_desfire_comm_t comm);
const char *mf_desfire_file_read_status_name(mf_desfire_file_read_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* MF_DESFIRE_PARSE_H_ */
