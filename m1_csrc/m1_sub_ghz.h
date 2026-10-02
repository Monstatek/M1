/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz.h
*
*  M1 sub-ghz functions
*
* M1 Project
*
*
*/
/*!
 *
 * Silicon Laboratories Confidential
 * Copyright 2011 Silicon Laboratories, Inc.
 */

#ifndef M1_SUB_GHZ_H_
#define M1_SUB_GHZ_H_

#include "m1_io_defs.h"
#include "m1_ring_buffer.h"

#define SUBGHZ_RX_TIMER                 TIM1        /*!< Timer used for Sub-GHz decoding */
/* TIM prescaler is computed to have 1 μs as time base. TIM frequency (in MHz) / (prescaler+1) */
#define SUBGHZ_RX_TIM_PRESCALER         74           /*!< TIM prescaler */
#define SUBGHZ_RX_TIMER_CLK             __HAL_RCC_TIM1_CLK_ENABLE      /*!< Clock of the used timer */
#define SUBGHZ_RX_TIMER_CLK_DIS         __HAL_RCC_TIM1_CLK_DISABLE
#define SUBGHZ_RX_TIMER_IRQn            TIM1_CC_IRQn             /*!< IR TIM IRQ */
#define SUBGHZ_RX_TIMER_RX_CHANNEL   	TIM_CHANNEL_1            /*!< IR TIM Channel */
#define SUBGHZ_RX_TIMER_CH_ACTIV  		HAL_TIM_ACTIVE_CHANNEL_1

#define SUBGHZ_TX_CARRIER_TIMER         TIM1        /*!< Timer used for IR encoding */
#define SUBGHZ_TX_TIM_PRESCALER         74           /*!< TIM prescaler */
#define SUBGHZ_TX_TIMER_CLK     		__HAL_RCC_TIM1_CLK_ENABLE      /*!< Clock of the used timer */
#define SUBGHZ_TX_TIMER_CLK_DIS 		__HAL_RCC_TIM1_CLK_DISABLE
#define SUBGHZ_TX_TIMER_TX_CHANNEL   	TIM_CHANNEL_4 //CH4N
#define SUBGHZ_TX_TIMER_ENC_CH_ACTIV  	HAL_TIM_ACTIVE_CHANNEL_4
#define SUBGHZ_TX_TIMER_IRQn            TIM1_UP_IRQn

#define SUBGHZ_TX_CARRIER_FREQ_36KHZ_PERIOD		2083	// clock tick period = 1/75MHz, carrier period = 1/36KHz = 75MHz/36KHz = 2083 clock tick periods
#define SUBGHZ_TX_CARRIER_FREQ_30_KHZ			(uint32_t)30000
#define SUBGHZ_TX_CARRIER_FREQ_32_KHZ           (uint32_t)32000
#define SUBGHZ_TX_CARRIER_FREQ_36_KHZ           (uint32_t)36000
#define SUBGHZ_TX_CARRIER_FREQ_38_KHZ           (uint32_t)38000
#define SUBGHZ_TX_CARRIER_FREQ_40_KHZ           (uint32_t)40000
#define SUBGHZ_TX_CARRIER_FREQ_56_KHZ           (uint32_t)56000
#define SUBGHZ_TX_CARRIER_FREQ_455_KHZ          (uint32_t)455000

#define SUBGHZ_TX_CARRIER_PRESCALE_FACTOR		10

#define SUBGHZ_RAW_DATA_SAMPLES_TO_RW			512
#define SUBGHZ_FORTMATTED_DATA_SAMPLES_TO_RW    3840//5120//2560//3072//3840

#define SUBGHZ_MODULATION_LIST					3

// Defines for ISM bands regions
#define SUBGHZ_ISM_BAND_REGIONS_LIST			3

#define SUBGHZ_ISM_BAND_REGION_NORTH_AMERICA	0
#define SUBGHZ_ISM_BAND_REGION_EUROPE_REGION_1	1
#define SUBGHZ_ISM_BAND_REGION_ASIA				2

#define SUBGHZ_ISM_BAND_REGION					SUBGHZ_ISM_BAND_REGION_NORTH_AMERICA//SUBGHZ_ISM_BAND_REGION_ASIA//SUBGHZ_ISM_BAND_REGION_NORTH_AMERICA
// End - Defines for ISM bands regions

// SUBGHZ_GPIO_0(RX)	PORTE.9	<--> TIM1_CH1
// SUBGHZ_GPIO_2(TX) 	PORTD.5	<--> TIM1_CH4N
#define SUBGHZ_RX_GPIO_PORT         SI4463_GPIO0_GPIO_Port
#define SUBGHZ_RX_GPIO_PIN         	SI4463_GPIO0_Pin
#define SUBGHZ_RX_GPIO_PORT_CLK		__HAL_RCC_GPIOE_CLK_ENABLE

#define SUBGHZ_TX_GPIO_PORT         SI4463_GPIO2_GPIO_Port
#define SUBGHZ_TX_GPIO_PIN         	SI4463_GPIO2_Pin
#define SUBGHZ_TX_GPIO_PORT_CLK		__HAL_RCC_GPIOD_CLK_ENABLE

#define SUBGHZ_GPIO_AF_TX          	GPIO_AF1_TIM1
#define SUBGHZ_GPIO_AF_RX         	GPIO_AF1_TIM1
/* Menu option: SUBGHz Tx mapped to an external GPIO - TIM8_CH4N */
#define SUBGHZ_TX_CARRIER_TIMER_REMAP		TIM8
#define SUBGHZ_TX_TIMER_CLK_REMAP    		__HAL_RCC_TIM8_CLK_ENABLE
#define SUBGHZ_TX_TIMER_CLK_DIS_REMAP		__HAL_RCC_TIM8_CLK_DISABLE
#define SUBGHZ_TX_TIMER_TX_CHANNEL_REMAP	TIM_CHANNEL_4
#define SUBGHZ_TX_GPIO_PORT_REMAP        	PD0_GPIO_Port // GPIOD
#define SUBGHZ_TX_GPIO_PIN_REMAP      		PD0_Pin // GPIO_PIN_0
#define SUBGHZ_TX_GPIO_PORT_CLK_REMAP		__HAL_RCC_GPIOD_CLK_ENABLE
#define SUBGHZ_GPIO_AF_TX_REMAP          	GPIO_AF3_TIM8
//#define SUBGHZ_TX_TIMER_TX_CHANNEL_REMAP   	TIM_CHANNEL_4 //CH4N - Same channel
#define SUBGHZ_TX_TIMER_IRQn_REMAP        	TIM8_UP_IRQn

#define TIM_FORCED_ACTIVE      		((uint16_t)0x0050)
#define TIM_FORCED_INACTIVE    		((uint16_t)0x0040)

#define SUBGHZ_OTA_PULSE_BIT_MASK	0x0001 // LSB bit = 1 for Mark, using OR operator
#define SUBGHZ_OTA_SPACE_BIT_MASK	0xFFFE // LSB bit = 0 for Space, using AND operator

#define SUBGHZ_RX_TIMEOUT_TIME      20000
#define SUBGHZ_TX_TIMEOUT_TIME      20000

// RF_Input_Level_dBm = (RSSI_value / 2) – MODEM_RSSI_COMP – 70
#define MODEM_RSSI_COMP 	0x40 // = 64d is appropriate for most applications.

typedef enum
{
	SUB_GHZ_BAND_300 = 0,
	SUB_GHZ_BAND_310,
	SUB_GHZ_BAND_315,
	SUB_GHZ_BAND_345,
	SUB_GHZ_BAND_372,
	SUB_GHZ_BAND_390,
	SUB_GHZ_BAND_433,
	SUB_GHZ_BAND_433_92,
	SUB_GHZ_BAND_915,
	SUB_GHZ_BAND_EOL
} S_M1_SubGHz_Band;

typedef enum
{
	SUB_GHZ_OPMODE_ISOLATED = 0,
	SUB_GHZ_OPMODE_RX,
	SUB_GHZ_OPMODE_TX,
	SUB_GHZ_OPMODE_EOL
} S_M1_SubGHz_OpMode;

#define SUB_GHZ_OPMODE_TX_CW	SUB_GHZ_OPMODE_TX

typedef enum {
	PULSE_DET_NORMAL = 0,
	PULSE_DET_IDLE,
	PULSE_DET_ACTIVE, // Ready and waiting for a valid frame gap to continue
	PULSE_DET_EOP,	// End-of-packet condition met
	PULSE_DET_RISING,
	PULSE_DET_FALLING,
	PULSE_DET_UNKNOWN
} S_M1_SubGHz_Pulse_Det;

typedef enum {
	MODULATION_OOK = 0,
	MODULATION_ASK,
	MODULATION_FSK,
	MODULATION_UNKNOWN
} S_M1_SubGHz_Modulation;

typedef struct
{
	uint8_t channel;
	S_M1_SubGHz_Band band;
	S_M1_SubGHz_Modulation modulation;
} S_M1_SubGHz_Scan_Config;

void menu_sub_ghz_init(void);
void menu_sub_ghz_exit(void);

void sub_ghz_init(void);
void sub_ghz_scan(void);
void sub_ghz_record(void);
void sub_ghz_replay(void);
void sub_ghz_frequency_reader(void);
void sub_ghz_gpio_remap(void);

extern EXTI_HandleTypeDef 	si4463_exti_hdl;
extern TIM_HandleTypeDef   	timerhdl_subghz_tx;
extern TIM_HandleTypeDef   	timerhdl_subghz_rx;
extern DMA_HandleTypeDef	hdma_subghz_tx;
extern volatile uint8_t subghz_tx_tc_flag;
extern S_M1_RingBuffer subghz_rx_rawdata_rb;
extern uint8_t subghz_record_mode_flag;
/* ISR latches once; task clears only with capture stopped. */
extern volatile uint32_t subghz_capture_overflow;
extern uint8_t subghz_scan_mode_flag;
extern GPIO_TypeDef *subghz_tx_remap_port;
extern uint16_t subghz_tx_remap_pin;
#endif /* M1_SUB_GHZ_H_ */
