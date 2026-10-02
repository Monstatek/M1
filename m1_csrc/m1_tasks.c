/* See COPYING.txt for license details. */

/*
*
* m1_tasks.c
*
* Task functions for M1
*
* M1 Project
*
*/


/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "m1_tasks.h"
#include "m1_sdcard.h"
#include "lfrfid.h"
//#include "m1_nfc.h"
#include "nfc_driver.h"
#include "m1_feedback_task.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Tasks"

#define MAIN_QUEUE_ITEMS_MAX_N				256
#define SDCARD_DET_QUEUE_ITEMS_MAX_N		10

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

extern IWDG_HandleTypeDef hiwdg;

//extern osThreadId_t dummytaskHandle;
osTimerId_t dummyTimerHandle;
const osTimerAttr_t dumyTimer_attributes = {
  .name = "dummyTimer"
};

QueueHandle_t	main_q_hdl;
QueueHandle_t	sdcard_det_q_hdl;
TaskHandle_t	runonce_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_tasks_init(void);
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vApplicationMallocFailedHook(void);

void m1_dummy_task(void *argument);
void m1_dummytimer_task(void *argument);
void m1_runonce_task_handler(void *param);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes and creates default tasks for the system
 * after power up.
 */
/*============================================================================*/
void m1_tasks_init(void)
{
	BaseType_t ret;
	size_t free_heap;

	/* M1-FB-STD-001 v1.1 feedback manager: started first so every other
	 * task/module created below can safely call fb_request()/fb_release()
	 * (and the fb_net_..., fb_pwr_..., fb_alert_... orchestration wrappers)
	 * from the moment it starts running. See
	 * documentation/M1_FEEDBACK_RECONCILIATION.md. */
	m1_feedback_task_init();

	main_q_hdl = xQueueCreate(MAIN_QUEUE_ITEMS_MAX_N, sizeof(S_M1_Main_Q_t));
	assert(main_q_hdl != NULL);

	sdcard_det_q_hdl = xQueueCreate(SDCARD_DET_QUEUE_ITEMS_MAX_N, sizeof(S_M1_SdCard_Q_t));
	assert(sdcard_det_q_hdl != NULL);

	dummyTimerHandle = osTimerNew(m1_dummytimer_task, osTimerOnce, NULL, &dumyTimer_attributes);

	ret = xTaskCreate(system_periodic_task, "system_periodic_task_n", M1_TASK_STACK_SIZE_DEFAULT, NULL, TASK_PRIORITY_SYSTEM_TASK_HANDLER, &system_task_hdl);
	assert(ret==pdPASS);
	assert(system_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize(); // xPortGetMinimumEverFreeHeapSize()
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(sdcard_detection_task, "sdcard_detection_task_n", M1_TASK_STACK_SIZE_DEFAULT, NULL, TASK_PRIORITY_SDCARD_HANDLER, &sdcard_task_hdl);
	assert(ret==pdPASS);
	assert(sdcard_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(menu_main_handler_task, "menu_main_handler_task_n", M1_TASK_STACK_SIZE_1024/*M1_TASK_STACK_SIZE_DEFAULT*/, NULL, TASK_PRIORITY_MENU_MAIN_HANDLER, &menu_main_handler_task_hdl);
	assert(ret==pdPASS);
	assert(menu_main_handler_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(subfunc_handler_task, "subfunc_handler_task_n", M1_TASK_STACK_SIZE_4096, NULL, TASK_PRIORITY_SUBFUNC_HANDLER, &subfunc_handler_task_hdl);
	assert(ret==pdPASS);
	assert(subfunc_handler_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(log_db_handler_task, "log_db_handler_task_n", M1_TASK_STACK_SIZE_1024, NULL, TASK_PRIORITY_LOG_DB_HANDLER, &log_db_task_hdl);
	assert(ret==pdPASS);
	assert(log_db_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(idle_handler_task, "idle_handler_task_n", M1_TASK_STACK_SIZE_0512, NULL, TASK_PRIORITY_IDLE_HANDLER, &idle_task_hdl);
	assert(ret==pdPASS);
	assert(idle_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(m1_runonce_task_handler, "m1_runonce_task_n", M1_TASK_STACK_SIZE_0512, NULL, TASK_PRIORITY_RUNONCE_TASK_HANDLER, &runonce_task_hdl);
	assert(ret==pdPASS);
	assert(runonce_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	ret = xTaskCreate(vSer2UsbTask, "m1_ser2usb_task_n", M1_TASK_STACK_SIZE_2048, NULL, TASK_PRIORITY_RUNONCE_TASK_HANDLER, &usb2ser_task_hdl);
	assert(ret==pdPASS);
	assert(usb2ser_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);
} // void m1_tasks_init(void)



/*============================================================================*/
/*
 * This task is called by the OS when the system is in idle state.
 * configUSE_IDLE_HOOK must be set to 1 in FreeRTOSConfig.h
 */
/*============================================================================*/
void vApplicationIdleHook(void)
{
	// This should only be called when the M1 is on M1_OPERATION_MODE_DISPLAY_ON
	// mode to play some screen animation
	// m1_gui_scr_animation();
    //xTaskGetIdleTaskHandle();
    //xTaskGetCurrentTaskHandle();
} // void vApplicationIdleHook(void)



/*============================================================================*/
/*
* The application stack overflow hook is called when a stack overflow is detected for a task.
*
* Details on stack overflow detection can be found here: https://www.FreeRTOS.org/Stacks-and-stack-overflow-checking.html
*
* configCHECK_FOR_STACK_OVERFLOW must be defined (1 or 2) in FreeRTOSConfig.h
*
* @param xTask the task that just exceeded its stack boundaries.
* @param pcTaskName A character string containing the name of the offending task.
*/
/*============================================================================*/

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	UNUSED(xTask);
	UNUSED(pcTaskName);
	taskDISABLE_INTERRUPTS();
	for(;;)
	{
		__asm("nop");
	}
} // void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)



/*============================================================================*/
/*
* The application heap failure hook is called when malloc failure is detected for a task.
*
* configUSE_MALLOC_FAILED_HOOK in FreeRTOSConfig.h
*/
/*============================================================================*/
void vApplicationMallocFailedHook(void)
{
	taskDISABLE_INTERRUPTS();
	for(;;)
	{
		__asm("nop");
	}
	//Error_Handler();
	// These return the available heap space and the least amount of free heap space ever recorded
	//xPortGetFreeHeapSize();
	//xPortGetMinimumEverFreeHeapSize();
	//vTaskGetInfo();
	//uxTaskGetStackHighWaterMark();
} // void vApplicationMallocFailedHook (void)



/*============================================================================*/
/*
* This task does nothing. It's defined as a dummy task to avoid compile error!
*
*/
/*============================================================================*/
void m1_dummy_task(void *argument)
{
  /* USER CODE BEGIN defaultTask */
  /* Infinite loop */
  for(;;)
  {
	  osDelay(portMAX_DELAY);
  }
  /* USER CODE END defaultTask */
} // void m1_dummy_task(void *argument)



/*============================================================================*/
/*
* This task does nothing. It's defined as a dummy task to avoid compile error!
*
*/
/*============================================================================*/
void m1_dummytimer_task(void *argument)
{
  /* USER CODE BEGIN m1_dummytimer_task */
	/* Infinite loop */
	for(;;)
	{
		osDelay(portMAX_DELAY);
	}
  /* USER CODE END m1_dummytimer_task */
} // void m1_dummytimer_task(void *argument)



/*============================================================================*/
/*
* This task runs once after power up
*
*/
/*============================================================================*/
void m1_runonce_task_handler(void *param)
{
	//m1_led_indicator_on(NULL);
	vTaskDelete(NULL); // Delete this task
} // void m1_runonce_task_handler(void *param)

