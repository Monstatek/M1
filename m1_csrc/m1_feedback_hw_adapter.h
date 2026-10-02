/* See COPYING.txt for license details. */

/*
*
* m1_feedback_hw_adapter.h
*
* The ONE fb_hw_adapter_t implementation allowed to call into the LP5814
* RGB/backlight driver (m1_lp5814.c) and the buzzer driver (m1_buzzer.c)
* for feedback purposes. Diagnostic/factory CLI bypasses documented in
* documentation/M1_FEEDBACK_RECONCILIATION.md are the only exception.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_HW_ADAPTER_H_
#define M1_FEEDBACK_HW_ADAPTER_H_

#include "m1_feedback_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the adapter's fb_hw_adapter_t for fb_manager_init(). Call once at
 * startup, after the LP5814 and buzzer peripherals are initialized. */
const fb_hw_adapter_t *m1_feedback_hw_adapter_get(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_FEEDBACK_HW_ADAPTER_H_ */
