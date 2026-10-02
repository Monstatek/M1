/* See COPYING.txt for license details. */

/*
 * m1_bq27421_boot_guard.h
 *
 * Pure, injectable control-flow for the two bounded-wait loops inside
 * bq27421_init() (m1_bq27421.c): waiting for the gauge's ITPOR (reset)
 * flag after a forced reset, and waiting for the average current reading
 * to stabilize after a golden-image update. Both were previously inline,
 * hardware-coupled loops with two related defects:
 *
 *   1. The ITPOR wait used `while (timeout--)` -- a uint16_t post-decrement
 *      in the loop CONDITION. On full expiry this wraps to 0xFFFF, and the
 *      caller's `if (timeout)` then reads that wrapped nonzero value as
 *      "succeeded", silently converting a genuine gauge-unresponsive
 *      timeout into a false "success" that falls through to the golden-
 *      image-update path against a gauge that never actually reset.
 *   2. The post-update current-stabilization wait was a bare `while(1)`
 *      with no bound at all, kicking the watchdog on every pass -- an
 *      unresponsive gauge here defeats the one failsafe (IWDG) that would
 *      otherwise recover the device, hanging boot forever. This is the
 *      class of defect confirmed reachable on the factory/new-unit
 *      configuration path (design-capacity/taper-rate mismatch).
 *
 * Extracted here, host-testable with injected mock operations, so the
 * bounded-wait control flow itself (not the real I2C/HAL calls) can be
 * proven correct independent of hardware: immediate success, success on
 * the final permitted attempt, complete timeout, and -- critically -- no
 * wraparound path exists that can convert a timeout into a reported
 * success.
 */
#ifndef M1_BQ27421_BOOT_GUARD_H_
#define M1_BQ27421_BOOT_GUARD_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Returns true once the gauge reports ITPOR (reset complete). */
    bool (*itpor_ready)(void);
    /* Returns the gauge's current average-current reading (mA). */
    int16_t (*read_current_mA)(void);
    /* Blocks for approximately this many milliseconds. */
    void (*delay_ms)(uint32_t ms);
    /* Feeds the independent watchdog once. */
    void (*wdt_kick)(void);
} bq27421_boot_guard_ops_t;

/* Polls ops->itpor_ready() up to max_attempts times, ops->delay_ms(1)
 * between attempts. Returns true the moment it reports ready; returns
 * false (never wraps, never reinterprets exhaustion as success) if
 * max_attempts is reached with no ready report. max_attempts == 0 returns
 * false immediately without polling. */
bool bq27421_wait_for_itpor(const bq27421_boot_guard_ops_t *ops, uint16_t max_attempts);

/* Polls ops->read_current_mA() up to max_attempts times, ops->delay_ms(10)
 * and ops->wdt_kick() between attempts (kicking the watchdog is legitimate
 * ONLY within this bounded window -- calibration genuinely can take a few
 * seconds; it must never be unconditional). "Stable" per the gauge's own
 * existing acceptance band: current > 100 mA or < -2 mA. Returns true the
 * moment the reading is in-band; returns false (no further watchdog kicks,
 * no further waiting) if max_attempts is reached first -- the caller must
 * then continue boot in a defined degraded state rather than loop again. */
bool bq27421_wait_for_current_stable(const bq27421_boot_guard_ops_t *ops, uint16_t max_attempts);

#endif /* M1_BQ27421_BOOT_GUARD_H_ */
