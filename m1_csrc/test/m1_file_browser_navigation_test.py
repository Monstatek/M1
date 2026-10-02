#!/usr/bin/env python3
"""Executable host tests for the generic-Explorer BACK-navigation state
machine (root/depth determination, dir_level initialization for a non-root
start directory, and the ascend-one-level operation).

m1_file_browser.c pulls in u8g2/mui/STM32 HAL/FreeRTOS transitively and
cannot be host-built as a whole translation unit (same limitation as
m1_file_browser_empty_state_test.c / m1_file_browser_alloc_safety_test.c,
which fall back to source-text verification for that reason). The specific
functions this fix touches -- fb_is_root_path(), fb_dir_level_from_path(),
m1_fb_set_start_dir(), m1_fb_init(), m1_fb_navigate_back(), m1_fb_deinit(),
m1_fb_set_explorer_nav_enabled()/m1_fb_explorer_nav_active() -- do NOT touch
u8g2/mui drawing at all, so this file uses the same technique as
m1_wifi_persistence_hardening_test.py in this repo: extract the REAL,
unmodified function bodies (and the plain struct/#define/static-variable
declarations they depend on) with a source-text regex, and host-compile
them against a tiny FatFs/u8g2-field stand-in defined here -- so the actual
production control flow is what gets exercised, not a re-implementation of
it. Source-text grep is used only as a secondary check (in
m1_storage_menu_cleanup_test.c) for the parts of storage_browse()'s BACK
dispatch that remain HAL-coupled.

From the repository root: python3 m1_csrc/test/m1_file_browser_navigation_test.py
"""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text()


def function(text, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;{}]*\)\s*\n\{", text, re.M)
    assert match, name
    start = match.start()
    depth = 1
    body = text[match.end():]
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                             r'//[^\n]*|/\*[\s\S]*?\*/|[{}]', body):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
        if depth == 0:
            return text[start:match.end() + token.end()]
    raise AssertionError(name)


def define(text, name):
    m = re.search(r"^#define\s+" + name + r"\b.*$", text, re.M)
    assert m, name
    return m.group()


def line(text, pattern):
    m = re.search(pattern, text, re.M)
    assert m, pattern
    return m.group()


def flat_block(text, start_text, end_marker):
    """Extract from the first occurrence of start_text through the next
    occurrence of end_marker (inclusive), for a block with no nested braces
    of its own between them (a plain struct or a single statement)."""
    start = text.index(start_text)
    end = text.index(end_marker, start) + len(end_marker)
    return text[start:end]


def build_run(tmp, name, text):
    src = Path(tmp) / f"{name}.c"
    src.write_text(text)
    exe = Path(tmp) / name
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function",
                     "-fsanitize=address,undefined", "-g",
                     str(src), "-o", str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)


STUBS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

typedef char TCHAR;
typedef unsigned int UINT;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM, FR_MKFS_ABORTED,
    FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

/* Minimal FatFs DIR stand-in: m1_fb_navigate_back() only ever calls
 * f_closedir() on it, never inspects its fields. */
typedef struct { int dummy; } DIR;
static int g_closedir_calls = 0;
FRESULT f_closedir(DIR *dp) { (void)dp; g_closedir_calls++; return FR_OK; }

/* Minimal u8g2 stand-in: m1_fb_init() only reads ->width/->height. */
typedef struct { uint16_t width, height; } u8g2_t;
static void m1_u8g2_firstpage(void) {}

#define M1_GUI_FONT_WIDTH          6
#define M1_GUI_FONT_HEIGHT         10
#define M1_GUI_FONT_HEIGHT_SPACING 2

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)
'''


def navigation_tests(tmp):
    fb = source("m1_csrc/m1_file_browser.c")
    sdcard_h = source("m1_csrc/m1_sdcard.h")

    code = STUBS
    code += "\n" + define(sdcard_h, "SDCARD_DEFAULT_DRIVE_PATH")

    # S_M1_file_info (from m1_file_browser.h) -- extracted the same
    # "anchor on the closing marker" way as the WiFi task's simple_typedef,
    # applied by hand here since this one is a plain enum + struct pair,
    # not a single typedef.
    fb_h = source("m1_csrc/m1_file_browser.h")
    code += "\n" + flat_block(fb_h, "typedef enum\n{\n\tFB_OK = 0,", "} S_M1_file_browser_code;")
    code += "\n" + flat_block(fb_h, "typedef struct\n{\n\tchar *dir_name;", "} S_M1_file_info;")
    code += "\n" + flat_block(fb_h, "typedef enum\n{\n\tM1_FB_BACK_AT_ROOT = 0,", "} m1_fb_back_result_t;")

    # The real opaque handle struct + its one global instance + the shared
    # DIR handle m1_fb_navigate_back() closes -- all real, unmodified.
    code += "\n" + flat_block(fb, "struct S_M1_file_browser_hdl\n{", "};")
    code += "\ntypedef struct S_M1_file_browser_hdl S_M1_file_browser_hdl;\n"
    code += "\n" + line(fb, r"^static S_M1_file_browser_hdl \*pfb_hdl = NULL;$")
    code += "\n" + line(fb, r"^static u8g2_t \*plcd_hdl;$")
    code += "\n" + line(fb, r"^static DIR directory;$")
    code += "\n" + line(fb, r"^static bool fb_gui_check;$")
    code += "\n" + line(fb, r"^static char s_fb_start_dir\[128\];$")
    code += "\n" + line(fb, r"^static bool s_fb_start_dir_set = false;$")
    code += "\n" + function(fb, "m1_fb_set_start_dir")

    code += "\n" + line(fb, r"^static bool s_fb_explorer_nav_enabled = false;$")
    code += "\n" + function(fb, "m1_fb_set_explorer_nav_enabled")
    code += "\n" + function(fb, "m1_fb_explorer_nav_active")
    code += "\n" + function(fb, "fb_is_root_path")
    code += "\n" + function(fb, "fb_dir_level_from_path")

    # m1_fb_deinit() calls fb_sorted_free(), which touches s_fb_sorted /
    # s_fb_sorted_count -- pull in the real (tiny) versions of those too.
    code += "\ntypedef struct { int dummy; } fb_sorted_entry_t;\n"
    code += "\n" + line(fb, r"^static fb_sorted_entry_t \*s_fb_sorted\s*=\s*NULL;.*$")
    code += "\n" + line(fb, r"^static uint16_t\s*s_fb_sorted_count = 0;$")
    code += "\n" + function(fb, "fb_sorted_free")

    # m1_fb_deinit() also clears these two (real, unmodified) session flags.
    code += "\n" + line(fb, r"^static bool\s*s_fb_sort_enabled = false;$")
    code += "\n" + line(fb, r"^static bool s_fb_friendly_names = false;$")

    # m1_fb_init() calls m1_fb_deinit() on its own allocation-failure paths,
    # textually before m1_fb_deinit()'s definition -- forward-declare it,
    # same as the real m1_file_browser.c's own prototype section does.
    code += "\nvoid m1_fb_deinit(void);\n"

    code += "\n" + function(fb, "m1_fb_init")
    code += "\n" + function(fb, "m1_fb_navigate_back")
    code += "\n" + function(fb, "m1_fb_deinit")

    code += r'''
static u8g2_t lcd;

int main(void)
{
    lcd.width = 128; lcd.height = 64;

    /* ---- fb_is_root_path() / fb_dir_level_from_path(): pure logic ---- */
    CHECK(fb_is_root_path("0:/") == true, "0:/ is the root");
    CHECK(fb_is_root_path("0:") == true, "bare 0: (post-ascend form) is the root");
    CHECK(fb_is_root_path("0:/nfc") == false, "0:/nfc is not the root");
    CHECK(fb_is_root_path(NULL) == true, "NULL path is defensively treated as root");

    CHECK(fb_dir_level_from_path("0:/") == 0, "depth of 0:/ is 0");
    CHECK(fb_dir_level_from_path("0:") == 0, "depth of bare 0: is 0");
    CHECK(fb_dir_level_from_path("0:/nfc") == 1, "depth of 0:/nfc is 1");
    CHECK(fb_dir_level_from_path("0:/nfc/system") == 2, "depth of 0:/nfc/system is 2");
    CHECK(fb_dir_level_from_path("0:/a/b/c") == 3, "depth of 0:/a/b/c is 3");

    /* ---- 1: Generic Explorer at 0:/ -- BACK reports AT_ROOT ---- */
    m1_fb_set_explorer_nav_enabled(true);
    CHECK(m1_fb_init(&lcd) != NULL, "init at default root succeeds");
    CHECK(strcmp(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH) == 0, "session starts at the real root");
    CHECK(pfb_hdl->dir_level == 0, "dir_level is 0 at the real root");
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_AT_ROOT, "BACK at 0:/ reports AT_ROOT");
    CHECK(strcmp(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH) == 0, "dir_name unchanged after an AT_ROOT report");
    m1_fb_deinit();
    CHECK(m1_fb_explorer_nav_active() == false, "explorer-nav flag cleared on deinit");

    /* ---- 2: Generic Explorer at 0:/nfc (reopened, not manually descended) --
     * BACK opens 0:/ -- the exact reopened-folder-false-root bug this fix
     * targets. ---- */
    m1_fb_set_explorer_nav_enabled(true);
    m1_fb_set_start_dir("0:/nfc");
    CHECK(m1_fb_init(&lcd) != NULL, "init at a reopened non-root start dir succeeds");
    CHECK(strcmp(pfb_hdl->info.dir_name, "0:/nfc") == 0, "session starts at the reopened folder");
    /* --- 6: that reopened directory is NOT treated as the real root --- */
    CHECK(pfb_hdl->dir_level == 1, "dir_level correctly reflects real depth (1), not a false-root 0");
    {
        int before = g_closedir_calls;
        m1_fb_back_result_t r = m1_fb_navigate_back();
        CHECK(r == M1_FB_BACK_PARENT_OPENED, "BACK from the reopened folder ascends instead of exiting");
        CHECK(g_closedir_calls == before + 1, "ascend closes the open directory handle before reopening the parent");
    }
    CHECK(strcmp(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH) == 0,
          "ascending from 0:/nfc lands on canonical 0:/, not bare 0:");
    CHECK(pfb_hdl->dir_level == 0, "dir_level is 0 after ascending to the root");
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_AT_ROOT, "a second BACK at the root reports AT_ROOT, does not underflow");
    m1_fb_deinit();

    /* ---- 3: multiple levels deep -- each BACK ascends exactly one level ---- */
    m1_fb_set_explorer_nav_enabled(true);
    m1_fb_set_start_dir("0:/nfc/system");
    CHECK(m1_fb_init(&lcd) != NULL, "init two levels deep succeeds");
    CHECK(pfb_hdl->dir_level == 2, "dir_level correctly reflects depth 2");
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_PARENT_OPENED, "first BACK ascends");
    CHECK(strcmp(pfb_hdl->info.dir_name, "0:/nfc") == 0, "first BACK lands exactly one level up, not straight to root");
    CHECK(pfb_hdl->dir_level == 1, "dir_level decremented by exactly one");
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_PARENT_OPENED, "second BACK ascends again");
    CHECK(strcmp(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH) == 0,
          "second BACK reaches the canonical real root");
    CHECK(pfb_hdl->dir_level == 0, "dir_level is 0 at the root");
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_AT_ROOT, "third BACK reports AT_ROOT -- never traverses above 0:/");
    m1_fb_deinit();

    /* ---- 7 / preserve-elsewhere: a caller that never enables explorer-nav
     * mode (firmware pickers, NFC/RFID/Sub-GHz Saved browsers) keeps
     * dir_level == 0 even at a non-root start dir -- unchanged from today,
     * so their existing BACK-always-exits behavior is untouched. ---- */
    m1_fb_set_start_dir("0:/nfc");
    CHECK(m1_fb_init(&lcd) != NULL, "init without explorer-nav mode succeeds");
    CHECK(m1_fb_explorer_nav_active() == false, "explorer-nav mode is off for this session");
    CHECK(strcmp(pfb_hdl->info.dir_name, "0:/nfc") == 0, "still opens the requested start dir");
    CHECK(pfb_hdl->dir_level == 0, "dir_level stays 0 (unchanged legacy behavior) when explorer-nav mode is not enabled");
    m1_fb_deinit();

    /* ---- 9: repeated sessions cannot retain stale mode/path/depth state ---- */
    m1_fb_set_explorer_nav_enabled(true);
    m1_fb_set_start_dir("0:/nfc/system");
    CHECK(m1_fb_init(&lcd) != NULL, "deep session init succeeds");
    CHECK(pfb_hdl->dir_level == 2, "deep session starts at depth 2");
    m1_fb_deinit();
    /* No m1_fb_set_start_dir()/m1_fb_set_explorer_nav_enabled() this time:
     * a fresh session must not inherit the previous one's start dir, mode,
     * or depth -- both are one-shot/session-scoped and cleared above. */
    CHECK(m1_fb_init(&lcd) != NULL, "fresh session init succeeds");
    CHECK(strcmp(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH) == 0, "fresh session starts at the default root, not the prior session's deep path");
    CHECK(pfb_hdl->dir_level == 0, "fresh session starts at dir_level 0, not the prior session's depth");
    CHECK(m1_fb_explorer_nav_active() == false, "fresh session does not inherit the prior session's explorer-nav flag");
    m1_fb_deinit();

    /* ---- 10: allocation/error safety ---- */
    CHECK(m1_fb_navigate_back() == M1_FB_BACK_ERROR, "navigate_back with no active session reports ERROR, not a crash");

    printf("m1_file_browser navigation state machine: %d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
'''
    build_run(tmp, "fb_navigation_tests", code)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        navigation_tests(tmp)
    print("All file-browser navigation host tests PASS (ASan/UBSan).")
