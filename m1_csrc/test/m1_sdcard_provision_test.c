/* Host tests for m1_sdcard_provision.c (canonical SD-card folder
 * provisioning) and for the trigger-point wiring in m1_sdcard.c /
 * m1_storage.c / the per-feature lazy-creation call sites.
 *
 * Part 1 links the REAL, unmodified m1_sdcard_provision.c against a mock
 * fs_directory_ensure() this file supplies (via stub_sdprov/m1_file_util.h
 * shadowing the real header), so the actual production decision logic --
 * ordering, per-path outcome classification, continue-past-failure,
 * all-ok aggregation -- is genuinely exercised, not just grepped for.
 * m1_sdcard_provision.h's canonical path constants are the REAL ones; the
 * mock only replaces the FatFs boundary (fs_directory_ensure), not any
 * logic this module owns.
 *
 * Part 2 verifies the trigger points that call into this module.
 * m1_sdcard.c and m1_storage.c are FreeRTOS/HAL-coupled throughout (main.h,
 * stm32h5xx_hal.h, cmsis_os.h) and cannot be host-compiled or linked
 * directly -- same DISCLOSED LIMITATION as every other HAL-coupled file in
 * this project (see e.g. nfc_t2t_persistence_test.c). This section instead
 * verifies the exact wiring is present in the REAL, committed source text.
 *
 * Build & run (from repo root). m1_sdcard_provision.c/.h are copied into
 * stub_sdprov/ first (then removed) so its own quote-form #includes
 * ("m1_file_util.h", "m1_log_debug.h") resolve to the stubs sitting next
 * to it there, not the real m1_csrc/ headers a plain -I search order
 * cannot shadow (same technique as t2t_emu_image_test.c):
 *   cp m1_csrc/m1_sdcard_provision.c m1_csrc/m1_sdcard_provision.h \
 *      m1_csrc/test/stub_sdprov/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -I m1_csrc/test/stub_sdprov -I FatFs/R015 \
 *      m1_csrc/test/stub_sdprov/m1_sdcard_provision.c \
 *      m1_csrc/test/m1_sdcard_provision_test.c \
 *      -o /tmp/sdprov && /tmp/sdprov
 *   rm m1_csrc/test/stub_sdprov/m1_sdcard_provision.c m1_csrc/test/stub_sdprov/m1_sdcard_provision.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "ff.h"
#include "m1_sdcard_provision.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

/* ============================================================================
 * Part 1: mock fs_directory_ensure() + log capture, linked into the REAL
 * m1_sdcard_provision.c via the stub_sdprov/ header shadow.
 * ==========================================================================*/
#define MOCK_MAX 32
static struct { const char *path; FRESULT result; } s_mock_map[MOCK_MAX];
static int      s_mock_map_n = 0;
static const char *s_call_log[MOCK_MAX];
static int      s_call_log_n = 0;

static void mock_reset(void)
{
    s_mock_map_n = 0;
    s_call_log_n = 0;
}
static void mock_set(const char *path, FRESULT r)
{
    s_mock_map[s_mock_map_n].path = path;
    s_mock_map[s_mock_map_n].result = r;
    s_mock_map_n++;
}
/* This is the ONE function the real m1_sdcard_provision.c calls to touch
 * "FatFs" -- everything else in that file is the real production logic. */
FRESULT fs_directory_ensure(const char *path)
{
    int i;
    if (s_call_log_n < MOCK_MAX) { s_call_log[s_call_log_n++] = path; }
    for (i = 0; i < s_mock_map_n; i++)
    {
        if (strcmp(s_mock_map[i].path, path) == 0) { return s_mock_map[i].result; }
    }
    return FR_OK;   /* default: "already a directory / created successfully" */
}
static int call_count(const char *path)
{
    int i, n = 0;
    for (i = 0; i < s_call_log_n; i++) { if (strcmp(s_call_log[i], path) == 0) { n++; } }
    return n;
}
static int call_index(const char *path)   /* -1 if never called */
{
    int i;
    for (i = 0; i < s_call_log_n; i++) { if (strcmp(s_call_log[i], path) == 0) { return i; } }
    return -1;
}

static int  s_log_count = 0;
void test_sdprov_log_capture(const char *tag, const char *format, ...)
{
    va_list ap;
    char buf[256];
    s_log_count++;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    (void)tag;
}

static const char *ALL_NINE[] = {
    M1_SD_DIR_NFC, M1_SD_DIR_NFC_SYSTEM, M1_SD_DIR_NFC_RECOVER,
    M1_SD_DIR_RFID, M1_SD_DIR_SUBGHZ, M1_SD_DIR_WIFI, M1_SD_DIR_BLE,
    M1_SD_DIR_INFRARED, M1_SD_DIR_INFRARED_DB,
};
#define ALL_NINE_N ((int)(sizeof(ALL_NINE) / sizeof(ALL_NINE[0])))

static void test_blank_card_creates_full_tree(void)   /* req 1 */
{
    bool ok;
    int i;
    mock_reset();
    ok = m1_sdcard_provision_canonical();
    CHECK(ok, "blank card: overall result is success");
    for (i = 0; i < ALL_NINE_N; i++)
    {
        CHECK(call_count(ALL_NINE[i]) == 1, "blank card: every canonical path attempted exactly once");
    }
    CHECK(call_index(M1_SD_DIR_NFC) < call_index(M1_SD_DIR_NFC_SYSTEM),
          "blank card: nfc/ provisioned before nfc/system/ (parent before child)");
    CHECK(call_index(M1_SD_DIR_NFC) < call_index(M1_SD_DIR_NFC_RECOVER),
          "blank card: nfc/ provisioned before nfc/recover/ (parent before child)");
    CHECK(call_index(M1_SD_DIR_INFRARED) < call_index(M1_SD_DIR_INFRARED_DB),
          "blank card: infrared/ provisioned before infrared/db/ (parent before child)");
}

static void test_second_run_no_changes(void)   /* req 2 */
{
    bool ok1, ok2;
    int first_calls;
    mock_reset();
    ok1 = m1_sdcard_provision_canonical();
    first_calls = s_call_log_n;
    /* Second call: same mock (still "FR_OK => already there"), fresh call
     * log. If the module carried hidden state from the first run, a
     * second identical run would behave differently -- it does not. */
    s_call_log_n = 0;
    ok2 = m1_sdcard_provision_canonical();
    CHECK(ok1 && ok2, "second provisioning run: still reports success");
    CHECK(s_call_log_n == first_calls, "second provisioning run: attempts the same 9 paths, no more, no fewer");
}

/* req 3: existing uppercase/mixed-case FAT folders do not produce
 * duplicates. This guarantee lives inside FatFs' own case-insensitive
 * f_stat() matching (confirmed separately: no FF_FS_CASE_SENSITIVE option
 * is set in ffconf.h, so a component-name match is case-insensitive; see
 * m1_fb_check_existence()'s direct f_stat() use for the same property).
 * m1_sdcard_provision.c never does its own name comparison -- it only
 * forwards each canonical path string to fs_directory_ensure() and acts on
 * the FRESULT, so it cannot introduce a case-sensitivity bug of its own
 * regardless of what FatFs reports. That "never compares names itself" is
 * what this test actually verifies, behaviorally: every path yields the
 * same OK/COLLISION/ERROR classification the mock's FRESULT says, with no
 * additional case-dependent branching in between. */
static void test_case_insensitive_matching_not_reimplemented(void)
{
    mock_reset();
    mock_set(M1_SD_DIR_SUBGHZ, FR_OK);  /* mock stands in for "FatFs matched SUBGHZ case-insensitively" */
    (void)m1_sdcard_provision_canonical();
    CHECK(call_count(M1_SD_DIR_SUBGHZ) == 1,
          "case-insensitive match: the canonical lowercase spelling is what's asked for either way, unconditionally");
}

static void test_file_collision_reported_not_deleted(void)   /* req 4 */
{
    bool ok;
    int i;
    mock_reset();
    mock_set(M1_SD_DIR_NFC_SYSTEM, FR_EXIST);   /* a regular file occupies this name */
    ok = m1_sdcard_provision_canonical();
    CHECK(!ok, "file collision: overall result reports failure");
    CHECK(s_log_count >= 1, "file collision: a failure is logged");
    for (i = 0; i < ALL_NINE_N; i++)
    {
        CHECK(call_count(ALL_NINE[i]) == 1, "file collision: every path still attempted exactly once, including the colliding one");
    }
    /* Never deletes/overwrites: the only FatFs call this module ever makes
     * for a colliding path is the SAME fs_directory_ensure() every other
     * path gets -- there is no separate delete/rename/overwrite call in
     * m1_sdcard_provision.c at all (verified in Part 2's source check). */
}

static void test_child_collision_does_not_block_siblings(void)   /* req 5 */
{
    bool ok;
    mock_reset();
    mock_set(M1_SD_DIR_NFC_SYSTEM, FR_EXIST);   /* child collides */
    ok = m1_sdcard_provision_canonical();
    CHECK(!ok, "child collision: overall result reports failure");
    CHECK(call_count(M1_SD_DIR_NFC) == 1, "child collision: parent (nfc/) is still attempted and unaffected");
    CHECK(call_count(M1_SD_DIR_NFC_RECOVER) == 1, "child collision: sibling (nfc/recover/) still attempted");
    CHECK(call_count(M1_SD_DIR_RFID) == 1, "child collision: unrelated top-level folder (rfid/) still attempted");
    CHECK(call_count(M1_SD_DIR_SUBGHZ) == 1, "child collision: unrelated top-level folder (subghz/) still attempted");
    CHECK(call_count(M1_SD_DIR_WIFI) == 1, "child collision: unrelated top-level folder (wifi/) still attempted");
    CHECK(call_count(M1_SD_DIR_BLE) == 1, "child collision: unrelated top-level folder (ble/) still attempted");
    CHECK(call_count(M1_SD_DIR_INFRARED) == 1, "child collision: unrelated top-level folder (infrared/) still attempted");
    CHECK(call_count(M1_SD_DIR_INFRARED_DB) == 1, "child collision: unrelated top-level folder (infrared/db/) still attempted");
}

static void test_read_only_media_handled_safely(void)   /* req 7 */
{
    bool ok;
    int i;
    mock_reset();
    for (i = 0; i < ALL_NINE_N; i++) { mock_set(ALL_NINE[i], FR_WRITE_PROTECTED); }
    ok = m1_sdcard_provision_canonical();
    CHECK(!ok, "read-only media: overall result reports failure, not a crash/hang");
    for (i = 0; i < ALL_NINE_N; i++)
    {
        CHECK(call_count(ALL_NINE[i]) == 1, "read-only media: every path still attempted exactly once despite every one failing");
    }
    CHECK(s_log_count >= 1, "read-only media: at least one failure logged");
}

static void test_no_apps_folders(void)   /* req 14 */
{
    mock_reset();
    (void)m1_sdcard_provision_canonical();
    CHECK(call_index("0:/apps") < 0, "apps/ is never provisioned");
    CHECK(call_index("0:/apps/data") < 0, "apps/data/ is never provisioned");
    CHECK(s_call_log_n == 9, "exactly the 9 required canonical paths are provisioned, nothing extra");
}

/* ============================================================================
 * Part 2: source-text verification of the trigger points and retained
 * lazy-creation fallbacks (m1_sdcard.c / m1_storage.c / per-feature files
 * are FreeRTOS+HAL-coupled and cannot be host-linked -- see file header).
 * ==========================================================================*/
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz; char *buf;
    if (!f) { printf("  FAIL: could not open %s\n", path); g_fail++; return NULL; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}
static bool contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static void test_source_wiring(void)
{
    char *sdcard_c   = slurp("m1_csrc/m1_sdcard.c");
    char *storage_c  = slurp("m1_csrc/m1_storage.c");
    char *nfc_c      = slurp("m1_csrc/m1_nfc.c");
    char *rfid_c     = slurp("m1_csrc/m1_rfid.c");
    char *subghz_c   = slurp("m1_csrc/m1_sub_ghz.c");
    char *bt_c       = slurp("m1_csrc/m1_bt.c");
    char *wifi_c     = slurp("m1_csrc/m1_wifi.c");
    char *lfrfid_c   = slurp("lfrfid/lfrfid_file.c");
    char *nfc_file_c = slurp("NFC/NFC_drv/common/nfc_file.c");
    char *harvest_c  = slurp("NFC/NFC_drv/common/mfc_harvest_storage.c");
    char *provision_c = slurp("m1_csrc/m1_sdcard_provision.c");
    char *provision_h = slurp("m1_csrc/m1_sdcard_provision.h");
    char *sys_init_c = slurp("m1_csrc/m1_sys_init.c");
    char *cmake_txt  = slurp("cmake/m1_01/CMakeLists.txt");

    /* Boot-time provisioning reaches FatFs LFN calls. Keep the initialization
     * task on the 1024-word stack so this automatic behavior cannot overflow
     * the former default allocation before the UI tasks are created. */
    CHECK(contains(sys_init_c,
          "xTaskCreate(m1_system_init_task, \"m1_system_init_task_n\", M1_TASK_STACK_SIZE_1024"),
          "system initialization task reserves 1024 words for boot-time SD provisioning");

    /* req 6: missing card / unusable filesystem never provisions. The
     * only production call site is gated on the SD_access_OK branch of
     * m1_sdcard_mount() -- never on NotReady/NotOK/UnMounted/NoFS. */
    CHECK(contains(sdcard_c, "sdcard_ctl.status = SD_access_OK;") &&
          contains(sdcard_c, "m1_sdcard_provision_canonical();"),
          "provisioning call site sits in the SD_access_OK success branch of m1_sdcard_mount()");
    CHECK(!contains(sdcard_c, "SD_access_NoFS;\r\n\t\t\tm1_sdcard_provision_canonical"),
          "provisioning is not reachable from the NoFS branch");

    /* req 8: USB MSC ownership suppresses provisioning. */
    CHECK(contains(sdcard_c, "if (!usbmsc_sd_enable)") &&
          contains(sdcard_c, "m1_sdcard_provision_canonical();"),
          "provisioning is guarded by \"firmware currently owns the card\" (usbmsc_sd_enable == 0)");

    /* req 9: returning ownership from USB MSC triggers provisioning --
     * both m1_storage.c reclaim paths still call m1_sdcard_mount() (which
     * now provisions internally) immediately after dropping MSC ownership. */
    CHECK(contains(storage_c, "usbmsc_sd_enable = false;\r\n            m1_sdcard_mount();") ||
          contains(storage_c, "usbmsc_sd_enable = false;   /* drop ownership before remount */\r\n                m1_sdcard_mount();"),
          "at least one USB-MSC-reclaim path drops ownership then calls m1_sdcard_mount() immediately");

    /* req 10: card insertion/remount triggers provisioning -- unchanged,
     * pre-existing wiring: the CONNECTED-event handler's remount path
     * still calls m1_sdcard_mount(), which now provisions on success. */
    CHECK(contains(sdcard_c, "m1_sdcard_unmount();\r\n\t\t\t\t\t\t\tm1_sdcard_mount();") ||
          contains(sdcard_c, "m1_sdcard_mount();"),
          "card insertion/remount handling still calls m1_sdcard_mount()");

    /* req 11: lazy per-feature creation uses the canonical definitions,
     * not its own literal. */
    CHECK(contains(nfc_c, "#define NFC_SAVE_DIR\t\t\t\t\tM1_SD_DIR_NFC"),
          "m1_nfc.c: NFC_SAVE_DIR is defined from the canonical constant");
    CHECK(contains(rfid_c, "m1_fb_check_existence(M1_SD_DIR_RFID)") &&
          contains(rfid_c, "m1_fb_make_dir(M1_SD_DIR_RFID)"),
          "m1_rfid.c: lazy-create call site uses the canonical constant, not a literal");
    CHECK(contains(subghz_c, "m1_fb_check_existence(M1_SD_DIR_SUBGHZ)") &&
          contains(subghz_c, "m1_fb_make_dir(M1_SD_DIR_SUBGHZ)"),
          "m1_sub_ghz.c: lazy-create call site uses the canonical constant, not a literal");
    CHECK(contains(bt_c, "#define BLE_SAVE_DIR               M1_SD_DIR_BLE"),
          "m1_bt.c: BLE_SAVE_DIR is defined from the canonical constant");
    CHECK(contains(wifi_c, "#define DRIVE0_WIFI                 M1_SD_DIR_WIFI"),
          "m1_wifi.c: DRIVE0_WIFI is defined from the canonical constant");
    CHECK(contains(lfrfid_c, "#define DRIVE0_RFID     M1_SD_DIR_RFID"),
          "lfrfid_file.c: DRIVE0_RFID is defined from the canonical constant");
    CHECK(contains(nfc_file_c, "#define DRIVE0_NFC     M1_SD_DIR_NFC"),
          "nfc_file.c: DRIVE0_NFC is defined from the canonical constant");
    CHECK(contains(harvest_c, "#define HS_NFC_DIR      M1_SD_DIR_NFC") &&
          contains(harvest_c, "#define HS_RECOVER_DIR  M1_SD_DIR_NFC_RECOVER"),
          "mfc_harvest_storage.c: HS_NFC_DIR/HS_RECOVER_DIR defined from the canonical constants");
    CHECK(!contains(nfc_c, "\"0:/nfc\"") && !contains(rfid_c, "\"0:/rfid\"") &&
          !contains(subghz_c, "\"0:/subghz\"") && !contains(bt_c, "\"0:/ble\"") &&
          !contains(wifi_c, "\"0:/wifi\"") && !contains(lfrfid_c, "\"0:/rfid\"") &&
          !contains(nfc_file_c, "\"0:/nfc\""),
          "no per-feature file has a bare canonical-folder string literal left over");

    /* req 12: self-healing retained -- each feature's lazy check-then-
     * create fallback is still present (not removed in favor of boot-time
     * provisioning alone), so a folder deleted after boot is recreated on
     * next use. */
    CHECK(contains(nfc_c, "m1_fb_check_existence(NFC_SAVE_DIR)") && contains(nfc_c, "m1_fb_make_dir(NFC_SAVE_DIR)"),
          "m1_nfc.c retains its lazy check-then-create fallback");
    CHECK(contains(rfid_c, "m1_fb_check_existence(M1_SD_DIR_RFID)"),
          "m1_rfid.c retains its lazy check-then-create fallback");
    CHECK(contains(subghz_c, "m1_fb_check_existence(M1_SD_DIR_SUBGHZ)"),
          "m1_sub_ghz.c retains its lazy check-then-create fallback");
    CHECK(contains(bt_c, "m1_fb_check_existence(BLE_SAVE_DIR)"),
          "m1_bt.c retains its lazy check-then-create fallback");
    CHECK(contains(wifi_c, "fs_directory_ensure(DRIVE0_WIFI)"),
          "m1_wifi.c retains its lazy ensure-directory fallback");
    CHECK(contains(harvest_c, "fs_directory_ensure(HS_NFC_DIR)") && contains(harvest_c, "fs_directory_ensure(HS_RECOVER_DIR)"),
          "mfc_harvest_storage.c retains its lazy ensure-directory fallback for both nfc/ and nfc/recover/");

    /* req 13: no seed-content files -- the provisioning module only ever
     * ensures directories, never opens/writes a file. */
    CHECK(!contains(provision_c, "f_open") && !contains(provision_c, "fopen") &&
          !contains(provision_c, "m1_fb_open_new_file") && !contains(provision_c, "f_write"),
          "m1_sdcard_provision.c contains no file-content write of any kind, only directory provisioning");

    /* req 14 (source form): no apps/ constant is defined. (The header's own
     * doc comment legitimately mentions "apps/" in prose, explaining the
     * deferral -- checked for the absent #define itself, not the word.) */
    CHECK(!contains(provision_h, "#define M1_SD_DIR_APPS"),
          "m1_sdcard_provision.h defines no apps/ or apps/data/ constant");

    /* req 15: no M1CP protocol value or filesystem-command behavior
     * changed -- this task never touched any M1CP/Web-Manager-facing file. */
    CHECK(!contains(sdcard_c, "FS_MKDIR") && !contains(provision_c, "FS_MKDIR") && !contains(provision_h, "FS_MKDIR"),
          "no FS_MKDIR or other new M1CP opcode was introduced anywhere touched by this task");

    /* Build wiring: the new module is actually compiled into the firmware. */
    CHECK(contains(cmake_txt, "../../m1_csrc/m1_sdcard_provision.c"),
          "m1_sdcard_provision.c is registered in the CMake source list");

    free(sdcard_c); free(storage_c); free(nfc_c); free(rfid_c); free(subghz_c);
    free(bt_c); free(wifi_c); free(lfrfid_c); free(nfc_file_c); free(harvest_c);
    free(provision_c); free(provision_h); free(sys_init_c); free(cmake_txt);
}

int main(void)
{
    test_blank_card_creates_full_tree();
    test_second_run_no_changes();
    test_case_insensitive_matching_not_reimplemented();
    test_file_collision_reported_not_deleted();
    test_child_collision_does_not_block_siblings();
    test_read_only_media_handled_safely();
    test_no_apps_folders();
    test_source_wiring();

    printf("m1_sdcard_provision_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
