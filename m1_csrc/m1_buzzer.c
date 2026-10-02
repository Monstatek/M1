/* See COPYING.txt for license details. */

/*
*
* m1_buzzer.c
*
* Driver and library for buzzer controller
*
* M1 Project
*
*/


/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_buzzer.h"
#include "m1_feedback_manager.h"
#include "m1_feedback_types.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

#define BUZZER_NOTIFICATION_DURATION	250 //ms

/* Minimum spacing, measured from when the previous tone stopped, before a new
 * tone is accepted. Backstops feature code that fires a beep per RF/decode
 * event without its own throttling; semantic-level deduplication belongs to
 * the feedback service (Phase 2), this is just a low-level safety net. */
#define BUZZER_MIN_REPEAT_INTERVAL_MS	50

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef    Timerhdl_Buzzer;

static bool buzzer_busy = false;
static bool buzzer_muted = false;
static TimerHandle_t buzzer_timer_hdl = NULL;
static TickType_t buzzer_last_stop_tick = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool buzzer_sys_init(uint16_t frequency);
static void buzzer_stop_hw(void);
static void buzzer_timer_expired(TimerHandle_t xTimer);
static bool buzzer_timer_ensure(void);

m1_buzzer_result_t m1_buzzer_set(uint16_t frequency, uint16_t duration);
m1_buzzer_result_t m1_buzzer_notification(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
  * @brief  Initialize the buzzer controller
  * Output modulated (PWM carrier + base band) data on SPEAKER GPIO pin
 */
/*============================================================================*/
bool buzzer_sys_init(uint16_t frequency)
{
#ifdef M1_APP_BUZZER_USE_TIMER8
	TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
#endif // #ifdef M1_APP_BUZZER_USE_TIMER8
	TIM_MasterConfigTypeDef sMasterConfig;
	TIM_OC_InitTypeDef sConfigOC;
	GPIO_InitTypeDef gpio_init_struct;
	uint32_t tim_prescaler_val;

	BUZZER_TIMER_CLK();

	Timerhdl_Buzzer.Instance = BUZZER_TIMER;

	/*Configure GPIO pin */
	gpio_init_struct.Pin = SPK_CTRL_Pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_MEDIUM;
	gpio_init_struct.Alternate = BUZZER_GPIO_AF_TR;
	HAL_GPIO_Init(SPK_CTRL_GPIO_Port, &gpio_init_struct);

	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / (frequency*BUZZER_CARRIER_PRESCALE_FACTOR)) - 1;

	Timerhdl_Buzzer.Init.Prescaler = tim_prescaler_val;
	Timerhdl_Buzzer.Init.CounterMode = TIM_COUNTERMODE_UP;
	Timerhdl_Buzzer.Init.Period = BUZZER_CARRIER_PRESCALE_FACTOR - 1;
	Timerhdl_Buzzer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	Timerhdl_Buzzer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

	if (HAL_TIM_PWM_Init(&Timerhdl_Buzzer) != HAL_OK)
		return false;

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

	if (HAL_TIMEx_MasterConfigSynchronization(&Timerhdl_Buzzer, &sMasterConfig) != HAL_OK)
		return false;

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = Timerhdl_Buzzer.Init.Period/2; /* Duty cycle = 50% */
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	//sConfigOC.OCNPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
	//sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;

	if (HAL_TIM_PWM_ConfigChannel(&Timerhdl_Buzzer, &sConfigOC, BUZZER_TIMER_TX_CHANNEL) != HAL_OK)
		return false;
	//Timerhdl_Buzzer.Instance->CCER &= ~(TIM_CCER_CCxE_MASK + TIM_CCER_CCxNE_MASK);

#ifdef M1_APP_BUZZER_USE_TIMER8
	sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
	sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
	sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
	sBreakDeadTimeConfig.DeadTime = 0;
	sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
	sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
	sBreakDeadTimeConfig.BreakFilter = 0;
	sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
	sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
	sBreakDeadTimeConfig.Break2Filter = 0;
	sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
	if (HAL_TIMEx_ConfigBreakDeadTime(&Timerhdl_Buzzer, &sBreakDeadTimeConfig) != HAL_OK)
		return false;
#endif // #ifdef M1_APP_BUZZER_USE_TIMER8

	//HAL_TIMEx_PWMN_Start(&Timerhdl_Buzzer, BUZZER_TIMER_TX_CHANNEL);
	HAL_TIM_PWM_Start(&Timerhdl_Buzzer, BUZZER_TIMER_TX_CHANNEL);

	/* TIM Disable */
	//__HAL_TIM_DISABLE(&Timerhdl_Buzzer);
	return true;
} // bool buzzer_sys_init(uint16_t frequency)



/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief  Force PWM output and SPK_CTRL to a safe idle state. Called on
  *         normal completion, explicit cancellation, and init failure, so
  *         the speaker never gets left mid-tone.
  * @retval None
  */
/*============================================================================*/
static void buzzer_stop_hw(void)
{
	GPIO_InitTypeDef gpio_init_struct;

	HAL_TIM_PWM_Stop(&Timerhdl_Buzzer, BUZZER_TIMER_TX_CHANNEL);

	BUZZER_TIMER_CLK_DIS();

	HAL_NVIC_DisableIRQ(BUZZER_TIMER_IRQn);

	gpio_init_struct.Pin = SPK_CTRL_Pin;
	gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
	gpio_init_struct.Pull = GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(SPK_CTRL_GPIO_Port, &gpio_init_struct);
	HAL_GPIO_WritePin(SPK_CTRL_GPIO_Port, SPK_CTRL_Pin, GPIO_PIN_RESET);

	//HAL_GPIO_DeInit(SPK_CTRL_GPIO_Port, SPK_CTRL_Pin);
} // static void buzzer_stop_hw(void)



/*============================================================================*/
/**
  * @brief  Timer callback: the requested duration elapsed, stop the tone.
  * @retval None
  */
/*============================================================================*/
static void buzzer_timer_expired(TimerHandle_t xTimer)
{
	(void)xTimer;
	buzzer_stop_hw();
	buzzer_last_stop_tick = xTaskGetTickCount();
	buzzer_busy = false; // unlock
} // static void buzzer_timer_expired(TimerHandle_t xTimer)



/*============================================================================*/
/**
  * @brief  Lazily create the single reusable stop-timer. Reused for every
  *         tone (xTimerChangePeriod both re-arms the duration and (re)starts
  *         it), so no timer object is ever leaked across repeated beeps.
  * @retval true if a timer handle is available
  */
/*============================================================================*/
static bool buzzer_timer_ensure(void)
{
	if ( buzzer_timer_hdl == NULL )
	{
		buzzer_timer_hdl = xTimerCreate("m1_buzzer", pdMS_TO_TICKS(BUZZER_NOTIFICATION_DURATION),
										 pdFALSE, NULL, buzzer_timer_expired);
	}
	return (buzzer_timer_hdl != NULL);
} // static bool buzzer_timer_ensure(void)



/*============================================================================*/
/**
  * @brief  Turn on the buzzer
  * @param  frequency in Hertz
  * 		duration in millisecond
  * @retval M1_BUZZER_ACCEPTED / MUTED / BUSY / INVALID / HW_FAIL
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_set(uint16_t frequency, uint16_t duration_ms)
{
	TickType_t now;

	if ( xPortIsInsideInterrupt() ) // never call from ISR or timing-critical RF context
		return M1_BUZZER_INVALID;

	if ( frequency==0 || duration_ms==0 ) // validate BEFORE claiming busy
		return M1_BUZZER_INVALID;

	if ( buzzer_muted )
		return M1_BUZZER_MUTED;

	if ( buzzer_busy )
		return M1_BUZZER_BUSY;

	now = xTaskGetTickCount();
	if ( (now - buzzer_last_stop_tick) < pdMS_TO_TICKS(BUZZER_MIN_REPEAT_INTERVAL_MS) ) // rate-limit
		return M1_BUZZER_BUSY;

	if ( !buzzer_timer_ensure() )
		return M1_BUZZER_HW_FAIL;

	buzzer_busy = true; // lock -- only after every validation has passed

	if ( !buzzer_sys_init(frequency) ) // Start buzzer
	{
		buzzer_stop_hw();
		buzzer_busy = false;
		return M1_BUZZER_HW_FAIL;
	}

	if ( xTimerChangePeriod(buzzer_timer_hdl, pdMS_TO_TICKS(duration_ms), 0) != pdPASS ) // (re)arm the stop timer
	{
		buzzer_stop_hw();
		buzzer_busy = false;
		return M1_BUZZER_HW_FAIL;
	}

	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_set(uint16_t frequency, uint16_t duration_ms)



/*============================================================================*/
/**
  * @brief  Nonblocking cancel of any tone in progress. Idempotent: calling
  *         it with nothing playing is a no-op success, not an error.
  * @retval M1_BUZZER_ACCEPTED / INVALID
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_cancel(void)
{
	if ( xPortIsInsideInterrupt() )
		return M1_BUZZER_INVALID;

	if ( !buzzer_busy )
		return M1_BUZZER_ACCEPTED;

	if ( buzzer_timer_hdl != NULL )
		xTimerStop(buzzer_timer_hdl, 0); // nonblocking

	buzzer_stop_hw();
	buzzer_last_stop_tick = xTaskGetTickCount();
	buzzer_busy = false;

	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_cancel(void)



/*============================================================================*/
/**
  * @brief  Cancel any active tone and release the reusable timer object.
  *         For device power-down / driver teardown, not per-beep use.
  * @retval M1_BUZZER_ACCEPTED / INVALID
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_shutdown(void)
{
	m1_buzzer_result_t result = m1_buzzer_cancel();

	if ( buzzer_timer_hdl != NULL )
	{
		xTimerDelete(buzzer_timer_hdl, 0);
		buzzer_timer_hdl = NULL;
	}

	return result;
} // m1_buzzer_result_t m1_buzzer_shutdown(void)



/*============================================================================*/
/**
  * @brief  Mute/unmute optional sound. Does not affect a tone already
  *         playing; takes effect on the next m1_buzzer_set() request.
  * @retval None
  */
/*============================================================================*/
void m1_buzzer_set_muted(bool muted)
{
	buzzer_muted = muted;
} // void m1_buzzer_set_muted(bool muted)

bool m1_buzzer_is_muted(void)
{
	return buzzer_muted;
} // bool m1_buzzer_is_muted(void)



/*============================================================================*/
/**
  * @brief  Play a standard notification sound.
  *
  * Narrow legacy wrapper (M1-FB-STD-001 v1.1 migration): submits the
  * feedback manager's SEQ_SND_BEEP2 request instead of driving the buzzer
  * directly, so all existing call sites automatically go through the
  * manager's arbitration/Sound-Off/volume policy without needing to be
  * edited individually. See documentation/M1_FEEDBACK_RECONCILIATION.md.
  * @retval M1_BUZZER_ACCEPTED (request submitted; manager arbitration may
  *         still suppress it, e.g. Sound Off -- there is no separate
  *         MUTED/BUSY/INVALID/HW_FAIL outcome to report through this
  *         narrowed return type any more)
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_notification(void)
{
	(void)fb_request(SEQ_SND_BEEP2, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_notification(void)


/*============================================================================*/
/**
  * @brief  Play a standard notification sound (alternate pitch).
  *
  * Narrow legacy wrapper -- see m1_buzzer_notification() above.
  * @retval M1_BUZZER_ACCEPTED (see m1_buzzer_notification() note)
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_notification2(void)
{
	(void)fb_request(SEQ_SND_BEEP, FB_OWNER_NOTIFY, FB_PRIO_APP_ALERT);
	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_notification2(void)



/*============================================================================*/
/**
  * @brief  Start (or change) the buzzer pitch and hold it until
  *         m1_buzzer_stop_note() is called. No software auto-stop timer is
  *         armed. Used exclusively by the M1 feedback manager's hardware
  *         adapter (m1_feedback_hw_adapter.c) -- see m1_buzzer.h.
  * @param  frequency_hz: buzzer pitch in Hertz. 0 stops the buzzer.
  * @retval M1_BUZZER_ACCEPTED / MUTED / INVALID / HW_FAIL
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_start_note(uint16_t frequency_hz)
{
	if ( xPortIsInsideInterrupt() )
		return M1_BUZZER_INVALID;

	if ( frequency_hz == 0u )
	{
		m1_buzzer_stop_note();
		return M1_BUZZER_ACCEPTED;
	} // if (frequency 0 means "stop")

	if ( buzzer_muted )
		return M1_BUZZER_MUTED;

	/* A note already held is simply re-pitched -- the buzzer is monophonic
	 * (reference section 8) -- rather than routed through the timed
	 * m1_buzzer_set() busy/rate-limit gate, which does not apply to the
	 * manager-driven hold-until-stopped model. */
	if ( buzzer_busy )
		buzzer_stop_hw();

	/* This is a hold-until-told-to-stop note, not a timed one: make sure no
	 * previously-armed self-stop timer (from m1_buzzer_set()) fires later
	 * and cuts this note off underneath the manager. */
	if ( buzzer_timer_hdl != NULL )
		xTimerStop(buzzer_timer_hdl, 0);

	if ( !buzzer_sys_init(frequency_hz) )
	{
		buzzer_stop_hw();
		buzzer_busy = false;
		return M1_BUZZER_HW_FAIL;
	}

	buzzer_busy = true;
	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_start_note(uint16_t frequency_hz)



/*============================================================================*/
/**
  * @brief  Stop the buzzer and drive it to its idle state. Always safe to
  *         call, including when no note is currently held (MSG_SND_OFF must
  *         always execute -- reference section 5.1).
  * @retval M1_BUZZER_ACCEPTED / INVALID
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_stop_note(void)
{
	if ( xPortIsInsideInterrupt() )
		return M1_BUZZER_INVALID;

	if ( !buzzer_busy )
		return M1_BUZZER_ACCEPTED;

	buzzer_stop_hw();
	buzzer_last_stop_tick = xTaskGetTickCount();
	buzzer_busy = false;

	return M1_BUZZER_ACCEPTED;
} // m1_buzzer_result_t m1_buzzer_stop_note(void)

/*============================================================================*/
/**
  * @brief  Play a standard notification sound
  * @param
  * @retval M1_BUZZER_ACCEPTED / MUTED / BUSY / INVALID / HW_FAIL
  */
/*============================================================================*/
m1_buzzer_result_t m1_buzzer_demoTest(uint8_t freqStp)
{
	switch (freqStp)
	{
	case 0:
		return m1_buzzer_set(BUZZER_FREQ_02_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 1:
		return m1_buzzer_set(BUZZER_FREQ_04_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 2:
		return m1_buzzer_set(BUZZER_FREQ_06_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 3:
		return m1_buzzer_set(BUZZER_FREQ_07_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 4:
		return m1_buzzer_set(BUZZER_FREQ_08_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 5:
		return m1_buzzer_set(BUZZER_FREQ_12_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 6:
		return m1_buzzer_set(BUZZER_FREQ_14_KHZ, BUZZER_NOTIFICATION_DURATION);
	case 7:
		return m1_buzzer_set(BUZZER_FREQ_16_KHZ, BUZZER_NOTIFICATION_DURATION);
	default:
		return M1_BUZZER_INVALID;
	}
} // m1_buzzer_result_t m1_buzzer_demoTest(uint8_t freqStp)
