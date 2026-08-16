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

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Storage"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

#define BROWSE_GUI_DISP_LINE_LEN_MAX			(M1_LCD_DISPLAY_WIDTH/M1_SUB_MENU_SFONT_WIDTH - 3)

#define SDCARD_EXPLORE_FUNCTIONS_N				1

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
void storage_mount(void);
void storage_unmount(void);
void storage_format(void);
static void browse_gui_update(uint8_t sel_item, char *file_name);
static void browse_info_box_update(uint8_t box_y, char *new_info);
static uint8_t browse_refresh(S_M1_file_info **f_info);
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

	sel_active = 0;
	refresh = 0;

	no_file = browse_refresh(&f_info);
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
							assert(fullpath!=NULL);
							sprintf(fullpath, "%s/%s", f_info->dir_name, f_info->file_name);
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
				} // else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )

				if ( refresh )
				{
					refresh = 0;
					no_file = browse_refresh(&f_info);
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
	S_M1_file_info *f_info;
	uint8_t error_stat;
	BaseType_t ret;

	file_info.status = FB_OK;
	file_info.file_is_selected = false;

	m1_fb_init(&m1_u8g2);

	/* Graphic work starts here */
    m1_u8g2_firstpage(); // This call required for page drawing in mode 1
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set the color to Black
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    m1_u8g2_nextpage();

    error_stat = 0;
    switch ( m1_sdcard_get_status() )
    {
    	case SD_access_OK:
    		m1_fb_display(NULL);
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
					if (f_info->status==FB_OK)
					{
						if ( f_info->file_is_selected )
						{
							//m1_fb_dyn_strcat(fullpath, 2, "",  f_info->dir_name, f_info->file_name);
							file_info.file_is_selected = true;
							strncpy(file_info.file_name, f_info->file_name, ESP_FILE_NAME_LEN_MAX - 1);
							file_info.file_name[ESP_FILE_NAME_LEN_MAX - 1] = '\0';
							strncpy(file_info.dir_name, f_info->dir_name, ESP_FILE_PATH_LEN_MAX - 1);
							file_info.dir_name[ESP_FILE_PATH_LEN_MAX - 1] = '\0';
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


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void storage_mount(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	uint8_t mount_ok;
	BaseType_t ret;

    mount_ok = m1_sdcard_get_status();
    /* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
    if ( mount_ok!=SD_access_NotReady && mount_ok!=SD_access_OK && mount_ok!=SD_access_NoFS )
    {
    	mount_ok = true;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		u8g2_DrawStr(&m1_u8g2, 22, 20, "Mount SD Card");
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 18, 30, "SD card will be");
		u8g2_DrawStr(&m1_u8g2, 18, 40, "accessible");

		u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
		u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
		u8g2_DrawStr(&m1_u8g2, 11, 61, "Cancel");
		u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
		u8g2_DrawStr(&m1_u8g2, 92, 61, "Mount");
    } // if ( mount_ok!=SD_access_NotReady && mount_ok!=SD_access_OK && mount_ok!=SD_access_NoFS )
    else
    {
    	mount_ok = false;
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
					if ( mount_ok )
					{
						u8g2_FirstPage(&m1_u8g2); // Clear screen
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
						u8g2_DrawStr(&m1_u8g2, 30, 10, "Mounting...");
				    	u8g2_DrawXBMP(&m1_u8g2, 55, 16, 18, 32, hourglass_18x32); // Draw icon
				    	m1_u8g2_nextpage(); // Update display RAM

				    	m1_sdcard_mount();
				    	mount_ok = m1_sdcard_get_status();

				    	if ( mount_ok==SD_access_OK || mount_ok==SD_access_NoFS )
				    		mount_ok = true;
				    	else
				    		mount_ok = false;

				    	if ( mount_ok )
						{
				    		u8g2_DrawStr(&m1_u8g2, 38, 60, "Successful");
						} // if ( format_ok )
				    	else
				    	{
				    		u8g2_DrawStr(&m1_u8g2, 45, 60, "Failed");
				    	}
				    	m1_u8g2_nextpage(); // Update display RAM
				    	mount_ok = false;
					} // if ( format_ok )
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

} // void storage_mount(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void storage_unmount(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	uint8_t unmount_ok;
	BaseType_t ret;

	unmount_ok = m1_sdcard_get_status();
	if ( unmount_ok==SD_access_OK || unmount_ok==SD_access_NoFS )
		unmount_ok = true;
	else
		unmount_ok = false;
    /* Graphic work starts here */
	u8g2_FirstPage(&m1_u8g2);
    if ( unmount_ok )
    {
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		u8g2_DrawStr(&m1_u8g2, 22, 20, "UnMount SD Card");
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 18, 30, "SD card will be");
		u8g2_DrawStr(&m1_u8g2, 18, 40, "inaccessible");

		u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
		u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
		u8g2_DrawStr(&m1_u8g2, 11, 61, "Cancel");
		u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
		u8g2_DrawStr(&m1_u8g2, 84, 61, "Unmount");
    } // if ( unmount_ok )
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
					if ( unmount_ok )
					{
						u8g2_FirstPage(&m1_u8g2); // Clear screen
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
						u8g2_DrawStr(&m1_u8g2, 30, 10, "Unmounting...");
				    	u8g2_DrawXBMP(&m1_u8g2, 55, 16, 18, 32, hourglass_18x32); // Draw icon
				    	m1_u8g2_nextpage(); // Update display RAM

				    	m1_sdcard_unmount();

				    	u8g2_DrawStr(&m1_u8g2, 38, 60, "Successful");
						m1_u8g2_nextpage(); // Update display RAM
				    	unmount_ok = false;
					} // if ( format_ok )
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

} // void storage_unmount(void)



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
