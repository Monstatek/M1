/*
 * read_icon_preview.c
 *
 * Host-side preview harness for the NFC / RFID read-waiting screens.
 * Links the in-tree U8g2 and the shared m1_read_icon drawing module, so the
 * previews come from the exact same code that runs on the device.
 * Renders upright (U8G2_R0) 128x64 frames (see recordraw_preview.c note).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "m1_read_icon.h"

static uint8_t dummy_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{ (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr; return 1; }
static uint8_t dummy_gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{ (void)u8x8; (void)arg_int; (void)arg_ptr; (void)msg; return 1; }

static int get_pixel(u8g2_t *u8g2, int x, int y)
{
    uint8_t *buf = u8g2_GetBufferPtr(u8g2);
    int w = u8g2_GetBufferTileWidth(u8g2) * 8;
    long idx = (long)(y >> 3) * w + x;
    return (buf[idx] >> (y & 7)) & 1;
}

static void write_pbm(u8g2_t *u8g2, const char *path)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f) { perror(path); return; }
    fprintf(f, "P1\n# M1 read-waiting screen preview - 128x64 1-bit\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fprintf(f, "%d%s", get_pixel(u8g2, x, y), (x + 1 == 128) ? "" : " ");
        fprintf(f, "\n");
    }
    fclose(f);
}

static void put_le32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

static void write_bmp(u8g2_t *u8g2, const char *path, int S)
{
    int W = 128 * S, H = 64 * S;
    int row_bytes = (W * 3 + 3) & ~3;
    int img_size = row_bytes * H;
    uint8_t hdr[54];
    uint8_t *row;
    int x, y;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    memset(hdr, 0, sizeof(hdr));
    hdr[0]='B'; hdr[1]='M';
    put_le32(hdr+2, 54 + img_size);
    put_le32(hdr+10, 54);
    put_le32(hdr+14, 40);
    put_le32(hdr+18, W);
    put_le32(hdr+22, H);
    hdr[26]=1; hdr[28]=24;
    put_le32(hdr+34, img_size);
    fwrite(hdr, 1, 54, f);
    row = malloc(row_bytes);
    for (y = H - 1; y >= 0; y--) {
        memset(row, 0, row_bytes);
        for (x = 0; x < W; x++) {
            uint8_t v = get_pixel(u8g2, x / S, y / S) ? 0x00 : 0xFF; /* lit -> black */
            row[x*3+0]=v; row[x*3+1]=v; row[x*3+2]=v;
        }
        fwrite(row, 1, row_bytes, f);
    }
    free(row);
    fclose(f);
}

int main(void)
{
    u8g2_t u8g2;
    u8g2_Setup_st7567_enh_dg128064i_f(&u8g2, U8G2_R0, dummy_byte_cb, dummy_gpio_cb);
    u8g2_SetFontMode(&u8g2, 1);

    m1_read_icon_draw(&u8g2, 'N', 3);   /* NFC  */
    write_pbm(&u8g2, "out/nfc_read.pbm");
    write_bmp(&u8g2, "out/nfc_read.bmp", 6);

    m1_read_icon_draw(&u8g2, 'R', 3);   /* RFID */
    write_pbm(&u8g2, "out/rfid_read.pbm");
    write_bmp(&u8g2, "out/rfid_read.bmp", 6);

    /* Shared emulate screen (NFC == RFID): 4 animation frames. */
    {
        int f;
        char pbm[64], bmp[64];
        for (f = 0; f < 4; f++) {
            m1_read_icon_draw_emulate(&u8g2, (uint8_t)f);
            snprintf(pbm, sizeof(pbm), "out/emulate_f%d.pbm", f + 1);
            snprintf(bmp, sizeof(bmp), "out/emulate_f%d.bmp", f + 1);
            write_pbm(&u8g2, pbm);
            write_bmp(&u8g2, bmp, 6);
        }
    }

    printf("read + emulate previews written to out/*.pbm and out/*.bmp\n");
    return 0;
}
