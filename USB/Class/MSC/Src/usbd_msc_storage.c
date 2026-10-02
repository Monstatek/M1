/**
  ******************************************************************************
  * @file    usbd_msc_storage_template.c
  * @author  MCD Application Team
  * @brief   Memory management layer
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_sd.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include <usbd_msc_storage.h>
#include "m1_sdcard.h"
#include "m1_usb_cdc_msc.h"

/* Private typedef -----------------------------------------------------------*/
#define M1_LOGDB_TAG  "USB-MSC"

/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Extern function prototypes ------------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

#define STORAGE_LUN_NBR             1U

#define MSC_SD_DATATIMEOUT          100000

#define SDCARD_CB_READ_CPLT_MSG     1
#define SDCARD_CB_WRITE_CPLT_MSG    2

uint8_t usbmsc_sd_enable = 0;
/* Host MSC eject event: set by SCSI START_STOP_UNIT (USB context), handled and
 * cleared by the storage task. Only an event flag - no filesystem work here. */
volatile uint8_t usbmsc_host_ejected = 0;

extern SD_HandleTypeDef *phsd;
extern S_M1_SDCard_Info sdcard_info;
extern S_M1_SDCard_Hdl sdcard_ctl;
extern QueueHandle_t   sdcard_cb_q_hdl;

int8_t STORAGE_Init(uint8_t lun);
int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
                           uint16_t *block_size);
int8_t STORAGE_status_usbmsc_sd(void);
int8_t STORAGE_IsReady(uint8_t lun);
int8_t STORAGE_IsWriteProtected(uint8_t lun);
int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                    uint16_t blk_len);
int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr,
                     uint16_t blk_len);
int8_t STORAGE_GetMaxLun(void);

/* USB Mass storage Standard Inquiry Data */
int8_t  STORAGE_Inquirydata[] =  /* 36 */
{
  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  'M', '1', ' ', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
  'M', 'o', 'n', 's', 't', 'a', 'T', 'e', /* Product      : 16 Bytes */
  'k', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  '0', '.', '0', '1',                     /* Version      : 4 Bytes */
};

USBD_StorageTypeDef USBD_MSC_Interface_fops =
{
  STORAGE_Init,
  STORAGE_GetCapacity,
  STORAGE_IsReady,
  STORAGE_IsWriteProtected,
  STORAGE_Read,
  STORAGE_Write,
  STORAGE_GetMaxLun,
  STORAGE_Inquirydata,
};

/**
  * @brief  Initializes the storage unit (medium)
  * @param  lun: Logical unit number
  * @retval Status (0 : OK / -1 : Error)
  */
int8_t STORAGE_Init(uint8_t lun)
{
  UNUSED(lun);
  //m1_sdcard_unmount();
  return (0);
}

/**
  * @brief  Returns the medium capacity.
  * @param  lun: Logical unit number
  * @param  block_num: Number of total block number
  * @param  block_size: Block size
  * @retval Status (0: OK / -1: Error)
  */
int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  UNUSED(lun);

  HAL_SD_CardInfoTypeDef cardinfo;
  int8_t res = -1;

  if (STORAGE_status_usbmsc_sd())
  {
    HAL_SD_GetCardInfo(phsd, &cardinfo);

    *block_num  = cardinfo.BlockNbr;
    *block_size = cardinfo.BlockSize ;

    res = 0;
  }
  return (res);
}

int8_t STORAGE_status_usbmsc_sd(void)
{
    if ((sdcard_ctl.status == SD_access_UnMounted) &&
        m1_sd_detected() &&
        usbmsc_sd_enable)
        return true;
    else
        return false;
}

/**
  * @brief  Checks whether the medium is ready.
  * @param  lun: Logical unit number
  * @retval Status (0: OK / -1: Error)
  */
int8_t  STORAGE_IsReady(uint8_t lun)
{
  DSTATUS stat;

  UNUSED(lun);

  if (STORAGE_status_usbmsc_sd())
  {
    m1_USB_MSC_ready = 0;
  }
  else
  {
    m1_USB_MSC_ready = -1;
  }

  return (m1_USB_MSC_ready);  // 0=MSC ready, -1=MSC not ready
}

/**
  * @brief  Checks whether the medium is write protected.
  * @param  lun: Logical unit number
  * @retval Status (0: write enabled / -1: otherwise)
  */
int8_t  STORAGE_IsWriteProtected(uint8_t lun)
{
  UNUSED(lun);
  return  0;
}

uint32_t DBG_sd_rd_timeout_cnt = 0;
uint32_t DBG_sd_wr_timeout_cnt = 0;
uint8_t *DBG_sd_buf;

/**
  * @brief  Reads data from the medium.
  * @param  lun: Logical unit number
  * @param  buf: data buffer
  * @param  blk_addr: Logical block address
  * @param  blk_len: Blocks number
  * @retval Status (0: OK / -1: Error)
  */
int8_t STORAGE_Read(uint8_t lun, uint8_t *buf,
                    uint32_t blk_addr, uint16_t blk_len)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  int8_t res = -1;
  uint32_t timer;
  uint16_t event = 0;
  BaseType_t status;

  UNUSED(lun);

  DBG_sd_buf = buf;

  if (STORAGE_status_usbmsc_sd())
  {
    if (HAL_SD_ReadBlocks_DMA(phsd, buf, (uint32_t)blk_addr, blk_len)==HAL_OK)
    {
      while(1)
      {
        status = xQueueReceiveFromISR(sdcard_cb_q_hdl, (void *)&event, &xHigherPriorityTaskWoken);
        if (status==pdTRUE) break;
        //if (res !=0) DBG_sd_rd_timeout_cnt++;
        m1_wdt_reset();
      }
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

      if (event==SDCARD_CB_READ_CPLT_MSG)
      {
        timer = osKernelGetTickCount();
        while ( (osKernelGetTickCount() - timer) < MSC_SD_DATATIMEOUT )
        {
          if (HAL_SD_GetCardState(phsd) == HAL_SD_CARD_TRANSFER)
          {
            res = 0;
            break;
          }
          m1_wdt_reset();
          if (res !=0) DBG_sd_rd_timeout_cnt++;
        } // while (...)
      } // if (event==SDCARD_CB_READ_CPLT_MSG)
      else
      {
        if (res !=0) DBG_sd_rd_timeout_cnt++;
      }
    }
  }

  return (res);
}

/**
  * @brief  Writes data into the medium.
  * @param  lun: Logical unit number
  * @param  buf: data buffer
  * @param  blk_addr: Logical block address
  * @param  blk_len: Blocks number
  * @retval Status (0 : OK / -1 : Error)
  */
int8_t STORAGE_Write(uint8_t lun, uint8_t *buf,
                     uint32_t blk_addr, uint16_t blk_len)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  int8_t res = -1;
  uint32_t timer;
  uint16_t event = 0;
  BaseType_t status;

  UNUSED(lun);

  if (STORAGE_status_usbmsc_sd())
  {
    if ( HAL_SD_WriteBlocks_DMA(phsd, buf, (uint32_t)blk_addr, blk_len)==HAL_OK )
    {
      while(1)
      {
        status = xQueueReceiveFromISR(sdcard_cb_q_hdl, (void *)&event, &xHigherPriorityTaskWoken);
        if (status==pdTRUE) break;

        //if (res !=0) DBG_sd_wr_timeout_cnt++;
        m1_wdt_reset();
      }

      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

      if (event==SDCARD_CB_WRITE_CPLT_MSG)
      {
        timer = osKernelGetTickCount();
        while ( (osKernelGetTickCount() - timer) < MSC_SD_DATATIMEOUT )
        {
          if (HAL_SD_GetCardState(phsd)==HAL_SD_CARD_TRANSFER)
          {
            res = 0;
            break;
          }
          if (res !=0) DBG_sd_wr_timeout_cnt++;
          m1_wdt_reset();
        } // while (...)
      }
      else
      {
        if (res !=0) DBG_sd_rd_timeout_cnt++;
      }
    }
  }

  if (res !=0) DBG_sd_wr_timeout_cnt++;

  return (res);
}

/**
  * @brief  Returns the Max Supported LUNs.
  * @param  None
  * @retval Lun(s) number
  */
int8_t STORAGE_GetMaxLun(void)
{
  return (STORAGE_LUN_NBR - 1);
}


