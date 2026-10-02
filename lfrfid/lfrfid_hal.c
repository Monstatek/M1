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
#include "lfrfid_dma_tx.h"

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

/*
 * Per-acquisition-pass edge filter ceiling (see lfrfid_hal.h for the two
 * named values). Defaults to the original ASK ceiling; lfrfid.c calls
 * lfrfid_hal_set_edge_max_us() when switching acquisition pass. Plain
 * volatile read/write (ISR reads, task context writes) -- same
 * simple-flag convention already used for lfrfid_pettag_mode; a torn read
 * during the brief switch window just means one edge is judged against
 * the outgoing pass's ceiling instead of the incoming one, which is
 * harmless (decoders are reset immediately after every pass switch
 * anyway).
 *
 * LFRFID_HAL_PSK_EDGE_MAX_US=28000 derivation: PSK1 protocols (Keri,
 * NexWatch) differentially encode at 255us/bit, so long runs of
 * identical-valued bits merge into one long edge, and both protocols
 * transmit their frame continuously/cyclically so a run can span a
 * repetition boundary. Worst case allowing any structurally valid
 * (can-be-decoded-accepting) payload, computed programmatically:
 *   Keri     (64-bit frame):  35 bits x 255us =  8925us
 *   NexWatch (96-bit frame):  90 bits x 255us = 22950us
 * NexWatch dominates at 22950us; +20% documented tolerance (matching the
 * existing PERIOD_TOL_PCT convention in lfrfid_protocol_h10301.c) gives
 * 27540us, rounded to 28000us. Fits uint16_t (max 65535) with wide
 * margin. Edges this long during the ASK pass are NOT affected -- the ASK
 * pass always uses LFRFID_HAL_ASK_EDGE_MAX_US=1000, unchanged from
 * before. Genuinely invalid/silence gaps longer than a protocol's own
 * frame size are already safely ignored (not expanded into repeated bit
 * pushes) by each PSK decoder's own bit_count>=ENCODED_BITS guard in
 * *_feed_internal() -- e.g. a 28000us gap is ~110 bit-periods, which
 * exceeds both Keri's 64-bit and NexWatch's 96-bit frame size, so
 * feed_internal() returns false without touching the shift-register
 * buffer at all.
 */
static volatile uint16_t g_lfrfid_edge_max_us = LFRFID_HAL_ASK_EDGE_MAX_US;

void lfrfid_hal_set_edge_max_us(uint16_t max_us)
{
	g_lfrfid_edge_max_us = max_us;
}

/********************* F U N C T I O N   P R O T O T Y P E S ******************/


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
  /* LF READ (capture) only. LF EMULATE no longer has a CPU-serviced path at
   * all -- see lfrfid_dma_tx.c: TIM5's Update event drives two GPDMA2
   * linked-list channels directly (GPIOA->BSRR, TIM5->ARR), with UIE never
   * enabled during emulation, so this handler is never entered for it. */
	uint32_t sr   = TIM5->SR;
	uint32_t dier = TIM5->DIER;
	if ((sr & TIM_SR_CC4IF) && (dier & TIM_DIER_CC4IE))
	{
      rfid_read_handler(&Timerhdl_RfIdTIM5);
      TIM5->SR = ~TIM_SR_CC4IF;
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

    if(ccr < 7 || ccr > g_lfrfid_edge_max_us)	// filter
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
  * @brief  Starts continuous, DMA-driven LF emulation of the completed
  *         waveform in lfrfid_encoded_data (see lfrfid_dma_tx.c). Replaces
  *         the previous per-edge ISR transmitter entirely -- there is no
  *         fallback path; a failed start leaves nothing armed.
  * @param  None
  * @retval None
  */
/*============================================================================*/
void lfrfid_emul_hw_init(void)
{
    rfid_rxtx_is_taking_this_irq = lfrfid_dma_tx_start(&lfrfid_encoded_data) ? 1 : 0;
}


/*============================================================================*/
/**
  * @brief  Stops and fully releases DMA-driven LF emulation (see
  *         lfrfid_dma_tx.c). Idempotent: safe to call when nothing is
  *         running, including after a failed lfrfid_emul_hw_init().
  * @param  None
  * @retval None
  */
/*============================================================================*/
void lfrfid_emul_hw_deinit(void)
{
  lfrfid_dma_tx_stop();
  rfid_rxtx_is_taking_this_irq = 0;
} // lfrfid_emul_hw_deinit


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
/**
 * @brief  Explicit-duty exciter carrier init, for acquisition passes that
 *         are not 50% duty (currently: the PSK pass, 62.5kHz/25%).
 *         lfrfid_RFIDOut_Init() above is left completely untouched so
 *         every existing 50%-duty caller (normal ASK Read, Pet Tag) is
 *         bit-for-bit unchanged; this is an independent function, not a
 *         refactor of it.
 * @param  freq: exciter frequency in Hz.
 * @param  duty_pct: PWM high-time percentage, clamped to [1,99] -- 0 or
 *         100 would degenerate the waveform to a constant level.
 */
/*============================================================================*/
void lfrfid_RFIDOut_Init_ex(uint32_t freq, uint8_t duty_pct)
{
	GPIO_InitTypeDef gpio_init_struct = {0};
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_OC_InitTypeDef sConfigOC = {0};

	if (duty_pct < 1U)  duty_pct = 1U;
	if (duty_pct > 99U) duty_pct = 99U;

	__HAL_RCC_TIM3_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	gpio_init_struct.Pin = RFID_OUT_Pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	gpio_init_struct.Alternate = GPIO_AF2_TIM3;
	HAL_GPIO_Init(RFID_OUT_GPIO_Port, &gpio_init_struct);

	uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
	uint32_t period = (pclk1 / freq) - 1U;  /* same derivation as lfrfid_RFIDOut_Init */

	Timerhdl_RfIdTIM3.Instance = TIM3;
	Timerhdl_RfIdTIM3.Init.Prescaler = 0;
	Timerhdl_RfIdTIM3.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_RfIdTIM3.Init.Period = period;
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

	/* CCR3 (Pulse) sets the LOW-time tick count, not the high-time tick
	 * count: in PWM1 mode the internal OC1REF is "active" while
	 * CNT<CCR, and OCPolarity below is LOW, meaning "active" maps to a
	 * LOW pin level. So CNT<CCR -> PB0 LOW, CNT in [CCR, period] -> PB0
	 * HIGH. duty_pct is the caller's intended PB0 HIGH-time percentage
	 * (see lfrfid.c's PSK acquisition-mode comment), so
	 * CCR must be loaded with the complementary LOW-time tick count,
	 * not duty_pct's own tick count directly. Previously this used
	 * duty_pct's tick count directly, which produced a HIGH-time of
	 * (100-duty_pct)% instead of duty_pct% -- invisible at the
	 * previously-only-caller's 50% duty (self-complementary), but wrong
	 * for the PSK pass's 25% duty (produced 75% HIGH-time, not 25%).
	 * uint64_t intermediate: cheap, explicit overflow safety net (period
	 * is ~1199 at 62.5kHz/75MHz pclk1, nowhere near overflow, but
	 * validated rather than assumed). Clamped strictly inside (0,
	 * period) so no duty_pct/period combination can ever degenerate the
	 * waveform to a constant level regardless of rounding. */
	uint64_t period_ticks = (uint64_t)period + 1U;
	uint64_t high_ticks64 = (period_ticks * duty_pct + 50U) / 100U;
	uint64_t pulse64      = period_ticks - high_ticks64;
	uint32_t pulse = (uint32_t)pulse64;
	if (pulse == 0U)      pulse = 1U;
	if (pulse >= period)  pulse = period - 1U;

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = pulse;
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
void lfrfid_read_hw_init(uint32_t carrier_hz, uint8_t duty_pct)
{
  if(rfid_rxtx_is_taking_this_irq)
	return;

  // Enable EXT_5V
  HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_SET);

  /* 50% duty routes to the original, untouched lfrfid_RFIDOut_Init (normal
   * ASK-pass Read at 125kHz, or Pet Tag's 134.2kHz FDX-B carrier -- on a
   * 75 MHz timer clock the divider is 75e6/134200 = 558, giving an actual
   * carrier of 75e6/558 = 134408.6 Hz (+0.155%), within FDX-B tolerance).
   * Any other duty (currently: the PSK pass, 62.5kHz/25%) routes to the
   * explicit-duty variant. Caller (lfrfid.c) decides carrier_hz/duty_pct
   * from pettag_mode / the active acquisition pass. */
  if (duty_pct == 50U)
    lfrfid_RFIDOut_Init(carrier_hz);
  else
    lfrfid_RFIDOut_Init_ex(carrier_hz, duty_pct);

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
