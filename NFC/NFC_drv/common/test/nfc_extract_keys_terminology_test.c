/* Host tests for the "Extract Keys" terminology checkpoint: renames the
 * user-visible "Detect Reader" menu label and on-screen header to
 * "Extract Keys", and gives its capture-progress text truthful,
 * state-matched wording ("Waiting for reader...", "Authentication data
 * captured"). Source-only checks (source_contains()-style, against the
 * real committed m1_csrc/m1_menu.c and m1_csrc/m1_nfc.c) plus symbol-
 * stability checks proving no internal function/enum/scene name changed.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      nfc_extract_keys_terminology_test.c -o /tmp/exkeys && /tmp/exkeys
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}
static bool contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static void test_menu_label_renamed(void)
{
    char *menu = slurp(REPO_ROOT "/m1_csrc/m1_menu.c");
    CHECK(menu != NULL, "m1_menu.c readable");

    CHECK(contains(menu, "\"Extract Keys\", nfc_detect_reader,"),
          "m1_menu.c: top-level menu label reads 'Extract Keys' and still dispatches to nfc_detect_reader() unchanged");
    CHECK(!contains(menu, "\"Detect Reader\""),
          "m1_menu.c: no quoted 'Detect Reader' menu label remains");
    CHECK(contains(menu, "menu_NFC_Detect_Reader"),
          "m1_menu.c: the menu struct's own C symbol name is unchanged (internal symbols not renamed)");
    CHECK(!contains(menu, "\"MFC Recovery\", nfc_harvest,"),
          "m1_menu.c: the obsolete 'MFC Recovery' Tools item has been removed from the production menu");

    free(menu);
}

static void test_extract_keys_screen_text(void)
{
    char *m1nfc = slurp(REPO_ROOT "/m1_csrc/m1_nfc.c");
    CHECK(m1nfc != NULL, "m1_nfc.c readable");

    CHECK(contains(m1nfc, "u8g2_DrawStr(&m1_u8g2, 2, 10, \"Extract Keys\");"),
          "m1_nfc.c: on-screen header now draws 'Extract Keys'");
    CHECK(!contains(m1nfc, "u8g2_DrawStr(&m1_u8g2, 2, 10, \"Detect Reader\");"),
          "m1_nfc.c: the old 'Detect Reader' header DrawStr call is gone");

    /* Active state. (Note: the near-identical phrase "Waiting for reader"
     * without an ellipsis legitimately still exists elsewhere in this file,
     * for the unrelated MFC Emulate screen's own M1_MFC_RAW_* state machine
     * -- that screen is out of scope for this checkpoint and must not be
     * touched, so this suite checks the new text is present rather than
     * asserting a whole-file absence of the old, differently-scoped one.) */
    CHECK(contains(m1nfc, "\"Waiting for reader...\""),
          "m1_nfc.c: idle/waiting state text is 'Waiting for reader...'");
    CHECK(contains(m1nfc, "if (st == MFC_DR_READER_ACTIVE) stage = \"Reader traffic\";"),
          "m1_nfc.c: the Extract Keys screen's own else-branch still assigns from the real MFC_DR_* state, not fabricated");

    /* Successful capture state -- "Auth detected" was unique to this screen
     * (the MFC Emulate screen never used this exact phrase), so a whole-
     * file absence check is safe and meaningful here. */
    CHECK(contains(m1nfc, "\"Authentication data captured\""),
          "m1_nfc.c: successful-capture text is 'Authentication data captured'");
    CHECK(!contains(m1nfc, "\"Auth detected\""),
          "m1_nfc.c: the old terse 'Auth detected' text is gone");

    /* Width-fit guard for the 128px display, matching the codebase's own
     * established u8g2_GetStrWidth() convention for long strings. */
    CHECK(contains(m1nfc, "u8g2_GetStrWidth(&m1_u8g2, captured) > 124"),
          "m1_nfc.c: the successful-capture string is measured against the display width before being drawn");
    CHECK(contains(m1nfc, "u8g2_DrawStr(&m1_u8g2, 2, 26, \"Authentication data\");") &&
          contains(m1nfc, "u8g2_DrawStr(&m1_u8g2, 2, 35, \"captured\");"),
          "m1_nfc.c: a two-line fallback exists so the successful-capture text always fits 128x64");

    /* "Keys recovered" must never actually be DRAWN by this view: it only
     * captures auth data, it never derives/validates keys itself. Checked
     * as a DrawStr call signature (quoted string immediately closing a
     * paren) so this doesn't false-positive on the explanatory comment
     * above that names the phrase while explaining why it's avoided. */
    CHECK(!contains(m1nfc, "\"Keys recovered\")"),
          "m1_nfc.c: 'Keys recovered' is never passed to a draw call -- this view never truthfully reaches that state");

    free(m1nfc);
}

static void test_internal_symbols_unchanged(void)
{
    char *m1nfc = slurp(REPO_ROOT "/m1_csrc/m1_nfc.c");
    CHECK(m1nfc != NULL, "m1_nfc.c readable");

    static const char *symbols[] = {
        "nfc_detect_reader_kp_handler",
        "nfc_detect_reader_gui_create",
        "nfc_detect_reader_gui_destroy",
        "nfc_detect_reader_gui_update",
        "nfc_detect_reader_gui_message",
        "nfc_detect_reader_gui_init",
        "void nfc_detect_reader(void)",
        "VIEW_MODE_NFC_DETECT_READER",
    };
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        CHECK(contains(m1nfc, symbols[i]), symbols[i]);
    }
    free(m1nfc);

    char *detect_h = slurp(REPO_ROOT "/NFC/NFC_drv/legacy/mfc_detect.h");
    CHECK(detect_h != NULL, "mfc_detect.h readable");
    CHECK(contains(detect_h, "mfc_dr_state_t") &&
          contains(detect_h, "MFC_DR_IDLE") &&
          contains(detect_h, "MFC_DR_WAIT_READER") &&
          contains(detect_h, "MFC_DR_READER_ACTIVE") &&
          contains(detect_h, "MFC_DR_AUTH_ACTIVE") &&
          contains(detect_h, "MFC_DR_DONE"),
          "mfc_detect.h: the full state-machine enum is unchanged -- no scene/state renamed");
    free(detect_h);
}

static void test_no_rf_or_recovery_behavior_touched(void)
{
    /* This is a text-only checkpoint: the RF listener start/stop calls and
     * the state-machine transition logic in the gui_create/gui_destroy
     * handlers must be byte-identical to before -- only string literals
     * inside gui_update() changed. */
    char *m1nfc = slurp(REPO_ROOT "/m1_csrc/m1_nfc.c");
    CHECK(m1nfc != NULL, "m1_nfc.c readable");

    CHECK(contains(m1nfc, "m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_START_DETECT_READER);"),
          "m1_nfc.c: the RF-start event dispatch is unchanged");
    CHECK(contains(m1nfc, "ListenerRequestStop();") && contains(m1nfc, "mfc_detect_end();"),
          "m1_nfc.c: the RF-stop / capture-store teardown calls are unchanged");
    CHECK(contains(m1nfc, "mfc_dr_state_t st = mfc_detect_state();"),
          "m1_nfc.c: state is still read from the real backend, not fabricated");

    free(m1nfc);
}

int main(void)
{
    test_menu_label_renamed();
    test_extract_keys_screen_text();
    test_internal_symbols_unchanged();
    test_no_rf_or_recovery_behavior_touched();

    printf("nfc_extract_keys_terminology_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
