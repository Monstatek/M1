/*============================================================================*/
/**
 * @file    m1_mfc_fast.c
 * @brief   Ultra-low-latency MIFARE Classic nonce responder (Scope B / B2a).
 *          See m1_mfc_fast.h. Guarded by M1_MFC_RAW_EMULATION.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#include "m1_mfc_fast.h"

#if defined(M1_MFC_RAW_EMULATION)

#include "stm32h5xx.h"       /* CoreDebug, DWT (cycle counter)                 */
#include "st25r3916.h"       /* commands, st25r3916SetNumTxBits               */
#include "st25r3916_com.h"   /* st25r3916ExecuteCommand, st25r3916WriteFifo   */
#include "rfal_platform.h"   /* platformLog (report only, not the hot path)   */
#include <string.h>

#define MFC_CMD_AUTH_A  0x60U
#define MFC_CMD_AUTH_B  0x61U

static volatile bool  s_armed      = false;
static volatile bool  s_fired_act  = false;   /* fired for current activation   */
static uint8_t        s_nt[4]      = { 0x01U, 0x02U, 0x03U, 0x04U };

/* Deferred diagnostics (filled in the hot path, printed from task context). */
static volatile bool     s_diag_valid = false;
static volatile uint32_t s_diag_rxe   = 0U;
static volatile uint32_t s_diag_tx    = 0U;
static volatile uint32_t s_fire_count = 0U;

static void mfc_fast_dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void m1_mfc_fast_arm(const uint8_t nt[4])
{
    if (nt != NULL) { memcpy(s_nt, nt, 4); }
    mfc_fast_dwt_enable();
    s_fired_act  = false;
    s_diag_valid = false;
    s_fire_count = 0U;
    s_armed      = true;
}

void m1_mfc_fast_disarm(void)
{
    s_armed     = false;
    s_fired_act = false;
}

void m1_mfc_fast_rearm_activation(void)
{
    s_fired_act = false;
}

bool m1_mfc_fast_on_rxe(uint32_t rxe_cyc)
{
    if (!s_armed || s_fired_act) { return false; }
    s_fired_act = true;

    /* Leanest possible reply: set TX length (32 bits), load the plaintext nonce,
     * transmit without CRC. The chip adds standard parity and hardware-aligns the
     * FDT. No logging here -- every microsecond counts against the ~90us window. */
    st25r3916SetNumTxBits(32U);
    st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
    st25r3916WriteFifo(s_nt, 4U);
    st25r3916ExecuteCommand(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);

    s_diag_rxe   = rxe_cyc;
    s_diag_tx    = DWT->CYCCNT;
    s_fire_count++;
    s_diag_valid = true;
    return true;
}

void m1_mfc_fast_report(void)
{
    if (!s_diag_valid) { return; }
    s_diag_valid = false;

    uint32_t cpu = SystemCoreClock / 1000000U;
    if (cpu == 0U) { cpu = 1U; }
    uint32_t dcyc = s_diag_tx - s_diag_rxe;

    platformLog("[B2-NT] fired=%lu nt=%02X%02X%02X%02X rxe->tx=%lu cyc (%luus)\r\n",
                (unsigned long)s_fire_count,
                s_nt[0], s_nt[1], s_nt[2], s_nt[3],
                (unsigned long)dcyc, (unsigned long)(dcyc / cpu));
}

#endif /* M1_MFC_RAW_EMULATION */
