/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_dict_phase.h
 * @brief   Canonical MIFARE Classic dictionary-phase orchestration: the
 *          key-major "for each candidate key, for each unsolved sector, try
 *          Key A then Key B" loop shared by normal Read's dictionary
 *          continuation and Tools > Dictionary Scan.
 *
 * SEAM: RF/crypto operations (card reselect, authenticate, read a sector's
 * blocks) are injected via mfc_dict_phase_ops_t rather than called by name,
 * so this file has NO RFAL/Crypto1 dependency of its own and links
 * unmodified into a host test binary -- production (nfc_poller.c) supplies
 * ops backed by the real mfc_reselect()/mfc_do_auth()/mfc_read_sector_blocks(),
 * completely unchanged; host tests supply a deterministic mock. `ops->dev`
 * and `ops->crypto_ctx` are caller-owned opaque handles this file never
 * dereferences -- it only threads them through to the callbacks, exactly as
 * the loop it replaces always threaded an rfalNfcDevice* and a Crypto1*
 * through mfc_do_auth()/mfc_read_sector_blocks()/mfc_reselect() by pointer.
 *
 * `mfc`/`sc` (the real nfc_mfc_info_t/nfc_mfc_scan_t -- see mfc_dict_types.h)
 * are written DIRECTLY, at the exact points the original loop always wrote
 * them, so the UI task polling `sc` for live progress sees an identical
 * sequence of updates while the worker task runs this loop -- not a
 * batched copy-in/copy-out that would make progress appear to freeze.
 *
 * This file does not alter authentication semantics, RFAL calls, Crypto1
 * usage, or acquisition timing in any way -- it is the same control flow as
 * m1_mfc_acquire_dict_phase() (nfc_poller.c), moved here so it can be
 * exercised directly (not transcribed) by a host test.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_DICT_PHASE_H_
#define NFC_DRV_MFC_DICT_PHASE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mfc_key_source.h"
#include "mfc_dict_types.h"
#include "mfc_dict_resume.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ISO/MIFARE key-type selectors for authenticate -- the exact `keytype`
 * values ops->auth() is called with. Matches nfc_poller.c's own
 * MFC_KEY_A_CMD/MFC_KEY_B_CMD (0x60/0x61); exposed here (not just used
 * internally) so a caller wiring mock ops -- e.g. a host test -- can match
 * them precisely rather than guessing or hardcoding a drifted copy. */
#define MFC_DICT_PHASE_KEY_A_CMD  0x60U
#define MFC_DICT_PHASE_KEY_B_CMD  0x61U

typedef struct {
    void *dev;          /* opaque RF device handle, threaded through unchanged
                          * (an rfalNfcDevice* in production) */
    void *crypto_ctx;   /* opaque running cipher session, threaded through unchanged
                          * (a Crypto1* in production) */

    /* Re-select the card (fresh + card-removal detection). */
    bool (*reselect)(void *dev);

    /* Attempt to authenticate `block`/`keytype` with `key` (the 48-bit key
     * in the low 48 bits of the uint64_t), against `cuid`. `*authed`
     * in/out, exactly as mfc_do_auth()'s own signature. */
    bool (*auth)(void *dev, void *crypto_ctx, uint8_t block, uint8_t keytype,
                uint64_t key, uint32_t cuid, bool *authed);

    /* Read `nblocks` blocks starting at absolute block `first` using the
     * just-authed cipher session; stores into whatever backing store the
     * real implementation owns (the shared dump buffer, in production). */
    void (*read_sector)(void *dev, void *crypto_ctx, uint16_t first, uint8_t nblocks, bool *authed);
} mfc_dict_phase_ops_t;

/* The exact key-major orchestration loop from m1_mfc_acquire_dict_phase()
 * (nfc_poller.c): walks the canonical [USER, SYSTEM] source list (see
 * mfc_key_source.h) via mfc_key_source_iter_*, seeding `already_tried` (e.g.
 * the fast pass's default key) so it is never re-attempted. For each
 * candidate, for each sector not yet fully solved, tries Key A then Key B;
 * on success, stores the key, marks the slot found, and reads that sector's
 * blocks. Checks `*abort_flag` before every reselect and before every
 * individual auth attempt -- identical granularity to the loop it replaces.
 *
 * `resume`, if non-NULL, makes this call a Find-Missing-Keys CONTINUATION
 * rather than a fresh acquisition -- NULL preserves today's exact behavior
 * byte-for-byte for every existing call site (normal Read's dictionary
 * phase, Tools > Dictionary Scan). When non-NULL:
 *   - iteration begins via mfc_key_source_iter_begin_resume() seeded from
 *     resume->seen/seen_n/seen_overflowed (built-in/User candidates already
 *     tried in an earlier generation-scoped call sharing this SAME resume
 *     struct are never re-yielded -- matched by value, so a genuinely new
 *     User-dictionary key is still tried normally) and, if resume->sys_valid,
 *     resume->sys_byte_offset (the caller is responsible for having already
 *     verified this offset still applies to the file about to be streamed --
 *     see mfc_dict_resume_system_identity_matches() -- this function trusts
 *     whatever offset it is given);
 *   - resume->seen/seen_n/seen_overflowed are updated in place as candidates
 *     are yielded, exactly mirroring the iterator's own accumulation, so the
 *     caller can persist the struct forward into a LATER continuation;
 *   - resume->sys_byte_offset/sys_valid are advanced ONLY once a System
 *     candidate's full inner sector loop (every still-unresolved sector,
 *     both Key A and Key B) completes without the operation having been
 *     aborted or the card lost mid-candidate -- an interrupted candidate
 *     leaves the offset exactly where it was, so it is retried (never
 *     skipped, never double-counted) on the next continuation.
 *
 * `skip_source_flag`, if non-NULL and observed true (checked at the same two
 * points `abort_flag` is: before fetching the next candidate, and on every
 * iteration of the card-lost retry wait below), is cleared and honored by
 * calling mfc_key_source_iter_skip_source() -- the CURRENT source's
 * remaining candidates are abandoned and iteration resumes at the next
 * configured source (or reports exhaustion, if that was the last one --
 * which is exactly "skip on the final stage finishes acquisition", achieved
 * for free by the ordinary exhaustion path with no special-casing here).
 * Every already-proven key/block, and the resume state, are completely
 * unaffected by a source skip -- only which CANDIDATES are tried next
 * changes. NULL preserves today's behavior (no skip capability) for any
 * caller that doesn't need it.
 *
 * Card loss (ops->reselect() failing) is a LIVE, RESUMABLE state, not a
 * terminal failure: after one tolerated transient failure, `sc->state` is
 * set to NFC_SCAN_CARD_LOST (a live value a UI polls to show "Lost the
 * tag!") and reselect is retried indefinitely -- checking `*abort_flag`
 * (clean stop, returns true, current candidate never touched -- see the
 * card-lost-return comment in the .c file) and `*skip_source_flag` (skips
 * the source that was active when the tag was lost, then continues) on
 * every retry -- until either the SAME card reappears (state returns to
 * NFC_SCAN_RUNNING and the current candidate proceeds normally) or one of
 * those two controls fires. Same-card-vs-different-card discrimination is
 * NOT this function's concern -- ops->reselect() itself is responsible for
 * only reporting success when the reselected card is provably the one this
 * acquisition started with (see nfc_poller.c's production reselect wrapper,
 * which uses mfc_identity_matches()); from here, a different card being
 * presented during a loss window looks identical to the card still being
 * away, so it can never be silently adopted mid-acquisition.
 *
 * Returns true unconditionally in this implementation -- natural
 * exhaustion, a clean user-abort (including one that fires while the card
 * is lost), and a source skip that exhausts every source all preserve
 * whatever was already proven on `mfc`/`sc`/`resume` and return true. There
 * is no remaining path that returns false: card loss, the previous reason
 * for a false return, is now always retried through rather than given up
 * on. The boolean return type is kept (rather than changed to void) purely
 * to avoid API churn across its three existing callers, whose own
 * `if (!m1_mfc_acquire_dict_phase(...))` card-lost-handling branches are
 * consequently dead code today -- left in place as defensive belt-and-
 * suspenders rather than speculatively removed. */
bool mfc_dict_phase_run(const mfc_key_source_cfg_t *sources, size_t n_sources,
                        const uint8_t already_tried[MFC_KEY_SIZE], uint32_t cuid,
                        const mfc_dict_phase_ops_t *ops,
                        nfc_mfc_info_t *mfc, nfc_mfc_scan_t *sc,
                        const volatile bool *abort_flag,
                        mfc_dict_resume_t *resume,
                        volatile bool *skip_source_flag);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_DICT_PHASE_H_ */
