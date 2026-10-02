/* See COPYING.txt for license details. */

/*
 * lfrfid_hal.h
 */

#ifndef LFRFID_HAL_H_
#define LFRFID_HAL_H_

void rfid_read_handler(TIM_HandleTypeDef *htim);
/* rfid_emul_handler() (the old per-edge ISR emulate handler) is removed --
 * see lfrfid_dma_tx.c. LF emulation is now GPDMA2-driven with no CPU/ISR
 * participation in steady state. */

/* carrier_hz: exciter field frequency. duty_pct: exciter PWM high-time
 * percentage (1-99); pass 50 for the original ASK behavior (routes to the
 * untouched lfrfid_RFIDOut_Init, bit-identical to the pre-PSK-mode-arch
 * carrier). Any other value routes to lfrfid_RFIDOut_Init_ex(). */
void lfrfid_read_hw_init(uint32_t carrier_hz, uint8_t duty_pct);
void lfrfid_read_hw_deinit(void);

void lfrfid_emul_hw_init(void);
void lfrfid_emul_hw_deinit(void);

/* Original, UNTOUCHED entry point -- fixed 50% duty. Kept exactly as-is
 * (not reimplemented on top of _ex) so every existing ASK/Pet-Tag caller's
 * timing is bit-for-bit unchanged. */
void lfrfid_RFIDOut_Init(uint32_t freq);
/* Explicit-duty variant for the PSK acquisition pass (62.5kHz/25%) -- see lfrfid_hal.c for the
 * bounds/rounding derivation. duty_pct clamped to [1,99]. */
void lfrfid_RFIDOut_Init_ex(uint32_t freq, uint8_t duty_pct);
void lfrfid_RFIDIn_Init(void);

/* Runtime-selected upper bound for the shared LF edge acquisition filter
 * (rfid_read_handler's ccr<7||ccr>max check). Set per acquisition pass --
 * ASK pass uses LFRFID_HAL_ASK_EDGE_MAX_US (the original, untouched 1000),
 * PSK pass uses LFRFID_HAL_PSK_EDGE_MAX_US. Never applied globally. */
#define LFRFID_HAL_ASK_EDGE_MAX_US   (1000U)
#define LFRFID_HAL_PSK_EDGE_MAX_US   (28000U)
void lfrfid_hal_set_edge_max_us(uint16_t max_us);

extern TIM_HandleTypeDef   Timerhdl_RfIdTIM5;
extern TIM_HandleTypeDef   Timerhdl_RfIdTIM3;

extern uint8_t rfid_rxtx_is_taking_this_irq;
extern void lfrfid_isr_init(void);

//***************************************************************************
// emulation
//***************************************************************************
/* Encoded_Data_t / EncodedTx_Data_t / ENCODED_DATA_MAX now live in
 * lfrfid_dma_types.h (HAL-free), shared with lfrfid_dma_tx.h's host-testable
 * transport-preparation seam. */
#include "lfrfid_dma_types.h"

extern EncodedTx_Data_t lfrfid_encoded_data;

#endif /* LFRFID_HAL_H_ */
