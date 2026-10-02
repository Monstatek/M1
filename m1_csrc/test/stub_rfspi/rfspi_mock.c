/* Counting-mock implementations of the stub HAL/FreeRTOS surface, backing the
 * REAL m1_rf_spi.c in the host test. No hardware, no real SPI -- each call just
 * bumps a counter (and the SPI ops return g_hal_spi_ret) so the test can prove
 * exactly what a given m1_spi_hal_trans_req() did or did not touch. */
#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "semphr.h"
#include "rfspi_mock.h"

int g_sem_take_count   = 0;
int g_sem_give_count   = 0;
int g_gpio_write_count = 0;
int g_spi_tx_count     = 0;
int g_spi_rx_count     = 0;
int g_spi_txrx_count   = 0;

HAL_StatusTypeDef g_hal_spi_ret = HAL_OK;
int g_mock_mutex_create_null = 0;

static int s_fake_mutex_object;

void rfspi_mock_reset(void)
{
    g_sem_take_count = g_sem_give_count = 0;
    g_gpio_write_count = 0;
    g_spi_tx_count = g_spi_rx_count = g_spi_txrx_count = 0;
    g_hal_spi_ret = HAL_OK;
    g_mock_mutex_create_null = 0;
}

int rfspi_mock_total_side_effects(void)
{
    return g_sem_take_count + g_sem_give_count + g_gpio_write_count
         + g_spi_tx_count + g_spi_rx_count + g_spi_txrx_count;
}

/* ---- HAL GPIO / SPI ---- */

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)port; (void)pin; (void)state;
    g_gpio_write_count++;
}

void HAL_GPIO_DeInit(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port; (void)pin;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    (void)h; (void)pData; (void)Size; (void)Timeout;
    g_spi_tx_count++;
    return g_hal_spi_ret;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    (void)h; (void)pData; (void)Size; (void)Timeout;
    g_spi_rx_count++;
    return g_hal_spi_ret;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *pTx, uint8_t *pRx, uint16_t Size, uint32_t Timeout)
{
    (void)h; (void)pTx; (void)pRx; (void)Size; (void)Timeout;
    g_spi_txrx_count++;
    return g_hal_spi_ret;
}

void HAL_NVIC_DisableIRQ(IRQn_Type irq) { (void)irq; }

/* ---- FreeRTOS semaphore ---- */

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    if (g_mock_mutex_create_null)
        return (SemaphoreHandle_t)0;
    return (SemaphoreHandle_t)&s_fake_mutex_object;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, uint32_t timeout)
{
    (void)s; (void)timeout;
    g_sem_take_count++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    (void)s;
    g_sem_give_count++;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }
