/* Host tests for mfc_result.c -- executed against the REAL production
 * source (compiled directly into this binary below), not a transcribed
 * copy. Pure logic, zero I/O dependency, no mocking needed at all.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../mfc_result.c mfc_result_test.c -I.. -o /tmp/mres && /tmp/mres
 */
#include "../mfc_result.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static void test_classify_complete(void)
{
    CHECK(mfc_classify_outcome(16, 16, 32, 32, false) == MFC_OUTCOME_COMPLETE,
          "1K: every sector read, every key found -> COMPLETE");
    CHECK(mfc_classify_outcome(40, 40, 80, 80, false) == MFC_OUTCOME_COMPLETE,
          "4K: every sector read, every key found -> COMPLETE");
    /* Complete even if (implausibly) called with cancelled=true -- a finished
     * acquisition is complete regardless of how it was told to stop. */
    CHECK(mfc_classify_outcome(16, 16, 32, 32, true) == MFC_OUTCOME_COMPLETE,
          "complete counts win over a stale cancelled flag");
}

static void test_classify_partial(void)
{
    CHECK(mfc_classify_outcome(16, 10, 32, 17, false) == MFC_OUTCOME_PARTIAL,
          "some sectors/keys missing, not cancelled -> PARTIAL");
    CHECK(mfc_classify_outcome(16, 1, 32, 1, true) == MFC_OUTCOME_PARTIAL,
          "cancelled but with genuine partial data -> PARTIAL, not CANCELLED");
    CHECK(mfc_classify_outcome(16, 16, 32, 31, false) == MFC_OUTCOME_PARTIAL,
          "every sector block-read but one key still missing -> PARTIAL (not COMPLETE)");
}

static void test_classify_failed_and_cancelled(void)
{
    CHECK(mfc_classify_outcome(16, 0, 32, 0, false) == MFC_OUTCOME_FAILED,
          "zero data, not cancelled -> FAILED");
    CHECK(mfc_classify_outcome(16, 0, 32, 0, true) == MFC_OUTCOME_CANCELLED,
          "zero data, cancelled -> CANCELLED (distinct from FAILED)");
    /* Card-loss/timeout must classify identically to a plain failure --
     * only genuine user cancellation may produce CANCELLED. */
    CHECK(mfc_classify_outcome(16, 0, 32, 0, false) != MFC_OUTCOME_CANCELLED,
          "an unfinished, non-cancelled zero-data result is never CANCELLED");
}

static void test_eligibility_matrix(void)
{
    mfc_action_eligibility_t e;

    e = mfc_action_eligibility(MFC_OUTCOME_COMPLETE);
    CHECK(e.save && !e.save_partial, "COMPLETE: Save (not Save Partial)");
    CHECK(e.info, "COMPLETE: Info");
    CHECK(e.emulate_write_allowed, "COMPLETE: Emulate/Write may be considered");
    CHECK(!e.try_again, "COMPLETE: no Try Again");
    CHECK(!e.find_missing_keys, "COMPLETE: no Find Missing Keys (nothing missing)");

    e = mfc_action_eligibility(MFC_OUTCOME_PARTIAL);
    CHECK(e.save_partial && !e.save, "PARTIAL: Save Partial (not plain Save)");
    CHECK(e.info, "PARTIAL: Info");
    CHECK(e.find_missing_keys, "PARTIAL: Find Missing Keys");
    CHECK(!e.try_again, "PARTIAL: no blind Try Again -- Find Missing Keys replaces it");
    CHECK(!e.emulate_write_allowed, "PARTIAL: Emulate/Write never considered");

    e = mfc_action_eligibility(MFC_OUTCOME_FAILED);
    CHECK(e.try_again, "FAILED: Try Again");
    CHECK(e.info, "FAILED: Info (the card still identified itself)");
    CHECK(!e.find_missing_keys,
          "FAILED: no Find Missing Keys -- zero progress exists, so it would be "
          "functionally identical to Try Again, a dead duplicate action");
    CHECK(!e.save && !e.save_partial, "FAILED: no Save of any kind");
    CHECK(!e.emulate_write_allowed, "FAILED: Emulate/Write never considered");

    e = mfc_action_eligibility(MFC_OUTCOME_CANCELLED);
    CHECK(e.try_again, "CANCELLED (no data): Try Again -- not a dead end");
    CHECK(e.info, "CANCELLED (no data): Info (the card still identified itself)");
    CHECK(!e.find_missing_keys, "CANCELLED (no data): no Find Missing Keys, same reasoning as FAILED");
    CHECK(!e.save && !e.save_partial && !e.emulate_write_allowed,
          "CANCELLED (no data): no Save/Emulate/Write");

    e = mfc_action_eligibility(MFC_OUTCOME_NONE);
    CHECK(!e.save && !e.save_partial && !e.info && !e.try_again &&
          !e.find_missing_keys && !e.emulate_write_allowed,
          "NONE: no actions offered at all");
}

/* mfc_action_eligibility_any() drives whether the read-result screen's
 * "More" control is shown/functional at all (see m1_nfc.c's
 * nfc_read_more_menu_has_items(), which cannot itself be linked/tested on
 * host -- it calls into the HAL-coupled nfc_build_action_menu()). This is
 * the pure predicate that production relies on matching that real menu's
 * item count; exercised here for every outcome mfc_classify_outcome() can
 * actually produce. */
static void test_action_eligibility_any_covers_every_outcome(void)
{
    CHECK(mfc_action_eligibility_any(mfc_action_eligibility(MFC_OUTCOME_COMPLETE)),
          "COMPLETE: menu has items (Save + Info)");
    CHECK(mfc_action_eligibility_any(mfc_action_eligibility(MFC_OUTCOME_PARTIAL)),
          "PARTIAL: menu has items (Save Partial + Find Missing Keys + Info)");
    CHECK(mfc_action_eligibility_any(mfc_action_eligibility(MFC_OUTCOME_FAILED)),
          "FAILED: menu has items (Info) -- More is offered even though there's nothing to save");
    CHECK(mfc_action_eligibility_any(mfc_action_eligibility(MFC_OUTCOME_CANCELLED)),
          "CANCELLED (no data): menu has items (Info) -- More is offered, not a dead end");
    CHECK(!mfc_action_eligibility_any(mfc_action_eligibility(MFC_OUTCOME_NONE)),
          "NONE: menu is empty -- More must not be offered");
}

int main(void)
{
    test_classify_complete();
    test_classify_partial();
    test_classify_failed_and_cancelled();
    test_eligibility_matrix();
    test_action_eligibility_any_covers_every_outcome();

    printf("mfc_result_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
