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
#include "m1_compile_cfg.h"
#include "irmp.h"
#include "irsnd.h"

#ifdef M1_APP_FLIPPER_COMPAT_ENABLE
#include "m1_ir_universal.h"
#endif


/*************************** D E F I N E S ************************************/


//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef   Timerhdl_IrCarrier;
TIM_HandleTypeDef   Timerhdl_IrTx;
TIM_HandleTypeDef   Timerhdl_IrRx;
IRMP_DATA 			irmp_data;

volatile S_M1_IR_Det IrRx_Edge_Det; // Flag for first falling edge detected

volatile uint8_t ir_ota_data_tx_active;
uint8_t ir_ota_data_tx_len;
volatile uint8_t ir_ota_data_tx_counter;
uint16_t *pir_ota_data_tx_buffer;
static TimerHandle_t ir_tx_timer_hdl = NULL;

static IRMP_DATA 			irmp_loopback_data;
static uint8_t				new_remote_learned;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_infrared_init(void);
void menu_infrared_exit(void);

void infrared_universal_remotes(void);
void infrared_learn_new_remote(void);
void infrared_saved_remotes(void);
S_M1_IR_Tx_States infrared_transmit(uint8_t init);

static void infrared_decode_sys_init(void);
static void infrared_decode_sys_deinit(void);
void infrared_encode_sys_init(void);
void infrared_encode_sys_deinit(void);
static void infrared_encode_timer_cb(TimerHandle_t xTimer);

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
S_M1_IR_Tx_States infrared_transmit(uint8_t init)
{
	static uint32_t ir_delay_time;
	static S_M1_IR_Tx_States ir_tx_state = IR_TX_INIT;
	uint16_t wtemp;
	BaseType_t ret;

	if (init)
	{
		ir_tx_state = IR_TX_INIT;
		return 0;
	} // if (init)

	while (1) // Conditional loop
	{
		switch ( ir_tx_state )
		{
			case IR_TX_INIT:
				wtemp = m1_make_ir_ota_multiframes();
				if ( wtemp )
				{
					if ( wtemp==TRUE ) // Return value is not a delay time?
					{
						if ( m1_check_ir_ota_frame_status() )
						{
							ir_ota_data_tx_active = TRUE;
							ir_ota_data_tx_counter = 0;
							ir_ota_data_tx_len = m1_get_ir_ota_frame_len();
							pir_ota_data_tx_buffer = m1_get_ir_ota_buffer_ptr();
							__HAL_TIM_URS_ENABLE(&Timerhdl_IrTx); // Enable URS to temporarily disable the UIF when the UG bit is set
							Timerhdl_IrTx.Instance->ARR = pir_ota_data_tx_buffer[0]; // Update Auto Reload Register ARR value
							HAL_TIM_GenerateEvent(&Timerhdl_IrTx, TIM_EVENTSOURCE_UPDATE); // Generate Update Event (set UG bit) to reload the valid ARR and reset the counter
							__HAL_TIM_URS_DISABLE(&Timerhdl_IrTx); // Disable URS to enable the UIF again
							  /* Check if the update flag is set after the Update Generation, if so clear the UIF flag */
							if (HAL_IS_BIT_SET(Timerhdl_IrTx.Instance->SR, TIM_FLAG_UPDATE))
							{
								/* Clear the update flag */
								CLEAR_BIT(Timerhdl_IrTx.Instance->SR, TIM_FLAG_UPDATE);
							}
							if ( pir_ota_data_tx_buffer[0] & 0x0001 ) // First OTA bit is a Mark?
								irsnd_on(); // Start the PWM of the carrier at the output
							__HAL_TIM_ENABLE(&Timerhdl_IrTx); // Start the timer of the baseband to control the carrier
							// Update reload value for the next bit (next period)
							Timerhdl_IrTx.Instance->ARR = pir_ota_data_tx_buffer[++ir_ota_data_tx_counter]; // Save the ARR for the next bit
							//__HAL_TIM_SET_COUNTER(&Timerhdl_IrTx, 0);
							ir_tx_state = IR_TX_ACTIVE; // update state machine
						} // if ( m1_check_ir_ota_frame_status() )
						else // OTA frame buffer is not ready for some reason. Let finish.
						{
							; // Send warning message to display
							; // exit
							ir_tx_state = IR_TX_COMPLETED;
						}
					}// if ( wtemp==TRUE )
					else // Let do a delay with the delay time given in the return value
					{
						ir_delay_time = wtemp;
						ir_delay_time /= 1000; // Convert delay time from us to ms
						ret = pdFAIL;
						if ( ir_delay_time ) // Valid delay time?
						{
							// Create the one shot timer for the delay
							ir_tx_timer_hdl = xTimerCreate( "Ir_Tx_Delay_Once", pdMS_TO_TICKS(ir_delay_time), pdFALSE, 0, infrared_encode_timer_cb);
							if( ir_tx_timer_hdl != NULL )
							{
								ret = xTimerStart( ir_tx_timer_hdl, 0 );
							} // if( ( ir_tx_timer_hdl != NULL )
						} // if ( ir_delay_time )
						if (ret!=pdPASS) // Not a valid delay time or timer fails, just finish
						{
							ir_tx_state = IR_TX_COMPLETED;
						} // if (ret!=pdPASS)
					} // else
				} // if ( wtemp )
				else // Task is complete. Let finish.
				{
					ir_tx_state = IR_TX_COMPLETED;
				}
				break;

			case IR_TX_ACTIVE:
				if ( !ir_ota_data_tx_active ) // Tx completed?
				{
					__HAL_TIM_DISABLE(&Timerhdl_IrTx); // Stop the timer of the baseband
					m1_ir_ota_frame_post_process(irmp_data.protocol);
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
} // S_M1_IR_Tx_States infrared_transmit(uint8_t init)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void infrared_universal_remotes(void)
{
#ifdef M1_APP_FLIPPER_COMPAT_ENABLE
	/* Use the full IRDB browser and Flipper .ir file support */
	ir_universal_init();
	ir_universal_run();
	ir_universal_deinit();
#else
	/* Fallback: basic stub that waits for BACK button */
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					break;
				}
			}
		}
	}
#endif
} // void infrared_universal_remotes(void)



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
	uint8_t ir_data[20];

	infrared_decode_sys_init();
	irmp_init();

	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

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
					m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off

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
    }
	else
	{
		return;
		irmp_data.protocol = IRMP_NEC_PROTOCOL;//IRMP_RC5_PROTOCOL;//IRMP_NEC_PROTOCOL; // use NEC protocol
		irmp_data.address  = 0xFD02; // set address to 0x00FF
		irmp_data.command  = 0x005C; // set command to 0x0001
		irmp_data.flags    = 1; // don't repeat frame
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

	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
    infrared_encode_sys_init();
	irsnd_generate_tx_data(irmp_data); // make ota data
	infrared_transmit(1); // initialize the tx
	ir_tx_complete = 0;

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;
		infrared_transmit(0);

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
					m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off

					; // Do extra tasks here if needed
					if ( ir_ota_data_tx_active ) // Tx not completed?
					{
						m1_ir_ota_frame_post_process(0xFF); // Reset
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

					m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
					irsnd_init(&Timerhdl_IrCarrier, IR_ENCODE_TIMER_TX_CHANNEL);
					vTaskDelay(20); // A delay here is necessary for the function to work properly!
					irsnd_generate_tx_data(irmp_data); // make ota data
					infrared_transmit(1); // initialize the tx
					ir_tx_complete = 0;
				}
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else if ( q_item.q_evt_type==Q_EVENT_IRRED_TX ) // Transmit completed?
			{
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
				ir_tx_complete = 1;
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void infrared_saved_remotes(void)



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

	Timerhdl_IrRx.Instance = IR_DECODE_TIMER;

	Timerhdl_IrRx.Init.ClockDivision = 0;
	Timerhdl_IrRx.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_IrRx.Init.Period = IRMP_TIMEOUT_TIME;
	Timerhdl_IrRx.Init.Prescaler = tim_prescaler_val;
	Timerhdl_IrRx.Init.RepetitionCounter = 0;
	Timerhdl_IrRx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_IC_Init(&Timerhdl_IrRx) != HAL_OK)
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
	if (HAL_TIMEx_MasterConfigSynchronization( &Timerhdl_IrRx, &tim_master_conf) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	tim_ic_init.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
	tim_ic_init.ICSelection = TIM_ICSELECTION_DIRECTTI;
	tim_ic_init.ICPrescaler = TIM_ICPSC_DIV1;
	tim_ic_init.ICFilter = 0;
	if (HAL_TIM_IC_ConfigChannel(&Timerhdl_IrRx, &tim_ic_init, IR_DECODE_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	/* Enable the TIMx global Interrupt */
	HAL_NVIC_SetPriority(IR_DECODE_TIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(IR_DECODE_TIMER_IRQn);

	/* Configures the TIM Update Request Interrupt source: counter overflow */
	__HAL_TIM_URS_ENABLE(&Timerhdl_IrRx);

	/* Clear update flag */
	__HAL_TIM_CLEAR_FLAG( &Timerhdl_IrRx, TIM_FLAG_UPDATE);

	/* Enable TIM Update Event Interrupt Request */
	/* Enable the CCx/CCy Interrupt Request */
	__HAL_TIM_ENABLE_IT( &Timerhdl_IrRx, TIM_FLAG_UPDATE);

	/* Enable the timer */
	//__HAL_TIM_ENABLE(&Timerhdl_IrRx);

	if (HAL_TIM_IC_Start_IT(&Timerhdl_IrRx, IR_DECODE_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	IrRx_Edge_Det = EDGE_DET_IDLE;
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
	HAL_TIM_IC_DeInit(&Timerhdl_IrRx);

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
	//TIM_OC_InitTypeDef ch_config;
	TIM_MasterConfigTypeDef sMasterConfig;
	GPIO_InitTypeDef gpio_init_struct;
	uint32_t tim_prescaler_val;

	IR_ENCODE_CARRIER_TIMER_CLK();
	IR_ENCODE_BASEBAND_TIMER_CLK();

	Timerhdl_IrTx.Instance = IR_ENCODE_BASEBAND_TIMER;
	Timerhdl_IrCarrier.Instance = IR_ENCODE_CARRIER_TIMER;

	/*Configure GPIO pin */
	gpio_init_struct.Pin = IR_TX_GPIO_PIN;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_MEDIUM;
	gpio_init_struct.Alternate = IR_GPIO_AF_TR;
	HAL_GPIO_Init(IR_GPIO_PORT, &gpio_init_struct);

	irsnd_init(&Timerhdl_IrCarrier, IR_ENCODE_TIMER_TX_CHANNEL);
/*
	// HAL_TIM_PWM_DeInit(&Timerhdl_IrCarrier);

	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / (IR_ENCODE_CARRIER_FREQ_36_KHZ*IR_ENCODE_CARRIER_PRESCALE_FACTOR)) - 1;

	// Time base configuration for timer x
	Timerhdl_IrCarrier.Init.Prescaler = tim_prescaler_val;
	Timerhdl_IrCarrier.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_IrCarrier.Init.Period = IR_ENCODE_CARRIER_PRESCALE_FACTOR - 1;
	Timerhdl_IrCarrier.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_IrCarrier.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_PWM_Init(&Timerhdl_IrCarrier) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	// Output Compare Timing Mode configuration: Channel 4N
	ch_config.OCMode = TIM_OCMODE_PWM1;
	ch_config.Pulse = Timerhdl_IrCarrier.Init.Period / 4; // Duty cycle = 25%
	ch_config.OCPolarity = TIM_OCPOLARITY_HIGH;
	ch_config.OCNPolarity = TIM_OCNPOLARITY_HIGH;
	ch_config.OCFastMode = TIM_OCFAST_DISABLE;
	ch_config.OCIdleState = TIM_OCIDLESTATE_RESET;
	ch_config.OCNIdleState = TIM_OCNIDLESTATE_RESET;
	if (HAL_TIM_PWM_ConfigChannel(&Timerhdl_IrCarrier, &ch_config, IR_ENCODE_TIMER_TX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	//TIM_SET_CAPTUREPOLARITY(&Timerhdl_IrCarrier, IR_ENCODE_TIMER_TX_CHANNEL, TIM_CCxN_ENABLE | TIM_CCx_ENABLE );

	//HAL_TIM_PWM_Start(&Timerhdl_IrCarrier, IR_ENCODE_TIMER_TX_CHANNEL);
*/

	// DeInit TIMx
	//HAL_TIM_Base_DeInit(&Timerhdl_IrTx);

	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / 1000000) - 1; // 1MHz

	// Time Base configuration for timer x
	Timerhdl_IrTx.Init.Prescaler = tim_prescaler_val;
	Timerhdl_IrTx.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_IrTx.Init.Period = 0xFFFF; // Temporary value, it will be updated with valid value later
	Timerhdl_IrTx.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_IrTx.Init.RepetitionCounter = 0;
	Timerhdl_IrTx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&Timerhdl_IrTx) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&Timerhdl_IrTx, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}

	/* Configures the TIM Update Request Interrupt source: counter overflow */
	//__HAL_TIM_URS_ENABLE(&Timerhdl_IrTx);

	/* Clear update flag */
	__HAL_TIM_CLEAR_FLAG( &Timerhdl_IrTx, TIM_FLAG_UPDATE);

	/* Enable TIM Update Event Interrupt Request */
	__HAL_TIM_ENABLE_IT( &Timerhdl_IrTx, TIM_FLAG_UPDATE);

	/* Enable the timer */
	//__HAL_TIM_ENABLE(&Timerhdl_IrTx);
	//HAL_TIM_Base_Stop_IT(&Timerhdl_IrTx);

	//if (HAL_TIM_Base_Start_IT(&Timerhdl_IrTx) != HAL_OK)
	//{
		//_Error_Handler(__FILE__, __LINE__);
	//	Error_Handler();
	//}

	/* Peripheral interrupt init */
	HAL_NVIC_SetPriority(IR_ENCODE_TIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(IR_ENCODE_TIMER_IRQn);

	/* TIM Disable */
	//__HAL_TIM_DISABLE(&Timerhdl_IrTx);
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
	BaseType_t ret;
	GPIO_InitTypeDef gpio_init_struct;

	gpio_init_struct.Pin = IR_DRV_Pin;
	gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(IR_GPIO_PORT, &gpio_init_struct);
	HAL_GPIO_WritePin(IR_GPIO_PORT, IR_DRV_Pin, GPIO_PIN_RESET);

	HAL_TIM_PWM_DeInit(&Timerhdl_IrCarrier);
	HAL_TIM_Base_DeInit(&Timerhdl_IrTx);

	IR_ENCODE_CARRIER_TIMER_CLK_DIS();
	IR_ENCODE_BASEBAND_TIMER_CLK_DIS();

	HAL_NVIC_DisableIRQ(IR_ENCODE_TIMER_IRQn);

	if( ir_tx_timer_hdl != NULL )
	{
		ret = xTimerDelete(ir_tx_timer_hdl, 0);
		assert_param(ret==pdPASS);
		ir_tx_timer_hdl = NULL;
	}

	if ( main_q_hdl != NULL)
		xQueueReset(main_q_hdl);

	//HAL_GPIO_DeInit(IR_GPIO_PORT, IR_TX_GPIO_PIN);
} // void infrared_encode_sys_deinit(void)



/*============================================================================*/
/**
  * This is a callback function for the software timer
  * It's used as a delay function for the Tx task
  *
  */
/*============================================================================*/
static void infrared_encode_timer_cb(TimerHandle_t xTimer)
{
	S_M1_Main_Q_t q_item;

	q_item.q_data.ir_tx_data = 1; // any value, not used
	q_item.q_evt_type = Q_EVENT_IRRED_TX;
	xQueueSend(main_q_hdl, &q_item, portMAX_DELAY);
} // static void infrared_encode_timer_cb(TimerHandle_t xTimer)
