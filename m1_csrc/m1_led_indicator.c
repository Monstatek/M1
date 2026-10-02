/* See COPYING.txt for license details. */

/*
*
* m1_led_indicator.c
*
* Hardware adapter over m1_led_priority's owner/priority arbiter. Legacy
* callers (m1_led_fast_blink, m1_led_fw_update_on/off, m1_led_batt_*,
* m1_led_set_blink_timer, m1_led_insert_function_id, ...) keep their exact
* original signatures; internally they now each own a fixed slot in the
* arbiter instead of sharing the old 3-entry recovery queue, so one owner's
* request can no longer silently evict or corrupt another's.
*
* Priority (declared in m1_led_priority.h, low to high):
*   feature activity < SD event < charging < firmware update < critical
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
/* Host-test build (see m1_csrc/tests/test_led_indicator_fw_update.c):
 * swap the STM32 HAL / FreeRTOS / lp5814 stack for a host-side fake so the
 * REAL functions below (m1_led_fw_update_on/_off and their timer callback,
 * in particular) can be compiled and exercised on the host, not a
 * reimplementation of their logic. Mirrors the M1_FEEDBACK_HOST_TEST
 * pattern already used by m1_feedback_manager.c. Production builds are
 * unaffected -- this branch is never taken unless the macro is explicitly
 * defined on the host-test compile command line. */
#ifdef M1_LED_INDICATOR_HOST_TEST
#include "m1_led_indicator_host_test_shim.h"
#else
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_lp5814.h"
#endif
#include "m1_led_priority.h"
#include "m1_led_indicator.h"

/*************************** D E F I N E S ************************************/

/***************************** V A R I A B L E S ******************************/

/* Each legacy call site is an implicit single owner (only one feature runs
 * at a time on this UI), so its token just needs to be remembered here --
 * legacy callers never see or pass tokens themselves. */
static uint8_t s_activity_token = 0;
static uint8_t s_fw_update_token = 0;
static uint8_t s_charging_token = 0;
static uint8_t s_sd_event_token = 0;

/* SD events are self-terminating on real hardware (LED_BLINK_TIMER_TIMEOUT),
 * driven by our own reusable FreeRTOS timer -- not lp5814_set_blink_timer's
 * internal one, so the auto-stop only fires m1_led_owner_end() (soft state),
 * never a raw hardware clear that could stomp a higher-priority owner that
 * has since preempted it. See sd_event_timer_expired(). */
static TimerHandle_t s_sd_event_timer_hdl = NULL;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void m1_led_render(void);
static void sd_event_timer_expired(TimerHandle_t xTimer);
static bool sd_event_timer_ensure(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
 * @brief  Push whatever the arbiter says should currently be visible to the
 *         real LP5814 hardware. Called after every owner state change.
 *
 * SOLID effects explicitly clear all RGB channels first (matching every
 * existing solid-color call site, e.g. the old m1_led_batt_charged_on()) --
 * lp5814_led_on_rgb() only OR's in enable bits, it never clears stale
 * channels from a previous owner on its own. BLINK effects go straight to
 * lp5814_fastblink_on_R_G_B(), which performs a full register write and is
 * therefore self-clearing -- the same call every feature-activity call site
 * already uses today when switching colours while blinking is active.
 */
/*============================================================================*/
static void m1_led_render(void)
{
	m1_led_effect_t eff;

	if ( m1_led_topmost(&eff) == M1_LED_OWNER_NONE )
	{
		lp5814_all_off_RGB();
		return;
	}

	if ( eff.mode == M1_LED_MODE_SOLID )
	{
		lp5814_all_off_RGB();
		lp5814_led_on_rgb(eff.r_g_b, eff.pwm);
	}
	else
	{
		lp5814_fastblink_on_R_G_B(eff.r_g_b, eff.pwm, eff.on_off_ms);
	}
} // static void m1_led_render(void)



/*============================================================================*/
/**
 * @brief  New explicit-ownership API (Phase 2 / future callers): register a
 *         request and render immediately. See m1_led_priority.h.
 */
/*============================================================================*/
m1_led_result_t m1_led_request(m1_led_owner_t owner, const m1_led_effect_t *effect, uint8_t *out_token)
{
	m1_led_result_t r = m1_led_owner_begin(owner, effect, out_token);
	if ( r == M1_LED_ACCEPTED )
		m1_led_render();
	return r;
} // m1_led_result_t m1_led_request(...)

m1_led_result_t m1_led_release(m1_led_owner_t owner, uint8_t token)
{
	m1_led_result_t r = m1_led_owner_end(owner, token);
	if ( r == M1_LED_ACCEPTED )
		m1_led_render();
	return r;
} // m1_led_result_t m1_led_release(...)



/*============================================================================*/
/*
 * This function turns off all R/G/B LEDs.
 *
 * Only external call site: m1_system.c's charging-state logic, to indicate
 * "not charging" -- so this releases the CHARGING owner rather than forcing
 * a raw hardware clear, letting a lower-priority feature-activity blink
 * that's currently running underneath correctly stay visible.
*/
/*============================================================================*/
void m1_led_indicator_off(uint8_t *params)
{
	(void)params;
	m1_led_release(M1_LED_OWNER_CHARGING, s_charging_token);
} // void m1_led_indicator_off(uint8_t *params)



/*============================================================================*/
/*
 * This function turns on LED indicator (solid green). No current callers
 * (dead in the original code too); kept for API compatibility only.
*/
/*============================================================================*/
void m1_led_indicator_on(uint8_t *params)
{
	m1_led_effect_t eff;
	(void)params;

	eff.r_g_b = LED_BLINK_ON_GREEN;
	eff.pwm = LED_ON_PWM_GREEN;
	eff.on_off_ms = 0;
	eff.mode = M1_LED_MODE_SOLID;
	m1_led_request(M1_LED_OWNER_CHARGING, &eff, &s_charging_token);
} // void m1_led_indicator_on(uint8_t *params)



/*============================================================================*/
/*
 * Wrapper function of the function m1_led_fast_blink
 * Input param:
 *
*/
/*============================================================================*/
void m1_led_fast_blink_ex(uint8_t *params)
{
	if ( params!= NULL )
	{
		m1_led_fast_blink(params[0], params[1], params[2]);
	} // if ( params!= NULL )
} // void m1_led_fast_blink_ex(uint8_t *params)



/*============================================================================*/
/*
 * This function turns on LED indicator in fast blink mode. This is the
 * FEATURE_ACTIVITY owner -- NFC/RFID/Sub-GHz/IR listening/scanning/emulating.
 * Input param: pwm_rgb brightness of the RGB LED, 0 - 255
 * 				on_off_ms on off time in ms; 0 = turn off
*/
/*============================================================================*/
void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms)
{
	m1_led_effect_t eff;

	if ( on_off_ms != LED_FASTBLINK_ONTIME_OFF ) // Turn on?
	{
		eff.r_g_b = r_g_b;
		eff.pwm = pwm_rgb;
		eff.on_off_ms = on_off_ms;
		eff.mode = M1_LED_MODE_BLINK;
		m1_led_request(M1_LED_OWNER_FEATURE_ACTIVITY, &eff, &s_activity_token);
	}
	else // Turn off
	{
		m1_led_release(M1_LED_OWNER_FEATURE_ACTIVITY, s_activity_token);
	}
} // void m1_led_fast_blink(uint8_t r_g_b, uint8_t pwm_rgb, uint8_t on_off_ms)




/*============================================================================*/
/*
 * This function turns on RGB led in fast blink mode during device FW
 * update. FW_UPDATE owns its own slot (higher priority than feature
 * activity and charging), so it is genuinely protected/non-overridable --
 * unlike the original code, which reused the same slot as feature activity
 * and had no real priority guarantee.
 * Input param: none
*/
/*============================================================================*/
void m1_led_fw_update_on(uint8_t *params)
{
	m1_led_effect_t eff;
	(void)params;

	eff.r_g_b = LED_BLINK_ON_GREEN | LED_BLINK_ON_BLUE;
	eff.pwm = 25;
	eff.on_off_ms = 100;
	eff.mode = M1_LED_MODE_BLINK;
	m1_led_request(M1_LED_OWNER_FW_UPDATE, &eff, &s_fw_update_token);
} // void m1_led_fw_update_on(uint8_t *params)



/*============================================================================*/
/*
 * This function turns off RGB led in fast blink mode after device FW update.
 * Input param: none
*/
/*============================================================================*/
void m1_led_fw_update_off(void)
{
	m1_led_release(M1_LED_OWNER_FW_UPDATE, s_fw_update_token);
} // void m1_led_fw_update_off(void)



/*============================================================================*/
/**
 * @brief Turn on LED indicator for battery charge status (solid red)
 */
/*============================================================================*/
void m1_led_batt_charged_on(uint8_t *params)
{
	m1_led_effect_t eff;
	(void)params;

	eff.r_g_b = LED_BLINK_ON_RED;
	eff.pwm = LED_FASTBLINK_PWM_M;
	eff.on_off_ms = 0;
	eff.mode = M1_LED_MODE_SOLID;
	m1_led_request(M1_LED_OWNER_CHARGING, &eff, &s_charging_token);
} // void m1_led_batt_charged_on(uint8_t *params)



/*============================================================================*/
/**
 * @brief Turn on LED indicator for battery full status (solid green)
 */
/*============================================================================*/
void m1_led_batt_full_on(uint8_t *params)
{
	m1_led_effect_t eff;
	(void)params;

	eff.r_g_b = LED_BLINK_ON_GREEN;
	eff.pwm = LED_FASTBLINK_PWM_M;
	eff.on_off_ms = 0;
	eff.mode = M1_LED_MODE_SOLID;
	m1_led_request(M1_LED_OWNER_CHARGING, &eff, &s_charging_token);
} // void m1_led_batt_full_on(uint8_t *params)



/*============================================================================*/
/**
 * @brief  SD event completion: LP5814 hardware auto-stop timer expired,
 *         release the SD_EVENT owner (soft arbiter state only -- does not
 *         touch hardware directly) and let m1_led_render() decide what
 *         should be visible next. This is what makes a preempted SD event
 *         (e.g. charging started while it was still blinking) end safely:
 *         if SD_EVENT wasn't topmost when this fires, releasing it changes
 *         nothing on screen, because it wasn't controlling the LEDs anyway.
 */
/*============================================================================*/
static void sd_event_timer_expired(TimerHandle_t xTimer)
{
	(void)xTimer;
	m1_led_release(M1_LED_OWNER_SD_EVENT, s_sd_event_token);
} // static void sd_event_timer_expired(TimerHandle_t xTimer)

static bool sd_event_timer_ensure(void)
{
	if ( s_sd_event_timer_hdl == NULL )
	{
		s_sd_event_timer_hdl = xTimerCreate("m1_led_sd", pdMS_TO_TICKS(LED_BLINK_TIMER_TIMEOUT),
											 pdFALSE, NULL, sd_event_timer_expired);
	}
	return (s_sd_event_timer_hdl != NULL);
} // static bool sd_event_timer_ensure(void)



/*============================================================================*/
/**
  * @brief  Blink (or solid) the R/G/B for a self-terminating SD-card event.
  *         SD_EVENT owns its own slot, ranked above feature activity but
  *         below charging -- a card event correctly interrupts an ongoing
  *         NFC/RFID/Sub-GHz/IR blink, but never overrides a charging
  *         indication. PWM is fixed at LED_FASTBLINK_PWM_M, matching every
  *         existing call site (m1_sdcard.c never varied it).
  * @param  r_g_b select LED(s)
  * 		on_off_ms blink cadence in ms (ignored in SOLID mode)
  * 		mode LED_BLINK_TIMER_MODE_SOLID or LED_BLINK_TIMER_MODE_BLINK
  * @retval None
  */
/*============================================================================*/
void m1_led_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode)
{
	m1_led_effect_t eff;

	eff.r_g_b = r_g_b;
	eff.pwm = LED_FASTBLINK_PWM_M;
	eff.on_off_ms = on_off_ms;
	eff.mode = (mode == LED_BLINK_TIMER_MODE_SOLID) ? M1_LED_MODE_SOLID : M1_LED_MODE_BLINK;

	m1_led_request(M1_LED_OWNER_SD_EVENT, &eff, &s_sd_event_token);

	if ( sd_event_timer_ensure() )
		xTimerChangePeriod(s_sd_event_timer_hdl, pdMS_TO_TICKS(LED_BLINK_TIMER_TIMEOUT), 0); // (re)arm auto-stop
} // void m1_led_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode)



/*============================================================================*/
/**
 * @brief  Return the id of whichever owner is currently topmost (visible),
 *         translated to the legacy S_M1_LED_FUNC_ID vocabulary. The sole
 *         consumer (m1_system.c's battery_indicator_update()) only branches
 *         on LED_FAST_BLINK_FN_ID vs LED_INDICATOR_OFF_FN_ID vs the two
 *         battery ids; every branch converges on the same underlying
 *         "(re)apply the charging effect now" call regardless of which one
 *         is taken (m1_led_insert_function_id() and the direct
 *         m1_led_batt_*_on()/m1_led_indicator_off() calls are equivalent
 *         under this model), so an owner this enum has no dedicated id for
 *         (SD_EVENT) safely falls back to LED_FAST_BLINK_FN_ID.
 */
/*============================================================================*/
uint8_t m1_led_get_running_id(void)
{
	m1_led_effect_t eff;

	switch ( m1_led_topmost(&eff) )
	{
		case M1_LED_OWNER_FW_UPDATE:
			return LED_FW_UPDATE_ON_FN_ID;
		case M1_LED_OWNER_CHARGING:
			return (eff.r_g_b == LED_BLINK_ON_GREEN) ? LED_BATTERY_FULL_ON_FN_ID : LED_BATTERY_CHARGED_ON_FN_ID;
		case M1_LED_OWNER_SD_EVENT:
		case M1_LED_OWNER_FEATURE_ACTIVITY:
			return LED_FAST_BLINK_FN_ID;
		case M1_LED_OWNER_CRITICAL:
		case M1_LED_OWNER_NONE:
		default:
			return LED_INDICATOR_OFF_FN_ID;
	}
} // uint8_t m1_led_get_running_id(void)



/*============================================================================*/
/**
 * @brief  Set the CHARGING owner's state directly from a legacy func id.
 *         Only external caller: m1_system.c's battery_indicator_update(),
 *         on the "a feature-activity blink is currently running" branch.
 *
 *         Under the old recovery-queue model this deferred the charging
 *         update until the blink finished (a real priority-inversion bug --
 *         "charging" is supposed to outrank feature activity, not wait
 *         behind it). Here the arbiter already ranks CHARGING above
 *         FEATURE_ACTIVITY, so applying it immediately is simply correct:
 *         it preempts the blink right away and the blink is automatically
 *         restored, unmodified, whenever charging next ends.
 */
/*============================================================================*/
void m1_led_insert_function_id(uint8_t func_id, uint8_t *params)
{
	(void)params;

	switch ( func_id )
	{
		case LED_BATTERY_UNCHARGED_FN_ID:
			m1_led_indicator_off(NULL);
			break;
		case LED_BATTERY_CHARGED_ON_FN_ID:
			m1_led_batt_charged_on(NULL);
			break;
		case LED_BATTERY_FULL_ON_FN_ID:
			m1_led_batt_full_on(NULL);
			break;
		default:
			break;
	}
} // void m1_led_insert_function_id(uint8_t func_id, uint8_t *params)



/*============================================================================*/
/*
 * Legacy no-op. The old 3-entry recovery-queue pop is gone -- restoration is
 * now automatic (m1_led_render() always shows the highest-priority active
 * owner, recomputed on every begin()/end()), so there is nothing left for a
 * caller to manually trigger. Kept only so any surviving reference still
 * links; not wired to anything.
*/
/*============================================================================*/
void m1_led_func_recovery(void)
{
} // void m1_led_func_recovery(void)
