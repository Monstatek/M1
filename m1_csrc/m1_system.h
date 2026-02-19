/* See COPYING.txt for license details. */

/*
*
*  m1_system.h
*
* M1 Project
*
*/

#ifndef M1_SYSTEM_H_
#define M1_SYSTEM_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_freertos.h"
#include "queue.h"
#include "m1_fw_update_bl.h"

#define BUTTON_OK_KP_ID 		0
#define BUTTON_UP_KP_ID 		1
#define BUTTON_LEFT_KP_ID		2
#define BUTTON_RIGHT_KP_ID 		3
#define BUTTON_DOWN_KP_ID 		4
#define BUTTON_BACK_KP_ID 		5

#define NUM_BUTTONS_MAX     	6

#define LONG_PRESS_1000     1000
#define LONG_PRESS_2000     2000 // 2000 * 1ms = 2,000ms = 2 seconds
#define LONG_PRESS_3000     3000
#define LONG_PRESS_4000     4000
#define LONG_PRESS_5000     5000

#define BUTTON_DEBOUNCE_MS  		50 // ms
//#define BUTTON_DBC_MIDDLE   		500 // maximum interval between two clicks for double click recognition
#define BUTTON_DBC_MIDDLE   		300
#define BUTTON_DBC_CLICK    		500 // maximum click duration for double click recognition
#define BUTTON_LONG_PRESS   		LONG_PRESS_1000
#define BUTTON_REPEATED_PRESS   	150

#define BUTTON_IS_IDLE              0
#define BUTTON_PRESS_ATTEMPT        1
#define BUTTON_IS_PRESSED           2
#define BUTTON_IS_LPRESSED          3
#define BUTTON_RELEASE_ATTEMPT      4
#define BUTTON_IS_RELEASED          0   // Released # Idle

#define BUTTON_DBC_IDLE             0
#define BUTTON_DBC_CLK1             1 // first valid click
#define BUTTON_DBC_CLK2             2 // second valid click

#define BUTTON_LOGIC_HIGH           0b00000001
#define BUTTON_LOGIC_LOW            0b00000000

#define BUTTON_PRESS_STATE          BUTTON_LOGIC_LOW // logic level of the button when pressed
#define BUTTON_RELEASE_STATE        BUTTON_LOGIC_HIGH // logic level of the button when released (idle)

#define BUTTON_REPEATED_PRESS_ENABLE	// Enable repeated press event to replace the long press event

#define EXTRA_VIEW_TIMEOUT          0732 // 0732 * 4.096 ms = 3000 ms # 3s
#define RADIO_LISTEN_TIMEOUT        1250 // 1250 * 4.096 ms = 5120 ms # 5s
#define INACTIVITY_TIMEOUT          3750 // 3750 * 4.096 ms = 15360 ms # 15s

#define LED_ON_PWM_GREEN			175
#define LED_ON_PWM_RED				255

#define LED_FASTBLINK_PWM_H			255 // Highest brightness
#define LED_FASTBLINK_PWM_M			175
#define LED_FASTBLINK_PWM_L			100
#define LED_FASTBLINK_PWM_OFF		0 // Lowest brightness

#define LED_ONTIME_MIN				50	// ms

#define LED_FASTBLINK_ONTIME_H		50	// ms
#define LED_FASTBLINK_ONTIME_M		100	// ms
#define LED_FASTBLINK_ONTIME_L		150	// ms
#define LED_FASTBLINK_ONTIME_OFF	0	// ms

#define POWER_UP_SYS_CONFIG_WAIT_TIME	100 // ms

#define SYS_CONFIG_MAGIC_NUMBER    ((uint32_t)0x534A1F41)

#define LCD_SAVER_PERIOD			30000 // ms

typedef struct {
	uint32_t magic_number;
	uint8_t device_op_status;
	uint8_t reserved_1;
	uint8_t reserved_2;
	uint8_t reserved_3;
} S_M1_BK_REGS_t;

typedef enum {
	BK_REGS_SELECT_DEV_OP_STAT = 1,
	BK_REGS_SELECT_UNKNOWN
} S_M1_BK_REGS_SELECT_t;

typedef enum {
	DEV_OP_STATUS_NO_OP = 0,
	DEV_OP_STATUS_FW_UPDATE_ACTIVE,
	DEV_OP_STATUS_FW_UPDATE_COMPLETE,
	DEV_OP_STATUS_FW_ROLLBACK_COMPLETE,
	DEV_OP_STATUS_REBOOT,
	DEV_OP_STATUS_USB_DFU_REQUEST,
	DEV_OP_STATUS_UNKNOWN
} S_M1_DEV_OP_STATUS_t;

typedef enum {
	M1_OPERATION_MODE_POWER_UP = 0x01, // Power is up and the M1 is still in power off mode (shutdown mode)
	M1_OPERATION_MODE_DISPLAY_ON, // M1 is turned on. Default welcome screen is displayed
	M1_OPERATION_MODE_MENU_ON, // Main menu is activated
	M1_OPERATION_MODE_SUB_FUNC_RUNNING, // a sub function from the menu is running
	M1_OPERATION_MODE_FIRMWARE_UPDATE, // Firmware update is in progress
	M1_OPERATION_MODE_UNDEFINED,
	M1_OPERATION_MODE_SLEEP
	//M1_OPERATION_MODE_SHUTDOWN
} S_M1_Op_Mode;

typedef enum {
	BUTTON_EVENT_IDLE = 0x00,
	BUTTON_EVENT_CLICK,
	BUTTON_EVENT_DBCLICK,
	BUTTON_EVENT_LCLICK
} S_M1_Key_Event;

typedef struct
{
	S_M1_Key_Event		event; // single click, double click or long press event; reset after read by caller function
    uint8_t     status; // single click status, used for state machine
    uint8_t		active_level; // high (1) or low (0) in pressed state
    uint32_t    click_counter; // for single click
    uint8_t     dbc_status; // double click status, used for state machine
    uint32_t    dbc_counter; // for double click
} S_Buttons_Control;

typedef struct
{
	GPIO_TypeDef *gpio_port;
	uint16_t gpio_pin;
} S_GPIO_IO_t;

typedef struct
{
    S_M1_Key_Event event[NUM_BUTTONS_MAX];
    uint32_t timestamp;
} S_M1_Buttons_Status;

typedef struct
{
    uint32_t active_timestamp;
    void (*sub_func)(void);
    S_M1_Op_Mode op_mode;
    S_M1_FW_CONFIG_t config;
    S_M1_BK_REGS_t bu_regs;
    uint16_t active_bank;
    uint8_t dev_reset_by_wdt;
} S_M1_Device_Status_t;

extern S_Buttons_Control 	buttons_ctl[];
extern S_M1_Buttons_Status 	m1_buttons_status;
extern S_M1_Device_Status_t	m1_device_stat;
extern TaskHandle_t			system_task_hdl;
extern TaskHandle_t 		idle_task_hdl;
extern QueueHandle_t 		button_events_q_hdl;

void system_periodic_task(void *param);
void idle_handler_task(void *param);
uint8_t m1_button_pressed_check(uint8_t button_id);
void m1_buttons_status_reset(void);
void startup_device_init(void);
void startup_config_handler(void);
void startup_config_write(uint8_t config_byte, uint8_t config_val);
void startup_info_screen_display(const char *scr_text);

#endif /* M1_SYSTEM_H_ */
