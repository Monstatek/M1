#!/usr/bin/env python3
"""Exercise the production variable-length GATT hex editor with button events."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from m1_pass1_memory_safety_test import function

source = (Path(__file__).resolve().parents[1] / "m1_virtual_kb.c").read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define pdTRUE 1
#define portMAX_DELAY 0
#define Q_EVENT_KEYPAD 1
#define BUTTON_EVENT_CLICK 1
#define BUTTON_BACK_KP_ID 0
#define BUTTON_OK_KP_ID 1
#define BUTTON_RIGHT_KP_ID 2
#define BUTTON_LEFT_KP_ID 3
#define BUTTON_DOWN_KP_ID 4
#define BUTTON_UP_KP_ID 5
#define M1_VKBS_DEFAULT_KEY_MAP_ROW_ID 0
#define M1_VKBS_DEFAULT_KEY_MAP_COL_ID 0
#define M1_VIRTUAL_KBS_COLUMN_SIZE 9
#define M1_VIRTUAL_KBS_ROW_SIZE 2
typedef int BaseType_t;
typedef struct { int event[6]; } S_M1_Buttons_Status;
typedef struct { int q_evt_type; } S_M1_Main_Q_t;
typedef enum { M1_VKB_FUNC_UNDEFINED_KEY_ID, M1_VKB_FUNC_BS_KEY_ID,
               M1_VKB_FUNC_ENTER_KEY_ID } S_M1_VKB_Func_Key_ID;
static const uint8_t m1_vkbs_map[2][9] = {
    {'1','2','3','4','5','A','B','C',8},
    {'6','7','8','9','0','D','E','F',10}
};
static S_M1_VKB_Func_Key_ID m1_vkb_check_function_key(uint8_t key) {
    return key==8?M1_VKB_FUNC_BS_KEY_ID:key==10?M1_VKB_FUNC_ENTER_KEY_ID:M1_VKB_FUNC_UNDEFINED_KEY_ID;
}
static uint8_t m1_kb_col_wrap(uint8_t col,int step,uint8_t count) {
    return (uint8_t)((col+count+step)%count);
}
static int main_q_hdl=1, button_events_q_hdl=2;
static int events[300], event_count, event_pos, resets, renders, warnings;
static void vkbs_render(const char *title,const char *value,uint8_t width,uint8_t cursor,
                        uint8_t row,uint8_t col) {
    (void)value;(void)row;(void)col;assert(width>=1&&width<=128&&cursor<width);
    if (title && !strcmp(title,"Enter whole bytes")) warnings++;
    renders++;
}
static BaseType_t xQueueReceive(int queue,void *out,int timeout) {
    (void)timeout;
    assert(event_pos<event_count);
    if (queue==main_q_hdl) ((S_M1_Main_Q_t *)out)->q_evt_type=Q_EVENT_KEYPAD;
    else { S_M1_Buttons_Status *b=out;memset(b,0,sizeof(*b));b->event[events[event_pos++]]=BUTTON_EVENT_CLICK; }
    return pdTRUE;
}
static void xQueueReset(int queue) {(void)queue;resets++;}
'''
code += function(source, "m1_vkbs_get_hex_bytes") + r'''
static void begin(void) {event_count=event_pos=resets=renders=warnings=0;}
static void key(int button) {events[event_count++]=button;}
static void enter(void) {key(BUTTON_DOWN_KP_ID);for(int i=0;i<8;i++) key(BUTTON_RIGHT_KP_ID);key(BUTTON_OK_KP_ID);}
int main(void) {
    struct {char hex[129];char guard;} output;
    begin();memset(&output,0x5a,sizeof(output));
    key(BUTTON_OK_KP_ID);key(BUTTON_OK_KP_ID);enter();
    assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,64)==2);
    assert(!strcmp(output.hex,"11")&&output.guard=='Z'&&resets==1&&renders>1);

    begin();key(BUTTON_OK_KP_ID);enter();key(BUTTON_OK_KP_ID);
    key(BUTTON_UP_KP_ID);key(BUTTON_LEFT_KP_ID);key(BUTTON_OK_KP_ID);
    key(BUTTON_DOWN_KP_ID);key(BUTTON_RIGHT_KP_ID);key(BUTTON_OK_KP_ID);
    assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,64)==2);
    assert(warnings>0&&resets==1&&output.guard=='Z');

    begin();key(BUTTON_BACK_KP_ID);
    assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,64)==0&&resets==1);
    begin();assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,0)==0);
    assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,65)==0);

    begin();for(int i=0;i<130;i++) key(BUTTON_OK_KP_ID);enter();
    assert(m1_vkbs_get_hex_bytes("Write hex",output.hex,64)==128);
    assert(strlen(output.hex)==128&&output.guard=='Z');
    puts("GATT hex editor byte length, validation, cancel and bounds PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="m1-gatt-hex-") as directory:
    cfile = Path(directory) / "hex.c"
    executable = cfile.with_suffix("")
    cfile.write_text(code)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(cfile), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
