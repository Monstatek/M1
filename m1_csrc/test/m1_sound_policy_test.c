/*
 * m1_sound_policy_test.c - host tests for the buzzer semantic sound policy.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_sound_policy.c m1_csrc/test/m1_sound_policy_test.c \
 *      -o /tmp/sound_policy_test && /tmp/sound_policy_test
 */
#include "m1_sound_policy.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static void test_spec_for_each_event(void)
{
	m1_sound_spec_t s;

	printf("test_spec_for_each_event\n");

	s = m1_sound_spec_for(M1_SOUND_DETECTED);
	CHECK(s.repeat_count == 1 && s.duration_ms > 0 && s.duration_ms <= 100,
		  "detected is a single, very short tone");

	s = m1_sound_spec_for(M1_SOUND_SUCCESS);
	CHECK(s.repeat_count == 1 && s.freq_hz == 4000 && s.duration_ms == 250,
		  "success matches the existing verified 4kHz/250ms standard tone");

	s = m1_sound_spec_for(M1_SOUND_WARNING);
	CHECK(s.repeat_count == 2 && s.gap_ms > 0, "warning is a double tone with a gap between beeps");

	s = m1_sound_spec_for(M1_SOUND_ERROR);
	CHECK(s.repeat_count == 1 && s.freq_hz == 2000 && s.duration_ms == 250,
		  "error matches the existing verified 2kHz/250ms alternate tone");
	CHECK(s.freq_hz < m1_sound_spec_for(M1_SOUND_SUCCESS).freq_hz, "error tone is lower-pitched than success, per spec");

	s = m1_sound_spec_for((m1_sound_event_t)999);
	CHECK(s.repeat_count == 0, "an invalid event yields a zeroed spec, not garbage");
}

static void test_gate_first_call_always_allowed(void)
{
	printf("test_gate_first_call_always_allowed\n");
	m1_sound_policy_reset();

	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 1000) == true, "first-ever detected is allowed");
	CHECK(m1_sound_policy_gate(M1_SOUND_SUCCESS, 1000) == true, "first-ever success is allowed");
	CHECK(m1_sound_policy_gate(M1_SOUND_WARNING, 1000) == true, "first-ever warning is allowed");
	CHECK(m1_sound_policy_gate(M1_SOUND_ERROR, 1000) == true, "first-ever error is allowed");
}

static void test_gate_rate_limits_within_cooldown(void)
{
	printf("test_gate_rate_limits_within_cooldown\n");
	m1_sound_policy_reset();

	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 1000) == true, "first detected sounds");
	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 1010) == false, "a detected 10ms later is rate-limited");
	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 1299) == false, "still within the 300ms cooldown at +299ms");
	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 1300) == true, "allowed again exactly at the cooldown boundary");
}

static void test_gate_events_independent(void)
{
	printf("test_gate_events_independent\n");
	m1_sound_policy_reset();

	CHECK(m1_sound_policy_gate(M1_SOUND_ERROR, 5000) == true, "error sounds");
	CHECK(m1_sound_policy_gate(M1_SOUND_DETECTED, 5001) == true,
		  "a detected right after an error is NOT suppressed -- each event has its own cooldown");
	CHECK(m1_sound_policy_gate(M1_SOUND_SUCCESS, 5002) == true, "success independent of both");
	CHECK(m1_sound_policy_gate(M1_SOUND_WARNING, 5003) == true, "warning independent of all three");
}

static void test_gate_invalid_event_always_denied(void)
{
	printf("test_gate_invalid_event_always_denied\n");
	m1_sound_policy_reset();

	CHECK(m1_sound_policy_gate((m1_sound_event_t)999, 1000) == false, "an out-of-range event never sounds");
}

static void test_gate_reset_clears_cooldowns(void)
{
	printf("test_gate_reset_clears_cooldowns\n");
	m1_sound_policy_reset();

	m1_sound_policy_gate(M1_SOUND_SUCCESS, 1000);
	CHECK(m1_sound_policy_gate(M1_SOUND_SUCCESS, 1010) == false, "still within cooldown before reset");

	m1_sound_policy_reset();
	CHECK(m1_sound_policy_gate(M1_SOUND_SUCCESS, 1010) == true, "reset clears the cooldown immediately");
}

static void test_gate_tick_wraparound_safe(void)
{
	uint32_t near_wrap = 0xFFFFFFF0U;

	printf("test_gate_tick_wraparound_safe\n");
	m1_sound_policy_reset();

	CHECK(m1_sound_policy_gate(M1_SOUND_ERROR, near_wrap) == true, "sounds just before the tick counter wraps");
	CHECK(m1_sound_policy_gate(M1_SOUND_ERROR, near_wrap + 10U) == false, "still rate-limited 10 ticks later");
	/* near_wrap + 500 overflows past UINT32_MAX and wraps back around to a
	 * small value -- the gate must still measure the elapsed time as 500
	 * (via unsigned wraparound subtraction), not treat "now" as having gone
	 * backwards and let a spurious extra tone through. */
	CHECK(m1_sound_policy_gate(M1_SOUND_ERROR, near_wrap + 500U) == true,
		  "allowed again once the cooldown has genuinely elapsed across the wrap");
}

int main(void)
{
	test_spec_for_each_event();
	test_gate_first_call_always_allowed();
	test_gate_rate_limits_within_cooldown();
	test_gate_events_independent();
	test_gate_invalid_event_always_denied();
	test_gate_reset_clears_cooldowns();
	test_gate_tick_wraparound_safe();

	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
