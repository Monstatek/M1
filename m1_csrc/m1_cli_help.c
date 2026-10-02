/* See COPYING.txt for license details. */

/*
*
* m1_cli_help.c
*
* Help for CLI commands
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include "main.h"
#include "m1_cli.h"
#include "FreeRTOS.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"CLI"


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

BaseType_t cmd_m1_mtest_help(void);
void cmd_m1_mtest_help_basic_system(void);
void cmd_m1_mtest_help_sdcard(void);
void cmd_m1_mtest_help_led(void);
void cmd_m1_mtest_help_lcd(void);
void cmd_m1_mtest_help_infrared(void);
void cmd_m1_mtest_help_power(void);
void cmd_m1_mtest_help_subghz(void);
void cmd_m1_mtest_help_esp32(void);
void cmd_m1_mtest_help_gpio(void);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This command displays help for the mtest command
 */
/*============================================================================*/
BaseType_t cmd_m1_mtest_help(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\nCommand syntax: cmd_type param1 param2... \r\n");

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 0-9: basic system test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 10-19: SD card test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 20-29: LED test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 30-39: LCD test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 40-49: Infrared test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 50-59: Power test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 60-69: Sub-GHz test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 70-79: ESP32 test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 80-89: GPIO test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 90-99: NFC test\r\n");
	
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");

	return pdFALSE;
} // BaseType_t cmd_m1_mtest_help(void)



/*============================================================================*/
/*
 * This command displays help for basic system test
 */
/*============================================================================*/
void cmd_m1_mtest_help_basic_system(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 0: virtual keyboard test\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 0 text_field default_filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 1: buzzer - play\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest frequency(Hz) duration(millisecond)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 2: Set font\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 2 font_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 3: Write text\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 3 text row_number\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 4: Clear display\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 4 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 5: heap and task stack telemetry\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 5\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");

} // void cmd_m1_mtest_help_basic_system(void)


/*============================================================================*/
/*
 * This command displays help for sdcard
 */
/*============================================================================*/
void cmd_m1_mtest_help_sdcard(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 10: SD card - create a new file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 10 filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 11: SD card - close a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 11 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 12: SD card - open a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 12 filename\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 13: SD card - write to a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 13 write_content write_size\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 14: SD card - read from a file\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 14 read_size\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 17: SD card - delete a file or folder\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 17 file_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 18: SD card - Format a drive\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 18 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 19: SD card - list all files and folders\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 19 folder_name\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_sdcard(void)



/*============================================================================*/
/*
 * This command displays help for LED
 */
/*============================================================================*/
void cmd_m1_mtest_help_led(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 20: LED - fast blink mode with RGB\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 20 r_g_b(1-2-4 or combined) rgb_pwm(0-255) on_off_time(0-65535)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 21: LED - off mode with RGB\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 21 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 22: LED - direct mode with R\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 22 r_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 23: LED - direct mode with G\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 23 g_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 24: LED - direct mode with B\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 24 b_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_led(void)



/*============================================================================*/
/*
 * This command displays help for LCD
 */
/*============================================================================*/
void cmd_m1_mtest_help_lcd(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 30: LCD - set backlight\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 30 bl_pwm(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 31: LCD - set power save\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 31 on(1)_off(0)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 32: LCD - clear display\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 32 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 33: LCD - set contrast\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 33 contrast_val(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 34: LCD - set regulation ratio\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 34 reg_ratio(0-7)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 35: LCD - init retry when display fails\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 35 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_lcd(void)



/*============================================================================*/
/*
 * This command displays help for infrared
 */
/*============================================================================*/
void cmd_m1_mtest_help_infrared(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 40: Infrared - transmit\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 40 protocol address command repeat\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_infrared(void)



/*============================================================================*/
/*
 * This command displays help for power
 */
/*============================================================================*/
void cmd_m1_mtest_help_power(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 50: power off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 50 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 51: reboot\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 51 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 52: read charger register\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 52 reg_no(0h ~ 14h)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 53: read fuel gauge register\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 53 reg_no(0 ~ 63)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 54: read usb-pd register\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 54 reg_no(1 ~ 10h, 3Ch ~ 43h)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 59: get battery information\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 59 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_power(void)



/*============================================================================*/
/*
 * This command displays help for Sub-GHz
 */
/*============================================================================*/
void cmd_m1_mtest_help_subghz(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 60: Sub-GHz - init\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 60 frequency(304, 310, 315, 345, 390, 433, 915)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 61: Sub-GHz - transmit in CW mode\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 61 channel(0-255, 256 to turn off) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 62: Sub-GHz - set transmit power\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 62 power(0-127) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 63: Sub-GHz - receive mode\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 63 channel(0-255) \r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 64: Sub-GHz - select frontend network\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 64 network(0:315, 1:433, 2:915, 3:None)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 68: Sub-GHz - get RSSI\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 68 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 69: Sub-GHz - get device operating state\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 69 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_subghz(void)



/*============================================================================*/
/*
 * This command displays help for ESP32
 */
/*============================================================================*/
void cmd_m1_mtest_help_esp32(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 70: ESP32 - init ESP32 module\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 70 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 71: ESP32 - get access point list\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 71 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 72: ESP32 - send AT command\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 72 AT_command\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 78: ESP32 - reset\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 78 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 79: ESP32 - deinit for flash update\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 79 ref(0-255)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_esp32(void)



/*============================================================================*/
/*
 * This command displays help for GPIO
 */
/*============================================================================*/
void cmd_m1_mtest_help_gpio(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 80: GPIO - turn external 3V3 on/off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 80 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 81: GPIO - turn external 5V on/off\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 81 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job

	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_gpio(void)



/*============================================================================*/
/*
 * This command displays help for NFC
 */
/*============================================================================*/
void cmd_m1_mtest_help_nfc(void)
{
	M1_LOG_N(M1_LOGDB_TAG, "\r\n- cmd_type 90: NFC - NULL\r\n");
	M1_LOG_N(M1_LOGDB_TAG, "Syntax: mtest 90 1(on)/0(off)\r\n");
	vTaskDelay(1); // Give the log task some time to do its job


	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
} // void cmd_m1_mtest_help_nfc(void)
