/* See COPYING.txt for license details. */

/*
*  m1_spi.c
*
* SPI driver
*
* M1 Project
*
*/

/*
Peripheral					SPI			IO pins												Remarks
LCD	JHD12864-G386BTW		hspi1		MOSI(PA7), MISO(PA6), CLK(PA5), CS(PA3)				[IRQ enabled]
NFC ST25R3916-AQWT			hspi2		MOSI(PB15), MISO(PB14), CLK(PB13), NFC_CS(PD12)		[IRQ enabled]
Sub-GHz SI4463				hspi2		MOSI(PB15), MISO(PB14), CLK(PB13), Si4463_CS(PD10)	[IRQ enabled]
ESP32						hspi3		MOSI(PB2), MISO(PB4), CLK(PB3), NSS(PA15)			[IRQ enabled]
External connector			hspi4		xMOSI(PE6), xMISO(PE5), CLK(PE2), NSS(PE4)			[IRQ enabled]


ESP32: 		GPDMA1_Channel4
			GPDMA1_Channel5
Log/Debug: 	GPDMA1_Channel1
LCD: 		[GPDMA1_Channel0]
Sub-GHz Tx:	GPDMA1_Channel0
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "app_freertos.h"
#include "semphr.h"
#include "m1_rf_spi.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"RF_SPI"

#define RF_SPI_RX_QUEUE_SIZE		2

//************************** C O N S T A N T **********************************/

static const S_M1_SPI_NSS spi_nss_port[SPI_DEVICE_END_OF_LIST] = {{NFC_CS_GPIO_Port, NFC_CS_Pin}, {SI4463_CS_GPIO_Port, SI4463_CS_Pin}};

/* Single authoritative device-count bound, derived from the actual
 * spi_nss_port[] length (same sizeof-idiom this codebase uses elsewhere,
 * e.g. SUBGHZ_REC_FREQ_COUNT). This replaces the former
 * #define SPI_NUM_OF_DEVICES_MAX 2, a duplicated numeric bound that could
 * silently drift out of sync with the array if a device were ever added. */
#define SPI_NSS_PORT_COUNT		(sizeof(spi_nss_port) / sizeof(spi_nss_port[0]))

//************************** S T R U C T U R E S *******************************


/***************************** V A R I A B L E S ******************************/

static SPI_HandleTypeDef *pspihdl;
static SemaphoreHandle_t mutex_rf_spi_trans;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

HAL_StatusTypeDef m1_spi_hal_trans_req(S_M1_SPI_Trans_Inf *trans_inf);
uint32_t m1_spi_hal_get_error(void);

void m1_spi_hal_init(SPI_HandleTypeDef *phspi);
void m1_spi_hal_deinit(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief SPI Initialization Function
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_spi_hal_init(SPI_HandleTypeDef *phspi)
{
	assert(phspi!=NULL);
	pspihdl = phspi;

	// Known issue with freeRTOS
	// https://community.st.com/t5/stm32-mcus-embedded-software/hal-tick-problem/td-p/598944
	mutex_rf_spi_trans = xSemaphoreCreateMutex();
	assert(mutex_rf_spi_trans!=NULL);
} // void m1_spi_hal_init(SPI_HandleTypeDef *phspi)



/*============================================================================*/
/**
* @brief SPI De-Initialization
* This function frees the hardware resources
* @param hi2c: SPI handle pointer
* @retval None
*/
/*============================================================================*/
void m1_spi_hal_deinit(void)
{
	uint8_t i;

	__HAL_RCC_SPI2_CLK_DISABLE();

    /**SPI2 GPIO Configuration
    PB13     ------> SPI2_SCK
    PB14     ------> SPI2_MISO
    PB15     ------> SPI2_MOSI
    */
	for (i=0; i<SPI_DEVICE_END_OF_LIST; i++)
	{
		HAL_GPIO_DeInit(spi_nss_port[i].spi_nss_port, spi_nss_port[i].spi_nss_pin);
	}
    HAL_NVIC_DisableIRQ(SPI2_IRQn);

    if ( mutex_rf_spi_trans != NULL )
    	vSemaphoreDelete(mutex_rf_spi_trans);
} // void m1_spi_hal_deinit(void)



/*============================================================================*/
/**
  * @brief  Make a transaction with the SPI in blocking mode.
  * @param  trans_inf an SPI object
  * @retval HAL status
  */
/*============================================================================*/
HAL_StatusTypeDef m1_spi_hal_trans_req(S_M1_SPI_Trans_Inf *trans_inf)
{
	HAL_StatusTypeDef stat = HAL_OK;

	/* Fail-closed preflight. The previous assert(trans_inf!=NULL) and
	 * assert(dev_id < ...) provided NO runtime protection in shipped
	 * firmware: this project's release build defines NDEBUG (confirmed in
	 * compile_commands.json), which erases assert() entirely, so a NULL
	 * trans_inf was dereferenced and dev_id indexed spi_nss_port[]
	 * unchecked -- a release-active memory-safety boundary. Validate
	 * everything this function will touch BEFORE taking the mutex, driving
	 * any NSS pin, or starting any SPI transfer, and reject cleanly with
	 * HAL_ERROR: no lock is taken and no partial transaction is left
	 * behind on rejection. dev_id is bounded by SPI_NSS_PORT_COUNT (the
	 * actual array length, the single authoritative bound). The short-
	 * circuit order guarantees trans_inf is non-NULL before dev_id is read;
	 * the (size_t) cast makes a negative/garbage enum value fail closed.
	 * Applied uniformly to every trans_type -- the NO_NSS variants don't
	 * index spi_nss_port[], but both real callers already pass an in-range
	 * dev_id (SPI_DEVICE_NFC / SPI_DEVICE_SUBGHZ), so this rejects nothing
	 * that works today. */
	if ((trans_inf == NULL) ||
	    (pspihdl == NULL) ||
	    (mutex_rf_spi_trans == NULL) ||
	    ((size_t)trans_inf->dev_id >= SPI_NSS_PORT_COUNT))
	{
		return HAL_ERROR;
	}

	xSemaphoreTake(mutex_rf_spi_trans, portMAX_DELAY);

	switch (trans_inf->trans_type)
	{
		case SPI_TRANS_WRITE_DATA:
			//taskENTER_CRITICAL();
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_RESET);
			stat = HAL_SPI_Transmit(pspihdl, trans_inf->pdata_tx, trans_inf->data_len, trans_inf->timeout);
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_SET);
			//taskEXIT_CRITICAL();
			break;

		case SPI_TRANS_WRITEREAD_DATA:
		case SPI_TRANS_READ_DATA:
			//taskENTER_CRITICAL();
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_RESET);
			if ( trans_inf->trans_type==SPI_TRANS_READ_DATA )
				stat = HAL_SPI_Receive(pspihdl, trans_inf->pdata_rx, trans_inf->data_len, trans_inf->timeout);
			else
				stat = HAL_SPI_TransmitReceive(pspihdl, trans_inf->pdata_tx, trans_inf->pdata_rx, trans_inf->data_len, trans_inf->timeout);
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_SET);
			//taskEXIT_CRITICAL();
			break;

		case SPI_TRANS_WRITE_DATA_NO_NSS:
			//taskENTER_CRITICAL();
			stat = HAL_SPI_Transmit(pspihdl, trans_inf->pdata_tx, trans_inf->data_len, trans_inf->timeout);
			//taskEXIT_CRITICAL();
			break;

		case SPI_TRANS_READ_DATA_NO_NSS:
			//taskENTER_CRITICAL();
			stat = HAL_SPI_Receive(pspihdl, trans_inf->pdata_rx, trans_inf->data_len, trans_inf->timeout);
			//taskEXIT_CRITICAL();
			break;

		case SPI_TRANS_WRITEREAD_DATA_NO_NSS:
			//taskENTER_CRITICAL();
			stat = HAL_SPI_TransmitReceive(pspihdl, trans_inf->pdata_tx, trans_inf->pdata_rx, trans_inf->data_len, trans_inf->timeout);
			//taskEXIT_CRITICAL();
			break;

		case SPI_TRANS_ASSERT_NSS:
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_RESET);
			break;

		case SPI_TRANS_DEASSERT_NSS:
			HAL_GPIO_WritePin(spi_nss_port[trans_inf->dev_id].spi_nss_port , spi_nss_port[trans_inf->dev_id].spi_nss_pin, GPIO_PIN_SET);
			break;

		default:
			break;
	} // switch (trans_inf.trans_type)

	xSemaphoreGive(mutex_rf_spi_trans);

	if (stat!=HAL_OK)
	{
		//uint32_t error = HAL_SPI_GetError(pspihdl);
		M1_LOG_E(M1_LOGDB_TAG, "Error with transaction!\r\n");
	} // if (stat==HAL_OK)

	return stat;
} // HAL_StatusTypeDef m1_spi_hal_trans_req(S_M1_SPI_Trans_Inf trans_inf)



/*============================================================================*/
/**
  * @brief  Return the SPI error code.
  * @param  hi2c Pointer to a SPI_HandleTypeDef structure that contains
  *              the configuration information for the specified SPI.
  * @retval SPI Error Code
  */
/*============================================================================*/
uint32_t m1_spi_hal_get_error(void)
{
	return pspihdl->ErrorCode;
} // uint32_t m1_spi_hal_get_error(void)

/*
HAL_DMA_Init(&hdma_spi1_rx);
HAL_DMA_Init(&hdma_spi1_tx);
HAL_SPI_Receive_DMA
HAL_SPI_Transmit_DMA
*/


/*============================================================================*/
/**
  * @brief      SPI Read and Write byte(s) to device
  * @param[in]  pTxData : Pointer to data buffer to write
  * @param[out] pRxData : Pointer to data buffer for read data
  * @param[in]  Length : number of bytes to write
  * @return     BSP status
  */
/*============================================================================*/
int32_t m1_spi_hal_wrapper(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length)
{
	HAL_StatusTypeDef status = HAL_OK;
	int32_t ret = 0;
	S_M1_SPI_Trans_Inf nfc_spi_trans_inf = {
		  	  	  	  	  	  	  	  	  	  .dev_id = SPI_DEVICE_NFC,
						  	  	  	  	  	  .timeout = 2000,
											  .pdata_rx = pRxData,
											  .pdata_tx = pTxData,
											  .data_len = Length
  	  	  	  	  	  	  	  	  	  	  };

	if( (pTxData != NULL) && (pRxData != NULL) )
	{
		nfc_spi_trans_inf.trans_type = SPI_TRANS_WRITEREAD_DATA_NO_NSS;
	}
	else if ( (pTxData != NULL) && (pRxData == NULL) )
	{
		nfc_spi_trans_inf.trans_type = SPI_TRANS_WRITE_DATA_NO_NSS;
	}
	else if ( (pTxData == NULL) && (pRxData != NULL) )
	{
		nfc_spi_trans_inf.trans_type = SPI_TRANS_READ_DATA_NO_NSS;
	}
	else
	{
		status = HAL_ERROR;
	}

	if ( status==HAL_OK )
		status = m1_spi_hal_trans_req(&nfc_spi_trans_inf);

	/* Check the communication status */
	if (status != HAL_OK)
	{
		ret = -2;
	}

  	return ret;
} // int32_t m1_spi_hal_wrapper(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length)


/*============================================================================*/
/**
  * @brief  POSTAUTH-6: checked single-byte ST25R3916 direct command over SPI2,
  *         returning the ACTUAL HAL status (unlike m1_spi_hal_wrapper which maps
  *         HAL failures to -2/0). Drives NFC_CS low, transmits `cmd`, drives
  *         NFC_CS high on every exit path. Used for the D1 (0xD1) handoff so the
  *         emulator can prove the SPI peripheral actually accepted the byte.
  * @retval Real HAL_StatusTypeDef from HAL_SPI_Transmit (HAL_OK/BUSY/ERROR/TIMEOUT).
  */
/*============================================================================*/
HAL_StatusTypeDef m1_nfc_spi_checked_cmd(uint8_t cmd)
{
	if (pspihdl == NULL) { return HAL_ERROR; }
	HAL_GPIO_WritePin(spi_nss_port[SPI_DEVICE_NFC].spi_nss_port,
	                  spi_nss_port[SPI_DEVICE_NFC].spi_nss_pin, GPIO_PIN_RESET);
	HAL_StatusTypeDef st = HAL_SPI_Transmit(pspihdl, &cmd, 1U, 5U);
	HAL_GPIO_WritePin(spi_nss_port[SPI_DEVICE_NFC].spi_nss_port,
	                  spi_nss_port[SPI_DEVICE_NFC].spi_nss_pin, GPIO_PIN_SET);
	return st;
} // HAL_StatusTypeDef m1_nfc_spi_checked_cmd(uint8_t cmd)

