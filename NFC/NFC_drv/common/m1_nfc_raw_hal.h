/*============================================================================*/
/**
 * @file    m1_nfc_raw_hal.h
 * @brief   MonstaTek low-level HAL seam for the raw MIFARE Classic emulation
 *          listener (Scope B). Isolates every ST25R3916/RFAL access the raw
 *          listener needs so the protocol logic (m1_mfc_raw_listener) stays
 *          hardware-agnostic.
 *
 * Phase B1 (this file): identity sourcing only -- pull the UID/ATQA/SAK of the
 * card to present into the RFAL listen config, plus a reset hook. NO transparent
 * mode yet; ISO14443-A activation is handled by the ST25R3916 PT-memory HW
 * auto-anticollision that RFAL already configures.
 *
 * Phase B2 will add here: transparent-mode enter/exit and the timer+GPDMA
 * custom-parity load-modulation TX/RX (the "Technique B" waveform engine), which
 * is the only ST25R3916-supported way to emit MIFARE Classic encrypted parity in
 * card-emulation mode (Datasheet Rev 8, Table 27: no_tx_par/no_rx_par are
 * reader-mode only).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_NFC_RAW_HAL_H
#define M1_NFC_RAW_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The card identity presented to a reader in raw MFC emulation. */
typedef struct {
    uint8_t uid[7];
    uint8_t uid_len;   /* 4 or 7                                              */
    uint8_t atqa[2];   /* SENS_RES                                            */
    uint8_t sak;       /* SEL_RES (0x08 = Classic 1K, 0x18 = Classic 4K)      */
    bool    from_saved;/* true = sourced from a read/loaded card; false = fallback */
} m1_raw_identity_t;

/* Resolve the identity to present. Prefers the active emu context (a card that
 * was just read/loaded, via Emu_GetNfcA); if none is available or it is not a
 * MIFARE Classic SAK, falls back to a fixed Classic-1K test identity so B1 is
 * always testable. Always fills *out and returns true. */
bool m1_nfc_raw_get_identity(m1_raw_identity_t *out);

/* Reset any raw-HAL state and hand the radio back to normal RFAL. B1: clears
 * local state only (activation is RFAL-owned). B2 will restore transparent-mode
 * / timer / DMA / framing registers here. Idempotent. */
void m1_nfc_raw_reset(void);

/*===========================================================================*/
/* Phase C RAWOWN backend: exclusive raw passive-target ownership. The GPIO    */
/* ISR only wakes the dedicated task; it performs no SPI/register access. The  */
/* task directly consumes the chip's clear-on-read IRQ status and disables     */
/* NFC-A automatic responses when WU_A/WU_A_X is observed.                     */
/*                                                                              */
/* From acquire() until release(), rfalWorker() is NEVER called and RFAL's     */
/* higher rfalNfcWorker()/ListenerCycle() layer is NEVER invoked -- see         */
/* nfc_driver.c's m1_mfc_raw_hw_active() gate, checked BEFORE                  */
/* nfc_process_func(). rfalListenStart() alone (via its internal               */
/* rfalListenSetState(POWER_OFF) call) is sufficient to arm hardware           */
/* auto-response and issue GOTO_SENSE -- REQA/ANTICOLL/SELECT are entirely     */
/* silicon-automatic and need no software polling at all; rfalWorker() was     */
/* only ever needed for detecting ACTIVE_A, which the dedicated task now does  */
/* directly. There is exactly one caller of every ST25R3916 access from        */
/* session start to end: st25r3916Isr()/rfalWorker() are                       */
/* categorically unreachable while m1_mfc_raw_hw_active() is true (the shared  */
/* EXTI dispatcher branches on that flag before either can run -- see          */
/* m1_mfc_raw_listener.c's m1_mfc_authrx_isr()).                               */
/*                                                                              */
/* release() calls rfalListenStop() then ListenIni() -- the SAME                */
/* top-level rebuild every persona switch already uses -- so normal NFC Read /  */
/* Detect Reader / every other feature resumes exactly as after any other      */
/* persona switch. Idempotent; safe from any terminal path. Called only after  */
/* the dedicated RAWOWN task has been confirmed quiesced (see                  */
/* m1_mfc_raw_session_hw.c's session_end()), so it never races that task's own */
/* SPI access.                                                                 */
/*                                                                              */
/* SINGLE-SESSION LIFETIME: one MFC Emulate entry -> exactly one               */
/* rfalListenStart() call, one BUILD banner, one [B1-PRESENT] line, one raw    */
/* listener init. No listener rebuild / IDLE->DISCOVERY transition can occur   */
/* while the raw session is armed, because ListenerCycle() is never called at  */
/* all during that time -- not merely stopped after a race window. Only field  */
/* loss, timeout, STOP, BACK, or terminal session completion may exit.         */
/*                                                                              */
/* OTHER RULES (enforced by this contract, unchanged):                        */
/*  - RX-ARM-BEFORE-TX: every response arms the NEXT receive as its final step  */
/*    (nonce submit -> arm {Nr,Ar} RX; {At} TX -> arm READ RX), so the reader   */
/*    can never out-race an un-armed receiver.                                 */
/*  - The ISR is limited to a single ISR-safe RTOS task-notification give. NO   */
/*    SPI, register access, Crypto1, logging, allocation, or lock-taking in     */
/*    the ISR, for the WHOLE session (not just a pre-ACTIVE_A phase -- there    */
/*    is no longer a phase split). The dedicated task is the sole reader of    */
/*    IRQ status and sole FIFO/register accessor from session start to end     */
/*    (see m1_mfc_raw_session_hw.c).                                           */
/*===========================================================================*/

typedef struct {
    uint8_t valid;   /* 1 once acquire() has transferred ownership */
} m1_raw_own_snapshot_t;

/* Acquire exclusive raw passive-target ownership at MFC Emulate session entry
 * (called once, synchronously, from ListenIni()'s MFC_EMU branch -- BEFORE the
 * driver loop can ever call nfc_process_func()/ListenerCycle()). Calls
 * rfalListenStart(RFAL_LM_MASK_NFCA, ...) -- the low-level listener-init
 * primitive -- FIRST, then enables the full listener IRQ mask (m1_nfc_raw_own_enable_auth_
 * irqs()) and forces the receiver truly on via rfalListenSetState(IDLE),
 * both AFTER rfalListenStart() returns. This ordering is load-bearing, not
 * cosmetic: rfalListenStart()'s internal rfalListenSetState(POWER_OFF) call
 * does st25r3916DisableInterrupts(ALL) before its own selective re-enable
 * (which does not include RXE/FWL/TXE/NRE), so enabling our mask before that
 * call gets it wiped; that same transition also conditionally clears
 * OP_CONTROL.en if no reader field is present at that instant, and normally
 * only rfalWorker() polling (never called by this backend) would catch the
 * chip's own EON interrupt afterward to re-enable it -- the explicit
 * rfalListenSetState(IDLE) call replaces that recovery. Before reporting
 * success, verifies by direct register readback (not by assuming the calls
 * above worked) that OP_CONTROL.en/rx_en are set, PASSIVE_TARGET.d_106_ac_a
 * is clear, and RXE/WU_A/WU_A_X are enabled in the interrupt mask registers
 * -- on any mismatch, rolls back (disables the mask, rfalListenStop()) and
 * returns false rather than letting the caller believe the session started.
 * Returns false (session not started) if rfalListenStart() itself fails
 * (e.g. RFAL not yet base-initialized) -- the caller must not proceed as if
 * armed. */
bool m1_nfc_raw_own_acquire(m1_raw_own_snapshot_t *snap,
                            const uint8_t uid[4], uint8_t atqa[2], uint8_t sak);

/* Which of acquire()'s internal steps caused its last failure, so the caller
 * can log a specific reason instead of a single ambiguous message that fires
 * identically regardless of cause. Valid only immediately after acquire()
 * returns false; meaningless (M1_RAW_ACQUIRE_FAIL_NONE) after a success. */
#define M1_RAW_ACQUIRE_FAIL_NONE           (0U)
#define M1_RAW_ACQUIRE_FAIL_OSC_ON         (1U)   /* st25r3916OscOn() failed */
#define M1_RAW_ACQUIRE_FAIL_LISTEN_START   (2U)   /* rfalListenStart() != RFAL_ERR_NONE */
#define M1_RAW_ACQUIRE_FAIL_VERIFY         (3U)   /* register-readback verification failed */
uint8_t m1_nfc_raw_own_fail_reason(void);

/* Idempotent release: rfalListenStop() then ListenIni() (the product's own
 * top-level lifecycle path -- see contract above), so discovery/listener/
 * PT-memory state resumes exactly as it does after any other persona switch.
 * Safe to call from any terminal path (DONE/HALT/WUPA/field-loss/STOP/BACK/
 * timeout/err) and repeatedly. */
void m1_nfc_raw_own_release(m1_raw_own_snapshot_t *snap);

/* Clears PASSIVE_TARGET.d_106_ac_a (re-enable NFC-A auto-response), then sends
 * STOP and GOTO_SENSE. Called on ordinary field-off to re-arm the next
 * activation WITHOUT ending
 * the listener session -- only an explicit abort does that. Ownership
 * (m1_raw_own_snapshot_t), EXTI mask, IRQ mask, and task state are untouched
 * by this call; the caller (m1_mfc_raw_session_hw.c on ordinary field-loss)
 * is responsible for resetting per-activation session/protocol state
 * separately. Also used internally by m1_nfc_raw_own_release() and
 * acquire()'s own verify-failure rollback. */
void m1_nfc_raw_own_idle_direct(void);

/* Enable the raw IRQ mask -- FWL, TXE, RXS, RXE, PAR, CRC, ERR1, ERR2, NRE,
 * EOF, WU_A, WU_A_X -- on top of RFAL's
 * own (never touched -- release() only removes exactly these bits) and arm
 * RX. Called from m1_nfc_raw_own_acquire() AFTER rfalListenStart() returns --
 * that call's own internal rfalListenSetState(POWER_OFF) transition disables
 * ALL interrupts before its own selective re-enable, so applying this mask
 * any earlier would just be wiped; see m1_nfc_raw_own_acquire()'s own
 * comment for the full explanation. */
void m1_nfc_raw_own_enable_auth_irqs(void);

/* Arm the receiver for the next reader frame (CLEAR_FIFO + UNMASK_RECEIVE_DATA).
 * Called as the FINAL step of every response so RX is ready before the reader
 * can answer. ISR-safe (bounded, blocking-poll SPI only). */
void m1_nfc_raw_own_arm_rx(void);

/* MCU-level EXTI masking for the ST25R3916 IRQ line, via NVIC_DisableIRQ/
 * EnableIRQ(IRQ_ST25R_EXTI_IRQn) -- the SAME IRQn RFAL's own
 * platformProtectST25RComm()/platformUnprotectST25RComm() (rfal_platform.h)
 * already uses for brief, single-transaction protection; this driver holds
 * it for the whole STARTING/STOPPING transition instead. Masking here is
 * NVIC-level only (the EXTI peripheral's own edge detector and pending bit
 * are untouched), so an edge that occurs while masked still latches as
 * pending and is delivered the instant NVIC_EnableIRQ runs -- no event is
 * lost, only the CPU's reaction to it is deferred. Call ONLY from
 * nfc_worker_task (the sole owner of every ST25R3916 SPI/register access,
 * including these NVIC calls, in this driver's contract). */
void m1_nfc_raw_own_exti_mask(void);
void m1_nfc_raw_own_exti_unmask(void);

/* Clear stale interrupt state across all three places it can hide: the
 * chip's own hardware IRQ status registers, RFAL's software sticky word
 * (both via st25r3916ClearInterrupts(), st25r3916_irq.c -- already used by
 * today's release() path), and the MCU's own latched-but-masked EXTI
 * pending bit (NVIC_ClearPendingIRQ). Call ONLY while EXTI is masked
 * (m1_nfc_raw_own_exti_mask()) and ONLY at the two points the corrected
 * lifecycle calls for: BEFORE m1_nfc_raw_own_acquire()/GOTO_SENSE during
 * STARTING (so a genuine WU_A latched right after GOTO_SENSE is never
 * erased -- calling this AFTER acquire() would risk exactly that), and
 * AFTER m1_nfc_raw_own_release() during STOPPING (so RFAL resumes into a
 * clean, empty state). Never call this between acquire() and release(). */
void m1_nfc_raw_own_clear_stale_irq_state(void);

/* Direct, uncached read of the ST25R3916's own IRQ status registers
 * (IRQ_MAIN..IRQ_TARGET, 0x1A-0x1D), bypassing RFAL's software sticky-status
 * word entirely. Drains the chip's own clear-on-read registers in a
 * loop while the physical IRQ pin remains asserted (multiple back-to-back
 * events coalesce into one call), OR-ing every read together. Call ONLY from
 * the dedicated RAWOWN task, which is this driver's sole consumer of these
 * registers for the whole raw-emulation session (see m1_mfc_raw_session_hw.c).
 * Bounded, blocking-poll SPI only -- safe outside an ISR; NOT called from one. */
uint32_t m1_nfc_raw_read_irq_status(void);

/* "Is a raw-owned session currently active?" has exactly ONE authoritative
 * answer: m1_mfc_raw_hw_active() (m1_mfc_raw_session_hw.h), which
 * nfc_driver.c checks BEFORE calling nfc_process_func() and the shared EXTI
 * ISR checks to select its dispatch target -- both from the very first pass,
 * never racing ListenerCycle(). This module intentionally does not keep a
 * second copy of that flag; acquire()/release() track their own local
 * validity via m1_raw_own_snapshot_t.valid instead. */

#ifdef __cplusplus
}
#endif

#endif /* M1_NFC_RAW_HAL_H */
