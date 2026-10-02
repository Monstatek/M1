/*
 * recordraw_dir_test.c
 *
 * Deterministic regression test for the Record RAW graph DIRECTION.
 * Replays the exact firmware state sequence using the real drawing module and
 * the real ctx operations, and asserts the Recording graph always progresses
 * left-to-right - including after PLAY and after the ring buffer wraps.
 *
 * The firmware transition rules replicated here (from m1_sub_ghz.c):
 *   - entering READY / a new recording  -> subghz_rr_ctx_reset(...)   [Start @1149, enter_ready @979]
 *   - entering PLAY (transmit)           -> ctx.tx_phase = 0          [@1182/@1202]
 *   - PLAY tick                          -> subghz_rr_tx_advance(ctx) [@1038]
 *   - Recording tick                     -> subghz_rr_push_rssi(ctx, rssi, rssi>=thr)
 *   - Recording render                   -> subghz_rr_draw_recording (left_to_right = 1)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "m1_subghz_recordraw_draw.h"

static uint8_t dummy_byte_cb(u8x8_t *a, uint8_t b, uint8_t c, void *d){(void)a;(void)b;(void)c;(void)d;return 1;}
static uint8_t dummy_gpio_cb(u8x8_t *a, uint8_t b, uint8_t c, void *d){(void)a;(void)b;(void)c;(void)d;return 1;}

static int get_pixel(u8g2_t *u,int x,int y){
    uint8_t*buf=u8g2_GetBufferPtr(u); int w=u8g2_GetBufferTileWidth(u)*8;
    return (buf[(long)(y>>3)*w+x]>>(y&7))&1;
}
static void put_le32(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void write_bmp(u8g2_t*u,const char*path,int S){
    int W=128*S,H=64*S,rb=(W*3+3)&~3,is=rb*H,x,y; uint8_t hdr[54],*row; FILE*f=fopen(path,"wb");
    if(!f)return; memset(hdr,0,54); hdr[0]='B';hdr[1]='M'; put_le32(hdr+2,54+is); put_le32(hdr+10,54);
    put_le32(hdr+14,40); put_le32(hdr+18,W); put_le32(hdr+22,H); hdr[26]=1;hdr[28]=24; put_le32(hdr+34,is);
    fwrite(hdr,1,54,f); row=malloc(rb);
    for(y=H-1;y>=0;y--){memset(row,0,rb); for(x=0;x<W;x++){uint8_t v=get_pixel(u,x/S,y/S)?0:0xFF; row[x*3]=row[x*3+1]=row[x*3+2]=v;} fwrite(row,1,rb,f);}
    free(row); fclose(f);
}

/* Push n RSSI samples the way the Recording tick does. `above` selects whether
 * each sample is above threshold (advance) so we can control fill deterministically. */
static void record_push(SubGhz_RR_Ctx_t *ctx, int n, int above)
{
    int i;
    int16_t rssi = above ? (SUBGHZ_RR_RSSI_MAX_DBM - 6)   /* well above threshold, h>0 */
                         : (SUBGHZ_RR_RSSI_MIN_DBM + 2);  /* below threshold, no advance */
    for (i = 0; i < n; i++)
        subghz_rr_push_rssi(ctx, rssi, (uint8_t)(rssi >= ctx->threshold_dbm));
}

/* Measure the filled graph columns on the baseline row (bars only; the dashed
 * threshold line sits on a different row so it can't be mistaken for a bar). */
static void measure(u8g2_t *u, int *left, int *right, int *nfilled)
{
    int x, y = SUBGHZ_RR_GRAPH_BOTTOM;
    int lo = -1, hi = -1, cnt = 0;
    for (x = SUBGHZ_RR_GRAPH_X; x < SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W; x++) {
        if (get_pixel(u, x, y)) { if (lo < 0) lo = x; hi = x; cnt++; }
    }
    *left = lo; *right = hi; *nfilled = cnt;
}

static int fails = 0;
static void check(const char *label, int cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", label);
    if (!cond) fails++;
}

/* Assert a partially-filled Recording graph is LEFT-anchored (fills rightward). */
static void assert_left_to_right(u8g2_t *u, const char *tag)
{
    int left, right, n;
    measure(u, &left, &right, &n);
    printf("  %-22s filled cols=%d  left=x%d  right=x%d  (GRAPH_X=%d, right_edge=x%d)\n",
           tag, n, left, right, SUBGHZ_RR_GRAPH_X, SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W - 1);
    check("history is left-anchored (leftmost bar at GRAPH_X)", left == SUBGHZ_RR_GRAPH_X);
    check("newest edge is NOT pinned to the right (i.e. not right-aligned)",
          right < SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W - 1);
}

int main(void)
{
    u8g2_t u8g2;
    SubGhz_RR_Ctx_t ctx;
    const int THR = -90;
    u8g2_Setup_st7567_enh_dg128064i_f(&u8g2, U8G2_R0, dummy_byte_cb, dummy_gpio_cb);
    u8g2_SetFontMode(&u8g2, 1);

    printf("== Record RAW graph-direction regression (A-F) ==\n");

    /* A. Fresh Record entry + first recording (enter_ready reset, then Start reset). */
    subghz_rr_ctx_reset(&ctx, THR);                 /* enter_ready */
    subghz_rr_ctx_reset(&ctx, THR);                 /* Start */
    record_push(&ctx, 40, 1);
    ctx.sample_total = 1600;
    subghz_rr_draw_recording(&u8g2, &ctx, "433.920", "OOK");
    write_bmp(&u8g2, "out/dir_A_first_recording.bmp", 6);
    printf("A. First recording:\n");
    assert_left_to_right(&u8g2, "A first recording");

    /* B. Stop -> Complete (no ctx change) then Play: tx_phase=0, animate. */
    ctx.tx_phase = 0;                                /* PLAY entry */
    { int i; for (i = 0; i < 25; i++) subghz_rr_tx_advance(&ctx); } /* PLAY ticks */
    subghz_rr_draw_transmitting(&u8g2, &ctx, "433.920", "OOK");
    write_bmp(&u8g2, "out/dir_B_playing.bmp", 6);
    printf("B. Stop -> Play (TX animating, tx_phase now %u)\n", ctx.tx_phase);

    /* C. Return from Play to Ready (enter_ready reset). */
    subghz_rr_ctx_reset(&ctx, THR);                 /* enter_ready */
    printf("C. Return from Play -> Ready (ctx reset: wr=%u count=%u tx_phase=%u)\n",
           ctx.wr, ctx.count, ctx.tx_phase);
    check("C reset write index to 0", ctx.wr == 0);
    check("C reset count to 1 (no stale fullness)", ctx.count == 1);
    check("C reset tx_phase to 0 (TX state cleared)", ctx.tx_phase == 0);

    /* D. Start second recording (Start reset) + push. */
    subghz_rr_ctx_reset(&ctx, THR);                 /* Start */
    record_push(&ctx, 40, 1);
    ctx.sample_total = 1600;
    subghz_rr_draw_recording(&u8g2, &ctx, "433.920", "OOK");
    write_bmp(&u8g2, "out/dir_D_second_recording.bmp", 6);
    printf("D/E. Second recording (after Play):\n");
    assert_left_to_right(&u8g2, "D second recording");

    /* F. Fill and wrap the second graph; confirm it still scrolls left-to-right
     * (newest sample stays on the right). */
    record_push(&ctx, SUBGHZ_RR_HIST_SIZE + 20, 1); /* overflow -> wrap */
    /* push one last, distinctively MAX-height sample as the newest */
    subghz_rr_push_rssi(&ctx, 0 /* >= MAX -> full height */, 1);
    subghz_rr_draw_recording(&u8g2, &ctx, "433.920", "OOK");
    write_bmp(&u8g2, "out/dir_F_wrapped.bmp", 6);
    {
        int left, right, n, hL, hR, y;
        measure(&u8g2, &left, &right, &n);
        printf("F. Wrapped graph: filled cols=%d left=x%d right=x%d (SIZE=%d)\n",
               n, left, right, SUBGHZ_RR_HIST_SIZE);
        check("F graph is full width", left == SUBGHZ_RR_GRAPH_X &&
              right == SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W - 1);
        /* newest (tallest) column must be on the RIGHT edge, not the left */
        hL = hR = 0;
        for (y = SUBGHZ_RR_GRAPH_Y; y <= SUBGHZ_RR_GRAPH_BOTTOM; y++) {
            if (get_pixel(&u8g2, SUBGHZ_RR_GRAPH_X + 1, y)) hL++;
            if (get_pixel(&u8g2, SUBGHZ_RR_GRAPH_X + SUBGHZ_RR_GRAPH_W - 2, y)) hR++;
        }
        printf("   leftmost col height=%d  rightmost col height=%d\n", hL, hR);
        check("F newest (tallest) sample is on the RIGHT (chronological L->R)", hR > hL);
    }

    printf("\n== %s ==\n", fails ? "FAILURES DETECTED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
