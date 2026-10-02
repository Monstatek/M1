/*============================================================================*/
/**
 * @file    m1_mfc_dma.h
 * @brief   MIFARE Classic {At} transparent-mode transmitter over an internal
 *          timer (TIM7) update event + two GPDMA1 streams:
 *            - GPDMA1 Ch6: bsrr[]  -> GPIOB->BSRR   (PB15 load-modulation level)
 *            - GPDMA1 Ch7: arr[]   -> TIM7->ARR     (per-edge period)
 *          STM32H573 implementation (GPDMA1, TIM7). Fractional edge timing with a
 *          remainder accumulator (m1_at_edges) -- no fixed half-slots. The
 *          ST25R3916 MCU_CLK is NOT used or routed; TIM7 is the internal source.
 *
 *          Ch6/Ch7 are chosen because Ch0=Sub-GHz Tx, Ch1=log Tx, Ch2=log Rx,
 *          Ch5=ESP32 are already claimed on this board; Ch6/Ch7 are free.
 *
 *          The SPI2 bus (PB13/14/15) is SHARED (ST25R3916 + Si4463); PB15 is
 *          SPI2_MOSI (AF5). The transparent-mode handoff disables SPI2 (SPE=0),
 *          re-drives PB15 as a very-high-speed GPIO output, holds NFC_CS (PC0)
 *          high, and blocks competing RF-SPI access for the whole 0xDC..0xD1
 *          window -- no ST25R register access or SPI command inside it.
 *
 * Guarded by M1_MFC_RAW_EMULATION.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_MFC_DMA_H
#define M1_MFC_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Completion diagnostics for one {At} transmit (all cycle stamps are DWT). */
typedef struct {
    uint16_t edges;       /* edge count from m1_at_edges_build              */
    uint32_t total_cyc;   /* generated waveform length, TIM7 cycles         */
    uint32_t timer_hz;    /* resolved TIM7 kernel clock used for encoding    */
    uint8_t  dma_ok;      /* 1 = both DMA streams reached Transfer-Complete  */
    uint32_t gpio_err;    /* HAL_DMA_GetError of the BSRR (GPIO) stream      */
    uint32_t arr_err;     /* HAL_DMA_GetError of the ARR (period) stream     */
    uint32_t rxe;         /* hardware RXE stamp (ISR), 0 if none             */
    uint8_t  rxe_valid;   /* 1 if an RXE was captured this session          */
    uint32_t entry;       /* tx entry                                       */
    uint32_t first_mod;   /* first modulation edge (edge 0 applied)          */
    uint32_t last_mod;    /* after both DMA TC / TIM7 stop                  */
    uint32_t dc;          /* 0xDC transparent-enter command cost            */

    /* Transparent-routing audit -- GPIO/SPI readbacks captured immediately
     * before TIM7 start (want: pb_moder=1 pb_otype=0 pb_pupd=0 pb_ospeed=3
     * pc_moder=1 pc_odr=1 spi_spe=0 pb_init_low=1). */
    uint8_t  pb_moder;    /* PB15 MODER field                               */
    uint8_t  pb_otype;    /* PB15 OTYPER bit                                */
    uint8_t  pb_pupd;     /* PB15 PUPDR field                               */
    uint8_t  pb_ospeed;   /* PB15 OSPEEDR field                             */
    uint8_t  pc_moder;    /* PC0 (NFC_CS) MODER field                       */
    uint8_t  pc_odr;      /* PC0 ODR bit (want 1 = deasserted)              */
    uint8_t  pc_idr;      /* PC0 IDR bit -- physical CS level (want 1)      */
    uint32_t post_dc_spi; /* ST25 SPI transactions during 0xDC..0xD1 (want 0)*/
    uint8_t  spi_spe;     /* SPI2 CR1.SPE (want 0 = disabled)               */
    uint8_t  pb_init_low; /* PB15 IDR before edge 0 (want 1 = read-low ok)  */
    uint8_t  final_pb15;  /* PB15 level read back after the waveform (want 0)*/

    /* Four-signal handoff audit (prodfix5). */
    uint8_t  sck_moder;   /* PB13/SCK  MODER (want 0 = input)               */
    uint8_t  sck_pupd;    /* PB13/SCK  PUPDR (want 1 = pull-up)             */
    uint8_t  miso_moder;  /* PB14/MISO MODER (want 0 = input)              */
    uint8_t  miso_pupd;   /* PB14/MISO PUPDR (want 1 = pull-up)            */
    uint8_t  bss_pupd;    /* PC0/BSS   PUPDR (want 1 = pull-up)            */
    uint8_t  restored;    /* 1 = restore register-verified (POSTAUTH-5)     */
    uint8_t  bss_abort;   /* 1 = aborted: PC0 IDR not physically high      */

    /* POSTAUTH-5: real D1 handoff timing + post-restore register verification. */
    uint32_t final_edge_cyc;  /* DWT at last waveform edge                  */
    uint32_t restore_cyc;     /* DWT after restore writes complete          */
    uint32_t d1_begin_cyc;    /* DWT just before checked D1 SPI transaction  */
    uint32_t d1_end_cyc;      /* DWT just after checked D1 SPI transaction   */
    uint8_t  d1_rc;           /* real HAL status of D1 (0 = HAL_OK)          */
    uint8_t  nss_moder;       /* PC0/NSS MODER post-restore (want 1 output) */
    uint8_t  nss_odr;         /* PC0/NSS ODR   post-restore (want 1 high)   */
    uint8_t  pb15_af;         /* PB15 MODER    post-restore (want 2 AF)     */
    uint8_t  sck_af;          /* PB13 MODER    post-restore (want 2 AF)     */
    uint8_t  miso_af;         /* PB14 MODER    post-restore (want 2 AF)     */

    /* POSTAUTH-7: receiver-state snapshot taken AFTER the first (exit) D1, plus
     * the conditional OP_CONTROL re-arm + CLEAR_FIFO + final (arm) D1. */
    uint8_t  p7_op;           /* OP_CONTROL (0x02) after first D1           */
    uint8_t  p7_mode;         /* MODE (0x03) after first D1                 */
    uint8_t  p7_aux;          /* AUX_DISPLAY (0x31) after first D1          */
    uint8_t  p7_pt;           /* PASSIVE_TARGET_STATUS (0x21) after first D1 */
    uint8_t  p7_op_changed;   /* 1 = we rewrote OP_CONTROL (EN|RX_EN were   */
                              /*     missing); other bits preserved         */
    uint8_t  p7_op_after;     /* OP_CONTROL value we wrote (== p7_op if none)*/
    uint8_t  p7_db_rc;        /* real HAL status of checked CLEAR_FIFO (0xDB)*/
    uint8_t  p7_final_d1_rc;  /* real HAL status of the final (arm) D1       */
    uint32_t p7_final_d1_beg; /* DWT just before the final D1               */
    uint32_t p7_final_d1_end; /* DWT just after the final D1                */
    uint8_t  p7_valid;        /* 1 = final-edge -> final-D1 < 50us          */

    /* ST25R3916 register snapshot (captured pre-0xDC in normal mode). */
    uint8_t  reg_op;      /* OP_CONTROL   (0x02) */
    uint8_t  reg_mode;    /* MODE         (0x03) */
    uint8_t  reg_pt;      /* PASSIVE_TARGET (0x08) */
    uint8_t  reg_auxm;    /* AUX_MOD      (B/0x28) */
    uint8_t  reg_ptm;     /* PT_MOD       (0x29) */
    uint8_t  reg_bitrate; /* BIT_RATE     (0x04) */
    uint8_t  reg_iso;     /* ISO14443A_NFC(0x05) */
    uint8_t  reg_valid;   /* 1 if the snapshot has been captured            */
} m1_dma_result_t;

/* Reset the waveform cache; one-time GPDMA1/TIM7 configuration is done lazily. */
void m1_mfc_dma_init(void);

/* Snapshot the ST25R3916 analog/mode registers for the routing audit. MUST be
 * called in NORMAL mode (SPI active), e.g. on a cache miss before any transmit. */
void m1_mfc_dma_snapshot(void);

/* Pre-build (and cache) the {At} edge lists WITHOUT transmitting, so the encode
 * cost is out of the FDT-critical window. Call on a cache miss before the next
 * (cache-hit) transmit. Returns the edge count, or 0 on failure. */
uint16_t m1_mfc_dma_prime(const uint8_t at[4], const uint8_t par[4]);

/* N-byte generalization (POSTAUTH-1): pre-build/transmit an arbitrary payload of
 * `nbytes` (1..18) data bytes each with its custom parity bit. The 4-byte {At}
 * entries above are thin wrappers over these and produce identical output. */
uint16_t m1_mfc_dma_prime_frame(const uint8_t *data, const uint8_t *par, uint8_t nbytes);
uint32_t m1_mfc_dma_tx_frame(const uint8_t *data, const uint8_t *par, uint8_t nbytes,
                             uint32_t rxe_ref, uint32_t target_delay_cyc,
                             m1_dma_result_t *r);

/* Transmit the cached {At} waveform (must already be primed / unchanged).
 * rxe_ref: the caller-validated end-of-receive reference (DWT cycles) for THIS
 * {Nr}{Ar} frame -- passed in (not read from the ISR global) so a stale/missing
 * hardware RXE cannot skew alignment. target_delay_cyc: if non-zero, spin until
 * (DWT->CYCCNT - rxe_ref) reaches it before emitting edge 0 -- the FDT alignment
 * knob (0 = no wait, natural minimum). The 0xDC..0xD1 window runs with the RTOS
 * scheduler suspended and NO ST25R/SPI access; NOTHING in the timed window logs
 * or runs crypto. Fills *r. Returns first_mod - rxe_ref cycles. */

#ifdef __cplusplus
}
#endif

#endif /* M1_MFC_DMA_H */
