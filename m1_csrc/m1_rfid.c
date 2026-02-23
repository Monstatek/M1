/* See COPYING.txt for license details. */

/*
*
*  m1_rfid.c
*
*  M1 RFID functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_rfid.h"
#include "uiView.h"
#include "m1_virtual_kb.h"
#include "m1_storage.h"
#include "m1_ring_buffer.h"
#include "m1_sdcard_man.h"
#include "Res_String.h"
#include "lfrfid.h"
#include "lfrfid_file.h"
#include "privateprofilestring.h"
#include "m1_file_util.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"RFID"

// m1_sdcard_man.h
//#define RFID_FILEPATH						"/RFID"
#define RFID_FILE_EXTENSION_TMP				"rfid" // rfh for NFC
//#define RFID_FILE_PREFIX					"rfid_" // nfc_ for NFC
//#define RFID_FILE_INFIX						"" // Not used

#define RFID_READ_MORE_OPTIONS					3
#define RFID_READ_SAVED_MORE_OPTIONS				6
#define RFID_READ_ADD_MANUALLY_MORE_OPTIONS (LFRFIDProtocolMax)

//************************** C O N S T A N T **********************************/
const char *m1_rfid_save_mode_options[] = {
	"Emulate",
	"Write",
	"Edit",
	"Rename",
	"Delete",
	"Info"
};

const char *m1_rfid_more_options[] = {
	"Save",
	"Emulate",
	"Write"
};

enum {
	VIEW_MODE_LFRFID_NONE,
	VIEW_MODE_LFRFID_READ,
	VIEW_MODE_LFRFID_READ_SUBMENU,
	VIEW_MODE_LFRFID_READ_SAVE,
	VIEW_MODE_LFRFID_READ_EMULATE,
	VIEW_MODE_LFRFID_READ_WRITE,
	VIEW_MODE_LFRFID_READ_END
};

enum {
	VIEW_MODE_LFRFID_ADDM_SUBMENU = 1,
	VIEW_MODE_LFRFID_ADDM_EDIT,
	VIEW_MODE_LFRFID_ADDM_SAVE,
	VIEW_MODE_LFRFID_ADDM_END
};

enum {
	VIEW_MODE_LFRFID_SAVED_BROWSE = 1,
	VIEW_MODE_LFRFID_SAVED_SUBMENU,
	VIEW_MODE_LFRFID_SAVED_EMULATE,
	VIEW_MODE_LFRFID_SAVED_WRITE,
	VIEW_MODE_LFRFID_SAVED_EDIT,
	VIEW_MODE_LFRFID_SAVED_RENAME,
	VIEW_MODE_LFRFID_SAVED_DELETE,
	VIEW_MODE_LFRFID_SAVED_INFO,
	VIEW_MODE_LFRFID_SAVED_END
};

//************************** S T R U C T U R E S *******************************

typedef enum
{
	RFID_READ_DISPLAY_PARAM_READING_READY = 0,
	RFID_READ_DISPLAY_PARAM_READING_COMPLETE,
	RFID_READ_DISPLAY_PARAM_READING_EOL
} S_M1_rfid_read_display_mode_t;

/***************************** V A R I A B L E S ******************************/

static S_M1_file_info *f_info = NULL;

static char lfrfid_protocol_menu_list[LFRFIDProtocolMax][24];
static char *lfrfid_protocol_menu_items[LFRFIDProtocolMax];
static uint8_t lfrfid_uiview_gui_latest_param;
S_M1_RFID_Record_t record_stat;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void rfid_125khz_read(void);
void rfid_125khz_saved(void);
void rfid_125khz_add_manually(void);
void rfid_125khz_utilities(void);

// ***********************************************
// rfid_125khz_read
// read
static void lfrfid_read_init(void);
static void lfrfid_read_create(uint8_t param);
static void lfrfid_read_destroy(uint8_t param);
static void lfrfid_read_update(uint8_t param);
static int lfrfid_read_message(void);
static int lfrfid_read_kp_handler(void);
// read - submenu(save,emulate,write)
static void lfrfid_read_submenu_init(void);
static void lfrfid_read_submenu_create(uint8_t param);
static void lfrfid_read_submenu_destroy(uint8_t param);
static void lfrfid_read_submenu_update(uint8_t param);
static int lfrfid_read_submenu_message(void);
static int lfrfid_read_submenu_kp_handler(void);
// read - save
static void lfrfid_read_save_init(void);
static void lfrfid_read_save_create(uint8_t param);
static void lfrfid_read_save_destroy(uint8_t param);
static void lfrfid_read_save_update(uint8_t param);
static int lfrfid_read_save_message(void);
static int lfrfid_read_save_kp_handler(void);
// read - emulate
static void lfrfid_read_emulate_init(void);
static void lfrfid_read_emulate_create(uint8_t param);
static void lfrfid_read_emulate_destroy(uint8_t param);
static void lfrfid_read_emulate_update(uint8_t param);
static int lfrfid_read_emulate_message(void);
static int lfrfid_read_emulate_kp_handler(void);
// read - write
static void lfrfid_read_write_init(void);
static void lfrfid_read_write_create(uint8_t param);
static void lfrfid_read_write_destroy(uint8_t param);
static void lfrfid_read_write_update(uint8_t param);
static int lfrfid_read_write_message(void);
static int lfrfid_read_write_kp_handler(void);

// ***********************************************
// rfid_125khz_saved
// save - browser
static void lfrfid_saved_browse_init(void);
static void lfrfid_saved_browse_create(uint8_t param);
static void lfrfid_saved_browse_destroy(uint8_t param);
static void lfrfid_saved_browse_update(uint8_t param);
static int lfrfid_saved_browse_message(void);
static int lfrfid_saved_browse_kp_handler(void);
// save - submenu
static void lfrfid_saved_submenu_init(void);
static void lfrfid_saved_submenu_create(uint8_t param);
static void lfrfid_saved_submenu_destroy(uint8_t param);
static void lfrfid_saved_submenu_update(uint8_t param);
static int lfrfid_saved_submenu_message(void);
static int lfrfid_saved_submenu_kp_handler(void);
// save - submenu - emulate
static void lfrfid_saved_emulate_init(void);
static void lfrfid_saved_emulate_create(uint8_t param);
static void lfrfid_saved_emulate_destroy(uint8_t param);
static void lfrfid_saved_emulate_update(uint8_t param);
static int lfrfid_saved_emulate_message(void);
static int lfrfid_saved_emulate_kp_handler(void);
// save - submenu - write
static void lfrfid_saved_write_init(void);
static void lfrfid_saved_write_create(uint8_t param);
static void lfrfid_saved_write_destroy(uint8_t param);
static void lfrfid_saved_write_update(uint8_t param);
static int lfrfid_saved_write_message(void);
static int lfrfid_saved_write_kp_handler(void);
// save - submenu - edit
static void lfrfid_saved_edit_init(void);
static void lfrfid_saved_edit_create(uint8_t param);
static void lfrfid_saved_edit_destroy(uint8_t param);
static void lfrfid_saved_edit_update(uint8_t param);
static int lfrfid_saved_edit_message(void);
static int lfrfid_saved_edit_kp_handler(void);
// save - submenu - rename
static void lfrfid_saved_rename_init(void);
static void lfrfid_saved_rename_create(uint8_t param);
static void lfrfid_saved_rename_destroy(uint8_t param);
static void lfrfid_saved_rename_update(uint8_t param);
static int lfrfid_saved_rename_message(void);
static int lfrfid_saved_rename_kp_handler(void);
// save - submenu - delete
static void lfrfid_saved_delete_init(void);
static void lfrfid_saved_delete_create(uint8_t param);
static void lfrfid_saved_delete_destroy(uint8_t param);
static void lfrfid_saved_delete_update(uint8_t param);
static int lfrfid_saved_delete_message(void);
static int lfrfid_saved_delete_kp_handler(void);
// save - submenu - info
static void lfrfid_saved_info_init(void);
static void lfrfid_saved_info_create(uint8_t param);
static void lfrfid_saved_info_destroy(uint8_t param);
static void lfrfid_saved_info_update(uint8_t param);
static int lfrfid_saved_info_message(void);
static int lfrfid_saved_info_kp_handler(void);

// ***********************************************
// rfid_125khz_add_manually
// dd manually - em4100,em4100/32, em4100/16, h10301...
static void lfrfid_addm_submenu_init(void);
static void lfrfid_addm_submenu_create(uint8_t param);
static void lfrfid_addm_submenu_destroy(uint8_t param);
static void lfrfid_addm_submenu_update(uint8_t param);
static int lfrfid_addm_submenu_message(void);
static int lfrfid_addm_submenu_kp_handler(void);
// add manually - edit
static void lfrfid_addm_edit_init(void);
static void lfrfid_addm_edit_create(uint8_t param);
static void lfrfid_addm_edit_destroy(uint8_t param);
static void lfrfid_addm_edit_update(uint8_t param);
static int lfrfid_addm_edit_message(void);
static int lfrfid_addm_edit_kp_handler(void);
// add manually - save
static void lfrfid_addm_save_init(void);
static void lfrfid_addm_save_create(uint8_t param);
static void lfrfid_addm_save_destroy(uint8_t param);
static void lfrfid_addm_save_update(uint8_t param);
static int lfrfid_addm_save_message(void);
static int lfrfid_addm_save_kp_handler(void);

// ***********************************************
// sub-functions
static void lfrfid_write_screen_draw(int param, char* filename);


//************************** C O N S T A N T **********************************/

static const view_func_t view_lfrfid_read_table[] = {
	NULL,
	lfrfid_read_init,
	lfrfid_read_submenu_init,
	lfrfid_read_save_init,
	lfrfid_read_emulate_init,
	lfrfid_read_write_init,
};

static const view_func_t view_lfrfid_saved_table[] = {
	NULL,
	lfrfid_saved_browse_init,
	lfrfid_saved_submenu_init,
	lfrfid_saved_emulate_init,
	lfrfid_saved_write_init,
	lfrfid_saved_edit_init,
	lfrfid_saved_rename_init,
	lfrfid_saved_delete_init,
	lfrfid_saved_info_init,
};

static const view_func_t view_lfrfid_add_manually_table[] = {
	NULL,
	lfrfid_addm_submenu_init,
	lfrfid_addm_edit_init,
	lfrfid_addm_save_init,
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_protocol_make_menu_list(int protocol_idx, char *szString)
{
	const char* protocol = protocol_get_name(protocol_idx);
	const char* manufacturer = protocol_get_manufacturer(protocol_idx);

	if(manufacturer && protocol)
		sprintf(szString, "%s %s",manufacturer, protocol);
	else
		sprintf(szString, "%s", protocol);
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void menu_125khz_rfid_init(void)
{
	lfrfid_Init();
	// Enable EXT_5V
	HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_SET);

} // void menu_125khz_rfid_init(void)


/*============================================================================*/
/*
 * This function will exit this sub-menu and return to the upper level menu.
 */
/*============================================================================*/
void menu_125khz_rfid_deinit(void)
{
	lfrfid_DeInit();
	// RFID_PULL output LOW (MUTE OFF - DET)
	HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);

	// Disable EXT_5V
	HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_RESET);
} // void menu_125khz_rfid_deinit(void)


/*============================================================================*/
/*
  * @brief  rfid_125khz_read
  * @param  None
  * @retval None
 */
/*============================================================================*/
void rfid_125khz_read(void)
{
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// init
	m1_uiView_functions_init(VIEW_MODE_LFRFID_READ_END, view_lfrfid_read_table);
	m1_uiView_display_switch(VIEW_MODE_LFRFID_READ, 0);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}

} // void rfid_125khz_read(void)



/*============================================================================*/
/*
  * @brief  rfid read
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_read_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_STOP);

		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )	// retry
	{
		// Do other things for this task, if needed
		if(record_stat==RFID_READ_DONE)
		{
			lfrfid_read_create(RFID_READ_DISPLAY_PARAM_READING_READY);
		}
	}
	else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )	// more
	{
		// Do other things for this task, if needed
		if(record_stat==RFID_READ_DONE)
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_RESET);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_create(uint8_t param)
{
	if( param==RFID_READ_DISPLAY_PARAM_READING_READY )
	{
		record_stat = RFID_READ_READING;
		m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_START_READ);
	}
	else if( param==RFID_READ_DISPLAY_PARAM_READING_COMPLETE )
	{
		record_stat = RFID_READ_DONE;
	}

	m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_destroy(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_update(uint8_t param)
{
    if ( lfrfid_uiview_gui_latest_param==X_MENU_UPDATE_RESET )
    {
    	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_RESTORE);
    }
    lfrfid_uiview_gui_latest_param = param; // Update new param

    /* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

    if( param==RFID_READ_DISPLAY_PARAM_READING_READY )	// reading
    {
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_DrawXBMP(&m1_u8g2, 1, 4, 125, 24, rfid_read_125x24);

		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 0,40,128,res_string(IDS_READING), TEXT_ALIGN_CENTER);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		m1_draw_text_box(&m1_u8g2, 0,50,128,10,res_string(IDS_HOLD_CARD_), TEXT_ALIGN_CENTER);

		u8g2_NextPage(&m1_u8g2); // Update display RAM
    }
    else if( param==RFID_READ_DISPLAY_PARAM_READING_COMPLETE )	// read done
    {
    	char *hex;
    	char *fc;
    	char *card_num;

    	char szString[64];
    	lfrfid_protocol_make_menu_list(lfrfid_tag_info.protocol, szString);

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 2, 12,124,szString, TEXT_ALIGN_LEFT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		protocol_render_data(lfrfid_tag_info.protocol,szString);
        hex = strtok(szString,"\n");
        fc = strtok(NULL,"\n");
        card_num = strtok(NULL,"\n");

		m1_draw_text(&m1_u8g2, 2, 22,124,hex, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 2, 32,124,fc, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 2, 42,124,card_num, TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, res_string(IDS_RETRY),res_string(IDS_MORE),arrowright_8x8 );

		m1_u8g2_nextpage(); // Update display RAM
    }
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_read_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_read_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
		{
			// Do other things for this task
			osDelay(100);
			m1_uiView_display_update(RFID_READ_DISPLAY_PARAM_READING_COMPLETE);

			record_stat = RFID_READ_DONE;

			m1_buzzer_notification();
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);

			//m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_STOP);

		} // else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_read_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_READ, lfrfid_read_create, lfrfid_read_update, lfrfid_read_destroy, lfrfid_read_message);
}

/*============================================================================*/
/*
  * @brief  rfid submenu
  *
  *
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_read_submenu_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	uint8_t menu_index;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ, RFID_READ_DISPLAY_PARAM_READING_COMPLETE);
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
	{
		// Do other things for this task, if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ, RFID_READ_DISPLAY_PARAM_READING_COMPLETE);
	}
	else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	{
		menu_index = m1_gui_submenu_update(NULL, 0, 0, MENU_UPDATE_NONE); // Get menu index
		// Do other things for this task, if needed
		if ( menu_index==0 )
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SAVE, 0);
		else if ( menu_index==1 )
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_EMULATE, 0);
		else if ( menu_index==2 )
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_WRITE, 0);
	}
	else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_UP);
	}
	else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_DOWN);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_submenu_create(uint8_t param)
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
static void lfrfid_read_submenu_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_submenu_update(uint8_t param)
{
	m1_gui_submenu_update(m1_rfid_more_options, RFID_READ_MORE_OPTIONS, 0, param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_read_submenu_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_read_submenu_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		} // else if ( q_item.q_evt_type==Q_EVENT_RFID_COMPLETE )
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_read_submenu_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_READ_SUBMENU, lfrfid_read_submenu_create, lfrfid_read_submenu_update, lfrfid_read_submenu_destroy, lfrfid_read_submenu_message);
}

/*============================================================================*/
/*
  * @brief  rfid save
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_read_save_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_read_save_create(uint8_t param)
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
static void lfrfid_read_save_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_save_update(uint8_t param)
{
	char data_buffer[64];
	BaseType_t ret;
	const uint8_t *pBitmap;

	ret = lfrfid_save_file_keyboard(data_buffer);
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
	}
	else
	{
		pBitmap = micro_sd_card_error;

		if (ret==0)
		{
			if(lfrfid_profile_save(data_buffer, &lfrfid_tag_info))
				pBitmap = nfc_saved_63_63;
		}

		m1_draw_icon(M1_DISP_DRAW_COLOR_TXT, 32, 0, 63, 63, pBitmap);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
	}
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_read_save_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_read_save_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if(q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_read_save_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_READ_SAVE, lfrfid_read_save_create, lfrfid_read_save_update, lfrfid_read_save_destroy, lfrfid_read_save_message);
}


/*============================================================================*/
/*
  * @brief  rfid emulate
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_read_emulate_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE_STOP);
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_read_emulate_create(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

	m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE);

    m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_emulate_destroy(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_emulate_update(uint8_t param)
{
	char szString[24];
	const char* protocol = protocol_get_name(lfrfid_tag_info.protocol);
	strcpy(szString, protocol);

	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 2, 8, 48, 48, nfc_emit_48x48);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	m1_draw_text(&m1_u8g2, 60, 20, 60, res_string(IDS_EMULATING), TEXT_ALIGN_LEFT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	m1_draw_text(&m1_u8g2, 60, 30, 60, res_string(IDS_UNSAVED), TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 60, 40, 60, szString, TEXT_ALIGN_LEFT);

	m1_u8g2_nextpage(); // Update display RAM
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_read_emulate_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_read_emulate_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
		{
			// Do other things for this task
			//osDelay(100);
			//m1_uiView_display_update(1);

			record_stat = RFID_READ_DONE;

			m1_buzzer_notification();
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
			//m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_STOP);
		}
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_read_emulate_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_READ_EMULATE, lfrfid_read_emulate_create, lfrfid_read_emulate_update, lfrfid_read_emulate_destroy, lfrfid_read_emulate_message);
}


/*============================================================================*/
/*
  * @brief  rfid write
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_read_write_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE_STOP);

		memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));

		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_read_write_create(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
	memcpy(lfrfid_tag_info_back, &lfrfid_tag_info, sizeof(LFRFID_TAG_INFO));

	lfrfid_write_count = 0;
	m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);
	m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_write_destroy(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_read_write_update(uint8_t param)
{
	lfrfid_write_screen_draw(param , NULL);

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_read_write_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_read_write_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if( q_item.q_evt_type==Q_EVENT_UI_LFRFID_WRITE_DONE)
		{
			record_stat = RFID_READ_READING;
			m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_START_READ);
		}
		else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
		{
			// Do other things for this task
			if(lfrfid_write_count > LFRFID_WRITE_ERROR_COUNT)
			{
				m1_uiView_display_update(2);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				return 1;
			}

			if(lfrfid_write_verify(lfrfid_tag_info_back, &lfrfid_tag_info))
			{
				m1_uiView_display_update(1);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);

			}
			else
			{
				// retry
				memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));
				m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);	// retry
			}

		} //
		else if ( q_item.q_evt_type==Q_EVENT_UI_LFRFID_READ_TIMEOUT )
		{
			if(lfrfid_write_count > LFRFID_WRITE_ERROR_COUNT)
			{
				m1_uiView_display_update(2);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				return 1;
			}

			// retry
			memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));
			m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);
			m1_uiView_display_update(0);
		}
		else if(q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			//rfid_125khz_saved();
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_read_write_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_READ_WRITE, lfrfid_read_write_create, lfrfid_read_write_update, lfrfid_read_write_destroy, lfrfid_read_write_message);
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void rfid_125khz_saved(void)
{
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// init
	m1_uiView_functions_init(VIEW_MODE_LFRFID_SAVED_END, view_lfrfid_saved_table);
	m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}

} // void rfid_125khz_saved(void)


/*============================================================================*/
/*
  * @brief  rfid save browser
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_browse_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )	//
	{
		// Do other things for this task, if needed
		//lfrfid_read_create(0);
		//m1_uiView_display_update(0);
	}
	else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )	//
	{
		// Do other things for this task, if needed

	}

	return 1;
}


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_browse_create(uint8_t param)
{
	m1_uiView_display_update(0);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_browse_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_browse_update(uint8_t param)
{
    if ( lfrfid_uiview_gui_latest_param==X_MENU_UPDATE_RESET )
    {
    	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_RESTORE);
    }
	lfrfid_uiview_gui_latest_param = param; // Update new param

	do
	{
		f_info = storage_browse();
		if ( f_info->file_is_selected )
		{
			memcpy(lfrfid_tag_info.filepath, f_info->dir_name, sizeof(lfrfid_tag_info.filepath));
			memcpy(lfrfid_tag_info.filename, f_info->file_name, sizeof(lfrfid_tag_info.filename));

			if(lfrfid_profile_load(f_info, RFID_FILE_EXTENSION_TMP))
			{
				m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_RESET);
				lfrfid_uiview_gui_latest_param = X_MENU_UPDATE_RESET; // Update new param
			}
			else
			{
				m1_message_box(&m1_u8g2, res_string(IDS_UNSUPPORTED_FILE_)," ",NULL,  res_string(IDS_BACK));
				continue;
			}
		} // if ( f_info->file_is_selected )
		else	// user escaped?
		{
		    m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
		}
	} while (0);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_browse_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_browse_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if(q_item.q_evt_type==Q_EVENT_MENU_EXIT)
		{
			m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
			xQueueReset(main_q_hdl); // Reset main q before return
			return 0;
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_browse_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_BROWSE, lfrfid_saved_browse_create, lfrfid_saved_browse_update, lfrfid_saved_browse_destroy, lfrfid_saved_browse_message);
}


/*============================================================================*/
/*
  * @brief  rfid save submenu
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_submenu_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	uint8_t menu_index, view_id;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	{
		menu_index = m1_gui_submenu_update(NULL, 0, 0, MENU_UPDATE_NONE); // Get menu index
		// Do other things for this task, if needed
		view_id = 0xFF;
		switch (menu_index)
		{
			case 0:
				view_id = VIEW_MODE_LFRFID_SAVED_EMULATE;
				break;

			case 1:
				view_id = VIEW_MODE_LFRFID_SAVED_WRITE;
				break;

			case 2:
				view_id = VIEW_MODE_LFRFID_SAVED_EDIT;
				break;

			case 3:
				view_id = VIEW_MODE_LFRFID_SAVED_RENAME;
				break;

			case 4:
				view_id = VIEW_MODE_LFRFID_SAVED_DELETE;
				break;

			case 5:
				view_id = VIEW_MODE_LFRFID_SAVED_INFO;
				break;

			default:
				break;
		} // switch (menu_index)
		if (view_id != 0xFF)
		{
			m1_uiView_display_switch(view_id, 0);
		}
	} // else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_UP);
	}
	else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_DOWN);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_submenu_create(uint8_t param)
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
static void lfrfid_saved_submenu_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_submenu_update(uint8_t param)
{
	m1_gui_submenu_update(m1_rfid_save_mode_options, RFID_READ_SAVED_MORE_OPTIONS, 0, param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_submenu_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_submenu_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_submenu_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_SUBMENU, lfrfid_saved_submenu_create, lfrfid_saved_submenu_update, lfrfid_saved_submenu_destroy, lfrfid_saved_submenu_message);
}

/*============================================================================*/
/*
  * @brief  rfid emulate
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_emulate_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE_STOP);
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_saved_emulate_create(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
	m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE);

    m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_emulate_destroy(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_emulate_update(uint8_t param)
{
	char szString[32];
	const char* protocol = protocol_get_name(lfrfid_tag_info.protocol);
	strcpy(szString, protocol);

	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 2, 8, 48, 48, nfc_emit_48x48);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	m1_draw_text(&m1_u8g2, 60, 20, 50, res_string(IDS_EMULATING), TEXT_ALIGN_CENTER);

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	fu_get_filename_without_ext(lfrfid_tag_info.filename, szString, sizeof(szString));
	m1_draw_text_box(&m1_u8g2, 60,30, 60, 10, szString, TEXT_ALIGN_LEFT);

	m1_u8g2_nextpage(); // Update display RAM
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_emulate_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_emulate_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_emulate_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_EMULATE, lfrfid_saved_emulate_create, lfrfid_saved_emulate_update, lfrfid_saved_emulate_destroy, lfrfid_saved_emulate_message);
}

/*============================================================================*/
/*
  * @brief  rfid write
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_write_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE_STOP);
		memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));

		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_saved_write_create(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

	memcpy(lfrfid_tag_info_back, &lfrfid_tag_info, sizeof(LFRFID_TAG_INFO));
	lfrfid_write_count = 0;
	m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);

	m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_write_destroy(uint8_t param)
{
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_write_update(uint8_t param)
{
	lfrfid_write_screen_draw(param, lfrfid_tag_info.filename);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_write_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_write_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if( q_item.q_evt_type==Q_EVENT_UI_LFRFID_WRITE_DONE)
		{
			record_stat = RFID_READ_READING;
			m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_START_READ);
		}
		else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
		{
			// Do other things for this task
			if(lfrfid_write_count > LFRFID_WRITE_ERROR_COUNT)
			{
				m1_uiView_display_update(2);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				return 1;
			}

			if(lfrfid_write_verify(lfrfid_tag_info_back, &lfrfid_tag_info))
			{
				m1_uiView_display_update(1);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);

			}
			else
			{
				memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));
				m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);
			}

		} // else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
		else if ( q_item.q_evt_type==Q_EVENT_UI_LFRFID_READ_TIMEOUT )
		{
			if(lfrfid_write_count > LFRFID_WRITE_ERROR_COUNT)
			{
				m1_uiView_display_update(2);

				record_stat = RFID_READ_DONE;
				m1_buzzer_notification();
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				return 1;
			}

			memcpy(&lfrfid_tag_info, lfrfid_tag_info_back, sizeof(LFRFID_TAG_INFO));
			m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE);
			m1_uiView_display_update(0);
		} // else if ( q_item.q_evt_type==Q_EVENT_UI_LFRFID_READ_TIMEOUT )
		else if(q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			//rfid_125khz_saved();
			m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
		
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_write_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_WRITE, lfrfid_saved_write_create, lfrfid_saved_write_update, lfrfid_saved_write_destroy, lfrfid_saved_write_message);
}

/*============================================================================*/
/*
  * @brief  rfid save-edit
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_edit_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_saved_edit_create(uint8_t param)
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
static void lfrfid_saved_edit_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_edit_update(uint8_t param)
{
	char data_buffer[64];
	uint8_t data_size;
	uint8_t val;
	const uint8_t *pBitmap;

	data_size = protocol_get_data_size(lfrfid_tag_info.protocol);

	memset(data_buffer,0,sizeof(data_buffer));

	m1_byte_to_hextext(lfrfid_tag_info.uid, data_size, data_buffer);

	// escape : val=0,
    val = m1_vkbs_get_data((char*)res_string(IDS_ENTER_HEX_DATA), data_buffer);

    if(val)
    {
		memset(lfrfid_tag_info.uid,0, sizeof(lfrfid_tag_info.uid));
		m1_strtob_with_base(data_buffer, lfrfid_tag_info.uid, sizeof(lfrfid_tag_info.uid), 16);

		fu_path_combine(data_buffer, sizeof(data_buffer),lfrfid_tag_info.filepath , lfrfid_tag_info.filename);

		pBitmap = micro_sd_card_error;
		if(lfrfid_profile_save(data_buffer, &lfrfid_tag_info))
			pBitmap = nfc_saved_63_63;

		m1_draw_icon(M1_DISP_DRAW_COLOR_TXT, 32, 0, 63, 63, pBitmap);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		//vTaskDelay(2000);
		//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
    }
    else // escape
    	m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_edit_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_edit_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
			m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_edit_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_EDIT, lfrfid_saved_edit_create, lfrfid_saved_edit_update, lfrfid_saved_edit_destroy, lfrfid_saved_edit_message);
}

/*============================================================================*/
/*
  * @brief  rfid save
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_rename_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_saved_rename_create(uint8_t param)
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
static void lfrfid_saved_rename_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_rename_update(uint8_t param)
{
	char new_file[64];
	char old_file[64];
	BaseType_t ret;
	const uint8_t *pBitmap;

	ret = lfrfid_save_file_keyboard(new_file);
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
	}
	else
	{
		pBitmap = micro_sd_card_error;

		if (ret==0)
		{
			fu_path_combine(old_file, sizeof(old_file), lfrfid_tag_info.filepath, lfrfid_tag_info.filename);
			if(f_rename(old_file,new_file)==FR_OK)
			{
				pBitmap = nfc_saved_63_63;
				fu_get_directory_path(new_file, lfrfid_tag_info.filepath, sizeof(lfrfid_tag_info.filepath));
				const char *pbuff = fu_get_filename(new_file);
				strncpy(lfrfid_tag_info.filename, pbuff, sizeof(lfrfid_tag_info.filename));
			}
		}

		m1_draw_icon(M1_DISP_DRAW_COLOR_TXT, 32, 0, 63, 63, pBitmap);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		//vTaskDelay(2000);
		//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
	}
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_rename_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_rename_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
			m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_rename_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_RENAME, lfrfid_saved_rename_create, lfrfid_saved_rename_update, lfrfid_saved_rename_destroy, lfrfid_saved_rename_message);
}

/*============================================================================*/
/*
  * @brief  rfid save-delete
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_delete_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
	{
		// Do other things for this task, if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
	}
	else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
	{
		// Do other things for this task, if needed
		char file_path[64];
		fu_path_combine(file_path, sizeof(file_path), lfrfid_tag_info.filepath, lfrfid_tag_info.filename);
		m1_fb_delete_file(file_path);

		m1_uiView_display_update(1);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		//vTaskDelay(2000);
		//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_delete_create(uint8_t param)
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
static void lfrfid_saved_delete_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_delete_update(uint8_t param)
{
	char *hex;
	char szString[64];

	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

	if(param==0)
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);

		//uint16_t w = u8g2_GetStrWidth(&m1_u8g2,"Delete");
		//u8g2_DrawStr(&m1_u8g2, (128-w)/2, 10, "Delete");
		m1_draw_text(&m1_u8g2, 0,10,128,res_string(IDS_DELETE), TEXT_ALIGN_CENTER);

		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		strcpy(szString,res_string(IDS_NAME_));
		fu_get_filename_without_ext(lfrfid_tag_info.filename,&szString[strlen(szString)], sizeof(szString)-strlen(szString));
		m1_draw_text(&m1_u8g2, 2, 22, 120, szString, TEXT_ALIGN_LEFT);

		//u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		lfrfid_protocol_make_menu_list(lfrfid_tag_info.protocol, szString);
		m1_draw_text(&m1_u8g2, 2, 32, 120, szString, TEXT_ALIGN_LEFT);

		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		protocol_render_data(lfrfid_tag_info.protocol,szString);
		hex = strtok(szString,"\n");
		m1_draw_text(&m1_u8g2, 2, 42, 120, hex, TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8,res_string(IDS_CANCEL), res_string(IDS_DELETE), arrowright_8x8);

		m1_u8g2_nextpage(); // Update display RAM
	}
	else if(param==1)
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 0,10,128,res_string(IDS_DELETE), TEXT_ALIGN_CENTER);

		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		m1_draw_text_box(&m1_u8g2, 10,22,108,10,res_string(IDS_DELETE_SUCCESS), TEXT_ALIGN_CENTER);

		m1_u8g2_nextpage(); // Update display RAM
	}
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_delete_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_delete_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
			m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_delete_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_DELETE, lfrfid_saved_delete_create, lfrfid_saved_delete_update, lfrfid_saved_delete_destroy, lfrfid_saved_delete_message);
}

/*============================================================================*/
/*
  * @brief  rfid save-info
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_saved_info_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_saved_info_create(uint8_t param)
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
static void lfrfid_saved_info_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_saved_info_update(uint8_t param)
{
	char *hex;
	char *fc;
	char *card_num;

	char szString[64];

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

	strcpy(szString,res_string(IDS_NAME_));
	fu_get_filename_without_ext(lfrfid_tag_info.filename,&szString[strlen(szString)], sizeof(szString)-strlen(szString));
	m1_draw_text(&m1_u8g2, 2, 10, 124, szString, TEXT_ALIGN_LEFT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	lfrfid_protocol_make_menu_list(lfrfid_tag_info.protocol, szString);
	m1_draw_text(&m1_u8g2, 2, 22,124,szString, TEXT_ALIGN_LEFT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	protocol_render_data(lfrfid_tag_info.protocol,szString);
    hex = strtok(szString,"\n");
    fc = strtok(NULL,"\n");
    card_num = strtok(NULL,"\n");

	m1_draw_text(&m1_u8g2, 2, 32,124,hex, TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 2, 42,124,fc, TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 2, 52,124,card_num, TEXT_ALIGN_LEFT);

	m1_u8g2_nextpage(); // Update display RAM
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_saved_info_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_saved_info_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_saved_info_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_SAVED_INFO, lfrfid_saved_info_create, lfrfid_saved_info_update, lfrfid_saved_info_destroy, lfrfid_saved_info_message);
}



/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void rfid_125khz_add_manually(void)
{
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// init
	m1_uiView_functions_init(VIEW_MODE_LFRFID_ADDM_END, view_lfrfid_add_manually_table);
	m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_RESET);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}

} // void rfid_125khz_add_manually(void)

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_addm_submenu_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	{
		// Do other things for this task, if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_EDIT, 0);
	}
	else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_UP);
	}
	else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	{
		m1_uiView_display_update(X_MENU_UPDATE_MOVE_DOWN);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_submenu_create(uint8_t param)
{
	for(int i=LFRFIDProtocolEM4100; i<LFRFIDProtocolMax; i++)
	{
		lfrfid_protocol_make_menu_list(i, lfrfid_protocol_menu_list[i]);
		lfrfid_protocol_menu_items[i] = lfrfid_protocol_menu_list[i];
	}
	m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_submenu_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_submenu_update(uint8_t param)
{
	m1_gui_submenu_update((const char **)lfrfid_protocol_menu_items, RFID_READ_ADD_MANUALLY_MORE_OPTIONS, 0, param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_addm_submenu_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_addm_submenu_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_addm_submenu_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_ADDM_SUBMENU, lfrfid_addm_submenu_create, lfrfid_addm_submenu_update, lfrfid_addm_submenu_destroy, lfrfid_addm_submenu_message);
}

/*============================================================================*/
/*
  * @brief  rfid lfrfid_addm_edit
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_addm_edit_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_REFRESH);
		//break; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
	else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	{
		// Do other things for this task, if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SAVE, 0);
	}

	return 1;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_edit_create(uint8_t param)
{
	lfrfid_tag_info.protocol = param;
    m1_uiView_display_update(param);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_edit_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_edit_update(uint8_t param)
{
	char data_buffer[20];
	uint8_t data_size;
	uint8_t val, menu_index;

	menu_index = m1_gui_submenu_update(NULL, 0, 0, MENU_UPDATE_NONE); // Get menu index
	data_size = protocol_get_data_size(menu_index);

	memset(data_buffer,0,sizeof(data_buffer));

	if(param==0xFF) // addm_save -> edit
	{
		m1_byte_to_hextext(lfrfid_tag_info.uid, data_size, data_buffer);
	}
	else // submenu list -> edit
	{
		m1_vkb_set_initial_text(data_size, data_buffer);
		lfrfid_tag_info.protocol = menu_index;
	}
	// escape : val=0,
    val = m1_vkbs_get_data((char*)res_string(IDS_ENTER_HEX_DATA), data_buffer);

    if(val)
    {
		memset(lfrfid_tag_info.uid,0, sizeof(lfrfid_tag_info.uid));
		m1_strtob_with_base(data_buffer, lfrfid_tag_info.uid, sizeof(lfrfid_tag_info.uid), 16);

		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SAVE, 0);
    }
    else // escape
    	m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_REFRESH);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_addm_edit_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_addm_edit_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_addm_edit_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_ADDM_EDIT, lfrfid_addm_edit_create, lfrfid_addm_edit_update, lfrfid_addm_edit_destroy, lfrfid_addm_edit_message);
}

/*============================================================================*/
/*
  * @brief  rfid save
  * @param  None
  * @retval None
 */
/*============================================================================*/
static int lfrfid_addm_save_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		// Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_REFRESH);
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
static void lfrfid_addm_save_create(uint8_t param)
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
static void lfrfid_addm_save_destroy(uint8_t param)
{

}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static void lfrfid_addm_save_update(uint8_t param)
{
	char data_buffer[64];
	BaseType_t ret;
	const uint8_t *pBitmap;

	ret = lfrfid_save_file_keyboard(data_buffer);
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_EDIT, 0xFF);
	}
	else
	{
		pBitmap = micro_sd_card_error;

		if (ret==0)
		{
			if(lfrfid_profile_save(data_buffer, &lfrfid_tag_info))
				pBitmap = nfc_saved_63_63;
		}

		m1_draw_icon(M1_DISP_DRAW_COLOR_TXT, 32, 0, 63, 63, pBitmap);

		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		//vTaskDelay(2000);
		//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
	}
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
static int lfrfid_addm_save_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = lfrfid_addm_save_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if(q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT)
		{
			//m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);
			m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_REFRESH);
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void lfrfid_addm_save_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_LFRFID_ADDM_SAVE, lfrfid_addm_save_create, lfrfid_addm_save_update, lfrfid_addm_save_destroy, lfrfid_addm_save_message);
}

/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void rfid_125khz_utilities(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	/*
	* Display a list of known protocols to emulate ==> SW team
	*/
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

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
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void rfid_125khz_utilities(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void lfrfid_write_screen_draw(int param, char* filename)
{
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 2, 8, 48, 48, nfc_emit_48x48);

    if(param==0)	// writing
    {
    	char szString[24];
    	const char* protocol = protocol_get_name(lfrfid_tag_info.protocol);
    	strcpy(szString, protocol);

    	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
    	m1_draw_text(&m1_u8g2, 60, 20, 60, res_string(IDS_WRITING), TEXT_ALIGN_LEFT);

    	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
       	m1_draw_text(&m1_u8g2, 60, 30, 60, szString, TEXT_ALIGN_LEFT);

    	if(filename)
    	{
    		fu_get_filename_without_ext(filename, szString, sizeof(szString));
    		m1_draw_text_box(&m1_u8g2, 60,40, 60, 10, szString, TEXT_ALIGN_LEFT);
    	}
    	else
    		m1_draw_text(&m1_u8g2, 60, 40, 60, res_string(IDS_UNSAVED), TEXT_ALIGN_LEFT);


    	m1_u8g2_nextpage(); // Update display RAM
    }
    else if(param==1)	// Writing Complete
    {
    	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
    	m1_draw_text(&m1_u8g2, 60, 20, 60, res_string(IDS_WRITING), TEXT_ALIGN_LEFT);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		m1_draw_text(&m1_u8g2, 60, 30, 60, res_string(IDS_SUCCESS), TEXT_ALIGN_LEFT);

       	m1_u8g2_nextpage(); // Update display RAM

       	uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
    }
    else if(param==2)	// Writing error
    {
    	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
    	m1_draw_text(&m1_u8g2, 60, 20, 60, res_string(IDS_WRITING), TEXT_ALIGN_LEFT);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		m1_draw_text(&m1_u8g2, 60, 30, 60, res_string(IDS_ERROR), TEXT_ALIGN_LEFT);

       	m1_u8g2_nextpage(); // Update display RAM
    }

} // static void rfid_read_more_options_write(void)


