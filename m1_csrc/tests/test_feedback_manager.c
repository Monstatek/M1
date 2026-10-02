/* See COPYING.txt for license details. */

/*
*
* test_feedback_manager.c
*
* Host-side unit tests for the M1 feedback manager (m1_feedback_manager.c /
* m1_feedback_sequences.c / m1_feedback_orchestration.c). Builds and runs
* entirely on the host with a fake hardware adapter and manually-advanced
* simulated time -- no FreeRTOS, no real clock, no hardware. Covers the
* twelve V01-V12 verification cases from M1-FB-STD-001 v1.1 section 15,
* plus the additional coverage requested in the implementation brief:
* message-array termination safety, malformed/unclosed loop handling,
* Sound-Off suppression with mandatory MSG_SND_OFF, same-priority
* replacement vs. drop rules, per-resource independent preemption, and
* cancellation mid-sequence for every message kind.
*
* Build (see also cmake/m1_01/CMakeLists.txt "feedback_manager_host_tests"
* target):
*   cc -std=c99 -DM1_FEEDBACK_HOST_TEST -I.. \
*      ../m1_feedback_manager.c ../m1_feedback_sequences.c \
*      ../m1_feedback_orchestration.c test_feedback_manager.c \
*      -o test_feedback_manager && ./test_feedback_manager
*
* M1 Project
*
*/

#include <stdio.h>
#include <string.h>
#include "m1_feedback_manager.h"
#include "m1_feedback_sequences.h"
#include "m1_feedback_orchestration.h"

/*************************** F A K E   H A R D W A R E ****************************/

static uint8_t  g_hw_led;
static uint8_t  g_hw_bl;
static bool     g_hw_snd_playing;
static uint8_t  g_hw_snd_note;
static uint32_t g_led_write_count;
static uint32_t g_bl_write_count;
static uint32_t g_snd_note_count;
static uint32_t g_snd_off_count;

static void fake_led_write(uint8_t mask)  { g_hw_led = mask; g_led_write_count++; }
static void fake_bl_write(uint8_t pct)    { g_hw_bl = pct; g_bl_write_count++; }
static void fake_snd_note(uint8_t note)   { g_hw_snd_playing = true; g_hw_snd_note = note; g_snd_note_count++; }
static void fake_snd_off(void)            { g_hw_snd_playing = false; g_snd_off_count++; }

static const fb_hw_adapter_t g_fake_hw =
{
	.led_write = fake_led_write,
	.bl_write  = fake_bl_write,
	.snd_note  = fake_snd_note,
	.snd_off   = fake_snd_off,
};

static void reset_manager(void)
{
	memset(&g_hw_led, 0, sizeof(g_hw_led));
	g_hw_bl = 0;
	g_hw_snd_playing = false;
	g_hw_snd_note = 0;
	g_led_write_count = g_bl_write_count = g_snd_note_count = g_snd_off_count = 0;
	fb_manager_init(&g_fake_hw);
} // static void reset_manager(void)

static void tick(uint32_t ms)
{
	fb_manager_tick(ms);
} // static void tick(uint32_t ms)

static void tick_n(uint32_t ms_each, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++)
		tick(ms_each);
} // static void tick_n(uint32_t ms_each, uint32_t n)

/*************************** T I N Y   T E S T   F R A M E W O R K ****************/

static int g_checks = 0;
static int g_failures = 0;
static const char *g_current_test = "";

#define CHECK(cond_) \
	do { \
		g_checks++; \
		if ( !(cond_) ) { \
			g_failures++; \
			printf("  FAIL [%s] %s:%d: %s\n", g_current_test, __FILE__, __LINE__, #cond_); \
		} \
	} while (0)

#define RUN_TEST(fn_) \
	do { \
		g_current_test = #fn_; \
		reset_manager(); \
		fn_(); \
	} while (0)

/*************************** V 0 1 - V 1 2 *****************************************/

/* V01: Start green background blink, then trigger low battery. Blink
 * suspends, low-battery pattern completes, blink resumes from its first
 * step. */
static void test_V01_background_loop_suspend_resume(void)
{
	fb_handle_t h = fb_request(SEQ_BLINK_GREEN_100, FB_OWNER_RADIO, FB_PRIO_BACKGROUND);
	CHECK(h != FB_HANDLE_INVALID);

	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_GREEN); /* first step: LED on */
	CHECK(fb_debug_led_loop_active());

	fb_pwr_batt_low(); /* SYSTEM_ALERT, priority 3 -- preempts BACKGROUND (6) */
	tick(1);
	CHECK(!fb_debug_led_loop_active());
	CHECK(fb_debug_led_loop_suspended());
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* low-battery pattern now visible */

	/* Low-battery LED pattern is 600 ms (3 x (100+100)); run it to
	 * completion. The suspended loop's resume registers a fresh instance
	 * during the finishing tick but that instance's first step (LED on)
	 * only executes on the *following* tick -- one more small tick makes
	 * the resumed state visible, exactly like any other newly-requested
	 * sequence. */
	tick_n(50, 13); /* 650 ms */
	tick(1);
	CHECK(!fb_debug_led_loop_suspended());
	CHECK(fb_debug_led_loop_active());
	CHECK(fb_debug_visible_led() == FB_LED_GREEN); /* resumed from its first step */
} // static void test_V01_background_loop_suspend_resume(void)

/* V02: Attach charger, then request Success. LED remains solid red; success
 * sound plays only when Sound is enabled. */
static void test_V02_charging_outranks_success_led(void)
{
	fb_set_sound_enabled(true);
	fb_pwr_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	fb_alert_success(FB_OWNER_NOTIFY);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* PWR_STATE(2) beats APP_ALERT(4) */
	CHECK(g_hw_snd_playing); /* sound still plays -- audio is independent of LED */
} // static void test_V02_charging_outranks_success_led(void)

/* V03: Complete charging while USB remains enumerated. LED changes from
 * solid red to solid green; it does not blink or change to cyan. */
static void test_V03_charge_complete_with_usb(void)
{
	fb_pwr_charging();
	fb_usb_connect();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	fb_pwr_charger_attached(true); /* charging -> charged */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_GREEN);
	CHECK(!fb_debug_led_loop_active());
} // static void test_V03_charge_complete_with_usb(void)

/* V04: Disconnect charger while USB remains enumerated. Power owner
 * releases and solid cyan USB state becomes visible. */
static void test_V04_charger_removed_with_usb(void)
{
	fb_pwr_charging();
	fb_usb_connect();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	fb_pwr_not_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
} // static void test_V04_charger_removed_with_usb(void)

/* V05: Disable Sound, then trigger every alert family. No note starts; LED
 * and backlight behavior remains correct. */
static void test_V05_sound_off_suppresses_every_family(void)
{
	fb_set_sound_enabled(false);

	fb_alert_success(FB_OWNER_NOTIFY);
	tick_n(10, 40);
	CHECK(!g_hw_snd_playing);
	CHECK(g_snd_note_count == 0u);

	fb_alert_error(FB_OWNER_NOTIFY);
	tick_n(10, 80);
	CHECK(g_snd_note_count == 0u);

	fb_alert_notify(FB_OWNER_NOTIFY);
	tick_n(10, 40);
	CHECK(g_snd_note_count == 0u);

	fb_pwr_batt_low();
	tick_n(10, 40);
	CHECK(g_snd_note_count == 0u);

	fb_pwr_boot();
	tick_n(10, 90);
	CHECK(g_snd_note_count == 0u);

	/* LED still ran (independent of Sound Off). */
	CHECK(g_led_write_count > 0u);
} // static void test_V05_sound_off_suppresses_every_family(void)

/* V06: Enable Sound and execute every audio sequence. Buzzer timer is idle
 * after every finite sequence. */
static void test_V06_every_audio_sequence_ends_idle(void)
{
	static const fb_seq_id_t finite_snd[] = {
		SEQ_SND_SUCCESS, SEQ_SND_ERROR, SEQ_SND_NOTIFY, SEQ_SND_BATT_LOW,
		SEQ_SND_BOOT, SEQ_SND_POWEROFF, SEQ_SND_BEEP, SEQ_SND_BEEP2,
		SEQ_SND_USB_IN, SEQ_SND_USB_OUT, SEQ_SND_TARGET,
	};
	uint32_t i;

	fb_set_sound_enabled(true);
	for (i = 0; i < sizeof(finite_snd) / sizeof(finite_snd[0]); i++)
	{
		fb_handle_t h = fb_request(finite_snd[i], FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
		CHECK(h != FB_HANDLE_INVALID);
		tick_n(10, 200); /* generously longer than the longest sequence (700 ms) */
		CHECK(fb_debug_snd_idle());
		CHECK(!g_hw_snd_playing);
	} // for (every finite audio sequence)
} // static void test_V06_every_audio_sequence_ends_idle(void)

/* V07: Cancel the critical-battery sequence by attaching a charger. Siren
 * stops immediately and charging state becomes solid red. */
static void test_V07_charger_cancels_critical_battery(void)
{
	fb_set_sound_enabled(true);
	fb_pwr_batt_crit();
	tick(1);
	CHECK(fb_debug_led_loop_active());
	CHECK(g_hw_snd_playing || g_snd_note_count > 0u);

	fb_pwr_charger_attached(false);
	tick(1);
	CHECK(!fb_debug_led_loop_active());
	CHECK(!g_hw_snd_playing);
	CHECK(fb_debug_visible_led() == FB_LED_RED);
} // static void test_V07_charger_cancels_critical_battery(void)

/* V08: Fault an application while emulation, payload, or backlight hold is
 * active. All resources owned by that application release without
 * rebooting. */
static void test_V08_fault_releases_owned_resources(void)
{
	fb_net_emulating_start(); /* LED hold + BL hold, owner RADIO */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* PR2: pulse blue, was flashing magenta */
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());

	fb_cleanup_owner(FB_OWNER_RADIO); /* fault path */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
	CHECK(fb_debug_visible_bl() == 0u);
} // static void test_V08_fault_releases_owned_resources(void)

/* V09: Trigger USB activity for 30 seconds. LED remains solid cyan when
 * eligible; no flashing occurs. */
static void test_V09_usb_activity_no_flash(void)
{
	fb_usb_connect();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);

	/* fb_usb_activity() is an intentional no-op; simulate 30 s of periodic
	 * activity calls and confirm the LED never changes. */
	{
		uint32_t i;
		for (i = 0; i < 30; i++)
		{
			fb_usb_activity();
			tick_n(100, 10); /* 1 s */
			CHECK(fb_debug_visible_led() == FB_LED_CYAN);
			CHECK(!fb_debug_led_loop_active());
		} // for (30 seconds, 1 s steps)
	}
} // static void test_V09_usb_activity_no_flash(void)

/* V10: Run each 10 ms flash under normal system load. On the host this
 * verifies logical timing (exact tick-for-tick delay accounting), which is
 * the manager's contribution to Section 14 tolerance; real scheduling
 * jitter can only be measured on hardware (recorded as hardware
 * verification pending). */
static void test_V10_10ms_flash_logical_timing(void)
{
	fb_handle_t h = fb_request(SEQ_FLASH_RED_10, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);
	tick(9); /* 10 ms total */
	CHECK(fb_debug_visible_led() != FB_LED_RED); /* restored already */
} // static void test_V10_10ms_flash_logical_timing(void)

/* V11: Start one-shot feedback, then change charger state before
 * restoration. Restoration recomputes ownership and does not overwrite the
 * new power state. */
static void test_V11_restore_does_not_overwrite_new_state(void)
{
	fb_handle_t h = fb_request(SEQ_FLASH_BLUE_100, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);

	/* Charging is a higher-priority hold; while the flash is still in
	 * flight it is registered but not yet visible. */
	fb_pwr_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* preempted immediately */

	tick_n(10, 20); /* let the (preempted, now-gone) flash's nominal duration pass */
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* restore did not resurrect blue */
} // static void test_V11_restore_does_not_overwrite_new_state(void)

/* V12: Call cleanup twice for the same owner. Second call is harmless and
 * outputs remain correct. */
static void test_V12_cleanup_idempotent(void)
{
	fb_request(SEQ_SOLID_BLUE, FB_OWNER_PAYLOAD, FB_PRIO_PERSISTENT);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);

	fb_cleanup_owner(FB_OWNER_PAYLOAD);
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	fb_cleanup_owner(FB_OWNER_PAYLOAD); /* must be a harmless no-op */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_V12_cleanup_idempotent(void)

/*************************** A D D I T I O N A L   C O V E R A G E ****************/

static void test_msg_end_termination_safety(void)
{
	static const fb_msg_t prog[] = { FB_MSG(FB_MSG_LED_SET, FB_LED_YELLOW), FB_END };
	fb_handle_t h = fb_request_raw(prog, FB_RES_LED, FB_OWNER_DIAGNOSTIC, FB_PRIO_ADMIN, FB_KIND_ONESHOT, SEQ_ID_COUNT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_OFF); /* one-shot ran and released */
} // static void test_msg_end_termination_safety(void)

static void test_malformed_unclosed_loop_does_not_hang(void)
{
	/* MSG_LOOP_BEGIN with no matching MSG_LOOP_END and no MSG_DELAY inside
	 * it: the per-tick step guard (FB_STEP_GUARD) must stop forward
	 * progress for one tick rather than spinning forever. */
	static const fb_msg_t prog[] = {
		FB_MSG(FB_MSG_LOOP_BEGIN, 0),
		FB_MSG(FB_MSG_LED_ADD, FB_LED_RED),
		FB_END, /* unreachable while the malformed loop spins */
	};
	fb_handle_t h = fb_request_raw(prog, FB_RES_LED, FB_OWNER_DIAGNOSTIC, FB_PRIO_ADMIN, FB_KIND_LOOP, SEQ_ID_COUNT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1); /* must return promptly */
	tick(1);
	CHECK(true); /* reaching here at all proves the tick did not hang */

	/* Also exercise an unmatched MSG_LOOP_END (no MSG_LOOP_BEGIN): must be
	 * a safe no-op, not a program-counter corruption. */
	{
		static const fb_msg_t prog2[] = {
			FB_MSG(FB_MSG_LOOP_END, 0),
			FB_MSG(FB_MSG_LED_SET, FB_LED_GREEN),
			FB_END,
		};
		fb_handle_t h2 = fb_request_raw(prog2, FB_RES_LED, FB_OWNER_DIAGNOSTIC, FB_PRIO_ADMIN, FB_KIND_ONESHOT, SEQ_ID_COUNT);
		CHECK(h2 != FB_HANDLE_INVALID);
		tick(1);
		CHECK(fb_debug_visible_led() == FB_LED_OFF); /* ran to completion and released */
	}
} // static void test_malformed_unclosed_loop_does_not_hang(void)

static void test_sound_off_note_suppressed_but_snd_off_always_runs(void)
{
	fb_set_sound_enabled(false);
	fb_request(SEQ_SND_BEEP, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	tick_n(10, 10);
	CHECK(g_snd_note_count == 0u);       /* the note itself never reached hardware */
	CHECK(g_snd_off_count > 0u);         /* but MSG_SND_OFF still executed          */
} // static void test_sound_off_note_suppressed_but_snd_off_always_runs(void)

static void test_zero_volume_also_suppresses_notes(void)
{
	fb_set_sound_enabled(true);
	fb_set_alert_volume_pct(0);
	fb_request(SEQ_SND_BEEP, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	tick_n(10, 10);
	CHECK(g_snd_note_count == 0u);
} // static void test_zero_volume_also_suppresses_notes(void)

static void test_same_priority_same_owner_replaces(void)
{
	fb_request(SEQ_SOLID_RED, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	fb_request(SEQ_SOLID_GREEN, FB_OWNER_POWER, FB_PRIO_PWR_STATE); /* same owner, same class */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_GREEN);
} // static void test_same_priority_same_owner_replaces(void)

static void test_same_priority_different_owner_does_not_replace(void)
{
	fb_request(SEQ_SOLID_RED, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	/* A different owner at the *same* priority class must not preempt an
	 * existing owner's persistent hold (reference section 6, rule 6). */
	fb_handle_t h = fb_request(SEQ_SOLID_GREEN, FB_OWNER_DIAGNOSTIC, FB_PRIO_PWR_STATE);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);
	/* The hold is still bookkept even though not visible (rule 8). */
	CHECK(h != FB_HANDLE_INVALID);
} // static void test_same_priority_different_owner_does_not_replace(void)

static void test_lower_priority_oneshot_dropped_not_replayed(void)
{
	fb_request(SEQ_SOLID_RED, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
	tick(1);

	fb_handle_t h = fb_request(SEQ_FLASH_GREEN_10, FB_OWNER_NOTIFY, FB_PRIO_BACKGROUND);
	CHECK(h == FB_HANDLE_INVALID); /* dropped: BACKGROUND(6) cannot preempt PWR_STATE(2) */
	tick_n(1, 20);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* never replayed */
} // static void test_lower_priority_oneshot_dropped_not_replayed(void)

static void test_per_resource_independent_preemption(void)
{
	/* An LED hold at PWR_STATE must not block a lower-priority-class
	 * request on the *backlight* resource -- arbitration is independent
	 * per resource (reference section 6). */
	fb_request(SEQ_SOLID_RED, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);

	/* A HOLD (not a bare one-shot): a one-shot's defining behavior is to
	 * restore whatever it temporarily changed once it ends (reference
	 * section 2, "One-shot"), so a persistent value belongs in a HOLD. */
	static const fb_msg_t bl_prog[] = { FB_MSG(FB_MSG_BL_SET, 42), FB_END };
	fb_handle_t h = fb_request_raw(bl_prog, FB_RES_BL, FB_OWNER_RADIO, FB_PRIO_BACKGROUND, FB_KIND_HOLD, SEQ_ID_COUNT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1);
	CHECK(fb_debug_visible_bl() == 42u);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* unaffected */
} // static void test_per_resource_independent_preemption(void)

static void test_cancellation_mid_hold(void)
{
	fb_request(SEQ_SOLID_CYAN, FB_OWNER_USB, FB_PRIO_PERSISTENT);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	fb_release(FB_OWNER_USB, FB_RES_LED);
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_cancellation_mid_hold(void)

static void test_cancellation_mid_loop(void)
{
	fb_request(SEQ_BLINK_RED_10, FB_OWNER_RADIO, FB_PRIO_BACKGROUND);
	tick(1);
	CHECK(fb_debug_led_loop_active());
	fb_cleanup_owner(FB_OWNER_RADIO);
	CHECK(!fb_debug_led_loop_active());
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_cancellation_mid_loop(void)

static void test_cancellation_mid_oneshot(void)
{
	fb_handle_t h = fb_request(SEQ_FLASH_WHITE_100, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	CHECK(h != FB_HANDLE_INVALID);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	fb_cleanup_owner(FB_OWNER_NOTIFY); /* cancel before the flash's D(100) elapses */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
	tick_n(10, 20);
	CHECK(fb_debug_visible_led() == FB_LED_OFF); /* the cancelled flash never resurfaces */
} // static void test_cancellation_mid_oneshot(void)

static void test_cancellation_mid_note(void)
{
	fb_set_sound_enabled(true);
	fb_request(SEQ_SND_BOOT, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT); /* 700 ms sequence */
	tick(1);
	CHECK(g_hw_snd_playing);
	fb_cleanup_owner(FB_OWNER_POWER);
	CHECK(!g_hw_snd_playing);
	CHECK(fb_debug_snd_idle());
} // static void test_cancellation_mid_note(void)

/*************************** P R 2   S U P E R S E D I N G   P A L E T T E ********/
/*
 * Coverage for the superseding interaction palette (see documentation/
 * M1_FEEDBACK_RECONCILIATION.md, "Superseding interaction palette (PR2)"),
 * which replaces the prior pass's user-authored nine-state mapping
 * (flashing cyan/yellow/magenta/red) for Detect/Read/Emulate/Replay, adds
 * the two new Write and Firmware-update states, and leaves Success/Error/
 * Charging/Charged/USB unchanged.
 */

/* N01: Detect/Listen is slow flashing blue, 500 ms on / 500 ms off -- a
 * markedly slower cadence than Read's fast flash (verified against Read
 * directly in N08) -- and the field-detect compatibility wrapper drives the
 * exact same behavior. */
static void test_N01_detect_listen_slow_flash_blue(void)
{
	fb_net_detect_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active());
	tick(499); /* 500 ms total: on-phase elapses, off-phase begins */
	CHECK(fb_debug_visible_led() != FB_LED_BLUE);
	tick(500); /* another full 500/500 on/off period */
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	fb_net_detect_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	/* Compatibility wrapper: identical behavior, not a second implementation. */
	fb_net_field_detect_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active());
	fb_net_field_detect_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_N01_detect_listen_slow_flash_blue(void)

/* N02: Read is fast flashing white, 100 ms on / 100 ms off. */
static void test_N02_read_fast_flash_white(void)
{
	fb_net_read_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	CHECK(fb_debug_led_loop_active());
	tick(99);
	CHECK(fb_debug_visible_led() != FB_LED_WHITE);
	tick(100);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	fb_net_read_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
	/* Idempotent: calling stop again (never started, or already stopped)
	 * must be harmless. */
	fb_net_read_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_N02_read_fast_flash_white(void)

/* N03: Emulate is pulse blue (1000 ms on / 400 ms off, the closest
 * non-blocking approximation of a brightness ramp -- see the sequence
 * catalogue's "Pulsing implementation note" for why a true PWM ramp was not
 * used) -- AND the backlight hold at user brightness survives the
 * colour/cadence change unmodified (still FB_KIND_HOLD, still released only
 * by fb_net_emulating_stop() / fb_cleanup_owner()). */
static void test_N03_emulate_pulse_blue_backlight_hold_survives(void)
{
	fb_sleep_timer_wake(); /* normal awake baseline is established at boot */
	tick(1);
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());

	fb_net_emulating_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active()); /* pulsing, not a bare hold */
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct()); /* backlight hold intact */

	tick(999); /* 1000 ms total: on-phase elapses, off-phase begins */
	CHECK(fb_debug_visible_led() != FB_LED_BLUE);
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());

	tick(400); /* 1400 ms total: full pulse period, back on */
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());

	fb_net_emulating_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());
	/* Repeated cleanup cannot release another owner's awake baseline. */
	fb_net_emulating_stop();
	CHECK(fb_debug_visible_bl() == fb_get_brightness_pct());
} // static void test_N03_emulate_pulse_blue_backlight_hold_survives(void)

/* N04: Replay/Transmit is alternating blue and white, 150 ms each colour
 * (equal dwell for both). */
static void test_N04_replay_transmit_alternating_blue_white(void)
{
	fb_net_replay_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active());
	tick(149); /* 150 ms total: switches to white */
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	tick(150); /* 300 ms total: one full period, back to blue */
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	fb_net_replay_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_N04_replay_transmit_alternating_blue_white(void)

/* N05: Replay/Transmit must not stomp on solid-red Charging
 * (Charging is FB_PRIO_PWR_STATE=2, Replay is FB_PRIO_PERSISTENT=5), and
 * Replay must correctly reappear via logical restore once Charging
 * releases -- the same pattern already verified for Emulation-vs-Charging
 * (V02) and USB-vs-charging (V03/V04). */
static void test_N05_replay_vs_charging_priority_and_restore(void)
{
	fb_net_replay_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active());

	fb_pwr_charging(); /* higher priority */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* Charging's solid red preempts */
	CHECK(!fb_debug_led_loop_active()); /* Replay's loop is suspended, not visible */

	fb_pwr_not_charging(); /* Charging releases */
	tick(1); /* resume registers this tick; per V01, a freshly-resumed loop's
	          * own first step is deferred to the *next* tick to avoid
	          * double-counting elapsed time (see m1_feedback_manager.c) */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Replay reappears via logical restore */
	CHECK(fb_debug_led_loop_active());
} // static void test_N05_replay_vs_charging_priority_and_restore(void)

/* N06: Slow-flashing-blue Detect/Listen must not stomp on solid-cyan USB
 * connected (Detect is FB_PRIO_BACKGROUND=6, USB is FB_PRIO_PERSISTENT=5),
 * and Detect must correctly reappear once USB disconnects. */
static void test_N06_detect_vs_usb_priority_and_restore(void)
{
	fb_net_detect_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	CHECK(fb_debug_led_loop_active());

	fb_usb_connect(); /* higher priority */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN); /* USB's solid cyan preempts */
	CHECK(!fb_debug_led_loop_active()); /* Detect's flashing loop is suspended */

	fb_usb_disconnect();
	tick(1); /* resume deferral, see N05 */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Detect reappears via logical restore */
	CHECK(fb_debug_led_loop_active());
} // static void test_N06_detect_vs_usb_priority_and_restore(void)

/* N07: Success/Error/Charging/Charged remain exactly as before (not
 * touched by PR2's activity-state changes) -- brief verification alongside
 * the new states, per the task's "verify, don't break" requirement for
 * these five identifiers. */
static void test_N07_success_error_charging_charged_unchanged(void)
{
	fb_alert_success(FB_OWNER_NOTIFY);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_GREEN); /* brief green */
	CHECK(g_hw_snd_playing); /* plus success sound */
	tick_n(10, 20); /* let the brief flash complete */
	CHECK(fb_debug_visible_led() != FB_LED_GREEN);

	fb_alert_error(FB_OWNER_NOTIFY);
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* brief red */
	CHECK(g_hw_snd_playing); /* plus error sound */

	fb_pwr_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* solid red */
	CHECK(!fb_debug_led_loop_active());

	fb_pwr_charged();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_GREEN); /* solid green */
	CHECK(!fb_debug_led_loop_active());
} // static void test_N07_success_error_charging_charged_unchanged(void)

/* N08: Write is double-flash white -- two quick 80 ms flashes with an 80 ms
 * gap, then a 600 ms pause, repeating -- and this shape is visibly distinct
 * from a simple single flash (SEQ_BLINK_WHITE_100/Read's fast flash), not
 * just a different timing number. A simple 100/100 flash would already be
 * back ON by t=200; double-flash is still in its long pause at that point
 * and does not turn back on until t=840 (two 80/80 flashes plus a 600 ms
 * pause). Also demonstrates the cadence is visibly distinct from Read's
 * fast flash (100/100) and Detect's slow flash (500/500). */
static void test_N08_write_double_flash_white(void)
{
	fb_net_write_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE); /* first flash */
	CHECK(fb_debug_led_loop_active());

	tick(79); /* t=80: first flash ends */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	tick(79); /* t=159: still in the 80 ms gap between flashes */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	tick(1); /* t=160: second flash begins -- a simple 100/100 flash would
	          * already be approaching its *next* on-phase here, not
	          * starting a *second* flash within the same double-pulse. */
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);

	tick(79); /* t=239: still the second flash */
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);

	tick(1); /* t=240: second flash ends, long 600 ms pause begins */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	tick(599); /* t=839: still paused -- proves the pause is long, not a
	            * simple flash's short off-phase */
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	tick(1); /* t=840: pattern repeats, first flash of the next cycle */
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);

	fb_net_write_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_N08_write_double_flash_white(void)

/* N09: Write vs. Charging arbitration, mirroring N05's coverage of Replay --
 * Write is also FB_PRIO_PERSISTENT, so it must be outranked by solid-red
 * Charging (FB_PRIO_PWR_STATE=2) and correctly resume via logical restore
 * once Charging releases. */
static void test_N09_write_vs_charging_priority_and_restore(void)
{
	fb_net_write_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	CHECK(fb_debug_led_loop_active());

	fb_pwr_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);
	CHECK(!fb_debug_led_loop_active());

	fb_pwr_not_charging();
	tick(1); /* resume deferral, see N05 */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_WHITE);
	CHECK(fb_debug_led_loop_active());

	fb_net_write_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);
} // static void test_N09_write_vs_charging_priority_and_restore(void)

/* N10: Firmware update (the exposed fb_fw_update_start()/_stop() API -- NOT
 * wired to the real protected/non-overridable in-progress-flash indication,
 * see the disclosed integration gap in m1_feedback_orchestration.h) is
 * alternating cyan and magenta, 200 ms each colour, at FB_PRIO_SYSTEM_ALERT
 * -- high enough to preempt every radio/protocol activity state (Detect/
 * Read/Emulate/Write/Replay, all FB_PRIO_BACKGROUND or FB_PRIO_PERSISTENT)
 * and USB-connected (FB_PRIO_PERSISTENT), resuming each via logical restore
 * once the update finishes. Also verifies that, purely within the feedback
 * manager's own internal priority table, this exposed API does NOT outrank
 * Charging/Charged (FB_PRIO_PWR_STATE=2 numerically outranks
 * FB_PRIO_SYSTEM_ALERT=3) -- irrelevant to the real protected flash
 * indication (which never goes through this API at all), but correct and
 * worth verifying for whatever future caller does use this exposed API. */
static void test_N10_firmware_update_alternating_cyan_magenta_and_preempts(void)
{
	/* Cadence and colours. */
	fb_fw_update_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	CHECK(fb_debug_led_loop_active());
	tick(199); /* 200 ms total: switches to magenta */
	CHECK(fb_debug_visible_led() == FB_LED_MAGENTA);
	tick(200); /* 400 ms total: one full period, back to cyan */
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	fb_fw_update_stop();
	CHECK(fb_debug_visible_led() == FB_LED_OFF);

	/* Preempts Detect (FB_PRIO_BACKGROUND). */
	fb_net_detect_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE);
	fb_fw_update_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	CHECK(fb_debug_led_loop_suspended()); /* Detect's slow flash suspended, not dropped */
	fb_fw_update_stop();
	tick(1); /* resume deferral, see N05 */
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Detect resumes via logical restore */
	fb_net_detect_stop();

	/* Preempts USB-connected (FB_PRIO_PERSISTENT) -- proven by observing the
	 * magenta half of the alternating cycle, which USB's static solid-cyan
	 * hold would never show. */
	fb_usb_connect();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	CHECK(!fb_debug_led_loop_active()); /* USB hold, not a loop */
	fb_fw_update_start();
	tick(200); /* fresh LOOP request (not a resume): crosses the 200 ms
	            * cyan->magenta threshold directly */
	CHECK(fb_debug_visible_led() == FB_LED_MAGENTA); /* proves the FW-update loop is running */
	fb_fw_update_stop();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_CYAN);
	CHECK(!fb_debug_led_loop_active()); /* USB's solid hold resumed, not a loop */
	fb_usb_disconnect();

	/* Disclosed design choice: Firmware update does NOT outrank Charging. */
	fb_pwr_charging();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED);
	fb_fw_update_start();
	tick(1);
	CHECK(fb_debug_visible_led() == FB_LED_RED); /* Charging still wins -- by design, disclosed */
	fb_fw_update_stop();
	fb_pwr_not_charging();
} // static void test_N10_firmware_update_alternating_cyan_magenta_and_preempts(void)

/* N11: Explicit cadence-distinctness assertions across the whole palette --
 * every continuous LED state must be told apart from every other by its
 * timing/shape, not just by a color a viewer might not immediately place.
 * (Colours are already exercised individually by N01-N06, N08, N10; this
 * test focuses purely on distinguishing timing/period/shape.) */
static void test_N11_cadence_distinctness_across_palette(void)
{
	/* Detect (500/500, period 1000) vs Read (100/100, period 200): at
	 * t=100 ms, Detect must still be in its first on-phase while Read must
	 * already have completed a full on/off cycle and be back on for its
	 * second on-phase. */
	fb_net_detect_start();
	tick(100);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Detect: still in the 500 ms on-phase */
	fb_net_detect_stop();

	fb_net_read_start();
	tick(100);
	CHECK(fb_debug_visible_led() != FB_LED_WHITE); /* Read: first 100 ms on-phase JUST ended */
	fb_net_read_stop();

	/* Pulse (1000/400, period 1400) vs Detect (500/500, period 1000): at
	 * t=600 ms, Pulse must still be in its long on-phase while Detect would
	 * already be into its second on/off cycle. */
	fb_net_emulating_start();
	tick(600);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Pulse: still in the 1000 ms on-phase */
	fb_net_emulating_stop();

	/* Write's double-flash (two 80 ms pulses + 600 ms pause, period 840) vs
	 * a simple flash: at t=300 ms, Write must be in its long pause (which
	 * runs from t=240 to t=840) while a same-family simple flash would have
	 * completed 1.5 further on/off cycles by then. */
	fb_net_write_start();
	tick(300);
	CHECK(fb_debug_visible_led() == FB_LED_OFF); /* Write: mid long-pause, not toggling */
	fb_net_write_stop();

	/* Replay (blue/white, period 300) vs Firmware update (cyan/magenta,
	 * period 400): both alternate, but at t=350 ms Replay has completed one
	 * full period plus 50 ms (back into its blue half) while Firmware
	 * update is still in its first cyan half (200 ms) -- proving the two
	 * alternating states are distinguishable by period, independent of
	 * their (already-distinct) colours. */
	fb_net_replay_start();
	tick(350);
	CHECK(fb_debug_visible_led() == FB_LED_BLUE); /* Replay: into its 2nd period's blue half */
	fb_net_replay_stop();

	fb_fw_update_start();
	tick(350);
	CHECK(fb_debug_visible_led() == FB_LED_MAGENTA); /* FW update: still in its 1st period */
	fb_fw_update_stop();
} // static void test_N11_cadence_distinctness_across_palette(void)

/*************************** M A I N ***********************************************/

int main(void)
{
	RUN_TEST(test_V01_background_loop_suspend_resume);
	RUN_TEST(test_V02_charging_outranks_success_led);
	RUN_TEST(test_V03_charge_complete_with_usb);
	RUN_TEST(test_V04_charger_removed_with_usb);
	RUN_TEST(test_V05_sound_off_suppresses_every_family);
	RUN_TEST(test_V06_every_audio_sequence_ends_idle);
	RUN_TEST(test_V07_charger_cancels_critical_battery);
	RUN_TEST(test_V08_fault_releases_owned_resources);
	RUN_TEST(test_V09_usb_activity_no_flash);
	RUN_TEST(test_V10_10ms_flash_logical_timing);
	RUN_TEST(test_V11_restore_does_not_overwrite_new_state);
	RUN_TEST(test_V12_cleanup_idempotent);

	RUN_TEST(test_msg_end_termination_safety);
	RUN_TEST(test_malformed_unclosed_loop_does_not_hang);
	RUN_TEST(test_sound_off_note_suppressed_but_snd_off_always_runs);
	RUN_TEST(test_zero_volume_also_suppresses_notes);
	RUN_TEST(test_same_priority_same_owner_replaces);
	RUN_TEST(test_same_priority_different_owner_does_not_replace);
	RUN_TEST(test_lower_priority_oneshot_dropped_not_replayed);
	RUN_TEST(test_per_resource_independent_preemption);
	RUN_TEST(test_cancellation_mid_hold);
	RUN_TEST(test_cancellation_mid_loop);
	RUN_TEST(test_cancellation_mid_oneshot);
	RUN_TEST(test_cancellation_mid_note);

	RUN_TEST(test_N01_detect_listen_slow_flash_blue);
	RUN_TEST(test_N02_read_fast_flash_white);
	RUN_TEST(test_N03_emulate_pulse_blue_backlight_hold_survives);
	RUN_TEST(test_N04_replay_transmit_alternating_blue_white);
	RUN_TEST(test_N05_replay_vs_charging_priority_and_restore);
	RUN_TEST(test_N06_detect_vs_usb_priority_and_restore);
	RUN_TEST(test_N07_success_error_charging_charged_unchanged);
	RUN_TEST(test_N08_write_double_flash_white);
	RUN_TEST(test_N09_write_vs_charging_priority_and_restore);
	RUN_TEST(test_N10_firmware_update_alternating_cyan_magenta_and_preempts);
	RUN_TEST(test_N11_cadence_distinctness_across_palette);

	printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
} // int main(void)
