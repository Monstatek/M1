/* See COPYING.txt for license details. */

/*
*
* m1_watchdog.h
*
* Watchdog functions
*
* M1 Project
*
*/

#ifndef M1_WATCHDOG_H_
#define M1_WATCHDOG_H_

typedef enum
{
	M1_REPORT_ID_SYSTEM_PERIODIC_TASK = 0,
	// More tasks here if needed
	M1_REPORT_ID_END_OF_LIST
} S_M1_WDT_Report_ID;

typedef struct
{
	S_M1_WDT_Report_ID report_id;
	bool inactive;
	uint32_t report_period;
	uint32_t min_rpt_percent, max_rpt_percent;
	uint32_t run_time;
} S_M1_WDT_Report;

void m1_wdt_init(void);
void m1_wdt_report_init(void);
void m1_wdt_send_report(S_M1_WDT_Report_ID rpt_id, uint32_t time);
void m1_wdt_send_report_ex(S_M1_WDT_Report_ID rpt_id, TickType_t start_time);
void m1_wdt_send_delayed_report(S_M1_WDT_Report_ID rpt_id, uint32_t delay_ms, uint8_t repeat);
void m1_wdt_reset(void);
void m1_wdt_kick(void);

/* Read-only accessor for the watchdog task's own handle (private/static to
 * m1_watchdog.c otherwise) -- exists solely so a diagnostic (mtest 5, see
 * m1_cli.c) can report its stack high-water mark alongside every other
 * production task's. Never used to control/modify the task itself. */
TaskHandle_t m1_wdt_get_task_handle(void);

#endif /* M1_WATCHDOG_H_ */
