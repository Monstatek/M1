/* See COPYING.txt for license details. */

/*
*
*  m1_lcd.h
*
*  M1 lcd display functions
*
* M1 Project
*
*/

#ifndef M1_LCD_H
#define M1_LCD_H

#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "queue.h"
#include "u8g2.h"
//#include "u8x8.h"

// Porting to STM32 MCU
/*
 * "These functions are both used as callbacks by the U8G2/U8X8 library.
 * They are setup when you call the first initialization function e.g. u8x8_Setup(&u8x8, u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_i2c, u8x8_byte_hw_i2c, psoc_gpio_and_delay_cb);
 * Writing a u8x8 byte communication callback is only required, if you want to use existing uC communication interfaces (I2C, SPI, etc)."
*/

// (x,y) = (0,0) at top left part of the LCD
#define M1_LCD_DISPLAY_WIDTH	128
#define M1_LCD_DISPLAY_HEIGHT	64

#define M1_LCD_MENU_TEXT_FRAME_W			126							// Width of the frame for the selected menu item text
#define M1_LCD_MENU_TEXT_FRAME_H			21 							// Height of the frame for the selected menu item text
#define M1_LCD_SUB_MENU_TEXT_FRAME_W		(M1_LCD_DISPLAY_WIDTH - 4) 	// Width of the frame for the selected sub menu item text
#define M1_LCD_SUB_MENU_TEXT_FRAME_H		12 							// Height of the frame for the selected sub menu item text
#define M1_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1 							// y coordinate of the menu text frame of the first menu item text

#define M1_LCD_MENU_TEXT_FONT_H				10 		// Height of the font configured for menu text

#define M1_LCD_MENU_TEXT_FIRST_COL_X		0 		// x coordinate of a menu item text
#define M1_LCD_MENU_TEXT_FIRST_ROW_Y		15 		// y coordinate of the first menu item text
#define M1_LCD_MENU_TEXT_SPACE_Y			21 		// Space between two menu item text rows

#ifndef U8G2_FONT_MODE_SOLID
#define U8G2_FONT_MODE_SOLID			0
#endif // #ifndef U8G2_FONT_MODE_SOLID

#ifndef U8G2_FONT_MODE_TRANSPARENT
#define U8G2_FONT_MODE_TRANSPARENT		1
#endif // #ifndef U8G2_FONT_MODE_TRANSPARENT

uint8_t u8x8_byte_stm32_4wire_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr); // Standard 8-bit SPI communication with "four pins" (SCK, MOSI, DC, CS)
uint8_t u8x8_stm32_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr); // The "uC specific" GPIO and Delay callback function

void m1_lcd_init(SPI_HandleTypeDef *phspi);
void m1_u8g2_firstpage(void);
uint8_t m1_u8g2_nextpage(void);
void m1_lcd_cleardisplay(void);

extern u8g2_t m1_u8g2;
extern QueueHandle_t	lcdspi_q_hdl;
extern SPI_HandleTypeDef *plcd_hspi;

#endif // #ifndef M1_LCD_H
