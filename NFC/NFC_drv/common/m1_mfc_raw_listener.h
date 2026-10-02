/*============================================================================*/
/**
 * @file    m1_mfc_raw_listener.h
 * @brief   MonstaTek raw MIFARE Classic card-emulation listener (Scope B).
 *          Hardware-agnostic protocol layer; all radio access goes through
 *          m1_nfc_raw_hal.
 *
 * Phased:
 *   B1 (this file): ISO14443-A activation observation only -- present a saved
 *       card identity (via RFAL PT-memory HW auto-anticollision), then log the
 *       first post-SELECT command (AUTH bit count), HALT, and clean re-select.
 *       No card->reader response is emitted yet.
 *   B2: Crypto1 auth transport (Nt / {Nr}{Ar} / encrypted {At}) over the
 *       transparent-mode waveform engine in m1_nfc_raw_hal + m1_mfc_crypto1_session.
 *   B3: authenticated READ, then WRITE, then value ops.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_MFC_RAW_LISTENER_H
#define M1_MFC_RAW_LISTENER_H

#include <stdint.h>
#include <stdbool.h>
#include "m1_mfc_emu_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Saved-card production image (Scope: saved-card MFC 1K emulation). Arms the
 * session with a real, pre-validated card snapshot (see m1_mfc_emu_image.h) --
 * copied in, never referenced by pointer, so it is independent of nfc_ctx from
 * this call on. m1_mfc_raw_begin() uses it (real UID/ATQA/SAK/keys/blocks)
 * when armed; otherwise it falls back to the fixed MFC-1K diagnostic identity
 * unchanged (the "NFC > Tools > MFC Emulate" dev entry point never arms this).
 * Cleared automatically at session end (m1_mfc_raw_end()). Returns false (and
 * does not arm) if img is NULL. */
bool m1_mfc_raw_set_emu_image(const m1_mfc_emu_image_t *img);

/* True iff a production image is currently armed for the next/current
 * session. Used by nfc_driver.c to decide whether Q_EVENT_NFC_START_EMULATE
 * should select the raw-MFC persona for this dispatch. */
bool m1_mfc_raw_has_emu_image(void);

typedef enum {
    M1_MFC_RAW_IDLE = 0,     /* not running                                    */
    M1_MFC_RAW_WAIT_READER,  /* listener up, no reader field / activation yet   */
    M1_MFC_RAW_ACTIVE,       /* reader completed activation (post-SELECT)       */
    M1_MFC_RAW_AUTH_SEEN,    /* a MIFARE AUTH (0x60/0x61) frame was observed    */
} m1_mfc_raw_state_t;

/* Start a raw-emulation session: resolve+log the presented identity and enter
 * WAIT_READER. Call once when the listener is (re)initialised for the
 * EMU_PERSONA_MFC_EMU persona. */
void m1_mfc_raw_begin(void);

/* End the session (BACK / stop / teardown), called from the UI (or any
 * non-nfc_worker_task caller): requests m1_mfc_raw_hw_run() to stop and
 * blocks (bounded, ~2s) for its STOPPED acknowledgment -- never touches
 * ST25R3916 SPI/registers directly. Idempotent. Returns false on timeout,
 * in which case the caller MUST NOT assume hardware was released -- see
 * m1_mfc_raw_hw_request_stop_and_wait(). */
bool m1_mfc_raw_end(void);

/* Non-hardware session cleanup (fast-path disarm, HAL reset, [B1-END] log,
 * pending-AUTH clear) -- called once m1_mfc_raw_hw_run() has itself fully
 * torn down the hardware side. Called from m1_mfc_raw_end() above (bounded
 * by its STOPPED wait) and from nfc_driver.c's NFC_STATE_PROCESS dispatch
 * directly after m1_mfc_raw_hw_run() returns (covers field-loss/attempt-
 * timeout/protocol-error exits the UI never explicitly requested). Pure
 * software state; safe from any task. Idempotent. */
void m1_mfc_raw_teardown_cleanup(void);

/* Reader completed activation (post-SELECT). Called from the listener ACTIVATED
 * transition. Moves WAIT_READER -> ACTIVE. */

/* Field / reader loss (called from the listener deactivation / field-loss
 * path). Returns to WAIT_READER without teardown. */

/* Offer one received listener frame. B1: classifies + logs (AUTH 0x60/0x61,
 * HALT 0x50) with the exact RFAL bit count; always returns true (frame consumed,
 * caller re-arms RX). rxLenBits is the received length in BITS. */

/* Read-only status for the UI. */
m1_mfc_raw_state_t m1_mfc_raw_state(void);

/* --- POSTAUTH-9: critical post-SELECT receive window ------------------------
 * The worker must NOT sleep across READY_A/ACTIVE_A or the reader's first
 * post-SELECT AUTH is overwritten before it is copied. g_m1_mfc_rx_critical is
 * 1 during that window; the NFC worker spins (no vTaskDelay) while it is set.
 * All diagnostics are recorded to RAM here and printed once, deferred. */
extern volatile uint8_t g_m1_mfc_rx_critical;
void    m1_mfc_raw_pa9_reset(void);

/* --- AUTHRX: IRQ/event-driven receive (POSTAUTH-10) ------------------------
 * The ST25R3916 INT EXTI callback for the listener is wrapped so that, after
 * the real st25r3916Isr() runs, the NFC worker is woken immediately via a
 * dedicated binary semaphore. The worker BLOCKS on that semaphore (no polling
 * of the RX path) and services RFAL the instant an RX interrupt fires, so the
 * post-SELECT AUTH is copied within IRQ+context-switch latency, not a poll
 * period. All ISR work is fixed-memory counters + the give; no logging/alloc/
 * mutex/crypto. Everything is armed only during an MFC_EMU listener session. */

/* ISR-context wrapper: calls the real st25r3916Isr() (RFAL handler preserved),
 * then, while armed, records fixed counters/timestamps and wakes the worker.
 * Registered as the ST25R INT RisingCallback by NFC_Listening_Init (ON build). */
void m1_mfc_authrx_isr(void);

/* True only between m1_mfc_raw_begin() and m1_mfc_raw_end() (MFC_EMU session). */
uint8_t m1_mfc_authrx_armed(void);

/* Worker block: wait for the next ST25R RX interrupt (or the WDT-bounded
 * housekeeping heartbeat). Returns 1 if an IRQ woke it, 0 on heartbeat timeout.
 * max_ticks is the heartbeat ceiling (bounded by the IWDG service window). */
uint8_t m1_mfc_authrx_wait(uint32_t max_ticks);

/* Guaranteed deferred failure record. Emits [B1-AUTHRX-FAIL] with the full
 * counter/timing set. reason is a short static string (exit cause). Idempotent
 * per session; NOT dependent on [B1-ACT] or view destruction. */

/* Worker calls once per loop pass: closes a stalled attempt on the real
 * monotonic deadline and emits the failure record (independent of field-loss
 * and BACK/view destruction). */
void m1_mfc_authrx_housekeep(void);

/* --- AUTH-HANDOFF: authoritative pending-AUTH slot + stage machine ---------- */
typedef struct {
    uint8_t  cmd[2];     /* exact two command bytes (0x60/0x61, block)          */
    uint16_t bits;       /* exact received bit length (16)                       */
    uint32_t sess_gen;   /* session generation when captured                    */
    uint32_t rx_gen;     /* receive generation (bumped per accepted AUTH)        */
    uint32_t rx_cyc;     /* DWT timestamp at receive                             */
    uint8_t  valid;      /* populated                                           */
    uint8_t  consumed;   /* dispatched exactly once                             */
} m1_pending_auth_t;

typedef enum {
    M1_STG_NO_AUTH = 0,
    M1_STG_AUTH_PENDING,
    M1_STG_AUTH_DISPATCHED,
    M1_STG_NT_TX,
    M1_STG_NRAR_WAIT,
    M1_STG_AT_TX,
    M1_STG_POSTAUTH,
} m1_authstage_t;

/* Snapshot the authoritative AUTH at the LA cmd=60/61 site (accept gate inside:
 * armed + bits==16 + cmd 0x60/0x61). NO logging. */
/* Consume the fresh pending AUTH once after RFAL ACTIVATED (rejects stale). */
/* PEEK the pending AUTH without consuming (out[2]=bytes, *gen=generation). */
/* Mark consumed ONLY when the canonical handler accepted it: advances the stage
 * and emits the real [B1-AUTH]. Generation-guarded so it dispatches exactly once. */
/* Clear the pending slot (session start / new selection / any restart). */
void    m1_mfc_pauth_clear(void);

/* FDT-critical-path stage/timing markers (fixed memory only, no logging). */

/* Flush the deferred authoritative trace ([MFC-EMU] LA / B1-READY / B1-FIRST /
 * B1-ACT / B1-AUTH), outside the FDT window. */
void m1_mfc_evt_flush(void);

/* ONE idempotent terminal finalizer -> [B1-AUTHRX-FAIL] with the reached stage.
 * Reached from RX error, WUPA/restart, field loss, link loss, timeout, STOP,
 * BACK, session end. NOT suppressed by a prior frame/[B1-ACT]; only POSTAUTH
 * (completion) suppresses it. */
void m1_mfc_authrx_final(const char *reason);

/* True while inside the AUTH->nt->{Nr}{Ar}->{At} critical window (logs deferred). */
uint8_t m1_mfc_in_auth_crit(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_MFC_RAW_LISTENER_H */
