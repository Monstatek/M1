/* See COPYING.txt for license details. */

/*
 * m1_led_priority.h
 *
 * Hardware-independent LED request arbitration: fixed owner/priority model,
 * ownership tokens, and "what should currently be displayed" resolution.
 *
 * No STM32 HAL / FreeRTOS / lp5814 dependency -- this is pure state so it can
 * be host-tested directly (see m1_csrc/test/m1_led_priority_test.c). The
 * hardware adapter lives in m1_led_indicator.c, which calls m1_led_topmost()
 * and pushes the result to the real LP5814 driver.
 *
 * M1 Project
 */

#ifndef M1_LED_PRIORITY_H_
#define M1_LED_PRIORITY_H_

#include <stdint.h>
#include <stdbool.h>

/* Priority is the enum's declared order (ascending). Every owner has its own
 * permanent slot -- there is no shared queue, so one owner's request can
 * never evict or overflow another owner's state. */
typedef enum
{
	M1_LED_OWNER_NONE = 0,
	M1_LED_OWNER_FEATURE_ACTIVITY,	/* NFC/RFID/Sub-GHz/IR listening/scanning/emulating blink	*/
	M1_LED_OWNER_SD_EVENT,			/* SD insert/remove/connect/fail, self-terminating			*/
	M1_LED_OWNER_CHARGING,			/* persistent charging / charged / uncharged state			*/
	M1_LED_OWNER_FW_UPDATE,			/* protected, non-overridable firmware-update indication	*/
	M1_LED_OWNER_CRITICAL,			/* highest-priority hardware/system fault (reserved)		*/
	M1_LED_OWNER_COUNT
} m1_led_owner_t;

typedef enum
{
	M1_LED_MODE_BLINK = 0,	/* autonomous LP5814 fast-blink pattern	*/
	M1_LED_MODE_SOLID,		/* fixed-on colour							*/
} m1_led_mode_t;

typedef enum
{
	M1_LED_ACCEPTED = 0,		/* request registered (may or may not be the one currently shown)	*/
	M1_LED_REJECTED_STALE,		/* token didn't match the owner's current generation -- ignored	*/
	M1_LED_REJECTED_INVALID,	/* bad owner id														*/
} m1_led_result_t;

typedef struct
{
	uint8_t      r_g_b;		/* LED_BLINK_ON_RED/GREEN/BLUE bitmask	*/
	uint8_t      pwm;
	uint16_t     on_off_ms;	/* blink cadence; ignored in SOLID mode	*/
	m1_led_mode_t mode;
} m1_led_effect_t;

/* Register/replace owner's request. Always succeeds for a valid owner and
 * issues a fresh token, which invalidates whatever token that owner held
 * before -- a stale completion callback from a superseded request can no
 * longer end the new one. Triggers re-arbitration. */
m1_led_result_t m1_led_owner_begin(m1_led_owner_t owner, const m1_led_effect_t *effect, uint8_t *out_token);

/* Release owner's request. Idempotent (ending an inactive owner is a no-op
 * success). Rejected if token is stale. Triggers re-arbitration. */
m1_led_result_t m1_led_owner_end(m1_led_owner_t owner, uint8_t token);

bool m1_led_owner_is_active(m1_led_owner_t owner);

/* Resolve what should currently be displayed: the active owner with the
 * highest priority, or M1_LED_OWNER_NONE if nothing is active (effect_out
 * is untouched in that case). Pure query, no side effects. */
m1_led_owner_t m1_led_topmost(m1_led_effect_t *effect_out);

/* Test-only: clear all slots back to inactive/token-0. Never called from
 * production code paths. */
void m1_led_priority_reset(void);

#endif /* M1_LED_PRIORITY_H_ */
