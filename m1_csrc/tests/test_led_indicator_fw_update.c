/* See COPYING.txt for license details. */
/* Exercise the production FW update LED owner with fake LP5814/RTOS.
 * The update indication must use autonomous hardware blink: scheduling
 * periodic I2C writes here can overrun the interrupt-driven ESP loader RX.
 * Build recipe is unchanged: compile with M1_LED_INDICATOR_HOST_TEST,
 * m1_led_priority.c, m1_led_indicator.c and the host test shim. */
#include <stdio.h>
#include "m1_led_indicator_host_test_shim.h"
#include "m1_led_priority.h"
#include "m1_led_indicator.h"

static unsigned passed, failed;
#define CHECK(c) do { if(c) passed++; else { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)
#define CYAN (LED_BLINK_ON_GREEN | LED_BLINK_ON_BLUE)

int main(void)
{
    m1_led_effect_t effect;
    m1_led_priority_reset();
    m1_led_test_reset_fakes();
    m1_led_fw_update_off();
    m1_led_fw_update_off();
    CHECK(!m1_led_owner_is_active(M1_LED_OWNER_FW_UPDATE));

    m1_led_batt_charged_on(NULL);
    CHECK(m1_led_topmost(&effect) == M1_LED_OWNER_CHARGING);
    for (unsigned cycle = 0; cycle < 5; cycle++) {
        m1_led_test_reset_fakes();
        m1_led_fw_update_on(NULL);
        CHECK(m1_led_topmost(&effect) == M1_LED_OWNER_FW_UPDATE);
        CHECK(m1_led_owner_is_active(M1_LED_OWNER_CHARGING));
        CHECK(effect.mode == M1_LED_MODE_BLINK);
        CHECK(g_fake_led_blink_calls == 1);
        CHECK(g_fake_led_solid_calls == 0);
        CHECK(g_fake_led_last_r_g_b == CYAN);
        CHECK(g_fake_led_last_pwm == 25);
        CHECK(g_fake_led_last_on_off_ms == 100);
        /* No background callback may write I2C during ESP responses. */
        for (unsigned i = 0; i < FAKE_TIMER_POOL_MAX; i++)
            CHECK(!g_fake_timer_pool[i].in_use);
        m1_led_fw_update_off();
        CHECK(!m1_led_owner_is_active(M1_LED_OWNER_FW_UPDATE));
        CHECK(m1_led_topmost(&effect) == M1_LED_OWNER_CHARGING);
        CHECK(effect.mode == M1_LED_MODE_SOLID);
        CHECK(effect.r_g_b == LED_BLINK_ON_RED);
        m1_led_fw_update_off();
        CHECK(m1_led_topmost(&effect) == M1_LED_OWNER_CHARGING);
    }
    printf("%u passed, %u failed\n", passed, failed);
    return failed != 0;
}
