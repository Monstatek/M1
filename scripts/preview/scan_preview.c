/*
 * scan_preview.c - host preview for the active Sub-GHz Scan screen.
 * Links in-tree U8g2 + m1_display_data + m1_subghz_scan_draw. Renders upright
 * (U8G2_R0) 128x64. See recordraw_preview.c for the rotation note.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u8g2.h"
#include "m1_subghz_scan_draw.h"

static uint8_t bcb(u8x8_t*a,uint8_t b,uint8_t c,void*d){(void)a;(void)b;(void)c;(void)d;return 1;}
static uint8_t gcb(u8x8_t*a,uint8_t b,uint8_t c,void*d){(void)a;(void)b;(void)c;(void)d;return 1;}
static int gp(u8g2_t*u,int x,int y){uint8_t*b=u8g2_GetBufferPtr(u);int w=u8g2_GetBufferTileWidth(u)*8;return (b[(long)(y>>3)*w+x]>>(y&7))&1;}
static void wpbm(u8g2_t*u,const char*p){FILE*f=fopen(p,"w");int x,y;if(!f)return;fprintf(f,"P1\n# M1 Sub-GHz Scan preview 128x64 1-bit\n128 64\n");for(y=0;y<64;y++){for(x=0;x<128;x++)fprintf(f,"%d%s",gp(u,x,y),x==127?"":" ");fprintf(f,"\n");}fclose(f);}
static void le(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void wbmp(u8g2_t*u,const char*p,int S){int W=128*S,H=64*S,rb=(W*3+3)&~3,is=rb*H,x,y;uint8_t h[54],*r;FILE*f=fopen(p,"wb");if(!f)return;memset(h,0,54);h[0]='B';h[1]='M';le(h+2,54+is);le(h+10,54);le(h+14,40);le(h+18,W);le(h+22,H);h[26]=1;h[28]=24;le(h+34,is);fwrite(h,1,54,f);r=malloc(rb);for(y=H-1;y>=0;y--){memset(r,0,rb);for(x=0;x<W;x++){uint8_t v=gp(u,x/S,y/S)?0:0xFF;r[x*3]=r[x*3+1]=r[x*3+2]=v;}fwrite(r,1,rb,f);}free(r);fclose(f);}

int main(void){
    u8g2_t u; u8g2_Setup_st7567_enh_dg128064i_f(&u,U8G2_R0,bcb,gcb); u8g2_SetFontMode(&u,1);
    subghz_scan_draw(&u,"433.920","OOK");
    wpbm(&u,"out/subghz_scan.pbm"); wbmp(&u,"out/subghz_scan.bmp",6);
    printf("scan preview written\n"); return 0;
}
