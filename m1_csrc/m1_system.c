/* See COPYING.txt for license details. */

/*
*
* m1_system.c
*
* System functions for M1
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
#include "app_freertos.h"
#include "m1_tasks.h"
#include "m1_power_ctl.h"
#include "m1_fw_update.h"
#include "m1_fw_update_bl.h"
#include "m1_lp5814.h"
#include "battery.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"System"

#define BUTTON_EVENT_ACTIVE		0x01	// A button active event occurs
#define BUTTON_EVENT_RESTORED	0x02	// A button active event restores to idle (released) state

#define SYSTEM_PERIODIC_TASK_DELAY	BUTTON_DEBOUNCE_MS/2 //ms - Period for the task to read buttons' press-release status

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_Buttons_Control buttons_ctl[NUM_BUTTONS_MAX];
S_GPIO_IO_t m1_buttons_io[NUM_BUTTONS_MAX] = 	{	{.gpio_port = GPIOC, .gpio_pin = GPIO_PIN_13},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_11},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_12},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_13},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_14},
													{.gpio_port = GPIOE, .gpio_pin = GPIO_PIN_10}
												};

S_M1_Buttons_Status m1_buttons_status = {	.event= {BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE, BUTTON_EVENT_IDLE},
											.timestamp = 0x00
										};


S_M1_Device_Status_t 	m1_device_stat = {0};
QueueHandle_t 			button_events_q_hdl = NULL;
TaskHandle_t			system_task_hdl;
TaskHandle_t 			idle_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void system_periodic_task(void *param);
void idle_handler_task(void *param);
static void send_button_evt_to_queue(void);
uint8_t m1_button_pressed_check(uint8_t button_id);
void m1_buttons_status_reset(void);
uint32_t TIM_GetCounterCLKValue(uint16_t prescaler);
static void battery_indicator_update(void);
static void lcd_saver_update(void);
void startup_config_handler(void);
void HAL_Delay(uint32_t Delay);
static void startup_bu_registers_init(void);
void startup_info_screen_display(const char *scr_text);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
 * @brief This task handles periodic tasks, e.g. keypad handler
 */
/*============================================================================*/
void system_periodic_task(void *param)
{
    uint32_t temp, current_tick;
    uint8_t this_button_level, i;
    uint8_t event_change;

	// Create Queue.
    button_events_q_hdl = xQueueCreate(1, sizeof(S_M1_Buttons_Status));
    assert(button_events_q_hdl!=NULL);

    while (TRUE)
    {
        event_change = 0x00;
        current_tick = HAL_GetTick();
        for (i=0; i<NUM_BUTTONS_MAX; i++)
        {
        	this_button_level = HAL_GPIO_ReadPin(m1_buttons_io[i].gpio_port, m1_buttons_io[i].gpio_pin);
        	switch ( buttons_ctl[i].status )
        	{
            	case BUTTON_IS_IDLE: // BUTTON_IS_RELEASED
            		if ( this_button_level==buttons_ctl[i].active_level ) // press attempt
            		{
            			buttons_ctl[i].status = BUTTON_PRESS_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
            		if ( buttons_ctl[i].dbc_status==BUTTON_DBC_CLK1 )
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].dbc_counter; // calculate the time past since the first click
            			if ( temp > BUTTON_DBC_MIDDLE ) // timeout for second click, reset
            			{
            				buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE;
            				buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
            				event_change = BUTTON_EVENT_RESTORED;
            			}
            		} // if ( buttons_ctl[i].dbc_status==BUTTON_DBC_CLK1 )
            		break;

            	case BUTTON_PRESS_ATTEMPT:
            		temp = current_tick; // get current timer tick
            		temp -= buttons_ctl[i].click_counter; // calculate the time past for de-bounce
            		if ( temp >= BUTTON_DEBOUNCE_MS )
            		{
            			if ( this_button_level==buttons_ctl[i].active_level ) // still pressed
            			{
            				buttons_ctl[i].status = BUTTON_IS_PRESSED;
            				buttons_ctl[i].event = BUTTON_EVENT_CLICK;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				switch ( buttons_ctl[i].dbc_status )
            				{
                            	case BUTTON_DBC_IDLE:
                            		buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		break;

                            	case BUTTON_DBC_CLK1:
                            		temp = current_tick; // get current timer tick
                            		temp -= buttons_ctl[i].dbc_counter; // calculate the duration between two clicks
                            		if ( temp <= BUTTON_DBC_MIDDLE )
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_CLK2; // valid double click received
                            			buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		}
                            		else
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // invalid double click received, reset
                            		}
                            		break;

                            	case BUTTON_DBC_CLK2:
                            		buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // new single click received, reset
                            		break;

                            	default:
                            		break;
            				} // switch ( buttons_ctl[i].dbc_status )
            				event_change = BUTTON_EVENT_ACTIVE;
            			} // if ( this_button_level==buttons_ctl[i].active_level )
            			else // not a valid press (logic 0 read)
            			{
            				buttons_ctl[i].status = BUTTON_IS_IDLE;
            			}
            		} // if ( temp >= BUTTON_DEBOUNCE_MS )
            		break;

            	case BUTTON_IS_PRESSED:
            		if ( this_button_level!=buttons_ctl[i].active_level ) // release attempt
            		{
            			buttons_ctl[i].status = BUTTON_RELEASE_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
            		else
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].click_counter;
            			if ( temp >= BUTTON_LONG_PRESS )
            			{
            				buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				buttons_ctl[i].event = BUTTON_EVENT_LCLICK;
            				event_change = BUTTON_EVENT_ACTIVE;
            			}
            		} // else
            		break;

            	case BUTTON_IS_LPRESSED:
            		if ( this_button_level!=buttons_ctl[i].active_level ) // release attempt
            		{
            			buttons_ctl[i].status = BUTTON_RELEASE_ATTEMPT;
            			buttons_ctl[i].click_counter = current_tick; // store current timer tick
            		}
#ifdef BUTTON_REPEATED_PRESS_ENABLE
            		else
            		{
            			temp = current_tick; // get current timer tick
            			temp -= buttons_ctl[i].click_counter;
            			if ( temp >= BUTTON_REPEATED_PRESS )
            			{
            				buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick
            				buttons_ctl[i].event = BUTTON_EVENT_CLICK;
            				event_change = BUTTON_EVENT_ACTIVE;
            			} // if ( temp >= BUTTON_REPEATED_PRESS )
            		} // else
#endif // #ifdef BUTTON_REPEATED_PRESS_ENABLE
            		break;

            	case BUTTON_RELEASE_ATTEMPT:
            		temp = current_tick; // get current timer tick
            		temp -= buttons_ctl[i].click_counter; // calculate the time past for debounce
            		if ( temp >= BUTTON_DEBOUNCE_MS )
            		{
            			if ( this_button_level!=buttons_ctl[i].active_level ) // still released
            			{
            				buttons_ctl[i].status = BUTTON_IS_RELEASED;
            				buttons_ctl[i].click_counter = current_tick; // store current timer tick

            				switch ( buttons_ctl[i].dbc_status )
            				{
                            	case BUTTON_DBC_IDLE:
                            		temp = current_tick; // get current timer tick
                            		temp -= buttons_ctl[i].dbc_counter; // calculate the duration of the first click
                            		if ( temp <= BUTTON_DBC_CLICK )
                            		{
                            			buttons_ctl[i].dbc_status = BUTTON_DBC_CLK1;
                            			buttons_ctl[i].dbc_counter = current_tick; // store current timer tick
                            		}
                            		else // Timeout for a potential first click of a double click
                            		{
                            			buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
                            			event_change = BUTTON_EVENT_RESTORED;
                            		}
                            		break;

                            	case BUTTON_DBC_CLK1: // this case does not exist, do nothing

                            	case BUTTON_DBC_CLK2:
                            		buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE; // double click released, reset
                            		buttons_ctl[i].event = BUTTON_EVENT_IDLE; // reset if not reset by caller function
                            		event_change = BUTTON_EVENT_RESTORED;
                            		break;

                            	default:
                            		break;
            				} // switch ( buttons_ctl[i].dbc_status )
            			} // if ( this_button_level!=buttons_ctl[i].active_level )
            			else // not a valid release (logic 1 read)
            			{
            				if ( buttons_ctl[i].event==BUTTON_EVENT_CLICK )
            					buttons_ctl[i].status = BUTTON_IS_PRESSED;
            				else
            					buttons_ctl[i].status = BUTTON_IS_LPRESSED;
            			}
            		} // if ( temp >= BUTTON_DEBOUNCE_MS )
            		break;

            	default:
            		break;
        	} // switch ( buttons_ctl[i].status )
        } // for (i=0; i<NUM_BUTTONS_MAX; i++)

        if ( event_change )
        {
        	for (i=0; i<NUM_BUTTONS_MAX; i++)
        	{
        		m1_buttons_status.event[i] = buttons_ctl[i].event; // Update latest status
        		buttons_ctl[i].event = BUTTON_EVENT_IDLE; // Reset
        	}
    		m1_device_stat.active_timestamp = current_tick; // Update latest time stamp
        } // if ( event_change )

        if ( m1_device_stat.op_mode != M1_OPERATION_MODE_FIRMWARE_UPDATE )
        {
        	if ( event_change & BUTTON_EVENT_ACTIVE ) // Notify task only when there's an active event
        	{
    			// Update to queue to notify any listener.
    			//xQueueOverwrite(button_events_q_hdl, &m1_buttons_status);
    			xQueueSend(button_events_q_hdl, &m1_buttons_status, 0);
    			//UBaseType_t uxQueueMessagesWaiting( const QueueHandle_t xQueue )
    			// Send notification to button event handling task
    			send_button_evt_to_queue();
        	} // if ( event_change & BUTTON_EVENT_ACTIVE )
        	battery_indicator_update();
        	lcd_saver_update();
        } // if ( m1_device_stat.op_mode != M1_OPERATION_MODE_FIRMWARE_UPDATE )

        vTaskDelay(pdMS_TO_TICKS(SYSTEM_PERIODIC_TASK_DELAY));
        m1_wdt_send_report(M1_REPORT_ID_BUTTONS_HANDLER_TASK, SYSTEM_PERIODIC_TASK_DELAY);
    } // while (TRUE)

} // void system_periodic_task(void *param)



/*============================================================================*/
/*
 * This function sends notification to an active task for a button event
 */
/*============================================================================*/
static void send_button_evt_to_queue(void)
{
	BaseType_t ret;
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;

	// Check if the queue has any button event
	ret = uxQueueMessagesWaiting(button_events_q_hdl);
	if ( ret ) // Is there a button event?
	{
		switch(m1_device_stat.op_mode)
		{
			case M1_OPERATION_MODE_POWER_UP:
			case M1_OPERATION_MODE_DISPLAY_ON:
			case M1_OPERATION_MODE_MENU_ON:
				//xTaskNotify(menu_main_handler_task_hdl, 0x00, eNoAction);
				//break;
			case M1_OPERATION_MODE_SUB_FUNC_RUNNING:
				// Instead of sending notification to the running task (sub-function),
				// let send a keypad event to the queue that will be read later
				// by the running sub-function
				//xTaskNotify(subfunc_handler_task_hdl, 0x00, eNoAction);
				q_item.q_evt_type = Q_EVENT_KEYPAD;
				q_item.q_data.keypad_evt = 1; // any value,  not used
				xQueueSend(main_q_hdl, &q_item, portMAX_DELAY);
				break;

			default:
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0); // Remove this button event
				break;
		} // switch(m1_device_stat.op_mode)
	} // if ( ret )
} // static void send_button_evt_to_queue(void)



/*============================================================================*/
/*
 * This task handles idle tasks
*/
/*============================================================================*/
void idle_handler_task(void *param)
{
	uint32_t task_val;
	while(1)
	{
		xTaskNotifyWait(0, 0, &task_val, portMAX_DELAY);
	} // while(1)
} // void idle_handler_task(void *param)




/*============================================================================*/
/*
 * This function checks if a button is being pressed or not
*/
/*============================================================================*/
uint8_t m1_button_pressed_check(uint8_t button_id)
{
	if ( button_id >= NUM_BUTTONS_MAX )
		return FALSE;

	if ( m1_buttons_status.event[button_id]==BUTTON_EVENT_CLICK )
		return TRUE;

    return FALSE;
} // uint8_t m1_button_pressed_check(uint8_t button_id)



/*============================================================================*/
/*
 * This function resets all buttons status to default state
*/
/*============================================================================*/
void m1_buttons_status_reset(void)
{
	uint8_t i;

	for (i=0; i<NUM_BUTTONS_MAX; i++)
		m1_buttons_status.event[i] = BUTTON_EVENT_IDLE;
} // void m1_buttons_status_reset(void)



/*============================================================================*/
/**
  * @brief  Identify TIM clock
  * @param  None
  * @retval Timer clock
  */
/*============================================================================*/
uint32_t TIM_GetCounterCLKValue(uint16_t prescaler)
{
	uint32_t apbprescaler = 0, apbfrequency = 0;

	/* Get the clock prescaler of APB1 */
	apbprescaler = ((RCC->CFGR2 >> 4) & 0x7);
	apbfrequency = HAL_RCC_GetPCLK1Freq();

	/* If APBx clock div >= 4 */
	if (apbprescaler >= 4)
	{
		return ((apbfrequency * 2) / (prescaler + 1));
	}
	else
	{
		return (apbfrequency / (prescaler + 1));
	}
} // uint32_t TIM_GetCounterCLKValue(uint16_t prescaler)



/*============================================================================*/
/**
 * @brief Update LED indicator based on battery charge status
 */
/*============================================================================*/
static void battery_indicator_update(void)
{
	S_M1_Power_Status_t SystemPowerStatus;
	uint8_t new_stat, running_id;
	static uint8_t old_stat = 0xFF;
	static uint16_t batt_info_timer_count = 0;

	batt_info_timer_count += SYSTEM_PERIODIC_TASK_DELAY;
	if ( batt_info_timer_count >= TASKDELAY_BATTERY_INFO_TIMER )
	{
		batt_info_timer_count = 0;
		battery_status_update();
	} // if ( batt_info_timer_count >= TASKDELAY_BATTERY_INFO_TIMER )

	battery_power_status_get(&SystemPowerStatus);

	if ( SystemPowerStatus.fault == 0 )
	{
		new_stat = SystemPowerStatus.stat;
		if ( old_stat != new_stat ) // New status?
		{
			running_id = m1_led_get_running_id();
			switch ( new_stat )
			{
				case 0: // Not charging
					if ( running_id==LED_FAST_BLINK_FN_ID )
					{
						 m1_led_insert_function_id(LED_BATTERY_UNCHARGED_FN_ID, NULL);
					}
					else if ( running_id==LED_BATTERY_CHARGED_ON_FN_ID || running_id==LED_BATTERY_FULL_ON_FN_ID )
					{
						m1_led_indicator_off(NULL);
					}
					break;

				case 1: // Pre-charge/Charging
				case 2:
					if ( running_id==LED_FAST_BLINK_FN_ID )
					{
						m1_led_insert_function_id(LED_BATTERY_CHARGED_ON_FN_ID, NULL);
					}
					else if ( running_id==LED_INDICATOR_OFF_FN_ID || running_id!=LED_BATTERY_CHARGED_ON_FN_ID )
					{
						m1_led_batt_charged_on(NULL);
					}
					break;

				case 3: // Full charged
					if ( running_id==LED_FAST_BLINK_FN_ID )
					{
						m1_led_insert_function_id(LED_BATTERY_FULL_ON_FN_ID, NULL);
					}
					else if ( running_id==LED_INDICATOR_OFF_FN_ID || running_id!=LED_BATTERY_FULL_ON_FN_ID )
					{
						m1_led_batt_full_on(NULL);
					}
					break;

				default:
					break;
			} // switch ( new_stat )
			old_stat = new_stat; // Update
		} // if ( old_stat != SystemPowerStatus.stat )
	} // if ( SystemPowerStatus.fault == 0 )

} // static void battery_indicator_update(void)



/*============================================================================*/
/**
 * @brief Update LCD backlight operating status
 */
/*============================================================================*/
static void lcd_saver_update(void)
{
	uint8_t static saver_mode = 0;
	uint32_t delta;

	delta = HAL_GetTick() - m1_device_stat.active_timestamp;
	if ( saver_mode )
	{
		if ( delta < LCD_SAVER_PERIOD ) // Keypad is active?
		{
			lp5814_backlight_on(M1_BACKLIGHT_BRIGHTNESS); // Turn on backlight
			saver_mode = 0;
		}
	} // if ( saver_mode )
	else
	{
		if ( delta >= LCD_SAVER_PERIOD ) // Keypad has been inactive?
		{
			lp5814_backlight_on(M1_BACKLIGHT_OFF); // Turn on backlight
			saver_mode = 1;
		}
	} // else

} // static void lcd_saver_update(void)


/*============================================================================*/
/*
 * This function initializes default values for the system after power on.
 */
/*============================================================================*/
void startup_device_init(void)
{
    uint8_t i, k, stat;
    uint32_t bu_reg_read, *bu_reg_write;

    for (i=0; i<NUM_BUTTONS_MAX; i++)
    {
    	buttons_ctl[i].status = BUTTON_IS_IDLE;
    	buttons_ctl[i].dbc_status = BUTTON_DBC_IDLE;
    	buttons_ctl[i].event = BUTTON_EVENT_IDLE;
    	buttons_ctl[i].active_level = BUTTON_PRESS_STATE;
    } // for (i=0; i<NUM_BUTTONS_MAX; i++)

    m1_device_stat.op_mode = M1_OPERATION_MODE_POWER_UP;
    m1_device_stat.active_timestamp = HAL_GetTick();
    m1_device_stat.sub_func = NULL;

    // Read configuration data from device flash
    memcpy((uint8_t *)&m1_device_stat.config, (__IO uint8_t *)FW_CONFiG_ADDRESS, sizeof(S_M1_FW_CONFIG_t));

	//SBF: System standby flag
	//This bit is set by hardware and cleared only by a POR or by setting the CSSF bit.
	//0: system has not been in Standby mode.
	//1: system has been in Standby mode.
	stat = __HAL_PWR_GET_FLAG(PWR_FLAG_SBF);
	__HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF); // Clear stand-by flag
	__HAL_PWR_CLEAR_FLAG(PWR_WAKEUP_FLAG4); // Clear Wake-up flag
	if ( !stat ) // Not reset by Stand-by mode, reset by all other reasons
	{
		//HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW); // Enable WKUP4 pin, falling edge
		//HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
		//HAL_PWR_EnterSTANDBYMode(); // Enter Standby mode
		;
	} // if ( !stat )

	k = (sizeof(S_M1_BK_REGS_t) + 3)/4;
	bu_reg_read = RTC_BKP_DR0; // Backup address, address offset: 0x100 + 0x04 * x, (x = 0 to 31)
	bu_reg_write = (uint32_t *)&m1_device_stat.bu_regs;
	HAL_PWR_EnableBkUpAccess();
	for (i=0; i<k; i++)
	{
		*bu_reg_write = HAL_RTCEx_BKUPRead(&hrtc, bu_reg_read);
		bu_reg_read++;
		bu_reg_write++;
	} // for (i=0; i<k; i++)
//	HAL_PWR_DisableBkUpAccess();

    // Disable RTC
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
} // void startup_device_init(void)



/*============================================================================*/
/*
 * This function checks the start-up status and does tasks accordingly
 */
/*============================================================================*/
void startup_config_handler(void)
{
    uint16_t i, k;
    uint32_t *bu_reg_read, crc32_add;
    uint32_t fw_ver_old, fw_ver_new;
    S_M1_FW_CONFIG_t old_fw_config;
    BaseType_t ret;
    S_M1_Main_Q_t q_item;
    S_M1_Buttons_Status this_button_status;

	if ( m1_device_stat.bu_regs.magic_number != SYS_CONFIG_MAGIC_NUMBER ) // Not initialized yet?
	{
		startup_bu_registers_init();
	} // if ( m1_device_stat.bu_regs.magic_number != SYS_CONFIG_MAGIC_NUMBER )
	else
	{
		if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_ACTIVE )
		{
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("UPDATE FAILED!");
			M1_LOG_I(M1_LOGDB_TAG, "FW update failed. Device got reset unexpectedly!\r\n");
		} // if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_ACTIVE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_COMPLETE )
		{
			startup_bu_registers_init(); // Reinitialize after update
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("UPDATE COMPLETED!");
			M1_LOG_I(M1_LOGDB_TAG, "FW update complete!\r\n");
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_UPDATE_COMPLETE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_ROLLBACK_COMPLETE )
		{
			startup_bu_registers_init(); // Reinitialize after rollback
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("ROLLBACK COMPLETED!");
			M1_LOG_I(M1_LOGDB_TAG, "FW rollback complete!\r\n");
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_FW_ROLLBACK_COMPLETE )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_REBOOT )
		{
			m1_led_fw_update_on(NULL);
			if ( !m1_device_stat.dev_reset_by_wdt ) // Device got reset by normal cause?
			{
				vTaskDelay(POWER_UP_SYS_CONFIG_WAIT_TIME); // Wait for stable key press during power up
				do
				{
					// menu_main_handler_task() is giving some time to this function to read the main_q_hdl during power-up.
					ret = xQueueReceive(main_q_hdl, &q_item, 0);
					if ( ret != pdTRUE )
						break;
					if ( q_item.q_evt_type!=Q_EVENT_KEYPAD )
						break;
					ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
					if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) &&
											(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK) ) // Rollback request?
					{
			    		// User requests the update rollback.
			    		M1_LOG_I(M1_LOGDB_TAG, "Update rollback requested!\r\n");
			    		// Display rollback confirm message here
			    		bu_reg_read = (__IO uint32_t *)(FW_CONFiG_ADDRESS + M1_FLASH_BANK_SIZE); // Always read data from the other bank
			    		k = FW_CONFiG_SIZE/4 - 1; // Convert to 32-bit and exclude the last slot for the add-on CRC32
			    		for (i=0; i<k; i++)
			    		{
			    			if ( *bu_reg_read==FW_CONFIG_MAGIC_NUMBER_2 )
			    				break;
			    			bu_reg_read++;
			    		} // for (i=0; i<k; i++)
			    		if ( i < k )
			    		{
			    			M1_LOG_I(M1_LOGDB_TAG, "Valid firmware found for rollback. Checking CRC...");
			    			bu_reg_read++; // Move to CRC32 location which is right after the Magic Number 2
			    			crc32_add = (uint32_t)bu_reg_read; // Get the CRC address and use it as the size of the firmware resided in this bank
			    			crc32_add -= (FW_START_ADDRESS + M1_FLASH_BANK_SIZE); // Exclude the size of bank 1
			    			crc32_add /= 4; // convert size from byte to word (32-bit)
			    			if ( bl_crc_check(crc32_add)==BL_CODE_OK )
			    			{
			    				M1_LOG_N(M1_LOGDB_TAG, "OK\r\n");
			    				i++; // Move to CRC32 location which is right after the Magic Number 2
					    		memcpy((uint8_t *)&old_fw_config, (__IO uint8_t *)(FW_CONFiG_ADDRESS + M1_FLASH_BANK_SIZE), i*4);
			    				fw_ver_new = *(uint32_t *)&m1_device_stat.config.fw_version_rc;
			    				fw_ver_old = *(uint32_t *)&old_fw_config.fw_version_rc;
			    				if ( fw_ver_old < fw_ver_new ) // Existing FW in bank 2 is older than current FW?
			    				{
			    					M1_LOG_I(M1_LOGDB_TAG, "Rollback is ready!\r\n");
			    					startup_info_screen_display("FW ROLLBACK...");
			    					startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_FW_ROLLBACK_COMPLETE);
			    					vTaskDelay(pdMS_TO_TICKS(2000));
			    					bl_swap_banks();
			    				} // if ( fw_ver_old < fw_ver_new )
			    				else // Existing FW in bank 2 is newer than current FW. Rollback is not allowed!
			    				{
			    					M1_LOG_I(M1_LOGDB_TAG, "Rollback was already completed!\r\n");
			    				}
			    			} // if ( bl_crc_check(crc32_add)==BL_CODE_OK )
			    			else
			    			{
			    				M1_LOG_N(M1_LOGDB_TAG, "Failed\r\n");
			    			} // else
			    		} // if ( i < k )
			    		else
			    		{
			    			startup_info_screen_display("ROLLBACK FAILED!");
			    			M1_LOG_I(M1_LOGDB_TAG, "No valid firmware found for rollback!\r\n");
			    		} // else
			    	} // if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) &&
				} while (0);
			} // if ( !m1_device_stat.dev_reset_by_wdt )
			m1_led_fw_update_off();
		} // else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_REBOOT )
		else if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_USB_DFU_REQUEST )
		{
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
			startup_info_screen_display("USB DFU MODE...");
			firmware_update_enter_usb_dfu();
			startup_info_screen_display("DFU MODE FAILED!");
			M1_LOG_I(M1_LOGDB_TAG, "Failed to enter USB DFU mode!\r\n");
		}
	} // else

	if ( m1_device_stat.op_mode != M1_OPERATION_MODE_DISPLAY_ON )
	{
		m1_gui_welcome_scr();
	}

	m1_device_stat.active_bank = bl_get_active_bank();

	M1_LOG_I(M1_LOGDB_TAG, "Device firmware version %d.%d.%d.%d.\r\n", m1_device_stat.config.fw_version_major,
			m1_device_stat.config.fw_version_minor, m1_device_stat.config.fw_version_build, m1_device_stat.config.fw_version_rc);
} // void startup_config_handler(void)



/*============================================================================*/
/*
 * This function writes data to backup registers
 */
/*============================================================================*/
void startup_config_write(uint8_t config_byte, uint8_t config_val)
{
    uint32_t *bu_reg_read, bu_reg_write;

	bu_reg_write = RTC_BKP_DR0; // Backup address, address offset: 0x100 + 0x04 * x, (x = 0 to 31)
    switch (config_byte)
	{
		case BK_REGS_SELECT_DEV_OP_STAT:
			bu_reg_write += 1;
			bu_reg_read = (uint32_t *)&m1_device_stat.bu_regs.device_op_status;
			m1_device_stat.bu_regs.device_op_status = config_val;
			break;

		default:
			break;
	} // switch (config_byte)

    if ( bu_reg_read )
    {
    	HAL_PWR_EnableBkUpAccess();
    	HAL_RTCEx_BKUPWrite(&hrtc, bu_reg_write, *bu_reg_read);
//    	HAL_PWR_DisableBkUpAccess();
    } // if ( bu_reg_read )
} // void startup_config_write(uint8_t config_byte, uint8_t config_val)



/*============================================================================*/
/*
 * This function initializes the backup registers during power up
 */
/*============================================================================*/
static void startup_bu_registers_init(void)
{
    uint16_t i, k;
    uint32_t *bu_reg_read, bu_reg_write;

	k = sizeof(S_M1_BK_REGS_t);
	memset((uint8_t *)&m1_device_stat.bu_regs, 0x00, k); // Reset reg data
	m1_device_stat.bu_regs.magic_number = SYS_CONFIG_MAGIC_NUMBER; // Assign magic number
	k = (sizeof(S_M1_BK_REGS_t) + 3)/4; // Convert size in byte to 32-bit
	bu_reg_write = RTC_BKP_DR0; // Backup address, address offset: 0x100 + 0x04 * x, (x = 0 to 31)
	bu_reg_read = (uint32_t *)&m1_device_stat.bu_regs;
	HAL_PWR_EnableBkUpAccess();
	for (i=0; i<k; i++)
	{
		HAL_RTCEx_BKUPWrite(&hrtc, bu_reg_write, *bu_reg_read);
		bu_reg_write++;
		bu_reg_read++;
	} // for (i=0; i<k; i++)
	//HAL_PWR_DisableBkUpAccess();
	M1_LOG_I(M1_LOGDB_TAG, "Backup registers initialized!\r\n");
} // static void startup_bu_registers_init(void)



/*============================================================================*/
/*
 * This function displays the M1 welcome screen
*/
/*============================================================================*/
void startup_info_screen_display(const char *scr_text)
{
	char fw_ver[20];
	uint8_t len, x0;

	u8g2_SetPowerSave(&m1_u8g2, false);

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, M1_POWERUP_LOGO_LEFT_POS_X, M1_POWERUP_LOGO_TOP_POS_Y, M1_POWERUP_LOGO_WIDTH, M1_POWERUP_LOGO_HEIGHT, m1_logo_40x32);

	sprintf(fw_ver, "Version %d.%d", m1_device_stat.config.fw_version_major, m1_device_stat.config.fw_version_minor);
	len = strlen(fw_ver);
	u8g2_SetFont(&m1_u8g2, M1_POWERUP_LOGO_FONT);
	u8g2_DrawStr(&m1_u8g2, M1_POWERUP_LOGO_LEFT_POS_X + M1_POWERUP_LOGO_WIDTH + 3, M1_POWERUP_LOGO_TOP_POS_Y + 15, "MONSTATEK M1");
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, M1_POWERUP_LOGO_LEFT_POS_X + M1_POWERUP_LOGO_WIDTH + 3, M1_POWERUP_LOGO_TOP_POS_Y + 25, fw_ver);
	u8g2_DrawStr(&m1_u8g2, M1_POWERUP_LOGO_LEFT_POS_X + M1_POWERUP_LOGO_WIDTH + 3, M1_POWERUP_LOGO_TOP_POS_Y + 35, FW_VERSION_FORK_TAG);

	len = strlen(scr_text);
	x0 = (M1_LCD_DISPLAY_WIDTH - len*M1_GUI_FONT_WIDTH)/2;
	if ( x0 >= M1_GUI_FONT_WIDTH )
		x0 -= M1_GUI_FONT_WIDTH;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, x0, 62, scr_text);

	m1_u8g2_nextpage(); // Update display RAM

	lp5814_backlight_on(M1_BACKLIGHT_BRIGHTNESS); // Turn on backlight

	m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	m1_device_stat.active_timestamp = HAL_GetTick(); // reset timeout
} // void startup_info_screen_display(const char *scr_text)




/*============================================================================*/
/*
 * This function does a non-blocking delay. It replaces the default HAL_Delay() function
 */
/*============================================================================*/
void HAL_Delay(uint32_t Delay)
{
	vTaskDelay(Delay);
} // void HAL_Delay(uint32_t Delay)
