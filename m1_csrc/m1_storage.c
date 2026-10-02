/* See COPYING.txt for license details. */

/*
*
*  m1_storage.c
*
*  M1 storage functions
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
#include "m1_storage.h"
#include "m1_nfc.h"
#include "m1_rfid.h"
#include "m1_bq25896.h"       /* bq_getVBUS_GD() - USB cable (VBUS) presence      */
#include "m1_usb_cdc_msc.h"   /* m1_usb_cdc_force_reconnect() - media-ready re-enum */
#include "m1_file_util.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Storage"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

#define BROWSE_GUI_DISP_LINE_LEN_MAX			(M1_LCD_DISPLAY_WIDTH/M1_SUB_MENU_SFONT_WIDTH - 3)

#define SDCARD_EXPLORE_FUNCTIONS_N				2

/* USB Drive (MSC) active-mode polling for host-eject and cable (VBUS) loss. */
#define USBMSC_POLL_MS					250u
/* Consecutive "no VBUS" polls (~250ms each) required to confirm a physical
 * cable removal, debouncing brief transients. ~3 x 250ms = ~750ms. */
#define USBMSC_VBUS_LOSS_DEBOUNCE		3u

//************************** S T R U C T U R E S *******************************

/***************************** C O N S T A N T S ******************************/
static const char *sdcard_fat_sys_defs[] = {
    "UNKNOWN",
    "FAT12",
    "FAT16",
    "FAT32",
    "FAT_EXT"
};

static const char *sdcard_explore_options[] = {
		"Delete",
		"Rename",
		"Run",
};

/***************************** V A R I A B L E S ******************************/

static char info_filename[ESP_FILE_NAME_LEN_MAX];
static char info_filepath[ESP_FILE_PATH_LEN_MAX];

static S_M1_file_info file_info = {
	.dir_name = info_filepath,
	.file_name = info_filename
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_setting_storage_init(void);
static void menu_setting_storage_exit(void);

void storage_about(void);
void storage_explore(void);
void storage_usbmsc(void);
void storage_format(void);
static void browse_gui_update(uint8_t sel_item, char *file_name);
static void browse_info_box_update(uint8_t box_y, char *new_info);
static uint8_t browse_refresh(S_M1_file_info **f_info);
static uint8_t browse_and_dispatch(S_M1_file_info **f_info);
S_M1_file_info *storage_browse(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_setting_storage_init(void)
{
	;
} // void menu_setting_storage_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void  menu_setting_storage_exit(void)
{
	; // Do extra tasks here if needed
	m1_fb_deinit();

	xQueueReset(main_q_hdl); // Reset main q before return
} // void  menu_setting_storage_exit(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void storage_about(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	uint8_t about_ok;
	S_M1_SDCard_Info *info;
	BaseType_t ret;
	char info_str[10];

	about_ok = m1_sdcard_get_status();
	if ( about_ok==SD_access_OK || about_ok==SD_access_NoFS )
		about_ok = true;
	else
		about_ok = false;
    if ( about_ok )
    {
    	info = m1_sdcard_get_info();
    	if ( info==NULL )
    		about_ok = false;
    }
    /* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
    if ( about_ok )
    {
    	; //
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 2, 10, "Label: ");
		u8g2_DrawStr(&m1_u8g2, 37, 10, info->vol_label);
		u8g2_DrawStr(&m1_u8g2, 2, 20, "Type: ");
		u8g2_DrawStr(&m1_u8g2, 32, 20, sdcard_fat_sys_defs[info->fs_type]);
		u8g2_DrawStr(&m1_u8g2, 2, 30, "Total: ");
		sprintf(info_str, "%uGB", info->total_cap_kb/(1024*1024));
		u8g2_DrawStr(&m1_u8g2, 37, 30, info_str);
		u8g2_DrawStr(&m1_u8g2, 2, 40, "Free: ");
		sprintf(info_str, "%uGB", info->free_cap_kb/(1024*1024));
		u8g2_DrawStr(&m1_u8g2, 32, 40, info_str);
    } // if ( about_ok )
    else
    {
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
    }
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

} // void storage_about(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void storage_explore(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	S_M1_file_info *f_info;
	BaseType_t ret;
	uint8_t uret, no_file, sel_active, sel_item, refresh;
	char *fullpath = NULL;
	char new_filename[ESP_FILE_NAME_LEN_MAX];

	sel_active = 0;
	refresh = 0;

	no_file = browse_and_dispatch(&f_info);
	if ( no_file )
	{
		xQueueReset(main_q_hdl); // Reset main q before return
		return; // Exit and return to the calling task (subfunc_handler_task)
	} // if ( no_file )

	sel_item = 0;
	browse_gui_update(sel_item, f_info->file_name);

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
					if ( no_file )
					{
						xQueueReset(main_q_hdl); // Reset main q before return
						break; // Exit and return to the calling task (subfunc_handler_task)
					}
					refresh = 1;
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // go up?
				{
					; // Do other things for this task, if needed
					sel_active = 0; // Reset
					sel_item--;
					if ( sel_item > SDCARD_EXPLORE_FUNCTIONS_N )
						sel_item = SDCARD_EXPLORE_FUNCTIONS_N - 1;
					browse_gui_update(sel_item, f_info->file_name);
				} // else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // go down?
				{
					; // Do other things for this task, if needed
					sel_active = 0; // Reset
					sel_item++;
					if ( sel_item >= SDCARD_EXPLORE_FUNCTIONS_N )
						sel_item = 0;
					browse_gui_update(sel_item, f_info->file_name);
				} // else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // go right?
				{
					; // Do other things for this task, if needed
					if (sel_item==0) // Delete?
					{
						if ( sel_active )
						{
							fullpath = malloc(ESP_FILE_PATH_LEN_MAX + ESP_FILE_NAME_LEN_MAX + 1);
							/* assert() compiles out under NDEBUG (this project's real ARM
							 * release build passes -DNDEBUG); a failed allocation here --
							 * reachable from ordinary Delete button presses, not boot --
							 * would otherwise fall straight into fu_path_combine() on a
							 * NULL pointer. */
							if ( fullpath == NULL )
							{
								browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "DELETE failed!");
								sel_active = 0;
							}
							else
							{
								fu_path_combine(fullpath, ESP_FILE_PATH_LEN_MAX + ESP_FILE_NAME_LEN_MAX + 1, f_info->dir_name, f_info->file_name);
								uret = m1_fb_delete_file(fullpath);
								if ( !uret )
								{
									sel_active = 0;
									refresh = 1;
									browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "DELETE successfully!");
									vTaskDelay(500);
								} // if ( !uret )
								else
								{
									browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "DELETE failed!");
								} // else
								sel_active = 0;
								free(fullpath);
								fullpath = NULL;
							}
						} // if ( sel_active )
					} // if (sel_item==0)
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // OK?
				{
					if (sel_item==0) // Delete?
					{
						; // Do other things for this task, if needed
						if ( !sel_active )
						{
							browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "RIGHT key to confirm");
						    //u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 40, menu_text_y - THIS_LCD_MENU_TEXT_ROW_SPACE + 2, 10, 10, arrowleft_10x10);
						    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 20, INFO_BOX_Y_POS_ROW_3 - THIS_LCD_MENU_TEXT_ROW_SPACE + 1, 10, 10, arrowright_10x10);
						    m1_u8g2_nextpage(); // Update display RAM
						    sel_active = 1;
						} // if ( !sel_active )
						else
						{
							browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "");
						    sel_active = 0;
						} // else
					} // if (sel_item==0)
					else if (sel_item==1) // Rename
					{
						fullpath = malloc(ESP_FILE_PATH_LEN_MAX + ESP_FILE_NAME_LEN_MAX + 1);
						/* assert() compiles out under NDEBUG -- see the matching Delete
						 * branch above for the full rationale. */
						if ( fullpath == NULL )
						{
							browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "RENAME failed!");
							vTaskDelay(500);
						}
						else
						{
							fu_path_combine(fullpath, ESP_FILE_PATH_LEN_MAX + ESP_FILE_NAME_LEN_MAX + 1, f_info->dir_name, f_info->file_name);
							uret = m1_fb_rename_file(fullpath, new_filename, false);
							if ( uret )
							{
								browse_info_box_update(INFO_BOX_Y_POS_ROW_3, "RENAME failed!");
								vTaskDelay(500);
							} // if ( uret )
							free(fullpath);
							fullpath = NULL;
						}
						refresh = 1;
					} // else if (sel_item==1)
				} // else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )

				if ( refresh )
				{
					refresh = 0;
					no_file = browse_and_dispatch(&f_info);
					if ( no_file )
					{
						xQueueReset(main_q_hdl); // Reset main q before return
						return; // Exit and return to the calling task (subfunc_handler_task)
					} // if ( no_file )
					sel_item = 0;
					browse_gui_update(sel_item, f_info->file_name);
				} // if ( refresh )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void storage_explore(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
S_M1_file_info *storage_browse(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	const S_M1_file_info *f_info;
	uint8_t error_stat;
	BaseType_t ret;

	file_info.status = FB_OK;
	file_info.file_is_selected = false;
	info_filename[0] = '\0';
	info_filepath[0] = '\0';

	if (m1_fb_init(&m1_u8g2) == NULL)
	{
		file_info.status = FB_ERR_GUI;
		menu_setting_storage_exit();
		return &file_info;
	}

	/* Graphic work starts here */
    m1_u8g2_firstpage(); // This call required for page drawing in mode 1
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set the color to Black
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    m1_u8g2_nextpage();

    error_stat = 0;
    switch ( m1_sdcard_get_status() )
    {
	case SD_access_OK:
		/* The common success path below performs the one required initial
		 * display. Calling it here as well enumerated and rendered the same
		 * directory twice whenever the card was already mounted. */
		break;

    	case SD_access_NotReady:
    		do
    		{
        		if ( !m1_sd_detected() ) // No SD card detected?
        		{
        			error_stat = 1;
        			break;
        		} // if ( !m1_sd_detected() )
        		if ( m1_sdcard_init_retry() != SD_RET_OK )
        		{
        			error_stat = 1;
        			break;
        		}
    		} while (0);
    		break;

    	case SD_access_NoFS:
    		; // Update GUI if necessary
    		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    		u8g2_DrawXBMP(&m1_u8g2, 0, 0, M1_LCD_DISPLAY_WIDTH, M1_LCD_DISPLAY_HEIGHT, micro_sd_card_error_format);
    		m1_u8g2_nextpage();
    		error_stat = 2;
    		break;

    	default:
    		m1_sdcard_mount(); // Try to mount the SD card again
    		break;
    } // switch ( m1_sdcard_get_status() )

    if ( !error_stat )
	{
		if ( m1_sdcard_get_status()==SD_access_OK )
		{
			m1_fb_display(NULL);
		}
		else
		{
			error_stat = 1;
		}
	} // if ( !error_stat )

    if ( error_stat==1 )
	{
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
	} // if ( error_stat==1 )
	M1_LOG_I(M1_LOGDB_TAG, "Init status: %s\r\n", m1_sd_error_msg(m1_sdcard_get_status()));

	while (1 ) // Main loop of this task
	{
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
					/* Generic Explorer only (m1_fb_explorer_nav_active()): BACK ascends
					 * one directory instead of exiting, unless already at the real SD
					 * root -- every other caller of this shared loop (STM32/ESP32
					 * firmware pickers, NFC/RFID/Sub-GHz Saved browsers) never enables
					 * explorer-nav mode, so BACK keeps its existing exit-always behavior
					 * for them, unchanged. */
					if ( m1_fb_explorer_nav_active() )
					{
						m1_fb_back_result_t back_res = m1_fb_navigate_back();
						if ( back_res == M1_FB_BACK_PARENT_OPENED )
						{
							f_info = m1_fb_display(NULL);
							if ( !f_info || f_info->status != FB_OK )
							{
								/* The directory handle was closed before changing to the
								 * parent.  If reopening that parent fails, this session is
								 * no longer usable: publish an error result and terminate
								 * instead of continuing with a closed/stale DIR object. */
								file_info.status = FB_ERR_SDCARD;
								file_info.file_is_selected = false;
								m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
								menu_setting_storage_exit();
								break;
							} // if ( !f_info || f_info->status != FB_OK )
							continue; // Stay in this browsing session; parent directory is now open
						} // if ( back_res == M1_FB_BACK_PARENT_OPENED )
						// M1_FB_BACK_AT_ROOT or M1_FB_BACK_ERROR: fall through and exit, same as today
					} // if ( m1_fb_explorer_nav_active() )
					menu_setting_storage_exit();
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					if ( error_stat ) // Do nothing if there is an init error!
						continue;
					if ( m1_sdcard_get_status() != SD_access_OK )
					{
						m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
						continue; // Skip doing tasks, wait for exit or new SD card access status
					} // if ( m1_sdcard_get_status() != SD_access_OK )
					f_info = m1_fb_display(&this_button_status);
					if (f_info && f_info->status==FB_OK)
					{
						if ( f_info->file_is_selected )
						{
							/* Never publish a truncated or unterminated filesystem path. */
							if (!m1_fb_copy_selection(info_filepath, sizeof(info_filepath),
							                          info_filename, sizeof(info_filename)))
							{
								file_info.status = FB_ERR_SDCARD;
								m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
								continue;
							}
							file_info.status = FB_OK;
							file_info.file_is_selected = true;
							menu_setting_storage_exit();
							break; // Exit and return to the calling task (subfunc_handler_task)
						} // if ( f_info->file_is_selected )
					} // if (f_info->status==FB_OK)
					else
					{
						m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
					} // else
				} // else
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

	return &file_info;

} // S_M1_file_info *storage_browse(void)


/* storage_mount()/storage_unmount() (manual Mount/Unmount SD Card screens)
 * were removed: neither had any caller left once their Settings > Storage
 * menu items were removed, and the underlying SD driver mount/unmount
 * primitives (m1_sdcard_mount()/m1_sdcard_unmount()) are still used directly
 * by the SD auto-retry and USB Mass Storage paths elsewhere in this file. */



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/* ---- USB Drive screens: the SAME large USB icon in the upper portion for every
 * state; only the status/action text below changes. ------------------------- */
#define USBMSC_ICON_W   52
#define USBMSC_ICON_H   24
#define USBMSC_ICON_X   ((M1_LCD_DISPLAY_WIDTH - USBMSC_ICON_W) / 2)   /* centered */
#define USBMSC_ICON_Y   4

static void usbmsc_draw_icon(void)
{
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_DrawXBMP(&m1_u8g2, USBMSC_ICON_X, USBMSC_ICON_Y, USBMSC_ICON_W, USBMSC_ICON_H, usb_drive_icon_52x24);
}

/* STATE 1 -- pre-mount gate: "Press [CENTER] to mount" (center-button glyph inline). */
static void usbmsc_screen_gate(void)
{
    u8g2_FirstPage(&m1_u8g2);
    usbmsc_draw_icon();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);   /* menu bold (helvB08) */
    const char *a = "Press ", *b = " to mount";
    int wa = (int)u8g2_GetStrWidth(&m1_u8g2, a);
    int wb = (int)u8g2_GetStrWidth(&m1_u8g2, b);
    int x0 = (M1_LCD_DISPLAY_WIDTH - (wa + 10 + wb)) / 2;
    u8g2_DrawStr(&m1_u8g2, x0, 46, a);
    u8g2_DrawXBMP(&m1_u8g2, x0 + wa, 37, 10, 10, target_10x10);   /* CENTER button icon */
    u8g2_DrawStr(&m1_u8g2, x0 + wa + 10, 46, b);
    m1_u8g2_nextpage();
}

/* STATE 2 -- active / host has the drive: "Eject on computer". */
static void usbmsc_screen_active(void)
{
    u8g2_FirstPage(&m1_u8g2);
    usbmsc_draw_icon();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);   /* menu bold (helvB08) */
    const char *t = "Eject on computer";
    u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(&m1_u8g2, t)) / 2, 46, t);
    m1_u8g2_nextpage();
}

/* STATE 3 -- host has ejected: "Ejected" (no "SD card ready"). A genuine remount
 * failure is still reported truthfully. */
static void usbmsc_screen_ejected(uint8_t mounted_ok)
{
    u8g2_FirstPage(&m1_u8g2);
    usbmsc_draw_icon();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);   /* menu bold (helvB08) */
    const char *t = mounted_ok ? "Ejected" : "Remount err";
    u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(&m1_u8g2, t)) / 2, 44, t);
    if ( !mounted_ok )
    {
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        const char *e = "Check SD card";
        u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(&m1_u8g2, e)) / 2, 58, e);
    }
    m1_u8g2_nextpage();
}

void storage_usbmsc(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    uint8_t sd_stat;
    uint8_t vbus_lost_cnt;
    uint8_t mounted_ok;

    /* -------------------- CENTER/OK Mount gate --------------------
     * The SD card stays internally mounted until the user presses CENTER/OK.
     * Selecting this menu item must never, by itself, hand the card to a host. */
    usbmsc_screen_gate();

    for ( ; ; )
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if ( ret!=pdTRUE || q_item.q_evt_type!=Q_EVENT_KEYPAD )
            continue;
        (void)xQueueReceive(button_events_q_hdl, &this_button_status, 0);

        if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ||
             this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
        {
            /* Leave without taking ownership; SD stays mounted for internal use. */
            xQueueReset(main_q_hdl);
            return;
        }

        if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
        {
            /* Require a live USB cable (VBUS present, via the BQ25896 charger)
             * before handing over the card. This also arms cable-removal
             * detection immediately and avoids a false "unsafe removal" when no
             * host is attached. RIGHT is intentionally unused. */
            if ( bq_getVBUS_GD()==0 )
            {
                u8g2_FirstPage(&m1_u8g2);
                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_B);   /* menu bold (helvB08) */
                {
                    const char *l1 = "Please connect";
                    const char *l2 = "USB cable";
                    u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(&m1_u8g2, l1)) / 2, 28, l1);
                    u8g2_DrawStr(&m1_u8g2, (M1_LCD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(&m1_u8g2, l2)) / 2, 44, l2);
                }
                m1_u8g2_nextpage();
                vTaskDelay(pdMS_TO_TICKS(2400));   /* doubled dwell (~2.4s) */
                /* Redraw the gate prompt and keep waiting for a valid Mount. */
                usbmsc_screen_gate();
                continue;
            }
            break; /* VBUS present -> proceed to the mount transition */
        }
        /* Any other key is ignored on the gate. */
    }

    /* Transition order: block user filesystem access, sync/close any writable
     * handles, unmount, then enable MSC. This modal path runs in
     * subfunc_handler_task with no background SD writer or persistent writable
     * handles. Verify each step and never grant MSC after a failure. */
    sd_stat = m1_sdcard_get_status();
    if ( sd_stat!=SD_access_OK && sd_stat!=SD_access_NoFS )
    {
        /* No usable card to share (NotReady / NotOK). Do not grant MSC. */
        u8g2_FirstPage(&m1_u8g2);
        m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
        m1_u8g2_nextpage();
        vTaskDelay(pdMS_TO_TICKS(1500));
        xQueueReset(main_q_hdl);
        return;
    }

    /* This entry path has no open writable handles. If a writer is added,
     * sync and close its handles here before unmounting. */

    if ( m1_sdcard_unmount()!=FR_OK )
    {
        /* Unmount failed -> restore the internal mount, report, do NOT grant MSC. */
        m1_sdcard_mount();
        u8g2_FirstPage(&m1_u8g2);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
        u8g2_DrawStr(&m1_u8g2, 22, 24, "Storage busy");
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        u8g2_DrawStr(&m1_u8g2, 14, 42, "Cannot share now");
        m1_u8g2_nextpage();
        vTaskDelay(pdMS_TO_TICKS(1500));
        xQueueReset(main_q_hdl);
        return;
    }

    /* Clear any stale host-eject event, then grant ownership - only now that the
     * card is confirmed unmounted from the internal FatFs. */
    usbmsc_host_ejected = 0;
    usbmsc_sd_enable = true;

    /* MSC readiness is now satisfied by construction (status==UnMounted, card
     * present, usbmsc_sd_enable==true -> STORAGE_status_usbmsc_sd() reports ready
     * when the host probes).
     *
     * Media-ready re-enumeration: media is prepared FIRST (above); now force the
     * host to re-probe by toggling D+, so the host's first TEST_UNIT_READY /
     * READ_CAPACITY after enumeration already sees ready media (no slow retry). */
    m1_usb_cdc_force_reconnect();

    /* -------------------- Active screen -------------------- */
    usbmsc_screen_active();

    /* -------- Active loop: event-driven reclaim, in task context -------- */
    vbus_lost_cnt = 0;
    for ( ; ; )
    {
        ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(USBMSC_POLL_MS));

        /* (1) Confirmed host eject (posted by the SCSI START_STOP_UNIT hook).
         *     Reclaim cleanly: disable MSC first, then remount. No warning. */
        if ( usbmsc_host_ejected )
        {
            usbmsc_sd_enable = false;
            m1_sdcard_mount();
            sd_stat = m1_sdcard_get_status();
            mounted_ok = ( sd_stat==SD_access_OK || sd_stat==SD_access_NoFS );

            usbmsc_screen_ejected(mounted_ok);
            vTaskDelay(pdMS_TO_TICKS(1500));
            xQueueReset(main_q_hdl);
            return;
        }

        /* (2) Physical cable removal via debounced VBUS loss. Reached only if no
         *     host eject occurred first. Reclaim the card silently: the brief
         *     "unsafe removal" notice has been removed. A genuine remount /
         *     filesystem failure is still reported rather than hidden. */
        if ( bq_getVBUS_GD()==0 )
        {
            vbus_lost_cnt++;
            if ( vbus_lost_cnt>=USBMSC_VBUS_LOSS_DEBOUNCE )
            {
                usbmsc_sd_enable = false;   /* drop ownership before remount */
                m1_sdcard_mount();
                sd_stat = m1_sdcard_get_status();
                mounted_ok = ( sd_stat==SD_access_OK || sd_stat==SD_access_NoFS );

                if ( !mounted_ok )
                {
                    u8g2_FirstPage(&m1_u8g2);
                    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                    u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
                    u8g2_DrawStr(&m1_u8g2, 10, 24, "Remount failed");
                    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
                    u8g2_DrawStr(&m1_u8g2, 20, 42, "Check SD card");
                    m1_u8g2_nextpage();
                    vTaskDelay(pdMS_TO_TICKS(1500));
                }
                xQueueReset(main_q_hdl);
                return;
            }
        }
        else
        {
            vbus_lost_cnt = 0; /* VBUS present -> reset debounce */
        }

        /* (3) Buttons: Back/Left must NOT steal a mounted volume. Re-show the
         *     eject instruction and stay active. */
        if ( ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD )
        {
            (void)xQueueReceive(button_events_q_hdl, &this_button_status, 0);
            if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ||
                 this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
            {
                usbmsc_screen_active();   /* re-show active state (same icon + "Eject on computer") */
            }
        }
    }

} // void storage_usbmsc(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void browse_gui_update(uint8_t sel_item, char *file_name)
{
	uint8_t prn_name[BROWSE_GUI_DISP_LINE_LEN_MAX + 1] = {0};
	uint8_t i, len, menu_text_y;
	char *print_ptr;

	menu_text_y = THIS_LCD_MENU_TEXT_FIRST_ROW_Y;

	/* Graphic work starts here */
	m1_u8g2_firstpage(); // This call required for page drawing in mode 1
	for (i=0; i<SDCARD_EXPLORE_FUNCTIONS_N; i++)
	{
		if ( i==sel_item )
		{
			// Draw box for selected menu item with text color
			u8g2_DrawBox(&m1_u8g2, 0, menu_text_y - THIS_LCD_MENU_TEXT_ROW_SPACE + 2, M1_LCD_SUB_MENU_TEXT_FRAME_W, THIS_LCD_MENU_TEXT_ROW_SPACE);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			u8g2_DrawStr(&m1_u8g2, 4, menu_text_y, sdcard_explore_options[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // return to default font
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, menu_text_y, sdcard_explore_options[i]);
		}
		menu_text_y += THIS_LCD_MENU_TEXT_ROW_SPACE;
	} // for (i=0; i<SDCARD_EXPLORE_FUNCTIONS_N; i++)

	// Draw info box at the bottom
	m1_info_box_display_init(true);
	print_ptr = file_name;
	len = strlen(file_name);
	if ( len >= BROWSE_GUI_DISP_LINE_LEN_MAX )
	{
		strncpy(prn_name, file_name, BROWSE_GUI_DISP_LINE_LEN_MAX);
		strcpy(&prn_name[BROWSE_GUI_DISP_LINE_LEN_MAX - 3], "...");
		print_ptr = prn_name;
	}
	m1_info_box_display_draw(INFO_BOX_ROW_1, print_ptr);

    m1_u8g2_nextpage(); // Update display RAM

} // static void browse_gui_update(uint8_t sel_item, char *file_name)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void browse_info_box_update(uint8_t box_y, char *new_info)
{
	// Clear old content
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	u8g2_DrawBox(&m1_u8g2, 4, box_y - M1_SUB_MENU_FONT_HEIGHT - 1, 120, M1_SUB_MENU_FONT_HEIGHT + 2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set to background color
	u8g2_DrawStr(&m1_u8g2, 4, box_y, new_info);

	m1_u8g2_nextpage(); // Update display RAM
} // void browse_info_box_update(uint8_t box_y, char *new_info)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t browse_refresh(S_M1_file_info **f_info)
{
	uint8_t uret, sys_error = 1;

	m1_fb_set_sort_enabled(true);       /* generic explorer: alphabetical, dir-first */
	m1_fb_set_friendly_names(true);     /* display-only top-level labels */
	m1_fb_set_explorer_nav_enabled(true); /* generic explorer: BACK ascends one level */
	*f_info = storage_browse();
	if ( (*f_info)->file_is_selected )
	{
		do
		{
			uret = strlen((*f_info)->file_name);
			if ( !uret )
				break;
			sys_error = 0;
		} while (0);
	} // if ( *f_info->file_is_selected )

	return sys_error;
} // static uint8_t browse_refresh(S_M1_file_info **f_info)

/* ---- General-browser -> native-feature dispatch (separation of
 * filesystem browsing from saved-content shortcuts; M1 native handlers remain the
 * single source of truth for loading/validation/result screens) --------------- */
extern void sub_ghz_replay(void);   /* SubG  Replay menu entry (m1_sub_ghz.c)*/

static char s_ex_dir[ESP_FILE_PATH_LEN_MAX + 1];
static char s_ex_name[ESP_FILE_NAME_LEN_MAX + 1];
static S_M1_file_info s_ex_info;
static uint8_t s_ex_state = 0;   /* 0 idle, 1 pending, 2 active */

void m1_browser_request_explore_open(const S_M1_file_info *f)
{
	strncpy(s_ex_dir,  (f && f->dir_name)  ? f->dir_name  : "", sizeof(s_ex_dir)  - 1); s_ex_dir[sizeof(s_ex_dir)  - 1] = 0;
	strncpy(s_ex_name, (f && f->file_name) ? f->file_name : "", sizeof(s_ex_name) - 1); s_ex_name[sizeof(s_ex_name) - 1] = 0;
	s_ex_info.dir_name = s_ex_dir; s_ex_info.file_name = s_ex_name;
	s_ex_info.file_is_selected = true; s_ex_info.status = FB_OK;
	s_ex_state = 1; /* pending */
}
S_M1_file_info *m1_browser_explore_take_pending(void)
{
	if (s_ex_state == 1) { s_ex_state = 2; return &s_ex_info; } /* -> active */
	return NULL;
}
int  m1_browser_explore_active(void) { return s_ex_state == 2; }
void m1_browser_explore_end(void)    { s_ex_state = 0; }

static int ex_ends_with(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext), i; const char *p;
	if (n < e) return 0;
	p = name + (n - e);
	for (i = 0; i < e; i++) { char a = p[i], b = ext[i]; if (a>='A'&&a<='Z') a+=32; if (b>='A'&&b<='Z') b+=32; if (a!=b) return 0; }
	return 1;
}
/* Recognize by extension and DISPATCH into the native feature flow (which loads,
 * validates, shows its result view, and returns to us on Back). */
static int explore_dispatch_recognized(S_M1_file_info *f)
{
	if (!f || !f->file_name) return 0;
	/* Native saved-file menus render via the shared submenu path, which keys off
	 * menu_level_id. Reached from the browser (outside the menu system) it can be
	 * stale at 0 -> main-menu look (left icons + large font). Force sub-menu level
	 * so a recognized file renders identically to its in-feature Saved/Replay entry. */
	m1_gui_force_sub_menu_level();
	if      (ex_ends_with(f->file_name, ".nfc"))  { nfc_saved_launch(f->dir_name, f->file_name);         return 1; }
	else if (ex_ends_with(f->file_name, ".rfid")) { rfid_125khz_saved_launch(f->dir_name, f->file_name); return 1; }
	else if (ex_ends_with(f->file_name, ".sgh"))  { m1_browser_request_explore_open(f); sub_ghz_replay();    m1_browser_explore_end(); return 1; }
	return 0;
}
/* Browse; recognized file -> native flow then re-browse; unknown file -> return so
 * the caller offers the generic Rename/Delete actions. */
static uint8_t browse_and_dispatch(S_M1_file_info **f_info)
{
	uint8_t no_file;
	for (;;)
	{
		no_file = browse_refresh(f_info);
		if (no_file) return no_file;
		if (!explore_dispatch_recognized(*f_info)) return 0;
		/* recognized file opened in its native view; on Back, re-browse the SAME
		 * folder (one-shot start dir) instead of jumping to root. */
		if ((*f_info) && (*f_info)->dir_name && (*f_info)->dir_name[0])
			m1_fb_set_start_dir((*f_info)->dir_name);
	}
}



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void storage_format(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	bool format_ok;
	BaseType_t ret;

    format_ok = false;
    /* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
    if ( m1_sdcard_get_status()!=SD_access_NotReady )
    {
    	format_ok = true;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		u8g2_DrawStr(&m1_u8g2, 22, 20, "Format SD Card");
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 18, 30, "Data will be lost!");

		u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
		u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
		u8g2_DrawStr(&m1_u8g2, 11, 61, "Cancel");
		u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
		u8g2_DrawStr(&m1_u8g2, 87, 61, "Format");
    } // if ( m1_sdcard_get_status()!=SD_access_NotReady )
    else
    {
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
    }
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
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					; // Do extra tasks here if needed

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( !format_ok )
						continue;
					if ( m1_sdcard_get_status()==SD_access_NotReady ) // Check again before executing
						format_ok = false;
					if ( format_ok )
					{
						u8g2_FirstPage(&m1_u8g2); // Clear screen
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
						u8g2_DrawStr(&m1_u8g2, 30, 10, "Formatting...");
				    	u8g2_DrawXBMP(&m1_u8g2, 55, 16, 18, 32, hourglass_18x32); // Draw icon
				    	m1_u8g2_nextpage(); // Update display RAM
				    	if ( m1_sdcard_format()!=FR_OK )
				    	{
				    		if ( m1_sdcard_get_status()==SD_access_NotOK )
				    			format_ok = false;
				    	}
				    	if ( format_ok )
						{
				    		u8g2_DrawStr(&m1_u8g2, 38, 60, "Successful");
						} // if ( format_ok )
				    	else
				    	{
				    		u8g2_DrawStr(&m1_u8g2, 45, 60, "Failed");
				    	}
				    	m1_u8g2_nextpage(); // Update display RAM
				    	format_ok = false;
					} // if ( format_ok )
					else
					{
						m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
					}
				} // else if ( this_button_status.event[BUTTON_RIGT_KP_ID]==BUTTON_EVENT_CLICK )
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

} // void storage_format(void)
