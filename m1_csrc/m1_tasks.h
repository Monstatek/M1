/* See COPYING.txt for license details. */

/*
*
* m1_tasks.h
*
* Task functions for M1
*
* M1 Project
*
*/

//#include "main.h"
#include "stm32h5xx_hal.h"
#include "app_freertos.h"
#include "queue.h"
#include "timers.h"

#ifndef M1_TASKS_H_
#define M1_TASKS_H_

#define M1_TASK_STACK_SIZE_DEFAULT		configMINIMAL_STACK_SIZE // Default 256 32-bit words
#define M1_TASK_STACK_SIZE_0512			0512 // 32-bit words
#define M1_TASK_STACK_SIZE_1024			1024 // 32-bit words
#define M1_TASK_STACK_SIZE_2048			2048 // 32-bit words
#define M1_TASK_STACK_SIZE_3072			3072 // 32-bit words
#define M1_TASK_STACK_SIZE_4096			4096 // 32-bit words
#define M1_TASK_STACK_SIZE_7168			7168 // 32-bit words
#define M1_TASK_STACK_SIZE_8192			8192 // 32-bit words
#define M1_TASK_STACK_SIZE_12288		12288 // 32-bit words
#define M1_TASK_STACK_SIZE_16384		16384 // 32-bit words

#define M1_LOW_FREE_HEAP_WARNING_SIZE	4096 // bytes


#define tskNORMAL_PRIORITY						osPriorityNormal		// 24
#define tskABOVENORMAL_PRIORITY					osPriorityAboveNormal	// 32

#define TASK_PRIORITY_WDT_HANDLER				(tskIDLE_PRIORITY + 1)
#define TASK_PRIORITY_RUNONCE_TASK_HANDLER		(tskIDLE_PRIORITY + 1)
#define TASK_PRIORITY_IDLE_HANDLER				(tskIDLE_PRIORITY + 1)
#define TASK_PRIORITY_LOG_DB_HANDLER			(tskIDLE_PRIORITY + 3)
#define TASK_PRIORITY_CLI_HANDLER				(tskIDLE_PRIORITY + 5)
#define TASK_PRIORITY_MENU_MAIN_HANDLER			(tskIDLE_PRIORITY + 8)
#define TASK_PRIORITY_SDCARD_HANDLER			(tskIDLE_PRIORITY + 10)
#define TASK_PRIORITY_SDCARD_MANAGER            (tskIDLE_PRIORITY + 10)
#define TASK_PRIORITY_ESP32_TASKS				(tskIDLE_PRIORITY + 10)
#define TASK_PRIORITY_SYSTEM_TASK_HANDLER		(tskNORMAL_PRIORITY + 0) // Must be at this priority for the Sub-GHz samples recording to work properly!
#define TASK_PRIORITY_SUBFUNC_HANDLER			(tskNORMAL_PRIORITY + 0)
/* Temporary priority nfc_worker_task raises itself to (vTaskPrioritySet) for
 * the duration of a raw MFC-emulation session only (M1_MFC_RAW_EMULATION),
 * restored to NFC_WORKER_TASK_PRIORITY on session end. This priority boost
 * scoped to the latency-critical span rather than applied permanently. Kept
 * strictly below TASK_PRIORITY_SYS_INIT (the one documented absolute-max,
 * one-shot at boot only) and above every other long-running task priority
 * in this file. */
#define M1_MFC_RAWOWN_PRIORITY					(tskABOVENORMAL_PRIORITY + 1)
#define TASK_PRIORITY_SYS_INIT					(tskNORMAL_PRIORITY + 10) // Must be highest priority for all tasks

#define TASKDELAY_SDCARD_DET_TASK				700 // ms, for SD card detection debouncing
#define TASKDELAY_BATTERY_INFO_TIMER			2000 // ms

typedef enum
{
	Q_EVENT_KEYPAD = 1,
	Q_EVENT_IRRED_RX,
	Q_EVENT_IRRED_TX,
	Q_EVENT_SDCARD_CHANGE,
	Q_EVENT_SDCARD_INSERTED,
	Q_EVENT_SDCARD_CONNECTED,
	Q_EVENT_SDCARD_REMOVED,
	Q_EVENT_SDCARD_DISCONNECTED,
	Q_EVENT_SDCARD_MANAGER,
	Q_EVENT_ESP_TX_TC,
	Q_EVENT_ESP_RX_TC,
	Q_EVENT_SUBGHZ_RX,
	Q_EVENT_SUBGHZ_TX,
	Q_EVENT_NFC_START_READ,
	Q_EVENT_NFC_READ_COMPLETE,
	Q_EVENT_NFC_START_EMULATE,
	Q_EVENT_NFC_EMULATE_STOP,
	Q_EVENT_NFC_WRITE,
	Q_EVENT_NFC_START_DETECT_READER,
	Q_EVENT_NFC_DICT_SCAN,
	Q_EVENT_NFC_MFC_FIND_KEYS,  /* Find Missing Keys: dictionary-phase continuation
	                             * of a partial MFC read, same card, resuming
	                             * (never restarting) the same acquisition */
	Q_EVENT_NFC_HARVEST,        /* live MIFARE Classic nested-nonce harvest (3b) */
	Q_EVENT_NFC_SOLVE,          /* MFC Recovery nested-dictionary solve + verify */
	Q_EVENT_NFC_MFC_WRITE,      /* MIFARE Classic 1K write (clone/restore)       */
	Q_EVENT_NFC_T2T_UNLOCK,     /* NTAG/Ultralight Unlock (genuine PWD_AUTH)     */
#if defined(M1_MFC_RAW_EMULATION)
	Q_EVENT_NFC_MFC_EMULATE,    /* raw MIFARE Classic emulation (Scope B)         */
#endif
	Q_EVENT_NFC_COMPLETE,
	Q_EVENT_NFC_STOP,

	//Q_EVENT_NFC_RX,
	//Q_EVENT_NFC_TX,
	//Q_EVENT_NFC_COMPLETE,
	//Q_EVENT_RFID_RX,
	//Q_EVENT_RFID_TX,
	//Q_EVENT_RFID_COMPLETE,

	Q_EVENT_LFRFID_TAG_DETECTED,
	Q_EVENT_LFRFID_FRAME_READY,
	Q_EVENT_LFRFID_ERROR_TIMEOUT,
	Q_EVENT_LFRFID_PROTOCOL_CHANGED,
	Q_EVENT_LFRFID_PASS_SWITCH,  /* ASK<->PSK acquisition-mode switch timer fired */

	Q_EVENT_UI_LFRFID_START_READ,
	Q_EVENT_UI_LFRFID_STOP,
	Q_EVENT_UI_LFRFID_READ_TIMEOUT,
	Q_EVENT_UI_LFRFID_EMULATE,
	Q_EVENT_UI_LFRFID_EMULATE_STOP,
	Q_EVENT_UI_LFRFID_WRITE,
	Q_EVENT_UI_LFRFID_WRITE_DONE,
	Q_EVENT_UI_LFRFID_WRITE_STOP,

	Q_EVENT_BATTERY_UPDATED,
	Q_EVENT_MENU_EXIT,
	Q_EVENT_MENU_TIMEOUT,
	Q_EVENT_EOL
} S_M1_Q_Event_Type_t;

typedef struct
{
	uint32_t ir_edge_te;
	uint8_t ir_edge_dir;
} S_M1_IR_Rx_Data_t;

typedef union
{
	volatile S_M1_IR_Rx_Data_t ir_rx_data; // Updated in interrupt
	uint8_t ir_tx_data;
	uint8_t keypad_evt;
	uint8_t cmd_opt;
} S_M1_Q_Union_t;

typedef struct
{
	S_M1_Q_Event_Type_t q_evt_type;
	S_M1_Q_Union_t q_data;
} S_M1_Main_Q_t;

typedef struct
{
	S_M1_Q_Event_Type_t q_evt_type;
} S_M1_SdCard_Q_t;

typedef struct
{
	S_M1_Q_Event_Type_t q_evt_type;
	uint16_t cmd_opt;
	uint16_t write_size;
	void *descriptor;
} S_M1_SdCardManager_Q_t;

void m1_tasks_init(void);
void vApplicationIdleHook(void);
void m1_dummy_task(void *argument);

extern QueueHandle_t	main_q_hdl;
extern QueueHandle_t	sdcard_det_q_hdl;

#endif /* M1_TASKS_H_ */
