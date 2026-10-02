/* See COPYING.txt for license details. */

/*
*
* m1_fw_update_bl.c
*
* Firmware update support functions
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
#include "m1_watchdog.h"
#include "m1_fw_update.h"
#include "m1_fw_update_bl.h"
#include "m1_sub_ghz.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG		"FW-BL"

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {
	.magic_number_1 = FW_CONFIG_MAGIC_NUMBER_1,
	.fw_version_rc = FW_VERSION_RC,
	.fw_version_build = FW_VERSION_BUILD,
	.fw_version_minor = FW_VERSION_MINOR,
	.fw_version_major = FW_VERSION_MAJOR,
	.user_option_1 = 0,
	.User_option_2 = 0,
	.ism_band_region = SUBGHZ_ISM_BAND_REGION,
	.magic_number_2 = FW_CONFIG_MAGIC_NUMBER_2
};
// 4 bytes CRC32 will be manually added here with the tool srec_cat
// Example:
// 000FFC00:
// 000FFC10: magic_number_2 CRC32
// 000FFC20:

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t bl_crc_check(uint32_t image_size);
uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk);
void bl_swap_banks(void);
static uint16_t bl_flash_if_init(void);
static uint16_t bl_flash_if_deinit(void);
static uint8_t bl_flash_start(uint32_t image_size);
void fw_gui_progress_update(size_t remainder);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t bl_crc_check(uint32_t image_size)
{
    CRC_HandleTypeDef crchdl;
    uint32_t result, crc32;

    __HAL_RCC_CRC_CLK_ENABLE();
    crchdl.Instance                     = CRC;
    crchdl.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
    crchdl.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
    crchdl.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
    crchdl.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    crchdl.InputDataFormat              = CRC_INPUTDATA_FORMAT_WORDS;
    if(HAL_CRC_Init(&crchdl) != HAL_OK)
    {
        return BL_CODE_CHK_ERROR;
    }

    result = HAL_CRC_Calculate(&crchdl, (uint32_t*)(FW_START_ADDRESS + M1_FLASH_BANK_SIZE), image_size);

    HAL_CRC_DeInit(&crchdl);

    __HAL_RCC_CRC_FORCE_RESET();
    __HAL_RCC_CRC_RELEASE_RESET();

    crc32 = *(uint32_t*)(FW_CRC_ADDRESS + M1_FLASH_BANK_SIZE);

    if ( crc32==result )
    {
        return BL_CODE_OK;
    }

	 M1_LOG_E(M1_LOGDB_TAG, "crc32: 0x%X, cal_crc32: 0x%X, 32-bit image size: %ld\r\n", crc32, result, image_size);

    return BL_CODE_CHK_ERROR;
} // uint8_t bl_crc_check(uint32_t image_size)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk)
{
    static CRC_HandleTypeDef crchdl;
    uint32_t result;

    if (crc_init)
    {
    	__HAL_RCC_CRC_CLK_ENABLE();
    	crchdl.Instance                     = CRC;
    	crchdl.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
    	crchdl.Init.CRCLength				= CRC_POLYLENGTH_32B;
    	crchdl.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
    	crchdl.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;//CRC_INPUTDATA_INVERSION_WORD;
    	crchdl.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;//CRC_OUTPUTDATA_INVERSION_ENABLE;
    	crchdl.InputDataFormat              = CRC_INPUTDATA_FORMAT_WORDS;
    	if(HAL_CRC_Init(&crchdl) != HAL_OK)
    	{
    		return BL_CODE_CHK_ERROR;
    	}
    	result = HAL_CRC_Calculate(&crchdl, data_scr, 0);
    } // if (crc_init)
    else
    {
        result = HAL_CRC_Accumulate(&crchdl, data_scr, len);
        if ( last_chunk )
        {
    	    HAL_CRC_DeInit(&crchdl);
    	    __HAL_RCC_CRC_FORCE_RESET();
    	    __HAL_RCC_CRC_RELEASE_RESET();
        } // if ( last_chunk )
    } // else

    return result;
} // uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk)



/*============================================================================*/
/**
  * @brief  Get the active bank
  * @param  None
  * @retval The active bank.
  */
/*============================================================================*/
uint16_t bl_get_active_bank(void)
{
	FLASH_OBProgramInitTypeDef OBInit;

	/* Get the boot configuration status */
    HAL_FLASHEx_OBGetConfig(&OBInit);

    /* Check Swap Flash banks  status */
    if ((OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE)
    {
    	/*Active Bank is bank 1 */
    	M1_LOG_I(M1_LOGDB_TAG, "Active Bank 1\r\n");
    	return BANK1_ACTIVE;
     }
     else
     {
    	 /*Active Bank is bank 2 */
    	 M1_LOG_I(M1_LOGDB_TAG, "Active Bank 2\r\n");
    	 return BANK2_ACTIVE;
     }
} // uint16_t bl_get_active_bank(void)



/*============================================================================*/
/**
  * @brief  Initializes Memory.
  * @param  None
  * @retval 0 if operation is successful, MAL_FAIL else.
  */
/*============================================================================*/
static uint16_t bl_flash_if_init(void)
{

	/* Disable instruction cache prior to internal cacheable memory update */
	// if (HAL_ICACHE_Disable() != HAL_OK)
	// {
	//   	Error_Handler();
	// }
	/* Unlock the internal flash */
	HAL_FLASH_Unlock();

	return 0;
} // static uint16_t bl_flash_if_init(void)



/*============================================================================*/
/**
  * @brief  De-Initializes Memory.
  * @param  None
  * @retval 0 if operation is successeful, MAL_FAIL else.
  */
/*============================================================================*/
static uint16_t bl_flash_if_deinit(void)
{
	/* Lock the internal flash */
	HAL_FLASH_Lock();

	return 0;
} // static uint16_t bl_flash_if_deinit(void)







/*============================================================================*/
/**
  * @brief  Writes Data into Memory.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be written (in bytes).
  * @retval 0 if operation is successeful, MAL_FAIL else.
  */
/*============================================================================*/
uint8_t bl_flash_if_write(uint8_t *src, uint8_t *dest, uint32_t len)
{
	uint16_t i, j;
	uint32_t *src_chk, *dst_chk;

	src_chk = (uint32_t *)src;
	dst_chk = (uint32_t *)dest;
	for (i=0; i<len; i+=16)
	{
	    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, (uint32_t)dst_chk,
	    		(uint32_t)src_chk)==HAL_OK)
	    {
	    	for (j=0; j<4; j++)
	    	{
	    		/* Check the written value */
	    		if (*src_chk != *dst_chk)
	    		{
	    			/* Flash content doesn't match SRAM content */
	    			return BL_CODE_CHK_ERROR;
	    		}
	    		src_chk++;
	    		dst_chk++;
	    	} // for (j=0; j<4; j++)
	    } // if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
	    else
	    {
	    	/* Error occurred while writing data in Flash memory */
	    	return BL_WRITE_ERROR;
	    }
	} // for (i=0; i<len; i+=16)

	return BL_CODE_OK;
} // uint8_t bl_flash_if_write(uint8_t * src, uint8_t * dest, uint32_t Len)












/*============================================================================*/
/**
  * @brief  Swap the banks
  * @param  None
  * @retval None
  */
/*============================================================================*/
void bl_swap_banks(void)
{
	FLASH_OBProgramInitTypeDef OBInit;

	/* Unlock the User Flash area */
	HAL_FLASH_Unlock();

	HAL_FLASH_OB_Unlock();
    /* Get the boot configuration status */
    HAL_FLASHEx_OBGetConfig(&OBInit);

    /* Check Swap Flash banks status */
    if ( (OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE )
    {
    	/*Swap to bank2 */
    	M1_LOG_I(M1_LOGDB_TAG, "Swap to bank2\n\r");
    	/*Set OB SWAP_BANK_OPT to swap Bank2*/
    	OBInit.USERConfig = OB_SWAP_BANK_ENABLE;
     } // if ( (OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE )
     else
     {
    	 /* Swap to bank1 */
    	 M1_LOG_I(M1_LOGDB_TAG, "Swap to bank1\n\r");
    	 /*Set OB SWAP_BANK_OPT to swap Bank1*/
    	 OBInit.USERConfig = OB_SWAP_BANK_DISABLE;
     } // else

	 OBInit.OptionType = OPTIONBYTE_USER;
	 OBInit.USERType = OB_USER_SWAP_BANK;
	 HAL_FLASHEx_OBProgram(&OBInit);

	 /* Launch Option bytes loading */
	 HAL_FLASH_OB_Launch();

	 vTaskDelay(pdMS_TO_TICKS(200));
	 /* Reset the MCU */
	 HAL_NVIC_SystemReset();
} // void bl_swap_banks(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
static uint8_t bl_flash_start(uint32_t image_size)
{
	//uint32_t startsector = 0;
	uint32_t sectornb = 0;
	/* Variable contains Flash operation status */
	HAL_StatusTypeDef status;
	FLASH_EraseInitTypeDef EraseInitStruct;
	uint16_t n_sectors;
	uint8_t i;

	/*
	 An STM32H5 crashing when accessing the FLASH_BANK_SIZE register is typically caused by
	 accessing unwritten memory, triggering an Error Correction Code (ECC) fault.
	 Accessing this unwritten memory causes a Double Error Detection (DED), which raises a Non-Maskable Interrupt (NMI).
	 Solution:
	 Read the Flash ECC Data Register (FLASH_ECCDR): Inside the NMI handler, check the FLASH_ECCDR to identify the nature of the ECC error.
	 uint32_t eccdr_value = FLASH->ECCDR; // Read ECCDR to clear the NMI NMI_Handler
	 */
	if ( image_size > M1_FLASH_BANK_SIZE/*FLASH_BANK_SIZE*/ )
		return BL_CODE_SIZE_ERROR;

	n_sectors = (image_size + (FLASH_SECTOR_SIZE - 1))/FLASH_SECTOR_SIZE;
	if (!n_sectors)
		return BL_CODE_SIZE_ERROR;

	EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;

	// we want to erase the other bank than the active one
	if (bl_get_active_bank()==BANK1_ACTIVE)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Erase sectors in Bank 2\r\n");
		EraseInitStruct.Banks = FLASH_BANK_2;
	}
	else
	{
		M1_LOG_I(M1_LOGDB_TAG, "Erase sectors in Bank 1\r\n");
		EraseInitStruct.Banks = FLASH_BANK_1;
	}

	EraseInitStruct.NbSectors = 1;
	for (i=0; i<n_sectors; i++)
	{
		m1_wdt_reset();
		EraseInitStruct.Sector = i;
		status = HAL_FLASHEx_Erase(&EraseInitStruct, &sectornb);
		if (status != HAL_OK)
			break;
	} // for (i=0; i<n_sectors; i++)

	if (status != HAL_OK)
		return BL_CODE_ERASE_ERROR;

	return BL_CODE_OK;
} // static uint8_t bl_flash_start(uint32_t image_size)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
/* File-scope (was: function-local `static`) so bl_flash_stream_reset() can
 * force them back to their pre-BEGIN state -- see that function's own doc
 * comment in m1_fw_update_bl.h for why that matters. */
static uint32_t s_bl_write_acc;
static uint8_t *s_bl_flash_add;
static bool     s_bl_init_done = false;

void bl_flash_stream_reset(void)
{
    s_bl_init_done = false;
    s_bl_write_acc = 0;
    s_bl_flash_add = NULL;
}

static uint8_t bl_flash_binary(uint8_t *payload, size_t size)
{
    uint8_t err;

    if ( !s_bl_init_done )
    {
        M1_LOG_I(M1_LOGDB_TAG, "Start flashing...\r\n");
        s_bl_flash_add = (uint8_t *)FW_START_ADDRESS;
        s_bl_flash_add += M1_FLASH_BANK_SIZE; // It should always write to Bank 2 destination
        s_bl_write_acc = 0;
        s_bl_init_done = true;
    } // if ( !s_bl_init_done )

    if ( size )
    {
        err = bl_flash_if_write(payload, s_bl_flash_add, size);
        if (err != BL_CODE_OK)
        {
            M1_LOG_I(M1_LOGDB_TAG, "Writing flash error at 0x%X.\r\n", s_bl_flash_add);
            return err;
        }
        s_bl_write_acc += size;
        s_bl_flash_add += size;
        return BL_CODE_OK;
    } // if ( size )

    M1_LOG_I(M1_LOGDB_TAG, "\r\nFlashing completed!\r\n");
    s_bl_init_done = false; // reset

    /* An image smaller than its own trailing CRC can't be valid -- reject it
     * here rather than letting the subtraction below underflow s_bl_write_acc
     * (uint32_t) into a huge word count, which bl_crc_check() would then hand
     * straight to HAL_CRC_Calculate() as a read length far past the flash
     * bank (memory fault / garbage compared against whatever 4 bytes happen
     * to sit at FW_CRC_ADDRESS). Streamed callers can reach this with a tiny
     * client-declared image_size (the M1CP coordinator only rejects
     * image_size == 0, not 1..FW_IMAGE_CRC_SIZE-1). */
    if ( s_bl_write_acc < FW_IMAGE_CRC_SIZE )
    {
        M1_LOG_I(M1_LOGDB_TAG, "Image too small to contain its own CRC (%lu bytes).\r\n",
                 (unsigned long)s_bl_write_acc);
        return BL_CODE_SIZE_ERROR;
    }

    s_bl_write_acc -= FW_IMAGE_CRC_SIZE; // exclude the CRC at the end of the image file
    s_bl_write_acc /= 4; // convert image size from byte to word (32-bit)
    err = bl_crc_check(s_bl_write_acc);

	if (err != BL_CODE_OK)
    {
        M1_LOG_I(M1_LOGDB_TAG, "CRC not matched.\r\n");
        return err;
    }
    M1_LOG_I(M1_LOGDB_TAG, "Flash verified.\r\n");

    return BL_CODE_OK;
} // static uint8_t bl_flash_binary(uint8_t *payload, size_t size)




/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
uint8_t bl_flash_app(FIL *hfile)
{
    uint8_t fw_payload[FW_IMAGE_CHUNK_SIZE];
	uint8_t flash_err;
	uint16_t count;
	size_t write_size;

	f_lseek(hfile, 0); // Move file pointer to the beginning of the file
	write_size = f_size(hfile); // Get image size
    M1_LOG_I(M1_LOGDB_TAG, "Erasing flash...\r\n");
	// Display glass hour here

    bl_flash_if_init();
    flash_err = bl_flash_start(write_size);
    // Clear glass hour
    if ( flash_err != BL_CODE_OK )
    {
    	M1_LOG_I(M1_LOGDB_TAG, "Failed\r\n");
    	write_size = 0; // Set end condition
    }

    while ( write_size )
	{
        m1_wdt_reset();

        flash_err = BL_CODE_CHK_ERROR;
		count = m1_fb_read_from_file(hfile, fw_payload, FW_IMAGE_CHUNK_SIZE);
		if ( !count || (count % 4 != 0) ) // Read failed?
			break;

		fw_gui_progress_update(write_size);

		write_size -= count;
		if ( count )
		{
			flash_err = bl_flash_binary(fw_payload, count);
			if ( flash_err != BL_CODE_OK )
				break;
		} // if ( count )

		if (!write_size)
		{
			// Last step to verify CRC
			flash_err = bl_flash_binary(NULL, 0);
		} // if (!write_size)
	} // while ( write_size )

    bl_flash_if_deinit();

	if ( write_size || (flash_err != BL_CODE_OK) )
	{
		; // Display error here
	}

	return flash_err;
} // uint8_t bl_flash_app(FIL *hfile)



/******************************************************************************/
/**
  * @brief  Streamed programming wrappers for the M1CP firmware-update path.
  *         These reuse the exact SD-path engine (same calls, same order) but are
  *         driven by streamed chunks instead of a FatFs file, so no on-device SD
  *         staging is needed. Engine logic is unchanged.
  * @retval BL_CODE_OK on success, else an S_M1_BL_CODES_t error.
  */
/******************************************************************************/
uint8_t bl_flash_stream_begin(uint32_t image_size)
{
	/* Force the write cursor back to its pre-BEGIN state FIRST, in case the
	 * previous attempt (if any) was aborted or failed before its own
	 * bl_flash_binary(NULL,0) finalize call ran -- otherwise this BEGIN's
	 * fresh erase would be written into starting from that stale,
	 * mid-previous-image cursor instead of the start of the bank. See
	 * bl_flash_stream_reset()'s own doc comment (m1_fw_update_bl.h). */
	bl_flash_stream_reset();
	bl_flash_if_init();
	return bl_flash_start(image_size);   // erase the inactive bank
} // uint8_t bl_flash_stream_begin(uint32_t image_size)

uint8_t bl_flash_stream_write(uint8_t *payload, uint32_t size)
{
	return bl_flash_binary(payload, (size_t)size);   // program one chunk
} // uint8_t bl_flash_stream_write(uint8_t *payload, uint32_t size)

uint8_t bl_flash_stream_finish(void)
{
	uint8_t err = bl_flash_binary(NULL, 0);   // finalize + bl_crc_check
	bl_flash_if_deinit();
	return err;
} // uint8_t bl_flash_stream_finish(void)

void bl_flash_stream_abort(void)
{
	// Release/reset for every exit path that ISN'T a successful
	// bl_flash_stream_finish() (which already re-locks flash itself, above).
	// bl_flash_if_init/deinit are file-local (HAL_FLASH_Unlock/Lock); this is
	// the one exported way a caller outside this file can make sure flash
	// ends up locked again after a begin() that was aborted or that never
	// reached finish(). Safe to call even when nothing was ever unlocked
	// this attempt: bl_flash_stream_reset() is a pure state reset and
	// HAL_FLASH_Lock() is idempotent on already-locked flash.
	bl_flash_stream_reset();
	bl_flash_if_deinit();
} // void bl_flash_stream_abort(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void fw_gui_progress_update(size_t remainder)
{
	uint8_t percent_txt[30];
	uint8_t percent;
	size_t percent_cnt;
	static uint32_t image_size = 0;
	static uint8_t progress_percent_count = 0;
	static uint8_t progress_slider_x_post;

	if ( image_size < remainder )
	{
		image_size = remainder; // init
		progress_percent_count = 0;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
		// Draw solid box to clear existing content
		u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT + 1, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
		u8g2_DrawXBMP(&m1_u8g2, FW_UPDATE_PROGRESS_SLIDE_STRIP_COL, FW_UPDATE_PROGRESS_SLIDE_STRIP_ROW,
					126, 14, fw_update_slide_strip_126x14); // Progress slide strip
		u8g2_DrawStr(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1, "Updating: 00%");
		progress_slider_x_post = 3;
		M1_LOG_N(M1_LOGDB_TAG, "\r\n");
	}

	percent_cnt = image_size - remainder;
	percent_cnt /= (image_size/FW_UPDATE_FULL_PROGRESS_COUNT);
	if ( percent_cnt )
	{
		percent_cnt -= progress_percent_count;
		if ( percent_cnt )
		{
			progress_percent_count++;
			percent = FW_UPDATE_FULL_PROGRESS_FACTOR*progress_percent_count;
			sprintf(percent_txt, "Updating: %02d%%", percent);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
			// Draw solid box to clear existing content
			u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT + 1, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
			u8g2_DrawStr(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1, percent_txt); // Write new content
			u8g2_DrawXBMP(&m1_u8g2, progress_slider_x_post, FW_UPDATE_PROGRESS_SLIDE_STRIP_ROW + 3,
						4, 8, fw_update_slider_5x8); // Progress slider
			progress_slider_x_post += FW_UPDATE_SLIDER_WIDTH - FW_UPDATE_SLIDER_OVERLAP;
			if ( percent==100 ) // complete 100%?
				image_size = 0; // Reset
		    M1_LOG_I(M1_LOGDB_TAG, "\rProgress: %d%%", percent);
		} // if ( percent_cnt )
 	} // if ( percent_cnt )

	m1_u8g2_nextpage(); // Update display RAM
} // void fw_gui_progress_update(size_t remainder)
