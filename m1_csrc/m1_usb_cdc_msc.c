/* See COPYING.txt for license details. */

/*
 * m1_usb_cdc.c
 *
 *      Author:
 */


/*************************** I N C L U D E S **********************************/
/* Standard includes. */
/* Utils includes. */

#include "m1_usb_cdc_msc.h"
#include "stm32h5xx.h"
#include "main.h"
#include "diskio.h"
#include "app_freertos.h"
#include "m1_sys_init.h"
#include "cli_app.h"
#include "m1_cli.h"
#include "m1_compile_cfg.h"
#include "m1_sdcard.h"

/*************************** D E F I N E S ************************************/

#define CHUNK_SIZE		512


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/****************************************************************************/
/* USB CDC Line Coding Structure                                            */
/*                                                                          */
/* Offset Field         Size  Description                                   */
/* 0      dwDTERate     4     Data rate, in bits per second                 */
/* 4      bCharFormat   1     Stop bits                                     */
/*                              0 - 1 Stop bit                              */
/*                              1 - 1.5 Stop bits                           */
/*                              2 - 2 Stop bits                             */
/* 5      bParityType   1     Parity                                        */
/*                              0 - None                                    */
/*                              1 - Odd                                     */
/*                              2 - Even                                    */
/*                              3 - Mark                                    */
/*                              4 - Space                                   */
/* 6      bDataBits     1     Number Data bits (5, 6, 7, 8 or 16)           */
/****************************************************************************/
/* STM32 HAL Parameters                                                     */
/*                                                                          */
/* #define UART_STOPBITS_0_5                                                */
/* #define UART_STOPBITS_1                                                  */
/* #define UART_STOPBITS_1_5                                                */
/* #define UART_STOPBITS_2                                                  */
/*                                                                          */
/* #define UART_PARITY_NONE                                                 */
/* #define UART_PARITY_EVEN                                                 */
/* #define UART_PARITY_ODD                                                  */
/*                                                                          */
/* #define UART_WORDLENGTH_7B                                               */
/* #define UART_WORDLENGTH_8B                                               */
/* #define UART_WORDLENGTH_9B                                               */
/****************************************************************************/
USBD_CDC_LineCodingTypeDef linecoding =
{
  115200, /* baud rate*/
  0x00,   /* stop bits-1*/
  0x00,   /* parity - none*/
  0x08    /* nb. of bits 8*/
};

/***************************** V A R I A B L E S ******************************/

USBD_HandleTypeDef hUsbDeviceFS;
PCD_HandleTypeDef hpcd_USB_DRD_FS;

StreamBufferHandle_t h_uart_rx_streambuf;
SemaphoreHandle_t ser2usb_task_semaphore;
SemaphoreHandle_t ser2usb_tx_semaphore;

StreamBufferHandle_t h_usb_rx_streambuf;
SemaphoreHandle_t usb2ser_task_semaphore;
SemaphoreHandle_t usb2ser_tx_semaphore;

uint8_t usb_tx_temp_buffer[USB_TX_BUF_SIZE];

volatile uint16_t head_usart1_dma = 0;
volatile uint16_t tail_usart1_dma = 0;

volatile uint16_t head_usbcdc_rx = 0;
volatile uint16_t tail_usbcdc_rx = 0;

volatile uint8_t usbcdc_rx_paused = 0;
volatile int8_t m1_USB_CDC_ready = 0;  // 0=ready, -1=not

// USB MSC
/*
 * msc_sd_stat
 * #define STA_NOINIT    0x01  // Drive not initialized
 * #define STA_NODISK    0x02  // No medium in the drive
 * #define STA_PROTECT   0x04  // Write protected
*/
volatile uint8_t msc_sd_stat = STA_NOINIT;
volatile int8_t m1_USB_MSC_ready = -1; // 0 = ready, -1=not ready

#if M1_USB_MODE == M1_CFG_USB_CDC_MSC
uint8_t MSC_EpAdd_Inst[2] = {MSC_IN_EP, MSC_OUT_EP};              /* MSC Endpoint Addresses array */
uint8_t CDC_EpAdd_Inst[3] = {CDC_IN_EP, CDC_OUT_EP, CDC_CMD_EP};  /* CDC Endpoint Addresses array */
#elif M1_USB_MODE == M1_CFG_USB_MSC
uint8_t MSC_EpAdd_Inst[2] = {MSC_IN_EP, MSC_OUT_EP};              /* MSC Endpoint Addresses array */
#elif M1_USB_MODE == M1_CFG_USB_CDC
uint8_t CDC_EpAdd_Inst[3] = {CDC_IN_EP, CDC_OUT_EP, CDC_CMD_EP};  /* CDC Endpoint Addresses array */
#endif

uint8_t CDC_InstID = 0;
uint8_t MSC_InstID = 0;

#if 1
enCdcMode m1_usbcdc_mode = CDC_MODE_LOG_CLI;
#else
enCdcMode m1_usbcdc_mode = CDC_MODE_VCP;
#endif

//#define TASKDELAY_Usb2Ser_handler_task  100 //ms
//#define TASKDELAY_Ser2Usb_handler_task  100 //ms

//osThreadId_t usb2ser_task_hdl;
//osThreadId_t ser2usb_task_hdl;

TaskHandle_t usb2ser_task_hdl;
TaskHandle_t ser2usb_task_hdl;

uint32_t DEBUG_dma_timeout = 0;
uint32_t DEBUG_bytes_to_send = 0;
uint32_t DEBUG_received_bytes = 0;
volatile uint8_t tx_cptl_usart1 = 0;

const osThreadAttr_t Usb2SerTask_attributes = {
  .name = "Usb2SerTask",
  .priority = (osPriority_t)TASK_PRIORITY_LOG_DB_HANDLER,
  //.stack_size = M1_TASK_STACK_SIZE_4096
  .stack_size = M1_TASK_STACK_SIZE_2048
};

const osThreadAttr_t Ser2UsbTask_attributes = {
  .name = "Ser2UsbTask",
  .priority = (osPriority_t)TASK_PRIORITY_LOG_DB_HANDLER,
  //.stack_size = M1_TASK_STACK_SIZE_4096
  .stack_size = M1_TASK_STACK_SIZE_2048
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t m1_usb_msc_process(void);
uint8_t m1_usb_msc_sd_detected(void);
void vSer2UsbTask(void *pvParameters);
void CDC_Signal_Next_Tx(void);
void vUsb2SerTask(void *pvParameters);
void usart_rxupdate_head_pointer(void);
uint16_t usart_rxget_data_length(void);
static void cdc_start_usb2ser(void);
void m1_usb_cdc_comdefault(void);
void m1_usb_cdc_comconfig(void);
void usb_cdc_init(void);
static void usb_cdc_deinit(void);
void MX_USB_PCD_Init(void);
void HAL_PCD_MspInit(PCD_HandleTypeDef* hpcd);
void HAL_PCD_MspDeInit(PCD_HandleTypeDef* hpcd);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/*
 * Return TRUE if USB MSC status is present.
 *
 */
/******************************************************************************/
uint8_t m1_usb_msc_process(void)
{

#if 1

//  if (m1_sd_detected() == 0)
//  {
//    m1_USB_MSC_ready = -1;
//  }

  return TRUE; // DEBUG : USB MSC process running always

#else
  if (m1_sd_detected() == 0)
  {
    // not sd card
    m1_USB_MSC_ready = -1;
    return FALSE; // No USB MSC process
  }

  if (m1_USB_MSC_ready == -1)
  { // not ready USB MSC
    return FALSE; // No USB MSC process
  }

  return TRUE;   // USB MSC process running
#endif
}


/******************************************************************************/
/*
 * Return TRUE if an SD card is detected (causing the Detect Switch to pull low)
 *
 */
/******************************************************************************/
uint8_t m1_usb_msc_sd_detected(void)
{
  uint8_t detected = FALSE;

#if 1
  // FAT system
  detected = m1_sd_detected();

#else
  if (m1_USB_MSC_ready == 0)
  {
    // USB MSC
    if (sdcard_ctl.status != SD_access_NotReady)
    {
      detected = FALSE;
    }
  }
  else
  {
    // FAT system
    detected = m1_sd_detected();
  }
#endif

  return detected;
}


/*============================================================================*/
/**
  * @brief  USB CDC handler task - USART1 to USB CDC
  * @param
  * @retval None
  */
/*============================================================================*/
void vSer2UsbTask(void *pvParameters)
{
  size_t received_bytes;

  UNUSED(pvParameters);

  // USB CDC init...
  usb_cdc_init();

  cdc_start_usb2ser();

  for(;;)
  {
    /* Read data from the USART RX Stream Buffer (blocking) */
    received_bytes = xStreamBufferReceive(h_uart_rx_streambuf,
                                          (void *)usb_tx_temp_buffer,
                                          sizeof(usb_tx_temp_buffer),
                                          portMAX_DELAY);
    if (received_bytes > 0)
    {
      /* Wait for previous USB transfer to complete (return semaphore in TxCpltCallback) */
      //if(xSemaphoreTake(ser2usb_task_semaphore, portMAX_DELAY) == pdTRUE)
      if(xSemaphoreTake(ser2usb_task_semaphore, pdMS_TO_TICKS( 500 )) == pdTRUE)
      {
        if ((hUsbDeviceFS.pClassData != NULL) &&
            (m1_USB_CDC_ready == 0))
        {
          /* Data transfer request */
          if (CDC_Transmit_FS(usb_tx_temp_buffer, received_bytes) != USBD_OK)
          { // error handling
            // Initialize flags and immediately return semaphore when transmission fails
            xSemaphoreGive(ser2usb_task_semaphore);
          }
          // semaphore is given in TxCpltCallback when successful (not returned here)
        }
        else
        {
          // usb cable plug off
          xSemaphoreGive(ser2usb_task_semaphore);
        }
      }
      else
      { // Error !!, cancel data
        xSemaphoreGive(ser2usb_task_semaphore);
      }
    }
    //vTaskDelay(pdMS_TO_TICKS(TASKDELAY_Ser2Usb_handler_task));
    //m1_wdt_send_report(M1_REPORT_ID_SER2USB_HANDLER_TASK, TASKDELAY_Ser2Usb_handler_task);
  }
} // void vSer2UsbTask(void *pvParameters)


/*============================================================================*/
/**
  * @brief  Called by CDC_TransmitCplt_FS when a USB transfer is complete to notify the next transfer.
  * @param
  * @retval
  */
/*============================================================================*/
void CDC_Signal_Next_Tx(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ser2usb_task_semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*============================================================================*/
/**
  * @brief  USB CDC handler task - USB CDC to USART1
  * @param
  * @retval
  */
/*============================================================================*/
void vUsb2SerTask(void *pvParameters)
{
  size_t received_bytes;
  size_t bytes_to_send;
  size_t bytes_remaining;
  uint8_t* current_buffer_ptr;
  enCdcMode prev_cdc_mode = m1_usbcdc_mode;

  UNUSED(pvParameters);

  const TickType_t xMaxBlockTime = pdMS_TO_TICKS(500);

  for(;;)
  {
#if 0
    /* Wait indefinitely until data arrives */
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    /* USB CDC Mode change */
    // debug : USB CDC mode changing test
    if (prev_cdc_mode != m1_usbcdc_mode)
    {
      prev_cdc_mode = m1_usbcdc_mode;
      //usb_cdc_init();

      m1_usb_cdc_comconfig();
    }

#if 0
                  // debug : USB CDC mode changing test
                  #include "m1_usb_cdc_msc.h"
                  extern enCdcMode m1_usbcdc_mode;


                  if (m1_usbcdc_mode == CDC_MODE_LOG_CLI)
                  {
                    m1_usbcdc_mode = CDC_MODE_VCP;
                  }
                  else
                  {
                    m1_usbcdc_mode = CDC_MODE_LOG_CLI;
                  }
#endif

#else
    /* Wait indefinitely until data arrives */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif

    size_t data_available = xStreamBufferBytesAvailable(h_usb_rx_streambuf);
    if (data_available == 0)
      continue; // If there is no data, wait again

#if 0
    if( xSemaphoreTake(usb2ser_tx_semaphore, portMAX_DELAY) != pdTRUE ) // Remove or keep a mutex
    {
    }
#endif
    received_bytes = xStreamBufferReceive(h_usb_rx_streambuf,
                                          (void *)logdb_tx_buffer,
                                          M1_LOGDB_TX_BUFFER_SIZE, 0);

    if (DEBUG_received_bytes < received_bytes) DEBUG_received_bytes = received_bytes;

    if (received_bytes > 0)
    {
      bytes_remaining = received_bytes;
      current_buffer_ptr = logdb_tx_buffer;

      while(bytes_remaining > 0)
      {
        bytes_to_send = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;
        //bytes_to_send = (bytes_remaining > M1_LOGDB_TX_BUFFER_SIZE) ? M1_LOGDB_TX_BUFFER_SIZE : bytes_remaining;

if (DEBUG_bytes_to_send < bytes_to_send) DEBUG_bytes_to_send = bytes_to_send;

        // Start USART1 transmission using GPDMA1 channel 1

        tx_cptl_usart1 = 1;
        __DSB();  /* Ensure flag is visible before DMA starts */

        if (HAL_UART_Transmit_DMA(&huart_logdb, current_buffer_ptr, bytes_to_send) == HAL_OK)
        {
          /* Wait for DMA completion via polling with bounded timeout.
           * Using osDelay(1) keeps the task yielding and avoids busy-wait.
           * A hard timeout prevents indefinite stall if DMA never completes. */
          uint32_t dma_wait_ticks = 0;
          while (tx_cptl_usart1 == 1)
          {
            osDelay(1);
            dma_wait_ticks++;
            if (dma_wait_ticks >= (uint32_t)xMaxBlockTime)
            {
              /* DMA completion missed — abort and recover */
              HAL_UART_DMAStop(&huart_logdb);
              huart_logdb.gState = HAL_UART_STATE_READY;
              tx_cptl_usart1 = 0;
              DEBUG_dma_timeout++;
              break;
            }
          }
          //osDelay(1); //osDelay(10);

          current_buffer_ptr += bytes_to_send;
          bytes_remaining -= bytes_to_send;
        }
        else
        {
          //printf("HAL_UART_Transmit_DMA Failed to Start!\r\n");
          break;
        }
      }

      // After processing all data, if it was stopped, request to resume.
      // Use a critical section to atomically clear the paused flag and
      // re-arm the USB endpoint, preventing the ISR from seeing an
      // inconsistent state between the flag and endpoint readiness.
      if (usbcdc_rx_paused == 1)
      {
        taskENTER_CRITICAL();
        usbcdc_rx_paused = 0;
        __DSB();  /* Ensure paused flag write is committed before re-arming */
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
        taskEXIT_CRITICAL();
      }
    }

    // if (xCommsSemaphore != NULL) xSemaphoreGive(xCommsSemaphore); // Returning a mutex

    //vTaskDelay(pdMS_TO_TICKS(TASKDELAY_Usb2Ser_handler_task));
    //m1_wdt_send_report(M1_REPORT_ID_USB2SER_HANDLER_TASK, TASKDELAY_Usb2Ser_handler_task);
  }
} // void vUsb2SerTask(void *pvParameters)


/*============================================================================*/
/**
  * @brief  Update the head pointer based on the DMA counter.
  * @param
  * @retval
  */
/*============================================================================*/
void usart_rxupdate_head_pointer(void)
{
  head_usart1_dma = (M1_LOGDB_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart_logdb.hdmarx)) % M1_LOGDB_RX_BUFFER_SIZE;
}


/*============================================================================*/
/**
  * @brief  Returns the valid length of data currently in the ring buffer.
  * @param
  * @retval
  */
/*============================================================================*/
uint16_t usart_rxget_data_length(void)
{
  if (head_usart1_dma >= tail_usart1_dma) {
      return head_usart1_dma - tail_usart1_dma;
  } else {
    return M1_LOGDB_RX_BUFFER_SIZE - tail_usart1_dma + head_usart1_dma;
  }
}


/*============================================================================*/
/**
  * @brief Called from an ISR to move data into the stream buffer.
  * This function runs in the context of the ISR.
  * @param
  * @retval
  */
/*============================================================================*/
void usart_rxdata_process_from_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint16_t current_len;

    uint16_t prev_tail = tail_usart1_dma; // Temporarily save the current tail position
    current_len = usart_rxget_data_length();   // Calculate total length to process

    if (current_len > 0) {
        uint16_t part1_len = (prev_tail + current_len) > M1_LOGDB_RX_BUFFER_SIZE ? (M1_LOGDB_RX_BUFFER_SIZE - prev_tail) : current_len;
        uint16_t part2_len = current_len - part1_len;

        if (part1_len > 0) {
            xStreamBufferSendFromISR(h_uart_rx_streambuf, (void*)&logdb_rx_buffer[prev_tail], part1_len, &xHigherPriorityTaskWoken);
        }
        if (part2_len > 0) {
            xStreamBufferSendFromISR(h_uart_rx_streambuf, (void*)&logdb_rx_buffer, part2_len, &xHigherPriorityTaskWoken);
        }

        // Update the volatile variable tail only after data copying is complete
        tail_usart1_dma = (prev_tail + current_len) % M1_LOGDB_RX_BUFFER_SIZE;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*============================================================================*/
/**
  * @brief start usb2ser task
  * @param
  * @retval
  */
/*============================================================================*/
static void cdc_start_usb2ser(void)
{
  // Prepare usart1 rx to usb tx */
  h_uart_rx_streambuf = xStreamBufferCreate(RXSTREAMBUF_UART_SIZE, 1);
  ser2usb_task_semaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(ser2usb_task_semaphore);

  /* Prepare usb rx to usart tx */
  h_usb_rx_streambuf = xStreamBufferCreate(RXSTREAMBUF_USB_SIZE, 1);

  usb2ser_task_semaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(usb2ser_task_semaphore);

  usb2ser_tx_semaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(usb2ser_tx_semaphore);

  usb2ser_task_hdl = osThreadNew(vUsb2SerTask, NULL, &Usb2SerTask_attributes);
}


/*============================================================================*/
/**
  * @brief  m1_usb_cdc_comdefault
  *         Configure the COM Port with the default parameters.
  * @param
  * @retval
  */
/*============================================================================*/
void m1_usb_cdc_comdefault(void)
{
  /* Stop bit */
  if (huart_logdb.Init.StopBits == UART_STOPBITS_1)
    linecoding.format = 0;
  else if (huart_logdb.Init.StopBits == UART_STOPBITS_2)
    linecoding.format = 1;

  /* parity bit*/
  if (huart_logdb.Init.Parity == UART_PARITY_NONE)
    linecoding.paritytype = 0;
  else if (huart_logdb.Init.Parity == UART_PARITY_ODD)
    linecoding.paritytype = 1;
  else if (huart_logdb.Init.Parity == UART_PARITY_EVEN)
    linecoding.paritytype = 2;

  /* data type : 8bits and 9bits is supported */
  if ((huart_logdb.Init.WordLength == UART_WORDLENGTH_8B) &&
      (huart_logdb.Init.Parity != UART_PARITY_NONE))
  {
      linecoding.datatype = 7;
  }
  else
    linecoding.datatype = 8;
}


/*============================================================================*/
/**
  * @brief  m1_usb_cdc_comconfig
  *         Configure the COM Port with the parameters received from host.
  * @param  None.
  * @retval None.
  * @note   When a configuration is not supported, a default value is used.
  */
/*============================================================================*/
void m1_usb_cdc_comconfig(void)
{
	/* Deinitialize USART1 */
	m1_logdb_deinit();

	/* Initialize USART1 */
	/* Stop bit */
	switch (linecoding.format)
	{
		case 0:
			huart_logdb.Init.StopBits = UART_STOPBITS_1;
			break;

		case 2:
			huart_logdb.Init.StopBits = UART_STOPBITS_2;
			break;

		default :
			huart_logdb.Init.StopBits = UART_STOPBITS_1;
			break;
	}

	/* parity bit*/
	switch (linecoding.paritytype)
	{
		case 0:
			huart_logdb.Init.Parity = UART_PARITY_NONE;
			break;

		case 1:
			huart_logdb.Init.Parity = UART_PARITY_ODD;
			break;

		case 2:
			huart_logdb.Init.Parity = UART_PARITY_EVEN;
			break;

		default :
			huart_logdb.Init.Parity = UART_PARITY_NONE;
			break;
	}

	/* data type : 8bits and 9bits is supported */
	switch (linecoding.datatype)
	{
		case 0x07:
			/* configuration a parity (Even or Odd) */
			huart_logdb.Init.WordLength = UART_WORDLENGTH_8B;
			break;

		case 0x08:
			if(huart_logdb.Init.Parity == UART_PARITY_NONE)
			{
				huart_logdb.Init.WordLength = UART_WORDLENGTH_8B;
			}
			else
			{
				huart_logdb.Init.WordLength = UART_WORDLENGTH_9B;
			}
			break;

		default:
			huart_logdb.Init.WordLength = UART_WORDLENGTH_8B;
			break;
	}

	huart_logdb.Init.BaudRate = linecoding.bitrate;

	m1_logdb_init();
}

/*============================================================================*/
/**
  * @brief  USB-CDC Initialization Function
  * @param  None.
  * @retval None.
  * @note   When a configuration is not supported, a default value is used.
  */
/*============================================================================*/
void usb_cdc_init(void)
{
////  __disable_irq();
//
  m1_usb_cdc_comdefault();
//  m1_usb_cdc_comconfig();
//
////  __enable_irq();

  head_usart1_dma = 0;
  tail_usart1_dma = 0;

  head_usbcdc_rx = 0;
  tail_usbcdc_rx = 0;

  usbcdc_rx_paused = 0;
  m1_USB_CDC_ready = 0;

  if (hpcd_USB_DRD_FS.State == HAL_PCD_STATE_RESET)
  {
    MX_USB_PCD_Init();
  }
} // void usb_cdc_init(void)


/*============================================================================*/
/**
  * @brief USB-CDC Deinitialization Function
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void usb_cdc_deinit(void)
{
  if (hpcd_USB_DRD_FS.State != HAL_PCD_STATE_RESET)
  {
    USBD_DeInit(&hUsbDeviceFS);
  }
} // void usb_cdc_deinit(void)


/*============================================================================*/
/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
/*============================================================================*/
void MX_USB_PCD_Init(void)
{
  /* USER CODE BEGIN USB_Init 0 */
  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */
  /* USER CODE END USB_Init 1 */
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
  hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */
  if(USBD_Init(&hUsbDeviceFS, &Class_Desc, 0) != USBD_OK)
        Error_Handler();

#if M1_USB_MODE == M1_CFG_USB_CDC_MSC
  /* Store the MSC Class ID */
  MSC_InstID = hUsbDeviceFS.classId;

  /* Add Class MSC, hUsbDeviceFS.classId++ */
  if(USBD_RegisterClassComposite(&hUsbDeviceFS, USBD_MSC_CLASS, CLASS_TYPE_MSC, MSC_EpAdd_Inst) != USBD_OK)
    Error_Handler();

  /* Store the CDC Class ID */
  CDC_InstID = hUsbDeviceFS.classId;

  /* Register CDC Class hUsbDeviceFS.classId++ */
  if(USBD_RegisterClassComposite(&hUsbDeviceFS, USBD_CDC_CLASS, CLASS_TYPE_CDC, CDC_EpAdd_Inst) != USBD_OK)
    Error_Handler();

  /* Add MSC Media interface */
  if (USBD_CMPSIT_SetClassID(&hUsbDeviceFS, CLASS_TYPE_MSC, 0) != 0xFF)
  {
    /* Add Storage callbacks for MSC Class */
    USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_MSC_Interface_fops);
  }

  /* Add CDC Interface Class */
  if (USBD_CMPSIT_SetClassID(&hUsbDeviceFS, CLASS_TYPE_CDC, 0) != 0xFF)
  {
    /* Add callbacks for CDC Class */
    USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_CDC_Interface_fops);
  }
#elif M1_USB_MODE == M1_CFG_USB_MSC
  /* Add Class MSC */
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_MSC) != USBD_OK)
        Error_Handler();

  if(USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_MSC_Interface_fops) != USBD_OK)
        Error_Handler();


#elif M1_USB_MODE == M1_CFG_USB_CDC
  /* Add Class CDC */
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK)
        Error_Handler();

  if(USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_CDC_Interface_fops) != USBD_OK)
        Error_Handler();

#endif


  if(USBD_Start(&hUsbDeviceFS) != USBD_OK)
        Error_Handler();

  hpcd_USB_DRD_FS.pData = &hUsbDeviceFS;
  /* USER CODE END USB_Init 2 */
} // void MX_USB_PCD_Init(void)


/*============================================================================*/
/**
* @brief PCD MSP Initialization
* This function configures the hardware resources used in this example
* @param hpcd: PCD handle pointer
* @retval None
*/
/*============================================================================*/
void HAL_PCD_MspInit(PCD_HandleTypeDef* hpcd)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  if(hpcd->Instance==USB_DRD_FS)
  {
    /* USER CODE BEGIN USB_DRD_FS_MspInit 0 */
    /* USER CODE END USB_DRD_FS_MspInit 0 */

    /** Initializes the peripherals clock
    */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USB GPIO Configuration
    PA11     ------> USB_DM
    PA12     ------> USB_DP
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF10_USB;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Enable VDDUSB */
    HAL_PWREx_EnableVddUSB();

    /* Peripheral clock enable */
    __HAL_RCC_USB_CLK_ENABLE();

    /* USB_DRD_FS interrupt Init */
    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY+1, 5); //configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1, 5);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);

    /* USER CODE BEGIN USB_DRD_FS_MspInit 1 */
    /* USER CODE END USB_DRD_FS_MspInit 1 */
  }
} // void HAL_PCD_MspInit(PCD_HandleTypeDef* hpcd)


/*============================================================================*/
/**
* @brief PCD MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hpcd: PCD handle pointer
* @retval None
*/
/*============================================================================*/
void HAL_PCD_MspDeInit(PCD_HandleTypeDef* hpcd)
{
  if(hpcd->Instance==USB_DRD_FS)
  {
    /* USER CODE BEGIN USB_DRD_FS_MspDeInit 0 */
    /* USER CODE END USB_DRD_FS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USB_CLK_DISABLE();

    /* USB_DRD_FS interrupt DeInit */
    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    /* USER CODE BEGIN USB_DRD_FS_MspDeInit 1 */

    /* Enable VDDUSB */
    HAL_PWREx_DisableVddUSB();

    /* USER CODE END USB_DRD_FS_MspDeInit 1 */
  }
} // void HAL_PCD_MspDeInit(PCD_HandleTypeDef* hpcd)


