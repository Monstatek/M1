/* See COPYING.txt for license details. */

/*
*
* m1_cli.c
*
* CLI commands for device testing
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include "main.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "m1_cli.h"
#include "m1_virtual_kb.h"
#include "m1_file_browser.h"
#include "m1_sdcard.h"
#include "m1_lp5814.h"
#include "m1_lcd.h"
#include "m1_buzzer.h"
#include "m1_infrared.h"
#include "irsnd.h"
#include "m1_sub_ghz_api.h"
#include "m1_power_ctl.h"
#include "m1_esp32_hal.h"
#include "m1_esp_hosted_config.h"
#include "m1_gpio.h"
//#include "control.h"
//#include "app_main.h"
#include "spi_master.h"
#include "ctrl_api.h"
#include "m1_lcd.h"
#include "m1_bq25896.h"
#include "m1_bq27421.h"
#include "m1_fusb302.h"
#include "m1_nfc.h"
#include "battery.h"
#include "mfc_harvest_storage.h"   /* debug: mock .m1h harvest to SD (m1_mtest 91) */
#include "nfc_poller.h"            /* debug: live nested-nonce harvest (m1_mtest 92) */
#include "task.h"                  /* uxTaskGetStackHighWaterMark() (m1_mtest 5)      */
#include "m1_heap_stack_report.h"  /* pure formatting for m1_mtest 5                  */
#include "m1_watchdog.h"           /* m1_wdt_get_task_handle() (m1_mtest 5)           */
#include "nfc_driver.h"            /* nfc_worker_task_hdl extern (m1_mtest 5)         */
#include "m1_menu.h"               /* menu_main_handler_task_hdl/subfunc_handler_task_hdl (m1_mtest 5) */
#include "m1_system.h"             /* system_task_hdl/idle_task_hdl (m1_mtest 5)      */
#include "m1_log_debug.h"          /* log_db_task_hdl (m1_mtest 5)                    */
#include "m1_usb_cdc_msc.h"        /* usb2ser_task_hdl (m1_mtest 5)                   */
#include "m1_isr_drop_counters.h"  /* main_q_hdl ISR-send drop counters (m1_mtest 5)  */
#include "m1_file_browser.h"       /* directory-scan / file-open perf counters (m1_mtest 5) */
#include "privateprofilestring.h"  /* INI-header open counter (m1_mtest 5)            */

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"CLI"

#define FILE_READWRITE_LEN_MAX		100

#define SDCARD_CLI_DRIVE_PATH		"0:/"

#define INPUT_PARAMS_MAX			5

//************************** C O N S T A N T **********************************/

const osThreadAttr_t cmdLineTask_attributes = {
  .name = "cmdLineTask", // defined in cli_app.c
  .priority = (osPriority_t)TASK_PRIORITY_CLI_HANDLER,
  .stack_size = M1_TASK_STACK_SIZE_4096
};

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

osThreadId_t cmdLineTaskHandle; // new command line task
FIL m1_cli_file;


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

BaseType_t cmd_m1_mtest(char *pconsole, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
void cmd_m1_mtest_basic_system(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_sdcard(char *pconsole, size_t output_size, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_led(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_lcd(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_infrared(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_power(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_subghz(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_esp32(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_gpio(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);
void cmd_m1_mtest_nfc(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This command runs a specific test based on the command type given in the parameters
 */
/*============================================================================*/
BaseType_t cmd_m1_mtest(char *pconsole, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
	/* Remove compile time warnings about unused parameters, and check the
	write buffer is not NULL.  NOTE - for simplicity, this example assumes the
	write buffer length is adequate, so does not check for buffer overflows. */
	//(void)pcCommandString; // contains the command string
    //(void)xWriteBufferLen; // contains the length of the write buffer
	int32_t cmd_type, temp32;
	uint8_t i, n_params;
	char *input_params[INPUT_PARAMS_MAX];
	BaseType_t input_params_len[INPUT_PARAMS_MAX];

	if ( num_of_params==0 ) // Help command?
	{
		cmd_m1_mtest_help();
		return pdFALSE;
	} // if ( num_of_params==0 )

    // CLI tests should not run when there's an active running sub-function
	if ( m1_device_stat.op_mode==M1_OPERATION_MODE_SUB_FUNC_RUNNING )
	{
		strcpy(pconsole, "Error: some task is being executed!\r\n");
		return pdFALSE;
	} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_SUB_FUNC_RUNNING )

	n_params = (num_of_params <= INPUT_PARAMS_MAX)?num_of_params:INPUT_PARAMS_MAX;

	for (i=0; i<n_params; i++)
	{
	    /* Obtain the name of the source file, and the length of its name, from
	    the command string. The name of the source file is the first parameter. */
		input_params[i] = FreeRTOS_CLIGetParameter
	                        (
	                          /* The command string itself. */
	                          pcCommandString,
	                          /* Return the i parameter, starting from 1. */
	                          i + 1,
	                          /* Store the parameter string length. */
	                          &input_params_len[i]
	                        );
	} // for (i=0; i<n_params; i++)

	for (i=0; i<n_params; i++)
		input_params[i][input_params_len[i]] = 0x00; // Add NULL to the end of string

    // convert the string to a number
    cmd_type = strtol(input_params[0], NULL, 10);
    temp32 = cmd_type/10;
    temp32 *= 10;

	switch (temp32)
	{
		case 0:
			/* mtest 5 is a read-only snapshot and intentionally takes no
			 * argument beyond its command number.  The other basic-system
			 * commands retain their existing parameter requirement. */
			if ( (n_params < 2) && (cmd_type != 5) )
				cmd_m1_mtest_help_basic_system();
			else
				cmd_m1_mtest_basic_system(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 10:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_sdcard();
    		else
			cmd_m1_mtest_sdcard(pconsole, xWriteBufferLen, input_params, n_params, cmd_type);
    		break;

    	case 20:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_led();
    		else
    			cmd_m1_mtest_led(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 30:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_lcd();
    		else
    			cmd_m1_mtest_lcd(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 40:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_infrared();
    		else
    			cmd_m1_mtest_infrared(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 50:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_power();
    		else
    			cmd_m1_mtest_power(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 60:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_subghz();
    		else
    			cmd_m1_mtest_subghz(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 70:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_esp32();
    		else
    			cmd_m1_mtest_esp32(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 80:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_gpio();
    		else
    			cmd_m1_mtest_gpio(pconsole, input_params, n_params, cmd_type);
    		break;

    	case 90:
    		if ( n_params < 2 )
    			cmd_m1_mtest_help_nfc();
    		else
    			cmd_m1_mtest_nfc(pconsole, input_params, n_params, cmd_type);
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
    } // switch (temp32)

    // If return is pdFALSE, then no further strings will be returned.
    // Otherwise, the command was not found.
    // Handled by FreeRTOS_CLIProcessCommand()
    return pdFALSE;

} // BaseType_t cmd_m1_mtest(char *pconsole, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)


/*============================================================================*/
/*
 * This command runs tests for basic system
 */
/*============================================================================*/
void cmd_m1_mtest_basic_system(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;
	uint32_t input2_val;

	switch (cmd_type)
	{
    	case 0:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: virtual keyboard\r\n");
    		M1_LOG_N(M1_LOGDB_TAG, "Test not enabled!\r\n");
    		break;

    	case 1:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Buzzer - play\r\n");
    		if ( n_params < 3 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		input2_val = strtol(input_params[2], NULL, 10);
    		m1_buzzer_set(input1_val, input2_val);

    		break;

    	case 2:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Set font\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		 // Impossible to change font dynamically!
    		//u8g2_SetFont(&m1_u8g2, U8G2_FONT_SECTION(input_params[1]));
    		break;

    	case 3:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Write text\r\n");
    		if ( n_params < 3 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[2], NULL, 10);
    		u8g2_DrawStr(&m1_u8g2, 0, input1_val, input_params[2]);
    		break;

    	case 4:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Clear display\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		u8g2_FirstPage(&m1_u8g2);
    		u8g2_NextPage(&m1_u8g2);
    		break;

    	case 5:   /* debug: heap + per-task stack-headroom telemetry ("mtest 5").
    	           * No allocation, no periodic logging, no ISR work -- a single
    	           * synchronous snapshot, logged via M1_LOG_N (the console
    	           * output buffer is only configCOMMAND_INT_MAX_OUTPUT_SIZE=200
    	           * bytes, too small for 13 task lines in one response).
    	           * Read-only: touches no task priority/size/heap-size/timing. */
    	{
    		/* m1_sdm_task_hdl/runonce_task_hdl/sys_init_task_hdl have no
    		 * extern declaration in any header (verified) -- declared here,
    		 * at the point of use, rather than editing three unrelated
    		 * subsystems' headers for one diagnostic. */
    		extern TaskHandle_t m1_sdm_task_hdl;
    		extern TaskHandle_t runonce_task_hdl;
    		extern TaskHandle_t sys_init_task_hdl;

    		char line[128];
    		m1_heap_stack_format_heap_line(line, sizeof(line),
    		    (size_t)xPortGetFreeHeapSize(), (size_t)xPortGetMinimumEverFreeHeapSize());
    		M1_LOG_N(M1_LOGDB_TAG, "%s\r\n", line);

    		/* one_shot: a task known to vTaskDelete(NULL) itself immediately
    		 * after a single boot-time run, with its handle never nulled
    		 * afterward -- querying uxTaskGetStackHighWaterMark() on it post-
    		 * deletion is undefined behavior (the TCB memory may be reused),
    		 * so it is never queried; "unavailable" is reported instead,
    		 * which is the honest state, not a missing feature. */
    		struct { const char *name; TaskHandle_t hdl; bool one_shot; } tasks[] = {
    			{ "SDManager_MainTask",      m1_sdm_task_hdl,                     false },
    			{ "nfc_worker",              nfc_worker_task_hdl,                 false },
    			{ "m1_wdt_handler_task_n",   m1_wdt_get_task_handle(),            false },
    			{ "system_periodic_task_n",  system_task_hdl,                     false },
    			{ "sdcard_detection_task_n", sdcard_task_hdl,                     false },
    			{ "menu_main_handler_task_n",menu_main_handler_task_hdl,          false },
    			{ "subfunc_handler_task_n",  subfunc_handler_task_hdl,            false },
    			{ "log_db_handler_task_n",   log_db_task_hdl,                     false },
    			{ "idle_handler_task_n",     idle_task_hdl,                       false },
    			{ "m1_ser2usb_task_n",       usb2ser_task_hdl,                    false },
    			{ "cmdLineTask",             (TaskHandle_t)cmdLineTaskHandle,     false },
    			{ "m1_runonce_task_n",       runonce_task_hdl,                    true  },
    			{ "m1_system_init_task_n",   sys_init_task_hdl,                   true  },
    		};
    		for (size_t ti = 0; ti < (sizeof(tasks) / sizeof(tasks[0])); ti++)
    		{
    			m1_task_stack_entry_t e;
    			e.name = tasks[ti].name;
    			if (tasks[ti].one_shot || tasks[ti].hdl == NULL)
    			{
    				e.available = false;
    				e.stack_words = 0;
    			}
    			else
    			{
    				e.available = true;
    				e.stack_words = (long)uxTaskGetStackHighWaterMark(tasks[ti].hdl);
    			}
    			m1_heap_stack_format_task_line(line, sizeof(line), &e, sizeof(StackType_t));
    			M1_LOG_N(M1_LOGDB_TAG, "%s\r\n", line);
    		}

    		/* main_q_hdl ISR-send drops (queue full): purely additive counters,
    		 * one per ISR call site (m1_isr_drop_counters.h/.c). A snapshot
    		 * read here is safe at any time -- see that header's rationale. */
    		M1_LOG_N(M1_LOGDB_TAG, "main_q_hdl ISR drops: ir_tx=%lu ir_rx_start=%lu ir_rx_edge=%lu subghz_tx=%lu subghz_rx=%lu\r\n",
    		    (unsigned long)m1_isr_drop_get_ir_tx(), (unsigned long)m1_isr_drop_get_ir_rx_start(),
    		    (unsigned long)m1_isr_drop_get_ir_rx_edge(), (unsigned long)m1_isr_drop_get_subghz_tx(),
    		    (unsigned long)m1_isr_drop_get_subghz_rx());

		/* File-browser perf counters: purely additive, cumulative since
		 * boot or the last "m1_mtest 6" reset (see below). Use to compare
		 * a browsing session's directory-scan and file-open counts
		 * before/after a change, on real hardware, without a wall-clock
		 * trace -- e.g. press UP/DOWN N times in a saved-files list, then
		 * read this line and confirm scan count stayed at 1 (cached),
		 * not N. */
		M1_LOG_N(M1_LOGDB_TAG, "file-browser perf: dir_scans=%lu file_opens=%lu\r\n",
		    (unsigned long)m1_fb_get_perf_scan_count(), (unsigned long)m1_fb_get_perf_file_open_count());

		/* INI-header profile opens (NFC/RFID saved-file parsing): counts
		 * real f_open() calls made by privateprofilestring.c -- one per
		 * profile_session_open() regardless of how many keys are then
		 * read through it, so a fully populated NFC record now shows 1
		 * here per load instead of the pre-session ~19-22. */
		M1_LOG_N(M1_LOGDB_TAG, "profile-parser perf: ini_opens=%lu\r\n",
		    (unsigned long)get_private_profile_open_count());

    		strcpy(pconsole, "Heap/stack telemetry logged to console.\r\n");
    		break;
    	}

	case 6:   /* debug: reset the file-browser perf counters ("mtest 6") so a
	           * hardware tester can zero them right before a measurement
	           * window (e.g. "N presses of UP/DOWN in a saved-files list"),
	           * then read them back via "mtest 5". Read-only otherwise --
	           * touches no browsing state, no task, no timing. */
		m1_fb_reset_perf_counters();
		reset_private_profile_open_count();
		strcpy(pconsole, "File-browser perf counters reset.\r\n");
		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_basic_system(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)



/*============================================================================*/
/*
 * This command runs tests for sdcard
 */
/*============================================================================*/
void cmd_m1_mtest_sdcard(char *pconsole, size_t output_size, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	char buffer[FILE_READWRITE_LEN_MAX + 1];
	uint8_t ret;
	uint16_t size;

    buffer[FILE_READWRITE_LEN_MAX] = 0x00; // Add NULL to the end of the string

	switch (cmd_type)
	{
    	case 10:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - create a new file\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		ret = m1_fb_dyn_strcat(buffer, 2, "", SDCARD_CLI_DRIVE_PATH, input_params[1]);
    		if ( !ret )
    			break;

    		ret = m1_fb_open_new_file(&m1_cli_file, input_params[1]);
    		if ( ret )
    			strcpy(pconsole, "Error!\r\n");
    		else
    			strcpy(pconsole, "File created successfully!\r\n");
    		break;

    	case 11:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - close a file\r\n");
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		ret = m1_fb_close_file(&m1_cli_file);
    		if ( ret )
    			strcpy(pconsole, "Error!\r\n");
    		else
    			strcpy(pconsole, "File closed successfully!\r\n");
    		break;

    	case 12:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - open a file\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		ret = m1_fb_open_file(&m1_cli_file, input_params[1]);
    		if ( ret )
    			strcpy(pconsole, "Error!\r\n");
    		else
    			strcpy(pconsole, "File opened successfully!\r\n");
    		break;

    	case 13:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - write to a file\r\n");
    		if ( n_params < 3 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		// convert the string to a number
    		size = strtol(input_params[2], NULL, 10);
    		if ( size > FILE_READWRITE_LEN_MAX )
    			size = FILE_READWRITE_LEN_MAX;
    		ret = m1_fb_write_to_file(&m1_cli_file, input_params[1], size);
    		if ( !ret )
    			strcpy(pconsole, "Error!\r\n");
    		else
    			strcpy(pconsole, "Write to file successfully!\r\n");
    		break;

	case 14:
		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - read from a file\r\n");
		if (pconsole == NULL || output_size == 0U)
			break;
		if ( n_params < 2 )
		{
			snprintf(pconsole, output_size, "%s", "Error: missing parameter(s)!\r\n");
			break;
		}
		if ( m1_sdcard_init_retry() != SD_RET_OK )
		{
			break;
		}
		// convert the string to a number
		size = strtol(input_params[1], NULL, 10);
		if ( size > FILE_READWRITE_LEN_MAX )
			size = FILE_READWRITE_LEN_MAX;
		ret = m1_fb_read_from_file(&m1_cli_file, buffer, size);
		if ( !ret )
			snprintf(pconsole, output_size, "%s", "Error!\r\n");
		else
		{
			buffer[ret] = '\0';
			snprintf(pconsole, output_size, "%s", buffer);
		}
		break;

    	case 17:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - delete a file or directory\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		ret = m1_fb_delete_file(input_params[1]);
    		if ( ret )
    			strcpy(pconsole, "Error!\r\n");
    		else
    			strcpy(pconsole, "File deleted successfully!\r\n");
    		break;

    	case 18:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - Format a drive\r\n");
    		if ( !m1_sd_detected() )
    		{
    			strcpy(pconsole, "Card not detected!\r\n");
    			break;
    		}
    		m1_sdcard_format();
    		if ( m1_sdcard_get_status()==SD_access_OK )
    			strcpy(pconsole, "Format successfully!\r\n");
    		break;

    	case 19:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: SD card - list all files and folders\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_sdcard_init_retry() != SD_RET_OK )
    		{
    			break;
    		}
    		FRESULT fres = m1_fb_listing(input_params[1]);
    		if ( fres!=FR_OK )
    			strcpy(pconsole, "Error!\r\n");
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_sdcard(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)


/*============================================================================*/
/*
 * This command runs tests for led
 */
/*============================================================================*/
void cmd_m1_mtest_led(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val, input2_val, input3_val;

	switch (cmd_type)
	{
    	case 20:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LED - fast blink mode with RGB\r\n");
    		if ( n_params < 4 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		input2_val = strtol(input_params[2], NULL, 10);
    		input3_val = strtol(input_params[3], NULL, 10);
    		//lp5814_fastblink_on_RGB(input1_val, input2_val);
    		lp5814_fastblink_on_R_G_B(input1_val, input2_val, input3_val);
    		break;

    	case 21:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LED - off mode with RGB\r\n");
    		lp5814_all_off_RGB();
    		break;

    	case 22:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LED - direct mode with R\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		lp5814_led_on_Red(input1_val);
    		break;

    	case 23:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LED - direct mode with G\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		lp5814_led_on_Green(input1_val);
    		break;

    	case 24:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LED - direct mode with B\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		lp5814_led_on_Blue(input1_val);
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_led(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)



/*============================================================================*/
/*
 * This command runs tests for lcd
 */
/*============================================================================*/
void cmd_m1_mtest_lcd(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;

	switch (cmd_type)
	{
    	case 30:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - set backlight\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		lp5814_backlight_on(input1_val); // Turn on backlight
    		break;

    	case 31:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - set power save\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		u8g2_SetPowerSave(&m1_u8g2, input1_val);
    		break;

    	case 32:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - clear display\r\n");
    		m1_lcd_cleardisplay();
    		break;

    	case 33:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - set contrast\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		u8g2_SetContrast(&m1_u8g2, input1_val);
    		break;

    	case 34:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - set regulation ratio\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		input1_val &= 0x07; // Regulation Ratio is a 3-bit value
    		input1_val |= 0x20; // 0x2X
    		u8x8_cad_StartTransfer(&m1_u8g2.u8x8);
    		u8x8_cad_SendCmd(&m1_u8g2.u8x8, input1_val);
    		u8x8_cad_EndTransfer(&m1_u8g2.u8x8);
    		break;

    	case 35:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: LCD - init retry when display fails\r\n");
    		u8g2_InitDisplay(&m1_u8g2);
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_lcd(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)


/*============================================================================*/
/*
 * This command runs tests for infrared
 */
/*============================================================================*/
void cmd_m1_mtest_infrared(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val, input2_val, input3_val, input4_val;
	uint8_t force_quit, ret;
	S_M1_Main_Q_t q_item;
	IRMP_DATA irmp_data;
	GPIO_InitTypeDef gpio_init_struct;

	switch (cmd_type)
	{
    	case 40:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Infrared - transmit\r\n");
    		if ( n_params < 5 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10); // protocol
    		input2_val = strtol(input_params[2], NULL, 10); // address
    		input3_val = strtol(input_params[3], NULL, 10); // command
    		input4_val = strtol(input_params[4], NULL, 10); // repeat

    		irmp_data.protocol = input1_val;
    		irmp_data.address  = input2_val;
    		irmp_data.command  = input3_val;
    		irmp_data.flags    = input4_val;

    		// must be in this order
    		infrared_encode_sys_init();
    		irsnd_generate_tx_data(irmp_data); // make ota data
    		infrared_transmit(1, irmp_data.protocol); // initialize the tx
    		force_quit = 0;

    		while (1 )
    		{
    			// Wait for the notification from button_event_handler_task to subfunc_handler_task.
    			// This task is the sub-task of subfunc_handler_task.
    			// The notification is given in the form of an item in the main queue.
    			// So let read the main queue.
    			ret = pdFALSE;
    			force_quit = 0;
    			q_item.q_evt_type = 0;
    			if ( infrared_transmit(0, 0)==IR_TX_ACTIVE ) // Transmit with no error?
    			{
    				// ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    				while ( ir_ota_data_tx_active )
    					;
    				ret = pdTRUE;
    				q_item.q_evt_type = Q_EVENT_IRRED_TX;
    			}
    			else
    				force_quit = 1;
    			if (ret==pdTRUE || force_quit)
    			{
    				if ( q_item.q_evt_type==Q_EVENT_IRRED_TX || force_quit ) // Transmit completed?
    				{
    					if ( infrared_transmit(0, 0)==IR_TX_ACTIVE ) // There's still repeated packet to transmit?
    						continue; // Let repeat

    					if ( pir_ota_data_tx_buffer!=NULL )
							free(pir_ota_data_tx_buffer);

						infrared_encode_sys_deinit();

						xQueueReset(main_q_hdl); // Reset main q before return
						break;
    				} // if ( q_item.q_evt_type==Q_EVENT_IRRED_TX || force_quit )
    			} // if (ret==pdTRUE || force_quit)
    		} // while (1 )
    		strcpy(pconsole, "Transmit completed!\r\n");
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_infrared(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)

void ShowRegister(uint8_t reg, uint8_t value)
{
	uint8_t t = 0x80;
	uint8_t msg[20];

	msg[0] = 0;
	M1_LOG_N(M1_LOGDB_TAG, "Register %2xh: %xh\r\n", reg, value);
	M1_LOG_N("     ", "7 6 5 4 3 2 1 0\r\n");
	for(int i = 0; i < 8; ++i) {
		if ((value & t) != 0)
			strcat(msg, "1 ");
		else
			strcat(msg, "0 ");
		t >>= 1;
	}
	M1_LOG_N("     ", "%s\r\n", msg);
}

/*============================================================================*/
/*
 * This command runs tests for power
 */
/*============================================================================*/
void cmd_m1_mtest_power(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint8_t 		reg = 0xff;
	int				res = -1;
	if (n_params >=3)
		reg = (uint8_t)atoi(input_params[2]);
	switch (cmd_type)
	{
    	case 50:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: power off\r\n");
    		m1_power_down();
    		break;

    	case 51:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: power reboot\r\n");
    		power_reboot();
    		break;

    	case 52: // Read Register
    		if (reg != 0xff) { // Read Register
    			M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Read Register");
				if (strcmp(input_params[1], "bc") == 0) {			// Battery Charger
					M1_LOG_N(M1_LOGDB_TAG, "(Battery Charger)\r\n");
					res = bq_readRegister(reg);
				} else if (strcmp(input_params[1], "pd") == 0) {	// USB-PD
					M1_LOG_N(M1_LOGDB_TAG, "(USB-PD)\r\n");
					res = pd_readRegister(reg);
				}
    		} else {	// Show Status
    			M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Show Status ");
    			if (strcmp(input_params[1], "bc") == 0) {			// Battery Charger
					M1_LOG_N(M1_LOGDB_TAG, "(Battery Charger)\r\n");
					bq_cli_status();
				} else if (strcmp(input_params[1], "pd") == 0) {	// USB-PD
					M1_LOG_N(M1_LOGDB_TAG, "(USB-PD)\r\n");
					pd_cli_status();
				}
    		}
    		break;

    	case 59:
    		S_M1_Power_Status_t SystemPowerStatus;
    		battery_power_status_get(&SystemPowerStatus);

    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: get battery information\r\n");
    		//get_power_status();
    		M1_LOG_N(M1_LOGDB_TAG, "Batt. level: %u%%\r\n", SystemPowerStatus.battery_level);
    		M1_LOG_N(M1_LOGDB_TAG, "Batt. temp: %uC\r\n", SystemPowerStatus.battery_temp);
    		M1_LOG_N(M1_LOGDB_TAG, "Batt. heath: %u%%\r\n", SystemPowerStatus.battery_health);
            if (SystemPowerStatus.stat==0)
            {
            	M1_LOG_N(M1_LOGDB_TAG, "Batt. consumption: %dmA\r\n", SystemPowerStatus.consumption_current);
            }
            else
            {
            	M1_LOG_N(M1_LOGDB_TAG, "Batt. charge current: %umA\r\n", SystemPowerStatus.charge_current);
    		}
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_power(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)


/*============================================================================*/
/*
 * This command runs tests for Sub-GHz
 */
/*============================================================================*/
void cmd_m1_mtest_subghz(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;
	int16_t rssi;
	S_M1_SubGHz_Band freq;
	uint8_t mod_type;
	struct si446x_reply_REQUEST_DEVICE_STATE_map *pdevstate;
	struct si446x_reply_GET_MODEM_STATUS_map *pmodemstat;

	switch (cmd_type)
	{
    	case 60:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - init\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);

    		switch(input1_val)
    		{
    			case 310:
    			case 315:
    			case 345:
    			case 390:
    			case 433:
    			//case 433.92:
    				freq = input1_val;
    				mod_type = MODEM_MOD_TYPE_OOK;
    				break;

    			case 915:
    				freq = input1_val;
    				mod_type = MODEM_MOD_TYPE_FSK;
    				break;

    			case 0:
    			    freq = SUB_GHZ_BAND_EOL;
    			    mod_type = MODEM_MOD_TYPE_FSK;
    			    break;

    			default:
    				freq = SUB_GHZ_BAND_EOL;
    				mod_type = MODEM_MOD_TYPE_FSK;
    				break;
    		} // switch(input1_val)

    		if ( freq==SUB_GHZ_BAND_EOL )
    		{
    			M1_LOG_N(M1_LOGDB_TAG, "Warning: frequency unknown, switching to default config!\r\n");
    		} // if ( freq==RADIO_FRONTEND_NW_UNKNOWN )

    		radio_init_rx_tx(freq, mod_type, true);

    		// Put the radio in sleep mode
    		SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SLEEP);
    		radio_set_antenna_mode(RADIO_ANTENNA_MODE_ISOLATED);
    		struct si446x_reply_PART_INFO_map *pinfo = SI446x_PartInfo();
    		sprintf(pconsole, "Init OK.\r\nPart %d Rev. %d Rom ID %d\r\n", pinfo->PART, pinfo->CHIPREV, pinfo->ROMID);
    		break;

    	case 61:
// Check for init status ready before executing...
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - transmit in CW mode\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
#if defined(M1_SUBGHZ_CW_TEST_TX)
    		/* Raw continuous-wave transmit for internal bench/characterization
    		 * only. This path keys the transmitter directly and does NOT pass the
    		 * regional TX-permission gate, so it is compiled out of shipped images
    		 * and enabled solely in internal/dev builds via M1_SUBGHZ_CW_TEST_TX. */
    		radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
    		SI446x_Start_Tx_CW(input1_val, MODEM_MOD_TYPE_CW);
#else
    		(void)input1_val;
    		strcpy(pconsole, "CW test TX not available in this build\r\n");
#endif
    		break;

    	case 62:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - set transmit power\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		input1_val &= 0xFF;
    		SI446x_Set_Tx_Power(input1_val);
    		break;

    	case 63:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - receive mode\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		// Put the radio in Rx mode
    		SI446x_Start_Rx(input1_val);
    		radio_set_antenna_mode(RADIO_ANTENNA_MODE_RX);
    		break;

    	case 64:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - select frontend network\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		SI446x_Select_Frontend(input1_val);
    		break;

    	case 68:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - get RSSI\r\n");
			pmodemstat = SI446x_Get_ModemStatus(0x00);
			// RF_Input_Level_dBm = (RSSI_value / 2) – MODEM_RSSI_COMP – 70
			// MODEM_RSSI_COMP = 0x40 = 64d is appropriate for most applications.
			rssi = pmodemstat->CURR_RSSI/2;
			rssi -= (0x40 + 70);
    		M1_LOG_N(M1_LOGDB_TAG, "RSSI: %ddBm\r\n", rssi);
    		break;

    	case 69:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: Sub-GHz - get device operating state\r\n");
    		pdevstate = SI446x_Request_DeviceState();
    		M1_LOG_N(M1_LOGDB_TAG, "State: %d Channel: %d\r\n", pdevstate->CURR_STATE, pdevstate->CURRENT_CHANNEL);
    		M1_LOG_N(M1_LOGDB_TAG, "1:Sleep 2:Active 3-4:Ready 5-6:Tune 7:Tx 8:Rx");
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_subghz(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)



/*============================================================================*/
/*
 * This command runs tests for ESP32
 */
/*============================================================================*/
void cmd_m1_mtest_esp32(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();

	switch (cmd_type)
	{
    	case 70:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: ESP32 - init esp32 module\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		//size_t free_heap = xPortGetFreeHeapSize();
    		if ( !m1_esp32_get_init_status() )
    		{
    			m1_esp32_init();
    		}
    		break;

    	case 71:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: ESP32 - get access point list\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_esp32_get_init_status() )
    		{
    			if ( !get_esp32_main_init_status() )
    				esp32_main_init();
    			if ( get_esp32_ready_status() )
    				test_get_available_wifi();
    			else
    				strcpy(pconsole, "ESP32 not ready!\r\n");
    		} // if ( m1_esp32_get_init_status() )
    		else
    		{
    			strcpy(pconsole, "ESP32 not init!\r\n");
    		}
    		break;

    	case 72:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: ESP32 - send AT command\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		if ( m1_esp32_get_init_status() )
    		{
    			if ( !get_esp32_main_init_status() )
    				esp32_main_init();
    			strcat(input_params[1], "\r\n");
				app_req.at_cmd = input_params[1];
				app_req.cmd_len = strlen(input_params[1]);
    			spi_AT_app_send_command(&app_req);
    		} // if ( get_esp32_ready_status() )
    		else
    		{
    			strcpy(pconsole, "ESP32 not ready!\r\n");
    		}
    		break;

    	case 78:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: ESP32 - reset\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		esp32_disable();
    		HAL_Delay(100);
    		esp32_enable();
    		break;

    	case 79:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: ESP32 - deinit for flash update\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		m1_esp32_deinit();
    		esp32_enable();
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_esp32(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)



/*============================================================================*/
/*
 * This command runs tests for GPIO
 */
/*============================================================================*/
void cmd_m1_mtest_gpio(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;
	int16_t rssi;
	uint8_t mode_type;

	switch (cmd_type)
	{
    	case 80:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: GPIO - turn external 3V3 on/off\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		ext_power_3V_set(input1_val);
    		break;

    	case 81:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: GPIO - turn external 5V on/off\r\n");
    		if ( n_params < 2 )
    		{
    			strcpy(pconsole, "Error: missing parameter(s)!\r\n");
    			break;
    		}
    		// convert the string to a number
    		input1_val = strtol(input_params[1], NULL, 10);
    		ext_power_5V_set(input1_val);
    		break;

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_gpio(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)


/*============================================================================*/
/*
 * This command runs tests for NFC
 */
/*============================================================================*/
void cmd_m1_mtest_nfc(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
{
	uint32_t input1_val;
	int16_t rssi;
	uint8_t mode_type;

	M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: NFC - CLI TEST input_params[%s] cmd_type[%d] n_params[%d]\r\n", *input_params, cmd_type, n_params);

	switch (cmd_type)
	{
		case 90:
    		break;

    	case 91:   /* debug: write a mock .m1h capture to SD (no radio) */
    		mfc_harvest_mock_run(pconsole, configCOMMAND_INT_MAX_OUTPUT_SIZE);
    		break;

    	case 92:   /* debug: arm a LIVE nested-nonce harvest ("mtest 92 <srcSec> <tgtSec> [n]") */
    	{
    		uint8_t src_sec = (n_params > 1) ? (uint8_t)atoi(input_params[1]) : 0U;
    		uint8_t tgt_sec = (n_params > 2) ? (uint8_t)atoi(input_params[2]) : 1U;
    		uint8_t nsamp   = (n_params > 3) ? (uint8_t)atoi(input_params[3]) : 4U;
    		/* Key A (0x60) on both sectors, known key FFFFFFFFFFFF; block = sector*4 */
    		nfc_harvest_arm((uint8_t)(src_sec * 4U), 0x60U,
    		                (uint8_t)(tgt_sec * 4U), 0x60U,
    		                nsamp, 0xFFFFFFFFFFFFULL);
    		snprintf(pconsole, configCOMMAND_INT_MAX_OUTPUT_SIZE,
    		         "Harvest armed: src sec %u -> tgt sec %u, %u samples, Key A FFFFFFFFFFFF.\r\n"
    		         "Present a MIFARE Classic card now; result logs to console.\r\n",
    		         src_sec, tgt_sec, nsamp);
    		break;
    	}

    	default:
    		M1_LOG_N(M1_LOGDB_TAG, "CLI mtest: command not defined yet!\r\n");
    		break;
	} // switch (cmd_type)
} // void cmd_m1_mtest_nfc(char *pconsole, char *input_params[], uint8_t n_params, uint8_t cmd_type)
