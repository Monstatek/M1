/*
 * Source-seam regression test for Home-LEFT saved-file lifecycle routing.
 * HAL/FreeRTOS-coupled UI translation units cannot be linked on the host, so
 * this reads the production sources and verifies the architectural seams that
 * previously regressed: protocol-owned launchers, bounded queue delivery,
 * manager-owned awake backlight baseline, and transactional Sub-GHz cleanup.
 *
 * Build/run from repository root:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/m1_saved_file_lifecycle_test.c -o /tmp/m1_saved_lifecycle \
 *      && /tmp/m1_saved_lifecycle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *data;
    size_t got;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size + 1U);
    if (!data) { fclose(f); return NULL; }
    got = fread(data, 1, (size_t)size, f);
    data[got] = 0;
    fclose(f);
    return data;
}

static int has(const char *s, const char *needle)
{
    return s != NULL && strstr(s, needle) != NULL;
}

int main(void)
{
    char *storage = slurp("m1_csrc/m1_storage.c");
    char *nfc = slurp("m1_csrc/m1_nfc.c");
    char *rfid = slurp("m1_csrc/m1_rfid.c");
    char *lib = slurp("m1_csrc/m1_lib.c");
    char *task = slurp("m1_csrc/m1_feedback_task.c");
    char *system = slurp("m1_csrc/m1_system.c");
    char *subghz = slurp("m1_csrc/m1_sub_ghz.c");

    CHECK(storage && nfc && rfid && lib && task && system && subghz,
          "all production sources are readable");
    if (!storage || !nfc || !rfid || !lib || !task || !system || !subghz)
        return 1;

    CHECK(has(storage, "nfc_saved_launch(f->dir_name, f->file_name)"),
          "global .nfc dispatch uses the NFC lifecycle-aware launcher");
    CHECK(has(storage, "rfid_125khz_saved_launch(f->dir_name, f->file_name)"),
          "global .rfid dispatch uses the RFID lifecycle-aware launcher");
    CHECK(!has(storage, "m1_browser_request_explore_open(f); nfc_saved();"),
          "global explorer no longer calls the NFC menu leaf directly");
    CHECK(!has(storage, "m1_browser_request_explore_open(f); rfid_125khz_saved();"),
          "global explorer no longer calls the RFID menu leaf directly");

    CHECK(has(nfc, "nfc_saved_launch(NULL, NULL);"),
          "native NFC Saved delegates to the authoritative launcher");
    CHECK(has(nfc, "menu_nfc_init();") && has(nfc, "menu_nfc_deinit();") &&
          has(nfc, "s_nfc_saved_launch.pending"),
          "NFC exact-file launcher owns lifecycle and copied selection");
    CHECK(!has(nfc, "m1_browser_explore_take_pending()") &&
          !has(nfc, "m1_browser_explore_active()"),
          "NFC no longer depends on the generic bridge state");

    CHECK(has(rfid, "rfid_125khz_saved_launch(NULL, NULL);"),
          "native RFID Saved delegates to the authoritative launcher");
    CHECK(has(rfid, "menu_125khz_rfid_init();") &&
          has(rfid, "menu_125khz_rfid_deinit();") &&
          has(rfid, "s_rfid_saved_launch.pending"),
          "RFID exact-file launcher owns lifecycle and copied selection");
    CHECK(!has(rfid, "m1_browser_explore_take_pending()") &&
          !has(rfid, "m1_browser_explore_active()"),
          "RFID no longer depends on the generic bridge state");

    CHECK(has(lib, "if (Handle == NULL)") && has(lib, "return pdFALSE;"),
          "common queue send rejects NULL handles");
    CHECK(has(lib, "pdMS_TO_TICKS(100)") && !has(lib, "xQueueSend(Handle, &q_item, portMAX_DELAY)"),
          "common queue send is bounded rather than waiting forever");

    CHECK(has(task, "fb_sleep_timer_wake();"),
          "feedback startup seeds the manager-owned awake baseline");
    CHECK(has(system, "fb_sleep_timer_wake(); // keep normal awake brightness manager-owned"),
          "welcome/status display refreshes the manager-owned awake baseline");

    CHECK(has(subghz, "if (sys_error)") &&
          has(subghz, "sub_ghz_raw_samples_deinit(false);") &&
          has(subghz, "sub_ghz_ring_buffers_deinit();"),
          "Sub-GHz failed loads close/free both resource groups");
    CHECK(has(subghz, "subghz_front_buffer != NULL && subghz_ring_read_buffer != NULL") &&
          has(subghz, "memset(&datfile_info.dat_file_hdl, 0"),
          "Sub-GHz partial allocation/file state cannot masquerade as initialized");

    free(storage); free(nfc); free(rfid); free(lib); free(task); free(system); free(subghz);
    printf("m1_saved_file_lifecycle_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
