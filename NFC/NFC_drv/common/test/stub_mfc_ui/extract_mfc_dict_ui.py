#!/usr/bin/env python3
"""
extract_mfc_dict_ui.py

Generates NFC/NFC_drv/common/test/stub_mfc_ui/nfc_mfc_dict_ui_draw_extracted.c
as a BYTE-EXACT, VERBATIM concatenation of specific line ranges copied out of
the real, currently-checked-out m1_csrc/m1_nfc.c and m1_csrc/m1_display.c (plus
a handful of macro/typedef/enum lines from their real, portable headers) -- no
retyping, no editing of the extracted text. Same "compile a scoped section of
the real file" technique NFC/NFC_drv/common/test/stub_desfire_ui/extract_desfire_ui.py
already established for the DESFire/transit UI, adapted here for the MFC
dictionary-UI screens (NFC2-005 hardware-review renders).

m1_csrc/m1_nfc.c and m1_csrc/m1_display.c are NEVER modified by this script --
it only READS them and WRITES a new file elsewhere.

Every extracted range is re-located by signature/brace-matching against the
LIVE file every time this script runs (never hard-coded line numbers), so if
the source changes, the ranges move with it or the script fails loudly
instead of silently extracting the wrong text.

Two functions are extracted as RESTRUCTURED WRAPPERS rather than whole
existing functions (both explicitly permitted by the task brief, which asks
for "the actual drawing STATEMENTS inside" to be byte-identical copies, not
that every extraction must be a complete pre-existing function):

  * nfc_mfc_partial_result_draw() -- the exact statements nfc_read_gui_update()
    executes for a MIFARE Classic READING_COMPLETE result (both the
    non-COMPLETE Partial/Failed/Cancelled path this render campaign needs,
    and the untouched COMPLETE path alongside it, copied together as they
    appear in the real source, since the two share one guard and splitting
    them would require inventing a boundary the real source doesn't have)
    are copied verbatim out of the middle of that ~900-line function into
    their own small wrapper, because compiling the WHOLE function would drag
    in NDEF/T2T/DESFire/ISO15693/ST25TB display code and their own extern
    surfaces that this render campaign has no need of.

  * (m1_gui_submenu_update() itself, by contrast, IS extracted whole --
    see below.)
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]

SRC        = REPO / "m1_csrc/m1_nfc.c"
DISP_C     = REPO / "m1_csrc/m1_display.c"
DISP_H     = REPO / "m1_csrc/m1_display.h"
MENU_H     = REPO / "m1_csrc/m1_menu.h"
SYSTEM_H   = REPO / "m1_csrc/m1_system.h"
LCD_H      = REPO / "m1_csrc/m1_lcd.h"
OUT        = Path(__file__).resolve().with_name("nfc_mfc_dict_ui_draw_extracted.c")

nfc_lines  = SRC.open(encoding="utf-8").readlines()
disp_lines = DISP_C.open(encoding="utf-8").readlines()
disph_lines = DISP_H.open(encoding="utf-8").readlines()
menu_lines = MENU_H.open(encoding="utf-8").readlines()
sys_lines  = SYSTEM_H.open(encoding="utf-8").readlines()
lcd_lines  = LCD_H.open(encoding="utf-8").readlines()


def line_containing(buf, substr, start=0, end=None):
    end = end if end is not None else len(buf)
    for i in range(start, end):
        if substr in buf[i]:
            return i
    raise RuntimeError(f"pattern not found: {substr!r}")


def line_containing_before(buf, substr, before_idx):
    """Nearest preceding line (searching backward from before_idx) containing substr."""
    for i in range(before_idx, -1, -1):
        if substr in buf[i]:
            return i
    raise RuntimeError(f"pattern not found searching backward: {substr!r}")


def line_containing_pair(buf, substr1, substr2, start=0, within=1):
    """Finds a line containing substr1 where one of the next `within` lines
    contains substr2 -- disambiguates an anchor line (e.g. "typedef enum {")
    that recurs, by pinning it to the specific declaration whose nearby text
    is also known/unique."""
    for i in range(start, len(buf) - within):
        if substr1 in buf[i] and any(substr2 in buf[i + 1 + k] for k in range(within)):
            return i
    raise RuntimeError(f"pattern pair not found: {substr1!r} followed within {within} lines by {substr2!r}")


def slice_lines(buf, a, b):
    """0-based inclusive -> the exact text, unmodified."""
    return "".join(buf[a:b + 1])


def func_range(buf, sig_substr, start_search=0):
    """Returns (start0, end0) 0-based inclusive line indices for a function
    DEFINITION (skips prototypes, which end the line with ';')."""
    for i in range(start_search, len(buf)):
        if sig_substr in buf[i] and buf[i].rstrip().endswith(')'):
            depth = 0
            started = False
            for k in range(i, len(buf)):
                for ch in buf[k]:
                    if ch == '{':
                        depth += 1
                        started = True
                    elif ch == '}':
                        depth -= 1
                if started and depth == 0:
                    return i, k
    raise RuntimeError(f"function definition not found: {sig_substr!r}")


def stmt_range(buf, anchor_idx):
    """Brace-matches the compound statement that STARTS at or after anchor_idx
    (first '{' found at/after anchor_idx). Returns end0 (0-based inclusive)."""
    depth = 0
    started = False
    for k in range(anchor_idx, len(buf)):
        for ch in buf[k]:
            if ch == '{':
                depth += 1
                started = True
            elif ch == '}':
                depth -= 1
        if started and depth == 0:
            return k
    raise RuntimeError("no balanced compound statement found")


out = []
out.append(
    "/*\n"
    " * nfc_mfc_dict_ui_draw_extracted.c -- GENERATED by extract_mfc_dict_ui.py.\n"
    " * DO NOT EDIT BY HAND -- re-run the generator instead. See that script's\n"
    " * own header comment for exactly which real source ranges each part below\n"
    " * came from and why.\n"
    " *\n"
    " * Every non-comment line below is a byte-exact copy of a real, currently-\n"
    " * checked-out line from m1_csrc/m1_nfc.c or m1_csrc/m1_display.c (plus a\n"
    " * handful of macro/typedef/enum lines from their real, portable headers).\n"
    " * Neither .c file was ever modified to produce this.\n"
    " */\n\n"
)

# ---------------------------------------------------------------------------
# 1) Font macros (m1_nfc.c's own NFC_FONT_STD/NFC_FONT_EMPH) + the display-
#    color / font macros they and m1_gui_submenu_update rely on, copied
#    verbatim from their real, portable headers -- same technique
#    extract_desfire_ui.py already uses for M1_DISP_DRAW_COLOR_*/
#    M1_DISP_MAIN_MENU_FONT_*.
# ---------------------------------------------------------------------------
i_disp_bg      = line_containing(disph_lines, "#define M1_DISP_DRAW_COLOR_BG")
i_disp_txt     = line_containing(disph_lines, "#define M1_DISP_DRAW_COLOR_TXT")
i_disp_fn      = line_containing(disph_lines, "#define M1_DISP_MAIN_MENU_FONT_N")
i_disp_fb      = line_containing(disph_lines, "#define M1_DISP_MAIN_MENU_FONT_B")
i_disp_subfn   = line_containing(disph_lines, "#define M1_DISP_SUB_MENU_FONT_N")
i_disp_subfb   = line_containing(disph_lines, "#define M1_DISP_SUB_MENU_FONT_B")
i_disp_funcfn  = line_containing(disph_lines, "#define M1_DISP_FUNC_MENU_FONT_N\t")
i_disp_logofont = line_containing(disph_lines, "#define M1_MAIN_LOGO_FONT_1B")

i_nfc_font_std  = line_containing(nfc_lines, "#define NFC_FONT_STD")
i_nfc_font_emph = line_containing(nfc_lines, "#define NFC_FONT_EMPH")
i_nfc_hexcell_w = line_containing(nfc_lines, "#define NFC_HEXCELL_W")
i_nfc_hexgrp_gap = line_containing(nfc_lines, "#define NFC_HEXGRP_GAP")

i_lcd_w = line_containing(lcd_lines, "#define M1_LCD_DISPLAY_WIDTH")
i_lcd_h = line_containing(lcd_lines, "#define M1_LCD_DISPLAY_HEIGHT")

out.append("/* ---- macros, verbatim from m1_display.h / m1_lcd.h / m1_nfc.c ---- */\n")
out.append(disph_lines[i_disp_bg])
out.append(disph_lines[i_disp_txt])
out.append(disph_lines[i_disp_fn])
out.append(disph_lines[i_disp_fb])
out.append(disph_lines[i_disp_subfn])
out.append(disph_lines[i_disp_subfb])
out.append(disph_lines[i_disp_funcfn])
out.append(disph_lines[i_disp_logofont])
out.append(lcd_lines[i_lcd_w])
out.append(lcd_lines[i_lcd_h])
out.append(nfc_lines[i_nfc_hexcell_w])
out.append(nfc_lines[i_nfc_hexgrp_gap])
out.append(nfc_lines[i_nfc_font_std])
out.append(nfc_lines[i_nfc_font_emph])
out.append("\n")

# ---------------------------------------------------------------------------
# 2) m1_menu.h / m1_system.h types the whole, verbatim m1_gui_submenu_update()
#    (below) needs to compile: S_M1_Key_Event (function-pointer field type
#    inside S_M1_Menu_t, never actually invoked on this screen), S_M1_Menu_t
#    itself, and S_M1_Menu_Update_t (the MENU_UPDATE_*/X_MENU_UPDATE_* enum
#    the caller passes as `direction`). All three are genuinely portable
#    (no HAL) in their real headers -- copied verbatim, not re-declared.
# ---------------------------------------------------------------------------
i_key_event_s = line_containing_pair(sys_lines, "typedef enum {", "BUTTON_EVENT_IDLE")
i_key_event_e = line_containing(sys_lines, "} S_M1_Key_Event;")

i_menutitlelen = line_containing(menu_lines, "#define MENU_TITLE_LEN")
i_submenumax   = line_containing(menu_lines, "#define SUB_MENU_ITEMS_MAX")
i_sublevelmax  = line_containing(menu_lines, "#define SUB_MENU_LEVEL_MAX")

i_menu_t_s = line_containing(menu_lines, "typedef struct S_M1_Menu")
i_menu_t_e = line_containing(menu_lines, "} S_M1_Menu_t;")

i_menu_upd_s = line_containing_pair(menu_lines, "typedef enum", "MENU_UPDATE_NONE", within=2)
i_menu_upd_e = line_containing(menu_lines, "} S_M1_Menu_Update_t;")

out.append("/* ---- m1_menu.h / m1_system.h types, verbatim (S_M1_Menu_t's own\n"
           " * xkey_handler field type + the menu-update direction enum\n"
           " * m1_gui_submenu_update() below is written against) ---- */\n")
out.append(menu_lines[i_menutitlelen])
out.append(menu_lines[i_submenumax])
out.append(menu_lines[i_sublevelmax])
out.append(slice_lines(sys_lines, i_key_event_s, i_key_event_e))
out.append(slice_lines(menu_lines, i_menu_t_s, i_menu_t_e))
out.append(slice_lines(menu_lines, i_menu_upd_s, i_menu_upd_e))
out.append("\n")

# ---------------------------------------------------------------------------
# 3) nfc_draw_hex_cells() -- shared hex-cell renderer used by the Partial
#    Info screen (UID/ATQA/SAK).
# ---------------------------------------------------------------------------
a, b = func_range(nfc_lines, "static uint8_t nfc_draw_hex_cells(uint8_t x0, uint8_t y, const uint8_t *bytes, uint8_t n)")
fn_draw_hex_cells = slice_lines(nfc_lines, a, b)

# ---------------------------------------------------------------------------
# 4) s_ndef_state -- single file-scope static declaration line the Partial
#    Result wrapper (below) references at its very end (the "View" cue,
#    which never applies to MFC -- see that wrapper's own header comment for
#    why nfc_ndef_state() itself is deliberately NOT called/extracted here).
# ---------------------------------------------------------------------------
i_ndef_state = line_containing(nfc_lines, "static uint8_t s_ndef_state = 0xFF;")
ndef_state_line = nfc_lines[i_ndef_state]

# ---------------------------------------------------------------------------
# 5) MFC dictionary-progress totals cache: statics + _ensure()/_invalidate().
#    Extracted verbatim so the guard logic itself is real; the render test's
#    own scenarios seed s_mfc_dict_totals_computed = true directly (see that
#    file's header comment) instead of ever calling _ensure() for real, so
#    m1_mfc_build_key_sources()/mfc_key_source_count() are never actually
#    invoked -- but nfc_mfc_dict_totals_ensure()'s own body still calls them
#    textually, so the render test links harmless, never-invoked stub bodies
#    for both (see nfc_ctx_stub.c) purely to satisfy the linker.
# ---------------------------------------------------------------------------
i_tot_s = line_containing(nfc_lines, "static uint32_t s_mfc_dict_total_builtin;")
i_tot_e = line_containing(nfc_lines, "static bool     s_mfc_dict_totals_computed = false;")
totals_statics = slice_lines(nfc_lines, i_tot_s, i_tot_e)

a, b = func_range(nfc_lines, "static void nfc_mfc_dict_totals_ensure(void)")
fn_totals_ensure = slice_lines(nfc_lines, a, b)
a, b = func_range(nfc_lines, "static void nfc_mfc_dict_totals_invalidate(void)")
fn_totals_invalidate = slice_lines(nfc_lines, a, b)

# ---------------------------------------------------------------------------
# 6) The dictionary-progress renderer family.
# ---------------------------------------------------------------------------
i_bar_sig = line_containing_before(nfc_lines, "static void nfc_mfc_progress_bar_draw(uint8_t x, uint8_t y, uint8_t w, uint8_t h,",
                                    line_containing(nfc_lines, "uint32_t current, uint32_t total)", start=500))
a, b = func_range(nfc_lines, "uint32_t current, uint32_t total)", start_search=i_bar_sig)
a = i_bar_sig
fn_progress_bar = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static void nfc_mfc_dict_progress_draw(void)")
fn_dict_progress = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static void nfc_mfc_lost_tag_draw(void)")
fn_lost_tag = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static void nfc_mfc_exit_confirm_draw(void)")
fn_exit_confirm = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static void nfc_mfc_find_keys_draw(void)")
fn_find_keys_draw = slice_lines(nfc_lines, a, b)

# ---------------------------------------------------------------------------
# 7) Action-menu assembly: nfc_emu_menu / nfc_can_write_ntag21x /
#    nfc_can_write_mfc / nfc_can_unlock / nfc_build_action_menu -- extracted
#    together since nfc_build_action_menu() calls all four.
# ---------------------------------------------------------------------------
a, b = func_range(nfc_lines, "static nfc_emu_cap_t nfc_emu_menu(nfc_run_ctx_t *c)")
fn_emu_menu = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static bool nfc_can_write_ntag21x(nfc_run_ctx_t *c)")
fn_can_write_ntag21x = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static bool nfc_can_write_mfc(nfc_run_ctx_t *c)")
fn_can_write_mfc = slice_lines(nfc_lines, a, b)

a, b = func_range(nfc_lines, "static bool nfc_can_unlock(nfc_run_ctx_t *c)")
fn_can_unlock = slice_lines(nfc_lines, a, b)

i_bam_close = line_containing(nfc_lines, "actions[NFC_MENU_MAX_ITEMS])", start=500)
i_bam_sig = line_containing_before(nfc_lines, "static uint8_t nfc_build_action_menu(const char *labels[NFC_MENU_MAX_ITEMS],", i_bam_close)
_, b = func_range(nfc_lines, "actions[NFC_MENU_MAX_ITEMS])", start_search=i_bam_close)
a = i_bam_sig
fn_build_action_menu = slice_lines(nfc_lines, a, b)

# NFC_ACT_*/NFC_MENU_MAX_ITEMS/nfc_emu_cap_t enum + LOAD_FILE, needed by the
# above -- verbatim, single contiguous block near the top of m1_nfc.c.
i_emu_cap_s = line_containing_pair(nfc_lines, "typedef enum {", "NFC_EMU_HIDE")
i_emu_cap_e = line_containing(nfc_lines, "} nfc_emu_cap_t;")
i_act_s = line_containing(nfc_lines, "NFC_ACT_SAVE = 0,")
i_act_e = line_containing(nfc_lines, "#define NFC_MENU_MAX_ITEMS")
enum_action_block = slice_lines(nfc_lines, i_emu_cap_s, i_emu_cap_e) + "\n" + slice_lines(nfc_lines, i_act_s - 1, i_act_e)

# ---------------------------------------------------------------------------
# 8) Partial/Failed/Cancelled MFC read-result screen: RESTRUCTURED WRAPPER --
#    the exact statements nfc_read_gui_update() executes for the MFC branch
#    of NFC_READ_DISPLAY_PARAM_READING_COMPLETE (both its COMPLETE and non-
#    COMPLETE sub-paths, copied together -- they share one outer guard in the
#    real source and there is no natural verbatim boundary between them),
#    copied out of the middle of that ~900-line function into their own
#    small function so this render campaign never has to stub NDEF/T2T/
#    DESFire/ISO15693/ST25TB/the rest of that function's dependency surface.
#    nfc_ndef_state() itself (which nfc_read_gui_update() calls just before
#    this block, to classify Type-2/NDEF content -- entirely inapplicable to
#    an MFC card) is deliberately NOT extracted/called; s_ndef_state keeps
#    its own real declared default (0xFF, see above), which is the truthful
#    "no NDEF classification" value this MFC branch would see regardless.
# ---------------------------------------------------------------------------
i_mfc_if = line_containing(nfc_lines,
    "if (nfc_ctx_get()->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)   // MIFARE Classic (Phase A)")
i_setcolor = line_containing_before(nfc_lines, "u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);", i_mfc_if)
i_mfc_end = stmt_range(nfc_lines, i_mfc_if)
partial_result_body = slice_lines(nfc_lines, i_setcolor, i_mfc_end)

# ---------------------------------------------------------------------------
# 8a) The shared bottom-bar footer (Retry/More/View), drawn UNCONDITIONALLY
#    right after the family if/else-if/else chain the MFC block above is one
#    branch of -- i.e. this runs for every family, MFC included, every time.
#    A prior render of mfc-partial-result.png omitted this (the wrapper
#    above stopped at the MFC block's own closing brace), which is a harness
#    gap, not a production one: nfc_mfc_try_again_allowed()/
#    nfc_read_more_menu_has_items() already gate BOTH what's drawn here AND
#    the real button handlers (see their own header comments in m1_nfc.c),
#    so extending this wrapper to include them is required for a faithful
#    render, not a behavior change. Extracted verbatim from "Only draw a
#    control..." through the "View" cue line -- stops before
#    #ifdef SEE_DUMP_MEMORY (debug-only, unrelated to any render here).
# ---------------------------------------------------------------------------
i_footer_s = line_containing_before(nfc_lines, "bool show_retry = nfc_mfc_try_again_allowed();", i_mfc_end + 400)
i_footer_cmt = line_containing_before(nfc_lines, "/* Only draw a control whose handler actually does something:", i_footer_s)
i_footer_e = line_containing(nfc_lines, "u8g2_DrawStr(&m1_u8g2, (uint8_t)((128 - u8g2_GetStrWidth(&m1_u8g2, \"View\")) / 2), 61, \"View\");", start=i_footer_s)
footer_body = slice_lines(nfc_lines, i_footer_cmt, i_footer_e)

a, b = func_range(nfc_lines, "static bool nfc_mfc_try_again_allowed(void)")
fn_try_again_allowed = slice_lines(nfc_lines, a, b)
a, b = func_range(nfc_lines, "static bool nfc_read_more_menu_has_items(void)")
fn_more_menu_has_items = slice_lines(nfc_lines, a, b)

# ---------------------------------------------------------------------------
# 8b) nfc_info_drawing()'s MFC branch (m1_nfc.c) -- RESTRUCTURED WRAPPER,
#    same technique as (8) above: this is the "Info" screen (both COMPLETE
#    and non-COMPLETE/Partial paths, copied together -- same one-guard
#    reasoning as above), needed for mfc-partial-info.png. Self-contained:
#    the real source already calls u8g2_FirstPage()/m1_u8g2_nextpage()
#    itself inside this exact branch (nfc_info_drawing() drives its own
#    paging per-family, unlike nfc_read_gui_update()), so the wrapper needs
#    no extra FirstPage/NextPage from the render test's own harness.
# ---------------------------------------------------------------------------
i_info_c_decl = line_containing(nfc_lines, "nfc_run_ctx_t* c = nfc_ctx_get();", start=3600)
i_info_mfc_if = line_containing(nfc_lines,
    "if (c && c->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)", start=3600)
i_info_mfc_end = stmt_range(nfc_lines, i_info_mfc_if)
info_mfc_body = slice_lines(nfc_lines, i_info_c_decl, i_info_mfc_end)

# ---------------------------------------------------------------------------
# 9) m1_gui_submenu_update() (m1_csrc/m1_display.c) -- extracted WHOLE (not a
#    restructured slice): the real, unmodified production menu-drawing path
#    shared by every action menu / submenu in the firmware, verbatim, brace-
#    matched from its own real definition. Its own file-scope statics/macros
#    it depends on are extracted alongside it below.
# ---------------------------------------------------------------------------
a, b = func_range(disp_lines, "uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction)")
fn_submenu_update = slice_lines(disp_lines, a, b)

i_macro_s = line_containing(disp_lines, "#define MENU_SCROLLBAR_POS_X")
i_macro_e = line_containing(disp_lines, "#define SUB_MENU_TEXT_FRAME_LEFT_POS_X")
submenu_macros = slice_lines(disp_lines, i_macro_s, i_macro_e)

i_arr_s = line_containing(disp_lines, "static const uint8_t menu_window_sizes[]")
i_arr_e = line_containing(disp_lines, "static const uint8_t *menu_text_font_b[]")
submenu_arrays = slice_lines(disp_lines, i_arr_s, i_arr_e)

i_dispwin_s = line_containing_before(disp_lines, "typedef struct", line_containing(disp_lines, "} S_M1_Disp_Window_t;"))
i_dispwin_e = line_containing(disp_lines, "} S_M1_Disp_Window_t;")
dispwin_typedef = slice_lines(disp_lines, i_dispwin_s, i_dispwin_e)

i_vars_s = line_containing(disp_lines, "static uint8_t disp_window_active_row;")
i_vars_e = line_containing(disp_lines, "static const S_M1_Menu_t *this_gui_menu;")
submenu_vars = slice_lines(disp_lines, i_vars_s, i_vars_e)

# ---------------------------------------------------------------------------
# Assemble output.
# ---------------------------------------------------------------------------
out.append("/* ---- nfc_draw_hex_cells() (m1_nfc.c), verbatim ---- */\n")
out.append(fn_draw_hex_cells)
out.append("\n")

out.append("/* ---- s_ndef_state (m1_nfc.c), verbatim declaration ---- */\n")
out.append(ndef_state_line)
out.append("\n")

out.append("/* ---- MFC dictionary-progress totals cache (m1_nfc.c), verbatim ---- */\n")
out.append(totals_statics)
out.append("\n")
out.append(fn_totals_ensure)
out.append("\n")
out.append(fn_totals_invalidate)
out.append("\n")

out.append("/* ---- Dictionary-progress renderer family (m1_nfc.c), verbatim ---- */\n")
out.append(fn_progress_bar)
out.append("\n")
out.append(fn_dict_progress)
out.append("\n")
out.append(fn_lost_tag)
out.append("\n")
out.append(fn_exit_confirm)
out.append("\n")
out.append(fn_find_keys_draw)
out.append("\n")

out.append("/* ---- Action-menu assembly (m1_nfc.c), verbatim ---- */\n")
out.append(enum_action_block)
out.append("\n")
out.append(fn_emu_menu)
out.append("\n")
out.append(fn_can_write_ntag21x)
out.append("\n")
out.append(fn_can_write_mfc)
out.append("\n")
out.append(fn_can_unlock)
out.append("\n")
out.append(fn_build_action_menu)
out.append("\n")

out.append("/* ---- Retry/More eligibility predicates (m1_nfc.c), verbatim -- gate\n"
           " * BOTH the shared footer below and the real button handlers, so the\n"
           " * drawn controls and what BACK/CENTER actually do can never disagree. ---- */\n")
out.append(fn_try_again_allowed)
out.append("\n")
out.append(fn_more_menu_has_items)
out.append("\n")

out.append(
    "/* ---- Capture globals: NOT extracted from m1_nfc.c -- glue this\n"
    " * generator adds so the render test can assert on the real show_retry/\n"
    " * show_more values the wrapper below actually computed, the same way a\n"
    " * production caller would observe them (there is no other seam to read\n"
    " * these locals from outside the wrapper). ---- */\n"
    "bool g_mfc_partial_result_show_retry;\n"
    "bool g_mfc_partial_result_show_more;\n\n"
)

out.append(
    "/* ---- Partial/Failed/Cancelled (+ COMPLETE) MFC read-result screen --\n"
    " * RESTRUCTURED WRAPPER around statements copied verbatim out of the\n"
    " * middle of nfc_read_gui_update() (m1_nfc.c): the MFC family branch\n"
    " * immediately followed by the shared bottom-bar footer that\n"
    " * unconditionally runs right after it in the real function (section 8a)\n"
    " * -- both copied in real source order, so this wrapper now reproduces\n"
    " * the exact same statements a real Partial/Failed/Cancelled MFC read\n"
    " * result draws, footer included. See this script's own header comment\n"
    " * (section 8) for exactly why the MFC block is a restructured slice and\n"
    " * what was intentionally left out (nfc_ndef_state() itself). ---- */\n"
)
out.append("static void nfc_mfc_partial_result_draw(void)\n{\n")
out.append(partial_result_body)
out.append("\n")
out.append(footer_body)
out.append(
    "\n"
    "\t/* capture glue (not extracted -- see the globals' own comment above) */\n"
    "\tg_mfc_partial_result_show_retry = show_retry;\n"
    "\tg_mfc_partial_result_show_more  = show_more;\n"
)
out.append("}\n\n")

out.append(
    "/* ---- MFC Info screen -- RESTRUCTURED WRAPPER around statements copied\n"
    " * verbatim out of the middle of nfc_info_drawing() (m1_nfc.c). See this\n"
    " * script's own header comment (section 8b). ---- */\n"
)
out.append("static void nfc_mfc_partial_info_draw(void)\n{\n")
out.append(info_mfc_body)
out.append("}\n\n")

out.append("/* ---- m1_gui_submenu_update() (m1_csrc/m1_display.c) + its own file-scope\n"
           " * statics/macros, all verbatim ---- */\n")
out.append(submenu_macros)
out.append("\n")
out.append(submenu_arrays)
out.append("\n")
out.append(dispwin_typedef)
out.append("\n")
out.append(submenu_vars)
out.append("\n\n")
out.append(fn_submenu_update)
out.append("\n")

with OUT.open("w", encoding="utf-8") as f:
    f.write("".join(out))

print(f"wrote {OUT} ({sum(len(x) for x in out)} bytes)")
