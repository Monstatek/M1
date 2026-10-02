/* See COPYING.txt for license details. */

/*
 * m1_read_icon.c
 *
 * Shared drawing code for the NFC / RFID read-waiting screens.
 * See m1_read_icon.h. Must stay free of STM32 HAL / FreeRTOS headers so it can
 * also be compiled natively by the preview harness.
 *
 * M1 Project
 */

#include <stdint.h>
#include <string.h>
#include "m1_read_icon.h"

/* Fonts: the M1's own U8g2 fonts, referenced via u8g2.h so this module needs
 * no hardware display header.
 *   RI_FONT_LETTER == u8g2_font_courB14_tf  (bold, for the N / R glyph)
 *   RI_FONT_TEXT   == u8g2_font_5x8_tf      (clean 5px caption font)          */
#define RI_FONT_LETTER  u8g2_font_courB14_tf
#define RI_FONT_TEXT    M1_READ_ICON_CAPTION_FONT   /* shared bold caption font */

#define RI_COLOR_FG     1
#define RI_COLOR_BG     0

/* Centered tag geometry (128x64). */
#define RI_TAG_W        44
#define RI_TAG_H        24
#define RI_TAG_X        ((128 - RI_TAG_W) / 2)   /* 42 */
#define RI_TAG_Y        11
#define RI_TAG_R        5
#define RI_TAG_CX       (RI_TAG_X + RI_TAG_W / 2)          /* 64 */
#define RI_TAG_CY       (RI_TAG_Y + RI_TAG_H / 2)          /* 23 */

/* Signal-wave geometry (concentric arcs sharing a centre at the tag edge).
 * u8g2_DrawArc angles: 0 = East, 64 = North, 128 = West, 192 = South (0..255).
 * RI_WAVE_HALF is the half-span of each arc (~45 deg -> ~90 deg total). */
#define RI_WAVE_R0      5     /* innermost arc radius                      */
#define RI_WAVE_STEP    5     /* radius increment per arc                  */
#define RI_WAVE_MAX     4     /* clamp                                     */
#define RI_WAVE_HALF    32    /* ~45 deg half-span                         */

/* Caption baseline. */
#define RI_TEXT_Y       58

static void ri_center_str(u8g2_t *u8g2, int y, const char *s)
{
    int w = (int)u8g2_GetStrWidth(u8g2, s);
    int x = (128 - w) / 2;
    if (x < 0)
        x = 0;
    u8g2_DrawStr(u8g2, x, y, s);
}

/* Bold (2 px) rounded tag body with clean corners: a filled rounded box with a
 * background-coloured rounded box hollowed out of the middle. */
static void ri_draw_tag(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, RI_COLOR_FG);
    u8g2_DrawRBox(u8g2, RI_TAG_X, RI_TAG_Y, RI_TAG_W, RI_TAG_H, RI_TAG_R);
    u8g2_SetDrawColor(u8g2, RI_COLOR_BG);
    u8g2_DrawRBox(u8g2, RI_TAG_X + 2, RI_TAG_Y + 2, RI_TAG_W - 4, RI_TAG_H - 4, RI_TAG_R - 2);
    u8g2_SetDrawColor(u8g2, RI_COLOR_FG);
}

/* Letter (centred, shifted left) + small chip dot to its right. */
static void ri_draw_glyph(u8g2_t *u8g2, char letter)
{
    char s[2];
    int  lw, lx, ly;

    s[0] = letter;
    s[1] = '\0';

    u8g2_SetFont(u8g2, RI_FONT_LETTER);
    u8g2_SetFontMode(u8g2, 1);
    u8g2_SetDrawColor(u8g2, RI_COLOR_FG);

    lw = (int)u8g2_GetStrWidth(u8g2, s);
    lx = RI_TAG_CX - 6 - lw / 2;                 /* nudged left to leave room for the dot */
    ly = RI_TAG_CY + (int)u8g2_GetAscent(u8g2) / 2;
    u8g2_DrawStr(u8g2, lx, ly, s);

    /* Chip dot: small filled square vertically centred on the tag. */
    u8g2_DrawBox(u8g2, RI_TAG_X + RI_TAG_W - 12, RI_TAG_CY - 1, 3, 3);
}

/* Concentric ~120 deg signal arcs on both sides of the tag (opening outward). */
static void ri_draw_waves(u8g2_t *u8g2, uint8_t n_waves)
{
    int lcx = RI_TAG_X;                 /* left arcs share a centre at the tag edge  */
    int rcx = RI_TAG_X + RI_TAG_W;      /* right arcs share a centre at the tag edge */
    uint8_t i;

    if (n_waves > RI_WAVE_MAX)
        n_waves = RI_WAVE_MAX;

    u8g2_SetDrawColor(u8g2, RI_COLOR_FG);
    for (i = 0; i < n_waves; i++)
    {
        int r = RI_WAVE_R0 + (int)i * RI_WAVE_STEP;
        /* Left arc centred on West (128), bulging left toward the edge. */
        u8g2_DrawArc(u8g2, lcx, RI_TAG_CY, r,
                     (uint8_t)(128 - RI_WAVE_HALF), (uint8_t)(128 + RI_WAVE_HALF));
        /* Right arc centred on East (0/256); start>end wraps through East. */
        u8g2_DrawArc(u8g2, rcx, RI_TAG_CY, r,
                     (uint8_t)(256 - RI_WAVE_HALF), (uint8_t)(RI_WAVE_HALF));
    }
}

/* Whole-screen composition shared by the read and emulate screens.
 * letter != 0 -> letter + chip dot inside the tag (read screens);
 * letter == 0 -> chip dot only, no letter (emulate screen). */
static void ri_draw_screen(u8g2_t *u8g2, char letter, uint8_t n_waves,
                           const char *caption)
{
    u8g2_ClearBuffer(u8g2);

    ri_draw_waves(u8g2, n_waves);
    ri_draw_tag(u8g2);
    if (letter)
        ri_draw_glyph(u8g2, letter);
    else
        u8g2_DrawBox(u8g2, RI_TAG_X + 8, RI_TAG_CY - 1, 3, 3); /* dot only */

    u8g2_SetFont(u8g2, RI_FONT_TEXT);
    u8g2_SetFontMode(u8g2, 1);
    u8g2_SetDrawColor(u8g2, RI_COLOR_FG);
    ri_center_str(u8g2, RI_TEXT_Y, caption);
}

void m1_read_icon_draw(u8g2_t *u8g2, char letter, uint8_t n_waves)
{
    ri_draw_screen(u8g2, letter, n_waves, "Hold Tag to M1");
}

void m1_read_icon_draw_emulate(u8g2_t *u8g2, uint8_t frame)
{
    /* Storyboard wave progression: 1 -> 2 -> 3 arcs, then clear, looping. */
    static const uint8_t seq[4] = { 1, 2, 3, 0 };
    ri_draw_screen(u8g2, 0, seq[frame & 3u], "EMULATING...");
}

/* Shared rounded tag body + chip dot only (no letter/waves/caption).
 * Reuses the exact RI_TAG_* geometry via ri_draw_tag() so the Write
 * screen's tag can never drift from the Read/Emulate tag. */
void m1_read_icon_draw_tag(u8g2_t *u8g2)
{
    ri_draw_tag(u8g2);
    u8g2_DrawBox(u8g2, RI_TAG_X + 8, RI_TAG_CY - 1, 3, 3);   /* chip dot (emulate style) */
}
