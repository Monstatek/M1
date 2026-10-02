/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_result.h
 * @brief   MIFARE Classic acquisition outcome classification + action
 *          eligibility. Pure logic, no I/O, no RF, no nfc_ctx dependency --
 *          the acquisition session (nfc_poller.c) computes the four
 *          arguments from the canonical nfc_mfc_info_t/nfc_mfc_scan_t it
 *          already owns and stores the result on nfc_mfc_info_t.outcome;
 *          UI/menu code reads that stored value rather than re-deriving it
 *          from raw counts, so a card-loss/cancellation that happened
 *          during acquisition is never silently reinterpreted later as a
 *          plain "sectors_read==0" failure or a false success.
 *
 * SCOPE: this module answers "what happened" and "which read-result actions
 * are meaningful for that", nothing else. It knows nothing about MFC 1K vs
 * 4K, UID length, or the raw emulation engine's own stricter requirements --
 * those are m1_mfc_emu_image_build()'s job (see m1_mfc_emu_image.h),
 * deliberately kept separate ("emulation readiness remains a separate
 * predicate" from a plain read-completeness classification). A caller ANDs
 * mfc_action_eligibility_t.emulate_write_allowed with that real backend
 * check before actually offering Emulate/Write -- this module only says
 * whether the OUTCOME permits considering them at all.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_RESULT_H_
#define NFC_DRV_MFC_RESULT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MFC_OUTCOME_NONE = 0,   /* no acquisition attempted yet, or context cleared */
    MFC_OUTCOME_COMPLETE,   /* every sector read AND every key found            */
    MFC_OUTCOME_PARTIAL,    /* some data obtained, not all                      */
    MFC_OUTCOME_FAILED,     /* card identified, but zero usable data obtained   */
    MFC_OUTCOME_CANCELLED,  /* user-cancelled before any usable data existed    */
} mfc_outcome_t;

/* Classifies a finished (or cancelled) MFC acquisition from its final counts.
 * `cancelled` must reflect genuine user cancellation (nfc_mfc_scan_t.state ==
 * NFC_SCAN_STOPPED at the point acquisition ended) -- a card-loss or timeout
 * is NOT "cancelled" and classifies as PARTIAL/FAILED from its counts alone,
 * so it is never misreported as a user action. A cancellation that still
 * produced genuine partial data classifies as PARTIAL, not CANCELLED --
 * CANCELLED is reserved for a cancellation before any usable data existed,
 * so a caller can distinguish "offer to keep what I have" from "nothing to
 * keep, just go back". */
mfc_outcome_t mfc_classify_outcome(uint8_t sectors_total, uint8_t sectors_read,
                                   uint8_t keys_total, uint8_t keys_found,
                                   bool cancelled);

typedef struct {
    bool save;                    /* offer "Save" (complete data)                */
    bool save_partial;            /* offer "Save Partial" (some data, truthful)  */
    bool info;                    /* offer "Info" (complete, partial, or a
                                    * failed/cancelled read that still
                                    * identified the card)                       */
    bool try_again;               /* offer "Try Again" (full restart; failed or
                                    * an empty-handed cancel -- never partial,
                                    * where find_missing_keys is the truthful,
                                    * progress-preserving continuation instead) */
    bool find_missing_keys;       /* offer "Find Missing Keys" (partial only --
                                    * resumes the same acquisition, preserving
                                    * every already-proven key/block; never
                                    * offered for failed/cancelled, since with
                                    * zero prior progress it would be functionally
                                    * identical to Try Again, a dead duplicate
                                    * action) */
    bool emulate_write_allowed;   /* outcome permits considering Emulate/Write   */
} mfc_action_eligibility_t;

/* Which read-result actions the outcome alone permits considering. A caller
 * must still AND emulate_write_allowed with its own real, family/type-
 * specific eligibility check for each of Emulate and Write individually --
 * this function never claims either is actually eligible by itself. */
mfc_action_eligibility_t mfc_action_eligibility(mfc_outcome_t outcome);

/* Whether the read-result action menu built from `e` has anything in it at
 * all -- i.e. whether opening it would show at least one item. Relies on
 * this module's own invariant that Emulate/Write are never offered without
 * the underlying data also being complete enough for `save` to be true
 * (m1_mfc_emu_image_build()'s stricter checks only run once the outcome is
 * COMPLETE, and nfc_can_write_mfc() requires MFC_OUTCOME_COMPLETE outright),
 * so `save || save_partial || info` alone never disagrees with the real
 * menu builder's item count for any outcome this module can produce.
 * find_missing_keys is not included in that OR: every outcome offering it
 * (PARTIAL) already sets save_partial and info too, so it never changes
 * whether the menu is non-empty -- only what else is in it. */
bool mfc_action_eligibility_any(mfc_action_eligibility_t e);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_RESULT_H_ */
