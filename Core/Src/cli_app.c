/*
FreeRTOS+CLI is released under the following MIT license.

Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//https://github.com/FreeRTOS/FreeRTOS/tree/main/FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "m1_log_debug.h"
#include "m1_cli.h"

#define MAX_INPUT_LENGTH 		64
#define USING_VS_CODE_TERMINAL 	0
#define USING_OTHER_TERMINAL 	1 // e.g. Putty, TerraTerm

char cOutputBuffer[configCOMMAND_INT_MAX_OUTPUT_SIZE], pcInputString[MAX_INPUT_LENGTH];
extern const CLI_Command_Definition_t xCommandList[];
int8_t cRxedChar;
const char * cli_prompt = "\r\ncli> ";
/* CLI escape sequences*/
uint8_t backspace[] = "\b \b";
uint8_t backspace_tt[] = " \b";

BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_clearScreen_help(void);
void vRegisterCLICommands(void);
void cliWrite(const char *str);
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex);
void handleBackspace(uint8_t *cInputIndex, char *pcInputString);
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString);

const CLI_Command_Definition_t xCommandList[] = {
    {
        .pcCommand = "cls", /* The command string to type. */
        .pcHelpString = "cls:\r\n Clears screen\r\n\r\n",
        .pxCommandInterpreter = cmd_clearScreen, /* The function to run. */
		.pxCommandHelper = cmd_clearScreen_help, /* Help for the function */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "mtest", /* The command string to type. */
        .pcHelpString = "mtest:\r\nThis is the multi-purpose test command\r\n\r\n",
        .pxCommandInterpreter = cmd_m1_mtest, /* The function to run. */
		.pxCommandHelper = cmd_m1_mtest_help, /* Help for the function. */
        .cExpectedNumberOfParameters = -1 /* variable parameters are expected. */
    },
    {
        .pcCommand = "dfu", /* The command string to type. */
        .pcHelpString = "dfu:\r\n Reboot to USB DFU mode\r\n\r\n",
        .pxCommandInterpreter = cmd_dfu, /* The function to run. */
		.pxCommandHelper = cmd_dfu_help, /* Help for the function. */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }
};


/*============================================================================*/
/*
 * Command Line Interface handler task
 *
 */
/*============================================================================*/
void vCommandConsoleTask(void *pvParameters)
{
    uint8_t cInputIndex = 0; // simply used to keep track of the index of the input string
    uint32_t receivedValue; // used to store the received value from the notification

    UNUSED(pvParameters);
    vRegisterCLICommands();

    for (;;)
    {
        xTaskNotifyWait(pdFALSE,    // Don't clear bits on entry
                                  0,  // Clear all bits on exit
                                  &receivedValue, // Receives the notification value
                                  portMAX_DELAY); // Wait indefinitely
        //echo recevied char
        cRxedChar = receivedValue & 0xFF;
        cliWrite((char *)&cRxedChar);
        if (cRxedChar == '\r' || cRxedChar == '\n')
        {
            // user pressed enter, process the command
            handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
        }
        else
        {
            // user pressed a character add it to the input string
            handleCharacterInput(&cInputIndex, pcInputString);
        }
    }
} // void vCommandConsoleTask(void *pvParameters)



/*============================================================================*/
/*
 * CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    /* Remove compile time warnings about unused parameters, and check the
	write buffer is not NULL.  NOTE - for simplicity, this example assumes the
	write buffer length is adequate, so does not check for buffer overflows. */
    (void)pcCommandString;
    (void)xWriteBufferLen;
    memset(pcWriteBuffer, 0x00, xWriteBufferLen);
    printf("\033[2J\033[1;1H");
    return pdFALSE;
} // BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)



/*============================================================================*/
/*
 * Hlep for the CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_clearScreen_help(void)



/*============================================================================*/
/*
 * Register the list of commands
 *
 */
/*============================================================================*/
void vRegisterCLICommands(void)
{
    //itterate through the list of commands and register them
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++)
    {
        FreeRTOS_CLIRegisterCommand(&xCommandList[i]);
    }
} // void vRegisterCLICommands(void)




/*============================================================================*/
/*
 * Write to CLI UART
 *
 */
/*============================================================================*/
void cliWrite(const char *str)
{
   printf("%s", str);
   fflush(stdout);
} // void cliWrite(const char *str)




/*============================================================================*/
/*
 * Handle the CR + LF from the input CLI command
 *
 */
/*============================================================================*/
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)
{
    cliWrite("\r\n");

    BaseType_t xMoreDataToFollow;
    do
    {
        xMoreDataToFollow = FreeRTOS_CLIProcessCommand(pcInputString, cOutputBuffer, configCOMMAND_INT_MAX_OUTPUT_SIZE);
        cliWrite(cOutputBuffer);
        *cOutputBuffer = 0x00; // Clear string after use
    } while (xMoreDataToFollow != pdFALSE);

    cliWrite(cli_prompt);
    *cInputIndex = 0;
    memset((void*)pcInputString, 0x00, MAX_INPUT_LENGTH);
} // void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)




/*============================================================================*/
/*
 * Handle the backspace from the input CLI command
 *
 */
/*============================================================================*/
void handleBackspace(uint8_t *cInputIndex, char *pcInputString)
{
    if (*cInputIndex > 0)
    {
        (*cInputIndex)--;
        pcInputString[*cInputIndex] = '\0';

#if USING_VS_CODE_TERMINAL
        cliWrite((char *)backspace);
#elif USING_OTHER_TERMINAL
        cliWrite((char *)backspace_tt);
#endif
    }
    else
    {
#if USING_OTHER_TERMINAL
        uint8_t right[] = "\x1b\x5b\x43";
        cliWrite((char *)right);
#endif
    }
} // void handleBackspace(uint8_t *cInputIndex, char *pcInputString)



/*============================================================================*/
/*
 * Handle a character from the input CLI command
 *
 */
/*============================================================================*/
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)
{
    if (cRxedChar == '\r')
    {
        return;
    }
    else if (cRxedChar == (uint8_t)0x08 || cRxedChar == (uint8_t)0x7F)
    {
        handleBackspace(cInputIndex, pcInputString);
    }
    else
    {
        if (*cInputIndex < MAX_INPUT_LENGTH)
        {
            pcInputString[*cInputIndex] = cRxedChar;
            (*cInputIndex)++;
        }
    }
} // void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)

#endif /* CLI_COMMANDS_H */
