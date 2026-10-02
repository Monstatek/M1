/* Host test for mfc_keys_ui_model.h -- the REAL production decision rules
 * the MIFARE Classic Keys UI (m1_nfc.c) calls directly, not a transcription.
 * Header-only static inline, zero HAL dependency, so this links and runs
 * unmodified on host (m1_nfc.c itself cannot: it is FreeRTOS/u8g2/HAL-
 * coupled throughout, so its screen-transition/draw code is exercised by
 * code review and the exact same functions this file tests, not by a
 * separate host binary).
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      mfc_keys_ui_model_test.c -I.. -o /tmp/mkuit && /tmp/mkuit
 */
#include "../mfc_keys_ui_model.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/*----------------------------------------------------------------------------*/
/* 1) Dashboard model: List visibility and system-label selection.            */
/*----------------------------------------------------------------------------*/
static void test_list_visibility(void)
{
    CHECK(!mfc_keys_ui_list_visible(0), "List hidden for zero user keys");
    CHECK(mfc_keys_ui_list_visible(1),  "List shown for exactly one user key");
    CHECK(mfc_keys_ui_list_visible(2511), "List shown for many user keys");
}

static void test_system_label(void)
{
    CHECK(mfc_keys_ui_system_label(MFC_PATH_PRESENT) == MFC_KEYS_UI_SYS_INSTALLED,
          "PRESENT -> show the exact count");
    CHECK(mfc_keys_ui_system_label(MFC_PATH_ABSENT) == MFC_KEYS_UI_SYS_NOT_INSTALLED,
          "ABSENT -> \"System: Not installed\"");
    CHECK(mfc_keys_ui_system_label(MFC_PATH_READ_ERROR) == MFC_KEYS_UI_SYS_READ_ERROR,
          "READ_ERROR -> \"System: Read error\"");
}

/*----------------------------------------------------------------------------*/
/* 2) Add: duplicate classification (the piece of the Add workflow that is    */
/* pure decision logic, independent of the hex editor / file I/O around it).  */
/*----------------------------------------------------------------------------*/
static void test_dup_kind(void)
{
    /* Mirrors mfc_keys_t's layout: built-ins at [0, builtin_count), user
     * keys after -- 2 built-ins, 2 user keys. */
    static const uint8_t keys[4][MFC_KEY_SIZE] = {
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },   /* builtin 0 */
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* builtin 1 */
        { 0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1 },   /* user 0 */
        { 0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2 },   /* user 1 */
    };
    const uint16_t count = 4, builtin_count = 2;

    uint8_t dup_builtin[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    CHECK(mfc_keys_ui_dup_kind(keys, count, builtin_count, dup_builtin) == MFC_KEYS_UI_DUP_BUILTIN,
          "a key equal to a built-in classifies as DUP_BUILTIN (\"Already included\")");

    uint8_t dup_user[6] = {0xA1,0xA1,0xA1,0xA1,0xA1,0xA1};
    CHECK(mfc_keys_ui_dup_kind(keys, count, builtin_count, dup_user) == MFC_KEYS_UI_DUP_USER,
          "a key equal to a saved user key classifies as DUP_USER (\"Already saved\")");

    /* Present ONLY in the (never-loaded-here) SD system dictionary: this
     * array structurally cannot contain it, so it must classify as NONE --
     * proving a system-only key is free to be promoted into the user
     * dictionary, exactly as the spec requires. */
    uint8_t system_only[6] = {0xC3,0xC3,0xC3,0xC3,0xC3,0xC3};
    CHECK(mfc_keys_ui_dup_kind(keys, count, builtin_count, system_only) == MFC_KEYS_UI_DUP_NONE,
          "a key absent from built-in+user classifies as DUP_NONE (may be added/promoted)");

    uint8_t brand_new[6] = {0xD4,0xD4,0xD4,0xD4,0xD4,0xD4};
    CHECK(mfc_keys_ui_dup_kind(keys, count, builtin_count, brand_new) == MFC_KEYS_UI_DUP_NONE,
          "a genuinely new key also classifies as DUP_NONE");

    /* Empty snapshot (no user dictionary loaded yet): every key is new. */
    CHECK(mfc_keys_ui_dup_kind(keys, 0, 0, brand_new) == MFC_KEYS_UI_DUP_NONE,
          "empty snapshot: every key classifies as DUP_NONE");
}

/*----------------------------------------------------------------------------*/
/* 3) List/Delete: navigation, selection clamping, target-index, and the      */
/* post-delete mode transition that hides List when the last key is gone.     */
/*----------------------------------------------------------------------------*/
static void test_nav_up_down(void)
{
    uint16_t sel = 2, scroll = 0;
    mfc_keys_ui_nav_up(&sel, &scroll);
    CHECK(sel == 1 && scroll == 0, "UP decrements selection without moving scroll when already visible");

    sel = 0; scroll = 0;
    mfc_keys_ui_nav_up(&sel, &scroll);
    CHECK(sel == 0, "UP at the top is a no-op");

    /* 4 visible rows; selection at row 3 (last visible), scroll at 0. DOWN
     * with 6 total items must both advance selection and scroll the window. */
    sel = 3; scroll = 0;
    mfc_keys_ui_nav_down(&sel, &scroll, /*count=*/6, /*visible_rows=*/4);
    CHECK(sel == 4 && scroll == 1, "DOWN past the visible window advances selection AND scroll");

    sel = 5; scroll = 2;   /* last item (index 5 of 6) */
    mfc_keys_ui_nav_down(&sel, &scroll, 6, 4);
    CHECK(sel == 5 && scroll == 2, "DOWN at the last item is a no-op (never runs past count)");

    /* UP pulling the scroll window back up once selection scrolls above it. */
    sel = 4; scroll = 1;
    mfc_keys_ui_nav_up(&sel, &scroll);
    CHECK(sel == 3 && scroll == 1, "UP within the visible window leaves scroll alone");
    mfc_keys_ui_nav_up(&sel, &scroll);   /* sel 3 -> 2, still within the scroll=1 window (indices 1..4) */
    CHECK(sel == 2 && scroll == 1, "UP still within the visible window: scroll unchanged");
}

static void test_clamp_selection(void)
{
    CHECK(mfc_keys_ui_clamp_selection(5, 10) == 5, "selection within range is unchanged");
    CHECK(mfc_keys_ui_clamp_selection(9, 10) == 9, "selection at the last valid index is unchanged");
    CHECK(mfc_keys_ui_clamp_selection(9, 9)  == 8, "selection past the new count clamps to count-1");
    CHECK(mfc_keys_ui_clamp_selection(0, 0)  == 0, "selection on an empty list clamps to 0");
    CHECK(mfc_keys_ui_clamp_selection(3, 0)  == 0, "any selection on an empty list clamps to 0");
}

static void test_target_index(void)
{
    CHECK(mfc_keys_ui_target_index(6, 0) == 6, "target index skips exactly builtin_count entries");
    CHECK(mfc_keys_ui_target_index(6, 4) == 10, "target index tracks the user-space selection");
    CHECK(mfc_keys_ui_target_index(0, 4) == 4,  "zero built-ins: target index equals the selection");
    /* Structural guarantee: for ANY non-negative user_sel, the target index
     * is always >= builtin_count, so it can never land on a built-in slot. */
    for (uint16_t builtin_count = 0; builtin_count < 8; builtin_count++) {
        for (uint16_t sel = 0; sel < 8; sel++) {
            CHECK(mfc_keys_ui_target_index(builtin_count, sel) >= builtin_count,
                  "target index is structurally never inside the built-in range");
        }
    }
}

static void test_mode_after_delete(void)
{
    CHECK(mfc_keys_ui_mode_after_delete(3, /*list=*/1, /*dashboard=*/0) == 1,
          "user keys remain -> stay on the list");
    CHECK(mfc_keys_ui_mode_after_delete(1, 1, 0) == 1,
          "exactly one user key remains -> stay on the list");
    CHECK(mfc_keys_ui_mode_after_delete(0, 1, 0) == 0,
          "the deleted key was the last one -> land on the dashboard (List hides itself, per list_visible(0))");
}

int main(void)
{
    test_list_visibility();
    test_system_label();
    test_dup_kind();
    test_nav_up_down();
    test_clamp_selection();
    test_target_index();
    test_mode_after_delete();

    printf("mfc_keys_ui_model_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
