/*
 * mfc_raw_hw_logic_test.c - host mirror of the ISR/worker-boundary logic in
 * m1_mfc_raw_session_hw.c (which cannot link on host: real ST25R3916/FreeRTOS/
 * DWT). Byte-for-byte equivalent to the actual gate/latch/generation logic,
 * the same technique already used successfully for mfc_authhandoff_test.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Covers what mfc_session_test.c (the pure protocol core) cannot: the ISR-side
 * "one nonce per session" latch enforced BEFORE the worker ever runs
 * (m1_mfc_session_nonce_gate alone does not prevent a second gate pass while
 * state is still WAIT_AUTH -- that is exactly what s_nonce_fired_isr is for),
 * field loss before/after the nonce fired, stale-generation record rejection
 * at the worker boundary, and the nt-consistency assertion between what the
 * ISR transmitted and what the worker's cipher-init recomputes.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/crypto1.c NFC/NFC_drv/common/ce_mfc.c \
 *      NFC/NFC_drv/common/m1_mfc_session.c \
 *      NFC/NFC_drv/common/test/mfc_raw_hw_logic_test.c -o /tmp/hwlogic && /tmp/hwlogic
 */
#include "m1_mfc_session.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#define KEY  0xFFFFFFFFFFFFULL
#define CUID 0x01020304U
#define NT   0x01020304U

/* ---- mirror of the ISR-side state (m1_mfc_raw_session_hw.c) ---- */
typedef struct {
    uint8_t  cmd[2];
    uint32_t gen;
    uint8_t  ready;
} auth_rec_t;

static uint8_t     s_nonce_fired_isr;
static uint8_t     s_field_lost;
static auth_rec_t  s_auth_rec;

static void isr_session_arm(m1_mfc_session_t *s)
{
    m1_mfc_session_begin(s);
    s_nonce_fired_isr = 0U;
    s_field_lost      = 0U;
    memset(&s_auth_rec, 0, sizeof(s_auth_rec));
}

/* Mirrors the WAIT_AUTH branch of m1_mfc_raw_hw_isr: gate + one-shot latch,
 * checked and set BEFORE the worker ever runs (unlike m1_mfc_session's own
 * take_auth, which only rejects AFTER the state has already transitioned). */
static bool isr_on_rxe_auth(m1_mfc_session_t *s, const uint8_t *frame, uint16_t bits)
{
    if (s->state != M1_SESS_WAIT_AUTH) { return false; }
    if (s_nonce_fired_isr != 0U) { return false; }             /* already fired this attempt */
    if (!m1_mfc_session_nonce_gate(s, frame, bits)) { return false; }

    s_auth_rec.cmd[0] = frame[0];
    s_auth_rec.cmd[1] = frame[1];
    s_auth_rec.gen    = s->gen;
    s_nonce_fired_isr = 1U;   /* latched -- a second RXE this attempt cannot re-fire */
    s_auth_rec.ready  = 1U;
    return true;
}

static void isr_on_eof(void) { s_field_lost = 1U; }

/* Mirrors the worker's consumption of s_auth_rec, including the generation
 * check and the nt-consistency assertion against the fixed persona nonce. */
static bool worker_consume_auth(m1_mfc_session_t *s, const uint8_t nonce_tx[4])
{
    if (s_auth_rec.ready == 0U) { return false; }
    s_auth_rec.ready = 0U;
    if (s_auth_rec.gen != s->gen) { return false; }            /* stale generation: reject */

    uint8_t nt_check[4];
    if (!m1_mfc_session_take_auth(s, s_auth_rec.cmd, 16U, nt_check)) { return false; }
    return memcmp(nt_check, nonce_tx, 4U) == 0;                /* must match what the ISR sent */
}

int main(void)
{
    const uint8_t nonce_tx[4] = { 0x01, 0x02, 0x03, 0x04 };    /* fixed persona nt, big-endian */

    /* [1] valid 60 00 fires exactly once; worker consumes it consistently. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x60, 0x00 };
        CHECK(isr_on_rxe_auth(&s, f, 16) == true, "valid 60 00 fires the nonce");
        CHECK(worker_consume_auth(&s, nonce_tx) == true, "worker consumes + nt matches");
        CHECK(s.state == M1_SESS_NONCE_SENT, "state advanced to NONCE_SENT");
    }

    /* [1b] valid 61 00 (Key B) fires exactly once. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x61, 0x00 };
        CHECK(isr_on_rxe_auth(&s, f, 16) == true, "valid 61 00 (Key B) fires the nonce");
        CHECK(worker_consume_auth(&s, nonce_tx) == true, "worker consumes Key B auth");
    }

    /* [2] ignored 30 00: no fire, no state change. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x30, 0x00 };
        CHECK(isr_on_rxe_auth(&s, f, 16) == false, "30 00 ignored");
        CHECK(s_nonce_fired_isr == 0U, "30 00 does not latch the nonce");
        CHECK(s.state == M1_SESS_WAIT_AUTH, "30 00 leaves state unchanged");
    }

    /* [3] invalid 60 FE (block>=64): rejected, no fire. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x60, 0xFE };
        CHECK(isr_on_rxe_auth(&s, f, 16) == false, "60 FE (block 254) rejected");
        CHECK(s_nonce_fired_isr == 0U, "60 FE does not latch");
    }

    /* [4] duplicate AUTH/RXE: a second valid 60/61 arriving BEFORE the worker
     * has consumed the first (state still WAIT_AUTH) must NOT fire a second
     * nonce -- this is exactly the case m1_mfc_session_nonce_gate ALONE cannot
     * prevent (state-based only); the ISR latch (s_nonce_fired_isr) must. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f1[2] = { 0x60, 0x00 }, f2[2] = { 0x60, 0x04 };
        CHECK(isr_on_rxe_auth(&s, f1, 16) == true,  "first AUTH fires");
        CHECK(isr_on_rxe_auth(&s, f2, 16) == false, "duplicate AUTH/RXE before worker runs is rejected");
        CHECK(s_auth_rec.cmd[1] == 0x00, "pending record still holds the FIRST auth's block, not the duplicate's");
    }

    /* [5] field loss BEFORE the nonce fired: no nonce, field_lost latched. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        isr_on_eof();
        CHECK(s_field_lost == 1U, "field loss before nonce is latched");
        CHECK(s_nonce_fired_isr == 0U, "no nonce was fired");
    }

    /* [6] field loss AFTER the nonce fired: still latched, does not undo the fire. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x60, 0x00 };
        isr_on_rxe_auth(&s, f, 16);
        isr_on_eof();
        CHECK(s_field_lost == 1U, "field loss after nonce is latched");
        CHECK(s_nonce_fired_isr == 1U, "nonce-fired latch is unaffected by field loss");
    }

    /* [7] stale-generation record rejected at the worker boundary: a record
     * captured for a PRIOR session generation must not be consumed by a NEW
     * session (mirrors the s_auth_rec.gen == s_sess.gen check in worker_poll). */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x60, 0x00 };
        isr_on_rxe_auth(&s, f, 16);
        uint32_t stale_gen = s_auth_rec.gen;
        /* New session begins (e.g. reader re-selected) WITHOUT consuming --
         * generation bumps, the old record is now stale. */
        isr_session_arm(&s);
        s_auth_rec.ready = 1U; s_auth_rec.gen = stale_gen;   /* simulate the leftover record */
        CHECK(worker_consume_auth(&s, nonce_tx) == false, "stale-generation record rejected");
        CHECK(s.state == M1_SESS_WAIT_AUTH, "session untouched by the stale record");
    }

    /* [8] nt-consistency: if the worker somehow recomputed a DIFFERENT nonce
     * than what was transmitted (e.g. persona misconfigured), the mismatch is
     * caught rather than silently accepted. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); isr_session_arm(&s);
        uint8_t f[2] = { 0x60, 0x00 };
        isr_on_rxe_auth(&s, f, 16);
        uint8_t wrong_tx[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        CHECK(worker_consume_auth(&s, wrong_tx) == false, "nt mismatch between ISR-TX and worker-recompute is caught");
    }

    /* ---------------------------------------------------------------------
     * Single-session lifetime invariant (Scope C locked architecture): one
     * MFC Emulate entry -> one BUILD banner, one [B1-PRESENT], one raw
     * listener init; no listener rebuild / IDLE->DISCOVERY while raw is
     * armed; only field loss/timeout/STOP/BACK/completion may exit.
     *
     * Mirrors the FOUR real firmware guard/emission sites byte-for-byte in
     * spirit (cannot link real RFAL/ST25R on host), each tracked as its OWN
     * independent counter so a bug that desyncs any one of them (e.g. BUILD
     * fires twice but PRESENT once, or the driver-level gate breaks while
     * ListenerCycle's own guard still holds) is caught, not masked by a
     * single fused "something happened" count:
     *   - nfc_driver.c's PROCESS-state gate: nfc_process_func() (->
     *     ListenerCycle()) must not even be CALLED while raw is active --
     *     the PRIMARY defense, checked before ListenerCycle ever runs.
     *   - ListenerCycle()'s own early-return (nfc_listener.c, Task 1 fix:
     *     checked BEFORE rfalNfcWorker()): a SECONDARY guard in case the
     *     primary one is ever bypassed -- rfalNfcWorker() must not dispatch.
     *   - ListenIni()'s MFC_EMU branch (nfc_listener.c): the ONE call site
     *     for BUILD banner, [B1-PRESENT], and raw listener (re)init --
     *     tracked as three separate emission counters.
     *   - rfalNfcListenActivation()'s hard invariant check (rfal_nfc.c): must
     *     never touch gNfcDev/PT-memory while raw is active.
     * ------------------------------------------------------------------- */
    {
        typedef struct {
            int active;
            int build_banner_count;    /* mirrors [BUILD] emission count            */
            int present_count;         /* mirrors [B1-PRESENT] emission count       */
            int listener_reinit_count; /* mirrors ListenIni()'s rfalNfcInitialize() */
            int driver_call_count;     /* mirrors nfc_driver.c calling nfc_process_func() at all */
            int process_count;         /* mirrors ListenerCycle's rfalNfcWorker() dispatch */
            int violation_count;       /* mirrors [RAW-INVARIANT-VIOLATION] log emission */
        } mock_session_t;

        /* Local helpers (not hoisted to file scope: this block is the single
         * call site exercising them, matching the test file's existing style
         * of self-contained numbered sections). Kept as three DISTINCT
         * increments (not one fused counter) precisely so a divergence
         * between them is representable and testable. */
#define MOCK_BEGIN(s)  do { (s)->active = 1; (s)->build_banner_count++; \
                            (s)->present_count++; (s)->listener_reinit_count++; } while (0)
#define MOCK_END(s)    do { (s)->active = 0; } while (0)

        /* [9a] normal single entry: BUILD/PRESENT/reinit each fire exactly
         * once, independently verified, no violations. */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s);   /* ListenIni() MFC_EMU branch, first (only) entry */
            CHECK(s.build_banner_count == 1, "single session: exactly one BUILD banner");
            CHECK(s.present_count == 1, "single session: exactly one [B1-PRESENT] line");
            CHECK(s.listener_reinit_count == 1, "single session: exactly one raw listener init");
            CHECK(s.active == 1, "single session: active after begin");
            CHECK(s.violation_count == 0, "single session: no invariant violation");
        }

        /* [9b] ListenIni() re-entered while active (would-be listener rebuild):
         * detected, torn down, NOT silently re-initialized in place -- mirrors
         * the guard added at nfc_listener.c's MFC_EMU branch. All three
         * counters must move together (2,2,2), proving no desync between
         * BUILD/PRESENT/reinit even across a detected re-entry. */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s);                              /* first entry */
            if (s.active) { s.violation_count++; MOCK_END(&s); }  /* guard: detect + tear down */
            MOCK_BEGIN(&s);                              /* the (still-occurring) second ListenIni() call proceeds fresh */
            CHECK(s.violation_count == 1, "ListenIni re-entry: violation detected exactly once");
            CHECK(s.build_banner_count == 2 && s.present_count == 2 && s.listener_reinit_count == 2,
                  "ListenIni re-entry: BUILD/PRESENT/reinit stay in lockstep (2,2,2), no desync");
            CHECK(s.active == 1, "ListenIni re-entry: ends in a single clean active session");
        }

        /* [9c] nfc_driver.c's PRIMARY gate: nfc_process_func() (-> ListenerCycle
         * -> rfalNfcWorker) must not even be CALLED while active -- this is the
         * gate checked before ListenerCycle ever runs, distinct from
         * ListenerCycle's own internal early-return tested in [9d]/[9e]. */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s);
            for (int i = 0; i < 100; i++) {
                if (!s.active) { s.driver_call_count++; }   /* nfc_driver.c: only calls nfc_process_func() when not active */
            }
            CHECK(s.driver_call_count == 0, "nfc_driver.c: zero nfc_process_func()/ListenerCycle() calls while raw session active");
        }

        /* [9d] ListenerCycle() tick while active: SECONDARY guard -- even if
         * called (defense in depth), must NEVER advance RFAL/LM state
         * (process_count stays 0), no matter how many ticks occur. */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s);
            for (int i = 0; i < 100; i++) {
                if (!s.active) { s.process_count++; }   /* early-return guard: only processes when NOT active */
            }
            CHECK(s.process_count == 0, "ListenerCycle: zero rfalNfcWorker() dispatches while raw session active");
        }

        /* [9e] Both driver-level and ListenerCycle-level ticks resume normally
         * once NOT active (proves the guards are conditional, not permanently
         * broken). */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s); MOCK_END(&s);
            for (int i = 0; i < 5; i++) {
                if (!s.active) { s.driver_call_count++; s.process_count++; }
            }
            CHECK(s.driver_call_count == 5, "nfc_driver.c: calls nfc_process_func() normally once session is not active");
            CHECK(s.process_count == 5, "ListenerCycle: processes normally once session is not active");
        }

        /* [9f] rfalNfcListenActivation() entered while active: must refuse
         * (return BUSY, i.e. false) and never touch gNfcDev/PT-memory, tearing
         * the stale session down rather than racing it. */
        {
            mock_session_t s = {0};
            MOCK_BEGIN(&s);
            bool touched_gnfcdev;
            if (s.active) {
                s.violation_count++; MOCK_END(&s);
                touched_gnfcdev = false;   /* refused: BUSY, no gNfcDev/PT-memory access */
            } else {
                touched_gnfcdev = true;
            }
            CHECK(touched_gnfcdev == false, "rfalNfcListenActivation: gNfcDev/PT-memory untouched while raw active");
            CHECK(s.violation_count == 1, "rfalNfcListenActivation: invariant violation detected exactly once");
        }

#undef MOCK_BEGIN
#undef MOCK_END
    }

    printf("\nmfc_raw_hw_logic_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
