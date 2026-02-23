/* See COPYING.txt for license details. */

/*
*
* m1_power_ctl.c
*
* Driver and library for battery charger
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "FreeRTOSConfig.h"
#include "main.h"
#include "m1_sys_init.h"
#include "m1_power_ctl.h"
#include "m1_gpio.h"
#include "m1_bq25896.h"
#include "m1_lp5814.h"
#include "m1_bq27421.h"
#include "m1_fusb302.h"
#include "battery.h"
#include "uiView.h"
#include "Res_String.h"


#if 0
static const char * BQ2589X_VBUS_STAT_MSG[8] = {"No input", "USB Host SDP", "Adapter 3.25A", "Err", "Err", "Err", "Err", "USB OTG"};

static const char * BQ2589X_CHRG_STAT_MSG[4] = {"Not charging", "Pre-charge", "Fast Charge", "Complete"};
#endif

#define DELAY_BEFORE_POWER_REBOOT		1000 // ms

//************************** S T R U C T U R E S *******************************

enum {
	VIEW_MODE_BATTERY_NONE,
	VIEW_MODE_BATTERY_INFO,
	VIEW_MODE_POWER_REBOOT,
	VIEW_MODE_POWER_SHUTDOWN,
	VIEW_MODE_BATTERY_END
};


/***************************** V A R I A B L E S ******************************/

float f_monitor;
static TimerHandle_t batt_info_timer_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_setting_power_init(void);
void menu_setting_power_exit(void);

void power_battery_info(void);
void power_reboot(void);
void power_off(void);
//void power_init(void);
void m1_pre_power_down(void);
static void m1_system_drivers_disable(void);
void m1_power_down(void);
uint8_t m1_check_battery_level(uint8_t remaining_charge);
static void battery_info_timer(TimerHandle_t xTimer);

static void battery_info_gui_init(void);
static void battery_info_gui_create(uint8_t param);
static void battery_info_gui_destroy(uint8_t param);
static void battery_info_gui_update(uint8_t param);
static int battery_info_gui_message(void);

static void power_reboot_gui_init(void);
static void power_reboot_gui_create(uint8_t param);
static void power_reboot_gui_destroy(uint8_t param);
static void power_reboot_gui_update(uint8_t param);
static int power_reboot_gui_message(void);

static void power_shutdown_gui_init(void);
static void power_shutdown_gui_create(uint8_t param);
static void power_shutdown_gui_destroy(uint8_t param);
static void power_shutdown_gui_update(uint8_t param);
static int power_shutdown_gui_message(void);

static void battery_info_timer(TimerHandle_t xTimer);

//************************** S T R U C T U R E S *******************************
static const view_func_t view_power_battery_table[] = {
	NULL,
	battery_info_gui_init,
	power_reboot_gui_init,
	power_shutdown_gui_init
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes display for this sub-menu item.
 */
/*============================================================================*/
void menu_setting_power_init(void)
{
	;
} // void menu_infrared_init(void)



/*============================================================================*/
/*
 * This function will exit this sub-menu and return to the upper level menu.
 */
/*============================================================================*/
void menu_setting_power_exit(void)
{
	;
} // void menu_infrared_exit(void)


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void power_battery_info(void)
{
	batt_info_timer_hdl = xTimerCreate("batt_info_timer", TASKDELAY_BATTERY_INFO_TIMER/portTICK_PERIOD_MS, pdTRUE, NULL, battery_info_timer);
	assert(batt_info_timer_hdl!=NULL);
	xTimerStart(batt_info_timer_hdl, 0);

	// initial
	m1_uiView_functions_init(VIEW_MODE_BATTERY_END, view_power_battery_table);
	m1_uiView_display_switch(VIEW_MODE_BATTERY_INFO, 0);

	// loop
	while(m1_uiView_q_message_process())
	{
		;
	}
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void power_reboot(void)
{
	// initial
	m1_uiView_functions_init(VIEW_MODE_BATTERY_END, view_power_battery_table);
	m1_uiView_display_switch(VIEW_MODE_POWER_REBOOT, 0);

	// loop
	while(m1_uiView_q_message_process())
	{
		;
	}
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void power_off(void)
{
	// initial
	m1_uiView_functions_init(VIEW_MODE_BATTERY_END, view_power_battery_table);
	m1_uiView_display_switch(VIEW_MODE_POWER_SHUTDOWN, 0);

	// loop
	while(m1_uiView_q_message_process())
	{
		;
	}
}

/*============================================================================*/
/**
  * @brief  Battery info timer
  * @param
  * @retval None
  */
/*============================================================================*/
static void battery_info_timer(TimerHandle_t xTimer)
{
	S_M1_Main_Q_t q_item;

	q_item.q_evt_type = Q_EVENT_BATTERY_UPDATED;
    xQueueSend(main_q_hdl, &q_item, portMAX_DELAY);
} // static void battery_info_timer(TimerHandle_t xTimer)



/*============================================================================*/
/*
  * @brief  battery info
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int battery_info_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		if ( batt_info_timer_hdl )
		{
			xTimerDelete(batt_info_timer_hdl, 0);
			batt_info_timer_hdl = NULL;
		} // if ( batt_info_timer_hdl )
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )

	return 1;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void battery_info_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void battery_info_gui_destroy(uint8_t param)
{

}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void battery_info_gui_update(uint8_t param)
{
	char stat_msg[32], stat_msg2[16];
	S_M1_Power_Status_t SystemPowerStatus;

	battery_power_status_get(&SystemPowerStatus);

	float battery_voltage = SystemPowerStatus.battery_voltage / 1000.0f + 0.05;

	//get_power_status();
	// stc3115_cli_status();

    /* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
    do
    {
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        for(int i = 0; i < 4; ++i) {
        	int x = i * 32 + 1;
        	u8g2_DrawFrame(&m1_u8g2, x, 45, 30, 18);
        }

        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        for(int i = 0; i < 4; ++i) {
        	int x = i * 32 + 1+6;
        	u8g2_DrawBox(&m1_u8g2, x, 41, 18, 8);
        }

        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

        // 16 x 6
        m1_draw_text(&m1_u8g2, 7, 47,18, res_string(IDS_L), TEXT_ALIGN_CENTER);
        sprintf(stat_msg, "%u%%", SystemPowerStatus.battery_level);
        m1_draw_text(&m1_u8g2, 1, 60, 30, stat_msg, TEXT_ALIGN_CENTER);

        m1_draw_text(&m1_u8g2, 39, 47,18, res_string(IDS_T), TEXT_ALIGN_CENTER);
		sprintf(stat_msg, "%uC", SystemPowerStatus.battery_temp);
		m1_draw_text(&m1_u8g2, 33, 60, 30, stat_msg, TEXT_ALIGN_CENTER);

	    m1_draw_text(&m1_u8g2, 71, 47,18, res_string(IDS_V), TEXT_ALIGN_CENTER);
        m1_float_to_string(stat_msg, battery_voltage, 1);
        strcat(stat_msg, "V");
        m1_draw_text(&m1_u8g2, 65, 60, 30, stat_msg, TEXT_ALIGN_CENTER);

        m1_draw_text(&m1_u8g2, 103, 47,18, res_string(IDS_H), TEXT_ALIGN_CENTER);
        sprintf(stat_msg, "%u%%", SystemPowerStatus.battery_health);
        m1_draw_text(&m1_u8g2, 97, 60, 30, stat_msg, TEXT_ALIGN_CENTER);

        int str_id = IDS_USER_DEFINED;
		if (SystemPowerStatus.fault==0) {
			if (SystemPowerStatus.stat==0) {
				str_id = IDS_CONSUMPTION;
			} else if (SystemPowerStatus.stat==1) {
				str_id = IDS_PRE_CHARGE;
			} else if (SystemPowerStatus.stat==2) {
				str_id = IDS_CHARGING;
			} else if (SystemPowerStatus.stat==3) {
				str_id = IDS_COMPLETE;
			}
		} else if (SystemPowerStatus.fault==1) {
			str_id = IDS_INPUT_FAULT;
		} else if (SystemPowerStatus.fault==2) {
			str_id = IDS_THERMAL_SHUTDOWN;
		} else if (SystemPowerStatus.fault==3) {
			str_id = IDS_SAFETY_TIME_EXP;
		}
		if(str_id < IDS_USER_DEFINED)
			m1_draw_text(&m1_u8g2, 2, 7,120, res_string(str_id), TEXT_ALIGN_LEFT);

        if (SystemPowerStatus.stat==0) {
        	sprintf(stat_msg, "%dmA", SystemPowerStatus.consumption_current);
        	m1_draw_text(&m1_u8g2, 2, 17,120, stat_msg, TEXT_ALIGN_LEFT);
        } else {
			m1_float_to_string(stat_msg, SystemPowerStatus.charge_voltage, 1);
			strcat(stat_msg, "V ");
			sprintf(stat_msg2, "%umA", SystemPowerStatus.charge_current);
			strcat(stat_msg, stat_msg2);
			m1_draw_text(&m1_u8g2, 2, 17,120, stat_msg, TEXT_ALIGN_LEFT);
		}
    } while (u8g2_NextPage(&m1_u8g2));
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int battery_info_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			if(battery_info_kp_handler()==0)
				return 0;	// exit
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_BATTERY_UPDATED )
		{
			// Do other things for this task
			m1_uiView_display_update(0);
		} // else if ( q_item.q_evt_type==Q_EVENT_RFID_COMPLETE )
	} // if (ret==pdTRUE)

	return 1;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void battery_info_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_BATTERY_INFO, battery_info_gui_create, battery_info_gui_update, battery_info_gui_destroy, battery_info_gui_message);
}

/*============================================================================*/
/*
  * @brief  power reboot
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int power_reboot_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) ||
		 (this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK) ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Reboot?
	{
		//Set boot mode in backup registers, if any
		startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_REBOOT);
		//Set registers in RTC, if any
		//Close SD card, if any; // Do other things for this task, if needed
		vTaskDelay(pdMS_TO_TICKS(DELAY_BEFORE_POWER_REBOOT)); // Without this delay, the reboot won't work properly!
		m1_pre_power_down();
		//vTaskEndScheduler();
		NVIC_SystemReset();
	} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )

	return 1;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_reboot_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_reboot_gui_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_reboot_gui_update(uint8_t param)
{
	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 23, 1, 82, 36, m1_device_82x36);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_WARNING_FONT_1B);
	m1_draw_text(&m1_u8g2, 2, 49, 124, res_string(IDS_REBOOT), TEXT_ALIGN_CENTER);
	//u8g2_DrawStr(&m1_u8g2, 35, 49, "REBOOT");

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, res_string(IDS_CANCEL), res_string(IDS_REBOOT1), arrowright_8x8);

	m1_u8g2_nextpage(); // Update display RAM
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int power_reboot_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			if(power_reboot_kp_handler()==0)
				return 0;	// exit
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )

	} // if (ret==pdTRUE)

	return 1;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void power_reboot_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_POWER_REBOOT, power_reboot_gui_create, power_reboot_gui_update, power_reboot_gui_destroy, power_reboot_gui_message);
}

/*============================================================================*/
/*
  * @brief  power reboot
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int power_shutdown_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) ||
		 (this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK) ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Power off?
	{
		m1_power_down();
	} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_shutdown_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_shutdown_gui_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void power_shutdown_gui_update(uint8_t param)
{
	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 23, 1, 82, 36, m1_device_82x36);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_WARNING_FONT_1B);
	m1_draw_text(&m1_u8g2, 2, 49, 124, res_string(IDS_POWER_OFF), TEXT_ALIGN_CENTER);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, res_string(IDS_CANCEL), res_string(IDS_POWER_OFF1), arrowright_8x8);

	m1_u8g2_nextpage(); // Update display RAM
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int power_shutdown_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			if(power_shutdown_kp_handler()==0)
				return 0;	// exit
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )

	} // if (ret==pdTRUE)

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void power_shutdown_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_POWER_SHUTDOWN, power_shutdown_gui_create, power_shutdown_gui_update, power_shutdown_gui_destroy, power_shutdown_gui_message);
}


/*============================================================================*/
/*
 * This function will put the device in the lowest power mode before going to shutdown mode
 */
/*============================================================================*/
void m1_pre_power_down(void)
{
	// Disable IWDG - Start
	HAL_FLASH_Unlock(); // Unlock the FLASH control registers access
	HAL_FLASH_OB_Unlock(); // Unlock the FLASH Option Control Registers access

	// Initialize the Option Bytes structure
	FLASH_OBProgramInitTypeDef OBInit;
	HAL_FLASHEx_OBGetConfig(&OBInit); // Get the Option byte configuration

	// Set the IWDG_SW bit
	OBInit.OptionType = OPTIONBYTE_USER; // USER option byte configuration
	OBInit.USERType = OB_USER_IWDG_STDBY; // Independent watchdog counter freeze in standby mode
	OBInit.USERConfig = OB_IWDG_STDBY_FREEZE; // IWDG counter frozen in STANDBY mode

	// Write the Option Bytes
	HAL_FLASHEx_OBProgram(&OBInit); // Program option bytes

	HAL_FLASH_OB_Launch(); // Launch the option bytes loading.

	HAL_FLASH_OB_Lock(); // Lock the FLASH Option Control Registers access.
	HAL_FLASH_Lock(); // Locks the FLASH control registers access
	// Disable IWDG - End

	// LED Driver shutdown
	lp5814_backlight_on(0); // Turn off back light
	lp5814_all_off_RGB();
	lp5814_shutdown();

	//Set power save mode ON for LCD display
	u8g2_SetPowerSave(&m1_u8g2, true);

	// Turn off battery charger
	;
	// Disable ESP32
	;
	;// NFC/RFID
	// Put all GPIOs in power-saving mode
	;
	// Turn off external power
	;
	m1_system_GPIO_init(); // set all GPIOs to default states
	// ***Turn off all drivers like UART, SPI, I2C, DMA, Timers, etc.

	HAL_PWREx_EnableStandbyIORetention();

	m1_device_stat.op_mode = M1_OPERATION_MODE_SLEEP;

} // void m1_pre_power_down(void)


/*============================================================================*/
/*
 * This function turns off all drivers like UART, SPI, I2C, DMA, Timers, etc.
 */
/*============================================================================*/
static void m1_system_drivers_disable(void)
{
	;
} // static void m1_system_drivers_disable(void)



/*============================================================================*/
/*
 * This function will shutdown the device
 */
/*============================================================================*/
void m1_power_down(void)
{
	// Set GPIOs to expected logic levels
	// Disable WDT
	// Go to Stand-by mode
	__HAL_PWR_CLEAR_FLAG(PWR_WAKEUP_FLAG4); // Clear Wake-up flag
	HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW); // Enable WKUP4 pin, falling edge
	/* clear the RTC Wake UP (WU) flag */
	__HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
	HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
	//HAL_PWREx_EnableWakeUpPin()
	//HAL_SuspendTick();
	m1_pre_power_down();

	bq_shipMode(ENABLE);
	//bq27421_setHiberate();

	m1_system_drivers_disable();

	HAL_PWR_EnterSTANDBYMode(); // Enter Standby mode
} // void m1_power_down(void)


/******************************************************************************/
/**
  * @brief Check the remaining battery charge
  * @param None
  * @retval True if remaining charge is higher than some level
  */
/******************************************************************************/
uint8_t m1_check_battery_level(uint8_t remaining_charge)
{
	S_M1_Power_Status_t SystemPowerStatus;

	battery_power_status_get(&SystemPowerStatus);
    if ( SystemPowerStatus.battery_level >= remaining_charge)
    	return true;

    return false;
} // uint8_t m1_check_battery_level(uint8_t remaining_charge)
