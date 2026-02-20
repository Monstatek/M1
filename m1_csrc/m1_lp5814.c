/* See COPYING.txt for license details. */

/*
 * LP5814 RGB LED Driver
 *
 * Partially developed with reference to:
 * https://github.com/rickkas7/LP5562-RK
 *
 * Original project licensed under the MIT License.
 *
 * Adapted and modified for LP5814 hardware by Monstatek.
 *
 * Copyright (C) 2026 Monstatek
 */


/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_types_def.h"
#include "m1_lp5814.h"
#include "m1_i2c.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_M1_lp5814 m1_lp5814_ctl;

static S_M1_I2C_Trans_Inf i2c_inf;

void (*blink_timer_cb_func)(void) = NULL;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void lp5814_withLEDCurrent_all(float all);
void lp5814_withLEDCurrent(float red, float green, float blue, float white);

bool lp5814_begin(void);

uint8_t lp5814_getEnable(void);
uint8_t lp5814_currentToPercent(float value, bool max51mA);
float 	lp5814_get_time(int n);
uint8_t	lp5814_set_time(float f);

void lp5814_led_on(uint8_t port, uint8_t value);
void lp5814_led_on_rgb(uint8_t led_rgb, uint8_t value);
void lp5814_led_off(uint8_t port);

void lp5814_init(void);
void lp5814_shutdown(void);
void lp5814_backlight_on(uint8_t brightness);
void lp5814_all_off_RGB(void);
void lp5814_led_on_Red(uint8_t pwm);
void lp5814_led_on_Green(uint8_t pwm);
void lp5814_led_on_Blue(uint8_t pwm);
void lp5814_fastblink_on_RGB(uint8_t pwm_rgb, uint16_t on_off_ms);
void lp5814_fastblink_on_R_G_B(uint8_t r_g_b, uint8_t pwm_rgb, uint16_t on_off_ms);
void lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode, void (*callback_fn)());
static void lp5814_stop_blink_timer(TimerHandle_t xTimer);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief index to time
 *
 * @param n 0 ~ 15
 *
 * @return A float value in sec
 *
 * 0 ~ 10 ( n * 0.05 sec), 11(1sec),12 ~ 15 (n-11) * 2sec
 */
/*============================================================================*/
float lp5814_get_time(int n)
{
	if (n <= 10)
		return (n * 0.05f);
	else if (n == 11)
		return 1.0f;
	else if (n < 16)
		return (n - 11) * 2.0f;
	else
		return 0.0f;
}


/*============================================================================*/
/**
 * @brief time to index
 *
 * @param n 0 ~ 8sec
 *
 * @return A uint8_t value in tenths of a mA. For example, passing 5 (mA) will return 50.
 *
 * 0.05 ~ 0.5sec (f / 0.05 sec), 1sec(11), 2 ~ 8sec (f /
 */
/*============================================================================*/
uint8_t	lp5814_set_time(float f)
{
	uint8_t ret = 0;

	if (f <= 0.5f)
		ret = (uint8_t)(f / 0.05f);
	else if (f < 1.0f)
		ret = 10;
	else if (f < 2.0f)
		ret = 11;
	else if (f <= 8.0f) {
		ret = (uint8_t)(f / 2.0f);
		ret += 11;
	}
	return ret;
}


/*============================================================================*/
/**
 * @brief Low-level call to read a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 */
/*============================================================================*/
uint8_t lp5814_readRegister(uint8_t reg)
{
	i2c_inf.dev_id = I2C_DEVICE_LP5814;
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = reg;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
}


/*============================================================================*/
/**
 * @brief Low-level call to write a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 *
 * @param value The value to set
 *
 * Note that setProgram bypasses this to write multiple bytes at once, to improve efficiency.
 */
/*============================================================================*/
bool lp5814_writeRegister(uint8_t reg, uint8_t value)
{
	HAL_StatusTypeDef	stat;

	i2c_inf.dev_id = I2C_DEVICE_LP5814;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = reg;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
}


/*============================================================================*/
/**
 * @brief Sets the LED current
 *
 * @param all The current for all LEDs (R, G, B, and W). You can set specific different currents with
 * the other overload. The default is 5 mA. The range of from 0.1 to 25.5 mA in increments of 0.1 mA.
 *
 * This method returns a lp5814 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5814_withLEDCurrent_all(float all)
{
	lp5814_withLEDCurrent(all, all, all, all);
} // void lp5814_withLEDCurrent_all(float all)



/*============================================================================*/
/**
 * @brief Sets the LED current
 *
 * @param red The current for the red LED. The default is 5 mA. The range of from 0.1 to 25.5 mA in
 * increments of 0.1 mA.
 *
 * @param green The current for the green LED. The default is 5 mA. The range of from 0.1 to 25.5 mA in
 * increments of 0.1 mA.
 *
 * @param blue The current for the blue LED. The default is 5 mA. The range of from 0.1 to 25.5 mA in
 * increments of 0.1 mA.
 *
 * @param white The current for the white LED. The default is 5 mA. The range of from 0.1 to 25.5 mA in
 * increments of 0.1 mA.
 *
 * This method returns a lp5814 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5814_withLEDCurrent(float red, float green, float blue, float white)
{
	uint8_t	max_current_51mA = 0;

	max_current_51mA = lp5814_readRegister(LP5814_REG_DEV_CONFIG0) & 0x01;
	max_current_51mA = 1;

	m1_lp5814_ctl.redCurrent = lp5814_currentToPercent(red, max_current_51mA);
	m1_lp5814_ctl.greenCurrent = lp5814_currentToPercent(green, max_current_51mA);
	m1_lp5814_ctl.blueCurrent = lp5814_currentToPercent(blue, max_current_51mA);
	m1_lp5814_ctl.whiteCurrent = lp5814_currentToPercent(white, max_current_51mA);
} // void lp5814_withLEDCurrent(float red, float green, float blue, float white)



/*============================================================================*/
/**
 * @brief Get the value of the enable register (0x00)
 */
/*============================================================================*/
uint8_t lp5814_getEnable(void)
{
	return (lp5814_readRegister(LP5814_REG_ENABLE) & LP5814_ENABLE_CHIP_EN);
} // uint8_t lp5814_getEnable(void)


/*============================================================================*/
/**
 * @brief Convert a current value in mA to the format used by the lp5814
 *
 * @param value Value in mA
 *
 * @return A uint8_t value in DC(0 ~ 255) depend MAX_CURRENT. For example, passing 5 (mA) (MAX 51mA) will return 25(10%)
 */
/*============================================================================*/
uint8_t lp5814_currentToPercent(float value, bool max51mA)
{
	uint16_t result = 0;
	if (max51mA)
		result = (value * 5.0f); // 0.1 mA = 0, 0.2 mA = 1, ..., 51 mA = 255
	else
		result = (value * 10.0f); // 0.1 mA = 1, 0.2 mA = 2, ..., 25.5 mA = 255
	return (uint8_t)(result > 255 ? 255 : result); // Limit to 255
} // uint8_t lp5814_currentToPercent(float value)


/*============================================================================*/
/**
 * @brief Set up the I2C device and begin running.
 *
 * Make sure you set the LED current using withLEDCurrent() before calling begin if your LED has a
 * current other than the default of 5 mA.
 */
/*============================================================================*/
bool lp5814_begin(void)
{
	if (!lp5814_writeRegister(LP5814_RESET_CMD, LP5814_RESET))
		return false;

	// Set MAX_CURRENT, 1 (51mA), 0 (25.5mA)
	if (!lp5814_writeRegister(LP5814_REG_DEV_CONFIG0, LP5814_DEV_CONFIG0_51mA))
		return false;

	// Set Default Current, 5mA (2.5mA)
	if (!lp5814_writeRegister(LP5814_OUT_DC(LED_R), m1_lp5814_ctl.redCurrent))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_DC(LED_G), m1_lp5814_ctl.greenCurrent))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_DC(LED_B), m1_lp5814_ctl.blueCurrent))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_DC(LED_W), m1_lp5814_ctl.whiteCurrent))
		return false;

	// Set the default PWM levels to 0 % initially
	if (!lp5814_writeRegister(LP5814_OUT_PWM(LED_R), 0))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_PWM(LED_G), 0))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_PWM(LED_B), 0))
		return false;
	if (!lp5814_writeRegister(LP5814_OUT_PWM(LED_W), 0))
		return false;

	// Enable the chip
	if (!lp5814_writeRegister(LP5814_REG_ENABLE, LP5814_ENABLE_CHIP_EN))
		return false;

	// Hardware start-up delay 500us
	HAL_Delay(1);
	//vTaskDelay(pdMS_TO_TICKS(1));

	return true;
} // bool lp5814_begin(void)



/*============================================================================*/
/**
 * @brief Initialize the lp5814 and set a default LED indicator
 */
/*============================================================================*/
void lp5814_init(void)
{
	// LED   Color Name   Actual Color   Current
	// 1     Red          Red            20mA
	// 2     Green        Green          20mA
	// 3     Blue         Yellow         20mA
	// 4     White        Red            10mA
	m1_lp5814_ctl.addr = lp5814_I2C_ADD;
	m1_lp5814_ctl.redCurrent 	= 25; // <=> 2.5mA or 5mA
	m1_lp5814_ctl.greenCurrent 	= 25; // <=> 2.5mA or 5mA
	m1_lp5814_ctl.blueCurrent 	= 25; // <=> 2.5mA or 5mA
	m1_lp5814_ctl.whiteCurrent 	= 25; // <=> 2.5mA or 5mA

	lp5814_withLEDCurrent(5.0, 3.0, 3.0, 20.0); //all current
	lp5814_begin(); // Set up the I2C device and begin running.
} // void lp5814_init(void)


/*============================================================================*/
/**
 * @brief SHUTDOWN the lp5814
 */
/*============================================================================*/
void lp5814_shutdown(void)
{
	lp5814_writeRegister(LP5814_SHUTDOWN, LP5814_SHUTDOWN_CMD);
	lp5814_writeRegister(LP5814_UPDATE, LP5814_UPDATE_CMD);

	HAL_Delay(1); // Wait for 1ms to ensure the shutdown command is processed
}


/*============================================================================*/
/**
 * @brief Turn on LED with pwm
 */
/*============================================================================*/
void lp5814_led_on(uint8_t port, uint8_t value)
{
	lp5814_writeRegister(LP5814_OUT_PWM(port), value);

	uint8_t stat = lp5814_readRegister(LP5814_REG_DEV_CONFIG1);

	stat |= LP5814_OUT_ENABLE(port);
	lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, stat);
	lp5814_writeRegister(LP5814_UPDATE_CMD, LP5814_UPDATE);
}



/*============================================================================*/
/**
 * @brief Turn on LED R/G/B
 */
/*============================================================================*/
void lp5814_led_on_rgb(uint8_t led_rgb, uint8_t value)
{
	uint8_t stat = lp5814_readRegister(LP5814_REG_DEV_CONFIG1);

	if ( led_rgb & LED_BLINK_ON_RED )
	{
		lp5814_writeRegister(LP5814_OUT_PWM(LED_R), value);
		stat |= LP5814_OUT_ENABLE(LED_R);
	}
	if ( led_rgb & LED_BLINK_ON_GREEN )
	{
		lp5814_writeRegister(LP5814_OUT_PWM(LED_G), value);
		stat |= LP5814_OUT_ENABLE(LED_G);
	}
	if ( led_rgb & LED_BLINK_ON_BLUE )
	{
		lp5814_writeRegister(LP5814_OUT_PWM(LED_B), value);
		stat |= LP5814_OUT_ENABLE(LED_B);
	}

	lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, stat);
	lp5814_writeRegister(LP5814_UPDATE_CMD, LP5814_UPDATE);
}



/*============================================================================*/
/**
 * @brief Turn off LED
 */
/*============================================================================*/
void lp5814_led_off(uint8_t port)
{
	uint8_t stat = lp5814_readRegister(LP5814_REG_DEV_CONFIG1);

	stat &= ~LP5814_OUT_ENABLE(port);
	lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, stat);
	lp5814_writeRegister(LP5814_UPDATE_CMD, LP5814_UPDATE);
}


/*============================================================================*/
/**
 * @brief Turn off RGB leds
 */
/*============================================================================*/
void lp5814_all_off_RGB(void)
{
	lp5814_led_off(LED_R);
	lp5814_led_off(LED_G);
	lp5814_led_off(LED_B);
} // void lp5814_all_off_RGB(void)


/*============================================================================*/
/**
 * @brief Turn on the White LED used as the backlight for the LCD display
 */
/*============================================================================*/
void lp5814_backlight_on(uint8_t brightness)
{
	if (brightness == 0)
		lp5814_led_off(LED_W);
	lp5814_led_on(LED_W, brightness);
} // void lp5814_backlight_on(uint8_t brightness)


/*============================================================================*/
/**
 * @brief Turn on Red led with given PWM level
 */
/*============================================================================*/
void lp5814_led_on_Red(uint8_t pwm)
{
	lp5814_led_on(LED_R, pwm);
} // void lp5814_led_on_Red(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on Green led with given PWM
 */
/*============================================================================*/
void lp5814_led_on_Green(uint8_t pwm)
{
	lp5814_led_on(LED_G, pwm);
} // void lp5814_led_on_Green(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on Blue led with given PWM
 */
/*============================================================================*/
void lp5814_led_on_Blue(uint8_t pwm)
{
	lp5814_led_on(LED_B, pwm);
} // void lp5814_led_on_Blue(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on fast blink for RGB with given PWM
 */
/*============================================================================*/
void lp5814_fastblink_on_RGB(uint8_t pwm_rgb, uint16_t on_off_ms)
{
	lp5814_fastblink_on_R_G_B(LED_BLINK_ON_RGB, pwm_rgb, on_off_ms);
} // void lp5814_fastblink_on_RGB(uint8_t pwm_rgb, uint16_t on_off_ms)



/*============================================================================*/
/**
 * @brief Turn on fast blink for R/G/B with given PWM
 */
/*============================================================================*/
void lp5814_fastblink_on_R_G_B(uint8_t r_g_b, uint8_t pwm_rgb, uint16_t on_off_ms)
{
	// PAUSE_T0 - SLOPER_T0 - SLOPER_T1 - SLOPER_T2 - SLOPER_T3 - PAUSE_T1
	// L        - rising    - H         - H         - falling   - L
	// PWM0			PWM0		PWM1		PWM2		PW3			PWM4
	/*
	 *			PWM0=y		PWM1=y		PWM0			PWM1	...repeat...
	 * 	  		  __________			   _____________
	 *						| 			  |				|
	 * 						|			  |				|
	 * 	PULSE_T0 = 0		|SLOPER_T1=0  |SLOPER_T3=0	|
	 * 						|			  |				|
	 * 						|			  |				|
	 * 						|_____________|				|____________
	 * 		SLOPER_T0=x		PWM2=0		PWM3=0			PWM2
	 * 						SLOPER_T2=x PWM4=0
	 */

	uint8_t rgb_out_auto = 0;

	if (on_off_ms == 0)
	{
		lp5814_writeRegister(LP5814_STOP_CMD, LP5814_STOP_ANI);
		lp5814_writeRegister(LP5814_ENGINE_CONFIG4, 0x00); // Engine1_Order3-Order0 | Engine0_Order3-Order0 disable
		lp5814_writeRegister(LP5814_REG_DEV_CONFIG3, 0x00); // animation disable
		lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, LP5814_OUT_ENABLE(LED_W));
		lp5814_writeRegister(LP5814_UPDATE_CMD, LP5814_UPDATE);
		return;
	}

	on_off_ms += (LED_ONTIME_MIN - 1);
	on_off_ms /= LED_ONTIME_MIN;

	lp5814_writeRegister(LP5814_PATTERN0_PAUSE, LP5814_PATTERNX_PAUSE_H_L(0, 0));	// PAUSE_T0(0 msec). PAUSE_T1(0 msec)
	lp5814_writeRegister(LP5814_PATTERN_REPEAT_TIME, 0x0F);	// infinite

	lp5814_writeRegister(LP5814_PATTERN_PWM_0, pwm_rgb);	// H
	lp5814_writeRegister(LP5814_PATTERN_PWM_1, pwm_rgb);	// H
	lp5814_writeRegister(LP5814_PATTERN_PWM_2, 0x00);	// L
	lp5814_writeRegister(LP5814_PATTERN_PWM_3, 0x00);	// L
	lp5814_writeRegister(LP5814_PATTERN_PWM_4, 0x00);	// L

	lp5814_writeRegister(LP5814_PATTERN_SLOPER_TIME1, LP5814_SLOPER_TIMEX_PATTERN_H_L(0, on_off_ms));	// T1(0 msec), T0(n*50 msec)
	lp5814_writeRegister(LP5814_PATTERN_SLOPER_TIME2, LP5814_SLOPER_TIMEX_PATTERN_H_L(0, on_off_ms));	// T3(0 msec) , T2(n*50 msec)

	if ( r_g_b & LED_BLINK_ON_RED )
		rgb_out_auto |= LP5814_OUT_ENABLE(LED_R);
	if ( r_g_b & LED_BLINK_ON_GREEN )
		rgb_out_auto |= LP5814_OUT_ENABLE(LED_G);
	if ( r_g_b & LED_BLINK_ON_BLUE )
		rgb_out_auto |= LP5814_OUT_ENABLE(LED_B);

	lp5814_writeRegister(LP5814_REG_DEV_CONFIG1, rgb_out_auto | LP5814_OUT_ENABLE(LED_W));
	lp5814_writeRegister(LP5814_REG_DEV_CONFIG3, rgb_out_auto);
	lp5814_writeRegister(LP5814_ENGINE_CONFIG4, 0x01); // Engine1_Order3-Order0 | Engine0_Order3-Order0 enable

	lp5814_writeRegister(LP5814_UPDATE_CMD, LP5814_UPDATE);

	lp5814_writeRegister(LP5814_START_CMD, LP5814_START_ANI);
} // void lp5814_fastblink_on_R_G_B(uint8_t r_g_b, uint8_t pwm_rgb, uint16_t on_off_ms)



/*============================================================================*/
/**
  * @brief  Blink the R/G/B with timer.
  * @param  r_g_b select LED(s) to blink
  * 		on_off_ms blink time in millisecond
  * 		mode blink mode
  * 		callback_fn call this function after the timer completes to restore previous led mode, if any
  * @retval None
  */
/*============================================================================*/
void lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode, void (*callback_fn)())
{
	lp5814_all_off_RGB(); // Turn off
	lp5814_fastblink_on_R_G_B(0, 0, 0);

	switch ( mode )
	{
		case LED_BLINK_TIMER_MODE_SOLID:
			lp5814_led_on_rgb(r_g_b, LED_FASTBLINK_PWM_M);
			break;

		case LED_BLINK_TIMER_MODE_BLINK:
			lp5814_fastblink_on_R_G_B(0, 0, 0); // Turn off first
			lp5814_fastblink_on_R_G_B(r_g_b, LED_FASTBLINK_PWM_M, on_off_ms);
			break;

		default:
			break;
	} // switch ( mode )

	blink_timer_cb_func = callback_fn;

	TimerHandle_t blink_timer_play = xTimerCreate("led_blink_timer", LED_BLINK_TIMER_TIMEOUT/portTICK_PERIOD_MS, pdFALSE, NULL, lp5814_stop_blink_timer);
	assert_param(blink_timer_play != NULL);
	xTimerStart(blink_timer_play, 0); // Schedule to stop blink_timer_play

} // void lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, void (*callback_fn)())



/*============================================================================*/
/**
  * @brief  Stop the blink timer
  * @param
  *
  * @retval None
  */
/*============================================================================*/
static void lp5814_stop_blink_timer(TimerHandle_t xTimer)
{
	lp5814_all_off_RGB(); // Turn off
	lp5814_fastblink_on_R_G_B(0, 0, 0);
	if ( blink_timer_cb_func )
	{
		blink_timer_cb_func();
		blink_timer_cb_func = NULL; // reset
	} // if ( blink_timer_cb_func )
} // static void lp5814_stop_blink_timer(TimerHandle_t xTimer)
