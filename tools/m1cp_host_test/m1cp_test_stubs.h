/* Minimal stubs for the M1CP host unit test (scratchpad only). Provides the
 * task-tick macro the firmware build otherwise gets from the HAL. A controllable
 * tick lets the test exercise the frame/session timeouts deterministically. */
#ifndef M1CP_TEST_STUBS_H_
#define M1CP_TEST_STUBS_H_

#include <stdint.h>

extern uint32_t g_test_tick_ms;
#define M1CP_TICK_MS()   (g_test_tick_ms)

/* m1_esp32_hal.c (real ISR/ring-buffer counters) isn't linked into the host
 * test - stub these to zero so m1_manager_protocol.c links. */
static uint32_t m1_esp32_get_and_clear_rx_drop_count(void) { return 0U; }
static uint32_t m1_esp32_get_and_clear_ore_count(void) { return 0U; }

#endif
