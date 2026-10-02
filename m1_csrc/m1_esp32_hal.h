/* See COPYING.txt for license details. */

/*
*
* m1_esp32_hal.h
*
* M1 HAL for EPS32 module
*
* M1 Project
*
*/

#ifndef M1_ESP32_HAL_H_
#define M1_ESP32_HAL_H_

#include "app_freertos.h"
#include "semphr.h"
#include "m1_ring_buffer.h"

#define ESP32_UART_BAUDRATE					115200
#define ESP32_UART_HIGH_BAUDRATE			460800

//#define ESP32_UART_DISABLE
#define ESP32_DATAREADY_DISABLE

extern UART_HandleTypeDef huart_esp;
extern SPI_HandleTypeDef hspi_esp;
extern DMA_HandleTypeDef hgpdma1_channel5_tx;
extern DMA_HandleTypeDef hgpdma1_channel4_rx;
extern EXTI_HandleTypeDef esp32_exti_handshake;
extern EXTI_HandleTypeDef esp32_exti_dataready;
extern SemaphoreHandle_t sem_esp32_trans;

extern S_M1_RingBuffer esp32_rb_hdl;

void m1_esp32_init(void);
void m1_esp32_reset_buffer(void);
void m1_esp32_deinit(void);
void esp32_enable(void);
void esp32_disable(void);
void m1_esp32_uart_tx(char *txdata);
void esp32_uartrx_handler(uint8_t rx_byte);
uint8_t m1_esp32_get_init_status(void);
void esp32_UART_init(void);
void esp32_UART_deinit(void);
void esp32_UART_change_baudrate(uint32_t baudrate);
void m1_esp32_uart_notify_ore(void);
uint32_t m1_esp32_get_and_clear_rx_drop_count(void);
uint32_t m1_esp32_get_and_clear_ore_count(void);

#endif /* M1_ESP32_HAL_H_ */
