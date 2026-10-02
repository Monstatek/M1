/* Host unit test for m1_manager_espupdate.c (the ESP32 radio-update M1CP
 * dispatch shim) driving the SHARED m1_manager_update_coordinator through a
 * mock backend (no hardware): get_version, begin -> data -> finish (persists
 * the version), plus sequencing, alignment, precondition, ownership and
 * error paths. Also confirms ESP32_UPDATE is advertised and ESP_GET_VERSION
 * routes in-session. The coordinator's own generic properties (manifest
 * validation, resend idempotency) are proven directly in
 * test_m1cp_update_coordinator.c; this file's job is the ESP-specific wire
 * parsing (BEGIN's manifest layout) and version persistence.
 *
 * NOTE: the 480-byte-M1CP-frame -> 4096-byte-ROM-block STAGING is now
 * entirely internal to the real device backend's write()/finish()
 * (m1_manager_espupdate.c's own #ifndef M1CP_HOST_TEST section) rather than
 * dispatch-shim logic -- this mock backend, like the coordinator itself,
 * has no concept of it and is called once per DATA chunk exactly as sent.
 * Staging behavior is verified by m1_manager_espupdate_seam_test.c instead
 * (source-text check against the real backend, which has a genuine
 * HAL/esp_loader dependency and can't be host-linked). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_manager_espupdate.h"
#include "m1_manager_protocol.h"
#include "m1_esp_version.h"

uint32_t g_test_tick_ms = 0;

/* ---- mock backend ---- */
static bool     g_ready = true;
static uint8_t  g_begin_rc = 0, g_write_rc = 0, g_finish_rc = 0;
static uint32_t g_begin_off = 0, g_begin_size = 0;
static int      g_aborts = 0;
static uint8_t  g_prog[8192];
static uint32_t g_prog_len = 0;
static uint32_t g_write_calls = 0U;
static uint8_t  g_finish_hash[32]; static uint8_t g_finish_hash_len = 0xFFU;

static bool    be_ready(void) { return g_ready; }
static uint8_t be_begin(uint32_t off, uint32_t sz) { g_begin_off=off; g_begin_size=sz; g_prog_len=0; return g_begin_rc; }
static uint8_t be_write(const uint8_t *d, uint32_t n) {
    if (g_write_rc) return g_write_rc;
    g_write_calls++;
    if (g_prog_len + n <= sizeof g_prog) { memcpy(g_prog + g_prog_len, d, n); g_prog_len += n; }
    return 0;
}
static uint8_t be_finish(const uint8_t *hash, uint8_t hlen) {
    g_finish_hash_len = hlen;
    if (hlen != 0U) { memcpy(g_finish_hash, hash, hlen); }
    return g_finish_rc;
}
static void    be_abort(void) { g_aborts++; }
static const m1cp_update_backend_t BE =
{
    "mock-esp32", M1CP_ESP_MAX_IMAGE, 4U, false, M1CP_UPDATE_HASH_MD5, M1CP_ERR_ESP,
    be_ready, be_begin, be_write, be_finish, NULL, be_abort
};

static void reset_all(void) {
    g_ready = true; g_begin_rc = g_write_rc = g_finish_rc = 0;
    g_begin_off = g_begin_size = 0; g_aborts = 0; g_prog_len = 0; g_write_calls = 0U;
    g_finish_hash_len = 0xFFU;
    m1cp_esp_version_init();      /* -> unavailable (host load=false) */
    m1cp_espupdate_init(&BE);
}

/* ---- payload builders ---- */
static const uint8_t MD5A[16] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
                                 0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
static uint16_t mk_begin(uint8_t *p, uint32_t size, uint32_t off, const uint8_t md5[16],
                         const uint8_t ver[4], uint16_t confirm) {
    p[0]=size&0xff;p[1]=(size>>8)&0xff;p[2]=(size>>16)&0xff;p[3]=(size>>24)&0xff;
    p[4]=off&0xff;p[5]=(off>>8)&0xff;p[6]=(off>>16)&0xff;p[7]=(off>>24)&0xff;
    memcpy(p+8, md5, 16); memcpy(p+24, ver, 4);
    p[28]=confirm&0xff; p[29]=(confirm>>8)&0xff; return 30;
}
static uint16_t mk_data(uint8_t *p, uint32_t off, const uint8_t *d, uint16_t n) {
    p[0]=off&0xff;p[1]=(off>>8)&0xff;p[2]=(off>>16)&0xff;p[3]=(off>>24)&0xff; memcpy(p+4,d,n); return (uint16_t)(4+n);
}

static uint8_t g_out[16]; static uint16_t g_olen;
static uint8_t H(uint8_t type, const uint8_t *pl, uint16_t plen) { return m1cp_espupdate_handle(type, pl, plen, g_out, &g_olen); }

static int fails=0, count=0;
static void ok(int c, const char *m){ count++; if(c) printf("  ok  : %s\n", m); else { printf("  FAIL: %s\n", m); fails++; } }

/* protocol-level caps/routing capture */
static uint8_t g_last[600]; static int g_last_len=0;
static int cap_send(const uint8_t *d, uint16_t n){ if(n<=sizeof g_last){memcpy(g_last,d,n);g_last_len=n;} return n; }
static void cap_legacy(const uint8_t *d, uint16_t n){ (void)d;(void)n; }
static int build_frame(uint8_t *b,uint8_t t,uint8_t f,uint16_t s,const uint8_t*pl,uint16_t pn){
    b[0]='M';b[1]='1';b[2]='C';b[3]='P';b[4]=1;b[5]=t;b[6]=f;b[7]=s&0xff;b[8]=s>>8;b[9]=pn&0xff;b[10]=pn>>8;
    if(pn&&pl)memcpy(b+11,pl,pn); uint16_t c=m1cp_crc16(b+4,(size_t)((11-4)+pn)); b[11+pn]=c&0xff;b[12+pn]=c>>8; return 11+pn+2;
}

int main(void) {
    uint8_t pl[600];
    const uint8_t VER[4] = {2,7,1,4};
    uint8_t chunk480[480]; for (int i=0;i<480;i++) chunk480[i]=(uint8_t)i;
    uint8_t tail[40];      for (int i=0;i<40;i++)  tail[i]=(uint8_t)(0xC0+i);

    /* ---- GET_VERSION: unavailable then after a set ---- */
    reset_all();
    ok(H(M1CP_CMD_ESP_GET_VERSION, pl, 0) == M1CP_ERR_NONE && g_olen==5, "GET_VERSION ok, 5-byte resp");
    ok(g_out[0]==0 && g_out[1]==0xFF && g_out[4]==0xFF, "GET_VERSION unavailable -> state 0 + sentinel");

    /* ---- happy path: begin -> 480 -> 480 -> 40 -> finish -> version persisted ---- */
    reset_all();
    ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0x10000,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_NONE && g_olen==2, "BEGIN ok, returns max_chunk");
    ok((g_out[0]|(g_out[1]<<8)) == M1CP_ESP_CHUNK_MAX, "BEGIN max_chunk == 480");
    ok(g_begin_off==0x10000 && g_begin_size==1000, "backend flash_start(offset,size)");
    ok(m1cp_espupdate_busy(), "busy (owns radio) after BEGIN");
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 0 ok");
    { uint32_t nx=g_out[0]|(g_out[1]<<8)|(g_out[2]<<16)|((uint32_t)g_out[3]<<24); ok(nx==480,"next_offset 480"); }
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,480,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 1 ok");
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,960,tail,40)) == M1CP_ERR_NONE, "DATA final 40B ok");
    ok(g_write_calls==3U, "each DATA chunk reaches the backend's write() directly (staging, if any, is backend-internal)");
    ok(H(M1CP_CMD_ESP_UPDATE_FINISH, pl, 0) == M1CP_ERR_NONE, "FINISH ok (MD5 verified by backend)");
    ok(g_prog_len==1000 && memcmp(g_prog,chunk480,480)==0 && memcmp(g_prog+960,tail,40)==0, "streamed bytes match");
    ok(g_finish_hash_len==16U && memcmp(g_finish_hash, MD5A, 16)==0, "FINISH passed the exact client MD5 to backend");
    ok(!m1cp_espupdate_busy(), "idle after FINISH (radio released)");
    { uint8_t ev[4]; ok(m1cp_esp_version_get(ev) && memcmp(ev,VER,4)==0, "installed version persisted (2.7.1.4)"); }
    ok(H(M1CP_CMD_ESP_GET_VERSION, pl, 0)==M1CP_ERR_NONE && g_out[0]==1 && g_out[1]==2 && g_out[4]==4, "GET_VERSION now reports 2.7.1.4");

    /* ---- BEGIN validation ---- */
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,0x0000)) == M1CP_ERR_BAD_ARG, "BEGIN wrong confirm -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,0,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_ARG, "BEGIN size 0 -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,M1CP_ESP_MAX_IMAGE+4,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_ARG, "BEGIN oversize -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1002,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_ARG, "BEGIN size not 4-aligned -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0x10800,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_ARG, "BEGIN offset not 4K-aligned -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_ESP_MAX_OFFSET+0x1000,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_ARG, "BEGIN offset too high -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, 29) == M1CP_ERR_BAD_PAYLOAD, "BEGIN short payload -> BAD_PAYLOAD");

    /* ---- ownership / precondition failures ---- */
    reset_all(); g_ready=false; ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BAD_STATE, "BEGIN low battery -> BAD_STATE");
    reset_all(); g_begin_rc=1; ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BUSY, "BEGIN radio busy -> BUSY");
    reset_all(); g_begin_rc=2; ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_ESP, "BEGIN connect fail -> ERR_ESP (hard_err_code)");
    ok(g_aborts>=1, "backend released on BEGIN failure");

    /* ---- DATA ordering / alignment / bounds -- each gets its own fresh
     * BEGIN since a genuine BAD_ARG cleanly ends the session. ---- */
    reset_all(); ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,4)) == M1CP_ERR_BAD_STATE, "DATA before BEGIN -> BAD_STATE");

    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,4,chunk480,4)) == M1CP_ERR_BAD_ARG, "DATA offset ahead of received -> BAD_ARG");
    ok(!m1cp_espupdate_busy() && g_aborts>=1, "radio released after a genuine (non-resend) BAD_ARG");

    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,10)) == M1CP_ERR_BAD_ARG, "non-final chunk not 4-mult -> BAD_ARG");

    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    { static uint8_t big[481]; ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,big,481)) == M1CP_ERR_BAD_ARG, "chunk > max (481) -> BAD_ARG"); }

    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,500,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,480,chunk480,32)) == M1CP_ERR_BAD_ARG, "overrun beyond image_size -> BAD_ARG");

    /* ---- resent-chunk idempotency (the actual WEB-004 hardware bug: a
     * lost/CRC-failed SUCCESS response makes the client resend a chunk the
     * device already accepted, at an offset now behind received) ---- */
    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 0 (first send) ok");
    { uint32_t calls_before = g_write_calls;
      uint8_t rc = H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));  /* exact resend, client's own SUCCESS response was lost */
      ok(rc == M1CP_ERR_NONE, "exact resend of an already-fully-accepted chunk -> ACK, not BAD_ARG");
      { uint32_t nx=g_out[0]|(g_out[1]<<8)|(g_out[2]<<16)|((uint32_t)g_out[3]<<24); ok(nx==480,"resend's next_offset is still 480 (unchanged, not double-counted)"); }
      ok(g_write_calls==calls_before, "resent bytes are not reprocessed/re-written");
      ok(m1cp_espupdate_busy(), "session stays alive across an idempotent resend");
    }
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,480,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 1 after a resend still lands at the right offset");
    { static uint8_t tail2[40]; ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,960,tail2,40)) == M1CP_ERR_NONE, "DATA final chunk after a resend completes the image"); }
    ok(H(M1CP_CMD_ESP_UPDATE_FINISH, pl, 0) == M1CP_ERR_NONE, "FINISH still succeeds after a mid-transfer resend");

    /* A resend that only PARTIALLY overlaps already-accepted bytes is not a
     * shape any real client retry produces -- still a genuine desync,
     * still BAD_ARG. */
    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));  /* received now 480 */
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,200,chunk480,480)) == M1CP_ERR_BAD_ARG, "resend starting behind received but extending past it (partial overlap) -> still BAD_ARG");

    /* ---- write failure aborts ---- */
    reset_all(); g_write_rc=2; H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    ok(H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)) == M1CP_ERR_ESP, "write fail -> ERR_ESP (hard_err_code)");
    ok(!m1cp_espupdate_busy() && g_aborts>=1, "radio released after write failure");

    /* ---- FINISH incomplete / integrity ---- */
    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_ESP_UPDATE_FINISH, pl, 0) == M1CP_ERR_BAD_STATE, "FINISH incomplete -> BAD_STATE");
    reset_all(); g_finish_rc=1; H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,480,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_ESP_UPDATE_FINISH, pl, 0) == M1CP_ERR_INTEGRITY, "FINISH MD5 mismatch -> INTEGRITY");
    { uint8_t ev[4]; ok(!m1cp_esp_version_get(ev), "no version persisted on integrity failure"); }

    /* ---- FINISH soft success: verify COMMAND failed (rc 3) but blocks OK ---- */
    reset_all(); g_finish_rc=3; H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,480,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    H(M1CP_CMD_ESP_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_ESP_UPDATE_FINISH, pl, 0) == M1CP_ERR_NONE, "FINISH verify-command-fail (rc3) -> success (completes)");
    { uint8_t ev[4]; ok(m1cp_esp_version_get(ev) && memcmp(ev,VER,4)==0, "version persisted on soft-verify success"); }
    ok(!m1cp_espupdate_busy(), "idle after soft-verify success");

    /* ---- BUSY: second BEGIN while receiving ---- */
    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    ok(H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM)) == M1CP_ERR_BUSY, "second BEGIN while active -> BUSY");

    /* ---- ABORT + reset release the radio ---- */
    ok(H(M1CP_CMD_ESP_UPDATE_ABORT, pl, 0) == M1CP_ERR_NONE, "ABORT ok");
    ok(!m1cp_espupdate_busy() && g_aborts>=1, "idle + released after ABORT");
    reset_all(); H(M1CP_CMD_ESP_UPDATE_BEGIN, pl, mk_begin(pl,1000,0,MD5A,VER,M1CP_ESP_BEGIN_CONFIRM));
    m1cp_espupdate_reset();
    ok(!m1cp_espupdate_busy() && g_aborts==1, "reset() releases the radio");
    reset_all(); ok(H(0x35, pl, 0) == M1CP_ERR_NOT_IMPLEMENTED, "unknown 0x35 -> NOT_IMPLEMENTED");

    /* ---- protocol level: ESP32_UPDATE advertised; GET_VERSION routes in-session ---- */
    m1cp_init(cap_send, cap_legacy);
    m1cp_espupdate_init(&BE);
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_HELLO,0,1,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      const uint8_t *p=g_last+11; uint32_t caps=(uint32_t)(p[3]|(p[4]<<8)|(p[5]<<16)|((uint32_t)p[6]<<24));
      ok((caps & M1CP_CAP_ESP32_UPDATE)!=0, "ESP32_UPDATE advertised (flash path enabled)");
      ok((caps & M1CP_CAP_FILESYSTEM)!=0, "FILESYSTEM still advertised"); }
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_ESP_GET_VERSION,0,2,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      ok(g_last[5]==M1CP_CMD_ESP_GET_VERSION && (g_last[6]&M1CP_FLAG_RESPONSE), "ESP_GET_VERSION routed in-session -> RESPONSE"); }
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_ESP_UPDATE_ABORT,0,3,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      ok((g_last[6]&M1CP_FLAG_ACK) && g_last[5]==M1CP_CMD_ESP_UPDATE_ABORT, "ESP_UPDATE_ABORT routed in-session -> ACK"); }
    { /* a genuine (non-resend) BAD_ARG now carries the shared diagnostic NACK body, including the ESP-specific UART counters */
      uint8_t beginpl[30]; uint16_t n2 = mk_begin(beginpl, 1000, 0, MD5A, VER, M1CP_ESP_BEGIN_CONFIRM);
      uint8_t f[64]; int n=build_frame(f,M1CP_CMD_ESP_UPDATE_BEGIN,0,4,beginpl,n2); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      uint8_t f2[64]; int n3=build_frame(f2,M1CP_CMD_ESP_UPDATE_DATA,0,5,beginpl,4); m1cp_rx_from_isr(f2,(uint16_t)n3,NULL);
      ok((g_last[6]&M1CP_FLAG_NACK) && g_last[5]==M1CP_CMD_ESP_UPDATE_DATA, "DATA zero-length -> NACK routed in-session");
      ok(g_last[9]==23, "NACK body is 23 bytes (err+step+code+md5+uartDrop+uartOre) on this target"); }

    printf("\n%s (%d/%d)\n", fails==0?"ALL PASS":"FAILURES", count-fails, count);
    fflush(stdout);
    return fails?1:0;
}
