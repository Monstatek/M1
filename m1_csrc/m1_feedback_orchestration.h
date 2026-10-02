/* See COPYING.txt for license details. */

/*
*
* m1_feedback_orchestration.h
*
* Multi-resource orchestrations of the primitive catalogue
* (m1_feedback_sequences.h): reference sections 9-12 -- composite alerts,
* power/lifecycle feedback, radio/protocol states, USB states, automated
* payload states, and settings previews. Each function issues one
* fb_request_raw()/fb_request()/fb_release() call per resource it drives;
* "composite elements start together when possible" (reference section 9)
* means the calls happen back-to-back with no intervening manager tick,
* not that they share one arbitration decision -- each resource is still
* arbitrated independently, per the manager's core contract.
*
* This layer is pure application-facing glue: it contains no hardware
* register access and no FreeRTOS calls, so (like the manager core) it can
* be linked into the host unit tests.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_ORCHESTRATION_H_
#define M1_FEEDBACK_ORCHESTRATION_H_

#include "m1_feedback_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************** 9 .   C O M P O S I T E   A L E R T S ****************/

void fb_alert_success(fb_owner_t owner);
void fb_alert_error(fb_owner_t owner);
void fb_alert_notify(fb_owner_t owner);
void fb_alert_beep(fb_owner_t owner);
void fb_alert_beep2(fb_owner_t owner);

/*************************** 1 0 .   P O W E R / L I F E C Y C L E ****************/

void fb_pwr_boot(void);
void fb_pwr_shutdown(void);			/* Runs 10.2 cleanup, then SEQ_PWR_OFF.   */
void fb_pwr_charging(void);			/* SEQ_PWR_CHARGING                        */
void fb_pwr_charged(void);				/* SEQ_PWR_CHARGED                         */
void fb_pwr_not_charging(void);		/* SEQ_PWR_NOT_CHARGING                    */
void fb_pwr_batt_low(void);
void fb_pwr_batt_crit(void);
void fb_pwr_batt_crit_clear(void);		/* Battery exits critical without a charger*/

/* Reference section 10.2: charger-attach transition. `charged` selects
 * whether SEQ_PWR_CHARGING or SEQ_PWR_CHARGED is activated once the
 * critical-battery cleanup completes. */
void fb_pwr_charger_attached(bool charged);

/*************************** 1 1 . 1   R A D I O   S T A T E S ********************/
/* Radio indications share FB_OWNER_RADIO and FB_RES_LED; a new request
 * replaces that owner's previous LED indication. Pair each start with stop.
 *
 * Detect/Listen: blue 500/500 ms; Read: white 100/100 ms.
 * Both use FB_PRIO_BACKGROUND for passive scanning/receiving.
 * Emulate: approximated blue pulse plus a backlight hold at user brightness.
 * Replay/Transmit: alternating blue/white 150/150 ms.
 * Write: repeating double-flash white.
 * These active operations use FB_PRIO_PERSISTENT. Charging outranks them;
 * USB-connected outranks passive scanning. recompute_baseline() restores
 * lower-priority indications when the higher-priority owner releases.
 * See documentation/M1_FEEDBACK_RECONCILIATION.md for palette rationale.
 */

void fb_net_detect_start(void);
void fb_net_detect_stop(void);
void fb_net_read_start(void);
void fb_net_read_stop(void);
void fb_net_emulating_start(void);
void fb_net_emulating_stop(void);
void fb_net_replay_start(void);
void fb_net_replay_stop(void);

/* Write: double-flash white. See the priority-classification note
 * above. */
void fb_net_write_start(void);
void fb_net_write_stop(void);

/* Compatibility names share the slow-flashing-blue Detect/Listen path. */
void fb_net_field_detect_start(void);
void fb_net_field_detect_stop(void);

void fb_net_hopping_start(void);
void fb_net_hopping_stop(void);
void fb_net_target(void);				/* One-shot; no explicit stop.             */

/*************************** F I R M W A R E   U P D A T E   ***/
/* Reserved feedback API for noncritical update-adjacent indications:
 * alternating cyan/magenta at 200/200 ms, FB_PRIO_SYSTEM_ALERT.
 *
 * Actual flash-in-progress indication uses m1_led_fw_update_on()/_off() in
 * m1_led_indicator.c and the protected M1_LED_OWNER_FW_UPDATE slot. Its
 * update indication uses the hardware sequencer after the timer rollback;
 * only the critical owner outranks it. Routing it through these wrappers would use the lower
 * M1_LED_OWNER_FEATURE_ACTIVITY slot and allow charging/SD activity to preempt
 * it. Keep actual update call sites on the protected path.
 * See documentation/M1_FEEDBACK_RECONCILIATION.md for ownership rationale.
 */

void fb_fw_update_start(void);
void fb_fw_update_stop(void);

/*************************** 1 1 . 2   U S B   S T A T E S ************************/

void fb_usb_connect(void);				/* Call only on confirmed enumeration.     */
void fb_usb_activity(void);			/* Intentional no-op -- see reference 11.2.*/
void fb_usb_disconnect(void);

/*************************** 1 1 . 3   P A Y L O A D   S T A T E S ****************/

void fb_pay_start(void);
void fb_pay_done(void);
void fb_pay_fail(void);

/*************************** 1 2 .   S E T T I N G S   P R E V I E W S ************/

/* Plays the preview note at `candidate_pct` without altering the committed
 * alert-volume setting. The settings screen must call
 * fb_ui_vol_preview_cancel() with the previously committed value if the
 * user leaves without confirming (reference section 12: "Leaving the
 * settings screen early cancels the preview and immediately restores the
 * previously committed setting"). Confirming simply calls
 * fb_set_alert_volume_pct() with the committed value as normal. */
void fb_ui_vol_preview(uint8_t candidate_pct);
void fb_ui_vol_preview_cancel(uint8_t committed_pct);

void fb_ui_bright_preview(uint8_t candidate_pct);
void fb_ui_bright_preview_cancel(void);

/*************************** L C D   S L E E P   T I M E R *************************/
/*
 * Reference section 5.3: "The sleep timer remains authoritative unless a
 * named hold is active." Modeled as the lowest-priority backlight HOLD
 * (FB_OWNER_SLEEP_TIMER, FB_PRIO_BACKGROUND) so any higher-priority holder
 * (user brightness holds, critical-battery wake, emulation hold, settings
 * preview, ...) transparently takes over the backlight and the sleep timer
 * resumes automatically -- via the normal logical-restore recomputation --
 * once that holder releases. Call fb_sleep_timer_wake() / _sleep() from the
 * existing keypad-activity/inactivity edges instead of calling
 * lp5814_backlight_on() directly. */
void fb_sleep_timer_wake(void);
void fb_sleep_timer_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_FEEDBACK_ORCHESTRATION_H_ */
