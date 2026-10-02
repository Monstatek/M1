/*============================================================================*/
/**
 * @file    m1_nfc_raw_hal.c
 * @brief   MonstaTek low-level HAL seam for raw MIFARE Classic emulation (B1).
 *          See m1_nfc_raw_hal.h. Guarded by M1_MFC_RAW_EMULATION so the standard
 *          build is byte-identical when the feature is off.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#include "m1_nfc_raw_hal.h"

#if defined(M1_MFC_RAW_EMULATION)

#include "legacy/nfc_driver.h"   /* Emu_GetNfcA, EmuNfcA_t */
#include "rfal_utils.h"          /* ReturnCode/RFAL_ERR_NONE only -- st25r3916OscOn()'s
                                  * return type. No RFAL RF-layer/LM state-machine
                                  * function is called anywhere in this file. */
#include "st25r3916.h"           /* SetNumTxBits, GetNumFIFOBytes, commands       */
#include "st25r3916_com.h"       /* ExecuteCommand, register addresses, ReadMultipleRegisters */
#include "st25r3916_irq.h"       /* Enable/Disable/Clear interrupts, masks        */
#include "rfal_platform.h"       /* platformGpioIsHigh                            */
#include "nfc_conf.h"            /* ST25R_INT_PORT / ST25R_INT_PIN                */
#include <string.h>

bool m1_nfc_raw_get_identity(m1_raw_identity_t *out)
{
    if (out == NULL) { return false; }
    memset(out, 0, sizeof(*out));

    EmuNfcA_t emuA;
    /* Prefer a just-read/loaded MIFARE Classic card (SAK 0x08 = 1K, 0x18 = 4K). */
    if (Emu_GetNfcA(&emuA) && emuA.valid &&
        ((emuA.sak == 0x08U) || (emuA.sak == 0x18U))) {
        out->uid_len = (emuA.uid_len == 7U) ? 7U : 4U;
        memcpy(out->uid, emuA.uid, out->uid_len);
        out->atqa[0]    = emuA.atqa[0];
        out->atqa[1]    = emuA.atqa[1];
        out->sak        = emuA.sak;
        out->from_saved = true;
        return true;
    }

    /* Fallback: fixed MIFARE Classic 1K identity so B1 is always testable. */
    {
        static const uint8_t fb_uid[4] = { 0x01U, 0x02U, 0x03U, 0x04U };
        memcpy(out->uid, fb_uid, sizeof(fb_uid));
        out->uid_len    = 4U;
        out->atqa[0]    = 0x04U;   /* ATQA 0x0004 */
        out->atqa[1]    = 0x00U;
        out->sak        = 0x08U;   /* SAK 0x08 = MIFARE Classic 1K */
        out->from_saved = false;
    }
    return true;
}

void m1_nfc_raw_reset(void)
{
    /* B1: no low-level hardware state is owned yet -- ISO14443-A activation is
     * handled by RFAL/PT-memory HW auto-anticollision. B2 will restore
     * transparent-mode, timer, GPDMA and framing/CRC/parity registers here. */
}

/*===========================================================================*/
/* Phase C: exclusive raw passive-target ownership. See m1_nfc_raw_hal.h for   */
/* the full contract and the root-cause note on why the prior (rejected)      */
/* rfalNfcDeactivate-based hybrid left RFAL and the raw backend as concurrent  */
/* owners on hardware.                                                        */
/*===========================================================================*/

/* Complete listener interrupt mask: FWL, TXE, RXS, RXE, PAR, CRC, ERR1, ERR2, NRE, EON,
 * EOF, WU_A_X, WU_A -- all 13 bits, none omitted. Earlier revisions of this
 * mask omitted EON on the theory that RFAL's own rfalListenStart() enabled
 * it as a side effect; now that acquire() no longer calls rfalListenStart()
 * at all (see below), nothing enables it unless this mask does, so it is
 * listed explicitly here rather than depending on a side effect that no
 * longer exists. */
#define M1_RAW_OWN_IRQ_MASK  (ST25R3916_IRQ_MASK_FWL  | ST25R3916_IRQ_MASK_TXE  | \
                              ST25R3916_IRQ_MASK_RXS  | ST25R3916_IRQ_MASK_RXE  | \
                              ST25R3916_IRQ_MASK_PAR  | ST25R3916_IRQ_MASK_CRC  | \
                              ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | \
                              ST25R3916_IRQ_MASK_NRE  | ST25R3916_IRQ_MASK_EON  | \
                              ST25R3916_IRQ_MASK_EOF  | ST25R3916_IRQ_MASK_WU_A | \
                              ST25R3916_IRQ_MASK_WU_A_X)

/* The chip's own IRQ status register block (R, clear-on-read at the silicon
 * level), read directly here -- NOT via RFAL's st25r3916GetInterrupt(), which
 * only drains the separate software sticky word st25r3916Isr() populates.
 * IRQ_MAIN (0x1A) through IRQ_TARGET (0x1D), four consecutive read-only
 * registers. */
#define M1_RAW_IRQ_STATUS_REG_BASE  (ST25R3916_REG_IRQ_MAIN)
#define M1_RAW_IRQ_STATUS_REG_LEN   (4U)

/* Arm the next receive: clear the FIFO and unmask receive data. Called as the
 * FINAL step of every response (nonce / {At} / block) so RX is ready before the
 * reader can answer -- never "wait for TXE, then configure RX". Bounded,
 * blocking-poll SPI only -> ISR-safe. */
void m1_nfc_raw_own_arm_rx(void)
{
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
}

void m1_nfc_raw_own_enable_auth_irqs(void)
{
    st25r3916EnableInterrupts(M1_RAW_OWN_IRQ_MASK);
    m1_nfc_raw_own_arm_rx();
}

/* Verify by direct register readback -- not by assuming the calls in
 * m1_nfc_raw_own_acquire() worked -- that the chip is actually armed to
 * receive REQA and respond automatically:
 *   - OP_CONTROL.en = 1, OP_CONTROL.rx_en = 1 (RF frontend + receiver on)
 *   - PASSIVE_TARGET.d_106_ac_a clear (hardware auto-response for NFC-A
 *     still enabled -- required for REQA/ANTICOLL/SELECT)
 *   - RXE, WU_A, WU_A_X unmasked in the interrupt MASK registers (0x16-0x19,
 *     the enable-mask counterpart to the status registers
 *     m1_nfc_raw_read_irq_status() reads at 0x1A-0x1D; same bit layout, so
 *     the same ST25R3916_IRQ_MASK_* constants apply as bit positions) -- but
 *     note the POLARITY on this specific register block is inverted from
 *     the status block: st25r3916EnableInterrupts(mask) ->
 *     st25r3916ModifyInterrupts(clr_mask=mask, set_mask=0) (st25r3916_irq.c)
 *     CLEARS those bits in the tracked mask and writes that cleared value
 *     out -- i.e. a CLEAR bit here means "unmasked" (enabled), a SET bit
 *     means "masked" (disabled). These are exactly the bits this backend
 *     depends on to ever see SELECT-completion and post-SELECT frames.
 * Bounded, blocking-poll SPI only -- safe to call from task context (the
 * only context m1_nfc_raw_own_acquire() runs in). */
static bool m1_nfc_raw_own_verify_armed(void)
{
    uint8_t op_control = 0U;
    (void)st25r3916ReadRegister(ST25R3916_REG_OP_CONTROL, &op_control);
    if ((op_control & (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en)) !=
        (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en)) {
        return false;
    }

    uint8_t passive_target = 0U;
    (void)st25r3916ReadRegister(ST25R3916_REG_PASSIVE_TARGET, &passive_target);
    if ((passive_target & ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a) != 0U) {
        return false;
    }

    uint8_t imask[4] = {0};
    (void)st25r3916ReadMultipleRegisters(ST25R3916_REG_IRQ_MASK_MAIN, imask, 4U);
    uint32_t masked_off = (uint32_t)imask[0] | ((uint32_t)imask[1] << 8) |
                          ((uint32_t)imask[2] << 16) | ((uint32_t)imask[3] << 24);
    const uint32_t required = ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X;
    if ((masked_off & required) != 0U) {   /* any required bit still SET here is still masked/disabled */
        return false;
    }

    return true;
}

/* No RFAL call, consistent with acquire(). Re-enables automatic responses (so
 * a bare, un-owned chip left listening still autonomously handles REQA --
 * exactly the state every other persona's own subsequent
 * rfalListenStart()/rfalNfcDiscover() call already reprograms unconditionally
 * on its way in; rfalListenSetState(POWER_OFF)'s body does not gate on any
 * prior gRFAL.Lm.state value, confirmed by reading it) and returns to sense
 * mode. Shared by m1_nfc_raw_own_release(), acquire()'s own verify-failure
 * rollback, and (non-static: see m1_nfc_raw_hal.h) m1_mfc_raw_session_hw.c's
 * ordinary-field-loss re-arm. Only an explicit abort ends the session.
 * So there remains exactly one place this register sequence is written. */
void m1_nfc_raw_own_idle_direct(void)
{
    (void)st25r3916ClrRegisterBits(ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_STOP);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SENSE);
}

/* Which of acquire()'s own steps last failed -- see m1_nfc_raw_own_fail_
 * reason()'s declaration. The single "[B1-RAWHW-START-FAIL] rfalListenStart()
 * rejected" log line this drives from m1_mfc_raw_hw_starting() previously
 * fired identically regardless of which internal check failed, which cost
 * real diagnosis time working out from source alone that a verify_armed()
 * polarity bug (not rfalListenStart() itself) was the actual cause -- this
 * makes that distinction directly visible in the next capture. */
static uint8_t s_last_acquire_fail_reason = 0U;   /* M1_RAW_ACQUIRE_FAIL_* */

uint8_t m1_nfc_raw_own_fail_reason(void)
{
    return s_last_acquire_fail_reason;
}

bool m1_nfc_raw_own_acquire(m1_raw_own_snapshot_t *snap,
                            const uint8_t uid[4], uint8_t atqa[2], uint8_t sak)
{
    s_last_acquire_fail_reason = M1_RAW_ACQUIRE_FAIL_NONE;
    if ((snap == NULL) || (uid == NULL) || (atqa == NULL)) { return false; }
    memset(snap, 0, sizeof(*snap));

    /* Direct listener sequence, not rfalListenStart()/rfalListenSetState()
     * (removed entirely from this
     * function). Every hardware surprise found while hybridizing with RFAL's
     * low-level LM state machine (the field-dependent OP_CONTROL.en clear in
     * rfalListenSetState(POWER_OFF), the DisableInterrupts(ALL) that wiped
     * this driver's own IRQ mask, the IRQ-mask polarity mismatch, the missing
     * NFC-A-listen analog config) traced back to that state machine being
     * designed for continuous rfalWorker() polling, which this backend
     * deliberately never does. Use a flat, unconditional sequence of direct
     * register writes in a fixed order with the vendor st25r3916* primitives (every constant
     * used confirmed present, identically named, in st25r3916_com.h) --
     * still bounded, blocking-poll SPI only, task-context only, no vendor
     * RFAL file touched. */

    /* Oscillator precondition: a standalone, self-contained utility (no RFAL
     * LM state-machine involvement -- verified by reading its body), not
     * part of the register sequence but harmless ahead of it: the first write
     * below (OP_CONTROL) sets `en` unconditionally regardless,
     * so this can only make that write redundant-safe, never wrong. */
    if (st25r3916OscOn() != RFAL_ERR_NONE) {
        s_last_acquire_fail_reason = M1_RAW_ACQUIRE_FAIL_OSC_ON;
        return false;
    }

    /* --- ISO14443-A listener initialization ------------------------------ */

    /* OP_CONTROL: en|rx_en|en_fd_auto_efd, unconditional -- unlike RFAL's
     * rfalListenSetState(POWER_OFF), there is no field-presence check here
     * and nothing clears `en` again afterward. This is the fix for the
     * "chip goes to sleep at acquire time, nothing wakes it" bug by keeping
     * the chip enabled. */
    (void)st25r3916WriteRegister(ST25R3916_REG_OP_CONTROL,
                                 ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
                                 ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
    (void)st25r3916WriteRegister(ST25R3916_REG_MODE,
                                 ST25R3916_REG_MODE_targ_targ | ST25R3916_REG_MODE_om0);
    (void)st25r3916WriteRegister(ST25R3916_REG_PASSIVE_TARGET,
                                 ST25R3916_REG_PASSIVE_TARGET_fdel_2 | ST25R3916_REG_PASSIVE_TARGET_fdel_0 |
                                 ST25R3916_REG_PASSIVE_TARGET_d_ac_ap2p | ST25R3916_REG_PASSIVE_TARGET_d_212_424_1r);
    (void)st25r3916WriteRegister(ST25R3916_REG_MASK_RX_TIMER, 0x02U);

    (void)st25r3916ExecuteCommand(ST25R3916_CMD_STOP);

    /* Clear stale interrupt state, then enable the complete listener mask
     * (M1_RAW_OWN_IRQ_MASK, now including EON -- see its own comment above)
     * using the vendor drain and enable primitives. */
    st25r3916ClearInterrupts();
    m1_nfc_raw_own_enable_auth_irqs();

    /* Enable automatic responses for NFC-A (auto-ATQA/anticollision/SAK),
     * then enter sense mode. */
    (void)st25r3916ClrRegisterBits(ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SENSE);

    /* --- ISO14443-A receiver initialization ------------------------------
     * Receiver analog front-end (demodulator/AGC/correlator) for NFC-A --
     * RFAL's own analog-config table has no LISTEN|TECH_NFCA entry for these
     * registers at all, so set them explicitly and unconditionally. */
    (void)st25r3916WriteRegister(ST25R3916_REG_RX_CONF1, ST25R3916_REG_RX_CONF1_z600k);
    (void)st25r3916WriteRegister(ST25R3916_REG_RX_CONF2,
                                 ST25R3916_REG_RX_CONF2_agc6_3 | ST25R3916_REG_RX_CONF2_agc_m |
                                 ST25R3916_REG_RX_CONF2_agc_en | ST25R3916_REG_RX_CONF2_sqm_dyn);
    (void)st25r3916WriteRegister(ST25R3916_REG_RX_CONF3, 0x00U);
    (void)st25r3916WriteRegister(ST25R3916_REG_RX_CONF4, 0x00U);
    (void)st25r3916WriteRegister(ST25R3916_REG_CORR_CONF1,
                                 ST25R3916_REG_CORR_CONF1_corr_s0 | ST25R3916_REG_CORR_CONF1_corr_s4 |
                                 ST25R3916_REG_CORR_CONF1_corr_s6);
    (void)st25r3916WriteRegister(ST25R3916_REG_CORR_CONF2, 0x00U);

    /* --- ISO14443-A collision-resolution data ----------------------------
     * PT-memory (UID/ATQA/SAK). Fixed 4-byte-UID persona, matching this
     * backend's own contract -- so only the 4-byte branch is ported.
     * st25r3916WritePTMem() is the same public, low-level primitive RFAL's
     * own rfalListenStart() used internally for this exact write
     * (rfal_rfst25r3916.c ST25R3916_PTM_A_LEN == 15) -- reused directly, not
     * reimplemented, so the SPI framing for this write is the same
     * already-proven code either way. */
    (void)st25r3916ChangeRegisterBits(ST25R3916_REG_AUX, ST25R3916_REG_AUX_nfc_id_mask,
                                      ST25R3916_REG_AUX_nfc_id_4bytes);
    {
        uint8_t pt_memory[ST25R3916_PTM_A_LEN] = {0};
        pt_memory[0] = uid[0]; pt_memory[1] = uid[1]; pt_memory[2] = uid[2]; pt_memory[3] = uid[3];
        pt_memory[10] = atqa[0];
        pt_memory[11] = atqa[1];
        pt_memory[12] = (uint8_t)(sak & ~0x04U);
        pt_memory[13] = (uint8_t)(sak & ~0x04U);
        pt_memory[14] = (uint8_t)(sak & ~0x04U);
        (void)st25r3916WritePTMem(pt_memory, ST25R3916_PTM_A_LEN);
    }

    /* Verify by direct register readback -- not by trusting the writes above
     * "should have" worked -- before this function reports success (and
     * m1_mfc_raw_hw_run() gives STARTED on the strength of that). On
     * failure, roll back exactly what this function armed and report
     * failure; do not leave the chip in a possibly wrong intermediate
     * state. */
    if (!m1_nfc_raw_own_verify_armed()) {
        s_last_acquire_fail_reason = M1_RAW_ACQUIRE_FAIL_VERIFY;
        st25r3916DisableInterrupts(M1_RAW_OWN_IRQ_MASK);
        m1_nfc_raw_own_idle_direct();
        return false;
    }

    snap->valid = 1U;   /* single authoritative "ownership acquired" signal -- see
                         * m1_mfc_raw_hw_active()/s_state in m1_mfc_raw_session_hw.c,
                         * which nfc_driver.c and the shared EXTI ISR actually gate on */
    return true;
}

void m1_nfc_raw_own_release(m1_raw_own_snapshot_t *snap)
{
    /* Idempotent: safe from any terminal path and repeatedly. A second call
     * after the first has already released is a no-op. In practice the sole
     * caller (m1_mfc_raw_hw_stopping(), m1_mfc_raw_session_hw.c) only ever
     * reaches here once per session; this check is defense in depth against
     * a direct call, using snap->valid rather than a second static flag so
     * there is exactly one place ownership state is tracked. */
    if ((snap == NULL) || (snap->valid == 0U)) { return; }

    st25r3916DisableInterrupts(M1_RAW_OWN_IRQ_MASK);
    st25r3916ClearInterrupts();

    snap->valid = 0U;   /* snap is non-NULL and was valid -- guaranteed by the guard above */

    m1_nfc_raw_own_idle_direct();
}

/* NVIC-level EXTI mask/unmask for the ST25R3916 IRQ line -- see the
 * declarations in m1_nfc_raw_hal.h. IRQ_ST25R_EXTI_IRQn is the same IRQn
 * platformProtectST25RComm()/platformUnprotectST25RComm() (rfal_platform.h)
 * already uses for brief, single-transaction protection elsewhere in RFAL --
 * reusing it here (held for a whole STARTING/STOPPING transition rather than
 * one SPI transaction) is the same underlying mechanism, not a competing one. */
void m1_nfc_raw_own_exti_mask(void)
{
    NVIC_DisableIRQ(IRQ_ST25R_EXTI_IRQn);
}

void m1_nfc_raw_own_exti_unmask(void)
{
    NVIC_EnableIRQ(IRQ_ST25R_EXTI_IRQn);
}

/* Clear stale interrupt state -- hardware + RFAL-sticky (one call) + MCU
 * pending. See the declaration in m1_nfc_raw_hal.h for exactly when this may
 * be called (before acquire()/GOTO_SENSE on STARTING, after release() on
 * STOPPING -- never in between). */
void m1_nfc_raw_own_clear_stale_irq_state(void)
{
    st25r3916ClearInterrupts();
    NVIC_ClearPendingIRQ(IRQ_ST25R_EXTI_IRQn);
}

/* Direct read of the chip's own IRQ status registers -- see the declaration
 * in m1_nfc_raw_hal.h and M1_RAW_IRQ_STATUS_REG_BASE/_LEN above. Loops over
 * the 4-register block while the physical IRQ pin is still asserted (the
 * chip's own registers self-clear on read; a still-asserted pin after one
 * read means another event landed during the SPI transaction), OR-ing every
 * read together so back-to-back events within one wake coalesce into a
 * single returned mask instead of being silently dropped. platformGpioIsHigh/
 * ST25R_INT_PORT/ST25R_INT_PIN are the same primitives RFAL's own
 * st25r3916CheckForReceivedInterrupts() uses (st25r3916_irq.c) -- this
 * function is a deliberately independent, parallel implementation that never
 * touches st25r3916interrupt.status, so it cannot desync from or race RFAL's
 * own bookkeeping (which, while raw owns the session, is never touched by
 * anything -- see m1_mfc_authrx_isr()). Bounded, blocking-poll SPI only --
 * safe to call from task context; NOT ISR-safe (not called from one). */
uint32_t m1_nfc_raw_read_irq_status(void)
{
    uint32_t irq_status = 0U;

    while (platformGpioIsHigh(ST25R_INT_PORT, ST25R_INT_PIN)) {
        uint8_t iregs[M1_RAW_IRQ_STATUS_REG_LEN] = {0};
        (void)st25r3916ReadMultipleRegisters(M1_RAW_IRQ_STATUS_REG_BASE, iregs,
                                             M1_RAW_IRQ_STATUS_REG_LEN);
        irq_status |= (uint32_t)iregs[0];
        irq_status |= (uint32_t)iregs[1] << 8;
        irq_status |= (uint32_t)iregs[2] << 16;
        irq_status |= (uint32_t)iregs[3] << 24;
    }

    return irq_status;
}

#endif /* M1_MFC_RAW_EMULATION */
