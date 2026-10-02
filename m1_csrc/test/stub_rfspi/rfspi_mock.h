/* Counting mocks backing the stub HAL/FreeRTOS for m1_rf_spi.c host tests.
 * Every HAL SPI/GPIO and FreeRTOS semaphore entry point m1_rf_spi.c calls is
 * implemented in rfspi_mock.c and increments one of these counters, so a test
 * can prove exactly which side effects a given m1_spi_hal_trans_req() call did
 * (or, for a rejected call, did NOT) perform. */
#ifndef RFSPI_MOCK_H_
#define RFSPI_MOCK_H_

#include "stm32h5xx_hal.h"

extern int g_sem_take_count;    /* xSemaphoreTake() calls                     */
extern int g_sem_give_count;    /* xSemaphoreGive() calls                     */
extern int g_gpio_write_count;  /* HAL_GPIO_WritePin() calls (NSS assert/deassert) */
extern int g_spi_tx_count;      /* HAL_SPI_Transmit() calls                   */
extern int g_spi_rx_count;      /* HAL_SPI_Receive() calls                    */
extern int g_spi_txrx_count;    /* HAL_SPI_TransmitReceive() calls            */

/* Return value the mocked HAL SPI ops hand back (lets a test inject a
 * transport failure). Defaults to HAL_OK. */
extern HAL_StatusTypeDef g_hal_spi_ret;

/* When non-zero, the next xSemaphoreCreateMutex() returns NULL (used to drive
 * m1_spi_hal_init() into leaving mutex_rf_spi_trans == NULL). */
extern int g_mock_mutex_create_null;

void rfspi_mock_reset(void);
int  rfspi_mock_total_side_effects(void); /* sem+gpio+spi calls, for the "zero" checks */

#endif /* RFSPI_MOCK_H_ */
