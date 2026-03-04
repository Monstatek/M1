/* See COPYING.txt for license details. */

/*
 *  m1_int_hdl.c
 *
 *  M1 interrupts handlers
 *
 * M1 Project
 */
/*************************** I N C L U D E S **********************************/

#include <m1_sub_ghz_decenc.h>
#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_int_hdl.h"
#include "m1_lcd.h"
#include "m1_infrared.h"
#include "irsnd.h"
#include "m1_sdcard.h"
#include "m1_esp32_hal.h"
#include "m1_cli.h"
#include "m1_sub_ghz.h"
#include "m1_sub_ghz_api.h"
//#include "spi_drv.h"
#include "spi_master.h"
#include "m1_rfid.h"
#include "lfrfid.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/* Sub-GHz RX ring buffer overflow diagnostics */
uint32_t DEBUG_subghz_rb_overflow_cnt = 0;
uint32_t DEBUG_subghz_rb_highwater = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function handles External interrupt for the Fuel gauge.
 */
/*============================================================================*/

void EXTI3_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(FG_INT_Pin);
} // void EXTI8_IRQHandler(void)



/*============================================================================*/
/*
 * This function handles External interrupt for the USB-C Power delivery.
 */
/*============================================================================*/
void EXTI6_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(USBC_INT_Pin);
} // void EXTI8_IRQHandler(void)



/*============================================================================*/
/*
 * This function handles External interrupt for the battery charger.
 */
/*============================================================================*/
void EXTI8_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(I2C_INT_Pin);
} // void EXTI8_IRQHandler(void)



/*============================================================================*/
/*
 * This function is the callback function of the External interrupt handlers
 */
/*============================================================================*/
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin==I2C_INT_Pin) // Battery charger external interrupt - PB8
    {
    	;
    }

    if (GPIO_Pin==FG_INT_Pin) // Fuel gauge external interrupt - PE3
    {
    	;
    }

    if (GPIO_Pin==USBC_INT_Pin) // USB-C Power delivery external interrupt - PC6
    {
    	;
    }
} // void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)


/*============================================================================*/
/*
 * This function handles EXTI15 global interrupt for SD card
 *
 */
/*============================================================================*/
void EXTI15_IRQHandler(void)
{
	S_M1_SdCard_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	if ( sdcard_status_changed==0 )
	{
		sdcard_status_changed = 1;
		q_item.q_evt_type = Q_EVENT_SDCARD_CHANGE;
		xQueueSendFromISR(sdcard_det_q_hdl, &q_item, &xHigherPriorityTaskWoken);
		//xQueueOverwriteFromISR(sdcard_det_q_hdl, &q_item, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	} // if ( sdcard_status_changed==0 )
	HAL_EXTI_IRQHandler(&sdcard_exti_hdl);

} // void EXTI15_IRQHandler(void)


/**
  *
  * @brief EXTI line detection callback, used as SPI handshake GPIO
  * /*
  * This function is called when the handshake line goes high.
  * There are two ways to trigger the GPIO interrupt:
  * 1. Master sends data, slave has received successfully
  * 2. Slave has data want to transmit
  *
  */
void ESP32_GPIO_EXTI_Callback(void)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
    //Give the semaphore.
    spi_master_msg_t spi_msg = {
        .slave_notify_flag = true,
    };
    if (esp_spi_msg_queue)
    {
    	xQueueSendFromISR(esp_spi_msg_queue, (void*)&spi_msg, &xHigherPriorityTaskWoken ); // notify master to do next transaction
    	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
} // void ESP32_GPIO_EXTI_Callback(void)



/*============================================================================*/
/*
 * This function handles EXTI1 global interrupt for ESP32 DATAREADY
 *
 */
/*============================================================================*/
void EXTI1_IRQHandler(void)
{
	ESP32_GPIO_EXTI_Callback();
	HAL_EXTI_IRQHandler(&esp32_exti_dataready);

} // void EXTI1_IRQHandler(void)



/*============================================================================*/
/*
 * This function handles EXTI7 global interrupt for ESP32 HANDSHAKE
 *
 */
/*============================================================================*/
void EXTI7_IRQHandler(void)
{
	ESP32_GPIO_EXTI_Callback();
	HAL_EXTI_IRQHandler(&esp32_exti_handshake);

} // void EXTI1_IRQHandler(void)



/*============================================================================*/
/*
 * This function handles EXTI12 global interrupt for SI4463
 * Si446x NIRQ
 * Let check the Packet Handler interrupt for PRx interrupt
 * Modem Handler interrupt will be handled by other interrupt handler
 ***The nIRQ pin will remain low until the microcontroller reads the Interrupt Status Registers.
*/
/*============================================================================*/
void EXTI12_IRQHandler(void)
{
    if ( radio_state_flag & RADIO_STATE_TX )
    {
    	radio_state_flag = RADIO_STATE_IDLE;
    }
    else
    {
        si446x_nIRQ_active = TRUE; // CMD error or Rx FIFO full
    }

	HAL_EXTI_IRQHandler(&si4463_exti_hdl);
} // void EXTI12_IRQHandler(void)

/*============================================================================*/
/*
 * This function handles TIM2 global interrupt for Infrared Rx & RFID
 */
/*============================================================================*/
void TIM2_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&Timerhdl_IrRx);
} // void TIM2_IRQHandler(void)




/*============================================================================*/
/*
 * This function handles TIM16 global interrupt for Infrared Rx-Tx.
 */
/*============================================================================*/
void TIM16_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&Timerhdl_IrTx);
} // void TIM16_IRQHandler(void)



/******************************************************************************/
/*
 * USART1 Interrupt handler
 */
/******************************************************************************/
void USART1_IRQHandler(void)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	if (m1_usbcdc_mode == CDC_MODE_VCP)
	{
		/* USART1 rx and USB CDC tx */
		HAL_UART_IRQHandler(&huart_logdb);

		// IDLE interrupt
		if (huart_logdb.Instance->ISR & UART_FLAG_IDLE)
		{
			__HAL_UART_CLEAR_IDLEFLAG(&huart_logdb); // Clear IDLE flag

			usart_rxupdate_head_pointer();
			usart_rxdata_process_from_isr();
		}
	}
	else
	{
#ifdef M1_DEBUG_CLI_ENABLE
		HAL_UART_IRQHandler(&huart_logdb);

		//get ready to receive another char
		HAL_UART_Receive_IT(&huart_logdb, logdb_rx_buffer, 1);

		if ( (hUsbDeviceFS.pClassData != NULL) &&
				(m1_USB_CDC_ready == 0) )
		{ // add device ID
			logdb_rx_buffer[1] = TASK_NOTIFY_USBCDC;
			// cancel ...
		}
		else
		{ // add device ID
			logdb_rx_buffer[1] = TASK_NOTIFY_USART1;
			//send the char to the command line task
			xTaskNotifyFromISR(cmdLineTaskHandle, (uint32_t)logdb_rx_buffer[0], eSetValueWithOverwrite , &xHigherPriorityTaskWoken);
		}
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
#endif // #ifdef M1_DEBUG_CLI_ENABLE
	}
} // void USART1_IRQHandler(void)


/******************************************************************************/
/*
 * @brief Tx Transfer completed callback.
 * @param huart UART handle.
 * @retval None
 */
/******************************************************************************/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  // Call after TX GPDMA transmission is complete
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if(huart->Instance == USART1)
  {
    //huart->gState = HAL_UART_STATE_READY; // State explicit reset

#if 1
    tx_cptl_usart1 = 0;
#else
    if (usb2ser_tx_semaphore != NULL) {
      // Release xUsartTxSemaphore to wake up UsartTxTask
      xSemaphoreGiveFromISR(usb2ser_tx_semaphore, &xHigherPriorityTaskWoken);
    }
#endif
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}


/******************************************************************************/
/*
 * @brief This function handles GPDMA1 Channel 0 global interrupt.
 *        USART1 - RX
 */
/******************************************************************************/
void GPDMA1_Channel2_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_rxlogdb);

  usart_rxupdate_head_pointer();
  usart_rxdata_process_from_isr();
}


/******************************************************************************/
/*
 * @brief This function handles USB FS global interrupt.
 */
/******************************************************************************/
void USB_DRD_FS_IRQHandler(void)
{
  /* USER CODE BEGIN USB_DRD_FS_IRQn 0 */

  /* USER CODE END USB_DRD_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_DRD_FS);
  /* USER CODE BEGIN USB_DRD_FS_IRQn 1 */

  /* USER CODE END USB_DRD_FS_IRQn 1 */
}


/******************************************************************************/
/*
 * @brief This function handles SPI3 global interrupt.
 */
/******************************************************************************/
void SPI3_IRQHandler(void)
{
  /* USER CODE BEGIN SPI3_IRQn 0 */

  /* USER CODE END SPI3_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi_esp);
  /* USER CODE BEGIN SPI3_IRQn 1 */

  /* USER CODE END SPI3_IRQn 1 */
} // void SPI3_IRQHandler(void)



/******************************************************************************/
/*
 * DMA for UART Interrupt handler
 */
/******************************************************************************/
void GPDMA1_Channel1_IRQHandler(void)
{
	QueueHandle_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
    /* Transfer Complete Interrupt */
    if ( (__HAL_DMA_GET_FLAG(&hdma_logdb, DMA_FLAG_TC) != 0U) )
    {
    	/* Check if interrupt source is enabled */
    	if ( __HAL_DMA_GET_IT_SOURCE(&hdma_logdb, DMA_IT_TC) != 0U )
    	{
    		m1_logdb_update_tx_buffer(); // Update ring buffer counter
    		if ( !m1_logdb_check_empty_state() ) // There's still data to send?
    		{
    			xQueueSendFromISR(log_q_hdl, &q_item, &xHigherPriorityTaskWoken);
    			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    		} // if ( !m1_logdb_check_empty_state() )
    	} // if ( __HAL_DMA_GET_IT_SOURCE(&hdma_logdb, DMA_IT_TC) != 0U )
    } // if ( (__HAL_DMA_GET_FLAG(&hdma_logdb, DMA_FLAG_TC) != 0U) )

	HAL_DMA_IRQHandler(&hdma_logdb);
} // void GPDMA1_Channel1_IRQHandler(void)



/******************************************************************************/
/*
 * DMA for Sub-GHz Tx mode
 */
/******************************************************************************/
void GPDMA1_Channel0_IRQHandler(void)
{
	uint32_t flag = __HAL_DMA_GET_FLAG(&hdma_subghz_tx, DMA_FLAG_TC);

	HAL_DMA_IRQHandler(&hdma_subghz_tx);

    /* Transfer Complete Interrupt */
    if ( flag )
    {
    	subghz_tx_tc_flag = 1;
    } // if ( flag )
} // void GPDMA1_Channel1_IRQHandler(void)




/******************************************************************************/
/*
 * DMA for UART4 Interrupt handler, Tx for ESP32
 */
/******************************************************************************/
void GPDMA1_Channel5_IRQHandler(void)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	/* Transfer Complete Interrupt */
    if ( (__HAL_DMA_GET_FLAG(&hgpdma1_channel5_tx, DMA_FLAG_TC) != 0U) )
    {
    	/* Check if interrupt source is enabled */
    	if ( __HAL_DMA_GET_IT_SOURCE(&hgpdma1_channel5_tx, DMA_IT_TC) != 0U )
    	{
    		; // Do something here if necessary
    	}
    	xSemaphoreGiveFromISR(sem_esp32_trans, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } // if ( (__HAL_DMA_GET_FLAG(&hgpdma1_channel5_tx, DMA_FLAG_TC) != 0U) )

	HAL_DMA_IRQHandler(&hgpdma1_channel5_tx);
} // void GPDMA1_Channel5_IRQHandler(void)





/******************************************************************************/
/*
 * UART4 Interrupt handler, Rx/Tx for ESP32
 */
/******************************************************************************/
void UART4_IRQHandler(void)
{
    if ( __HAL_UART_GET_FLAG(&huart_esp, UART_FLAG_RXFNE) || __HAL_UART_GET_FLAG(&huart_esp, UART_FLAG_ORE) )
    {
    	/* Check if interrupt source is enabled */
    	if ( __HAL_UART_GET_IT_SOURCE(&huart_esp, UART_IT_RXFNE) != 0U )
    	{
    		esp32_uartrx_handler(huart_esp.Instance->RDR);
    	}
        // Error(s) should be cleared here before calling the default ISR
        // ISR may disable interrupt unexpectedly if it detects error(s) in the Status Register
    	__HAL_UART_CLEAR_FLAG(&huart_esp, UART_CLEAR_OREF);
    } // if ( __HAL_UART_GET_FLAG(&huart_esp, UART_FLAG_RXFNE) || __HAL_UART_GET_FLAG(&huart_esp, UART_FLAG_ORE) )

    HAL_UART_IRQHandler(&huart_esp);
} // void UART4_IRQHandler(void)




/******************************************************************************/
/*
 * SDMMC1 Interrupt handler
 */
/******************************************************************************/
/*
//Implemented in stm32h5xx_it.c
void SDMMC1_IRQHandler(void)
{
	HAL_SD_IRQHandler(&hsd1);
} // void SDMMC1_IRQHandler(void)
*/


/*============================================================================*/
/**
  * @brief  Period elapsed callback in non blocking mode - timeout
  * @param  htim: TIM handle
  * @retval None
  */
/*============================================================================*/
void HAL_TIM_PeriodElapsedCallback_IR(TIM_HandleTypeDef *htim)
{
	uint32_t cap_val;
	S_M1_Main_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	if (htim == &Timerhdl_IrTx )
	{
		if ( ir_ota_data_tx_active )
		{
			if ( ir_ota_data_tx_counter < ir_ota_data_tx_len )
			{
				cap_val = pir_ota_data_tx_buffer[ir_ota_data_tx_counter - 1]; // Take the transmitted data
				if ( Timerhdl_IrTx.Instance->ARR & 0x0001 ) // Tx data of this period (this bit) is a Mark
				{
					if ( !(cap_val & 0x0001) ) // Previous tx_ed is a Space?
						irsnd_on(); // Let turn on the Carrier
				}
				else // Tx data for this period (this bit) is a Space
				{
					if ( cap_val & 0x0001 ) // Previous tx_ed is a Mark?
						irsnd_off(); // Let turn off the Carrier
				}
				// Update reload value for the next bit (next period)
				Timerhdl_IrTx.Instance->ARR = pir_ota_data_tx_buffer[++ir_ota_data_tx_counter];
			} // if ( ir_ota_data_tx_counter <= ir_ota_data_tx_len )
			else
			{
				irsnd_off(); // Let turn off the Carrier
				ir_ota_data_tx_active = FALSE;
				q_item.q_data.ir_tx_data = 1; // any value, not used
				q_item.q_evt_type = Q_EVENT_IRRED_TX;
				xQueueSendFromISR(main_q_hdl, &q_item, &xHigherPriorityTaskWoken); // Send sample to queue, return: pdPASS or errQUEUE_FULL
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
		} // if ( ir_ota_data_tx_active )
	} // if (htim == &Timerhdl_IrTx )

	else if (htim == &Timerhdl_IrRx )
	{
		cap_val = __HAL_TIM_GET_COUNTER(htim); // get the timeout counter
		__HAL_TIM_SET_COUNTER(htim, 0); // reset counter after reading, htim->Instance->CNT = 0x00;
		IrRx_Edge_Det = EDGE_DET_IDLE; // timeout case, let reset this flag
		if ( irmp_start_bit_is_detected() )
		{
			//cap_val = IRMP_TIMEOUT_TIME + 1;
			cap_val = Timerhdl_IrRx.Init.Period + 1; // Plus 1 for the timeout condition to be met
			q_item.q_evt_type = Q_EVENT_IRRED_RX;
			q_item.q_data.ir_rx_data.ir_edge_te = cap_val;
			q_item.q_data.ir_rx_data.ir_edge_dir = (IR_RX_GPIO_Port->IDR & IR_RX_Pin)?1:0; // edge: '1' for Rising  or '0' for falling edge
			xQueueSendFromISR(main_q_hdl, &q_item, &xHigherPriorityTaskWoken); // Send sample to queue, return: pdPASS or errQUEUE_FULL
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		} // if ( irmp_start_bit_is_detected() )
	}
} // void HAL_TIM_PeriodElapsedCallback_IR(TIM_HandleTypeDef *htim)



/*============================================================================*/
/**
  * @brief  Capture callback in non blocking mode - level change
  * @param  htim: TIM handle
  * @retval None
  */
/*============================================================================*/
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	uint32_t cap_val;
	S_M1_Main_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	if (htim->Channel == IR_DECODE_TIMER_DEC_CH_ACTIV)
	{
		/* - Timer Falling Edge Event:
    	The Timer interrupt is used to measure the period between two
    	successive falling edges (The whole pulse duration).

    	- Timer Rising Edge Event:
    	It is also used to measure the duration between falling and rising
    	edges (The low pulse duration).
    	The two durations are useful to determine the bit value. Each bit is
    	determined according to the last bit.

    	Update event:InfraRed decoders time out event.
    	---------------------------------------------
    	It resets the InfraRed decoders packet.
    	- The Timer Overflow is set to 3.6 ms .*/

		//cap_val =  HAL_TIM_ReadCapturedValue(htim, IR_DECODE_TIMER_RX_CHANNEL);
		//cap_val = __HAL_TIM_GET_COMPARE(htim, IR_DECODE_TIMER_RX_CHANNEL); // read compare counter
		//__HAL_TIM_SET_COMPARE(htim, IR_DECODE_TIMER_RX_CHANNEL, 0); // reset counter after reading, htim->Instance->CCR4 = 0x00;
		cap_val =  __HAL_TIM_GET_COUNTER(htim);
		__HAL_TIM_SET_COUNTER(htim, 0); // reset counter after reading, htim->Instance->CNT = 0x00;
		if ( IrRx_Edge_Det==EDGE_DET_IDLE )
		{
			if ((IR_RX_GPIO_Port->IDR & IR_RX_Pin) == 0U) // A falling edge just happened?
			{
				IrRx_Edge_Det = EDGE_DET_FALLING; // Update current edge
				q_item.q_data.ir_rx_data.ir_edge_te = 0;
				q_item.q_data.ir_rx_data.ir_edge_dir = EDGE_DET_FALLING; // edge: '1' for Rising  or '0' for falling edge
			}
			return; // Do nothing
		} // if ( IrRx_Edge_Det==EDGE_DET_IDLE )
		if ( IrRx_Edge_Det==EDGE_DET_FALLING ) // Previous edge was falling?
		{
			IrRx_Edge_Det = EDGE_DET_RISING; // Update current edge
			q_item.q_data.ir_rx_data.ir_edge_te = cap_val;
			q_item.q_data.ir_rx_data.ir_edge_dir = EDGE_DET_RISING; // edge: '1' for Rising  or '0' for falling edge
		} // if ( IrRx_Edge_Det==EDGE_DET_IDLE )
		else // Previous edge was rising. This edge is falling
		{
			IrRx_Edge_Det = EDGE_DET_FALLING; // Update current edge
			q_item.q_data.ir_rx_data.ir_edge_te = cap_val;
			q_item.q_data.ir_rx_data.ir_edge_dir = EDGE_DET_FALLING; // edge: '1' for Rising  or '0' for falling edge
		}
		q_item.q_evt_type = Q_EVENT_IRRED_RX;
		xQueueSendFromISR(main_q_hdl, &q_item, &xHigherPriorityTaskWoken); // Send sample to queue, return: pdPASS or errQUEUE_FULL
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	} // if (htim->Channel == IR_DECODE_TIMER_DEC_CH_ACTIV)
} // void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)



/*============================================================================*/
/**
  * @brief  Interrupt handler for TIM1 update event for Sub-GHz Tx
  * @param
  * @retval None
  */
/*============================================================================*/
void TIM1_UP_IRQHandler(void)
{
	S_M1_Main_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
	uint16_t toggle;

	// Clear the update interrupt flag
	__HAL_TIM_CLEAR_FLAG(&timerhdl_subghz_tx, TIM_FLAG_UPDATE);
	if ( !subghz_tx_tc_flag ) // DMA not completed?
	{
		// The fix for the case when an IRS interrupt handler may be missed due to a short pulse (<<100us).
		// In that case, the following bits' polarity will be inverted.
		// Solution:
		// Each time this ISR occurs, the DMA counter (CBR1) is expected to decrease by one data block.
		// If an ISR is missed, the DMA counter will decrease more than one data block in the next ISR.
		// If that happens, the CCR4 should be kept unchanged.
		// Or
		// SUBGHZ_TX_GPIO_PIN should always be HIGH at the odd data block of a DMA transfer.
		// That means, an odd data block should be in sync with the HIGH at SUBGHZ_TX_GPIO_PIN.
		// Otherwise, an ISR may be missed. In that case, keep the CCR4 unchanged.
		// DMA transfer is always ahead of this ISR.
		toggle = subghz_decenc_ctl.ntx_raw_len - hdma_subghz_tx.Instance->CBR1; // Data blocks transferred
		toggle >>= 1; // Convert length from bytes to block
		toggle &= 0x01; // Make odd or even data block
		if ( SUBGHZ_TX_GPIO_PORT->IDR & SUBGHZ_TX_GPIO_PIN ) // Pin is high?
			toggle ^= 1; // Combine the data block and the output data level

		if ( toggle )
			timerhdl_subghz_tx.Instance->CCR4 ^= 0xFFFF; // toggle to create high pulse (100% PWM) and low pulse (0% PWM)
	} // if ( !subghz_tx_tc_flag )
	else // DMA completed
	{
		if ( subghz_tx_tc_flag++ > 2 ) // DMA is 2-bits ahead of this timer.
		{
			q_item.q_evt_type = Q_EVENT_SUBGHZ_TX;
			xQueueSendFromISR(main_q_hdl, &q_item, &xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			subghz_tx_tc_flag = 0;
		}
		timerhdl_subghz_tx.Instance->CCR4 = 0;
	} // else
} // void TIM1_UP_IRQHandler(void)



/*============================================================================*/
/**
  * @brief  Interrupt handler for TIM1 Capture and Compare
  * @param  htim: TIM handle
  * @retval None
  */
/*============================================================================*/
void TIM1_CC_IRQHandler(void)
{
	uint32_t cap_val;
	S_M1_Main_Q_t q_item;
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
	static uint16_t pulse_counter = 0;
	uint8_t send_to_q;

	/* Clear Capture Compare flag */
	__HAL_TIM_CLEAR_FLAG(&timerhdl_subghz_rx, TIM_FLAG_CC1);

	//cap_val =  HAL_TIM_ReadCapturedValue(htim, IR_DECODE_TIMER_RX_CHANNEL);
	//cap_val = __HAL_TIM_GET_COMPARE(htim, IR_DECODE_TIMER_RX_CHANNEL); // read compare counter
	//__HAL_TIM_SET_COMPARE(htim, IR_DECODE_TIMER_RX_CHANNEL, 0); // reset counter after reading, htim->Instance->CCR4 = 0x00;
	cap_val =  __HAL_TIM_GET_COUNTER(&timerhdl_subghz_rx);
	__HAL_TIM_SET_COUNTER(&timerhdl_subghz_rx, 0); // reset counter after reading, htim->Instance->CNT = 0x00;

	if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_IDLE )
	{
		if ( SUBGHZ_RX_GPIO_PORT->IDR & SUBGHZ_RX_GPIO_PIN ) // A rising edge detected?
		{
			subghz_decenc_ctl.pulse_det_stat = PULSE_DET_ACTIVE; // Wake up
		}
	} // if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_IDLE )
	else if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_EOP )
	{
		subghz_decenc_ctl.pulse_det_stat = PULSE_DET_NORMAL; // Update state
	} // else if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_EOP )

	if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_ACTIVE ) // Waiting for a rising edge
	{
		if ( SUBGHZ_RX_GPIO_PORT->IDR & SUBGHZ_RX_GPIO_PIN ) // A rising edge detected?
		{
			subghz_decenc_ctl.pulse_det_stat = PULSE_DET_NORMAL; // Update state
			subghz_decenc_ctl.pulse_det_pol = PULSE_DET_RISING; // Update current edge
			pulse_counter = 0;
		} // if ( SUBGHZ_RX_GPIO_PORT->IDR & SUBGHZ_RX_GPIO_PIN )
		return;
	} // if ( subghz_decenc_ctl.pulse_det_stat==PULSE_DET_ACTIVE )

	if ( subghz_decenc_ctl.pulse_det_pol==PULSE_DET_RISING ) // Previous edge was rising?
	{
		if ( SUBGHZ_RX_GPIO_PORT->IDR & SUBGHZ_RX_GPIO_PIN ) // A rising edge detected?
			return; // A falling edge might be missed. Skip this one.
		subghz_decenc_ctl.pulse_det_pol = PULSE_DET_FALLING; // Update current edge
		q_item.q_data.ir_rx_data.ir_edge_te = cap_val;
		q_item.q_data.ir_rx_data.ir_edge_dir = PULSE_DET_FALLING; // edge: '1' for Rising  or '0' for falling edge
	} // if ( subghz_decenc_ctl.pulse_det_pol==PULSE_DET_RISING )
	else // Previous edge was falling
	{
		if ( !(SUBGHZ_RX_GPIO_PORT->IDR & SUBGHZ_RX_GPIO_PIN) ) // A falling edge detected?
			return; // A rising edge might be missed. Skip this one.
		subghz_decenc_ctl.pulse_det_pol = PULSE_DET_RISING; // Update current edge
		q_item.q_data.ir_rx_data.ir_edge_te = cap_val;
		q_item.q_data.ir_rx_data.ir_edge_dir = PULSE_DET_RISING; // edge: '1' for Rising  or '0' for falling edge
	} // else

	send_to_q = 1;
	if ( subghz_record_mode_flag )
	{
#ifdef M1_APP_SUB_GHZ_RAW_DATA_RX_NOISE_FILTER_ENABLE
		if ( cap_val < M1_APP_SUB_GHZ_RAW_DATA_NOISE_PULSE_WIDTH ) // Possibly noise?
		{
			send_to_q = 0;
			// Because this bit is skipped due to the noise condition above,
			// the next bit must be skipped so that polarity of the
			// the following bits won't be inverted!
			if ( subghz_decenc_ctl.pulse_det_pol==PULSE_DET_FALLING )
				subghz_decenc_ctl.pulse_det_pol = PULSE_DET_RISING;
			else
				subghz_decenc_ctl.pulse_det_pol = PULSE_DET_FALLING;
		} // if ( cap_val < M1_APP_SUB_GHZ_RAW_DATA_NOISE_PULSE_WIDTH )
		else
#else
		if ( true )
#endif // #ifdef M1_APP_SUB_GHZ_RAW_DATA_RX_NOISE_FILTER_ENABLE
		{
			/* Track ring buffer capacity for overflow diagnostics */
			uint32_t rb_used = ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
			if (rb_used > DEBUG_subghz_rb_highwater) {
				DEBUG_subghz_rb_highwater = rb_used;
			}

			uint32_t rb_free = ringbuffer_get_empty_slots(&subghz_rx_rawdata_rb);
			if (rb_free == 0) {
				/* Ring buffer is full — insert will overwrite oldest data */
				DEBUG_subghz_rb_overflow_cnt++;
			}

			m1_ringbuffer_insert(&subghz_rx_rawdata_rb, (uint8_t *)&cap_val);
			pulse_counter++;
			if ( pulse_counter >= SUBGHZ_RAW_DATA_SAMPLES_TO_RW )
			{
				pulse_counter = 0;
			} // if ( pulse_counter >= SUBGHZ_RAW_DATA_SAMPLES_TO_RW )
			else
			{
				send_to_q = 0; // Do not flood queue for the raw data
			}
		}
	} // if ( subghz_record_mode_flag )
	if ( send_to_q )
	{
		q_item.q_evt_type = Q_EVENT_SUBGHZ_RX;
		xQueueSendFromISR(main_q_hdl, &q_item, &xHigherPriorityTaskWoken); // Send sample to queue, return: pdPASS or errQUEUE_FULL
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	} // if ( send_to_q )

} // void TIM1_CC_IRQHandler(void)
