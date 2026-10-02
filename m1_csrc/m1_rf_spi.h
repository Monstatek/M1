/* See COPYING.txt for license details. */

/*
*  m1_rf_spi.h
*
* SPI driver
*
* M1 Project
*
*/
#ifndef M1_RF_SPI_H_
#define M1_RF_SPI_H_

/*
Peripheral					SPI			IO pins												Remarks
LCD	JHD12864-G386BTW		hspi1		MOSI(PA7), MISO(PA6), CLK(PA5), CS(PA3)
NFC ST25R3916-AQWT			hspi2		MOSI(PB15), MISO(PB14), CLK(PB13), NFC_CS(PD12)		[IRQ enabled]
Sub-GHz SI4463				hspi2		MOSI(PB15), MISO(PB14), CLK(PB13), Si4463_CS(PD10)	[IRQ enabled]
External connector			hspi4		xMOSI(PE6), xMISO(PE5), CLK(PE2), NSS(PE4)			[IRQ enabled]
*/

#define SPI_WRITE_TIMEOUT		200 //ms
#define SPI_READ_TIMEOUT		200 //ms

typedef enum
{
	SPI_DEVICE_NFC = 0,
	SPI_DEVICE_SUBGHZ,
	SPI_DEVICE_END_OF_LIST
} S_M1_SPI_DeviceId;

typedef enum
{
	SPI_TRANS_READ_DATA = 0,
	SPI_TRANS_WRITE_DATA,
	SPI_TRANS_WRITEREAD_DATA,
	SPI_TRANS_WRITE_DATA_NO_NSS, // Write data without asserting NSS pin
	SPI_TRANS_READ_DATA_NO_NSS, // Read data without asserting NSS pin
	SPI_TRANS_WRITEREAD_DATA_NO_NSS,  // Write and read data without asserting NSS pin
	SPI_TRANS_ASSERT_NSS, // Assert NSS pin manually
	SPI_TRANS_DEASSERT_NSS, // De-assert NSS pin manually
} S_M1_SPI_Transaction_Type;

typedef struct
{
	GPIO_TypeDef *spi_nss_port;
	uint16_t spi_nss_pin;
} S_M1_SPI_NSS;

typedef struct
{
	S_M1_SPI_DeviceId dev_id;
	S_M1_SPI_Transaction_Type trans_type;
	uint16_t data_len;
	uint8_t *pdata_tx;
	uint8_t *pdata_rx;
	uint32_t timeout;
} S_M1_SPI_Trans_Inf;


void m1_spi_hal_init(SPI_HandleTypeDef *phspi);
HAL_StatusTypeDef m1_spi_hal_trans_req(S_M1_SPI_Trans_Inf *trans_inf);
uint32_t m1_i2c_hal_get_error(void);
int32_t m1_spi_hal_wrapper(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length);

/* POSTAUTH-6: checked single-byte ST25R3916 direct command; returns the real
 * HAL status (HAL_OK/BUSY/ERROR/TIMEOUT). NFC_CS asserted low, byte sent, high. */
HAL_StatusTypeDef m1_nfc_spi_checked_cmd(uint8_t cmd);
#endif /* M1_RF_SPI_H_ */
