/* See COPYING.txt for license details. */

/*
*
* m1_buzzer.h
*
* Driver and library for audio controller
*
* M1 Project
*
*/

#ifndef M1_BUZZER_H_
#define M1_BUZZER_H_

#include <stdbool.h>

#ifdef M1_APP_BUZZER_USE_TIMER3

#define BUZZER_TIMER				TIM3        /*!< Timer used for Audio encoding */
/* TIM prescaler is computed to have 1 μs as time base. TIM frequency (in MHz) / (prescaler+1) */
#define BUZZER_PRESCALER          	74           /*!< TIM prescaler */
#define BUZZER_TIMER_CLK            __HAL_RCC_TIM3_CLK_ENABLE      /*!< Clock of the used timer */
#define BUZZER_TIMER_CLK_DIS        __HAL_RCC_TIM3_CLK_DISABLE
#define BUZZER_TIMER_IRQn           TIM3_IRQn		/*!< Audio TIM IRQ */
#define BUZZER_TIMER_TX_CHANNEL   	TIM_CHANNEL_2            /*!< Audio TIM Channel */
#define BUZZER_TIMER_DEC_CH_ACTIV  	HAL_TIM_ACTIVE_CHANNEL_2

#define BUZZER_GPIO_AF_TR          	GPIO_AF2_TIM3

#elif defined M1_APP_BUZZER_USE_TIMER8

#define BUZZER_TIMER				TIM8/*!< Timer used for Audio encoding */
/* TIM prescaler is computed to have 1 μs as time base. TIM frequency (in MHz) / (prescaler+1) */
#define BUZZER_PRESCALER          	74           /*!< TIM prescaler */
#define BUZZER_TIMER_CLK            __HAL_RCC_TIM8_CLK_ENABLE      /*!< Clock of the used timer */
#define BUZZER_TIMER_CLK_DIS        __HAL_RCC_TIM8_CLK_DISABLE
#define BUZZER_TIMER_IRQn           TIM8_UP_IRQn
#define BUZZER_TIMER_TX_CHANNEL   	TIM_CHANNEL_2            /*!< Audio TIM Channel */
#define BUZZER_TIMER_DEC_CH_ACTIV  	HAL_TIM_ACTIVE_CHANNEL_2

#define BUZZER_GPIO_AF_TR          	GPIO_AF3_TIM8

#else
#error "TIMER3 or TIMER8 must be defined for buzzer control"
#endif // #ifdef M1_APP_BUZZER_USE_TIMER3

#define BUZZER_FREQ_36KHZ_PERIOD	2083	// clock tick period = 1/75MHz, carrier period = 1/36KHz = 75MHz/36KHz = 2083 clock tick periods
#define BUZZER_FREQ_005_KHZ			(uint32_t)500
#define BUZZER_FREQ_01_KHZ			(uint32_t)1000
#define BUZZER_FREQ_02_KHZ			(uint32_t)2000
#define BUZZER_FREQ_04_KHZ			(uint32_t)4000
#define BUZZER_FREQ_06_KHZ			(uint32_t)6000
#define BUZZER_FREQ_07_KHZ			(uint32_t)7000
#define BUZZER_FREQ_08_KHZ			(uint32_t)8000
#define BUZZER_FREQ_12_KHZ			(uint32_t)12000
#define BUZZER_FREQ_14_KHZ			(uint32_t)14000
#define BUZZER_FREQ_16_KHZ			(uint32_t)16000
#define BUZZER_FREQ_30_KHZ			(uint32_t)30000
#define BUZZER_FREQ_32_KHZ          (uint32_t)32000
#define BUZZER_FREQ_36_KHZ          (uint32_t)36000
#define BUZZER_FREQ_38_KHZ          (uint32_t)38000
#define BUZZER_FREQ_40_KHZ          (uint32_t)40000
#define BUZZER_FREQ_56_KHZ          (uint32_t)56000
//#define BUZZER_FREQ_455_KHZ         (uint32_t)455000

#define BUZZER_CARRIER_PRESCALE_FACTOR		10
#define BUZZER_BASEBAND_PRESCALE_FACTOR		2

typedef enum
{
	M1_BUZZER_ACCEPTED = 0,	/* tone started						*/
	M1_BUZZER_MUTED,		/* sound is muted, request dropped		*/
	M1_BUZZER_BUSY,			/* already playing / rate-limited		*/
	M1_BUZZER_INVALID,		/* bad args or unsafe calling context	*/
	M1_BUZZER_HW_FAIL,		/* timer/PWM peripheral failed to arm	*/
} m1_buzzer_result_t;

m1_buzzer_result_t m1_buzzer_set(uint16_t frequency, uint16_t duration_ms);
m1_buzzer_result_t m1_buzzer_notification(void);
m1_buzzer_result_t m1_buzzer_notification2(void);
m1_buzzer_result_t m1_buzzer_demoTest(uint8_t freqStp);
m1_buzzer_result_t m1_buzzer_cancel(void);
m1_buzzer_result_t m1_buzzer_shutdown(void);
void m1_buzzer_set_muted(bool muted);
bool m1_buzzer_is_muted(void);

/*
 * Non-blocking start/stop primitives for the M1 feedback manager's hardware
 * adapter (m1_feedback_hw_adapter.c). Unlike m1_buzzer_set() above, these do
 * not use a self-expiring software timer: the pitch is held until
 * m1_buzzer_stop_note() is called, matching MSG_SND_NOTE/MSG_SND_OFF
 * semantics (M1-FB-STD-001 v1.1, section 4.3). This is an intentional,
 * minimal driver-level addition -- the manager's "hold a note until told to
 * stop" model cannot be expressed with the pre-existing timer-scheduled
 * m1_buzzer_set() API. Only the feedback manager's hardware adapter may
 * call these; diagnostic/factory CLI code continues to use m1_buzzer_set().
 */
m1_buzzer_result_t m1_buzzer_start_note(uint16_t frequency_hz);
m1_buzzer_result_t m1_buzzer_stop_note(void);

#endif /* M1_BUZZER_H_ */
