#!/usr/bin/env python3
"""
extract_desfire_ui.py

Generates NFC/NFC_drv/common/test/stub_desfire_ui/nfc_desfire_ui_draw_extracted.c
as a BYTE-EXACT, VERBATIM concatenation of specific line ranges copied out of
the real, currently-checked-out m1_csrc/m1_nfc.c -- no retyping, no editing of
the extracted text. This is the "compile a scoped section of the real file"
technique explicitly permitted by the task brief, chosen over a full-TU
compile of m1_nfc.c because the whole file pulls in ~150+ distinct externs
(FreeRTOS, HAL, uiView menu framework, file browser, storage, mfc raw/
capture/detect, harvest, T2T unlock, virtual keyboard, etc.) that the six
draw functions themselves never call -- see the session's symbol census.

m1_csrc/m1_nfc.c ITSELF IS NEVER MODIFIED. This script only READS it and
WRITES a new file elsewhere.

Each extracted range is re-verified against the live file's line numbers
every time this script runs (it locates functions by signature + brace
matching, not hard-coded line numbers), so if m1_nfc.c changes, the ranges
move with it or the script fails loudly instead of silently extracting the
wrong text.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
SRC = REPO / "m1_csrc/m1_nfc.c"
DISPLAY_H = REPO / "m1_csrc/m1_display.h"
OUT = Path(__file__).resolve().with_name("nfc_desfire_ui_draw_extracted.c")

lines = SRC.open().readlines()
disp_lines = DISPLAY_H.open().readlines()

def line_containing(substr, start=0, end=None, must_equal=False):
    end = end or len(lines)
    for i in range(start, end):
        if substr in lines[i]:
            return i  # 0-based
    raise RuntimeError(f"pattern not found: {substr!r}")

def func_range(sig_substr, start_search=3500):
    """Returns (start0, end0) 0-based inclusive line indices for a function
    DEFINITION (skips prototypes, which end the line with ';')."""
    for i in range(start_search, len(lines)):
        if sig_substr in lines[i] and lines[i].rstrip().endswith(')'):
            depth = 0
            started = False
            for k in range(i, len(lines)):
                for ch in lines[k]:
                    if ch == '{':
                        depth += 1
                        started = True
                    elif ch == '}':
                        depth -= 1
                if started and depth == 0:
                    return i, k
    raise RuntimeError(f"function definition not found: {sig_substr!r}")

def slice_lines(a, b):
    """0-based inclusive -> the exact text, unmodified."""
    return "".join(lines[a:b+1])

# ---- macro/define lines (single lines, verbatim) ----
i_info_lines   = line_containing("#define NFC_INFO_LINES_PER_SCREEN")
i_visible_rows = line_containing("#define NFC_DESF_LIST_VISIBLE_ROWS")
i_font_std     = line_containing("#define NFC_FONT_STD    M1_DISP_MAIN_MENU_FONT_N")
i_font_emph    = line_containing("#define NFC_FONT_EMPH   M1_DISP_MAIN_MENU_FONT_B")

# M1_DISP_DRAW_COLOR_* / M1_DISP_MAIN_MENU_FONT_* are defined in the real,
# unmodified m1_csrc/m1_display.h (a different real production file than
# m1_nfc.c) -- copied verbatim from there too, same technique.
def line_containing_in(buf, substr):
    for i, l in enumerate(buf):
        if substr in l:
            return i
    raise RuntimeError(f"pattern not found in m1_display.h: {substr!r}")

i_disp_bg   = line_containing_in(disp_lines, "#define M1_DISP_DRAW_COLOR_BG")
i_disp_txt  = line_containing_in(disp_lines, "#define M1_DISP_DRAW_COLOR_TXT")
i_disp_fn   = line_containing_in(disp_lines, "#define M1_DISP_MAIN_MENU_FONT_N")
i_disp_fb   = line_containing_in(disp_lines, "#define M1_DISP_MAIN_MENU_FONT_B")

macros_block = (
    disp_lines[i_disp_bg] +
    disp_lines[i_disp_txt] +
    disp_lines[i_disp_fn] +
    disp_lines[i_disp_fb] +
    lines[i_info_lines] +
    lines[i_visible_rows] +
    lines[i_font_std] +
    lines[i_font_emph]
)

# ---- file-scope static state (verbatim contiguous block, lines 184-200 area) ----
i_statics_start = line_containing("static uint8_t s_desf_scroll = 0;")
i_statics_end   = line_containing("static uint8_t s_desf_summary_line_count = 0;")
statics_block = slice_lines(i_statics_start, i_statics_end)

# ---- nfc_desf_draw_str_fit (helper the 6 draw functions call) ----
a, b = func_range("static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)")
fn_draw_str_fit = slice_lines(a, b)

# ---- the six draw functions under test ----
fn_names = [
    "static void nfc_desfire_summary_draw(void)",
    "static void nfc_desfire_data_draw(void)",
    "static void nfc_desfire_transit_draw(void)",
    "static void nfc_desfire_apps_draw(void)",
    "static void nfc_desfire_app_detail_draw(void)",
    "static void nfc_desfire_files_draw(void)",
    "static void nfc_desfire_file_detail_draw(void)",
]
fn_blocks = []
ranges_report = []
for name in fn_names:
    a, b = func_range(name)
    fn_blocks.append(slice_lines(a, b))
    ranges_report.append((name, a+1, b+1))  # report as 1-based

header = f'''/* GENERATED FILE -- DO NOT HAND-EDIT.
 *
 * nfc_desfire_ui_draw_extracted.c
 *
 * A byte-exact, verbatim concatenation of specific line ranges copied
 * directly out of the REAL, currently-checked-out m1_csrc/m1_nfc.c by
 * extract_desfire_ui.py. Every line below is production source text,
 * character-for-character identical to what ships in m1_nfc.c today --
 * nothing here was retyped, paraphrased, or hand-adjusted. m1_csrc/m1_nfc.c
 * itself was never opened for writing; this file is a NEW, separate file.
 *
 * WHY A SLICE, NOT THE WHOLE FILE: m1_csrc/m1_nfc.c is ~7100 lines and,
 * across its full body (menu create/destroy handlers, keypad handlers,
 * MFC raw capture/detect, harvest, T2T unlock, virtual keyboard, file
 * browser, storage, NDEF, RFAL driver/listener/poller glue), references
 * on the order of 150 distinct externs from FreeRTOS/CMSIS-RTOS2, the
 * STM32 HAL, and M1's uiView menu framework -- none of which the six
 * drawing functions below actually call. Stubbing that whole surface to
 * satisfy the compiler for code paths irrelevant to on-screen pixel
 * geometry would be a disproportionate, error-prone undertaking distinct
 * from this task's actual question (does the real drawing code overlap
 * pixels). Extracting the exact, unmodified function bodies that DO matter
 * and linking them against the REAL u8g2 library and REAL nfc_ctx/
 * mf_desfire_parse/nfc_transit_clipper implementations (all genuinely
 * portable, HAL-free C, per the existing stub_desfire/common/nfc_ctx.h
 * precedent) gets real, unmodified drawing logic onto a real pixel buffer
 * without inventing a schematic re-implementation of it.
 *
 * Extracted from: {SRC.relative_to(REPO)}
 *       and (for the 4 M1_DISP_* macros only): {DISPLAY_H.relative_to(REPO)}
 * Ranges (1-based, inclusive, as of extraction time):
 *   M1_DISP_DRAW_COLOR_BG/TXT define   : lines {i_disp_bg+1}, {i_disp_txt+1} (in m1_display.h)
 *   M1_DISP_MAIN_MENU_FONT_N/B define  : lines {i_disp_fn+1}, {i_disp_fb+1} (in m1_display.h)
 *   NFC_INFO_LINES_PER_SCREEN define   : line {i_info_lines+1}
 *   NFC_DESF_LIST_VISIBLE_ROWS define  : line {i_visible_rows+1}
 *   NFC_FONT_STD / NFC_FONT_EMPH defines: lines {i_font_std+1}-{i_font_emph+1}
 *   file-scope static nav/scroll state : lines {i_statics_start+1}-{i_statics_end+1}
'''
for name, a1, b1 in [("nfc_desf_draw_str_fit", func_range("static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)")[0]+1, func_range("static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)")[1]+1)]:
    header += f' *   {name:<32}: lines {a1}-{b1}\n'
for (name, a1, b1) in ranges_report:
    short = name.split("(")[0].replace("static void ", "")
    header += f' *   {short:<32}: lines {a1}-{b1}\n'
header += ''' *
 * If a future edit to m1_nfc.c changes these functions, re-run
 * extract_desfire_ui.py to regenerate this file with the new text -- do not
 * hand-patch this file to "keep it working"; that would defeat its purpose
 * as proof of the REAL shipped code.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "mf_desfire_parse.h"
#include "nfc_transit_clipper.h"
#include "nfc_ctx.h"          /* the existing stub_desfire/common structurally-faithful stub */
/* m1_u8g2 / arrowright_8x8 / target_10x10 are declared+defined by whichever
 * driver .c #includes this generated file into its own translation unit
 * (see nfc_desfire_ui_render_test.c) -- done that way, rather than via a
 * header here, because this file is only ever consumed as an #include,
 * never compiled standalone. */

/* ---- macros copied verbatim from m1_nfc.c (see header comment above) ---- */
''' + macros_block + '''
/* ---- file-scope static nav/scroll state, copied verbatim from m1_nfc.c ---- */
''' + statics_block + '''
/* ---- nfc_desf_draw_str_fit(), copied verbatim from m1_nfc.c ---- */
''' + fn_draw_str_fit + "\n"

body = "\n".join(fn_blocks) + "\n"

with OUT.open("w") as f:
    f.write(header)
    f.write("\n/* ---- the six draw functions under test, copied verbatim from m1_nfc.c ---- */\n")
    f.write(body)

print("wrote", OUT)
for name, a1, b1 in ranges_report:
    print(f"  {name}: lines {a1}-{b1}")
print(f"  statics: lines {i_statics_start+1}-{i_statics_end+1}")
