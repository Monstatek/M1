/* See COPYING.txt for license details. */

/*
 * m1_subghz_recordraw_draw.h
 *
 * Shared, hardware-independent drawing code for the Sub-GHz Record RAW view.
 *
 * Compiled into two targets:
 *   1. The M1 firmware   - called by m1_sub_ghz.c to render the Record RAW view.
 *   2. A host test harness - to generate exact 128x64 1-bit previews from the
 *                            very same drawing code that runs on the device.
 *
 * The Record RAW view is a SINGLE view (VIEW_MODE_SUBGHZ_RECORD). Ready,
 * Recording, Capture Complete, Replay Ready and Transmitting are all states of
 * that one view. Only Ready, Recording and Transmitting carry the redesigned
 * visuals; Capture Complete and Replay Ready keep the baseline M1 layout.
 *
 * M1 Project
 */

#ifndef M1_SUBGHZ_RECORDRAW_DRAW_H
#define M1_SUBGHZ_RECORDRAW_DRAW_H

#include <stdint.h>
#include "u8g2.h"

/*----------------------------------------------------------------------------*/
/* Display geometry - exact M1 panel: 128 x 64, true 1-bit monochrome.        */
/*----------------------------------------------------------------------------*/
#define SUBGHZ_RR_LCD_W          128
#define SUBGHZ_RR_LCD_H          64

/* Baseline M1 Ready header coordinates (unchanged from the public repo). */
#define SUBGHZ_RR_CHANGE_X       70
#define SUBGHZ_RR_CHANGE_Y       0
#define SUBGHZ_RR_CHANGE_W       58
#define SUBGHZ_RR_CHANGE_H       10
#define SUBGHZ_RR_FREQ_X         60   /* subghz_band_text position  (M1)       */
#define SUBGHZ_RR_FREQ_Y         18
#define SUBGHZ_RR_MOD_X          108  /* subghz_modulation_text position (M1)  */
#define SUBGHZ_RR_MOD_Y          18

/* Shared graph region - identical fixed coordinates for Ready, Recording and
 * Transmitting. Sits below the M1 header row and above the option bar. */
#define SUBGHZ_RR_GRAPH_X        2
#define SUBGHZ_RR_GRAPH_Y        22
#define SUBGHZ_RR_GRAPH_W        124
#define SUBGHZ_RR_GRAPH_H        27
#define SUBGHZ_RR_GRAPH_BOTTOM   (SUBGHZ_RR_GRAPH_Y + SUBGHZ_RR_GRAPH_H - 1)

/* One history column per horizontal pixel of the graph. */
#define SUBGHZ_RR_HIST_SIZE      SUBGHZ_RR_GRAPH_W

/* Stacked-segment RSSI columns. */
#define SUBGHZ_RR_SEG_H          2
#define SUBGHZ_RR_SEG_GAP        1

/* dBm window mapped onto the vertical extent of the graph. */
#define SUBGHZ_RR_RSSI_MIN_DBM   (-110)
#define SUBGHZ_RR_RSSI_MAX_DBM   (-24)

/* Bottom option bar (unchanged M1 convention). */
#define SUBGHZ_RR_BAR_Y          52
#define SUBGHZ_RR_BAR_H          12

/* Procedural TX packet-train animation (no sine, no float, no bitmap frames). */
#define SUBGHZ_RR_EMIT_W         18   /* left emitter zone width               */
#define SUBGHZ_RR_PKT_W          12   /* packet block width                    */
#define SUBGHZ_RR_PKT_GAP        8    /* gap between packets                   */
#define SUBGHZ_RR_PKT_PERIOD     (SUBGHZ_RR_PKT_W + SUBGHZ_RR_PKT_GAP)
#define SUBGHZ_RR_PKT_STEP       3    /* pixels advanced per ~100 ms tick      */

/* Draw colours (mirror M1_DISP_DRAW_COLOR_*): 1 = foreground, 0 = background. */
#define SUBGHZ_RR_COLOR_FG       1
#define SUBGHZ_RR_COLOR_BG       0

/* Displayed-RSSI text is a rolling average of the raw samples, latched roughly
 * every SUBGHZ_RR_RSSI_DISP_MS, and only re-displayed when the smoothed value
 * moves by at least SUBGHZ_RR_RSSI_DEADBAND_DB (a display deadband) so the
 * on-screen number stops flickering. Affects ONLY the text - the graph still
 * uses every raw sample at the full rate. */
#define SUBGHZ_RR_RSSI_DISP_MS       500
#define SUBGHZ_RR_RSSI_DEADBAND_DB   2

/*----------------------------------------------------------------------------*/
/* Render context: owns the circular RSSI history and the TX phase counter.   */
/*----------------------------------------------------------------------------*/
typedef struct
{
    uint8_t  rssi_hist[SUBGHZ_RR_HIST_SIZE]; /* column heights, 0..GRAPH_H     */
    uint16_t wr;              /* index of the current (newest) working column  */
    uint16_t count;           /* visible columns incl. working (1..SIZE)       */
    int16_t  last_rssi_dbm;   /* most recent RAW RSSI sample (drives the graph) */
    int16_t  threshold_dbm;   /* RSSI threshold line / advance level           */
    uint32_t sample_total;    /* running RAW sample count shown while recording */
    uint16_t tx_phase;        /* TX packet-train phase counter                 */
    /* Replay/Transmitting progress (bytes of the current DMA burst). The caller
     * fills these from the live TX DMA counter before drawing; the renderer only
     * reads them. tx_total == 0 means "no/unknown progress" -> empty box.       */
    uint32_t tx_pos;          /* bytes already transmitted in the current burst */
    uint32_t tx_total;        /* total bytes of the current burst               */
    uint8_t  tx_done;         /* 1 = idle/waiting on the Transmitting screen       */
    uint8_t  tx_held;         /* 1 = center button currently held (hint emphasis) */
    uint8_t  tx_show_hint;    /* 1 = draw the HOLD TO REPLAY prompt (replay phase) */
    /* Displayed-RSSI text smoothing (rolling average + deadband, text only). */
    int32_t  rssi_disp_acc;   /* sum of raw dBm samples in the current window   */
    uint16_t rssi_disp_n;     /* number of samples accumulated this window      */
    int16_t  rssi_disp_dbm;   /* latched whole-number dBm shown as text         */
    uint8_t  rssi_disp_seeded;/* 0 until the first sample seeds the display once */
} SubGhz_RR_Ctx_t;

/*----------------------------------------------------------------------------*/
/* History helpers - circular buffer, no whole-array memmove.                 */
/*----------------------------------------------------------------------------*/

/* Reset history/counters and set the threshold level (dBm). Also clears the
 * displayed-RSSI smoothing state so a new recording starts clean. */
void subghz_rr_ctx_reset(SubGhz_RR_Ctx_t *ctx, int16_t threshold_dbm);

/* Feed one RAW RSSI sample into the display-text rolling average (call at the
 * full sample rate). Does NOT touch the graph. */
void subghz_rr_rssi_feed(SubGhz_RR_Ctx_t *ctx, int16_t rssi_dbm);

/* Latch the current window average into the whole-number dBm shown as text
 * (call roughly every SUBGHZ_RR_RSSI_DISP_MS) and start a new window. */
void subghz_rr_rssi_latch(SubGhz_RR_Ctx_t *ctx);

/* Map an RSSI value in dBm to a bar height in pixels (0..GRAPH_H). */
uint8_t subghz_rr_rssi_to_h(int16_t rssi_dbm);

/*
 * Push one RSSI sample.
 *   advance != 0 : commit the working column and advance the cursor by one
 *                  (trace advances only when the
 *                  sample is above the display threshold);
 *   advance == 0 : update the current/working column and the live RSSI value
 *                  WITHOUT advancing the cursor.
 * This only affects the on-screen history - it never gates RAW recording.
 */
void subghz_rr_push_rssi(SubGhz_RR_Ctx_t *ctx, int16_t rssi_dbm, uint8_t advance);

/* Advance the TX packet-train phase by one animation step. */
void subghz_rr_tx_advance(SubGhz_RR_Ctx_t *ctx);

/*----------------------------------------------------------------------------*/
/* Whole-screen state renderers. Each clears the U8g2 buffer and draws the     */
/* complete 128x64 frame for one state; the caller flushes to the panel.       */
/* band / mod are the existing M1 strings (e.g. "433.920", "OOK").             */
/*----------------------------------------------------------------------------*/

/* Redesigned states (approved scope). */
void subghz_rr_draw_ready(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                          const char *band, const char *mod);
void subghz_rr_draw_recording(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                              const char *band, const char *mod);
void subghz_rr_draw_transmitting(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                                 const char *band, const char *mod);

/* Baseline M1 states (reverted per review - target-icon bottom bars only). */
void subghz_rr_draw_complete(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                             const char *band, const char *mod);
void subghz_rr_draw_replay_ready(u8g2_t *u8g2, const SubGhz_RR_Ctx_t *ctx,
                                 const char *band, const char *mod);

#endif /* M1_SUBGHZ_RECORDRAW_DRAW_H */
