/*
 * Host verification for the shared Keri/NexWatch PSK1 waveform builder.
 *
 * Build/run:
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -DLFRFID_DMA_TX_HOST_TEST -I lfrfid \
 *      lfrfid/lfrfid_psk1_wave.c lfrfid/lfrfid_dma_tx.c \
 *      lfrfid/test/lfrfid_psk1_wave_test.c -o /tmp/lfpsk1 && /tmp/lfpsk1
 */
#include "lfrfid_dma_tx.h"
#include "lfrfid_psk1_wave.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_GPIO_PIN  (2U)
#define MAX_STEPS      (4096U)

static Encoded_Data_t g_steps[MAX_STEPS];
static uint32_t g_bsrr[MAX_STEPS];
static uint32_t g_arr[MAX_STEPS];
static int g_pass;
static int g_fail;

#define CHECK(condition, message) do { \
    if(condition) g_pass++; \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (message), __LINE__); } \
} while(0)

static uint8_t get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8U] >> (7U - (position % 8U))) & 1U);
}

static bool step_is_set(const Encoded_Data_t *step)
{
    return step->bsrr == (1UL << TEST_GPIO_PIN);
}

static void verify_frame(const char *name, const uint8_t *frame, size_t bits)
{
    size_t n = 0;
    size_t expected_n = bits * LFRFID_PSK1_STEPS_PER_BIT;
    int rc = lfrfid_psk1_encoded_to_wave(frame, bits, TEST_GPIO_PIN,
                                          g_steps, MAX_STEPS, &n);
    CHECK(rc == 0, name);
    CHECK(n == expected_n, name);

    bool expected_polarity = true;
    uint8_t last_bit = get_bit(frame, bits - 1U);
    uint64_t total_us = 0;

    for(size_t bit = 0; bit < bits; bit++) {
        size_t base = bit * LFRFID_PSK1_STEPS_PER_BIT;
        CHECK(step_is_set(&g_steps[base]) == expected_polarity,
              "bit begins at the expected differential phase");

        for(size_t cycle = 0; cycle < LFRFID_PSK1_CYCLES_PER_BIT; cycle++) {
            size_t i = base + cycle * 2U;
            CHECK(g_steps[i].time_us == LFRFID_PSK1_HALF_CYCLE_US,
                  "first half-cycle is 8 us");
            CHECK(g_steps[i + 1U].time_us == LFRFID_PSK1_HALF_CYCLE_US,
                  "second half-cycle is 8 us");
            CHECK(step_is_set(&g_steps[i]) == expected_polarity,
                  "first half-cycle preserves bit phase");
            CHECK(step_is_set(&g_steps[i + 1U]) != expected_polarity,
                  "second half-cycle is the inverse phase");
            total_us += g_steps[i].time_us + g_steps[i + 1U].time_us;
        }

        uint8_t current_bit = get_bit(frame, bit);
        if(current_bit != last_bit)
            expected_polarity = !expected_polarity;
        last_bit = current_bit;
    }

    CHECK(expected_polarity, "circular frame returns to its starting phase");
    CHECK(total_us == bits * 256U, "each encoded bit occupies exactly 256 us");

    EncodedTx_Data_t wave = {g_steps, (uint16_t)n, 0};
    uint16_t prepared = 0;
    lfrfid_dma_tx_status_t status = lfrfid_dma_tx_prepare(
        &wave, 1000000U, g_bsrr, g_arr, MAX_STEPS, &prepared);
    CHECK(status == LFRFID_DMA_TX_OK, "DMA preparation accepts PSK1 waveform");
    CHECK(prepared == n, "DMA preparation preserves PSK1 step count");
    for(size_t i = 0; i < n; i++) {
        CHECK(g_arr[i] == 7U, "8 us PSK1 step becomes ARR=7 at 1 MHz");
        CHECK(g_bsrr[i] == g_steps[i].bsrr, "DMA preserves PSK1 GPIO state");
    }
}

static void test_fail_closed(void)
{
    const uint8_t frame[8] = {0};
    size_t n = 123U;

    CHECK(lfrfid_psk1_encoded_to_wave(NULL, 64U, TEST_GPIO_PIN,
                                      g_steps, MAX_STEPS, &n) == -1 && n == 0U,
          "NULL encoded input is rejected");
    CHECK(lfrfid_psk1_encoded_to_wave(frame, 0U, TEST_GPIO_PIN,
                                      g_steps, MAX_STEPS, &n) == -1 && n == 0U,
          "zero-bit frame is rejected");
    CHECK(lfrfid_psk1_encoded_to_wave(frame, 64U, 16U,
                                      g_steps, MAX_STEPS, &n) == -1 && n == 0U,
          "invalid GPIO pin is rejected");
    CHECK(lfrfid_psk1_encoded_to_wave(frame, 64U, TEST_GPIO_PIN,
                                      g_steps, 2047U, &n) == -1 && n == 0U,
          "undersized Keri destination is rejected before writing");
}

int main(void)
{
    /* Keri wire frame: fixed E0 000000 preamble and marker bit set in byte 4. */
    const uint8_t keri[8] = {0xE0, 0x00, 0x00, 0x00, 0x92, 0x34, 0x56, 0x78};
    /* NexWatch wire frame: 0x56 preamble, reserved zeros, then stored data. */
    const uint8_t nexwatch[12] = {
        0x56, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE
    };

    verify_frame("Keri builder succeeds", keri, 64U);
    verify_frame("NexWatch builder succeeds", nexwatch, 96U);
    test_fail_closed();

    printf("lfrfid_psk1_wave_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
