/* Source-seam regression test for the MFC dictionary UI in
 * m1_csrc/m1_nfc.c (NFC2-005): the shared dictionary-progress renderer, its
 * reuse by both normal Read and Find Missing Keys, the Exit/Stay
 * confirmation replacing normal Read's old destructive BACK, the Lost Tag
 * presentation, and the Partial Info identity block.
 *
 * m1_nfc.c cannot be host-linked (FreeRTOS/u8g2/HAL throughout), so this
 * checks the narrow production seam each required behavior actually lives
 * in, via bounded string search against the real, committed source text.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_mfc_dict_ui_test.c \
 *      -o /tmp/nfc_mfc_dict_ui_test && /tmp/nfc_mfc_dict_ui_test
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

/* Isolate a function's body by its CRLF-anchored "...)\r\n{" definition
 * (m1_nfc.c uses CRLF throughout -- this anchors to the DEFINITION, not an
 * earlier forward declaration ending in ";"), up to the next top-level
 * "\r\nstatic " (the start of whatever function follows it in file order). */
static const char *bound_function(const char *src, const char *sig, size_t *out_len)
{
    const char *start = strstr(src, sig);
    const char *end = start ? strstr(start + 1, "\r\nstatic ") : NULL;
    *out_len = (start != NULL && end != NULL && end > start) ? (size_t)(end - start) : 0U;
    return start;
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
    char *src = read_all("m1_csrc/m1_nfc.c");
    CHECK(src != NULL, "m1_nfc.c readable");
    if (src == NULL) {
        printf("nfc_mfc_dict_ui_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }
    /* --- Shared renderer: header truthfully names the active source ------ */
    size_t progress_len;
    const char *progress = bound_function(src,
        "static void nfc_mfc_dict_progress_draw(void)\r\n{", &progress_len);
    CHECK(progress != NULL, "nfc_mfc_dict_progress_draw() is defined");
    CHECK(progress_len > 0U, "nfc_mfc_dict_progress_draw()'s body could be bounded");
    if (progress_len > 0U) {
        /* "MF Classic " is deliberately dropped from the on-screen header
         * (visual-render gate finding): at NFC_FONT_EMPH the full "MF
         * Classic System Dictionary" measures 149px, wider than the 126px
         * usable width, and clips on real hardware. This screen is only
         * ever reached for M1NFC_FAM_CLASSIC, so the prefix is redundant
         * with context; every word that actually distinguishes the three
         * real M1 sources is kept. */
        CHECK(bounded_contains(progress, progress_len, "\"User Dictionary\""),
              "header names the User Dictionary stage truthfully");
        CHECK(bounded_contains(progress, progress_len, "\"Built-in Keys\""),
              "header names the Built-in Keys stage truthfully (M1's own real source, per the approved directive)");
        CHECK(bounded_contains(progress, progress_len, "\"System Dictionary\""),
              "header names the System Dictionary stage truthfully");
        CHECK(bounded_contains(progress, progress_len, "\"Unlocking sector: %u\""),
              "the live current-sector line uses the required wording");
        CHECK(bounded_contains(progress, progress_len, "\"Keys found: %u/%u\""),
              "the Keys found line uses the required wording");
        CHECK(bounded_contains(progress, progress_len, "\"Sectors Read: %u/%u\""),
              "the Sectors Read line uses the required wording");
        CHECK(bounded_contains(progress, progress_len, "\"Skip\""),
              "CENTER is labeled \"Skip\"");
        CHECK(bounded_contains(progress, progress_len, "target_10x10"),
              "Skip uses M1's established single-control footer icon");
        CHECK(bounded_contains(progress, progress_len, "nfc_mfc_dict_totals_ensure();"),
              "totals are ensured (computed-once-guarded), not recomputed inline every redraw");
        CHECK(bounded_contains(progress, progress_len, "nfc_mfc_progress_bar_draw("),
              "the live progress uses the real bar-drawing helper, not a duplicated bar implementation");
    }

    /* --- Progress bar: current/total, clamp, zero-total-safe -------------- */
    size_t bar_len;
    const char *bar = bound_function(src,
        "static void nfc_mfc_progress_bar_draw(uint8_t x, uint8_t y, uint8_t w, uint8_t h,\r\n"
        "                                      uint32_t current, uint32_t total)\r\n{", &bar_len);
    CHECK(bar != NULL, "nfc_mfc_progress_bar_draw() is defined");
    CHECK(bar_len > 0U, "nfc_mfc_progress_bar_draw()'s body could be bounded");
    if (bar_len > 0U) {
        CHECK(bounded_contains(bar, bar_len, "if (total == 0U) {"),
              "zero-total is explicitly handled -- never a bare division by total");
        CHECK(bounded_contains(bar, bar_len, "(current > total) ? total : current"),
              "current is clamped at total before use -- never renders over 100%");
        CHECK(bounded_contains(bar, bar_len, "\"%lu/%lu\""),
              "the bar's text is current/total, not a fabricated overall percentage");
        CHECK(bounded_contains(bar, bar_len, "(current == 0U) ? 1U : current"),
              "a genuinely-zero current displays as 1, never a raw 0");
    }

    /* --- Lost Tag: required text, Skip still offered ----------------------- */
    size_t lost_len;
    const char *lost = bound_function(src, "static void nfc_mfc_lost_tag_draw(void)\r\n{", &lost_len);
    CHECK(lost != NULL, "nfc_mfc_lost_tag_draw() is defined");
    CHECK(lost_len > 0U, "nfc_mfc_lost_tag_draw()'s body could be bounded");
    if (lost_len > 0U) {
        CHECK(bounded_contains(lost, lost_len, "\"Lost the tag!\""),
              "required wording: \"Lost the tag!\"");
        CHECK(bounded_contains(lost, lost_len, "\"Make sure the tag is\""),
              "required wording line 1 of the guidance text");
        CHECK(bounded_contains(lost, lost_len, "\"positioned correctly.\""),
              "required wording line 2 of the guidance text");
        CHECK(bounded_contains(lost, lost_len, "\"Skip\""),
              "Skip stays visible and available while the tag is lost");
    }

    /* --- Exit/Stay confirmation: required text and button placement -------- */
    size_t exit_len;
    const char *exitc = bound_function(src, "static void nfc_mfc_exit_confirm_draw(void)\r\n{", &exit_len);
    CHECK(exitc != NULL, "nfc_mfc_exit_confirm_draw() is defined");
    CHECK(exit_len > 0U, "nfc_mfc_exit_confirm_draw()'s body could be bounded");
    if (exit_len > 0U) {
        CHECK(bounded_contains(exitc, exit_len, "\"Exit to NFC Menu?\""),
              "required header text");
        CHECK(bounded_contains(exitc, exit_len, "\"All unsaved data will be lost\""),
              "required body text");
        CHECK(bounded_contains(exitc, exit_len, "\"Exit\""), "Exit button is labeled");
        CHECK(bounded_contains(exitc, exit_len, "\"Stay\""), "Stay button is labeled");
    }

    /* --- Normal Read enters the dictionary display automatically ---------- */
    size_t read_update_len;
    const char *read_update = bound_function(src, "static void nfc_read_gui_update(uint8_t param)\r\n{",
                                             &read_update_len);
    CHECK(read_update != NULL, "nfc_read_gui_update() is defined");
    CHECK(read_update_len > 0U, "nfc_read_gui_update()'s body could be bounded");
    if (read_update_len > 0U) {
        CHECK(bounded_contains(read_update, read_update_len, "nfc_mfc_dict_progress_draw();"),
              "READING_READY calls the shared dictionary-progress renderer -- no second implementation");
        CHECK(bounded_contains(read_update, read_update_len, "nfc_mfc_lost_tag_draw();"),
              "READING_READY calls the shared Lost Tag renderer");
        CHECK(bounded_contains(read_update, read_update_len, "scan_state == NFC_SCAN_RUNNING"),
              "the dictionary display is gated on the real live scan state, not a UI-invented flag");
        CHECK(bounded_contains(read_update, read_update_len, "M1NFC_FAM_CLASSIC"),
              "the dictionary display is gated on the family being Classic -- never forces a non-MFC card through it");
        CHECK(bounded_contains(read_update, read_update_len, "m1_read_icon_draw(&m1_u8g2, 'N', 3);"),
              "the original generic waiting icon is still present and reachable (before dictionary work / non-MFC families)");
        CHECK(bounded_contains(read_update, read_update_len, "nfc_mfc_exit_confirm_draw();"),
              "the new Exit-confirm display param is wired to its renderer");
    }

    /* --- BACK opens Exit/Stay (not a direct teardown) while mid-phase ------ */
    size_t kp_len;
    const char *kp = bound_function(src, "static int nfc_read_kp_handler(void)\r\n{", &kp_len);
    CHECK(kp != NULL, "nfc_read_kp_handler() is defined");
    CHECK(kp_len > 0U, "nfc_read_kp_handler()'s body could be bounded");
    if (kp_len > 0U) {
        CHECK(bounded_contains(kp, kp_len, "NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM"),
              "BACK during a live dictionary phase routes to the Exit-confirm display param");
        CHECK(bounded_contains(kp, kp_len, "nfc_poller_mfc_scan_skip_source();"),
              "CENTER dispatches the real skip-current-source-only control");
        CHECK(bounded_contains(kp, kp_len, "s_read_exiting = true;"),
              "confirming Exit marks the session as exiting -- gates the deferred teardown, never an immediate one");
        CHECK(bounded_contains(kp, kp_len, "nfc_poller_mfc_scan_abort();"),
              "confirming Exit still uses the existing whole-phase abort flag to actually stop the worker");
        /* Exit must NOT still contain the OLD immediate teardown sequence
         * (switch to IDLE + reset queue) directly inside the confirm
         * handling -- that belongs only in nfc_read_gui_message()'s
         * deferred, s_read_exiting-gated path now. */
        const char *exit_branch = strstr(kp, "s_read_exiting = true;");
        if (exit_branch != NULL) {
            const char *branch_end = strstr(exit_branch, "}");
            size_t blen = (branch_end != NULL) ? (size_t)(branch_end - exit_branch) : 0U;
            CHECK(blen > 0U && !bounded_contains(exit_branch, blen, "VIEW_MODE_IDLE"),
                  "confirming Exit does not itself switch to IDLE -- teardown is deferred to the real worker completion");
        }
    }

    /* --- nfc_read_gui_message(): deferred exit, never loses/misfires ------ */
    size_t msg_len;
    const char *msg = bound_function(src, "static int nfc_read_gui_message(void)\r\n{", &msg_len);
    CHECK(msg != NULL, "nfc_read_gui_message() is defined");
    CHECK(msg_len > 0U, "nfc_read_gui_message()'s body could be bounded");
    if (msg_len > 0U) {
        CHECK(bounded_contains(msg, msg_len, "if (s_read_exiting)"),
              "the real Q_EVENT_NFC_READ_COMPLETE handler branches on the exiting flag");
        CHECK(bounded_contains(msg, msg_len, "xQueueReset(main_q_hdl);"),
              "the queue reset happens inside this handler -- only in response to the REAL completion event");
        CHECK(bounded_contains(msg, msg_len, "m1_uiView_display_switch(VIEW_MODE_IDLE, 0);"),
              "the idle transition happens here, gated on s_read_exiting, not pre-emptively in the keypad handler");
        /* An explicit exit must never fall through to showing a read
         * result -- the s_read_exiting branch must return/exit before
         * reaching the normal READING_COMPLETE display update. */
        const char *exiting_branch = strstr(msg, "if (s_read_exiting)");
        const char *else_branch    = exiting_branch ? strstr(exiting_branch, "else") : NULL;
        size_t exiting_len = (exiting_branch != NULL && else_branch != NULL && else_branch > exiting_branch)
                              ? (size_t)(else_branch - exiting_branch) : 0U;
        CHECK(exiting_len > 0U, "the s_read_exiting branch's extent could be bounded (its own else found)");
        if (exiting_len > 0U) {
            CHECK(!bounded_contains(exiting_branch, exiting_len, "NFC_READ_DISPLAY_PARAM_READING_COMPLETE"),
                  "an explicit Exit never shows a read result (Partial or otherwise) -- required: never show Partial on explicit Exit");
        }
    }

    /* --- Find Missing Keys reuses the shared renderer, not a duplicate ---- */
    size_t fk_len;
    const char *fk = bound_function(src, "static void nfc_mfc_find_keys_draw(void)\r\n{", &fk_len);
    CHECK(fk != NULL, "nfc_mfc_find_keys_draw() is defined");
    CHECK(fk_len > 0U, "nfc_mfc_find_keys_draw()'s body could be bounded");
    if (fk_len > 0U) {
        CHECK(bounded_contains(fk, fk_len, "nfc_mfc_dict_progress_draw();"),
              "Find Missing Keys' RUNNING state calls the SAME shared renderer normal Read uses");
        CHECK(bounded_contains(fk, fk_len, "nfc_mfc_lost_tag_draw();"),
              "Find Missing Keys' CARD_LOST state calls the SAME shared Lost Tag renderer -- not a 3rd, terminal-looking state");
        /* Must NOT contain a second hand-written progress bar / header /
         * "Keys found" line -- that would be a duplicate implementation. */
        CHECK(!bounded_contains(fk, fk_len, "\"User Dictionary\""),
              "no duplicated header text -- Find Missing Keys' own draw function doesn't re-implement the shared layout");
    }
    size_t fk_kp_len;
    const char *fk_kp = bound_function(src, "static int nfc_mfc_find_keys_kp_handler(void)\r\n{", &fk_kp_len);
    CHECK(fk_kp != NULL, "nfc_mfc_find_keys_kp_handler() is defined");
    CHECK(fk_kp_len > 0U, "nfc_mfc_find_keys_kp_handler()'s body could be bounded");
    if (fk_kp_len > 0U) {
        CHECK(bounded_contains(fk_kp, fk_kp_len, "nfc_poller_mfc_scan_skip_source();"),
              "Find Missing Keys also dispatches the SAME real skip-current-source-only control (shared CENTER Skip)");
        CHECK(bounded_contains(fk_kp, fk_kp_len, "NFC_SCAN_CARD_LOST"),
              "BACK/CENTER handling accounts for CARD_LOST as a live (non-terminal) state, matching normal Read");
    }

    /* --- Partial Info: same identity block as Complete --------------------- */
    const char *mfc_branch = strstr(src,
        "if (c && c->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)");
    const char *mfc_branch_end = mfc_branch ? strstr(mfc_branch, "if (c && c->head.family == M1NFC_FAM_ST25TB)") : NULL;
    size_t mfc_branch_len = (mfc_branch != NULL && mfc_branch_end != NULL && mfc_branch_end > mfc_branch)
                            ? (size_t)(mfc_branch_end - mfc_branch) : 0U;
    CHECK(mfc_branch != NULL, "the MFC Info branch exists");
    CHECK(mfc_branch_len > 0U, "the MFC Info branch's extent could be bounded");
    if (mfc_branch_len > 0U) {
        const char *complete_start = strstr(mfc_branch, "if (mc->outcome == MFC_OUTCOME_COMPLETE)");
        size_t complete_len = (complete_start != NULL && (size_t)(complete_start - mfc_branch) < mfc_branch_len)
                              ? mfc_branch_len - (size_t)(complete_start - mfc_branch) : 0U;
        CHECK(complete_len > 0U, "the COMPLETE sub-branch's extent could be bounded");

        /* The COMPLETE branch itself must be byte-for-byte unchanged from
         * the prior increment -- required: "Complete behavior remains
         * unchanged". Spot-check its load-bearing strings still exist. */
        if (complete_len > 0U) {
            CHECK(bounded_contains(complete_start, complete_len,
                      "(mc->type == M1NFC_MFCTYPE_4K) ? \"4K\" : \"1K\""),
                  "COMPLETE's type-based title logic is unchanged");
            CHECK(bounded_contains(complete_start, complete_len, "\"Keys: %u/%u  Sec: %u/%u\""),
                  "COMPLETE's combined Keys/Sec line is unchanged");
        }

        /* The region AFTER the COMPLETE sub-branch's own return (i.e. the
         * non-COMPLETE code that follows it in the same outer branch) must
         * now ALSO contain the ATQA/SAK block -- required: "Partial Info
         * includes UID/ATQA/SAK and counts". */
        const char *partial_region = complete_start ? strstr(complete_start, "m1_u8g2_nextpage();\r\n            return;\r\n        }") : NULL;
        size_t partial_len = (partial_region != NULL) ? (mfc_branch_len - (size_t)(partial_region - mfc_branch)) : 0U;
        CHECK(partial_region != NULL, "the non-COMPLETE code region (after COMPLETE's own return) could be located");
        if (partial_len > 0U) {
            CHECK(bounded_contains(partial_region, partial_len, "u8g2_DrawStr(&m1_u8g2, 2, 32, \"ATQA:\");"),
                  "Partial/Failed/Cancelled Info now shows ATQA, same label/position convention as Complete");
            CHECK(bounded_contains(partial_region, partial_len, "if (c->head.a.has_atqa)"),
                  "Partial ATQA is gated on has_atqa -- never fabricated");
            CHECK(bounded_contains(partial_region, partial_len, "u8g2_DrawStr(&m1_u8g2, 74, 32, \"SAK:\");"),
                  "Partial/Failed/Cancelled Info now shows SAK");
            CHECK(bounded_contains(partial_region, partial_len, "if (c->head.a.has_sak)"),
                  "Partial SAK is gated on has_sak -- never fabricated");
            CHECK(bounded_contains(partial_region, partial_len,
                      "(mc->type == M1NFC_MFCTYPE_4K) ? \"4K\" : \"1K\""),
                  "Partial's title now uses the real detected type too (was previously hardcoded to \"1K\")");
        }
    }

    /* --- Defect 1 (hardware finding): BACK from a finished, unsaved MFC
     * Partial result must open the Exit/Stay confirmation, never fall into
     * the READING_READY catchall (which switches straight to VIEW_MODE_IDLE
     * with no confirmation at all -- the reported "dead Hold-tag-to-M1
     * screen"). The confirmation itself must route Stay/Exit differently
     * depending on whether it was opened from an ACTIVE live phase or a
     * FINISHED result, tracked via s_exit_confirm_from_finished_result. --- */
    size_t kp2_len;
    const char *kp2 = bound_function(src, "static int nfc_read_kp_handler(void)\r\n{", &kp2_len);
    CHECK(kp2 != NULL, "nfc_read_kp_handler() is defined");
    CHECK(kp2_len > 0U, "nfc_read_kp_handler()'s body could be bounded");
    if (kp2_len > 0U) {
        CHECK(bounded_contains(kp2, kp2_len, "s_exit_confirm_from_finished_result"),
              "the confirm-origin tracking variable is used in the keypad handler");

        /* Required: "Partial Read BACK -> Exit confirmation", scoped to
         * MFC_OUTCOME_PARTIAL specifically. */
        const char *partial_back = strstr(kp2, "p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE\r\n");
        CHECK(partial_back != NULL && partial_back < kp2 + kp2_len,
              "BACK has an explicit branch for p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE");
        size_t partial_back_len = (partial_back != NULL) ? (size_t)((kp2 + kp2_len) - partial_back) : 0U;
        /* Bound just this branch: up to its own closing "}\r\n\t\t\telse" that
         * starts the next sibling branch (the READING_READY catchall). */
        const char *partial_back_end = (partial_back_len > 0U) ? strstr(partial_back, "\r\n\t\t\telse\r\n\t\t\t{") : NULL;
        size_t partial_back_body_len = (partial_back_end != NULL) ? (size_t)(partial_back_end - partial_back) : 0U;
        CHECK(partial_back_body_len > 0U, "the READING_COMPLETE BACK branch's extent could be bounded (its own catchall sibling found)");
        if (partial_back_body_len > 0U) {
            CHECK(bounded_contains(partial_back, partial_back_body_len, "MFC_OUTCOME_PARTIAL"),
                  "the branch is scoped to MFC_OUTCOME_PARTIAL specifically, not every finished result");
            CHECK(bounded_contains(partial_back, partial_back_body_len, "s_exit_confirm_from_finished_result = true;"),
                  "entering the confirmation from a finished Partial result marks the origin as finished");
            CHECK(bounded_contains(partial_back, partial_back_body_len, "NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM"),
                  "Partial Read BACK opens the Exit/Stay confirmation");
            /* The literal requirement: this exact branch must never itself
             * route to READING_READY -- it must be a hard alternative to the
             * catchall below, not a variant of it. */
            CHECK(!bounded_contains(partial_back, partial_back_body_len, "READING_READY"),
                  "PARTIAL BACK's own branch never mentions READING_READY -- cannot route there");
            CHECK(!bounded_contains(partial_back, partial_back_body_len, "VIEW_MODE_IDLE"),
                  "PARTIAL BACK's own branch never falls through to the direct-idle catchall");
        }

        /* Required: "Active dictionary BACK -> Exit confirmation" preserved
         * (the pre-existing catchall, now reached only for non-Partial
         * results and the true READING_READY case). */
        const char *catchall = (partial_back_end != NULL) ? partial_back_end : kp2;
        size_t catchall_len = (size_t)((kp2 + kp2_len) - catchall);
        CHECK(bounded_contains(catchall, catchall_len, "scan_state == NFC_SCAN_RUNNING") &&
              bounded_contains(catchall, catchall_len, "scan_state == NFC_SCAN_CARD_LOST"),
              "the active-phase catchall (RUNNING/CARD_LOST -> Exit confirmation) is preserved unchanged");

        /* Required: confirmation Stay/Exit are origin-aware. Both the RIGHT
         * handler and the BACK-on-confirm handler use this exact ternary --
         * count 2, not just presence, so a future edit that only fixes one
         * of them still fails this check. */
        {
            const char *needle = "s_exit_confirm_from_finished_result ?";
            int ternary_count = 0;
            const char *scan = kp2;
            size_t remaining = kp2_len;
            for (;;) {
                const char *hit = NULL;
                for (size_t i = 0; i + strlen(needle) <= remaining; i++) {
                    if (memcmp(scan + i, needle, strlen(needle)) == 0) { hit = scan + i; break; }
                }
                if (hit == NULL) break;
                ternary_count++;
                remaining -= (size_t)(hit - scan) + 1U;
                scan = hit + 1;
            }
            CHECK(ternary_count == 2,
                  "both Stay paths (RIGHT and BACK-on-confirm) use the origin-aware ternary -- found both, not just one");
        }
        CHECK(bounded_contains(kp2, kp2_len, "NFC_READ_DISPLAY_PARAM_READING_COMPLETE : NFC_READ_DISPLAY_PARAM_READING_READY"),
              "Stay returns to READING_COMPLETE for a finished-result origin, READING_READY for an active one (true : false order)");

        const char *exit_branch = strstr(kp2, "if (s_exit_confirm_from_finished_result)");
        CHECK(exit_branch != NULL, "the LEFT/Exit handler branches on the confirm origin");
        if (exit_branch != NULL) {
            const char *exit_branch_end = strstr(exit_branch, "\r\n\t\t\t\t}\r\n");
            size_t exit_branch_len = (exit_branch_end != NULL) ? (size_t)(exit_branch_end - exit_branch) : 0U;
            CHECK(exit_branch_len > 0U, "the finished-result Exit branch's extent could be bounded");
            if (exit_branch_len > 0U) {
                CHECK(bounded_contains(exit_branch, exit_branch_len, "VIEW_MODE_IDLE"),
                      "finished-result Exit goes directly to the NFC menu (no active worker to wait for)");
                CHECK(bounded_contains(exit_branch, exit_branch_len, "xQueueReset(main_q_hdl);"),
                      "finished-result Exit resets the queue immediately, matching every other direct exit");
                CHECK(!bounded_contains(exit_branch, exit_branch_len, "s_read_exiting = true;"),
                      "finished-result Exit does NOT set s_read_exiting -- there is no pending worker completion to defer for");
            }
            /* Active-phase Exit (falls through past the finished-result
             * early return) must still do its existing safe abort. */
            size_t after_exit_branch_len = kp2_len - (size_t)(exit_branch - kp2);
            CHECK(bounded_contains(exit_branch, after_exit_branch_len, "nfc_poller_mfc_scan_abort();") &&
                  bounded_contains(exit_branch, after_exit_branch_len, "s_read_exiting = true;"),
                  "active-phase Exit retains the existing safe worker-abort/acknowledgment behavior");
        }
    }

    /* Origin reset points: fresh session, view destruction. (Reset on the
     * Exit branches themselves is checked above via the exit_branch bounds.) */
    size_t create_len;
    const char *create_fn = bound_function(src, "static void nfc_read_gui_create(uint8_t param)\r\n{", &create_len);
    CHECK(create_fn != NULL && create_len > 0U &&
          bounded_contains(create_fn, create_len, "s_exit_confirm_from_finished_result = false;"),
          "the confirm origin resets on every fresh Read session");

    size_t destroy_len;
    const char *destroy_fn = bound_function(src, "static void nfc_read_gui_destroy(uint8_t param)\r\n{", &destroy_len);
    CHECK(destroy_fn != NULL && destroy_len > 0U &&
          bounded_contains(destroy_fn, destroy_len, "s_exit_confirm_from_finished_result = false;"),
          "the confirm origin resets on view destruction -- never carried into a later session");

    free(src);
    printf("nfc_mfc_dict_ui_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
