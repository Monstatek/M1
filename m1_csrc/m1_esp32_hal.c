/* See COPYING.txt for license details. */

/*
*
* esp32_hal.c
*
* M1 HAL for EPS32 module
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_settings.h"
#include "m1_esp32_hal.h"
//#include "spi_drv.h"
#include "m1_ring_buffer.h"

/*************************** D E F I N E S ************************************/

#define ESP32_UART_IRQn				UART4_IRQn
#define ESP32_UART_DMA_Tx_IRQn		GPDMA1_Channel5_IRQn
#define ESP32_UART_DMA_Rx_IRQn		GPDMA1_Channel4_IRQn

#define ESP32_DATAREADY_EXTI_IRQn   EXTI1_IRQn
#define ESP32_HANDSHAKE_EXTI_IRQn   EXTI7_IRQn

#define ESP32_DMA_RX_BUFFER_LEN 	128
#define ESP32_RX_BUFFER_LEN			4096

#define M1_LOGDB_TAG				"ESP32"

/*
	GPDMA_CxTR2.REQSEL[7:0]		Selected GPDMA request
			27							uart4_rx_dma
			28							uart4_tx_dma
*/
//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

// GPDMA linked list entry
typedef struct {
    __IO uint32_t CTR1;
    __IO uint32_t CTR2;
    __IO uint32_t CBR1;
    __IO uint32_t CSAR;
    __IO uint32_t CDAR;
    __IO uint32_t CLLR;
} S_M1_DMA_LL;

/***************************** V A R I A B L E S ******************************/

DMA_HandleTypeDef hgpdma1_channel5_tx;

SPI_HandleTypeDef hspi_esp;
UART_HandleTypeDef huart_esp;
EXTI_HandleTypeDef esp32_exti_handshake;
EXTI_HandleTypeDef esp32_exti_dataready;

static uint8_t esp32_init_done = FALSE;
static uint8_t esp32_uart_init_done = FALSE;
SemaphoreHandle_t sem_esp32_trans;
static volatile uint32_t esp32_uart_rx_drop_cnt = 0;
static volatile uint32_t esp32_uart_ore_cnt = 0;

S_M1_RingBuffer esp32_rb_hdl = {0};
static uint8_t *pesp32_rx = NULL;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_esp32_init(void);
void m1_esp32_reset_buffer(void);
void m1_esp32_uart_tx(char *txdata);
void m1_esp32_deinit(void);
uint8_t m1_esp32_get_init_status(void);
void esp32_uartrx_handler(uint8_t rx_byte);
void esp32_enable(void);
void esp32_disable(void);
static void esp32_UART_DMA_init(void);
void esp32_UART_init(void);
void esp32_UART_deinit(void);
void esp32_UART_change_baudrate(uint32_t baudrate);
static void esp32_SPI3_init(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/******************************************************************************/
/**
  * @brief Initializes UART/SPI communication between the MCU and the ESP32 module
  * @param None
  * @retval None
  */
/******************************************************************************/
void m1_esp32_init(void)
{
	GPIO_InitTypeDef gpio_init_structure;

	if ( !esp32_init_done )
	{
		esp32_init_done = TRUE;
#ifndef ESP32_UART_DISABLE
		esp32_UART_init();
#endif // #ifndef ESP32_UART_DISABLE
		esp32_SPI3_init();

#ifndef ESP32_DATAREADY_DISABLE
		/* Configure Interrupt mode for DATAREADY pin */
		gpio_init_structure.Pin = ESP32_DATAREADY_Pin;
		gpio_init_structure.Pull = GPIO_PULLDOWN;
		gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;
		gpio_init_structure.Mode = GPIO_MODE_IT_RISING;
		HAL_GPIO_Init(ESP32_DATAREADY_GPIO_Port, &gpio_init_structure);

		/* Enable DATAREADY EXTI Interrupt */
		HAL_NVIC_SetPriority((IRQn_Type)(ESP32_DATAREADY_EXTI_IRQn), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
		HAL_NVIC_EnableIRQ((IRQn_Type)(ESP32_DATAREADY_EXTI_IRQn));
#else
		/* Configure Interrupt mode for DATAREADY pin */
		gpio_init_structure.Pin = ESP32_DATAREADY_Pin;
		gpio_init_structure.Pull = GPIO_PULLDOWN;
		gpio_init_structure.Speed = GPIO_SPEED_FREQ_LOW;
		gpio_init_structure.Mode = GPIO_MODE_ANALOG;
		HAL_GPIO_Init(ESP32_DATAREADY_GPIO_Port, &gpio_init_structure);
#endif // #ifndef ESP32_DATAREADY_DISABLE

		/* Configure Interrupt mode for HANDSHAKE pin */
		gpio_init_structure.Pin = ESP32_HANDSHAKE_Pin;
		gpio_init_structure.Pull = GPIO_PULLDOWN;
		gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;
		gpio_init_structure.Mode = GPIO_MODE_IT_RISING;
		HAL_GPIO_Init(ESP32_HANDSHAKE_GPIO_Port, &gpio_init_structure);

		/* Enable HANDSHAKE EXTI Interrupt */
		HAL_NVIC_SetPriority((IRQn_Type)(ESP32_HANDSHAKE_EXTI_IRQn), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
		HAL_NVIC_EnableIRQ((IRQn_Type)(ESP32_HANDSHAKE_EXTI_IRQn));
#ifndef ESP32_DATAREADY_DISABLE
		esp32_exti_dataready.Line = EXTI_LINE_1;
		esp32_exti_dataready.RisingCallback = NULL;
		esp32_exti_dataready.FallingCallback = NULL;
#endif // #ifndef ESP32_DATAREADY_DISABLE
		esp32_exti_handshake.Line = EXTI_LINE_7;
		esp32_exti_handshake.RisingCallback = NULL;
		esp32_exti_handshake.FallingCallback = NULL;

		esp32_enable();
	} // if ( !esp32_init_done )

} // void m1_esp32_init(void)


/******************************************************************************/
/**
  * @brief Enables ESP32 module
  * @param None
  * @retval None
  */
/******************************************************************************/
void esp32_enable(void)
{
	HAL_GPIO_WritePin(GPIOA, ESP32_EN_Pin, GPIO_PIN_SET);
} // void esp32_enable(void)



/******************************************************************************/
/**
  * @brief Disables ESP32 module
  * @param None
  * @retval None
  */
/******************************************************************************/
void esp32_disable(void)
{
	HAL_GPIO_WritePin(GPIOA, ESP32_EN_Pin, GPIO_PIN_RESET);
	HAL_Delay(50);
} // static void esp32_disable(void)



/******************************************************************************/
/**
  * @brief Reset rx buffer
  * @param None
  * @retval None
  */
/******************************************************************************/
void m1_esp32_reset_buffer(void)
{
	m1_ringbuffer_reset(&esp32_rb_hdl);
} // void m1_esp32_reset_buffer(void)



/******************************************************************************/
/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
/******************************************************************************/
void esp32_UART_init(void)
{
    if ( esp32_uart_init_done )
        return;

    /* Publish valid storage before either UART or DMA interrupts can run. */
    HAL_NVIC_DisableIRQ(ESP32_UART_IRQn);
    HAL_NVIC_DisableIRQ(ESP32_UART_DMA_Tx_IRQn);
    if (!pesp32_rx)
        pesp32_rx = malloc(ESP32_RX_BUFFER_LEN);
    if (!sem_esp32_trans)
        sem_esp32_trans = xSemaphoreCreateBinary();
    if (!pesp32_rx || !sem_esp32_trans)
    {
        Error_Handler();
        return;
    }
    m1_ringbuffer_init(&esp32_rb_hdl, pesp32_rx, ESP32_RX_BUFFER_LEN, sizeof(uint8_t));
    (void)xSemaphoreGive(sem_esp32_trans);

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    // Initializes the peripherals clock
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_UART4;
    PeriphClkInit.Uart4ClockSelection = RCC_UART4CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

    /* Enable Peripheral clock */
    __HAL_RCC_UART4_CLK_ENABLE();

    //__HAL_RCC_GPIOA_CLK_ENABLE();
    /* UART4 GPIO Configuration
    PA0    ------> UART4_TX (alias ESP32_RX)
    PA1    ------> UART4_RX (alias ESP32_TX)
    */
    GPIO_InitStruct.Pin = ESP32_TX_Pin|ESP32_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init( ESP32_TX_GPIO_Port, &GPIO_InitStruct);

    huart_esp.Instance = UART4;
    huart_esp.Init.BaudRate = ESP32_UART_BAUDRATE;
    huart_esp.Init.WordLength = UART_WORDLENGTH_8B;
    huart_esp.Init.StopBits = UART_STOPBITS_1;
    huart_esp.Init.Parity = UART_PARITY_NONE;
    huart_esp.Init.Mode = UART_MODE_TX_RX;
    huart_esp.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_esp.Init.OverSampling = UART_OVERSAMPLING_16;
    huart_esp.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart_esp.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart_esp.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart_esp) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&huart_esp, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart_esp, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(&huart_esp) != HAL_OK)
    {
        Error_Handler();
    }

	esp32_UART_DMA_init();
	esp32_uart_init_done = TRUE;
	__HAL_UART_ENABLE_IT(&huart_esp, UART_IT_RXFNE);
	__HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ORE);
	__HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ERR);

	/* Enable the UART global Interrupt */
	HAL_NVIC_SetPriority(ESP32_UART_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(ESP32_UART_IRQn);

	//HAL_UART_Receive_IT(&huart_esp, esp_rx_buffer, 1);
} // void esp32_UART_init(void)



/******************************************************************************/
/**
* @brief UART-DMA Initialization
* This function configures UART-DMA
* @param huart: UART handle pointer
* @retval None
*/
/******************************************************************************/
static void esp32_UART_DMA_init(void)
{
	/*
	GPDMA_CxTR2.REQSEL[7:0]		Selected GPDMA request
			27							uart4_rx_dma
			28							uart4_tx_dma
	*/

	/* Peripheral clock enable */
	__HAL_RCC_GPDMA1_CLK_ENABLE();

    /* UART4 DMA Init */
    /* GPDMA1_REQUEST_UART4_TX Init */
    hgpdma1_channel5_tx.Instance = GPDMA1_Channel5;
    hgpdma1_channel5_tx.Init.Request = GPDMA1_REQUEST_UART4_TX;
    hgpdma1_channel5_tx.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    hgpdma1_channel5_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hgpdma1_channel5_tx.Init.SrcInc = DMA_SINC_INCREMENTED;
    hgpdma1_channel5_tx.Init.DestInc = DMA_DINC_FIXED;
    hgpdma1_channel5_tx.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    hgpdma1_channel5_tx.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    hgpdma1_channel5_tx.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    hgpdma1_channel5_tx.Init.SrcBurstLength = 1;
    hgpdma1_channel5_tx.Init.DestBurstLength = 1;
    hgpdma1_channel5_tx.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
    hgpdma1_channel5_tx.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hgpdma1_channel5_tx.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&hgpdma1_channel5_tx) != HAL_OK)
    {
    	Error_Handler();
    }

    __HAL_LINKDMA(&huart_esp, hdmatx, hgpdma1_channel5_tx);

    if (HAL_DMA_ConfigChannelAttributes(&hgpdma1_channel5_tx, DMA_CHANNEL_NPRIV) != HAL_OK)
    {
    	Error_Handler();
    }

	//Activate DMA interrupts: Transfer complete
    hgpdma1_channel5_tx.Instance->CCR = DMA_CCR_TCIE;


	/* GPDMA interrupts enable */
	HAL_NVIC_SetPriority(ESP32_UART_DMA_Tx_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(ESP32_UART_DMA_Tx_IRQn);

	// Enable the DMA transfer for the receiver/transmitter request by setting the DMAR/DMAT bit
	// in the UART CR3 register
	//huart_esp.Instance->CR3 |= USART_CR3_DMAT_Msk + USART_CR3_DMAR_Msk;
	ATOMIC_SET_BIT(huart_esp.Instance->CR3, USART_CR3_DMAT);

	/* GPDMA1 interrupt Init */
	HAL_NVIC_SetPriority(GPDMA1_Channel5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(GPDMA1_Channel5_IRQn);

} // static void esp32_UART_DMA_init(void)



/*============================================================================*/
/*
 * This function starts the uart tx using DMA
 *
 */
/*============================================================================*/
void m1_esp32_uart_tx(char *txdata)
{
	uint8_t len;

	len = strlen(txdata);
	if ( len==0 )
		return;
	/* Enable common interrupts: Transfer Complete and Transfer Errors ITs */
	//__HAL_DMA_ENABLE_IT(&hgpdma1_channel5_tx, (DMA_IT_TC | DMA_IT_DTE | DMA_IT_ULE | DMA_IT_USE | DMA_IT_TO));

	xSemaphoreTake(sem_esp32_trans, portMAX_DELAY);

    //DMA_SetConfig()
	/* Configure the DMA channel data size */
	MODIFY_REG(hgpdma1_channel5_tx.Instance->CBR1, DMA_CBR1_BNDT, (len & DMA_CBR1_BNDT));
	/* Clear all interrupt flags */
	__HAL_DMA_CLEAR_FLAG(&hgpdma1_channel5_tx, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE | DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_SUSP |
		                       DMA_FLAG_TO);
	/* Configure DMA channel source address */
	hgpdma1_channel5_tx.Instance->CSAR = (uint32_t)txdata;
	/* Configure DMA channel destination address */
	hgpdma1_channel5_tx.Instance->CDAR = (uint32_t)&huart_esp.Instance->TDR;

	/* Start DMA transfer */
	__HAL_DMA_ENABLE(&hgpdma1_channel5_tx);
} // void m1_esp32_uart_tx(char *txdata)



/*============================================================================*/
/*
 * This function processes Rx data from the UART
 *
 */
/*============================================================================*/
void esp32_uartrx_handler(uint8_t rx_byte)
{
	if (m1_ringbuffer_write(&esp32_rb_hdl, &rx_byte, 1) == 0)
	{
		esp32_uart_rx_drop_cnt++;
	}
} // void esp32_uartrx_handler(uint8_t rx_byte)



/******************************************************************************/
/**
* @brief Notify UART overrun event from ISR
* @param None
* @retval None
*/
/******************************************************************************/
void m1_esp32_uart_notify_ore(void)
{
	esp32_uart_ore_cnt++;
}



/******************************************************************************/
/**
* @brief Get and clear dropped RX byte count
* @param None
* @retval Count value
*/
/******************************************************************************/
uint32_t m1_esp32_get_and_clear_rx_drop_count(void)
{
	uint32_t cnt;

	taskENTER_CRITICAL();
	cnt = esp32_uart_rx_drop_cnt;
	esp32_uart_rx_drop_cnt = 0;
	taskEXIT_CRITICAL();

	return cnt;
}



/******************************************************************************/
/**
* @brief Get and clear UART ORE count
* @param None
* @retval Count value
*/
/******************************************************************************/
uint32_t m1_esp32_get_and_clear_ore_count(void)
{
	uint32_t cnt;

	taskENTER_CRITICAL();
	cnt = esp32_uart_ore_cnt;
	esp32_uart_ore_cnt = 0;
	taskEXIT_CRITICAL();

	return cnt;
}



/******************************************************************************/
/**
* @brief DMA and UART De-initialization
*
* @param
* @retval None
*/
/******************************************************************************/
void m1_esp32_deinit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	if ( esp32_init_done )
	{
		HAL_SPI_DeInit(&hspi_esp);
	    __HAL_RCC_SPI3_CLK_DISABLE();

		GPIO_InitStruct.Pin = ESP32_DATAREADY_Pin;
		GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		HAL_GPIO_Init(ESP32_DATAREADY_GPIO_Port, &GPIO_InitStruct);

		GPIO_InitStruct.Pin = ESP32_HANDSHAKE_Pin;
		GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pull = GPIO_PULLDOWN;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		HAL_GPIO_Init(ESP32_HANDSHAKE_GPIO_Port, &GPIO_InitStruct);

		HAL_NVIC_DisableIRQ((IRQn_Type)(ESP32_DATAREADY_EXTI_IRQn));
		HAL_NVIC_DisableIRQ((IRQn_Type)(ESP32_HANDSHAKE_EXTI_IRQn));
#ifndef ESP32_UART_DISABLE
		esp32_UART_deinit();
#endif // #ifndef ESP32_UART_DISABLE
//		esp32_disable();

		esp32_init_done = FALSE;
		esp32_uart_init_done = FALSE;
	} // if ( esp32_init_done )
} // void m1_esp32_deinit(void)



/******************************************************************************/
/**
* @brief Return the initialization status of the ESP32 module
*
* @param
* @retval None
*/
/******************************************************************************/
uint8_t m1_esp32_get_init_status(void)
{
	return esp32_init_done;
} // uint8_t m1_esp32_get_init_status(void)



/******************************************************************************/
/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
/******************************************************************************/
static void esp32_SPI3_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	/* Initializes the peripherals clock */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI3;
    PeriphClkInitStruct.Spi3ClockSelection = RCC_SPI3CLKSOURCE_PLL1Q;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
    	Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_SPI3_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**SPI3 GPIO Configuration
    PB2     			------> SPI3_MOSI
    PA15(JTDI)     		------> SPI3_NSS
    PB3(JTDO/TRACESWO)  ------> SPI3_SCK
    PB4(NJTRST)     	------> SPI3_MISO
    */
    GPIO_InitStruct.Pin = ESP32_SPI3_MOSI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_SPI3;
    HAL_GPIO_Init(ESP32_SPI3_MOSI_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ESP32_SPI3_NSS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ESP32_SPI3_NSS_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(ESP32_SPI3_NSS_GPIO_Port, ESP32_SPI3_NSS_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = ESP32_SPI3_SCK_Pin|ESP32_SPI3_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI3 interrupt Init */
    HAL_NVIC_SetPriority(SPI3_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(SPI3_IRQn);

	hspi_esp.Instance = SPI3;
	hspi_esp.Init.Mode = SPI_MODE_MASTER;
	hspi_esp.Init.Direction = SPI_DIRECTION_2LINES;
	hspi_esp.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi_esp.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi_esp.Init.CLKPhase = SPI_PHASE_2EDGE;
	hspi_esp.Init.NSS = SPI_NSS_SOFT;//SPI_NSS_HARD_OUTPUT;
	hspi_esp.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
	hspi_esp.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi_esp.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi_esp.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi_esp.Init.CRCPolynomial = 0x7;
	hspi_esp.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
	hspi_esp.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
	hspi_esp.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
	hspi_esp.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
	hspi_esp.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
	hspi_esp.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
	hspi_esp.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
	hspi_esp.Init.IOSwap = SPI_IO_SWAP_DISABLE;
	hspi_esp.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
	hspi_esp.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
	if (HAL_SPI_Init(&hspi_esp) != HAL_OK)
	{
		Error_Handler();
	}

} // static void esp32_SPI3_init(void)



/******************************************************************************/
/**
  * @brief ESP UART De-initialization Function
  * @param None
  * @retval None
  */
/******************************************************************************/
void esp32_UART_deinit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* Quiesce producers before invalidating their storage. Keep the UART
	 * clock running until HAL has disabled the peripheral. */
	HAL_NVIC_DisableIRQ(ESP32_UART_IRQn);
	HAL_NVIC_DisableIRQ(ESP32_UART_DMA_Tx_IRQn);
	if (hgpdma1_channel5_tx.Instance != NULL)
	{
		(void)HAL_DMA_Abort(&hgpdma1_channel5_tx);
		if (HAL_DMA_DeInit(&hgpdma1_channel5_tx) != HAL_OK)
		{
			Error_Handler();
			return;
		}
	}
	if (huart_esp.Instance == UART4)
		HAL_UART_DeInit(&huart_esp);
	HAL_NVIC_ClearPendingIRQ(ESP32_UART_IRQn);
	HAL_NVIC_ClearPendingIRQ(ESP32_UART_DMA_Tx_IRQn);
	__HAL_RCC_UART4_CLK_DISABLE();
	memset(&esp32_rb_hdl, 0, sizeof(esp32_rb_hdl));

	/* UART4 GPIO Configuration
	PA0    ------> UART4_TX (alias ESP32_RX)
	PA1    ------> UART4_RX (alias ESP32_TX)
	*/
	GPIO_InitStruct.Pin = ESP32_TX_Pin|ESP32_RX_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(ESP32_TX_GPIO_Port, &GPIO_InitStruct);

	if ( pesp32_rx )
	{
		free(pesp32_rx);
		pesp32_rx = NULL;
	} // if ( pesp32_rx )

	/* Mirror the "initialized" flag esp32_UART_init() guards on - without this,
	 * a second init in the SAME boot session (e.g. a Gate D ESP flash retry)
	 * sees the stale TRUE left by the FIRST attempt's init and skips reiniting
	 * UART4 entirely (clock stays disabled, RX buffer stays freed), so only a
	 * full MCU reboot (which resets this static back to FALSE) ever recovers -
	 * exactly the "needs a physical power cycle every time" symptom seen in Gate
	 * D on-device testing. */
	esp32_uart_init_done = FALSE;

} // void esp32_UART_deinit(void)



/******************************************************************************/
/**
  * @brief Change UART baud rate
  * @param None
  * @retval None
  */
/******************************************************************************/
void esp32_UART_change_baudrate(uint32_t baudrate)
{
	huart_esp.Init.BaudRate = baudrate;
	if (HAL_UART_Init(&huart_esp) != HAL_OK)
	{
		Error_Handler();
	}
} // void esp32_UART_change_baudrate(uint32_t baudrate)
