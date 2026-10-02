/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_result.c
 * @brief   See mfc_result.h.
 */
/*============================================================================*/
#include "mfc_result.h"

mfc_outcome_t mfc_classify_outcome(uint8_t sectors_total, uint8_t sectors_read,
                                   uint8_t keys_total, uint8_t keys_found,
                                   bool cancelled)
{
    bool complete = (sectors_total > 0U) && (sectors_read == sectors_total) &&
                    (keys_total > 0U)    && (keys_found   == keys_total);
    if (complete) return MFC_OUTCOME_COMPLETE;

    bool has_data = (sectors_read > 0U) || (keys_found > 0U);
    if (cancelled) return has_data ? MFC_OUTCOME_PARTIAL : MFC_OUTCOME_CANCELLED;
    return has_data ? MFC_OUTCOME_PARTIAL : MFC_OUTCOME_FAILED;
}

mfc_action_eligibility_t mfc_action_eligibility(mfc_outcome_t outcome)
{
    mfc_action_eligibility_t e;
    e.save = false; e.save_partial = false; e.info = false;
    e.try_again = false; e.find_missing_keys = false; e.emulate_write_allowed = false;

    switch (outcome) {
        case MFC_OUTCOME_COMPLETE:
            e.save = true;
            e.info = true;
            e.emulate_write_allowed = true;
            break;
        case MFC_OUTCOME_PARTIAL:
            /* Find Missing Keys (resumable continuation) replaces the old
             * blind full-restart Try Again here -- Partial always has real
             * progress worth preserving and continuing, never worth
             * discarding for a from-scratch retry. */
            e.save_partial = true;
            e.info = true;
            e.find_missing_keys = true;
            break;
        case MFC_OUTCOME_FAILED:
            /* Zero progress exists, so there is nothing for Find Missing
             * Keys to resume that Try Again (a full restart) would not
             * already do identically -- offering both would be a dead
             * duplicate action. The card still identified itself (mc->valid
             * is set before any key is ever tried), so Info has something
             * truthful to show. */
            e.try_again = true;
            e.info = true;
            break;
        case MFC_OUTCOME_CANCELLED:
            /* Only ever reached with zero data (see mfc_classify_outcome:
             * a cancellation that produced real data classifies as PARTIAL
             * instead) -- same shape as FAILED, so an empty-handed cancel
             * is never a dead end either. */
            e.try_again = true;
            e.info = true;
            break;
        case MFC_OUTCOME_NONE:
        default:
            break;   /* nothing usable to act on */
    }
    return e;
}

bool mfc_action_eligibility_any(mfc_action_eligibility_t e)
{
    return e.save || e.save_partial || e.info;
}
