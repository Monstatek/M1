/* Host test for the pure lfrfid_dma_tx_prepare() transport-preparation seam
 * (lfrfid/lfrfid_dma_tx.c, compiled with -DLFRFID_DMA_TX_HOST_TEST so the
 * hardware-touching half of that file is excluded -- no HAL/RTOS dependency
 * needed to compile or run this).
 *
 * Build/run:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -DLFRFID_DMA_TX_HOST_TEST -I lfrfid \
 *      lfrfid/lfrfid_dma_tx.c lfrfid/test/lfrfid_dma_tx_test.c \
 *      -o /tmp/lfdmatx && /tmp/lfdmatx
 */
#include "lfrfid_dma_tx.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#define N 8U
static Encoded_Data_t g_steps[N];
static uint32_t g_bsrr_out[N];
static uint32_t g_arr_out[N];

static void fill_steps(uint32_t bsrr_base, uint16_t time_us_base)
{
    for (uint16_t i = 0; i < N; i++) {
        g_steps[i].bsrr    = bsrr_base + i;
        g_steps[i].time_us = (uint16_t)(time_us_base + i);
    }
}

static void test_one_output_per_input_and_pairing_preserved(void)
{
    EncodedTx_Data_t wave = { g_steps, N, 0 };
    uint16_t out_n = 0;
    fill_steps(0x1000U, 32U);

    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, N, &out_n);

    CHECK(st == LFRFID_DMA_TX_OK, "prepare() OK for a well-formed wave");
    CHECK(out_n == N, "exactly one output entry per input entry");
    for (uint16_t i = 0; i < N; i++) {
        CHECK(g_bsrr_out[i] == g_steps[i].bsrr, "BSRR value copied verbatim, in order");
    }
}

static void test_arr_inclusive_counter_math(void)
{
    /* At tick_hz == 1,000,000 (1 tick == 1us), time_us microseconds should
     * become exactly (time_us - 1) in ARR (the inclusive up-counter
     * correction this module centralizes). */
    EncodedTx_Data_t wave = { g_steps, N, 0 };
    uint16_t out_n = 0;
    fill_steps(0, 32U);   /* time_us = 32,33,...,39 */

    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, N, &out_n);
    CHECK(st == LFRFID_DMA_TX_OK, "prepare() OK for the ARR-math case");
    for (uint16_t i = 0; i < N; i++) {
        uint32_t expect = (uint32_t)(32U + i) - 1U;
        CHECK(g_arr_out[i] == expect, "ARR == ticks-1 at a 1 MHz tick rate");
    }

    /* Same durations at DOUBLE the tick rate must double the tick count
     * before the -1, proving the conversion genuinely uses tick_hz and does
     * not silently assume 1 MHz. */
    st = lfrfid_dma_tx_prepare(&wave, 2000000U, g_bsrr_out, g_arr_out, N, &out_n);
    CHECK(st == LFRFID_DMA_TX_OK, "prepare() OK at 2x tick rate");
    for (uint16_t i = 0; i < N; i++) {
        uint32_t expect = (uint32_t)((32U + i) * 2U) - 1U;
        CHECK(g_arr_out[i] == expect, "ARR scales correctly with tick_hz (2x case)");
    }
}

static void test_zero_length_rejected(void)
{
    EncodedTx_Data_t wave = { g_steps, 0, 0 };
    uint16_t out_n = 99;
    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, N, &out_n);
    CHECK(st == LFRFID_DMA_TX_ERR_EMPTY, "zero-length wave is rejected");
    CHECK(out_n == 0U, "out_count reset to 0 on rejection");
}

static void test_too_short_rejected(void)
{
    /* A single-entry wave has nothing for the circular body's own wrap
     * (prologue would need 0 words) -- must be refused, not silently
     * degrade to a 1-step "circular" loop. */
    fill_steps(0, 40U);
    EncodedTx_Data_t wave = { g_steps, 1, 0 };
    uint16_t out_n = 0;
    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, N, &out_n);
    CHECK(st == LFRFID_DMA_TX_ERR_TOO_SHORT, "a 1-entry wave is rejected (too short for prologue+circular body)");
}

static void test_oversize_rejected(void)
{
    EncodedTx_Data_t wave = { g_steps, N, 0 };
    uint16_t out_n = 0;
    /* out_cap smaller than wave->length -> must refuse, never write past cap. */
    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, (uint16_t)(N - 1U), &out_n);
    CHECK(st == LFRFID_DMA_TX_ERR_OVERSIZE, "wave->length > out_cap is rejected");
}

static void test_max_steps_boundary(void)
{
    /* wave->length above LFRFID_DMA_TX_MAX_STEPS must be rejected even if
     * out_cap itself is large enough -- both bounds are enforced. */
    EncodedTx_Data_t wave = { g_steps, (uint16_t)(LFRFID_DMA_TX_MAX_STEPS + 1U), 0 };
    uint16_t out_n = 0;
    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out,
                                                       (uint16_t)(LFRFID_DMA_TX_MAX_STEPS + 1U), &out_n);
    CHECK(st == LFRFID_DMA_TX_ERR_OVERSIZE, "wave->length > LFRFID_DMA_TX_MAX_STEPS is rejected");
}

static void test_bad_duration_rejected(void)
{
    /* time_us=0 rounds to 0 ticks at any tick_hz -- must be refused rather
     * than silently emitting an ARR that wraps to 0xFFFFFFFF (ticks64=0,
     * arr = 0-1 would underflow if not explicitly guarded). */
    fill_steps(0, 40U);
    g_steps[3].time_us = 0U;
    EncodedTx_Data_t wave = { g_steps, N, 0 };
    uint16_t out_n = 0;
    lfrfid_dma_tx_status_t st = lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, g_arr_out, N, &out_n);
    CHECK(st == LFRFID_DMA_TX_ERR_BAD_DURATION, "a zero-duration step is rejected");
}

static void test_null_inputs_rejected(void)
{
    EncodedTx_Data_t wave_null_data = { NULL, N, 0 };
    uint16_t out_n = 0;
    CHECK(lfrfid_dma_tx_prepare(NULL, 1000000U, g_bsrr_out, g_arr_out, N, &out_n) == LFRFID_DMA_TX_ERR_NULL,
          "NULL wave pointer is rejected");
    CHECK(lfrfid_dma_tx_prepare(&wave_null_data, 1000000U, g_bsrr_out, g_arr_out, N, &out_n) == LFRFID_DMA_TX_ERR_NULL,
          "NULL wave->data is rejected");
    {
        EncodedTx_Data_t wave = { g_steps, N, 0 };
        fill_steps(0, 40U);
        CHECK(lfrfid_dma_tx_prepare(&wave, 1000000U, NULL, g_arr_out, N, &out_n) == LFRFID_DMA_TX_ERR_NULL,
              "NULL bsrr_out is rejected");
        CHECK(lfrfid_dma_tx_prepare(&wave, 1000000U, g_bsrr_out, NULL, N, &out_n) == LFRFID_DMA_TX_ERR_NULL,
              "NULL arr_out is rejected");
        CHECK(lfrfid_dma_tx_prepare(&wave, 0U, g_bsrr_out, g_arr_out, N, &out_n) == LFRFID_DMA_TX_ERR_NULL,
              "tick_hz == 0 is rejected");
    }
}

static void test_deterministic_across_repeated_builds(void)
{
    uint32_t bsrr_a[N], arr_a[N], bsrr_b[N], arr_b[N];
    uint16_t n_a = 0, n_b = 0;
    EncodedTx_Data_t wave = { g_steps, N, 0 };
    fill_steps(0x2000U, 37U);

    lfrfid_dma_tx_status_t sa = lfrfid_dma_tx_prepare(&wave, 1500000U, bsrr_a, arr_a, N, &n_a);
    lfrfid_dma_tx_status_t sb = lfrfid_dma_tx_prepare(&wave, 1500000U, bsrr_b, arr_b, N, &n_b);

    CHECK(sa == LFRFID_DMA_TX_OK && sb == LFRFID_DMA_TX_OK, "both repeated builds succeed");
    CHECK(n_a == n_b, "repeated builds produce the same step count");
    CHECK(memcmp(bsrr_a, bsrr_b, sizeof(bsrr_a)) == 0, "repeated builds produce identical BSRR output");
    CHECK(memcmp(arr_a, arr_b, sizeof(arr_a)) == 0, "repeated builds produce identical ARR output");
}

int main(void)
{
    test_one_output_per_input_and_pairing_preserved();
    test_arr_inclusive_counter_math();
    test_zero_length_rejected();
    test_too_short_rejected();
    test_oversize_rejected();
    test_max_steps_boundary();
    test_bad_duration_rejected();
    test_null_inputs_rejected();
    test_deterministic_across_repeated_builds();

    printf("lfrfid_dma_tx_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
