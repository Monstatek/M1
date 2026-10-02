#!/usr/bin/env python3
"""Execute production BLE parser, picker, UART reader and meter loop with host I/O.
The actual ring buffer is linked, not reimplemented. Run with Python 3.
"""
from pathlib import Path
import re, subprocess, tempfile
from m1_pass1_memory_safety_test import function
root = Path(__file__).resolve().parents[2]
src = (root/'m1_csrc/m1_bt.c').read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "m1_ble_signal.h"
#include "m1_ring_buffer.h"
#define BLE_SCAN_NAME_MAX 40
#define BLE_SCAN_MAX_RESULTS 64
#define BSSID_STR_SIZE 18
#define SUCCESS 0
#define ERROR 1
#define BUTTON_BACK_KP_ID 1
#define pdMS_TO_TICKS(x) (x)
typedef uint32_t TickType_t;
static TickType_t now;
static TickType_t xTaskGetTickCount(void) { return now; }
static void vTaskDelay(unsigned n) { now += n; }
static int main_q_hdl;
static void xQueueReset(int q) { (void)q; }
S_M1_RingBuffer esp32_rb_hdl;
static uint8_t ring[512];
static void feed(const char *s) { assert(m1_ringbuffer_write(&esp32_rb_hdl, (uint8_t *)s, strlen(s)) == strlen(s)); }
'''
for typ in ('ble_scan_item_t', 'ble_selected_t', 'bt_pick_row_t'):
    end=src.index('} '+typ+';')+len('} '+typ+';')
    start=src.rfind('typedef struct {',0,end)
    code+=src[start:end]+'\n'
code+=r'''
static ble_scan_item_t g_ble_scan_items[64];
static uint16_t g_ble_scan_count;
static ble_selected_t g_ble_sel;
static bt_pick_row_t g_pick[64];
static uint16_t g_pick_n;
static char g_bt_line[160], g_bt_scratch[4096];
static uint16_t g_bt_line_len;
static uint8_t g_bt_line_drop;
'''
for name in ('bt_scan_parse_output','ble_get_display_name','ble_sel_set_from_item',
             'bt_scan_index_of','bt_pick_index_of','bt_pick_sync','bt_stream_reset','bt_read_line'):
    code+=(re.search(r'^static void bt_stream_reset.*$',src,re.M).group() if name=='bt_stream_reset' else function(src,name))+'\n'
code+=r'''
static int draws, stops, resumes, polls, scenario;
static uint8_t states[20]; static int values[20];
static void bt_signal_draw(uint8_t state,int avg,const char *cat) {
    assert(cat); assert(draws<20); states[draws]=state; values[draws++]=avg;
}
static void bt_message_screen(const char *a,const char *b,const char *c) { (void)a;(void)b;(void)c; assert(0); }
static void bt_session_resume_scan(void) { resumes++; }
static uint8_t bt_cmd_process(const char *cmd,char *buf,uint16_t size,uint32_t timeout) {
    (void)timeout;
    if (!strcmp(cmd,"signal stop")) { stops++; return SUCCESS; }
    assert(draws==1); assert(!strcmp(cmd,"signal AA:BB:CC:DD:12:34"));
    snprintf(buf,size,"[BLE:SIG:START]\n%s>> ",scenario==0 ? "[BLE:SIG] raw=-53 avg=-53 cat=0 age=0\n" : "");
    return SUCCESS;
}
static uint8_t bt_poll_button(void) {
    polls++;
    if (scenario==0) {
        if (polls==1) feed("[BLE:SIG:LOST]\n");
        if (polls==2) feed("[BLE:SIG] raw=-61 avg=-56 cat=1 age=0\n");
        if (polls==3) return BUTTON_BACK_KP_ID;
    } else {
        if (polls==1) now += 6000;
        if (polls==2) return BUTTON_BACK_KP_ID;
    }
    return 255;
}
'''
code+=function(src,'bt_signal_run')
code+=r'''
int main(void) {
    m1_ringbuffer_init(&esp32_rb_hdl,ring,sizeof(ring),1);
    const char *rows="[07] AA:BB:CC:DD:12:34 RSSI=-52 AGE=0 MFG=65535 NAME=\n"
                     "[09] AA:BB:CC:DD:56:78 RSSI=-70 AGE=0 MFG=65535 NAME=\n"
                     "[12] AA:BB:CC:DD:90:12 RSSI=-60 AGE=0 MFG=65535 NAME=Sensor\n";
    g_ble_scan_count=bt_scan_parse_output(rows); assert(g_ble_scan_count==3);
    bt_pick_sync(0); assert(!strcmp(g_pick[0].mac,"AA:BB:CC:DD:12:34"));
    assert(strstr(g_pick[0].disp,"Unnamed") && strstr(g_pick[0].disp,"12:34"));
    assert(strstr(g_pick[2].disp,"56:78"));
    ble_sel_set_from_item(0); assert(g_ble_sel.index==7 && g_ble_sel.rssi==-52);
    g_ble_scan_items[1].rssi=-30; strcpy(g_ble_scan_items[0].name,"Response name");
    bt_pick_sync(1); assert(!strcmp(g_pick[0].mac,g_ble_sel.mac));
    assert(!strcmp(g_pick[0].disp,"Response name"));
    bt_pick_sync(0); assert(!strcmp(g_pick[0].mac,"AA:BB:CC:DD:56:78"));
    char label[40]; m1_ble_label(label,sizeof(label),"","AA:BB:CC:DD:12:34",76);
    assert(strstr(label,"Apple"));
    assert(!strcmp(m1_ble_category(-55),"Excellent")); assert(!strcmp(m1_ble_category(-56),"Good"));
    assert(!strcmp(m1_ble_category(-67),"Good")); assert(!strcmp(m1_ble_category(-68),"Fair"));
    assert(!strcmp(m1_ble_category(-77),"Fair")); assert(!strcmp(m1_ble_category(-78),"Weak"));
    assert(!strcmp(m1_ble_category(-87),"Weak")); assert(!strcmp(m1_ble_category(-88),"Very Weak"));
    char line[160]; int avg=-52;
    feed("[BLE:SIG] raw=-61 av"); assert(bt_read_line(line,sizeof(line),2)==0);
    feed("g=-60 cat=1 age=0\r\n"); assert(bt_read_line(line,sizeof(line),2)>0);
    assert(m1_ble_signal_record(line,&avg) && avg==-60);
    assert(!m1_ble_signal_record("[BLE:SIG] avg=127",&avg));
    assert(!m1_ble_signal_record("[BLE:SIG] avg=bad",&avg));
    assert(!m1_ble_signal_record("[BLE:SIG:START]",&avg) && avg==-60);
    char longline[230]; memset(longline,'x',sizeof(longline)-1); longline[229]=0; feed(longline);
    feed("[BLE:SIG] avg=-1\n"); assert(bt_read_line(line,sizeof(line),2)==0);
    feed("[BLE:SIG] avg=-62\n"); assert(bt_read_line(line,sizeof(line),2)>0);
    assert(m1_ble_signal_record(line,&avg) && avg==-62);
    bt_signal_run(); assert(values[0]==-52 && states[0]==1);
    assert(values[1]==-53); assert(states[2]==2 && values[2]==-53);
    assert(states[3]==1 && values[3]==-56); assert(stops==1 && resumes==1);
    scenario=1; draws=polls=0; bt_signal_run(); assert(states[2]==2 && values[2]==-52);
    scenario=2; draws=polls=0; g_ble_sel.rssi=127; bt_signal_run(); assert(states[0]==0 && states[1]==0);
    assert(stops==3 && resumes==3);
    puts("BLE parser/picker/immediate RSSI/loss/recovery/BACK/split UART: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    t=Path(tmp)
    (t/'stm32h5xx_hal.h').write_text('#include <stdint.h>\n')
    (t/'main.h').write_text('#include <assert.h>\n#define taskENTER_CRITICAL() ((void)0)\n#define taskEXIT_CRITICAL() ((void)0)\n')
    (t/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-fsanitize=address,undefined','-I',str(t),'-I',str(root/'m1_csrc'),str(t/'test.c'),str(root/'m1_csrc/m1_ring_buffer.c'),'-o',str(t/'test')],check=True)
    subprocess.run([str(t/'test')],check=True)
