/* See COPYING.txt for license details. */

/*
*
* m1_fw_update.c
*
* Firmware update functionality
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_fw_update.h"
#include "m1_fw_update_bl.h"
#include "m1_storage.h"
#include "m1_power_ctl.h"
#include "m1_usb_cdc_msc.h"

/*************************** D E F I N E S ************************************/

#define FLASH_BL_ADDRESS	0x08000000
#define FLASH_SYSTEM_MEMORY_ADDRESS	0x0BF97000

#define M1_LOGDB_TAG		"FW-UPDATE"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

//************************** S T R U C T U R E S *******************************


/***************************** V A R I A B L E S ******************************/

typedef void (*pFunction)(void);

static char *pfullpath = NULL;
static uint8_t fw_update_status = M1_FW_UPDATE_NOT_READY;
static uint32_t fw_version_new;
static FIL hfile_fw;
static S_M1_file_info *f_info = NULL;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void firmware_update_init(void);
void firmware_update_exit(void);
void firmware_update_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void firmware_update_get_image_file(void);
void firmware_update_start(void);
static void firmware_update_info_box(uint8_t sel_item);
static bool firmware_update_usb_dfu_confirm(void);
void firmware_update_usb_dfu_mode(void);
void firmware_update_enter_usb_dfu(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes display for this sub-menu item.
 */
/*============================================================================*/
void firmware_update_init(void)
{
	if ( !pfullpath )
		pfullpath = malloc(FW_FILE_PATH_LEN_MAX + FW_FILE_NAME_LEN_MAX);
	fw_update_status = M1_FW_UPDATE_NOT_READY;
} // void firmware_update_init(void)



/*============================================================================*/
/*
 * This function will exit this sub-menu and return to the upper level menu.
 */
/*============================================================================*/
void firmware_update_exit(void)
{
	if ( pfullpath )
	{
		free(pfullpath);
		pfullpath = NULL;
	}
} // void firmware_update_exit(void)




/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void firmware_update_start(void)
{
	uint8_t uret, old_op_mode;

	uret = M1_FW_UPDATE_NOT_READY;
	if ( !m1_check_battery_level(50) ) // Is battery level less than 50%?
    {
		fw_update_status = M1_FW_UPDATE_NOT_READY; // Force quit
    	uret = M1_FW_UPDATE_LOW_BATTERY;
    } // if ( !m1_check_battery_level(50) )

	old_op_mode = m1_device_stat.op_mode;

	do
    {
        if ( fw_update_status != M1_FW_UPDATE_READY )
        {
        	break;
        }

        //
		// - Check for battery status before proceeding
		//

        m1_device_stat.op_mode = M1_OPERATION_MODE_FIRMWARE_UPDATE;

        m1_led_fw_update_on(NULL); // Turn on
    	startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_FW_UPDATE_ACTIVE);

    	uret = bl_flash_app(&hfile_fw);

    	m1_led_fw_update_off(); // Turn off

    	if ( uret != BL_CODE_OK )
		{
			uret = M1_FW_UPDATE_FAILED;
			startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_NO_OP);
		}
		else
		{
			uret = M1_FW_UPDATE_SUCCESS;
	    	startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_FW_UPDATE_COMPLETE);
		}
    } while (0);

    if ( fw_update_status==M1_FW_UPDATE_READY )
    	m1_fb_close_file(&hfile_fw);

    fw_update_status = uret; // Update new status
	if ( fw_update_status==M1_FW_UPDATE_SUCCESS )
	{
		// Display warning message: device will reboot in n seconds...
		bl_swap_banks();
	} // if ( fw_update_status==M1_FW_UPDATE_SUCCESS )

	m1_device_stat.op_mode = old_op_mode;

    xQueueReset(main_q_hdl); // Reset main q before return
} // void firmware_update_start(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void firmware_update_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)
{
	uint8_t i, n_items;
	uint8_t menu_text_y;
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

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

    	switch ( sel_item )
    	{
    		case 0: // Image file
    	    	if ( f_info && f_info->file_is_selected )
    	    	{
    	    		if ( strlen(f_info->file_name) > GUI_DISP_LINE_LEN_MAX )
    	    		{
    	    			strncpy(prn_name, f_info->file_name, GUI_DISP_LINE_LEN_MAX - 2);
    	    			prn_name[GUI_DISP_LINE_LEN_MAX - 2] = 0;
    	    			strcat(prn_name, "..");
    	    		}
    	    		else
    	    		{
    	    			strcpy(prn_name, f_info->file_name);
    	    		}
    	    	} // if ( f_info && f_info->file_is_selected )

    	    	switch (fw_update_status)
    	    	{
    	    		case M1_FW_UPDATE_READY:
    	    			m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
        				sprintf(prn_name, "New ver. %d.%d.%d.%d", (uint8_t)(fw_version_new>>24), (uint8_t)(fw_version_new>>16), (uint8_t)(fw_version_new>>8), (uint8_t)fw_version_new);
        				m1_info_box_display_draw(INFO_BOX_ROW_2, prn_name);
        				sprintf(prn_name, "Old ver. %d.%d.%d.%d", m1_device_stat.config.fw_version_major, m1_device_stat.config.fw_version_minor, m1_device_stat.config.fw_version_build, m1_device_stat.config.fw_version_rc);
        				m1_info_box_display_draw(INFO_BOX_ROW_3, prn_name);
    	    			break;

    	    		case M1_FW_IMAGE_FILE_ACCESS_ERROR:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "Image access error!");
        				break;

    	    		case M1_FW_IMAGE_FILE_TYPE_ERROR:
        			case M1_FW_IMAGE_SIZE_INVALID:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "Invalid image file!");
        				break;

        			case M1_FW_VERSION_ERROR:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "Image version error!");
        				break;

        			case M1_FW_ISM_BAND_REGION_ERROR:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "ISM region error!");
        				break;

        			case M1_FW_CRC_CHECKSUM_UNMATCHED:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "CRC failed!");
        				break;

        			default:
        				break;
    	    	} // switch (fw_update_status)
    			break;

    		case 1: // Firmware update
		    	switch (fw_update_status)
		    	{
		    		case M1_FW_UPDATE_READY:
		    			m1_info_box_display_draw(INFO_BOX_ROW_1, "Ready to flash!");
		    			break;

		    		case M1_FW_UPDATE_SUCCESS:
		    			m1_info_box_display_draw(INFO_BOX_ROW_1, "Update successfully!");
		    			fw_update_status = M1_FW_UPDATE_NOT_READY; // Reset after process complete
		    			f_info->file_is_selected = false; // Reset
		    			// Display warning message: device will reboot in n seconds...
		    			break;

		    		case M1_FW_UPDATE_FAILED:
		    			m1_info_box_display_draw(INFO_BOX_ROW_1, "Update failed!");
		    			break;

        			case M1_FW_UPDATE_LOW_BATTERY:
        				m1_info_box_display_draw(INFO_BOX_ROW_1, "Battery level < 50%!");
        				break;

		    		default:
		    			break;
			    	} // switch (fw_update_status)
	    		break;

	    	case 2: // USB DFU mode
				m1_info_box_display_draw(INFO_BOX_ROW_1, "ROM bootloader mode");
				m1_info_box_display_draw(INFO_BOX_ROW_2, "Use dfu-util/CubeProg");
				m1_info_box_display_draw(INFO_BOX_ROW_3, "OK/RIGHT: enter");
				break;

	    	default: // Unknown selection
	    		break;
	    	} // switch ( sel_item )
    } while (m1_u8g2_nextpage());

} // void firmware_update_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void firmware_update_get_image_file(void)
{
	uint8_t uret, ext, fret;
    uint8_t fw_payload[FW_IMAGE_CHUNK_SIZE];
    size_t count, sum;
    uint32_t crc32ret, image_size, fwver_old;
    S_M1_FW_CONFIG_t fwconfig;

	f_info = storage_browse();

	fw_update_status = M1_FW_IMAGE_FILE_TYPE_ERROR; // reset
	if ( f_info->file_is_selected )
	{
		do
		{
			uret = strlen(f_info->file_name);
			if ( !uret )
				break;
			ext = 0;
			while ( uret )
			{
				ext++;
				if ( f_info->file_name[--uret]=='.' ) // Find the dot
					break;
			} // while ( uret )
			if ( !uret || (ext!=4) ) // ext==length of '.bin'
				break;
			if ( strcmp(&f_info->file_name[uret], ".bin" ))
				break;
			fw_update_status = M1_FW_UPDATE_READY;
		} while (0);
	} // if ( f_info->file_is_selected )

	uret = M1_FW_UPDATE_NOT_READY;
	do
    {
        if ( fw_update_status != M1_FW_UPDATE_READY )
        {
        	break;
        }

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
		// Draw box with background color to clear the area for the hourglass icon
		u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 28, THIS_LCD_MENU_TEXT_FIRST_ROW_Y + THIS_LCD_MENU_TEXT_ROW_SPACE + 2, 24, THIS_LCD_MENU_TEXT_ROW_SPACE);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
    	u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 24, 16, 18, 32, hourglass_18x32); // Draw icon
    	m1_u8g2_nextpage(); // Update display RAM

        uret = m1_fb_dyn_strcat(pfullpath, 2, "",  f_info->dir_name, f_info->file_name);
        uret = m1_fb_open_file(&hfile_fw, pfullpath);
		if ( !uret )
		{
			image_size = f_size(&hfile_fw);
		    // Both the address and image size must be aligned to 4 bytes
		    if ( (!image_size) || (image_size % 4 != 0) || (image_size > (FW_IMAGE_SIZE_MAX + FW_IMAGE_CRC_SIZE)) )
		    {
		    	uret = M1_FW_IMAGE_SIZE_INVALID;
		    	break;
		    } // if ( (!image_size) || (image_size % 4 != 0) || (image_size > (FW_IMAGE_SIZE_MAX + FW_IMAGE_CRC_SIZE) )

		    // Call the function to initialize this transaction.
		    // Do not take the return value in this case. Input data can be anything.
		    bl_get_crc_chunk(&crc32ret, 0, true, false); // CRC init
			sum = image_size;
			while ( sum )
			{
				count = m1_fb_read_from_file(&hfile_fw, fw_payload, FW_IMAGE_CHUNK_SIZE);
				if ( !count || (count % 4 != 0) ) // Read failed?
				{
					uret = M1_FW_IMAGE_FILE_ACCESS_ERROR;
					break;
				} // if ( !count || (count % 4 != 0) )
				sum -= count;
				if ( count < FW_IMAGE_CHUNK_SIZE ) // possible last chunk?
				{
					count -= FW_IMAGE_CRC_SIZE; // exclude CRC data at the end
					if ( count )
						crc32ret = bl_get_crc_chunk((uint32_t *)fw_payload, count/FW_IMAGE_CRC_SIZE, false, true); // complete
					else // last chunk is the appended CRC data
					{
						// Call the function to complete the transaction.
						// Do not take the return value in this case. Input data can be anything.
						bl_get_crc_chunk(&crc32ret, 1, false, true); // complete
					}
					break; // exit
				} // if ( count < FW_IMAGE_CHUNK_SIZE )
				else
				{
					if ( !sum ) // end of file?
					{
						count -= FW_IMAGE_CRC_SIZE; // exclude CRC data at the end
						crc32ret = bl_get_crc_chunk((uint32_t *)fw_payload, count/FW_IMAGE_CRC_SIZE, false, true); // complete
						break; // exit
					} // if ( !sum )
					else
						crc32ret = bl_get_crc_chunk((uint32_t *)fw_payload, count/FW_IMAGE_CRC_SIZE, false, false);
				} // else
			} // while ( sum )

			if ( !sum )
			{
			    // Compare CRC here
			    uret = memcmp(&fw_payload[count], &crc32ret, FW_IMAGE_CRC_SIZE);
			    if ( uret )
			    {
			    	uret = M1_FW_CRC_CHECKSUM_UNMATCHED;
			    	break;
			    }
			} // if ( !sum )
			else
			{
				uret = M1_FW_IMAGE_FILE_ACCESS_ERROR;
				break;
			}

			// Check for fw version here
			//M1_FW_VERSION_ERROR,
			fret = f_lseek(&hfile_fw, FW_START_ADDRESS ^ FW_CONFiG_ADDRESS); // Move file pointer to the config data
			if ( fret != FR_OK )
			{
				uret = M1_FW_IMAGE_FILE_ACCESS_ERROR;
				break;
			} // if ( fret != FR_OK )
			count = m1_fb_read_from_file(&hfile_fw, (char *)&fwconfig, sizeof(S_M1_FW_CONFIG_t));
			if ( count != sizeof(S_M1_FW_CONFIG_t) )
			{
				uret = M1_FW_IMAGE_FILE_ACCESS_ERROR;
				break;
			}
			fwver_old = *(uint32_t *)&m1_device_stat.config.fw_version_rc;
			fw_version_new = *(uint32_t *)&fwconfig.fw_version_rc;
		} // if ( !uret )
		else
		{
			uret = M1_FW_IMAGE_FILE_ACCESS_ERROR;
			break;
		}
    } while (0);

    if ( fw_update_status==M1_FW_UPDATE_READY )
    {
    	if ( uret ) // error?
    	{
    		m1_fb_close_file(&hfile_fw);
    		fw_update_status = uret;
    	} // if ( uret )
    } // if ( fw_update_status==M1_FW_UPDATE_READY )

	xQueueReset(main_q_hdl); // Reset main q before return

} // void firmware_update_get_image_file(void)


/*============================================================================*/
/*
 * Confirm USB DFU mode entry from keypad.
 */
/*============================================================================*/
static bool firmware_update_usb_dfu_confirm(void)
{
	S_M1_Main_Q_t q_item;
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	m1_u8g2_firstpage();
	do
	{
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 4, 12, "Enter USB DFU mode?");
		u8g2_DrawStr(&m1_u8g2, 4, 26, "RIGHT/OK: Yes");
		u8g2_DrawStr(&m1_u8g2, 4, 38, "LEFT/BACK: No");
		u8g2_DrawStr(&m1_u8g2, 4, 52, "Device will reboot");
	} while (m1_u8g2_nextpage());

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE)
			continue;
		if (q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if (ret != pdTRUE)
			continue;

		if ((this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) ||
			(this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK))
		{
			xQueueReset(main_q_hdl);
			return true;
		}

		if ((this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) ||
			(this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK))
		{
			xQueueReset(main_q_hdl);
			return false;
		}
	}
} // static bool firmware_update_usb_dfu_confirm(void)


/*============================================================================*/
/*
 * This function requests USB DFU mode through reboot.
 */
/*============================================================================*/
void firmware_update_usb_dfu_mode(void)
{
	if (!firmware_update_usb_dfu_confirm())
	{
		M1_LOG_I(M1_LOGDB_TAG, "USB DFU mode canceled by user\r\n");
		return;
	}

	startup_config_write(BK_REGS_SELECT_DEV_OP_STAT, DEV_OP_STATUS_USB_DFU_REQUEST);
	M1_LOG_I(M1_LOGDB_TAG, "USB DFU mode requested. Rebooting...\r\n");
	vTaskDelay(pdMS_TO_TICKS(50));
	NVIC_SystemReset();
} // void firmware_update_usb_dfu_mode(void)


/*============================================================================*/
/*
 * This function jumps to STM32 system memory bootloader (USB DFU).
 */
/*============================================================================*/
void firmware_update_enter_usb_dfu(void)
{
	uint32_t jump_address;
	pFunction jump_to_boot;
	uint32_t idx;

	M1_LOG_I(M1_LOGDB_TAG, "Entering USB DFU mode...\r\n");

	if ((*(uint32_t *)FLASH_SYSTEM_MEMORY_ADDRESS & 0x2FFE0000U) != 0x20000000U)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Invalid system bootloader vector table\r\n");
		return;
	}

	USBD_Stop(&hUsbDeviceFS);
	USBD_DeInit(&hUsbDeviceFS);
	HAL_PCD_DeInit(&hpcd_USB_DRD_FS);

	HAL_RCC_DeInit();

	__disable_irq();

	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	for (idx = 0; idx < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); idx++)
	{
		NVIC->ICER[idx] = 0xFFFFFFFF;
		NVIC->ICPR[idx] = 0xFFFFFFFF;
	}

	jump_address = *(uint32_t *)(FLASH_SYSTEM_MEMORY_ADDRESS + 4U);
	jump_to_boot = (pFunction)jump_address;

	SCB->VTOR = FLASH_SYSTEM_MEMORY_ADDRESS;
	__DSB();
	__ISB();

	__set_CONTROL(0x00U);
	__set_PSP(0x00U);
	__set_MSP(*(uint32_t *)FLASH_SYSTEM_MEMORY_ADDRESS);

	jump_to_boot();
} // void firmware_update_enter_usb_dfu(void)



/*============================================================================*/
/*
 * This function will jump to the bootloader
 */
/*============================================================================*/
void firmware_update_JumpTo_BL(void)
{
	uint32_t Jump_Address, Bl_Signature;
	pFunction Jump_To_Bl;
	uint8_t i;

	M1_LOG_I(M1_LOGDB_TAG, "%s", "\nBL App running");

	Bl_Signature = *(uint32_t *)FLASH_BL_ADDRESS;

	// The first value stored in the vector table is the reset value of the stack pointer
	if ( (*(uint32_t *)FLASH_BL_ADDRESS & 0x2FFE0000)==0x20000000 )
	{
		//PrintDebugMsg("\nBootloader starts...");
		Jump_Address = *(uint32_t *)(FLASH_BL_ADDRESS + 4);
		Jump_To_Bl = (pFunction)Jump_Address;

//		HAL_Delay(100); // wait for the PrintDebugMsg() to complete

		// To-Do list before jumping to the Bl
		// - Disable all interrupts
		// - Restore all settings
		// - Reset SysTick

		/* Disable all interrupts */
		__disable_irq();
		//HAL_NVIC_DisableIRQ(IRQn)

		/* Disable Systick timer */
		SysTick->CTRL = 0;

		/* Set the clock to the default state */
		HAL_RCC_DeInit();

		HAL_DeInit();

		/* Clear Interrupt Enable Register & Interrupt Pending Register */
		for (i=0; i<8; i++)
		{
			NVIC->ICER[i] = 0xFFFFFFFF;
			NVIC->ICPR[i] = 0xFFFFFFFF;
		}

		/* Re-enable all interrupts */
		__enable_irq();
/*
		__asm volatile(
				" mov r0, #0			\n"
				" msr msp, r0			\n" // Reset Main Stack Pointer
				" msr psp, r0			\n" // Reset Process Stack Pointer
				" msr control, r0		\n" // Reset Control register
		);
*/
		__set_CONTROL(0x00);
		__set_PSP(0x0);
		__set_MSP(*(uint32_t *)FLASH_BL_ADDRESS);

		Jump_To_Bl();
	}
	else
	{
		M1_LOG_I(M1_LOGDB_TAG, "%s", "\nNo Bootloader found");
	}
} // void firmware_update_JumpTo_BL(void)



/*============================================================================*/
/*
 * This function will disable the watchdog timer
 */
/*============================================================================*/
void firmware_update_IWDG_disable(void)
{
	// Disable IWDG - Start
	HAL_FLASH_Unlock(); // Unlock the FLASH control registers access
	HAL_FLASH_OB_Unlock(); // Unlock the FLASH Option Control Registers access

	// Initialize the Option Bytes structure
	FLASH_OBProgramInitTypeDef OBInit;
	HAL_FLASHEx_OBGetConfig(&OBInit); // Get the Option byte configuration

	// Set the IWDG_SW bit
	OBInit.OptionType = OPTIONBYTE_USER; // USER option byte configuration
	OBInit.USERType = OB_USER_IWDG_STDBY; // Independent watchdog counter freeze in standby mode
	OBInit.USERConfig = OB_IWDG_STDBY_FREEZE; // IWDG counter frozen in STANDBY mode

	// Write the Option Bytes
	HAL_FLASHEx_OBProgram(&OBInit); // Program option bytes

	HAL_FLASH_OB_Launch(); // Launch the option bytes loading.

	HAL_FLASH_OB_Lock(); // Lock the FLASH Option Control Registers access.
	HAL_FLASH_Lock(); // Locks the FLASH control registers access
	// Disable IWDG - End
} // void firmware_update_IWDG_disable(void)
