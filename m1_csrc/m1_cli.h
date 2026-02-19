/* See COPYING.txt for license details. */

/*
*
* m1_cli.h
*
* CLI commands for device testing
*
* M1 Project
*
*/
#ifndef M1_CLI_H_
#define M1_CLI_H_

BaseType_t cmd_m1_mtest(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_dfu(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_dfu_help(void);

extern BaseType_t cmd_m1_mtest_help(void);
extern void cmd_m1_mtest_help_basic_system(void);
extern void cmd_m1_mtest_help_sdcard(void);
extern void cmd_m1_mtest_help_led(void);
extern void cmd_m1_mtest_help_lcd(void);
extern void cmd_m1_mtest_help_infrared(void);
extern void cmd_m1_mtest_help_power(void);
extern void cmd_m1_mtest_help_subghz(void);
extern void cmd_m1_mtest_help_esp32(void);
extern void cmd_m1_mtest_help_gpio(void);
extern void cmd_m1_mtest_help_nfc(void);

extern osThreadId_t cmdLineTaskHandle;
extern const osThreadAttr_t cmdLineTask_attributes;

#endif /* M1_CLI_H_ */
