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
#ifndef M1_LP5814_H_
#define M1_LP5814_H_

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "m1_types_def.h"

#define	LED_BLINK_ON_RED		0x01
#define	LED_BLINK_ON_GREEN		0x02
#define	LED_BLINK_ON_BLUE		0x04
#define	LED_BLINK_ON_RGB		(LED_BLINK_ON_RED + LED_BLINK_ON_GREEN + LED_BLINK_ON_BLUE)

#define LED_BLINK_TIMER_MODE_SOLID		0
#define LED_BLINK_TIMER_MODE_BLINK		1

#define LED_BLINK_TIMER_ONTIME			100 // on time, ms
#define LED_BLINK_TIMER_PWM				175
#define LED_BLINK_TIMER_TIMEOUT			(TASKDELAY_SDCARD_DET_TASK - 100) //ms

enum LED_PORT {
	LED_B = 0,
	LED_G,
	LED_R,
	LED_W,
	LED_MAX
};

#define LP5814_REG_ENABLE 						0x00			//!< Enable register (0x00)
#define LP5814_ENABLE_CHIP_EN 					0x01			//!< Enable the chip. Power-up default is off. Make sure you set the current before enabling!

#define LP5814_REG_DEV_CONFIG0					0x01
#define LP5814_DEV_CONFIG0_51mA					0x01			// 0(25.5mA), 1(51mA)

#define LP5814_REG_DEV_CONFIG1					0x02
#define LP5814_OUT_ENABLE(n)					(0x01 << n)		// 1, 2, 4, 8

#define LP5814_REG_DEV_CONFIG2					0x03			// 7 ~ 4, 0 ~ 10 ( n * 0.05 sec), 11(1sec),12 ~ 15 (n-11) * 2sec
#define LP5814_FADE_ENABLE(n)					(0x01 << n) 	// 1, 2, 4, 8

#define LP5814_REG_DEV_CONFIG3					0x04
#define LP5814_EXP_ENABLE(n)					(0x10 << n)		// exponential pwm dimming
#define LP5814_AUTO_ENABLE(n)					(0x01 << n)		// Autonomous animation

#define LP5814_REG_DEV_CONFIG4					0x05
#define LP5814_OUT_ENGINE(n, eng_p)				(eng_p << (n * 2))

#define LP5814_REG_ENGINE_CONFIG(cfg)			(0x06 + cfg)				// cfg(0 ~ 3)
#define LP5814_ORDER_PATTERN(order, pattern)	(pattern << (order * 2))	// pattern(0 ~ 3), order (0 ~ 3)


#define LP5814_ENGINE_CONFIG4					0x0A
#define LP5814_ENGINE_CONFIG5					0x0B
#define LP5814_ORDER_ENABLE(eng_n, order_p)		(0x01 << ((eng_p & 0x01) * 4 + order_p))
/* Engine3 Order 3 enable
 * Engine3 Order 2 : Left shift 6
 * Engine3 Order 1 : Left shift 5
 * Engine3 Order 0 : Left shift 4
 * Engine2 Order 3 : Left shift 3
 * Engine2 Order 2 : Left shift 2
 * Engine2 Order 1 : Left shift 1
 * Engine2 Order 0 : Left shift 0 */


#define LP5814_ENGINE_CONFIG6					0x0C
#define LP5814_REPEAT(eng_n, rpt)				(rpt << (eng_n * 2))		// rpt (0 ~ 3[infinite])

#define LP5814_SHUTDOWN_CMD						0x0D
#define LP5814_SHUTDOWN							0x33

#define LP5814_RESET_CMD						0x0E
#define LP5814_RESET							0xCC

#define LP5814_UPDATE_CMD						0x0F
#define LP5814_UPDATE							0x55

#define LP5814_START_CMD						0x10
#define LP5814_START_ANI						0xFF		// Start Autonomous Animation

#define LP5814_STOP_CMD							0x11
#define LP5814_STOP_ANI							0xAA		// Stop Autonomous Animation

#define LP5814_PAUSE_CONT						0x12
#define LP5814_CONTINUE							0x00		// Continue Autonomous Animation
#define LP5814_PAUSE							0x01		// Pause Autonomous Animation

#define LP5814_FLAG_CLR							0x13
#define LP5814_FLAG_TSD							0b10		// Clear TSD
#define LP5814_FLAG_POR							0b01		// Clear POR


// 0x14 ~ 0x17
#define LP5814_OUT_DC(port)						(0x14 + port)	// Dot Current(%). 0 ~ 255  Iout = Iout_max * DC / 255 (mA), Iavg = Iout_max * DC / 255 * Dpwm
// 0x18 ~ 0x1B
#define LP5814_OUT_PWM(port)					(0x18 + port)	// 0 ~ 255, manual PWM setting (%)


#define LP5814_PATTERN0_PAUSE					0x1C
#define LP5814_PATTERNX_PAUSE_H_L(patt_h, patt_l)	((patt_h<<4) + patt_l)	// 0 ~ 10 ( n * 0.05 sec), 11(1sec),12 ~ 15 (n-11) * 2sec

#define LP5814_PATTERN_REPEAT_TIME				0x1D // 0 ~ 14 (n times) , 15(Infinity)

#define LP5814_PATTERN_PWM_0					0x1E
#define LP5814_PATTERN_PWM_1					0x1F
#define LP5814_PATTERN_PWM_2					0x20
#define LP5814_PATTERN_PWM_3					0x21
#define LP5814_PATTERN_PWM_4					0x22

#define LP5814_PATTERN_SLOPER_TIME1				0x23
#define LP5814_PATTERN_SLOPER_TIME2				0x24

#define LP5814_SLOPER_TIMEX_PATTERN_H_L(patt_h, patt_l)	((patt_h<<4) + patt_l)	// patt_x: 0 ~ 10 ( n * 0.05 sec), 11(1sec),12 ~ 15 (n-11) * 2sec


#define LP5814_FLAG					0x40
#define LP5814_FLAG_OUT3_ENGINE_BUSY	0x40
#define LP5814_FLAG_OUT2_ENGINE_BUSY	0x20
#define LP5814_FLAG_OUT1_ENGINE_BUSY	0x10
#define LP5814_FLAG_OUT0_ENGINE_BUSY	0x08
#define LP5814_FLAG_ENGINE_BUSY		0x04
//#define LP5814_FLAG_TSD				0x02 // Defined above
//#define LP5814_FLAG_POR				0x01	// Defined above


#define lp5814_I2C_ADD			0x2C
#define lp5814_I2C_WR_TIMEOUT	200 // Timeout for I2C Write/Read access in ms



typedef struct {
	/**
	 * @brief The I2C address (0x00 - 0x7f). Default is 0x30.
	 *
	 * If you passed in an address 0 - 3 into the constructor, 0x30 - 0x33 is stored here.
	 */
	uint8_t addr;


	/**
	 * @brief Current to supply to the red LED. Default is 5 mA
	 *
	 * Make sure this is set before calling begin! Setting the value too high can destroy the LED!
	 * Note the hardware value is even higher, but the library sets it always and uses a safe
	 * default.
	 */
	uint8_t redCurrent; // = 50;

	/**
	 * @brief Current to supply to the green LED. Default is 5 mA
	 *
	 * Make sure this is set before calling begin! Setting the value too high can destroy the LED!
	 * Note the hardware value is even higher, but the library sets it always and uses a safe
	 * default.
	 */
	uint8_t greenCurrent; // = 50;

	/**
	 * @brief Current to supply to the blue LED. Default is 5 mA
	 *
	 * Make sure this is set before calling begin! Setting the value too high can destroy the LED!
	 * Note the hardware value is even higher, but the library sets it always and uses a safe
	 * default.
	 */
	uint8_t blueCurrent; // = 50;

	/**
	 * @brief Current to supply to the white LED. Default is 5 mA
	 *
	 * Make sure this is set before calling begin! Setting the value too high can destroy the LED!
	 * Note the hardware value is even higher, but the library sets it always and uses a safe
	 * default.
	 */
	uint8_t whiteCurrent; // = 50;

} S_M1_lp5814;


/**
 * @brief Sets the LED current
 *
 * @param all The current for all LEDs (R, G, B, and W). You can set specific different currents with
 * the other overload. The default is 5 mA. The range of from 0.1 to 25.5 mA in increments of 0.1 mA.
 *
 * This method returns a lp5814 object so you can chain multiple configuration calls together, fluent-style.
 */
void lp5814_withLEDCurrent_all(float all);


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
void lp5814_withLEDCurrent(float red, float green, float blue, float white);


/**
 * @brief Set up the I2C device and begin running.
 *
 * You cannot do this from STARTUP or global object construction time. It should only be done from setup
 * or loop (once).
 *
 * Make sure you set the LED current using withLEDCurrent() before calling begin if your LED has a
 * current other than the default of 5 mA.
 */
bool lp5814_begin(void);


/**
 * @brief Get the value of the enable register (0x00)
 */
uint8_t lp5814_getEnable(void);


/**
 * @brief Convert a current value in mA to the format used by the lp5814
 *
 * @param value Value in mA
 *
 * @return A uint8_t value in DC(0 ~ 255) depend MAX_CURRENT. For example, passing 5 (mA) (MAX 51mA) will return 25(10%)
 */
uint8_t lp5814_currentToPercent(float value, bool max51mA);

/**
 * @brief Low-level call to read a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 */
uint8_t lp5814_readRegister(uint8_t reg);

/**
 * @brief Low-level call to write a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 *
 * @param value The value to set
 *
 * Note that setProgram bypasses this to write multiple bytes at once, to improve efficiency.
 */
bool lp5814_writeRegister(uint8_t reg, uint8_t value);

float 	lp5814_get_time(int n);
uint8_t	lp5814_set_time(float f);

void 	lp5814_led_on(uint8_t port, uint8_t value);
void 	lp5814_led_on_rgb(uint8_t led_rgb, uint8_t value);
void 	lp5814_led_off(uint8_t port);

void 	lp5814_init(void);
void 	lp5814_shutdown(void);
void 	lp5814_backlight_on(uint8_t brightness);
void 	lp5814_all_off_RGB(void);
void 	lp5814_led_on_Red(uint8_t pwm);
void 	lp5814_led_on_Green(uint8_t pwm);
void 	lp5814_led_on_Blue(uint8_t pwm);
void 	lp5814_fastblink_on_RGB(uint8_t pwm_rgb, uint16_t on_off_ms);
void 	lp5814_fastblink_on_R_G_B(uint8_t r_g_b, uint8_t pwm_rgb, uint16_t on_off_ms);
void 	lp5814_set_blink_timer(uint8_t r_g_b, uint16_t on_off_ms, uint8_t mode, void (*callback_fn)());

#endif /* M1_LP5814_H_ */
