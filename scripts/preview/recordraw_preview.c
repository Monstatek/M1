/*
 * recordraw_preview.c
 *
 * Host-side preview harness for the Sub-GHz Record RAW view.
 *
 * Links the in-tree U8g2 library, the real M1 bitmap assets
 * (m1_display_data.c) and the shared drawing module
 * (m1_subghz_recordraw_draw.c) so the previews are produced by the exact same
 * drawing code that runs on the device.
 *
 * The device uses U8G2_R2 only to compensate for the panel being physically
 * mounted upside-down; the logical image is identical. We render with U8G2_R0
 * and read the buffer directly to obtain the upright 128x64 frame a user sees.
 *
 * Output per state:
 *   - <name>.pbm  : exact 128x64, true 1-bit (P1 ASCII).
 *   - <name>.bmp  : 6x-scaled 24-bit BMP for easy viewing (-> PNG via sips).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "m1_subghz_recordraw_draw.h"

/* ---- dummy comm/gpio callbacks: we never touch real hardware ---- */
static uint8_t dummy_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
    return 1;
}
static uint8_t dummy_gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8; (void)arg_int; (void)arg_ptr;
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
        case U8X8_MSG_DELAY_MILLI:
        case U8X8_MSG_DELAY_10MICRO:
        case U8X8_MSG_DELAY_100NANO:
            return 1;
    }
    return 1;
}

/* Read logical pixel (x,y) from the u8g2 full-frame buffer (R0). */
static int get_pixel(u8g2_t *u8g2, int x, int y)
{
    uint8_t *buf = u8g2_GetBufferPtr(u8g2);
    int w = u8g2_GetBufferTileWidth(u8g2) * 8;
    int tile_row = y >> 3;
    long idx = (long)tile_row * w + x;
    return (buf[idx] >> (y & 7)) & 1;
}

static void write_pbm(u8g2_t *u8g2, const char *path)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f) { perror(path); return; }
    fprintf(f, "P1\n# M1 Sub-GHz Record RAW preview - 128x64 1-bit\n%d %d\n",
            SUBGHZ_RR_LCD_W, SUBGHZ_RR_LCD_H);
    for (y = 0; y < SUBGHZ_RR_LCD_H; y++) {
        for (x = 0; x < SUBGHZ_RR_LCD_W; x++)
            fprintf(f, "%d%s", get_pixel(u8g2, x, y), (x + 1 == SUBGHZ_RR_LCD_W) ? "" : " ");
        fprintf(f, "\n");
    }
    fclose(f);
}

static void put_le32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* 24-bit BMP, scaled by S, lit pixels black on white (as the LCD reads). */
static void write_bmp(u8g2_t *u8g2, const char *path, int S)
{
    int W = SUBGHZ_RR_LCD_W * S, H = SUBGHZ_RR_LCD_H * S;
    int row_bytes = (W * 3 + 3) & ~3;
    int img_size = row_bytes * H;
    int file_size = 54 + img_size;
    uint8_t hdr[54];
    uint8_t *row;
    int x, y;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }

    memset(hdr, 0, sizeof(hdr));
    hdr[0]='B'; hdr[1]='M';
    put_le32(hdr+2, file_size);
    put_le32(hdr+10, 54);
    put_le32(hdr+14, 40);
    put_le32(hdr+18, W);
    put_le32(hdr+22, H);
    hdr[26]=1; hdr[28]=24;
    put_le32(hdr+34, img_size);
    fwrite(hdr, 1, 54, f);

    row = malloc(row_bytes);
    /* BMP rows are bottom-up. */
    for (y = H - 1; y >= 0; y--) {
        memset(row, 0, row_bytes);
        for (x = 0; x < W; x++) {
            int on = get_pixel(u8g2, x / S, y / S);
            uint8_t v = on ? 0x00 : 0xFF;   /* lit -> black */
            row[x*3+0] = v; row[x*3+1] = v; row[x*3+2] = v;
        }
        fwrite(row, 1, row_bytes, f);
    }
    free(row);
    fclose(f);
}

/* Fill a context with a synthetic but representative RSSI capture. The cursor
 * advances only when the sample is above the display threshold (trace). */
static void seed_rssi(SubGhz_RR_Ctx_t *ctx, int nsamples)
{
    static const int pat[] = { -108,-106,-104,-100,-96,-70,-52,-48,-55,-90,
                               -104,-107,-103,-60,-44,-46,-62,-98,-106,-105,
                               -101,-72,-50,-49,-58,-95,-107,-104,-100,-88 };
    int i;
    for (i = 0; i < nsamples; i++) {
        int rssi = pat[i % (int)(sizeof(pat)/sizeof(pat[0]))];
        uint8_t advance = (rssi >= ctx->threshold_dbm) ? 1u : 0u;
        subghz_rr_push_rssi(ctx, (int16_t)rssi, advance);
    }
}

int main(void)
{
    u8g2_t u8g2;
    SubGhz_RR_Ctx_t ctx;
    const char *band = "433.920";
    const char *mod  = "OOK";
    int threshold = -90;

    u8g2_Setup_st7567_enh_dg128064i_f(&u8g2, U8G2_R0, dummy_byte_cb, dummy_gpio_cb);
    u8g2_SetFontMode(&u8g2, 1);

    /* Ready: partially-filled live RSSI. */
    subghz_rr_ctx_reset(&ctx, threshold);
    seed_rssi(&ctx, 60);
    subghz_rr_draw_ready(&u8g2, &ctx, band, mod);
    write_pbm(&u8g2, "out/ready.pbm");
    write_bmp(&u8g2, "out/ready.bmp", 6);

    /* Recording: full history + sample count. */
    subghz_rr_ctx_reset(&ctx, threshold);
    seed_rssi(&ctx, 240);
    ctx.sample_total = 1327;
    subghz_rr_draw_recording(&u8g2, &ctx, band, mod);
    write_pbm(&u8g2, "out/recording.pbm");
    write_bmp(&u8g2, "out/recording.bmp", 6);

    /* Recording left-to-right progression: history grows from the LEFT edge and
     * the cursor advances rightward as samples arrive (additive proof frames). */
    {
        int p; static const int counts[3] = { 30, 90, 200 };
        char pbm[48], bmp[48];
        for (p = 0; p < 3; p++) {
            subghz_rr_ctx_reset(&ctx, threshold);
            seed_rssi(&ctx, counts[p]);
            ctx.sample_total = (uint32_t)counts[p] * 40u;
            subghz_rr_draw_recording(&u8g2, &ctx, band, mod);
            snprintf(pbm, sizeof(pbm), "out/recording_prog_%d.pbm", p + 1);
            snprintf(bmp, sizeof(bmp), "out/recording_prog_%d.bmp", p + 1);
            write_pbm(&u8g2, pbm);
            write_bmp(&u8g2, bmp, 6);
        }
    }

    /* Capture complete (baseline M1 layout). */
    ctx.sample_total = 4096;
    subghz_rr_draw_complete(&u8g2, &ctx, band, mod);
    write_pbm(&u8g2, "out/complete.pbm");
    write_bmp(&u8g2, "out/complete.bmp", 6);

    /* Replay ready (baseline M1 layout). */
    subghz_rr_draw_replay_ready(&u8g2, &ctx, band, mod);
    write_pbm(&u8g2, "out/replay_ready.pbm");
    write_bmp(&u8g2, "out/replay_ready.bmp", 6);

    /* Transmitting: 4 consecutive animation frames proving motion + wrap. */
    {
        int f;
        char pbm[48], bmp[48];
        /* Start near the wrap boundary so a packet visibly wraps within 4 ticks. */
        ctx.tx_phase = 98;
        for (f = 0; f < 4; f++) {
            snprintf(pbm, sizeof(pbm), "out/transmitting_%d.pbm", f + 1);
            snprintf(bmp, sizeof(bmp), "out/transmitting_%d.bmp", f + 1);
            subghz_rr_draw_transmitting(&u8g2, &ctx, band, mod);
            write_pbm(&u8g2, pbm);
            write_bmp(&u8g2, bmp, 6);
            subghz_rr_tx_advance(&ctx);   /* exactly one ~100 ms tick */
        }
        /* Keep a canonical single-frame name too. */
        subghz_rr_draw_transmitting(&u8g2, &ctx, band, mod);
        write_pbm(&u8g2, "out/transmitting.pbm");
        write_bmp(&u8g2, "out/transmitting.bmp", 6);
    }

    printf("previews written to out/*.pbm and out/*.bmp\n");
    return 0;
}
