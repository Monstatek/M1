/* See COPYING.txt for license details. */

/*
*
* m1_led_indicator.h
*
* Library for led indicator
*
* M1 Project
*
*/

#ifndef M1_LED_INDICATOR_H_
#define M1_LED_INDICATOR_H_

#include "m1_led_priority.h"

/* Legacy ids, preserved for source compatibility with existing callers
 * (m1_system.c's charging-state logic). Internally these no longer index a
 * recovery queue -- m1_led_get_running_id() derives the closest-matching id
 * from whichever m1_led_owner_t is currently topmost; see m1_led_indicator.c. */
typedef enum
{
	LED_INDICATOR_OFF_FN_ID = 0,
	LED_INDICATOR_ON_FN_ID,
	LED_FAST_BLINK_FN_ID,
	LED_FW_UPDATE_ON_FN_ID,
	LED_BATTERY_UNCHARGED_FN_ID,
	LED_BATTERY_CHARGED_ON_FN_ID,
	LED_BATTERY_FULL_ON_FN_ID,
	LED_EOL_FN_ID,
} S_M1_LED_FUNC_ID;

/* --- Legacy API: unchanged signatures, now backed by m1_led_priority --- */
void m1_led_indicator_off(uint8_t *params);
void m1_led_indicator_on(uint8_t *params);
void m1_led_fast_blink_ex(uint8_t *params);
void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms);
void m1_led_fw_update_on(uint8_t *params);
void m1_led_fw_update_off(void);
void m1_led_batt_charged_on(uint8_t *params);
void m1_led_batt_full_on(uint8_t *params);
void m1_led_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode);
uint8_t m1_led_get_running_id(void);
void m1_led_insert_function_id(uint8_t func_id, uint8_t *params);
void m1_led_func_recovery(void);

/* --- New explicit-ownership API, for Phase 2 (m1_feedback) and any new
 * caller that wants a real token instead of the implicit legacy ownership
 * m1_led_fast_blink()/m1_led_fw_update_on() manage internally. --- */
m1_led_result_t m1_led_request(m1_led_owner_t owner, const m1_led_effect_t *effect, uint8_t *out_token);
m1_led_result_t m1_led_release(m1_led_owner_t owner, uint8_t token);

#endif /* M1_LED_INDICATOR_H_ */
