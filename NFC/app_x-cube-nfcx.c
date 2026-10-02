
/**
  ******************************************************************************
  * File Name          :  app_x-cube-nfcx.c
  * Description        : This file provides code for the configuration
  *                      of the STMicroelectronics.X-CUBE-NFC6.3.1.0 instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rfal_platform.h"
#include "nfc_conf.h"
#include "app_x-cube-nfcx.h"
#include "m1_nfc.h"
#include "NFC_drv/legacy/nfc_poller.h"
#include "NFC_drv/legacy/nfc_listener.h"
#include "NFC_drv/legacy/m1_t2t_transport.h"
#if defined(M1_MFC_RAW_EMULATION)
#include "NFC_drv/common/m1_mfc_raw_listener.h"   /* AUTHRX ISR wrapper */
#endif
#include "uiView.h"                    // ← 추가: m1_app_send_q_message() 선언
#include "st25r3916.h"                 // ← 추가: st25r3916Deinitialize() 선언
#include "rfal_platform.h"
#include "rfal_nfc.h"
/* Includes ------------------------------------------------------------------*/

/** @defgroup App NFC6
  * @{
  */

/** @defgroup Main
  * @{
  */
 EXTI_HandleTypeDef USR_INT_LINE;

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Global variables ----------------------------------------------------------*/

uint8_t globalCommProtectCnt = 0;   /*!< Global Protection counter     */
bool isNFCDeviceOk = false;
bool isNFCCardFound = false;
#if 0
bool isNFCCardEmulation = false;
bool isNFCCardRead = false;
#endif

NFCTxType nfc_tx_type = NFC_TX_A; // Default to NFC-A

char NFC_Family[128] = "Unknown"; //
char NFC_Type[128] = "NFC-F"; // Default NFC Type, can be updated later
char NFC_UID[128] = "1122334455667788"; // Default NFC UID, can be updated later
uint8_t NFC_ID[20] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0}; // Buffer to hold NFC ID, max 20 bytes
uint8_t NFC_ID_LEN = 4;

char NFC_ATQA[2] = {0};
char NFC_SAK = 0;


/* NFC New Code Structure ----------------------------------------------------*/
void NFC_Polling_Init(void)
{
  //platformLog("NFC Polling Init\r\n");

   USR_INT_LINE.Line = USR_INT_LINE_NUM;
   USR_INT_LINE.RisingCallback = st25r3916Isr;

   /* Configure interrupt callback */
  (void)HAL_EXTI_GetHandle(&USR_INT_LINE, USR_INT_LINE.Line);
  (void)HAL_EXTI_RegisterCallback(&USR_INT_LINE, HAL_EXTI_COMMON_CB_ID, USR_INT_LINE.RisingCallback);// BSP_NFC0XCOMM_IRQ_Callback);

  HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_SET);

  /* Initalize RFAL */
  if( !ReadIni() )
  {
    platformLog("Initialization failed..\r\n");
    isNFCDeviceOk = false;
  }
  else
  {
    //platformLog("Initialization succeeded..\r\n");
    isNFCDeviceOk = true;
  }

  //platformLog("rfalNfcGetState() = %d", rfalNfcGetState());
   
}

void NFC_Listening_Init(void)
{
  platformLog("NFC Listening Init\r\n");

  USR_INT_LINE.Line = USR_INT_LINE_NUM;
#if defined(M1_MFC_RAW_EMULATION)
  /* AUTHRX: wrap the ST25R INT callback so, after the real st25r3916Isr() runs,
   * the NFC worker is woken immediately (only while an MFC_EMU session is armed).
   * Listener path only -- the poller (NFC_Polling_Init) keeps st25r3916Isr, and
   * the OFF build keeps the original registration (#else). */
  USR_INT_LINE.RisingCallback = m1_mfc_authrx_isr;
#else
  USR_INT_LINE.RisingCallback = st25r3916Isr;
#endif

   /* Configure interrupt callback */
  (void)HAL_EXTI_GetHandle(&USR_INT_LINE, USR_INT_LINE.Line);
  (void)HAL_EXTI_RegisterCallback(&USR_INT_LINE, HAL_EXTI_COMMON_CB_ID, USR_INT_LINE.RisingCallback);// BSP_NFC0XCOMM_IRQ_Callback);

  HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_SET);

  /* Initalize RFAL */
  if( !ListenIni() )
  {
    platformLog("Initialization failed..\r\n");
    isNFCDeviceOk = false;
  }
  else
  {
    //platformLog("Initialization succeeded..\r\n");
    isNFCDeviceOk = true;
  }

}

void NFC_Polling_Process(void)
{
  ReadCycle();
}

void NFC_Listening_Process(void)
{
  ListenerCycle(); 
}


void NFC_Polling_DeInit(void)
{
  //platformLog("NFC Polling DeInit\r\n");
  m1_app_send_q_message(main_q_hdl, Q_EVENT_NFC_READ_COMPLETE);
  rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
  st25r3916Deinitialize();
}

void NFC_Listening_DeInit(void)
{
  //platformLog("NFC Listening DeInit\r\n");
  /* Final safety net: guarantees the dedicated Type-2 transport is stopped
   * whenever the Listener role itself is torn down, even on an exit path
   * that didn't already stop it (NFC_T2TTransportProcess()'s stop-flag
   * check, or Q_EVENT_NFC_EMULATE_STOP in nfc_driver.c). Idempotent --
   * a no-op if already stopped, never a second real rfalListenStop(). */
  m1_t2t_transport_stop();
}

/**
  * @brief      SPI Read and Write byte(s) to device
  * @param[in]  pTxData : Pointer to data buffer to write
  * @param[out] pRxData : Pointer to data buffer for read data
  * @param[in]  Length : number of bytes to write
  * @return     BSP status
  */
int32_t BSP_NFC0XCOMM_SendRecv(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length)
{
  HAL_StatusTypeDef status = HAL_ERROR;
  int32_t ret = 0;// BSP_ERROR_NONE;

  if((pTxData != NULL) && (pRxData != NULL))
  {
    status = HAL_SPI_TransmitReceive(&COMM_HANDLE, (uint8_t *)pTxData, (uint8_t *)pRxData, Length, 2000);
  }
  else if ((pTxData != NULL) && (pRxData == NULL))
  {
    status = HAL_SPI_Transmit(&COMM_HANDLE, (uint8_t *)pTxData, Length, 2000);
  }
  else if ((pTxData == NULL) && (pRxData != NULL))
  {
    status = HAL_SPI_Receive(&COMM_HANDLE, (uint8_t *)pRxData, Length, 2000);
  }
  else
  {
  	ret = -2; //BSP_ERROR_WRONG_PARAM;
  }

  /* Check the communication status */
  if (status != HAL_OK)
  {
    /* Execute user timeout callback */
    // ret = BSP_NFC0XCOMM_Init();
  }

  return ret;
}

#if 0
/**
  * @brief  BSP SPI1 callback
  * @param  None
  * @return None
  */
__weak void BSP_NFC0XCOMM_IRQ_Callback(void)
{
  /* Prevent unused argument(s) compilation warning */

  /* This function should be implemented by the user application.
   * It is called into this driver when an event from ST25R3916 is triggered.
   */
  st25r3916Isr();
}
#endif

#ifdef __cplusplus
}
#endif

#if 0
 /**
   * @brief  This function is executed in case of error occurrence.
   * @retval None
   */
 void _Error_Handler(char * file, int line)
 {
 	return;


   /* USER CODE BEGIN Error_Handler_Debug */
   /* User can add his own implementation to report the HAL error return state */
   while (1)
   {
   }
   /* USER CODE END Error_Handler_Debug */
 }
#endif
