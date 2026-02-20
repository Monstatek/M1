/* See COPYING.txt for license details. */

/*
 * T5577 LF RFID implementation
 *
 * This file is derived from the Flipper Zero firmware project.
 * The original implementation has been modified to support
 * Monstatek hardware by adapting the hardware abstraction layer.
 *
 * Original project:
 * https://github.com/flipperdevices/flipperzero-firmware
 *
 * Copyright (C) Flipper Devices Inc.
 *
 * Licensed under the GNU General Public License v3.0 (GPLv3).
 *
 * Modifications:
 *   - Hardware interface adaptation for Monstatek platform
 *   - Integration into Monstatek firmware framework
 *
 * Copyright (C) 2026 Monstatek
 */
/*************************** I N C L U D E S **********************************/
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"

#include "lfrfid.h"

/*************************** D E F I N E S ************************************/

#define T5577_TIMING_WAIT_TIME 400
#define T5577_TIMING_START_GAP 30
#define T5577_TIMING_WRITE_GAP 18
#define T5577_TIMING_DATA_0    24
#define T5577_TIMING_DATA_1    56
#define T5577_TIMING_PROGRAM   700

#define T5577_OPCODE_PAGE_0 0b10
#define T5577_OPCODE_PAGE_1 0b11
#define T5577_OPCODE_RESET  0b00

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef htim5;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void t5577_delay_init(void);
static void t5577_delay_us(uint32_t us);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
//HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);  // ON (CC3E set)
//HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_3);   // OFF (CC3E clear)
static void t5577_write_start(void)
{

	t5577_delay_init();

	HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
	t5577_delay_us(28);
	HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_SET);

	lfrfid_RFIDOut_Init(125000);

	HAL_TIM_PWM_Start(&Timerhdl_RfIdTIM3, TIM_CHANNEL_3);

}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_stop(void)
{
	HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_delay_init(void)
{
    __HAL_RCC_TIM5_CLK_ENABLE();

    htim5.Instance = TIM5;
    htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim5.Init.Period = 0xFFFFFFFF;   // 32-bit max
    htim5.Init.Prescaler = (SystemCoreClock / 1000000) - 1;
    htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    HAL_TIM_Base_Init(&htim5);
    HAL_TIM_Base_Start(&htim5);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_delay_us(uint32_t us)
{
    uint32_t start = TIM5->CNT;
    while ((uint32_t)(TIM5->CNT - start) < us)
    {
        __NOP();
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_gap(uint32_t gap_time)
{
	  HAL_TIM_PWM_Stop(&Timerhdl_RfIdTIM3, TIM_CHANNEL_3);
    t5577_delay_us(gap_time * 8);
    HAL_TIM_PWM_Start(&Timerhdl_RfIdTIM3, TIM_CHANNEL_3);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_bit(bool value)
{
    if(value) {
    	t5577_delay_us(T5577_TIMING_DATA_1 * 8);
    } else {
    	t5577_delay_us(T5577_TIMING_DATA_0 * 8);
    }
    t5577_write_gap(T5577_TIMING_WRITE_GAP);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_opcode(uint8_t value)
{
    t5577_write_bit((value >> 1) & 1);
    t5577_write_bit((value >> 0) & 1);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_reset(void)
{
    t5577_write_gap(T5577_TIMING_START_GAP);
    t5577_write_bit(1);
    t5577_write_bit(0);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_block_data(
    uint8_t page,
    uint8_t block,
    bool lock_bit,
    uint32_t data,
    bool with_pass,
    uint32_t password)
{
	t5577_delay_us(T5577_TIMING_WAIT_TIME * 8);

    // start gap
    t5577_write_gap(T5577_TIMING_START_GAP);

    // opcode for page
    t5577_write_opcode((page == 1) ? T5577_OPCODE_PAGE_1 : T5577_OPCODE_PAGE_0);

    // password
    if(with_pass) {
        for(uint8_t i = 0; i < 32; i++) {
            t5577_write_bit((password >> (31 - i)) & 1);
        }
    }

    // lock bit
    t5577_write_bit(lock_bit);

    // data
    for(uint8_t i = 0; i < 32; i++) {
        t5577_write_bit((data >> (31 - i)) & 1);
    }

    // block address
    t5577_write_bit((block >> 2) & 1);
    t5577_write_bit((block >> 1) & 1);
    t5577_write_bit((block >> 0) & 1);

    t5577_delay_us(T5577_TIMING_PROGRAM * 8);

    t5577_delay_us(T5577_TIMING_WAIT_TIME * 8);
    t5577_write_reset();
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void t5577_write_block(uint8_t block, bool lock_bit, uint32_t data)
{
    t5577_write_block_data(0, block, lock_bit, data, false, 0);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void t5577_execute_write(LFRFIDProgram* write, int block)
{
	t5577_write_start();
    taskENTER_CRITICAL();
    for(size_t i = 0; i < write->t5577.max_blocks; i++) {
    	t5577_write_block(i, false, write->t5577.block_data[i]);
    }
    t5577_write_reset();

    t5577_delay_us(1600);
    t5577_write_gap(27);

    taskEXIT_CRITICAL();
    t5577_write_stop();

}
