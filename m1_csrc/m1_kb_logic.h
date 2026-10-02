/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    m1_kb_logic.h
 * @brief   Pure (host-testable) state logic shared by the M1 on-screen keyboards
 *          (m1_virtual_kb.c). No drawing, no RTOS -- only the grid-navigation
 *          and edit-buffer transitions, so the GLOBAL keyboard rules can be unit
 *          tested off-device and used identically by every keyboard.
 *
 * GLOBAL RULES encoded here:
 *   1. Horizontal wrap: LEFT from the first column selects the last column of
 *      the same row; RIGHT from the last column selects the first column.
 *   2. Generated-default filename: while the "generated" flag is set, one
 *      Backspace clears the WHOLE default, and the first typed character
 *      replaces it -- then normal per-character editing resumes.
 */
/*============================================================================*/
#ifndef M1_KB_LOGIC_H_
#define M1_KB_LOGIC_H_

#include <stdint.h>

/* Horizontal keyboard-grid move WITH WRAP. dir>0 = RIGHT, dir<0 = LEFT.
 * col 0 + LEFT -> ncols-1 ; col ncols-1 + RIGHT -> 0. */
static inline uint8_t m1_kb_col_wrap(uint8_t col, int dir, uint8_t ncols)
{
    if (ncols == 0u) return 0u;
    if (dir > 0) return (uint8_t)((col + 1u) % ncols);
    return (uint8_t)((col + ncols - 1u) % ncols);
}

/* Fixed-width hex/data field (the vkbs editor): write hex char c at the current
 * position, then advance one position (skipping a display ' ' separator),
 * clamped at the last position. Returns the new position. `len` is the field
 * width (number of editable chars); MUST be > 0. */
static inline uint8_t m1_kb_data_put(char* buf, uint8_t pos, uint8_t len, char c)
{
    buf[pos] = c;
    if (len && (pos < (uint8_t)(len - 1u))) {
        pos++;
        if ((buf[pos] == ' ') && (pos < (uint8_t)(len - 1u))) pos++;
    }
    return pos;
}

/* Fixed-width hex/data field backspace: clear the current position to '0', then
 * step back one (skipping a display ' ' separator). Returns the new position. */
static inline uint8_t m1_kb_data_bs(char* buf, uint8_t pos)
{
    buf[pos] = '0';
    if (pos > 0u) {
        pos--;
        if ((buf[pos] == ' ') && (pos > 0u)) pos--;
    }
    return pos;
}

/* Filename char entry with generated-default semantics. While *gen is set, the
 * first typed character REPLACES the entire generated default (and clears the
 * flag); otherwise the character is appended (bounded by maxlen). Returns the
 * new length. */
static inline uint8_t m1_kb_name_put(char* buf, uint8_t len, uint8_t maxlen, char c, uint8_t* gen)
{
    if (gen && *gen) { buf[0] = c; buf[1] = '\0'; *gen = 0u; return 1u; }
    if (len < maxlen) { buf[len] = c; buf[len + 1u] = '\0'; return (uint8_t)(len + 1u); }
    return len;
}

/* Filename backspace with generated-default semantics. While *gen is set, ONE
 * press clears the ENTIRE generated default (and clears the flag); otherwise a
 * single character is removed. Returns the new length. */
static inline uint8_t m1_kb_name_bs(char* buf, uint8_t len, uint8_t* gen)
{
    if (gen && *gen) { buf[0] = '\0'; *gen = 0u; return 0u; }
    if (len) { buf[len - 1u] = '\0'; return (uint8_t)(len - 1u); }
    return 0u;
}

#endif /* M1_KB_LOGIC_H_ */
