/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_dict_phase.c
 * @brief   See mfc_dict_phase.h. No RFAL/Crypto1 dependency by design -- all
 *          RF/crypto access goes through the injected mfc_dict_phase_ops_t
 *          seam, so this file compiles and runs unmodified in the host test
 *          binary.
 */
/*============================================================================*/
#include "mfc_dict_phase.h"
#include <string.h>

bool mfc_dict_phase_run(const mfc_key_source_cfg_t *sources, size_t n_sources,
                        const uint8_t already_tried[MFC_KEY_SIZE], uint32_t cuid,
                        const mfc_dict_phase_ops_t *ops,
                        nfc_mfc_info_t *mfc, nfc_mfc_scan_t *sc,
                        const volatile bool *abort_flag,
                        mfc_dict_resume_t *resume,
                        volatile bool *skip_source_flag)
{
    bool                  authed = false;
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   cand;
    bool                  have_prev_source = false;   /* src_tried tracking: no candidate yet */
    uint8_t               prev_source = 0;

    if (resume != NULL) {
        /* Explicit cast: uint8_t(*)[N] -> const uint8_t(*)[N] is not an
         * implicit conversion pre-C23 (unlike a plain T*->const T*), even
         * though resume->seen's actual storage is perfectly fine to read
         * as const here -- this cast changes nothing about the bytes,
         * only the type used to view them. */
        mfc_key_source_iter_begin_resume(&it, sources, n_sources,
            (const uint8_t (*)[MFC_KEY_SIZE])resume->seen, resume->seen_n, resume->seen_overflowed,
            resume->sys_valid ? resume->sys_byte_offset : 0U);
    } else {
        mfc_key_source_iter_begin(&it, sources, n_sources);
    }
    if (already_tried != NULL) mfc_key_source_iter_seed_seen(&it, already_tried);

    while (mfc_key_source_iter_next(&it, &cand)) {
        if (*abort_flag) break;
        if ((skip_source_flag != NULL) && *skip_source_flag) {
            /* Abandon the rest of THIS source only -- cand (just fetched)
             * is discarded untried, exactly like an aborted-before-tried
             * candidate; every already-proven key/block and all of `resume`
             * are untouched. The next iter_next() call (the while
             * condition, next iteration) resumes at the next source, or
             * reports exhaustion if that was the last one. */
            *skip_source_flag = false;
            mfc_key_source_iter_skip_source(&it);
            continue;
        }
        if (sc->found >= mfc->keys_total) break;   /* all key slots solved */

        /* Fresh card + card-removal detection. A single transient failure
         * is tolerated silently (unchanged from before); from the SECOND
         * consecutive failure, sc->state becomes NFC_SCAN_CARD_LOST -- a
         * LIVE value a UI polls to show "Lost the tag!" -- and reselect is
         * retried indefinitely rather than giving up, matching the
         * Requirement that the same card re-presented
         * resumes safely. abort_flag/skip_source_flag are still honored on
         * every retry so the user is never stuck with no way out while the
         * card is away. Discrimination between "the same card came back"
         * and "a different card was presented" is entirely ops->reselect()'s
         * job (see nfc_poller.c's production wrapper) -- from here both a
         * still-missing card and a wrong card look identical: reselect
         * keeps failing. */
        bool skipped_while_lost = false;
        int  reselect_fail_count = 0;
        while (!ops->reselect(ops->dev)) {
            reselect_fail_count++;
            if (reselect_fail_count >= 2) sc->state = NFC_SCAN_CARD_LOST;
            if (*abort_flag) {
                /* `cand` was never tried -- resume left exactly as it was,
                 * same reasoning as the ordinary skip-source case above. */
                mfc_key_source_iter_end(&it);
                return true;
            }
            if ((skip_source_flag != NULL) && *skip_source_flag) {
                *skip_source_flag = false;
                mfc_key_source_iter_skip_source(&it);
                skipped_while_lost = true;
                break;
            }
        }
        if (skipped_while_lost) continue;
        if (reselect_fail_count >= 2) sc->state = NFC_SCAN_RUNNING;   /* the same card is back */

        authed = false;
        sc->keys_tried++;
        sc->cur_source = cand.from_builtin ? NFC_SCAN_SRC_BUILTIN :
                          (cand.kind == MFC_KEY_SRC_USER) ? NFC_SCAN_SRC_USER : NFC_SCAN_SRC_SYSTEM;
        /* src_tried: candidates tried within cur_source only -- resets to 1
         * on every source change, mirroring how sectors_acc is already
         * incremented conditionally rather than recomputed. */
        if (have_prev_source && (prev_source == sc->cur_source)) sc->src_tried++;
        else sc->src_tried = 1U;
        have_prev_source = true;
        prev_source = sc->cur_source;

        uint64_t k = ((uint64_t)cand.key[0] << 40) | ((uint64_t)cand.key[1] << 32) |
                     ((uint64_t)cand.key[2] << 24) | ((uint64_t)cand.key[3] << 16) |
                     ((uint64_t)cand.key[4] << 8)  |  (uint64_t)cand.key[5];

        for (uint8_t s = 0; s < mfc->sectors_total; s++) {
            nfc_mfc_sector_t *S = &mfc->sec[s];
            if (S->key_a_found && S->key_b_found) continue;
            sc->cur_sector = s;
            uint16_t first   = m1nfc_mfc_sector_first_block(s);
            uint8_t  nblocks = m1nfc_mfc_sector_blocks(s);

            if (!S->key_a_found) {
                if (*abort_flag) break;
                sc->cur_keytype = 0;   /* Key A */
                if (ops->auth(ops->dev, ops->crypto_ctx, (uint8_t)first, MFC_DICT_PHASE_KEY_A_CMD,
                              k, cuid, &authed)) {
                    if (!S->key_b_found) sc->sectors_acc++;   /* sector just became accessible */
                    S->key_a_found = true; (void)memcpy(S->key_a, cand.key, MFC_KEY_SIZE);
                    mfc->keys_found++; sc->found++;
                    ops->read_sector(ops->dev, ops->crypto_ctx, first, nblocks, &authed);
                    (void)ops->reselect(ops->dev); authed = false;   /* back to fresh */
                }
            }
            if (!S->key_b_found) {
                if (*abort_flag) break;
                sc->cur_keytype = 1;   /* Key B */
                if (ops->auth(ops->dev, ops->crypto_ctx, (uint8_t)first, MFC_DICT_PHASE_KEY_B_CMD,
                              k, cuid, &authed)) {
                    if (!S->key_a_found) sc->sectors_acc++;   /* sector just became accessible */
                    S->key_b_found = true; (void)memcpy(S->key_b, cand.key, MFC_KEY_SIZE);
                    mfc->keys_found++; sc->found++;
                    ops->read_sector(ops->dev, ops->crypto_ctx, first, nblocks, &authed);
                    (void)ops->reselect(ops->dev); authed = false;
                }
            }
        }

        /* Resume-state commit point: ONLY once this candidate's full sector
         * loop above completed without *abort_flag firing mid-way (checked
         * fresh here, since the loop's own internal breaks may have exited
         * it early) does this candidate count as having reached a definite
         * result -- for EVERY source, not just System. A built-in/User
         * candidate is added to the iterator's own seen[] the instant it is
         * yielded (before any auth is even attempted), so if this candidate
         * was interrupted before its sector loop finished, propagating that
         * seen[] into `resume` here would permanently mark an only-
         * partially-tried candidate as fully exhausted -- silently skipping
         * whatever sectors it never reached on every future continuation.
         * Skipping the whole commit (not just the System offset) when
         * aborted keeps every source's resumability correct by the same
         * one rule. */
        if ((resume != NULL) && !(*abort_flag)) {
            resume->seen_n = it.seen_n; resume->seen_overflowed = it.seen_overflowed;
            (void)memcpy(resume->seen, it.seen, sizeof(resume->seen));
            if (it.last_system_offset_valid) {
                resume->sys_valid       = true;
                resume->sys_byte_offset = it.last_system_offset;
            }
        }
    }
    mfc_key_source_iter_end(&it);
    return true;
}
