/* See COPYING.txt for license details. */

/*
*
* m1_log_debug.h
*
* Library to support logging and debugging functionality
*
* M1 Project
*
*/

#ifndef M1_LOG_DEBUG_H_
#define M1_LOG_DEBUG_H_

#include "stm32h5xx_hal.h"
#include "cmsis_os2.h"
#include "app_freertos.h"
#include "m1_log_debug_defs.h"

void m1_logdb_printf(S_M1_LogDebugLevel_t, const char* tag, const char* format, ...)
    _ATTRIBUTE((__format__(__printf__, 3, 4)));

#define M1_LOG_N(tag, format, ...)    	m1_logdb_printf(LOG_DEBUG_LEVEL_NONE, tag, format, ##__VA_ARGS__)
#define M1_LOG_E(tag, format, ...)		m1_logdb_printf(LOG_DEBUG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define M1_LOG_W(tag, format, ...)    	m1_logdb_printf(LOG_DEBUG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define M1_LOG_I(tag, format, ...)    	m1_logdb_printf(LOG_DEBUG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define M1_LOG_D(tag, format, ...)    	m1_logdb_printf(LOG_DEBUG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define M1_LOG_T(tag, format, ...)    	m1_logdb_printf(LOG_DEBUG_LEVEL_TRACE, tag, format, ##__VA_ARGS__)

/* task notify device ID */
#define TASK_NOTIFY_USART1  0x0
#define TASK_NOTIFY_USBCDC  0x1

#define UART_BAUD_115200        115200
#define UART_BAUD_230400        230400
#define UART_BAUD_460800        460800
#define UART_BAUD_921600        921600
#define LOG_DEBUG_UART_BAUD     UART_BAUD_460800

#define M1_LOGDB_MESSAGE_SIZE     80
#define M1_LOGDB_TX_BUFFER_SIZE   2048
#define M1_LOGDB_RX_BUFFER_SIZE   256
#define M1_LOGDB_DMA_TX_LEN       64

extern UART_HandleTypeDef huart_logdb;
extern DMA_HandleTypeDef hdma_txlogdb;
extern DMA_HandleTypeDef hdma_rxlogdb;
extern uint8_t logdb_rx_buffer[];
extern uint8_t logdb_tx_buffer[];

void m1_logdb_init();
extern void m1_logdb_update_tx_buffer(void);
extern void log_db_handler_task(void *param);
extern uint8_t m1_logdb_check_empty_state(void);
extern TaskHandle_t log_db_task_hdl;
extern QueueHandle_t log_q_hdl;

void m1_logdb_deinit(void);
#endif /* M1_LOG_DEBUG_H_ */
