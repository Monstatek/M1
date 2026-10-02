/* See COPYING.txt for license details. */

/*
 * lfrfid_dma_tx.h
 *
 * Deterministic hardware-timed LF emulation transport.
 *
 * Replaces the previous per-edge, CPU/ISR-serviced TIM5 update handler
 * (rfid_emul_handler() in lfrfid_hal.c, still present and used for LF READ
 * capture only -- untouched by this module) with a genuinely continuous,
 * DMA-driven waveform replay:
 *
 *   TIM5 update event -> GPDMA2 channel A -> GPIOA->BSRR (next GPIO state)
 *   TIM5 update event -> GPDMA2 channel B -> TIM5->ARR   (next period)
 *
 * Both channels are triggered by the identical GPDMA2_REQUEST_TIM5_UP
 * hardware request line (one word per request, DMA_BREQ_SINGLE_BURST), the
 * same pairing mechanism already hardware-validated for the MFC {At}
 * transmitter's GPDMA1/TIM7 pair (NFC/NFC_drv/common/m1_mfc_dma.c) -- this
 * module is the same technique retargeted to GPDMA2/TIM5/GPIOA/PA2, made
 * continuous (linked-list circular) instead of one-shot.
 *
 * GPDMA2 is used specifically because it is completely unclaimed elsewhere
 * in this firmware (GPDMA1 channels 0,1,2,4,5,6,7 are already owned by
 * Sub-GHz TX, log TX/RX, ESP32 UART, capture, and MFC {At} -- verified by
 * repo-wide grep before this module was written). Channels 0 and 1 of
 * GPDMA2 are claimed here; nothing else in this firmware uses GPDMA2 at
 * all, so there is no contention and no other subsystem is affected.
 *
 * Once started, steady-state operation involves NO CPU/ISR participation
 * whatsoever -- the two DMA linked lists free-run in hardware until BACK
 * (lfrfid_dma_tx_stop()) or a real transfer error is detected by the
 * periodic health poll (lfrfid_dma_tx_check_health(), called from task
 * context, NOT from the TIM5 update path). This eliminates the coupling
 * between the transmitted waveform and CPU interrupt-entry/execution
 * latency that made the old ISR path's protocol-specific fixed "-2"/"-3"
 * fudge constants fragile (see the removed EMUL_PERIOD_*_US / -2 and
 * EMUL_EM4100_CORR constants in the protocol files, and this module's own
 * ARR-inclusive-counting conversion, which now performs that correction
 * exactly once, centrally, from the real measured TIM5 kernel clock).
 */
#ifndef LFRFID_DMA_TX_H_
#define LFRFID_DMA_TX_H_

#include <stdint.h>
#include <stdbool.h>
#include "lfrfid_dma_types.h"   /* EncodedTx_Data_t / Encoded_Data_t, HAL-free */

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum waveform steps this transport will accept. Must be >= the largest
 * ENCODED_DATA_MAX-bounded waveform any protocol builder can produce (see
 * lfrfid_hal.h's own ENCODED_DATA_MAX=4096 sizing comment). Kept as an
 * independent constant (not a #include of ENCODED_DATA_MAX) so the pure
 * preparation function below has no dependency beyond this header. */
#define LFRFID_DMA_TX_MAX_STEPS  4096U

typedef enum {
    LFRFID_DMA_TX_OK = 0,
    LFRFID_DMA_TX_ERR_NULL,          /* wave / wave->data was NULL           */
    LFRFID_DMA_TX_ERR_EMPTY,         /* wave->length == 0                    */
    LFRFID_DMA_TX_ERR_OVERSIZE,      /* wave->length > LFRFID_DMA_TX_MAX_STEPS or caller's out capacity */
    LFRFID_DMA_TX_ERR_BAD_DURATION,  /* a step's time_us produced <1 tick at the given tick_hz */
    LFRFID_DMA_TX_ERR_TOO_SHORT,     /* wave->length < 2 (need at least one manual-prime step + one DMA step) */
    LFRFID_DMA_TX_ERR_HW,            /* HAL/DMA/timer setup or start failed  */
} lfrfid_dma_tx_status_t;

/*============================================================================
 * PURE TRANSPORT-PREPARATION SEAM -- no HAL/RTOS/register access, fully
 * host-testable. Converts a completed EncodedTx_Data_t waveform (as already
 * supplied by every existing native protocol encoder -- em4100/h10301/awid/
 * ioprox/pyramid -- unchanged) into two tightly-packed uint32_t arrays ready
 * to be DMA'd verbatim into GPIOA->BSRR and TIM5->ARR:
 *
 *   bsrr_out[i] = wave->data[i].bsrr                       (copied as-is)
 *   arr_out[i]  = round(wave->data[i].time_us * tick_hz / 1000000) - 1
 *
 * The "-1" is the standard inclusive up-counter correction (a timer whose
 * ARR holds N counts N+1 ticks, 0..N inclusive, before the update event) --
 * previously missing from the ISR path (rfid_emul_handler() writes
 * ARR = time_us with no adjustment at all), which is exactly what each
 * protocol builder's own hand-tuned "-2"/"-3" constant was silently
 * compensating for. This function is the ONE place that correction now
 * happens, derived from the real measured TIM5 kernel clock rather than a
 * fixed per-protocol guess.
 *
 * out capacity (out_cap) must be >= wave->length or LFRFID_DMA_TX_ERR_OVERSIZE
 * is returned before anything is written. *out_count receives the number of
 * entries actually written (== wave->length) on success only.
 */
lfrfid_dma_tx_status_t lfrfid_dma_tx_prepare(const EncodedTx_Data_t *wave,
                                             uint32_t tick_hz,
                                             uint32_t *bsrr_out,
                                             uint32_t *arr_out,
                                             uint16_t out_cap,
                                             uint16_t *out_count);

/*============================================================================
 * HARDWARE ENTRY POINTS (ARM target only -- touch real registers/HAL state).
 *============================================================================*/

/* Start continuous DMA-driven emulation of the given completed waveform.
 * Fails closed on any invalid waveform or HAL/DMA/timer setup error --
 * returns false and leaves no hardware armed; NEVER falls back to the old
 * per-edge ISR transmitter (rfid_emul_handler() is not touched/reused by
 * this path at all). Safe to call again after a prior lfrfid_dma_tx_stop()
 * (or after a failed start, which already leaves nothing armed). */
bool lfrfid_dma_tx_start(const EncodedTx_Data_t *wave);

/* Stop and fully release: disable TIM5, abort + de-init both GPDMA2 linked
 * lists, force PA2 (RFID_PULL) inactive, clear pending flags. Idempotent --
 * safe to call when nothing is running (including after a failed start). */
void lfrfid_dma_tx_stop(void);

/* True only while a start() has succeeded and stop()/an error has not yet
 * torn it down. */
bool lfrfid_dma_tx_active(void);

/* Periodic health poll -- call from TASK CONTEXT (never from an ISR) while
 * active, at whatever cadence the caller's own message loop already uses
 * (this module adds no periodic timer of its own). Checks both GPDMA2
 * channels' DTE/ULE/USE error flags; on either, treats it as fatal, calls
 * lfrfid_dma_tx_stop() itself, and returns false. Returns true while healthy
 * or already stopped (a no-op call when inactive is always true). */
bool lfrfid_dma_tx_check_health(void);

#ifdef __cplusplus
}
#endif

#endif /* LFRFID_DMA_TX_H_ */
