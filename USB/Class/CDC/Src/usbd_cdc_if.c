/**
  ******************************************************************************
  * @file    usbd_cdc_if_template.c
  * @author  MCD Application Team
  * @brief   Generic media access Layer.
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
#pragma GCC push_options
#pragma GCC optimize("O0")

/* Includes ------------------------------------------------------------------*/

#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "m1_cli.h"
#include "m1_log_debug.h"
#include "m1_usb_cdc_msc.h"
#include "m1_manager_protocol.h"   /* M1CP tee-and-gate seam */

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_CDC
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_CDC_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_CDC_Private_Defines
  * @{
  */
extern uint8_t DEBUG_esp32_reset_pin;

__ALIGN_BEGIN static uint8_t usbRxBuffer[USB_RX_BUF_SIZE] __ALIGN_END;
static volatile uint16_t usbRxBufIndex = 0;

#define LOGCLI_RX_FIFO_SIZE 256U
static uint8_t logcli_rx_fifo[LOGCLI_RX_FIFO_SIZE];
static volatile uint16_t logcli_rx_head = 0U;
static volatile uint16_t logcli_rx_tail = 0U;

static inline void logcli_rx_push(uint8_t ch)
{
  uint16_t next_head = (uint16_t)((logcli_rx_head + 1U) % LOGCLI_RX_FIFO_SIZE);
  if (next_head == logcli_rx_tail)
  {
    // FIFO full: drop oldest byte to keep latest CLI input responsive.
    logcli_rx_tail = (uint16_t)((logcli_rx_tail + 1U) % LOGCLI_RX_FIFO_SIZE);
  }

  logcli_rx_fifo[logcli_rx_head] = ch;
  logcli_rx_head = next_head;
}

int CDC_LogCli_GetChar(uint8_t *ch)
{
  if ((ch == NULL) || (logcli_rx_head == logcli_rx_tail))
  {
    return 0;
  }

  *ch = logcli_rx_fifo[logcli_rx_tail];
  logcli_rx_tail = (uint16_t)((logcli_rx_tail + 1U) % LOGCLI_RX_FIFO_SIZE);
  return 1;
}

void CDC_LogCli_ResetFifo(void)
{
  logcli_rx_head = 0U;
  logcli_rx_tail = 0U;
}

/* Push bytes into the legacy CLI input FIFO. Used by M1CP (task context) to
 * replay non-protocol / rejected-candidate bytes so legacy console behavior is
 * preserved when no valid HELLO handshake occurs. */
void CDC_LogCli_PushBytes(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  if (data == NULL) { return; }
  for (i = 0U; i < len; i++)
  {
    logcli_rx_push(data[i]);
  }
}
/**
  * @}
  */


/** @defgroup USBD_CDC_Private_Macros
  * @{
  */

/**
  * @}
  */

/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */

/** Received data over USB are stored in this buffer      */
__ALIGN_BEGIN uint8_t UserRxBufferFS[USB_FS_CHUNK_SIZE] __ALIGN_END;

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[USB_FS_CHUNK_SIZE];

extern USBD_HandleTypeDef hUsbDeviceFS;


/** @defgroup USBD_CDC_Private_FunctionPrototypes
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

static inline void CDC_SelectActiveClass(void)
{
  /* No-op: avoid mutating global classId from task context in composite mode. */
}

void CDC_Rearm_FS(void)
{
#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
  USBD_CDC_HandleTypeDef *hcdc;

  CDC_SelectActiveClass();
  hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassDataCmsit[CDC_InstID];
  if (hcdc == NULL)
  {
    return;
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0, CDC_InstID);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);
#elif M1_USB_CONFIG == M1_CFG_USB_CDC
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

  if (hcdc == NULL)
  {
    return;
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
#endif

  hcdc->TxState = 0;
  hcdc->RxState = 0;
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_InstID);
}

void CDC_LogCli_ForceRecover_FS(void)
{
#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
  USBD_CDC_HandleTypeDef *hcdc;

  CDC_SelectActiveClass();
  hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassDataCmsit[CDC_InstID];
  if (hcdc == NULL)
  {
    return;
  }

  (void)USBD_LL_FlushEP(&hUsbDeviceFS, CDC_IN_EP);
  (void)USBD_LL_FlushEP(&hUsbDeviceFS, CDC_OUT_EP);
  hcdc->TxState = 0;
  hcdc->RxState = 0;
  CDC_LogCli_ResetFifo();
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0, CDC_InstID);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_InstID);
#elif M1_USB_CONFIG == M1_CFG_USB_CDC
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

  if (hcdc == NULL)
  {
    return;
  }

  (void)USBD_LL_FlushEP(&hUsbDeviceFS, CDC_IN_EP);
  (void)USBD_LL_FlushEP(&hUsbDeviceFS, CDC_OUT_EP);
  hcdc->TxState = 0;
  hcdc->RxState = 0;
  CDC_LogCli_ResetFifo();
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  CDC_SelectActiveClass();
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
#endif
}

USBD_CDC_ItfTypeDef USBD_CDC_Interface_fops =
{
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  CDC_Init_FS
  *         Initializes the CDC media low layer
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
    // USE_USBD_COMPOSITE
  CDC_SelectActiveClass();
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0, CDC_InstID);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);
#elif M1_USB_CONFIG == M1_CFG_USB_CDC
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
#endif

    return (0);
}

/**
  * @brief  CDC_De\Init_FS
  *         DeInitializes the CDC media low layer
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
    /* Physical disconnect/host-reset tears down the CDC class through this
     * same DeInit path (USBD_LL_Reset -> USBD_ClrClassConfig -> class
     * DeInit) regardless of what M1CP was in the middle of -- an Add Files
     * transfer, an in-progress firmware update, etc. Without this, a link
     * drop mid-transfer left M1CP's session/transfer/update state stuck
     * until the next byte happened to resync it. */
    m1cp_notify_link_down();

    return (0);
}


volatile int esp32_reset_en = 0;

/**
  * @brief  CDC_Control_FS
  *         Manage the CDC class requests
  * @param  Cmd: Command code
  * @param  Buf: Buffer containing command data (request parameters)
  * @param  Len: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    UNUSED(length);

    switch (cmd)
    {
        case CDC_SEND_ENCAPSULATED_COMMAND:
            /* Add your code here */
            break;

        case CDC_GET_ENCAPSULATED_RESPONSE:
            /* Add your code here */
            break;

        case CDC_SET_COMM_FEATURE:
            /* Add your code here */
            break;

        case CDC_GET_COMM_FEATURE:
            /* Add your code here */
            break;

        case CDC_CLEAR_COMM_FEATURE:
            /* Add your code here */
            break;

        case CDC_SET_LINE_CODING:
            linecoding.bitrate    = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | \
                                             (pbuf[2] << 16) | (pbuf[3] << 24));
            linecoding.format     = pbuf[4];
            linecoding.paritytype = pbuf[5];
            linecoding.datatype   = pbuf[6];

            if ((m1_usbcdc_mode == CDC_MODE_VCP) || (m1_usbcdc_mode == CDC_MODE_ESP32))
            {
                m1_usb_cdc_comconfig();
            }
            break;

        case CDC_GET_LINE_CODING:
            pbuf[0] = (uint8_t)(linecoding.bitrate);
            pbuf[1] = (uint8_t)(linecoding.bitrate >> 8);
            pbuf[2] = (uint8_t)(linecoding.bitrate >> 16);
            pbuf[3] = (uint8_t)(linecoding.bitrate >> 24);
            pbuf[4] = linecoding.format;
            pbuf[5] = linecoding.paritytype;
            pbuf[6] = linecoding.datatype;

            /* Add your code here */
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            // pbuf[2]: wValue's Low Byte (RTS, DTR information)
            // bit 1: RTS (Ready To Send)
            // bit 0: DTR (Data Terminal Ready)

            uint8_t dtr = (pbuf[2] & 0x01);         // Bit 0
            uint8_t rts = (pbuf[2] & 0x02) >> 1;    // Bit 1

            if (m1_usbcdc_mode == CDC_MODE_ESP32)
            {
                // 1. EN(Reset) 핀 제어 로직 (2-Transistor 에뮬레이션 유지)
                // DTR과 RTS가 서로 다를 때만 EN을 Low로 내려서 확실한 리셋 구간을 확보합니다.
                if (dtr ^ rts)
                { // XOR 연산: 두 신호가 다를 때만 Reset
                    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET);

                    osDelay(20); DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);

                    for(volatile int i=0; i<5000; i++); // 미세 지연으로 대체
                }
                else
                {
                    // 신호가 같아지면 즉시 High로 복구하여 부팅 시작 유도
                    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_SET);

                    osDelay(20); DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);

                    esp32_reset_en++;
                }
            }
            break;

        case CDC_SEND_BREAK:
            /* Add your code here */
            break;

        default:
            break;
    }

    return (USBD_OK);
}

/**
  * @brief  CDC_Receive_FS
  *         Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */

uint32_t DEBUG_ovf_cnt = 0;
uint32_t DEBUG_max_usbRxBufIndex = 0;
uint32_t DEBUG_max_ReceivedBytesBuf = 0;
uint32_t DEBUG_max_sentBytes = 0;
uint32_t DEBUG_paused_cnt = 0;
uint32_t DEBUG_prev_data[4];
uint32_t DEBUG_ovrl_cnt = 0;

volatile uint32_t dbg_cdc_rx_total = 0;
volatile uint32_t dbg_cdc_rx_bridge = 0;
volatile uint32_t dbg_cdc_rx_logcli = 0;
volatile uint32_t dbg_cdc_rx_notify = 0;
volatile uint32_t dbg_cdc_rx_last_len = 0;
volatile uint32_t dbg_cdc_rx_last_mode = 0;

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    size_t sentBytes;
    size_t freeSpace;
    size_t ReceivedBytesBuf;

    dbg_cdc_rx_total++;
    dbg_cdc_rx_last_len = *Len;
    dbg_cdc_rx_last_mode = (uint32_t)m1_usbcdc_mode;

    if ((m1_usbcdc_mode == CDC_MODE_VCP) || (m1_usbcdc_mode == CDC_MODE_ESP32))
    {
      dbg_cdc_rx_bridge++;
        if (h_usb_rx_streambuf == NULL || *Len == 0)
        {
          // Always re-arm RX even if bridge stream buffer is not ready yet.
          // Otherwise a single early packet can stop further OUT callbacks
          // until the host reopens/reconfigures the port.
          USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);
          USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_InstID);
          return USBD_OK;
        }
        /* Input 64 bytes of data into usbRxBuffer */
        if ((usbRxBufIndex + *Len) < USB_RX_BUF_SIZE)
        {
            memcpy(&usbRxBuffer[usbRxBufIndex], Buf, *Len);
            usbRxBufIndex += *Len;

            // debug-m1
            //if (usbRxBufIndex > DEBUG_max_usbRxBufIndex) DEBUG_max_usbRxBufIndex = usbRxBufIndex;
        }
        else
        {
            /* Handle usbRxBuffer overflow */
            //printf("Overflow - CDC RX Buffer!\r\n");
            // debug-m1
            DEBUG_ovf_cnt++;
        }

        /* Check for empty space in the stream buffer */
        freeSpace = xStreamBufferSpacesAvailable(h_usb_rx_streambuf);

        if (freeSpace > 0 && usbRxBufIndex > 0)
        {
            /* Transfer usbRxBuffer data to the empty space in the Stream Buffer. */
            ReceivedBytesBuf = (usbRxBufIndex < freeSpace) ? usbRxBufIndex : freeSpace;

            // debug-m1
            //if (ReceivedBytesBuf > DEBUG_max_ReceivedBytesBuf) DEBUG_max_ReceivedBytesBuf = ReceivedBytesBuf;

            sentBytes = xStreamBufferSendFromISR(h_usb_rx_streambuf,
                                             (void *)usbRxBuffer,
                                             ReceivedBytesBuf,
                                             &xHigherPriorityTaskWoken);
            if (sentBytes > 0)
            {
                /* Move/remove intermediate buffer data as much as transferred to Stream Buffer. */
                if (sentBytes < usbRxBufIndex) {
                    /* Move forward if there is remaining data*/
                    memmove(usbRxBuffer, usbRxBuffer + sentBytes, usbRxBufIndex - sentBytes);
                }
                // debug-m1
                //if (sentBytes > DEBUG_max_sentBytes) DEBUG_max_sentBytes = sentBytes;

                usbRxBufIndex -= sentBytes;

                /* Notify Task */
                if (usb2ser_task_hdl != NULL)
                {
                    vTaskNotifyGiveFromISR(usb2ser_task_hdl, &xHigherPriorityTaskWoken);
                }
            }
        }

        if (freeSpace == 0)
        {
            usbcdc_rx_paused = 1;
        }

        if (usbcdc_rx_paused == 0)
        {
          USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);

            //Buf[*Len] = 0;
            USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_InstID);

            //debugt-m1
            DEBUG_paused_cnt = 0;
        }
        else
        {
            //debugt-m1
            DEBUG_paused_cnt++;

          // If RX is paused because stream buffer is full, ensure the consumer
          // task wakes up and drains existing bytes; otherwise RX can stall.
          if ((usb2ser_task_hdl != NULL) && (xStreamBufferBytesAvailable(h_usb_rx_streambuf) > 0U))
          {
            vTaskNotifyGiveFromISR(usb2ser_task_hdl, &xHigherPriorityTaskWoken);
          }
        }

        if (xHigherPriorityTaskWoken == pdTRUE)
        {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
    else  // == CDC_MODE_LOG_CLI
    {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      uint32_t i;

        dbg_cdc_rx_logcli++;
      USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS, CDC_InstID);
      USBD_CDC_ReceivePacket(&hUsbDeviceFS, CDC_InstID);

        if (*Len > 0U)
        {
          /* M1CP sniff-and-hold seam: feed all RX to the manager parser (ISR-safe
           * ingest only; parsing/dispatch happen in the CLI task via
           * m1cp_process()). M1CP owns the byte stream and decides, in task
           * context, which non-protocol bytes are replayed to the legacy CLI - a
           * valid HELLO handshake is consumed and never echoed. Only if M1CP is
           * not initialized yet do we push to the CLI FIFO directly here, so the
           * legacy console still works at early boot. */
          if (!m1cp_rx_from_isr(Buf, (uint16_t)*Len, &xHigherPriorityTaskWoken))
          {
            for (i = 0U; i < *Len; i++)
            {
              logcli_rx_push(Buf[i]);
            }
          }

          // Wake CLI task after FIFO push. Use no-value notify so repeated
          // packets cannot clobber a previous notify value across mode changes.
          if (cmdLineTaskHandle != NULL)
          {
            xTaskNotifyFromISR(cmdLineTaskHandle, 0U, eNoAction, &xHigherPriorityTaskWoken);
            dbg_cdc_rx_notify++;
          }
        }
        if (xHigherPriorityTaskWoken == pdTRUE)
        {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } //if ((m1_usbcdc_mode == CDC_MODE_VCP) || (m1_usbcdc_mode == CDC_MODE_ESP32))

    return (USBD_OK);
}



/*
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted over USB OUT endpoint 1
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data sent (in bytes)
  * @param  epnum: Endpoint number
  * @retval USBD_OK: Results of operation
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    uint8_t result = USBD_OK;
    uint8_t q_item = 0;
    portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

    // Bridge/VCP TX completed: release sender task for next chunk.
    if ((cdc_tx_owner_mode != CDC_MODE_LOG_CLI) && (ser2usb_task_semaphore != NULL))
    {
      xSemaphoreGiveFromISR(ser2usb_task_semaphore, &xHigherPriorityTaskWoken);
    }

    // LOG_CLI TX completed: progress ringbuffer and enqueue next chunk.
    // Owner-aware completion is required because bridge completions can arrive
    // after a mode switch and must not consume CLI buffered output.
    /* During an M1CP session the log path is gated off and CDC TX carries
     * protocol frames, so a completion must not advance the log ring buffer. */
    if ((cdc_tx_owner_mode == CDC_MODE_LOG_CLI) && (!m1cp_session_active()))
    {
      m1_logdb_update_tx_buffer();
      if ((log_q_hdl != NULL) && (!m1_logdb_check_empty_state()))
      {
        xQueueSendFromISR(log_q_hdl, &q_item, &xHigherPriorityTaskWoken);
      }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

  return result;
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted over USB OUT endpoint 1
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data sent (in bytes)
  * @param  epnum: Endpoint number
  * @retval USBD_OK: Results of operation
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
    uint8_t result = USBD_OK;

#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
    USBD_CDC_HandleTypeDef *hcdc;

    // MSC+CDC Composite
    hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassDataCmsit[CDC_InstID];
    if ((hcdc == NULL) || (hcdc->TxState != 0))
    {
        return USBD_BUSY;
    }
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len, CDC_InstID);
    result = USBD_CDC_TransmitPacket(&hUsbDeviceFS, CDC_InstID);

#elif M1_USB_CONFIG == M1_CFG_USB_CDC
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

    if ((hcdc == NULL) || (hcdc->TxState != 0))
    {
        return USBD_BUSY;
    }

    // CDC only
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
    result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
#endif

    return result;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

#pragma GCC push_options
