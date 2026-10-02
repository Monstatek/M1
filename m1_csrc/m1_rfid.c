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
#include "m1_sdcard_provision.h"
#include "m1_rfid.h"
#include "uiView.h"
#include "m1_virtual_kb.h"
#include "m1_storage.h"
#include "m1_ring_buffer.h"
#include "m1_sdcard_man.h"
#include "res_string.h"
#include "m1_read_icon.h"
#include "lfrfid.h"
#include "lfrfid_file.h"
#include "privateprofilestring.h"
#include "m1_file_util.h"
#include "m1_feedback_orchestration.h"

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

/* Utilities -> Pet Tag. Pet Tag reuses the EXACT normal Read views (ids 1..5)
 * for acquisition/result/actions; it adds only a Utilities submenu at id 6.
 * The 134.2 kHz carrier and FDX-B-only decode are engaged by lfrfid_pettag_mode
 * (set here, cleared on exit); normal Read stays 125 kHz / all-protocols. */
enum {
	VIEW_MODE_LFRFID_UTIL_MENU = VIEW_MODE_LFRFID_READ_END,  /* = 6 */
	VIEW_MODE_LFRFID_UTIL_END                                /* = 7 (table size) */
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

/* Protocol-owned launch context for an exact file selected by Home -> LEFT.
 * The synchronous saved-view loop keeps this storage valid until it returns. */
static struct {
	bool external;
	bool pending;
	char dir[ESP_FILE_PATH_LEN_MAX + 1U];
	char name[ESP_FILE_NAME_LEN_MAX + 1U];
	S_M1_file_info file;
} s_rfid_saved_launch;

static bool s_lfrfid_read_emu_feedback_acquired = false;
static bool s_lfrfid_saved_emu_feedback_acquired = false;

static char lfrfid_protocol_menu_list[LFRFIDProtocolMax][24];
static char *lfrfid_protocol_menu_items[LFRFIDProtocolMax];
static uint8_t lfrfid_uiview_gui_latest_param;
S_M1_RFID_Record_t record_stat;

static const char *lfrfid_util_menu_options[] = {
	"FDX-B (134.2 kHz)"
};
#define RFID_UTILITIES_OPTIONS  (1)

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void rfid_125khz_read(void);
void rfid_125khz_saved(void);
void rfid_125khz_add_manually(void);
void rfid_125khz_utilities(void);
// rfid_125khz_utilities -> Pet Tag (reuses the read views; Utilities submenu only)
static void lfrfid_util_menu_init(void);
static void lfrfid_util_menu_create(uint8_t param);
static void lfrfid_util_menu_destroy(uint8_t param);
static void lfrfid_util_menu_update(uint8_t param);
static int  lfrfid_util_menu_message(void);
static int  lfrfid_util_menu_kp_handler(void);

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
static bool lfrfid_addm_parse_exact_hex(const char *str, uint8_t *out, int expected); /* defined below, near the Add-Manual edit flow; also used by lfrfid_saved_edit_update() */
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
	/* Invalid protocol must leave a deterministic empty title. */
	szString[0] = '\0';
	const char* protocol = protocol_get_name(protocol_idx);
	const char* manufacturer = protocol_get_manufacturer(protocol_idx);

	/* When manufacturer and protocol name are identical,
	 * (e.g. AWID/AWID) show the name once instead of "AWID AWID". */
	if(manufacturer && protocol && strcmp(manufacturer, protocol) != 0)
		sprintf(szString, "%s %s",manufacturer, protocol);
	else if(protocol)
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
	if (lfrfid_q_hdl != NULL)
		return;
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
	lfrfid_pettag_mode = 0;   // normal Read is always 125 kHz / all-protocols
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

		if(lfrfid_pettag_mode)
		{
			/* Pet Tag: STOP tears down the 134.2 kHz carrier + capture; clear the mode
			 * (normal Read returns to 125 kHz) and go back to the Utilities submenu.
			 * Use RESET (not REFRESH) so the FDX-B row is redrawn selected/highlighted,
			 * exactly as when entering the list. REFRESH restores the stale submenu-
			 * level scroll state left by the More submenu and draws the row unhighlighted. */
			lfrfid_pettag_mode = 0;
			m1_uiView_display_switch(VIEW_MODE_LFRFID_UTIL_MENU, X_MENU_UPDATE_RESET);
			return 1;
		}

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
		fb_net_read_start(); // migrated: LF-RFID read entry (RFID_READ_DISPLAY_PARAM_READING_READY)
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
	fb_net_read_stop(); // migrated: LF-RFID read exit
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
		if(lfrfid_pettag_mode)
		{
			// Pet Tag reading screen: identical to the RFID read screen
			// (tag + letter + signal waves + caption); 'P' marks Pet Tag.
			m1_read_icon_draw(&m1_u8g2, 'P', 3);
			u8g2_NextPage(&m1_u8g2); // Update display RAM
		}
		else
		{
			// Centered RFID read/waiting screen: tag + "R" + signal waves + caption.
			// Same tag/waves family as NFC; the letter (R vs N) distinguishes them.
			m1_read_icon_draw(&m1_u8g2, 'R', 3);
			u8g2_NextPage(&m1_u8g2); // Update display RAM
		}
    }
    else if( param==RFID_READ_DISPLAY_PARAM_READING_COMPLETE )	// read done
    {
    	char *hex;
    	char *fc;
    	char *card_num;

		char title[64] = {0};
		char szString[64] = {0};
		const uint16_t protocol = lfrfid_tag_info.protocol;

		/* A completion notification is not itself proof of a usable result.
		 * Validate before dispatch, and prepare all text before drawing. */
		if(protocol < LFRFIDProtocolMax && protocol_get_name(protocol) &&
		   protocol_get_data_size(protocol))
		{
			lfrfid_protocol_make_menu_list(protocol, title);
			protocol_render_data(protocol, szString);
		}
		if(!title[0] || !szString[0] || szString[0] == '\n' || protocol != lfrfid_tag_info.protocol)
		{
			lfrfid_read_create(RFID_READ_DISPLAY_PARAM_READING_READY);
			return;
		}

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 2, 12,124,title, TEXT_ALIGN_LEFT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        hex = strtok(szString,"\n");
        fc = strtok(NULL,"\n");
        card_num = strtok(NULL,"\n");

		m1_draw_text(&m1_u8g2, 2, 22,124,hex, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 2, 32,124,fc, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 2, 42,124,card_num, TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, res_string(IDS_RETRY),res_string(IDS_MORE),arrowright_8x8 );

		m1_u8g2_nextpage(); // Update display RAM
		record_stat = RFID_READ_DONE;
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

			/* Invalid results restart reading in the display handler. */
			if(record_stat == RFID_READ_DONE)
			{
				fb_net_read_stop(); // LF-RFID valid read complete
				fb_alert_success(FB_OWNER_RADIO);
			}

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
	uint8_t error;

	ret = lfrfid_save_file_keyboard(data_buffer, sizeof(data_buffer));
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ_SUBMENU, X_MENU_UPDATE_REFRESH);
	}
	else
	{
		error = 1;
		if (ret==0)
		{
			if(lfrfid_profile_save(data_buffer, &lfrfid_tag_info))
				error = 0;
		}
		if ( error )
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		else
			m1_draw_file_saved_screen();
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
static uint8_t lfrfid_emu_frame; // Emulate-screen RF-wave animation frame (shared by both RFID emulate views)

static void lfrfid_read_emulate_create(uint8_t param)
{
	s_lfrfid_read_emu_feedback_acquired = false;
	if (m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE) == pdTRUE)
	{
		fb_net_emulating_start();
		s_lfrfid_read_emu_feedback_acquired = true;
		lfrfid_emu_frame = 0; // Start the wave animation from the first frame
	}
	else
	{
		lfrfid_state = LFRFID_STATE_ERROR;
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
static void lfrfid_read_emulate_destroy(uint8_t param)
{
	if (s_lfrfid_read_emu_feedback_acquired)
	{
		(void)m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE_STOP);
		fb_net_emulating_stop();
		s_lfrfid_read_emu_feedback_acquired = false;
	}
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
	if (lfrfid_state == LFRFID_STATE_ERROR)
	{
		m1_u8g2_firstpage();
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 2, 28, "Emulation failed");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "BACK to return");
		m1_u8g2_nextpage();
		return;
	}
	// Shared centered emulate screen: tag + animated RF waves + EMULATING...
	m1_read_icon_draw_emulate(&m1_u8g2, lfrfid_emu_frame);
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

	// Short timeout so the RF-wave animation advances (~M1_READ_ICON_EMU_MS).
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(M1_READ_ICON_EMU_MS));
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

			fb_net_emulating_stop(); // migrated: LF-RFID (read) emulate stop (Q_EVENT_LFRFID_TAG_DETECTED)
			fb_alert_success(FB_OWNER_RADIO);
			//m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_STOP);
		}
		else
		{
			; // Do other things for this task
		}
	} // if (ret==pdTRUE)
	else
	{
		lfrfid_emu_frame++;           // advance the RF-wave animation
		m1_uiView_display_update(0);  // redraw the current frame
	}

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
	fb_net_write_start();
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
	fb_net_write_stop();
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
				fb_net_write_stop();
				return 1;
			}

			if(lfrfid_write_verify(lfrfid_tag_info_back, &lfrfid_tag_info))
			{
				m1_uiView_display_update(1);

				record_stat = RFID_READ_DONE;
				fb_net_write_stop(); // migrated PR2: LF-RFID write exit
				fb_alert_success(FB_OWNER_RADIO);

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
				fb_net_write_stop();
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
void rfid_125khz_saved_launch(const char *dir_name, const char *file_name)
{
	bool owns_lifecycle = false;
	bool was_initialized;

	memset(&s_rfid_saved_launch, 0, sizeof(s_rfid_saved_launch));
	if ((dir_name == NULL) != (file_name == NULL))
	{
		M1_LOG_E(M1_LOGDB_TAG, "saved launch rejected incomplete path\r\n");
		return;
	}
	if (dir_name != NULL)
	{
		if (strlen(dir_name) > ESP_FILE_PATH_LEN_MAX ||
		    strlen(file_name) > ESP_FILE_NAME_LEN_MAX)
		{
			M1_LOG_E(M1_LOGDB_TAG, "saved launch rejected overlong path\r\n");
			return;
		}
		memcpy(s_rfid_saved_launch.dir, dir_name, strlen(dir_name) + 1U);
		memcpy(s_rfid_saved_launch.name, file_name, strlen(file_name) + 1U);
		s_rfid_saved_launch.file.dir_name = s_rfid_saved_launch.dir;
		s_rfid_saved_launch.file.file_name = s_rfid_saved_launch.name;
		s_rfid_saved_launch.file.file_is_selected = true;
		s_rfid_saved_launch.file.status = FB_OK;
		s_rfid_saved_launch.external = true;
		s_rfid_saved_launch.pending = true;

		was_initialized = (lfrfid_q_hdl != NULL);
		menu_125khz_rfid_init();
		owns_lifecycle = !was_initialized;
		if (lfrfid_q_hdl == NULL)
		{
			M1_LOG_E(M1_LOGDB_TAG, "saved launch failed: worker lifecycle unavailable\r\n");
			m1_message_box(&m1_u8g2, "RFID unavailable", "", NULL, res_string(IDS_BACK));
			if (owns_lifecycle)
				menu_125khz_rfid_deinit();
			memset(&s_rfid_saved_launch, 0, sizeof(s_rfid_saved_launch));
			return;
		}
	}

	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// init
	m1_uiView_functions_init(VIEW_MODE_LFRFID_SAVED_END, view_lfrfid_saved_table);
	/* Open the browser directly inside the RFID folder (one-shot start dir;
	 * other file-browser users unaffected) -- mirrors NFC Saved. */
	if (!m1_fb_check_existence(M1_SD_DIR_RFID)) m1_fb_make_dir(M1_SD_DIR_RFID);
	m1_fb_set_start_dir(M1_SD_DIR_RFID);
	m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_BROWSE, 0);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}

	memset(&s_rfid_saved_launch, 0, sizeof(s_rfid_saved_launch));
	if (owns_lifecycle)
		menu_125khz_rfid_deinit();

} // void rfid_125khz_saved_launch(const char *, const char *)

void rfid_125khz_saved(void)
{
	rfid_125khz_saved_launch(NULL, NULL);
}


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
	if (s_rfid_saved_launch.external && !s_rfid_saved_launch.pending)
	{
		(void)m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
		return;
	}
    if ( lfrfid_uiview_gui_latest_param==X_MENU_UPDATE_RESET )
    {
    	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_RESTORE);
    }
	lfrfid_uiview_gui_latest_param = param; // Update new param

	do
	{
		if (s_rfid_saved_launch.pending)
		{
			s_rfid_saved_launch.pending = false;
			f_info = &s_rfid_saved_launch.file;
		}
		else
		{
			f_info = storage_browse();
		}
		if (f_info == NULL)
		{
			(void)m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
			break;
		}
		if ( f_info->file_is_selected )
		{
			if (f_info->dir_name != NULL && f_info->file_name != NULL &&
			    strlen(f_info->dir_name) < sizeof(lfrfid_tag_info.filepath) &&
			    strlen(f_info->file_name) < sizeof(lfrfid_tag_info.filename) &&
			    lfrfid_profile_load(f_info, RFID_FILE_EXTENSION_TMP))
			{
				/* Commit file identity only after a successful load, so a
				 * rejected file never alters the current credential state. */
				memcpy(lfrfid_tag_info.filepath, f_info->dir_name, strlen(f_info->dir_name) + 1U);
				memcpy(lfrfid_tag_info.filename, f_info->file_name, strlen(f_info->file_name) + 1U);

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
		m1_fb_set_start_dir(M1_SD_DIR_RFID);
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
	s_lfrfid_saved_emu_feedback_acquired = false;
	if (m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE) == pdTRUE)
	{
		fb_net_emulating_start();
		s_lfrfid_saved_emu_feedback_acquired = true;
		lfrfid_emu_frame = 0; // Start the wave animation from the first frame
	}
	else
	{
		lfrfid_state = LFRFID_STATE_ERROR;
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
static void lfrfid_saved_emulate_destroy(uint8_t param)
{
	if (s_lfrfid_saved_emu_feedback_acquired)
	{
		(void)m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_EMULATE_STOP);
		fb_net_emulating_stop();
		s_lfrfid_saved_emu_feedback_acquired = false;
	}
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
	if (lfrfid_state == LFRFID_STATE_ERROR)
	{
		lfrfid_read_emulate_update(param);
		return;
	}
	// Shared centered emulate screen: tag + animated RF waves + EMULATING...
	m1_read_icon_draw_emulate(&m1_u8g2, lfrfid_emu_frame);
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

	// Short timeout so the RF-wave animation advances (~M1_READ_ICON_EMU_MS).
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(M1_READ_ICON_EMU_MS));
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
	else
	{
		lfrfid_emu_frame++;           // advance the RF-wave animation
		m1_uiView_display_update(0);  // redraw the current frame
	}

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
	fb_net_write_start();

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
	fb_net_write_stop();
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
				fb_net_write_stop();
				return 1;
			}

			if(lfrfid_write_verify(lfrfid_tag_info_back, &lfrfid_tag_info))
			{
				m1_uiView_display_update(1);

				record_stat = RFID_READ_DONE;
				fb_net_write_stop(); // migrated PR2: LF-RFID write exit
				fb_alert_success(FB_OWNER_RADIO);

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
				fb_net_write_stop();
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
	uint8_t val, error;

	data_size = protocol_get_data_size(lfrfid_tag_info.protocol);

	memset(data_buffer,0,sizeof(data_buffer));

	m1_byte_to_hextext(lfrfid_tag_info.uid, data_size, data_buffer);

	// escape : val=0,
    val = m1_vkbs_get_data((char*)res_string(IDS_ENTER_HEX_DATA), data_buffer);

    if(val)
    {
		uint8_t parsed[sizeof(lfrfid_tag_info.uid)];

		/* Require exactly data_size valid hex bytes for this protocol,
		 * mirroring the Add-Manual path's own validation
		 * (lfrfid_addm_parse_exact_hex, below) -- reject invalid/missing/
		 * extra input and never store or save a partial credential. This
		 * previously used m1_strtob_with_base() and ignored its returned
		 * parse count entirely, silently overwriting the file's existing,
		 * previously-valid UID with whatever it managed to parse
		 * (including nothing at all, from an empty/all-invalid entry). */
		if(data_size == 0 || data_size > sizeof(lfrfid_tag_info.uid) ||
		   !lfrfid_addm_parse_exact_hex(data_buffer, parsed, data_size))
		{
			m1_message_box(&m1_u8g2, res_string(IDS_ERROR)," ",NULL, res_string(IDS_BACK));
			m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
			return;
		}

		memset(lfrfid_tag_info.uid,0, sizeof(lfrfid_tag_info.uid));
		memcpy(lfrfid_tag_info.uid, parsed, data_size);

		fu_path_combine(data_buffer, sizeof(data_buffer),lfrfid_tag_info.filepath , lfrfid_tag_info.filename);

		error = 1;
		if( lfrfid_profile_save(data_buffer, &lfrfid_tag_info) )
			error = 0;

		if ( error )
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		else
			m1_draw_file_saved_screen();
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
	uint8_t error;

	ret = lfrfid_save_file_keyboard(new_file, sizeof(new_file));
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_SAVED_SUBMENU, X_MENU_UPDATE_REFRESH);
	}
	else
	{
		error = 1;
		if (ret==0)
		{
			fu_path_combine(old_file, sizeof(old_file), lfrfid_tag_info.filepath, lfrfid_tag_info.filename);
			if(f_rename(old_file,new_file)==FR_OK)
			{
				error = 0;
				fu_get_directory_path(new_file, lfrfid_tag_info.filepath, sizeof(lfrfid_tag_info.filepath));
				const char *pbuff = fu_get_filename(new_file);
				strncpy(lfrfid_tag_info.filename, pbuff, sizeof(lfrfid_tag_info.filename));
			}
		}

		if ( error )
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		else
			m1_draw_file_saved_screen();
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

		/* Show success only when the file is actually removed; on failure the
		 * saved file is preserved and a failure result is shown instead. */
		if(m1_fb_delete_file(file_path) == FR_OK)
			m1_uiView_display_update(1);
		else
			m1_uiView_display_update(2);
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
	char szString[64] = {0};

	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

	if(param==0)
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);

		//uint16_t w = u8g2_GetStrWidth(&m1_u8g2,"Delete");
		//u8g2_DrawStr(&m1_u8g2, (128-w)/2, 10, "Delete");
		m1_draw_text(&m1_u8g2, 0,10,128,res_string(IDS_DELETE), TEXT_ALIGN_CENTER);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

		strcpy(szString,res_string(IDS_NAME_));
		fu_get_filename_without_ext(lfrfid_tag_info.filename,&szString[strlen(szString)], sizeof(szString)-strlen(szString));
		m1_draw_text(&m1_u8g2, 2, 22, 120, szString, TEXT_ALIGN_LEFT);

		//u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
		lfrfid_protocol_make_menu_list(lfrfid_tag_info.protocol, szString);
		m1_draw_text(&m1_u8g2, 2, 32, 120, szString, TEXT_ALIGN_LEFT);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		protocol_render_data(lfrfid_tag_info.protocol,szString);
		hex = strtok(szString,"\n");
		m1_draw_text(&m1_u8g2, 2, 42, 120, hex, TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8,res_string(IDS_CANCEL), res_string(IDS_DELETE), arrowright_8x8);

		m1_u8g2_nextpage(); // Update display RAM
	}
	else if(param==1)
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 0,10,128,res_string(IDS_DELETE), TEXT_ALIGN_CENTER);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		m1_draw_text_box(&m1_u8g2, 10,22,108,10,res_string(IDS_DELETE_SUCCESS), TEXT_ALIGN_CENTER);

		m1_u8g2_nextpage(); // Update display RAM
	}
	else if(param==2)	// delete failed: file preserved
	{
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 0,10,128,res_string(IDS_DELETE), TEXT_ALIGN_CENTER);

		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		m1_draw_text_box(&m1_u8g2, 10,22,108,10,res_string(IDS_ERROR), TEXT_ALIGN_CENTER);

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

	char szString[64] = {0};

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

	strcpy(szString,res_string(IDS_NAME_));
	fu_get_filename_without_ext(lfrfid_tag_info.filename,&szString[strlen(szString)], sizeof(szString)-strlen(szString));
	m1_draw_text(&m1_u8g2, 2, 10, 124, szString, TEXT_ALIGN_LEFT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
	lfrfid_protocol_make_menu_list(lfrfid_tag_info.protocol, szString);
	m1_draw_text(&m1_u8g2, 2, 22,124,szString, TEXT_ALIGN_LEFT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
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
/* Parse a space-separated hex string into `out`, requiring EXACTLY `expected`
 * bytes, each a valid 1-2 digit hex value. Returns true only on a fully valid,
 * exact-length match; rejects missing, extra, or non-hex input. `out` may be
 * partially written on a rejected input, so callers must ignore it unless this
 * returns true. */
static bool lfrfid_addm_parse_exact_hex(const char *str, uint8_t *out, int expected)
{
	int count = 0;

	if(str == NULL || out == NULL || expected <= 0)
		return false;

	while(*str)
	{
		while(*str == ' ') str++;			/* skip separators */
		if(*str == '\0') break;

		int ndig = 0;
		unsigned int byte = 0;
		while(*str && *str != ' ')
		{
			char c = *str++;
			int d;
			if(c >= '0' && c <= '9') d = c - '0';
			else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false;				/* invalid character */
			byte = (byte << 4) | (unsigned)d;
			ndig++;
		}
		if(ndig < 1 || ndig > 2)			/* each byte is 1-2 hex digits */
			return false;
		if(count >= expected)				/* extra token */
			return false;
		out[count++] = (uint8_t)byte;
	}

	return (count == expected);				/* reject if fewer than expected */
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
	char data_buffer[64];	/* 12-byte spaced hex = 35 chars + NUL = 36; 64 for headroom */
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
		uint8_t parsed[sizeof(lfrfid_tag_info.uid)];

		/* Require exactly data_size valid hex bytes for the chosen protocol.
		 * Reject invalid/missing/extra input and never store a partial
		 * credential. */
		if(data_size == 0 || data_size > sizeof(lfrfid_tag_info.uid) ||
		   !lfrfid_addm_parse_exact_hex(data_buffer, parsed, data_size))
		{
			m1_message_box(&m1_u8g2, res_string(IDS_ERROR)," ",NULL, res_string(IDS_BACK));
			m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_SUBMENU, X_MENU_UPDATE_REFRESH);
			return;
		}

		memset(lfrfid_tag_info.uid,0, sizeof(lfrfid_tag_info.uid));
		memcpy(lfrfid_tag_info.uid, parsed, data_size);

		/* Derive bitrate from the selected protocol (same mapping as file
		 * load) so Emulate/Write use the correct EM4100 clock. */
		if(lfrfid_tag_info.protocol == LFRFIDProtocolEM4100)
			lfrfid_tag_info.bitrate = 64;
		else if(lfrfid_tag_info.protocol == LFRFIDProtocolEM4100_32)
			lfrfid_tag_info.bitrate = 32;
		else if(lfrfid_tag_info.protocol == LFRFIDProtocolEM4100_16)
			lfrfid_tag_info.bitrate = 16;

		/* Valid credential entered: open the standard read-result flow
		 * (credential details + Save/Emulate/Write on lfrfid_tag_info),
		 * which does NOT auto-save. Re-register the read view table so its
		 * views are available even if no read happened this session. */
		m1_uiView_functions_init(VIEW_MODE_LFRFID_READ_END, view_lfrfid_read_table);
		m1_uiView_display_switch(VIEW_MODE_LFRFID_READ, RFID_READ_DISPLAY_PARAM_READING_COMPLETE);
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
	uint8_t error;

	ret = lfrfid_save_file_keyboard(data_buffer, sizeof(data_buffer));
	if ( ret==3 ) // user escaped?
	{
		m1_uiView_display_switch(VIEW_MODE_LFRFID_ADDM_EDIT, 0xFF);
	}
	else
	{
		error = 1;

		if (ret==0)
		{
			if(lfrfid_profile_save(data_buffer, &lfrfid_tag_info))
				error = 0;
		}

		if ( error )
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		else
			m1_draw_file_saved_screen();

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
/*============================================================================*/
/*                         P E T   T A G  (Utilities)                         */
/*                                                                            */
/* Pet Tag reuses the EXACT normal Read views for acquisition/result/actions  */
/* (no second reader or FDX-B decoder). One view table registers the read     */
/* views at ids 1..5 plus the Utilities submenu at id 6. Selecting Pet Tag     */
/* engages lfrfid_pettag_mode (134.2 kHz carrier + FDX-B-only decode) and      */
/* enters the read view; read BACK clears the mode and returns here.           */
/*============================================================================*/
static const view_func_t view_lfrfid_utilities_table[] = {
	NULL,
	lfrfid_read_init,          /* 1 = VIEW_MODE_LFRFID_READ (reused)   */
	lfrfid_read_submenu_init,  /* 2 = READ_SUBMENU (Save/Emulate/Write) */
	lfrfid_read_save_init,     /* 3 = READ_SAVE                        */
	lfrfid_read_emulate_init,  /* 4 = READ_EMULATE                     */
	lfrfid_read_write_init,    /* 5 = READ_WRITE                       */
	lfrfid_util_menu_init,     /* 6 = VIEW_MODE_LFRFID_UTIL_MENU       */
};

/*============================================================================*/
static int lfrfid_util_menu_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	uint8_t menu_index;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // exit Utilities
	{
		lfrfid_pettag_mode = 0;
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl);
		return 0;
	}
	else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	{
		menu_index = m1_gui_submenu_update(NULL, 0, 0, MENU_UPDATE_NONE);
		if ( menu_index==0 )	// Pet Tag -> 134.2 kHz FDX-B read (reuse read views)
		{
			lfrfid_pettag_mode = 1;
			m1_uiView_display_switch(VIEW_MODE_LFRFID_READ, RFID_READ_DISPLAY_PARAM_READING_READY);
		}
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

static void lfrfid_util_menu_create(uint8_t param)
{
	m1_uiView_display_update(param);
}

static void lfrfid_util_menu_destroy(uint8_t param)
{
}

static void lfrfid_util_menu_update(uint8_t param)
{
	m1_gui_submenu_update(lfrfid_util_menu_options, RFID_UTILITIES_OPTIONS, 0, param);
}

static int lfrfid_util_menu_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			ret_val = lfrfid_util_menu_kp_handler();
	}
	return ret_val;
}

void lfrfid_util_menu_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_LFRFID_UTIL_MENU, lfrfid_util_menu_create, lfrfid_util_menu_update, lfrfid_util_menu_destroy, lfrfid_util_menu_message);
}

/*============================================================================*/
/*
  * @brief  RFID -> Utilities. Hosts Pet Tag (134.2 kHz FDX-B reader), which
  *         reuses the normal Read views gated by lfrfid_pettag_mode.
  * @param  None
  * @retval None
 */
/*============================================================================*/
void rfid_125khz_utilities(void)
{
	m1_gui_let_update_fw();

	lfrfid_pettag_mode = 0;
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	lfrfid_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter

	m1_uiView_functions_init(VIEW_MODE_LFRFID_UTIL_END, view_lfrfid_utilities_table);
	m1_uiView_display_switch(VIEW_MODE_LFRFID_UTIL_MENU, X_MENU_UPDATE_RESET);

	while( m1_uiView_q_message_process() )
	{
		;
	}

	lfrfid_pettag_mode = 0;

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
	(void)filename;

	if(param==0)		// writing
	{
		m1_draw_write_screen("WRITING...");
	}
	else if(param==1)	// Writing Complete (success) -- preserve existing timeout/return
	{
		m1_draw_write_screen("SUCCESS");
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
	}
	else if(param==2)	// Writing error (screen unchanged)
	{
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_DrawXBMP(&m1_u8g2, 2, 8, 48, 48, nfc_emit_48x48);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);
		m1_draw_text(&m1_u8g2, 60, 20, 60, res_string(IDS_WRITING), TEXT_ALIGN_LEFT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		m1_draw_text(&m1_u8g2, 60, 30, 60, res_string(IDS_ERROR), TEXT_ALIGN_LEFT);
		m1_u8g2_nextpage();
	}

} // static void rfid_read_more_options_write(void)


