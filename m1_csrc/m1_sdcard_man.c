/* See COPYING.txt for license details. */

/*
*
* m1_sdcard_man.c
*
* Library for accessing SD card
*
* M1 Project
*
* Reference: https://github.com/STMicroelectronics/fp-sns-datalog1
*/

/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
//#include "main.h"
#include "m1_sdcard.h"
#include "m1_sdcard_man.h"
#include "semphr.h"
#include "m1_lp5814.h"
#include "m1_led_indicator.h"
#include "app_freertos.h"
#include "cmsis_os.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG		"SDM"
#define SD_CHECK_TIME     	30000 //ms

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/
FIL *m1_pdatfile_hdl;

uint8_t sd_logging_active = 0;
uint8_t sd_logging_error = 0;
static uint8_t sdwrite_buffer_id = 0;
volatile uint8_t battery_is_low = 0;
uint8_t sdcard_is_low = 0;
S_M1_SDM_Device_Info_t dev_sd_hdl;
//Specifies a software timer used to stop the current datalog phase.
static TimerHandle_t s_xstoptimer = NULL;
TaskHandle_t m1_sdm_task_hdl;
TimerHandle_t m1_sdm_timer_hdl;
QueueHandle_t sdmtaskqueue, sdiosem;

volatile uint8_t log_dev_status = M1_DATALOG_IDLE;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
static void m1_sdm_memory_timer_callback(TimerHandle_t xTimer);
static void m1_sdm_main_task(void *argument);
static void m1_sdm_dataready(S_M1_SdCardManager_Q_t evt);
static uint8_t m1_sdm_startstop_logging(void);
static uint8_t m1_sdm_start_logging(void);
static uint8_t m1_sdm_stop_logging(void);
void m1_sdm_task_init(void);
void m1_sdm_task_deinit(void);
void m1_sdm_task_start(void);
void m1_sdm_task_stop(void);
static uint8_t m1_sdm_memory_init(void);
static uint8_t m1_sdm_memory_deinit(void);
uint32_t m1_sdm_getlastfilenumber(char *dirname, char *prefix);
uint8_t m1_sdm_get_logging_error(void);
uint8_t m1_sdm_file_init(S_M1_SDM_DatFileInfo_t *pfileinfo);
uint8_t m1_sdm_write_buffer(uint8_t *buffer, uint32_t size);
uint8_t m1_sdm_fill_buffer(uint8_t *src, uint16_t srcSize);
uint8_t m1_sdm_flush_buffer(void);
uint8_t m1_sdm_close_all_files(void);
static uint8_t m1_sdm_close_datfile(void);
static uint8_t m1_sdm_sync_datfile(void);
static uint8_t m1_sdm_check_low_sdcard(void);
static void m1_sdm_startup_check(TimerHandle_t xTimer);


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
 * @brief:
 * @retval None
 */
/*============================================================================*/
static uint8_t m1_sdm_check_low_sdcard(void)
{
	DWORD free_sectors, total_sectors;

	if ( m1_sdcard_get_status()==SD_access_OK )
	{
		if ( m1_sdcard_get_info()==NULL )
			return 1;

		total_sectors = m1_sdcard_get_total_capacity();
		free_sectors = m1_sdcard_get_free_capacity();

	    if (free_sectors < (total_sectors/100)*10 ) // less than 10% left?
	    {
	    	return 1;
	    }
	    else
	    {
	    	return 0;
	    }
	} // if ( m1_sdcard_get_status()==SD_access_OK )

	return 1;
} // static uint8_t m1_sdm_check_low_sdcard(void)



/*============================================================================*/
/**
 * @brief: SD card main thread
 * @retval None
 */
/*============================================================================*/
static void m1_sdm_main_task(void *argument)
{
	BaseType_t ret;
	S_M1_SdCardManager_Q_t evt;

	(void)argument;

#if (configUSE_APPLICATION_TASK_TAG==1 && defined(TASK_M1_SDM_DEBUG_PIN))
	vTaskSetApplicationTaskTag(NULL, (TaskHookFunction_t)TASK_M1_SDM_DEBUG_PIN);
#endif /* (configUSE_APPLICATION_TASK_TAG==1 && defined(TASK_M1_SDM_DEBUG_PIN)) */
	for (;;)
	{
	    ret = xQueueReceive(sdmtaskqueue, &evt, portMAX_DELAY);  /* wait for message */

	    if (log_dev_status==M1_DATALOG_IDLE || log_dev_status==M1_DATALOG_SD_STARTED)
	    {
	        if (evt.cmd_opt==M1_SDM_CHECK_SDCARD_USAGE)      /* Check SD card memory available */
	        {
	        	M1_LOG_N(M1_LOGDB_TAG, "sdm_main_task CHECK_SDCARD_USAGE\r\n");
	        	sdcard_is_low = m1_sdm_check_low_sdcard();
	        	if ( sdcard_is_low==1 )
	        	{
// It needs a better solution to handle this case.
// If the queue is full, it will be blocked here!
	        		m1_sdm_task_stop();
	        	}
	        }
	        if (evt.cmd_opt==M1_SDM_START_STOP)              /* start/stop acquisition command */
	        {
	        	M1_LOG_N(M1_LOGDB_TAG, "sdm_main_task START_STOP\r\n");
	        	sd_logging_error = m1_sdm_startstop_logging();
	        }
	        else if (evt.cmd_opt & M1_SDM_DATA_READY_MASK)     /* transfer data to sd card command */
	        {
	        	M1_LOG_N(M1_LOGDB_TAG, "sdm_main_task DATA_READY\r\n");
	        	m1_sdm_dataready(evt);
	        }
	        else
	        {
	        	;
	        }
	    } // if (log_dev_status==M1_DATALOG_IDLE || log_dev_status==M1_DATALOG_SD_STARTED)
	} // for (;;)
} // static void m1_sdm_main_task(void *argument)



/*============================================================================*/
/**
  * @brief  Handle M1_SDM_START_STOP task message
  * @param  None
  * @retval None
  */
/*============================================================================*/
static uint8_t m1_sdm_startstop_logging(void)
{
	uint8_t ret;

	if (sd_logging_active==0)
	{
		ret = m1_sdm_start_logging();
	}
	else if (sd_logging_active==1)
	{
		ret = m1_sdm_stop_logging();
	}

	return ret;
} // static uint8_t m1_sdm_startstop_logging(void)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static uint8_t m1_sdm_start_logging(void)
{
	log_dev_status = M1_DATALOG_SD_STARTED;
	sd_logging_active = 1;
	xTimerStart(m1_sdm_timer_hdl, SD_CHECK_TIME);

	return 0;
} // static uint8_t m1_sdm_start_logging(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static uint8_t m1_sdm_stop_logging(void)
{
	uint8_t ret;

	xTimerStop(m1_sdm_timer_hdl, 0);
	ret = m1_sdm_close_all_files();
	sd_logging_active = 0;
	log_dev_status = M1_DATALOG_IDLE;

	return ret;
} // static uint8_t m1_sdm_stop_logging(void)


/*============================================================================*/
/**
  * @brief  Handle M1_SDM_DATA_READY_MASK task message
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void m1_sdm_dataready(S_M1_SdCardManager_Q_t evt)
{
	uint32_t buf_size;
	uint16_t write_size;
	uint8_t *dst;
	uint8_t sdwrite_buffer_id;

	sdwrite_buffer_id = evt.cmd_opt & ~(M1_SDM_DATA_READY_MASK);
	if ( sdwrite_buffer_id >= M1_SDM_BUFFER_ARRAY_SIZE )
		sdwrite_buffer_id = 0;
	dst = dev_sd_hdl.buff_info.sd_write_buffer;
	buf_size = dev_sd_hdl.sdWriteBufferSize;
	dst += buf_size*sdwrite_buffer_id;
	write_size = evt.write_size;

    m1_sdm_write_buffer(dst, write_size);
    M1_LOG_N(M1_LOGDB_TAG, "m1_sdm_dataready %d\r\n", sdwrite_buffer_id);
    vTaskDelay(1);
    //m1_test_gpio_pull_high();
	m1_sdm_sync_datfile();
	//m1_test_gpio_pull_low();
	M1_LOG_N(M1_LOGDB_TAG, "sdm_sync OK\r\n");
	vTaskDelay(1);
} // static void m1_sdm_dataready(S_M1_SdCardManager_Q_t evt)



/*============================================================================*/
/**
  * @brief  PWR PVD interrupt callback
  * @param  None
  * @retval None
  */
/*============================================================================*/
void HAL_PWR_PVDCallback(void)
{
	S_M1_SdCardManager_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

    /* If the battery is too low close the file and turn off the system */
	battery_is_low = 1;
	if (log_dev_status==M1_DATALOG_SD_STARTED)
	{
		log_dev_status = M1_DATALOG_IDLE;
		q_item.cmd_opt = M1_SDM_START_STOP;
		xQueueSendFromISR(sdmtaskqueue, &q_item, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
} // void HAL_PWR_PVDCallback(void)




/*============================================================================*/
/**
  * @brief  SD Card Manager memory initialization. Performs the dynamic allocation for
  *         the sd_write_buffer
  * @param
  * @retval 0: no error
  */
/*============================================================================*/
static uint8_t m1_sdm_memory_init(void)
{
	dev_sd_hdl.sdWriteBufferSize = M1_SDM_MIN_BUFFER_SIZE;

	dev_sd_hdl.buff_info.sd_write_buffer_idx = 0;
	dev_sd_hdl.buff_info.sd_write_buffer = m1_malloc(dev_sd_hdl.sdWriteBufferSize*M1_SDM_BUFFER_ARRAY_SIZE);

	if (dev_sd_hdl.buff_info.sd_write_buffer==NULL)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Mem alloc error:%ld %d@%s\r\n", dev_sd_hdl.sdWriteBufferSize*M1_SDM_BUFFER_ARRAY_SIZE, __LINE__, __FILE__);
		return 1;
	}
	else
	{
	    M1_LOG_I(M1_LOGDB_TAG, "Mem alloc ok:%ld %d@%s\r\n", dev_sd_hdl.sdWriteBufferSize*M1_SDM_BUFFER_ARRAY_SIZE, __LINE__, __FILE__);
	}

	return 0;
} // static uint8_t m1_sdm_memory_init(void)



/*============================================================================*/
/**
  * @brief  SD Card Manager memory De-initialization.
  * @param
  * @retval 0: no error
  */
/*============================================================================*/
static uint8_t m1_sdm_memory_deinit(void)
{
    if (dev_sd_hdl.buff_info.sd_write_buffer != NULL)
    {
    	m1_free(dev_sd_hdl.buff_info.sd_write_buffer);
    	dev_sd_hdl.buff_info.sd_write_buffer = NULL;
    }

    return 0;
} // static uint8_t m1_sdm_memory_deinit(void)




/*============================================================================*/
/**
  * @brief  Initialize SD Card Manager thread and queue
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_sdm_task_init(void)
{
	BaseType_t ret;

	sdmtaskqueue = xQueueCreate(M1_SDM_BUFFER_ARRAY_SIZE, sizeof(S_M1_SdCardManager_Q_t));
	assert(sdmtaskqueue != NULL);

	/*
	Note that binary semaphores created using
	 * the vSemaphoreCreateBinary() macro are created in a state such that the
	 * first call to 'take' the semaphore would pass, whereas binary semaphores
	 * created using xSemaphoreCreateBinary() are created in a state such that the
	 * the semaphore must first be 'given' before it can be 'taken'
	 *
	*/
	sdiosem = xSemaphoreCreateBinary();
	//xSemaphoreTake(sdiosem, portMAX_DELAY);

	/* create the software timer: one-shot timer.*/
	s_xstoptimer = xTimerCreate("SDMTim", 1, pdFALSE, NULL, m1_sdm_startup_check);
	assert(s_xstoptimer != NULL);

	/* Start thread */
	ret = xTaskCreate(m1_sdm_main_task, "SDManager_MainTask", M1_TASK_STACK_SIZE_1024, NULL, TASK_PRIORITY_SDCARD_MANAGER, &m1_sdm_task_hdl);
	assert(ret==pdPASS);
	assert(m1_sdm_task_hdl!=NULL);

	/* Create periodic timer to check SD card memory */
	m1_sdm_timer_hdl = xTimerCreate("m1_sdm_Memory_Timer", SD_CHECK_TIME, pdTRUE, NULL, m1_sdm_memory_timer_callback);
	assert_param(m1_sdm_timer_hdl != NULL);
} // void m1_sdm_task_init(void)




/*============================================================================*/
/**
  * @brief  De-initialize SD Card Manager thread and queue
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_sdm_task_deinit(void)
{
	if ( m1_sdm_task_hdl!=NULL )
	{
		vTaskDelete(m1_sdm_task_hdl);
		m1_sdm_task_hdl = NULL;
	}

	if ( m1_sdm_timer_hdl!=NULL )
	{
		xTimerDelete(m1_sdm_timer_hdl, 0);
		m1_sdm_timer_hdl = 0;
	}

	if ( sdiosem!=NULL )
	{
		vSemaphoreDelete(sdiosem);
		sdiosem = NULL;
	}

	if ( sdmtaskqueue!=NULL )
	{
		vQueueDelete(sdmtaskqueue);
		sdmtaskqueue = NULL;
	}
} // void m1_sdm_task_deinit(void)



/*============================================================================*/
/**
  * @brief  Start the SD card manager task
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_sdm_task_start(void)
{
	S_M1_SdCardManager_Q_t q_item;

	q_item.cmd_opt = M1_SDM_START_STOP;
	xQueueSend(sdmtaskqueue, &q_item, 0);
} // void m1_sdm_task_start(void)




/*============================================================================*/
/**
  * @brief  Stop the SD card manager task
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_sdm_task_stop(void)
{
	S_M1_SdCardManager_Q_t q_item;

	if (log_dev_status==M1_DATALOG_SD_STARTED)
	{
		log_dev_status = M1_DATALOG_IDLE;
		q_item.cmd_opt = M1_SDM_START_STOP;
		xQueueSend(sdmtaskqueue, &q_item, portMAX_DELAY);
	}

	while ( sd_logging_active==1 )
	{
		vTaskDelay(20);
	}
} // void m1_sdm_task_stop(void)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval
  */
/*============================================================================*/
static uint8_t m1_sdm_close_datfile(void)
{
	uint8_t r = f_close(m1_pdatfile_hdl);
	M1_LOG_I(M1_LOGDB_TAG, "SDM: f_close=%d\r\n", r);
	return r;
} // static uint8_t m1_sdm_close_datfile(void)




/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval
  */
/*============================================================================*/
static uint8_t m1_sdm_sync_datfile(void)
{
	uint8_t r = f_sync(m1_pdatfile_hdl);
	M1_LOG_I(M1_LOGDB_TAG, "SDM: f_sync=%d\r\n", r);
	return r;
} // static uint8_t m1_sdm_sync_datfile(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval
  */
/*============================================================================*/
uint8_t m1_sdm_get_logging_error(void)
{
	return sd_logging_error;
} // uint8_t m1_sdm_get_logging_error(void)



/*============================================================================*/
/**
  * @brief  Scan SD Card file system to find the latest filename number that
  * includes to the given prefix
  * @param  None
  * @retval
  * Note: FF_USE_FIND must be defined in ffconf.h to use the functions f_findfirst, etc.
  */
/*============================================================================*/
uint32_t m1_sdm_getlastfilenumber(char *dirname, char *prefix)
{
	FRESULT fr;     /* Return value */
	DIR dir;         /* Directory search object */
	FILINFO fno;    /* File information */
	int32_t tmp, file_n;
	char *filename;

	filename = malloc(strlen(prefix) + 6);
	/* assert() compiles out under NDEBUG (this project's actual ARM release
	 * build passes -DNDEBUG -- confirmed via compile_commands.json), so it
	 * was never a real safety net here; a failed allocation would fall
	 * through to sprintf() on a NULL pointer. This is reachable on every
	 * new SD-card data file created (not boot-time), so treat it as a
	 * recoverable failure: report file_n = 0, exactly the value already
	 * used when the scan below simply finds no matching files. The caller
	 * (m1_sdm_file_init()) already re-checks existence in its own retry
	 * loop before using whatever number this returns, so a false "0" here
	 * cannot cause it to silently reuse/overwrite an existing file -- it
	 * just costs a few extra existence-check iterations. */
	if (filename == NULL)
		return 0;
	sprintf(filename, "%s*", prefix); // * = wildcard
	file_n = 0;
	fr = f_findfirst(&dir, &fno, dirname, filename);  /* Start to search for matching files */
	while (fr==FR_OK && fno.fname[0])
	{
	    tmp = strtol(&fno.fname[strlen(prefix)], NULL, 10);
	    if (file_n < tmp)
	    {
	    	file_n = tmp;
	    }
	    fr = f_findnext(&dir, &fno);   /* Search for next item */
	} // while (fr==FR_OK && fno.fname[0])

	f_closedir(&dir);

	free(filename); /* was never released -- every call (once per new SD-card
	                 * data file created, e.g. m1_sdm_file_init() below) leaked
	                 * strlen(prefix)+6 bytes. */

	return file_n;
} // uint32_t m1_sdm_getlastfilenumber(char *dirname, char *prefix)



/*============================================================================*/
/**
  * @brief  Open file to store raw data
  * @param  None
  * @retval 1 for f_write error, 2 for mem init error, else 0
  */
/*============================================================================*/
uint8_t m1_sdm_file_init(S_M1_SDM_DatFileInfo_t *pfileinfo)
{
	uint8_t ret;
	uint32_t file_number;

	if ( m1_sdcard_get_status()!=SD_access_OK )
		return 1;

	if (!m1_fb_check_existence(pfileinfo->dir_name))
	{
		if ( m1_fb_make_dir(pfileinfo->dir_name))
	    {
			M1_LOG_E(M1_LOGDB_TAG, "Error creating directory on SD card!");
			return 1;
	    }
	} // if (!m1_fb_check_existence(dir_name))

	sprintf(pfileinfo->dat_filename, "%s%s%s_", pfileinfo->file_prefix, pfileinfo->file_infix, pfileinfo->file_suffix);
	file_number = m1_sdm_getlastfilenumber(pfileinfo->dir_name, pfileinfo->dat_filename);
	file_number++;
    //srand(HAL_GetTick());
    do
    {
    	sprintf(pfileinfo->dat_filename, "%s/%s%s%s_%d%s", pfileinfo->dir_name, pfileinfo->file_prefix, pfileinfo->file_infix, pfileinfo->file_suffix, file_number++, pfileinfo->file_ext);
    } while (m1_fb_check_existence(pfileinfo->dat_filename));

    m1_pdatfile_hdl = &pfileinfo->dat_file_hdl;

    ret = m1_fb_open_new_file(m1_pdatfile_hdl, pfileinfo->dat_filename);
    if (ret)
    {
    	M1_LOG_E(M1_LOGDB_TAG, "Error creating file on SD card!");
    	return 1;
    }

    ret = m1_sdm_memory_init();
    if ( ret )
    {
    	m1_fb_close_file(m1_pdatfile_hdl);
    	m1_fb_delete_file(pfileinfo->dat_filename);
    	return 2;
    }

	return 0;

} // uint8_t m1_sdm_file_init(S_M1_SDM_DatFileInfo_t *pfileinfo)



/*============================================================================*/
/**
  * @brief  Close all files
  * @param  None
  * @retval 1 for f_close error, else 0
  */
/*============================================================================*/
uint8_t m1_sdm_close_all_files(void)
{
	uint8_t ret = 0;

	if ( m1_sdcard_get_status()==SD_access_OK )
	{
		m1_sdm_flush_buffer();
		ret = m1_sdm_close_datfile();
	} // if ( m1_sdcard_get_status()==SD_access_OK )

	/* Deallocate here SD buffers to have enough memory for next section */
  	m1_sdm_memory_deinit();

  	return ret;
} // uint8_t m1_sdm_close_all_files(void)


/*============================================================================*/
/**
  * @brief  Write data buffer to SD card
  * @param  None
  * @retval 1 for f_write error, else 0
  */
/*============================================================================*/
uint8_t m1_sdm_write_buffer(uint8_t *buffer, uint32_t size)
{
	uint32_t byteswritten;

	if (f_write(m1_pdatfile_hdl, buffer, size, (void *)&byteswritten) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_sdm_write_buffer(uint8_t *buffer, uint32_t size)


/*============================================================================*/
/**
  * @brief  Write down all the data available
  * @param  id: sensor id
  * @retval 1 for f_write error, else 0
  */
/*============================================================================*/
uint8_t m1_sdm_flush_buffer(void)
{
	uint8_t *src, ret = 0, q_empty;
	uint32_t bufSize, psrc;

	q_empty = uxQueueMessagesWaiting(sdmtaskqueue); // Queue is not empty? This case should never happen!

	bufSize = dev_sd_hdl.sdWriteBufferSize;
	psrc = dev_sd_hdl.buff_info.sd_write_buffer_idx;
	src = dev_sd_hdl.buff_info.sd_write_buffer;
	src += sdwrite_buffer_id*bufSize;

	if ( psrc > 0 )
	{
		ret = m1_sdm_write_buffer(src, psrc);
	}

	M1_LOG_N(M1_LOGDB_TAG, "m1_sdm_flush_buffer: buffer_id %d write_size %d q messages: %d\r\n", sdwrite_buffer_id, psrc, q_empty);

	dev_sd_hdl.buff_info.sd_write_buffer_idx = 0;
	sdwrite_buffer_id = 0;

	return ret;
} // uint8_t m1_sdm_flush_buffer(void)


#define SDM_FILL_BUFFER_VERSION	1
/*============================================================================*/
/**
  * @brief  Fill SD buffer with new data
  * @param
  * @param  src: pointer to data buffer
  * @param  srcSize: buffer size
  * @retval 0: ok
  */
/*============================================================================*/
#if SDM_FILL_BUFFER_VERSION==2
uint8_t m1_sdm_fill_buffer(uint8_t *src, uint16_t srcSize)
{
	uint8_t *dst;
	uint32_t n_copy;
	uint32_t dstSize;
	static uint8_t sdwrite_buffer_id = 0;
	S_M1_SdCardManager_Q_t q_item;

	vTaskDelay(10); // Give the SD main task some time to do its job
	M1_LOG_I(M1_LOGDB_TAG, "m1_sdm_fill_buffer %d..\r\n", sdwrite_buffer_id);
	if ( uxQueueSpacesAvailable(sdmtaskqueue)==0 ) // Queue is full?
	{
		M1_LOG_N(M1_LOGDB_TAG, "Full!\r\n"); // Skip this data block
		return 1;
	} // if ( uxQueueSpacesAvailable(sdmtaskqueue)==0 )

	dstSize = dev_sd_hdl.sdWriteBufferSize;
	dst = dev_sd_hdl.buff_info.sd_write_buffer;
	dst += dstSize*sdwrite_buffer_id;
    n_copy = (srcSize < dstSize )?srcSize:dstSize;
    memcpy(dst, src, n_copy); // Copy to the destination buffer
    q_item.cmd_opt = M1_SDM_DATA_READY_MASK | sdwrite_buffer_id;
	q_item.write_size = n_copy;
	sdwrite_buffer_id++;
	if ( sdwrite_buffer_id >= M1_SDM_BUFFER_ARRAY_SIZE )
		sdwrite_buffer_id = 0;
	if ( xQueueSend(sdmtaskqueue, &q_item, 0) != pdPASS )
	{
		M1_LOG_N(M1_LOGDB_TAG, "Full!\r\n");
		return 1;
	}

	return 0;
} // uint8_t m1_sdm_fill_buffer(uint8_t *src, uint16_t srcSize)

#elif SDM_FILL_BUFFER_VERSION==1

uint8_t m1_sdm_fill_buffer(uint8_t *src, uint16_t srcSize)
{
	uint8_t *dst;
	uint32_t n_free, n_copy;
	uint32_t dstP, dstSize;
	S_M1_SdCardManager_Q_t q_item = {0};

	vTaskDelay(10); // Give the SD main task some time to do its job
	M1_LOG_N(M1_LOGDB_TAG, "m1_sdm_fill_buffer %d..", sdwrite_buffer_id);
	if ( uxQueueSpacesAvailable(sdmtaskqueue)==0 ) // Queue is full?
	{
		M1_LOG_N(M1_LOGDB_TAG, " Full!\r\n"); // Skip this data block
		vTaskDelay(10); // Give the SD main task more time
		return 1;
	} // if ( uxQueueSpacesAvailable(sdmtaskqueue)==0 )

	dstSize = dev_sd_hdl.sdWriteBufferSize;
	dst = dev_sd_hdl.buff_info.sd_write_buffer;
	dst += dstSize*sdwrite_buffer_id;
	dstP = dev_sd_hdl.buff_info.sd_write_buffer_idx;

	n_free = dstSize - dstP;
    n_copy = (n_free < srcSize)?n_free:srcSize;
    memcpy(&dst[dstP], src, n_copy); // Copy to the destination buffer
    dstP += n_copy; // Update the destination index
    srcSize -= n_copy; // Update the remainder
    if (dstP >= dstSize) // Index got an overflow?
    {
    	dstP = 0; // Reset
        q_item.cmd_opt = M1_SDM_DATA_READY_MASK | sdwrite_buffer_id;
        q_item.write_size = dstSize;
    	sdwrite_buffer_id++;
    	if ( sdwrite_buffer_id >= M1_SDM_BUFFER_ARRAY_SIZE )
    	{
    		sdwrite_buffer_id = 0;
    		dst = dev_sd_hdl.buff_info.sd_write_buffer;
    	} // if ( sdwrite_buffer_id >= M1_SDM_BUFFER_ARRAY_SIZE )
    	else
    	{
    		dst += dstSize;
    	}
    } // if (dstP >= dstSize)
    if (srcSize > 0) // Remainder needs to be copied?
    {
        memcpy(&dst[dstP], &src[n_copy], srcSize); // Copy the remainder to the start of the buffer
        dstP = srcSize; // Update the index
    }
	dev_sd_hdl.buff_info.sd_write_buffer_idx = dstP; // Update new write index

	if ( q_item.cmd_opt & M1_SDM_DATA_READY_MASK )
	{
		xQueueSend(sdmtaskqueue, &q_item, 0);
		M1_LOG_N(M1_LOGDB_TAG, " queued OK!\r\n");
	} // if ( q_item.cmd_opt & M1_SDM_DATA_READY_MASK )

	return 0;
} // uint8_t m1_sdm_fill_buffer(uint8_t *src, uint16_t srcSize)
#else
#error "SDM_FILL_BUFFER_VERSION is not defined yet!"
#endif // #elif SDM_FILL_BUFFER_VERSION==1


/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
static void m1_sdm_startup_check(TimerHandle_t xTimer)
{
	m1_sdm_task_stop();
} // static void m1_sdm_startup_check(TimerHandle_t xTimer)



/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
static void m1_sdm_memory_timer_callback(TimerHandle_t xTimer)
{
	S_M1_SdCardManager_Q_t q_item;

	q_item.cmd_opt = M1_SDM_CHECK_SDCARD_USAGE;
	xQueueSend(sdmtaskqueue, &q_item, 0); // portMAX_DELAY
} // static void m1_sdm_memory_timer_callback(TimerHandle_t xTimer)

