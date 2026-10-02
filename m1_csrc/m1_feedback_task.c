/* See COPYING.txt for license details. */

/*
*
* m1_feedback_task.c
*
* Dedicated FreeRTOS task driving fb_manager_tick() at a nominal 1 ms
* period. See m1_feedback_manager.h for the task/ISR contract: this is the
* only function permitted to call fb_manager_tick(), and therefore the
* only feedback-manager code path that may block on the hardware adapter
* (I2C to the LP5814, etc).
*
* M1 Project
*
*/

#include "m1_feedback_task.h"
#include "m1_feedback_manager.h"
#include "m1_feedback_orchestration.h"
#include "m1_feedback_hw_adapter.h"
#include "m1_tasks.h"
#include "FreeRTOS.h"
#include "task.h"

/* 1 ms nominal period (reference section 3.1). A dedicated task at a
 * moderate priority -- above the idle/CLI/UI tasks so scheduling jitter
 * stays within Section 14's tolerances under normal load, but below the
 * time-critical Sub-GHz sampling task (TASK_PRIORITY_SYSTEM_TASK_HANDLER)
 * so feedback never competes with radio sampling timing. This priority
 * choice has not been measured against Section 14 on real hardware; see
 * documentation/M1_FEEDBACK_RECONCILIATION.md. */
#define FEEDBACK_TASK_PERIOD_MS		1u
#define FEEDBACK_TASK_PRIORITY			(tskIDLE_PRIORITY + 6)
#define FEEDBACK_TASK_STACK_SIZE		M1_TASK_STACK_SIZE_1024

static TaskHandle_t g_feedback_task_hdl;

static void feedback_task(void *argument)
{
	TickType_t last_wake = xTaskGetTickCount();

	(void)argument;

	for ( ;; )
	{
		vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FEEDBACK_TASK_PERIOD_MS));
		fb_manager_tick(FEEDBACK_TASK_PERIOD_MS);
	} // for (;;)
} // static void feedback_task(void *argument)



void m1_feedback_task_init(void)
{
	BaseType_t ret;

	fb_manager_init(m1_feedback_hw_adapter_get());
	/* Register the awake baseline before any task can release a temporary
	 * backlight owner. The saver starts awake and only requests a hold on
	 * its first sleep/wake transition, leaving boot otherwise unowned. */
	fb_sleep_timer_wake();

	ret = xTaskCreate(feedback_task, "m1_feedback_task_n", FEEDBACK_TASK_STACK_SIZE,
	                   NULL, FEEDBACK_TASK_PRIORITY, &g_feedback_task_hdl);
	configASSERT(ret == pdPASS);
} // void m1_feedback_task_init(void)
