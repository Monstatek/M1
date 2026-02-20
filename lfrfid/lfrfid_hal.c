/* See COPYING.txt for license details. */

/*
 * lfrfid_hal.c
 *
 *      Author: Thomas
 */
/*************************** I N C L U D E S **********************************/
#include <stdbool.h>

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "m1_log_debug.h"

#include "lfrfid.h"

/*************************** D E F I N E S ************************************/


/*
#define RFID_PULL_Pin 					GPIO_PIN_2
#define RFID_PULL_GPIO_Port 			GPIOA
#define RFID_RF_IN_Pin 					GPIO_PIN_3
#define RFID_RF_IN_GPIO_Port 			GPIOA
#define RFID_OUT_Pin 					GPIO_PIN_0
#define RFID_OUT_GPIO_Port 				GPIOB
#define RF_CARRIER_Pin 					GPIO_PIN_15
#define RF_CARRIER_GPIO_Port 			GPIOA
*/

#define LFRFID_RFIN_PIN_MASK   (RFID_RF_IN_Pin)

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef   Timerhdl_RfIdTIM5;
TIM_HandleTypeDef   Timerhdl_RfIdTIM3;

EncodedTx_Data_t lfrfid_encoded_data;

uint8_t rfid_rxtx_is_taking_this_irq; // Flag to use shared interrupt handler

static lfrfid_evt_t isr_batch[LFR_BATCH_ITEMS];
static uint16_t     isr_batch_index = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void LFRFID_Timebase_Init(uint32_t freq_hz, uint32_t period_us);


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function handles TIM3 global interrupt for RFID emulation
 */
/*============================================================================*/
void TIM3_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&Timerhdl_RfIdTIM3);

} // TIM3_IRQHandler


#if 1
/*============================================================================*/
/*
 * This function handles TIM5 global interrupt for RFID read
 */
/*============================================================================*/
void TIM5_IRQHandler(void)
{
  //HAL_TIM_IRQHandler(&Timerhdl_RfIdTIM5);
	uint32_t sr   = TIM5->SR;
	uint32_t dier = TIM5->DIER;
	if ((sr & TIM_SR_CC4IF) && (dier & TIM_DIER_CC4IE))
	{
      rfid_read_handler(&Timerhdl_RfIdTIM5);
      TIM5->SR = ~TIM_SR_CC4IF;
	}
	if ((sr & TIM_SR_UIF) && (dier & TIM_DIER_UIE))
	{
	  rfid_emul_handler(&Timerhdl_RfIdTIM5);
	  TIM5->SR = ~TIM_SR_UIF;
	}
} // TIM5_IRQHandler

#endif


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_isr_init(void)
{
	memset((char*)isr_batch, 0 ,sizeof(isr_batch));
	isr_batch_index = 0;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void rfid_read_handler(TIM_HandleTypeDef *htim)
{
  /*
   * TIM5 PWM input capture
   *  Channel4
   *    capure interrupt both edge
   */
    /* Get the Input Capture value */


    //uint16_t ccr = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);
	//uint16_t ccr = __HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_4);
	uint16_t ccr = htim->Instance->CCR4;
	__HAL_TIM_SET_COUNTER(htim, 0);	//htim->Instance->CNT = 0;

    if(ccr < 7 || ccr > 1000)	// filter
    	return;

    uint8_t lvl1 = (RFID_RF_IN_GPIO_Port->IDR & LFRFID_RFIN_PIN_MASK) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    //__NOP(); __NOP();
    //uint8_t lvl2 = (RFID_RF_IN_GPIO_Port->IDR & LFRFID_RFIN_PIN_MASK) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    //uint8_t level = (lvl1 == lvl2) ? lvl1 : lvl2;

    if (isr_batch_index < LFR_BATCH_ITEMS)
    {
        isr_batch[isr_batch_index].t_us    = ccr;
        isr_batch[isr_batch_index].edge    = lvl1;
        isr_batch_index++;
    }

    if (isr_batch_index >= LFR_BATCH_ITEMS)
    {
    	if(lfrfid_sb_hdl)
    	{
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;

			size_t sent = xStreamBufferSendFromISR(
					lfrfid_sb_hdl,
				(uint8_t *)isr_batch,
				LFR_TRIGGER_BYTES,
				&xHigherPriorityTaskWoken
			);

			if (sent == LFR_TRIGGER_BYTES)
			{
				isr_batch_index = 0;
			}
			else
			{
				isr_batch_index = 0;
			}

			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    	}
    	else
    		isr_batch_index = 0;
    }
} // rfid_read_handler


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void rfid_emul_handler(TIM_HandleTypeDef *htim)
{
	if ((htim == &Timerhdl_RfIdTIM5))
	{
		GPIOA->BSRR = lfrfid_encoded_data.data[lfrfid_encoded_data.index].bsrr;

		htim->Instance->ARR = lfrfid_encoded_data.data[lfrfid_encoded_data.index].time_us;
		__HAL_TIM_SET_COUNTER(htim, 0);	//htim->Instance->CNT = 0;

		lfrfid_encoded_data.index++;
		if(lfrfid_encoded_data.index >= lfrfid_encoded_data.length)
			lfrfid_encoded_data.index = 0;

	}
}


/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
void lfrfid_emul_hw_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = RFID_PULL_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RFID_PULL_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = RFID_OUT_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RFID_OUT_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RFID_OUT_GPIO_Port, RFID_OUT_Pin, GPIO_PIN_RESET);

    // 100us
    LFRFID_Timebase_Init(1000000, 100);	// 1Mhz, 100us

    HAL_TIM_Base_Start_IT(&Timerhdl_RfIdTIM5);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_emul_hw_deinit(void)
{
  //GPIO_InitTypeDef gpio_init_struct = {0};

  /* Disable the timer */
  HAL_NVIC_DisableIRQ(TIM5_IRQn);

  /* Disable the TIM Update interrupt */
  __HAL_TIM_DISABLE_IT(&Timerhdl_RfIdTIM5, TIM_IT_UPDATE);
  /* Disable the Peripheral */
  __HAL_TIM_DISABLE(&Timerhdl_RfIdTIM5);

  __HAL_RCC_TIM5_CLK_DISABLE();

  //HAL_GPIO_WritePin(GPIOA, RFID_PULL_Pin, GPIO_PIN_SET);
  //HAL_GPIO_DeInit(RF_CARRIER_GPIO_Port, RF_CARRIER_Pin);
  HAL_GPIO_WritePin(RFID_OUT_GPIO_Port, RFID_OUT_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
  rfid_rxtx_is_taking_this_irq = 0;

} // rfid_emul_deinit


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void LFRFID_Timebase_Init(uint32_t freq_hz, uint32_t period_us)
{
    __HAL_RCC_TIM5_CLK_ENABLE();

    Timerhdl_RfIdTIM5.Instance = TIM5;

    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();  // 예: 75000000 (75 MHz)

    uint32_t presc = tim_clk / freq_hz;
    if (presc == 0U) presc = 1U;

    Timerhdl_RfIdTIM5.Init.Prescaler         = (uint16_t)(presc - 1U);
    Timerhdl_RfIdTIM5.Init.CounterMode       = TIM_COUNTERMODE_UP;
    Timerhdl_RfIdTIM5.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    Timerhdl_RfIdTIM5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    Timerhdl_RfIdTIM5.Init.Period = period_us - 1U;
    if (Timerhdl_RfIdTIM5.Init.Period == 0xFFFFFFFF) Timerhdl_RfIdTIM5.Init.Period = 0xFFFE;

    if (HAL_TIM_Base_Init(&Timerhdl_RfIdTIM5) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(TIM5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY-1, 0);
    HAL_NVIC_EnableIRQ(TIM5_IRQn);
}


/*============================================================================*/
/*
  * @brief  Initialize the rfid read module
  * TIM3 CH3 PWM output mode
  * Output PWM carrier data on PB0 (TIM1 CH2N)
  *
  * @param  None
  * @retval None
 */
/*============================================================================*/
void lfrfid_RFIDOut_Init(uint32_t freq)
{
	GPIO_InitTypeDef gpio_init_struct = {0};
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_OC_InitTypeDef sConfigOC = {0};

	/* Peripheral clock enable */
	__HAL_RCC_TIM3_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/**TIM3 GPIO Configuration
	PB0     ------> TIM3_CH3
	*/
	gpio_init_struct.Pin = RFID_OUT_Pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	gpio_init_struct.Alternate = GPIO_AF2_TIM3;
	HAL_GPIO_Init(RFID_OUT_GPIO_Port, &gpio_init_struct);

	uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
	//TIM3 - CH3 PB0 PWM, 125KHz (8us)
	Timerhdl_RfIdTIM3.Instance = TIM3;
	Timerhdl_RfIdTIM3.Init.Prescaler = 0;
	Timerhdl_RfIdTIM3.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_RfIdTIM3.Init.Period = (pclk1 /(freq))-1 ; //600-1;
	Timerhdl_RfIdTIM3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_RfIdTIM3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&Timerhdl_RfIdTIM3) != HAL_OK)
	{
	  Error_Handler();
	}
	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&Timerhdl_RfIdTIM3, &sClockSourceConfig) != HAL_OK)
	{
	  Error_Handler();
	}
	if (HAL_TIM_PWM_Init(&Timerhdl_RfIdTIM3) != HAL_OK)
	{
	  Error_Handler();
	}
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&Timerhdl_RfIdTIM3, &sMasterConfig) != HAL_OK)
	{
	  Error_Handler();
	}
	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = ((pclk1 /(freq))-1)/2;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	if (HAL_TIM_PWM_ConfigChannel(&Timerhdl_RfIdTIM3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
	{
	  Error_Handler();
	}
}


/*============================================================================*/
/*
 * TIM5 PWM input capture
 *  Channel4
 *    capture interrupt both edge
 */
/*============================================================================*/
void lfrfid_RFIDIn_Init(void)
{
	GPIO_InitTypeDef gpio_init_struct = {0};
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_IC_InitTypeDef sConfigIC = {0};

	/* Peripheral clock enable */
	__HAL_RCC_TIM5_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	/**TIM5 GPIO Configuration
	PA3     ------> TIM5_CH4
	*/
	gpio_init_struct.Pin = RFID_RF_IN_Pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	gpio_init_struct.Alternate = GPIO_AF2_TIM5;
	HAL_GPIO_Init(RFID_RF_IN_GPIO_Port, &gpio_init_struct);

	uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

	Timerhdl_RfIdTIM5.Instance = TIM5;
	Timerhdl_RfIdTIM5.Init.Prescaler = ((pclk1 /(1000000))-1);  // 1us
	Timerhdl_RfIdTIM5.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_RfIdTIM5.Init.Period = 4294967295;
	Timerhdl_RfIdTIM5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_RfIdTIM5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&Timerhdl_RfIdTIM5) != HAL_OK)
	{
	  Error_Handler();
	}
	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&Timerhdl_RfIdTIM5, &sClockSourceConfig) != HAL_OK)
	{
	  Error_Handler();
	}
	if (HAL_TIM_IC_Init(&Timerhdl_RfIdTIM5) != HAL_OK)
	{
	  Error_Handler();
	}
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&Timerhdl_RfIdTIM5, &sMasterConfig) != HAL_OK)
	{
	  Error_Handler();
	}

	// input capture - channel 4, both_edge
	sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
	sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
	sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
	sConfigIC.ICFilter = 0;
	if (HAL_TIM_IC_ConfigChannel(&Timerhdl_RfIdTIM5, &sConfigIC, TIM_CHANNEL_4) != HAL_OK)
	{
	  Error_Handler();
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_read_hw_init(void)
{
  if(rfid_rxtx_is_taking_this_irq)
	return;

  // Enable EXT_5V
  HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_SET);

  lfrfid_RFIDOut_Init(125000);

  // TIM5-CH4 capture RFID_RF_IN
  lfrfid_RFIDIn_Init();

  /* TIM5 interrupt Init */
  HAL_NVIC_SetPriority(TIM5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1, 0);   // Default int priority: configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
  HAL_NVIC_EnableIRQ(TIM5_IRQn);

#if 1
  HAL_TIM_PWM_Start(&Timerhdl_RfIdTIM3, TIM_CHANNEL_3);
  HAL_TIM_IC_Start_IT(&Timerhdl_RfIdTIM5, TIM_CHANNEL_4);
#endif
  //lfrfid_stream_init();
  rfid_rxtx_is_taking_this_irq = 1;
} // void rfid_read_init(void)


/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
void lfrfid_read_hw_deinit(void)
{
	if(rfid_rxtx_is_taking_this_irq == 0)
		return;

#if 1
	GPIO_InitTypeDef gpio_init_struct = {0};

#if 1
  //HAL_TIMEx_PWMN_Stop(&Timerhdl_RfIdTIM3, TIM_CHANNEL_2);

  HAL_TIM_IC_Stop_IT(&Timerhdl_RfIdTIM5, TIM_CHANNEL_4);

  HAL_TIM_PWM_Stop(&Timerhdl_RfIdTIM3, TIM_CHANNEL_3);
  HAL_TIM_PWM_DeInit(&Timerhdl_RfIdTIM5);
#endif

  /* Disable the timer */
  __HAL_TIM_DISABLE(&Timerhdl_RfIdTIM3);
  __HAL_TIM_DISABLE(&Timerhdl_RfIdTIM5);

//   Disable EXT_5V
//  HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_RESET);

  /**TIM2 GPIO Configuration
  */
  HAL_GPIO_DeInit(RFID_OUT_GPIO_Port, RFID_OUT_Pin);
  HAL_GPIO_DeInit(RFID_RF_IN_GPIO_Port, RFID_RF_IN_Pin);

  gpio_init_struct.Pin = RFID_OUT_Pin | RFID_RF_IN_Pin;
  gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init_struct.Pull = GPIO_NOPULL;
  gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RFID_OUT_GPIO_Port, &gpio_init_struct);

  HAL_GPIO_WritePin(RFID_OUT_GPIO_Port, RFID_OUT_Pin, GPIO_PIN_RESET);

  HAL_NVIC_DisableIRQ(TIM5_IRQn);

#endif
  /* Peripheral clock disable */
  __HAL_RCC_TIM3_CLK_DISABLE();
  __HAL_RCC_TIM5_CLK_DISABLE();

  //lfrfid_stream_deinit();
  rfid_rxtx_is_taking_this_irq = 0; // reset
} // void rfid_read_deinit(void)

