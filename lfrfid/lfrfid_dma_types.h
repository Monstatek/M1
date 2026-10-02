/* See COPYING.txt for license details. */

/*
 * lfrfid_dma_types.h
 *
 * The two plain-data waveform types shared between lfrfid_hal.h (the real
 * ARM emulation-buffer global, lfrfid_encoded_data, and every protocol
 * encoder that fills it) and lfrfid_dma_tx.h (the DMA transport that
 * consumes it). Deliberately HAL/RTOS-free -- stdint/stdbool only -- so the
 * transport's pure lfrfid_dma_tx_prepare() seam stays host-testable without
 * pulling in stm32h5xx_hal.h.
 */
#ifndef LFRFID_DMA_TYPES_H_
#define LFRFID_DMA_TYPES_H_

#include <stdint.h>

/* Max LF emulation steps held in the tx wave buffer.
 * H10301 (96-bit) needs <=1152; Pyramid (128-bit) needs <=1536 worst case
 * (all fc/8 bits: 128 * 6 cycles * 2 steps); HIDExt (192-bit) needs <=2304
 * worst case (192 * 6 * 2). PSK1 uses 32 steps per bit: Keri needs 2048
 * and NexWatch needs 3072. 4096 gives headroom for all. Heap-allocated
 * (lfrfid.c), so this only grows a malloc, not .bss. */
#define ENCODED_DATA_MAX 4096

typedef struct
{
    uint32_t bsrr;
    uint16_t time_us;
} Encoded_Data_t;

typedef struct
{
    Encoded_Data_t *data;
    uint16_t  length;
    uint16_t  index;
} EncodedTx_Data_t;

#endif /* LFRFID_DMA_TYPES_H_ */
