/* See COPYING.txt for license details. */

/*
*
*  m1_infrared.c
*
*  M1 Infrared functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_infrared.h"
#include "irmp.h"
#include "irsnd.h"
#include "m1_ir_remotes.h"
#include "m1_feedback_orchestration.h"

/*************************** D E F I N E S ************************************/

#define GENERAL_FRAME_REPEAT_PAUSE_TIME		200 //mS

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

const uint32_t ir_carrier_frequency_list[IR_ENCODE_CARRIER_FREQ_LIST_MAX] =
{
	IR_ENCODE_CARRIER_FREQ_30_KHZ,
	IR_ENCODE_CARRIER_FREQ_32_KHZ,
	IR_ENCODE_CARRIER_FREQ_36_KHZ,
	IR_ENCODE_CARRIER_FREQ_36_045_KHZ,
	IR_ENCODE_CARRIER_FREQ_36_700_KHZ,
	IR_ENCODE_CARRIER_FREQ_38_KHZ,
	IR_ENCODE_CARRIER_FREQ_40_KHZ,
	IR_ENCODE_CARRIER_FREQ_56_KHZ,
	IR_ENCODE_CARRIER_FREQ_455_KHZ
};

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef   timerhdl_ir_carrier;
TIM_HandleTypeDef   timerhdl_ir_tx;
TIM_HandleTypeDef   timerhdl_ir_rx;
IRMP_DATA 			irmp_data;

volatile S_M1_IR_Det ir_rx_edge_det; // Flag for first falling edge detected

volatile uint8_t ir_ota_data_tx_active;
volatile uint8_t ir_ota_data_is_a_mark;
uint16_t ir_ota_data_tx_len;
volatile uint16_t ir_ota_data_tx_counter;
uint16_t *pir_ota_data_tx_buffer;

static IRMP_DATA 			irmp_loopback_data;
static uint8_t				new_remote_learned;
static uint8_t				ir_encode_sys_active = 0;
/* Menu option: IR Tx mapped to an external GPIO - TIM8_CH4N */
static uint8_t ir_tx_on_ext_gpio = 0;
static GPIO_TypeDef *ir_tx_remap_port = IR_GPIO_PORT;
static uint16_t ir_tx_remap_pin = IR_TX_GPIO_PIN;
static uint8_t ir_tx_remap_pin_alt = IR_GPIO_AF_TR;
static TIM_TypeDef *ir_tx_remap_timer = IR_ENCODE_CARRIER_TIMER;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_infrared_init(void);
void menu_infrared_exit(void);

void infrared_learn_new_remote(void);
void infrared_saved_remotes(void);
void infrared_remap_gpio(void);
S_M1_IR_Tx_States infrared_transmit(uint8_t init, uint8_t tx_protocol);

static void infrared_decode_sys_init(void);
static void infrared_decode_sys_deinit(void);
void infrared_encode_sys_init(void);
void infrared_encode_sys_deinit(void);
static void infrared_remap_status_update(uint8_t gpio_remapped);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * "This function initializes display for this sub-menu item.
 */
/*============================================================================*/
void menu_infrared_init(void)
{
	;
	new_remote_learned = 0;
} // void menu_infrared_init(void)



/*============================================================================*/
/*
 * This function will exit this sub-menu and return to the upper level menu.
 */
/*============================================================================*/
void menu_infrared_exit(void)
{
	;
} // void menu_infrared_exit(void)



/*============================================================================*/
/*
 * This function will transmit infrared frame(s) based on pre-configured data
 * Return: state machine's status of the transmitter
 */
/*============================================================================*/
S_M1_IR_Tx_States infrared_transmit(uint8_t init, uint8_t tx_protocol)
{
	static S_M1_IR_Tx_States ir_tx_state = IR_TX_INIT;
	static uint8_t ir_protocol = IRMP_UNKNOWN_PROTOCOL;
	uint16_t ir_ok;
	BaseType_t ret;

	if (init)
	{
		ir_tx_state = IR_TX_INIT;
		ir_protocol = tx_protocol;
		return 0;
	} // if (init)

	while (1) // Conditional loop
	{
		switch ( ir_tx_state )
		{
			case IR_TX_INIT:
				ir_ok = m1_make_ir_ota_multiframes();
				if ( ir_ok )
				{
					if ( m1_check_ir_ota_frame_status() )
					{
						ir_ota_data_tx_active = TRUE;
						ir_ota_data_tx_counter = 0;
						ir_ota_data_tx_len = m1_get_ir_ota_frame_len();
						pir_ota_data_tx_buffer = m1_get_ir_ota_buffer_ptr();
						__HAL_TIM_URS_ENABLE(&timerhdl_ir_tx); // Enable URS to temporarily disable the UIF when the UG bit is set
						timerhdl_ir_tx.Instance->ARR = pir_ota_data_tx_buffer[0]; // Update Auto Reload Register ARR value
						HAL_TIM_GenerateEvent(&timerhdl_ir_tx, TIM_EVENTSOURCE_UPDATE); // Generate Update Event (set UG bit) to reload the valid ARR and reset the counter
						__HAL_TIM_URS_DISABLE(&timerhdl_ir_tx); // Disable URS to enable the UIF again
						  /* Check if the update flag is set after the Update Generation, if so clear the UIF flag */
						if (HAL_IS_BIT_SET(timerhdl_ir_tx.Instance->SR, TIM_FLAG_UPDATE))
						{
							/* Clear the update flag */
							CLEAR_BIT(timerhdl_ir_tx.Instance->SR, TIM_FLAG_UPDATE);
						}
						if ( ir_ota_data_tx_len & 0x01 ) // Odd number means that there's is a pause time
						{
							ir_ota_data_is_a_mark = 0; // First transmitted data is a space (pause time).
						}
						else // Normal frame without pause time
						{
							ir_ota_data_is_a_mark = 1; // First transmitted data is a mark.
							irsnd_on(); // Start the PWM of the carrier at the output
						}
						__HAL_TIM_ENABLE(&timerhdl_ir_tx); // Start the timer of the baseband to control the carrier
						// Update reload value for the next bit (next period)
						timerhdl_ir_tx.Instance->ARR = pir_ota_data_tx_buffer[++ir_ota_data_tx_counter]; // Save the ARR for the next bit
						//__HAL_TIM_SET_COUNTER(&timerhdl_ir_tx, 0);
						ir_tx_state = IR_TX_ACTIVE; // update state machine
					} // if ( m1_check_ir_ota_frame_status() )
					else // OTA frame buffer is not ready for some reason. Let finish.
					{
						; // Send warning message to display
						; // exit
						ir_tx_state = IR_TX_COMPLETED;
					}
				} // if ( ir_ok )
				else // Task is complete. Let finish.
				{
					ir_tx_state = IR_TX_COMPLETED;
				}
				break;

			case IR_TX_ACTIVE:
				if ( !ir_ota_data_tx_active ) // Tx completed?
				{
					__HAL_TIM_DISABLE(&timerhdl_ir_tx); // Stop the timer of the baseband
					m1_ir_ota_frame_repeat_handler(ir_protocol);
					ir_tx_state = IR_TX_INIT; // reset to repeat the process
					continue; // Repeat the process
				} // if ( !ir_ota_data_tx_active )
				break;

			case IR_TX_COMPLETED:
				// Wait for user to exit
				break;

			default:
				break;
		} // switch ( ir_tx_state )
		break;
	} //while (1)

	return ir_tx_state;
} // S_M1_IR_Tx_States infrared_transmit(uint8_t init, uint8_t tx_protocol)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_learn_new_remote(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ir_data[20], prev_protocol;
	uint32_t ir_rx_frame_tn;

	infrared_decode_sys_init();
	irmp_init();
	prev_protocol = 0xFF;
	ir_rx_frame_tn = 0;

	fb_net_read_start(); // migrated: IR learn-new-remote entry

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
	u8g2_DrawStr(&m1_u8g2, 60, 20, "Reading...");
	m1_u8g2_nextpage(); // Update display RAM

	while (1) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_IRRED_RX )
			{
				irmp_data_sampler(q_item.q_data.ir_rx_data.ir_edge_te, q_item.q_data.ir_rx_data.ir_edge_dir);
				/* Decode the Rx frame */
				if (irmp_get_data(&irmp_data))
				{
					if ( irmp_data.protocol==prev_protocol ) // Same protocol?
					{
						if ( (HAL_GetTick() - ir_rx_frame_tn) < GENERAL_FRAME_REPEAT_PAUSE_TIME ) // Possibly a repeated frame?
						{
							ir_rx_frame_tn = HAL_GetTick(); // Update time for this frame
							continue; // Skip it
						}
					} // if ( irmp_data.protocol==prev_protocol )
					prev_protocol = irmp_data.protocol; // Update the latest protocol
					ir_rx_frame_tn = HAL_GetTick(); // Update time for this frame

					m1_buzzer_notification();
					u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
					u8g2_DrawBox(&m1_u8g2, 0, 30, 128, 34); // Clear old content
					u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
					u8g2_DrawStr(&m1_u8g2, 15, 40, irmp_protocol_names[irmp_data.protocol]);
					sprintf(ir_data, "Address: 0x%04X", irmp_data.address);
					u8g2_DrawStr(&m1_u8g2, 15, 50, ir_data);
					sprintf(ir_data, "Command: 0x%04X", irmp_data.command);
					u8g2_DrawStr(&m1_u8g2, 15, 60, ir_data);
					u8g2_NextPage(&m1_u8g2); // Update display RAM

					memcpy(&irmp_loopback_data, &irmp_data, sizeof(IRMP_DATA));
					new_remote_learned = 1;

				} // if (irmp_get_data (&irmp_data))
			} // if ( q_item.q_evt_type==Q_EVENT_IRRED_RX )
			else if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					fb_net_read_stop(); // migrated: IR learn-new-remote Back-exit

					; // Do extra tasks here if needed
					infrared_decode_sys_deinit();

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					; // Do other things for this task, if needed
				}
			} // else if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void infrared_learn_new_remote(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_saved_remotes(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ir_tx_complete, ir_data[20];

	// m1_gui_let_update_fw();

	/* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 2, 2, 48, 25, remote_48x25);
	u8g2_DrawStr(&m1_u8g2, 60, 20, "Sending...");
	m1_u8g2_nextpage(); // Update display RAM

	if (new_remote_learned)
	{
    	memcpy(&irmp_data, &irmp_loopback_data, sizeof(IRMP_DATA));
    	irmp_data.flags    = 1;
    	irmp_data.duty_cycle = 33;
    }
	else
	{
		return;
/*
		//IRMP_KASEIKYO_PROTOCOL;//IRMP_NEC42_PROTOCOL; //IRMP_RC6_PROTOCOL;//IRMP_SAMSUNG32_PROTOCOL; // IRMP_NEC_PROTOCOL;//IRMP_RC5_PROTOCOL;
		irmp_data.protocol = IRMP_SIRCS15_PROTOCOL;//IRMP_PIONEER_PROTOCOL;//IRMP_RC5X_PROTOCOL;//IRMP_SIRCS20_PROTOCOL;//IRMP_NEC42_PROTOCOL;//IRMP_NEC_PROTOCOL;//IRMP_SIRCS_PROTOCOL;//IRMP_RCA_PROTOCOL;//
		irmp_data.address  = 0x07;//0xAA;//0x07; // Power on/off for Samsung32
		irmp_data.command  = 0xE6;//0x1C;//0xE6; //
		irmp_data.duty_cycle = 33;
		irmp_data.flags    = 1; // don't repeat frame
*/
    }

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawBox(&m1_u8g2, 0, 30, 128, 34); // Clear old content
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawStr(&m1_u8g2, 15, 40, irmp_protocol_names[irmp_data.protocol]);
	sprintf(ir_data, "Address: 0x%04X", irmp_data.address);
	u8g2_DrawStr(&m1_u8g2, 15, 50, ir_data);
	sprintf(ir_data, "Command: 0x%04X", irmp_data.command);
	u8g2_DrawStr(&m1_u8g2, 15, 60, ir_data);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	fb_net_replay_start(); // migrated: IR saved-remote initial transmit
    infrared_encode_sys_init();
	irsnd_generate_tx_data(irmp_data); // make ota data
	infrared_transmit(1, irmp_data.protocol); // initialize the tx
	ir_tx_complete = 0;

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;
		infrared_transmit(0, 0);

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					fb_net_replay_stop(); // migrated: IR saved-remote Back-exit

					; // Do extra tasks here if needed
					if ( ir_ota_data_tx_active ) // Tx not completed?
					{
						m1_ir_ota_frame_repeat_handler(0xFF); // Reset
					} // if ( !ir_ota_data_tx_active )

					infrared_encode_sys_deinit();

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if (this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK)
				{
					; // Re-Send
					if ( !ir_tx_complete )
						continue;

					fb_net_replay_start(); // migrated: IR saved-remote OK-click re-send
					irsnd_init(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL);
					//vTaskDelay(30); // A delay here is necessary for the function to work properly!
					irsnd_generate_tx_data(irmp_data); // make ota data
					infrared_transmit(1, irmp_data.protocol); // initialize the tx
					ir_tx_complete = 0;
				}
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else if ( q_item.q_evt_type==Q_EVENT_IRRED_TX ) // Transmit completed?
			{
				fb_net_replay_stop(); // migrated: IR Q_EVENT_IRRED_TX complete
				ir_tx_complete = 1;
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void infrared_saved_remotes(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_remap_gpio(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t refresh = 0;

	/* Graphic work starts here */
	m1_u8g2_firstpage(); // This call required for page drawing in mode 1
	// Draw box for selected menu item with text color
	u8g2_DrawBox(&m1_u8g2, 0, THIS_LCD_MENU_TEXT_FIRST_ROW_Y - THIS_LCD_MENU_TEXT_ROW_SPACE + 2, M1_LCD_SUB_MENU_TEXT_FRAME_W, THIS_LCD_MENU_TEXT_ROW_SPACE);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 4, THIS_LCD_MENU_TEXT_FIRST_ROW_Y, "IR Tx output");
	// Draw arrows left and right
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 40, 0 + 2, 10, 10, arrowleft_10x10);
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 20, 0 + 2, 10, 10, arrowright_10x10);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // return to default font
	// Draw info box at the bottom
	m1_info_box_display_init(true);
	infrared_remap_status_update(ir_tx_on_ext_gpio);
	m1_u8g2_nextpage(); // Update display RAM

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;
		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if (this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK)
				{
					ir_tx_on_ext_gpio ^= 1; // Toggle
					refresh = 1;
				}
				else if (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK)
				{
					ir_tx_on_ext_gpio ^= 1; // Toggle
					refresh = 1;
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )

			if ( refresh )
			{
				refresh = 0;
				infrared_remap_status_update(ir_tx_on_ext_gpio);
			} // if ( refresh )
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void infrared_remap_gpio(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void infrared_remap_status_update(uint8_t gpio_remapped)
{
	const char *gpio_map;

	if ( !gpio_remapped )
	{
		gpio_map = "Monstatek M1";
	}
	else
	{
		gpio_map = "GPIO 16 (PD0)";
	}
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set to text color
	m1_info_box_display_draw(INFO_BOX_ROW_1, gpio_map);
	m1_u8g2_nextpage(); // Update display RAM
} // static void infrared_remap_status_update(uint8_t gpio_remapped)




/*============================================================================*/
/*
  * @brief  Initialize the decoder module
  * Input capture mode for both rising and falling edges, given in TIMx_CCR1 for each edge capture
  * @param  None
  * @retval None
 */
/*============================================================================*/
static void infrared_decode_sys_init(void)
{
	GPIO_InitTypeDef gpio_init_struct;
	TIM_IC_InitTypeDef tim_ic_init = {0};
	TIM_MasterConfigTypeDef tim_master_conf = {0};
	uint32_t tim_prescaler_val;

	/* Pin configuration: input floating */
	gpio_init_struct.Pin = IR_RX_GPIO_PIN;
	gpio_init_struct.Mode = GPIO_MODE_AF_OD;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio_init_struct.Alternate = IR_GPIO_AF_RX;
	HAL_GPIO_Init(IR_GPIO_PORT, &gpio_init_struct);

	/*  Clock Configuration for TIMER */
	IR_DECODE_TIMER_CLK();

	/* Timer Clock */
	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / 1000000) - 1; // 1MHz

	timerhdl_ir_rx.Instance = IR_DECODE_TIMER;

	timerhdl_ir_rx.Init.ClockDivision = 0;
	timerhdl_ir_rx.Init.CounterMode = TIM_COUNTERMODE_UP;
	timerhdl_ir_rx.Init.Period = IRMP_TIMEOUT_TIME;
	timerhdl_ir_rx.Init.Prescaler = tim_prescaler_val;
	timerhdl_ir_rx.Init.RepetitionCounter = 0;
	timerhdl_ir_rx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_IC_Init(&timerhdl_ir_rx) != HAL_OK)
	{
		//Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}
/*
  Input capture mode
  In input capture mode, the capture/compare registers (TIMx_CCRx) are used to latch the value of the counter after a transition detected by the corresponding ICx signal. When a
  capture occurs, the corresponding CCXIF flag (TIMx_SR register) is set and an interrupt or a DMA request can be sent if they are enabled. If a capture occurs while the CCxIF flag was
  already high, then the overcapture flag CCxOF (TIMx_SR register) is set. CCxIF can be
  cleared by software by writing it to 0 or by reading the captured data stored in the
  TIMx_CCRx register. CCxOF is cleared when it is written with 0.
  The following example shows how to capture the counter value in TIMx_CCR1 when tim_ti1
  input rises. To do this, use the following procedure:
  1. Select the proper tim_tix_in[15:0] source (internal or external) with the TI1SEL[3:0] bits in the TIMx_TISEL register.
  2. Select the active input: TIMx_CCR1 must be linked to the tim_ti1 input, so write the
  CC1S bits to 01 in the TIMx_CCMR1 register. As soon as CC1S becomes different
  from 00, the channel is configured in input and the TIMx_CCR1 register becomes read-
  only.
  3. Program the needed input filter duration in relation with the signal connected to the
  timer (when the input is one of the tim_tix (ICxF bits in the TIMx_CCMRx register). Let’s
  imagine that, when toggling, the input signal is not stable during at most five internal clock cycles. We must program a filter duration longer than these five clock cycles. We can validate a transition on tim_ti1 when eight consecutive samples with the new level
  have been detected (sampled at fDTS frequency). Then write IC1F bits to 0011 in the
  TIMx_CCMR1 register.
  4. Select the edge of the active transition on the tim_ti1 channel by writing the CC1P and CC1NP bits to 000 in the TIMx_CCER register (rising edge in this case).
  5. Program the input prescaler. In this example, the capture is to be performed at each
  valid transition, so the prescaler is disabled (write IC1PS bits to 00 in the TIMx_CCMR1 register).
  6. Enable capture from the counter into the capture register by setting the CC1E bit in the TIMx_CCER register.
  7. If needed, enable the related interrupt request by setting the CC1IE bit in the
  TIMx_DIER register, and/or the DMA request by setting the CC1DE bit in the TIMx_DIER register.
  When an input capture occurs:
  • The TIMx_CCR1 register gets the value of the counter on the active transition.
  • CC1IF flag is set (interrupt flag). CC1OF is also set if at least two consecutive captures occurred whereas the flag was not cleared.
  • An interrupt is generated depending on the CC1IE bit.
  • A DMA request is generated depending on the CC1DE bit.
  In order to handle the overcapture, it is recommended to read the data before the overcapture flag. This is to avoid missing an overcapture which may happen after reading
  the flag and before reading the data.
  Note: IC interrupt and/or DMA requests can be generated by software by setting the
  corresponding CCxG bit in the TIMx_EGR register.
*/
  /* Enable the Master/Slave Mode */
  /* SMS = 1000:Combined reset + trigger mode - Rising edge of the selected trigger input (tim_trgi)
   * reinitializes the counter, generates an update of the registers and starts the counter.
   * SMS = 0100: Reset mode - Rising edge of the selected trigger input (tim_trgi)
   * reinitializes the counter and generates an update of the registers. -Recommended by Reference Manual
   */
	tim_master_conf.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;//TIM_SMCR_MSM;
	tim_master_conf.MasterOutputTrigger = TIM_TRGO_RESET;
	if (HAL_TIMEx_MasterConfigSynchronization( &timerhdl_ir_rx, &tim_master_conf) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	tim_ic_init.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
	tim_ic_init.ICSelection = TIM_ICSELECTION_DIRECTTI;
	tim_ic_init.ICPrescaler = TIM_ICPSC_DIV1;
	tim_ic_init.ICFilter = 0;
	if (HAL_TIM_IC_ConfigChannel(&timerhdl_ir_rx, &tim_ic_init, IR_DECODE_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	/* Enable the TIMx global Interrupt */
	HAL_NVIC_SetPriority(IR_DECODE_TIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(IR_DECODE_TIMER_IRQn);

	/* Configures the TIM Update Request Interrupt source: counter overflow */
	__HAL_TIM_URS_ENABLE(&timerhdl_ir_rx);

	/* Clear update flag */
	__HAL_TIM_CLEAR_FLAG( &timerhdl_ir_rx, TIM_FLAG_UPDATE);

	/* Enable TIM Update Event Interrupt Request */
	/* Enable the CCx/CCy Interrupt Request */
	__HAL_TIM_ENABLE_IT( &timerhdl_ir_rx, TIM_FLAG_UPDATE);

	/* Enable the timer */
	//__HAL_TIM_ENABLE(&timerhdl_ir_rx);

	if (HAL_TIM_IC_Start_IT(&timerhdl_ir_rx, IR_DECODE_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	ir_rx_edge_det = EDGE_DET_IDLE;
} // static void infrared_decode_sys_init(void)


/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void infrared_decode_sys_deinit(void)
{
	HAL_TIM_IC_DeInit(&timerhdl_ir_rx);

	IR_DECODE_TIMER_CLK_DIS();
	HAL_NVIC_DisableIRQ(IR_DECODE_TIMER_IRQn);

	HAL_GPIO_DeInit(IR_GPIO_PORT, IR_RX_GPIO_PIN);

	if ( main_q_hdl != NULL)
		xQueueReset(main_q_hdl);
} // static void infrared_decode_sys_deinit(void)



/*============================================================================*/
/*
  * @brief  Initialize the encoder module
  * Output modulated (PWM carrier + base band) data on Ir Tx GPIO pin
  * @param  None
  * @retval None
 */
/*============================================================================*/
void infrared_encode_sys_init(void)
{
	//TIM_OC_InitTypeDef sConfigOC;
	TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig;
	GPIO_InitTypeDef gpio_init_struct;
	uint32_t tim_prescaler_val;

	IR_ENCODE_BASEBAND_TIMER_CLK();
	timerhdl_ir_tx.Instance = IR_ENCODE_BASEBAND_TIMER;

	if ( !ir_tx_on_ext_gpio ) // IR transmits on M1
	{
		ir_tx_remap_port = IR_GPIO_PORT;
		ir_tx_remap_pin = IR_TX_GPIO_PIN;
		ir_tx_remap_pin_alt = IR_GPIO_AF_TR;
		ir_tx_remap_timer = IR_ENCODE_CARRIER_TIMER;
		IR_ENCODE_CARRIER_TIMER_CLK();
		timerhdl_ir_carrier.Instance = IR_ENCODE_CARRIER_TIMER;
	}
	else // IR transmits on external GPIO
	{
		ir_tx_remap_port = IR_GPIO_PORT_REMAP;
		ir_tx_remap_pin = IR_TX_GPIO_PIN_REMAP;
		ir_tx_remap_pin_alt = IR_GPIO_AF_TR_REMAP;
		ir_tx_remap_timer = IR_ENCODE_CARRIER_TIMER_REMAP;
		IR_ENCODE_CARRIER_TIMER_CLK_REMAP();
		timerhdl_ir_carrier.Instance = IR_ENCODE_CARRIER_TIMER_REMAP;
	}

	/*Configure GPIO pin */
	gpio_init_struct.Pin = ir_tx_remap_pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_MEDIUM;
	gpio_init_struct.Alternate = ir_tx_remap_pin_alt;
	HAL_GPIO_Init(ir_tx_remap_port, &gpio_init_struct);

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&timerhdl_ir_carrier, &sMasterConfig) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
	sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
	sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
	sBreakDeadTimeConfig.DeadTime = 0;
	sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
	sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
	sBreakDeadTimeConfig.BreakFilter = 0;
	sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
	sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
	sBreakDeadTimeConfig.Break2Filter = 0;
	sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
	if (HAL_TIMEx_ConfigBreakDeadTime(&timerhdl_ir_carrier, &sBreakDeadTimeConfig) != HAL_OK)
	{
		Error_Handler();
	}

/*
	//TIM_SET_CAPTUREPOLARITY(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL, TIM_CCxN_ENABLE | TIM_CCx_ENABLE );
	//HAL_TIM_PWM_Start(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL);
*/
	// DeInit TIMx
	//HAL_TIM_Base_DeInit(&timerhdl_ir_tx);

	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / (1000000/IR_ENCODE_BASEBAND_PRESCALE_FACTOR)) - 1; // 1MHz/IR_ENCODE_BASEBAND_PRESCALE_FACTOR

	// Time Base configuration for timer x
	timerhdl_ir_tx.Init.Prescaler = tim_prescaler_val;
	timerhdl_ir_tx.Init.CounterMode = TIM_COUNTERMODE_UP;
	timerhdl_ir_tx.Init.Period = 0xFFFF; // Temporary value, it will be updated with valid value later
	timerhdl_ir_tx.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	timerhdl_ir_tx.Init.RepetitionCounter = 0;
	timerhdl_ir_tx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&timerhdl_ir_tx) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&timerhdl_ir_tx, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}

	/* Configures the TIM Update Request Interrupt source: counter overflow */
	//__HAL_TIM_URS_ENABLE(&timerhdl_ir_tx);

	/* Clear update flag */
	__HAL_TIM_CLEAR_FLAG( &timerhdl_ir_tx, TIM_FLAG_UPDATE);

	/* Enable TIM Update Event Interrupt Request */
	__HAL_TIM_ENABLE_IT( &timerhdl_ir_tx, TIM_FLAG_UPDATE);

	/* Enable the timer */
	//__HAL_TIM_ENABLE(&timerhdl_ir_tx);
	//HAL_TIM_Base_Stop_IT(&timerhdl_ir_tx);

	//if (HAL_TIM_Base_Start_IT(&timerhdl_ir_tx) != HAL_OK)
	//{
		//_Error_Handler(__FILE__, __LINE__);
	//	Error_Handler();
	//}

	/* Peripheral interrupt init */
	HAL_NVIC_SetPriority(IR_ENCODE_TIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(IR_ENCODE_TIMER_IRQn);

	/* TIM Disable */
	//__HAL_TIM_DISABLE(&timerhdl_ir_tx);

	irsnd_init(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL);

	ir_encode_sys_active = 1;
} // void infrared_encode_sys_init(void)



/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
void infrared_encode_sys_deinit(void)
{
	//BaseType_t ret;
	GPIO_InitTypeDef gpio_init_struct;

	if ( !ir_encode_sys_active )
		return;

	gpio_init_struct.Pin = ir_tx_remap_pin;
	gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(ir_tx_remap_port, &gpio_init_struct);
	HAL_GPIO_WritePin(ir_tx_remap_port, ir_tx_remap_pin, GPIO_PIN_RESET);

	irsnd_pwm_deinit();

	HAL_TIM_Base_DeInit(&timerhdl_ir_tx);

	if ( !ir_tx_on_ext_gpio ) // IR transmits on M1
	{
		IR_ENCODE_CARRIER_TIMER_CLK_DIS();
	}
	else
	{
		IR_ENCODE_CARRIER_TIMER_CLK_DIS_REMAP();
	}
	IR_ENCODE_BASEBAND_TIMER_CLK_DIS();

	HAL_NVIC_DisableIRQ(IR_ENCODE_TIMER_IRQn);

	if ( main_q_hdl != NULL)
		xQueueReset(main_q_hdl);

	//HAL_GPIO_DeInit(IR_GPIO_PORT, IR_TX_GPIO_PIN);
	ir_encode_sys_active = 0;
} // void infrared_encode_sys_deinit(void)
