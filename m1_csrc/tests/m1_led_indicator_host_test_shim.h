/* See COPYING.txt for license details. */

/*
 * m1_led_indicator_host_test_shim.h
 *
 * Host-test-only stand-in for the real stm32h5xx_hal.h / main.h /
 * m1_lp5814.h stack, so m1_led_indicator.c's real source (not a
 * reimplementation of its logic) can be compiled and exercised on the host
 * for test_led_indicator_fw_update.c. Only pulled in when
 * M1_LED_INDICATOR_HOST_TEST is defined -- production builds are
 * unaffected (see the #ifdef in m1_led_indicator.c). Mirrors the
 * M1_FEEDBACK_HOST_TEST pattern already used by m1_feedback_manager.c.
 *
 * Provides just the identifiers m1_led_indicator.c actually references:
 * the LED colour/pwm/mode constants, the three lp5814_* register-write
 * entry points (recorded into test-observable globals instead of touching
 * real hardware), and a minimal single-threaded FreeRTOS software-timer
 * fake (xTimerCreate/xTimerChangePeriod/xTimerStop/pvTimerGetTimerID/
 * vTimerSetTimerID) with a test-only m1_led_test_fire_timer() to invoke a
 * timer's callback synchronously and deterministically -- there is no
 * scheduler on the host, so timer callbacks never fire on their own; the
 * test decides exactly when.
 *
 * M1 Project
 */

#ifndef M1_LED_INDICATOR_HOST_TEST_SHIM_H_
#define M1_LED_INDICATOR_HOST_TEST_SHIM_H_

#include <stdint.h>
#include <stddef.h>

/* --- LED colour / pwm / mode constants (real values, m1_lp5814.h / m1_system.h) --- */
#define LED_BLINK_ON_RED			0x01
#define LED_BLINK_ON_GREEN			0x02
#define LED_BLINK_ON_BLUE			0x04
#define LED_BLINK_ON_RGB			(LED_BLINK_ON_RED + LED_BLINK_ON_GREEN + LED_BLINK_ON_BLUE)

#define LED_BLINK_TIMER_MODE_SOLID	0
#define LED_BLINK_TIMER_MODE_BLINK	1
#define LED_BLINK_TIMER_TIMEOUT		600 /* ms; real value is TASKDELAY_SDCARD_DET_TASK(700) - 100 */

#define LED_ON_PWM_GREEN			175
#define LED_ON_PWM_RED				255
#define LED_FASTBLINK_PWM_M			175
#define LED_FASTBLINK_ONTIME_OFF	0	/* ms */

/* --- Fake lp5814 driver: records the last write instead of touching hardware --- */
typedef enum { FAKE_LED_WRITE_NONE = 0, FAKE_LED_WRITE_SOLID, FAKE_LED_WRITE_BLINK, FAKE_LED_WRITE_ALL_OFF } fake_led_write_kind_t;

extern fake_led_write_kind_t g_fake_led_last_kind;
extern uint8_t  g_fake_led_last_r_g_b;
extern uint8_t  g_fake_led_last_pwm;
extern uint16_t g_fake_led_last_on_off_ms;
extern unsigned g_fake_led_all_off_calls;
extern unsigned g_fake_led_solid_calls;
extern unsigned g_fake_led_blink_calls;

static inline void lp5814_all_off_RGB(void)
{
	g_fake_led_all_off_calls++;
	g_fake_led_last_kind = FAKE_LED_WRITE_ALL_OFF;
}

static inline void lp5814_led_on_rgb(uint8_t r_g_b, uint8_t pwm)
{
	g_fake_led_solid_calls++;
	g_fake_led_last_kind = FAKE_LED_WRITE_SOLID;
	g_fake_led_last_r_g_b = r_g_b;
	g_fake_led_last_pwm = pwm;
}

static inline void lp5814_fastblink_on_R_G_B(uint8_t r_g_b, uint8_t pwm, uint16_t on_off_ms)
{
	g_fake_led_blink_calls++;
	g_fake_led_last_kind = FAKE_LED_WRITE_BLINK;
	g_fake_led_last_r_g_b = r_g_b;
	g_fake_led_last_pwm = pwm;
	g_fake_led_last_on_off_ms = on_off_ms;
}

/* --- Minimal single-threaded fake FreeRTOS software timer --- */
#define pdTRUE		1
#define pdFALSE		0
#define pdMS_TO_TICKS(ms)	(ms)

typedef void (*fake_timer_cb_t)(void *xTimer);
typedef void *TimerHandle_t;

#define FAKE_TIMER_POOL_MAX	4

typedef struct
{
	int             in_use;
	int             running;
	uint32_t        period;
	void           *id;
	fake_timer_cb_t callback;
} fake_timer_slot_t;

extern fake_timer_slot_t g_fake_timer_pool[FAKE_TIMER_POOL_MAX];

static inline TimerHandle_t xTimerCreate(const char *name, uint32_t period, int auto_reload,
										  void *id, fake_timer_cb_t callback)
{
	int i;
	(void)name;
	(void)auto_reload; /* every timer this file creates is auto-reload or one-shot per its own call site; the fake doesn't need to distinguish since the test always fires manually */

	for ( i = 0; i < FAKE_TIMER_POOL_MAX; i++ )
	{
		if ( !g_fake_timer_pool[i].in_use )
		{
			g_fake_timer_pool[i].in_use = 1;
			g_fake_timer_pool[i].running = 0;
			g_fake_timer_pool[i].period = period;
			g_fake_timer_pool[i].id = id;
			g_fake_timer_pool[i].callback = callback;
			return &g_fake_timer_pool[i];
		}
	}
	return NULL;
}

static inline int xTimerChangePeriod(TimerHandle_t xTimer, uint32_t period, uint32_t block)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	(void)block;
	if ( slot == NULL )
		return pdFALSE;
	slot->period = period;
	slot->running = 1; /* real xTimerChangePeriod() also starts a dormant timer */
	return pdTRUE;
}

static inline int xTimerStop(TimerHandle_t xTimer, uint32_t block)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	(void)block;
	if ( slot == NULL )
		return pdFALSE;
	slot->running = 0;
	return pdTRUE;
}

static inline void vTimerSetTimerID(TimerHandle_t xTimer, void *id)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	if ( slot != NULL )
		slot->id = id;
}

static inline void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	return (slot != NULL) ? slot->id : NULL;
}

/* Test-only: synchronously invoke a timer's registered callback, exactly as
 * the FreeRTOS timer service task would when the period elapses -- there is
 * no scheduler on the host to do this on its own. Firing a stopped timer is
 * intentionally still allowed here (it mirrors a callback that was already
 * dequeued by the timer service task a moment before xTimerStop() took
 * effect on real hardware); the callback itself is what's responsible for
 * no-op'ing via the generation/active-owner checks under test. */
static inline void m1_led_test_fire_timer(TimerHandle_t xTimer)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	if ( slot != NULL && slot->callback != NULL )
		slot->callback(xTimer);
}

static inline int m1_led_test_timer_running(TimerHandle_t xTimer)
{
	fake_timer_slot_t *slot = (fake_timer_slot_t *)xTimer;
	return (slot != NULL) ? slot->running : 0;
}

/* Deliberately does NOT touch g_fake_timer_pool: m1_led_indicator.c's own
 * timer handle (s_fw_update_timer_hdl) is a file-scope static that -- just
 * like on real hardware -- is created lazily once and then reused for the
 * rest of the process's life; it is never torn down between _on()/_off()
 * cycles. Wiping the fake pool here out from under that still-live handle
 * would desync the fake from the real static and break the very lifecycle
 * behaviour under test. Only the per-assertion LED-write recorder is reset,
 * so each test starts from a clean "what got rendered" slate. */
static inline void m1_led_test_reset_fakes(void)
{
	g_fake_led_last_kind = FAKE_LED_WRITE_NONE;
	g_fake_led_last_r_g_b = 0;
	g_fake_led_last_pwm = 0;
	g_fake_led_last_on_off_ms = 0;
	g_fake_led_all_off_calls = 0;
	g_fake_led_solid_calls = 0;
	g_fake_led_blink_calls = 0;
}

#endif /* M1_LED_INDICATOR_HOST_TEST_SHIM_H_ */
