/* Host test for the pure bq27421 boot-guard control flow (m1_bq27421_boot_guard.c).
 * No HAL/RTOS dependency.
 *
 * Build/run:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -I m1_csrc \
 *      m1_csrc/m1_bq27421_boot_guard.c \
 *      m1_csrc/test/m1_bq27421_boot_guard_test.c \
 *      -o /tmp/bq_guard && /tmp/bq_guard
 */
#include "m1_bq27421_boot_guard.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* ---- shared mock state ---- */
static int      g_delay_calls;
static int      g_wdt_calls;
static int      g_itpor_calls;
static int      g_current_calls;
static int      g_ready_at_call;      /* itpor_ready() returns true starting at this call (1-based); 0 = never */
static int      g_stable_at_call;     /* read_current_mA() returns an in-band value starting at this call; 0 = never */

static void mock_reset(void)
{
    g_delay_calls = 0; g_wdt_calls = 0; g_itpor_calls = 0; g_current_calls = 0;
    g_ready_at_call = 0; g_stable_at_call = 0;
}

static void mock_delay_ms(uint32_t ms) { (void)ms; g_delay_calls++; }
static void mock_wdt_kick(void) { g_wdt_calls++; }

static bool mock_itpor_ready(void)
{
    g_itpor_calls++;
    return (g_ready_at_call != 0) && (g_itpor_calls >= g_ready_at_call);
}

static int16_t mock_read_current(void)
{
    g_current_calls++;
    if ((g_stable_at_call != 0) && (g_current_calls >= g_stable_at_call)) {
        return 150;   /* in-band (>100) */
    }
    return 0;         /* in the dead zone (-2..100), not stable */
}

static const bq27421_boot_guard_ops_t k_ops = {
    mock_itpor_ready, mock_read_current, mock_delay_ms, mock_wdt_kick
};

/* ================= bq27421_wait_for_itpor ================= */

static void test_itpor_immediate_success(void)
{
    mock_reset();
    g_ready_at_call = 1;
    bool ok = bq27421_wait_for_itpor(&k_ops, 2000U);
    CHECK(ok == true, "itpor: immediate success returns true");
    CHECK(g_itpor_calls == 1, "itpor: immediate success polls exactly once");
}

static void test_itpor_success_on_final_permitted_attempt(void)
{
    mock_reset();
    g_ready_at_call = 5;
    bool ok = bq27421_wait_for_itpor(&k_ops, 5U);
    CHECK(ok == true, "itpor: success exactly on the last permitted attempt returns true");
    CHECK(g_itpor_calls == 5, "itpor: stops polling the instant it succeeds");
}

static void test_itpor_complete_timeout(void)
{
    mock_reset();
    g_ready_at_call = 0;   /* never ready */
    bool ok = bq27421_wait_for_itpor(&k_ops, 2000U);
    CHECK(ok == false, "itpor: complete timeout returns false, not true");
    CHECK(g_itpor_calls == 2000, "itpor: polls exactly max_attempts times on timeout");
    CHECK(g_delay_calls == 2000, "itpor: delays exactly max_attempts times on timeout");
}

static void test_itpor_no_wraparound_at_uint16_boundary(void)
{
    /* The original bug: uint16_t timeout-- wraps 0 -> 0xFFFF. Prove a
     * max_attempts of exactly 0xFFFF (the largest uint16_t value) still
     * times out cleanly with no wraparound-induced false success, and
     * that max_attempts == 0 is also handled (immediate false, no UB). */
    mock_reset();
    g_ready_at_call = 0;
    bool ok = bq27421_wait_for_itpor(&k_ops, 0xFFFFU);
    CHECK(ok == false, "itpor: max uint16_t attempts still times out honestly (no wraparound)");
    CHECK(g_itpor_calls == 0xFFFF, "itpor: polls exactly 0xFFFF times, no more, no less");

    mock_reset();
    ok = bq27421_wait_for_itpor(&k_ops, 0U);
    CHECK(ok == false, "itpor: max_attempts == 0 returns false immediately");
    CHECK(g_itpor_calls == 0, "itpor: max_attempts == 0 never polls");
}

/* ================= bq27421_wait_for_current_stable ================= */

static void test_current_immediate_success(void)
{
    mock_reset();
    g_stable_at_call = 1;
    bool ok = bq27421_wait_for_current_stable(&k_ops, 500U);
    CHECK(ok == true, "current: immediate stabilization returns true");
    CHECK(g_current_calls == 1, "current: stops polling the instant it stabilizes");
    CHECK(g_wdt_calls == 0, "current: no watchdog kick needed when already stable");
}

static void test_current_stabilizes_within_bound(void)
{
    mock_reset();
    g_stable_at_call = 500;
    bool ok = bq27421_wait_for_current_stable(&k_ops, 500U);
    CHECK(ok == true, "current: stabilizing exactly on the last permitted attempt succeeds");
    CHECK(g_current_calls == 500, "current: polls up to and including the stabilizing attempt");
    CHECK(g_wdt_calls == 499, "current: watchdog kicked once per non-stable attempt, never after success");
}

static void test_current_never_stabilizes(void)
{
    mock_reset();
    g_stable_at_call = 0;   /* never stabilizes */
    bool ok = bq27421_wait_for_current_stable(&k_ops, 500U);
    CHECK(ok == false, "current: never stabilizing returns false, not true");
    CHECK(g_current_calls == 500, "current: polls exactly max_attempts times");
    CHECK(g_wdt_calls == 500, "current: watchdog kicked exactly max_attempts times, then STOPS (bounded)");
}

static void test_current_null_ops_rejected(void)
{
    CHECK(bq27421_wait_for_itpor(NULL, 100U) == false, "itpor: NULL ops rejected, not UB");
    CHECK(bq27421_wait_for_current_stable(NULL, 100U) == false, "current: NULL ops rejected, not UB");
}

int main(void)
{
    test_itpor_immediate_success();
    test_itpor_success_on_final_permitted_attempt();
    test_itpor_complete_timeout();
    test_itpor_no_wraparound_at_uint16_boundary();

    test_current_immediate_success();
    test_current_stabilizes_within_bound();
    test_current_never_stabilizes();

    test_current_null_ops_rejected();

    printf("m1_bq27421_boot_guard_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
