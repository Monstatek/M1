/* See COPYING.txt for license details. */

/*
*
* m1_sys_init.c
*
* System init functions at power up
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "m1_lcd.h"
#include "m1_lp5814.h"
#include "m1_bq27421.h"
#include "m1_power_ctl.h"
#include "m1_bq25896.h"
#include "m1_log_debug.h"
#include "m1_i2c.h"
#include "m1_rf_spi.h"
#include "m1_sdcard.h"
#include "m1_esp32_hal.h"
#include "battery.h"
#include "m1_gpio.h"
#include "m1_log_debug.h"
/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Sys_Init"

#define TASKDELAY_sys_init_task		1 // ms

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

TaskHandle_t		sys_init_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_system_init(void);
void m1_system_init_task(void *param);
void m1_system_GPIO_init(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/*
*	This function initializes the device system before the OS starts
*
*/
/******************************************************************************/
void m1_system_init(void)
{
	BaseType_t ret;
	size_t free_heap;

	m1_system_GPIO_init();

	/* SD mount performs canonical-folder provisioning before the remaining
	 * application tasks start. FatFs LFN operations use substantial stack, so
	 * the initialization task needs the existing 1024-word allocation. */
	ret = xTaskCreate(m1_system_init_task, "m1_system_init_task_n", M1_TASK_STACK_SIZE_1024, NULL, TASK_PRIORITY_SYS_INIT, &sys_init_task_hdl);
	assert(ret==pdPASS);
	assert(sys_init_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);
} // void m1_system_init(void)



/******************************************************************************/
/*
*	This function initializes the device GPIOs to default settings
*
*/
/******************************************************************************/
void m1_system_GPIO_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    HAL_GPIO_WritePin(EN_EXT_3V3_GPIO_Port, EN_EXT_3V3_Pin, GPIO_PIN_RESET); // Low means external 3V3 disabled
    HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_RESET); // Low means external 5V disabled
    HAL_GPIO_WritePin(EN_EXT2_5V_GPIO_Port, EN_EXT2_5V_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    for(i=M1_EXT_GPIO_FIRST_ID; i<M1_EXT_GPIO_LIST_N; i++)
    {
    	GPIO_InitStruct.Pin = m1_ext_gpio[i].gpio_pin;
    	HAL_GPIO_Init(m1_ext_gpio[i].gpio_port, &GPIO_InitStruct);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF0_SWJ;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; // Pulldown for SWCLK
    GPIO_InitStruct.Pin = SWCLK_Pin;
    HAL_GPIO_Init(SWCLK_GPIO_Port, &GPIO_InitStruct); // SWCLK
    GPIO_InitStruct.Pull = GPIO_PULLUP; // Pullup for SWDIO
    GPIO_InitStruct.Pin = SWDIO_Pin;
    HAL_GPIO_Init(SWDIO_GPIO_Port, &GPIO_InitStruct); // SWDIO

    HAL_GPIO_WritePin(BAT_OTG_GPIO_Port, BAT_OTG_Pin, GPIO_PIN_RESET); // Low means boost mode (OTG) disabled

    HAL_GPIO_WritePin(Display_CS_GPIO_Port, Display_CS_Pin, GPIO_PIN_SET); // High means in inactive for SPI communication
    HAL_GPIO_WritePin(Display_RST_GPIO_Port, Display_RST_Pin, GPIO_PIN_SET); // High means reset inactive

    HAL_GPIO_WritePin(IR_DRV_GPIO_Port, IR_DRV_Pin, GPIO_PIN_RESET); // Low means inactive for infrared

    HAL_GPIO_WritePin(SPK_CTRL_GPIO_Port, SPK_CTRL_Pin, GPIO_PIN_RESET); // Low means inactive for speaker

    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET); // Low means power off

    HAL_GPIO_WritePin(SI4463_CS_GPIO_Port, SI4463_CS_Pin, GPIO_PIN_SET); // High means inactive for SPI communication
    HAL_GPIO_WritePin(SI4463_ENA_GPIO_Port, SI4463_ENA_Pin, GPIO_PIN_SET); // High means shutdown mode

    HAL_GPIO_WritePin(NFC_CS_GPIO_Port, NFC_CS_Pin, GPIO_PIN_SET); // High means inactive for SPI communication

    /* Put radio in shutdown */
    HAL_GPIO_WritePin(SI4463_ENA_GPIO_Port, SI4463_ENA_Pin, GPIO_PIN_SET);

	// Default IO setting for the SI4463 RF switch - isolated with antenna
	HAL_GPIO_WritePin(SI4463_RFSW1_GPIO_Port, SI4463_RFSW1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(SI4463_RFSW2_GPIO_Port, SI4463_RFSW2_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET); // Low means inactive for RFID circuit
    HAL_GPIO_WritePin(RFID_OUT_GPIO_Port, RFID_OUT_Pin, GPIO_PIN_RESET); // Low means inactive for RFID circuit

	// Default IO setting for spare PD3
	HAL_GPIO_WritePin(PD3_GPIO_Port, PD3_Pin, GPIO_PIN_RESET);

	// Freeze IWDG counter in debug mode
#ifndef M1_APP_IWDT_IN_DEBUG_MODE_ENABLE
	__HAL_DBGMCU_FREEZE_IWDG();
#endif // #ifndef M1_APP_IWDT_IN_DEBUG_MODE_ENABLE

} // void m1_system_GPIO_init(void)



/******************************************************************************/
/*
*	This function initializes the device system after the OS starts and
*	deletes itself
*
*/
/******************************************************************************/

void m1_system_init_task(void *param)
{
	while (1)
	{
#if ( osCMSIS < 0x20000 )
		if (osKernelRunning())
#else
		if (osKernelGetState()==osKernelRunning)
#endif // else
		{
			// Known issue with freeRTOS
			// https://community.st.com/t5/stm32-mcus-embedded-software/hal-tick-problem/td-p/598944
			/*
			This is a known issue with FreeRTOS.  Any function that creates a FreeRTOS task/mutex/queue/etc.
			and is called before osKernelStart() will disable interrupts.  This includes the usual MX_FREERTOS_Init() function.
			So any code you have between MX_FREERTOS_Init() and osKernelStart() will run with interrupts disabled.
			*/
			startup_device_init();
			//  MX_X_CUBE_NFC6_Init();
			m1_i2c_hal_init(&hi2c1);
			m1_spi_hal_init(&hspi2);
			lp5814_init();
			m1_lcd_init(&hspi1);

			m1_sdcard_init(&hsd1);
			//m1_esp32_init();
#if defined M1_DEBUG_CLI_ENABLE
			/* USART1 default config */
			huart_logdb.Init.BaudRate = LOG_DEBUG_UART_BAUD;
			huart_logdb.Init.WordLength = UART_WORDLENGTH_8B;
			huart_logdb.Init.StopBits = UART_STOPBITS_1;
			huart_logdb.Init.Parity = UART_PARITY_NONE;
			m1_logdb_init();
#endif // #if defined M1_DEBUG_CLI_ENABLE
			battery_service_init();
			m1_wdt_init();
			m1_tasks_init();
			startup_config_handler();
			M1_LOG_I(M1_LOGDB_TAG, "Power-up init done!\r\n");
			vTaskDelete(NULL); // Delete this task
		} // if (osKernelGetState()==osKernelRunning)
		else
		{
	        vTaskDelay(pdMS_TO_TICKS(TASKDELAY_sys_init_task));
		}
	} // while (1)
} // void m1_system_init_task(void *param)
