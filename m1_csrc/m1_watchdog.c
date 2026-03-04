/* See COPYING.txt for license details. */

/*
*
* m1_watchdog.c
*
* Watchdog functions
*
* M1 Project
*
* Reference: https://mcuoneclipse.com/2023/03/26/using-a-watchdog-timer-with-an-rtos/
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_watchdog.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Watchdog"

#define M1_WDT_TIMEOUT					IWDG_RELOAD
#define M1_WDT_SYSTEM_CHECK_TIMEOUT		(2*M1_WDT_TIMEOUT)

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static S_M1_WDT_Report wdt_report[M1_REPORT_ID_END_OF_LIST];

static TaskHandle_t m1_wdt_task_hdl;

static uint16_t m1_wdt_check_count = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_wdt_init(void);
void m1_wdt_report_init(void);
void m1_wdt_send_report(S_M1_WDT_Report_ID rpt_id, uint32_t time);
void m1_wdt_send_report_ex(S_M1_WDT_Report_ID rpt_id, TickType_t start_time);
void m1_wdt_send_delayed_report(S_M1_WDT_Report_ID rpt_id, uint32_t delay_ms, uint8_t repeat);
void m1_wdt_reset(void);
static void m1_wdt_add_task_to_report(S_M1_WDT_Report_ID rpt_id, uint32_t rpt_period, uint32_t min_rpt_percent, uint32_t max_rpt_percent);
static void m1_wdt_handler_task(void *param);
static void m1_wdt_checkin(void);
static void m1_wdt_checkin_check(void);
static void m1_wdt_checkout(void);
static void m1_wdt_checkout_check(void);
static void m1_wdt_system_check(void);
static void m1_wdt_failure_handler(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/*
*	This function initializes the device watchdog system
*
*/
/******************************************************************************/
void m1_wdt_init(void)
{
	BaseType_t ret;
	size_t free_heap;

	/*
	 * Freeze the IWDG counter when the CPU is halted by the debugger.
	 * Without this, the IWDG fires a reset during breakpoints/stepping,
	 * making debugging impossible with ST-Link/J-Link.
	 * This only has effect when the debug port is connected; in production
	 * (no debugger attached), this bit is ignored by the hardware.
	 */
#if defined(__HAL_DBGMCU_FREEZE_IWDG)
	__HAL_DBGMCU_FREEZE_IWDG();
#elif defined(DBGMCU_APB1FZR1_DBG_IWDG_STOP)
	SET_BIT(DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_IWDG_STOP);
#endif

	if(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) // Watchdog caused reset?
	{
		m1_device_stat.dev_reset_by_wdt = true;
		M1_LOG_I(M1_LOGDB_TAG, "Device reset by Watchdog!\r\n");
	}
	else // Reset by normal cause
	{
		m1_device_stat.dev_reset_by_wdt = false;
		M1_LOG_I(M1_LOGDB_TAG, "Device reset by normal cause\r\n");
	}
	__HAL_RCC_CLEAR_RESET_FLAGS(); // clears reset flags

	m1_wdt_report_init();

	ret = xTaskCreate(m1_wdt_handler_task, "m1_wdt_handler_task_n", M1_TASK_STACK_SIZE_DEFAULT, NULL, TASK_PRIORITY_WDT_HANDLER, &m1_wdt_task_hdl);
	assert(ret==pdPASS);
	assert(m1_wdt_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);
} // void m1_wdt_init(void)



/******************************************************************************/
/*
*	This function handles the device watchdog system
*
*/
/******************************************************************************/
static void m1_wdt_handler_task(void *param)
{
	static uint32_t run_time = 0;

	M1_LOG_I(M1_LOGDB_TAG, "WDT task started\r\n");
	while (1)
	{
		m1_wdt_checkin();
		vTaskDelay(pdMS_TO_TICKS(M1_WDT_TIMEOUT/2));
		run_time += M1_WDT_TIMEOUT/2;
		m1_wdt_checkout();
		if ( run_time >= M1_WDT_SYSTEM_CHECK_TIMEOUT )
		{
			m1_wdt_system_check();
			run_time = 0;
		} // if ( run_time >= M1_WDT_SYSTEM_CHECK_TIMEOUT )
		xTaskNotify(idle_task_hdl, 0, eSetValueWithoutOverwrite);
	} // while (1)
} // static void m1_wdt_handler_task(void *param)



/******************************************************************************/
/*
*	This function initializes the report table
*
*/
/******************************************************************************/
void m1_wdt_report_init(void)
{
	uint8_t i;

	for(i=0; i<M1_REPORT_ID_END_OF_LIST; i++)
	{
		wdt_report[i].run_time = 0;
	}
	m1_wdt_add_task_to_report(M1_REPORT_ID_BUTTONS_HANDLER_TASK, M1_WDT_SYSTEM_CHECK_TIMEOUT, 70, 120);
	// Add other tasks here if needed
} // void m1_wdt_report_init(void)



/******************************************************************************/
/*
*	This function adds an entry to the report table
*
*/
/******************************************************************************/
void m1_wdt_add_task_to_report(S_M1_WDT_Report_ID rpt_id, uint32_t rpt_period, uint32_t min_rpt_percent, uint32_t max_rpt_percent)
{
	if ( rpt_id < M1_REPORT_ID_END_OF_LIST )
	{
		wdt_report[rpt_id].report_id = rpt_id;
		wdt_report[rpt_id].inactive = false;
		wdt_report[rpt_id].report_period = rpt_period;
		wdt_report[rpt_id].min_rpt_percent = min_rpt_percent;
		wdt_report[rpt_id].max_rpt_percent = max_rpt_percent;
	} // if ( rpt_id < M1_REPORT_ID_END_OF_LIST )
	else
	{
		M1_LOG_E(M1_LOGDB_TAG, "Wrong task ID %d\r\n", rpt_id);
	}
} // void m1_wdt_add_task_to_report(S_M1_WDT_Report_ID rpt_id, uint32_t rpt_period, uint32_t min_rpt_percent, uint32_t max_rpt_percent)



/******************************************************************************/
/*
*	This function checks the wdt state at check-in
*
*/
/******************************************************************************/
static void m1_wdt_checkin_check(void)
{
	if ( m1_wdt_check_count!=0x5555 )
	{
		M1_LOG_E(M1_LOGDB_TAG, "WDT went wrong!\r\n");
		m1_wdt_failure_handler();
	}
	m1_wdt_check_count += 0x1111;
} // static void m1_wdt_checkin_check(void)



/******************************************************************************/
/*
*	This function checks the wdt state at check-out and update WDT
*
*/
/******************************************************************************/
static void m1_wdt_checkout_check(void)
{
	if ( m1_wdt_check_count != 0x8888 )
	{
		M1_LOG_E(M1_LOGDB_TAG, "WDT went wrong!\r\n");
		m1_wdt_failure_handler();
    }

	// Reset WDT counter
	__HAL_IWDG_RELOAD_COUNTER(&hiwdg);

	if ( m1_wdt_check_count!=0x8888 )
	{
		M1_LOG_E(M1_LOGDB_TAG, "WDT went wrong!\r\n");
		m1_wdt_failure_handler();
    }

	m1_wdt_check_count = 0; // Reset
} // static void m1_wdt_checkout_check(void)



/******************************************************************************/
/*
*	This function initializes the wdt state at check-in
*
*/
/******************************************************************************/
static void m1_wdt_checkin(void)
{
	m1_wdt_check_count = 0x5555;
	m1_wdt_checkin_check();
} // static void m1_wdt_checkin(void)



/******************************************************************************/
/*
*	This function checks the wdt state at check-out
*
*/
/******************************************************************************/
static void m1_wdt_checkout(void)
{
	m1_wdt_check_count += 0x2222;
	m1_wdt_checkout_check();
} // static void m1_wdt_checkout(void)



/******************************************************************************/
/*
*	This function checks the status of all tasks registered to the report table
*
*/
/******************************************************************************/
static void m1_wdt_system_check(void)
{
	uint32_t min, max;
	uint8_t i;

	for(i=0; i<M1_REPORT_ID_END_OF_LIST; i++)
	{
		min = (wdt_report[i].report_period/100)*wdt_report[i].min_rpt_percent;
		max = (wdt_report[i].report_period/100)*wdt_report[i].max_rpt_percent;
		taskENTER_CRITICAL();
		if ( (wdt_report[i].run_time >= min) && (wdt_report[i].run_time <= max) )
		{
			//M1_LOG_I(M1_LOGDB_TAG, "SysOK\r\n");
			wdt_report[i].run_time = 0;
		}
		else if (wdt_report[i].inactive)
		{
			M1_LOG_W(M1_LOGDB_TAG, "Task ID %d suspended\r\n", wdt_report[i].report_id);
			wdt_report[i].run_time = 0;
		}
		else
		{
			M1_LOG_W(M1_LOGDB_TAG, "WDT failure. Task ID %d, run time: %ld\r\n", wdt_report[i].report_id, wdt_report[i].run_time);
		    m1_wdt_failure_handler();
		} // else
		taskEXIT_CRITICAL();
	} // for(i=0; i<M1_REPORT_ID_END_OF_LIST; i++)
} // static void m1_wdt_system_check(void)




/******************************************************************************/
/*
*	This function returns the start time of a task
*
*/
/******************************************************************************/
TickType_t m1_wdt_get_start_time(void)
{
	return xTaskGetTickCount();
} // TickType_t m1_wdt_get_start_time(void)



/******************************************************************************/
/*
*	This function updates the report table for a task with start time
*
*/
/******************************************************************************/
void m1_wdt_send_report_ex(S_M1_WDT_Report_ID rpt_id, TickType_t start_time)
{
	m1_wdt_send_report(rpt_id, ((xTaskGetTickCount() - start_time)*configTICK_RATE_HZ)/1000);
} // void m1_wdt_send_report_ex(S_M1_WDT_Report_ID rpt_id, TickType_t start_time)



/******************************************************************************/
/*
*	This function updates the report table for a task
*
*/
/******************************************************************************/
void m1_wdt_send_report(S_M1_WDT_Report_ID rpt_id, uint32_t time)
{
	if ( rpt_id < M1_REPORT_ID_END_OF_LIST )
	{
		//taskENTER_CRITICAL();
		if ( rpt_id < M1_REPORT_ID_END_OF_LIST)
		{
			wdt_report[rpt_id].run_time += time;
		} // if ( rpt_id < M1_REPORT_ID_END_OF_LIST)
		else
		{
			M1_LOG_E(M1_LOGDB_TAG, "WDT went wrong!\r\n");
			vTaskDelay(1);
			m1_wdt_failure_handler();
	    } // else
		//taskEXIT_CRITICAL();
	} // if ( rpt_id < M1_REPORT_ID_END_OF_LIST )
} // void m1_wdt_send_report(S_M1_WDT_Report_ID rpt_id, uint32_t time)




/******************************************************************************/
/*
*	This function updates the report table for a task with long run time
*
*/
/******************************************************************************/
void m1_wdt_send_delayed_report(S_M1_WDT_Report_ID rpt_id, uint32_t delay_ms, uint8_t repeat)
{
	uint8_t i;

	for (i=0; i<repeat; i++)
	{
		vTaskDelay(pdMS_TO_TICKS(delay_ms));
		m1_wdt_send_report(rpt_id, delay_ms);
	}
} // void m1_wdt_send_delayed_report(S_M1_WDT_Report_ID rpt_id, uint32_t delay_ms, uint8_t repeat)




/******************************************************************************/
/*
*	This function handles the WDT failure case
*
*/
/******************************************************************************/
static void m1_wdt_failure_handler(void)
{
	/* Disable interrupts to prevent further damage, then spin.
	 * The IWDG will eventually fire and reset the system cleanly.
	 * In debug mode (IWDG frozen), this acts as a breakpoint-friendly
	 * infinite loop for post-mortem inspection. */
	__disable_irq();
	while (1)
	{
		__asm("nop");
	} // while (1)
} // static void m1_wdt_failure_handler(void)



/******************************************************************************/
/*
*	This function resets the WDT
*
*/
/******************************************************************************/
void m1_wdt_reset(void)
{
	// Reset WDT counter
	__HAL_IWDG_RELOAD_COUNTER(&hiwdg);
} // void m1_wdt_reset(void)
