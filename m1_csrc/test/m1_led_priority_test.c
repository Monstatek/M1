/*
 * m1_led_priority_test.c - host tests for the LED owner/priority arbiter.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_led_priority.c m1_csrc/test/m1_led_priority_test.c \
 *      -o /tmp/led_priority_test && /tmp/led_priority_test
 */
#include "m1_led_priority.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static m1_led_effect_t mk(uint8_t r_g_b, uint8_t pwm, uint16_t on_off_ms, m1_led_mode_t mode)
{
	m1_led_effect_t e;
	e.r_g_b = r_g_b;
	e.pwm = pwm;
	e.on_off_ms = on_off_ms;
	e.mode = mode;
	return e;
}

static void test_nothing_active_returns_none(void)
{
	m1_led_effect_t eff;
	m1_led_owner_t top;

	printf("test_nothing_active_returns_none\n");
	m1_led_priority_reset();

	top = m1_led_topmost(&eff);
	CHECK(top == M1_LED_OWNER_NONE, "fresh state has no topmost owner");
}

static void test_priority_ordering(void)
{
	m1_led_effect_t e_act = mk(0x04, 175, 100, M1_LED_MODE_BLINK); /* blue blink   */
	m1_led_effect_t e_sd  = mk(0x02, 175, 100, M1_LED_MODE_BLINK); /* green blink  */
	m1_led_effect_t e_chg = mk(0x01, 175, 0,   M1_LED_MODE_SOLID); /* solid red    */
	m1_led_effect_t e_fw  = mk(0x06, 25,  100, M1_LED_MODE_BLINK); /* green+blue   */
	m1_led_effect_t e_crit= mk(0x01, 255, 50,  M1_LED_MODE_BLINK); /* fast red     */
	m1_led_effect_t out;

	printf("test_priority_ordering\n");
	m1_led_priority_reset();

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e_act, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FEATURE_ACTIVITY, "activity alone is topmost");

	m1_led_owner_begin(M1_LED_OWNER_SD_EVENT, &e_sd, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_SD_EVENT, "SD event outranks activity");

	m1_led_owner_begin(M1_LED_OWNER_CHARGING, &e_chg, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_CHARGING, "charging outranks SD event");
	CHECK(out.r_g_b == e_chg.r_g_b && out.mode == M1_LED_MODE_SOLID, "topmost effect matches charging's content");

	m1_led_owner_begin(M1_LED_OWNER_FW_UPDATE, &e_fw, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FW_UPDATE, "firmware update outranks charging");

	m1_led_owner_begin(M1_LED_OWNER_CRITICAL, &e_crit, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_CRITICAL, "critical outranks everything, including firmware update");
}

static void test_preemption_and_restoration(void)
{
	m1_led_effect_t e_act = mk(0x04, 175, 100, M1_LED_MODE_BLINK);
	m1_led_effect_t e_chg = mk(0x02, 175, 0,   M1_LED_MODE_SOLID);
	uint8_t chg_token;
	m1_led_effect_t out;

	printf("test_preemption_and_restoration\n");
	m1_led_priority_reset();

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e_act, NULL);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FEATURE_ACTIVITY, "activity shown before preemption");

	m1_led_owner_begin(M1_LED_OWNER_CHARGING, &e_chg, &chg_token);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_CHARGING, "charging preempts activity");
	CHECK(m1_led_owner_is_active(M1_LED_OWNER_FEATURE_ACTIVITY), "activity stays registered while preempted, not cancelled");

	m1_led_owner_end(M1_LED_OWNER_CHARGING, chg_token);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FEATURE_ACTIVITY, "activity is restored once charging ends");
	CHECK(out.r_g_b == e_act.r_g_b && out.on_off_ms == e_act.on_off_ms,
		  "restored effect content is exactly what activity originally set, not stale/zeroed");
}

static void test_owner_cancellation_stale_token(void)
{
	m1_led_effect_t e1 = mk(0x01, 175, 100, M1_LED_MODE_BLINK);
	m1_led_effect_t e2 = mk(0x02, 175, 100, M1_LED_MODE_BLINK);
	uint8_t token1, token2;
	m1_led_result_t r;
	m1_led_effect_t out;

	printf("test_owner_cancellation_stale_token\n");
	m1_led_priority_reset();

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e1, &token1);
	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e2, &token2); /* supersedes token1 */
	CHECK(token1 != token2, "a new begin() issues a fresh token");

	r = m1_led_owner_end(M1_LED_OWNER_FEATURE_ACTIVITY, token1);
	CHECK(r == M1_LED_REJECTED_STALE, "ending with a superseded token is rejected");
	CHECK(m1_led_owner_is_active(M1_LED_OWNER_FEATURE_ACTIVITY), "owner is still active after a stale end() -- not cancelled by a stray callback");
	m1_led_topmost(&out);
	CHECK(out.r_g_b == e2.r_g_b, "the current effect is still the newer request's, unaffected by the stale end()");

	r = m1_led_owner_end(M1_LED_OWNER_FEATURE_ACTIVITY, token2);
	CHECK(r == M1_LED_ACCEPTED, "ending with the current token succeeds");
	CHECK(!m1_led_owner_is_active(M1_LED_OWNER_FEATURE_ACTIVITY), "owner is inactive after a valid end()");
}

static void test_saturation_no_overflow(void)
{
	m1_led_effect_t e_act = mk(0x04, 175, 100, M1_LED_MODE_BLINK);
	m1_led_effect_t e_sd  = mk(0x02, 175, 100, M1_LED_MODE_BLINK);
	m1_led_effect_t e_chg = mk(0x01, 175, 0,   M1_LED_MODE_SOLID);
	m1_led_effect_t e_fw  = mk(0x06, 25,  100, M1_LED_MODE_BLINK);
	m1_led_effect_t e_crit= mk(0x07, 255, 50,  M1_LED_MODE_BLINK);
	m1_led_owner_t owners[] = { M1_LED_OWNER_FEATURE_ACTIVITY, M1_LED_OWNER_SD_EVENT,
								M1_LED_OWNER_CHARGING, M1_LED_OWNER_FW_UPDATE, M1_LED_OWNER_CRITICAL };
	m1_led_effect_t *effects[] = { &e_act, &e_sd, &e_chg, &e_fw, &e_crit };
	size_t i;
	m1_led_effect_t out;

	printf("test_saturation_no_overflow\n");
	m1_led_priority_reset();

	/* Unlike the old fixed-size 3-entry queue, every owner has its own
	 * permanent slot -- activating all of them at once must not evict or
	 * silently drop any owner's state. */
	for ( i = 0; i < sizeof(owners) / sizeof(owners[0]); i++ )
		m1_led_owner_begin(owners[i], effects[i], NULL);

	for ( i = 0; i < sizeof(owners) / sizeof(owners[0]); i++ )
		CHECK(m1_led_owner_is_active(owners[i]), "every owner remains active with 5/5 owners occupied simultaneously");

	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_CRITICAL, "topmost is still the highest-priority owner under full occupancy");

	/* Ending owners top-down must reveal exactly the next-highest one each
	 * time, with its original content intact -- proving no state was lost
	 * while it sat underneath higher-priority owners. */
	m1_led_owner_end(M1_LED_OWNER_CRITICAL, 1);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FW_UPDATE, "FW update revealed after critical ends");
	CHECK(out.r_g_b == e_fw.r_g_b, "FW update's content survived being preempted by critical");

	m1_led_owner_end(M1_LED_OWNER_FW_UPDATE, 1);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_CHARGING, "charging revealed after FW update ends");
	CHECK(out.r_g_b == e_chg.r_g_b, "charging's content survived");

	m1_led_owner_end(M1_LED_OWNER_CHARGING, 1);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_SD_EVENT, "SD event revealed after charging ends");
	CHECK(out.r_g_b == e_sd.r_g_b, "SD event's content survived");

	m1_led_owner_end(M1_LED_OWNER_SD_EVENT, 1);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_FEATURE_ACTIVITY, "activity revealed after SD event ends");
	CHECK(out.r_g_b == e_act.r_g_b, "activity's content survived being buried under four higher-priority owners");

	m1_led_owner_end(M1_LED_OWNER_FEATURE_ACTIVITY, 1);
	CHECK(m1_led_topmost(&out) == M1_LED_OWNER_NONE, "nothing left active once every owner has ended");
}

static void test_repeated_requests(void)
{
	m1_led_effect_t e1 = mk(0x01, 100, 50,  M1_LED_MODE_BLINK);
	m1_led_effect_t e2 = mk(0x02, 150, 75,  M1_LED_MODE_BLINK);
	m1_led_effect_t e3 = mk(0x04, 200, 0,   M1_LED_MODE_SOLID);
	uint8_t t1, t2, t3;
	m1_led_effect_t out;

	printf("test_repeated_requests\n");
	m1_led_priority_reset();

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e1, &t1);
	m1_led_topmost(&out);
	CHECK(out.r_g_b == e1.r_g_b && out.pwm == e1.pwm, "first repeated request is shown");

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e2, &t2);
	m1_led_topmost(&out);
	CHECK(out.r_g_b == e2.r_g_b && out.pwm == e2.pwm, "second repeated request fully replaces the first");
	CHECK(t2 != t1, "each repeated request gets a distinct token");

	m1_led_owner_begin(M1_LED_OWNER_FEATURE_ACTIVITY, &e3, &t3);
	m1_led_topmost(&out);
	CHECK(out.r_g_b == e3.r_g_b && out.mode == M1_LED_MODE_SOLID, "third repeated request (mode change too) replaces the second");
	CHECK(t3 != t2 && t3 != t1, "token keeps advancing across repeated requests");
}

static void test_idempotent_end_and_invalid_owner(void)
{
	m1_led_result_t r;

	printf("test_idempotent_end_and_invalid_owner\n");
	m1_led_priority_reset();

	r = m1_led_owner_end(M1_LED_OWNER_SD_EVENT, 0);
	CHECK(r == M1_LED_ACCEPTED, "ending a never-begun owner is a no-op success, not an error");

	r = m1_led_owner_begin(M1_LED_OWNER_NONE, NULL, NULL);
	CHECK(r == M1_LED_REJECTED_INVALID, "M1_LED_OWNER_NONE cannot itself hold a request");

	r = m1_led_owner_begin((m1_led_owner_t)999, NULL, NULL);
	CHECK(r == M1_LED_REJECTED_INVALID, "an out-of-range owner id is rejected");

	r = m1_led_owner_end((m1_led_owner_t)999, 0);
	CHECK(r == M1_LED_REJECTED_INVALID, "an out-of-range owner id is rejected on end() too");
}

int main(void)
{
	test_nothing_active_returns_none();
	test_priority_ordering();
	test_preemption_and_restoration();
	test_owner_cancellation_stale_token();
	test_saturation_no_overflow();
	test_repeated_requests();
	test_idempotent_end_and_invalid_owner();

	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
