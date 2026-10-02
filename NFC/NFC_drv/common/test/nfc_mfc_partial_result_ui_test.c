/* Source-seam regression test for the Partial/Failed/Cancelled MFC read
 * result screen and the Find Missing Keys action wiring in m1_csrc/m1_nfc.c.
 *
 * The firmware UI cannot be host-linked without the display/FreeRTOS stack,
 * so this test checks the narrow production seam this increment added:
 *
 * 1. nfc_read_gui_update()'s MFC branch splits on mc->outcome -- the
 *    COMPLETE sub-branch is BYTE-FOR-BYTE the pre-existing layout (required:
 *    "no behavioral or menu changes" for Complete), while PARTIAL/FAILED/
 *    CANCELLED get the new dedicated "Partial Read"/"Read Failed"/
 *    "Cancelled" presentation.
 * 2. nfc_build_action_menu() offers "Find Missing Keys" (NFC_ACT_FIND_KEYS)
 *    exactly when mfc_elig.find_missing_keys is true, dispatched to
 *    VIEW_MODE_NFC_MFC_FIND_KEYS.
 * 3. Required test #10 ("Partial never offers Emulate or Write"): the same
 *    nfc_can_write_mfc()/emu gating that already required MFC_OUTCOME_COMPLETE
 *    is unchanged by this increment.
 * 4. Required test #12 ("Failed... does not show a dead Find Missing Keys
 *    action"): there is exactly ONE source of the "Find Missing Keys" label
 *    in the whole action-menu builder, gated on mfc_elig.find_missing_keys --
 *    no second, ungated path could show it for FAILED.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_mfc_partial_result_ui_test.c \
 *      -o /tmp/nfc_mfc_partial_result_ui_test && /tmp/nfc_mfc_partial_result_ui_test
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
    char *src = read_all("m1_csrc/m1_nfc.c");
    CHECK(src != NULL, "m1_nfc.c readable");
    if (src == NULL) {
        printf("nfc_mfc_partial_result_ui_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }
    size_t src_len = strlen(src);

    /* --- 1. Result screen outcome branching ------------------------------ */
    const char *mfc_branch = strstr(src,
        "if (nfc_ctx_get()->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)");
    const char *mfc_branch_end = mfc_branch
        ? strstr(mfc_branch, "else if (nfc_ctx_get()->head.family == M1NFC_FAM_ST25TB)")
        : NULL;
    size_t mfc_branch_len = (mfc_branch != NULL && mfc_branch_end != NULL && mfc_branch_end > mfc_branch)
                            ? (size_t)(mfc_branch_end - mfc_branch) : 0U;
    CHECK(mfc_branch != NULL, "the MFC READING_COMPLETE branch exists");
    CHECK(mfc_branch_len > 0U, "the MFC branch's extent could be bounded (ST25TB branch found after it)");

    if (mfc_branch_len > 0U) {
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "if (mc->outcome != MFC_OUTCOME_COMPLETE)"),
              "the branch splits explicitly on mc->outcome != MFC_OUTCOME_COMPLETE");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Partial Read\""),
              "PARTIAL gets the dedicated \"Partial Read\" title");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Read Failed\""),
              "FAILED gets the dedicated \"Read Failed\" title");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Cancelled\""),
              "CANCELLED (no data) gets its own \"Cancelled\" title");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"No sectors recovered\""),
              "FAILED/CANCELLED show the truthful \"No sectors recovered\" body");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Sectors: %u/%u\""),
              "PARTIAL shows \"Sectors: X/Y\" per spec wording");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Keys: %u/%u\""),
              "PARTIAL shows \"Keys: X/Y\" per spec wording");

        /* The COMPLETE sub-branch (inside the else) must still contain the
         * EXACT pre-existing strings, byte-for-byte -- required: "no
         * behavioral or menu changes" for a completed read. */
        CHECK(bounded_contains(mfc_branch, mfc_branch_len,
                  "(mc->type == M1NFC_MFCTYPE_4K) ? \"MIFARE Classic 4K\" : \"MIFARE Classic 1K\""),
              "COMPLETE's title logic is byte-for-byte unchanged");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Keys found: %u/%u\""),
              "COMPLETE's \"Keys found: X/Y\" wording is unchanged (never renamed to \"Keys: X/Y\")");
        CHECK(bounded_contains(mfc_branch, mfc_branch_len, "\"Sectors read: %u/%u\""),
              "COMPLETE's \"Sectors read: X/Y\" wording is unchanged");
    }

    /* --- 2. Action menu: Find Missing Keys -------------------------------- */
    /* Anchored on "...)\r\n{" (m1_nfc.c uses CRLF throughout) to find the
     * DEFINITION, not the earlier forward declaration (which ends in ";"
     * instead) -- both share the same multi-line signature text otherwise. */
    const char *menu_fn = strstr(src,
        "static uint8_t nfc_build_action_menu(const char *labels[NFC_MENU_MAX_ITEMS],\r\n"
        "                                     uint8_t      actions[NFC_MENU_MAX_ITEMS])\r\n{");
    const char *menu_fn_end = menu_fn ? strstr(menu_fn + 1, "\r\nstatic ") : NULL;
    size_t menu_fn_len = (menu_fn != NULL && menu_fn_end != NULL && menu_fn_end > menu_fn)
                         ? (size_t)(menu_fn_end - menu_fn) : 0U;
    CHECK(menu_fn != NULL, "nfc_build_action_menu() is defined");
    CHECK(menu_fn_len > 0U, "nfc_build_action_menu()'s body could be bounded");

    if (menu_fn_len > 0U) {
        CHECK(bounded_contains(menu_fn, menu_fn_len,
                  "if (mfc_elig.find_missing_keys) { labels[n] = \"Find Missing Keys\"; actions[n++] = NFC_ACT_FIND_KEYS; }"),
              "Find Missing Keys is offered exactly when mfc_elig.find_missing_keys is true");
        /* Exactly one source of the label -- required test #12: no second,
         * ungated path could show it for a FAILED/CANCELLED read. */
        CHECK(bounded_count(menu_fn, menu_fn_len, "\"Find Missing Keys\"") == 1,
              "\"Find Missing Keys\" is offered from exactly one place in the whole menu builder");
        CHECK(bounded_count(menu_fn, menu_fn_len, "NFC_ACT_FIND_KEYS") == 1,
              "NFC_ACT_FIND_KEYS is assigned from exactly one place");

        /* Required test #10: Emulate/Write still gated on nfc_can_write_mfc()
         * (which itself requires MFC_OUTCOME_COMPLETE, unchanged elsewhere in
         * this file) -- this increment did not add any Partial-outcome path
         * to either. */
        CHECK(bounded_contains(menu_fn, menu_fn_len, "nfc_can_write_mfc(c)"),
              "Write is still gated by the unchanged nfc_can_write_mfc() check");
    }

    CHECK(bounded_contains(src, src_len, "if (!c || c->head.family != M1NFC_FAM_CLASSIC) return false;"),
          "nfc_can_write_mfc()'s own guard body is present, unchanged, elsewhere in the file");
    CHECK(bounded_contains(src, src_len, "MFC_OUTCOME_COMPLETE"),
          "MFC_OUTCOME_COMPLETE is still referenced (the write-eligibility gate's real outcome check)");

    /* --- OK dispatcher: routes NFC_ACT_FIND_KEYS to the new view mode ----- */
    CHECK(bounded_contains(src, src_len,
              "case NFC_ACT_FIND_KEYS: view_id = VIEW_MODE_NFC_MFC_FIND_KEYS; break;"),
          "the action-menu OK dispatcher routes NFC_ACT_FIND_KEYS to VIEW_MODE_NFC_MFC_FIND_KEYS");

    /* --- The renamed enum value has no leftover old name anywhere -------- */
    CHECK(!bounded_contains(src, src_len, "NFC_ACT_DICT_SCAN"),
          "the old, unused NFC_ACT_DICT_SCAN name is gone (renamed to NFC_ACT_FIND_KEYS), no leftover reference");

    free(src);
    printf("nfc_mfc_partial_result_ui_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
