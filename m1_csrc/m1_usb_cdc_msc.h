/* See COPYING.txt for license details. */

/*
 * m1_usb_cdc.h
 *
 *      Author:
 */
#ifndef M1_USB_CDC_MSC_H_
#define M1_USB_CDC_MSC_H_

/*********************************************/
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_conf.h"

#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
// USB MSC + CDC
#include "usbd_composite_builder.h"
#include "usbd_msc_storage.h"
#elif M1_USB_CONFIG == M1_CFG_USB_MSC
// USB MSC
#include "usbd_msc_storage.h"
#elif M1_USB_CONFIG == M1_CFG_USB_CDC
#endif

#include "usbd_cdc_if.h"

#include "app_freertos.h"
#include "semphr.h"
#include "message_buffer.h"

/**************************/
/* USB CDC operation mode */
/**************************/
typedef enum
{
    CDC_MODE_ESP32 = 0,
    CDC_MODE_VCP,
    CDC_MODE_LOG_CLI
} enCdcMode;

/*********************************************/
#define USB_FS_CHUNK_SIZE       64

#define USB_RX_BUF_SIZE         1024  //128 //512 //1024  //(USB_FS_CHUNK_SIZE * 8)
#define USB_TX_BUF_SIZE         1024  //(USB_FS_CHUNK_SIZE * 8)

#define RXSTREAMBUF_UART_SIZE   1024
#define RXSTREAMBUF_USB_SIZE    2048 //8192 //4096 //2048 //USB_RX_BUF_SIZE*2

/*********************************************/
extern volatile uint8_t CDC_InstID;

extern enCdcMode m1_usbcdc_mode;
extern enCdcMode prev_usbcdc_mode;
extern USBD_CDC_LineCodingTypeDef linecoding;

extern TaskHandle_t usb2ser_task_hdl;
extern TaskHandle_t ser2usb_task_hdl;

extern USBD_DescriptorsTypeDef Class_Desc;
extern USBD_HandleTypeDef hUsbDeviceFS;
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

extern StreamBufferHandle_t h_uart_rx_streambuf;
extern StreamBufferHandle_t h_usb_rx_streambuf;
extern SemaphoreHandle_t ser2usb_task_semaphore;
extern SemaphoreHandle_t usb2ser_tx_semaphore;

extern volatile uint16_t head_usartx_dma;
extern volatile uint8_t usbcdc_rx_paused;
extern volatile int8_t m1_USB_CDC_ready;
extern volatile uint8_t tx_cptl_usartx;
extern volatile enCdcMode cdc_tx_owner_mode;
extern volatile uint8_t m1_usbcdc_drop_bridge_tx;

extern volatile uint16_t tail_usartx_dma;

uint16_t usart_get_rx_data_length(void);
void vUsb2SerTask(void *pvParameters);
void vSer2UsbTask(void *pvParameters);
uint16_t usart_rxget_data_length(void);
void usart_rxupdate_head_pointer(void);
void usart_rxdata_process_from_isr(void);
void usb_rxdata_process(void);

void USB_DRD_FS_IRQHandler(void);
void m1_usb_cdc_comdefault(void);
void m1_usb_cdc_comconfig(void);
void m1_usb_cdc_force_reconnect(void);

/*********************************************/
// USB MSC
extern volatile int8_t m1_USB_MSC_ready;

uint8_t m1_usb_msc_process(void);
uint8_t m1_usb_msc_sd_detected(void);

#endif /* M1_USB_CDC_MSC_H_ */

