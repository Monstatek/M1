/* See COPYING.txt for license details. */

/*
*
* m1_feedback_task.h
*
* Owns the feedback manager's dedicated 1 ms tick task (reference section
* 3.1: "The manager must tick at 1 ms or use an equivalent timer mechanism
* capable of meeting Section 14"). This is the ONLY task that calls
* fb_manager_tick() -- see the task/ISR contract in m1_feedback_manager.h.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_TASK_H_
#define M1_FEEDBACK_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the feedback manager (wiring in the LP5814/buzzer hardware
 * adapter) and starts the 1 ms tick task. Call once from m1_tasks_init(),
 * after the LP5814 and buzzer peripherals are initialized. */
void m1_feedback_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_FEEDBACK_TASK_H_ */
