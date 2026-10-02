/* See COPYING.txt for license details. */

/*
*
* m1_feedback_orchestration.c
*
* Implements reference sections 9-12. See m1_feedback_orchestration.h for
* the design note on "composite elements start together" and
* documentation/M1_FEEDBACK_RECONCILIATION.md for the full identifier
* mapping, including the priority-class choices made for identifiers the
* reference's section-6 example table does not explicitly enumerate
* (radio field-detect/hopping, USB chime, settings previews).
*
* M1 Project
*
*/

#include <stddef.h>
#include "m1_feedback_orchestration.h"
#include "m1_feedback_sequences.h"

/*************************** 9 .   C O M P O S I T E   A L E R T S ****************/

/* Bounded LED pattern shared by Error/Notify (reference section 9):
 * SAVE_LED, LOOP(count), LED(colour), D(100), LED(LED_OFF), D(100),
 * LOOP_END, RESTORE_LED, END. */
static const fb_msg_t SEQ_PROG_ALERT_ERROR_LED[] = {
	FB_MSG(FB_MSG_LED_SAVE, 0),
	FB_MSG(FB_MSG_LOOP_BEGIN, 3),
	FB_MSG(FB_MSG_LED_SET, FB_LED_RED), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_MSG(FB_MSG_LED_RESTORE, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_ALERT_NOTIFY_LED[] = {
	FB_MSG(FB_MSG_LED_SAVE, 0),
	FB_MSG(FB_MSG_LOOP_BEGIN, 2),
	FB_MSG(FB_MSG_LED_SET, FB_LED_MAGENTA), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_MSG(FB_MSG_LED_RESTORE, 0),
	FB_END,
};

/* "Wake to user value" backlight legs. Duration matches each alert's LED
 * runtime so the wake and the LED effect complete together; only the
 * standard delay values from reference section 4.4 are used. */
static const fb_msg_t SEQ_PROG_ALERT_SUCCESS_BL[] = {
	FB_MSG(FB_MSG_BL_SAVE, 0), FB_MSG(FB_MSG_BL_USER, 0),
	FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_BL_RESTORE, 0), FB_END,
};

static const fb_msg_t SEQ_PROG_ALERT_ERROR_BL[] = {
	FB_MSG(FB_MSG_BL_SAVE, 0), FB_MSG(FB_MSG_BL_USER, 0),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_BL_RESTORE, 0), FB_END,
};

static const fb_msg_t SEQ_PROG_ALERT_NOTIFY_BL[] = {
	FB_MSG(FB_MSG_BL_SAVE, 0), FB_MSG(FB_MSG_BL_USER, 0),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_BL_RESTORE, 0), FB_END,
};

void fb_alert_success(fb_owner_t owner)
{
	(void)fb_request_raw(fb_seq_prog(SEQ_FLASH_GREEN_100), FB_RES_LED, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_SUCCESS);
	(void)fb_request_raw(SEQ_PROG_ALERT_SUCCESS_BL, FB_RES_BL, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_SUCCESS);
	(void)fb_request(SEQ_SND_SUCCESS, owner, FB_PRIO_APP_ALERT);
} // void fb_alert_success(fb_owner_t owner)

void fb_alert_error(fb_owner_t owner)
{
	(void)fb_request_raw(SEQ_PROG_ALERT_ERROR_LED, FB_RES_LED, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_ERROR);
	(void)fb_request_raw(SEQ_PROG_ALERT_ERROR_BL, FB_RES_BL, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_ERROR);
	(void)fb_request(SEQ_SND_ERROR, owner, FB_PRIO_APP_ALERT);
} // void fb_alert_error(fb_owner_t owner)

void fb_alert_notify(fb_owner_t owner)
{
	(void)fb_request_raw(SEQ_PROG_ALERT_NOTIFY_LED, FB_RES_LED, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_NOTIFY);
	(void)fb_request_raw(SEQ_PROG_ALERT_NOTIFY_BL, FB_RES_BL, owner, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_ALERT_NOTIFY);
	(void)fb_request(SEQ_SND_NOTIFY, owner, FB_PRIO_APP_ALERT);
} // void fb_alert_notify(fb_owner_t owner)

void fb_alert_beep(fb_owner_t owner)
{
	/* "None" for LED/backlight (reference section 9) -- SND only. */
	(void)fb_request(SEQ_SND_BEEP, owner, FB_PRIO_APP_ALERT);
} // void fb_alert_beep(fb_owner_t owner)

void fb_alert_beep2(fb_owner_t owner)
{
	(void)fb_request(SEQ_SND_BEEP2, owner, FB_PRIO_APP_ALERT);
} // void fb_alert_beep2(fb_owner_t owner)

/*************************** 1 0 .   P O W E R / L I F E C Y C L E ****************/

/* Blue 100 ms, cyan 100 ms, green 100 ms, then off. */
static const fb_msg_t SEQ_PROG_PWR_BOOT_LED[] = {
	FB_MSG(FB_MSG_LED_SET, FB_LED_BLUE),  FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_CYAN),  FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_GREEN), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),
	FB_END,
};

static const fb_msg_t SEQ_PROG_PWR_OFF_LED[] = {
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),
	FB_END,
};

static const fb_msg_t SEQ_PROG_PWR_OFF_BL[] = {
	FB_MSG(FB_MSG_BL_OFF, 0),
	FB_END,
};

/* SAVE_LED, LOOP(3), LED(LED_RED), D(100), LED(LED_OFF), D(100), LOOP_END,
 * RESTORE_LED, END -- reference section 10.1, exact sequence. */
static const fb_msg_t SEQ_PROG_PWR_BATT_LOW_LED[] = {
	FB_MSG(FB_MSG_LED_SAVE, 0),
	FB_MSG(FB_MSG_LOOP_BEGIN, 3),
	FB_MSG(FB_MSG_LED_SET, FB_LED_RED), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_MSG(FB_MSG_LED_RESTORE, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_PWR_BATT_LOW_BL[] = {
	FB_MSG(FB_MSG_BL_SAVE, 0), FB_MSG(FB_MSG_BL_USER, 0),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_BL_RESTORE, 0), FB_END,
};

/* Continuous: LOOP(0), LED(LED_RED), D(100), LED(LED_OFF), D(100), LOOP_END, END. */
static const fb_msg_t SEQ_PROG_PWR_BATT_CRIT_LED[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_LED_SET, FB_LED_RED), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF), FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_PWR_BATT_CRIT_BL[] = {
	FB_MSG(FB_MSG_BL_HOLD_USER, 0),
	FB_END,
};

void fb_pwr_boot(void)
{
	/*
	 * Backlight: reference section 10 lists "User brightness" for
	 * SEQ_PWR_BOOT with no explicit release/end condition, i.e. it is meant
	 * to become the new steady state, not a temporary effect. A one-shot
	 * that sets a value and then hits MSG_END immediately restores the
	 * prior baseline (that is the manager's whole definition of "one-shot"
	 * -- reference section 2), so registering a bare BL_USER,END one-shot
	 * here would set the backlight and then instantly release it again
	 * within the same tick, which is a no-op, not "wake to user
	 * brightness." The correct persistent owner of the normal-operation
	 * backlight baseline is the LCD sleep timer's own low-priority hold
	 * (fb_sleep_timer_wake()/_sleep(), FB_OWNER_SLEEP_TIMER) -- see
	 * m1_system.c's lcd_saver_update(), seeded during feedback-task startup
	 * and refreshed whenever the welcome/status screen wakes the display.
	 * No separate boot-specific owner is needed because the sleep-timer owner
	 * is already the authoritative normal-awake baseline.
	 */
	(void)fb_request_raw(SEQ_PROG_PWR_BOOT_LED, FB_RES_LED, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT, FB_KIND_ONESHOT, SEQ_PWR_BOOT);
	(void)fb_request(SEQ_SND_BOOT, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT);
} // void fb_pwr_boot(void)

void fb_pwr_shutdown(void)
{
	/* Reference section 10.2: shutdown performs the critical-battery/charger
	 * cleanup before the power-off sequence. */
	fb_release(FB_OWNER_POWER, FB_RES_LED);
	fb_release(FB_OWNER_POWER, FB_RES_BL);
	fb_release(FB_OWNER_POWER, FB_RES_SND);

	(void)fb_request_raw(SEQ_PROG_PWR_OFF_LED, FB_RES_LED, FB_OWNER_ADMIN, FB_PRIO_ADMIN, FB_KIND_ONESHOT, SEQ_PWR_OFF);
	/* SEQ_SND_POWEROFF must complete "before power cut" -- the caller (the
	 * shutdown state machine) is responsible for waiting for the audio
	 * sequence's nominal 700 ms duration before removing power; the manager
	 * itself never blocks (reference: no busy-wait). */
	(void)fb_request(SEQ_SND_POWEROFF, FB_OWNER_ADMIN, FB_PRIO_ADMIN);
	(void)fb_request_raw(SEQ_PROG_PWR_OFF_BL, FB_RES_BL, FB_OWNER_ADMIN, FB_PRIO_ADMIN, FB_KIND_ONESHOT, SEQ_PWR_OFF);
} // void fb_pwr_shutdown(void)

void fb_pwr_charging(void)
{
	(void)fb_request(SEQ_SOLID_RED, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
} // void fb_pwr_charging(void)

void fb_pwr_charged(void)
{
	(void)fb_request(SEQ_SOLID_GREEN, FB_OWNER_POWER, FB_PRIO_PWR_STATE);
} // void fb_pwr_charged(void)

void fb_pwr_not_charging(void)
{
	fb_release(FB_OWNER_POWER, FB_RES_LED);
} // void fb_pwr_not_charging(void)

void fb_pwr_batt_low(void)
{
	(void)fb_request_raw(SEQ_PROG_PWR_BATT_LOW_LED, FB_RES_LED, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT, FB_KIND_ONESHOT, SEQ_PWR_BATT_LOW);
	(void)fb_request_raw(SEQ_PROG_PWR_BATT_LOW_BL, FB_RES_BL, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT, FB_KIND_ONESHOT, SEQ_PWR_BATT_LOW);
	(void)fb_request(SEQ_SND_BATT_LOW, FB_OWNER_POWER, FB_PRIO_SYSTEM_ALERT);
} // void fb_pwr_batt_low(void)

void fb_pwr_batt_crit(void)
{
	(void)fb_request_raw(SEQ_PROG_PWR_BATT_CRIT_LED, FB_RES_LED, FB_OWNER_POWER, FB_PRIO_CRITICAL_PWR, FB_KIND_LOOP, SEQ_PWR_BATT_CRIT);
	(void)fb_request_raw(SEQ_PROG_PWR_BATT_CRIT_BL, FB_RES_BL, FB_OWNER_POWER, FB_PRIO_CRITICAL_PWR, FB_KIND_HOLD, SEQ_PWR_BATT_CRIT);
	(void)fb_request_raw(fb_seq_prog(SEQ_SND_BATT_CRIT), FB_RES_SND, FB_OWNER_POWER, FB_PRIO_CRITICAL_PWR, FB_KIND_LOOP, SEQ_PWR_BATT_CRIT);
} // void fb_pwr_batt_crit(void)

void fb_pwr_batt_crit_clear(void)
{
	fb_release(FB_OWNER_POWER, FB_RES_LED);
	fb_release(FB_OWNER_POWER, FB_RES_BL);
	fb_release(FB_OWNER_POWER, FB_RES_SND);
} // void fb_pwr_batt_crit_clear(void)

void fb_pwr_charger_attached(bool charged)
{
	/* Reference section 10.2, literally: cancel the critical LED loop,
	 * SND_OFF, release the critical backlight request, then immediately
	 * activate charging/charged. fb_release() already performs the
	 * "recompute next eligible owner" step and (for SND) always applies
	 * MSG_SND_OFF as a side effect of releasing the last SND owner. */
	fb_pwr_batt_crit_clear();

	if ( charged )
		fb_pwr_charged();
	else
		fb_pwr_charging();
} // void fb_pwr_charger_attached(bool charged)

/*************************** 1 1 . 1   R A D I O   S T A T E S ********************/

/*
 * User-authored nine-state interaction mapping (see
 * documentation/M1_FEEDBACK_RECONCILIATION.md, "User-authored interaction
 * mapping supersedes v1.1"): Detect/Listen, Read, Emulate and
 * Replay/Transmit are ALL continuous 100 ms-on/100 ms-off flashes, in cyan,
 * yellow, magenta and red respectively. These cadences are already in the
 * section 7.4 blink catalogue (SEQ_BLINK_{CYAN,YELLOW,MAGENTA,RED}_100), so
 * no private message array is needed for any of the four LED legs -- unlike
 * the old blue-500/500 field-detect pattern and the old solid-magenta
 * emulate pattern this replaces, both of which required (or were) private
 * programs. This is an explicit, disclosed supersession of the v1.1
 * reference's own field-detect/emulate rows, not a silent deviation -- see
 * the reconciliation doc for the precedence rule and the exact rows
 * replaced.
 */

static const fb_msg_t SEQ_PROG_NET_EMULATING_BL[] = {
	FB_MSG(FB_MSG_BL_HOLD_USER, 0),
	FB_END,
};

/* Green 10 ms / yellow 10 ms alternating, continuous. */
static const fb_msg_t SEQ_PROG_NET_HOPPING[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_LED_SET, FB_LED_GREEN),  FB_MSG(FB_MSG_DELAY, 10),
	FB_MSG(FB_MSG_LED_SET, FB_LED_YELLOW), FB_MSG(FB_MSG_DELAY, 10),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

/* Eight green flashes at 10 ms on / 25 ms off: SAVE_LED, LOOP(8),
 * LED(GREEN), D(10), LED(OFF), D(25), LOOP_END, RESTORE_LED, END. */
static const fb_msg_t SEQ_PROG_NET_TARGET[] = {
	FB_MSG(FB_MSG_LED_SAVE, 0),
	FB_MSG(FB_MSG_LOOP_BEGIN, 8),
	FB_MSG(FB_MSG_LED_SET, FB_LED_GREEN), FB_MSG(FB_MSG_DELAY, 10),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),   FB_MSG(FB_MSG_DELAY, 25),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_MSG(FB_MSG_LED_RESTORE, 0),
	FB_END,
};

/* Radio field-detect/read and channel-hopping are passive/background radio
 * activity with no entry of their own in the reference section 6 example
 * table; they are classified FB_PRIO_BACKGROUND ("Scanning, receiving,
 * channel hopping"), consistent with that table's own examples. This is
 * recorded as an explicit classification choice, not a silent deviation --
 * see documentation/M1_FEEDBACK_RECONCILIATION.md. */
void fb_net_detect_start(void)
{
	/* Detect/Listen: blue, 500 ms on / 500 ms off. */
	(void)fb_request(SEQ_BLINK_BLUE_SLOW, FB_OWNER_RADIO, FB_PRIO_BACKGROUND);
} // void fb_net_detect_start(void)

void fb_net_detect_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
} // void fb_net_detect_stop(void)

/* Compatibility names share the slow-flashing-blue Detect/Listen path. */
void fb_net_field_detect_start(void)
{
	fb_net_detect_start();
} // void fb_net_field_detect_start(void)

void fb_net_field_detect_stop(void)
{
	fb_net_detect_stop();
} // void fb_net_field_detect_stop(void)

void fb_net_read_start(void)
{
	/* Read reuses the white 100 ms on / 100 ms off sequence. */
	(void)fb_request(SEQ_BLINK_WHITE_100, FB_OWNER_RADIO, FB_PRIO_BACKGROUND);
} // void fb_net_read_start(void)

void fb_net_read_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
} // void fb_net_read_stop(void)

void fb_net_emulating_start(void)
{
	/* Emulate uses an approximated blue pulse and holds user backlight brightness. */
	(void)fb_request(SEQ_PULSE_BLUE, FB_OWNER_RADIO, FB_PRIO_PERSISTENT);
	(void)fb_request_raw(SEQ_PROG_NET_EMULATING_BL, FB_RES_BL, FB_OWNER_RADIO, FB_PRIO_PERSISTENT, FB_KIND_HOLD, SEQ_NET_EMULATING);
} // void fb_net_emulating_start(void)

void fb_net_emulating_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
	fb_release(FB_OWNER_RADIO, FB_RES_BL);
} // void fb_net_emulating_stop(void)

/* Replay/Transmit alternates blue/white at 150/150 ms. Its persistent
 * priority lets charging preempt it; baseline restoration resumes the
 * indication after charging releases. */
void fb_net_replay_start(void)
{
	(void)fb_request(SEQ_ALT_BLUE_WHITE, FB_OWNER_RADIO, FB_PRIO_PERSISTENT);
} // void fb_net_replay_start(void)

void fb_net_replay_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
} // void fb_net_replay_stop(void)

/* Write: double-flash white. Classified FB_PRIO_PERSISTENT --
 * see the priority-classification note in m1_feedback_orchestration.h. */
void fb_net_write_start(void)
{
	(void)fb_request(SEQ_DBLFLASH_WHITE, FB_OWNER_RADIO, FB_PRIO_PERSISTENT);
} // void fb_net_write_start(void)

void fb_net_write_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
} // void fb_net_write_stop(void)

void fb_net_hopping_start(void)
{
	(void)fb_request_raw(SEQ_PROG_NET_HOPPING, FB_RES_LED, FB_OWNER_RADIO, FB_PRIO_BACKGROUND, FB_KIND_LOOP, SEQ_NET_HOPPING);
} // void fb_net_hopping_start(void)

void fb_net_hopping_stop(void)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED);
} // void fb_net_hopping_stop(void)

void fb_net_target(void)
{
	(void)fb_request_raw(SEQ_PROG_NET_TARGET, FB_RES_LED, FB_OWNER_RADIO, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_NET_TARGET);
	(void)fb_request(SEQ_SND_TARGET, FB_OWNER_RADIO, FB_PRIO_APP_ALERT);
} // void fb_net_target(void)

/*************************** F I R M W A R E   U P D A T E   ***/

void fb_fw_update_start(void)
{
	(void)fb_request(SEQ_ALT_CYAN_MAGENTA, FB_OWNER_FW_UPDATE, FB_PRIO_SYSTEM_ALERT);
} // void fb_fw_update_start(void)

void fb_fw_update_stop(void)
{
	fb_release(FB_OWNER_FW_UPDATE, FB_RES_LED);
} // void fb_fw_update_stop(void)

/*************************** 1 1 . 2   U S B   S T A T E S ************************/

void fb_usb_connect(void)
{
	/* Solid cyan if no charging/charged owner is active -- automatic: the
	 * charging/charged hold is FB_PRIO_PWR_STATE (2), numerically higher
	 * priority than FB_PRIO_PERSISTENT (5) used here, so find_best_hold_slot
	 * always prefers it while both are registered (reference section 11.2).
	 * Caller must only invoke this on confirmed data enumeration, never on
	 * VBUS/charge-only detection. */
	(void)fb_request(SEQ_SOLID_CYAN, FB_OWNER_USB, FB_PRIO_PERSISTENT);
	(void)fb_request(SEQ_SND_USB_IN, FB_OWNER_USB, FB_PRIO_APP_ALERT);
} // void fb_usb_connect(void)

void fb_usb_activity(void)
{
	/* Intentional no-op: reference section 11.2 requires solid cyan with no
	 * flashing while a serial/WebUSB/MSC transfer is in progress. The
	 * connected-state hold already keeps the LED solid cyan, so activity
	 * must not submit any additional request. */
} // void fb_usb_activity(void)

void fb_usb_disconnect(void)
{
	fb_release(FB_OWNER_USB, FB_RES_LED);
	(void)fb_request(SEQ_SND_USB_OUT, FB_OWNER_USB, FB_PRIO_APP_ALERT);
} // void fb_usb_disconnect(void)

/*************************** 1 1 . 3   P A Y L O A D   S T A T E S ****************/

void fb_pay_start(void)
{
	(void)fb_request(SEQ_SOLID_BLUE, FB_OWNER_PAYLOAD, FB_PRIO_PERSISTENT);
} // void fb_pay_start(void)

void fb_pay_done(void)
{
	fb_release(FB_OWNER_PAYLOAD, FB_RES_LED);
	fb_alert_success(FB_OWNER_PAYLOAD);
} // void fb_pay_done(void)

void fb_pay_fail(void)
{
	fb_release(FB_OWNER_PAYLOAD, FB_RES_LED);
	fb_alert_error(FB_OWNER_PAYLOAD);
} // void fb_pay_fail(void)

/*************************** 1 2 .   S E T T I N G S   P R E V I E W S ************/

/* Fixed-size static buffers (no heap): rebuilt in place for each preview
 * tick. Safe because fb_request_raw() synchronously preempts/finishes any
 * prior instance using this buffer, under the manager's critical section,
 * before this function returns -- see the note in
 * m1_feedback_orchestration.h. A preview call that lands in the same 1 ms
 * tick as the previous one could in principle observe a transient, harmless
 * mix of old/new field values for a single step; this is a documented,
 * accepted limitation, not a memory-safety issue (all fields are
 * single-word and there is no dynamic sizing). */
static fb_msg_t s_vol_preview_prog[4];
static fb_msg_t s_bright_preview_prog[6];

void fb_ui_vol_preview(uint8_t candidate_pct)
{
	if ( candidate_pct == 0u )
	{
		s_vol_preview_prog[0] = (fb_msg_t)FB_MSG(FB_MSG_SND_OFF, 0);
		s_vol_preview_prog[1] = (fb_msg_t)FB_END;
	}
	else
	{
		uint8_t saved_volume = fb_get_alert_volume_pct();

		/* Reference section 12: the preview note plays "at that candidate
		 * volume" without corrupting the committed setting. The manager's
		 * volume gate is global, so it is set to the candidate only for the
		 * duration of building/issuing this one-shot and restored
		 * immediately -- the note itself is unaffected because a volume
		 * change "does not restart the currently playing note" and only
		 * gates whether a *new* MSG_SND_NOTE plays (reference section 5.2),
		 * so setting it back right after admitting this request does not
		 * suppress the note already in flight. */
		s_vol_preview_prog[0] = (fb_msg_t)FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5);
		s_vol_preview_prog[1] = (fb_msg_t)FB_MSG(FB_MSG_DELAY, 250);
		s_vol_preview_prog[2] = (fb_msg_t)FB_MSG(FB_MSG_SND_OFF, 0);
		s_vol_preview_prog[3] = (fb_msg_t)FB_END;

		fb_set_alert_volume_pct(candidate_pct);
		(void)fb_request_raw(s_vol_preview_prog, FB_RES_SND, FB_OWNER_UI_SETTINGS, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_UI_VOL_PREVIEW);
		fb_set_alert_volume_pct(saved_volume);
		return;
	} // else (candidate_pct != 0)

	(void)fb_request_raw(s_vol_preview_prog, FB_RES_SND, FB_OWNER_UI_SETTINGS, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_UI_VOL_PREVIEW);
} // void fb_ui_vol_preview(uint8_t candidate_pct)

void fb_ui_vol_preview_cancel(uint8_t committed_pct)
{
	fb_release(FB_OWNER_UI_SETTINGS, FB_RES_SND);
	fb_set_alert_volume_pct(committed_pct);
} // void fb_ui_vol_preview_cancel(uint8_t committed_pct)

void fb_ui_bright_preview(uint8_t candidate_pct)
{
	/* SAVE_BL, BL(candidate), D(1000), D(1000), RESTORE_BL, END. */
	s_bright_preview_prog[0] = (fb_msg_t)FB_MSG(FB_MSG_BL_SAVE, 0);
	s_bright_preview_prog[1] = (fb_msg_t)FB_MSG(FB_MSG_BL_SET, candidate_pct > 100u ? 100u : candidate_pct);
	s_bright_preview_prog[2] = (fb_msg_t)FB_MSG(FB_MSG_DELAY, 1000);
	s_bright_preview_prog[3] = (fb_msg_t)FB_MSG(FB_MSG_DELAY, 1000);
	s_bright_preview_prog[4] = (fb_msg_t)FB_MSG(FB_MSG_BL_RESTORE, 0);
	s_bright_preview_prog[5] = (fb_msg_t)FB_END;

	(void)fb_request_raw(s_bright_preview_prog, FB_RES_BL, FB_OWNER_UI_SETTINGS, FB_PRIO_APP_ALERT, FB_KIND_ONESHOT, SEQ_UI_BRIGHT_PREVIEW);
} // void fb_ui_bright_preview(uint8_t candidate_pct)

void fb_ui_bright_preview_cancel(void)
{
	fb_release(FB_OWNER_UI_SETTINGS, FB_RES_BL);
} // void fb_ui_bright_preview_cancel(void)

/*************************** L C D   S L E E P   T I M E R *************************/

static const fb_msg_t SEQ_PROG_SLEEP_WAKE[] = { FB_MSG(FB_MSG_BL_USER, 0), FB_END };
static const fb_msg_t SEQ_PROG_SLEEP_OFF[]  = { FB_MSG(FB_MSG_BL_OFF, 0),  FB_END };

void fb_sleep_timer_wake(void)
{
	(void)fb_request_raw(SEQ_PROG_SLEEP_WAKE, FB_RES_BL, FB_OWNER_SLEEP_TIMER, FB_PRIO_BACKGROUND, FB_KIND_HOLD, SEQ_ID_COUNT);
} // void fb_sleep_timer_wake(void)

void fb_sleep_timer_sleep(void)
{
	(void)fb_request_raw(SEQ_PROG_SLEEP_OFF, FB_RES_BL, FB_OWNER_SLEEP_TIMER, FB_PRIO_BACKGROUND, FB_KIND_HOLD, SEQ_ID_COUNT);
} // void fb_sleep_timer_sleep(void)
