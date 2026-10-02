/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_keys.h
 * @brief   MIFARE Classic key store + user-dictionary management.
 *
 * Owns the "MIFARE Classic Keys" tool's concerns only: the compiled-in
 * built-in array, and the bounded UI snapshot (list/add/remove/persist) for
 * the user dictionary. Line parsing, dictionary path precedence, and
 * candidate-key iteration/dedup for scanning are NOT duplicated here -- see
 * mfc_key_source.h, the single module that owns those for every MFC key
 * consumer (this tool's counts, the acquisition session's RF-attempt
 * scheduling, and any future source).
 *
 * SCALABILITY: mfc_keys_iter_* streams the user dictionary from SD one key
 * at a time (via mfc_key_source underneath), so the number of scannable user
 * keys is bounded only by the SD file -- NOT by any RAM array. The bounded
 * snapshot (mfc_keys_t) is a convenience for the UI list / add / delete only;
 * its MFC_KEYS_MAX capacity is never a limit on scanning.
 *
 * SCOPE: this module holds candidate keys and manages the user dictionary
 * only. It performs NO sector authentication, NO key search/recovery, NO
 * Crypto1, NO nested/mfkey/nonce work. "Recovered" keys (that actually
 * authenticate a sector) live in the existing nfc_mfc_sector_t (nfc_ctx),
 * not here.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_KEYS_H_
#define NFC_DRV_MFC_KEYS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mfc_key_source.h"   /* MFC_KEY_SIZE, mfc_key_parse_line, the iterator this wraps */
#include "nfc_fileio.h"       /* nfcfio_t (mfc_keys_save_user's write path) */

#ifdef __cplusplus
extern "C" {
#endif

/* User dictionary paths on SD. The canonical M1 path is tried first; if it does
 * not exist, the compatible legacy path is tried. Files are never
 * merged or overwritten. Both are interoperable plain-text; the parser is
 * extension-agnostic, so imported .dict/.dic files load identically. */
#define MFC_KEYS_USER_PATH          "nfc/system/mf_classic_dict_user.nfc"
#define MFC_KEYS_USER_PATH_COMPAT  "nfc/assets/mf_classic_dict_user.nfc"

/* Optional, read-only SD system dictionary: a larger community/proxmark-
 * style key list a user may drop onto SD. NOT required -- the compiled
 * built-in array above always remains usable as a fallback regardless of
 * whether this file is present. Same precedence and plain-text format as
 * the user dictionary; M1 never writes it. No production key list ships in
 * this file yet -- installing one at either path activates it without a
 * firmware rebuild (mfc_key_source resolves paths at scan time, not compile
 * time). */
#define MFC_KEYS_SYSTEM_PATH          "nfc/system/mf_classic_dict.nfc"
#define MFC_KEYS_SYSTEM_PATH_COMPAT  "nfc/assets/mf_classic_dict.nfc"

/* Bounded RAM snapshot capacity. Applies ONLY to the mfc_keys_t convenience
 * store below -- it is NOT a cap on scanning. ~6 KB at 1024; allocate a
 * mfc_keys_t statically/globally, never on the stack. */
#ifndef MFC_KEYS_MAX
#define MFC_KEYS_MAX        1024U
#endif

/*----------------------------------------------------------------------------*/
/* Built-in default keys (public well-known; compiled-in ROM).                */
/*----------------------------------------------------------------------------*/
uint16_t mfc_keys_builtin_count(void);
bool     mfc_keys_builtin_get(uint16_t idx, uint8_t out[MFC_KEY_SIZE]);

/* The raw built-in array + mfc_keys_builtin_count() entries, for a caller
 * building its own mfc_key_source_cfg_t (e.g. the acquisition session's
 * system-dictionary source). Read-only, static storage duration. */
const uint8_t (*mfc_keys_builtin_array(void))[MFC_KEY_SIZE];

/*----------------------------------------------------------------------------*/
/* Scalable streaming iterator over built-in keys then the user dictionary -- */
/* the path a card scan must use. A thin wrapper over mfc_key_source_iter_t   */
/* (see mfc_key_source.h for the actual precedence/dedup rules), kept for its */
/* one remaining caller's existing call shape.                                */
/*----------------------------------------------------------------------------*/
typedef struct {
    const char             *paths[2];
    mfc_key_source_cfg_t    cfg;
    mfc_key_source_iter_t   inner;
} mfc_keys_iter_t;

void mfc_keys_iter_begin(mfc_keys_iter_t* it, const char* user_path);
bool mfc_keys_iter_next (mfc_keys_iter_t* it, uint8_t out[MFC_KEY_SIZE]);
void mfc_keys_iter_end  (mfc_keys_iter_t* it);

/* True if the candidate most recently returned by mfc_keys_iter_next() came
 * from the compiled built-in array; false if it came from the streamed user
 * file. Valid only immediately after a successful mfc_keys_iter_next() call
 * -- for progress UI (e.g. nfc_mfc_scan_t.cur_source) that reports which
 * dictionary the current candidate came from. */
bool mfc_keys_iter_last_was_builtin(const mfc_keys_iter_t* it);

/* Total key count the iterator would yield (built-in + valid user keys, deduped
 * vs built-in). Streams the file once; bounded RAM. Pass NULL to auto-resolve
 * the user path (canonical then compatible fallback). */
uint32_t mfc_keys_total(const char* user_path);

/* Which default user-dictionary path currently exists: canonical M1 path if
 * present, else the compatible legacy path, else NULL. Does not modify files. */
const char* mfc_keys_resolve_user_path(void);

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot (convenience for the UI list / small consumers).       */
/* Fully deduplicated, deterministic order: built-in first, then user-file      */
/* order (first occurrence wins). Overflow past MFC_KEYS_MAX is flagged, never  */
/* allocated.                                                                   */
/*----------------------------------------------------------------------------*/
typedef struct {
    uint16_t count;                            /* total keys held              */
    uint16_t builtin_count;                    /* first N are built-in (RO)    */
    uint8_t  keys[MFC_KEYS_MAX][MFC_KEY_SIZE];
} mfc_keys_t;

/* Distinguishes a normal "no file" from an SD/file read error (both leave the
 * built-in keys usable). */
typedef enum {
    MFC_KEYS_SRC_NONE = 0,      /* no user dictionary file at either path         */
    MFC_KEYS_SRC_LOADED,        /* a user dictionary was opened and parsed        */
    MFC_KEYS_SRC_READ_ERROR,    /* a user dictionary file exists but could not be opened/read */
} mfc_keys_src_state_t;

typedef struct {
    bool                 ok;                /* == (state == MFC_KEYS_SRC_LOADED)  */
    mfc_keys_src_state_t state;             /* missing vs loaded vs read-error    */
    uint16_t             loaded;            /* user keys accepted                 */
    uint16_t             skipped_malformed; /* rejected lines                     */
    bool                 truncated;         /* hit MFC_KEYS_MAX (snapshot; NOT scan) */
    const char*          source_path;       /* which file was opened, or NULL     */
} mfc_keys_load_status_t;

void     mfc_keys_reset(mfc_keys_t* s);                                    /* clear + seed built-ins */
mfc_keys_load_status_t
         mfc_keys_load_user(mfc_keys_t* s, const char* user_path);         /* append user file, dedup, bounded, non-fatal */
bool     mfc_keys_add  (mfc_keys_t* s, const uint8_t key[MFC_KEY_SIZE]);   /* dedup, bounded (RAM only) */
bool     mfc_keys_remove_at(mfc_keys_t* s, uint16_t idx);                  /* remove a USER entry (idx >= builtin_count) */
uint16_t mfc_keys_count(const mfc_keys_t* s);
bool     mfc_keys_get  (const mfc_keys_t* s, uint16_t idx, uint8_t out[MFC_KEY_SIZE]);
bool     mfc_keys_is_builtin(const mfc_keys_t* s, uint16_t idx);

/* Persist the user (non-builtin) keys of the snapshot to MFC_KEYS_USER_PATH,
 * one 12-hex uppercase key per line, LF EOL. Atomic: written to a "<path>.tmp"
 * sibling first; the original is replaced (delete-then-rename) only after the
 * temp file is fully written and closed successfully, so a write failure
 * (SD full/removed mid-write) leaves the existing on-disk dictionary intact.
 * Built-in keys are never written. Returns 1 on success, 0 on failure. */
int      mfc_keys_save_user(const mfc_keys_t* s);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_KEYS_H_ */
