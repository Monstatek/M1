/* See COPYING.txt for license details. */

/*
*
* m1_sdcard.h
*
* Library for accessing SD card
*
* M1 Project
*
*/

#ifndef M1_SDCARD_H_
#define M1_SDCARD_H_

#include "main.h"
#include "ff.h"
#include "ff_gen_drv.h"

#define SDCARD_VOL_LABEL_LEN		24
#define SDCARD_PROD_NAME_LEN		6
#define SDCARD_OEM_ID_LEN			3

#define SDCARD_DEFAULT_DRIVE_PATH	"0:/"

typedef enum
{
	SD_access_OK = 0,
	SD_access_NotOK,
	SD_access_NotReady,
	SD_access_UnMounted,
	SD_access_NoFS,
	SD_access_EndOfStatus
} S_M1_SDCard_Access_Status;

typedef enum
{
	SD_RET_OK = 0,
	SD_RET_ERROR,
	SD_RET_ERROR_NO_CARD,
	SD_RET_ERROR_LOW_LEVEL // A hard error occurred in the low level disk I/O layer
} S_M1_SDCard_Init_Status;

typedef enum
{
    FATSYS_ANY = 0,
    FATSYS_12,
    FATSYS_16,
    FATSYS_32,
    FATSYS_EXT
} S_M1_SDCard_FAT_Sys;

typedef struct
{
    FATFS sdfs; // File system object for user logical drive
    char sdpath[4]; // Logical drive path for user
    S_M1_SDCard_Access_Status status; // SD access status
    uint32_t timestamp;
    uint8_t sd_detected;
} S_M1_SDCard_Hdl;

typedef struct
{
	S_M1_SDCard_FAT_Sys fs_type;
    uint32_t total_cap_kb; // total capacity in kbytes
    uint32_t free_cap_kb; // free capacity in kbytes
    uint16_t cluster_size;
    uint16_t sector_size;
    char vol_label[SDCARD_VOL_LABEL_LEN];
    char oem_id[SDCARD_OEM_ID_LEN];
    char prod_name[SDCARD_PROD_NAME_LEN]; // product name
    uint8_t manuf_id; // manufacturer id
    uint64_t capacity; // Capacity in bytes
    uint32_t block_size; // block size
    uint32_t loc_block_count; // logical capacity, in blocks
    uint32_t loc_block_size; // logical block size, in bytes
    uint8_t prod_rev_major; // product revision - major
    uint8_t prod_rev_minor; // product revision - minor
    uint32_t prod_sn; // product serial number
} S_M1_SDCard_Info;

DSTATUS m1_sdcard_drive_init(uint8_t param); /*!< Initialize Disk Drive*/
DSTATUS m1_sdcard_status(uint8_t param); /*!< Get Disk Status*/
DRESULT m1_sdcard_read(uint8_t param, uint8_t *buff, DWORD sector, UINT count);       /*!< Read Sector(s)*/
DRESULT m1_sdcard_write(uint8_t param, const uint8_t *buff, DWORD sector, UINT count); /*!< Write Sector(s)*/
DRESULT m1_sdcard_ioctl(uint8_t param, uint8_t cmd, void *buff);
S_M1_SDCard_Init_Status m1_sdcard_init(SD_HandleTypeDef *phsdcard);
S_M1_SDCard_Init_Status m1_sdcard_init_ex(void);
S_M1_SDCard_Init_Status m1_sdcard_init_retry(void);
/* Synchronous task-context results; do not infer success from cached status.
 * Mount returns the first failing mount/free-space result. */
FRESULT m1_sdcard_mount(void);
FRESULT m1_sdcard_unmount(void);
uint8_t m1_sdcard_format(void);
/* Request reinitialization; callers cannot publish an arbitrary ready state. */
void m1_sdcard_invalidate(void);
S_M1_SDCard_Access_Status m1_sdcard_get_status(void);
uint8_t m1_sd_detected(void);
char *m1_sd_error_msg(S_M1_SDCard_Access_Status ferr);
void sdcard_detection_task(void *param);
S_M1_SDCard_Info *m1_sdcard_get_info(void);
uint32_t m1_sdcard_get_total_capacity(void);
uint32_t m1_sdcard_get_free_capacity(void);
FRESULT m1_sdcard_get_error_code(void);

extern EXTI_HandleTypeDef sdcard_exti_hdl;
extern TaskHandle_t		sdcard_task_hdl;
extern uint8_t 			sdcard_status_changed;

#endif /* M1_SDCARD_H_ */
