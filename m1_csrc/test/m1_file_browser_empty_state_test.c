/*
 * m1_file_browser_empty_state_test.c - source-seam test for the empty-listing
 * safety guard, the empty-folder message, and the unified directory cache in
 * m1_file_browser.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DISCLOSED LIMITATION: m1_file_browser.c pulls in stm32h5xx_hal.h and
 * FreeRTOS transitively (via main.h/m1_sdcard.h), so it cannot be
 * host-compiled or linked directly. This suite instead verifies the exact
 * patterns are present in the REAL, committed source text (reads the actual
 * file; not a mock, not a paraphrase) -- same technique already used
 * elsewhere in this project (see m1_file_browser_alloc_safety_test.c).
 *
 * Scope:
 *  1. Pressing OK on an empty listing (num_of_files == 0) is a guarded no-op,
 *     positioned before the ".." gate and the stale-this_file fallthrough it
 *     used to reach -- this is the fix for the stale/phantom-selection defect
 *     found in the file-browser removal-scope audit.
 *  2. An empty listing draws an explicit "No files" message instead of a
 *     selection frame floating over nothing.
 *  3. The directory-entry cache (s_fb_sorted[]) is now built for every
 *     caller, not only the sort-enabled one, and the render loop reads from
 *     it via a cache-presence check rather than a sort-enabled check --
 *     eliminating the full per-redraw FatFs re-walk for unsorted callers
 *     (NFC/RFID/Sub-GHz/BLE saved) while leaving qsort() itself conditional.
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/m1_file_browser_empty_state_test.c -o /tmp/fb_empty && /tmp/fb_empty
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

static int count_occurrences(const char *hay, const char *needle)
{
    int n = 0; const char *p = hay; size_t len = strlen(needle);
    if (!hay || len == 0) return 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* Returns a malloc'd copy of [start, matching '}'] inclusive, given start
 * pointing at a function definition's own first character with its opening
 * brace somewhere ahead. Used only for fb_name_casecmp()/fb_friendly_dir_name()
 * below, neither of which contains a brace inside a string/char literal, so a
 * plain depth counter is exact here (no comment/literal skipping needed). */
static char *extract_braced(const char *start)
{
    const char *p = strchr(start, '{');
    const char *end;
    int depth;
    if (!p) return NULL;
    depth = 1;
    end = p + 1;
    while (*end && depth > 0)
    {
        if (*end == '{') depth++;
        else if (*end == '}') depth--;
        end++;
    }
    if (depth != 0) return NULL;
    {
        size_t len = (size_t)(end - start);
        char *out = malloc(len + 1);
        if (out) { memcpy(out, start, len); out[len] = 0; }
        return out;
    }
}

int main(void)
{
    char *src = slurp("m1_csrc/m1_file_browser.c");
    CHECK(src != NULL, "m1_file_browser.c readable from repo root");
    if (!src) { printf("%s: %d passed, %d failed (source unreadable, aborting)\n",
                        "m1_file_browser_empty_state_test", g_pass, g_fail); return 1; }

    /* --- 1. Empty-listing OK-press guard, positioned before the ".." gate --- */
    {
        const char *ok_click = strstr(src, "else if ( button_status->event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )");
        const char *guard = ok_click ? strstr(ok_click, "if (!num_of_files)") : NULL;
        const char *dotdot_gate = ok_click ? strstr(ok_click, "\"..\" row (suppressed at generic-browser root)") : NULL;
        const char *stale_fallthrough = ok_click ? strstr(ok_click, "if (this_file.fattrib & AM_DIR)") : NULL;
        CHECK(ok_click && guard, "OK-click handler has an explicit num_of_files==0 guard");
        CHECK(guard && dotdot_gate && guard < dotdot_gate,
              "empty-listing guard runs BEFORE the \"..\" suppression gate");
        CHECK(guard && stale_fallthrough && guard < stale_fallthrough,
              "empty-listing guard runs BEFORE the stale this_file/AM_DIR fallthrough it used to reach unguarded");
    }

    /* --- 2. Empty-folder message, frame/scrollbar guarded on the same condition --- */
    {
        const char *empty_branch = strstr(src, "if (num_of_files == 0)");
        const char *no_files = empty_branch ? strstr(empty_branch, "\"No files\"") : NULL;
        const char *frame_draw = strstr(src, "u8g2_DrawFrame(plcd_hdl, pfb_hdl->x, pfb_hdl->y + pfb_hdl->row_index");
        CHECK(empty_branch != NULL, "render path branches explicitly on num_of_files == 0");
        CHECK(no_files && empty_branch && no_files > empty_branch && (no_files - empty_branch) < 400,
              "empty listing draws an explicit \"No files\" message");
        CHECK(frame_draw && empty_branch && frame_draw > no_files,
              "the selection-frame draw is reached only outside the empty-listing branch (textually after it, under the else)");
    }

    /* --- 3. Cache built for every caller, sized to the actual entry count
     * (not a fixed FILE_BROWSER_MAX_FILES allocation every session) --- */
    {
        const char *reenum_comment = strstr(src, "Re-enumerate; drop any stale cached array first.");
        const char *free_call = reenum_comment ? strstr(reenum_comment, "fb_sorted_free();") : NULL;
        const char *old_fixed_alloc = strstr(src, "calloc(FILE_BROWSER_MAX_FILES, sizeof(fb_sorted_entry_t))");
        const char *entry_count_decl = free_call ? strstr(free_call, "uint16_t entry_count = 0;") : NULL;
        const char *enum_error_decl = entry_count_decl ? strstr(entry_count_decl, "bool enum_error = false;") : NULL;
        const char *counting_loop = enum_error_decl ? strstr(enum_error_decl, "while (entry_count < (FILE_BROWSER_MAX_FILES - 1))") : NULL;
        const char *rewind_for_pass2 = counting_loop ? strstr(counting_loop, "f_readdir(&directory, 0); // rewind for pass 2") : NULL;
        const char *sized_calloc = rewind_for_pass2 ? strstr(rewind_for_pass2, "s_fb_sorted = (fb_sorted_entry_t *)calloc(entry_count, sizeof(fb_sorted_entry_t));") : NULL;
        const char *pass2_loop = sized_calloc ? strstr(sized_calloc, "while (s_fb_sorted_count < entry_count)") : NULL;

        CHECK(old_fixed_alloc == NULL,
              "the old fixed-size calloc(FILE_BROWSER_MAX_FILES, ...) allocation is fully gone");
        CHECK(entry_count_decl && free_call && (entry_count_decl - free_call) < 500,
              "a pass-1 entry-counting pass runs right after dropping the stale cache");
        CHECK(enum_error_decl != NULL, "pass 1 distinguishes a genuine enumeration error from clean end-of-directory");
        CHECK(counting_loop != NULL, "pass-1 counting is still bounded by the FILE_BROWSER_MAX_FILES-1 cap");
        CHECK(rewind_for_pass2 != NULL, "the directory is rewound between the counting pass and the population pass");
        CHECK(sized_calloc != NULL,
              "the cache is allocated sized to the actual counted entry_count, not a fixed worst-case size");
        CHECK(pass2_loop != NULL, "pass 2 fills exactly entry_count records, not up to a fixed cap");
    }
    {
        const char *qsort_call = strstr(src, "qsort(s_fb_sorted, s_fb_sorted_count, sizeof(fb_sorted_entry_t), fb_entry_cmp);");
        const char *guard = NULL;
        if (qsort_call)
        {
            const char *p = qsort_call;
            /* Walk backward to the nearest preceding "if (s_fb_sort_enabled)" on the line above. */
            while (p > src && *p != '\n') p--;
            while (p > src && *(p - 1) != '\n') p--;
            guard = (strstr(p, "if (s_fb_sort_enabled)") == p || strstr(p, "if (s_fb_sort_enabled)") < qsort_call) ? strstr(p, "if (s_fb_sort_enabled)") : NULL;
        }
        CHECK(qsort_call && guard && guard < qsort_call && (qsort_call - guard) < 60,
              "qsort() itself is still conditional on s_fb_sort_enabled -- unsorted callers keep raw FatFs order");
    }
    CHECK(contains(src, "bool cache_active   = (s_fb_sorted != NULL);"),
          "render loop has a cache-presence flag independent of sort-enabled state");
    CHECK(contains(src, "if (cache_active)"),
          "per-row rendering reads from the cache whenever it exists, not only when sorting is enabled");
    CHECK(count_occurrences(src, "sorting_active") == 0,
          "the old sort-gated 'sorting_active' flag name is fully retired (renamed to reflect its real, broader meaning)");
    /* --- Card-removal / enumeration-failure invalidation --- */
    {
        const char *opendir_fail = strstr(src, "if (res != FR_OK)\n\t\t\t{\n\t\t\t\tfb_sorted_free();");
        CHECK(opendir_fail != NULL || contains(src, "fb_sorted_free(); // Card/media gone: don't keep a stale listing for it"),
              "a failed f_opendir (e.g. card removed) frees the cache instead of leaving a stale listing allocated");
        CHECK(contains(src, "enum_error = true"),
              "a genuine f_readdir error mid-enumeration is tracked separately from clean end-of-directory");
        const char *enum_fail_status = strstr(src, "if (enum_error)");
        const char *enum_fail_return = enum_fail_status ? strstr(enum_fail_status, "pfb_hdl->info.status = FB_ERR_SDCARD;") : NULL;
        CHECK(enum_fail_status && enum_fail_return && (enum_fail_return - enum_fail_status) < 500,
              "an enumeration failure surfaces the same SD-error status a failed open would, instead of showing a false-empty or partial list");
    }

    /* --- Perf instrumentation exists and is purely additive --- */
    CHECK(contains(src, "s_fb_perf_scan_count++;"), "a directory (re)scan increments the scan counter exactly once per enumeration");
    CHECK(contains(src, "uint32_t m1_fb_get_perf_scan_count(void)"), "scan counter has a getter for m1_mtest telemetry");
    CHECK(contains(src, "uint32_t m1_fb_get_perf_file_open_count(void)"), "file-open counter has a getter for m1_mtest telemetry");
    CHECK(contains(src, "s_fb_perf_file_open_count++;"), "m1_fb_open_file() increments the shared file-open counter (covers every caller through one hook)");

    /* --- this_file cleared at the start of every new session/directory
     * enumeration, before f_opendir can fail -- defense in depth alongside
     * the num_of_files==0 OK-press guard above. --- */
    {
        const char *button_null = strstr(src, "if (button_status==NULL)");
        const char *memset_call = button_null ? strstr(button_null, "memset(&this_file, 0, sizeof(this_file));") : NULL;
        const char *opendir_call = memset_call ? strstr(memset_call, "f_opendir(&directory, pfb_hdl->info.dir_name);") : NULL;
        CHECK(button_null && memset_call && (memset_call - button_null) < 400,
              "this_file is memset right at the top of the button_status==NULL (new enumeration) branch");
        CHECK(memset_call && opendir_call && opendir_call > memset_call,
              "this_file is cleared BEFORE f_opendir runs, so an opendir failure can never return with stale this_file");
    }

    /* --- Pass-2 (populate) failure or a count mismatch against pass 1 is
     * treated as an SD error and never exposes a partial cache. --- */
    {
        const char *pass2_loop = strstr(src, "while (s_fb_sorted_count < entry_count)");
        const char *populate_error_decl = strstr(src, "bool populate_error = false;");
        const char *mismatch_check = strstr(src, "if (populate_error || s_fb_sorted_count != entry_count)");
        const char *mismatch_free = mismatch_check ? strstr(mismatch_check, "fb_sorted_free();") : NULL;
        const char *mismatch_status = mismatch_free ? strstr(mismatch_free, "pfb_hdl->info.status = FB_ERR_SDCARD;") : NULL;
        CHECK(populate_error_decl && pass2_loop, "pass 2 tracks its own populate_error flag alongside the counting loop");
        CHECK(mismatch_check != NULL, "pass 2 checks for either an error OR a short count against pass 1's entry_count");
        CHECK(mismatch_free && mismatch_status && (mismatch_status - mismatch_free) < 100,
              "a pass-2 failure or shortfall frees the partial cache and surfaces the SD-error status rather than exposing it");
    }

    /* --- Friendly folder-name display: SD root only. Every managed root
     * folder maps case-insensitively to one canonical lowercase label while
     * the raw FAT name remains available for file operations. --- */
    CHECK(contains(src, "\"nfc\", \"rfid\", \"subghz\", \"wifi\", \"ble\","), "nfc/rfid/subghz/wifi/ble lowercase mappings are present");
    CHECK(contains(src, "\"infrared\", \"data\", \"monsta\", \"apps\""), "infrared/data/monsta/apps lowercase mappings are present");
    CHECK(contains(src, "if (!fb_name_casecmp(raw_name, managed_root_names[i]))"), "managed root matching is case-insensitive");
    CHECK(contains(src, "return managed_root_names[i];"), "the canonical lowercase table entry is returned for display");
    CHECK(!contains(src, "return \"Sub-GHz\";") && !contains(src, "return \"Wi-Fi\";") &&
          !contains(src, "return \"Infrared\";") && !contains(src, "return \"Apps\";"),
          "the old title-case root-folder display labels are absent");
    CHECK(contains(src, "s_fb_friendly_names && pfb_hdl->dir_level == 0 &&"),
          "friendly-name substitution is restricted to the SD root (dir_level == 0), not any nested folder of the same name");
    CHECK(contains(src, "memcpy(&this_file, &file_info, sizeof(FILINFO));"),
          "the OK/descend selection target (this_file) is copied from the raw file_info, never from the display string");

    /* --- Real-execution proof for fb_friendly_dir_name()/fb_name_casecmp():
     * both are pure string functions with no HAL/FreeRTOS/u8g2 dependency,
     * so (unlike the rest of this file) they can be extracted from the real
     * source and host-compiled, giving genuine behavioral proof instead of
     * only a text-presence check -- same extract-the-real-function
     * technique m1_file_browser_navigation_test.py uses elsewhere in this
     * suite, done in plain C here so this file's own single-file build
     * stays self-contained. Proves: all nine managed names map to
     * themselves; every uppercase and several mixed-case FAT spellings of
     * them map to the same lowercase label; and names outside the managed
     * set (including an empty string and a near-miss like "wifi2") come
     * back completely unchanged. */
    {
        /* m1_file_browser.c has mixed CRLF/LF line endings; these two
         * functions happen to sit in differently-terminated regions, so
         * both line-ending forms are tried for each anchor. */
        const char *cmp_start = strstr(src, "static int fb_name_casecmp(const char *a, const char *b)\r\n{");
        if (!cmp_start) cmp_start = strstr(src, "static int fb_name_casecmp(const char *a, const char *b)\n{");
        const char *name_start = strstr(src, "static const char *fb_friendly_dir_name(const char *raw_name)\r\n{");
        if (!name_start) name_start = strstr(src, "static const char *fb_friendly_dir_name(const char *raw_name)\n{");
        CHECK(cmp_start != NULL, "fb_name_casecmp() definition found for extraction");
        CHECK(name_start != NULL, "fb_friendly_dir_name() definition found for extraction");

        if (cmp_start && name_start)
        {
            char *cmp_body = extract_braced(cmp_start);
            char *name_body = extract_braced(name_start);
            CHECK(cmp_body != NULL, "fb_name_casecmp() body brace-matched cleanly");
            CHECK(name_body != NULL, "fb_friendly_dir_name() body brace-matched cleanly");

            if (cmp_body && name_body)
            {
                FILE *tf = fopen("/tmp/m1_fb_friendly_name_extract.c", "w");
                CHECK(tf != NULL, "temp file for the extracted functions opened");
                if (tf)
                {
                    fprintf(tf,
                        "#include <stddef.h>\n#include <string.h>\n#include <stdio.h>\n\n"
                        "%s\n\n%s\n\n"
                        "static int fail;\n"
                        "static void check(const char *in, const char *expect) {\n"
                        "    const char *got = fb_friendly_dir_name(in);\n"
                        "    if (strcmp(got, expect) != 0) {\n"
                        "        fail++;\n"
                        "        printf(\"  FAIL: fb_friendly_dir_name(\\\"%%s\\\") = \\\"%%s\\\", expected \\\"%%s\\\"\\n\", in, got, expect);\n"
                        "    }\n"
                        "}\n\n"
                        "int main(void) {\n"
                        "    static const char *managed[] = {\"nfc\",\"rfid\",\"subghz\",\"wifi\",\"ble\",\"infrared\",\"data\",\"monsta\",\"apps\"};\n"
                        "    static const char *upper[]   = {\"NFC\",\"RFID\",\"SUBGHZ\",\"WIFI\",\"BLE\",\"INFRARED\",\"DATA\",\"MONSTA\",\"APPS\"};\n"
                        "    size_t i;\n"
                        "    for (i = 0; i < sizeof(managed)/sizeof(managed[0]); i++) check(managed[i], managed[i]);\n"
                        "    for (i = 0; i < sizeof(upper)/sizeof(upper[0]); i++)   check(upper[i],   managed[i]);\n"
                        "    check(\"Nfc\", \"nfc\"); check(\"SubGhz\", \"subghz\"); check(\"WiFi\", \"wifi\");\n"
                        "    check(\"InfraRed\", \"infrared\"); check(\"Monsta\", \"monsta\"); check(\"Apps\", \"apps\");\n"
                        "    check(\"MyStuff\", \"MyStuff\"); check(\"backup\", \"backup\"); check(\"wifi2\", \"wifi2\");\n"
                        "    check(\"subghz_old\", \"subghz_old\"); check(\"\", \"\"); check(\"APPSX\", \"APPSX\");\n"
                        "    if (!fail) printf(\"fb_friendly_dir_name real-execution checks: all passed\\n\");\n"
                        "    return fail ? 1 : 0;\n"
                        "}\n",
                        cmp_body, name_body);
                    fclose(tf);

                    {
                        int rc = system("cc -std=c11 -Wall -Wextra -fsanitize=address,undefined "
                                         "/tmp/m1_fb_friendly_name_extract.c "
                                         "-o /tmp/m1_fb_friendly_name_extract 2>&1 "
                                         "&& /tmp/m1_fb_friendly_name_extract");
                        CHECK(rc == 0,
                              "extracted fb_friendly_dir_name()/fb_name_casecmp() compile and pass all real-input checks "
                              "(9 managed names map to themselves, 9 uppercase + 6 mixed-case forms map to the lowercase "
                              "label, 6 unmanaged names incl. empty string and a near-miss are returned byte-for-byte unchanged)");
                    }
                }
            }
            free(cmp_body); free(name_body);
        }
    }

    CHECK(contains(src, "bool sort_root_no_dotdot = (s_fb_sort_enabled && s_fb_sorted != NULL && pfb_hdl->dir_level == 0);"),
          "\"..\" suppression at a sort-enabled browser's root is preserved under its own explicit name");

    /* --- Unrelated semantics proven UNCHANGED --- */
    CHECK(contains(src, "if ( num_of_files > 0 && pfb_hdl->listing_index < (num_of_files - 1) )"),
          "DOWN-key num_of_files>0 guard is unchanged");
    CHECK(contains(src, "static FILINFO this_file = {0};"),
          "this_file remains the shared, zero-initialized static (guard above prevents it going stale-and-selected, not its storage)");

    printf("m1_file_browser_empty_state_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
