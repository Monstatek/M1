/* See COPYING.txt for license details. */
/*
 * m1_t2t_transport.h - dedicated Type-2 Tag (MIFARE Ultralight/NTAG) listener
 * transport.
 *
 * Owns the complete Type-2 radio session lifecycle (activation, first-frame
 * retrieval, response TX, RX rearm, HALT/field-loss/reactivation, clean
 * stop), calling M1's low-level RFAL listen-mode primitives DIRECTLY
 * (rfalListenStart/Stop/GetState/SetState, rfalStartTransceive/
 * rfalGetTransceiveStatus/rfalWorker) and bypassing the higher rfalNfc*()
 * convenience layer (rfalNfcDiscover/rfalNfcWorker/rfalNfcDataExchangeStart/
 * rfalNfcDataExchangeGetStatus) entirely for Type-2 emulation. That layer's
 * "already-buffered first frame" retrieval has twice failed hardware
 * acceptance (T4: wrong dev->type/rfInterface; T5: its GetStatus() verdict
 * still wrong for T2T even once the pointer retrieval itself was correct) --
 * this module depends on none of it.
 *
 * For ISO14443-3A/NFC Forum Type 2 Tag operation, activation and
 * "first frame received" are NOT modeled as two sequential states mediated
 * by a status call (RFAL's rfal_nfc.c does this, and it's the proven-twice
 * defect) -- they are one unified check (rfalListenGetState() reporting
 * both the active LM state AND its dataFlag together), so there is no
 * intermediate verdict that can disagree with reality.
 *
 * Ownership pattern (acquire/release, one session at a time, coexistence
 * gate checked before nfc_process_func()) is modeled on M1's own proven MFC
 * raw-emulation hardware-ownership contract (m1_nfc_raw_hal.h) -- but NONE
 * of MFC's Crypto1, transparent-mode DMA {At} engine, or dedicated-ISR/task
 * split is reused: Type-2 uses only standard ISO14443-A parity/CRC, fully
 * expressible through RFAL's already-proven-working standard transceive
 * (rfalTransceiveBlockingTx() for CRC'd responses, the existing
 * CeSendShortFrame() for 4-bit ACK/NAK) and its own rfalWorker()-serviced
 * interrupt handling -- no bespoke ISR or DMA is needed here.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef M1_T2T_TRANSPORT_H_
#define M1_T2T_TRANSPORT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dispatches one already-validated Type-2 command frame (rx/rxBits, rxBits
 * in BITS) and returns true if it was handled -- matching CeHandleT2TCmdRx()
 * exactly (this IS CeHandleT2TCmdRx(), wired in via callback so this module
 * never #includes nfc_listener.h and nfc_listener.c never #includes this
 * header's internals -- no circular dependency). The callback is
 * responsible for building and transmitting the response AND rearming RX on
 * success (via m1_t2t_transport_rearm_rx(), through the existing
 * CeRearmRxAfterTx() one-line branch) -- unchanged from today. On a false
 * return (command not handled), this module rearms RX itself. */
typedef bool (*m1_t2t_dispatch_fn)(const uint8_t *rx, uint16_t rxBits);

/* Register the dispatch callback once, at module/persona init (before any
 * m1_t2t_transport_start()). NULL is a valid "not wired yet" state in which
 * start() will refuse to arm (never silently drops frames into nowhere). */
void m1_t2t_transport_set_dispatch(m1_t2t_dispatch_fn cb);

/* Start a Type-2 listen session with the given identity (from the armed
 * saved-card image / synced emu context -- same source ListenIni() already
 * uses today). uidLen must be 4 or 7. Calls rfalListenStart() directly.
 * Returns false (session NOT started; caller must not proceed as armed) if
 * rfalListenStart() itself fails or no dispatch callback is registered. */
bool m1_t2t_transport_start(const uint8_t *uid, uint8_t uidLen,
                            const uint8_t atqa[2], uint8_t sak);

/* Idempotent: rfalListenStop() + local state reset. Safe from any exit path
 * (BACK/STOP, field-loss-with-no-reactivation, failed activation, normal
 * teardown) and safe to call when not started. Guarantees exactly one
 * rfalListenStop() per prior successful start() (a second call is a no-op,
 * not a second stop). */
void m1_t2t_transport_stop(void);

/* True from a successful start() until stop(). The nfc_driver.c ownership
 * gate checks this BEFORE nfc_process_func() -- exactly mirroring
 * m1_mfc_raw_hw_active()'s existing role and ordering, so MFC and Type-2
 * ownership can never overlap (a session is at most one persona at a time
 * already; this is defense in depth, matching the existing pattern). While
 * true, the legacy T2T handling inside ListenerCycle() is unreachable
 * because ListenerCycle() itself is never invoked for this persona -- there
 * is no code path where both the old and new T2T exchange logic can run. */
bool m1_t2t_transport_is_active(void);

/* Non-blocking, called once per worker-task tick while active (mirrors
 * ListenerCycle()'s own per-tick contract -- never blocks the worker/UI
 * task). Services rfalWorker() once, then advances the internal RX/TX state
 * machine: wait-for-activation-or-first-frame, dispatch, wait-for-next-
 * frame (via a directly-owned rfalStartTransceive(), never through
 * rfal_nfc.c), HALT/field-loss/reactivation handling. */
void m1_t2t_transport_tick(void);

/* Rearm the receiver for the next Type-2 frame via a directly-owned
 * rfalStartTransceive() (bypassing rfal_nfc.c entirely) -- called both from
 * this module's own "command not handled" fallback and from
 * CeRearmRxAfterTx()'s new branch (nfc_listener.c) when this transport owns
 * the session, so CeHandleT2TCmdRx() and its response builders need no
 * changes at all. Returns false if rfalStartTransceive() itself fails. */
bool m1_t2t_transport_rearm_rx(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_T2T_TRANSPORT_H_ */
