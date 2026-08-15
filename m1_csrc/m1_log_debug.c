/* See COPYING.txt for license details. */

/*
*
* m1_log_debug.c
*
* Library to support logging and debugging functionality
*
* M1 Project
*
* Reference: https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "m1_log_debug.h"
#include "main.h"
#include "app_freertos.h"
#include "semphr.h"
#include "task.h"
#include "cli_app.h"
#include "m1_ring_buffer.h"
#include "m1_usb_cdc_msc.h"

/*************************** D E F I N E S ************************************/

#define LOG_DEBUG_UART_IRQn			USART1_IRQn

#define UART_PUT_CHAR_TIMEOUT		2 // ms
#define UART_DEBUG_MSG_TX_TIMEOUT	10 // ms

#define M1_LOGDB_LEVEL_DEFAULT 		LOG_DEBUG_LEVEL_INFO


#define GET_MIN_NUM(m, n)                   ((m) < (n) ? (m) : (n))
#define GET_MAX_NUM(m, n)					((m) > (n) ? (m) : (n))
#define IS_BUFFER_VALID(pbuffer)            ((pbuffer!=NULL) && (pbuffer->pdata!=NULL) && (pbuffer->len > 0))

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

typedef struct {
	S_M1_LogDebugLevel_t log_level;
} S_M1_LogDebugConfig_t;

static S_M1_LogDebugConfig_t m1_logdb = {0};

typedef struct {
    const char *text;
    S_M1_LogDebugLevel_t level;
} S_M1_LogDebugDsc;

static const S_M1_LogDebugDsc M1_LOGDB_DESCRIPTIONS[] = {
    {"none", LOG_DEBUG_LEVEL_NONE},
    {"error", LOG_DEBUG_LEVEL_ERROR},
    {"warn", LOG_DEBUG_LEVEL_WARN},
    {"info", LOG_DEBUG_LEVEL_INFO},
    {"debug", LOG_DEBUG_LEVEL_DEBUG},
    {"trace", LOG_DEBUG_LEVEL_TRACE},
};

/***************************** V A R I A B L E S ******************************/

UART_HandleTypeDef huart_logdb;
DMA_HandleTypeDef hdma_logdb;
QueueHandle_t log_q_hdl = NULL;

DMA_HandleTypeDef hdma_rxlogdb;
static DMA_NodeTypeDef node_rxlogdb;
static DMA_QListTypeDef list_rxlogdb;
/* Buffer for logdb tx data */
uint8_t logdb_tx_buffer[M1_LOGDB_TX_BUFFER_SIZE];
uint8_t logdb_rx_buffer[M1_LOGDB_RX_BUFFER_SIZE];

static S_M1_RingBuffer logdb_tx_rb, *plogdb_tx_rb;
static volatile uint16_t logdb_dma_tx_len; // This variable may be modified by an interrupt

static SemaphoreHandle_t mutex_log_write_trans;
TaskHandle_t log_db_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
/*
 * printf() will call _write() which will call __io_putchar() by default to send data to UART
 * Both _write() and __io_putchar() are defined as (weak) in syscalls.c
 */
int __io_putchar (int ch);
int _write(int file, char *data, int len);

void m1_logdb_init(void);
void m1_logdb_deinit(void);
void m1_logdb_printf(S_M1_LogDebugLevel_t level, const char* tag, const char* format, ...);
void m1_logdb_write(const char* data);
static void m1_logdb_dyn_vsprintf(const char *format, va_list pargs, char **pstring);

static void m1_logdb_start_dma_tx(void);
void m1_logdb_update_tx_buffer(void);
uint8_t m1_logdb_check_empty_state(void);

void log_db_handler_task(void *param);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/*
 * This functions initializes the log_debug functionality
 */
/******************************************************************************/
void m1_logdb_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	DMA_NodeConfTypeDef NodeConfig= {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

	m1_logdb.log_level = M1_LOGDB_LEVEL_DEFAULT;

	// Initializes the peripherals clock
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
	PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
    	Error_Handler();
    }

	/* Enable Peripheral clock */
	__HAL_RCC_USART1_CLK_ENABLE();

	//__HAL_RCC_GPIOA_CLK_ENABLE();
	/* USART1 GPIO Configuration
	PA9     ------> USART1_TX (alias UART_1_TX)
	PA10    ------> USART1_RX (alias UART_1_RX)
	*/

	GPIO_InitStruct.Pin = UART_1_TX_Pin|UART_1_RX_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
	HAL_GPIO_Init(UART_1_TX_GPIO_Port, &GPIO_InitStruct);

	huart_logdb.Instance = USART1;
	//huart_logdb.Init.BaudRate = LOG_DEBUG_UART_BAUD;
	//huart_logdb.Init.WordLength = UART_WORDLENGTH_8B;
	//huart_logdb.Init.StopBits = UART_STOPBITS_1;
	//huart_logdb.Init.Parity = UART_PARITY_NONE;
	huart_logdb.Init.Mode = UART_MODE_TX_RX;
	huart_logdb.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart_logdb.Init.OverSampling = UART_OVERSAMPLING_16;
	huart_logdb.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart_logdb.Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart_logdb.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

	if (HAL_UART_Init(&huart_logdb) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_UARTEx_SetTxFifoThreshold(&huart_logdb, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_UARTEx_SetRxFifoThreshold(&huart_logdb, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_UARTEx_DisableFifoMode(&huart_logdb) != HAL_OK)
	{
		Error_Handler();
	}

  /*
  * USB CDC : Common settings for CDC_MODE_LOG_CLI and CDC_MODE_VCP
  */
  /* GPDMA1_REQUEST_USART1_RX Init */
  NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
  NodeConfig.Init.Request = GPDMA1_REQUEST_USART1_RX;
  NodeConfig.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
  NodeConfig.Init.Direction = DMA_PERIPH_TO_MEMORY;
  NodeConfig.Init.SrcInc = DMA_SINC_FIXED;
  NodeConfig.Init.DestInc = DMA_DINC_INCREMENTED;
  NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
  NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
  NodeConfig.Init.SrcBurstLength = 1;
  NodeConfig.Init.DestBurstLength = 1;
  NodeConfig.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT1;
  NodeConfig.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  NodeConfig.Init.Mode = DMA_NORMAL;
  NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
  NodeConfig.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
  NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
  if (HAL_DMAEx_List_BuildNode(&NodeConfig, &node_rxlogdb) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMAEx_List_InsertNode(&list_rxlogdb, NULL, &node_rxlogdb) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMAEx_List_SetCircularMode(&list_rxlogdb) != HAL_OK)
  {
    Error_Handler();
  }

  //hdma_rxlogdb.Instance = GPDMA1_Channel0;
  hdma_rxlogdb.Instance = GPDMA1_Channel2;
  hdma_rxlogdb.InitLinkedList.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
  hdma_rxlogdb.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
  hdma_rxlogdb.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT1;
  hdma_rxlogdb.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  hdma_rxlogdb.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
  if (HAL_DMAEx_List_Init(&hdma_rxlogdb) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMAEx_List_LinkQ(&hdma_rxlogdb, &list_rxlogdb) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_LINKDMA(&huart_logdb, hdmarx, hdma_rxlogdb);

  if (HAL_DMA_ConfigChannelAttributes(&hdma_rxlogdb, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    Error_Handler();
  }

  /* GPDMA1 interrupt Init */
  HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0); //configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1, 5);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);

 	/*
 	* GPDMA_CxTR2.REQSEL[7:0]		Selected GPDMA request
 	*			21						usart1_rx_dma
 	*			22						usart1_tx_dma
 	*/
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    hdma_logdb.Instance = GPDMA1_Channel1;
    hdma_logdb.Init.Request = GPDMA1_REQUEST_USART1_TX;
    hdma_logdb.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    hdma_logdb.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_logdb.Init.SrcInc = DMA_SINC_INCREMENTED;
    hdma_logdb.Init.DestInc = DMA_DINC_FIXED;
    hdma_logdb.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    hdma_logdb.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    hdma_logdb.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    hdma_logdb.Init.SrcBurstLength = 1;
    hdma_logdb.Init.DestBurstLength = 1;
    hdma_logdb.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT1;
    hdma_logdb.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hdma_logdb.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&hdma_logdb) != HAL_OK)
    {
    	Error_Handler();
    }
    if (HAL_DMA_ConfigChannelAttributes(&hdma_logdb, DMA_CHANNEL_NPRIV) != HAL_OK)
    {
    	Error_Handler();
    }

    if (m1_usbcdc_mode == CDC_MODE_VCP)
    {
    	/* USART1 DMA Init */
    	__HAL_LINKDMA(&huart_logdb, hdmatx, hdma_logdb);
    }
    else
    {
    	// Enable transfer complete interrupt
    	hdma_logdb.Instance->CCR |= DMA_CCR_TCIE;

    	// Enable DMA transmitter
    	huart_logdb.Instance->CR3 |= USART_CR3_DMAT_Msk;
    }

    /* GPDMA1 interrupt init */
    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0); //, 5);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

  	// Clear tx buffer
  	memset((void*)logdb_tx_buffer, 0x00, M1_LOGDB_TX_BUFFER_SIZE);

    m1_ringbuffer_init(&logdb_tx_rb, logdb_tx_buffer, M1_LOGDB_TX_BUFFER_SIZE, sizeof(uint8_t));

    logdb_dma_tx_len = 0;
    plogdb_tx_rb = &logdb_tx_rb; // Use this pointer for macro to avoid compile error

#ifdef M1_DEBUG_CLI_ENABLE
	/* Enable the UART global Interrupt */
	HAL_NVIC_SetPriority(LOG_DEBUG_UART_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(LOG_DEBUG_UART_IRQn);

	if (m1_usbcdc_mode == CDC_MODE_VCP)
	{
		// Enable UART IDLE line interrupt
		__HAL_UART_ENABLE_IT(&huart_logdb, UART_IT_IDLE);
		// Start receiving in DMA circular mode
		HAL_UART_Receive_DMA(&huart_logdb, (uint8_t *)logdb_rx_buffer, M1_LOGDB_RX_BUFFER_SIZE);
	}
	else
	{ /* CDC_MODE_LOG_CLI */
		// get USART ready to receive
		HAL_UART_Receive_IT(&huart_logdb, logdb_rx_buffer, 1);
	}
#endif // #ifdef M1_DEBUG_CLI_ENABLE

	mutex_log_write_trans = xSemaphoreCreateMutex();
	assert(mutex_log_write_trans);

	log_q_hdl = xQueueCreate(1, 1);
	assert(log_q_hdl!=NULL);
} // void m1_logdb_init(UART_HandleTypeDef *phuart)



/*============================================================================*/
/*
 * This function de-initializes the UART4
 *
 */
/*============================================================================*/
void m1_logdb_deinit(void)
{
    __HAL_RCC_USART1_CLK_DISABLE();

	/* USART1 GPIO Configuration
	PA9     ------> USART1_TX (alias UART_1_TX)
	PA10    ------> USART1_RX (alias UART_1_RX)
	*/
    HAL_GPIO_DeInit(UART_1_TX_GPIO_Port, UART_1_TX_Pin);
    HAL_GPIO_DeInit(UART_1_RX_GPIO_Port, UART_1_RX_Pin);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(LOG_DEBUG_UART_IRQn);

    __HAL_RCC_GPDMA1_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);
    HAL_DMA_DeInit(&hdma_logdb);

    // USB_CDC - USART1 TX
    HAL_NVIC_DisableIRQ(GPDMA1_Channel2_IRQn);
    HAL_DMA_DeInit(&hdma_rxlogdb);

    if ( mutex_log_write_trans != NULL )
    	vSemaphoreDelete(mutex_log_write_trans);
} // static void m1_logdb_deinit(void)



/*============================================================================*/
/*
 * This function starts the uart tx using DMA
 *
 */
/*============================================================================*/
static void m1_logdb_start_dma_tx(void)
{
    if ( !logdb_dma_tx_len )
    {
    	logdb_dma_tx_len = m1_ringbuffer_get_read_len(plogdb_tx_rb);

    	if ( logdb_dma_tx_len==0 )
    		return;

       if ( logdb_dma_tx_len > M1_LOGDB_DMA_TX_LEN ) // Too long?
            logdb_dma_tx_len = M1_LOGDB_DMA_TX_LEN;

	   /* Enable common interrupts: Transfer Complete and Transfer Errors ITs */
	   //__HAL_DMA_ENABLE_IT(&hdma_logdb, (DMA_IT_TC | DMA_IT_DTE | DMA_IT_ULE | DMA_IT_USE | DMA_IT_TO));

       //DMA_SetConfig()
	   /* Configure the DMA channel data size */
	   MODIFY_REG(hdma_logdb.Instance->CBR1, DMA_CBR1_BNDT, (logdb_dma_tx_len & DMA_CBR1_BNDT));
	   /* Clear all interrupt flags */
	   __HAL_DMA_CLEAR_FLAG(&hdma_logdb, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE | DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_SUSP |
		                       DMA_FLAG_TO);
	   /* Configure DMA channel source address */
	   hdma_logdb.Instance->CSAR = (uint32_t)m1_ringbuffer_get_read_address(plogdb_tx_rb);
	   /* Configure DMA channel destination address */
	   hdma_logdb.Instance->CDAR = (uint32_t)&huart_logdb.Instance->TDR;

	   /* Start DMA transfer */
	   __HAL_DMA_ENABLE(&hdma_logdb);
    } // if ( !logdb_dma_tx_len )

} // static void m1_logdb_start_dma_tx(void)


/*============================================================================*/
/*
 * This function starts the uart tx using USB CDC
 *
 */
/*============================================================================*/
static void m1_logdb_start_usbcdc_tx(void)
{
  uint8_t *bf;

  if ( !logdb_dma_tx_len )
  {
    //logdb_dma_tx_len = m1_logdb_get_read_len();
    logdb_dma_tx_len = m1_ringbuffer_get_read_len(plogdb_tx_rb);

    if ( logdb_dma_tx_len==0 )
      return;

    if ( logdb_dma_tx_len > USB_FS_CHUNK_SIZE ) // Too long?
    {
      logdb_dma_tx_len = USB_FS_CHUNK_SIZE;
    }
    //bf = (uint32_t)m1_logdb_get_read_address();
    bf = m1_ringbuffer_get_read_address(plogdb_tx_rb);
    CDC_Transmit_FS(bf, logdb_dma_tx_len);
  }
} // static void m1_logdb_start_usbcdc_tx(void)


/*============================================================================*/
/*
 * This function updates the tx buffer after the DMA transfer has been completed
 *
 */
/*============================================================================*/
void m1_logdb_update_tx_buffer(void)
{
	m1_ringbuffer_advance_read(plogdb_tx_rb, logdb_dma_tx_len);
    logdb_dma_tx_len = 0; // Clear after data has been transferred completely
    //m1_logdb_start_dma_tx();
} // void m1_logdb_update_tx_buffer(void)



/*============================================================================*/
/*
 * This function returns true if the ring buffer is empty
 *
 */
/*============================================================================*/
uint8_t m1_logdb_check_empty_state(void)
{
	return m1_ringbuffer_check_empty_state(plogdb_tx_rb);
} // uint8_t m1_logdb_check_empty_state(void)


/*============================================================================*/
/*
 * This function directs the printf() to UART
 * printf() will call _write() which will call __io_putchar() by default to send data to UART
 * Both _write() and __io_putchar() are defined as (weak) in syscalls.c
  */
/*============================================================================*/
int __io_putchar (int ch)
{
	uint32_t tickstart = HAL_GetTick();

	// Wait until the Transmit data register empty flag is set
	while ((__HAL_UART_GET_FLAG(&huart_logdb, UART_FLAG_TXE) ? SET : RESET) == RESET)
	{
		if ( (HAL_GetTick() - tickstart) > UART_PUT_CHAR_TIMEOUT )
		{
			return EOF; // Timeout error
		}
	} // while ((__HAL_UART_GET_FLAG(&huart_logdb, UART_FLAG_TXE) ? SET : RESET) == RESET)

	// Send the character
	huart_logdb.Instance->TDR = ch;

	// Wait until the Transmission complete register flag is set
	while ((__HAL_UART_GET_FLAG(&huart_logdb, UART_FLAG_TC) ? SET : RESET) == RESET)
	{
		if ( (HAL_GetTick() - tickstart) > UART_PUT_CHAR_TIMEOUT )
		{
			return EOF; // Timeout error
		}
	} // while ((__HAL_UART_GET_FLAG(&huart_logdb, UART_FLAG_TXE) ? SET : RESET) == RESET)

	return ch;
} // int __io_putchar (int ch)



int _write(int file, char *data, int len)
{
	uint8_t q_item;

	UNUSED(file);

	xSemaphoreTake(mutex_log_write_trans, portMAX_DELAY);

    len = m1_ringbuffer_write(plogdb_tx_rb, data, len);
    xQueueSend(log_q_hdl, &q_item, 0);
    //uint32_t pri_mask;
    //pri_mask = __get_PRIMASK();
    //__disable_irq();
    //m1_logdb_start_dma_tx();
    //__set_PRIMASK(primask);

    xSemaphoreGive(mutex_log_write_trans);

    return len;
} // int _write(int file, char *data, int len)



/*============================================================================*/
/*
 * This task writes log data to the UART
 *
 */
/*============================================================================*/
void log_db_handler_task(void *param)
{
	BaseType_t ret;
	uint8_t q_item;

	while (1)
	{
	    ret = xQueueReceive(log_q_hdl, &q_item, portMAX_DELAY);
	    if ( ret==pdPASS )
	    {
	    	if ( (hUsbDeviceFS.pClassData != NULL) &&
	    			(m1_USB_CDC_ready == 0) )
	    	{
	    		m1_logdb_start_usbcdc_tx();
	    	}
	    	else
	    	{
	    		m1_logdb_start_dma_tx();
	    	}
	    	//vTaskDelay(0);
	    } // if ( ret==pdPASS )
	} // while (1)
} // void log_db_handler_task(void *param)




/*============================================================================*/
/*
 * This function writes data to the debug/log output port
 */
/*============================================================================*/
void m1_logdb_write(const char* data)
{
	printf("%s", data);
	fflush(stdout);
	//huart->gState = HAL_UART_STATE_BUSY_TX;
	/* At end of Tx process, restore huart->gState to Ready */
	//huart->gState = HAL_UART_STATE_READY;
} // void m1_logdb_write(const char* data)



/*============================================================================*/
/*
 * This function writes a formatted debug/log message to the debug/log output
 */
/*============================================================================*/
void m1_logdb_printf(S_M1_LogDebugLevel_t level, const char* tag, const char* format, ...)
{
	char *db_string;
	char *log_choice = " ";
	va_list pargs;

	if (level > m1_logdb.log_level)
		return;

    switch(level)
    {
    	case LOG_DEBUG_LEVEL_ERROR:
    		log_choice = "E";
    		break;
    	case LOG_DEBUG_LEVEL_WARN:
    		log_choice = "W";
    		break;
    	case LOG_DEBUG_LEVEL_INFO:
    		log_choice = "I";
    		break;
    	case LOG_DEBUG_LEVEL_DEBUG:
    		log_choice = "D";
    		break;
    	case LOG_DEBUG_LEVEL_TRACE:
    		log_choice = "T";
    		break;
    	default:
    		log_choice = "R"; // Raw log data without additional info
    		break;
    } // switch(level)

    db_string = (char *)malloc(M1_LOGDB_MESSAGE_SIZE);
    if ( db_string==NULL )
    {
    	return; // Do nothing if memory allocation fails
    }

    if  (*log_choice != 'R')
    {
    	sprintf(db_string, " %lu [%s][%s] ", HAL_GetTick(), log_choice, tag);
    	m1_logdb_write(db_string);
    }

    va_start(pargs, format);
    m1_logdb_dyn_vsprintf(format, pargs, &db_string);
    va_end(pargs);

    if ( db_string!=NULL )
    {
        m1_logdb_write(db_string);
        free(db_string);
    } // if ( db_string!=NULL )

    //m1_logdb_write("\r\n");

} // void m1_logdb_printf(S_M1_LogDebugLevel_t level, const char* tag, const char* format, ...)



/*============================================================================*/
/*
 * This function copy the log/debug message to the buffer.
 * The buffer size will be dynamically adjusted.
 * Its purpose is replace the standard function vsprintf().
 *
  */
/*============================================================================*/
static void m1_logdb_dyn_vsprintf(const char *format, va_list pargs, char **pstring)
{
	int ret_n;
	int mem_size;
	va_list pargsc;

	mem_size = M1_LOGDB_MESSAGE_SIZE; // Set the default size first
	while (1)
	{
		va_copy(pargsc, pargs); // Make a copy of the argument list
		ret_n = vsnprintf(*pstring, mem_size, format, pargsc); // Try it
		if ( ret_n > -1 ) // Good try?
		{
			if (ret_n < mem_size) // Good size?
				break; // Exit loop and return
			else
				mem_size = ret_n + 20; // Adjust the allocated space
		} // if ( ret_n > -1 )
		else
			mem_size = UCHAR_MAX; // Exit condition

		free(*pstring); // Free allocated memory slot
		*pstring = NULL; // Caller function may check NULL for error condition
		if ( mem_size > 2*M1_LOGDB_MESSAGE_SIZE ) // Memory size exceeds the maximum allowed size?
			break; // Exit
		*pstring = (char *)malloc(mem_size);
	    if ( *pstring==NULL )
	    {
	    	break; // Do nothing if memory allocation fails
	    }
	} // while (1)
} // static void m1_logdb_dyn_vsprintf(const char *format, va_list pargs, char **pstring)
