/* See COPYING.txt for license details. */

/*
 * m1_led_indicator_host_test_shim.c
 *
 * Backing storage for the extern globals declared in
 * m1_led_indicator_host_test_shim.h (fake lp5814 write recorder, fake
 * FreeRTOS timer pool). Defined once here so both m1_led_indicator.c and
 * the test file that links against it share the same instances.
 *
 * M1 Project
 */

#include "m1_led_indicator_host_test_shim.h"

fake_led_write_kind_t g_fake_led_last_kind = FAKE_LED_WRITE_NONE;
uint8_t  g_fake_led_last_r_g_b = 0;
uint8_t  g_fake_led_last_pwm = 0;
uint16_t g_fake_led_last_on_off_ms = 0;
unsigned g_fake_led_all_off_calls = 0;
unsigned g_fake_led_solid_calls = 0;
unsigned g_fake_led_blink_calls = 0;

fake_timer_slot_t g_fake_timer_pool[FAKE_TIMER_POOL_MAX];
