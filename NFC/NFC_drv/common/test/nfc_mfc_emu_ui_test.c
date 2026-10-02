/* Source-seam regression test for the MFC Emulate screen.
 *
 * The firmware UI cannot be host-linked without the display/FreeRTOS stack,
 * so this test checks the narrow production seam that regressed twice:
 *
 * 1. A fresh u8g2 page must always be started before the dedicated
 *    full-screen refusal renderer draws, and that renderer's output must be
 *    flushed before returning. This prevents the prior action menu's
 *    "Emulate" row from remaining underneath the refusal text.
 * 2. The normal (non-refused) activation screen must use the SAME shared
 *    emulation presentation every other card family uses
 *    (m1_read_icon_draw_emulate(): tag + animated RF waves + "EMULATING...")
 *    -- not a Detect-Reader-style diagnostic ("Waiting for reader" /
 *    "Reader active" / "Auth requested", drawn boxes/circles). Raw listener
 *    state belongs in serial logs only, never on this screen.
 *
 * MIFARE Classic emulation now has exactly one route -- the dedicated MFC
 * Emulate view (nfc_mfc_emu_gui_update(), s_mfc_dedicated_refused) -- not
 * the generic Emulate view this test originally guarded
 * (nfc_emulate_gui_update(), s_mfc_emu_refused), which no longer has any
 * MIFARE-Classic-specific code path at all: Classic never reaches it.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_mfc_emu_ui_test.c \
 *      -o /tmp/nfc_mfc_emu_ui_test && /tmp/nfc_mfc_emu_ui_test
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

/* strstr() bounded to [hay, hay+hay_len) -- so an "absent" check on one
 * function's body can't accidentally match content in a LATER function. */
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
    if (src != NULL) {
        const char *helper  = strstr(src, "static void nfc_mfc_draw_refusal");
        /* "...)\r\n{" anchors to the DEFINITION, not the earlier forward
         * declaration ("...);") -- both share the same leading text.
         * m1_nfc.c uses CRLF line endings throughout. */
        const char *update  = strstr(src, "static void nfc_mfc_emu_gui_update(uint8_t param)\r\n{");
        const char *msg_fn  = strstr(src, "static int nfc_mfc_emu_gui_message(void)\r\n{");
        size_t      update_len = (update != NULL && msg_fn != NULL && msg_fn > update)
                                  ? (size_t)(msg_fn - update) : 0U;
        CHECK(helper != NULL, "dedicated MFC refusal renderer exists");
        CHECK(helper != NULL && strstr(helper, "\"Cannot Emulate\"") != NULL,
              "MFC refusal renderer has an unambiguous title");
        CHECK(helper != NULL && strstr(helper, "M1_MFC_EMU_INCOMPLETE_BLOCKS") != NULL &&
              strstr(helper, "M1_MFC_EMU_INCOMPLETE_KEYS") != NULL,
              "incomplete blocks and keys retain the production refusal gate");
        CHECK(update != NULL, "the dedicated MFC Emulate view's update function exists");
        if (update != NULL) {
            /* nfc_mfc_emu_gui_update() starts a fresh page UNCONDITIONALLY,
             * before checking refusal state -- stronger than the old
             * pattern (fresh page only inside the refusal branch), since
             * it also covers the normal/non-refused activation screen. */
            const char *fresh  = strstr(update, "u8g2_FirstPage(&m1_u8g2);");
            const char *branch = strstr(update, "if (s_mfc_dedicated_refused != M1_MFC_EMU_OK)");
            CHECK(fresh != NULL && branch != NULL && fresh < branch,
                  "a fresh page is started before the refusal check, not merely inside it");
            if (branch != NULL) {
                const char *draw  = strstr(branch, "nfc_mfc_draw_refusal(s_mfc_dedicated_refused);");
                const char *flush = strstr(branch, "m1_u8g2_nextpage();");
                const char *done  = strstr(branch, "return;");
                CHECK(draw != NULL && flush != NULL && done != NULL &&
                      draw < flush && flush < done,
                      "MFC refusal branch: full-screen content -> flush -> return, in order");
            }
        }
        CHECK(update_len > 0U, "nfc_mfc_emu_gui_update()'s body could be bounded (next function found)");
        if (update_len > 0U) {
            CHECK(bounded_contains(update, update_len, "m1_read_icon_draw_emulate(&m1_u8g2, nfc_emu_frame);"),
                  "the normal activation screen uses the shared card/RF-wave EMULATING presentation");
            CHECK(!bounded_contains(update, update_len, "\"Waiting for reader\""),
                  "Detect-Reader-style \"Waiting for reader\" text is gone");
            CHECK(!bounded_contains(update, update_len, "\"Reader active\""),
                  "Detect-Reader-style \"Reader active\" text is gone");
            CHECK(!bounded_contains(update, update_len, "\"Auth requested\""),
                  "Detect-Reader-style \"Auth requested\" text is gone");
            /* The trailing ";" distinguishes an actual call/statement from
             * this file's own explanatory comment mentioning the function
             * by name (which does not end the reference with ");" at that
             * exact position). */
            CHECK(!bounded_contains(update, update_len, "m1_mfc_raw_state();"),
                  "raw listener state is not READ by the draw function at all -- "
                  "it belongs in serial logs only, never on this screen");
        }
        CHECK(strstr(src, "if (s_mfc_emu_refused != M1_MFC_EMU_OK)") == NULL,
              "the generic Emulate view's own MFC refusal check is gone -- "
              "Classic never reaches nfc_emulate_gui_update() at all now");

        /* Permanent routing/lifecycle gate. These checks read the production
         * source rather than mirroring its intended decisions in test code. */
        CHECK(strstr(src, "static bool nfc_context_is_mfc") != NULL,
              "one Classic-evidence predicate owns all emulation routing");
        CHECK(strstr(src, "view_id = nfc_context_is_mfc(c)") != NULL,
              "the action dispatcher routes Classic evidence through the dedicated view");
        CHECK(strstr(src, "if (nfc_context_is_mfc(cc))") != NULL,
              "the generic emulator independently refuses Classic evidence before RF");
        CHECK(strstr(src, "s_mfc_dedicated_refused = mfc_emu_start()") != NULL,
              "the dedicated view uses the sole start coordinator");
        CHECK(strstr(src, "return M1_MFC_EMU_START_FAILED;") != NULL,
              "a missing STARTED acknowledgement is a refusal, never UI success");
        CHECK(strstr(src, "if (m1_mfc_raw_hw_active())") != NULL,
              "teardown follows authoritative RAWOWN state rather than UI state");
        CHECK(strstr(src, "s_mfc_dedicated_started") == NULL,
              "the stale duplicate UI lifecycle flag cannot return");
        free(src);
    }

    char *ctx_h = read_all("NFC/NFC_drv/common/nfc_ctx.h");
    char *ctx_c = read_all("NFC/NFC_drv/common/nfc_ctx.c");
    CHECK(ctx_h != NULL && strstr(ctx_h, "#define M1NFC_FAM_UNKNOWN") != NULL,
          "unknown NFC-A has a distinct family sentinel");
    CHECK(ctx_c != NULL && strstr(ctx_c, "return M1NFC_FAM_UNKNOWN;") != NULL,
          "unrecognised NFC-A is not silently classified as Classic");
    free(ctx_h);
    free(ctx_c);

    printf("nfc_mfc_emu_ui_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
