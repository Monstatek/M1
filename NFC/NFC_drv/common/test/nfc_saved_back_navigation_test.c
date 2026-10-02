/* Regression guard for the hardware-observed defect: NFC > Saved > select/
 * open a saved NFC file > BACK exited the whole Saved workflow and returned
 * to the parent NFC menu, instead of returning to the Saved file list.
 *
 * Root cause (confirmed by source review, not inferred): nfc_read_more_kp_handler()
 * (m1_csrc/m1_nfc.c) is the keypad handler for VIEW_MODE_NFC_READ_MORE, the
 * action/submenu screen shown after opening a saved file or completing a
 * live read. Its BACK handling distinguishes LOAD_FILE from LIVE_CARD.  The
 * LOAD_FILE branch used to just `return 0;` -- and nfc_saved()'s own driving
 * loop is `while (m1_uiView_q_message_process()) { ; }` (m1_csrc/m1_nfc.c),
 * which forwards whatever the CURRENT view's message() handler returns
 * (uiView.c's m1_uiView_q_message_process()). Returning 0 from any view's
 * message handler therefore terminates nfc_saved() outright, handing control
 * back to whatever called nfc_saved() (the NFC parent menu) -- the exact
 * observed defect.
 *
 * The fix makes the LOAD_FILE branch behave like the already-hardware-
 * validated NFC_ACT_DELETE success path a few lines above it in the same
 * function: recompute the loaded file's own directory from nfc_ctx_get()->
 * file.path (one-shot m1_fb_set_start_dir(), since m1_fb_init() always
 * resets to the SD root otherwise -- confirmed by reading m1_file_browser.c),
 * then m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0) and fall
 * through to the function's normal `return 1;` instead of returning 0.
 *
 * Known, documented limitation (source-confirmed, not a test gap): the file
 * browser has no persistent selection-cursor/scroll-position state to
 * restore across a re-entry -- m1_fb_init() (m1_csrc/m1_file_browser.c)
 * unconditionally zeroes *pfb_hdl->listing_index_buffer and
 * *pfb_hdl->row_index_buffer on every call, and m1_uiView_display_switch()
 * always tears down and recreates the view (uiView.c), so every reopen of
 * VIEW_MODE_NFC_SAVED_BROWSE starts at the top of the (correct) directory's
 * listing. Only the directory is preserved, matching the same limitation
 * the pre-existing Delete path already lives with.
 *
 * m1_nfc.c cannot be host-linked (FreeRTOS/u8g2/HAL throughout), so this
 * checks the narrow production seam each required behavior actually lives
 * in, via bounded string search against the real, committed source text --
 * same brace-matching bound_function()/bounded_contains() technique already
 * established by NFC/NFC_drv/common/test/nfc_saved_stack_test.c and
 * nfc_mfc_dict_ui_test.c for this same non-host-linkable file.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_saved_back_navigation_test.c \
 *      -o /tmp/nfc_saved_back_navigation_test && /tmp/nfc_saved_back_navigation_test
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

/* Brace-matched body bounding, anchored at a literal "...)\r\n{" (or
 * "...)\r\n\t\t\t{" for a nested block) signature -- same technique as
 * nfc_saved_stack_test.c, robust to nested braces inside the body. */
static const char *bound_block(const char *src, const char *sig, size_t *out_len)
{
    const char *start = strstr(src, sig);
    if (start == NULL) { *out_len = 0U; return NULL; }
    const char *open_brace = strchr(start, '{');
    if (open_brace == NULL) { *out_len = 0U; return NULL; }
    int depth = 0;
    const char *p = open_brace;
    const char *end = NULL;
    for (; *p != '\0'; p++) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) { end = p; break; }
        }
    }
    *out_len = (end != NULL) ? (size_t)(end - start + 1) : 0U;
    return start;
}

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

int main(void)
{
    char *src = read_all("m1_csrc/m1_nfc.c");
    CHECK(src != NULL, "m1_csrc/m1_nfc.c readable");
    if (src == NULL) {
        printf("nfc_saved_back_navigation_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }

    /* --- 1/2. LOAD_FILE action-screen BACK: routes to the Saved browser, --
     * never returns 0, never leaves the LOAD_FILE branch by any other path
     * (e.g. an accidental fallthrough to a stale "return 0;"). Bounded to
     * ONLY the `if (is_load_file) { ... }` sub-block inside
     * nfc_read_more_kp_handler(), not the whole function, so this can never
     * be satisfied by an unrelated return/switch call elsewhere in the same
     * handler. */
    size_t fn_len;
    const char *fn = bound_block(src, "static int nfc_read_more_kp_handler(void)\r\n{", &fn_len);
    CHECK(fn != NULL, "nfc_read_more_kp_handler(void) is defined");
    CHECK(fn_len > 0U, "nfc_read_more_kp_handler()'s body could be brace-bounded");

    if (fn_len > 0U) {
        size_t load_len;
        const char *load_block = bound_block(fn, "\t\t\tif (is_load_file)\r\n", &load_len);
        CHECK(load_block != NULL && load_len > 0U,
              "the LOAD_FILE BACK sub-block inside nfc_read_more_kp_handler() could be brace-bounded");

        if (load_block != NULL && load_len > 0U) {
            CHECK(bounded_contains(load_block, load_len,
                  "m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);"),
                  "LOAD_FILE BACK switches to VIEW_MODE_NFC_SAVED_BROWSE");
            CHECK(!bounded_contains(load_block, load_len, "return 0"),
                  "LOAD_FILE BACK no longer returns 0 (the exact defect: message-loop termination)");
            /* The one-shot start-dir recovery this fix adds -- same pattern
             * as the pre-existing, hardware-validated Delete path. */
            CHECK(bounded_contains(load_block, load_len, "m1_fb_set_start_dir("),
                  "LOAD_FILE BACK recovers the Saved directory via m1_fb_set_start_dir() before switching (m1_fb_init() would otherwise reset to the SD root)");
            CHECK(bounded_contains(load_block, load_len, "c->file.path"),
                  "LOAD_FILE BACK derives the recovered directory from the loaded file's own recorded path");
        }

        /* --- 3. LIVE_CARD BACK behavior: unchanged. Bounded to the `else`
         * sub-block immediately following the LOAD_FILE if-block. */
        const char *else_anchor = load_block != NULL ? (load_block + load_len) : NULL;
        size_t live_len = 0U;
        const char *live_block = NULL;
        if (else_anchor != NULL) {
            live_block = bound_block(else_anchor, "\r\n\t\t\telse\r\n", &live_len);
        }
        CHECK(live_block != NULL && live_len > 0U,
              "the LIVE_CARD (else) BACK sub-block could be brace-bounded");
        if (live_block != NULL && live_len > 0U) {
            CHECK(bounded_contains(live_block, live_len,
                  "m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);"),
                  "LIVE_CARD BACK still returns to the Read Complete screen, unchanged");
            CHECK(!bounded_contains(live_block, live_len, "VIEW_MODE_NFC_SAVED_BROWSE"),
                  "LIVE_CARD BACK path was not accidentally touched by the LOAD_FILE fix");
        }

        /* --- 5a. Delete navigation: untouched, still present verbatim
         * inside the same handler. */
        CHECK(bounded_contains(fn, fn_len, "case NFC_ACT_DELETE:"),
              "NFC_ACT_DELETE case still present in nfc_read_more_kp_handler()");
        CHECK(bounded_contains(fn, fn_len, "m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);"),
              "a successful Delete still switches to VIEW_MODE_NFC_SAVED_BROWSE (now shared in spirit, not code, with the LOAD_FILE BACK fix)");
    }

    /* --- 5b. Rename navigation: untouched -- BACK from the Rename view
     * (a distinct function/view, not touched by this fix) still returns to
     * the action/submenu screen, "the Saved context". */
    size_t rename_len;
    const char *rename_fn = bound_block(src, "static int nfc_rename_kp_handler(void)\r\n{", &rename_len);
    CHECK(rename_fn != NULL && rename_len > 0U, "nfc_rename_kp_handler(void) is defined and bounded");
    if (rename_fn != NULL && rename_len > 0U) {
        CHECK(bounded_contains(rename_fn, rename_len,
              "m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);"),
              "Rename view's BACK still returns to VIEW_MODE_NFC_READ_MORE (the Saved context), unchanged");
    }

    /* --- 4. Saved-list-level BACK: untouched -- still exits to the NFC
     * parent menu (via VIEW_MODE_IDLE) when the user backs out of the list
     * itself, not just out of an opened file. This is intentionally
     * DIFFERENT from the LOAD_FILE action-screen fix above: the list is the
     * top of the Saved workflow, so its own BACK legitimately leaves it. */
    size_t msg_len;
    const char *msg_fn = bound_block(src, "static int nfc_saved_browse_gui_message(void)\r\n{", &msg_len);
    CHECK(msg_fn != NULL && msg_len > 0U, "nfc_saved_browse_gui_message(void) is defined and bounded");
    if (msg_fn != NULL && msg_len > 0U) {
        CHECK(bounded_contains(msg_fn, msg_len, "Q_EVENT_MENU_EXIT"),
              "Saved-list BACK still routes through Q_EVENT_MENU_EXIT, unchanged");
        CHECK(bounded_contains(msg_fn, msg_len, "m1_uiView_display_switch(VIEW_MODE_IDLE, 0);"),
              "Saved-list BACK still exits to VIEW_MODE_IDLE (its normal parent), unchanged");
        CHECK(bounded_contains(msg_fn, msg_len, "ret_val = 0"),
              "Saved-list BACK still terminates nfc_saved()'s own loop at the list level, unchanged (this level exiting is correct/intentional)");
    }

    /* --- 6. No duplicate-browser-instance / state-leak risk: the generic
     * uiView dispatcher this fix relies on (unchanged by this fix, not part
     * of m1_nfc.c) still unconditionally destroys the current view before
     * creating the new one on every switch -- the structural guarantee that
     * repeated open -> BACK -> open cycles cannot accumulate more than one
     * live view/browser instance. Guards against a future regression in
     * this shared, safety-critical piece of infrastructure silently
     * invalidating the fix above. */
    {
        char *uiview_src = read_all("m1_csrc/uiView.c");
        CHECK(uiview_src != NULL, "m1_csrc/uiView.c readable");
        if (uiview_src != NULL) {
            size_t switch_len;
            /* uiView.c uses plain LF line endings, unlike m1_nfc.c's CRLF. */
            const char *switch_fn = bound_block(uiview_src,
                "void m1_uiView_display_switch(uint8_t mode, uint32_t lParam)\n{", &switch_len);
            CHECK(switch_fn != NULL && switch_len > 0U,
                  "m1_uiView_display_switch(void) is defined and bounded");
            if (switch_fn != NULL && switch_len > 0U) {
                CHECK(bounded_contains(switch_fn, switch_len, "uiview_view_list[uiview_current_mode].destroy"),
                      "m1_uiView_display_switch() still destroys the current view before creating the next one (no duplicate live view/browser instances across repeated switches)");
                CHECK(bounded_contains(switch_fn, switch_len, "uiview_view_list[mode].create"),
                      "m1_uiView_display_switch() still creates exactly the new mode's view, unchanged");
            }
            free(uiview_src);
        }
    }

    free(src);
    printf("nfc_saved_back_navigation_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
