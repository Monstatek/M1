/* Host-test stub for STM32H5 HAL, used to link the REAL, unmodified
 * m1_csrc/m1_rf_spi.c against counting mocks (rfspi_mock.c) so the actual
 * production m1_spi_hal_trans_req() fail-closed preflight is exercised --
 * not a reimplementation of it. Declares exactly the HAL surface that
 * m1_rf_spi.c references and nothing more. Same link-the-real-file-with-
 * stub-headers convention already used for m1_ring_buffer.c and
 * privateprofilestring.c in this repo. */
#ifndef STUB_STM32H5XX_HAL_H_
#define STUB_STM32H5XX_HAL_H_

#include <stdint.h>

typedef enum {
    HAL_OK      = 0x00,
    HAL_ERROR   = 0x01,
    HAL_BUSY    = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

typedef struct { uint32_t dummy; } GPIO_TypeDef;

typedef struct { uint32_t ErrorCode; } SPI_HandleTypeDef;

typedef enum { SPI2_IRQn_VALUE = 0 } IRQn_Type;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_DeInit(GPIO_TypeDef *port, uint16_t pin);

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *pTx, uint8_t *pRx, uint16_t Size, uint32_t Timeout);

void HAL_NVIC_DisableIRQ(IRQn_Type irq);

#define __HAL_RCC_SPI2_CLK_DISABLE() do {} while (0)

#endif /* STUB_STM32H5XX_HAL_H_ */
