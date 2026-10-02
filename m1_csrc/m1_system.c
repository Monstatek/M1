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
#include "m1_fw_update_bl.h"
#include "m1_lp5814.h"
#include "m1_bq25896.h"
#include "battery.h"
#include "m1_feedback_manager.h"
#include "m1_feedback_orchestration.h"

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
uint8_t m1_button_event_check(uint8_t button_id, uint8_t event);
void m1_buttons_status_reset(void);
uint32_t TIM_GetCounterCLKValue(uint16_t prescaler);
static void battery_indicator_update(void);
static void lcd_saver_update(void);
/* Post-update "Success!" toast duration before the boot artwork appears. */
#define M1_BOOT_SUCCESS_TOAST_MS   1500U

void startup_config_handler(void);
void power_on_button_check(void);
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

	// After power on, let wait here until user releases the OK button if it's the case!
	while ( true )
	{
		if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET ) // Button released?
			break;
		vTaskDelay(100); // Return some time to the system
        m1_wdt_send_report(M1_REPORT_ID_SYSTEM_PERIODIC_TASK, 100);
	} // while ( true )

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
        m1_wdt_send_report(M1_REPORT_ID_SYSTEM_PERIODIC_TASK, SYSTEM_PERIODIC_TASK_DELAY);
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
uint8_t m1_button_event_check(uint8_t button_id, uint8_t event)
{
	if ( button_id >= NUM_BUTTONS_MAX )
		return FALSE;

	if ( m1_buttons_status.event[button_id]==event )
		return TRUE;

    return FALSE;
} // uint8_t m1_button_event_check(uint8_t button_id, uint8_t event)



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
/* bq_getCHRG_STAT() (Battery/battery.c: battery_status_update() ->
 * bq_getCHRG_STAT(), m1_bq25896.c) is a single, un-filtered I2C register
 * read of the charger IC's own state machine, refreshed every 2s
 * (TASKDELAY_BATTERY_INFO_TIMER). Near a charge-current/voltage boundary
 * (marginal USB input current, battery approaching full) that raw read
 * genuinely bounces between adjacent states from one 2s refresh to the
 * next -- a documented characteristic of this class of charger IC, not a
 * transient fluke. Reacting to every single reading immediately makes that
 * bounce directly visible as the LED flashing/changing colour while
 * connected and charging.
 *
 * PERMANENT FIX, do not revert: require the SAME reading on two
 * consecutive 2s refreshes (~2-4s apart) before actually changing the LED.
 * A single noisy sample can never satisfy that -- it gets superseded by
 * the next sample before the threshold is met -- but a genuine sustained
 * transition (plug in, unplug, reach full) still shows up within a few
 * seconds. This was reverted once before (2026-08-22) as "an unproven
 * experiment made during unrelated RFID investigation work" on the
 * assumption the immediate-reaction version was already hardware-
 * validated flicker-free; that assumption was wrong -- the immediate-
 * reaction version was hardware-confirmed to flash again on 2026-08-25.
 * Do not remove BATTERY_STAT_CONFIRM_SAMPLES or react to new_stat before
 * it is confirmed; that reintroduces the exact flashing this fixes. */
#define BATTERY_STAT_CONFIRM_SAMPLES  2

static void battery_indicator_update(void)
{
	S_M1_Power_Status_t SystemPowerStatus;
	uint8_t new_stat, want;
	int pg;
	static uint8_t pending_stat = 0xFF;
	static uint8_t pending_confirm = 0;
	static uint8_t stat_stable = 0xFF;   /* last DEBOUNCE-confirmed CHRG_STAT */
	static uint8_t full_latched = 0;     /* green latched once "full" seen this power session */
	static uint8_t last_led = 0xFF;      /* last APPLIED charge-LED state (see `want`) */
	static uint16_t batt_info_timer_count = 0;

	batt_info_timer_count += SYSTEM_PERIODIC_TASK_DELAY;
	if ( batt_info_timer_count < TASKDELAY_BATTERY_INFO_TIMER )
		return; // only re-evaluate charge state once per fresh hardware sample

	batt_info_timer_count = 0;
	battery_status_update();
	battery_power_status_get(&SystemPowerStatus);

	if ( SystemPowerStatus.fault != 0 )
		return; // charger fault: leave the LED as-is (same as the prior code, which acted only when fault==0)

	/* Debounce CHRG_STAT: require BATTERY_STAT_CONFIRM_SAMPLES identical
	 * consecutive reads before trusting a new value. A single noisy sample can
	 * never satisfy this -- it is superseded by the next sample before the
	 * threshold is met (PERMANENT FIX, do not revert -- see the block comment
	 * above). */
	new_stat = SystemPowerStatus.stat;
	if ( new_stat != pending_stat )
	{
		pending_stat = new_stat;
		pending_confirm = 1;
	}
	else if ( pending_confirm < BATTERY_STAT_CONFIRM_SAMPLES )
	{
		pending_confirm++;
	}
	if ( pending_confirm >= BATTERY_STAT_CONFIRM_SAMPLES )
		stat_stable = new_stat; // trusted charge state

	/* Solid, no-flashing charge indicator.
	 *
	 * The charger IC's CHRG_STAT genuinely bounces between adjacent states near
	 * a charge boundary (e.g. Fast-Charge <-> Complete while topping off), which
	 * made the LED visibly alternate red<->green / blink while on power. Anchor
	 * the ON/OFF decision on PG_STAT (power-good: a clean, stable "external power
	 * present" signal, NOT the bouncy CHRG_STAT), and LATCH "full/green" for the
	 * duration of a power session so a bounce back to "charging" can never flip
	 * the colour. Result: solid red while charging, solid green once full, and
	 * off only when external power is actually removed -- the LED changes at most
	 * twice per power session (off->red, red->green) and never flashes. The
	 * charge state is still derived from the debounced CHRG_STAT above. */
	pg = bq_getPG_STAT(); // 1 = good external power present
	if ( !pg )
	{
		want = 0;         // not on external power -> off
		full_latched = 0; // reset the latch for the next power session
	}
	else
	{
		if ( stat_stable == 3 ) // charge complete
			full_latched = 1;
		want = full_latched ? 2 : 1; // green once full (latched), else red while charging
	}

	if ( want != last_led ) // apply ONLY on a real change -> no repeated writes, no flicker
	{
		/*
		 * Migrated to the M1-FB-STD-001 v1.1 feedback manager
		 * (documentation/M1_FEEDBACK_RECONCILIATION.md): charging LED
		 * feedback is now a manager-owned FB_OWNER_POWER hold (solid
		 * red/green, nine-state mapping states 7/8) instead of the old
		 * m1_led_indicator "running function id" state machine. The
		 * manager's hold registration is itself idempotent (a repeated
		 * request from the same owner replaces its own hold in place), so
		 * this no longer needs to inspect what LED function was previously
		 * running -- the debounce/latch logic above already guarantees
		 * `want` only changes on a real, confirmed transition.
		 * fb_pwr_charger_attached() also runs the reference section 10.2
		 * cleanup (releases any critical-battery loop/backlight/buzzer
		 * hold) before activating the new charging/charged state, in case
		 * this transition follows a critical-battery episode.
		 */
		switch ( want )
		{
			case 0: // off (power removed)
				fb_pwr_not_charging();
				break;

			case 1: // solid RED -- charging
				fb_pwr_charger_attached(false);
				break;

			case 2: // solid GREEN -- full
				fb_pwr_charger_attached(true);
				break;

			default:
				break;
		}
		last_led = want;
	}

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

	/*
	 * Migrated to the feedback manager (M1-FB-STD-001 v1.1, section 5.3):
	 * the sleep timer now registers/updates its own lowest-priority
	 * FB_OWNER_SLEEP_TIMER backlight hold instead of calling
	 * lp5814_backlight_on() directly, so any higher-priority backlight
	 * holder (user brightness wake for an alert, critical-battery wake, an
	 * Emulate hold, a settings-brightness preview, ...) transparently takes
	 * over and the sleep timer's own state resumes automatically via the
	 * manager's logical restore once that holder releases. See
	 * documentation/M1_FEEDBACK_RECONCILIATION.md.
	 */
	delta = HAL_GetTick() - m1_device_stat.active_timestamp;
	if ( saver_mode )
	{
		if ( delta < LCD_SAVER_PERIOD ) // Keypad is active?
		{
			fb_sleep_timer_wake();
			saver_mode = 0;
		}
	} // if ( saver_mode )
	else
	{
		if ( delta >= LCD_SAVER_PERIOD ) // Keypad has been inactive?
		{
			fb_sleep_timer_sleep();
			saver_mode = 1;
		}
	} // else

} // static void lcd_saver_update(void)


/*============================================================================*/
/**
 * @brief Wake the LCD backlight now and restart the inactivity timer.
 *
 * Used when a view representing fresh, user-relevant output is shown (e.g. the
 * NFC/RFID successful-read info screen). Mirrors startup_info_screen_display():
 * wake the manager-owned backlight immediately and reseat active_timestamp so
 * the normal LCD_SAVER_PERIOD window starts from this moment. Scanning is not
 * affected and continues to time out normally.
 */
/*============================================================================*/
void m1_lcd_wake_restart_timer(void)
{
	fb_sleep_timer_wake();
	m1_device_stat.active_timestamp = HAL_GetTick();
} // void m1_lcd_wake_restart_timer(void)


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
	size_t copy_len;
	S_M1_FW_CONFIG_t old_fw_config = {0};
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
			startup_info_screen_display("Success!");
				vTaskDelay(pdMS_TO_TICKS(M1_BOOT_SUCCESS_TOAST_MS));
				startup_info_screen_display("");   /* -> approved boot artwork */
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
					if (button_events_q_hdl == NULL)
						break;
					ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
					if ( ret != pdTRUE )
						break;
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
					    		copy_len = (size_t)i * 4U;
					    		if (copy_len > sizeof(S_M1_FW_CONFIG_t))
					    		{
					    			copy_len = sizeof(S_M1_FW_CONFIG_t);
					    		}
					    		memcpy((uint8_t *)&old_fw_config, (__IO uint8_t *)(FW_CONFiG_ADDRESS + M1_FLASH_BANK_SIZE), copy_len);
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
	} // else

	if ( m1_device_stat.op_mode != M1_OPERATION_MODE_DISPLAY_ON )
	{
		m1_gui_welcome_scr();
	}

	m1_device_stat.active_bank = bl_get_active_bank();

	M1_LOG_I(M1_LOGDB_TAG, "Device firmware version %d.%d.%d.%d %s.\r\n", m1_device_stat.config.fw_version_major,
			m1_device_stat.config.fw_version_minor, m1_device_stat.config.fw_version_build, m1_device_stat.config.fw_version_rc,
			FW_BUILD_LABEL);
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
	uint8_t len, x0;

	u8g2_SetPowerSave(&m1_u8g2, false);

	/* Normal power-on boot splash: the approved MONSTATEK wordmark artwork,
	 * drawn pixel-for-pixel at (0,0) as a fixed 128x64 bitmap (no old 40x32
	 * logo). The firmware-version line below it is NOT part of that bitmap --
	 * it is live text drawn with u8g2_DrawStr from the real
	 * m1_device_stat.config.fw_version_major/minor fields (same fields/
	 * pattern as settings_about_display_choice(), m1_settings.c), so it
	 * tracks the actual running firmware version rather than being baked
	 * into the artwork. A non-empty scr_text is an update/rollback STATUS
	 * screen, drawn below as its message text ONLY -- the old boot look
	 * appears nowhere. */
	if ((scr_text == NULL) || (scr_text[0] == '\0'))
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_FirstPage(&m1_u8g2);
		u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 64, m1_boot_logo_128x64);
		{
			char ver_str[24];
			uint8_t vw;
			snprintf(ver_str, sizeof(ver_str), "FW VERSION %u.%u",
			         (unsigned)m1_device_stat.config.fw_version_major,
			         (unsigned)m1_device_stat.config.fw_version_minor);
			u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			vw = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, ver_str);
			u8g2_DrawStr(&m1_u8g2, (uint8_t)((M1_LCD_DISPLAY_WIDTH - vw) / 2), 54, ver_str);
		}
		m1_u8g2_nextpage();
		fb_sleep_timer_wake();
		m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON;
		m1_device_stat.active_timestamp = HAL_GetTick();
		return;
	}

	/* Status screen (update/rollback): centered message text only -- no logo,
	 * no "MONSTATEK M1", no version. */
	len = strlen(scr_text);
	x0 = (M1_LCD_DISPLAY_WIDTH - len*M1_GUI_FONT_WIDTH)/2;
	if ( x0 >= M1_GUI_FONT_WIDTH )
		x0 -= M1_GUI_FONT_WIDTH;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, x0, 36, scr_text);   // vertically centered
	m1_u8g2_nextpage(); // Update display RAM

	fb_sleep_timer_wake(); // keep normal awake brightness manager-owned

	m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	m1_device_stat.active_timestamp = HAL_GetTick(); // reset timeout
} // void startup_info_screen_display(const char *scr_text)




/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void power_on_button_check(void)
{
	uint32_t reset_source;
	uint16_t tick_acc;
	int vbus_gd;

	vTaskDelay(50);

	bq_startADC();
	bq_oneShotADC();

	vbus_gd = bq_getVBUS_GD();

	bq_stopADC();

	reset_source = HAL_RCC_GetResetSource();
	reset_source &= (RCC_RESET_FLAG_SW + RCC_RESET_FLAG_IWDG + RCC_RESET_FLAG_WWDG);
	if ( reset_source ) // Not power on by normal cause?
		return;

	if ( vbus_gd==0 ) // No Vbus present?
	{
		if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET )
		{
			m1_power_down();
		}
	}

	// Device turns on by normal cause
	if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_NO_OP )
	{
		tick_acc = 0;
		while ( tick_acc < POWER_ON_LONG_PRESS )
		{
			vTaskDelay(100);
			if ( HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_Pin)==GPIO_PIN_SET ) // Button released?
				break;
			tick_acc += 100;
			m1_wdt_reset(); // Reset watchdog
		} // while ( tick_acc < POWER_ON_LONG_PRESS )
		if ( tick_acc < POWER_ON_LONG_PRESS ) // Button released too soon?
		{
			if ( vbus_gd==0 ) // No Vbus present?
			{
				m1_power_down(); // Shut down again
			}
		} // if ( tick_acc < POWER_ON_LONG_PRESS )
	} // if ( m1_device_stat.bu_regs.device_op_status==DEV_OP_STATUS_NO_OP )

} // void power_on_button_check(void)




/*============================================================================*/
/*
 * This function does a non-blocking delay. It replaces the default HAL_Delay() function
 */
/*============================================================================*/
void HAL_Delay(uint32_t Delay)
{
	vTaskDelay(Delay);
} // void HAL_Delay(uint32_t Delay)
