/* Host unit test for m1_esp_version.c + its reporting (Gate D Phase 1).
 * Verifies the persisted ESP32 version flows into GET_DEVICE_INFO and
 * GET_STATUS, and the unavailable sentinel when unset. No hardware (host build
 * uses a RAM cache). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_esp_version.h"
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;

static uint8_t g_last[700]; static int g_last_len = 0;
static int test_send(const uint8_t *d, uint16_t n) { if (n <= sizeof g_last) { memcpy(g_last, d, n); g_last_len = n; } return n; }
static void test_legacy(const uint8_t *d, uint16_t n) { (void)d; (void)n; }
static int build_frame(uint8_t *b, uint8_t t, uint16_t s, const uint8_t *pl, uint16_t pn) {
  b[0]='M';b[1]='1';b[2]='C';b[3]='P';b[4]=1;b[5]=t;b[6]=0;b[7]=s&0xff;b[8]=s>>8;b[9]=pn&0xff;b[10]=pn>>8;
  if (pn&&pl) memcpy(b+11,pl,pn); uint16_t c=m1cp_crc16(b+4,(size_t)((11-4)+pn)); b[11+pn]=c&0xff;b[12+pn]=c>>8; return 11+pn+2;
}
static const uint8_t *send_cmd(uint8_t type) { uint8_t f[16]; int n=build_frame(f,type,1,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL); return g_last+11; }

static int fails=0, count=0;
static void ok(int c,const char*m){count++; if(c)printf("  ok  : %s\n",m); else {printf("  FAIL: %s\n",m); fails++;}}

int main(void) {
  /* [1] direct get/set */
  m1cp_esp_version_init();
  { uint8_t v[4]; ok(m1cp_esp_version_get(v)==false, "unavailable before any set"); }
  { uint8_t set[4]={1,2,3,0}, got[4]={0}; m1cp_esp_version_set(set);
    ok(m1cp_esp_version_get(got) && memcmp(got,set,4)==0, "get returns the set version (1.2.3.0)"); }

  /* open a session */
  m1cp_init(test_send, test_legacy);   /* re-inits esp version -> unavailable (host load=false) */
  send_cmd(M1CP_CMD_HELLO);

  /* [2] unavailable by default (after init) */
  { const uint8_t *p = send_cmd(M1CP_CMD_GET_DEVICE_INFO);
    ok(p[45]==1, "device-info esp32_present=1");
    ok(p[46]==0xFF && p[47]==0xFF && p[48]==0xFF && p[49]==0xFF, "device-info esp version unavailable (sentinel)"); }
  { const uint8_t *p = send_cmd(M1CP_CMD_GET_STATUS);
    ok(p[16]==1 && p[17]==0, "status esp32 present, ver_state=unavailable");
    ok(p[18]==0xFF && p[21]==0xFF, "status esp version sentinel"); }

  /* [3] record a version -> reported truthfully */
  { uint8_t set[4]={2,7,1,4}; m1cp_esp_version_set(set); }
  { const uint8_t *p = send_cmd(M1CP_CMD_GET_DEVICE_INFO);
    ok(p[46]==2 && p[47]==7 && p[48]==1 && p[49]==4, "device-info reports recorded version 2.7.1.4"); }
  { const uint8_t *p = send_cmd(M1CP_CMD_GET_STATUS);
    ok(p[17]==1, "status esp32_ver_state=valid");
    ok(p[18]==2 && p[19]==7 && p[20]==1 && p[21]==4, "status reports recorded version 2.7.1.4"); }

  printf("\n%s (%d/%d)\n", fails==0?"ALL PASS":"FAILURES", count-fails, count);
  fflush(stdout);
  return fails?1:0;
}
