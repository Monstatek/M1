/*============================================================================*/
/**
 * @file    m1_mfc_raw_listener.c
 * @brief   Raw MIFARE Classic emulation listener -- Phase B1 (activation
 *          observation). See m1_mfc_raw_listener.h. Guarded by
 *          M1_MFC_RAW_EMULATION.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#include "m1_mfc_raw_listener.h"

#if defined(M1_MFC_RAW_EMULATION)

#include "m1_nfc_raw_hal.h"
#include "m1_mfc_raw_session_hw.h" /* Scope C: dedicated raw-MFC session ISR    */
#include "legacy/nfc_listener.h"   /* Phase A: Emu_SetPersona() reset on end    */
#include "m1_mfc_fast.h"     /* low-latency nonce responder (B2a) */
#include "ce_mfc.h"          /* card-side Crypto1 auth engine (B2b)  */
#include "ce_mfc_xport.h"    /* bit/parity (un)packing (B2b)         */
#include "rfal_platform.h"   /* platformLog */
#include <string.h>          /* memset */
#include "FreeRTOS.h"        /* AUTHRX: worker wake via binary semaphore */
#include "task.h"
#include "semphr.h"
extern void st25r3916Isr(void);   /* real RFAL ST25R INT handler (preserved) */

/* Build identity: the exact commit + clean flag are injected by CMake at
 * configure time from the (already-committed) repository -- never hardcoded. */
#ifndef M1_GIT_COMMIT
#define M1_GIT_COMMIT "unknown"
#endif
#ifndef M1_GIT_DIRTY
#define M1_GIT_DIRTY  1
#endif
#if defined(M1_MFC_DMA_AT)
#include "m1_mfc_dma.h"      /* TIM7 + dual-GPDMA1 {At} transport (pre-config) */
/* Set at each emulate-session start so the listener re-emits the one-time
 * ST25R3916 register snapshot ([B2b-REGS]) even if the log capture started
 * mid-session. Cleared by the listener after the snapshot is logged. */
volatile uint8_t g_m1_dump_regs = 1U;
#endif

/* MIFARE Classic command bytes recognised in B1 (dispatch/response come later). */
#define MFC_CMD_AUTH_A   0x60U
#define MFC_CMD_AUTH_B   0x61U
#define MFC_CMD_HALT     0x50U

static m1_mfc_raw_state_t s_state       = M1_MFC_RAW_IDLE;
static uint32_t           s_activations = 0U;
static uint32_t           s_auth_seen   = 0U;
static uint32_t           s_rxerr_dumps = 0U;   /* B1 diag rate-limit           */

/* Saved-card production image (see m1_mfc_emu_image.h). Copied in by
 * m1_mfc_raw_set_emu_image(), never referenced by pointer from outside this
 * file -- session-owned, independent of nfc_ctx once armed. Cleared at
 * session end so a stale image can never leak into an unrelated session
 * (the dev "NFC > Tools > MFC Emulate" entry point never arms this at all,
 * and keeps using the fixed diagnostic identity/key/block-data below). */
static m1_mfc_emu_image_t s_emu_image;
static bool               s_emu_image_armed = false;

bool m1_mfc_raw_set_emu_image(const m1_mfc_emu_image_t *img)
{
    if (img == NULL) { s_emu_image_armed = false; return false; }
    s_emu_image = *img;
    s_emu_image_armed = true;
    return true;
}

bool m1_mfc_raw_has_emu_image(void) { return s_emu_image_armed; }

static bool m1_mfc_raw_key_resolver(void *ctx, uint8_t key_type, uint8_t block,
                                    uint64_t *out_key)
{
    const m1_mfc_emu_image_t *img = (const m1_mfc_emu_image_t *)ctx;
    return m1_mfc_emu_image_key(img, key_type, block, out_key);
}

static bool m1_mfc_raw_block_resolver(void *ctx, uint8_t block, uint8_t out_data[16])
{
    const m1_mfc_emu_image_t *img = (const m1_mfc_emu_image_t *)ctx;
    if ((img == NULL) || (block >= M1_MFC_EMU_BLOCKS) || !img->block_valid[block]) {
        return false;
    }
    memcpy(out_data, img->block[block], 16U);
    return true;
}

/* --- POSTAUTH-9: critical post-SELECT receive window -----------------------
 * The reader sends its AUTH immediately after SELECT and abandons the card
 * within its FWT. The NFC worker's unconditional 5 ms sleep serviced RFAL too
 * slowly across READY_A/ACTIVE_A, so the AUTH was overwritten (by the reader's
 * re-select) before it was copied -- lm reaches ACTIVE_A but the frame is lost.
 * g_m1_mfc_rx_critical lets the worker spin with NO sleep across that window;
 * all diagnostics are recorded to RAM (edge-triggered, NO logging in the hot
 * path) and printed once, AFTER the frame is copied or the field is lost. */
volatile uint8_t g_m1_mfc_rx_critical = 0U;

static struct {
    uint32_t ready_a, active_a, rxe, eof, rxe_eof, dataflag, rxlen;
    uint32_t ready_cyc, r2f_us;
    uint8_t  ready_seen, first_seen, first_bytes, reported;
    uint16_t first_bits;
    uint8_t  first_raw[8];
    /* AUTHRX (IRQ-driven) telemetry, fixed memory only */
    uint32_t irq_count, irq2w_max_us, last_isr_cyc, wake_cyc;
    uint32_t fifo_level, last_rfal_state;
    uint8_t  fail_reported;
} s_pa9;

/* AUTHRX event mechanism. s_rx_evt is a dedicated, STATICALLY-allocated COUNTING
 * semaphore (not a binary one): every ST25R interrupt increments the count, so
 * coalescing cannot lose an RX edge -- the worker drains one RFAL pass per count.
 * It is created ONCE and never deleted, so the ISR can never touch a freed
 * object (safeguard #7); s_authrx_armed alone gates whether the ISR gives it.
 * A dedicated object never overwrites another subsystem's task notification. */
#define M1_MFC_RX_EVT_MAX   (16U)   /* generous cap; worker drains to 0 */
static StaticSemaphore_t s_rx_evt_buf;
static SemaphoreHandle_t s_rx_evt = NULL;
static volatile uint8_t  s_authrx_armed = 0U;

uint8_t m1_mfc_authrx_armed(void) { return s_authrx_armed; }

/* --- AUTH-HANDOFF state (declared early; used by housekeep/begin/finalizer) - */
static m1_pending_auth_t s_pauth;
static uint32_t          s_sess_gen = 0U;   /* bumped each session start        */
static uint32_t          s_rx_gen   = 0U;   /* bumped each accepted AUTH frame   */
static volatile m1_authstage_t s_stage = M1_STG_NO_AUTH;
static uint32_t s_auth_rx_cyc, s_nt_tx_cyc, s_nrar_rx_cyc, s_at_tx_cyc;
static uint8_t  s_nonce_tx_started, s_final_done;
enum { EVT_LA = 1, EVT_ACTIVATED, EVT_APPLIED, EVT_READY, EVT_FIRST, EVT_ACT, EVT_AUTH };
static struct { uint8_t code, a, b; uint16_t bits; uint32_t gen; } s_evt[24];
static uint8_t s_evt_n;

void m1_mfc_raw_pa9_reset(void) { memset(&s_pa9, 0, sizeof s_pa9); g_m1_mfc_rx_critical = 0U; }

/* Called from the RFAL listen worker every pass (task ctx), edge-triggered, NO
 * logging. Maintains the critical-window flag and the READY/ACTIVE counters. */

/* Called the instant RFAL reports post-SELECT data -- COPY FIRST, no logging.
 * Records only the first frame; releases the spin window. */

/* Optional IRQ-counter hook (RXE / EOF co-pending evidence, directive #8/#9). */

/* Print the deferred summary ONCE (called before [B1-ACT] on capture, or at
 * [B1-END] when the field is lost without a capture). [B1-READY]/[B1-FIRST] are
 * emitted here -- never in the timing-critical path. */

/*--------------------------- AUTHRX (IRQ-driven RX) -------------------------*/
/* Real monotonic bound for a single post-SELECT receive attempt. Derived from
 * the reader's ISO14443-3A frame timing: MIFARE FWTmax is ~4.8 ms and the
 * observed reader re-select cadence is ~30 ms, so a card that has been SELECTed
 * but has delivered no capturable frame for well beyond that (50 ms) is a
 * stalled attempt -- we emit the failure record and reset so the next approach
 * can retry. This is NOT an RX poll: the AUTH itself is caught by the ST25R RXE
 * interrupt in IRQ+context-switch time; this bound only closes a dead attempt. */
#define M1_MFC_AUTHRX_ATTEMPT_US   (50000U)

/* ISR context: preserve the real RFAL handler, then (armed only) record fixed
 * counters + wake the worker. NO logging/alloc/mutex/crypto/protocol here. */
void m1_mfc_authrx_isr(void)
{
    /* Scope C: while the dedicated raw-MFC session owns the radio, it is the
     * SOLE IRQ consumer -- st25r3916Isr() must NOT also run, since both read
     * and clear the same IRQ latches (RFAL's ISR would consume the event
     * before m1_mfc_raw_hw_isr() ever sees it). Checked first, every call. */
    if (m1_mfc_raw_hw_active()) {
        m1_mfc_raw_hw_isr();
        return;
    }

    st25r3916Isr();                       /* RFAL ST25R handler FIRST, unmodified */
    if (!s_authrx_armed) { return; }
    s_pa9.irq_count++;
    s_pa9.last_isr_cyc = DWT->CYCCNT;
    if (s_rx_evt != NULL) {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(s_rx_evt, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

/* Worker block: wait for the next ST25R interrupt, or the WDT-bounded heartbeat.
 * Returns 1 if an IRQ woke it, 0 on heartbeat timeout. */
uint8_t m1_mfc_authrx_wait(uint32_t max_ticks)
{
    if (s_rx_evt == NULL) { return 0U; }
    if (xSemaphoreTake(s_rx_evt, (TickType_t)max_ticks) == pdTRUE) {
        uint32_t hz = SystemCoreClock / 1000000U; if (hz == 0U) { hz = 1U; }
        uint32_t lat = (DWT->CYCCNT - s_pa9.last_isr_cyc) / hz;
        if (lat > s_pa9.irq2w_max_us && lat < 100000U) { s_pa9.irq2w_max_us = lat; }
        return 1U;
    }
    return 0U;
}

/* Guaranteed deferred failure record. Emitted once per session, only if no AUTH
 * was captured. Independent of [B1-ACT] and view destruction. */

/* Worker calls this once per loop pass: closes a stalled attempt on the real
 * monotonic deadline (does NOT depend on field-loss or BACK). */
void m1_mfc_authrx_housekeep(void)
{
    uint32_t hz = SystemCoreClock / 1000000U; if (hz == 0U) { hz = 1U; }
    /* Flush the deferred authoritative trace once the FDT window is safely past
     * (3 ms >> the whole nonce -> {Nr}{Ar} -> {At} chain). This prints the trace
     * on SUCCESS and FAILURE alike, always in task context outside the FDT
     * window -- never adding latency to the auth exchange itself. */
    if ((s_evt_n > 0U) && (s_auth_rx_cyc != 0U) &&
        (((DWT->CYCCNT - s_auth_rx_cyc) / hz) > 3000U)) {
        m1_mfc_evt_flush();
    }
    if (s_stage >= M1_STG_POSTAUTH) { return; }
    if (!s_pa9.ready_seen) { return; }
    if (((DWT->CYCCNT - s_pa9.ready_cyc) / hz) > M1_MFC_AUTHRX_ATTEMPT_US) {
        m1_mfc_authrx_final("attempt-timeout");
    }
}

/* True while inside the AUTH -> nt -> {Nr}{Ar} -> {At} critical window: the
 * activation logs are deferred (recorded to the event ring) during this window. */
uint8_t m1_mfc_in_auth_crit(void)
{
    return (uint8_t)((s_stage >= M1_STG_AUTH_PENDING) &&
                     (s_stage <  M1_STG_POSTAUTH) && !s_final_done);
}

/*=================== AUTH-HANDOFF: authoritative pending slot ================*/
/* The authoritative-AUTH slot, session/receive generations, stage machine and
 * deferred event ring are declared near the top of this file (used earlier by
 * the housekeeping deadline). Freshness by generation so a later re-exposure of
 * the same RFAL buffer cannot double-dispatch. */

void m1_mfc_evt_flush(void)   /* task ctx, outside the FDT window */
{
    for (uint8_t i = 0U; i < s_evt_n; i++) {
        switch (s_evt[i].code) {
        case EVT_LA:        platformLog("[MFC-EMU] LA cmd=%02X %02X bits=%u\r\n", s_evt[i].a, s_evt[i].b, (unsigned)s_evt[i].bits); break;
        case EVT_ACTIVATED: platformLog("[ListenerNotif] ListenerNotif: ACTIVATED\r\n"); break;
        case EVT_APPLIED:   platformLog("[CE] Applied Discovery Param to Active Dev\r\n"); break;
        case EVT_READY:     platformLog("[B1-READY] listener ready for first post-select frame (lm=6)\r\n"); break;
        case EVT_FIRST:     platformLog("[B1-FIRST] src=LA gen=%lu bits=%u raw=%02X %02X\r\n", (unsigned long)s_evt[i].gen, (unsigned)s_evt[i].bits, s_evt[i].a, s_evt[i].b); break;
        case EVT_ACT:       platformLog("[B1-ACT] reader activated (#%lu)\r\n", (unsigned long)s_activations); break;
        case EVT_AUTH:      platformLog("[B1-AUTH] key=%c blk=%u\r\n", (s_evt[i].a == 0x60U) ? 'A' : 'B', (unsigned)s_evt[i].b); break;
        default: break;
        }
    }
    s_evt_n = 0U;
}

/* Called at the LA cmd=60/61 site BEFORE returning/state change. Accept gate:
 * armed (raw MFC persona) + bits==16 + cmd 0x60/0x61. A 30 xx or anything else
 * can NEVER populate or suppress this slot. NO logging (records a deferred LA). */

/* PEEK the fresh pending AUTH without consuming (rejects stale session). The
 * caller dispatches the returned bytes to the canonical handler and only then
 * calls m1_mfc_pauth_dispatched(). Returns 1 and fills out[2]/gen if available. */

/* Mark the pending AUTH consumed ONLY when the canonical handler accepted it.
 * This is the authoritative "dispatched" point: it advances the stage and emits
 * the REAL [B1-AUTH] (deferred). Idempotent + generation-guarded, so a later
 * DATAEXCHANGE re-exposure of the same frame cannot dispatch a second time. */

/* Legacy consume-on-read (kept for the host seam test). Prefer peek+dispatched. */

/* Clear pending AUTH: session start, new selection, field loss, error, timeout,
 * STOP, BACK. Does NOT clear the finalized flag (that is per session). */
void m1_mfc_pauth_clear(void) { s_pauth.valid = 0U; s_pauth.consumed = 0U; }

/* Stage/timing setters called from the FDT-critical path (fixed memory only). */

static const char *m1_stage_name(m1_authstage_t s)
{
    switch (s) {
    case M1_STG_NO_AUTH:        return "NO_AUTH";
    case M1_STG_AUTH_PENDING:   return "AUTH_PENDING";
    case M1_STG_AUTH_DISPATCHED:return "AUTH_DISPATCHED";
    case M1_STG_NT_TX:          return "NT_TX";
    case M1_STG_NRAR_WAIT:      return "NRAR_WAIT";
    case M1_STG_AT_TX:          return "AT_TX";
    case M1_STG_POSTAUTH:       return "POSTAUTH";
    default:                    return "?";
    }
}

/* ONE idempotent terminal finalizer. Reached from RX error, WUPA/restart before
 * auth, field loss, link loss, attempt timeout, STOP/BACK, session end. A prior
 * 30 xx / [B1-FIRST] / [B1-ACT] must NOT suppress it -- only reaching POSTAUTH
 * (completion) does. Flushes the deferred trace, then the failure line. */
void m1_mfc_authrx_final(const char *reason)
{
    if (s_final_done || s_stage >= M1_STG_POSTAUTH) { return; }
    s_final_done = 1U;
    m1_mfc_evt_flush();   /* release the deferred authoritative trace first */
    uint32_t hz = SystemCoreClock / 1000000U; if (hz == 0U) { hz = 1U; }
    uint32_t a2n = (s_nonce_tx_started && s_auth_rx_cyc) ? ((s_nt_tx_cyc - s_auth_rx_cyc) / hz) : 0U;
    uint32_t n2a = (s_at_tx_cyc && s_nrar_rx_cyc) ? ((s_at_tx_cyc - s_nrar_rx_cyc) / hz) : 0U;
    platformLog("[B1-AUTHRX-FAIL] stage=%s reason=%s auth_gen=%lu consumed=%d nt_tx=%d "
                "auth_rx_to_nt_tx_us=%lu nrar_rx_to_at_tx_us=%lu irq=%lu readyA=%lu activeA=%lu "
                "dataFlag=%lu rxe=%lu eof=%lu\r\n",
                m1_stage_name(s_stage), (reason != NULL) ? reason : "?",
                (unsigned long)s_pauth.rx_gen, (int)s_pauth.consumed, (int)s_nonce_tx_started,
                (unsigned long)a2n, (unsigned long)n2a, (unsigned long)s_pa9.irq_count,
                (unsigned long)s_pa9.ready_a, (unsigned long)s_pa9.active_a,
                (unsigned long)s_pa9.dataflag, (unsigned long)s_pa9.rxe, (unsigned long)s_pa9.eof);
}

void m1_mfc_raw_begin(void)
{
    m1_raw_identity_t id;
    (void)m1_nfc_raw_get_identity(&id);

    s_state       = M1_MFC_RAW_WAIT_READER;
    s_activations = 0U;
    s_auth_seen   = 0U;
    s_rxerr_dumps = 0U;
    m1_mfc_raw_pa9_reset();   /* POSTAUTH-9 critical-window counters */

    /* AUTHRX: create the counting semaphore ONCE (static, persistent), drain any
     * stale counts, THEN arm the ISR wrapper (safeguard #6: wake object exists
     * before reception; session-active set before listen RX begins). The reader
     * cannot AUTH before discovery starts, which happens after this returns. */
    if (s_rx_evt == NULL) {
        s_rx_evt = xSemaphoreCreateCountingStatic(M1_MFC_RX_EVT_MAX, 0U, &s_rx_evt_buf);
    }
    if (s_rx_evt != NULL) { while (xSemaphoreTake(s_rx_evt, 0U) == pdTRUE) { /* drain */ } }
    s_authrx_armed = (s_rx_evt != NULL) ? 1U : 0U;

    /* AUTH-HANDOFF: new session generation; clear the authoritative slot, stage,
     * timing, finalizer + deferred trace so nothing leaks across sessions. */
    s_sess_gen++;
    m1_mfc_pauth_clear();
    s_stage = M1_STG_NO_AUTH;
    s_nonce_tx_started = 0U; s_final_done = 0U; s_evt_n = 0U;
    s_auth_rx_cyc = 0U; s_nt_tx_cyc = 0U; s_nrar_rx_cyc = 0U; s_at_tx_cyc = 0U;

#if defined(MFC_ROUTE_PROBE)
    platformLog("[BUILD] emu id=EMU-PROBE-1 (MFC_ROUTE_PROBE: unconditional {At} TX)\r\n");
#elif defined(M1_MFC_DMA_AT)
    g_m1_dump_regs = 1U;   /* re-emit [B2b-REGS] once for this session */
    /* Build-identity banner. M1_GIT_COMMIT/M1_GIT_DIRTY are supplied by build.sh
     * and identify the exact source this image was built from, which is the
     * provenance that matters. Compile __DATE__/__TIME__ are deliberately NOT
     * emitted: they change on every rebuild of identical source and were the
     * only nondeterministic input in the release image, which has to be
     * byte-reproducible. */
    platformLog("[BUILD] emu id=EMU-RAWOWN-1 candidate=RAWOWN git=%s dirty=%d\r\n",
                M1_GIT_COMMIT, (int)M1_GIT_DIRTY);
#endif

    char uidhex[16];
    static const char HEXD[] = "0123456789ABCDEF";
    uint8_t n = (id.uid_len > 7U) ? 7U : id.uid_len;
    for (uint8_t i = 0U; i < n; i++) {
        uidhex[(uint8_t)(i * 2U)]        = HEXD[(id.uid[i] >> 4) & 0x0FU];
        uidhex[(uint8_t)((i * 2U) + 1U)] = HEXD[id.uid[i] & 0x0FU];
    }
    uidhex[(uint8_t)(n * 2U)] = '\0';

    platformLog("[B1-PRESENT] uid=%s len=%u ATQA=%02X%02X SAK=%02X src=%s\r\n",
                uidhex, (unsigned)id.uid_len,
                id.atqa[0], id.atqa[1], id.sak,
                id.from_saved ? "saved" : "fallback");

    /* B2a: arm the low-latency plaintext nonce responder for this session. */
    {
        static const uint8_t nt0[4] = { 0x01U, 0x02U, 0x03U, 0x04U };
        m1_mfc_fast_arm(nt0);
    }

    /* Scope C: configure the dedicated raw-MFC session. Software/logging setup
     * only -- taking exclusive raw passive-target ownership itself (the
     * corrected STARTING sequence: EXTI mask, clear stale state, acquire,
     * unmask) happens later, inside m1_mfc_raw_hw_run(), called synchronously
     * from nfc_worker_task's own NFC_STATE_PROCESS dispatch (nfc_driver.c) --
     * the single task that owns every ST25R3916 SPI/register access for the
     * whole session (see m1_mfc_raw_session_hw.h). ListenIni() itself still
     * runs in that same task, so there is no cross-task gap between this call
     * and that one.
     *
     * Saved-card production image (m1_mfc_raw_set_emu_image(), armed by the
     * dedicated MFC Emulate view -- nfc_mfc_emu_gui_create(), m1_nfc.c --
     * BEFORE Q_EVENT_NFC_MFC_EMULATE is even sent; nfc_driver.c's own gate
     * on that event is the primary place this is enforced, this is the
     * second-layer canary): real UID/ATQA/SAK, and every AUTH resolves its
     * sector key through m1_mfc_raw_key_resolver() instead of a fixed
     * value. There is no production fallback identity: an unarmed image
     * reaching this point means the upstream gate was somehow bypassed, so
     * this refuses outright -- m1_mfc_raw_hw_init() is never called with a
     * fixed test UID/key (the old dev-only "NFC > Tools > MFC Emulate"
     * fallback persona no longer exists in production). */
    if (!s_emu_image_armed) {
        platformLog("[B1-REFUSED] m1_mfc_raw_begin(): no saved-card image armed -- refusing to start\r\n");
        return;
    }
    {
        uint32_t cuid = ((uint32_t)s_emu_image.uid[0] << 24) |
                        ((uint32_t)s_emu_image.uid[1] << 16) |
                        ((uint32_t)s_emu_image.uid[2] << 8)  |
                        (uint32_t)s_emu_image.uid[3];
        platformLog("[B1-PRESENT-REAL] uid=%02X%02X%02X%02X ATQA=%02X%02X SAK=%02X (saved-card image)\r\n",
                    s_emu_image.uid[0], s_emu_image.uid[1], s_emu_image.uid[2], s_emu_image.uid[3],
                    s_emu_image.atqa[0], s_emu_image.atqa[1], s_emu_image.sak);
        m1_mfc_raw_hw_init(cuid, 0ULL, 0x01020304U, s_emu_image.atqa, s_emu_image.sak,
                           m1_mfc_raw_key_resolver, &s_emu_image,
                           m1_mfc_raw_block_resolver, &s_emu_image);
    }

#if defined(M1_MFC_DMA_AT)
    /* Configure TIM7 + GPDMA1 and reset the waveform cache up front, so the
     * one-time peripheral setup is out of the FDT-critical window. */
    m1_mfc_dma_init();
#endif
}

/* UI-facing (or any non-nfc_worker_task caller) STOP entry point: requests
 * m1_mfc_raw_hw_run() (running inside nfc_worker_task, the sole owner of
 * every ST25R3916 SPI/register access) to stop, and blocks (bounded) for its
 * STOPPED acknowledgment -- see m1_mfc_raw_hw_request_stop_and_wait().
 * Neither this function nor its caller touches hardware directly; that
 * would be exactly the UI-task-calls-acquire()/release() violation this
 * architecture forbids. Returns false on timeout -- the caller MUST NOT
 * assume hardware was released, ownership was transferred, or EXTI was
 * re-enabled; m1_mfc_raw_hw_active() remains the sole authority on whether
 * anything may proceed, and stays true (gate closed) in that case. A no-op,
 * safe call if no session is active (m1_mfc_raw_hw_request_stop_and_wait()
 * still runs, but m1_mfc_raw_hw_run() -- not currently blocked in its own
 * wait -- has nothing to notify; the STOPPED wait will simply time out,
 * which is fine since there is nothing to tear down). */
bool m1_mfc_raw_end(void)
{
    bool stopped = m1_mfc_raw_hw_request_stop_and_wait(pdMS_TO_TICKS(2000));
    if (!stopped) {
        platformLog("[RAWOWN-STOP-TIMEOUT] m1_mfc_raw_end(): STOPPED not confirmed -- "
                    "leaving ownership gate closed, NOT assuming hardware was released\r\n");
        return false;
    }
    /* Session hardware teardown is already fully complete at this point
     * (m1_mfc_raw_hw_run() only signals STOPPED after release, interrupt
     * cleanup, the INACTIVE transition, EXTI restoration, and priority
     * restoration are all done). The remaining, non-hardware cleanup below
     * runs here, in the CALLER's task -- purely software state, never
     * ST25R3916 SPI/registers, so it is not subject to the same-task-only
     * contract. */
    m1_mfc_raw_teardown_cleanup();
    return true;
}

/* Non-hardware session cleanup, called once m1_mfc_raw_hw_run() has fully
 * torn down (either via m1_mfc_raw_end() above, from any caller, or directly
 * from nfc_driver.c's NFC_STATE_PROCESS dispatch right after
 * m1_mfc_raw_hw_run() returns for a field-loss/attempt-timeout/protocol-error
 * exit that the UI never explicitly requested). Idempotent. */
void m1_mfc_raw_teardown_cleanup(void)
{
    /* Safeguard #7: clear session-active FIRST so the ISR can no longer give the
     * semaphore, THEN wake any worker blocked on it so it resumes normal
     * scheduling and exits. The semaphore is static and never deleted, so the
     * ISR can never touch a freed object. (Legacy AUTHRX path only -- inert
     * for MFC_EMU, which never arms it; preserved unchanged.) */
    s_authrx_armed = 0U;
    if (s_rx_evt != NULL) { (void)xSemaphoreGive(s_rx_evt); }

    m1_mfc_fast_disarm();
    m1_nfc_raw_reset();
    s_state = M1_MFC_RAW_IDLE;
    /* Saved-card production image: cleared at the SAME unconditional
     * session-end hook as the persona reset above, so BACK/Stop/field-loss/
     * timeout/error all guarantee a stale image can never leak into the next
     * session -- the next Emulate must re-arm it fresh (or fall back to the
     * unchanged dev/diagnostic identity if it doesn't). */
    (void)m1_mfc_raw_set_emu_image(NULL);
    /* Phase A (persona/lifecycle fix): reset the sticky persona global back
     * to neutral the moment dedicated MFC Emulate genuinely ends -- this
     * function is the ONE unconditional "session is over" hook, reached from
     * every exit (UI BACK via m1_mfc_raw_end(), or directly from
     * nfc_driver.c for field-loss/attempt-timeout/protocol-error). Before
     * this, nothing ever reset EMU_PERSONA_MFC_EMU, so it silently hijacked
     * the next ordinary Read or legacy Read>More>Emulate. */
    Emu_SetPersona(EMU_PERSONA_RAW);
    m1_mfc_authrx_final("session-end");  /* idempotent finalizer (flushes trace) */
    /* Unambiguous session outcome: explicit stage + counters so "auth_seen=0"
     * after a deferred [B1-AUTH] is never ambiguous -- auth_cmd_seen = an AUTH was
     * captured; nt_tx_req = the nonce TX was actually requested; auth_completed =
     * a sector-0 READ was answered (POSTAUTH). */
    platformLog("[B1-END] activations=%lu auth_seen=%lu stage=%s auth_cmd_seen=%d nt_tx_req=%d auth_completed=%d\r\n",
                (unsigned long)s_activations, (unsigned long)s_auth_seen,
                m1_stage_name(s_stage), (int)(s_rx_gen > 0U),
                (int)s_nonce_tx_started, (int)(s_stage >= M1_STG_POSTAUTH));
    m1_mfc_pauth_clear();
}




m1_mfc_raw_state_t m1_mfc_raw_state(void)
{
    return s_state;
}






#endif /* M1_MFC_RAW_EMULATION */
