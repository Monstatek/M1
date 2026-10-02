/* See COPYING.txt for license details. */

/*
*  m1_i2c.c
*
* I2C driver
*
* M1 Project
*
*/

/*
Peripheral							I2C Peripheral Address (A7-A1)	8-bit address	Remarks
Fuel Gauge (BQ27220YZFR)			1010101							0xAA, 0xAB		IRQ enabled
USB-C Power Delivery (FUSB302BUCX)	0100010							0x44, 0x45		IRQ enabled
Charger (BQ25896RTWR)				1101011							0xD6, 0xD7		IRQ enabled
LED Driver (LP5562TMX/NOPB)			1100000							0x60, 0x61
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "app_freertos.h"
#include "semphr.h"
#include "m1_i2c.h"

/*************************** D E F I N E S ************************************/

#define I2C_NUM_OF_DEVICES_MAX	4

#define I2C_DEVICE_ADD_BQ27421	0xAA	// Fuel Gauge (BQ27421)
#define I2C_DEVICE_ADD_FUSB302	0x44	// USB-C Power Delivery (FUSB302BUCX)
#define I2C_DEVICE_ADD_BQ25896	0xD6	// Charger (BQ25896RTWR)
#define I2C_DEVICE_ADD_LP5814	0x58	// LED Driver (LP5814)

#define M1_LOGDB_TAG	"I2C"

//************************** C O N S T A N T **********************************/

static const uint8_t m1_i2c_addr[I2C_NUM_OF_DEVICES_MAX] = {I2C_DEVICE_ADD_BQ27421, I2C_DEVICE_ADD_FUSB302, I2C_DEVICE_ADD_BQ25896, I2C_DEVICE_ADD_LP5814};

//************************** S T R U C T U R E S *******************************


/***************************** V A R I A B L E S ******************************/

static I2C_HandleTypeDef *pi2chdl;
static SemaphoreHandle_t mutex_i2c_trans;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

HAL_StatusTypeDef m1_i2c_hal_trans_req(S_M1_I2C_Trans_Inf *trans_inf);
uint32_t m1_i2c_hal_get_error(void);

void m1_i2c_hal_init(I2C_HandleTypeDef *phi2c);
void m1_i2c_hal_deinit(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief I2C Initialization Function
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_i2c_hal_init(I2C_HandleTypeDef *phi2c)
{
	assert(phi2c!=NULL);
	pi2chdl = phi2c;

	// Known issue with freeRTOS
	// https://community.st.com/t5/stm32-mcus-embedded-software/hal-tick-problem/td-p/598944
	mutex_i2c_trans = xSemaphoreCreateMutex();
	assert(mutex_i2c_trans!=NULL);
} // void m1_i2c_hal_init(I2C_HandleTypeDef *phi2c)



/*============================================================================*/
/**
* @brief I2C De-Initialization
* This function frees the hardware resources
* @param hi2c: I2C handle pointer
* @retval None
*/
/*============================================================================*/
void m1_i2c_hal_deinit(void)
{
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    HAL_GPIO_DeInit(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    HAL_GPIO_DeInit(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);

    if ( mutex_i2c_trans != NULL )
    	vSemaphoreDelete(mutex_i2c_trans);
} // void m1_i2c_hal_deinit(void)



/*============================================================================*/
/**
  * @brief  Make a transaction with the I2C in blocking mode.
  * @param  trans_inf an I2C object
  * @retval HAL status
  */
/*============================================================================*/
HAL_StatusTypeDef m1_i2c_hal_trans_req(S_M1_I2C_Trans_Inf *trans_inf)
{
	HAL_StatusTypeDef stat = HAL_OK;

	assert(trans_inf!=NULL);

	xSemaphoreTake(mutex_i2c_trans, portMAX_DELAY);

	assert(trans_inf->dev_id < I2C_NUM_OF_DEVICES_MAX);
	switch (trans_inf->trans_type)
	{
		case I2C_TRANS_READ_REGISTER:
			stat = HAL_I2C_Mem_Read(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->reg_address, I2C_MEMADD_SIZE_8BIT, &trans_inf->reg_data, 1, trans_inf->timeout);
			break;

		case I2C_TRANS_WRITE_REGISTER:
			taskENTER_CRITICAL();
			stat = HAL_I2C_Mem_Write(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->reg_address, I2C_MEMADD_SIZE_8BIT, &trans_inf->reg_data, 1, trans_inf->timeout);
			taskEXIT_CRITICAL();
			break;

		case I2C_TRANS_READ_DATA:
			stat = HAL_I2C_Master_Receive(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->pdata, trans_inf->data_len, trans_inf->timeout);
			break;

		case I2C_TRANS_WRITE_DATA:
			taskENTER_CRITICAL();
			stat = HAL_I2C_Master_Transmit(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->pdata, trans_inf->data_len, trans_inf->timeout);
			taskEXIT_CRITICAL();
			break;

		case I2C_TRANS_READ_REGISTER_MULTIPLE:	// Added for STC3115 to read multiple registers, shb
			stat = HAL_I2C_Mem_Read(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->reg_address, I2C_MEMADD_SIZE_8BIT, trans_inf->pdata, trans_inf->data_len, trans_inf->timeout);
			break;

		case I2C_TRANS_WRITE_REGISTER_MULTIPLE:	// Added for STC3115 to write multiple registers, shb
			taskENTER_CRITICAL();
			stat = HAL_I2C_Mem_Write(pi2chdl, m1_i2c_addr[trans_inf->dev_id], trans_inf->reg_address, I2C_MEMADD_SIZE_8BIT, trans_inf->pdata, trans_inf->data_len, trans_inf->timeout);
			taskEXIT_CRITICAL();
			break;

		default:
			break;
	} // switch (trans_inf.trans_type)

	xSemaphoreGive(mutex_i2c_trans);

	return stat;
} // HAL_StatusTypeDef m1_i2c_hal_trans_req(S_M1_I2C_Trans_Inf trans_inf)



/*============================================================================*/
/**
  * @brief  Return the I2C error code.
  * @param  hi2c Pointer to a I2C_HandleTypeDef structure that contains
  *              the configuration information for the specified I2C.
  * @retval I2C Error Code
  */
/*============================================================================*/
uint32_t m1_i2c_hal_get_error(void)
{
	return pi2chdl->ErrorCode;
} // uint32_t m1_i2c_hal_get_error(void)

/*
HAL_DMA_Init(&hdma_spi1_rx);
HAL_DMA_Init(&hdma_spi1_tx);
HAL_SPI_Receive_DMA
HAL_SPI_Transmit_DMA
*/
