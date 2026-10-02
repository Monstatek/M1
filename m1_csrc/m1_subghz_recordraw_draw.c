/* See COPYING.txt for license details. */

/*
 * m1_subghz_recordraw_draw.c
 *
 * Shared, hardware-independent drawing code for the Sub-GHz Record RAW view.
 * See m1_subghz_recordraw_draw.h for the design notes. This file must not pull
 * in any STM32 HAL / FreeRTOS / radio headers so that it can also be compiled
 * natively by the preview harness.
 *
 * M1 Project
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "m1_subghz_recordraw_draw.h"

/*----------------------------------------------------------------------------*/
/* Fonts - the M1's own U8g2 fonts (same symbols as m1_display.h:              */
/*   M1_DISP_RUN_MENU_FONT_B  == u8g2_font_courB08_tf                          */
/*   M1_DISP_SUB_MENU_FONT_N  == u8g2_font_resoledmedium_tr                    */
/* Declared via u8g2.h so this module stays free of the HW display header.    */
/*----------------------------------------------------------------------------*/
#define RR_FONT_BOLD   u8g2_font_courB08_tf
#define RR_FONT_SMALL  u8g2_font_resoledmedium_tr
#define RR_FONT_BAR    u8g2_font_spleen5x8_mf   /* M1_DISP_FUNC_MENU_FONT_N -- match the product's footer/black-bar font (NFC etc.) */

/*----------------------------------------------------------------------------*/
/* Bitmap assets - the existing M1 assets from m1_display_data.c.             */
/*----------------------------------------------------------------------------*/
extern const uint8_t arrowright_8x8[];
extern const uint8_t arrowleft_8x8[];
extern const uint8_t arrowdown_8x8[];
extern const uint8_t target_10x10[];

/*============================================================================*/
/* History helpers                                                            */
/*============================================================================*/

void subghz_rr_ctx_reset(SubGhz_RR_Ctx_t *ctx, int16_t threshold_dbm)
{
    memset(ctx->rssi_hist, 0, sizeof(ctx->rssi_hist));
    ctx->wr            = 0;
    ctx->count         = 1;   /* the working column is always visible */
    ctx->last_rssi_dbm = SUBGHZ_RR_RSSI_MIN_DBM;
    ctx->threshold_dbm = threshold_dbm;
    ctx->sample_total  = 0;
    ctx->tx_phase      = 0;
    ctx->tx_pos        = 0;
    ctx->tx_total      = 0;
    ctx->tx_done       = 0;
    ctx->tx_held       = 0;
    ctx->tx_show_hint  = 0;
    /* Displayed-RSSI text smoothing + deadband: clean start for a new session. */
    ctx->rssi_disp_acc    = 0;
    ctx->rssi_disp_n      = 0;
    ctx->rssi_disp_dbm    = SUBGHZ_RR_RSSI_MIN_DBM;
    ctx->rssi_disp_seeded = 0;
}

void subghz_rr_rssi_feed(SubGhz_RR_Ctx_t *ctx, int16_t rssi_dbm)
{
    ctx->rssi_disp_acc += rssi_dbm;
    ctx->rssi_disp_n++;
    /* Seed the display ONCE per session (first sample after a reset) so it
     * shows a real value immediately instead of the reset floor. Must not
     * re-seed each window, or it would override the display deadband. */
    if (!ctx->rssi_disp_seeded)
    {
        ctx->rssi_disp_dbm    = rssi_dbm;
        ctx->rssi_disp_seeded = 1;
    }
}

void subghz_rr_rssi_latch(SubGhz_RR_Ctx_t *ctx)
{
    if (ctx->rssi_disp_n > 0)
    {
        int32_t n = (int32_t)ctx->rssi_disp_n;
        int32_t s = ctx->rssi_disp_acc;
        int16_t cand;   /* candidate whole-number dBm (window average) */
        int     diff;

        /* Whole-number dBm, rounded to nearest (ties away from zero); RSSI is
         * negative so the round-down branch is the usual one. */
        cand = (int16_t)((s >= 0) ? (s + n / 2) / n : (s - n / 2) / n);

        /* Display deadband: only change the shown number when the smoothed
         * value moves by at least SUBGHZ_RR_RSSI_DEADBAND_DB; otherwise hold
         * the current number so it stops flickering by +/-1 dB. */
        diff = (int)cand - (int)ctx->rssi_disp_dbm;
        if (diff < 0)
            diff = -diff;
        if (diff >= SUBGHZ_RR_RSSI_DEADBAND_DB)
            ctx->rssi_disp_dbm = cand;

        ctx->rssi_disp_acc = 0;
        ctx->rssi_disp_n   = 0;
    }
}

uint8_t subghz_rr_rssi_to_h(int16_t rssi_dbm)
{
    int32_t span = (int32_t)SUBGHZ_RR_RSSI_MAX_DBM - (int32_t)SUBGHZ_RR_RSSI_MIN_DBM;
    int32_t h;

    if (rssi_dbm <= SUBGHZ_RR_RSSI_MIN_DBM)
        return 0;
    if (rssi_dbm >= SUBGHZ_RR_RSSI_MAX_DBM)
        return SUBGHZ_RR_GRAPH_H;

    h = ((int32_t)(rssi_dbm - SUBGHZ_RR_RSSI_MIN_DBM) * SUBGHZ_RR_GRAPH_H) / span;
    if (h < 0)
        h = 0;
    if (h > SUBGHZ_RR_GRAPH_H)
        h = SUBGHZ_RR_GRAPH_H;
    return (uint8_t)h;
}

void subghz_rr_push_rssi(SubGhz_RR_Ctx_t *ctx, int16_t rssi_dbm, uint8_t advance)
{
    uint8_t h = subghz_rr_rssi_to_h(rssi_dbm);

    ctx->last_rssi_dbm = rssi_dbm;
    ctx->rssi_hist[ctx->wr] = h;   /* update current (working) column */

    if (advance)                   /* trace: commit and advance one column */
    {
        uint16_t next = (uint16_t)((ctx->wr + 1u) % SUBGHZ_RR_HIST_SIZE);
        ctx->rssi_hist[next] = h;  /* seed the new working column */
        ctx->wr = next;
        if (ctx->count < SUBGHZ_RR_HIST_SIZE)
            ctx->count++;
    }
}

void subghz_rr_tx_advance(SubGhz_RR_Ctx_t *ctx)
{
    ctx->tx_phase = (uint16_t)(ctx->tx_phase + SUBGHZ_RR_PKT_STEP);
}

/*============================================================================*/
/* Internal drawing primitives                                                */
/*============================================================================*/

/* Current frequency + modulation in the baseline M1 positions/font. */
static void rr_draw_freq_mod(u8g2_t *u8g2, const char *band, const char *mod)
{
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_SetFont(u8g2, RR_FONT_BOLD);
    u8g2_DrawStr(u8g2, SUBGHZ_RR_FREQ_X, SUBGHZ_RR_FREQ_Y, band ? band : "---.---");
    if (mod)
        u8g2_DrawStr(u8g2, SUBGHZ_RR_MOD_X, SUBGHZ_RR_MOD_Y, mod);
}

/* Baseline M1 "Change" selector (top-right). */
static void rr_draw_change_selector(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawBox(u8g2, SUBGHZ_RR_CHANGE_X, SUBGHZ_RR_CHANGE_Y,
                 SUBGHZ_RR_CHANGE_W, SUBGHZ_RR_CHANGE_H);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_BG);
    u8g2_SetFont(u8g2, RR_FONT_BOLD);
    u8g2_DrawXBMP(u8g2, 70, 1, 8, 8, arrowleft_8x8);
    u8g2_DrawXBMP(u8g2, 120, 1, 8, 8, arrowright_8x8);
    u8g2_DrawStr(u8g2, 82, 8, "Change");
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
}

/* Bottom option bar with the M1 target icon and an action label. */
static void rr_draw_bottom_bar(u8g2_t *u8g2, const char *label)
{
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawBox(u8g2, 0, SUBGHZ_RR_BAR_Y, 128, SUBGHZ_RR_BAR_H);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_BG);
    u8g2_SetFont(u8g2, RR_FONT_BAR);
    u8g2_DrawXBMP(u8g2, 84, SUBGHZ_RR_BAR_Y, 10, 10, target_10x10);
    u8g2_DrawStr(u8g2, 96, SUBGHZ_RR_BAR_Y + 9, label);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
}

/* Thin frame around the shared graph region. */
static void rr_draw_graph_frame(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawFrame(u8g2, SUBGHZ_RR_GRAPH_X - 1, SUBGHZ_RR_GRAPH_Y - 1,
                   SUBGHZ_RR_GRAPH_W + 2, SUBGHZ_RR_GRAPH_H + 2);
}

/* Dashed horizontal RSSI threshold line across the graph. */
static void rr_draw_threshold_line(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx)
{
    uint8_t th_h = subghz_rr_rssi_to_h(ctx->threshold_dbm);
    int y = SUBGHZ_RR_GRAPH_BOTTOM - (int)th_h;
    int x;

    if (th_h == 0 || th_h >= SUBGHZ_RR_GRAPH_H)
        return;

    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    for (x = SUBGHZ_RR_GRAPH_X; x < SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W; x += 3)
        u8g2_DrawPixel(u8g2, x, y);
}

/* Bottom-origin envelope Y for a sample height (h==0 -> bottom baseline). */
static int rr_trace_y(uint8_t h)
{
    int y = SUBGHZ_RR_GRAPH_BOTTOM - (int)h;
    if (y < SUBGHZ_RR_GRAPH_Y)
        y = SUBGHZ_RR_GRAPH_Y;
    if (y > SUBGHZ_RR_GRAPH_BOTTOM)
        y = SUBGHZ_RR_GRAPH_BOTTOM;
    return y;
}

/* A 2px-thick trace segment between two consecutive samples (drawn twice, offset
 * one pixel upward for weight). Clean scrolling waveform, not a filled bar. */
static void rr_draw_trace_seg(u8g2_t *u8g2, int x0, int y0, int x1, int y1)
{
    u8g2_DrawLine(u8g2, x0, y0, x1, y1);
    u8g2_DrawLine(u8g2, x0, y0 - 1, x1, y1 - 1);
}

/*
 * Draw the RSSI history in chronological order as stacked segmented columns,
 * plus a newest-sample cursor and rising-edge markers at the threshold level.
 *
 * The graph ALWAYS progresses left-to-right: the oldest visible column is
 * anchored at the left edge, the newest column (and cursor) advances rightward,
 * and once the history fills the oldest column drops off the left while new
 * samples enter on the right. There is no direction/alignment state - both the
 * Ready monitor and the Recording graph use this single left-to-right order, so
 * the graph can never render reversed.
 */
static void rr_draw_rssi_history(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx)
{
    uint16_t n = ctx->count;
    uint16_t i;
    uint16_t start;

    if (n == 0)
        return;

    /* Oldest visible column index (working column is the newest). */
    start = (uint16_t)((ctx->wr + SUBGHZ_RR_HIST_SIZE - (n - 1)) % SUBGHZ_RR_HIST_SIZE);

    /* Oldest sample anchored at the left edge; the graph fills rightward. A clean
     * bottom-origin CONNECTED 2px trace: baseline on the bottom, peaks rise, no
     * top cursor. Each sample joins the previous so the waveform is continuous. */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    {
        int prev_x = -1, prev_y = 0;
        for (i = 0; i < n; i++)
        {
            uint16_t idx = (uint16_t)((start + i) % SUBGHZ_RR_HIST_SIZE);
            int x = SUBGHZ_RR_GRAPH_X + (int)i;
            int y = rr_trace_y(ctx->rssi_hist[idx]);

            if (prev_x >= 0)
                rr_draw_trace_seg(u8g2, prev_x, prev_y, x, y);
            else
                u8g2_DrawVLine(u8g2, x, (y > SUBGHZ_RR_GRAPH_Y) ? y - 1 : y, 2); /* first sample */

            prev_x = x;
            prev_y = y;
        }
    }
}

/*============================================================================*/
/* Whole-screen state renderers                                               */
/*============================================================================*/

void subghz_rr_draw_ready(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                          const char *band, const char *mod)
{
    (void)ctx;

    u8g2_ClearBuffer(u8g2);

    /* READY uses the SAME graph-screen layout as Recording, just static: the
     * graph frame is shown with a static bottom baseline (no scrolling, no signal
     * reaction, no N:). Start begins the scroll/capture - it does NOT change the
     * screen. (The Ready RSSI monitor is disabled, so nothing animates here.) */
    rr_draw_freq_mod(u8g2, band, mod);       /* baseline M1 freq/mod positions */
    rr_draw_change_selector(u8g2);           /* baseline M1 Change (top-right)  */

    /* Static UI placeholder in the otherwise-empty top-left corner: a zeroed
     * instrument display before activation. READY is passive (no RX, no RSSI
     * read) so this is NOT a measured value; it is purely cosmetic. Once START
     * is pressed the recording screen shows the real live RSSI in this spot. */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_SetFont(u8g2, RR_FONT_SMALL);
    u8g2_DrawStr(u8g2, 2, 8, "0 dBm");

    rr_draw_graph_frame(u8g2);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawHLine(u8g2, SUBGHZ_RR_GRAPH_X, SUBGHZ_RR_GRAPH_BOTTOM,     SUBGHZ_RR_GRAPH_W);
    u8g2_DrawHLine(u8g2, SUBGHZ_RR_GRAPH_X, SUBGHZ_RR_GRAPH_BOTTOM - 1, SUBGHZ_RR_GRAPH_W); /* static 2px baseline */

    rr_draw_bottom_bar(u8g2, "Start");
}

void subghz_rr_draw_recording(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                              const char *band, const char *mod)
{
    char info[28];

    u8g2_ClearBuffer(u8g2);

    rr_draw_freq_mod(u8g2, band, mod);

    /* Upper-right status text removed: the Recording screen no longer shows a
     * "Recording..." label (top-right stays clear, as on the Ready screen). */

    /* Live RSSI + running RAW sample count in the free top-left corner. */
    snprintf(info, sizeof(info), "%ddBm N:%lu",
             (int)ctx->rssi_disp_dbm, (unsigned long)ctx->sample_total);
    u8g2_DrawStr(u8g2, 2, 8, info);

    rr_draw_graph_frame(u8g2);
    rr_draw_threshold_line(u8g2, ctx);
    rr_draw_rssi_history(u8g2, ctx); /* always chronological left-to-right */

    rr_draw_bottom_bar(u8g2, "Stop");
}

/*----------------------------------------------------------------------------*/
/* Redesigned Transmitting screen (image is the visual source of truth):       */
/*   header  : antenna glyph + filled TX badge + frequency + divider           */
/*   status  : centred "SENDING..."                                            */
/*   progress: outlined box with vertical bars filling left->right by real     */
/*             TX position (ctx->tx_pos / ctx->tx_total, bytes of the burst).   */
/*----------------------------------------------------------------------------*/

/* Small transmit-antenna glyph (inverted-triangle dish on a mast with two
 * radiating brackets each side). Drawn from primitives - no bitmap asset. */
static void rr_draw_tx_antenna(u8g2_t *u8g2, int x, int y)
{
    int cx = x + 8;   /* mast centre */

    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    /* dish (downward triangle) + mast + base */
    u8g2_DrawTriangle(u8g2, cx - 3, y, cx + 3, y, cx, y + 4);
    u8g2_DrawVLine(u8g2, cx, y + 4, 6);
    u8g2_DrawHLine(u8g2, cx - 2, y + 10, 5);
    /* radiating brackets: inner then outer, both sides */
    u8g2_DrawLine(u8g2, cx - 5, y,     cx - 6, y + 2);
    u8g2_DrawLine(u8g2, cx - 6, y + 2, cx - 5, y + 4);
    u8g2_DrawLine(u8g2, cx - 7, y - 1, cx - 8, y + 2);
    u8g2_DrawLine(u8g2, cx - 8, y + 2, cx - 7, y + 5);
    u8g2_DrawLine(u8g2, cx + 5, y,     cx + 6, y + 2);
    u8g2_DrawLine(u8g2, cx + 6, y + 2, cx + 5, y + 4);
    u8g2_DrawLine(u8g2, cx + 7, y - 1, cx + 8, y + 2);
    u8g2_DrawLine(u8g2, cx + 8, y + 2, cx + 7, y + 5);
}

/* Header row: antenna + filled TX badge (white text) + frequency + divider.
 * 'freq' is a MHz number string (e.g. "434.059"); "MHz" is appended here. */
static void rr_draw_tx_header(u8g2_t *u8g2, const char *freq)
{
    const int badge_x = 22, badge_w = 20, badge_h = 11;

    rr_draw_tx_antenna(u8g2, 1, 2);

    u8g2_SetFont(u8g2, RR_FONT_BOLD);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawBox(u8g2, badge_x, 1, badge_w, badge_h);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_BG);
    u8g2_DrawStr(u8g2, badge_x + 3, badge_h, "TX");

    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    if (freq)
    {
        char lbl[24];
        snprintf(lbl, sizeof(lbl), "%s MHz", freq);
        u8g2_DrawStr(u8g2, badge_x + badge_w + 4, badge_h, lbl);
    }

    u8g2_DrawHLine(u8g2, 0, 15, SUBGHZ_RR_LCD_W);
}

/* "Hold center button to repeat" hint (drawn only on the completed screen).
 * A small center-button glyph (filled while the button is actually held) plus a
 * short label. This is a status indicator only - it owns no control logic. */
static void rr_draw_hold_hint(u8g2_t *u8g2, uint8_t held)
{
    const char *txt = "HOLD TO REPLAY";
    int gw = 9, gap = 3, tw, total, gx, tx, gy = 54;

    u8g2_SetFont(u8g2, RR_FONT_SMALL);
    tw = u8g2_GetStrWidth(u8g2, txt);
    total = gw + gap + tw;
    gx = (SUBGHZ_RR_LCD_W - total) / 2;
    tx = gx + gw + gap;

    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    if (held)
        u8g2_DrawRBox(u8g2, gx, gy, gw, gw, 2);              /* pressed = filled  */
    else
    {
        u8g2_DrawRFrame(u8g2, gx, gy, gw, gw, 2);            /* idle = outline    */
        u8g2_DrawDisc(u8g2, gx + gw / 2, gy + gw / 2, 1, U8G2_DRAW_ALL);
    }
    u8g2_DrawStr(u8g2, tx, gy + gw - 1, txt);
}

void subghz_rr_draw_transmitting(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                                 const char *band, const char *mod)
{
    const int box_x = 6, box_y = 34, box_w = 116, box_h = 16;
    const int bar_w = 3, bar_gap = 2, period = bar_w + bar_gap;
    int in_x, in_y, in_h, in_w, nbars, filled, i, w;
    /* Text is always "SENDING...". The bar reflects the caller-provided visual
     * position (a fixed readable left-to-right cycle); on the completed/idle
     * screen (tx_done) the box is EMPTY and the hold-to-repeat hint is shown. */
    const char *s = "SENDING...";

    (void)mod;

    u8g2_ClearBuffer(u8g2);

    rr_draw_tx_header(u8g2, band);

    /* centred status text */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_SetFont(u8g2, RR_FONT_BOLD);
    w = u8g2_GetStrWidth(u8g2, s);
    u8g2_DrawStr(u8g2, (SUBGHZ_RR_LCD_W - w) / 2, 30, s);

    /* progress box + left-to-right vertical bars driven by real TX position */
    u8g2_DrawFrame(u8g2, box_x, box_y, box_w, box_h);
    in_x = box_x + 3;
    in_y = box_y + 3;
    in_w = box_w - 6;
    in_h = box_h - 6;
    nbars = (in_w + bar_gap) / period;
    if (nbars < 1)
        nbars = 1;

    /* Bars fill strictly left-to-right from the caller-set visual position.
     * tx_done shows an empty box (tx_pos is 0), plus the hold hint. */
    if (ctx->tx_total > 0 && ctx->tx_pos <= ctx->tx_total)
        filled = (int)((ctx->tx_pos * (uint32_t)nbars) / ctx->tx_total);
    else
        filled = 0;
    if (filled > nbars)
        filled = nbars;

    for (i = 0; i < filled; i++)                            /* leftmost first -> right */
        u8g2_DrawBox(u8g2, in_x + i * period, in_y, bar_w, in_h);

    /* HOLD TO REPLAY prompt: ALWAYS shown on the Transmitting screen, from the
     * very first frame of the initial transmission onward (it is a control hint;
     * it does not gate or delay the initial RF, which is started by the Play
     * action, and hold-to-replay still arms only after the Play press releases). */
    (void)ctx->tx_show_hint;
    rr_draw_hold_hint(u8g2, ctx->tx_held);
}

/*----------------------------------------------------------------------------*/
/* Baseline M1 states - reverted per review (no redesigned graph).            */
/*----------------------------------------------------------------------------*/

void subghz_rr_draw_complete(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                             const char *band, const char *mod)
{
    char info[20];

    u8g2_ClearBuffer(u8g2);

    rr_draw_freq_mod(u8g2, band, mod);

    /* Final RAW sample count (accurate) in the top-left corner. */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_SetFont(u8g2, RR_FONT_SMALL);
    snprintf(info, sizeof(info), "Captured N:%lu", (unsigned long)ctx->sample_total);
    u8g2_DrawStr(u8g2, 2, 8, info);

    /* Baseline M1 option bar: Play (OK) / Reset (left) / Save (down). */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawBox(u8g2, 0, SUBGHZ_RR_BAR_Y, 128, SUBGHZ_RR_BAR_H);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_BG);
    u8g2_SetFont(u8g2, RR_FONT_BAR);
    u8g2_DrawXBMP(u8g2, 84, SUBGHZ_RR_BAR_Y, 10, 10, target_10x10);
    u8g2_DrawStr(u8g2, 96, SUBGHZ_RR_BAR_Y + 9, "Play");
    u8g2_DrawXBMP(u8g2, 1, SUBGHZ_RR_BAR_Y + 1, 8, 8, arrowleft_8x8);
    u8g2_DrawStr(u8g2, 11, SUBGHZ_RR_BAR_Y + 9, "Reset");
    u8g2_DrawXBMP(u8g2, 48, SUBGHZ_RR_BAR_Y + 1, 8, 8, arrowdown_8x8);
    u8g2_DrawStr(u8g2, 58, SUBGHZ_RR_BAR_Y + 9, "Save");
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
}

void subghz_rr_draw_replay_ready(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                                 const char *band, const char *mod)
{
    (void)ctx;

    u8g2_ClearBuffer(u8g2);

    rr_draw_freq_mod(u8g2, band, mod);

    /* Baseline M1 option bar: target icon + "Press OK to replay". */
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
    u8g2_DrawBox(u8g2, 0, SUBGHZ_RR_BAR_Y, 128, SUBGHZ_RR_BAR_H);
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_BG);
    u8g2_SetFont(u8g2, RR_FONT_BAR);
    u8g2_DrawXBMP(u8g2, 2, SUBGHZ_RR_BAR_Y, 10, 10, target_10x10);
    u8g2_DrawStr(u8g2, 14, SUBGHZ_RR_BAR_Y + 9, "Press OK to replay");
    u8g2_SetDrawColor(u8g2, SUBGHZ_RR_COLOR_FG);
}
