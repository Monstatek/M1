/* Host unit test for m1_manager_fwupdate.c (the STM32 self-update M1CP
 * dispatch shim) driving the SHARED m1_manager_update_coordinator through
 * a mock flash backend (no hardware): begin -> data -> finish -> activate,
 * plus sequencing, alignment, precondition, resend-idempotency and error
 * paths. Also confirms STM32_UPDATE is advertised (flash path implemented).
 * The coordinator's own generic properties (manifest validation, resend
 * idempotency, activate-vs-immediate-complete, diagnostics) are already
 * proven directly in test_m1cp_update_coordinator.c -- this file's job is
 * only to confirm the STM32-specific wire parsing (BEGIN's manifest layout,
 * ACTIVATE's confirm token) is wired to the coordinator correctly. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_manager_fwupdate.h"
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;

/* ---- mock flash backend ---- */
static bool     g_ready = true;
static uint8_t  g_begin_rc = 0, g_write_rc = 0, g_finish_rc = 0;
static uint32_t g_begin_size = 0;
static int      g_activated = 0, g_aborts = 0;
static uint8_t  g_prog[4096];
static uint32_t g_prog_len = 0;
static uint8_t  g_finish_hash[32]; static uint8_t g_finish_hash_len = 0xFFU;

static bool    be_ready(void) { return g_ready; }
static uint8_t be_begin(uint32_t off, uint32_t sz) { (void)off; g_begin_size = sz; g_prog_len = 0; return g_begin_rc; }
static uint8_t be_write(const uint8_t *d, uint32_t n) {
    if (g_write_rc) return g_write_rc;
    if (g_prog_len + n <= sizeof g_prog) { memcpy(g_prog + g_prog_len, d, n); g_prog_len += n; }
    return 0;
}
static uint8_t be_finish(const uint8_t *hash, uint8_t hlen) {
    g_finish_hash_len = hlen;
    if (hlen != 0U) { memcpy(g_finish_hash, hash, hlen); }
    return g_finish_rc;
}
static void    be_activate(void) { g_activated++; }
static void    be_abort(void)    { g_aborts++; }
static const m1cp_update_backend_t BE =
{
    "mock-stm32", M1CP_FW_MAX_IMAGE, 16U, true, M1CP_UPDATE_HASH_NONE, M1CP_ERR_FLASH,
    be_ready, be_begin, be_write, be_finish, be_activate, be_abort
};

static void reset_all(void) {
    g_ready = true; g_begin_rc = g_write_rc = g_finish_rc = 0;
    g_begin_size = 0; g_activated = 0; g_aborts = 0; g_prog_len = 0; g_finish_hash_len = 0xFFU;
    m1cp_fwupdate_init(&BE);
}

/* ---- payload builders ----
 * BEGIN: image_size(4)|hash_algo(1)|hash(16)|confirm(2) = 23 bytes -- see
 * M1CP_FW_BEGIN_CONFIRM's own doc comment (m1_manager_fwupdate.h). */
static uint16_t mk_begin(uint8_t *p, uint32_t size, uint8_t hash_algo, const uint8_t *hash16)
{
    p[0]=size&0xff;p[1]=(size>>8)&0xff;p[2]=(size>>16)&0xff;p[3]=(size>>24)&0xff;
    p[4] = hash_algo;
    if (hash16 != NULL) { memcpy(&p[5], hash16, 16U); } else { memset(&p[5], 0, 16U); }
    p[21] = (uint8_t)(M1CP_FW_BEGIN_CONFIRM & 0xFFU);
    p[22] = (uint8_t)((M1CP_FW_BEGIN_CONFIRM >> 8) & 0xFFU);
    return 23U;
}
static uint16_t mk_begin_bad_confirm(uint8_t *p, uint32_t size)
{
    uint16_t n = mk_begin(p, size, M1CP_UPDATE_HASH_NONE, NULL);
    p[21] = 0x00U; p[22] = 0x00U;
    return n;
}
static uint16_t mk_data(uint8_t *p, uint32_t off, const uint8_t *d, uint16_t n) {
    p[0]=off&0xff;p[1]=(off>>8)&0xff;p[2]=(off>>16)&0xff;p[3]=(off>>24)&0xff; memcpy(p+4,d,n); return (uint16_t)(4+n);
}
static uint16_t mk_confirm(uint8_t *p, uint16_t c) { p[0]=c&0xff; p[1]=(c>>8)&0xff; return 2; }

static uint8_t g_out[16]; static uint16_t g_olen;
static uint8_t H(uint8_t type, const uint8_t *pl, uint16_t plen) { return m1cp_fwupdate_handle(type, pl, plen, g_out, &g_olen); }

static int fails=0, count=0;
static void ok(int c, const char *m){ count++; if(c) printf("  ok  : %s\n", m); else { printf("  FAIL: %s\n", m); fails++; } }

/* protocol-level caps capture */
static uint8_t g_last[600]; static int g_last_len=0;
static int cap_send(const uint8_t *d, uint16_t n){ if(n<=sizeof g_last){memcpy(g_last,d,n);g_last_len=n;} return n; }
static void cap_legacy(const uint8_t *d, uint16_t n){ (void)d;(void)n; }
static int build_frame(uint8_t *b,uint8_t t,uint8_t f,uint16_t s,const uint8_t*pl,uint16_t pn){
    b[0]='M';b[1]='1';b[2]='C';b[3]='P';b[4]=1;b[5]=t;b[6]=f;b[7]=s&0xff;b[8]=s>>8;b[9]=pn&0xff;b[10]=pn>>8;
    if(pn&&pl)memcpy(b+11,pl,pn); uint16_t c=m1cp_crc16(b+4,(size_t)((11-4)+pn)); b[11+pn]=c&0xff;b[12+pn]=c>>8; return 11+pn+2;
}

int main(void) {
    uint8_t pl[600];
    uint8_t chunk480[480]; for (int i=0;i<480;i++) chunk480[i]=(uint8_t)i;
    uint8_t tail[40];       for (int i=0;i<40;i++)  tail[i]=(uint8_t)(0xC0+i);
    uint8_t md5a[16];       for (int i=0;i<16;i++)  md5a[i]=(uint8_t)(0xA0+i);

    /* ---- happy path: begin (no hash) -> 2x480 -> 40 -> finish -> activate ---- */
    reset_all();
    ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl, 1000, M1CP_UPDATE_HASH_NONE, NULL)) == M1CP_ERR_NONE && g_olen==2, "BEGIN ok, returns max_chunk");
    ok((g_out[0] | (g_out[1]<<8)) == M1CP_FW_CHUNK_MAX, "BEGIN max_chunk == 480");
    ok(g_begin_size == 1000, "backend begin() called with image_size");
    ok(m1cp_fwupdate_busy(), "busy after BEGIN");
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 0 ok");
    { uint32_t nx=g_out[0]|(g_out[1]<<8)|(g_out[2]<<16)|((uint32_t)g_out[3]<<24); ok(nx==480,"next_offset 480"); }
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,480,chunk480,480)) == M1CP_ERR_NONE, "DATA chunk 1 ok");
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,960,tail,40)) == M1CP_ERR_NONE, "DATA final 40B (non-16-mult allowed on last)");
    ok(g_prog_len==1000, "backend programmed 1000 bytes");
    ok(memcmp(g_prog,chunk480,480)==0 && memcmp(g_prog+960,tail,40)==0, "programmed bytes match stream");
    ok(H(M1CP_CMD_FW_UPDATE_FINISH, pl, 0) == M1CP_ERR_NONE, "FINISH ok (CRC verified by backend)");
    ok(g_finish_hash_len==0U, "finish() got hash_len 0 -- no client hash was supplied");
    ok(H(M1CP_CMD_FW_UPDATE_ACTIVATE, pl, mk_confirm(pl,0xA5C3)) == M1CP_ERR_NONE, "ACTIVATE ok (ACK)");
    ok(m1cp_fwupdate_activate_pending(), "activate pending after ACK");
    m1cp_fwupdate_do_activate();
    ok(g_activated==1, "deferred activate invoked backend swap+reset");

    /* ---- happy path with a client-supplied MD5 (additive to the CRC32) ---- */
    reset_all();
    H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl, 480, M1CP_UPDATE_HASH_MD5, md5a));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_FW_UPDATE_FINISH, pl, 0) == M1CP_ERR_NONE, "FINISH ok with a client MD5 supplied");
    ok(g_finish_hash_len==16U && memcmp(g_finish_hash,md5a,16)==0, "finish() got the exact client-supplied MD5");
    /* last_md5's underlying digest is finalized by the REAL device backend
     * (fwu_be_finish(), #ifndef M1CP_HOST_TEST) -- like the ESP path's own
     * m1cp_espupdate_last_md5(), it is a device-only diagnostic this mock-
     * backend test doesn't populate, so it's not exercised for a real value
     * here (only its "unavailable before any hash was ever supplied" case,
     * above, is host-testable). */

    /* ---- preconditions ---- */
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,0,M1CP_UPDATE_HASH_NONE,NULL)) == M1CP_ERR_BAD_ARG, "BEGIN size 0 -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl, M1CP_FW_MAX_IMAGE+1,M1CP_UPDATE_HASH_NONE,NULL)) == M1CP_ERR_BAD_ARG, "BEGIN oversize -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin_bad_confirm(pl,1000)) == M1CP_ERR_BAD_ARG, "BEGIN wrong confirm token -> BAD_ARG");
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, 22) == M1CP_ERR_BAD_PAYLOAD, "BEGIN short payload -> BAD_PAYLOAD");
    reset_all(); g_ready=false; ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL)) == M1CP_ERR_BAD_STATE, "BEGIN low battery -> BAD_STATE");
    reset_all(); g_begin_rc=2; ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL)) == M1CP_ERR_FLASH, "BEGIN erase error -> FLASH (hard_err_code)");
    ok(g_aborts==1, "backend released on BEGIN failure");

    /* ---- DATA ordering / alignment / bounds -- each gets its own fresh
     * BEGIN since a genuine BAD_ARG now cleanly ends the session (matches
     * the ESP path's own behavior, fixed for the same reason: a stuck
     * session left the backend's flash unlocked/cursor stale). ---- */
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,16)) == M1CP_ERR_BAD_STATE, "DATA before BEGIN -> BAD_STATE");
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,16,chunk480,16)) == M1CP_ERR_BAD_ARG, "DATA offset ahead of received -> BAD_ARG");
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,100)) == M1CP_ERR_BAD_ARG, "non-final chunk not 16-mult -> BAD_ARG");
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    { static uint8_t big[481]; ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,big,481)) == M1CP_ERR_BAD_ARG, "chunk > max (481) -> BAD_ARG"); }

    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,500,M1CP_UPDATE_HASH_NONE,NULL));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,480,chunk480,32)) == M1CP_ERR_BAD_ARG, "overrun beyond image_size -> BAD_ARG");

    /* ---- resent-chunk idempotency (shared coordinator behavior, now also
     * on the STM32 path -- it never had this before) ---- */
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)) == M1CP_ERR_NONE, "exact resend of an already-accepted chunk -> ACK, not BAD_ARG");
    ok(H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,480,chunk480,480)) == M1CP_ERR_NONE, "DATA after a resend still lands at the right offset");

    /* ---- FINISH incomplete / integrity ---- */
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_FW_UPDATE_FINISH, pl, 0) == M1CP_ERR_BAD_STATE, "FINISH incomplete -> BAD_STATE");
    reset_all(); g_finish_rc=1; H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,480,M1CP_UPDATE_HASH_NONE,NULL));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480));
    ok(H(M1CP_CMD_FW_UPDATE_FINISH, pl, 0) == M1CP_ERR_INTEGRITY, "FINISH bad CRC -> INTEGRITY");

    /* ---- ACTIVATE gating ---- */
    reset_all(); ok(H(M1CP_CMD_FW_UPDATE_ACTIVATE, pl, mk_confirm(pl,0xA5C3)) == M1CP_ERR_BAD_STATE, "ACTIVATE before FINISH -> BAD_STATE");
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,480,M1CP_UPDATE_HASH_NONE,NULL));
    H(M1CP_CMD_FW_UPDATE_DATA, pl, mk_data(pl,0,chunk480,480)); H(M1CP_CMD_FW_UPDATE_FINISH, pl, 0);
    ok(H(M1CP_CMD_FW_UPDATE_ACTIVATE, pl, mk_confirm(pl,0x0000)) == M1CP_ERR_BAD_ARG, "ACTIVATE wrong confirm -> BAD_ARG");

    /* ---- BUSY: second BEGIN while receiving ---- */
    reset_all(); H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    ok(H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL)) == M1CP_ERR_BUSY, "second BEGIN while active -> BUSY");

    /* ---- ABORT clears state ---- */
    ok(H(M1CP_CMD_FW_UPDATE_ABORT, pl, 0) == M1CP_ERR_NONE, "ABORT ok");
    ok(!m1cp_fwupdate_busy(), "idle after ABORT");
    reset_all(); ok(H(0x26, pl, 0) == M1CP_ERR_NOT_IMPLEMENTED, "unknown 0x26 -> NOT_IMPLEMENTED");

    /* ---- diagnostics: previously the STM32 path had none at all ---- */
    reset_all(); g_begin_rc=2; H(M1CP_CMD_FW_UPDATE_BEGIN, pl, mk_begin(pl,1000,M1CP_UPDATE_HASH_NONE,NULL));
    ok(m1cp_fwupdate_last_diag() == 0x0102U, "last_diag: step=1 (begin), code=2 (the backend's own return value)");

    /* ---- protocol level: STM32_UPDATE now advertised; FW_UPDATE routes in-session ---- */
    m1cp_init(cap_send, cap_legacy);
    m1cp_fwupdate_init(&BE);
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_HELLO,0,1,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      const uint8_t *p=g_last+11; uint32_t caps=(uint32_t)(p[3]|(p[4]<<8)|(p[5]<<16)|((uint32_t)p[6]<<24));
      ok((caps & M1CP_CAP_STM32_UPDATE)!=0, "STM32_UPDATE advertised (flash path implemented)");
      ok((caps & M1CP_CAP_FILESYSTEM)!=0, "FILESYSTEM still advertised"); }
    { uint8_t f[16]; int n=build_frame(f,M1CP_CMD_FW_UPDATE_ABORT,0,2,NULL,0); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      ok((g_last[6]&M1CP_FLAG_ACK) && g_last[5]==M1CP_CMD_FW_UPDATE_ABORT, "FW_UPDATE_ABORT routed in-session -> ACK"); }
    { /* a genuine (non-resend) BAD_ARG now carries the shared diagnostic NACK body */
      uint8_t beginpl[23]; uint16_t n2 = mk_begin(beginpl, 1000, M1CP_UPDATE_HASH_NONE, NULL);
      uint8_t f[64]; int n=build_frame(f,M1CP_CMD_FW_UPDATE_BEGIN,0,3,beginpl,n2); m1cp_rx_from_isr(f,(uint16_t)n,NULL);
      uint8_t f2[64]; int n3=build_frame(f2,M1CP_CMD_FW_UPDATE_DATA,0,4,beginpl,4); m1cp_rx_from_isr(f2,(uint16_t)n3,NULL);
      ok((g_last[6]&M1CP_FLAG_NACK) && g_last[5]==M1CP_CMD_FW_UPDATE_DATA, "DATA zero-length -> NACK routed in-session");
      ok(g_last[9]==19, "NACK body is 19 bytes (err+step+code+md5) -- no ESP UART counters on this target"); }

    printf("\n%s (%d/%d)\n", fails==0?"ALL PASS":"FAILURES", count-fails, count);
    fflush(stdout);
    return fails?1:0;
}
