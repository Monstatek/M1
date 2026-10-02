/*
*
*  m1_subghz_fcc.c
*
*  For Sub-GHz FCC test
*
*  B.N @ MonstaTek, Sep 2025
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sub_ghz.h"
#include "m1_sub_ghz_api.h"
#include "m1_power_ctl.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG				"SubGHz-FCC"

#define ESP32_IO9_GPIO_Port						BUTTON_RIGHT_GPIO_Port
#define ESP32_IO9_Pin							BUTTON_RIGHT_Pin
#define ESP32_RESET_GPIO_Port					ESP32_EN_GPIO_Port
#define ESP32_RESET_Pin							ESP32_EN_Pin

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

#define GUI_DISP_LINE_LEN_MAX					20

#define SUB_GHZ_CHANNEL_MAX						2
#define SUB_GHZ_CHANNEL_CRYSTAL_31_999_MHZ		0
#define SUB_GHZ_CHANNEL_CRYSTAL_32_000_MHZ		1

#define SUB_GHZ_MODULATION_MODE_CW				0
#define SUB_GHZ_MODULATION_MODE_OOK_1K			1
#define SUB_GHZ_MODULATION_MODE_OOK_1K_SHOT		2

#define SUB_GHZ_PA_PWR_LEVEL_MAX				127
#define SUB_GHZ_PA_PWR_LEVEL_DEFAULT			7 // approximately 0dB, Si4464/63/61/60 user manual, section 5.4.1, page 35, TX Power vs. PA_PWR_LVL
#define SUB_GHZ_PA_PWR_LEVEL_LOW				2 // approximately -10 dBm
#define SUB_GHZ_PA_PWR_LEVEL_MIN				0 // approximately -35 dBm
#define SUB_GHZ_PA_PWR_LEVEL_CHANGE				5

#define	SUBGHZ_FCC_BASE_FREQ_304_50				(float)304.50001
#define	SUBGHZ_FCC_BASE_FREQ_313_25				(float)313.25001
#define	SUBGHZ_FCC_BASE_FREQ_321_95				(float)321.95001
#define	SUBGHZ_FCC_BASE_FREQ_433_075			(float)433.07501
#define	SUBGHZ_FCC_BASE_FREQ_433_825			(float)433.82501
#define	SUBGHZ_FCC_BASE_FREQ_434_775			(float)434.77501
#define SUBGHZ_FCC_BASE_FREQ_915_00				(float)915.00001
#define SUBGHZ_FCC_BASE_FREQ_926_50				(float)926.50001
#define SUBGHZ_FCC_BASE_FREQ_927_95				(float)927.95001

#define SUB_GHZ_FCC_CHANNEL_SPACING				(float)0.0250

#define SUB_GHZ_FCC_SHOT_DURATION				220 //ms, test duty cycle for shot mode

// Timer setting
#define SUB_GHZ_TX_1KHZ_TIMER         		TIM1
#define SUB_GHZ_TX_1KHZ_TIMER_CLK     		__HAL_RCC_TIM1_CLK_ENABLE      /*!< Clock of the used timer */
#define SUB_GHZ_TX_1KHZ_TIMER_CLK_DIS 		__HAL_RCC_TIM1_CLK_DISABLE
#define SUB_GHZ_TX_1KHZ_TIMER_TX_CHANNEL   	TIM_CHANNEL_4
#define SUB_GHZ_TX_1KHZ_FREQUENCY			500 // PWM frequency = 500Hz => OOK frequency = 2*PWM frequency = 1KHz
#define SUB_GHZ_TX_1KHZ_PRESCALE_FACTOR		100
#define SUB_GHZ_TX_1KHZ_GPIO_AF_TR          GPIO_AF1_TIM1

//************************** C O N S T A N T **********************************/

static const float sub_ghz_base_freq_bands[9] = {	SUBGHZ_FCC_BASE_FREQ_304_50, SUBGHZ_FCC_BASE_FREQ_313_25, SUBGHZ_FCC_BASE_FREQ_321_95,
													SUBGHZ_FCC_BASE_FREQ_433_075, SUBGHZ_FCC_BASE_FREQ_433_825,	SUBGHZ_FCC_BASE_FREQ_434_775,
													SUBGHZ_FCC_BASE_FREQ_915_00, SUBGHZ_FCC_BASE_FREQ_926_50, SUBGHZ_FCC_BASE_FREQ_927_95
												};

//************************** S T R U C T U R E S *******************************

typedef enum
{
	SUBGHZ_FCC_FREQ_304_50 = 0,
	SUBGHZ_FCC_FREQ_313_25,
	SUBGHZ_FCC_FREQ_321_95,
	SUBGHZ_FCC_FREQ_433_075,
	SUBGHZ_FCC_FREQ_433_825,
	SUBGHZ_FCC_FREQ_434_775,
	SUBGHZ_FCC_FREQ_915_00,
	SUBGHZ_FCC_FREQ_926_50,
	SUBGHZ_FCC_FREQ_927_95,
	SUBGHZ_FCC_FREQ_EOL
} S_M1_SUBGHZ_FCC_FREQ_t;

/***************************** V A R I A B L E S ******************************/

static uint8_t subghz_fcc_test_band = SUBGHZ_FCC_FREQ_304_50;
static uint8_t subghz_fcc_test_channel = SUB_GHZ_CHANNEL_CRYSTAL_32_000_MHZ;
static uint8_t subghz_fcc_test_pa_pwr_lvl = SUB_GHZ_PA_PWR_LEVEL_DEFAULT;
static uint8_t subghz_fcc_test_mod_mode = SUB_GHZ_MODULATION_MODE_CW;

static TIM_HandleTypeDef   Timerhdl_Tx_1Khz;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void sub_ghz_fcc_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power);
void sub_ghz_fcc_init(void);
void sub_ghz_fcc_exit(void);
void sub_ghz_fcc_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void sub_ghz_fcc_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);
void sub_ghz_fcc_frequency(void);
void sub_ghz_fcc_channel(void);
void sub_ghz_fcc_tx_power(void);
static void sub_ghz_update_info_box(uint8_t sel_item);
static void sub_ghz_start_tx_data_1khz(void);
static void sub_ghz_stop_tx_data_1khz(void);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_init(void)
{
	;
} // void sub_ghz_fcc_init(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_exit(void)
{
	;
} // void sub_ghz_fcc_exit(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item)
{
	bool gui_update = false;

	if ( event==BUTTON_EVENT_CLICK )
	{
		if ( button_id==BUTTON_LEFT_KP_ID ) // Left arrow key to decrease value
		{
			gui_update = true;
			switch(sel_item)
			{
				case 0: // Frequency
					if ( subghz_fcc_test_band > SUBGHZ_FCC_FREQ_304_50 )
						subghz_fcc_test_band--;
					else
						subghz_fcc_test_band = SUBGHZ_FCC_FREQ_927_95;
					//subghz_fcc_test_channel = 0; // reset
					//subghz_fcc_test_pa_pwr_lvl = SUB_GHZ_PA_PWR_LEVEL_DEFAULT;
					break;

				case 1: // Channel
					if ( subghz_fcc_test_channel > 0 )
						subghz_fcc_test_channel--;
					else
						subghz_fcc_test_channel = SUB_GHZ_CHANNEL_MAX - 1;
					break;

				case 2: // Tx power
					if ( subghz_fcc_test_pa_pwr_lvl > SUB_GHZ_PA_PWR_LEVEL_CHANGE )
						subghz_fcc_test_pa_pwr_lvl -= SUB_GHZ_PA_PWR_LEVEL_CHANGE;
					else if ( subghz_fcc_test_pa_pwr_lvl > SUB_GHZ_PA_PWR_LEVEL_MIN )
						subghz_fcc_test_pa_pwr_lvl--;
					else
						subghz_fcc_test_pa_pwr_lvl = SUB_GHZ_PA_PWR_LEVEL_MAX;
					break;

				default:
					break;
			} // switch(sel_item)
		} // if ( button_id==BUTTON_LEFT_KP_ID )
		else if ( button_id==BUTTON_RIGHT_KP_ID ) // Right arrow key to increase value
		{
			gui_update = true;
			switch(sel_item)
			{
				case 0: // Frequency
					if ( subghz_fcc_test_band < SUBGHZ_FCC_FREQ_927_95 )
						subghz_fcc_test_band++;
					else
						subghz_fcc_test_band = SUBGHZ_FCC_FREQ_304_50;
					//subghz_fcc_test_channel = 0; // reset
					//subghz_fcc_test_pa_pwr_lvl = SUB_GHZ_PA_PWR_LEVEL_DEFAULT;
					break;

				case 1: // Channel
					if ( subghz_fcc_test_channel < (SUB_GHZ_CHANNEL_MAX-1) )
						subghz_fcc_test_channel++;
					else
						subghz_fcc_test_channel = 0;
					break;

				case 2: // Tx power
					if ( subghz_fcc_test_pa_pwr_lvl < SUB_GHZ_PA_PWR_LEVEL_LOW )
						subghz_fcc_test_pa_pwr_lvl++;
					else
						subghz_fcc_test_pa_pwr_lvl += SUB_GHZ_PA_PWR_LEVEL_CHANGE;
					if ( subghz_fcc_test_pa_pwr_lvl > SUB_GHZ_PA_PWR_LEVEL_MAX )
						subghz_fcc_test_pa_pwr_lvl = SUB_GHZ_PA_PWR_LEVEL_DEFAULT;
					break;

				default:
					break;
			} // switch(sel_item)
		} // else if

		if ( gui_update )
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
			// Draw box to clear old content
			m1_info_box_display_clear();
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
			sub_ghz_update_info_box(0); // Update info box for frequency
			sub_ghz_update_info_box(1); // Update info box for channel
			sub_ghz_update_info_box(2); // Update info box for tx power
			m1_u8g2_nextpage(); // Update display RAM
		} // if ( gui_update )
	} // if ( event==BUTTON_EVENT_CLICK )
} // void sub_ghz_fcc_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_frequency(void)
{
	subghz_fcc_test_mod_mode++;
	if ( subghz_fcc_test_mod_mode > SUB_GHZ_MODULATION_MODE_OOK_1K_SHOT )
		subghz_fcc_test_mod_mode = 0;

	xQueueReset(main_q_hdl); // Reset main q before return
} // void sub_ghz_fcc_frequency(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_channel(void)
{
	xQueueReset(main_q_hdl); // Reset main q before return
} // void sub_ghz_fcc_channel(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_tx_power(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint32_t t0;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	// Draw box to clear old content
	m1_info_box_display_clear();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
    u8g2_DrawXBMP(&m1_u8g2, 32, 33, 64, 30, subghz_transmission_64_30);
    m1_u8g2_nextpage(); // Update display RAM

	sub_ghz_fcc_set_opmode(SUB_GHZ_OPMODE_TX_CW, subghz_fcc_test_band, subghz_fcc_test_channel, subghz_fcc_test_pa_pwr_lvl);
	if ( subghz_fcc_test_mod_mode != SUB_GHZ_MODULATION_MODE_CW )
	{
		sub_ghz_start_tx_data_1khz();
		if ( subghz_fcc_test_mod_mode==SUB_GHZ_MODULATION_MODE_OOK_1K_SHOT )
		{
			t0 = HAL_GetTick();
			while ( (HAL_GetTick() - t0) < SUB_GHZ_FCC_SHOT_DURATION )
			{
				vTaskDelay(10);
			}
			SI446x_Start_Tx_CW(256, MODEM_MOD_TYPE_OOK); // Turn off CW mode
			sub_ghz_fcc_set_opmode(SUB_GHZ_OPMODE_RX, subghz_fcc_test_band, subghz_fcc_test_channel, subghz_fcc_test_pa_pwr_lvl);
			sub_ghz_stop_tx_data_1khz();
			xQueueReset(main_q_hdl); // Reset main q before return
			return;
		} // if ( subghz_fcc_test_mod_mode==SUB_GHZ_MODULATION_MODE_OOK_1K_SHOT )
	} // if ( subghz_fcc_test_mod_mode != SUB_GHZ_MODULATION_MODE_CW )
    xQueueReset(main_q_hdl); // Reset main q before start
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
					SI446x_Start_Tx_CW(256, MODEM_MOD_TYPE_OOK); // Turn off CW mode
					sub_ghz_fcc_set_opmode(SUB_GHZ_OPMODE_RX, subghz_fcc_test_band, subghz_fcc_test_channel, subghz_fcc_test_pa_pwr_lvl);
					if ( subghz_fcc_test_mod_mode != SUB_GHZ_MODULATION_MODE_CW )
						sub_ghz_stop_tx_data_1khz();

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Toggle ON and OFF state for Tx
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void sub_ghz_fcc_tx_power(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void sub_ghz_fcc_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power)
{
	uint8_t mod_type;
	S_M1_SubGHz_Band freq;
	static S_M1_SubGHz_Band prev_freq = 0xFF;
	struct si446x_reply_PART_INFO_map *pinfo;

	switch(band)
	{
		case SUBGHZ_FCC_FREQ_304_50:
		case SUBGHZ_FCC_FREQ_313_25:
		case SUBGHZ_FCC_FREQ_321_95:
		case SUBGHZ_FCC_FREQ_433_075:
		case SUBGHZ_FCC_FREQ_433_825:
		case SUBGHZ_FCC_FREQ_434_775:
		case SUBGHZ_FCC_FREQ_915_00:
		case SUBGHZ_FCC_FREQ_926_50:
		case SUBGHZ_FCC_FREQ_927_95:
			freq = band;
			mod_type = MODEM_MOD_TYPE_OOK;
			break;

		default:
			freq = SUBGHZ_FCC_FREQ_304_50;
			mod_type = MODEM_MOD_TYPE_OOK;
			break;
	} // switch(band)

	freq += channel*SUBGHZ_FCC_FREQ_EOL;
	if ( subghz_fcc_test_mod_mode==SUB_GHZ_MODULATION_MODE_CW )
		mod_type = MODEM_MOD_TYPE_CW;

	if ( prev_freq != freq )
	{
		prev_freq = freq; // Save current band setting
		radio_init_rx_tx(freq, mod_type, true);
		SI446x_Select_Frontend(freq);

		pinfo = SI446x_PartInfo();
		M1_LOG_I(M1_LOGDB_TAG, "Init done.\r\nPart %d Rev. %d Rom ID %d\r\n", pinfo->PART, pinfo->CHIPREV, pinfo->ROMID);
	} // if ( prev_freq != freq )

	switch (opmode)
	{
		case SUB_GHZ_OPMODE_RX:
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_RX);
			// Put the radio in Rx mode
			SI446x_Start_Rx(0); // channel
			break;

		case SUB_GHZ_OPMODE_TX:
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
			// Read INTs, clear pending ones
			SI446x_Get_IntStatus(0, 0, 0);
			SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_READY);
			// Direct mode asynchronous mode, TX direct mode on GPIO2,  modulation is sourced in real-time, OOK
			// Mode: TX_DIRECT_MODE_TYPE[7]	TX_DIRECT_MODE_GPIO[6:5]	MOD_SOURCE[4:3]	MOD_TYPE[2:0]
			//					1					10						01				000
			SI446x_Change_ModType(0xC8 | mod_type);
			// Read INTs, clear pending ones
			SI446x_Get_IntStatus(0, 0, 0);
			SI446x_Set_Tx_Power(tx_power);
			/* Start sending packet, channel 0, START immediately */
			Radio_Start_Tx(0, START_TX_COMPLETE_STATE_NOCHANGE, 0); // Do not change state after completion of the packet transmission
			break;

#ifdef M1_APP_SUB_GHZ_FCC_MENU_ENABLE
		case SUB_GHZ_OPMODE_TX_CW:
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
			SI446x_Set_Tx_Power(tx_power);
			SI446x_Start_Tx_CW(0, mod_type); // channel
			break;
#endif // #ifdef M1_APP_SUB_GHZ_FCC_MENU_ENABLE

		default: // SUB_GHZ_OPMODE_ISOLATED
			// Put the radio in sleep mode
			SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SLEEP);
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_ISOLATED);
			break;
	} // switch (opmode)

} // static void sub_ghz_fcc_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void sub_ghz_fcc_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)
{
	uint8_t i, n_items;
	uint8_t menu_text_y;

	n_items = phmenu->num_submenu_items;
	menu_text_y = THIS_LCD_MENU_TEXT_FIRST_ROW_Y;

	/* Graphic work starts here */
	m1_u8g2_firstpage(); // This call required for page drawing in mode 1
    do
    {
    	for (i=0; i<n_items; i++)
    	{
    		if ( i==sel_item )
    		{
    			// Draw box for selected menu item with text color
    			u8g2_DrawBox(&m1_u8g2, 0, menu_text_y - THIS_LCD_MENU_TEXT_ROW_SPACE + 2, M1_LCD_SUB_MENU_TEXT_FRAME_W, THIS_LCD_MENU_TEXT_ROW_SPACE);
    			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
    			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
    			u8g2_DrawStr(&m1_u8g2, 4, menu_text_y, phmenu->submenu[i]->title);

    		    // Draw arrows left and right
    		    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 15, menu_text_y - THIS_LCD_MENU_TEXT_FIRST_ROW_Y + 3, 10, 10, arrowright_10x10);
    		    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 30, menu_text_y - THIS_LCD_MENU_TEXT_FIRST_ROW_Y + 3, 10, 10, arrowleft_10x10);

				if (i==0) // Frequency
				{
					u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 50, menu_text_y - THIS_LCD_MENU_TEXT_FIRST_ROW_Y + 3, 10, 10, xtal_10x10);
				} // if (i==0)
				else if (i==2) // Tx Power
    		    {
    		    	u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 50, menu_text_y - THIS_LCD_MENU_TEXT_FIRST_ROW_Y + 3, 10, 10, target_10x10);
    		    } // else if (i==2)

    			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
    			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // return to default font
    		}
    		else
    		{
    			u8g2_DrawStr(&m1_u8g2, 4, menu_text_y, phmenu->submenu[i]->title);
    		}
    		menu_text_y += THIS_LCD_MENU_TEXT_ROW_SPACE;
    	} // for (i=0; i<n_items; i++)

    	// Draw info box at the bottom
    	m1_info_box_display_init(true);
    	sub_ghz_update_info_box(0); // Update info box for frequency
    	sub_ghz_update_info_box(1); // Update info box for channel
    	sub_ghz_update_info_box(2); // Update info box for tx power
    } while (m1_u8g2_nextpage());

} // void sub_ghz_fcc_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)




/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
static void sub_ghz_update_info_box(uint8_t sel_item)
{
	uint8_t prn_buffer[GUI_DISP_LINE_LEN_MAX + 1] = {0};
	float base_freq;

	switch ( sel_item )
	{
		case 0: // Frequency
			base_freq = sub_ghz_base_freq_bands[subghz_fcc_test_band];
			//base_freq += subghz_fcc_test_channel*SUB_GHZ_FCC_CHANNEL_SPACING;
			m1_float_to_string(prn_buffer, base_freq, 3);
			strcat(prn_buffer, " MHz");
			if ( subghz_fcc_test_mod_mode==SUB_GHZ_MODULATION_MODE_CW )
				strcat(prn_buffer, " : CW");
			else if ( subghz_fcc_test_mod_mode==SUB_GHZ_MODULATION_MODE_OOK_1K )
				strcat(prn_buffer, " : OOK-1KHz");
			else
				strcat(prn_buffer, " :OOK-1KHz-SHOT");
			m1_info_box_display_draw(INFO_BOX_ROW_1, prn_buffer);
			break;

		case 1: // Channel
			if ( subghz_fcc_test_channel==SUB_GHZ_CHANNEL_CRYSTAL_31_999_MHZ )
				sprintf(prn_buffer, "Crystal 31.999 MHz");
			else
				sprintf(prn_buffer, "Crystal 32.000 MHz");
	    	m1_info_box_display_draw(INFO_BOX_ROW_2, prn_buffer);
			break;

		case 2: // Tx power
			sprintf(prn_buffer, "Tx power level %d", subghz_fcc_test_pa_pwr_lvl);
			m1_info_box_display_draw(INFO_BOX_ROW_3, prn_buffer);
			break;

		default: // Unknown selection
			break;
	} // switch ( sel_item )
} // static void sub_ghz_update_info_box(uint8_t sel_item)




/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
static void sub_ghz_start_tx_data_1khz(void)
{
	//TIM_OC_InitTypeDef ch_config;
	TIM_MasterConfigTypeDef sMasterConfig;
	TIM_OC_InitTypeDef sConfigOC;
	//TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
	GPIO_InitTypeDef gpio_init_struct;
	uint32_t tim_prescaler_val;

	SUB_GHZ_TX_1KHZ_TIMER_CLK();
	Timerhdl_Tx_1Khz.Instance = SUB_GHZ_TX_1KHZ_TIMER;

	/*Configure GPIO pin */
	gpio_init_struct.Pin = SI4463_GPIO2_Pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_MEDIUM;
	gpio_init_struct.Alternate = SUB_GHZ_TX_1KHZ_GPIO_AF_TR;
	HAL_GPIO_Init(SI4463_GPIO2_GPIO_Port, &gpio_init_struct);

	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / (SUB_GHZ_TX_1KHZ_FREQUENCY*SUB_GHZ_TX_1KHZ_PRESCALE_FACTOR)) - 1;
	Timerhdl_Tx_1Khz.Init.Prescaler = tim_prescaler_val;
	Timerhdl_Tx_1Khz.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_Tx_1Khz.Init.Period = SUB_GHZ_TX_1KHZ_PRESCALE_FACTOR - 1;
	Timerhdl_Tx_1Khz.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_Tx_1Khz.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

	if (HAL_TIM_PWM_Init(&Timerhdl_Tx_1Khz) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

	if (HAL_TIMEx_MasterConfigSynchronization(&Timerhdl_Tx_1Khz, &sMasterConfig) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = Timerhdl_Tx_1Khz.Init.Period/2; /* Duty cycle = 50% */
	sConfigOC.OCNPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
	if (HAL_TIM_PWM_ConfigChannel(&Timerhdl_Tx_1Khz, &sConfigOC, SUB_GHZ_TX_1KHZ_TIMER_TX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	HAL_TIMEx_PWMN_Start(&Timerhdl_Tx_1Khz, SUB_GHZ_TX_1KHZ_TIMER_TX_CHANNEL);
} // static void sub_ghz_start_tx_data_1khz(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
static void sub_ghz_stop_tx_data_1khz(void)
{
	GPIO_InitTypeDef gpio_init_struct;

	HAL_TIMEx_PWMN_Stop(&Timerhdl_Tx_1Khz, SUB_GHZ_TX_1KHZ_TIMER_TX_CHANNEL);
	HAL_TIM_PWM_DeInit(&Timerhdl_Tx_1Khz);
	SUB_GHZ_TX_1KHZ_TIMER_CLK_DIS();

	/*Configure GPIO pin */
	gpio_init_struct.Pin = SI4463_GPIO2_Pin;
	gpio_init_struct.Mode = GPIO_MODE_ANALOG;
	gpio_init_struct.Pull = GPIO_PULLDOWN;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(SI4463_GPIO2_GPIO_Port, &gpio_init_struct);
} // static void sub_ghz_stop_tx_data_1khz(void)
