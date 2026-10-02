/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_dict_resume.h
 * @brief   Persisted "same generation" dictionary-acquisition state a
 *          Find-Missing-Keys continuation needs, so it never re-tries a
 *          candidate key already attempted in an earlier pass against the
 *          same card.
 *
 * Two independent pieces, because the two source categories they cover have
 * fundamentally different resumability:
 *
 *  - Built-in and User-dictionary candidates: both are ACCUMULATE-eligible
 *    (see mfc_key_source.h) and bounded (MFC_KEY_SOURCE_MAX_SEEN entries),
 *    so the exact `seen[]`/`seen_n`/`seen_overflowed` triple from a finished
 *    iterator run can simply be carried into the next one -- a candidate
 *    already yielded once is never yielded again, keyed by VALUE, not
 *    position, so a genuinely NEW line appended to the User dictionary since
 *    the last pass is naturally still yielded (it was never in `seen[]`).
 *
 *  - The System dictionary is explicitly NOT accumulate-eligible (can hold
 *    several thousand entries -- see mfc_key_source_cfg_t.accumulate), so
 *    its own candidates are never added to `seen[]` and a `seen[]` snapshot
 *    alone cannot make it resumable. It needs its own persisted position: a
 *    byte offset into the resolved file, PLUS enough of the file's identity
 *    (path, size, FAT date/time) to detect "this is a different file than
 *    the one the offset was measured against" before ever trusting it --
 *    never blindly seek into a file that was edited or replaced since the
 *    offset was recorded.
 *
 * The byte offset is committed by the acquisition core (mfc_dict_phase.c)
 * ONLY once the candidate at that position has reached a definite result --
 * every Key A/Key B attempt against every still-unresolved sector completed
 * without the operation being aborted or the card being lost. An
 * interrupted candidate leaves the offset exactly where it was, so it is
 * retried (never skipped) on the next continuation.
 *
 * Pure construction/comparison only -- no I/O, no FatFs/HAL dependency, no
 * knowledge of RF or Crypto1. Links and runs unmodified in a host test
 * binary; the real path/size/date/time values it is compared against come
 * from mfc_key_source_sd_stat() (HAL-coupled, called by nfc_poller.c).
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_DICT_RESUME_H_
#define NFC_DRV_MFC_DICT_RESUME_H_

#include <stdint.h>
#include <stdbool.h>
#include "mfc_key_source.h"

/* Matches nfc_ctx.h's own #ifndef default exactly -- defined independently
 * here (rather than including nfc_ctx.h, which pulls in RFAL/HAL headers)
 * so this module stays pure/host-linkable; whichever header is included
 * first in a given translation unit wins, and both agree on 128. */
#ifndef NFC_PATH_MAX
#define NFC_PATH_MAX        128
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Built-in + User dedup, carried verbatim from a prior iterator run. */
    uint8_t   seen[MFC_KEY_SOURCE_MAX_SEEN][MFC_KEY_SIZE];
    uint16_t  seen_n;
    bool      seen_overflowed;

    /* System-dictionary resume cursor. sys_valid=false means "no committed
     * cursor yet" (fresh generation, or the System source has never been
     * reached) -- every other sys_* field is meaningless until then. */
    bool      sys_valid;
    char      sys_path[NFC_PATH_MAX];
    uint32_t  sys_file_size;
    uint16_t  sys_file_date;   /* FatFs FILINFO.fdate */
    uint16_t  sys_file_time;   /* FatFs FILINFO.ftime */
    uint32_t  sys_byte_offset; /* start of the next unconsumed System-dict line */
} mfc_dict_resume_t;

/* Fresh generation: no dedup history, no System cursor. Called whenever a
 * genuinely new card is presented (same points nfc_ctx_clear_mfc() already
 * runs), so continuation state can never leak across two different physical
 * cards or two unrelated read sessions. */
void mfc_dict_resume_reset(mfc_dict_resume_t *r);

/* The System dictionary file was edited/replaced since the cursor was last
 * committed (identity mismatch): drop ONLY the System cursor (sys_valid,
 * sys_byte_offset -> a fresh System sweep from the top), while the
 * built-in/User seen[] dedup -- and, critically, every already-recovered
 * key/block on the caller's nfc_mfc_info_t/nfc_mfc_scan_t, which this
 * struct does not even reference -- is left completely untouched. */
void mfc_dict_resume_reset_system_only(mfc_dict_resume_t *r);

/* Whether `r`'s committed System cursor is still trustworthy for `path` --
 * true only if a cursor exists (sys_valid) and path/size/date/time all match
 * exactly. A caller must check this (with values obtained from a real
 * mfc_key_source_sd_stat() call) before ever seeking a file open to
 * r->sys_byte_offset. path may be NULL (never matches, mirroring
 * mfc_identity_matches()'s NULL handling), and a path longer than
 * NFC_PATH_MAX-1 never matches, since it could not have been the same
 * string that produced sys_path in the first place. */
bool mfc_dict_resume_system_identity_matches(const mfc_dict_resume_t *r,
                                              const char *path, uint32_t size,
                                              uint16_t fdate, uint16_t ftime);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_DICT_RESUME_H_ */
