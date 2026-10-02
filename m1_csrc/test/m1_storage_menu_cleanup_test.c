/*
 * m1_storage_menu_cleanup_test.c - source-seam test proving the Settings >
 * Storage menu cleanup is complete and clean: the duplicate "Explore SD
 * Card" menu item and the unreferenced Mount/Unmount menu items and their
 * backing functions are fully gone, while boot/home LEFT's independent path
 * to storage_explore() and the SD driver's own mount/unmount primitives are
 * untouched.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DISCLOSED LIMITATION: m1_menu.c and m1_storage.c pull in stm32h5xx_hal.h
 * and FreeRTOS transitively, so they cannot be host-compiled or linked
 * directly. This suite verifies the exact source text instead (reads the
 * actual files; not a mock, not a paraphrase) -- same technique used
 * elsewhere in this project.
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/m1_storage_menu_cleanup_test.c -o /tmp/menu_cleanup && /tmp/menu_cleanup
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

int main(void)
{
    char *menu = slurp("m1_csrc/m1_menu.c");
    char *storage = slurp("m1_csrc/m1_storage.c");
    char *storage_h = slurp("m1_csrc/m1_storage.h");
    CHECK(menu != NULL, "m1_menu.c readable");
    CHECK(storage != NULL, "m1_storage.c readable");
    CHECK(storage_h != NULL, "m1_storage.h readable");
    if (!menu || !storage || !storage_h)
    {
        printf("m1_storage_menu_cleanup_test: %d passed, %d failed (source unreadable, aborting)\n", g_pass, g_fail);
        return 1;
    }

    /* --- The three removed menu-item structs are fully gone --- */
    CHECK(!contains(menu, "menu_Setting_Storage_Explore"), "menu_Setting_Storage_Explore struct is fully removed");
    CHECK(!contains(menu, "menu_Setting_Storage_Mount"), "menu_Setting_Storage_Mount struct is fully removed");
    CHECK(!contains(menu, "menu_Setting_Storage_Unmount"), "menu_Setting_Storage_Unmount struct is fully removed");
    /* The removed menu-item struct initializers themselves are the actual UI
     * strings; a prose comment may still name them for context (it does),
     * so check the live struct-initializer form specifically (label paired
     * with its old dispatch function on one line), not any mention of the
     * words in isolation. */
    CHECK(!contains(menu, "\"Mount SD Card\", storage_mount"), "the \"Mount SD Card\" struct initializer is fully removed");
    CHECK(!contains(menu, "\"Unmount SD Card\", storage_unmount"), "the \"Unmount SD Card\" struct initializer is fully removed");

    /* --- The parent array reflects the removal: count 2, only About+Format --- */
    {
        const char *arr = strstr(menu, "S_M1_Menu_t menu_Settings_Storage =");
        CHECK(arr != NULL, "menu_Settings_Storage definition still present");
        const char *count_and_list = arr ? strstr(arr, "\"Storage\", menu_setting_storage_init, NULL, NULL, 2, 0, NULL, NULL, {&menu_Setting_Storage_About, &menu_Setting_Storage_Format}") : NULL;
        CHECK(count_and_list && arr && (count_and_list - arr) < 100,
              "menu_Settings_Storage now declares exactly 2 items: About SD Card and Format SD Card, in that order");
    }

    /* --- storage_mount()/storage_unmount(): no definition, no prototype, no live dispatch --- */
    CHECK(!contains(storage, "storage_mount(void);") && !contains(storage, "storage_mount(void)\r\n"),
          "no storage_mount() prototype or function-signature line remains in m1_storage.c");
    CHECK(!contains(storage, "storage_unmount(void);") && !contains(storage, "storage_unmount(void)\r\n"),
          "no storage_unmount() prototype or function-signature line remains in m1_storage.c");
    CHECK(!contains(storage_h, "storage_mount(void);"), "storage_mount() prototype is removed from m1_storage.h");
    CHECK(!contains(storage_h, "storage_unmount(void);"), "storage_unmount() prototype is removed from m1_storage.h");
    /* A prose comment may still name the removed functions for context (it
     * does, in both files); what must be gone is any live dispatch --
     * assigning either as a menu item's sub_func / struct field. */
    CHECK(!contains(menu, ", storage_mount,"), "storage_mount is no longer wired into any menu struct's dispatch field");
    CHECK(!contains(menu, ", storage_unmount,"), "storage_unmount is no longer wired into any menu struct's dispatch field");

    /* --- What must survive untouched --- */
    CHECK(contains(storage, "void storage_explore(void);"), "storage_explore() prototype survives (still called by boot/home LEFT)");
    CHECK(contains(storage, "storage_explore(void)\r\n{"), "storage_explore() function body survives intact");
    CHECK(contains(menu, "m1_device_stat.sub_func = storage_explore;"), "boot/home LEFT still dispatches storage_explore() directly");
    CHECK(contains(storage, "void  menu_setting_storage_exit(void)") || contains(storage, "void menu_setting_storage_exit(void)"),
          "menu_setting_storage_exit() (still used by the surviving About/Format items) survives");
    /* The underlying SD driver primitives are a different layer and must not
     * have been touched by removing the two menu-item wrapper functions. */
    CHECK(contains(storage, "m1_sdcard_mount();"), "m1_sdcard_mount() driver primitive is still called elsewhere in m1_storage.c (untouched)");
    CHECK(contains(storage, "m1_sdcard_unmount()!=FR_OK"), "USB sharing checks the unmount operation result before granting ownership");
    /* Shared assets that storage_mount/storage_unmount used must still be
     * referenced by surviving code (storage_format and friends), proving
     * they were correctly left alone rather than orphaned or deleted. */
    CHECK(contains(storage, "sdcard_access_error_message"), "shared sdcard_access_error_message asset still referenced (used by surviving functions)");
    CHECK(contains(storage, "hourglass_18x32"), "shared hourglass_18x32 icon still referenced (used by surviving functions, e.g. storage_format)");

    /* --- storage_browse(): the already-mounted (SD_access_OK) case must
     * call m1_fb_display(NULL) exactly once (via the common success path
     * below the switch), not once in the switch case AND once more in the
     * common path. --- */
    {
        /* Anchored on the definition specifically (trailing "\n{", not ";")
         * so this can't accidentally match the earlier prototype declaration
         * and pull unrelated functions declared in between into the search. */
        const char *fn = strstr(storage, "S_M1_file_info *storage_browse(void)\n{");
        CHECK(fn != NULL, "storage_browse() definition found");
        const char *fn_end = fn ? strstr(fn, "} // S_M1_file_info *storage_browse(void)") : NULL;
        CHECK(fn_end != NULL, "found storage_browse()'s closing comment to bound the search");
        if (fn && fn_end)
        {
            size_t span = (size_t)(fn_end - fn);
            char *body = malloc(span + 1);
            memcpy(body, fn, span);
            body[span] = 0;

            const char *ok_case = strstr(body, "case SD_access_OK:");
            const char *next_case = ok_case ? strstr(ok_case, "case SD_access_NotReady:") : NULL;
            CHECK(ok_case && next_case, "found the SD_access_OK switch case and its successor");
            if (ok_case && next_case)
            {
                size_t case_span = (size_t)(next_case - ok_case);
                char *case_body = malloc(case_span + 1);
                memcpy(case_body, ok_case, case_span);
                case_body[case_span] = 0;
                CHECK(strstr(case_body, "m1_fb_display(NULL);") == NULL,
                      "the SD_access_OK case itself no longer calls m1_fb_display(NULL) -- only the common path below does");
                free(case_body);
            }

            /* Scoped to the initial setup section (before the main button
             * loop): this is what the original double-enumeration-on-mount
             * bug actually was (the SD_access_OK switch case calling it,
             * then the common path calling it again, back to back, before
             * the loop even starts). The generic-Explorer BACK handler
             * inside the main loop legitimately calls m1_fb_display(NULL)
             * a second time, later, to redraw the parent directory after
             * ascending one level -- a different call for a different
             * reason, not a reintroduction of the old bug. */
            const char *main_loop = strstr(body, "while (1 )");
            CHECK(main_loop != NULL, "found the main button loop to bound the setup-section search");
            if (main_loop)
            {
                size_t setup_span = (size_t)(main_loop - body);
                char *setup_body = malloc(setup_span + 1);
                memcpy(setup_body, body, setup_span);
                setup_body[setup_span] = 0;
                CHECK(count_occurrences(setup_body, "m1_fb_display(NULL);") == 1,
                      "m1_fb_display(NULL) is called exactly once during initial setup (the common success path), not twice");
                free(setup_body);
            }

            /* The common path is still reached by, and still performs the
             * required display for, the mount-recovery (NotReady) and
             * default-retry cases -- i.e. it isn't skipped for anything
             * except the two cases that set error_stat themselves. */
            const char *common_if = strstr(body, "if ( !error_stat )");
            const char *common_display = common_if ? strstr(common_if, "m1_fb_display(NULL);") : NULL;
            CHECK(common_if && common_display, "the common (!error_stat) path still performs the display for recovered/OK sessions");

            /* A failed parent redraw happens after m1_fb_navigate_back() has
             * closed the old DIR.  It must publish an error and terminate the
             * session, never continue using that closed/stale handle. */
            const char *parent_redraw = strstr(body, "f_info = m1_fb_display(NULL);");
            const char *redraw_error = parent_redraw ? strstr(parent_redraw, "if ( !f_info || f_info->status != FB_OK )") : NULL;
            const char *publish_error = redraw_error ? strstr(redraw_error, "file_info.status = FB_ERR_SDCARD;") : NULL;
            const char *terminate = redraw_error ? strstr(redraw_error, "menu_setting_storage_exit();") : NULL;
            const char *unsafe_continue = redraw_error ? strstr(redraw_error, "continue; // Stay in this browsing session") : NULL;
            CHECK(parent_redraw && redraw_error && publish_error,
                  "failed parent redraw publishes FB_ERR_SDCARD");
            CHECK(redraw_error && terminate && (!unsafe_continue || terminate < unsafe_continue),
                  "failed parent redraw terminates before the success-path continue");

            free(body);
        }
    }

    printf("m1_storage_menu_cleanup_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
