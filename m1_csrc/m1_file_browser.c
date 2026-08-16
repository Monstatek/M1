/* See COPYING.txt for license details. */

/*
*
 *m1_file_browser.c
*
 *Library for sd card file browsing
*
 * M1 Project
*
*/

/************************** *I N C L U D E S **********************************/

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"

/************************** *D E F I N E S ************************************/

#define DISABLE_IRQ			__disable_irq(); __DSB(); __ISB();
#define ENABLE_IRQ			__enable_irq();

#define FILE_BROWSER_MAX_FILES			96
#define DIRECTORY_MAX_DEPTH_LEVEL		32

#define GUI_SCROLLBAR_WIDTH				4 // pixel

#define FILENAME_LEN_ON_CLI_MAX			80 // Max filename length to display on console

#define M1_LOGDB_TAG	"SD-BROWSER"

//************************* *C O N S T A N T **********************************/

const char m1_fb_data_types[]    = ".log.LOG.text.TEXT.txt.TXT";

//************************* *S T R U C T U R E S *******************************

/**************************** *V A R I A B L E S ******************************/

static S_M1_file_browser_hdl *pfb_hdl = NULL;
static u8g2_t *plcd_hdl;

static bool fb_gui_check;
FIL m1_log_file;

/******************** *F U N C T I O N   P R O T O T Y P E S ******************/

extern void m1_u8g2_firstpage(void);
extern uint8_t m1_u8g2_nextpage(void);
S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status);
S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl);
void m1_fb_deinit(void);
uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...);
uint8_t m1_fb_open_new_file(FIL *file, const char *filename);
uint8_t m1_fb_open_file(FIL *file, const char *filename);
uint8_t m1_fb_close_file(FIL *file);
uint8_t m1_fb_open_log_file(const char *filename);
uint8_t m1_fb_close_log_file(void);
uint8_t m1_fb_delete_file(const char *filename);
uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size);
uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size);
uint8_t m1_fb_check_low_freespace(void);

/************** *F U N C T I O N   I M P L E M E N T A T I O N ****************/



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)
{
	pfb_hdl = (S_M1_file_browser_hdl*)calloc(1, sizeof(S_M1_file_browser_hdl));

	pfb_hdl->listing_index_buffer = (uint16_t *)calloc(1, sizeof(uint16_t));
	pfb_hdl->row_index_buffer = (uint16_t *)calloc(1, sizeof(uint16_t));
	*pfb_hdl->listing_index_buffer = 0;
	*pfb_hdl->row_index_buffer = 0;
	pfb_hdl->info.dir_name = malloc(strlen(SDCARD_DEFAULT_DRIVE_PATH) + 1);
	assert(pfb_hdl->info.dir_name != NULL);
	strcpy(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH);
	pfb_hdl->info.file_name = NULL;
	pfb_hdl->font_w = M1_GUI_FONT_WIDTH;
	pfb_hdl->font_h = M1_GUI_FONT_HEIGHT;
	pfb_hdl->font_h_spacing = M1_GUI_FONT_HEIGHT_SPACING;
	pfb_hdl->info.file_is_selected = FALSE;

	assert(lcd_hdl!=NULL);
	plcd_hdl = lcd_hdl;

	pfb_hdl->x = 0;
	pfb_hdl->y = 0;
	pfb_hdl->gui_width = plcd_hdl->width;
	pfb_hdl->gui_height = plcd_hdl->height;

	fb_gui_check = FALSE;

	m1_u8g2_firstpage(); // Reset display RAM

	return pfb_hdl;
} // S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
static uint8_t m1_fb_find_ext(char *file_name, const char *file_ext)
{
	int k, f;

	f = 0;
	k = strlen(file_name);
	while (k)
	{
		if (file_name[k - 1]=='.')
		{
			f = 1;
	   		break;
	   	}
	   	k--;
	}
	if (!f || k==1)
		return 0;
	if (strstr(file_ext, &file_name[k - 1]))
		return 1;

	return 0;
} // static uint8_t m1_fb_find_ext(char *file_name, const char *file_ext)




/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
static S_M1_file_browser_ext m1_fb_get_file_type(char *filename)
{
	if (m1_fb_find_ext(filename, m1_fb_data_types))
		return F_EXT_DATA;

	return F_EXT_OTHER;
} // static S_M1_file_browser_ext m1_fb_get_file_type(char *filename)



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
static void m1_fb_setframe(uint16_t x, uint16_t y, uint16_t x_width, uint16_t y_height)
{
	pfb_hdl->x = x;
	pfb_hdl->y = y;
	pfb_hdl->gui_width = x_width;
	pfb_hdl->gui_height = y_height;

	fb_gui_check = FALSE; // Reset each time the gui is updated
} // static void m1_fb_setframe(uint16_t x, uint16_t y, uint16_t x_width, uint16_t y_height)



/******************************************************************************/
/**
  * @brief  Create new file
  * @param  filename: name of the new file
  * 		file: pointer to the FIL object
  * @retval 1 for f_write error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_open_new_file(FIL *file, const char *filename)
{
	if ( file==NULL )
		return 0;

	if (f_open(file, filename, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_open_new_file(FIL *file, const char *filename)



/******************************************************************************/
/**
  * @brief  read from a file
  * @param  file: pointer to the FIL object
  * 		buffer: pointer to the data buffer
  * 		size: number of bytes to read
  * @retval number of read bytes
  */
/******************************************************************************/
uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size)
{
	UINT bytesread;

	if ( pfile==NULL )
		return 0;

    if (f_read(pfile, buffer, size, &bytesread) != FR_OK)
    {
    	return 0;
    }

	return bytesread;
} // uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size)




/******************************************************************************/
/**
  * @brief  write to a file
  * @param  file: pointer to the FIL object
  * 		buffer: pointer to the data buffer
  * 		size: number of bytes to write
  * @retval number of written bytes
  */
/******************************************************************************/
uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size)
{
	UINT byteswritten;

	if ( pfile==NULL )
		return 0;

	if (f_write(pfile, buffer, size, &byteswritten) != FR_OK)
	{
		return 0;
	}

	return byteswritten;
} // uint8_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size)




/******************************************************************************/
/**
  * @brief  Open an existing file
  * @param  filename: name of the new file
  * 		file: pointer to the FIL object*
  * @retval 1 for f_write error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_open_file(FIL *file, const char *filename)
{
	if ( file==NULL )
		return 0;

	if (f_open(file, filename, FA_OPEN_EXISTING | FA_READ) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_open_file(FIL *file, const char *filename)



/******************************************************************************/
/**
  * @brief  Open a directory
  * @param  directory: name of the new directory
  * 		dir: pointer to the DIR object
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_open_dir(DIR *dir, const char *directory)
{
	if ( dir==NULL )
		return 0;

	if (f_opendir(dir, directory) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_open_dir(DIR *dir, const char *directory)


/******************************************************************************/
/**
  * @brief  Make a sub-directory
  * @param  directory: name of the new directory
  *
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_make_dir(const char *directory)
{
	if ( directory==NULL )
		return 1;

	if (f_mkdir(directory) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_make_dir(const char *directory)



/******************************************************************************/
/**
  * @brief  Check the existance of a file or director
  * @param  filedir: name of the directory or file for checking
  * @retval 1 for existence, else 0
  */
/******************************************************************************/
uint8_t m1_fb_check_existence(const char *filedir)
{
	FRESULT fr;

	if ( filedir==NULL )
		return 0;

	fr = f_stat(filedir, NULL);
	if ( fr==FR_OK )
		return 1;

	return 0;
} // uint8_t m1_fb_check_existence(const char *filedir)



/******************************************************************************/
/**
  * @brief  Close a file
  * @param  file: pointer of the FIL object
  * @retval 1 for f_close error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_close_file(FIL *file)
{
	if ( file==NULL )
		return 0;

	return f_close(file);
} // uint8_t m1_fb_close_file(FIL *file)




/******************************************************************************/
/**
  * @brief  Open log file
  * @param  filename: name of the log file
  * @retval 1 for f_write error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_open_log_file(const char *filename)
{
	uint32_t byteswritten;

	if ( m1_fb_open_new_file(&m1_log_file, filename) )
	{
		return 1;
	}
	if (f_write(&m1_log_file, "MonstaTek M1 log file.\r\n", 24, (void *)&byteswritten) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_open_log_file(const char *filename)



/******************************************************************************/
/**
  * @brief  Close log file
  * @param  none
  * @retval 1 for f_close error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_close_log_file(void)
{
	return m1_fb_close_file(&m1_log_file);
} // uint8_t m1_fb_close_log_file(void)



/******************************************************************************/
/**
  * @brief  Delete a file or folder
  * @param  filename: name of the file or folder
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_delete_file(const char *filename)
{
	FRESULT  ret;
	if ( filename==NULL )
		return 1;

	ret = f_unlink(filename);

	return ret;
} // uint8_t m1_fb_delete_file(const char *filename)



/******************************************************************************/
/**
  * @brief  Check for low free space on an SD card
  * @param  None
  * @retval 1 for low free space, else 0
  */
/******************************************************************************/
uint8_t m1_fb_check_low_freespace(void)
{
	S_M1_SDCard_Access_Status stat;
	uint32_t free_cap, total_cap;

	if ( m1_sd_detected() )
	{
		stat = m1_sdcard_get_status();
		if ( stat==SD_access_NotReady )
		{
			M1_LOG_I(M1_LOGDB_TAG, "SD_access_NotReady.\r\n");
			m1_sdcard_init_ex();
		} // if ( stat==SD_access_NotReady )
		else
		{
			M1_LOG_I(M1_LOGDB_TAG, "SD_access_NotOK.\r\n");
			m1_sdcard_unmount();
			m1_sdcard_mount();
		} // else

		stat = m1_sdcard_get_status(); // Get latest status
		m1_sdcard_get_info();
	    if ( (stat != SD_access_OK) || (m1_sdcard_get_error_code() != FR_OK) )
	    {
	    	m1_sdcard_unmount();
	    	m1_sdcard_set_status(SD_access_NotReady);
	    	return 1;
	    } // if ( (stat != SD_access_OK) || (m1_sdcard_get_error_code() != FR_OK) )

	    total_cap = m1_sdcard_get_total_capacity();
	    free_cap = m1_sdcard_get_free_capacity();
	    if ( free_cap < (total_cap/100)*10 ) // 10%
	    {
	    	return 1;
	    }
	    else
	    {
	    	return 0;
	    }
	} // if ( m1_sd_detected() )

	return 1;
} // uint8_t m1_fb_check_low_freespace(void)




/******************************************************************************/
/*
*	This function displays files and folders on the LCD
*
*/
/******************************************************************************/
S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status)
{
	char name[FF_MAX_LFN + 1];
	FRESULT res;
	static DIR directory;
	FILINFO file_info, this_file;
	const S_M1_menu_icon_data *fb_icon;
	S_M1_file_browser_ext f_ext;
	static uint16_t num_of_files;
	static uint16_t gui_max_column, gui_max_row;
	static uint16_t gui_width, gui_height;
	static uint16_t scroll_h;
	uint16_t scroll_y, count, len;
	uint16_t l, k;
	uint8_t y_offset, disp_max_column, ext_len;
	static uint8_t spacing;
	static bool scroll_ok;
	bool flag;

	if (!fb_gui_check)
	{
		gui_max_column = pfb_hdl->gui_width / pfb_hdl->font_w;
		gui_max_row = pfb_hdl->gui_height / pfb_hdl->font_h;
		spacing = pfb_hdl->font_h_spacing;

		if (pfb_hdl->x < 0 || pfb_hdl->y < 0 ||
			pfb_hdl->x + pfb_hdl->gui_width > M1_LCD_DISPLAY_WIDTH ||
			pfb_hdl->y + pfb_hdl->gui_height > M1_LCD_DISPLAY_HEIGHT||
			gui_max_column < 3 ||
			gui_max_row < 1  )
		{
			pfb_hdl->info.status = FB_ERR_GUI;
			return &pfb_hdl->info;
		}

		gui_width = gui_max_column*pfb_hdl->font_w;
		gui_height = gui_max_row*pfb_hdl->font_h;
		gui_max_row -= 1; // Need room for row spacing
		fb_gui_check = TRUE;
	} // if (!fb_gui_check)

	while (1) // Not an endless loop
	{
		if (button_status==NULL)
		{
			res = f_opendir(&directory, pfb_hdl->info.dir_name);

			if (res != FR_OK)
			{
				pfb_hdl->info.status = FB_ERR_SDCARD;
				return &pfb_hdl->info;
			}
			if (!pfb_hdl->info.file_is_selected)
			{
				pfb_hdl->listing_index = pfb_hdl->listing_index_buffer[pfb_hdl->dir_level];
				pfb_hdl->row_index = pfb_hdl->row_index_buffer[pfb_hdl->dir_level];
			}

		    num_of_files = 1;
		    while (num_of_files < FILE_BROWSER_MAX_FILES)
		    {
		    	res = f_readdir(&directory, &file_info);
		    	if (res || !file_info.fname[0])
		    		break;
		    	if (!(file_info.fattrib & (AM_HID | AM_SYS))) // Not a system or hidden file?
		    		num_of_files++;
		    } // while (num_of_files < FILE_BROWSER_MAX_FILES)

			scroll_h = gui_height / num_of_files;

			if (scroll_h < pfb_hdl->font_h)
				scroll_h = pfb_hdl->font_h;

			scroll_ok = FALSE;
			if ((num_of_files > gui_max_row) && (gui_max_column > 3))
				scroll_ok = TRUE;
		} // if (button_status==NULL)

		// Reading keys from user
		else
		{
	       	if ( button_status->event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
	       		if ( pfb_hdl->listing_index < (num_of_files - 1) )
	       		{
	       			pfb_hdl->row_index++;
	       			if ((pfb_hdl->row_index >= gui_max_row) || (pfb_hdl->row_index >= num_of_files) )
	       				pfb_hdl->row_index--;
	       			pfb_hdl->listing_index++;
	       		} // if ( pfb_hdl->listing_index < (num_of_files - 1) )
	       	} // if ( button_status->event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )

	       	else if ( button_status->event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
	       		if ( pfb_hdl->listing_index > 0 )
	       		{
	       			pfb_hdl->listing_index--;
	       			if (pfb_hdl->row_index > 0)
	       				pfb_hdl->row_index--;

	       		} // if ( pfb_hdl->listing_index > 0 )
	       	} // else if ( button_status->event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )

	       	else if ( button_status->event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
	       		if (!pfb_hdl->listing_index) // Being at the .. directory
	       		{
	       			pfb_hdl->info.file_is_selected = FALSE;
	       			if (pfb_hdl->dir_level) // Being at sub-directory
	       			{
	       				l = strlen(pfb_hdl->info.dir_name) - 1;
	       				k = 0;
	       				while (l >= 0 && !k)
	       				{
	       					if (pfb_hdl->info.dir_name[l]=='/')
	       						k++;
	       					pfb_hdl->info.dir_name[l] = 0;
	       					l--;
	       				} // while (l >= 0 && !k)
	       				pfb_hdl->info.dir_name = (char *)realloc(pfb_hdl->info.dir_name, l + 2);
	       				pfb_hdl->listing_index_buffer = (uint16_t *)realloc(pfb_hdl->listing_index_buffer, pfb_hdl->dir_level * sizeof(uint16_t));
	       				pfb_hdl->row_index_buffer = (uint16_t *)realloc(pfb_hdl->row_index_buffer, pfb_hdl->dir_level * sizeof(uint16_t));
	       				pfb_hdl->dir_level--;
	       			}
	       			else // Being at root directory
	       			{
	       				//pfb_hdl->listing_index_buffer[0] = 0;
	       				//pfb_hdl->row_index_buffer[0] = 0;
	       				break; // Do nothing, just stay at where it is
	       			}
	       			pfb_hdl->info.file_is_selected = FALSE;

	       			f_closedir(&directory);
	       			button_status = NULL; // Reset so that the conditional loop will be executed one more time
	       			continue;
	       		} // if (!pfb_hdl->listing_index)

	       		if (this_file.fattrib & AM_DIR)
	       		{
	       			if ( pfb_hdl->dir_level >= DIRECTORY_MAX_DEPTH_LEVEL )
	       				break; // Do nothing if it goes too deep
	       			pfb_hdl->info.dir_name = (TCHAR *)realloc(pfb_hdl->info.dir_name, strlen(pfb_hdl->info.dir_name) + 1 + 1 + strlen(this_file.fname));
	       			strcat(pfb_hdl->info.dir_name, "/");
	       			strcat(pfb_hdl->info.dir_name, this_file.fname);
	       			pfb_hdl->listing_index_buffer[pfb_hdl->dir_level] = pfb_hdl->listing_index;
	       			pfb_hdl->row_index_buffer[pfb_hdl->dir_level] = pfb_hdl->row_index;
	       			pfb_hdl->dir_level++;
	       			pfb_hdl->listing_index_buffer = (uint16_t *)realloc(pfb_hdl->listing_index_buffer, (pfb_hdl->dir_level + 1) * sizeof(uint16_t));
	       			pfb_hdl->row_index_buffer = (uint16_t *)realloc(pfb_hdl->row_index_buffer, (pfb_hdl->dir_level + 1) * sizeof(uint16_t));
	       			pfb_hdl->listing_index_buffer[pfb_hdl->dir_level] = 0;
	       			pfb_hdl->row_index_buffer[pfb_hdl->dir_level] = 0;
	       			pfb_hdl->info.file_is_selected = FALSE;

	       			f_closedir(&directory);
	       			button_status = NULL; // Reset so that the conditional loop will be executed one more time
	       			continue;
	       		} // if (this_file.fattrib & AM_DIR)

	       		else
	       		{
	       		    if (pfb_hdl->info.file_name)
	       		    	free(pfb_hdl->info.file_name);
	       		    pfb_hdl->info.file_name = (char *)calloc(strlen(this_file.fname) + 1, 1);
	       		    assert_param(pfb_hdl->info.file_name!=NULL);
	       		    if ( pfb_hdl->info.file_name )
	       		    {
	       		    	strcpy(pfb_hdl->info.file_name, this_file.fname);
	       		    	pfb_hdl->info.status = FB_OK;
	       		    	pfb_hdl->info.file_is_selected = TRUE;
	       		    } // if ( pfb_hdl->info.file_name )

	       		    f_closedir(&directory);
	       			break;
	       		}
	       	} // else if ( button_status->event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
		} // else
		// if (button_status==NULL)

    	res = f_readdir(&directory, 0);
    	if ( res != FR_OK)
    	{
    		pfb_hdl->info.status = FB_ERR_SDCARD;
    		break;
    	} // if ( res != FR_OK)

    	// Clear GUI
    	m1_u8g2_firstpage();
    	//m1_lcd_cleardisplay();

    	count = 0;
    	y_offset = 0;
       	while (count < num_of_files)
       	{
       		name[0] = 0;
   			if (count)
   			{
   				res = f_readdir(&directory, &file_info);
   				if (res || !file_info.fname[0])
   					break;

   				if ((file_info.fattrib & (AM_HID | AM_SYS))) // Hidden and System file?
   					continue;
   			}

   			if ((count >= pfb_hdl->listing_index - pfb_hdl->row_index) &&
   				(count < pfb_hdl->listing_index - pfb_hdl->row_index + gui_max_row))
   			{
   				if (!count)
   				{
   					strcpy(name, "..");
   					fb_icon = &menu_fb_icon_prev;
   				}
   				else
  				{
   					len = strlen(file_info.fname);
   					disp_max_column = gui_max_column - 1 - scroll_ok;
   					if (len <= disp_max_column)
   					{
   						strcpy(name, file_info.fname);
   					}
   					else
   					{
   						if (file_info.fattrib & AM_DIR)
   						{
   							strncpy(name, file_info.fname, disp_max_column - 2);
   							name[disp_max_column - 2] = 0;
   							strcat(name, "..");
   						}
   						else
   						{
   							flag = FALSE;
   							while (len)
   							{
   								if (file_info.fname[len-1]=='.')
   								{
   									flag = TRUE;
   									break;
   								}
   								len--;
   							}
   							if (!flag) // filename without extension
   							{
   								strncpy(name, file_info.fname, disp_max_column - 2);
   								name[disp_max_column - 2] = 0;
   								strcat(name, "..");
   							}
   							else
   							{
   								ext_len = strlen(&file_info.fname[len - 1]);
   								if ( ext_len > 4 ) // the dot (.) + extension
   									ext_len = 4;
   								if ( len > disp_max_column )
   								{
   	   								strncpy(name, file_info.fname, disp_max_column - 2 - ext_len);
   	   								name[disp_max_column - 2 - ext_len] = 0;
   	   								strcat(name, "..");
   	   								strncat(name, &file_info.fname[len - 1], ext_len);
   								}
   								else
   								{
   	   								strncpy(name, file_info.fname, disp_max_column);
   	   								name[disp_max_column] = 0;
   								}
   							} // else
   						} // else
   					} // else

       				if (file_info.fattrib & AM_DIR)
       				{
       					fb_icon = &menu_fb_icon_dir;
       				}
       				else
       				{
       					f_ext = m1_fb_get_file_type(file_info.fname);
       					if (f_ext==F_EXT_DATA)
       						fb_icon = &menu_fb_icon_data;
       					else
       						fb_icon = &menu_fb_icon_other;
       				}

       				if (count==pfb_hdl->listing_index)
       				{
       					memcpy(&this_file, &file_info, sizeof(FILINFO));
       				}
       			} // else
   					// if (!count)

   				y_offset += spacing;
   				// Draw icon of folder or file
   				u8g2_DrawXBMP(plcd_hdl, pfb_hdl->x, pfb_hdl->y + y_offset + (pfb_hdl->font_h - fb_icon->icon_h), fb_icon->icon_w, fb_icon->icon_h, fb_icon->pdata);

    			y_offset += pfb_hdl->font_h;
    			// Draw text of file name or folder name
   				u8g2_DrawStr(plcd_hdl, pfb_hdl->x + fb_icon->icon_w + 2, pfb_hdl->y + y_offset, name);
       		} // if ((count >= pfb_hdl->listing_index - pfb_hdl->row_index) &&
				// (count < pfb_hdl->listing_index - pfb_hdl->row_index + gui_max_row))
   			count++;
       	} // while (count < num_of_files)

       	// Draw a frame around the selected file/sub-directory
       	u8g2_DrawFrame(plcd_hdl, pfb_hdl->x, pfb_hdl->y + pfb_hdl->row_index * (pfb_hdl->font_h + spacing) + spacing/2,
						      pfb_hdl->x + gui_width - scroll_ok*GUI_SCROLLBAR_WIDTH + 2,
							  pfb_hdl->font_h + spacing + 2
       					);

       	// Scroll bar is active if the number of files/directories > maximum number of rows
       	if (scroll_ok)
       	{
       		// Scroll bar position
       		scroll_y = (pfb_hdl->listing_index - pfb_hdl->row_index) * (M1_LCD_DISPLAY_HEIGHT - scroll_h) / (num_of_files - gui_max_row);
       		// Scroll bar
       		u8g2_DrawFrame(plcd_hdl, pfb_hdl->x + M1_LCD_DISPLAY_WIDTH - GUI_SCROLLBAR_WIDTH, pfb_hdl->y, GUI_SCROLLBAR_WIDTH, M1_LCD_DISPLAY_HEIGHT);
       		// Scroll bar slider
       		u8g2_DrawBox(plcd_hdl, pfb_hdl->x + M1_LCD_DISPLAY_WIDTH - GUI_SCROLLBAR_WIDTH, pfb_hdl->y + scroll_y, GUI_SCROLLBAR_WIDTH, scroll_h);
       	} // if (scroll_ok)

       	m1_u8g2_nextpage(); // Now let update display RAM with contents written above

       	break; // End this loop
	} // while (1) // Not an endless loop

	return &pfb_hdl->info;

} // S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status)




/******************************************************************************/
/*
*	This function displays files and folders to the console for CLI
*
*/
/******************************************************************************/
FRESULT m1_fb_listing(const char *dir_name)
{
	char name[FF_MAX_LFN + 1];
	FRESULT res;
	DIR directory;
	FILINFO file_info, this_file;
	uint16_t num_of_files;
	uint16_t count, len;
	bool flag;

	while (1) // Not an endless loop
	{
		res = f_opendir(&directory, dir_name);
		if (res != FR_OK)
		{
			break;
		}
	    num_of_files = 1;
	    while (num_of_files < FILE_BROWSER_MAX_FILES)
	    {
	    	res = f_readdir(&directory, &file_info);
	    	if (res || !file_info.fname[0])
	    		break;
	    	if (!(file_info.fattrib & (AM_HID | AM_SYS))) // Not a system or hidden file?
	    		num_of_files++;
	    } // while (num_of_files < FILE_BROWSER_MAX_FILES)

    	res = f_readdir(&directory, 0);
    	if ( res != FR_OK)
    	{
    		break;
    	} // if ( res != FR_OK)

    	count = 0;
       	while (count < num_of_files)
       	{
       		name[0] = 0;
   			if (count)
   			{
   				res = f_readdir(&directory, &file_info);
   				if (res || !file_info.fname[0])
   					break;

   				if ((file_info.fattrib & (AM_HID | AM_SYS))) // Hidden and System file?
   					continue;
   			} // if (count)

			if (!count)
			{
				strcpy(name, "..");
			}
			else
			{
				if (strlen(file_info.fname) <= FILENAME_LEN_ON_CLI_MAX)
				{
					strcpy(name, file_info.fname);
				}
				else
				{
					if (file_info.fattrib & AM_DIR)
					{
						strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX);
						name[FILENAME_LEN_ON_CLI_MAX] = 0;
						strcat(name, "..");
					}
					else
					{
						len = strlen(file_info.fname);
						flag = FALSE;
						while (len)
						{
							if (file_info.fname[len-1]=='.')
							{
								flag = TRUE;
								break;
							}
							len--;
						}
						if (!flag || len==1)
						{
							strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX);
							name[FILENAME_LEN_ON_CLI_MAX] = 0;
							strcat(name, "..");
						}
						else
						{
							strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX - strlen(file_info.fname + len));
							strncat(name, "..", FILENAME_LEN_ON_CLI_MAX - (strlen(name) + strlen(file_info.fname + len)));
							strncat(name, file_info.fname + len, FILENAME_LEN_ON_CLI_MAX - strlen(name));
						}
					} // else
				} // else
   			} // else
					// if (!count)
			M1_LOG_N(M1_LOGDB_TAG, "%s\r\n", name);
   			count++;
       	} // while (count < num_of_files)
       	break; // End this loop
	} // while (1) // Not an endless loop

	return res;
} // FRESULT m1_fb_listing(const char *dir_name)



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
void m1_fb_deinit(void)
{
	if (pfb_hdl)
	{
		if (pfb_hdl->listing_index_buffer)
			free(pfb_hdl->listing_index_buffer);

		if (pfb_hdl->row_index_buffer)
			free(pfb_hdl->row_index_buffer);

		if (pfb_hdl->info.dir_name)
			free(pfb_hdl->info.dir_name);

		if (pfb_hdl->info.file_name)
			free(pfb_hdl->info.file_name);

		free(pfb_hdl);
		pfb_hdl = NULL;
	} // if (pfb_hdl)
} // void m1_fb_deinit(void)



/******************************************************************************/
/*
*	This function dynamically concatenates number of strings given by num
*	Each string is separated by the separator symbol given in separators
*	Return: 1 on success, 0 if the resulting string is empty
*/
/******************************************************************************/
uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...)
{
	va_list pargs;
	size_t len;
	uint8_t k;
	char *tmp_buffer;

	assert(num >= 1);

	va_start(pargs, format);
	len = 0;
	k = num;
	while (k)
	{
		tmp_buffer = va_arg(pargs, char *);
		len += strlen(tmp_buffer);
		k--;
	} // while (k)

	if ( num > 1 )
		len += num - 1 ; // Add separator symbol to the end of each string

	if ( !len )
		return 0;

	va_start(pargs, format);
	k = num;
	strcpy(buffer, "");
	while (k)
	{
		tmp_buffer = va_arg(pargs, char *);
		if (strlen(tmp_buffer))
		{
			strcat(buffer, tmp_buffer);
			if (k != 1) // Not the last string?
				strcat(buffer, "/"); // Add separator symbol to the end of each string
		}
		k--;
	} // while (k)
	va_end(pargs);

	return 1;
} // uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...)

