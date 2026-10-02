/* See COPYING.txt for license details. */

/*
*
* m1_sdcard.c
*
* Library for accessing SD card
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_lp5814.h"
#include "m1_led_indicator.h"
#include "app_freertos.h"
#include "cmsis_os.h"
#include "m1_sdcard.h"
#include "m1_sdcard_provision.h"
#include "m1_storage.h"       /* usbmsc_sd_enable */

/*************************** D E F I N E S ************************************/

#define SD_DETECT_EXTI_IRQn         EXTI15_IRQn

#define M1_LOGDB_TAG				"SDCard"

#define SDCARD_LABEL				"MTEK-M1"

#define SD_TRANSFER_OK              0x00
#define SD_TRANSFER_BUSY           	0x01

//#define SD_DATATIMEOUT           	((uint32_t)30*1000)
#define SD_DATATIMEOUT           	((uint32_t)(IWDG_RELOAD*3)/4) // 75% of WDT period

#define SD_DEFAULT_BLOCK_SIZE 		512

#ifdef SDMMC_CMDTIMEOUT // default: 5000, defined in stm32h5xx_ll_sdmmc.h
#endif // #ifdef SDMMC_CMDTIMEOUT

#if defined(SDMMC_DATATIMEOUT)
#define SD_RW_TIMEOUT				SDMMC_DATATIMEOUT
#elif defined(SD_DATATIMEOUT)
#define SD_RW_TIMEOUT				SD_DATATIMEOUT
#else
#define SD_RW_TIMEOUT				30*1000
#endif

#define SDCARD_CB_QUEUE_SIZE         10

#define SDCARD_CB_READ_CPLT_MSG      1
#define SDCARD_CB_WRITE_CPLT_MSG     2

//************************** C O N S T A N T **********************************/

static const Diskio_drvTypeDef sdcard_driver =
{
	m1_sdcard_drive_init,
	m1_sdcard_status,
	m1_sdcard_read,
	m1_sdcard_write,
	m1_sdcard_ioctl
};
//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

SD_HandleTypeDef *phsd;
S_M1_SDCard_Info sdcard_info;
S_M1_SDCard_Hdl sdcard_ctl;
EXTI_HandleTypeDef 	sdcard_exti_hdl;
TaskHandle_t		sdcard_task_hdl;
QueueHandle_t		sdcard_cb_q_hdl = NULL;
uint8_t 			sdcard_status_changed = 0;

static FATFS 		*sd_pfatfs; 		// Pointer to File system object for user logical drive
static FRESULT 		sd_fres;  			// Return code for user
static uint32_t		sd_free_clusters;  	// Free Clusters
static uint32_t		sd_free_sectors;	// Free Sectors
static uint32_t		sd_total_sectors;  	// Total Sectors
static uint16_t 	sd_sector_size;		// Sector size
static volatile DSTATUS sd_stat = STA_NOINIT;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void sdcard_detection_task(void *param);
S_M1_SDCard_Init_Status m1_sdcard_init(SD_HandleTypeDef *phsdcard);
S_M1_SDCard_Init_Status m1_sdcard_init_ex(void);
S_M1_SDCard_Init_Status m1_sdcard_init_retry(void);
HAL_StatusTypeDef m1_sdcard_deinit(void);
uint8_t m1_sd_detected(void);
static void m1_sd_hardware_init(void);
char *m1_sd_error_msg(S_M1_SDCard_Access_Status ferr);
S_M1_SDCard_Info *m1_sdcard_get_info(void);
FRESULT m1_sdcard_get_error_code(void);
uint32_t m1_sdcard_get_total_capacity(void);
uint32_t m1_sdcard_get_free_capacity(void);
S_M1_SDCard_Access_Status m1_sdcard_get_status(void);
FRESULT m1_sdcard_mount(void);
FRESULT m1_sdcard_unmount(void);
uint8_t m1_sdcard_format(void);
static void m1_sdcard_set_status(S_M1_SDCard_Access_Status stat);
static uint8_t m1_sdcard_getcardstate(void);
static DSTATUS m1_sdcard_checkstatus(uint8_t param);
static uint8_t m1_sdcard_checkstatus_ex(uint32_t timeout);
void m1_sdcard_writecplt_callback(void);
void m1_sdcard_readcplt_callback(void);
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd);
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd);
static void m1_sdcard_error_handler(void);
DSTATUS m1_sdcard_drive_init(uint8_t param); /*!< Initialize Disk Drive*/
DSTATUS m1_sdcard_status(uint8_t param); /*!< Get Disk Status*/
DRESULT m1_sdcard_read(uint8_t param, uint8_t *buff, DWORD sector, UINT count);       /*!< Read Sector(s)*/
DRESULT m1_sdcard_write(uint8_t param, const uint8_t *buff, DWORD sector, UINT count); /*!< Write Sector(s)*/
DRESULT m1_sdcard_ioctl(uint8_t param, uint8_t cmd, void *buff);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/******************************************************************************/
/*
 * This function initializes the SD card hardware and configuration.
 *
 */
/******************************************************************************/
S_M1_SDCard_Init_Status m1_sdcard_init(SD_HandleTypeDef *phsdcard)
{
	GPIO_InitTypeDef gpio_init_structure;
	S_M1_SDCard_Init_Status sd_state = SD_RET_OK;

	assert(phsdcard!=NULL);
	phsd = phsdcard;

	sdcard_ctl.status = SD_access_NotReady;
	sdcard_ctl.sd_detected = FALSE;
	strcpy(sdcard_ctl.sdpath, SDCARD_DEFAULT_DRIVE_PATH);

	sdcard_exti_hdl.Line = EXTI_LINE_15;
	sdcard_exti_hdl.RisingCallback = NULL;
	sdcard_exti_hdl.FallingCallback = NULL;

	phsd->Instance = SDMMC1;
	phsd->Init.ClockEdge = SDMMC_CLOCK_EDGE_FALLING; //SDMMC_CLOCK_EDGE_RISING;
	phsd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
	phsd->Init.BusWide = SDMMC_BUS_WIDE_4B;
	phsd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
/*
	SDMMC_CK = SDMMCCLK / (2 * ClockDiv)

    In initialization mode and according to the SD Card standard,
    make sure that the SDMMC_CK frequency doesn't exceed 400KHz.
 */
	phsd->Init.ClockDiv = 8; //0;

	/* Configure Interrupt mode for SD detection pin */
	gpio_init_structure.Pin = SD_DETECT_Pin;
	gpio_init_structure.Pull = GPIO_PULLUP;
	gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio_init_structure.Mode = GPIO_MODE_IT_RISING_FALLING;
	HAL_GPIO_Init(SD_DETECT_GPIO_Port, &gpio_init_structure);

	/* Enable and set SD detect EXTI Interrupt to the low priority */
	HAL_NVIC_SetPriority((IRQn_Type)(SD_DETECT_EXTI_IRQn), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1, 0);
	HAL_NVIC_EnableIRQ((IRQn_Type)(SD_DETECT_EXTI_IRQn));

	m1_sd_hardware_init();

	sd_state = m1_sdcard_init_ex();

	return sd_state;
} // S_M1_SDCard_Init_Status m1_sdcard_init(SD_HandleTypeDef *phsdcard)



/******************************************************************************/
/*
 * This function deinitializes the SD card hardware and configuration.
 *
 */
/******************************************************************************/
HAL_StatusTypeDef m1_sdcard_deinit(void)
{
	GPIO_InitTypeDef gpio_init_structure;

	/* HAL SD deinitialization */
	if(HAL_SD_DeInit(phsd) != HAL_OK)
		return HAL_ERROR;

	/* Disable NVIC for SDIO interrupts */
	HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
	/* Disable SD detect EXTI Interrupt */
	HAL_NVIC_DisableIRQ((IRQn_Type)(SD_DETECT_EXTI_IRQn));

	/* Disable SDMMC1 clock */
	__HAL_RCC_SDMMC1_CLK_DISABLE();

	/* DeInit GPIO pins */
	gpio_init_structure.Pin = SD_DETECT_Pin;
	gpio_init_structure.Pull = GPIO_PULLUP;
	gpio_init_structure.Speed = GPIO_SPEED_FREQ_LOW;
	gpio_init_structure.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(SD_DETECT_GPIO_Port, &gpio_init_structure);

	// Unlinks a diskio driver and decrements the number of active linked drivers.
	FATFS_UnLinkDriver(sdcard_ctl.sdpath);

	sdcard_ctl.status = SD_access_NotReady;

	return HAL_OK;
} // HAL_StatusTypeDef m1_sdcard_deinit(void)



/******************************************************************************/
/*
 * This function initializes the SD card configuration.
 *
 */
/******************************************************************************/
S_M1_SDCard_Init_Status m1_sdcard_init_ex(void)
{
	HAL_SD_CardStatusTypeDef card_status;
	HAL_StatusTypeDef cfg_ret;
	uint8_t speedgrade, unitsize;

	/* Check the parameters */
	assert_param(IS_SDMMC_ALL_INSTANCE(phsd->Instance));
	assert_param(IS_SDMMC_CLOCK_EDGE(phsd->Init.ClockEdge));
	assert_param(IS_SDMMC_CLOCK_POWER_SAVE(phsd->Init.ClockPowerSave));
	assert_param(IS_SDMMC_BUS_WIDE(phsd->Init.BusWide));
	assert_param(IS_SDMMC_HARDWARE_FLOW_CONTROL(phsd->Init.HardwareFlowControl));
	assert_param(IS_SDMMC_CLKDIV(phsd->Init.ClockDiv));

	if ( m1_sd_detected() )
	{
		sdcard_ctl.sd_detected = TRUE;
	}
	else
	{
		sdcard_ctl.sd_detected = FALSE;
		return SD_RET_ERROR_NO_CARD;
	}

	phsd->State = HAL_SD_STATE_PROGRAMMING;

	/* Initialize the Card parameters */
	if (HAL_SD_InitCard(phsd) != HAL_OK)
	{
		M1_LOG_E(M1_LOGDB_TAG, "HAL_SD_InitCard() failed!\r\n");
		return SD_RET_ERROR;
	}

	if (HAL_SD_GetCardStatus(phsd, &card_status) != HAL_OK)
	{
		M1_LOG_E(M1_LOGDB_TAG, "HAL_SD_GetCardStatus() failed!\r\n");
		return SD_RET_ERROR;
	}

	/* Get Initial Card Speed from Card Status*/
	speedgrade = card_status.UhsSpeedGrade;
	unitsize = card_status.UhsAllocationUnitSize;
	if ((phsd->SdCard.CardType==CARD_SDHC_SDXC) && ((speedgrade != 0U) || (unitsize != 0U)))
	{
	    phsd->SdCard.CardSpeed = CARD_ULTRA_HIGH_SPEED;
	}
	else
	{
		if (phsd->SdCard.CardType==CARD_SDHC_SDXC)
	    {
			phsd->SdCard.CardSpeed  = CARD_HIGH_SPEED;
	    }
	    else
	    {
	    	phsd->SdCard.CardSpeed  = CARD_NORMAL_SPEED;
	    }
	} // else

	if ( !m1_sd_detected() ) // If the card is not ready, do not init
		return SD_RET_ERROR;
	M1_LOG_I(M1_LOGDB_TAG, "HAL_SD_ConfigWideBusOperation\r\n"); // For debug purpose only!
	vTaskDelay(1);
	/* Configure the bus wide */
	cfg_ret = HAL_SD_ConfigWideBusOperation(phsd, phsd->Init.BusWide);
	if ( cfg_ret!=HAL_OK )
		return SD_RET_ERROR;
/*
	// Verify that SD card is ready to use after Initialization
	tickstart = HAL_GetTick();
	while ((HAL_SD_GetCardState(phsd) != HAL_SD_CARD_TRANSFER))
	{
	    if ((HAL_GetTick() - tickstart) >=  SDMMC_SWDATATIMEOUT)
	    {
	      hsd->ErrorCode = HAL_SD_ERROR_TIMEOUT;
	      hsd->State = HAL_SD_STATE_READY;
	      return HAL_TIMEOUT;
	    }
	} // while ((HAL_SD_GetCardState(phsd) != HAL_SD_CARD_TRANSFER))
*/
	/* Initialize the error code */
	phsd->ErrorCode = HAL_SD_ERROR_NONE;
	/* Initialize the SD operation */
	phsd->Context = SD_CONTEXT_NONE;
	/* Initialize the SD state */
	phsd->State = HAL_SD_STATE_READY;

	// Let unlink first, just in case
	FATFS_UnLinkDriver(sdcard_ctl.sdpath);

	if ( FATFS_LinkDriver(&sdcard_driver, sdcard_ctl.sdpath)!=0 )
	{
		M1_LOG_E(M1_LOGDB_TAG, "FATFS_LinkDriver() failed!\r\n");
		m1_sdcard_error_handler();
	}

#if ( osCMSIS < 0x20000 )
	if (osKernelRunning())
#else
	if (osKernelGetState()==osKernelRunning )
	{
		if ( m1_sdcard_mount()==FR_DISK_ERR )
			return SD_RET_ERROR_LOW_LEVEL;
	} // if (osKernelGetState()==osKernelRunning )
	else
	{
		FATFS_UnLinkDriver(sdcard_ctl.sdpath);
	} // else
#endif // #if ( osCMSIS < 0x20000 )

	return SD_RET_OK;
} // S_M1_SDCard_Init_Status m1_sdcard_init_ex(void)



/*============================================================================*/
/*
 * This function tries to initialize the SD card if it's not in ready state
 * Return: access status of the SD card
 */
/*============================================================================*/
S_M1_SDCard_Init_Status m1_sdcard_init_retry(void)
{
	S_M1_SDCard_Access_Status sd_stat;
	S_M1_SDCard_Init_Status ret;

	ret = SD_RET_OK;
	sd_stat = m1_sdcard_get_status();
	switch ( sd_stat )
	{
		case SD_access_OK:
			break;

		case SD_access_NotReady:
			if ( !m1_sd_detected() ) // No SD card detected?
			{
				ret = SD_RET_ERROR_NO_CARD;
				break;
			} // if ( !m1_sd_detected() )
			M1_LOG_I(M1_LOGDB_TAG, "SD Card init retry\r\n");
			if ( m1_sdcard_deinit() != HAL_OK )
				break;
			ret = m1_sdcard_init(&hsd1);
			if ( ret==SD_RET_OK )
			{
				sd_stat = m1_sdcard_get_status();
			} // if ( ret==SD_RET_OK )
			break;

		case SD_access_NoFS:
			//M1_LOG_N(M1_LOGDB_TAG, "Error: no file system found!\r\n");
			break;

		default:
			m1_sdcard_mount(); // Try to mount the SD card again
			sd_stat = m1_sdcard_get_status();
			break;
	} // switch ( sd_stat )

	if ( sd_stat != SD_access_OK )
	{
		M1_LOG_E(M1_LOGDB_TAG, "Not accessible!\r\n");
	}

	return ret;
} // S_M1_SDCard_Init_Status m1_sdcard_init_retry(void)



/******************************************************************************/
/*
 * Return TRUE if an SD card is detected (causing the Detect Switch to pull low)
 *
 */
/******************************************************************************/
uint8_t m1_sd_detected(void)
{
	uint8_t detected = TRUE;

	/* Check SD card detect pin */
	if (HAL_GPIO_ReadPin(SD_DETECT_GPIO_Port, SD_DETECT_Pin)==GPIO_PIN_SET)
		detected = FALSE;

	return detected;
} // uint8_t m1_sd_detected(void)



/******************************************************************************/
/**
*
* This function configures the hardware resources for the SD card
*
*/
/******************************************************************************/
static void m1_sd_hardware_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
	HAL_StatusTypeDef ret;

	/** Initializes the peripherals clock
	 */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SDMMC1;
    PeriphClkInitStruct.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_PLL1Q;
    ret = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

    if ( ret!=HAL_OK )
    {
    	m1_sdcard_error_handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**SDMMC1 GPIO Configuration
    PC8     ------> SDMMC1_D0
    PC9     ------> SDMMC1_D1
    PC10     ------> SDMMC1_D2
    PC11     ------> SDMMC1_D3
    PC12     ------> SDMMC1_CK
    PD2     ------> SDMMC1_CMD
    */
    GPIO_InitStruct.Pin = SDIO1_D0_Pin|SDIO1_D1_Pin|SDIO1_D2_Pin|SDIO1_D3_Pin
                          |SDIO1_CK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SDIO1_CMD_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(SDIO1_CMD_GPIO_Port, &GPIO_InitStruct);

    /* SDMMC1 interrupt Init */
    HAL_NVIC_SetPriority(SDMMC1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 1);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);

} // static void m1_sd_hardware_init(void)




/******************************************************************************/
/**
*
* This function returns error messages from given error codes
*
*/
/******************************************************************************/
char *m1_sd_error_msg(S_M1_SDCard_Access_Status ferr)
{
	switch (ferr)
	{
		case SD_access_OK:
			return "OK";
			break;

		case SD_access_NotOK:
			return "Not OK";
			break;

		case SD_access_NotReady:
			return "Not Ready";
			break;

		case SD_access_UnMounted:
			return "Not mounted";
			break;

		case SD_access_NoFS:
			return "No File System";
			break;

		default:
			return "Unknown";
	} // switch (ferr)

} // char *m1_sd_error_msg(S_M1_SDCard_Access_Status ferr)



/******************************************************************************/
/**
*
* This function returns the result code of the last disk activity
*
*/
/******************************************************************************/
FRESULT m1_sdcard_get_error_code(void)
{
	return sd_fres; // Return code for user
} // FRESULT m1_sdcard_get_error_code(void)



/******************************************************************************/
/**
*
* This function returns capacity of an SD card in Kbytes
*
*/
/******************************************************************************/
uint32_t m1_sdcard_get_total_capacity(void)
{
	return sdcard_info.total_cap_kb;
} // uint32_t m1_sdcard_get_total_capacity(void)



/******************************************************************************/
/**
*
* This function returns free capacity of an SD card in Kbytes
*
*/
/******************************************************************************/
uint32_t m1_sdcard_get_free_capacity(void)
{
	return sdcard_info.free_cap_kb;
} // uint32_t m1_sdcard_get_free_capacity(void)




/******************************************************************************/
/**
*
* This function reads information of an SD card
*
*/
/******************************************************************************/
S_M1_SDCard_Info *m1_sdcard_get_info(void)
{
	// Reset data
    memset((void *)&sdcard_info, 0, sizeof(S_M1_SDCard_Info));

    // Read volume label
    sd_fres = f_getlabel(sdcard_ctl.sdpath, sdcard_info.vol_label, NULL);
    if( sd_fres==FR_OK )
    {
    	sd_fres = f_getfree(sdcard_ctl.sdpath, &sd_free_clusters, &sd_pfatfs);
    	if( sd_fres==FR_OK )
    	{
    		sd_total_sectors = (sd_pfatfs->n_fatent - 2) * sd_pfatfs->csize;
    		sd_free_sectors = sd_free_clusters * sd_pfatfs->csize;
            sd_sector_size = FF_MAX_SS;
#if FF_MAX_SS != FF_MIN_SS
            sd_sector_size = fs->ssize;
#endif // #if FF_MAX_SS != _MIN_SS

            switch(sd_pfatfs->fs_type)
            {
            	case FS_FAT12:
            		sdcard_info.fs_type = FATSYS_12;
            		break;

            	case FS_FAT16:
            		sdcard_info.fs_type = FATSYS_16;
            		break;

            	case FS_FAT32:
            		sdcard_info.fs_type = FATSYS_32;
            		break;

            	case FS_EXFAT:
            		sdcard_info.fs_type = FATSYS_EXT;
            		break;

            	default:
            		sdcard_info.fs_type = FATSYS_ANY;
            		break;
            } // switch(sd_pfatfs->fs_type)

            // Divided by 1024 to convert bytes to kbytes
            sdcard_info.total_cap_kb = (sd_total_sectors/1024)*sd_sector_size;
            sdcard_info.free_cap_kb = (sd_free_sectors/1024)*sd_sector_size;
            sdcard_info.cluster_size = sd_pfatfs->csize;
            sdcard_info.sector_size = sd_sector_size;
/*
            sdcard_info.manuf_id = 0x00;
            sdcard_info.oem_id = 0x00;
            sdcard_info.prod_name = 0x00;
            sdcard_info.prod_rev_major = 0x00;
            sdcard_info.prod_rev_minor = 0x00;
            sdcard_info.prod_sn = 0x00;
*/
    	} // if( sd_fres==FR_OK )
    	else
    	{
    		// Reset all information
            sdcard_info.total_cap_kb = 0;
            sdcard_info.free_cap_kb = 0;
            sdcard_info.cluster_size = 0;
            sdcard_info.sector_size = 0;
    	}
    } // if( sd_fres==FR_OK )

    if ( sd_fres!=FR_OK )
    {
    	M1_LOG_E(M1_LOGDB_TAG, "Getting info failed, error# %d\r\n", sd_fres);
    	return NULL;
    }

    return &sdcard_info;
} // S_M1_SDCard_Info *m1_sdcard_get_info(void)



/******************************************************************************/
/**
*
* This function returns the access status of the SD card
*
*/
/******************************************************************************/
S_M1_SDCard_Access_Status m1_sdcard_get_status(void)
{
	return sdcard_ctl.status;
} // S_M1_SDCard_Access_Status m1_sdcard_get_status(void)




/******************************************************************************/
/**
*
* This function mounts an SD card
*
*/
/******************************************************************************/
FRESULT m1_sdcard_mount(void)
{
	// Mount a Logical Drive
	sd_fres = f_mount(&sdcard_ctl.sdfs, sdcard_ctl.sdpath, 1);
	const FRESULT mount_result = sd_fres;
	if (sd_fres==FR_OK || sd_fres==FR_NO_FILESYSTEM)
	{
		sd_fres = f_getfree(sdcard_ctl.sdpath, &sd_free_clusters, &sd_pfatfs);
		if(sd_fres==FR_OK)
		{
			sdcard_ctl.status = SD_access_OK;
			/* Canonical SD folder structure: only now that mount succeeded,
			 * firmware owns the filesystem, and (by construction -- this path
			 * is unreachable while USB mass storage holds the card) the host
			 * does not. Idempotent and non-blocking; a failure here never
			 * fails this mount. Covers boot, card insertion/remount, and
			 * USB-MSC-ownership return -- every real caller of this function. */
			if (!usbmsc_sd_enable)
			{
				(void)m1_sdcard_provision_canonical();
			}
		}
		else if(sd_fres==FR_NO_FILESYSTEM)
			sdcard_ctl.status = SD_access_NoFS;
		else
			sdcard_ctl.status = SD_access_NotOK;
	} // if (sd_fres==FR_OK || sd_fres==FR_NO_FILESYSTEM)
	else
	{
		f_mount(0, sdcard_ctl.sdpath, 0); // unmount
		if ( sd_fres!=FR_DISK_ERR )
			sdcard_ctl.status = SD_access_UnMounted;
		else
			sdcard_ctl.status = SD_access_NotOK;
	} // else
	sdcard_ctl.timestamp = HAL_GetTick();
	M1_LOG_I(M1_LOGDB_TAG, "Mounting result: f_mount:%d %s\r\n", sd_fres, m1_sd_error_msg(sdcard_ctl.status));
	return mount_result != FR_OK ? mount_result : sd_fres;
} // FRESULT m1_sdcard_mount(void)




/******************************************************************************/
/**
*
* This function unmounts an SD card
*
*/
/******************************************************************************/
FRESULT m1_sdcard_unmount(void)
{
	// Unmount a Logical Drive
    sd_fres = f_mount(0, sdcard_ctl.sdpath, 0);
    sdcard_ctl.status = sd_fres == FR_OK ? SD_access_UnMounted : SD_access_NotOK;
    if (sd_fres == FR_OK) M1_LOG_I(M1_LOGDB_TAG, "Card unmounted.\r\n");
    return sd_fres;
} // FRESULT m1_sdcard_unmount(void)



/******************************************************************************/
/**
*
* This function updates the access status for the SD card
*
*/
/******************************************************************************/
static void m1_sdcard_set_status(S_M1_SDCard_Access_Status stat)
{
	if ( stat < SD_access_EndOfStatus )
		sdcard_ctl.status = stat;
} // void m1_sdcard_set_status(S_M1_SDCard_Access_Status stat)

void m1_sdcard_invalidate(void)
{
    m1_sdcard_set_status(SD_access_NotReady);
}




/******************************************************************************/
/**
*
* This function formats an SD card
*
*/
/******************************************************************************/
uint8_t m1_sdcard_format(void)
{
    uint8_t *pwork_buffer;
    MKFS_PARM defopt = {FM_ANY, 0, 0, 0, 0};

    pwork_buffer = malloc(FF_MAX_SS);

    assert(pwork_buffer!=NULL);
/*
    Create an FAT/exFAT volume
FRESULT f_mkfs (
	const TCHAR* path,		// Logical drive number
	const MKFS_PARM* opt,	// Format options
	void* work,				// Pointer to working buffer (null: use len bytes of heap memory)
	UINT len				// Size of working buffer [byte]
	)
	if (!opt) opt = &defopt;	// Use default parameter if it is not given
*/
    sd_fres = f_mkfs(sdcard_ctl.sdpath, &defopt, pwork_buffer, FF_MAX_SS);
    free(pwork_buffer);
    do
    {
        sdcard_ctl.status = SD_access_NotOK;
        if ( sd_fres!=FR_OK )
        	break;

        sdcard_ctl.status = SD_access_NoFS;
        sd_fres = f_setlabel(SDCARD_LABEL);
        if ( sd_fres!=FR_OK )
        	break;

        sdcard_ctl.status = SD_access_UnMounted;
        sd_fres = f_mount(&sdcard_ctl.sdfs, sdcard_ctl.sdpath, 1);
        if ( sd_fres!=FR_OK )
        	break;
        sdcard_ctl.status = SD_access_OK;
    } while(0);

    if ( sd_fres!=FR_OK )
    	M1_LOG_E(M1_LOGDB_TAG, "Format failed, error# %d\r\n", sd_fres);

    return 0;
} // uint8_t m1_sdcard_format(void)



/******************************************************************************/
/**
  * @brief  Gets the current SD card data status.
  * @retval Data transfer state.
  *          This value can be one of the following values:
  *            @arg  SD_TRANSFER_OK: No data transfer is acting
  *            @arg  SD_TRANSFER_BUSY: Data transfer is acting
  */
/******************************************************************************/
static uint8_t m1_sdcard_getcardstate(void)
{
	return((HAL_SD_GetCardState(phsd)==HAL_SD_CARD_TRANSFER) ? SD_TRANSFER_OK : SD_TRANSFER_BUSY);
} // static uint8_t m1_sdcard_getcardstate(void)



/******************************************************************************/
/**
  * @brief  Checks Disk Status
  * @param  param : not used
  * @retval DSTATUS: Operation status
  */
/******************************************************************************/
static DSTATUS m1_sdcard_checkstatus(uint8_t param)
{
	sd_stat = STA_NOINIT;

	if(m1_sdcard_getcardstate()==SD_TRANSFER_OK)
	{
		sd_stat &= ~STA_NOINIT;
	}

	return sd_stat;
} // static DSTATUS m1_sdcard_checkstatus(uint8_t param)




/******************************************************************************/
/**
  * @brief  Checks Disk Status with timeout
  * @param  timeout: timeout
  * @retval 0 if status OK, 1 otherwise
  */
/******************************************************************************/
static uint8_t m1_sdcard_checkstatus_ex(uint32_t timeout)
{
	uint32_t timer;

	timer = osKernelGetTickCount();
	while (osKernelGetTickCount() - timer < timeout)
	{
		if (m1_sdcard_getcardstate()==SD_TRANSFER_OK)
		{
			return 0;
		}
	} // while (osKernelGetTickCount() - timer < timeout)

	return 1;
} // static uint8_t m1_sdcard_checkstatus_ex(uint32_t timeout)



/******************************************************************************/
/**
  * @brief Tx Transfer completed callbacks
  * @param hsd: SD handle
  * @retval None
  */
/******************************************************************************/
void m1_sdcard_writecplt_callback(void)
{
  /*
   * No need to add an "osKernelRunning()" check here, as the SD_initialize()
   * is always called before any SD_Read()/SD_Write() call
   */
	const uint16_t msg = SDCARD_CB_WRITE_CPLT_MSG;

	xQueueSendFromISR(sdcard_cb_q_hdl, (const void *)&msg, 0);
} // void m1_sdcard_writecplt_callback(void)



/******************************************************************************/
/**
  * @brief Rx Transfer completed callbacks
  * @param hsd: SD handle
  * @retval None
  */
/******************************************************************************/
void m1_sdcard_readcplt_callback(void)
{
  /*
   * No need to add an "osKernelRunning()" check here, as the SD_initialize()
   * is always called before any SD_Read()/SD_Write() call
   */
	const uint16_t msg = SDCARD_CB_READ_CPLT_MSG;

	xQueueSendFromISR(sdcard_cb_q_hdl, (const void *)&msg, 0);
} // void m1_sdcard_readcplt_callback(void)



/******************************************************************************/
/**
  * @brief Tx Transfer completed callbacks
  * @param hsd: Pointer to SD handle
  * @retval None
  */
/******************************************************************************/
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
	/* Prevent unused argument(s) compilation warning */
	UNUSED(hsd);
	m1_sdcard_writecplt_callback();
} // void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)



/******************************************************************************/
/**
  * @brief Rx Transfer completed callbacks
  * @param hsd: Pointer SD handle
  * @retval None
  */
/******************************************************************************/
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
	/* Prevent unused argument(s) compilation warning */
	UNUSED(hsd);
	m1_sdcard_readcplt_callback();
} // void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)



/******************************************************************************/
/**
  * @brief  Initializes a Drive
  * @param  param : not used
  * @retval DSTATUS: Operation status
  */
/******************************************************************************/
DSTATUS m1_sdcard_drive_init(uint8_t param)
{
	sd_stat = STA_NOINIT;
	sd_stat = m1_sdcard_status(param);

    /*
    * if the SD is correctly initialized, create the operation queue
    * if not already created
    */
    if (sd_stat != STA_NOINIT)
    {
    	if (sdcard_cb_q_hdl==NULL)
    	{
    		sdcard_cb_q_hdl = xQueueCreate(SDCARD_CB_QUEUE_SIZE, 2);
    		assert_param(sdcard_cb_q_hdl != NULL);
    	}

    	if (sdcard_cb_q_hdl==NULL)
    	{
    		sd_stat |= STA_NOINIT;
    	}
    } // if (sd_stat != STA_NOINIT)

    return sd_stat;
} // DSTATUS m1_sdcard_drive_init(uint8_t param)



/******************************************************************************/
/**
  * @brief  Gets Disk Status
  * @param  param : not used
  * @retval DSTATUS: Operation status
  */
/******************************************************************************/
DSTATUS m1_sdcard_status(uint8_t param)
{
	return m1_sdcard_checkstatus(param);
} // DSTATUS m1_sdcard_status(uint8_t param)




/******************************************************************************/
/**
  * @brief  Reads Sector(s)
  * @param  param : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
/******************************************************************************/
DRESULT m1_sdcard_read(uint8_t param, uint8_t *buff, DWORD sector, UINT count)
{
	DRESULT res = RES_ERROR;
	uint32_t timer;
	uint16_t event;
	BaseType_t status;

	if ( m1_sdcard_checkstatus_ex(SD_DATATIMEOUT) )
	{
		return res;
	}

	if (HAL_SD_ReadBlocks_DMA(phsd, buff, (uint32_t)sector, count)==HAL_OK)
	{
		status = xQueueReceive(sdcard_cb_q_hdl, (void *)&event, SD_DATATIMEOUT);
		if ((status==pdTRUE) && (event==SDCARD_CB_READ_CPLT_MSG))
		{
			timer = osKernelGetTickCount();
			/* block until SDIO IP is ready or a timeout occur */
			while ( (osKernelGetTickCount() - timer) < SD_DATATIMEOUT )
			{
				if (m1_sdcard_getcardstate()==SD_TRANSFER_OK)
				{
					res = RES_OK;
					break;
				}
			} // while ( (osKernelGetTickCount() - timer) < SD_DATATIMEOUT )
        } // if ((status==pdTRUE) && (event==SDCARD_CB_READ_CPLT_MSG))
	} // if (HAL_SD_ReadBlocks_DMA(phsd, buff, (uint32_t)sector, count)==HAL_OK)

	return res;
/*	error HAL_SD_ERROR_RX_OVERRUN */
} // DRESULT m1_sdcard_read(uint8_t param, uint8_t *buff, DWORD sector, UINT count)



/******************************************************************************/
/**
  * @brief  Writes Sector(s)
  * @param  param : not used
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
/******************************************************************************/
DRESULT m1_sdcard_write(uint8_t param, const uint8_t *buff, DWORD sector, UINT count)
{
	DRESULT res = RES_ERROR;
	uint32_t timer;
	uint16_t event;
	BaseType_t status;

	if ( m1_sdcard_checkstatus_ex(SD_DATATIMEOUT) )
	{
		return res;
	}

	if ( HAL_SD_WriteBlocks_DMA(phsd, buff, (uint32_t)sector, count)==HAL_OK )
	{
		status = xQueueReceive(sdcard_cb_q_hdl, (void *)&event, SD_DATATIMEOUT);
		if ((status==pdTRUE) && (event==SDCARD_CB_WRITE_CPLT_MSG))
		{
			timer = osKernelGetTickCount();
			/* block until SDIO IP is ready or a timeout occur */
			while ( (osKernelGetTickCount() - timer) < SD_DATATIMEOUT )
			{
				if (m1_sdcard_getcardstate()==SD_TRANSFER_OK)
				{
					res = RES_OK;
					break;
				}
			} // while ( (osKernelGetTickCount() - timer) < SD_DATATIMEOUT )
        } // if ((status==pdTRUE) && (event==SDCARD_CB_READ_CPLT_MSG))
	} // if ( HAL_SD_WriteBlocks_DMA(phsd, buff, (uint32_t)sector, count)==HAL_OK )

	return res;
} // DRESULT m1_sdcard_write(uint8_t param, const uint8_t *buff, DWORD sector, UINT count)



/******************************************************************************/
/**
  * @brief  I/O control operation
  * @param  param : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
/******************************************************************************/
DRESULT m1_sdcard_ioctl(uint8_t param, uint8_t cmd, void *buff)
{
	HAL_SD_CardInfoTypeDef cardinfo;
	DRESULT res = RES_ERROR;

	if (sd_stat & STA_NOINIT)
		return RES_NOTRDY;

	switch (cmd)
	{
		/* Make sure that no pending write process */
	  	case CTRL_SYNC :
	  		res = RES_OK;
	  		break;

	  	/* Get number of sectors on the disk (DWORD) */
	  	case GET_SECTOR_COUNT :
	  		HAL_SD_GetCardInfo(phsd, &cardinfo);
	  		*(DWORD*)buff = cardinfo.LogBlockNbr;
	  		res = RES_OK;
	  		break;

	  	/* Get R/W sector size (WORD) */
	  	case GET_SECTOR_SIZE :
	  		HAL_SD_GetCardInfo(phsd, &cardinfo);
	  		*(WORD*)buff = cardinfo.LogBlockSize;
	  		res = RES_OK;
	  		break;

	  	/* Get erase block size in unit of sector (DWORD) */
	  	case GET_BLOCK_SIZE :
	  		HAL_SD_GetCardInfo(phsd, &cardinfo);
	  		*(DWORD*)buff = cardinfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
	  		res = RES_OK;
	  		break;

	  	default:
	  		res = RES_PARERR;
	  } // switch (cmd)

	  return res;
} // DRESULT m1_sdcard_ioctl(uint8_t param, uint8_t cmd, void *buff)



/*============================================================================*/
/**
  * @brief  This function is executed in case of error occurrence
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void m1_sdcard_error_handler(void)
{
	m1_sdcard_set_status(SD_access_NotReady);
	while (1)
	{
		Error_Handler();
	}
} // static void m1_sdcard_error_handler(void)



/*============================================================================*/
/*
 * This function updates the sd card status when it's inserted or removed
 * The notification is sent from ISR
 */
/*============================================================================*/
void sdcard_detection_task(void *param)
{
	BaseType_t ret;
	S_M1_SdCard_Q_t q_item;
	S_M1_SDCard_Access_Status stat;
	uint8_t init_retries = 1;

	if( m1_sd_detected() )
	{
		q_item.q_evt_type = Q_EVENT_SDCARD_CONNECTED;
		xQueueSend(sdcard_det_q_hdl, &q_item, portMAX_DELAY);
	}  // if( m1_sd_detected() )

	while (1)
	{
		ret = xQueueReceive(sdcard_det_q_hdl, &q_item, portMAX_DELAY);
		if ( ret != pdPASS )
			continue;

		if (ret==pdPASS)
		{
			switch ( q_item.q_evt_type )
			{
				case Q_EVENT_SDCARD_CHANGE:
			        if ( m1_sd_detected() )
			        {
			        	// The card may be being inserted or removed. Bouncing may happen when the card is being removed.
			        	stat = m1_sdcard_get_status();
			        	if ( stat != SD_access_NotReady ) // The card is being removed, not inserted
			        	{
			        		q_item.q_evt_type = Q_EVENT_SDCARD_REMOVED;
			        		m1_led_set_blink_timer(LED_BLINK_ON_BLUE, LED_BLINK_TIMER_ONTIME, LED_BLINK_TIMER_MODE_BLINK);
			        	} // if ( stat != SD_access_NotReady )
			        	else // The card is being removed with bouncing
			        	{
			        		q_item.q_evt_type = Q_EVENT_SDCARD_INSERTED;
			        		m1_led_set_blink_timer(LED_BLINK_ON_RED + LED_BLINK_ON_GREEN, LED_BLINK_TIMER_ONTIME, LED_BLINK_TIMER_MODE_SOLID); //NULL
			        	} // else
			        } // if ( m1_sd_detected() )
			        else
			        {
			        	q_item.q_evt_type = Q_EVENT_SDCARD_REMOVED;
			        	m1_led_set_blink_timer(LED_BLINK_ON_BLUE, LED_BLINK_TIMER_ONTIME, LED_BLINK_TIMER_MODE_BLINK);
			        } // else
			        xQueueSend(sdcard_det_q_hdl, &q_item, portMAX_DELAY);
			        vTaskDelay(TASKDELAY_SDCARD_DET_TASK); // Debounce
			        break;

				case Q_EVENT_SDCARD_INSERTED:
				case Q_EVENT_SDCARD_REMOVED:
					if ( m1_sd_detected() )
					{
						if ( q_item.q_evt_type==Q_EVENT_SDCARD_INSERTED )
							m1_led_set_blink_timer(LED_BLINK_ON_GREEN, LED_BLINK_TIMER_ONTIME, LED_BLINK_TIMER_MODE_SOLID);
						q_item.q_evt_type = Q_EVENT_SDCARD_CONNECTED;
					} // if ( m1_sd_detected() )
					else
					{
						if ( q_item.q_evt_type==Q_EVENT_SDCARD_INSERTED )
							m1_led_set_blink_timer(LED_BLINK_ON_RED, LED_BLINK_TIMER_ONTIME, LED_BLINK_TIMER_MODE_SOLID);
						q_item.q_evt_type = Q_EVENT_SDCARD_DISCONNECTED;
					}
					xQueueSend(sdcard_det_q_hdl, &q_item, portMAX_DELAY);
					break;

				case Q_EVENT_SDCARD_CONNECTED:
					M1_LOG_I(M1_LOGDB_TAG, "Card inserted: ");
					stat = m1_sdcard_get_status();
					switch ( stat )
					{
						case SD_access_NotReady:
							// Blink LED here
							// Unlinks a diskio driver and decrements the number of active linked drivers.
							M1_LOG_N(M1_LOGDB_TAG, "SD_access_NotReady. Init retries...\r\n");
							if ( m1_sdcard_init_retry()==SD_RET_ERROR_LOW_LEVEL )
							{
								if ( init_retries++ < 3 )
								{
									M1_LOG_I(M1_LOGDB_TAG, "Init retries: %d\r\n", init_retries);
									q_item.q_evt_type = Q_EVENT_SDCARD_CONNECTED;
									xQueueSend(sdcard_det_q_hdl, &q_item, portMAX_DELAY); // Retry
								} // if ( init_retries++ < 3 )
							} // if ( m1_sdcard_init_retry()==SD_RET_ERROR_LOW_LEVEL )
							else
							{
								init_retries = 1;
							}
							break;

						//case SD_access_OK:
						case SD_access_NotOK:
						case SD_access_NoFS:
						case SD_access_UnMounted:
							M1_LOG_N(M1_LOGDB_TAG, "SD_access_NotOK.\r\n");
							m1_sdcard_unmount();
							m1_sdcard_mount();
							if ( m1_sdcard_get_error_code()==FR_DISK_ERR )
							{
								m1_sdcard_set_status(SD_access_NotReady); // Force to retry init
								q_item.q_evt_type = Q_EVENT_SDCARD_CONNECTED;
								xQueueSend(sdcard_det_q_hdl, &q_item, portMAX_DELAY); // Retry
							} // if ( m1_sdcard_get_error_code()==FR_DISK_ERR )
							break;

						default:
							M1_LOG_N(M1_LOGDB_TAG, "Unknown!\r\n");
							break;
					} // switch ( stat )
					//Led_Blink_Off(LED3);
			        sdcard_status_changed = 0;
					break;

				case Q_EVENT_SDCARD_DISCONNECTED:
					stat = m1_sdcard_get_status();
					M1_LOG_I(M1_LOGDB_TAG, "Card removed: ");
					switch ( stat )
					{
						case SD_access_OK:
						case SD_access_NotOK:
						case SD_access_NoFS:
						case SD_access_UnMounted:
							M1_LOG_N(M1_LOGDB_TAG, "SD_access_NotOK.\r\n");
							m1_sdcard_unmount();
							m1_sdcard_set_status(SD_access_NotReady);
							break;

						case SD_access_NotReady:
							M1_LOG_N(M1_LOGDB_TAG, "SD_access_NotReady.\r\n");
							break;

						default:
							M1_LOG_N(M1_LOGDB_TAG, "Unknown!\r\n");
							break;
					} // switch ( stat )
					// Blink LED here
					// Unlinks a diskio driver and decrements the number of active linked drivers.
					FATFS_UnLinkDriver(sdcard_ctl.sdpath);
					//Led_Blink_Off(LED1);
			        //Led_Blink_On(LED3);
			        sdcard_status_changed = 0;
					break;

				default:
					break;
			} // switch ( q_item.q_evt_type )
		} // if (ret==pdPASS)
	} // while (1)
} // void sdcard_detection_task(void *param)

