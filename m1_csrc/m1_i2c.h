/* See COPYING.txt for license details. */

/*
*  m1_i2c.h
*
* I2C driver
*
* M1 Project
*
*/
#ifndef M1_I2C_H_
#define M1_I2C_H_

/**
The MCU PB6/PB7 are configured as I2C1_SCL and I2C1_SDA respectively.
The table below lists the peripheral addresses:

Peripheral	I2C Peripheral Address (A7 to A1)	8-bit address
Fuel Gauge	1110000								0xE0, 0xE1 // BQ27421
Fuel Gauge	1010101								0xAA, 0xAB // STC3115
USB PD		0100010								0x44, 0x45
Charger		1101011								0xD6, 0xD7
//LED Driver	1100000								0x60, 0x61
LED Driver	0101100								0x2C, 0x2D
**/

//hi2c1.Init.Timing = 0x00702787; -> 400KHz
//hi2c1.Init.Timing = 0x8000064A; -> 100KHz

#define I2C_WRITE_TIMEOUT		200 //ms
#define I2C_READ_TIMEOUT		200 //ms

typedef enum
{
	I2C_DEVICE_BQ27421 = 0,
	I2C_DEVICE_FUSB302,
	I2C_DEVICE_BQ25896,
	I2C_DEVICE_LP5814
} S_M1_I2C_DeviceId;

#define DUMMY_I2C_DEV			0xFF
#define I2C_DEVICE_STC3115		DUMMY_I2C_DEV
#define I2C_DEVICE_LP5562		DUMMY_I2C_DEV

typedef enum
{
	I2C_TRANS_READ_REGISTER = 0,
	I2C_TRANS_WRITE_REGISTER,
	I2C_TRANS_READ_DATA,
	I2C_TRANS_WRITE_DATA,
	I2C_TRANS_READ_REGISTER_MULTIPLE,		// Added for STC3115 to read multiple registers, shb
	I2C_TRANS_WRITE_REGISTER_MULTIPLE		// Added for STC3115 to write multiple registers, shb
} S_M1_I2C_Transaction_Type;

typedef struct
{
	S_M1_I2C_DeviceId dev_id;
	S_M1_I2C_Transaction_Type trans_type;
	uint16_t reg_address;
	uint8_t reg_data;
	uint16_t data_len;
	uint8_t *pdata;
	uint32_t timeout;
} S_M1_I2C_Trans_Inf;


void m1_i2c_hal_init(I2C_HandleTypeDef *phi2c);
HAL_StatusTypeDef m1_i2c_hal_trans_req(S_M1_I2C_Trans_Inf *trans_inf);
uint32_t m1_i2c_hal_get_error(void);

#endif /* M1_I2C_H_ */
