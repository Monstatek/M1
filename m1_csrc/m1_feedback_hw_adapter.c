/* See COPYING.txt for license details. */

/*
*
* m1_feedback_hw_adapter.c
*
* fb_hw_adapter_t implementation binding the feedback manager to this
* codebase's current LED/backlight/buzzer drivers.
*
* PORT NOTE (v0.8.0.5 base): this file differs from the m1_01-main version of
* the same port. Between the old RC34 base and this v0.8.0.5 base, the LED
* driver grew its own independent owner/priority arbiter
* (m1_led_priority.[ch] / m1_led_indicator.[ch]) -- fixed slots for
* FEATURE_ACTIVITY < SD_EVENT < CHARGING < FW_UPDATE < CRITICAL, resolved by
* m1_led_topmost() and pushed to the LP5814 by m1_led_indicator.c's own
* m1_led_render(). m1_led_indicator.h explicitly documents
* m1_led_request()/m1_led_release() as "New explicit-ownership API, for
* Phase 2 (m1_feedback) and any new caller that wants a real token" -- i.e.
* this integration point was already designed for this exact port. Calling
* lp5814_led_on()/lp5814_led_off() directly here (as the RC34-base version
* of this file did) would bypass that arbiter entirely and reintroduce the
* exact "two things fighting over the LED" class of bug it was built to
* prevent, so this port instead registers the whole feedback manager as one
* M1_LED_OWNER_FEATURE_ACTIVITY slot.
*
* Disclosed behavioral consequence of that choice (not a silent gap):
* on the old RC34 base, m1_led_batt_charged_on()/m1_led_batt_full_on() used
* the CHARGING slot, ranked ABOVE SD_EVENT -- a charging/charged indication
* could not be interrupted by an SD-card event. Because m1_system.c's
* battery_indicator_update() is migrated by this same port to call
* fb_pwr_charging()/fb_pwr_charged() (see documentation), and the feedback
* manager's own internal arbitration (FB_OWNER_POWER/FB_PRIO_PWR_STATE vs.
* FB_OWNER_RADIO/etc.) all funnels through this ONE FEATURE_ACTIVITY slot,
* a Charging/Charged feedback-manager LED state can now be transiently
* preempted by an SD_EVENT (e.g. card insert/remove/fail) where it
* previously could not, for the duration of that self-terminating SD
* indication. This is a real, hardware-observable change in arbitration
* between two independent subsystems (battery LED vs. SD-card LED), not a
* feedback-manager-internal one -- it was not something the porting task
* specified either way, and giving the feedback manager its own new
* higher-priority m1_led_priority owner slot (to fully preserve the old
* Charging-over-SD-event protection) would be inventing a change to a
* different, unrelated arbiter's priority ordering that nothing in this
* task requested. Recorded here, in the reconciliation notes, and in the
* build MANIFEST as a disclosed integration decision pending hardware
* confirmation that this edge case (an SD event firing while the device is
* mid-charge) is acceptable; not silently accepted or silently avoided.
*
* Everything else (RGB channel mapping, backlight 0-100 -> 0-255 scaling,
* buzzer note frequencies) is unchanged in spirit from the m1_01-main
* version of this file; see documentation/M1_FEEDBACK_RECONCILIATION.md
* (reference section 16) for the original hardware-detail record.
*
* M1 Project
*
*/

#include "main.h"		/* pulls in m1_compile_cfg.h (M1_APP_BUZZER_USE_TIMER3/8) before
					 * m1_buzzer.h is ever entered as the first/outermost include of
					 * that circular header group -- same include-order requirement as
					 * every other m1_csrc source file (e.g. m1_menu.c, m1_buzzer.c). */
#include "m1_feedback_hw_adapter.h"
#include "m1_feedback_types.h"
#include "m1_led_indicator.h"
#include "m1_led_priority.h"
#include "m1_buzzer.h"
#include "m1_system.h"		/* LED_FASTBLINK_PWM_M */

/* Reference section 8: twelve-tone equal temperament, A4 = 440 Hz, rounded
 * to the nearest Hertz for the buzzer driver's uint16_t frequency
 * parameter. Indexed by fb_note_t. */
static const uint16_t NOTE_FREQ_HZ[FB_NOTE_COUNT] =
{
	/* Transposed up two octaves (x4) from concert pitch: the M1 piezo is a
	 * resonant element peaking near ~3.2 kHz and is nearly silent below ~2 kHz,
	 * so the original 262-1047 Hz notes were inaudible on hardware. The x4
	 * shift preserves every cue's relative melody while moving the notes into
	 * the audible/resonant band. Buzzer drive is fixed 50% duty with no
	 * amplitude control, so frequency is the only software loudness lever.
	 * (Efficacy pending on-device confirmation.) */
	[FB_NOTE_C4] = 1047,	/* 261.63 Hz x4 */
	[FB_NOTE_A4] = 1760,	/* 440.00 Hz x4 */
	[FB_NOTE_C5] = 2093,	/* 523.25 Hz x4 */
	[FB_NOTE_E5] = 2637,	/* 659.26 Hz x4 */
	[FB_NOTE_G5] = 3136,	/* 783.99 Hz x4 -- near piezo resonance */
	[FB_NOTE_C6] = 4186,	/* 1046.50 Hz x4 */
};

/* Single owner token for the entire feedback manager's LED output -- see
 * the file header note above for why this is one slot rather than several. */
static uint8_t s_fb_led_token = 0;

static void hw_led_write(uint8_t mask)
{
	m1_led_effect_t eff;

	if ( mask == 0 )
	{
		m1_led_release(M1_LED_OWNER_FEATURE_ACTIVITY, s_fb_led_token);
		return;
	}

	eff.r_g_b = 0;
	if ( mask & FB_LED_RED )
		eff.r_g_b |= LED_BLINK_ON_RED;
	if ( mask & FB_LED_GREEN )
		eff.r_g_b |= LED_BLINK_ON_GREEN;
	if ( mask & FB_LED_BLUE )
		eff.r_g_b |= LED_BLINK_ON_BLUE;

	/* The feedback manager itself already produces the blink cadence by
	 * toggling the mask on/off at each program step and re-calling this
	 * function -- from the arbiter's point of view every call here is "set
	 * a solid color right now", so mode is always SOLID; the manager's own
	 * on/off timing is what makes a LOOP program visually blink. */
	eff.pwm = LED_FASTBLINK_PWM_M;
	eff.on_off_ms = 0;
	eff.mode = M1_LED_MODE_SOLID;

	m1_led_request(M1_LED_OWNER_FEATURE_ACTIVITY, &eff, &s_fb_led_token);
} // static void hw_led_write(uint8_t mask)



static void hw_bl_write(uint8_t percent)
{
	uint8_t pwm;

	if ( percent > 100u )
		percent = 100u;

	/* Normalized 0-100 percent -> hardware 0-255 PWM range (reference
	 * section 4.2: "The hardware adapter converts that value to the actual
	 * timer compare range and clamps it safely."). */
	pwm = (uint8_t)(((uint16_t)percent * 255u + 50u) / 100u);
	lp5814_backlight_on(pwm);
} // static void hw_bl_write(uint8_t percent)



static void hw_snd_note(uint8_t note_id)
{
	if ( note_id >= FB_NOTE_COUNT )
		return;

	/*
	 * NOTE ON VOLUME: the buzzer driver drives the carrier timer at a fixed
	 * 50% PWM duty (see buzzer_sys_init()) and has no duty-cycle/amplitude
	 * control API. The reference's normalized 0-100 percent alert-volume
	 * range (section 4.3) is therefore only enforced as a MUTE gate today:
	 * fb_get_sound_enabled()/fb_get_alert_volume_pct() are consulted by the
	 * manager core itself (m1_feedback_manager.c) to decide whether a
	 * MSG_SND_NOTE reaches this callback at all, but a non-zero volume
	 * value does not yet change the buzzer's loudness. Recorded as a known,
	 * hardware-verification-pending gap -- implementing true duty-cycle-
	 * based volume scaling requires changes to buzzer_sys_init()'s PWM
	 * compare configuration and is out of scope for this pass.
	 */
	m1_buzzer_start_note(NOTE_FREQ_HZ[note_id]);
} // static void hw_snd_note(uint8_t note_id)



static void hw_snd_off(void)
{
	m1_buzzer_stop_note();
} // static void hw_snd_off(void)



static const fb_hw_adapter_t g_m1_feedback_hw_adapter =
{
	.led_write = hw_led_write,
	.bl_write  = hw_bl_write,
	.snd_note  = hw_snd_note,
	.snd_off   = hw_snd_off,
};

const fb_hw_adapter_t *m1_feedback_hw_adapter_get(void)
{
	return &g_m1_feedback_hw_adapter;
} // const fb_hw_adapter_t *m1_feedback_hw_adapter_get(void)
