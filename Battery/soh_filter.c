/* See COPYING.txt for license details. */

/*
*
* soh_filter.c
*
* SOH filter functions for M1 battery health management
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"

#ifndef M1_DEBUG_T1_BOARD_ENABLE
#include "m1_lp5814.h"
#include "m1_bq27421.h"
#else
#include "m1_lp5562.h"
#include "m1_stc3115.h"
#endif // #ifndef M1_DEBUG_T1_BOARD_ENABLE
#include "battery.h"
#include "soh_filter.h"

/*========================================================
 * User Config
 *========================================================*/

#define SOH_FILTER_ALPHA_DOWN           (0.1f)    // SOH decrease response rate
#define SOH_FILTER_ALPHA_UP             (0.005f)  // SOH increase response rate

#define SOH_VALID_SOC_MIN               (95U)     // Minimum SOC for accepting SOH sample
#define SOH_VALID_ABS_CURRENT_MA_MAX    (100.0f)  // Stable-state current condition
#define SOH_VALID_TEMP_MIN_C            (10.0f)   // Lower temperature limit
#define SOH_VALID_TEMP_MAX_C            (40.0f)   // Upper temperature limit

#define SOH_STABLE_TIME_MS              (30000UL) // Stable-state hold time (30 seconds)
#define SOH_RAW_JUMP_REJECT_THRESHOLD   (5.0f)    // Reject abnormal sudden changes

#define SOH_MIN_VALUE                   (0.0f)
#define SOH_MAX_VALUE                   (100.0f)
/*========================================================
 * Types
 *========================================================*/
typedef struct
{
	uint8_t soh_raw;          // Raw SOH from gauge
	float soh_filtered;       // SOH after EMA filtering
	float voltage_v;          // Battery voltage
	int current_ma;           // Average or instant current
	uint8_t temperature_c;    // Battery temperature
	uint8_t soc;              // RSOC
	bool fc;                  // Full Charge flag
} BatteryStatus_t;

typedef struct
{
    bool initialized;

    bool fc_sample_latched;         // Whether a sample has already been accepted in this FC period
    bool stable_timer_running;      // Whether the stable-condition timer is running

    uint32_t stable_start_ms;       // Stable-condition start time

    float soh_filtered;             // Internal filtered value
    float last_sampled_soh_raw;     // Last accepted raw SOH
} SOHFilter_t;

/*========================================================
 * Platform-dependent function prototypes
 *========================================================*/

/* NVM save/load functions (optional but recommended) */
static bool app_nvm_load_soh_filtered(uint8_t *soh_filtered);
static bool app_nvm_save_soh_filtered(uint8_t soh_filtered);

/*========================================================
 * Utility
 *========================================================*/
static float clampf(float x, float min_v, float max_v)
{
    if (x < min_v) return min_v;
    if (x > max_v) return max_v;
    return x;
}

static float absf_local(float x)
{
    return (x >= 0.0f) ? x : -x;
}

/*========================================================
 * Battery status acquisition
 *========================================================*/
bool battery_status_read(BatteryStatus_t *st)
{
    if (st == NULL)
    {
        return false;
    }

	S_M1_Power_Status_t SystemPowerStatus;

	battery_power_status_get(&SystemPowerStatus);
    st->soc = SystemPowerStatus.battery_level;
    st->soh_raw = SystemPowerStatus.battery_health;
    st->voltage_v = SystemPowerStatus.battery_voltage;
    st->current_ma = SystemPowerStatus.consumption_current;
    st->temperature_c = SystemPowerStatus.battery_temp;
    st->fc = SystemPowerStatus.isFull;
    st->soh_raw = clampf(st->soh_raw, SOH_MIN_VALUE, SOH_MAX_VALUE);

    return (SystemPowerStatus.soh_state == 3);
    //return true;
}

/*========================================================
 * SOH filter core
 *========================================================*/
void soh_filter_init(SOHFilter_t *filter, uint8_t initial_soh)
{
    if (filter == NULL)
    {
        return;
    }

    filter->initialized = true;
    filter->fc_sample_latched = false;
    filter->stable_timer_running = false;
    filter->stable_start_ms = 0U;
    filter->soh_filtered = clampf(initial_soh, SOH_MIN_VALUE, SOH_MAX_VALUE);
    filter->last_sampled_soh_raw = filter->soh_filtered;
}
#if 0
bool soh_filter_init_from_nvm(SOHFilter_t *filter, float fallback_soh)
{
    float saved_soh = 0.0f;

    if (filter == NULL)
    {
        return false;
    }

    if (app_nvm_load_soh_filtered(&saved_soh))
    {
        soh_filter_init(filter, saved_soh);
        return true;
    }

    soh_filter_init(filter, fallback_soh);
    return false;
}
#endif
static bool soh_is_valid_sampling_condition(const BatteryStatus_t *st)
{
    if (st == NULL)
    {
        return false;
    }

    if (!st->fc)
    {
        return false;
    }

    if (st->soc < SOH_VALID_SOC_MIN)
    {
        return false;
    }

    if (absf_local(st->current_ma) > SOH_VALID_ABS_CURRENT_MA_MAX)
    {
        return false;
    }

    if (st->temperature_c < SOH_VALID_TEMP_MIN_C ||
        st->temperature_c > SOH_VALID_TEMP_MAX_C)
    {
        return false;
    }

    if (st->soh_raw < SOH_MIN_VALUE || st->soh_raw > SOH_MAX_VALUE)
    {
        return false;
    }

    return true;
}

static bool soh_is_stable_time_elapsed(SOHFilter_t *filter, uint32_t now_ms)
{
    if (filter == NULL)
    {
        return false;
    }

    if (!filter->stable_timer_running)
    {
        return false;
    }

    return ((uint32_t)(now_ms - filter->stable_start_ms) >= SOH_STABLE_TIME_MS);
}

static float soh_apply_ema(float prev_filtered, float raw_soh)
{
    float alpha = (raw_soh < prev_filtered) ? SOH_FILTER_ALPHA_DOWN : SOH_FILTER_ALPHA_UP;
    float out = prev_filtered + alpha * (raw_soh - prev_filtered);

    return clampf(out, SOH_MIN_VALUE, SOH_MAX_VALUE);
}

/*
 * return true  : A new SOH sample was accepted and the filter was updated in this call
 * return false : No update
 */
bool soh_filter_update(SOHFilter_t *filter, const BatteryStatus_t *st)
{
    uint32_t now_ms;
    float new_filtered;

    if (filter == NULL || st == NULL || !filter->initialized)
    {
        return false;
    }

    now_ms = xTaskGetTickCount();

    /* Release the latch when FC is cleared, so the next FC event can be sampled */
    if (!st->fc)
    {
        filter->fc_sample_latched = false;
        filter->stable_timer_running = false;
        filter->stable_start_ms = 0U;
        return false;
    }

    /* Do not update again if a sample has already been accepted in this FC period */
    if (filter->fc_sample_latched)
    {
        return false;
    }

    /* Reset the stable timer if the sampling condition is not satisfied */
    if (!soh_is_valid_sampling_condition(st))
    {
        filter->stable_timer_running = false;
        filter->stable_start_ms = 0U;
        return false;
    }

    /* Record the first time when the stable condition is satisfied */
    if (!filter->stable_timer_running)
    {
        filter->stable_timer_running = true;
        filter->stable_start_ms = now_ms;
        return false;
    }

    /* Stable time has not elapsed yet */
    if (!soh_is_stable_time_elapsed(filter, now_ms))
    {
        return false;
    }

    /* Reject abnormal sudden-change samples */
    if (absf_local(st->soh_raw - filter->last_sampled_soh_raw) > SOH_RAW_JUMP_REJECT_THRESHOLD)
    {
        filter->fc_sample_latched = true;
        filter->stable_timer_running = false;
        filter->stable_start_ms = 0U;
        return false;
    }

    /* Apply EMA */
    new_filtered = soh_apply_ema(filter->soh_filtered, st->soh_raw);

    filter->soh_filtered = new_filtered;
    filter->last_sampled_soh_raw = st->soh_raw;

    /* Accept only one sample per FC event */
    filter->fc_sample_latched = true;
    filter->stable_timer_running = false;
    filter->stable_start_ms = 0U;

    /* Save if needed */
    (void)app_nvm_save_soh_filtered(filter->soh_filtered);

    return true;
}

float soh_filter_get_value(const SOHFilter_t *filter)
{
    if (filter == NULL || !filter->initialized)
    {
        return 100.0f;
    }

    return filter->soh_filtered;
}

uint8_t soh_filter_get_display_value_1pct(const SOHFilter_t *filter)
{
    float value;

    if (filter == NULL || !filter->initialized)
    {
        return 100U;
    }

    value = clampf(filter->soh_filtered, SOH_MIN_VALUE, SOH_MAX_VALUE);
    return (uint8_t)(value + 0.5f);
}

uint8_t soh_filter_get_display_value_5pct(const SOHFilter_t *filter)
{
    uint8_t value_1pct;

    value_1pct = soh_filter_get_display_value_1pct(filter);

    return (uint8_t)(((value_1pct + 2U) / 5U) * 5U);
}

/*========================================================
 *
 *========================================================*/
bool app_nvm_load_soh_filtered(uint8_t *soh_filtered)
{
	uint8_t nvm_soh = 100;
	// read
	if(nvm_soh > 100)
		return false;

	*soh_filtered = nvm_soh;
	return true;
}

bool app_nvm_save_soh_filtered(uint8_t soh_filtered)
{
	// save
	return true;
}

/*========================================================
 * Example application task/service
 *========================================================*/
static SOHFilter_t g_soh_filter;

void app_soh_service_init(void)
{
    uint8_t saved_soh;

    if (app_nvm_load_soh_filtered(&saved_soh))
    {
        soh_filter_init(&g_soh_filter, saved_soh);
    }
    else
    {
        soh_filter_init(&g_soh_filter, 100);
    }
}

/* Recommended call interval: 2 seconds */
void app_soh_service_process(void)
{
    BatteryStatus_t st;
    bool updated;

    if (!battery_status_read(&st))
    {
        return;
    }

    updated = soh_filter_update(&g_soh_filter, &st);

    if (updated)
    {

    }
}

float app_get_filtered_soh(void)
{
    return soh_filter_get_value(&g_soh_filter);
}

uint8_t app_get_filtered_soh_ui(void)
{
    return soh_filter_get_display_value_1pct(&g_soh_filter);
}

