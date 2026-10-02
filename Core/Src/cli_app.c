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
#include "usbd_cdc_if.h"           /* CDC_Transmit_FS for the M1CP send adapter */
#include "m1_manager_protocol.h"   /* M1CP init/process host */
#include "usbd_cdc_if.h"

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

volatile uint32_t dbg_cli_task_wake = 0;

/* M1CP transmit adapter: send a protocol frame over CDC. Bounded, non-blocking
 * retry while the CDC IN endpoint is busy (task context only). */
static int m1cp_cdc_send(const uint8_t *data, uint16_t len)
{
    uint8_t r;
    uint8_t tries;
    for (tries = 0U; tries < 8U; tries++)
    {
        r = CDC_Transmit_FS((uint8_t *)data, len);
        if (r != (uint8_t)USBD_BUSY)
        {
            return (r == (uint8_t)USBD_OK) ? (int)len : -1;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return -1;
}

volatile uint32_t dbg_cli_fifo_bytes = 0;
volatile uint32_t dbg_cli_fallback_chars = 0;
volatile uint32_t dbg_cli_newline = 0;

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
    uint8_t fifoChar;
    uint8_t handled = 0;
    char echoBuf[2];

    UNUSED(pvParameters);
    vRegisterCLICommands();

    /* Initialize the M1 Manager protocol (M1CP). Creates its static RX stream
     * buffer and resets session/parser state. Legacy console remains the default
     * until a validated HELLO handshake arrives. */
    m1cp_init(m1cp_cdc_send, CDC_LogCli_PushBytes);

    for (;;)
    {
    (void)xTaskNotifyWait(pdFALSE,          // Don't clear bits on entry
                      0,                    // Clear all bits on exit
                      &receivedValue,       // Receives the notification value
                      pdMS_TO_TICKS(20));   // Also poll FIFO periodically
        dbg_cli_task_wake++;

        /* M1CP: drain the manager RX stream buffer, parse frames, dispatch and
         * respond in this task context (never in the USB callback). Also enforces
         * the session and partial-frame timeouts. */
        m1cp_process();

        handled = 0;
        while (CDC_LogCli_GetChar(&fifoChar) != 0)
        {
            handled = 1;
            dbg_cli_fifo_bytes++;
            cRxedChar = (int8_t)fifoChar;
            echoBuf[0] = (char)cRxedChar;
            echoBuf[1] = '\0';
            cliWrite(echoBuf);

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

        // Fallback path for legacy UART/notify-value based sources.
        if ((handled == 0) && (receivedValue != 0U))
        {
            dbg_cli_fallback_chars++;
            cRxedChar = receivedValue & 0xFF;
            echoBuf[0] = (char)cRxedChar;
            echoBuf[1] = '\0';
            cliWrite(echoBuf);

            if (cRxedChar == '\r' || cRxedChar == '\n')
            {
                handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
            }
            else
            {
                handleCharacterInput(&cInputIndex, pcInputString);
            }
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
   size_t remaining;
   const uint8_t *chunk_ptr;
    static uint8_t direct_cli_tx_buf[64];

   if (str == NULL)
   {
       return;
   }

   if (m1_usbcdc_mode == CDC_MODE_LOG_CLI)
   {
       remaining = strlen(str);
       chunk_ptr = (const uint8_t *)str;

       while (remaining > 0U)
       {
           uint16_t chunk_len = (remaining > 64U) ? 64U : (uint16_t)remaining;
           uint8_t retry = 0U;

           memcpy(direct_cli_tx_buf, chunk_ptr, chunk_len);

           while (CDC_Transmit_FS(direct_cli_tx_buf, chunk_len) != USBD_OK)
           {
               if (++retry >= 20U)
               {
                   return;
               }
               vTaskDelay(pdMS_TO_TICKS(2));
           }

           cdc_tx_owner_mode = CDC_MODE_LOG_CLI;
           chunk_ptr += chunk_len;
           remaining -= chunk_len;
       }
       return;
   }

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
    dbg_cli_newline++;
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
