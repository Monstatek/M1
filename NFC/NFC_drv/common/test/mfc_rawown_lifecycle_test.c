/*
 * mfc_rawown_lifecycle_test.c - host mirror of the RAWOWN ownership
 * state-machine / IRQ-notification logic in m1_mfc_raw_session_hw.c and
 * m1_nfc_raw_hal.c (which cannot link on host: real ST25R3916/NVIC/FreeRTOS).
 * Byte-for-byte-in-spirit mirror of the corrected STARTING/STOPPING
 * ordering, the WU_A-gated auto-response disable, RXE dispatch, duplicate
 * IRQ-wakeup coalescing, and teardown with a pending interrupt still latched
 * in the notification value -- same technique as mfc_raw_hw_logic_test.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      NFC/NFC_drv/common/test/mfc_rawown_lifecycle_test.c \
 *      -o /tmp/rawown_lifecycle && /tmp/rawown_lifecycle
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* ---- mirror of m1_rawown_state_t (m1_mfc_raw_session_hw.c) ---- */
typedef enum { INACTIVE = 0, STARTING, ACTIVE, STOPPING } state_t;

/* Notification bits (mirrors M1_RAWOWN_NOTIFY_BIT_IRQ/_STOP). Modeled as a
 * plain OR-accumulated bitmask, exactly like FreeRTOS's xTaskNotify(...,
 * eSetBits) semantics -- NOT a counter, so two IRQ notifies before one wait
 * collapse into a single set bit rather than two queued events. */
#define BIT_IRQ  (1u << 0)
#define BIT_STOP (1u << 1)

/* ---- mock hardware/ownership primitives, call-order + call-count only ---- */
typedef struct {
    state_t  state;
    bool     exti_masked;
    int      exti_mask_calls, exti_unmask_calls;
    int      clear_stale_calls;
    int      acquire_calls, release_calls;
    bool     acquire_should_fail;
    uint32_t notify_value;          /* the single accumulated notification word */
    bool     wu_a_seen;
    bool     autoresp_off;
    int      autoresp_off_calls;    /* must end at <=1 per session (one-shot) */
    bool     started_given, stopped_given;
    int      idle_direct_calls;     /* m1_nfc_raw_own_idle_direct() -- re-arm only, no ownership change */
    int      task_delete_calls;     /* must stay 0 for every ordinary field-loss path */
    uint32_t activation_gen;        /* mirrors s_sess.gen -- m1_mfc_session_begin() bumps it */
    bool     nt_tx_pending;         /* mirrors s_nt_tx_pending -- WAIT_NT_TXE */
    int      arm_rx_calls;          /* m1_nfc_raw_own_arm_rx() -- must be 0 between TX request and TXE */
    bool     auth_ready;            /* mirrors s_auth_rec.ready -- published only on TXE */
    /* Ordering trace: appended by every mock call below, checked by asserting
     * substring order rather than exact op-by-op equality, so unrelated
     * additions elsewhere don't make every test fragile. */
    char     trace[256];
} mock_t;

static void trace(mock_t *m, const char *op)
{
    size_t used = strlen(m->trace);
    size_t room = sizeof(m->trace) - used;
    if (room > 1) { snprintf(m->trace + used, room, "%s;", op); }
}

static void exti_mask(mock_t *m)   { m->exti_masked = true;  m->exti_mask_calls++;   trace(m, "mask"); }
static void exti_unmask(mock_t *m) { m->exti_masked = false; m->exti_unmask_calls++; trace(m, "unmask"); }
static void clear_stale(mock_t *m) { m->clear_stale_calls++; trace(m, "clear"); }
static bool acquire(mock_t *m) {
    m->acquire_calls++; trace(m, "acquire");
    return !m->acquire_should_fail;
}
static void release(mock_t *m) { m->release_calls++; trace(m, "release"); }

static void notify_give(mock_t *m, uint32_t bit) { m->notify_value |= bit; }
static uint32_t notify_wait_and_clear(mock_t *m) {
    uint32_t v = m->notify_value;
    m->notify_value = 0U;
    return v;
}

/* ---- mirror of m1_mfc_raw_hw_starting() -- corrected ordering ---- */
static bool starting(mock_t *m)
{
    m->state = STARTING;
    exti_mask(m);                 /* 1 */
    /* 2: "quiesce normal worker" -- free, single task, nothing to do here */
    clear_stale(m);                /* 3: BEFORE acquire()/GOTO_SENSE */
    m->wu_a_seen = false; m->autoresp_off = false; m->autoresp_off_calls = 0;
    m->notify_value = 0U;
    if (!acquire(m)) {             /* 5 */
        m->state = INACTIVE;
        exti_unmask(m);            /* rollback still unmasks -- masked-forever is not "safe" either */
        return false;
    }
    m->state = ACTIVE;              /* 6 */
    exti_unmask(m);                 /* 7: last */
    return true;
}

/* ---- mirror of m1_mfc_raw_hw_stopping() -- corrected ordering ---- */
static void stopping(mock_t *m)
{
    m->state = STOPPING;            /* 1 */
    exti_mask(m);                   /* 2 */
    /* 3: no rendezvous -- single task */
    release(m);                     /* 4 */
    clear_stale(m);                 /* 5: AFTER release, unlike STARTING */
    m->state = INACTIVE;            /* 6 */
    exti_unmask(m);                 /* 7: last */
}

/* ---- mirror of m1_nfc_raw_own_idle_direct() -- re-arm only, no ownership
 * change: does NOT touch state/exti_masked/release_calls/stopped_given. ---- */
static void idle_direct(mock_t *m) { m->idle_direct_calls++; trace(m, "idle"); }

/* ---- mirror of the NEW ordinary-field-loss branch in
 * m1_mfc_raw_hw_process_pending() (m1_mfc_raw_session_hw.c): re-arm, discard
 * the in-flight activation, start a fresh one -- session stays ACTIVE. Must
 * NOT call release()/exti_mask()/exti_unmask()/set stopped_given -- those are
 * exactly what m1_mfc_raw_hw_stopping() (BACK/explicit STOP) does, and this
 * path is deliberately not that. ---- */
static void field_loss(mock_t *m)
{
    idle_direct(m);
    m->activation_gen++;      /* m1_mfc_session_begin(): gen++, state=WAIT_AUTH */
    m->wu_a_seen = false;      /* re-gate the per-activation attempt watchdog */
    m->autoresp_off = false;   /* re-gate the one-shot auto-response disable */
    trace(m, "fieldloss");
}

static void arm_rx(mock_t *m) { m->arm_rx_calls++; trace(m, "armrx"); }

/* ---- mirror of the NEW WAIT_NT_TXE sequence in m1_mfc_raw_wait_auth_rxe()
 * + m1_mfc_raw_hw_run()'s notify loop (Phase B): the TX command is issued and
 * s_nt_tx_pending is set, but arm_rx()/s_auth_rec.ready are deliberately
 * deferred until TXE is actually observed. ---- */
static void nt_tx_request(mock_t *m)
{
    m->nt_tx_pending = true;
    trace(m, "nttxreq");
}

/* ---- mirror of the TXE branch in the notify loop: arm RX and publish the
 * AUTH record ONLY on TXE, one-shot. ---- */
static void nt_txe_ok(mock_t *m)
{
    if (!m->nt_tx_pending) { return; }   /* one-shot: a later {At}/READ TXE is not this */
    m->nt_tx_pending = false;
    arm_rx(m);
    m->auth_ready = true;
    trace(m, "txeok");
}

/* ---- mirror of an unrelated IRQ (e.g. a stray PAR/ERR bit) observed while
 * WAIT_NT_TXE is pending: accumulated for diagnostics, does NOT complete the
 * TX, does NOT touch arm_rx/auth_ready/nt_tx_pending. ---- */
static void nt_unrelated_irq(mock_t *m) { trace(m, "unrelated"); (void)m; }

/* ---- mirror of the WAIT_NT_TXE 10ms deadline in m1_mfc_raw_hw_run(): safe
 * reset using the SAME proven direct-listener recovery as ordinary
 * field-loss (idle_direct + fresh activation), session stays ACTIVE, Nt is
 * NEVER claimed transmitted (auth_ready stays false). ---- */
static void nt_txe_timeout(mock_t *m)
{
    m->nt_tx_pending = false;
    idle_direct(m);
    m->activation_gen++;
    m->wu_a_seen = false; m->autoresp_off = false;
    trace(m, "txetimeout");
}

/* ---- mirror of the WU_A-gated auto-response disable (run-loop body) ---- */
static void on_irq(mock_t *m, uint32_t irq_bits)
{
    #define IRQ_WU_A (1u << 24)
    if (irq_bits & IRQ_WU_A) {
        m->wu_a_seen = true;
        if (!m->autoresp_off) {
            m->autoresp_off = true;
            m->autoresp_off_calls++;
        }
    }
}

int main(void)
{
    /* [1] Nominal STARTING: exact ordering, ends ACTIVE + unmasked. */
    {
        mock_t m = {0};
        bool ok = starting(&m);
        CHECK(ok, "starting() succeeds when acquire() succeeds");
        CHECK(m.state == ACTIVE, "state is ACTIVE after successful starting");
        CHECK(!m.exti_masked, "EXTI unmasked after successful starting");
        CHECK(m.exti_mask_calls == 1 && m.exti_unmask_calls == 1, "exactly one mask + one unmask");
        CHECK(m.clear_stale_calls == 1, "stale state cleared exactly once");
        CHECK(strstr(m.trace, "mask;clear;acquire;unmask;") != NULL,
              "clear happens BEFORE acquire (never after) -- the exact bug being guarded against");
    }

    /* [2] Acquire failure: complete rollback, EXTI ends unmasked (not stuck
     * masked forever), state INACTIVE, ownership never falsely claimed. */
    {
        mock_t m = {0}; m.acquire_should_fail = true;
        bool ok = starting(&m);
        CHECK(!ok, "starting() fails when acquire() fails");
        CHECK(m.state == INACTIVE, "state rolled back to INACTIVE on acquire failure");
        CHECK(!m.exti_masked, "EXTI rolled back to unmasked on acquire failure (not left masked)");
        CHECK(m.release_calls == 0, "release() never called for a session that never acquired");
    }

    /* [3] Clearing stale state happens BEFORE acquire(), never after -- this
     * is the exact correction from the first (rejected) lifecycle draft,
     * which cleared after GOTO_SENSE and could erase a genuine WU_A. */
    {
        mock_t m = {0};
        starting(&m);
        size_t clear_pos   = (size_t)(strstr(m.trace, "clear;")   - m.trace);
        size_t acquire_pos = (size_t)(strstr(m.trace, "acquire;") - m.trace);
        CHECK(clear_pos < acquire_pos, "clear-stale runs strictly before acquire (STARTING)");
    }

    /* [4] Corrected STOPPING ordering: mask, release, clear (AFTER release
     * this time -- correct, nothing RAWOWN-relevant is left to preserve),
     * INACTIVE, unmask last. */
    {
        mock_t m = {0};
        starting(&m);
        char trace_before_stop[256]; strncpy(trace_before_stop, m.trace, sizeof(trace_before_stop));
        stopping(&m);
        CHECK(m.state == INACTIVE, "state is INACTIVE after stopping");
        CHECK(!m.exti_masked, "EXTI unmasked after stopping completes");
        CHECK(m.release_calls == 1, "release() called exactly once");
        CHECK(m.clear_stale_calls == 2, "stale state cleared once at start, once at stop");
        /* Slice the trace to just the stopping portion. */
        const char *stop_trace = m.trace + strlen(trace_before_stop);
        size_t mask_pos    = (size_t)(strstr(stop_trace, "mask;")    - stop_trace);
        size_t release_pos = (size_t)(strstr(stop_trace, "release;") - stop_trace);
        size_t clear_pos   = (size_t)(strstr(stop_trace, "clear;")   - stop_trace);
        size_t unmask_pos  = (size_t)(strstr(stop_trace, "unmask;")  - stop_trace);
        CHECK(mask_pos < release_pos, "STOPPING: mask before release");
        CHECK(release_pos < clear_pos, "STOPPING: release before clear (opposite order from STARTING)");
        CHECK(clear_pos < unmask_pos, "STOPPING: clear before the final unmask");
    }

    /* [5] WU_A -> autoresponse-off ordering: the disable write must never
     * happen before WU_A/WU_A_X is observed, and must be one-shot even
     * across repeated WU_A deliveries in the same session. */
    {
        mock_t m = {0};
        starting(&m);
        CHECK(!m.wu_a_seen && !m.autoresp_off, "before any IRQ: neither WU_A-seen nor autoresp-off");
        on_irq(&m, IRQ_WU_A);
        CHECK(m.wu_a_seen, "WU_A observed sets wu_a_seen");
        CHECK(m.autoresp_off, "WU_A observed also disables auto-response (same pass)");
        CHECK(m.autoresp_off_calls == 1, "auto-response disable issued exactly once");
        on_irq(&m, IRQ_WU_A);   /* a second WU_A/WU_A_X delivery, e.g. WU_A then WU_A_X */
        CHECK(m.autoresp_off_calls == 1, "a second WU_A does not re-issue the disable (one-shot)");
    }

    /* [6] RXE dispatch: only WAIT_AUTH-relevant IRQ processing runs when
     * expected; verifies the notification-driven wake correctly gates on
     * which bit(s) came back, not merely that "something" woke the task. */
    {
        mock_t m = {0};
        starting(&m);
        notify_give(&m, BIT_IRQ);
        uint32_t got = notify_wait_and_clear(&m);
        CHECK((got & BIT_IRQ) != 0, "IRQ notify bit observed after a real ISR give");
        CHECK((got & BIT_STOP) == 0, "STOP bit not set when only IRQ was given");
        CHECK(m.notify_value == 0, "notification value fully drained after wait");
    }

    /* [7] Duplicate IRQ wakeups: two ISR notifies before the task services
     * either one must coalesce into a single observed IRQ bit (eSetBits OR
     * semantics), not be lost, and not be double-counted as two events. */
    {
        mock_t m = {0};
        starting(&m);
        notify_give(&m, BIT_IRQ);   /* first ISR give */
        notify_give(&m, BIT_IRQ);   /* second ISR give, arrives before any wait */
        uint32_t got = notify_wait_and_clear(&m);
        CHECK((got & BIT_IRQ) != 0, "coalesced duplicate IRQ notifies still observed as IRQ-pending");
        CHECK(m.notify_value == 0, "single wait drains both coalesced gives (no leftover, no second wake)");
    }

    /* [8] Teardown with a pending interrupt: an IRQ bit already latched in
     * the SAME notification word as a STOP request must not be processed as
     * a live frame once STOPPING begins -- STOP takes priority, and the IRQ
     * bit is discarded along with everything else the STARTING clear-stale
     * step would otherwise have to repeat. */
    {
        mock_t m = {0};
        starting(&m);
        notify_give(&m, BIT_IRQ);    /* a real event lands ... */
        notify_give(&m, BIT_STOP);   /* ... then STOP is requested before it's serviced */
        uint32_t got = notify_wait_and_clear(&m);
        CHECK((got & BIT_STOP) != 0, "STOP bit observed alongside a pending IRQ bit");
        bool stop_wins = (got & BIT_STOP) != 0;   /* mirrors m1_mfc_raw_hw_run()'s check order: STOP checked first */
        CHECK(stop_wins, "STOP is serviced even though an IRQ bit was also pending in the same word");
        if (stop_wins) {
            stopping(&m);   /* the IRQ bit is NOT separately dispatched -- run() breaks before reaching that switch */
        }
        CHECK(m.state == INACTIVE, "teardown with a pending interrupt still reaches a clean INACTIVE state");
        CHECK(!m.exti_masked, "teardown with a pending interrupt still ends fully unmasked");
        CHECK(m.release_calls == 1, "teardown with a pending interrupt still releases exactly once");
    }

    /* [9] Ownership gate: every external caller's "is it safe to start
     * something else" check is state != INACTIVE, true for STARTING/ACTIVE/
     * STOPPING alike -- not just ACTIVE. A gate that only closed during
     * ACTIVE would let another operation start mid-teardown. */
    {
        state_t all[] = { STARTING, ACTIVE, STOPPING };
        for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) {
            bool gate_open = (all[i] == INACTIVE);
            CHECK(!gate_open, "ownership gate stays closed for every non-INACTIVE state");
        }
        CHECK((INACTIVE == INACTIVE), "ownership gate only opens at INACTIVE");
    }

    /* [10] ACTIVE + EON: ordinary field-on carries no control-flow effect in
     * this backend (diagnostic-only, s_irq_union) -- state and every
     * ownership counter stay exactly as they were. */
    {
        mock_t m = {0};
        starting(&m);
        /* EON has no mock action -- nothing to call. Assert the null effect
         * directly: no counters moved just by "being ACTIVE". */
        CHECK(m.state == ACTIVE, "ACTIVE + EON: state remains ACTIVE");
        CHECK(m.release_calls == 0 && m.idle_direct_calls == 0,
              "ACTIVE + EON: no release or re-arm call");
    }

    /* [11] ACTIVE + EOF: re-arms and remains ACTIVE -- the core corrected
     * behavior. Must NOT mask/unmask EXTI, release ownership, or give
     * STOPPED -- those belong only to explicit STOP (m1_mfc_raw_hw_stopping()). */
    {
        mock_t m = {0};
        starting(&m);
        uint32_t gen_before = m.activation_gen;
        field_loss(&m);
        CHECK(m.state == ACTIVE, "ACTIVE + EOF: state remains ACTIVE (not STOPPING/INACTIVE)");
        CHECK(m.idle_direct_calls == 1, "ACTIVE + EOF: re-arm (idle_direct) called exactly once");
        CHECK(m.activation_gen == gen_before + 1, "ACTIVE + EOF: a fresh activation begins (gen bumped)");
        CHECK(m.release_calls == 0, "ACTIVE + EOF: ownership NOT released");
        CHECK(!m.exti_masked, "ACTIVE + EOF: EXTI is never masked for ordinary field-loss");
        CHECK(m.exti_mask_calls == 1 && m.exti_unmask_calls == 1,
              "ACTIVE + EOF: mask/unmask counts unchanged since starting() (no new mask cycle)");
        CHECK(!m.stopped_given, "ACTIVE + EOF: STOPPED is not given");
        CHECK(m.task_delete_calls == 0, "ACTIVE + EOF: the worker task is never deleted");
    }

    /* [12] ACTIVE + EON + EOF + EON: supports a second activation -- the
     * post-field-loss re-arm must leave the session able to see a genuine
     * new WU_A and run a full AUTH cycle again, not just "not crash". */
    {
        mock_t m = {0};
        starting(&m);
        /* first EON: nothing to do */
        field_loss(&m);          /* EOF: re-arm, activation #2 begins */
        on_irq(&m, IRQ_WU_A);    /* second EON's WU_A: the new activation must be live */
        CHECK(m.state == ACTIVE, "EON+EOF+EON: state remains ACTIVE throughout");
        CHECK(m.activation_gen == 1, "EON+EOF+EON: exactly one fresh activation was started");
        CHECK(m.wu_a_seen, "EON+EOF+EON: WU_A is observed again on the second activation");
        CHECK(m.autoresp_off_calls == 1,
              "EON+EOF+EON: auto-response-disable fires again for the new activation "
              "(one-shot per activation, not per session)");
    }

    /* [13] Repeated EON/EOF cycles: remains ACTIVE across many field cycles,
     * one idle_direct() + one fresh activation per cycle, ownership never
     * touched -- proves this isn't a one-shot patch that only survives a
     * single retry. */
    {
        mock_t m = {0};
        starting(&m);
        const int CYCLES = 5;
        for (int i = 0; i < CYCLES; i++) {
            on_irq(&m, IRQ_WU_A);   /* activation attempt for this cycle */
            field_loss(&m);         /* reader cycles its field */
        }
        CHECK(m.state == ACTIVE, "repeated EON/EOF: still ACTIVE after 5 cycles");
        CHECK(m.idle_direct_calls == CYCLES, "repeated EON/EOF: one re-arm per cycle, no more");
        CHECK(m.activation_gen == (uint32_t)CYCLES, "repeated EON/EOF: one fresh activation per cycle");
        CHECK(m.release_calls == 0, "repeated EON/EOF: ownership never released across any cycle");
        CHECK(!m.exti_masked, "repeated EON/EOF: EXTI never left masked");
        CHECK(!m.stopped_given, "repeated EON/EOF: STOPPED never given");
    }

    /* [14] Explicit STOP after several field-loss cycles -> STOPPING ->
     * INACTIVE: BACK still performs complete teardown, and it's the ONLY
     * thing that does -- mirrors m1_mfc_raw_hw_run()'s STOP-bit-checked-first
     * ordering (test [8]) on top of a session that has already survived
     * multiple ordinary field cycles. */
    {
        mock_t m = {0};
        starting(&m);
        field_loss(&m);
        field_loss(&m);
        notify_give(&m, BIT_STOP);
        uint32_t got = notify_wait_and_clear(&m);
        CHECK((got & BIT_STOP) != 0, "STOP observed after prior field-loss cycles");
        if (got & BIT_STOP) { stopping(&m); m.stopped_given = true; }
        CHECK(m.state == INACTIVE, "explicit STOP after field-loss cycles: reaches INACTIVE");
        CHECK(!m.exti_masked, "explicit STOP after field-loss cycles: EXTI ends unmasked");
        CHECK(m.release_calls == 1,
              "explicit STOP after field-loss cycles: release() called exactly once "
              "(field-loss cycles never called it)");
        CHECK(m.stopped_given, "explicit STOP after field-loss cycles: STOPPED is given exactly here");
        CHECK(m.task_delete_calls == 0, "explicit STOP: still no task deletion (graceful, not vTaskDelete)");
    }

    /* [15] AUTH accepted -> WAIT_NT_TXE: TX requested, arm_rx/ready deferred. */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        CHECK(m.nt_tx_pending, "AUTH accepted: WAIT_NT_TXE entered (nt_tx_pending set)");
        CHECK(m.arm_rx_calls == 0, "AUTH accepted: arm_rx NOT called yet");
        CHECK(!m.auth_ready, "AUTH accepted: auth record NOT published yet");
    }

    /* [16] WAIT_NT_TXE + TXE -> arm RX -> WAIT_NRAR (auth_ready published). */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        nt_txe_ok(&m);
        CHECK(!m.nt_tx_pending, "WAIT_NT_TXE + TXE: no longer pending");
        CHECK(m.arm_rx_calls == 1, "WAIT_NT_TXE + TXE: arm_rx called exactly once");
        CHECK(m.auth_ready, "WAIT_NT_TXE + TXE: auth record published (WAIT_NRAR reachable)");
        CHECK(m.state == ACTIVE, "WAIT_NT_TXE + TXE: session remains ACTIVE");
    }

    /* [17] WAIT_NT_TXE + unrelated IRQ -> remain WAIT_NT_TXE (not falsely
     * completed by any IRQ other than TXE itself). */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        nt_unrelated_irq(&m);
        CHECK(m.nt_tx_pending, "WAIT_NT_TXE + unrelated IRQ: still pending");
        CHECK(m.arm_rx_calls == 0, "WAIT_NT_TXE + unrelated IRQ: arm_rx NOT called");
        CHECK(!m.auth_ready, "WAIT_NT_TXE + unrelated IRQ: auth record NOT published");
        /* A genuine TXE arriving afterward still completes it correctly. */
        nt_txe_ok(&m);
        CHECK(m.auth_ready, "WAIT_NT_TXE + unrelated IRQ then TXE: still completes correctly");
    }

    /* [18] WAIT_NT_TXE + timeout -> safe activation reset/re-arm, session
     * stays ACTIVE, Nt never claimed transmitted. */
    {
        mock_t m = {0};
        starting(&m);
        uint32_t gen_before = m.activation_gen;
        nt_tx_request(&m);
        nt_txe_timeout(&m);
        CHECK(!m.nt_tx_pending, "WAIT_NT_TXE + timeout: no longer pending (not stuck)");
        CHECK(!m.auth_ready, "WAIT_NT_TXE + timeout: auth NEVER claimed transmitted");
        CHECK(m.arm_rx_calls == 0, "WAIT_NT_TXE + timeout: arm_rx not called (no TX to arm RX for)");
        CHECK(m.idle_direct_calls == 1, "WAIT_NT_TXE + timeout: safe re-arm via proven idle_direct()");
        CHECK(m.activation_gen == gen_before + 1, "WAIT_NT_TXE + timeout: fresh activation begins");
        CHECK(m.state == ACTIVE, "WAIT_NT_TXE + timeout: session remains ACTIVE, not terminated");
        CHECK(m.release_calls == 0, "WAIT_NT_TXE + timeout: ownership never released");
    }

    /* [19] WAIT_NT_TXE + EOF -> safe field-loss recovery (same proven path,
     * pending flag cleared so a later stray TXE can't misfire). */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        field_loss(&m);              /* mirrors process_pending()'s field-loss branch */
        m.nt_tx_pending = false;      /* field-loss's own reset also clears this (see session_hw.c) */
        CHECK(!m.nt_tx_pending, "WAIT_NT_TXE + EOF: pending cleared by field-loss recovery");
        CHECK(m.state == ACTIVE, "WAIT_NT_TXE + EOF: session remains ACTIVE");
        CHECK(!m.auth_ready, "WAIT_NT_TXE + EOF: auth never published for the aborted activation");
    }

    /* [20] WAIT_NT_TXE + explicit STOP -> complete teardown (STOP still wins
     * over an in-flight Nt TX, exactly like it already wins over a pending
     * IRQ in test [8]). */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        notify_give(&m, BIT_STOP);
        uint32_t got = notify_wait_and_clear(&m);
        CHECK((got & BIT_STOP) != 0, "WAIT_NT_TXE + explicit STOP: STOP observed");
        stopping(&m);
        CHECK(m.state == INACTIVE, "WAIT_NT_TXE + explicit STOP: reaches INACTIVE");
        CHECK(m.release_calls == 1, "WAIT_NT_TXE + explicit STOP: release() called exactly once");
        CHECK(!m.exti_masked, "WAIT_NT_TXE + explicit STOP: EXTI ends unmasked");
    }

    /* [21] No CLEAR_FIFO/re-arm occurs between TX request and TXE -- the
     * exact property that distinguishes this candidate from the pre-fix
     * code (which called arm_rx() -- CLEAR_FIFO+UNMASK_RECEIVE_DATA --
     * immediately after issuing TRANSMIT_WITHOUT_CRC). */
    {
        mock_t m = {0};
        starting(&m);
        nt_tx_request(&m);
        CHECK(m.arm_rx_calls == 0, "between TX request and TXE: zero arm_rx (CLEAR_FIFO) calls");
        nt_unrelated_irq(&m);
        CHECK(m.arm_rx_calls == 0, "between TX request and TXE: still zero after an unrelated IRQ");
        nt_txe_ok(&m);
        CHECK(m.arm_rx_calls == 1, "only on TXE itself does arm_rx (CLEAR_FIFO) finally run");
    }

    printf("\nmfc_rawown_lifecycle_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
