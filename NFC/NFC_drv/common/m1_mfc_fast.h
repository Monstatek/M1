/*============================================================================*/
/**
 * @file    m1_mfc_fast.h
 * @brief   Ultra-low-latency MIFARE Classic listen-mode responder (Scope B / B2).
 *
 * The card->reader nonce reply must land within the ISO14443A FDT (~90us of the
 * reader's AUTH). The normal RFAL path reaches software ~158us after RXE and then
 * spends ~62us in rfalStartTransceive -- far too late (measured 220us; reader
 * rejects, sends WUPA). This module is called from the earliest software point
 * (the ST25R3916 RXE detection inside rfalWorker) and fires a pre-armed reply with
 * the leanest possible chip sequence (set len + FIFO + TRANSMIT), letting the chip
 * hardware-time the FDT.
 *
 * B2a (this file): plaintext first-auth nonce `nt` (standard parity, no CRC).
 * B2b will add the encrypted {At} via the transparent-mode waveform path.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_MFC_FAST_H
#define M1_MFC_FAST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arm the session: store the plaintext nonce to reply with, reset per-activation
 * state, and enable the fast responder. `nt` is 4 bytes, MSB-first as transmitted. */
void m1_mfc_fast_arm(const uint8_t nt[4]);

/* Disarm (session end / teardown). Idempotent. */
void m1_mfc_fast_disarm(void);

/* Re-arm for a fresh activation (call when the LM re-enters ACTIVE_A) so the
 * one-shot nonce reply fires again on the next reader auth. */
void m1_mfc_fast_rearm_activation(void);

/* Called from the RXE hook (LM == ACTIVE_A, RXE just seen). If armed and not yet
 * fired for this activation, transmits the pre-armed nonce with the minimal chip
 * sequence and records the RXE->TX latency. Returns true if it transmitted.
 * MUST stay lean -- no platformLog here (that is deferred to m1_mfc_fast_report). */
bool m1_mfc_fast_on_rxe(uint32_t rxe_cyc);

/* Emit the deferred [B2-NT] diagnostic (call from the normal task context, not
 * the hot path). Prints once per fire. */
void m1_mfc_fast_report(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_MFC_FAST_H */
