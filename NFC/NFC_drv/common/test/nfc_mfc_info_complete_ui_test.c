/* Source-seam regression test for the completed-MFC-read Info screen in
 * m1_csrc/m1_nfc.c's nfc_info_drawing().
 *
 * The firmware UI cannot be host-linked without the display/FreeRTOS stack,
 * so this test checks the narrow production seam that changed: for
 * mc->outcome == MFC_OUTCOME_COMPLETE, the MFC branch must render the actual
 * detected type, the real stored UID, ATQA/SAK gated on has_atqa/has_sak
 * (the same shared fields/helper the generic ISO14443A branch uses), and
 * keep the Data action -- while every OTHER outcome (partial/failed/
 * cancelled) must still render through the untouched, pre-existing layout
 * (this increment does not define a partial-result UI).
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_mfc_info_complete_ui_test.c \
 *      -o /tmp/nfc_mfc_info_complete_ui_test && /tmp/nfc_mfc_info_complete_ui_test
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

/* strstr() bounded to [hay, hay+hay_len). */
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
        /* Bound the whole MFC Info branch: from its family/outcome-agnostic
         * entry guard to the shared generic tech-A ATQA/SAK block further
         * down the same function (nfc_info_drawing() has no other MFC-
         * specific content after this point). */
        const char *branch_start = strstr(src,
            "if (c && c->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)");
        const char *branch_end = branch_start ? strstr(branch_start, "u8g2_DrawStr(&m1_u8g2, 2, 32, \"UID:\");") : NULL;
        size_t branch_len = (branch_start != NULL && branch_end != NULL && branch_end > branch_start)
                             ? (size_t)(branch_end - branch_start) : 0U;
        CHECK(branch_start != NULL, "the MFC Info branch exists");
        CHECK(branch_len > 0U, "the MFC Info branch's extent could be bounded (shared generic block found after it)");

        if (branch_len > 0U) {
            const char *complete_start = strstr(branch_start, "if (mc->outcome == MFC_OUTCOME_COMPLETE)");
            CHECK(complete_start != NULL && (size_t)(complete_start - branch_start) < branch_len,
                  "a COMPLETE-outcome gate exists inside the MFC Info branch");

            size_t complete_len = (complete_start != NULL)
                                   ? branch_len - (size_t)(complete_start - branch_start) : 0U;

            if (complete_len > 0U) {
                CHECK(bounded_contains(complete_start, complete_len,
                          "\"MIFARE Classic %s\","),
                      "the COMPLETE title uses the actual detected type, not a hard-coded \"1K\"");
                CHECK(bounded_contains(complete_start, complete_len,
                          "(mc->type == M1NFC_MFCTYPE_4K) ? \"4K\" : \"1K\""),
                      "the type string is chosen from mc->type, covering both 1K and 4K");

                CHECK(bounded_contains(complete_start, complete_len, "\"UID:\""),
                      "the real stored UID is labelled");
                CHECK(bounded_contains(complete_start, complete_len,
                          "nfc_draw_hex_cells(26, 21, c->head.uid, c->head.uid_len);"),
                      "the real stored UID bytes are drawn via the shared hex-cell helper");

                CHECK(bounded_contains(complete_start, complete_len, "\"ATQA:\""),
                      "ATQA is labelled");
                CHECK(bounded_contains(complete_start, complete_len, "if (c->head.a.has_atqa)"),
                      "ATQA is only drawn when has_atqa is true");
                CHECK(bounded_contains(complete_start, complete_len,
                          "nfc_draw_hex_cells(32, 32, c->head.a.atqa, 2);"),
                      "ATQA bytes are drawn via the shared hex-cell helper, preserving byte order");

                CHECK(bounded_contains(complete_start, complete_len, "\"SAK:\""),
                      "SAK is labelled");
                CHECK(bounded_contains(complete_start, complete_len, "if (c->head.a.has_sak)"),
                      "SAK is only drawn when has_sak is true");
                CHECK(bounded_contains(complete_start, complete_len,
                          "nfc_draw_hex_cells(98, 32, &c->head.a.sak, 1);"),
                      "SAK byte is drawn via the shared hex-cell helper");

                CHECK(bounded_contains(complete_start, complete_len,
                          "\"Keys: %u/%u  Sec: %u/%u\""),
                      "keys and sectors are shown on one combined summary line");

                CHECK(bounded_contains(complete_start, complete_len, "\"Data\""),
                      "the Data action label is present");
                CHECK(bounded_contains(complete_start, complete_len, "arrowright_8x8"),
                      "the Data action arrow icon is present");
            }

            /* Every other outcome renders through the pre-existing layout,
             * except: the title line -- NFC2-005 (Info identity consistency,
             * task section H) intentionally replaced the old hardcoded
             * "Type: MIFARE Classic 1K" with the same mc->type-based title
             * COMPLETE already used, so a 4K partial card is no longer
             * mislabeled 1K; and the keys/sectors lines' y-position -- a
             * visual-render gate finding showed the original y=43/y=54
             * placement overlapping the unchanged "Data" label at y=61 once
             * ATQA/SAK pushed them down, tightened to y=42/y=51 (kept as
             * two lines, NOT combined onto COMPLETE's own single-line
             * format, since that format itself clips past x=128 for
             * realistic two-digit values). */
            CHECK(bounded_contains(branch_start, branch_len,
                      "(mc->type == M1NFC_MFCTYPE_4K) ? \"4K\" : \"1K\""),
                  "the non-COMPLETE title now uses the real detected type, matching COMPLETE (NFC2-005)");
            CHECK(bounded_contains(branch_start, branch_len, "\"Keys found: %u/%u\""),
                  "the pre-existing non-COMPLETE keys-found line is preserved verbatim");
            CHECK(bounded_contains(branch_start, branch_len, "\"Sectors read: %u/%u\""),
                  "the pre-existing non-COMPLETE sectors-read line is preserved verbatim");
        }

        free(src);
    }

    printf("nfc_mfc_info_complete_ui_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
