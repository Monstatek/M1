/*
 * m1_mfc_raw_session_hw.h - ST25R3916 hardware glue for the dedicated raw-MFC
 * emulation session (Scope C). Wires m1_mfc_session.c (pure protocol logic) to
 * the real radio via m1_nfc_raw_hal (ownership) and m1_mfc_dma (transparent-mode
 * custom-parity TX for {At}/block).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SINGLE-TASK, TASK-OWNED SESSION. There is no
 * LM-progression/AUTH-onward split: rfalListenStart() alone (inside
 * m1_nfc_raw_own_acquire()) is sufficient to arm hardware auto-response and
 * enter sense mode -- REQA/ANTICOLL/SELECT are entirely silicon-automatic and
 * rfalWorker() is never called for the whole session.
 *
 * The existing nfc_worker_task (m1_csrc/m1_nfc.c, m1_nfc.h) IS the dedicated
 * task -- there is no second, independently-created task. It already is the
 * sole task in this firmware that ever touches RFAL/ST25R3916 (every other
 * such call site -- ReadCycle, NTAG/MFC write, dictionary scan, harvest,
 * solve, legacy ListenerCycle -- is a synchronous, run-to-completion, single
 * program counter branch of its one dispatch loop). m1_mfc_raw_hw_run() is
 * this file's ONE public entry point, called from that loop exactly like
 * nfc_mfc_write_run()/mfc_dict_scan()/etc. -- it blocks for the ENTIRE
 * session (start through stop) and only returns once fully torn down:
 *
 *   ISR (m1_mfc_raw_hw_isr): a single ISR-safe xTaskNotifyFromISR give (bit
 *       M1_RAWOWN_NOTIFY_BIT_IRQ) to nfc_worker_task. NO SPI, register
 *       access, FIFO read, gate logic, or Crypto1 -- for the whole session.
 *   m1_mfc_raw_hw_run() (runs entirely inside nfc_worker_task, temporarily
 *       raised to M1_MFC_RAWOWN_PRIORITY for the span): the SOLE consumer of
 *       every ST25R3916 SPI/register access for the whole session -- the
 *       chip's own clear-on-read IRQ status registers
 *       (m1_nfc_raw_read_irq_status(), bypassing RFAL's software
 *       sticky-status word entirely), NVIC-level EXTI masking around the
 *       ownership transitions, and all of it via xTaskNotifyWait (bits, not
 *       ulTaskNotifyTake) so an IRQ wake and a UI-requested STOP
 *       (M1_RAWOWN_NOTIFY_BIT_STOP) are distinguishable on the SAME wait.
 *       On WU_A/WU_A_X, immediately disables NFC-A automatic responses
 *       (PASSIVE_TARGET.d_106_ac_a). On
 *       RXE, dispatches through the existing, unmodified WAIT_AUTH/
 *       NONCE_SENT/AUTHED handlers. Crypto1 init/verify, {At}/block
 *       transparent-mode TX (m1_mfc_dma), READ processing, logging, and the
 *       idempotent terminal cleanup all still run here.
 *
 * RFAL's own IRQ consumption (st25r3916Isr()/st25r3916interrupt.status/
 * rfalWorker()) is categorically unreachable for the whole session -- the
 * shared EXTI dispatcher (m1_mfc_raw_listener.c m1_mfc_authrx_isr()) branches
 * on m1_mfc_raw_hw_active() before either RFAL's or RAWOWN's IRQ consumer can
 * run. During the STARTING/STOPPING transitions themselves, NVIC-level EXTI
 * masking (m1_nfc_raw_own_exti_mask/_unmask, m1_nfc_raw_hal.h) additionally
 * guarantees neither consumer's ISR can run at all, closing the window
 * around rfalListenStart()'s internal GOTO_SENSE and around release() that a
 * software flag alone cannot close.
 *
 * Only active while a raw session is running (MFC Emulate). Every other NFC
 * feature (Read, Detect Reader, ...) is untouched and keeps using RFAL's
 * normal rfalNfcWorker()/ListenerCycle() path as before.
 */
#ifndef NFC_DRV_M1_MFC_RAW_SESSION_HW_H_
#define NFC_DRV_M1_MFC_RAW_SESSION_HW_H_

#include <stdint.h>
#include <stdbool.h>
#include "m1_mfc_session.h"   /* m1_mfc_key_resolve_fn */

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the persona presented to the reader (identity + key + nonce) and
 * reset hardware-glue state. Call once before m1_mfc_raw_hw_run(). `key` is
 * the fixed-persona fallback key, used unless `key_resolve_fn` is non-NULL,
 * in which case every AUTH resolves its sector key through it instead (see
 * m1_mfc_session_set_key_resolver() -- this just forwards to it, since the
 * session object itself is private to this file). `atqa`/`sak` are the
 * identity bytes presented in PT-memory config -- pass the real saved card's
 * values in production, or the fixed MFC-1K test values (0x0004 / 0x08) for
 * the dev/diagnostic persona (which also passes key_resolve_fn=NULL). */
void m1_mfc_raw_hw_init(uint32_t cuid, uint64_t key, uint32_t nt,
                        const uint8_t atqa[2], uint8_t sak,
                        m1_mfc_key_resolve_fn key_resolve_fn, void *key_resolve_ctx,
                        m1_mfc_block_resolve_fn block_resolve_fn, void *block_resolve_ctx);

/* The ONE public entry point for a raw-MFC session -- call from
 * nfc_worker_task's own dispatch (see nfc_driver.c's NFC_STATE_PROCESS,
 * mirroring nfc_poller_mfc_write_active()/nfc_mfc_write_run()'s pattern).
 * Blocks until the session is fully started, run to completion (STOP
 * request, attempt-timeout, or field-loss), and fully torn down -- never
 * returns early leaving ownership ambiguous. Raises nfc_worker_task's own
 * priority to M1_MFC_RAWOWN_PRIORITY for the duration, restored before
 * return. Signals m1_mfc_raw_hw_wait_started()'s waiter (if any) once
 * ownership is ACTIVE and EXTI is enabled, or leaves it unsignaled (caller's
 * wait will time out) if acquisition failed -- see the STARTING-failure
 * rollback in the .c file. Signals m1_mfc_raw_hw_wait_stopped()'s waiter (if
 * any) only after release, interrupt cleanup, the INACTIVE transition, EXTI
 * restoration, and priority restoration are ALL complete. Call only from
 * nfc_worker_task -- see this file's header comment for why no other task
 * may touch ST25R3916 SPI/registers during a raw session. */
void m1_mfc_raw_hw_run(void);

/* UI-side (or any non-nfc_worker_task caller) helpers -- neither touches
 * ST25R3916 SPI/registers directly; both only wait on/signal RTOS
 * synchronization objects that m1_mfc_raw_hw_run() (running inside
 * nfc_worker_task) drives. */

/* Block (bounded) for m1_mfc_raw_hw_run()'s STARTED signal, after the UI has
 * posted the existing Q_EVENT_NFC_MFC_EMULATE trigger. Returns false on
 * timeout -- the caller must NOT assume the session started; report the
 * failure and leave the ownership gate (m1_mfc_raw_hw_active()) as the sole
 * authority on whether anything may proceed. */
bool m1_mfc_raw_hw_wait_started(uint32_t max_ticks);

/* UI prepares the rendezvous before queueing. A request remains reserved
 * until the worker finishes cleanup, including cancelled queued requests. */
bool m1_mfc_raw_hw_prepare_start(void);
bool m1_mfc_raw_hw_start_requested(void);
void m1_mfc_raw_hw_cancel_start(void);
void m1_mfc_raw_hw_finish_request(void);

/* Request STOP (an ISR-safe-style xTaskNotify of M1_RAWOWN_NOTIFY_BIT_STOP
 * to nfc_worker_task, waking m1_mfc_raw_hw_run()'s wait immediately
 * regardless of whether a reader is present) and block (bounded) for its
 * STOPPED signal. Returns false on timeout -- the caller must NOT assume
 * hardware was released, ownership was transferred, or EXTI was
 * re-enabled: none of that has necessarily happened. On false, report the
 * failure and leave the ownership gate closed (m1_mfc_raw_hw_active() still
 * true) rather than proceeding as if the NFC subsystem were free -- doing
 * otherwise risks exactly the concurrent-SPI-access bug this design exists
 * to prevent. */
bool m1_mfc_raw_hw_request_stop_and_wait(uint32_t max_ticks);

/* Invariant-violation canary ONLY -- kept solely because
 * Middlewares/ST/rfal/Src/rfal_nfc.c's rfalNfcListenActivation() (a
 * PRE-EXISTING M1 patch to vendor source, not touched by this backend) calls
 * this exact function by this exact name/signature if it is ever reentered
 * while m1_mfc_raw_hw_active() is true -- structurally unreachable in normal
 * operation (see that file's own comment), so this exists to avoid editing
 * the vendor file rather than to be a normal part of this backend's own
 * control flow (m1_mfc_raw_hw_run() never calls it). If reached, tears the
 * session down via the same corrected STOPPING sequence m1_mfc_raw_hw_run()
 * itself uses. No-op if not currently owning (INACTIVE). */
void m1_mfc_raw_hw_session_end(const char *reason);

/* Central ownership gate: true whenever RAWOWN owns (or is transitioning
 * into/out of owning) the chip -- i.e. state != INACTIVE. Every other NFC
 * operation's own entry point must refuse to start while this is true,
 * regardless of UI navigation state (see nfc_driver.c's NFC_STATE_WAIT
 * dispatch and ListenerCycle's pre-existing secondary guard). Also gates the
 * shared EXTI ISR's dispatch target (m1_mfc_raw_listener.c
 * m1_mfc_authrx_isr()). */
bool m1_mfc_raw_hw_active(void);

/* ISR context: the ENTIRE ISR-side contract for the raw-owned session -- a
 * single ISR-safe xTaskNotifyFromISR give (M1_RAWOWN_NOTIFY_BIT_IRQ) to
 * nfc_worker_task, nothing else. NO SPI, register access, FIFO read, gate
 * logic, Crypto1, logging, allocation, or lock-taking. Only ever invoked
 * when m1_mfc_raw_hw_active() is true (dispatched from the shared EXTI
 * callback in place of RFAL's st25r3916Isr while owned). Safe to call
 * redundantly for coalesced/duplicate wakeups -- xTaskNotifyFromISR's
 * eSetBits OR-accumulates, and m1_mfc_raw_hw_run() drains the chip's own IRQ
 * status in a loop per wake, so a second notify before the first is
 * serviced does not lose any event, it just causes one extra (harmless,
 * empty) drain pass. */
void m1_mfc_raw_hw_isr(void);

/* Deferred diagnostics, safe to print from task context only (never in a
 * timed window). Reports the last completed/failed session's stage reached,
 * the RXE-to-TX-submit / TX-to-TXE margins captured via DWT, and the
 * WU_A/auto-response-disable/AUTH-RXE/Nt-TX counters. */
void m1_mfc_raw_hw_report(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_M1_MFC_RAW_SESSION_HW_H_ */
