/* See COPYING.txt for license details. */

/*
 * lfrfid_dma_tx.c -- see lfrfid_dma_tx.h.
 *
 * Ownership: GPDMA2 Channel0 (BSRR stream) + GPDMA2 Channel1 (ARR stream).
 * GPDMA2 is claimed nowhere else in this firmware (verified by repo-wide
 * grep before this module was written -- every other DMA user in this tree
 * is on GPDMA1: Sub-GHz TX ch0, log TX/RX ch1/ch2, ESP32 UART ch4/ch5,
 * capture ch4, MFC {At} ch6/ch7). This module does not touch GPDMA1, does
 * not touch the MFC DMA backend, and does not touch TIM7.
 *
 * TIM5 is shared with LF READ (capture, TIM5 CH4 input-capture -- see
 * lfrfid_RFIDIn_Init()/rfid_read_handler() in lfrfid_hal.c, both untouched).
 * The existing rfid_rxtx_is_taking_this_irq ownership flag already prevents
 * Read and Emulate from being active at the same time; this module fully
 * reconfigures TIM5 from scratch on every start() and fully releases it
 * (CR1=0, DIER=0, clock disabled) on every stop(), so a subsequent switch to
 * Read re-initializes TIM5 from nothing, and vice versa.
 */
#include "lfrfid_dma_tx.h"
#include <string.h>
#include <stdlib.h>
#if !defined(LFRFID_DMA_TX_HOST_TEST)
#include "stm32h5xx_hal.h"
#include "main.h"     /* RFID_PULL_Pin / RFID_PULL_GPIO_Port */
#endif

/*============================================================================
 * PURE TRANSPORT-PREPARATION SEAM (host-testable: no HAL/register access).
 *============================================================================*/
lfrfid_dma_tx_status_t lfrfid_dma_tx_prepare(const EncodedTx_Data_t *wave,
                                             uint32_t tick_hz,
                                             uint32_t *bsrr_out,
                                             uint32_t *arr_out,
                                             uint16_t out_cap,
                                             uint16_t *out_count)
{
    if (out_count != NULL) { *out_count = 0U; }

    if ((wave == NULL) || (wave->data == NULL) ||
        (bsrr_out == NULL) || (arr_out == NULL) || (tick_hz == 0U)) {
        return LFRFID_DMA_TX_ERR_NULL;
    }
    if (wave->length == 0U) { return LFRFID_DMA_TX_ERR_EMPTY; }
    if (wave->length < 2U)  { return LFRFID_DMA_TX_ERR_TOO_SHORT; }
    if ((wave->length > out_cap) || (wave->length > LFRFID_DMA_TX_MAX_STEPS)) {
        return LFRFID_DMA_TX_ERR_OVERSIZE;
    }

    for (uint16_t i = 0U; i < wave->length; i++) {
        uint32_t time_us = (uint32_t)wave->data[i].time_us;
        /* ticks = round(time_us * tick_hz / 1e6). 64-bit intermediate: at
         * tick_hz up to ~200 MHz and time_us up to 65535, the product fits
         * comfortably in 64 bits with wide margin; done explicitly rather
         * than assumed. */
        uint64_t ticks64 = ((uint64_t)time_us * (uint64_t)tick_hz + 500000ULL) / 1000000ULL;
        if (ticks64 < 1ULL) {
            /* A step whose duration rounds to less than one tick cannot be
             * represented at all at this clock -- refuse rather than silently
             * emit a zero/negative-period ARR. */
            return LFRFID_DMA_TX_ERR_BAD_DURATION;
        }
        if (ticks64 > 0x100000000ULL) { return LFRFID_DMA_TX_ERR_BAD_DURATION; }
        bsrr_out[i] = wave->data[i].bsrr;
        arr_out[i]  = (uint32_t)(ticks64 - 1ULL);   /* inclusive up-counter: ARR = ticks - 1 */
    }

    if (out_count != NULL) { *out_count = wave->length; }
    return LFRFID_DMA_TX_OK;
}

#if !defined(LFRFID_DMA_TX_HOST_TEST)
/*============================================================================
 * HARDWARE BACKEND (ARM target).
 *============================================================================*/

/* Word-aligned DMA source buffers in SRAM (GPDMA-accessible; DCACHE off for
 * this region on this target, matching m1_mfc_dma.c's own buffers).
 * Heap-allocated on first use (freed never, same lifetime convention as
 * lfrfid_encoded_data.data itself -- allocated once in lfrfid_Init(), lives
 * for the life of the firmware) rather than static arrays: two more
 * permanent 16 KB .bss reservations for a feature that may never be used
 * in a given boot is avoided the same way the rest of this subsystem
 * already avoids it. */
static uint32_t *s_bsrr = NULL;
static uint32_t *s_arr  = NULL;
static uint16_t s_nsteps = 0U;

static bool dma_ensure_buffers(void)
{
    if ((s_bsrr != NULL) && (s_arr != NULL)) { return true; }
    if (s_bsrr == NULL) { s_bsrr = (uint32_t *)malloc(sizeof(uint32_t) * LFRFID_DMA_TX_MAX_STEPS); }
    if (s_arr  == NULL) { s_arr  = (uint32_t *)malloc(sizeof(uint32_t) * LFRFID_DMA_TX_MAX_STEPS); }
    return (s_bsrr != NULL) && (s_arr != NULL);
}

static DMA_HandleTypeDef s_hdma_bsrr;   /* GPDMA2_Channel0 */
static DMA_HandleTypeDef s_hdma_arr;    /* GPDMA2_Channel1 */
static DMA_NodeTypeDef   s_node_bsrr_prologue, s_node_bsrr_body;
static DMA_NodeTypeDef   s_node_arr_prologue,  s_node_arr_body;
static DMA_QListTypeDef  s_qlist_bsrr, s_qlist_arr;

static volatile bool s_active = false;

/* Resolve the TIM5 kernel clock (APB1): PCLK1 when the APB1 prescaler is 1,
 * else 2*PCLK1 (RM0481) -- same derivation already proven in m1_mfc_dma.c's
 * dma_tim7_hz(), applied to the same APB1 bus TIM5 sits on. Read fresh every
 * start() rather than cached, so it is always the real, current clock. */
static uint32_t dma_tim5_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t ppre1 = (RCC->CFGR2 & RCC_CFGR2_PPRE1) >> RCC_CFGR2_PPRE1_Pos;
    return (ppre1 < 4U) ? pclk1 : (pclk1 * 2U);
}

static void dma_pa2_force_inactive(void)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = RFID_PULL_Pin;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RFID_PULL_GPIO_Port, &gi);
    HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
}

/* Build one node's common (non-address, non-size) config. */
static void dma_node_common(DMA_NodeConfTypeDef *nc, uint32_t request,
                            uint32_t dst_inc_or_fixed_is_src)
{
    memset(nc, 0, sizeof(*nc));
    nc->NodeType                    = DMA_GPDMA_LINEAR_NODE;
    nc->Init.Request                = request;
    nc->Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;   /* 1 word / TIM5 update */
    nc->Init.Direction              = DMA_MEMORY_TO_PERIPH;
    nc->Init.SrcInc                 = DMA_SINC_INCREMENTED;    /* walk our own buffer   */
    nc->Init.DestInc                = DMA_DINC_FIXED;          /* GPIOA->BSRR / TIM5->ARR */
    nc->Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_WORD;
    nc->Init.DestDataWidth          = DMA_DEST_DATAWIDTH_WORD;
    nc->Init.Priority               = DMA_HIGH_PRIORITY;
    nc->Init.SrcBurstLength         = 1U;
    nc->Init.DestBurstLength        = 1U;
    nc->Init.TransferAllocatedPort  = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    nc->Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
    nc->Init.Mode                   = DMA_NORMAL;   /* per-node; circularity is the queue's */
    nc->TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    nc->DataHandlingConfig.DataExchange   = DMA_EXCHANGE_NONE;
    nc->DataHandlingConfig.DataAlignment  = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    (void)dst_inc_or_fixed_is_src;
}

/* Build one channel's two-node queue:
 *   prologue node : entries [1 .. n-1]  (n-1 words) -- executed exactly once
 *   body node     : entries [0 .. n-1]  (n   words) -- the circular target
 * The manual prime (applying entry 0 directly to GPIOA/TIM5 before the timer
 * starts) supplies entry 0 for the very first period; the prologue node then
 * supplies entries 1..n-1 for the remainder of the first lap; every lap
 * after that re-enters at the body node and correctly includes entry 0 --
 * entry 0 is never permanently skipped on wrap. */
static bool dma_build_channel(DMA_HandleTypeDef *hdma, DMA_Channel_TypeDef *inst,
                              DMA_NodeTypeDef *node_prologue, DMA_NodeTypeDef *node_body,
                              DMA_QListTypeDef *qlist,
                              const uint32_t *buf, uint16_t n, uint32_t request,
                              uint32_t dst_addr)
{
    DMA_NodeConfTypeDef nc;

    memset(qlist, 0, sizeof(*qlist));

    dma_node_common(&nc, request, 0U);
    nc.SrcAddress = (uint32_t)&buf[1];
    nc.DstAddress = dst_addr;
    nc.DataSize   = (uint32_t)(n - 1U) * 4U;
    if (HAL_DMAEx_List_BuildNode(&nc, node_prologue) != HAL_OK) { return false; }

    dma_node_common(&nc, request, 0U);
    nc.SrcAddress = (uint32_t)&buf[0];
    nc.DstAddress = dst_addr;
    nc.DataSize   = (uint32_t)n * 4U;
    if (HAL_DMAEx_List_BuildNode(&nc, node_body) != HAL_OK) { return false; }

    if (HAL_DMAEx_List_InsertNode_Tail(qlist, node_prologue) != HAL_OK) { return false; }
    if (HAL_DMAEx_List_InsertNode_Tail(qlist, node_body)     != HAL_OK) { return false; }
    /* node_body is NOT the head node (node_prologue is), so the queue runs
     * node_prologue exactly once, then loops node_body forever -- exactly
     * the documented behavior of SetCircularModeConfig() for a non-head
     * first-circular-node (stm32h5xx_hal_dma_ex.c's own module doc, verified
     * before writing this). */
    if (HAL_DMAEx_List_SetCircularModeConfig(qlist, node_body) != HAL_OK) { return false; }

    memset(hdma, 0, sizeof(*hdma));
    hdma->Instance                          = inst;
    hdma->InitLinkedList.Priority           = DMA_HIGH_PRIORITY;
    hdma->InitLinkedList.LinkStepMode       = DMA_LSM_FULL_EXECUTION;
    hdma->InitLinkedList.LinkAllocatedPort  = DMA_LINK_ALLOCATED_PORT0;
    hdma->InitLinkedList.TransferEventMode  = DMA_TCEM_BLOCK_TRANSFER;
    hdma->InitLinkedList.LinkedListMode     = DMA_LINKEDLIST_CIRCULAR;
    if (HAL_DMAEx_List_Init(hdma) != HAL_OK) { return false; }
    if (HAL_DMAEx_List_LinkQ(hdma, qlist)   != HAL_OK) { return false; }
    return true;
}

static bool dma_release_all(void)
{
    bool released = true;
    /* A node/queue build can fail before Instance is assigned. DeInit's
     * release-build implementation dereferences it even for RESET handles. */
    if (s_hdma_bsrr.Instance != NULL) {
        (void)HAL_DMA_Abort(&s_hdma_bsrr);
        if (HAL_DMAEx_List_DeInit(&s_hdma_bsrr) == HAL_OK) {
            memset(&s_hdma_bsrr, 0, sizeof(s_hdma_bsrr));
            memset(&s_qlist_bsrr, 0, sizeof(s_qlist_bsrr));
        } else { released = false; }
    }
    if (s_hdma_arr.Instance != NULL) {
        (void)HAL_DMA_Abort(&s_hdma_arr);
        if (HAL_DMAEx_List_DeInit(&s_hdma_arr) == HAL_OK) {
            memset(&s_hdma_arr, 0, sizeof(s_hdma_arr));
            memset(&s_qlist_arr, 0, sizeof(s_qlist_arr));
        } else { released = false; }
    }
    return released;
}

bool lfrfid_dma_tx_start(const EncodedTx_Data_t *wave)
{
    lfrfid_dma_tx_status_t st;
    uint16_t n = 0U;
    uint32_t tick_hz;

    if (s_active) { lfrfid_dma_tx_stop(); }   /* defensive: never double-arm */
    /* Never overwrite descriptors or buffers after a failed channel stop. */
    if (!dma_release_all()) { return false; }

    if (!dma_ensure_buffers()) { return false; }   /* fail closed: no allocation, no arm */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPDMA2_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();

    tick_hz = dma_tim5_hz();
    st = lfrfid_dma_tx_prepare(wave, tick_hz, s_bsrr, s_arr, LFRFID_DMA_TX_MAX_STEPS, &n);
    if (st != LFRFID_DMA_TX_OK) { return false; }   /* fail closed: nothing armed yet */
    s_nsteps = n;

    /* TIM5 basic up-counter: PSC=0 (dma_tim5_hz() already reflects the real
     * kernel clock with no software prescale), ARPE off so a DMA write to
     * ARR takes effect on the very next period (matching TIM7's proven MFC
     * config), UDE so the timer's own Update event drives both DMA requests.
     * No UIE -- the CPU is never interrupted during steady-state emulation. */
    TIM5->CR1  = 0U;
    TIM5->PSC  = 0U;
    TIM5->CNT  = 0U;
    TIM5->SR   = 0U;             /* clear any stale UIF/CC4IF before (re)starting */
    TIM5->DIER = 0U;

    if (!dma_build_channel(&s_hdma_bsrr, GPDMA2_Channel0, &s_node_bsrr_prologue,
                           &s_node_bsrr_body, &s_qlist_bsrr, s_bsrr, n,
                           GPDMA2_REQUEST_TIM5_UP, (uint32_t)&GPIOA->BSRR)) {
        lfrfid_dma_tx_stop();
        return false;
    }
    if (!dma_build_channel(&s_hdma_arr, GPDMA2_Channel1, &s_node_arr_prologue,
                           &s_node_arr_body, &s_qlist_arr, s_arr, n,
                           GPDMA2_REQUEST_TIM5_UP, (uint32_t)&TIM5->ARR)) {
        lfrfid_dma_tx_stop();
        return false;
    }

    /* Prime entry 0 directly (GPIO + ARR) before anything else runs -- the
     * timer has not started yet, so this fully determines the first period's
     * level and duration with no race. */
    TIM5->ARR   = s_arr[0];
    GPIOA->BSRR = s_bsrr[0];

    if (HAL_DMAEx_List_Start(&s_hdma_bsrr) != HAL_OK) { lfrfid_dma_tx_stop(); return false; }
    if (HAL_DMAEx_List_Start(&s_hdma_arr)  != HAL_OK) { lfrfid_dma_tx_stop(); return false; }

    TIM5->DIER = TIM_DIER_UDE;      /* enable the DMA request; UIE stays 0 */
    TIM5->CR1  = TIM_CR1_CEN;       /* start counting -- steady state begins here */

    s_active = true;
    return true;
}

void lfrfid_dma_tx_stop(void)
{
    TIM5->CR1  = 0U;
    TIM5->DIER = 0U;
    TIM5->SR   = 0U;
    dma_release_all();
    __HAL_RCC_TIM5_CLK_DISABLE();
    dma_pa2_force_inactive();

    s_nsteps = 0U;
    s_active = false;
}

bool lfrfid_dma_tx_active(void)
{
    return s_active;
}

/* Margin note (both channels share one TIM5_UP request/trigger, which
 * guarantees a common trigger instant, NOT that both writes physically land
 * in the same bus cycle -- GPIOA is AHB2, TIM5->ARR is APB1, so the two
 * writes travel different bus segments and can complete a few cycles apart).
 * A single-beat, single-word GPDMA memory-to-peripheral transfer completes
 * within a handful of AHB bus cycles of being granted (arbitration + one
 * read + one write beat) -- at this target's ~150 MHz AHB clock, worst-case
 * on the order of tens of nanoseconds, even generously bounded. Every
 * supported protocol's shortest waveform step (H10301/AWID's fc/8 half-cycle,
 * the tightest in the set) is 32 microseconds -- three orders of magnitude
 * larger. Both transfers are therefore guaranteed to complete and be ready
 * for the NEXT TIM5 update long before the shortest period in any supported
 * waveform elapses; there is no plausible race between "trigger fired" and
 * "both writes landed" at these timescales. This is a margin argument, not a
 * literal simultaneity guarantee -- which is exactly why
 * lfrfid_dma_tx_check_health() below treats EITHER channel's own hardware
 * error flag as fatal for the whole session, rather than assuming the
 * pairing can never be disturbed. */
bool lfrfid_dma_tx_check_health(void)
{
    bool bsrr_err, arr_err;

    if (!s_active) { return true; }

    bsrr_err = (__HAL_DMA_GET_FLAG(&s_hdma_bsrr, DMA_FLAG_DTE) ||
                __HAL_DMA_GET_FLAG(&s_hdma_bsrr, DMA_FLAG_ULE) ||
                __HAL_DMA_GET_FLAG(&s_hdma_bsrr, DMA_FLAG_USE));
    arr_err  = (__HAL_DMA_GET_FLAG(&s_hdma_arr, DMA_FLAG_DTE) ||
                __HAL_DMA_GET_FLAG(&s_hdma_arr, DMA_FLAG_ULE) ||
                __HAL_DMA_GET_FLAG(&s_hdma_arr, DMA_FLAG_USE));

    if (bsrr_err || arr_err) {
        /* Either channel faulting desyncs the GPIO/period pairing for every
         * step from here on -- treat as fatal and tear the whole session
         * down rather than continuing on one good channel. */
        lfrfid_dma_tx_stop();
        return false;
    }
    return true;
}

#endif /* !LFRFID_DMA_TX_HOST_TEST */
