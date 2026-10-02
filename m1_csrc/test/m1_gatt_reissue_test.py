#!/usr/bin/env python3
"""Exercise the current production GATT section with scripted UART and buttons."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from m1_pass1_memory_safety_test import function

root = Path(__file__).resolve().parents[2]
source = (root / 'm1_csrc/m1_bt.c').read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SUCCESS 0
#define ERROR 1
#define BUTTON_BACK_KP_ID 1
#define BUTTON_OK_KP_ID 2
#define BUTTON_UP_KP_ID 3
#define BUTTON_DOWN_KP_ID 4
#define BUTTON_LEFT_KP_ID 5
#define BUTTON_RIGHT_KP_ID 6
#define M1_GUI_FONT_HEIGHT 8
#define M1_GUI_ROW_SPACING 2
#define M1_DISP_DRAW_COLOR_TXT 1
#define M1_DISP_DRAW_COLOR_BG 0
#define pdMS_TO_TICKS(x) (x)
typedef uint32_t TickType_t;
static uint32_t now, cancel_at;
static int cancelled, next_button, resets, deinits, restores, services_screens;
static char message[160];
static uint8_t g_ble_ses_active;
static unsigned g_ble_scan_count;
static struct { unsigned index; } g_ble_sel;
static char g_bt_scratch[4096], g_bt_line[160];
static uint16_t g_bt_line_len;
static uint8_t g_bt_line_drop;
static int g_ble_ses_ctx, esp32_rb_hdl, button_events_q_hdl, main_q_hdl;
static int m1_u8g2;
static uint32_t xTaskGetTickCount(void) { return now; }
static void vTaskDelay(uint32_t ticks) { now += ticks; }
static void xQueueReset(int queue) { (void)queue; }
static void esp32_disable(void) { resets++; }
static void m1_esp32_deinit(void) { deinits++; }
static void bt_restore_esp32_cmd_path(const int *ctx) { (void)ctx; restores++; }
static void ble_sel_clear(void) { g_ble_sel.index=0; }
static void bt_message_screen(const char *title,const char *a,const char *b) {
    snprintf(message,sizeof(message),"%s|%s|%s",title,a?a:"",b?b:"");
}
static uint8_t bt_poll_button(void) {
    if (next_button) { int button=next_button; next_button=0; return button; }
    if (cancel_at && !cancelled && (int32_t)(now-cancel_at)>=0) {
        cancelled=1; return BUTTON_BACK_KP_ID;
    }
    return 0xFF;
}
static void m1_u8g2_firstpage(void) {}
static void m1_u8g2_nextpage(void) {}
static void u8g2_DrawStr(void *p,int x,int y,const char *s) {(void)p;(void)x;(void)y;(void)s;}
static void u8g2_SetDrawColor(void *p,int color) {(void)p;(void)color;}
static void u8g2_DrawBox(void *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
static void bt_draw_title(const char *s) {if(!strcmp(s,"GATT Services"))services_screens++;}
static void bt_draw_title_paged(const char *s,int a,int b) {(void)s;(void)a;(void)b;}
static void bt_draw_str_clipped(int a,int b,const char *s,int c) {(void)a;(void)b;(void)s;(void)c;}
static void bt_draw_scrollbar(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
static void bt_draw_actionbar(const char *a,const char *b) {(void)a;(void)b;}
static const char *bt_uuid16_name(uint16_t value) {(void)value;return NULL;}
static const char *bt_uuid16_char_name(uint16_t value) {(void)value;return NULL;}
static const char *bt_uuid16_desc_name(uint16_t value) {return value==0x2902?"CCCD":NULL;}
static const char *editor="ABCD";
static uint8_t m1_vkbs_get_hex_bytes(char *title,char *out,uint8_t max_bytes) {
    (void)title;assert(max_bytes==64);if(!editor)return 0;strcpy(out,editor);return (uint8_t)strlen(editor);
}
static uint8_t bt_session_ensure(void) {g_ble_ses_active=1;return SUCCESS;}
static int picker_count, picker_limit;
static int bt_device_picker(const char *a,const char *b) {(void)a;(void)b;return picker_count++<picker_limit?2:-1;}
static void ble_sel_set_from_item(uint16_t index) {g_ble_sel.index=index+5;}

struct reply {const char *command,*output;uint32_t delay;int fail_tx;};
static struct reply replies[12];
static unsigned reply_count, sent;
static const char *rx="", *stream="";
static uint32_t rx_at;
static void m1_esp32_reset_buffer(void) {rx="";}
static uint8_t esp32_uart_write(const uint8_t *wire,uint16_t size,uint32_t timeout) {
    (void)timeout; assert(sent<reply_count);
    const struct reply *r=&replies[sent++];
    assert(size==strlen(r->command)+2);
    assert(!memcmp(wire,r->command,size-2));
    rx=r->output;rx_at=now+r->delay;
    return !r->fail_tx;
}
static unsigned m1_ringbuffer_read(int *buffer,uint8_t *out,unsigned size) {
    (void)buffer;assert(size==1);
    if ((int32_t)(now-rx_at)<0) return 0;
    if (*rx) {*out=(uint8_t)*rx++;return 1;}
    if (*stream) {*out=(uint8_t)*stream++;return 1;}
    return 0;
}
'''
code += 'static void bt_stream_reset(void) {g_bt_line_len=0;g_bt_line_drop=0;}\n'
code += function(source, 'bt_read_line') + '\n'
begin = source.index('#define GATT_MAX_SVC')
end = source.index('} // void bluetooth_gatt_explorer(void)', begin) + len('} // void bluetooth_gatt_explorer(void)')
code += source[begin:end]
code += r'''
static void reset(void) {
    now=cancel_at=0;cancelled=next_button=resets=deinits=restores=services_screens=0;
    reply_count=sent=picker_count=0;picker_limit=1;rx=stream="";rx_at=0;message[0]=0;
    g_ble_ses_active=g_gatt_connected=1;g_gatt_remote_disconnected=0;g_ble_scan_count=1;editor="ABCD";bt_stream_reset();
}
static void reply(const char *cmd,const char *out,uint32_t delay,int fail_tx) {
    assert(reply_count<12);replies[reply_count++]=(struct reply){cmd,out,delay,fail_tx};
}
static void assert_closed(void) {
    assert(resets==1&&deinits==1&&restores==1);
    assert(!g_ble_ses_active&&!g_gatt_connected&&!g_ble_scan_count);
}
static void descriptors(void) {
    reset();
    bt_gatt_parse_services("[SVC 0] UUID16 0x1800 handles 1-20\n"
      "[CHR] UUID16 0x2A00 val=3 props=0x02\n"
      "[DSC] UUID16 0x2902 handle=4\n[DSC] UUID128 handle=5\n"
      "[DSC] UUID128 handle=0\n[DSC] UUID128 handle=65536\n"
      "[DSC] UUID128 bad\n[DSC] UUID16 0x2902\n");
    assert(g_gatt_dsc_n==2);
    assert(g_gatt_dsc[0].handle==4&&!g_gatt_dsc[0].is128);
    assert(g_gatt_dsc[1].handle==5&&g_gatt_dsc[1].is128);
    char label[40];g_gatt_map[0]=1;gatt_dsc_row(0,label,sizeof(label));
    assert(!strcmp(label,"UUID128 h=5"));
    puts("descriptor width, handle and malformed-row tests PASS");
}
static void connection(void) {
    const char *ok="[BLE:CONN] connected handle=42\n>> ";
    reset();reply("connect-only 7",ok,30000,0);assert(bt_gatt_connect(7));assert(now==30000&&!resets);
    reset();reply("connect-only 7",ok,39000,0);assert(!bt_gatt_connect(7));assert_closed();
    reset();reply("connect-only 7",ok,30000,0);cancel_at=7000;
    assert(!bt_gatt_connect(7));assert_closed();assert(now==7000);
    reset();reply("connect-only 7","[BLE:CONN] connected\n",0,0);
    assert(!bt_gatt_connect(7));assert_closed();assert(now==GATT_CONNECT_COMMAND_MS);
    reset();reply("connect-only 7",ok,0,1);assert(!bt_gatt_connect(7));assert_closed();
    reset();reply("connect-only 7","[BLE:ERR] connect failed\n>> ",0,0);
    reply("disconnect","Not connected\n>> ",0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    bt_gatt_session(7);assert(sent==3&&!resets);
    reset();reply("connect-only 7",ok,0,0);reply("services",">> ",0,0);
    reply("disconnect","[BLE:CONN] disconnected reason=0\n>> ",0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    bluetooth_gatt_explorer();assert(sent==4&&picker_count==2&&!resets);
    reset();g_gatt_svc_n=0;reply("connect-only 7",ok,0,0);
    reply("services","[SVC 0] UUID16 0x1800 handles 1-20\n"
          "[BLE:ERR] discovery output truncated\n>> ",0,0);
    reply("disconnect","[BLE:CONN] disconnected reason=0\n>> ",0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    bt_gatt_session(7);
    assert(sent==4&&!resets&&g_gatt_svc_n==0);
    assert(strstr(message,"Discovery failed")&&!strstr(message,"No services"));
    puts("30-second connect, cancel, timeout, TX failure and cleanup tests PASS");
}
static void writes(void) {
    gatt_chr_t chr={0};chr.val=5;
    reset();next_button=BUTTON_OK_KP_ID;
    reply("writenr 5 ABCD","PENDING WRITE (no-response)\n>> ",0,0);
    reply("confirm","[BLE:ERR] write failed status=5\n>> ",0,0);
    bt_gatt_write(&chr,0);assert(strstr(message,"Write failed")&&!strstr(message,"Sent"));
    reset();next_button=BUTTON_OK_KP_ID;
    reply("writenr 5 ABCD","PENDING WRITE (no-response)\n>> ",0,0);
    reply("confirm","[BLE:WRITE] sent (no response requested)\n>> ",0,0);
    bt_gatt_write(&chr,0);assert(strstr(message,"Sent")&&!resets);
    reset();next_button=BUTTON_OK_KP_ID;reply("writenr 5 ABCD",">> ",0,0);
    bt_gatt_write(&chr,0);assert(sent==1);assert_closed();
    reset();next_button=BUTTON_OK_KP_ID;
    reply("writenr 5 ABCD","PENDING WRITE (no-response)\n>> ",0,0);
    reply("confirm","",0,1);
    bt_gatt_write(&chr,0);assert_closed();assert(!strstr(message,"Sent"));
    reset();next_button=BUTTON_OK_KP_ID;
    reply("write 5 ABCD","PENDING WRITE (with-response)\n>> ",0,0);
    reply("confirm","[BLE:WRITE] ok\n>> ",0,0);
    bt_gatt_write(&chr,1);assert(strstr(message,"Write OK"));
    puts("write and write-without-response outcome tests PASS");
}
static void subscriptions(void) {
    reset();reply("subscribe 5","[BLE:ERR] subscribe failed status=5\n>> ",0,0);
    bt_gatt_notify(5,0);assert(sent==1&&strstr(message,"Subscribe failed"));
    reset();reply("subscribe 5",">> ",0,0);bt_gatt_notify(5,0);assert_closed();
    reset();reply("subscribe 5","[BLE:SUB] ok\n>> ",0,0);
    stream="[BLE:CONN] disconnected reason=531\n";bt_gatt_notify(5,0);
    assert(sent==1&&!g_gatt_connected&&g_gatt_remote_disconnected&&strstr(message,"Reason 0x0213"));
    for(unsigned indicate=0;indicate<2;indicate++) {
        reset();reply(indicate?"indicate 5":"subscribe 5","[BLE:SUB] ok\n>> ",0,0);
        reply("unsubscribe 5","[BLE:UNSUB] ok\n>> ",0,0);cancel_at=200;
        stream=indicate?"[BLE:IND] handle=5 len=1 data=42\n":"[BLE:NTF] handle=5 len=1 data=42\n";
        bt_gatt_notify(5,indicate);assert(sent==2&&g_gatt_connected&&!resets);
    }
    reset();reply("subscribe 5","[BLE:SUB] ok\n>> ",0,0);
    reply("unsubscribe 5","[BLE:ERR] unsubscribe failed status=5\n>> ",0,0);
    cancel_at=200;bt_gatt_notify(5,0);assert_closed();
    assert(strstr(message,"Unsubscribe failed"));
    reset();reply("subscribe 5","[BLE:SUB] ok\n>> ",0,0);
    reply("unsubscribe 5",">> ",0,0);cancel_at=200;
    bt_gatt_notify(5,0);assert_closed();assert(strstr(message,"Not confirmed"));
    reset();stream="[BLE:CONN] disconnected reason=531\n";
    assert(bt_gatt_wait_button()==BUTTON_BACK_KP_ID&&!g_gatt_connected);
    reset();reply("connect-only 7","[BLE:CONN] connected handle=42\n>> ",0,0);
    reply("services","[BLE:CONN] disconnected reason=531\n>> ",0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    bt_gatt_session(7);
    assert(sent==3&&!resets&&g_gatt_remote_disconnected);
    assert(strstr(message,"Reason 0x0213"));
    puts("subscribe/indicate, remote loss, unsubscribe success/error tests PASS");
}
static void remote_reselect(void) {
    const char *connected="[BLE:CONN] connected handle=42\n>> ";
    const char *services="[SVC 0] UUID16 0x1800 handles 1-5\n>> ";
    reset();picker_limit=2;
    reply("connect-only 7",connected,0,0);
    reply("services",services,0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    reply("connect-only 7",connected,0,0);
    reply("services",services,0,0);
    reply("resume","[BLE:SCAN:LIVE]\n>> ",0,0);
    stream="[BLE:CONN] disconnected reason=531\n"
           "[BLE:CONN] disconnected reason=531\n";
    bluetooth_gatt_explorer();
    assert(sent==6&&picker_count==3&&services_screens==2&&!resets);
    assert(!g_gatt_connected&&g_ble_ses_active);
    puts("remote disconnect, resumed scan, reselect and Services reopen PASS");
}
int main(void) {descriptors();connection();writes();subscriptions();remote_reselect();}
'''

with tempfile.TemporaryDirectory(prefix='m1-gatt-reissue-') as directory:
    c_file = Path(directory) / 'gatt.c'
    executable = c_file.with_suffix('')
    c_file.write_text(code)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Wno-unused-function', '-Wno-unused-variable', '-Werror',
                    '-fsanitize=address,undefined', str(c_file), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
