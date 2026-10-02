/* See COPYING.txt for license details. */

/*
*
*  m1_lcd.c
*
*  M1 lcd display functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "semphr.h"
#include "main.h"
//#include "u8g2.h"
//#include "mui.h"
#include "m1_rf_spi.h"
//#include "u8x8.h"
//#include "U8g2lib.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/
/*
static const uint8_t m1_lcd_custom_init_seq[] = {

  U8X8_START_TRANSFER(),             	// enable chip, delay is part of the transfer start

  U8X8_C(0x0e2),            			// soft reset
  U8X8_C(0x0ae),		                // display off
  U8X8_C(0x040),		                // set display start line to 0

  U8X8_C(0x0a1),		                // ADC set to reverse
  U8X8_C(0x0c0),		                // common output mode
  // Flipmode
  //U8X8_C(0x0a0),		                // ADC set to reverse
  //U8X8_C(0x0c8),		                // common output mode

  U8X8_C(0x0a6),		                // display normal, bit val 0: LCD pixel off.
  U8X8_C(0x0a2),		                // LCD bias 1/9
  // power on sequence from paxinstruments
  U8X8_C(0x028|4),		                // all power  control circuits on
  U8X8_DLY(50),
  U8X8_C(0x028|6),		                // all power  control circuits on
  U8X8_DLY(50),
  U8X8_C(0x028|7),		                // all power  control circuits on
  U8X8_DLY(50),

  U8X8_C(0x022),		                // v0 voltage resistor ratio
  U8X8_CA(0x081, 200>>2),				// set contrast, contrast value

  U8X8_C(0x0ae),		                // display off
  U8X8_C(0x0a5),		                // enter powersafe: all pixel on, issue 142

  U8X8_END_TRANSFER(),             	// disable chip
  U8X8_END()             			// end of sequence
};
*/
//void u8x8_cad_SendSequence(u8x8_t *u8x8, uint8_t const *data)
//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

u8g2_t m1_u8g2;
SPI_HandleTypeDef *plcd_hspi;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_stm32_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
void m1_lcd_init(SPI_HandleTypeDef *phspi);

void m1_u8g2_firstpage(void);
uint8_t m1_u8g2_nextpage(void);
void m1_lcd_cleardisplay(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This functions is used to interface to the communication port of the display controller, i.e. SPI, I2C, etc.
 * This interface may either be implemented as a bit-banged software interface or using the MCU specific hardware
 * This function takes a "msg" which is one of many #defines found in u8x8.h"
 */
/*============================================================================*/
uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
	HAL_StatusTypeDef status;

	switch (msg)
	{
		// Send one or more bytes, located at arg_ptr, arg_int contains the number of bytes.
		case U8X8_MSG_BYTE_SEND:
			status = HAL_SPI_Transmit(plcd_hspi, (uint8_t *) arg_ptr, arg_int, SPI_WRITE_TIMEOUT);
			// Check status
			if (status != HAL_OK)
			{
			    return 0; // Error
			} // if (status != HAL_OK)
			break;

		// Send once during the init phase of the display
		case U8X8_MSG_BYTE_INIT:
			break;

		// Set the level of the data/command pin. arg_int contains the expected output level.
		// Use u8x8_gpio_SetDC(u8x8, arg_int) to send a message to the GPIO procedure.
		case U8X8_MSG_BYTE_SET_DC:
			HAL_GPIO_WritePin(Display_DI_GPIO_Port, Display_DI_Pin, arg_int);
			break;

		// Set the chip select line here. u8x8->display_info->chip_enable_level contains the expected level.
		// Use u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level) to call the GPIO procedure.
		case U8X8_MSG_BYTE_START_TRANSFER:
			HAL_GPIO_WritePin(Display_CS_GPIO_Port, Display_CS_Pin, GPIO_PIN_RESET);
			break;

		// Unselect the device. Use the CS level from here: u8x8->display_info->chip_disable_level.
		case U8X8_MSG_BYTE_END_TRANSFER:
			HAL_GPIO_WritePin(Display_CS_GPIO_Port, Display_CS_Pin, GPIO_PIN_SET);
			break;

		default:
			return 0;
	} // switch (msg)

	return 1;
} // uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)



/*============================================================================*/
/*
 * This function is used to read, set and reset GPIOs.
 * This function takes a "msg" which is one of many #defines found in u8x8.h.
 * The return value should be 1 (true) for successful handling of the message."
 */
/*============================================================================*/
uint8_t u8x8_stm32_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
	switch (msg)
	{
		case U8X8_MSG_GPIO_MENU_SELECT:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_OK_GPIO_Port, BUTTON_OK_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_OK_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1; // u8g2 translates 0 as button pressed
			break;

		case U8X8_MSG_GPIO_MENU_NEXT:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_RIGHT_GPIO_Port, BUTTON_RIGHT_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_RIGHT_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1;
			break;

		case U8X8_MSG_GPIO_MENU_PREV:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_LEFT_GPIO_Port, BUTTON_LEFT_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_LEFT_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1;
			break;

		case U8X8_MSG_GPIO_MENU_HOME:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_BACK_GPIO_Port, BUTTON_BACK_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_BACK_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1;
			break;

		case U8X8_MSG_GPIO_MENU_UP:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_UP_GPIO_Port, BUTTON_UP_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_UP_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1;
			break;

		case U8X8_MSG_GPIO_MENU_DOWN:
			//u8x8->gpio_result = HAL_GPIO_ReadPin(BUTTON_DOWN_GPIO_Port, BUTTON_DOWN_GPIO_Pin);
			u8x8->gpio_result = (buttons_ctl[BUTTON_DOWN_KP_ID].event==BUTTON_EVENT_CLICK) ? 0 : 1;
			break;

		case U8X8_MSG_GPIO_AND_DELAY_INIT:
			HAL_Delay(1);
			break;

		case U8X8_MSG_DELAY_MILLI:
			HAL_Delay(arg_int);
			break;

		case U8X8_MSG_GPIO_DC:
			HAL_GPIO_WritePin(Display_DI_GPIO_Port, Display_DI_Pin, arg_int);
			break;

		case U8X8_MSG_GPIO_RESET:
			HAL_GPIO_WritePin(Display_RST_GPIO_Port, Display_RST_Pin, arg_int);
			break;

		default:
			u8x8_SetGPIOResult(u8x8, 1);			// default return value
			break;
	} // switch (msg)

	return 1;
} // uint8_t u8x8_stm32_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)



/*============================================================================*/
/*
 * This function initializes the LCD display
 */
/*============================================================================*/
void m1_lcd_init(SPI_HandleTypeDef *phspi)
{
	assert(phspi!=NULL);
	plcd_hspi = phspi;
    HAL_Delay(2); // Wait for stable power after power on, > 1ms
    u8g2_Setup_st7567_enh_dg128064i_f(&m1_u8g2, U8G2_R2, u8x8_byte_stm32_4wire_hw_spi, u8x8_stm32_gpio_and_delay);
	u8g2_InitDisplay(&m1_u8g2);

	// Custom init for the LCD
	// This helps to minimize the flickering effect on the display
	// minimize the effect of visible grid patterns
	u8x8_cad_StartTransfer(&m1_u8g2.u8x8);
	// Set Regulation Ratio down to 2, default after init is 3
	u8x8_cad_SendCmd(&m1_u8g2.u8x8, 0x22);
	// Set contrast
    u8x8_cad_SendCmd(&m1_u8g2.u8x8, 0x081);
    u8x8_cad_SendArg(&m1_u8g2.u8x8, 235 >> 2);
	u8x8_cad_EndTransfer(&m1_u8g2.u8x8);

	//Set power save mode ON to clear any unwanted objects displayed on the LCD unexpectedly after POR
	u8g2_SetPowerSave(&m1_u8g2, true);
} // void m1_lcd_init(SPI_HandleTypeDef *phspi)



/*============================================================================*/
/*
 * This function is the equivalent of the function void u8g2_FirstPage(u8g2_t *u8g2)
 */
/*============================================================================*/
void m1_u8g2_firstpage(void)
{
    u8g2_ClearBuffer(&m1_u8g2);
    u8g2_SetBufferCurrTileRow(&m1_u8g2, 0);
} // void m1_u8g2_firstpage(void)


/*============================================================================*/
/*
 * This function is the equivalent of the function uint8_t u8g2_NextPage(u8g2_t *u8g2)
 */
/*============================================================================*/
uint8_t m1_u8g2_nextpage(void)
{
	u8g2_SendBuffer(&m1_u8g2);
	u8x8_RefreshDisplay( u8g2_GetU8x8(&m1_u8g2) );

	return 0;
} // uint8_t m1_u8g2_nextpage(void)



/*============================================================================*/
/*
 * This function clears the display with inverted color effect
 * It is used to replace the default function u8g2_ClearDisplay()
 */
/*============================================================================*/
void m1_lcd_cleardisplay(void)
{
	//u8g2_SetFontMode(&m1_u8g2, 0);
	//u8g2_ClearDisplay(&m1_u8g2);
	//u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set the color to White
	//u8g2_DrawBox(&m1_u8g2, 0, 0, M1_LCD_DISPLAY_WIDTH, M1_LCD_DISPLAY_HEIGHT);
	//u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set the color to Black

	u8g2_ClearBuffer(&m1_u8g2);
	u8g2_SetBufferCurrTileRow(&m1_u8g2, 0);

	m1_u8g2_nextpage();
	u8g2_SetBufferCurrTileRow(&m1_u8g2, 0);

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set the color to Black
	u8g2_SetFontMode(&m1_u8g2, U8G2_FONT_MODE_SOLID); // U8G2_FONT_MODE_SOLID, U8G2_FONT_MODE_TRANSPARENT
} // void m1_lcd_cleardisplay(void)
