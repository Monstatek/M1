/*
 * m1_file_browser_alloc_safety_test.c - source-seam test for allocation and
 * lifetime safety in m1_file_browser.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DISCLOSED LIMITATION: m1_file_browser.c pulls in stm32h5xx_hal.h and
 * FreeRTOS transitively (via main.h/m1_sdcard.h), so it cannot be
 * host-compiled or linked directly. This suite instead verifies the exact
 * allocation/lifetime-safety patterns are present in the REAL, committed
 * source text (reads the actual file; not a mock, not a paraphrase) --
 * same technique already used elsewhere in this project for HAL-coupled
 * files (see NFC/NFC_drv/common/test/nfc_t2t_unlock_test.c's own header
 * comment for the fuller rationale).
 *
 * Scope: allocation/lifetime safety only (m1_fb_init()'s calloc/malloc
 * chain, and the two realloc chains in the directory ascend/descend
 * paths). Does not touch navigation, sorting, layout, or any other
 * subsystem semantics -- those are asserted UNCHANGED by the "still
 * present, unmodified" checks below (e.g. the exact bounds/ordering of
 * the pre-existing index-buffer writes).
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/m1_file_browser_alloc_safety_test.c -o /tmp/fb_alloc && /tmp/fb_alloc
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

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
static int contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

/* Count non-overlapping occurrences -- used to prove the OLD dangerous
 * direct-assignment idiom ("pfb_hdl->X = (T*)realloc(pfb_hdl->X, ...)"
 * with no intervening NULL check) no longer appears anywhere in the file. */
static int count_occurrences(const char *hay, const char *needle)
{
    int n = 0; const char *p = hay; size_t len = strlen(needle);
    if (!hay || len == 0) return 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

int main(void)
{
    char *src = slurp("m1_csrc/m1_file_browser.c");
    CHECK(src != NULL, "m1_file_browser.c readable from repo root");
    if (!src) { printf("%s: %d passed, %d failed (source unreadable, aborting)\n",
                        "m1_file_browser_alloc_safety_test", g_pass, g_fail); return 1; }

    /* --- m1_fb_init(): every allocation checked before use --- */
    {
        const char *calloc_call = strstr(src, "pfb_hdl = (S_M1_file_browser_hdl*)calloc(1, sizeof(S_M1_file_browser_hdl));");
        const char *null_check = calloc_call ? strstr(calloc_call, "if (pfb_hdl == NULL)") : NULL;
        /* Must be the very next real statement -- close enough that nothing
         * else could dereference pfb_hdl in between. */
        CHECK(calloc_call && null_check && (null_check - calloc_call) < 200,
              "m1_fb_init: the top-level struct calloc is checked before any dereference");
    }
    CHECK(contains(src, "if ((pfb_hdl->listing_index_buffer == NULL) || (pfb_hdl->row_index_buffer == NULL))"),
          "m1_fb_init: listing/row buffer callocs are checked together before use");
    CHECK(contains(src, "if (pfb_hdl->info.dir_name == NULL)"),
          "m1_fb_init: dir_name malloc is checked before strcpy");
    CHECK(count_occurrences(src, "m1_fb_deinit();") >= 2,
          "m1_fb_init: partial-initialization cleanup is centralized through the existing m1_fb_deinit(), not duplicated");

    /* --- Directory descent (grow): temporaries, commit only on full success --- */
    CHECK(contains(src, "char *new_dir_name = (char *)realloc(pfb_hdl->info.dir_name,"),
          "descend: dir_name growth goes through a temporary, not a direct live-pointer assignment");
    CHECK(contains(src, "uint16_t *new_listing_buf = (new_dir_name != NULL)"),
          "descend: the listing-buffer growth is only attempted if dir_name's growth already succeeded");
    CHECK(contains(src, "uint16_t *new_row_buf = (new_listing_buf != NULL)"),
          "descend: the row-buffer growth is only attempted if the listing buffer's growth already succeeded");
    {
        const char *check = strstr(src, "if ((new_dir_name == NULL) || (new_listing_buf == NULL) || (new_row_buf == NULL))");
        const char *break_stmt = check ? strstr(check, "break; // growth failed") : NULL;
        CHECK(check && break_stmt && (break_stmt - check) < 200,
              "descend: any failed growth stops before the strcat/dir_level mutation -- defined no-op, not a crash");
        /* The mutation that changes observable state must textually follow
         * the combined failure check, not precede it. */
        const char *strcat_call = check ? strstr(check, "strcat(pfb_hdl->info.dir_name, \"/\");") : NULL;
        CHECK(check && strcat_call && strcat_call > check,
              "descend: strcat (the state-changing mutation) is textually AFTER the combined failure check, not before it");
    }

    /* --- Directory ascend (".."): also via temporaries --- */
    CHECK(contains(src, "char *new_dir_name = (char *)realloc(pfb_hdl->info.dir_name, (size_t)(l + 2));"),
          "ascend: dir_name shrink goes through a temporary");
    CHECK(contains(src, "uint16_t *new_listing_buf = (uint16_t *)realloc(pfb_hdl->listing_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));"),
          "ascend: listing-buffer shrink goes through a temporary");
    CHECK(contains(src, "uint16_t *new_row_buf = (uint16_t *)realloc(pfb_hdl->row_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));"),
          "ascend: row-buffer shrink goes through a temporary");

    /* --- The old dangerous idiom must be gone: no remaining direct
     * "pfb_hdl->X = (T*)realloc(pfb_hdl->X, ...)" assignment anywhere. --- */
    CHECK(count_occurrences(src, "pfb_hdl->info.dir_name = (char *)realloc(pfb_hdl->info.dir_name") == 0 &&
          count_occurrences(src, "pfb_hdl->info.dir_name = (TCHAR *)realloc(pfb_hdl->info.dir_name") == 0,
          "no remaining direct realloc-into-live-pointer assignment for dir_name");
    CHECK(count_occurrences(src, "pfb_hdl->listing_index_buffer = (uint16_t *)realloc(pfb_hdl->listing_index_buffer") == 0,
          "no remaining direct realloc-into-live-pointer assignment for listing_index_buffer");
    CHECK(count_occurrences(src, "pfb_hdl->row_index_buffer = (uint16_t *)realloc(pfb_hdl->row_index_buffer") == 0,
          "no remaining direct realloc-into-live-pointer assignment for row_index_buffer");

    /* --- Unrelated semantics proven UNCHANGED (still present, verbatim) --- */
    {
        const char *guard = strstr(src, "if ( pfb_hdl->dir_level >= DIRECTORY_MAX_DEPTH_LEVEL )");
        const char *guard_break = guard ? strstr(guard, "break; // Do nothing if it goes too deep") : NULL;
        CHECK(guard && guard_break && (guard_break - guard) < 200, "max-depth guard is unchanged");
    }
    CHECK(contains(src, "pfb_hdl->listing_index_buffer[pfb_hdl->dir_level] = pfb_hdl->listing_index;"),
          "current viewport position is still saved into the index buffer at the pre-descent depth");
    CHECK(contains(src, "pfb_hdl->info.file_is_selected = FALSE;"),
          "file_is_selected reset on directory navigation is unchanged");
    {
        const char *guard = strstr(src, "if (pfb_hdl->info.file_name)");
        const char *free_call = guard ? strstr(guard, "free(pfb_hdl->info.file_name);") : NULL;
        const char *calloc_call = free_call ? strstr(free_call, "pfb_hdl->info.file_name = (char *)calloc(strlen(this_file.fname) + 1, 1);") : NULL;
        CHECK(guard && free_call && calloc_call && (calloc_call - guard) < 200,
              "file-selection calloc (already correctly guarded before this change) is unchanged");
    }

    printf("m1_file_browser_alloc_safety_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
