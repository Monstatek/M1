/* See COPYING.txt for license details. */

/*
 * m1_subghz_scan_draw.c
 *
 * Active Sub-GHz Scan screen ("RF lock-on") drawing. U8g2 primitives only; no
 * STM32 HAL / FreeRTOS / radio headers, so the preview harness can render the
 * exact on-device frame. See m1_subghz_scan_draw.h.
 *
 * M1 Project
 */

#include <stdint.h>
#include <string.h>
#include "m1_subghz_scan_draw.h"

/* M1's own U8g2 fonts (same symbols as m1_display.h). */
#define SC_FONT_B   u8g2_font_courB08_tf         /* M1_DISP_RUN_MENU_FONT_B */
#define SC_FONT_S   u8g2_font_resoledmedium_tr   /* M1_DISP_SUB_MENU_FONT_N */
#define SC_FONT_BAR u8g2_font_spleen5x8_mf        /* M1_DISP_FUNC_MENU_FONT_N -- match the product's footer/black-bar font (NFC etc.) */

/* Existing M1 bitmap asset for the option bar. */
extern const uint8_t target_10x10[];

#define SC_FG   1
#define SC_BG   0

/* Frequency / modulation text row (preserved M1 style/font). */
#define SC_FREQ_X   2
#define SC_FREQ_Y   9
#define SC_MOD_X    106
#define SC_MOD_Y    9

/* Concentric crosshair target ("RF lock-on"), centred in the right area. */
#define SC_T_CX     97
#define SC_T_CY     31
#define SC_T_R0     4
#define SC_T_STEP   5           /* rings at r = 4, 9, 14 */
#define SC_T_RINGS  3
#define SC_T_ROUT   (SC_T_R0 + (SC_T_RINGS - 1) * SC_T_STEP)   /* 14 */
#define SC_T_TICK   2           /* crosshair overshoot beyond outer ring */

/* RF arcs beside the target. u8g2 angles: 0=E,64=N,128=W,192=S. */
#define SC_ARC_GAP  4
#define SC_ARC_R0   3
#define SC_ARC_STEP 4
#define SC_ARC_N    2
#define SC_ARC_HALF 34          /* ~half of a ~95 deg arc */

/* Option bar (existing M1 convention: inverted bar + target icon + label). */
#define SC_BAR_Y    52
#define SC_BAR_H    12

static void sc_draw_freq_mod(u8g2_t *u8g2, const char *band, const char *mod)
{
    u8g2_SetDrawColor(u8g2, SC_FG);
    u8g2_SetFont(u8g2, SC_FONT_B);
    u8g2_DrawStr(u8g2, SC_FREQ_X, SC_FREQ_Y, band ? band : "---.---");
    if (mod) {
        int w = (int)u8g2_GetStrWidth(u8g2, mod);
        u8g2_DrawStr(u8g2, SC_MOD_X - w / 2, SC_MOD_Y, mod);
    }
}

/* Concentric rings + crosshair + centre dot. */
static void sc_draw_target(u8g2_t *u8g2)
{
    int i;
    u8g2_SetDrawColor(u8g2, SC_FG);

    for (i = 0; i < SC_T_RINGS; i++)
        u8g2_DrawCircle(u8g2, SC_T_CX, SC_T_CY, SC_T_R0 + i * SC_T_STEP, U8G2_DRAW_ALL);

    /* Crosshair: lines through the centre, overshooting the outer ring, with a
     * small gap at the very centre so the cross reads cleanly. */
    u8g2_DrawHLine(u8g2, SC_T_CX - SC_T_ROUT - SC_T_TICK, SC_T_CY, SC_T_ROUT + SC_T_TICK - 1);
    u8g2_DrawHLine(u8g2, SC_T_CX + 2, SC_T_CY, SC_T_ROUT + SC_T_TICK - 1);
    u8g2_DrawVLine(u8g2, SC_T_CX, SC_T_CY - SC_T_ROUT - SC_T_TICK, SC_T_ROUT + SC_T_TICK - 1);
    u8g2_DrawVLine(u8g2, SC_T_CX, SC_T_CY + 2, SC_T_ROUT + SC_T_TICK - 1);

    /* Centre lock dot. */
    u8g2_DrawDisc(u8g2, SC_T_CX, SC_T_CY, 1, U8G2_DRAW_ALL);
}

/* RF arcs opening outward on both sides of the target. */
static void sc_draw_rf_arcs(u8g2_t *u8g2)
{
    int lcx = SC_T_CX - SC_T_ROUT - SC_ARC_GAP;
    int rcx = SC_T_CX + SC_T_ROUT + SC_ARC_GAP;
    int i;
    u8g2_SetDrawColor(u8g2, SC_FG);
    for (i = 0; i < SC_ARC_N; i++) {
        int r = SC_ARC_R0 + i * SC_ARC_STEP;
        u8g2_DrawArc(u8g2, lcx, SC_T_CY, r,
                     (uint8_t)(128 - SC_ARC_HALF), (uint8_t)(128 + SC_ARC_HALF)); /* ( */
        u8g2_DrawArc(u8g2, rcx, SC_T_CY, r,
                     (uint8_t)(256 - SC_ARC_HALF), (uint8_t)(SC_ARC_HALF));       /* ) */
    }
}

static void sc_draw_option_bar(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, SC_FG);
    u8g2_DrawBox(u8g2, 0, SC_BAR_Y, 128, SC_BAR_H);
    u8g2_SetDrawColor(u8g2, SC_BG);
    u8g2_SetFont(u8g2, SC_FONT_BAR);
    u8g2_DrawXBMP(u8g2, 84, SC_BAR_Y, 10, 10, target_10x10);
    u8g2_DrawStr(u8g2, 96, SC_BAR_Y + 9, "Stop");
    u8g2_SetDrawColor(u8g2, SC_FG);
}

void subghz_scan_draw(u8g2_t *u8g2, const char *band, const char *mod)
{
    u8g2_ClearBuffer(u8g2);

    sc_draw_freq_mod(u8g2, band, mod);

    /* SCANNING... label on the left, vertically aligned with the target. */
    u8g2_SetDrawColor(u8g2, SC_FG);
    u8g2_SetFont(u8g2, SC_FONT_B);
    u8g2_DrawStr(u8g2, 2, SC_T_CY + 4, "SCANNING...");

    sc_draw_rf_arcs(u8g2);
    sc_draw_target(u8g2);

    sc_draw_option_bar(u8g2);
}
