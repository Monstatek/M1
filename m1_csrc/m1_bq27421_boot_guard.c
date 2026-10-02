/* See COPYING.txt for license details. */

/*
 * m1_bq27421_boot_guard.c -- see m1_bq27421_boot_guard.h.
 */
#include "m1_bq27421_boot_guard.h"
#include <stddef.h>

bool bq27421_wait_for_itpor(const bq27421_boot_guard_ops_t *ops, uint16_t max_attempts)
{
    uint16_t attempt;

    if ((ops == NULL) || (ops->itpor_ready == NULL) || (ops->delay_ms == NULL)) {
        return false;
    }

    for (attempt = 0U; attempt < max_attempts; attempt++) {
        if (ops->itpor_ready()) {
            return true;
        }
        ops->delay_ms(1U);
    }
    return false;   /* exhausted: no wraparound, no reinterpretation as success */
}

bool bq27421_wait_for_current_stable(const bq27421_boot_guard_ops_t *ops, uint16_t max_attempts)
{
    uint16_t attempt;

    if ((ops == NULL) || (ops->read_current_mA == NULL) || (ops->delay_ms == NULL) ||
        (ops->wdt_kick == NULL)) {
        return false;
    }

    for (attempt = 0U; attempt < max_attempts; attempt++) {
        int16_t current_mA = ops->read_current_mA();
        if ((current_mA > 100) || (current_mA < -2)) {
            return true;
        }
        ops->delay_ms(10U);
        ops->wdt_kick();   /* legitimate only inside this bounded window */
    }
    return false;   /* exhausted: caller must continue in a degraded state, not loop again */
}
