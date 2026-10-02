/* Host unit test for m1_manager_protocol.c (Gate A: GET_STATUS + error model).
 * Compiles the REAL protocol source with M1CP_HOST_TEST. Scratchpad only. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;

static uint8_t g_last[700];
static int     g_last_len = 0;
static int     g_send_count = 0;
static int     g_legacy_bytes = 0;

static int test_send(const uint8_t *d, uint16_t n) {
    if (n <= sizeof(g_last)) { memcpy(g_last, d, n); g_last_len = n; }
    g_send_count++;
    return (int)n;
}
static void test_legacy(const uint8_t *d, uint16_t n) { (void)d; g_legacy_bytes += n; }

static int build_frame(uint8_t *buf, uint8_t type, uint8_t flags, uint16_t seq,
                       const uint8_t *pl, uint16_t plen) {
    buf[0]='M'; buf[1]='1'; buf[2]='C'; buf[3]='P';
    buf[4]=0x01; buf[5]=type; buf[6]=flags;
    buf[7]=(uint8_t)(seq & 0xff); buf[8]=(uint8_t)(seq >> 8);
    buf[9]=(uint8_t)(plen & 0xff); buf[10]=(uint8_t)(plen >> 8);
    if (plen && pl) memcpy(buf+11, pl, plen);
    uint16_t crc = m1cp_crc16(buf+4, (size_t)((11-4)+plen));
    buf[11+plen]=(uint8_t)(crc & 0xff); buf[12+plen]=(uint8_t)(crc >> 8);
    return 11+plen+2;
}

static void feed(uint8_t type, uint8_t flags, uint16_t seq, const uint8_t *pl, uint16_t plen) {
    uint8_t f[700];
    int n = build_frame(f, type, flags, seq, pl, plen);
    g_send_count = 0; g_last_len = 0;
    m1cp_rx_from_isr(f, (uint16_t)n, NULL);
}

static int fails = 0, count = 0;
static void ok(int cond, const char *msg) {
    count++;
    if (cond) printf("  ok  : %s\n", msg);
    else { printf("  FAIL: %s\n", msg); fails++; }
}

/* response accessors on g_last */
static uint8_t  r_type(void)  { return g_last[5]; }
static uint8_t  r_flags(void) { return g_last[6]; }
static uint16_t r_seq(void)   { return (uint16_t)(g_last[7] | (g_last[8] << 8)); }
static uint16_t r_plen(void)  { return (uint16_t)(g_last[9] | (g_last[10] << 8)); }
static const uint8_t *r_pl(void) { return g_last + 11; }
static uint16_t r_crc_ok(void) {
    uint16_t plen = r_plen();
    uint16_t off = (uint16_t)(11 + plen);
    uint16_t got = (uint16_t)(g_last[off] | (g_last[off+1] << 8));
    uint16_t calc = m1cp_crc16(g_last+4, (size_t)((11-4)+plen));
    return got == calc;
}

static void open_session(void) {
    feed(M1CP_CMD_HELLO, 0, 0x0001, NULL, 0);
}

int main(void) {
    m1cp_init(test_send, test_legacy);

    /* [1] CRC known vector */
    { uint8_t s[9]={0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39};
      ok(m1cp_crc16(s,9)==0x29B1, "CRC-16/CCITT-FALSE(\"123456789\")==0x29B1"); }

    /* [2] pre-session non-HELLO command is ignored (no response), replayed to legacy */
    g_legacy_bytes = 0;
    feed(M1CP_CMD_GET_STATUS, 0, 0x1111, NULL, 0);
    ok(g_send_count==0, "pre-session GET_STATUS emits no frame");
    ok(g_legacy_bytes>0, "pre-session non-HELLO replayed to legacy console");

    /* [3] HELLO opens session, HELLO_RESPONSE carries CORE|DEVICE_INFO caps */
    feed(M1CP_CMD_HELLO, 0, 0x1234, NULL, 0);
    ok(g_send_count==1 && r_type()==M1CP_CMD_HELLO && (r_flags()&M1CP_FLAG_RESPONSE),
       "HELLO -> HELLO_RESPONSE");
    ok(r_seq()==0x1234 && r_crc_ok(), "HELLO_RESPONSE echoes seq, CRC valid");
    ok(r_plen()==7, "HELLO_RESPONSE 7-byte body");
    { uint32_t caps = (uint32_t)(r_pl()[3] | (r_pl()[4]<<8) | (r_pl()[5]<<16) | (r_pl()[6]<<24));
      ok(caps==(M1CP_CAP_CORE|M1CP_CAP_DEVICE_INFO|M1CP_CAP_STM32_UPDATE|M1CP_CAP_ESP32_UPDATE|M1CP_CAP_FILESYSTEM),
         "HELLO caps = CORE|DEVICE_INFO|STM32_UPDATE|ESP32_UPDATE|FILESYSTEM"); }
    ok(m1cp_session_active(), "session active after HELLO");

    /* [4] GET_STATUS shape + idle defaults + honest sentinels */
    feed(M1CP_CMD_GET_STATUS, 0, 0x2000, NULL, 0);
    ok(g_send_count==1 && r_type()==M1CP_CMD_GET_STATUS && (r_flags()&M1CP_FLAG_RESPONSE),
       "GET_STATUS -> response");
    ok(r_plen()==43 && r_crc_ok(), "GET_STATUS 43-byte payload, CRC valid");
    { const uint8_t *p = r_pl();
      ok(p[0]==0x01, "status struct_version==1");
      ok(p[1]==0x00, "device_state==OK when idle");
      ok(p[2]==M1CP_DOMAIN_NONE && p[3]==M1CP_OP_IDLE, "op_domain NONE, op_state IDLE");
      ok(p[4]==0 && p[5]==M1CP_ERR_NONE && p[6]==0, "progress/last_error/flags zero");
      ok(p[7]==0 && p[8]==8 && p[9]==0 && p[10]==4, "stm32 fw version 0.8.0.4");
      ok(p[11]==0 && p[12]==0xFF && p[13]==0xFF && p[14]==0xFF && p[15]==0xFF,
         "resource not present -> version sentinel");
      ok(p[16]==1 && p[17]==0 && p[18]==0xFF && p[21]==0xFF, "esp present, version unavailable");
      ok(p[42]==1, "max_concurrent_xfer==1"); }

    /* [5] status publishers reflected in GET_STATUS */
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_RECEIVING, 42);
    m1cp_status_set_error(M1CP_ERR_NO_SPACE);
    m1cp_status_set_flag(M1CP_STF_TRANSFER_ACTIVE, true);
    { uint8_t rv[4]={1,2,3,4}; m1cp_status_set_resource(1, rv); }
    feed(M1CP_CMD_GET_STATUS, 0, 0x2001, NULL, 0);
    { const uint8_t *p = r_pl();
      ok(p[1]==M1CP_DEV_BUSY, "device_state BUSY during op");
      ok(p[2]==M1CP_DOMAIN_FS && p[3]==M1CP_OP_RECEIVING && p[4]==42, "domain/state/progress published");
      ok(p[5]==M1CP_ERR_NO_SPACE, "last_error published");
      ok((p[6]&M1CP_STF_TRANSFER_ACTIVE)!=0, "transfer_active flag set");
      ok(p[11]==1 && p[12]==1 && p[13]==2 && p[14]==3 && p[15]==4, "resource version published"); }
    /* failed state -> device_state FAULT */
    m1cp_status_set(M1CP_DOMAIN_FS, M1CP_OP_FAILED, 0);
    feed(M1CP_CMD_GET_STATUS, 0, 0x2002, NULL, 0);
    ok(r_pl()[1]==M1CP_DEV_FAULT, "device_state FAULT when op failed");
    /* clear resource -> sentinel again */
    m1cp_status_set_resource(0, NULL);
    m1cp_status_set(M1CP_DOMAIN_NONE, M1CP_OP_IDLE, 0);

    /* [6] PING echoes payload */
    { uint8_t pl[5]={5,4,3,2,1};
      feed(M1CP_CMD_PING, 0, 0x3000, pl, 5);
      ok(r_type()==M1CP_CMD_PING && r_plen()==5 && memcmp(r_pl(),pl,5)==0, "PING echoes payload"); }

    /* [7] GET_DEVICE_INFO still fixed 56 bytes, fw 0.8.0.4, sd bytes sentinel (host) */
    feed(M1CP_CMD_GET_DEVICE_INFO, 0, 0x3001, NULL, 0);
    ok(r_plen()==56 && r_crc_ok(), "GET_DEVICE_INFO still 56 bytes");
    { const uint8_t *p = r_pl();
      ok(p[2]==0 && p[3]==8 && p[4]==0 && p[5]==4, "device-info fw 0.8.0.4");
      int allff=1; for (int i=29;i<45;i++) if (p[i]!=0xFF) allff=0;
      ok(allff, "device-info sd bytes sentinel (host build)"); }

    /* [8] unknown in-session command -> NACK ERR_UNKNOWN_CMD */
    feed(0x7E, 0, 0x4000, NULL, 0);
    ok((r_flags()&M1CP_FLAG_NACK) && r_plen()==1 && r_pl()[0]==M1CP_ERR_UNKNOWN_CMD,
       "unknown cmd -> NACK ERR_UNKNOWN_CMD");

    /* [9] bad CRC in-session -> NACK ERR_BAD_CRC */
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_PING,0,0x4001,NULL,0);
      f[n-1] ^= 0xFF; /* corrupt CRC high byte */
      g_send_count=0; m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      ok((r_flags()&M1CP_FLAG_NACK) && r_pl()[0]==M1CP_ERR_BAD_CRC, "bad CRC -> NACK ERR_BAD_CRC"); }

    /* [10] oversize payload_len -> NACK ERR_BAD_LENGTH (header rejected pre-payload) */
    { uint8_t h[11]={'M','1','C','P',0x01,M1CP_CMD_PING,0,0x02,0x50,0x01,0x02}; /* plen=0x0201=513 */
      g_send_count=0; m1cp_rx_from_isr(h,11,NULL);
      ok((r_flags()&M1CP_FLAG_NACK) && r_pl()[0]==M1CP_ERR_BAD_LENGTH, "oversize len -> NACK ERR_BAD_LENGTH"); }

    /* [11] bad version in-session -> NACK ERR_BAD_VERSION */
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_PING,0,0x4002,NULL,0);
      f[4]=0x02; /* wrong version; header check fires before CRC */
      g_send_count=0; m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      ok((r_flags()&M1CP_FLAG_NACK) && r_pl()[0]==M1CP_ERR_BAD_VERSION, "bad version -> NACK ERR_BAD_VERSION"); }

    /* [12] fragmented delivery of a GET_STATUS still parses */
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_GET_STATUS,0,0x5000,NULL,0);
      g_send_count=0;
      m1cp_rx_from_isr(f, 5, NULL);        /* partial */
      m1cp_rx_from_isr(f+5, (uint16_t)(n-5), NULL);
      ok(g_send_count==1 && r_type()==M1CP_CMD_GET_STATUS && r_seq()==0x5000, "fragmented GET_STATUS parses"); }

    /* [13] END_SESSION -> ACK, session closes */
    feed(M1CP_CMD_END_SESSION, 0, 0x6000, NULL, 0);
    ok((r_flags()&M1CP_FLAG_ACK) && r_type()==M1CP_CMD_END_SESSION, "END_SESSION -> ACK");
    ok(!m1cp_session_active(), "session closed after END_SESSION");

    /* [14] session inactivity timeout restores legacy */
    open_session();
    ok(m1cp_session_active(), "session reopened");
    g_test_tick_ms += (M1CP_SESSION_TIMEOUT_MS + 1);
    m1cp_process();
    ok(!m1cp_session_active(), "session times out after inactivity");

    printf("\n%s (%d/%d)\n", fails==0 ? "ALL PASS" : "FAILURES", count-fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
