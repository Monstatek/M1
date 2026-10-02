#!/usr/bin/env python3
"""Compile real result UI/dispatch/formatters with mocked display and queues.
Run: python3 lfrfid/test/result_render_test.py [optional source checkout]
No RF/decoder mocks are claimed as hardware validation.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def function(file, name):
    text = (root / file).read_text()
    match = re.search(r'^[\w* ]+\b' + name + r'\([^;]*?\)\s*\n\{', text, re.M)
    assert match, name
    start = match.start()
    # All selected production functions close at column zero.
    end = text.index('\n}', match.end()) + 2
    return text[start:end]

parts = []
for file, names in [
    ('lfrfid/lfrfid_protocol_em4100.c', ['protocol_em4100_get_data', 'protocol_em4100_render_data']),
    ('lfrfid/lfrfid_protocol_jablotron.c', ['jab_u64_to_hex', 'jab_card_id', 'jab_get_data', 'jab_render_data']),
    ('lfrfid/lfrfid_protocol_h10301.c', ['protocol_h10301_get_data', 'protocol_h10301_render_data']),
    ('lfrfid/lfrfid_protocol_keri.c', ['keri_get_bit', 'keri_get_bits32', 'keri_get_data', 'keri_descramble', 'keri_render_data']),
    ('lfrfid/lfrfid_protocol_nexwatch.c', ['nw_get_bit', 'nw_get_bits', 'nw_get_data', 'nw_descramble', 'nw_checksum', 'nw_render_data']),
]:
    parts.extend(function(file, name) for name in names)

pre = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t BYTE;
typedef uint16_t WORD;
#define MAKEWORD(a,b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
static struct {uint8_t uid[12], protocol; uint16_t bitrate;} lfrfid_tag_info;
'''
header = (root / 'lfrfid/lfrfid_protocol.h').read_text()
pre += re.search(r'typedef enum \{\s*LFRFIDProtocolEM4100,.*?} LFRFIDProtocol;', header, re.S)[0]

middle = r'''
typedef struct {const char *name, *manufacturer; unsigned data_size; void (*render_data)(void *,char *);} Entry;
static Entry em={"EM4100","EM-Micro",5,protocol_em4100_render_data};
static Entry hid={"H10301","HID",3,protocol_h10301_render_data};
static Entry keri={"Keri","Keri",4,keri_render_data};
static Entry nex={"Nexwatch","Honeywell",8,nw_render_data};
static Entry jab={"Jablotron","Jablotron",5,jab_render_data};
static Entry *lfrfid_protocols[LFRFIDProtocolMax]={
 [LFRFIDProtocolEM4100]=&em, [LFRFIDProtocolH10301]=&hid,
 [LFRFIDProtocolJablotron]=&jab, [LFRFIDProtocolKeri]=&keri, [LFRFIDProtocolNexwatch]=&nex};
'''
for name in ['protocol_get_name', 'protocol_get_manufacturer', 'protocol_get_data_size', 'protocol_render_data']:
    middle += function('lfrfid/lfrfid_protocol.c', name) + '\n'
middle += function('m1_csrc/m1_rfid.c', 'lfrfid_protocol_make_menu_list')

shim = r'''
#define RFID_READ_DISPLAY_PARAM_READING_READY 1
#define RFID_READ_DISPLAY_PARAM_READING_COMPLETE 2
#define RFID_READ_READING 1
#define RFID_READ_DONE 2
#define X_MENU_UPDATE_RESET 3
#define X_MENU_UPDATE_RESTORE 4
#define X_MENU_UPDATE_REFRESH 5
#define VIEW_MODE_IDLE 6
#define VIEW_MODE_LFRFID_UTIL_MENU 7
#define VIEW_MODE_LFRFID_READ_SUBMENU 8
#define M1_DISP_DRAW_COLOR_TXT 1
#define M1_DISP_MAIN_MENU_FONT_B 1
#define M1_DISP_MAIN_MENU_FONT_N 0
#define TEXT_ALIGN_LEFT 0
#define Q_EVENT_UI_LFRFID_START_READ 10
#define Q_EVENT_UI_LFRFID_STOP 11
#define Q_EVENT_KEYPAD 12
#define Q_EVENT_LFRFID_TAG_DETECTED 13
#define pdTRUE 1
#define portMAX_DELAY 0
#define BUTTON_EVENT_CLICK 1
#define BUTTON_BACK_KP_ID 0
#define BUTTON_LEFT_KP_ID 1
#define BUTTON_RIGHT_KP_ID 2
#define IDS_RETRY 1
#define IDS_MORE 2
#define FB_OWNER_RADIO 0
static int record_stat, lfrfid_uiview_gui_latest_param, lfrfid_pettag_mode, m1_u8g2;
static int lfrfid_q_hdl, main_q_hdl=1, button_events_q_hdl=2;
static int arrowleft_8x8, arrowright_8x8;
static int starts, text_count, bars, icons, beeps, stops, switched;
static char texts[4][64];
typedef int BaseType_t;
typedef struct {int q_evt_type;} S_M1_Main_Q_t;
typedef struct {int event[3];} S_M1_Buttons_Status;
static S_M1_Buttons_Status buttons;
static void lfrfid_read_update(uint8_t param);
static void lfrfid_read_create(uint8_t param);
static void m1_uiView_display_update(int p) {lfrfid_read_update(p);}
static void m1_uiView_display_switch(int v,int p) {(void)p;switched=v;}
static int xQueueReceive(int q,void *out,int ticks) {
 (void)ticks;
 if(q==button_events_q_hdl) *(S_M1_Buttons_Status*)out=buttons;
 else ((S_M1_Main_Q_t*)out)->q_evt_type=Q_EVENT_LFRFID_TAG_DETECTED;
 return pdTRUE;
}
static void xQueueReset(int q) {(void)q;}
static void m1_app_send_q_message(int q,int e) {(void)q;if(e==Q_EVENT_UI_LFRFID_START_READ)starts++;}
static void fb_net_read_start(void) {}
static void fb_net_read_stop(void) {stops++;}
static void m1_buzzer_notification(void) {beeps++;}
/* Success completion now raises the composite success alert instead of the
 * bare notification beep; count it as the one success cue the tests expect. */
static void fb_alert_success(int owner) {(void)owner;beeps++;}
static void osDelay(int ms) {(void)ms;}
static void m1_gui_submenu_update(void *a,int b,int c,int d) {(void)a;(void)b;(void)c;(void)d;}
static void u8g2_FirstPage(void *p) {(void)p;}
static void u8g2_NextPage(void *p) {(void)p;}
static void u8g2_SetDrawColor(void *p,int c) {(void)p;(void)c;}
static void u8g2_SetFont(void *p,int f) {(void)p;(void)f;}
static void m1_u8g2_nextpage(void) {}
static void m1_read_icon_draw(void *p,char c,int n) {(void)p;(void)c;(void)n;icons++;}
static const char *res_string(int i) {return i==IDS_RETRY?"Retry":"More";}
static void m1_draw_bottom_bar(void *p,int a,const char *l,const char *r,int b) {
 (void)p;(void)a;(void)b;assert(!strcmp(l,"Retry") && !strcmp(r,"More"));bars++;
}
static void m1_draw_text(void *p,int x,int y,int w,const char *s,int a) {
 (void)p;(void)x;(void)y;(void)w;(void)a;
 assert(text_count<4);snprintf(texts[text_count++],64,"%s",s?s:"");
}
'''
ui = '\n'.join(function('m1_csrc/m1_rfid.c', name) for name in
    ['lfrfid_read_kp_handler','lfrfid_read_create','lfrfid_read_update','lfrfid_read_message'])
main = r'''
static void reset(unsigned protocol) {
 memset(texts,0,sizeof(texts));memset(&buttons,0,sizeof(buttons));
 starts=text_count=bars=icons=beeps=stops=switched=0;
 record_stat=RFID_READ_READING;lfrfid_tag_info.protocol=protocol;
}
static void invalid(unsigned p) {
 reset(p);lfrfid_read_message();
 assert(starts==1 && icons==1 && text_count==0 && bars==0);
 assert(record_stat==RFID_READ_READING && !beeps && !stops);
 buttons.event[BUTTON_RIGHT_KP_ID]=BUTTON_EVENT_CLICK;lfrfid_read_kp_handler();assert(!switched);
}
static void empty(void *p,char *s) {(void)p;(void)s;}
static void clear_mid_render(void *p,char *s) {protocol_em4100_render_data(p,s);lfrfid_tag_info.protocol=255;}
static void one_line(void *p,char *s) {(void)p;strcpy(s,"Card: 123");}
int main(void) {
 /* Every uint16_t ID, including negative IDs converted to uint16_t, is safe. */
 for(unsigned p=0;p<=65535;p++) {
  char buf[64];memset(buf,0xa5,sizeof(buf));
  protocol_render_data(p,buf);
  if(p>=LFRFIDProtocolMax || !lfrfid_protocols[p]) assert(buf[0]==0);
 }
 protocol_render_data(65535,NULL);
 for(unsigned p=LFRFIDProtocolMax;p<=255;p++) invalid(p);
 invalid(LFRFIDProtocolPyramid); /* missing registry entry */
 Entry saved=em;
 em.render_data=NULL;invalid(0);
 em.render_data=empty;invalid(0); /* leaves caller buffer untouched */
 em.render_data=clear_mid_render;invalid(0);
 em=saved;em.name=NULL;invalid(0);
 em=saved;em.data_size=0;invalid(0);em=saved;
 unsigned ids[]={LFRFIDProtocolEM4100,LFRFIDProtocolH10301,LFRFIDProtocolJablotron,LFRFIDProtocolKeri,LFRFIDProtocolNexwatch};
 for(unsigned i=0;i<5;i++) {
  reset(ids[i]);memset(lfrfid_tag_info.uid,0x12,12);lfrfid_tag_info.bitrate=64;
  char expected[64]={0},title[64]={0};
  lfrfid_protocols[ids[i]]->render_data(NULL,expected);
  lfrfid_protocol_make_menu_list(ids[i],title);
  lfrfid_read_message();
  assert(!starts && !icons && bars==1 && text_count==4 && beeps==1 && stops==1);
  assert(record_stat==RFID_READ_DONE && !strcmp(texts[0],title));
  char *line=strtok(expected,"\n");
  for(unsigned n=1;n<4;n++){assert(!strcmp(texts[n],line?line:""));line=strtok(NULL,"\n");}
  buttons.event[BUTTON_RIGHT_KP_ID]=BUTTON_EVENT_CLICK;lfrfid_read_kp_handler();
  assert(switched==VIEW_MODE_LFRFID_READ_SUBMENU);
  memset(&buttons,0,sizeof(buttons));buttons.event[BUTTON_LEFT_KP_ID]=BUTTON_EVENT_CLICK;
  lfrfid_read_kp_handler();assert(starts==1 && record_stat==RFID_READ_READING);
 }
 /* One-line result remains legal: optional FC/card lines are not required. */
 em.render_data=one_line;reset(0);lfrfid_read_message();
 assert(bars==1 && !strcmp(texts[1],"Card: 123") && !texts[2][0] && !texts[3][0]);
 puts("PASS: all 65536 dispatcher IDs; invalid/missing/empty/changed results; real EM4100, H10301, Jablotron ASK, Keri, NexWatch formatters; normal UI/Retry/More; one-line result");
}
'''
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp)
    (p/'test.c').write_text(pre+'\n'.join(parts)+middle+shim+ui+main)
    subprocess.run(['cc','-std=c11','-funsigned-char','-Wno-deprecated-declarations',
                    '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                    str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
