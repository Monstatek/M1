#!/usr/bin/env python3
"""Execute the actual STM32 handlers with host hardware stubs under ASan/UBSan.

Run from the repository root:
  python3 m1_csrc/test/m1_pass1_memory_safety_test.py
Use --ref HEAD to check that the pre-fix handlers fail the regression cases.
Only temporary host-build files are written; this does not exercise real RF,
SD, display, or FreeRTOS hardware behavior.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read_source(path, ref):
    if ref:
        return subprocess.check_output(
            ["git", "show", f"{ref}:{path}"], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def function(source, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;\n]*\)\n\{", source, re.M)
    assert match, name
    start = match.start()
    body = source[match.end() - 1:]
    # Ignore braces inside comments and string/character literals.
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                         r'//[^\n]*|/\*[\s\S]*?\*/|[{}]', body)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:match.end() - 1 + token.end()]
    raise AssertionError(name)


PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BaseType_t;
typedef int FRESULT;
enum { FB_OK, FB_ERR_SDCARD, FB_ERR_GUI };
typedef enum { M1_FB_BACK_AT_ROOT, M1_FB_BACK_PARENT_OPENED, M1_FB_BACK_ERROR } m1_fb_back_result_t;
typedef struct { char *dir_name, *file_name; bool file_is_selected; int status; } S_M1_file_info;
typedef struct { int event[2]; } S_M1_Buttons_Status;
typedef struct { int q_evt_type; } S_M1_Main_Q_t;
#define ESP_FILE_NAME_LEN_MAX 32
#define ESP_FILE_PATH_LEN_MAX 64
#define FILE_READWRITE_LEN_MAX 100
#define SD_access_OK 0
#define SD_access_NotReady 1
#define SD_access_NoFS 2
#define SD_RET_OK 0
#define FR_OK 0
#define pdTRUE 1
#define portMAX_DELAY 0
#define Q_EVENT_KEYPAD 1
#define Q_EVENT_MENU_EXIT 2
#define BUTTON_BACK_KP_ID 0
#define BUTTON_EVENT_CLICK 1
#define M1_DISP_DRAW_COLOR_TXT 0
#define M1_DISP_MAIN_MENU_FONT_N 0
#define M1_LCD_DISPLAY_WIDTH 128
#define M1_LCD_DISPLAY_HEIGHT 64
#define SDCARD_ERROR_IMAGE_WIDTH 0
#define SDCARD_ERROR_IMAGE_HEIGHT 0
#define RFID_FILE_EXTENSION_TMP ".rfid"
#define X_MENU_UPDATE_RESET 1
#define X_MENU_UPDATE_RESTORE 2
#define VIEW_MODE_LFRFID_SAVED_SUBMENU 3
#define IDS_UNSUPPORTED_FILE_ 0
#define IDS_BACK 0
#define SDCARD_CLI_DRIVE_PATH "0:/"
#define M1_LOG_N(...) ((void)0)
#define M1_LOG_I(...) ((void)0)
#define u8g2_SetDrawColor(...) ((void)0)
#define u8g2_SetFont(...) ((void)0)
#define u8g2_DrawXBMP(...) ((void)0)
#define m1_u8g2_firstpage() ((void)0)
#define m1_u8g2_nextpage() ((void)0)
#define m1_sdcard_mount() ((void)0)
#define m1_sdcard_format() ((void)0)
#define m1_sd_detected() 1
#define m1_gui_submenu_update(...) ((void)0)
/* Generic-Explorer navigation mode is exercised by its own dedicated
 * m1_file_browser_navigation_test.py; keep it off here so this fuzz harness
 * continues exercising storage_browse()'s general BACK-always-exits path. */
#define m1_fb_explorer_nav_active() 0
#define m1_fb_navigate_back() M1_FB_BACK_AT_ROOT
#define m1_message_box(...) (++errors)
#define m1_app_send_q_message(...) ((void)0)
#define m1_uiView_display_switch(...) (++switches)
#define m1_fb_dyn_strcat(...) 1
#define m1_fb_open_new_file(...) 0
#define m1_fb_close_file(...) 0
#define m1_fb_open_file(...) 0
#define m1_fb_write_to_file(...) 1
#define m1_fb_delete_file(...) 0
#define m1_fb_listing(...) 0
static char info_filename[32], info_filepath[64];
static S_M1_file_info file_info = {info_filepath, info_filename, false, FB_OK};
static S_M1_file_info input, *f_info;
static struct {
    bool external, pending;
    char dir[65], name[33];
    S_M1_file_info file;
} s_rfid_saved_launch;
static struct { char filepath[128], filename[32]; } lfrfid_tag_info;
static int m1_u8g2, main_q_hdl = 1, button_events_q_hdl = 2, m1_cli_file;
static int sd_status, init_fail, initialized, display_calls, exits, events;
static int errors, switches, loads, load_ok, lfrfid_uiview_gui_latest_param;
static const unsigned char *read_data;
static size_t read_len;
static int m1_sdcard_get_status(void) { return sd_status; }
static int m1_sdcard_init_retry(void) { return SD_RET_OK; }
static void *m1_fb_init(void *lcd) { (void)lcd; initialized = !init_fail; return initialized ? &m1_u8g2 : NULL; }
static void menu_setting_storage_exit(void) { initialized = 0; ++exits; }
static S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *buttons) {
    (void)buttons; assert(initialized); ++display_calls; return &input;
}
static bool m1_fb_copy_selection(char *dir, size_t dir_size, char *name, size_t name_size) {
    if (!input.dir_name || !input.file_name || strlen(input.dir_name)>=dir_size ||
        strlen(input.file_name)>=name_size) return false;
    strcpy(dir,input.dir_name); strcpy(name,input.file_name); return true;
}
static void m1_image_message(int a, int b, int c, int d) {
    (void)a; (void)b; (void)c; (void)d; ++errors;
}
static int sd_card_error_46x36, sdcard_access_error_message;
static int xQueueReceive(int queue, void *out, int timeout) {
    (void)timeout;
    if (queue == main_q_hdl) {
        assert(events < 4); ((S_M1_Main_Q_t *)out)->q_evt_type = Q_EVENT_KEYPAD;
    } else {
        S_M1_Buttons_Status *b = out; memset(b, 0, sizeof(*b));
        if (events++) b->event[BUTTON_BACK_KP_ID] = BUTTON_EVENT_CLICK;
    }
    return pdTRUE;
}
static int lfrfid_profile_load(S_M1_file_info *f, const char *ext) {
    (void)f; (void)ext; ++loads; return load_ok;
}
static uint16_t m1_fb_read_from_file(int *f, char *out, uint16_t size) {
    (void)f; size_t n = read_len < size ? read_len : size;
    if (n) memcpy(out, read_data, n);
    return (uint16_t)n;
}
'''

TESTS = r'''
static void storage_reset(void) {
    init_fail = initialized = display_calls = exits = events = errors = 0;
    sd_status = SD_access_OK;
    memset(info_filename, 'X', sizeof(info_filename));
    memset(info_filepath, 'Y', sizeof(info_filepath));
    input = (S_M1_file_info){"0:/RFID", "tag.rfid", true, FB_OK};
}
static void test_storage_oom(void) {
    storage_reset(); init_fail = 1;
    S_M1_file_info *result = storage_browse();
    assert(!result->file_is_selected && result->status != FB_OK);
    assert(!display_calls && exits == 1 && !events);
    assert(result->dir_name[0] == 0 && result->file_name[0] == 0);
}
static void test_storage_path(void) {
    for (size_t n = 1; n <= 130; ++n) {
        storage_reset();
        char *path = malloc(n + 1); assert(path);
        memset(path, 'd', n); path[n] = 0; input.dir_name = path;
        S_M1_file_info *result = storage_browse();
        if (n < sizeof(info_filepath)) {
            assert(result->file_is_selected && result->status == FB_OK);
            assert(!strcmp(result->dir_name, path));
            assert(!strcmp(result->file_name, "tag.rfid"));
            assert(errors == 0 && events == 1);
        } else {
            assert(!result->file_is_selected && result->status != FB_OK);
            assert(result->dir_name[0] == 0 && errors == 1 && events == 2);
        }
        assert(exits == 1); free(path);
    }
}
static void test_rfid(void) {
    const size_t dirs[] = {1, 7, 63, 64, 127, 128, 129};
    const size_t names[] = {1, 8, 31, 32, 33};
    for (size_t a = 0; a < sizeof(dirs)/sizeof(*dirs); ++a)
    for (size_t b = 0; b < sizeof(names)/sizeof(*names); ++b)
    for (int ok = 0; ok <= 1; ++ok) {
        char *dir = malloc(dirs[a] + 1), *name = malloc(names[b] + 1);
        assert(dir && name); memset(dir, 'd', dirs[a]); dir[dirs[a]] = 0;
        memset(name, 'n', names[b]); name[names[b]] = 0;
        input = (S_M1_file_info){dir, name, true, FB_OK};
        s_rfid_saved_launch.external = true;
        s_rfid_saved_launch.pending = true;
        s_rfid_saved_launch.file = input;
        memset(&lfrfid_tag_info, 0, sizeof(lfrfid_tag_info));
        strcpy(lfrfid_tag_info.filepath, "old-dir");
        strcpy(lfrfid_tag_info.filename, "old-name");
        errors = switches = loads = 0; load_ok = ok;
        lfrfid_saved_browse_update(0);
        bool fits = dirs[a] < 128 && names[b] < 32;
        assert(loads == (fits ? 1 : 0));
        if (fits && ok) {
            assert(switches == 1 && !errors);
            assert(!strcmp(lfrfid_tag_info.filepath, dir));
            assert(!strcmp(lfrfid_tag_info.filename, name));
        } else {
            assert(!switches && errors == 1);
            assert(!strcmp(lfrfid_tag_info.filepath, "old-dir"));
            assert(!strcmp(lfrfid_tag_info.filename, "old-name"));
        }
        free(dir); free(name);
    }
}
static void test_cli(void) {
    unsigned char data[100]; memset(data, 'Q', sizeof(data)); read_data = data;
    char *params[] = {"14", "100"};
    for (size_t n = 1; n <= 100; ++n)
    for (size_t reverse = 0; reverse <= 201; ++reverse) {
        size_t cap = 201 - reverse;
        char *out = malloc(cap + 1); assert(out); memset(out, 'Z', cap + 1);
        read_len = n;
        CALL_CLI;
        assert(out[cap] == 'Z');
        if (cap) {
            size_t expected = n < cap - 1 ? n : cap - 1;
            assert(strlen(out) == expected);
            assert(!memcmp(out, data, expected));
        }
        free(out);
    }
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "storage_oom")) test_storage_oom();
    else if (!strcmp(argv[1], "storage_path")) test_storage_path();
    else if (!strcmp(argv[1], "rfid")) test_rfid();
    else if (!strcmp(argv[1], "cli")) test_cli();
    else abort();
    printf("%s: PASS\n", argv[1]); return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", help="Read handlers from this git revision")
    args = parser.parse_args()
    storage = read_source("m1_csrc/m1_storage.c", args.ref)
    rfid = read_source("m1_csrc/m1_rfid.c", args.ref)
    cli = read_source("m1_csrc/m1_cli.c", args.ref)
    sd_fn = function(cli, "cmd_m1_mtest_sdcard")
    has_capacity = "size_t output_size" in sd_fn
    if not args.ref:
        assert "cmd_m1_mtest_sdcard(pconsole, xWriteBufferLen," in cli
    call = ("cmd_m1_mtest_sdcard(out, cap, params, 2, 14)" if has_capacity
            else "cmd_m1_mtest_sdcard(out, params, 2, 14)")
    source = PREAMBLE + function(storage, "storage_browse") + "\n"
    source += function(rfid, "lfrfid_saved_browse_update") + "\n" + sd_fn
    source += TESTS.replace("CALL_CLI", call)
    with tempfile.TemporaryDirectory(prefix="m1-pass1-host-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(source)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-g", "-O1",
                        "-Wall", "-Wextra", "-Wno-unused-variable",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-ftrivial-auto-var-init=pattern", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        failed = []
        for case in ("storage_oom", "storage_path", "rfid", "cli"):
            run = subprocess.run([str(path / "test"), case], capture_output=True, text=True)
            print(run.stdout, end="")
            if run.returncode:
                failed.append(case)
                print(f"{case}: FAIL ({run.returncode})")
                print(run.stderr[:1800])
        if failed:
            raise SystemExit(1)
    print("All four handler regressions PASS (ASan/UBSan).")


if __name__ == "__main__":
    main()
