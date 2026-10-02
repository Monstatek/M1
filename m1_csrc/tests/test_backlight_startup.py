#!/usr/bin/env python3
"""Run production feedback initialization + LCD saver with actual manager code.
Usage: python3 m1_csrc/tests/test_backlight_startup.py [source checkout]
Mocks only scheduling, clock and hardware outputs; no device acceptance implied.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def function(file,name):
    s=(root/file).read_text()
    m=re.search(r'^(?:static )?void '+name+r'\(void\)\n\{',s,re.M)
    assert m,name
    return s[m.start():s.index('\n}',m.end())+2]
header=(root/'m1_csrc/m1_system.h').read_text()
period=re.search(r'^#define\s+LCD_SAVER_PERIOD\s+([^\r\n/]+)',header,re.M)[1].strip()
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "m1_feedback_manager.h"
#include "m1_feedback_orchestration.h"
typedef int BaseType_t;
typedef int TaskHandle_t;
#define FEEDBACK_TASK_STACK_SIZE 1
#define FEEDBACK_TASK_PRIORITY 1
#define pdPASS 1
#define configASSERT(x) assert(x)
static TaskHandle_t g_feedback_task_hdl;
static uint32_t now;
static struct {uint32_t active_timestamp;} m1_device_stat;
static uint8_t hardware_bl;
static unsigned writes;
static uint32_t HAL_GetTick(void){return now;}
static void bl_write(uint8_t p){hardware_bl=p;writes++;}
static const fb_hw_adapter_t hw={.bl_write=bl_write};
static const fb_hw_adapter_t *m1_feedback_hw_adapter_get(void){return &hw;}
static void feedback_task(void *p){(void)p;}
static BaseType_t xTaskCreate(void (*fn)(void*),const char *name,int stack,void *arg,int prio,TaskHandle_t *handle){
 (void)fn;(void)name;(void)stack;(void)arg;(void)prio;*handle=1;
 /* Must already have a normal owner before scheduling other tasks. */
 assert(fb_debug_visible_bl()==100);assert(writes==0);return pdPASS;
}
'''
main=r'''
static void tick(void){fb_manager_tick(1);}
static void button(void){now++;m1_device_stat.active_timestamp=now;lcd_saver_update();tick();}
int main(void){
 m1_feedback_task_init();
 /* Raw boot splash write; both normal and post-update status use this path. */
 hardware_bl=100;now=m1_device_stat.active_timestamp=1000;tick();
 assert(hardware_bl==100);
 /* Charger cleanup may happen before the first periodic saver check. */
 fb_pwr_charger_attached(false);tick();assert(hardware_bl==100);
 lcd_saver_update();tick();assert(hardware_bl==100);
 fb_pwr_charger_attached(true);tick();assert(hardware_bl==100);
 fb_pwr_not_charging();tick();assert(hardware_bl==100);
 button();assert(hardware_bl==100);
 /* Temporary owner's release restores normal light even before first sleep. */
 fb_net_emulating_start();tick();fb_net_emulating_stop();tick();assert(hardware_bl==100);
 fb_set_brightness_pct(37);tick();assert(hardware_bl==37);
 /* No per-tick rewrites during the awake period. */
 unsigned before=writes;
 for(unsigned i=0;i<20;i++){now++;lcd_saver_update();tick();}
 assert(writes==before);
 now=m1_device_stat.active_timestamp+LCD_SAVER_PERIOD-1;lcd_saver_update();tick();assert(hardware_bl==37);
 now++;lcd_saver_update();tick();assert(hardware_bl==0);
 /* One accepted button event must wake at the saved brightness. */
 button();assert(hardware_bl==37);
 /* A higher-priority hold still overrides sleep; release restores sleep. */
 fb_net_emulating_start();tick();now+=LCD_SAVER_PERIOD;lcd_saver_update();tick();assert(hardware_bl==37);
 fb_net_emulating_stop();tick();assert(hardware_bl==0);button();assert(hardware_bl==37);
 /* Critical-power cleanup respects both awake and sleeping baseline. */
 fb_pwr_batt_crit();tick();fb_pwr_charger_attached(false);tick();assert(hardware_bl==37);
 now+=LCD_SAVER_PERIOD;lcd_saver_update();tick();assert(hardware_bl==0);
 fb_pwr_charger_attached(true);tick();assert(hardware_bl==0);button();assert(hardware_bl==37);
 puts("PASS: startup owner before tasks; charger/temporary-owner cleanup; brightness; idle boundary; single-button wake; priority and sleeping restore; no repeated awake writes.");
}
'''
code=pre+'\n#define LCD_SAVER_PERIOD '+period+'\n'+function('m1_csrc/m1_feedback_task.c','m1_feedback_task_init')+'\n'+function('m1_csrc/m1_system.c','lcd_saver_update')+main
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DM1_FEEDBACK_HOST_TEST','-I'+str(root/'m1_csrc'),str(p/'test.c'),*[str(root/'m1_csrc'/f) for f in ['m1_feedback_manager.c','m1_feedback_sequences.c','m1_feedback_orchestration.c']],'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
