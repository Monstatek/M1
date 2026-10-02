/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    ulc_keys.h
 * @brief   MIFARE Ultralight C key store + user dictionary (key management).
 *
 * Owns the "loaded / candidate" 3DES key set for MIFARE Ultralight C:
 *   - a small compiled-in default (the public NXP factory key),
 *   - an owner-managed dictionary read from / written to SD, and
 *   - a read-only system dictionary shipped as managed content.
 *
 * KEY FORMAT: an Ultralight C authentication key is a 2-key 3DES (2K3DES) key,
 * 16 bytes = 32 hex characters -- NOT a MIFARE Classic 6-byte Key A/Key B, and
 * NOT an NTAG 4-byte PWD. Do not conflate the two.
 *
 * SCOPE: this module holds candidate keys only and provides the user
 * management (view / add / delete) plus a streaming iterator. It performs NO
 * card authentication and NO 3DES/crypto -- the M1 firmware has no 3DES
 * primitive, so actual Ultralight C dictionary authentication is a separate,
 * still-blocked stage (see the tool's report). This module is protocol-correct
 * key bookkeeping that can be validated on host and on device without a live
 * Ultralight C target.
 */
/*============================================================================*/
#ifndef NFC_DRV_ULC_KEYS_H_
#define NFC_DRV_ULC_KEYS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nfc_fileio.h"   /* nfcfio_t (embedded in the streaming iterator) */

#ifdef __cplusplus
extern "C" {
#endif

/* An Ultralight C 2K3DES key is 16 bytes = 32 hex characters. */
#define ULC_KEY_SIZE        16U
#define ULC_KEY_HEXLEN      32U

/* User dictionary path on SD (M1 canonical). Unlike the read-only MFC Keys
 * browse, this tool WRITES this file (add/delete), so a single canonical path
 * is used for both read and write -- no silent alternate-path fallback that could
 * desync add/delete. Plain-text, one 32-hex key per line; the parser is
 * extension-agnostic. */
#define ULC_KEYS_USER_PATH          "nfc/system/mf_ultralight_c_dict_user.nfc"
#define ULC_KEYS_SYSTEM_PATH        "nfc/system/mf_ultralight_c_dict.nfc"

/* Bounded RAM snapshot capacity (UI/management only -- NOT an auth cap). At 256
 * that is 4 KB; allocate a ulc_keys_t statically/globally, never on the stack.
 * Overflow past ULC_KEYS_MAX is flagged (truncated), never allocated. */
#ifndef ULC_KEYS_MAX
#define ULC_KEYS_MAX        256U
#endif

/*----------------------------------------------------------------------------*/
/* Strict line parsing (permissive whitespace/case, strict 32-hex key).        */
/*----------------------------------------------------------------------------*/
typedef enum {
    ULC_LINE_SKIP = 0,   /* blank line or '#' comment (not an error)           */
    ULC_LINE_KEY,        /* valid 16-byte key parsed into out[]                */
    ULC_LINE_BAD,        /* malformed first token (rejected)                   */
} ulc_line_kind_t;

/* Classify/parse one text line. Leading/trailing whitespace ignored; blank and
 * '#'-comment lines are SKIP. A KEY is exactly 32 hex chars (upper or lower);
 * after those 32 chars the rest may contain ONLY whitespace, or whitespace then
 * a '#' inline comment. Any other trailing text, or a token shorter/longer than
 * 32, or non-hex, is BAD (never truncated). */
ulc_line_kind_t ulc_keys_parse_line(const char* line, uint8_t out[ULC_KEY_SIZE]);

/*----------------------------------------------------------------------------*/
/* Built-in default key(s): public, well-known (NXP factory 2K3DES key).       */
/*----------------------------------------------------------------------------*/
uint16_t ulc_keys_builtin_count(void);
bool     ulc_keys_builtin_get(uint16_t idx, uint8_t out[ULC_KEY_SIZE]);

/*----------------------------------------------------------------------------*/
/* Streaming iterator: owner dictionary first, then the read-only system       */
/* dictionary. The compiled NXP default                                       */
/* is emitted only as a fallback when the system file is absent/unreadable.     */
/* The bounded seen[] set suppresses owner keys from being retried by the       */
/* system source. The release asset must itself contain no duplicate lines.     */
/*----------------------------------------------------------------------------*/
typedef enum {
    ULC_KEYS_PHASE_USER = 0,
    ULC_KEYS_PHASE_SYSTEM,
    ULC_KEYS_PHASE_FALLBACK,
    ULC_KEYS_PHASE_DONE,
} ulc_keys_phase_t;

typedef struct {
    nfcfio_t io;
    bool     file_open;
    ulc_keys_phase_t phase;
    uint16_t builtin_idx;    /* next built-in index to emit                    */
    uint16_t seen_count;
    uint8_t  seen[ULC_KEYS_MAX][ULC_KEY_SIZE];
    const char* user_path;
    const char* system_path;
    char     line[96];
} ulc_keys_iter_t;

void ulc_keys_iter_begin(ulc_keys_iter_t* it, const char* user_path);
void ulc_keys_iter_begin_paths(ulc_keys_iter_t* it, const char* user_path,
                               const char* system_path);
bool ulc_keys_iter_next (ulc_keys_iter_t* it, uint8_t out[ULC_KEY_SIZE]);
void ulc_keys_iter_end  (ulc_keys_iter_t* it);

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot (view / add / delete). Deterministic order: built-in    */
/* first, then user-file order (first occurrence wins). Overflow flagged.       */
/*----------------------------------------------------------------------------*/
typedef struct {
    uint16_t count;                            /* total keys held              */
    uint16_t builtin_count;                    /* first N are built-in (RO)    */
    uint16_t user_count;                       /* next N are owner-managed     */
    uint16_t system_count;                     /* final N are system (RO)      */
    uint8_t  keys[ULC_KEYS_MAX][ULC_KEY_SIZE];
} ulc_keys_t;

typedef enum {
    ULC_KEYS_SRC_NONE = 0,      /* no user dictionary file                      */
    ULC_KEYS_SRC_LOADED,        /* a user dictionary was opened and parsed      */
    ULC_KEYS_SRC_READ_ERROR,    /* file exists but could not be opened/read     */
} ulc_keys_src_state_t;

typedef struct {
    bool                 ok;
    ulc_keys_src_state_t state;
    uint16_t             loaded;
    uint16_t             valid;             /* valid lines before deduplication */
    uint16_t             skipped_malformed;
    bool                 truncated;
    const char*          source_path;
} ulc_keys_load_status_t;

void     ulc_keys_reset(ulc_keys_t* s);                                    /* clear + seed built-ins */
ulc_keys_load_status_t
         ulc_keys_load_user(ulc_keys_t* s, const char* user_path);         /* append user file, dedup, bounded */
ulc_keys_load_status_t
         ulc_keys_load_system(ulc_keys_t* s, const char* system_path);     /* append read-only system file */
bool     ulc_keys_add  (ulc_keys_t* s, const uint8_t key[ULC_KEY_SIZE]);   /* dedup, bounded (RAM only) */
bool     ulc_keys_remove_at(ulc_keys_t* s, uint16_t idx);                  /* remove an owner entry only */
uint16_t ulc_keys_count(const ulc_keys_t* s);
bool     ulc_keys_get  (const ulc_keys_t* s, uint16_t idx, uint8_t out[ULC_KEY_SIZE]);
bool     ulc_keys_is_builtin(const ulc_keys_t* s, uint16_t idx);
bool     ulc_keys_is_user(const ulc_keys_t* s, uint16_t idx);
bool     ulc_keys_is_system(const ulc_keys_t* s, uint16_t idx);

/* Persist the user (non-builtin) keys of the snapshot to ULC_KEYS_USER_PATH,
 * one 32-hex uppercase key per line, LF EOL. Overwrites the file. Built-in keys
 * are never written. Returns 1 on success, 0 on failure. */
int      ulc_keys_save_user(const ulc_keys_t* s);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_ULC_KEYS_H_ */
