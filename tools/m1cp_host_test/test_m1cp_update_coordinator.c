/* Host unit test for m1_manager_update_coordinator.c -- the generic,
 * transport-independent update state machine shared by every M1CP update
 * target. Exercises the coordinator DIRECTLY (not through either target's
 * M1CP dispatch shim -- those get their own tests) via a mock backend, so
 * every property this module claims to guarantee for EVERY future target
 * (manifest validation, resumable/resend-idempotent transfer, activate-vs-
 * immediate-complete, cancellation, diagnostics, measured counters) is
 * proven once, generically, rather than per-target. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "m1_manager_update_coordinator.h"
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;

/* This test exercises the coordinator in complete isolation -- not through
 * m1cp_dispatch() or any target's M1CP command shim -- so it links only
 * m1_manager_update_coordinator.c, not the rest of m1_manager_protocol.c
 * (which would pull in the FS/FW/ESP/version modules this test has nothing
 * to do with). Minimal stand-ins for the three status-publish calls the
 * coordinator makes are enough. */
static uint8_t g_status_domain, g_status_op, g_status_pct, g_status_err;
static uint8_t g_status_flag_bit; static bool g_status_flag_on;
void m1cp_status_set(uint8_t domain, uint8_t op_state, uint8_t progress)
{ g_status_domain = domain; g_status_op = op_state; g_status_pct = progress; }
void m1cp_status_set_error(uint8_t err) { g_status_err = err; }
void m1cp_status_set_flag(uint8_t flag_bit, bool on) { g_status_flag_bit = flag_bit; g_status_flag_on = on; }

/* ---- mock backend ---- */
static bool     g_ready = true;
static uint8_t  g_begin_rc = 0, g_write_rc = 0, g_finish_rc = 0;
static uint32_t g_begin_offset = 0, g_begin_size = 0;
static int      g_abort_calls = 0, g_activate_calls = 0;
static uint8_t  g_prog[4096];
static uint32_t g_prog_len = 0;
static uint8_t  g_finish_hash[32]; static uint8_t g_finish_hash_len = 0xFFU; /* 0xFF = not called */

static bool    be_ready(void)  { return g_ready; }
static uint8_t be_begin(uint32_t off, uint32_t sz) { g_begin_offset = off; g_begin_size = sz; g_prog_len = 0; return g_begin_rc; }
static uint8_t be_write(const uint8_t *d, uint32_t n)
{
    if (g_write_rc) return g_write_rc;
    if (g_prog_len + n <= sizeof g_prog) { memcpy(g_prog + g_prog_len, d, n); g_prog_len += n; }
    return 0;
}
static uint8_t be_finish(const uint8_t *hash, uint8_t hlen)
{
    g_finish_hash_len = hlen;
    if (hlen != 0U) { memcpy(g_finish_hash, hash, hlen); }
    return g_finish_rc;
}
static void be_activate(void) { g_activate_calls++; }
static void be_abort(void)    { g_abort_calls++; }

static m1cp_update_backend_t BE_ACTIVATE =
{
    "mock-activate", 0x100000U, 16U, true, M1CP_UPDATE_HASH_NONE, M1CP_ERR_FLASH,
    be_ready, be_begin, be_write, be_finish, be_activate, be_abort
};
static m1cp_update_backend_t BE_NOACTIVATE =
{
    "mock-noactivate", 0x400000U, 4U, false, M1CP_UPDATE_HASH_MD5, M1CP_ERR_ESP,
    be_ready, be_begin, be_write, be_finish, NULL, be_abort
};

static m1cp_update_session_t S;
static m1cp_update_ops_t OPS_A = { M1CP_DOMAIN_STM32, M1CP_OP_PROGRAMMING, M1CP_OP_VERIFYING_PROG, M1CP_OP_REBOOT_REQUIRED, M1CP_OP_COMPLETE };
static m1cp_update_ops_t OPS_B = { M1CP_DOMAIN_ESP32,  M1CP_OP_ESP_FLASHING, M1CP_OP_ESP_VERIFYING, 0xFFU, M1CP_OP_COMPLETE };

static void reset_all_activate(void)
{
    g_ready = true; g_begin_rc = g_write_rc = g_finish_rc = 0;
    g_begin_offset = g_begin_size = 0; g_abort_calls = g_activate_calls = 0;
    g_prog_len = 0; g_finish_hash_len = 0xFFU;
    m1cp_update_session_init(&S, &BE_ACTIVATE, &OPS_A);
}
static void reset_all_noactivate(void)
{
    g_ready = true; g_begin_rc = g_write_rc = g_finish_rc = 0;
    g_begin_offset = g_begin_size = 0; g_abort_calls = g_activate_calls = 0;
    g_prog_len = 0; g_finish_hash_len = 0xFFU;
    m1cp_update_session_init(&S, &BE_NOACTIVATE, &OPS_B);
}

static uint16_t mk_data(uint8_t *p, uint32_t off, const uint8_t *d, uint16_t n)
{
    p[0]=off&0xff;p[1]=(off>>8)&0xff;p[2]=(off>>16)&0xff;p[3]=(off>>24)&0xff; memcpy(p+4,d,n); return (uint16_t)(4+n);
}

static uint8_t g_out[16]; static uint16_t g_olen;

static int fails=0, count=0;
static void ok(int c, const char *m){ count++; if(c) printf("  ok  : %s\n", m); else { printf("  FAIL: %s\n", m); fails++; } }

int main(void)
{
    uint8_t pl[600];
    uint8_t chunk[16]; for (int i=0;i<16;i++) chunk[i]=(uint8_t)i;
    uint8_t md5a[16]; for (int i=0;i<16;i++) md5a[i]=(uint8_t)(0xA0+i);

    /* ---- happy path, backend WITH activate (STM32-shaped) ---- */
    reset_all_activate();
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_NONE, "BEGIN ok (no hash required)");
    ok(g_olen==2 && (g_out[0]|(g_out[1]<<8))==16U, "BEGIN echoes chunk_max");
    ok(g_begin_size==32U, "backend begin() called with image_size");
    ok(m1cp_update_busy(&S), "busy after BEGIN");
    ok(m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "DATA chunk 0 ok");
    { uint32_t nx=g_out[0]|(g_out[1]<<8)|(g_out[2]<<16)|((uint32_t)g_out[3]<<24); ok(nx==16U,"next_offset 16"); }
    ok(m1cp_update_data(&S, pl, mk_data(pl,16,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "DATA chunk 1 ok");
    ok(g_prog_len==32U && memcmp(g_prog,chunk,16)==0 && memcmp(g_prog+16,chunk,16)==0, "backend programmed both chunks");
    ok(m1cp_update_finish(&S) == M1CP_ERR_NONE, "FINISH ok");
    ok(g_finish_hash_len==0U, "finish() got hash_len 0 (no client hash was supplied)");
    ok(S.state == M1CP_UPD_VERIFIED, "state VERIFIED (activate required, not yet complete)");
    ok(m1cp_update_busy(&S), "still busy while VERIFIED");
    ok(m1cp_update_activate(&S, 0xA5C3U, 0xA5C3U) == M1CP_ERR_NONE, "ACTIVATE ok (ACK)");
    ok(m1cp_update_activate_pending(&S), "activate pending after ACK");
    m1cp_update_do_activate(&S);
    ok(g_activate_calls==1, "deferred activate invoked backend activate()");
    ok(!m1cp_update_busy(&S), "idle after do_activate");
    ok(S.stat_commands==4U, "stat_commands: BEGIN+2xDATA+FINISH");
    ok(S.stat_writes_ok==2U, "stat_writes_ok: two successful chunks");

    /* ---- happy path, backend WITHOUT activate (ESP-shaped): FINISH alone completes ---- */
    reset_all_noactivate();
    ok(m1cp_update_begin(&S, 0x10000U, 32U, M1CP_UPDATE_HASH_MD5, md5a, 16U, 480U, g_out, &g_olen) == M1CP_ERR_NONE, "BEGIN ok (MD5 required and supplied)");
    ok(g_begin_offset==0x10000U, "backend begin() got the flash_offset");
    ok(m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "DATA chunk 0 ok");
    ok(m1cp_update_data(&S, pl, mk_data(pl,16,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "DATA chunk 1 ok");
    ok(m1cp_update_finish(&S) == M1CP_ERR_NONE, "FINISH ok");
    ok(g_finish_hash_len==16U && memcmp(g_finish_hash,md5a,16)==0, "finish() got the exact client-supplied MD5");
    ok(S.state == M1CP_UPD_IDLE, "state IDLE immediately (no activate step for this backend)");
    ok(!m1cp_update_busy(&S), "not busy: FINISH alone completed the update");
    ok(m1cp_update_activate(&S, 0xA5C3U, 0xA5C3U) == M1CP_ERR_BAD_STATE, "ACTIVATE on a !supports_activate backend -> BAD_STATE, always");

    /* ---- BEGIN manifest validation (generic, both backends) ---- */
    reset_all_activate();
    ok(m1cp_update_begin(&S, 0, 0U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BAD_ARG, "BEGIN size 0 -> BAD_ARG");
    ok(m1cp_update_begin(&S, 0, BE_ACTIVATE.max_image_size+1U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BAD_ARG, "BEGIN over max_image_size -> BAD_ARG");
    ok(m1cp_update_begin(&S, 0, 32U, 99U, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BAD_ARG, "BEGIN unknown hash_algo -> BAD_ARG");
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_MD5, md5a, 8U, 16U, g_out, &g_olen) == M1CP_ERR_BAD_ARG, "BEGIN hash_len mismatched for declared algorithm -> BAD_ARG");
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 0U, g_out, &g_olen) == M1CP_ERR_BAD_ARG, "BEGIN chunk_max 0 -> BAD_ARG");
    reset_all_noactivate();
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 4U, g_out, &g_olen) == M1CP_ERR_INCOMPATIBLE,
       "BEGIN below this backend's min_hash_algo (MD5 required) -> INCOMPATIBLE");
    reset_all_activate();
    g_ready=false; ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BAD_STATE, "BEGIN not ready -> BAD_STATE");

    /* ---- BEGIN backend failure: busy vs hard error, abort() always called ---- */
    reset_all_activate();
    g_begin_rc=1; ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BUSY, "BEGIN backend rc=1 -> BUSY");
    ok(g_abort_calls==1, "abort() called even for a 'busy' begin failure (defensive, idempotent-safe)");
    reset_all_activate();
    g_begin_rc=2; ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_FLASH, "BEGIN backend rc=2 -> hard_err_code (FLASH for this backend)");
    ok(g_abort_calls==1, "abort() called for a hard begin failure");

    /* ---- BUSY: second BEGIN while RECEIVING/VERIFIED/ACTIVATING ---- */
    reset_all_activate();
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BUSY, "second BEGIN while RECEIVING -> BUSY");
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,16,chunk,16), g_out, &g_olen);
    m1cp_update_finish(&S);
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BUSY,
       "BEGIN while VERIFIED (pending activate) -> BUSY, does not silently discard the verified image");
    m1cp_update_activate(&S, 0xA5C3U, 0xA5C3U);
    ok(m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen) == M1CP_ERR_BUSY, "BEGIN while ACTIVATING -> BUSY");

    /* ---- DATA: resend idempotency (the exact fix ported from the ESP path) ---- */
    reset_all_activate();
    m1cp_update_begin(&S, 0, 48U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);   /* received = 16 */
    { uint32_t calls_before = S.stat_writes_ok;
      uint8_t rc = m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);   /* exact resend */
      ok(rc == M1CP_ERR_NONE, "exact resend of an already-accepted chunk -> ACK, not BAD_ARG");
      { uint32_t nx=g_out[0]|(g_out[1]<<8)|(g_out[2]<<16)|((uint32_t)g_out[3]<<24); ok(nx==16U,"resend's next_offset is unchanged (16)"); }
      ok(S.stat_writes_ok==calls_before, "resent bytes are not reprocessed/re-written");
      ok(S.stat_resends_ok==1U, "resend counted separately from a real write");
      ok(m1cp_update_busy(&S), "session survives an idempotent resend");
    }
    ok(m1cp_update_data(&S, pl, mk_data(pl,16,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "DATA after a resend still lands at the right offset");
    ok(m1cp_update_data(&S, pl, mk_data(pl,32,chunk,16), g_out, &g_olen) == M1CP_ERR_NONE, "final chunk after a resend completes the image");
    ok(m1cp_update_finish(&S) == M1CP_ERR_NONE, "FINISH still succeeds after a mid-transfer resend");

    reset_all_activate();
    m1cp_update_begin(&S, 0, 48U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);   /* received = 16 */
    ok(m1cp_update_data(&S, pl, mk_data(pl,8,chunk,16), g_out, &g_olen) == M1CP_ERR_BAD_ARG,
       "resend starting behind received but extending past it (partial overlap) -> still BAD_ARG");
    ok(g_abort_calls==1 && !m1cp_update_busy(&S), "genuine BAD_ARG cleanly fails the session (backend released)");

    /* ---- DATA: other bounds ---- */
    reset_all_activate(); ok(m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen) == M1CP_ERR_BAD_STATE, "DATA before BEGIN -> BAD_STATE");
    reset_all_activate(); m1cp_update_begin(&S, 0, 48U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    ok(m1cp_update_data(&S, pl, mk_data(pl,4,chunk,16), g_out, &g_olen) == M1CP_ERR_BAD_ARG, "DATA offset ahead of received -> BAD_ARG");
    reset_all_activate(); m1cp_update_begin(&S, 0, 48U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    ok(m1cp_update_data(&S, pl, mk_data(pl,0,chunk,10), g_out, &g_olen) == M1CP_ERR_BAD_ARG, "non-final chunk not chunk_alignment-mult -> BAD_ARG");
    reset_all_activate(); m1cp_update_begin(&S, 0, 48U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    { static uint8_t big[17]; ok(m1cp_update_data(&S, pl, mk_data(pl,0,big,17), g_out, &g_olen) == M1CP_ERR_BAD_ARG, "chunk > chunk_max -> BAD_ARG"); }
    reset_all_activate(); m1cp_update_begin(&S, 0, 20U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_data(&S, pl, mk_data(pl,16,chunk,16), g_out, &g_olen) == M1CP_ERR_BAD_ARG, "overrun beyond image_size -> BAD_ARG");

    /* ---- DATA write failure ---- */
    reset_all_activate(); g_write_rc=2;
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    ok(m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen) == M1CP_ERR_FLASH, "DATA write failure -> hard_err_code");
    ok(g_abort_calls==1 && !m1cp_update_busy(&S), "backend released after a write failure");

    /* ---- FINISH incomplete / integrity / hard-error / soft-success ---- */
    reset_all_activate();
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_finish(&S) == M1CP_ERR_BAD_STATE, "FINISH incomplete -> BAD_STATE");
    ok(g_abort_calls==1, "backend released -- finish() itself was never reached, so it never got a chance to self-clean");

    reset_all_activate(); g_finish_rc=1;
    m1cp_update_begin(&S, 0, 16U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_finish(&S) == M1CP_ERR_INTEGRITY, "FINISH rc=1 -> INTEGRITY");
    ok(g_abort_calls==0, "abort() NOT called after finish() -- it self-cleans on every outcome, per its own contract");

    reset_all_activate(); g_finish_rc=2;
    m1cp_update_begin(&S, 0, 16U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_finish(&S) == M1CP_ERR_FLASH, "FINISH rc=2 -> hard_err_code");
    ok(g_abort_calls==0, "still no abort() call -- finish() self-cleaned regardless of outcome");

    reset_all_noactivate(); g_finish_rc=3;
    m1cp_update_begin(&S, 0, 16U, M1CP_UPDATE_HASH_MD5, md5a, 16U, 480U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_finish(&S) == M1CP_ERR_NONE, "FINISH rc=3 (soft success) -> treated as success");
    ok(!m1cp_update_busy(&S), "soft success completes the update (no activate step for this backend)");

    /* ---- ABORT / RESET ---- */
    reset_all_activate();
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    ok(m1cp_update_abort(&S) == M1CP_ERR_NONE, "ABORT ok");
    ok(!m1cp_update_busy(&S) && g_abort_calls==1, "idle + backend released after ABORT");

    reset_all_activate();
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_reset(&S);
    ok(!m1cp_update_busy(&S) && g_abort_calls==1, "RESET (link-down) also releases the backend");

    reset_all_activate();
    m1cp_update_begin(&S, 0, 16U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    m1cp_update_finish(&S);
    m1cp_update_activate(&S, 0xA5C3U, 0xA5C3U);   /* now ACTIVATING */
    m1cp_update_reset(&S);
    ok(S.state == M1CP_UPD_ACTIVATING, "RESET never interrupts a committed ACTIVATING session");
    m1cp_update_abort(&S);
    ok(S.state == M1CP_UPD_ACTIVATING, "ABORT never interrupts a committed ACTIVATING session either");

    /* ---- diagnostics ---- */
    reset_all_activate(); g_write_rc=7;
    m1cp_update_begin(&S, 0, 32U, M1CP_UPDATE_HASH_NONE, NULL, 0U, 16U, g_out, &g_olen);
    m1cp_update_data(&S, pl, mk_data(pl,0,chunk,16), g_out, &g_olen);
    ok(m1cp_update_last_diag(&S) == 0x0207U, "last_diag: step=2 (write), code=7 (the backend's own write() return value, passed through exactly)");

    printf("\n%s (%d/%d)\n", fails==0?"ALL PASS":"FAILURES", count-fails, count);
    fflush(stdout);
    return fails?1:0;
}
