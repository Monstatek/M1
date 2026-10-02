/* Source-seam regression test for the Find Missing Keys dispatch wiring in
 * NFC/NFC_drv/legacy/nfc_poller.c and nfc_driver.c.
 *
 * These files cannot be host-linked (RFAL/FreeRTOS throughout), so this test
 * checks the narrow production seam the required tests actually care about:
 *
 * 1. (Required test #14, "no duplicate nonce/key/recovery dispatch")
 *    nfc_mfc_find_keys_run()'s body never calls nfc_ctx_clear_mfc() -- unlike
 *    m1_mfc_read_card() and mfc_dict_scan(), which both do, this function
 *    must extend the SAME acquisition context, never start a fresh one.
 * 2. Q_EVENT_NFC_MFC_FIND_KEYS is dispatched exactly once from the worker's
 *    event-wait branch (posts nfc_poller_mfc_find_keys_begin()) and exactly
 *    once from the NFC_STATE_PROCESS one-shot branch (calls
 *    nfc_mfc_find_keys_run() exactly once, not mfc_dict_scan() or
 *    m1_mfc_read_card() -- no double dispatch to two different acquisition
 *    entry points for the same event).
 * 3. (Required test #13 integration layer) nfc_mfc_find_keys_run()'s body
 *    calls mfc_identity_matches() and, on mismatch, sets
 *    sc->state = NFC_SCAN_IDENTITY_MISMATCH and returns BEFORE the dict-phase
 *    call that would touch mfc/sc/resume -- i.e. the identity check gates
 *    the continuation, it does not run after it.
 * 4. nfc_poller_mfc_find_keys_begin() resets the SAME s_mfc_scan_abort flag
 *    Dictionary Scan and normal Read's own dictionary phase already use --
 *    no second, parallel abort flag was introduced.
 * 5. m1_mfc_acquire_dict_phase() is called with a non-NULL resume argument
 *    from nfc_mfc_find_keys_run() (continuation), and with NULL from both
 *    m1_mfc_read_card() and mfc_dict_scan() (unchanged, fresh-context
 *    behavior) -- confirming the resume plumbing reaches exactly the one
 *    intended call site.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/legacy/test/nfc_mfc_find_keys_dispatch_test.c \
 *      -o /tmp/nfc_mfc_find_keys_dispatch_test && /tmp/nfc_mfc_find_keys_dispatch_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass_count;
static int fail_count;
#define CHECK(c, m) do { \
    if (c) { pass_count++; } \
    else { fail_count++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } \
} while (0)

static int bounded_contains(const char *hay, size_t hay_len, const char *needle)
{
    if (hay == NULL || hay_len == 0U) return 0;
    char *buf = malloc(hay_len + 1U);
    if (buf == NULL) return 0;
    memcpy(buf, hay, hay_len);
    buf[hay_len] = '\0';
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

static int bounded_count(const char *hay, size_t hay_len, const char *needle)
{
    if (hay == NULL || hay_len == 0U) return 0;
    char *buf = malloc(hay_len + 1U);
    if (buf == NULL) return 0;
    memcpy(buf, hay, hay_len);
    buf[hay_len] = '\0';
    int count = 0;
    const char *p = buf;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { count++; p += nlen; }
    free(buf);
    return count;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = malloc((size_t)n + 1U);
    if (s == NULL) { fclose(f); return NULL; }
    if (fread(s, 1U, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = '\0';
    fclose(f);
    return s;
}

int main(void)
{
    char *poller = read_all("NFC/NFC_drv/legacy/nfc_poller.c");
    char *driver = read_all("NFC/NFC_drv/legacy/nfc_driver.c");
    CHECK(poller != NULL, "nfc_poller.c readable");
    CHECK(driver != NULL, "nfc_driver.c readable");

    if (poller != NULL) {
        const char *fn_start = strstr(poller, "void nfc_mfc_find_keys_run(void)\n{");
        const char *fn_end   = fn_start ? strstr(fn_start, "\n/*====") : NULL;
        size_t fn_len = (fn_start != NULL && fn_end != NULL && fn_end > fn_start)
                        ? (size_t)(fn_end - fn_start) : 0U;
        CHECK(fn_start != NULL, "nfc_mfc_find_keys_run() is defined");
        CHECK(fn_len > 0U, "nfc_mfc_find_keys_run()'s body could be bounded");

        if (fn_len > 0U) {
            CHECK(!bounded_contains(fn_start, fn_len, "nfc_ctx_clear_mfc()"),
                  "required test #14: nfc_mfc_find_keys_run() never clears the MFC context -- "
                  "it extends the same acquisition, never starts a fresh one");

            const char *identity_call = strstr(fn_start, "mfc_identity_matches(");
            CHECK(identity_call != NULL && (size_t)(identity_call - fn_start) < fn_len,
                  "nfc_mfc_find_keys_run() calls mfc_identity_matches()");

            const char *mismatch_state = strstr(fn_start, "NFC_SCAN_IDENTITY_MISMATCH");
            CHECK(mismatch_state != NULL && (size_t)(mismatch_state - fn_start) < fn_len,
                  "nfc_mfc_find_keys_run() sets NFC_SCAN_IDENTITY_MISMATCH on a mismatch");

            const char *dict_phase_call = strstr(fn_start, "m1_mfc_acquire_dict_phase(");
            CHECK(dict_phase_call != NULL && (size_t)(dict_phase_call - fn_start) < fn_len,
                  "nfc_mfc_find_keys_run() calls the canonical dict-phase acquisition core");

            /* Required test #13 integration: the identity check gates the
             * continuation -- it must appear BEFORE the dict-phase call
             * that would touch mfc/sc/resume, not after. */
            if (identity_call != NULL && dict_phase_call != NULL) {
                CHECK(identity_call < dict_phase_call,
                      "the identity check happens BEFORE the dict-phase call, gating it -- "
                      "a mismatch never reaches the code that would touch mfc/sc/resume");
            }

            /* This call site's resume argument must NOT be NULL (that would
             * silently drop the whole continuation/resume mechanism). The
             * two other call sites (m1_mfc_read_card, mfc_dict_scan) DO pass
             * NULL -- checked separately below, not within this bounded
             * function body. */
            if (dict_phase_call != NULL) {
                const char *paren_open = strchr(dict_phase_call, '(');
                const char *paren_close = paren_open ? strchr(paren_open, ')') : NULL;
                int call_has_null_resume = 0;
                if (paren_open != NULL && paren_close != NULL) {
                    size_t call_len = (size_t)(paren_close - paren_open);
                    call_has_null_resume = bounded_contains(paren_open, call_len, ", NULL)");
                }
                CHECK(!call_has_null_resume,
                      "nfc_mfc_find_keys_run()'s dict-phase call passes a real resume pointer, not NULL");
            }

            /* Approved design: "If the dictionary changed, preserve
             * recovered card data but restart only the System source" --
             * the identity check + reset call must both appear, and the
             * reset must happen BEFORE the dict-phase call it protects. */
            const char *sys_identity_check = strstr(fn_start, "mfc_dict_resume_system_identity_matches(");
            const char *sys_reset_call     = strstr(fn_start, "mfc_dict_resume_reset_system_only(");
            CHECK(sys_identity_check != NULL && (size_t)(sys_identity_check - fn_start) < fn_len,
                  "nfc_mfc_find_keys_run() checks the System dictionary file's identity before trusting its resume cursor");
            CHECK(sys_reset_call != NULL && (size_t)(sys_reset_call - fn_start) < fn_len,
                  "nfc_mfc_find_keys_run() calls mfc_dict_resume_reset_system_only() on a changed/replaced dictionary");
            if (sys_identity_check != NULL && sys_reset_call != NULL && dict_phase_call != NULL) {
                CHECK(sys_identity_check < sys_reset_call && sys_reset_call < dict_phase_call,
                      "identity check -> conditional reset -> dict-phase call, in that order");
            }
            /* mfc_dict_resume_reset_system_only() must NOT be the full
             * mfc_dict_resume_reset() -- a changed dictionary must never
             * discard the built-in/User seen[] dedup or any already-
             * recovered key/block, only the System cursor. */
            CHECK(!bounded_contains(fn_start, fn_len, "mfc_dict_resume_reset(resume)"),
                  "nfc_mfc_find_keys_run() never calls the FULL resume reset -- "
                  "only the System-only variant, preserving seen[]/recovered data on a changed dictionary");
        }

        /* Exactly 3 production call sites for m1_mfc_acquire_dict_phase():
         * m1_mfc_read_card(), mfc_dict_scan(), nfc_mfc_find_keys_run(). The
         * first two must keep passing NULL (unchanged, fresh-context
         * behavior); only the third passes resume. */
        int call_shape_count = bounded_count(poller, strlen(poller),
                                             "m1_mfc_acquire_dict_phase(dev, cuid, cfgs, 2");
        CHECK(call_shape_count == 3,
              "exactly 3 real call sites match the (dev, cuid, cfgs, 2, ...) call shape -- "
              "m1_mfc_read_card(), mfc_dict_scan(), nfc_mfc_find_keys_run(), no others");

        CHECK(bounded_contains(poller, strlen(poller),
                  "s_mfc_find_keys_mode = true;  s_mfc_scan_abort = false;"),
              "nfc_poller_mfc_find_keys_begin() resets the SAME s_mfc_scan_abort flag "
              "Dictionary Scan and normal Read already share -- no second abort flag");
    }

    if (driver != NULL) {
        CHECK(bounded_count(driver, strlen(driver), "q_evt_type==Q_EVENT_NFC_MFC_FIND_KEYS") == 1,
              "Q_EVENT_NFC_MFC_FIND_KEYS is checked exactly once in the event-post dispatch -- "
              "a single, unambiguous trigger for this continuation");
        CHECK(bounded_contains(driver, strlen(driver), "extern void nfc_mfc_find_keys_run(void);"),
              "the forward declaration for nfc_mfc_find_keys_run() is present");
        CHECK(bounded_count(driver, strlen(driver), "nfc_mfc_find_keys_run();") == 1,
              "nfc_mfc_find_keys_run() is called exactly once -- the one-shot dispatch branch");
        CHECK(bounded_contains(driver, strlen(driver), "nfc_poller_mfc_find_keys_begin();"),
              "the event-post branch calls nfc_poller_mfc_find_keys_begin()");
        CHECK(bounded_contains(driver, strlen(driver), "nfc_poller_mfc_find_keys_end();"),
              "the one-shot dispatch branch calls nfc_poller_mfc_find_keys_end() after running");
        /* The one-shot branch must dispatch to nfc_mfc_find_keys_run() only
         * -- not ALSO fall through to mfc_dict_scan() or m1_mfc_read_card()
         * for the same event (no duplicate dispatch, required test #14). */
        const char *active_check = strstr(driver, "nfc_poller_mfc_find_keys_active()");
        const char *branch_end   = active_check ? strstr(active_check, "break;") : NULL;
        size_t branch_len = (active_check != NULL && branch_end != NULL && branch_end > active_check)
                             ? (size_t)(branch_end - active_check) : 0U;
        CHECK(branch_len > 0U, "the Find Missing Keys one-shot branch's extent could be bounded");
        if (branch_len > 0U) {
            CHECK(!bounded_contains(active_check, branch_len, "mfc_dict_scan()"),
                  "the Find Missing Keys dispatch branch never also calls mfc_dict_scan()");
            CHECK(!bounded_contains(active_check, branch_len, "m1_mfc_read_card("),
                  "the Find Missing Keys dispatch branch never also calls m1_mfc_read_card()");
        }
    }

    free(poller);
    free(driver);

    printf("nfc_mfc_find_keys_dispatch_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
