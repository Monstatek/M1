/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    ntag_pwd_keys.h
 * @brief   MIFARE Ultralight EV1 (UL11) / NTAG213/215/216 PWD_AUTH password
 *          candidate store + user dictionary (key management).
 *
 * Owns the "loaded / candidate" 4-byte PWD_AUTH password set for the
 * password-protected Type-2 variants this feature supports:
 *   - a small compiled-in fallback (the public NXP factory password),
 *   - a user dictionary read from / written to SD in the same plain-text,
 *     one-hex-key-per-line format ulc_keys.h already established, and
 *   - a read-only system dictionary shipped as managed content.
 *
 * KEY FORMAT: a PWD_AUTH password is exactly 4 bytes = 8 hex characters --
 * NOT a MIFARE Ultralight C 16-byte 2K3DES key (see ulc_keys.h) and NOT a
 * MIFARE Classic 6-byte Key A/Key B. Do not conflate any of the three; each
 * has its own dedicated store, and there is deliberately no shared/generic
 * "NFC key" abstraction across them (see the M1 engineering rules on
 * proven, minimal surface over speculative generalization).
 *
 * SCOPE: this module holds candidate passwords only and provides user
 * management (view/add/delete) plus a streaming iterator. It performs NO
 * card authentication -- the actual PWD_AUTH RF exchange (nfc_poller.c's
 * nfc_unlock_run()) is a separate, dedicated stage that consumes this
 * iterator. This module is protocol-correct key bookkeeping, identical in
 * shape to ulc_keys.h, validated the same way (host + on-device, no live
 * target required).
 */
/*============================================================================*/
#ifndef NFC_DRV_NTAG_PWD_KEYS_H_
#define NFC_DRV_NTAG_PWD_KEYS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nfc_fileio.h"   /* nfcfio_t (embedded in the streaming iterator) */

#ifdef __cplusplus
extern "C" {
#endif

/* A PWD_AUTH password is 4 bytes = 8 hex characters. */
#define NTAG_PWD_KEY_SIZE      4U
#define NTAG_PWD_KEY_HEXLEN    8U

/* User dictionary path on SD (M1 canonical). Unlike the read-only MFC Keys
 * browse, this tool WRITES this file (add/delete), so a single canonical
 * path is used for both read and write -- no alternate-path fallback
 * that could desync add/delete. Plain-text, one 8-hex password per line;
 * the parser is extension-agnostic. */
#define NTAG_PWD_KEYS_USER_PATH   "nfc/system/ntag_ul_pwd_dict_user.nfc"
#define NTAG_PWD_KEYS_SYSTEM_PATH "nfc/system/ntag_ul_pwd_dict.nfc"

/* Bounded RAM snapshot capacity (UI/management only -- NOT an auth cap;
 * dictionary SCANNING streams from SD via the iterator below and is
 * further bounded by AUTHLIM-safety logic in nfc_poller.c, never by this
 * constant). At 256 that is 1 KB; allocate a ntag_pwd_keys_t statically/
 * globally, never on the stack. Overflow past NTAG_PWD_KEYS_MAX is
 * flagged (truncated), never allocated. */
#ifndef NTAG_PWD_KEYS_MAX
#define NTAG_PWD_KEYS_MAX      256U
#endif

/*----------------------------------------------------------------------------*/
/* Strict line parsing (permissive whitespace/case, strict 8-hex password).    */
/*----------------------------------------------------------------------------*/
typedef enum {
    NTAG_PWD_LINE_SKIP = 0,  /* blank line or '#' comment (not an error)       */
    NTAG_PWD_LINE_KEY,       /* valid 4-byte password parsed into out[]        */
    NTAG_PWD_LINE_BAD,       /* malformed first token (rejected)               */
} ntag_pwd_line_kind_t;

/* Classify/parse one text line. Leading/trailing whitespace ignored; blank
 * and '#'-comment lines are SKIP. A KEY is exactly 8 hex chars (upper or
 * lower); after those 8 chars the rest may contain ONLY whitespace, or
 * whitespace then a '#' inline comment. Any other trailing text, or a
 * token shorter/longer than 8, or non-hex, is BAD (never truncated). */
ntag_pwd_line_kind_t ntag_pwd_keys_parse_line(const char* line, uint8_t out[NTAG_PWD_KEY_SIZE]);

/*----------------------------------------------------------------------------*/
/* Built-in default password(s): public, well-known (NXP factory default).     */
/*----------------------------------------------------------------------------*/
uint16_t ntag_pwd_keys_builtin_count(void);
bool     ntag_pwd_keys_builtin_get(uint16_t idx, uint8_t out[NTAG_PWD_KEY_SIZE]);

/*----------------------------------------------------------------------------*/
/* Streaming iterator: owner dictionary first, then the read-only system       */
/* dictionary. The compiled NXP                                                */
/* default is emitted only if the system file is absent/unreadable. AUTHLIM     */
/* safety remains enforced by nfc_unlock_run(), not by this iterator.           */
/*----------------------------------------------------------------------------*/
typedef enum {
    NTAG_PWD_KEYS_PHASE_USER = 0,
    NTAG_PWD_KEYS_PHASE_SYSTEM,
    NTAG_PWD_KEYS_PHASE_FALLBACK,
    NTAG_PWD_KEYS_PHASE_DONE,
} ntag_pwd_keys_phase_t;

typedef struct {
    nfcfio_t io;
    bool     file_open;
    ntag_pwd_keys_phase_t phase;
    uint16_t builtin_idx;    /* next built-in index to emit                    */
    uint16_t seen_count;
    uint8_t  seen[NTAG_PWD_KEYS_MAX][NTAG_PWD_KEY_SIZE];
    const char* user_path;
    const char* system_path;
    char     line[32];
} ntag_pwd_keys_iter_t;

void ntag_pwd_keys_iter_begin(ntag_pwd_keys_iter_t* it, const char* user_path);
void ntag_pwd_keys_iter_begin_paths(ntag_pwd_keys_iter_t* it, const char* user_path,
                                    const char* system_path);
bool ntag_pwd_keys_iter_next (ntag_pwd_keys_iter_t* it, uint8_t out[NTAG_PWD_KEY_SIZE]);
void ntag_pwd_keys_iter_end  (ntag_pwd_keys_iter_t* it);

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot (view / add / delete). Deterministic order: built-in    */
/* first, then user-file order (first occurrence wins). Overflow flagged.       */
/*----------------------------------------------------------------------------*/
typedef struct {
    uint16_t count;                                  /* total keys held        */
    uint16_t builtin_count;                          /* first N are built-in (RO) */
    uint16_t user_count;                             /* next N are owner-managed */
    uint16_t system_count;                           /* final N are system (RO) */
    uint8_t  keys[NTAG_PWD_KEYS_MAX][NTAG_PWD_KEY_SIZE];
} ntag_pwd_keys_t;

typedef enum {
    NTAG_PWD_KEYS_SRC_NONE = 0,      /* no user dictionary file                */
    NTAG_PWD_KEYS_SRC_LOADED,        /* a user dictionary was opened and parsed */
    NTAG_PWD_KEYS_SRC_READ_ERROR,    /* file exists but could not be opened/read */
} ntag_pwd_keys_src_state_t;

typedef struct {
    bool                       ok;
    ntag_pwd_keys_src_state_t  state;
    uint16_t                   loaded;
    uint16_t                   valid;
    uint16_t                   skipped_malformed;
    bool                       truncated;
    const char*                source_path;
} ntag_pwd_keys_load_status_t;

void     ntag_pwd_keys_reset(ntag_pwd_keys_t* s);                                  /* clear + seed built-ins */
ntag_pwd_keys_load_status_t
         ntag_pwd_keys_load_user(ntag_pwd_keys_t* s, const char* user_path);       /* append user file, dedup, bounded */
ntag_pwd_keys_load_status_t
         ntag_pwd_keys_load_system(ntag_pwd_keys_t* s, const char* system_path);   /* append read-only system file */
bool     ntag_pwd_keys_add  (ntag_pwd_keys_t* s, const uint8_t key[NTAG_PWD_KEY_SIZE]); /* dedup, bounded (RAM only) */
bool     ntag_pwd_keys_remove_at(ntag_pwd_keys_t* s, uint16_t idx);                /* remove a USER entry (idx >= builtin_count) */
uint16_t ntag_pwd_keys_count(const ntag_pwd_keys_t* s);
bool     ntag_pwd_keys_get  (const ntag_pwd_keys_t* s, uint16_t idx, uint8_t out[NTAG_PWD_KEY_SIZE]);
bool     ntag_pwd_keys_is_builtin(const ntag_pwd_keys_t* s, uint16_t idx);
bool     ntag_pwd_keys_is_user(const ntag_pwd_keys_t* s, uint16_t idx);
bool     ntag_pwd_keys_is_system(const ntag_pwd_keys_t* s, uint16_t idx);

/* Persist the user (non-builtin) keys of the snapshot to
 * NTAG_PWD_KEYS_USER_PATH, one 8-hex uppercase password per line, LF EOL.
 * Overwrites the file. Built-in keys are never written. Returns 1 on
 * success, 0 on failure. */
int      ntag_pwd_keys_save_user(const ntag_pwd_keys_t* s);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_NTAG_PWD_KEYS_H_ */
