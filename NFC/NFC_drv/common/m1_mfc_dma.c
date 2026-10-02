/*============================================================================*/
/**
 * @file    m1_mfc_dma.c
 * @brief   TIM7 update-event + dual-GPDMA1 {At} transparent-mode transmitter
 *          with a full SPI2->PB15 bus handoff and routing audit. See header.
 *          Guarded by M1_MFC_RAW_EMULATION.
 *
 * STM32H573 transport: TIM7_UP -> GPDMA1_Channel6 bsrr[] -> GPIOB->BSRR
 * and GPDMA1_Channel7 arr[] -> TIM7->ARR. Ch0/1/2/5 are already used on this
 * board (Sub-GHz Tx, log Tx/Rx, ESP32); Ch6/Ch7 are free. Durations come from
 * m1_at_edges (fractional, remainder accumulator) -- NO fixed half-slots and
 * NO ST25R3916 MCU_CLK.
 *
 * Transparent-mode bus handoff (SPI2 = PB13/14/15 shared ST25R3916 + Si4463):
 *   pre-0xDC (normal mode, on a cache miss): snapshot ST25R analog/mode regs.
 *   0xDC .. 0xD1 window runs with the RTOS scheduler suspended AND the ST25R
 *   EXTI IRQ masked, so no competing SPI2 access can cancel transparent mode;
 *   it contains NO ST25R register access:
 *     - 0xDC transparent-enter (SPI still active)
 *     - SPI2 SPE=0; four-signal handoff: SCK(PB13)/MISO(PB14) ->
 *       input,pull-up,low; BSS(PC0) -> input,pull-up,low (must stay physically
 *       high, IDR-gated); MOSI(PB15) -> preload low, output PP, no-pull, VH
 *     - GPIO/SPI readbacks captured; TIM7 + dual-GPDMA emit the waveform
 *     - MOSI low; restore all four signals + SPI2; 0xD1 UNMASK as first command
 */
/*============================================================================*/
#include "m1_mfc_dma.h"

#if defined(M1_MFC_RAW_EMULATION)

#include "m1_at_edges.h"
#include "stm32h5xx_hal.h"   /* HAL DMA + RCC clock macros / getters          */
#include "stm32h5xx.h"       /* GPIOB, GPIOC, TIM7, SPI2, DWT, RCC            */
#include "st25r3916.h"       /* ST25R3916_CMD_TRANSPARENT_MODE / _UNMASK_...   */
#include "st25r3916_com.h"   /* st25r3916ExecuteCommand / ReadRegister + regs  */
#include "rfal_platform.h"   /* platformProtect/UnprotectST25RComm (EXTI mask) */
#include "FreeRTOS.h"        /* vTaskSuspendAll / xTaskResumeAll               */
#include "task.h"
#include <string.h>

/* PB15 = SPI2_MOSI (AF5); transparent mode routes MOSI to load modulation. */
#define DMA_PB15_HI    (1u << 15)          /* GPIOB->BSRR: set   PB15 high  */
#define DMA_PB15_LO    (1u << (15 + 16))   /* GPIOB->BSRR: reset PB15 low   */
#define PB15_MODER_M   (3u << 30)          /* MODER  field for pin 15       */
#define PB15_MODER_OUT (1u << 30)          /* general-purpose output (01)   */
#define PB15_OSPEED_VH (3u << 30)          /* OSPEEDR very-high-speed (11)  */
#define PB15_PUPD_M    (3u << 30)          /* PUPDR   field for pin 15      */
#define PB15_OSPD_M    (3u << 30)          /* OSPEEDR field for pin 15      */
#define PC0_MODER_M    (3u << 0)           /* MODER  field for PC0          */
#define PC0_MODER_OUT  (1u << 0)           /* PC0 output (01)               */
#define PC0_PUPD_M     (3u << 0)           /* PUPDR   field for PC0         */
#define PC0_PUPD_UP    (1u << 0)           /* PC0 pull-up (01)              */
#define PC0_OSPD_M     (3u << 0)           /* OSPEEDR field for PC0         */
/* SCK=PB13, MISO=PB14 -> input, pull-up, low speed during transparent mode. */
#define PB13_MODER_M   (3u << 26)
#define PB14_MODER_M   (3u << 28)
#define PB13_PUPD_M    (3u << 26)
#define PB14_PUPD_M    (3u << 28)
#define PB13_PUPD_UP   (1u << 26)
#define PB14_PUPD_UP   (1u << 28)
#define PB13_OSPD_M    (3u << 26)
#define PB14_OSPD_M    (3u << 28)

/* Max payload = 18 bytes (16 block + CRC_A) => SOF + 18*9 = 163 bits; worst case
 * 9 edges/bit => <=1467. {At} = 4 bytes uses only 318. 1536 bounds both. */
#define DMA_MAX_BYTES 18u
#define DMA_MAX_EDGES 1536u
#define DMA_POLL_MS   10u                 /* generous vs the ~0.35 ms waveform */

/* Word-aligned DMA source buffers in SRAM (GPDMA-accessible; DCACHE off). */
/* RF end-of-receive latch: written by the ST25R3916 RX ISR
 * (st25r3916CheckForReceivedInterrupts) and read by this {At}/read DMA
 * transmit path for FDT alignment. These are part of the authoritative
 * Flow-A RAWOWN transport; they were extracted here (a live Flow-A file)
 * when the former bit-bang {At} transport (m1_mfc_bb.c) that used to host
 * their definition was removed. The externs live in the ISR/listener TUs. */
volatile uint32_t g_m1_rxe_cyc  = 0U;
volatile uint8_t  g_m1_rxe_seen = 0U;

static uint32_t s_arr[DMA_MAX_EDGES];     /* -> TIM7->ARR   (period - 1)       */
static uint32_t s_bsrr[DMA_MAX_EDGES];    /* -> GPIOB->BSRR (PB15 hi/lo)        */
static uint16_t s_nedges   = 0U;
static uint32_t s_total_cyc = 0U;
static uint8_t  s_wave_valid = 0U;
static uint8_t  s_cached_at[DMA_MAX_BYTES]  = {0U};
static uint8_t  s_cached_par[DMA_MAX_BYTES] = {0U};
static uint8_t  s_cached_n   = 0U;         /* payload length of the cached wave  */
static uint8_t  s_have_cache = 0U;

static DMA_HandleTypeDef s_hdma_bsrr;      /* GPDMA1_Channel6 */
static DMA_HandleTypeDef s_hdma_arr;       /* GPDMA1_Channel7 */
static uint8_t           s_hw_ready = 0U;  /* one-time peripheral config done  */
static uint32_t          s_timer_hz = 0U;

/* ST25R3916 register snapshot (captured in normal mode via m1_mfc_dma_snapshot). */
static uint8_t s_snap[7]   = {0};
static uint8_t s_snap_valid = 0U;

/* Written by the ST25R3916 ISR (st25r3916_irq.c) at every end-of-receive. */
extern volatile uint32_t g_m1_rxe_cyc;
extern volatile uint8_t  g_m1_rxe_seen;

/* POSTAUTH-6: checked D1 primitive (m1_rf_spi.c) returning the real HAL status. */
extern HAL_StatusTypeDef m1_nfc_spi_checked_cmd(uint8_t cmd);

/* Transparent-window SPI audit (read by st25r3916_com.c's comStart). tm_active
 * is 1 only between 0xDC and 0xD1; post_dc_spi counts any ST25 SPI in that
 * window -- must stay 0 or transparent mode is cancelled. */
volatile uint8_t  g_m1_tm_active   = 0U;
volatile uint32_t g_m1_post_dc_spi = 0U;

/* Resolve the TIM7 kernel clock: APB1 timer clock is PCLK1 when the APB1
 * prescaler is 1, else 2*PCLK1 (RM0481). Logged for on-device confirmation. */
static uint32_t dma_tim7_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t ppre1 = (RCC->CFGR2 & RCC_CFGR2_PPRE1) >> RCC_CFGR2_PPRE1_Pos;
    return (ppre1 < 4U) ? pclk1 : (pclk1 * 2U);   /* field >=4 => divider > 1 */
}

static void dma_config_channel(DMA_HandleTypeDef *h, DMA_Channel_TypeDef *inst)
{
    h->Instance                 = inst;
    h->Init.Request             = GPDMA1_REQUEST_TIM7_UP;
    h->Init.BlkHWRequest        = DMA_BREQ_SINGLE_BURST;   /* 1 word / update  */
    h->Init.Direction           = DMA_MEMORY_TO_PERIPH;
    h->Init.SrcInc              = DMA_SINC_INCREMENTED;     /* walk the buffer  */
    h->Init.DestInc             = DMA_DINC_FIXED;           /* fixed register   */
    h->Init.SrcDataWidth        = DMA_SRC_DATAWIDTH_WORD;
    h->Init.DestDataWidth       = DMA_DEST_DATAWIDTH_WORD;
    h->Init.Priority            = DMA_HIGH_PRIORITY;
    h->Init.SrcBurstLength      = 1U;
    h->Init.DestBurstLength     = 1U;
    h->Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    h->Init.TransferEventMode   = DMA_TCEM_BLOCK_TRANSFER;  /* TC at block end  */
    h->Init.Mode                = DMA_NORMAL;
    (void)HAL_DMA_Init(h);
}

static void dma_hw_setup(void)
{
    __HAL_RCC_GPDMA1_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();

    /* TIM7 basic timer: up-count, PSC=0, ARPE OFF so a DMA write to ARR takes
     * effect on the very next period, UDE for the DMA request. */
    TIM7->CR1  = 0U;
    TIM7->PSC  = 0U;
    TIM7->ARR  = 0xFFFFU;
    TIM7->CNT  = 0U;
    TIM7->DIER = TIM_DIER_UDE;

    dma_config_channel(&s_hdma_bsrr, GPDMA1_Channel6);
    dma_config_channel(&s_hdma_arr,  GPDMA1_Channel7);

    s_timer_hz = dma_tim7_hz();
    s_hw_ready = 1U;
}

static void dma_build_wave(const uint8_t *data, const uint8_t *par, uint8_t nbytes)
{
    if (nbytes > DMA_MAX_BYTES) { s_wave_valid = 0U; return; }
    if (s_timer_hz == 0U) { s_timer_hz = dma_tim7_hz(); }
    uint32_t tot = 0U;
    size_t   n   = m1_at_edges_build(data, par, nbytes, s_arr, s_bsrr,
                                     DMA_MAX_EDGES, s_timer_hz, &tot);
    if ((n < 2U) || (n > DMA_MAX_EDGES)) { s_wave_valid = 0U; return; }

    s_nedges     = (uint16_t)n;
    s_total_cyc  = tot;
    s_wave_valid = 1U;
    (void)memcpy(s_cached_at,  data, nbytes);
    (void)memcpy(s_cached_par, par,  nbytes);
    s_cached_n   = nbytes;
    s_have_cache = 1U;
}

void m1_mfc_dma_init(void)
{
    s_wave_valid = 0U;
    s_have_cache = 0U;
    s_snap_valid = 0U;
    if (!s_hw_ready) { dma_hw_setup(); }
}

void m1_mfc_dma_snapshot(void)
{
    /* NORMAL mode only (SPI active). st25r3916ReadRegister handles space-B. */
    static const uint8_t regs[7] = {
        ST25R3916_REG_OP_CONTROL,     /* 0x02 */
        ST25R3916_REG_MODE,           /* 0x03 */
        ST25R3916_REG_PASSIVE_TARGET, /* 0x08 */
        ST25R3916_REG_AUX_MOD,        /* B/0x28 */
        ST25R3916_REG_PT_MOD,         /* 0x29 */
        ST25R3916_REG_BIT_RATE,       /* 0x04 */
        ST25R3916_REG_ISO14443A_NFC,  /* 0x05 */
    };
    for (uint8_t i = 0U; i < 7U; i++) {
        uint8_t v = 0U;
        (void)st25r3916ReadRegister(regs[i], &v);
        s_snap[i] = v;
    }
    s_snap_valid = 1U;
}

/* Cache-aware (re)build for an N-byte payload. */
static void dma_ensure_wave(const uint8_t *data, const uint8_t *par, uint8_t nbytes)
{
    if ((s_have_cache == 0U) || (s_cached_n != nbytes) ||
        (memcmp(data, s_cached_at,  nbytes) != 0) ||
        (memcmp(par,  s_cached_par, nbytes) != 0)) {
        dma_build_wave(data, par, nbytes);
    }
}

uint16_t m1_mfc_dma_prime_frame(const uint8_t *data, const uint8_t *par, uint8_t nbytes)
{
    if ((data == NULL) || (par == NULL) || (nbytes == 0U) || (nbytes > DMA_MAX_BYTES)) { return 0U; }
    if (!s_hw_ready) { dma_hw_setup(); }
    dma_ensure_wave(data, par, nbytes);
    return s_wave_valid ? s_nedges : 0U;
}

uint16_t m1_mfc_dma_prime(const uint8_t at[4], const uint8_t par[4])
{
    return m1_mfc_dma_prime_frame(at, par, 4U);
}

uint32_t m1_mfc_dma_tx_frame(const uint8_t *data, const uint8_t *par, uint8_t nbytes,
                             uint32_t rxe_ref, uint32_t target_delay_cyc,
                             m1_dma_result_t *r)
{
    const uint8_t *at = data;               /* payload alias (N bytes)         */
    uint32_t rxe = rxe_ref;                 /* caller-validated RXE reference  */
    if (r != NULL) {
        (void)memset(r, 0, sizeof(*r));
        r->entry     = DWT->CYCCNT;
        r->rxe       = rxe;
        r->rxe_valid = g_m1_rxe_seen;
        r->timer_hz  = s_timer_hz;
        if (s_snap_valid) {
            r->reg_op = s_snap[0]; r->reg_mode = s_snap[1]; r->reg_pt = s_snap[2];
            r->reg_auxm = s_snap[3]; r->reg_ptm = s_snap[4];
            r->reg_bitrate = s_snap[5]; r->reg_iso = s_snap[6];
            r->reg_valid = 1U;
        }
    }
    if ((at == NULL) || (par == NULL) || (nbytes == 0U) || (nbytes > DMA_MAX_BYTES)) { return 0U; }
    if (!s_hw_ready) { dma_hw_setup(); }

    /* Normally primed already; build here only as a fallback (adds latency). */
    dma_ensure_wave(at, par, nbytes);
    if (s_wave_valid == 0U) { return 0U; }
    if (r != NULL) { r->edges = s_nedges; r->total_cyc = s_total_cyc;
                     r->timer_hz = s_timer_hz; }

    /* Save the bus/pin state we will disturb, so we can restore it exactly.
     * Whole-register snapshots cover PB13/PB14/PB15 and PC0 losslessly. */
    uint32_t spi_cr1  = SPI2->CR1;
    uint32_t pb_moder = GPIOB->MODER;
    uint32_t pb_otype = GPIOB->OTYPER;
    uint32_t pb_pupd  = GPIOB->PUPDR;
    uint32_t pb_ospd  = GPIOB->OSPEEDR;
    uint32_t pc_moder = GPIOC->MODER;
    uint32_t pc_otype = GPIOC->OTYPER;
    uint32_t pc_pupd  = GPIOC->PUPDR;
    uint32_t pc_ospd  = GPIOC->OSPEEDR;

    /* ---- Protected transparent-mode window: no competing RF-SPI task, AND the
     * ST25R3916 EXTI IRQ held disabled for the WHOLE window. This is critical:
     * ANY SPI access after 0xDC cancels transparent mode, and the ST25R IRQ ISR
     * (st25r3916Isr -> st25r3916CheckForReceivedInterrupts) does exactly that --
     * st25r3916ExecuteCommand(0xDC) re-enables IRQ_ST25R_EXTI_IRQn on the way out
     * (via comStop's platformUnprotectST25RComm), so without holding it disabled
     * the ISR fires during the ~350us waveform and kills the modulation.
     * platformProtect/Unprotect nest with the internal 0xDC/0xD1 protect calls. */
    vTaskSuspendAll();
    platformProtectST25RComm();

    /* 1) Enter transparent mode -- SPI still owns PB15 for this command. */
    uint32_t t_a = DWT->CYCCNT;
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_TRANSPARENT_MODE);
    uint32_t t_b = DWT->CYCCNT;
    /* From here until 0xD1, NO ST25 SPI may occur (it would cancel transparent
     * mode). Arm the audit counter; comStart increments it on any violation. */
    g_m1_post_dc_spi = 0U;
    g_m1_tm_active   = 1U;

    /* 2) Transparent-mode pin handoff for all four NFC SPI signals,
     *    implemented with STM32H5 GPIO registers and no
     *    generic m1_spi_hal_deinit which would tear down shared SPI2/NSS/mutex):
     *      SCK  PB13 -> input, pull-up, low speed
     *      MISO PB14 -> input, pull-up, low speed
     *      BSS  PC0  -> input, pull-up, low speed (must stay physically high)
     *      MOSI PB15 -> preload low, output push-pull, no-pull, very-high-speed
     *    Glitch-free order: preload output levels BEFORE any mode change; enable
     *    pull-ups before switching a pin to input; MODER written last. */
    SPI2->CR1 = spi_cr1 & ~(uint32_t)SPI_CR1_SPE;           /* SPE = 0          */

    GPIOB->BSRR    = DMA_PB15_LO;                           /* MOSI low first   */
    GPIOB->PUPDR   = (pb_pupd & ~(uint32_t)(PB13_PUPD_M | PB14_PUPD_M | PB15_PUPD_M))
                   | (uint32_t)(PB13_PUPD_UP | PB14_PUPD_UP);  /* SCK/MISO up, MOSI none */
    GPIOB->OSPEEDR = (pb_ospd & ~(uint32_t)(PB13_OSPD_M | PB14_OSPD_M | PB15_OSPD_M))
                   | (uint32_t)PB15_OSPEED_VH;              /* SCK/MISO low, MOSI VH */
    GPIOB->OTYPER &= ~(1u << 15);                           /* MOSI push-pull   */
    GPIOB->MODER   = (pb_moder & ~(uint32_t)(PB13_MODER_M | PB14_MODER_M | PB15_MODER_M))
                   | (uint32_t)PB15_MODER_OUT;              /* SCK/MISO in, MOSI out */

    /* BSS/CS PC0: keep high while switching to input pull-up (pull-up armed
     * before the mode change so the line never dips). */
    GPIOC->BSRR    = (1u << 0);                             /* PC0 ODR = 1      */
    GPIOC->PUPDR   = (pc_pupd & ~(uint32_t)PC0_PUPD_M) | (uint32_t)PC0_PUPD_UP;
    GPIOC->OSPEEDR = pc_ospd & ~(uint32_t)PC0_OSPD_M;       /* low speed        */
    GPIOC->MODER   = pc_moder & ~(uint32_t)PC0_MODER_M;     /* input            */

    /* 3) BSS physical-high gate: if PC0 IDR is not high, abort the waveform
     *    safely (restore + 0xD1 + log only after restoration). */
    uint8_t rb_bss_idr = (uint8_t)((GPIOC->IDR >> 0) & 1U);
    int aborted = (rb_bss_idr == 0U) ? 1 : 0;

    /* 4) Capture handoff readbacks (valid whether or not we transmit). */
    uint8_t rb_sck_moder  = (uint8_t)((GPIOB->MODER   >> 26) & 3U);
    uint8_t rb_sck_pupd   = (uint8_t)((GPIOB->PUPDR   >> 26) & 3U);
    uint8_t rb_miso_moder = (uint8_t)((GPIOB->MODER   >> 28) & 3U);
    uint8_t rb_miso_pupd  = (uint8_t)((GPIOB->PUPDR   >> 28) & 3U);
    uint8_t rb_pb_moder   = (uint8_t)((GPIOB->MODER   >> 30) & 3U);
    uint8_t rb_pb_otype   = (uint8_t)((GPIOB->OTYPER  >> 15) & 1U);
    uint8_t rb_pb_pupd    = (uint8_t)((GPIOB->PUPDR   >> 30) & 3U);
    uint8_t rb_pb_ospeed  = (uint8_t)((GPIOB->OSPEEDR >> 30) & 3U);
    uint8_t rb_pc_moder   = (uint8_t)((GPIOC->MODER   >>  0) & 3U);
    uint8_t rb_pc_pupd    = (uint8_t)((GPIOC->PUPDR   >>  0) & 3U);
    uint8_t rb_pc_odr     = (uint8_t)((GPIOC->ODR     >>  0) & 1U);
    uint8_t rb_spi_spe    = (uint8_t)((SPI2->CR1 & SPI_CR1_SPE) ? 1U : 0U);
    uint8_t rb_pb_low     = (uint8_t)(((GPIOB->IDR >> 15) & 1U) == 0U ? 1U : 0U);

    HAL_StatusTypeDef sa = HAL_OK, sb = HAL_OK, pa = HAL_OK, pb = HAL_OK;
    uint32_t first_mod = 0U, last_mod = 0U;
    uint8_t  rb_final = 1U;

    if (!aborted) {
        /* 5) Prime TIM7 period 0 and arm both DMA streams over edges [1..N-1]
         *    BEFORE the alignment spin, so the fixed setup is absorbed by the
         *    spin and edge 0 fires precisely at the target. */
        TIM7->CR1 &= ~(uint32_t)TIM_CR1_CEN;
        TIM7->CNT  = 0U;
        TIM7->ARR  = s_arr[0];
        uint32_t words = (uint32_t)(s_nedges - 1U);
        sb = HAL_DMA_Start(&s_hdma_bsrr, (uint32_t)&s_bsrr[1],
                           (uint32_t)&GPIOB->BSRR, words * 4U);
        sa = HAL_DMA_Start(&s_hdma_arr,  (uint32_t)&s_arr[1],
                           (uint32_t)&TIM7->ARR,  words * 4U);

        /* 6) FDT alignment: optionally spin until the target delay past RXE. */
        if (target_delay_cyc != 0U) {
            while ((uint32_t)(DWT->CYCCNT - rxe) < target_delay_cyc) { /* align */ }
        }

        /* 7) FIRE: edge 0 (first modulation transition), then release timer. */
        first_mod = DWT->CYCCNT;
        GPIOB->BSRR = s_bsrr[0];
        TIM7->CR1 |= (uint32_t)TIM_CR1_CEN;

        /* 8) Wait for both streams to reach block Transfer-Complete. */
        pa = HAL_DMA_PollForTransfer(&s_hdma_arr,  HAL_DMA_FULL_TRANSFER, DMA_POLL_MS);
        pb = HAL_DMA_PollForTransfer(&s_hdma_bsrr, HAL_DMA_FULL_TRANSFER, DMA_POLL_MS);
        last_mod = DWT->CYCCNT;
        TIM7->CR1 &= ~(uint32_t)TIM_CR1_CEN;
    }

    /* 9) Force MOSI low, then restore all four signals + SPI2 glitch-free.
     *    PC0 driven high before it returns to output; MOSI stays low. UNMASK
     *    (0xD1) is the FIRST ST25 SPI transaction after 0xDC. */
    GPIOB->BSRR    = DMA_PB15_LO;                           /* MOSI low         */
    rb_final       = (uint8_t)((GPIOB->IDR >> 15) & 1U);
    GPIOC->BSRR    = (1u << 0);                             /* PC0 high first   */
    GPIOB->OSPEEDR = pb_ospd;  GPIOB->PUPDR = pb_pupd;
    GPIOB->OTYPER  = pb_otype; GPIOB->MODER = pb_moder;     /* PB13/14/15 back to AF */
    GPIOC->OSPEEDR = pc_ospd;  GPIOC->PUPDR = pc_pupd;
    GPIOC->OTYPER  = pc_otype; GPIOC->MODER = pc_moder;     /* PC0 back to output */
    SPI2->CR1      = spi_cr1;                               /* restore SPI2     */
    g_m1_tm_active = 0U;                                    /* end no-SPI window */

    /* POSTAUTH-5: VERIFY the restore via MCU registers (no SPI bus access) BEFORE
     * D1, then issue D1 as the first/only ST25 SPI transaction, capturing real
     * timing. `restored` is register-verified here -- never hardcoded. NSS(PC0)
     * must be push-pull output driven high; PB13/14/15 must be AF (mode 2). SPE
     * may legitimately be 0 between HAL blocking transfers, so it is NOT gated. */
    uint32_t v_final_cyc   = last_mod;
    uint32_t v_restore_cyc = DWT->CYCCNT;   /* restore writes complete             */
    uint8_t  v_pc0_md  = (uint8_t)((GPIOC->MODER  >> 0) & 3U);   /* want 1 output   */
    uint8_t  v_pc0_odr = (uint8_t)((GPIOC->ODR    >> 0) & 1U);   /* want 1 high     */
    uint8_t  v_pc0_ot  = (uint8_t)((GPIOC->OTYPER >> 0) & 1U);   /* want 0 push-pull*/
    uint8_t  v_pb13_af = (uint8_t)((GPIOB->MODER  >> 26) & 3U);  /* want 2 AF       */
    uint8_t  v_pb14_af = (uint8_t)((GPIOB->MODER  >> 28) & 3U);  /* want 2 AF       */
    uint8_t  v_pb15_af = (uint8_t)((GPIOB->MODER  >> 30) & 3U);  /* want 2 AF       */
    uint8_t  v_restored = (uint8_t)((v_pc0_md == 1U) && (v_pc0_odr == 1U) &&
                                    (v_pc0_ot == 0U) && (v_pb13_af == 2U) &&
                                    (v_pb14_af == 2U) && (v_pb15_af == 2U));
    /* D1 as the FIRST/ONLY ST25 SPI after {At}, via the CHECKED primitive that
     * returns the real HAL status (0xD1 already carries the direct-command bits). */
    uint32_t v_d1_begin = DWT->CYCCNT;
    HAL_StatusTypeDef v_d1_rc = m1_nfc_spi_checked_cmd((uint8_t)ST25R3916_CMD_UNMASK_RECEIVE_DATA);
    uint32_t v_d1_end   = DWT->CYCCNT;

    /* POSTAUTH-7: transparent mode has now been left (the first D1 above is the
     * exit). Normal register SPI is valid again, still under the SAME protection
     * hold (EXTI off, scheduler suspended) so nothing preempts the re-arm and the
     * final-edge -> final-D1 window stays deterministic. Snapshot the receiver
     * state, and if EN|RX_EN were dropped, restore ONLY those bits. Then CLEAR_FIFO
     * + a final D1 as the receiver-arm operation -- all checked. */
    /* POSTAUTH-8 (finalized -- hardened NO_READ_NO_DELAY): for the {At}
     * auth-response handoff (nbytes == 4) the re-arm is exit-D1 (above) ->
     * CLEAR_FIFO -> final-D1 with NOTHING between -- UNCHANGED, proven on
     * hardware (POSTAUTH-8, 3/3/3), never touched by this candidate.
     *
     * NEXTREAD-1: for the 18-byte block-response handoff (nbytes != 4), the
     * re-arm is now exit-D1 ONLY. The TX exit sequence is:
     * TRANSPARENT_MODE -> gpio deinit -> signal -> MOSI low -> gpio init ->
     * exactly one UNMASK_RECEIVE_DATA. No CLEAR_FIFO, no second D1, for any
     * frame length, in that reference. The exit-D1 issued above IS that one
     * command; nothing further runs for nbytes != 4. */
    uint8_t  v_p7_opmode[2] = {0U, 0U};   /* [0]=OP_CONTROL(0x02) [1]=MODE(0x03) */
    uint8_t  v_p7_aux = 0U, v_p7_pt = 0U;
    uint8_t  v_p7_op = 0U, v_p7_op_want = 0U, v_p7_changed = 0U;
    HAL_StatusTypeDef v_p7_db_rc  = HAL_OK;
    uint32_t          v_p7_d1_beg = v_d1_end;
    HAL_StatusTypeDef v_p7_d1_rc  = v_d1_rc;
    uint32_t          v_p7_d1_end = v_d1_end;
    if (nbytes == 4U) {
        v_p7_db_rc  = m1_nfc_spi_checked_cmd((uint8_t)ST25R3916_CMD_CLEAR_FIFO);
        v_p7_d1_beg = DWT->CYCCNT;
        v_p7_d1_rc  = m1_nfc_spi_checked_cmd((uint8_t)ST25R3916_CMD_UNMASK_RECEIVE_DATA);
        v_p7_d1_end = DWT->CYCCNT;
    }

    platformUnprotectST25RComm();            /* re-arm the ST25R EXTI IRQ      */
    (void)xTaskResumeAll();
    /* ---- end protected window ------------------------------------------- */

    if (r != NULL) { r->dc = (uint32_t)(t_b - t_a); }

    /* On the normal path both streams completed and are READY -- leave them.
     * Only if a stream failed to complete (or we aborted) do we re-init. */
    if (aborted || (pa != HAL_OK) || (pb != HAL_OK)) {
        (void)HAL_DMA_Abort(&s_hdma_arr);
        (void)HAL_DMA_Abort(&s_hdma_bsrr);
        dma_config_channel(&s_hdma_arr,  GPDMA1_Channel7);
        dma_config_channel(&s_hdma_bsrr, GPDMA1_Channel6);
    }

    if (r != NULL) {
        r->first_mod  = first_mod;
        r->last_mod   = last_mod;
        r->dma_ok     = (uint8_t)((aborted == 0) && (sa == HAL_OK) && (sb == HAL_OK) &&
                                  (pa == HAL_OK) && (pb == HAL_OK));
        r->gpio_err   = HAL_DMA_GetError(&s_hdma_bsrr);
        r->arr_err    = HAL_DMA_GetError(&s_hdma_arr);
        r->pb_moder   = rb_pb_moder;  r->pb_otype = rb_pb_otype;
        r->pb_pupd    = rb_pb_pupd;   r->pb_ospeed = rb_pb_ospeed;
        r->pc_moder   = rb_pc_moder;  r->pc_odr = rb_pc_odr;
        r->pc_idr     = rb_bss_idr;
        r->spi_spe    = rb_spi_spe;   r->pb_init_low = rb_pb_low;
        r->final_pb15 = rb_final;
        r->post_dc_spi = g_m1_post_dc_spi;
        r->sck_moder  = rb_sck_moder;  r->sck_pupd  = rb_sck_pupd;
        r->miso_moder = rb_miso_moder; r->miso_pupd = rb_miso_pupd;
        r->bss_pupd   = rb_pc_pupd;    r->restored  = v_restored;   /* register-verified */
        r->bss_abort  = (uint8_t)aborted;
        r->final_edge_cyc = v_final_cyc;
        r->restore_cyc    = v_restore_cyc;
        r->d1_begin_cyc   = v_d1_begin;
        r->d1_end_cyc     = v_d1_end;
        r->d1_rc          = (uint8_t)v_d1_rc;   /* real HAL status (0 = HAL_OK)     */
        r->nss_moder      = v_pc0_md;
        r->nss_odr        = v_pc0_odr;
        r->pb15_af        = v_pb15_af;
        r->sck_af         = (uint8_t)((GPIOB->MODER >> 26) & 3U);
        r->miso_af        = (uint8_t)((GPIOB->MODER >> 28) & 3U);
        /* POSTAUTH-7 receiver-state + re-arm evidence. */
        r->p7_op          = v_p7_op;
        r->p7_mode        = v_p7_opmode[1];
        r->p7_aux         = v_p7_aux;
        r->p7_pt          = v_p7_pt;
        r->p7_op_changed  = v_p7_changed;
        r->p7_op_after    = v_p7_changed ? v_p7_op_want : v_p7_op;
        r->p7_db_rc       = (uint8_t)v_p7_db_rc;
        r->p7_final_d1_rc = (uint8_t)v_p7_d1_rc;
        r->p7_final_d1_beg = v_p7_d1_beg;
        r->p7_final_d1_end = v_p7_d1_end;
        {   /* final-edge -> final-D1 must be < 50us for a valid attempt. */
            uint32_t hz = (SystemCoreClock == 0U) ? 1U : SystemCoreClock;
            uint32_t cyc50 = (hz / 1000000U) * 50U;
            r->p7_valid = (uint8_t)(((uint32_t)(v_p7_d1_beg - v_final_cyc)) < cyc50);
        }
    }
    return (uint32_t)(first_mod - rxe);
}


#endif /* M1_MFC_RAW_EMULATION */
