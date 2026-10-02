/*
*
*  m1_lp5562.c
*
* Library for LP5562 RGB led driver
*
*  B.N @ MonstaTek, Nov 2024
*
* Reference: https://github.com/rickkas7/LP5562-RK
*/


/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_types_def.h"
#include "m1_lp5562.h"
#include "m1_i2c.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_M1_LP5562 m1_lp5562_ctl;
S_M1_LP5562_PROG m1_lp5562_prog;

static S_M1_I2C_Trans_Inf i2c_inf;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void lp5562_withLEDCurrent_all(float all);
void lp5562_withLEDCurrent(float red, float green, float blue, float white);
void lp5562_withUseExternalOscillator(bool value);
void lp5562_withUseLogarithmicMode(bool value);
void lp5562_withHighFrequencyMode(bool value);
bool lp5562_begin(void);
void lp5562_setPWM_R(uint8_t red);
void lp5562_setPWM_G(uint8_t green);
void lp5562_setPWM_B(uint8_t blue);
void lp5562_setPWM_RGB(uint8_t red, uint8_t green, uint8_t blue);
void lp5562_setPWM_all(uint32_t rgb);
void lp5562_setPWM_W(uint8_t white);
void lp5562_useDirect_RGB(void);
void lp5562_useDirect_W(void);
void lp5562_setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t on_ms, uint32_t off_ms);
void lp5562_setBlink_rgb(uint32_t rgb, uint32_t on_ms, uint32_t off_ms);
void lp5562_setBlink2(uint8_t red1, uint8_t green1, uint8_t blue1, uint32_t ms1, uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t ms2);
void lp5562_setBlink2_rgb(uint32_t rgb1, uint32_t ms1, uint32_t rgb2, uint32_t ms2);
void lp5562_setBreathe(bool red, bool green, bool blue, uint8_t stepTimeHalfMs, uint8_t lowLevel, uint8_t highLevel);
void lp5562_set_IndicatorMode(uint32_t eng1_on_ms, uint32_t eng1_off_ms, uint32_t eng2_on_ms, uint32_t eng2_off_ms, uint8_t eng3_breath_ms);
bool lp5562_setLedMapping(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);
bool lp5562_set_LedMapping_R(uint8_t mode, uint8_t value);
bool lp5562_set_LedMapping_G(uint8_t mode, uint8_t value);
bool lp5562_set_LedMapping_B(uint8_t mode, uint8_t value);
bool lp5562_set_LedMapping_W(uint8_t mode, uint8_t value);
uint8_t lp5562_get_LedMapping(void);
bool lp5562_setEnable(uint8_t engineMask, uint8_t engineMode);
uint8_t lp5562_getEnable(void);
uint8_t lp5562_engineNumToMask(size_t engine);
bool lp5562_setOpMode(size_t engine, uint8_t engineMode);
uint8_t lp5562_getOpMode(void);
uint8_t lp5562_getStatus(void);
bool lp5562_clearProgram(size_t engine);
bool lp5562_clear_AllPrograms(void);
bool lp5562_setProgram(size_t engine, const uint16_t *instructions, size_t numInstruction, bool startRunning);
uint8_t lp5562_floatToCurrent(float value);
bool lp5562_addCommand_Wait(bool prescale, uint8_t stepTime, int atInst);
bool lp5562_addCommand_Ramp(bool prescale, uint8_t stepTime, bool decrease, uint8_t numSteps, int atInst);
bool lp5562_addCommand_SetPWM(uint8_t level, int atInst);
bool lp5562_addCommand_GoToStart(int atInst);
bool lp5562_addCommand_Branch(uint8_t loopCount, uint8_t stepNum, int atInst);
bool lp5562_addCommand_End(bool generateInterrupt, bool setPWMto0, int atInst);
bool lp5562_addCommand_TriggerSend(uint8_t engineMask, int atInst);
bool lp5562_addCommand_TriggerWait(uint8_t engineMask, int atInst);
bool lp5562_addCommand(uint16_t cmd, int atInst);
bool lp5562_addDelay(uint32_t milliseconds);
void lp5562_clear(void);
uint8_t lp5562_get_StepNum(void);
uint16_t *lp5562_get_Instructions(void);
void lp5562_init(void);
void lp5562_backlight_on(uint8_t brightness);
void lp5562_all_off_RGB(void);
void lp5562_led_on_Red(uint8_t pwm);
void lp5562_led_on_Green(uint8_t pwm);
void lp5562_led_on_Blue(uint8_t pwm);
void lp5562_fastblink_on_RGB(uint8_t pwm_rgb, uint8_t on_off_ms);

#ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE
void lp5562_testPgm2(void);
void lp5562_testPgm1(void);
#endif // #ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief Sets the LED current
 *
 * @param all The current for all LEDs (R, G, B, and W). You can set specific different currents with
 * the other overload. The default is 5 mA. The range of from 0.1 to 25.5 mA in increments of 0.1 mA.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5562_withLEDCurrent_all(float all)
{
	lp5562_withLEDCurrent(all, all, all, all);
} // void lp5562_withLEDCurrent_all(float all)



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
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5562_withLEDCurrent(float red, float green, float blue, float white)
{
	m1_lp5562_ctl.redCurrent = lp5562_floatToCurrent(red);
	m1_lp5562_ctl.greenCurrent = lp5562_floatToCurrent(green);
	m1_lp5562_ctl.blueCurrent = lp5562_floatToCurrent(blue);
	m1_lp5562_ctl.whiteCurrent = lp5562_floatToCurrent(white);
} // void lp5562_withLEDCurrent(float red, float green, float blue, float white)



/*============================================================================*/
/**
 * @brief Set external oscillator mode. Default is internal.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5562_withUseExternalOscillator(bool value)
{
	m1_lp5562_ctl.useExternalOscillator = value;
} // void lp5562_withUseExternalOscillator(bool value)



/*============================================================================*/
/**
 * @brief Use Logarithmic Mode for PWM brightness. Default = true.
 *
 * This adjusts the PWM value for the perceived brightness from the human eye vs. actual linear values.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5562_withUseLogarithmicMode(bool value)
{
	m1_lp5562_ctl.useLogarithmicMode = value;
} // void lp5562_withUseLogarithmicMode(bool value)



/*============================================================================*/
/**
 * @brief Enable high frequency PWM. Default = false.
 *
 * Low frequency (default) is 256 Hz. High frequency is 558 Hz.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
/*============================================================================*/
void lp5562_withHighFrequencyMode(bool value)
{
	m1_lp5562_ctl.highFrequencyMode = value;
} // void lp5562_withHighFrequencyMode(bool value)



/*============================================================================*/
/**
 * @brief Set blinking mode on the RGB LED
 *
 * @param rgb Value in the form of 0x00RRGGBB. Each of RR, GG, and BB are from
 * 0x00 (off) to 0xFF (full brightness).
 *
 * @param on_ms The number of milliseconds to be on (1 - 61916)
 *
 * @param off_ms The number of milliseconds to be off (1 - 61916)
 */
/*============================================================================*/
void lp5562_setBlink_rgb(uint32_t rgb, uint32_t on_ms, uint32_t off_ms)
{
	lp5562_setBlink((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, on_ms, off_ms);
} // void lp5562_setBlink_rgb(uint32_t rgb, uint32_t on_ms, uint32_t off_ms)



/*============================================================================*/
/**
 * @brief Get the value of the LED mapping register (0x70)
 */
/*============================================================================*/
uint8_t lp5562_get_LedMapping(void)
{
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_LED_MAP;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // uint8_t lp5562_get_LedMapping(void)



/*============================================================================*/
/**
 * @brief Get the value of the enable register (0x00)
 */
/*============================================================================*/
uint8_t lp5562_getEnable(void)
{
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_ENABLE;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // uint8_t lp5562_getEnable(void)



/*============================================================================*/
/**
 * @brief Get the value of the operation mode register (0x01)
 */
/*============================================================================*/
uint8_t lp5562_getOpMode(void)
{
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_OP_MODE;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // uint8_t lp5562_getOpMode(void)



/*============================================================================*/
/**
 * @brief Get the value of the status/interrupt register
 *
 * Reading the status/interrupt register will clear any interrupts that are set.
 */
/*============================================================================*/
uint8_t lp5562_getStatus(void)
{
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_STATUS;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // uint8_t lp5562_getStatus(void)



/*============================================================================*/
/**
 * @brief Clears a program on the specified engine
 *
 * @param engine An engine number 1 <= engine <= 3
 */
/*============================================================================*/
bool lp5562_clearProgram(size_t engine)
{
	return lp5562_setProgram(engine, NULL, 0, false);
} // bool lp5562_clearProgram(size_t engine)


/*============================================================================*/
/**
 * @brief Convert a current value in mA to the format used by the LP5562
 *
 * @param value Value in mA
 *
 * @return A uint8_t value in tenths of a mA. For example, passing 5 (mA) will return 50.
 */
/*============================================================================*/
uint8_t lp5562_floatToCurrent(float value)
{
	if (value < 0) {
		value = 0;
	}
	if (value > 25.5) {
		value = 25.5;
	}
	return (uint8_t) (value * 10);
} // uint8_t lp5562_floatToCurrent(float value)


/*============================================================================*/
/**
 * @brief Set up the I2C device and begin running.
 *
 * Make sure you set the LED current using withLEDCurrent() before calling begin if your LED has a
 * current other than the default of 5 mA.
 */
/*============================================================================*/
bool lp5562_begin(void)
{
	uint8_t stat;

	i2c_inf.dev_id = I2C_DEVICE_LP5562;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;

	// Reset chip - reset all registers to default values. Note that resetting the MCU won't reset
	// the values in the chip, so it's a good idea to do this in begin().
	i2c_inf.reg_address = REG_RESET;
	i2c_inf.reg_data = 0xFF;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	// Set current level. The hardware default is 17.8 mA, but we default to 5 mA in software. You can
	// override this with the withLEDCurrent methods. Be sure to do this before enabling the chip!
	i2c_inf.reg_address = REG_R_CURRENT;
	i2c_inf.reg_data = m1_lp5562_ctl.redCurrent;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	i2c_inf.reg_address = REG_G_CURRENT;
	i2c_inf.reg_data = m1_lp5562_ctl.greenCurrent;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	i2c_inf.reg_address = REG_B_CURRENT;
	i2c_inf.reg_data = m1_lp5562_ctl.blueCurrent;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	i2c_inf.reg_address = REG_W_CURRENT;
	i2c_inf.reg_data = m1_lp5562_ctl.whiteCurrent;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	// Set the default PWM levels to 0 initially
	i2c_inf.reg_data = 0x00;

	i2c_inf.reg_address = REG_R_PWM;
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = REG_G_PWM;
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = REG_B_PWM;
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = REG_W_PWM;
	m1_i2c_hal_trans_req(&i2c_inf);

	// Enable the chip
	uint8_t value = REG_ENABLE_CHIP_EN;
	if (m1_lp5562_ctl.useLogarithmicMode) {
		value |= REG_ENABLE_LOG_EN;
	}

	i2c_inf.reg_address = REG_ENABLE;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	// Hardware start-up delay 500us
	HAL_Delay(1);
	//vTaskDelay(pdMS_TO_TICKS(1));

	// Enable clock
	value = 0x00;
	if (!m1_lp5562_ctl.useExternalOscillator)
	{
		value |= REG_CONFIG_INT_CLK_EN;
	}
	if (m1_lp5562_ctl.highFrequencyMode)
	{
		value |= REG_CONFIG_HF;
	}

	i2c_inf.reg_address = REG_CONFIG;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	// Enable direct PWM control for all LEDs by default
	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = 0x00;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
		return false;
	}

	return true;
} // bool lp5562_begin(void)



/*============================================================================*/
/**
 * @brief Clears programs on engines 1, 2, and 3 (all engines)
 */
/*============================================================================*/
bool lp5562_clear_AllPrograms(void)
{
	for(size_t engine = 1; engine <= 3; engine++) {
		bool bResult = lp5562_clearProgram(engine);
		if (!bResult) {
			return false;
		}
	}
	return true;
} // bool lp5562_clear_AllPrograms(void)



/*============================================================================*/
/**
 * @brief Sets a program on a specified engine from a from program words
 *
 * @param engine An engine number 1 <= engine <= 3
 *
 * @param instructions An array of uint16_t values containing up to 16 instructions. Can be NULL if numInstruction == 0.
 *
 * @param numInstruction The number of instruction words (0 - 15).
 *
 * @param startRunning true to start the program running immediately or false to leave it in halt mode.
 *
 * The unused instruction words (numInstruction to 16) are always set to 0 for safety. If you call this
 * with numInstruction == 0 it clears the program (which is what clearProgram and clearAllPrograms do).
 */
/*============================================================================*/
bool lp5562_setProgram(size_t engine, const uint16_t *instructions, size_t numInstructions, bool startRunning)
{
	bool  bResult;
	uint8_t i2c_buffer[MAX_INSTRUCTIONS*2]; // 1 instruction = 2 bytes
	uint16_t instructionsPadded[MAX_INSTRUCTIONS];
	uint8_t tx_size, stat, startAddr;

	// Set the engine to hold. This might not be necessary, I think it might happen automatically.
	lp5562_setEnable(lp5562_engineNumToMask(engine), REG_ENABLE_HOLD);

	// Enter code loading mode.
	bResult = lp5562_setOpMode(engine, REG_ENGINE_LOAD);
	if (!bResult) {
		return false;
	}

	// Make a copy of the instruction list with 0x0000 (go to start) instructions to the end of the buffer, like
	// is the case on boot. Also, so you don't need to manually add one to have the code loop.
	for(size_t ii = 0; ii < MAX_INSTRUCTIONS; ii++)
	{
		if (ii < numInstructions)
			instructionsPadded[ii] = instructions[ii];
		else
			instructionsPadded[ii] = 0;
	}

	startAddr = (uint8_t)(REG_PROGRAM_1 + (engine - 1) * 0x20);
	tx_size = 0;
	i2c_buffer[tx_size++] = startAddr;
	for(size_t ii = 0; ii < MAX_INSTRUCTIONS; ii++)
	{
		i2c_buffer[tx_size++] = (instructionsPadded[ii] >> 8); // MSB first
		i2c_buffer[tx_size++] = instructionsPadded[ii]; // LSB second
		// Log.info("startAddr=%02x ii=%u program=%04x", startAddr, ii, instructionsPadded[ii]);
		startAddr += 2;
	}

	/* Timeout is set to 100ms */
	i2c_inf.dev_id = I2C_DEVICE_LP5562;
	i2c_inf.pdata = i2c_buffer;
	i2c_inf.data_len = tx_size;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_DATA;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	if (stat != HAL_OK)
	{
	    /* Error_Handler() function is called when Timeout error occurs.
	       When Acknowledge failure occurs (Slave don't acknowledge its address)
	       Master restarts communication */
	    if (m1_i2c_hal_get_error() != HAL_I2C_ERROR_AF)
	    {
	    	//Error_Handler();
	    }
		return false;
	}

	// Get out of programming mode
	bResult = lp5562_setOpMode(engine, (numInstructions > 0) ? REG_ENGINE_RUN : REG_ENGINE_DISABLED);
	if (!bResult) {
		return false;
	}

	// If startRunning is true, actually start running
	if (startRunning && numInstructions > 0) {
		lp5562_setEnable(lp5562_engineNumToMask(engine), REG_ENABLE_RUN);
	}

	return true;
} // bool lp5562_setProgram(size_t engine, const uint16_t *instructions, size_t numInstructions, bool startRunning)



/*============================================================================*/
/**
 * @brief Convert an engine number 1 - 3 to an engineMask value
 *
 * @param engine An engine number 1 <= engine <= 3
 *
 * @return A mask value MASK_ENGINE_1, MASK_ENGINE_2, or MASK_ENGINE_3
 *
 * engine mask
 * 1      0b001
 * 2      0b010
 * 3      0b100
 */
/*============================================================================*/
uint8_t lp5562_engineNumToMask(size_t engine)
{
	switch(engine) {
		case 1:
			return MASK_ENGINE_1;
		case 2:
			return MASK_ENGINE_2;
		case 3:
			return MASK_ENGINE_3;
		default:
			return 0;
	}
} // uint8_t lp5562_engineNumToMask(size_t engine)



/*============================================================================*/
/**
 * @brief Set ledMapping to lp5562_ Not normally necessary.
 *
 * @param red REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3.
 *
 * @param green REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3.
 *
 * @param blue REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3.
 *
 * @param white REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3.
 *
 * This method is used to map the LED to direct or program mode. This is used internally by
 * setRGB, setBlink, setBlink2, or setBreathe so you don't normally need to call this yourself.
 */
/*============================================================================*/
bool lp5562_setLedMapping(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
	uint8_t stat;
	uint8_t value = 0;

	value |= (white & 0b11) << 6;
	value |= (red & 0b11) << 4;
	value |= (green & 0b11) << 2;
	value |= (blue & 0b11);

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_setLedMapping(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)


/*============================================================================*/
/**
 * @brief Set the led mapping for the red LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
/*============================================================================*/
bool lp5562_set_LedMapping_R(uint8_t mode, uint8_t value)
{
	uint8_t stat;
	uint8_t regValue = lp5562_get_LedMapping();

	regValue &= 0b11001111;
	regValue |= (mode & 0b11) << 4;

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;

	if ( mode==0 )
	{
		i2c_inf.reg_address = REG_R_PWM;
		i2c_inf.reg_data = value;
		m1_i2c_hal_trans_req(&i2c_inf);
	}

	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = regValue;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_set_LedMapping_R(uint8_t mode, uint8_t value)



/*============================================================================*/
/**
 * @brief Set the led mapping for the green LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
/*============================================================================*/
bool lp5562_set_LedMapping_G(uint8_t mode, uint8_t value)
{
	uint8_t stat;
	uint8_t regValue = lp5562_get_LedMapping();

	regValue &= 0b11110011;
	regValue |= (mode & 0b11) << 2;

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;

	if ( mode==0 )
	{
		i2c_inf.reg_address = REG_G_PWM;
		i2c_inf.reg_data = value;
		m1_i2c_hal_trans_req(&i2c_inf);
	}

	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = regValue;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_set_LedMapping_G(uint8_t mode, uint8_t value)


/*============================================================================*/
/**
 * @brief Set the led mapping for the blue LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
/*============================================================================*/
bool lp5562_set_LedMapping_B(uint8_t mode, uint8_t value)
{
	uint8_t stat;
	uint8_t regValue = lp5562_get_LedMapping();

	regValue &= 0b11111100;
	regValue |= (mode & 0b11);

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;

	if ( mode==0 )
	{
		i2c_inf.reg_address = REG_B_PWM;
		i2c_inf.reg_data = value;
		m1_i2c_hal_trans_req(&i2c_inf);
	}

	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = regValue;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_set_LedMapping_B(uint8_t mode, uint8_t value)


/*============================================================================*/
/**
 * @brief Set the led mapping for the white LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
/*============================================================================*/
bool lp5562_set_LedMapping_W(uint8_t mode, uint8_t value)
{
	uint8_t stat;
	uint8_t regValue = lp5562_get_LedMapping();

	regValue &= 0b00111111;
	regValue |= (mode & 0b11) << 6;

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;

	if ( mode==0 )
	{
		i2c_inf.reg_address = REG_W_PWM;
		i2c_inf.reg_data = value;
		m1_i2c_hal_trans_req(&i2c_inf);
	}

	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = regValue;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_set_LedMapping_W(uint8_t mode, uint8_t value)


/*============================================================================*/
/**
 * @brief Enable an engine mode on certain engines
 *
 * @param engineMask A mask of the engines to send to. Logical OR the values MASK_ENGINE_1,
 * MASK_ENGINE_2, and MASK_ENGINE_3 or use MASK_ENGINE_ALL for all 3 engines.
 *
 * @param engineMode One of the constants: REG_ENGINE_DISABLED, REG_ENGINE_LOAD,
 * REG_ENGINE_RUN, or REG_ENGINE_DIRECT.
 *
 * This is normally done automatically for you when entering program mode or direct mode.
 */
/*============================================================================*/
bool lp5562_setEnable(uint8_t engineMask, uint8_t engineMode)
{
	uint8_t value, stat;

	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_ENABLE;
	m1_i2c_hal_trans_req(&i2c_inf);

	value = i2c_inf.reg_data;

	if ((engineMask & MASK_ENGINE_1) != 0)
	{
		value &= 0b11001111;
		value |= (engineMode & 0b11) << 4;
	}

	if ((engineMask & MASK_ENGINE_2) != 0)
	{
		value &= 0b11110011;
		value |= (engineMode & 0b11) << 2;
	}

	if ((engineMask & MASK_ENGINE_3) != 0)
	{
		value &= 0b11111100;
		value |= (engineMode & 0b11);
	}

	// Log.info("setEnable mask 0x04x engineMode=%u value=%04x", engineMask, engineMode, value);
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_ENABLE;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_setEnable(uint8_t engineMask, uint8_t engineMode)


/*============================================================================*/
/**
 * @brief Sets the operation mode register (0x01)
 *
 * @param engine The engine number to set (1, 2, or 3)
 *
 * @param engineMode The mode to set: REG_ENGINE_DISABLED, REG_ENGINE_LOAD (also resets PC), REG_ENGINE_RUN, REG_ENGINE_DIRECT
 */
/*============================================================================*/
bool lp5562_setOpMode(size_t engine, uint8_t engineMode)
{
	uint8_t value, stat;

	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = REG_OP_MODE;
	m1_i2c_hal_trans_req(&i2c_inf);

	value = i2c_inf.reg_data;

	switch(engine)
	{
		case 1:
			value &= 0b11001111;
			value |= (engineMode & 0b11) << 4;
			break;

		case 2:
			value &= 0b11110011;
			value |= (engineMode & 0b11) << 2;
			break;

		case 3:
			value &= 0b11111100;
			value |= (engineMode & 0b11);
			break;

		default:
			break;
	}
	// Log.info("setOpMode engine=%u engineMode=%u value=%04x", engine, engineMode, value);
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_OP_MODE;
	i2c_inf.reg_data = value;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // bool lp5562_setOpMode(size_t engine, uint8_t engineMode)


/*============================================================================*/
/**
 * @brief Sets the PWM for the red channel
 *
 * @param red value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
/*============================================================================*/
void lp5562_setPWM_R(uint8_t red)
{
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_R_PWM;
	i2c_inf.reg_data = red;
	m1_i2c_hal_trans_req(&i2c_inf);
} // void lp5562_setPWM_R(uint8_t red)


/*============================================================================*/
/**
 * @brief Sets the PWM for the green channel
 *
 * @param green value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
/*============================================================================*/
void lp5562_setPWM_G(uint8_t green)
{
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_G_PWM;
	i2c_inf.reg_data = green;
	m1_i2c_hal_trans_req(&i2c_inf);
} // void lp5562_setPWM_G(uint8_t green)


/*============================================================================*/
/**
 * @brief Sets the PWM for the blue channel
 *
 * @param blue value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
/*============================================================================*/
void lp5562_setPWM_B(uint8_t blue)
{
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_B_PWM;
	i2c_inf.reg_data = blue;
	m1_i2c_hal_trans_req(&i2c_inf);
} // void lp5562_setPWM_B(uint8_t blue)


/*============================================================================*/
/**
 * @brief Sets the PWM for the R, G, and B channels.
 *
 * @param red value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param green value 0 - 255. 0 = off, 255 = full brightness.

 * @param blue value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
/*============================================================================*/
void lp5562_setPWM_RGB(uint8_t red, uint8_t green, uint8_t blue)
{
	lp5562_setPWM_R(red);
	lp5562_setPWM_G(green);
	lp5562_setPWM_B(blue);
} // void lp5562_setPWM_RGB(uint8_t red, uint8_t green, uint8_t blue)


/*============================================================================*/
/**
 * @brief Sets the PWM for the R, G, and B channels.
 *
 * @param rgb Value in the form of 0x00RRGGBB. Each of RR, GG, and BB are from
 * 0x00 (off) to 0xFF (full brightness).
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
/*============================================================================*/
void lp5562_setPWM_all(uint32_t rgb)
{
	lp5562_setPWM_R(rgb >> 16);
	lp5562_setPWM_G(rgb >> 8);
	lp5562_setPWM_B(rgb);
} // void lp5562_setPWM_all(uint32_t rgb)


/*============================================================================*/
/**
 * @brief Use direct mode on RGB LED. Changes the LED mapping register and if a program is running
 * on the R, G, or B LEDs, stops it.
 *
 * If you call setProgram() or functions like setBlink(), setBlink2(), or setBreathe() you must
 * call this before calling setRGB() or the program will continue to run and override your manual
 * setting!
 */
/*============================================================================*/
void lp5562_useDirect_RGB(void)
{
	uint8_t ledMap = lp5562_get_LedMapping();

	uint8_t engineMask = 0;

	engineMask |= lp5562_engineNumToMask(ledMap & 0b11);
	engineMask |= lp5562_engineNumToMask((ledMap >> 2) & 0b11);
	engineMask |= lp5562_engineNumToMask((ledMap >> 4) & 0b11);

	if (engineMask != 0)
	{
		lp5562_setEnable(engineMask, REG_ENABLE_HOLD);
		i2c_inf.timeout = I2C_WRITE_TIMEOUT;
		i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
		i2c_inf.reg_address = REG_LED_MAP;
		i2c_inf.reg_data = ledMap & 0b11000000;
		m1_i2c_hal_trans_req(&i2c_inf);
	}
} // void lp5562_useDirect_RGB(void)


/*============================================================================*/
/**
 * @brief Sets the W channel to the specified PWM value
 *
 * @param white value 0 - 255. 0 = off, 255 = full brightness.
 */
/*============================================================================*/
void lp5562_setPWM_W(uint8_t white)
{
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = REG_W_PWM;
	i2c_inf.reg_data = white;
	m1_i2c_hal_trans_req(&i2c_inf);
} // void lp5562_setPWM_W(uint8_t white)



/*============================================================================*/
/**
 * @brief Use direct mode on W LED. Changes the LED mapping register and if a program is running
 * on the W LED, stops it.
 *
 * If you call setProgram() or functions like setBlink(), setBlink2(), or setBreathe() you must
 * call this before calling setW() or the program will continue to run and override your manual
 * setting!
 */
/*============================================================================*/
void lp5562_useDirect_W(void)
{
	uint8_t ledMap = lp5562_get_LedMapping();

	uint8_t engineMask = 0;
	engineMask |= lp5562_engineNumToMask((ledMap >> 6) & 0b11);

	if (engineMask != 0)
	{
		lp5562_setEnable(engineMask, REG_ENABLE_HOLD);
		i2c_inf.timeout = I2C_WRITE_TIMEOUT;
		i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
		i2c_inf.reg_address = REG_LED_MAP;
		i2c_inf.reg_data = ledMap & 0b11000000;
		m1_i2c_hal_trans_req(&i2c_inf);
	}
} // void lp5562_useDirect_W(void)


/*============================================================================*/
/**
 * @brief Set indicator mode
 *
 * Engine 1 = Blink
 * Engine 2 = Fast Blink
 * Engine 3 = Breathe
 */
/*============================================================================*/
void lp5562_set_IndicatorMode(uint32_t eng1_on_ms, uint32_t eng1_off_ms, uint32_t eng2_on_ms, uint32_t eng2_off_ms, uint8_t eng3_breath_ms)
{
	lp5562_clear_AllPrograms();

	// The main program is either 6 or 8 instructions. When on_ms or off_ms is > 1000 ms, then the delay requires 2 instructions.

	// Normally blink
	lp5562_addCommand_SetPWM(255, -1); // full brightness
	lp5562_addDelay(eng1_on_ms);
	lp5562_addCommand_SetPWM(0, -1); // off
	lp5562_addDelay(eng1_off_ms);
	lp5562_addCommand_GoToStart(-1);
	lp5562_setProgram(1, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	// Normally fast blink
	lp5562_clear();
	lp5562_addCommand_SetPWM(255, -1); // full brightness
	lp5562_addDelay(eng2_on_ms);
	lp5562_addCommand_SetPWM(0, -1); // off
	lp5562_addDelay(eng2_off_ms);
	lp5562_addCommand_GoToStart(-1);
	lp5562_setProgram(2, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	// Breathe
	lp5562_clear();
	lp5562_addCommand_SetPWM(0, -1); // Start at lowLevel
	lp5562_addCommand_Ramp(false, eng3_breath_ms, false, 255, -1); // Ramp up
	lp5562_addCommand_Ramp(false, eng3_breath_ms, true, 255, -1); // Ramp down
	lp5562_setProgram(3, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	// Default to LEDs off
	lp5562_setLedMapping(REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT);
	lp5562_setPWM_RGB(0, 0, 0);
	lp5562_setPWM_W(0);

	lp5562_setEnable(MASK_ENGINE_ALL, REG_ENABLE_RUN);

} // void lp5562_set_IndicatorMode(uint32_t eng1_on_ms, uint32_t eng1_off_ms, uint32_t eng2_on_ms, uint32_t eng2_off_ms, uint8_t eng3_breath_ms)


/*============================================================================*/
/**
 * @brief Set blinking mode on the RGB LED
 *
 * @param red value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param green value 0 - 255. 0 = off, 255 = full brightness.

 * @param blue value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param on_ms The number of milliseconds to be on (1 - 61916)
 *
 * @param off_ms The number of milliseconds to be off (1 - 61916)
 */
/*============================================================================*/
void lp5562_setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t on_ms, uint32_t off_ms)
{
	lp5562_clear_AllPrograms();

	// The main program is either 6 or 8 instructions. When on_ms or off_ms is > 1000 ms, then the delay requires 2 instructions.
	lp5562_addCommand_SetPWM(red, -1);
	lp5562_addDelay(on_ms);
	lp5562_addCommand_SetPWM(0, -1);
	lp5562_addDelay(off_ms);
	uint8_t triggerStep = lp5562_get_StepNum();
	lp5562_addCommand_TriggerSend(MASK_ENGINE_2 | MASK_ENGINE_3, -1);
	lp5562_addCommand_GoToStart(-1);
	lp5562_setProgram(1, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_addCommand_SetPWM(green, 0);
	lp5562_addCommand_TriggerWait(MASK_ENGINE_1, triggerStep);
	lp5562_setProgram(2, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_addCommand_SetPWM(blue, 0);
	lp5562_setProgram(3, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_setLedMapping(REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, REG_LED_MAP_ENGINE_3, REG_LED_MAP_DIRECT);

	lp5562_setEnable(MASK_ENGINE_ALL, REG_ENABLE_RUN);
} // void lp5562_setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t on_ms, uint32_t off_ms)


/*============================================================================*/
/**
 * @brief Set alternating blink mode between two colors (no off phase)
 *
 * @param rgb1 Value in the form of 0x00RRGGBB. Each of RR, GG, and BB are from
 * 0x00 (off) to 0xFF (full brightness).
 *
 * @param ms1 The number of milliseconds to be the 1 color (1 - 61916)
 *
 * @param rgb2 Value in the form of 0x00RRGGBB. Each of RR, GG, and BB are from
 * 0x00 (off) to 0xFF (full brightness).
 *
 * @param ms2 The number of milliseconds to be the 2 color (1 - 61916)
 */
/*============================================================================*/
void lp5562_setBlink2_rgb(uint32_t rgb1, uint32_t ms1, uint32_t rgb2, uint32_t ms2)
{
	lp5562_setBlink2((uint8_t)(rgb1 >> 16), (uint8_t)(rgb1 >> 8), (uint8_t)rgb1, ms1,
			(uint8_t)(rgb2 >> 16), (uint8_t)(rgb2 >> 8), (uint8_t)rgb2, ms2);
} // void lp5562_setBlink2_rgb(uint32_t rgb1, uint32_t ms1, uint32_t rgb2, uint32_t ms2)


/*============================================================================*/
/**
 * @brief Set alternating blink mode between two colors (no off phase)
 *
 * @param red1 value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param green1 value 0 - 255. 0 = off, 255 = full brightness.

 * @param blue1 value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param ms1 The number of milliseconds to be the 1 color (1 - 61916)
 *
 * @param red2 value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param green2 value 0 - 255. 0 = off, 255 = full brightness.

 * @param blue2 value 0 - 255. 0 = off, 255 = full brightness.
 *
 * @param ms2 The number of milliseconds to be the 2 color (1 - 61916)
 */
/*============================================================================*/
void lp5562_setBlink2(uint8_t red1, uint8_t green1, uint8_t blue1, uint32_t ms1, uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t ms2)
{
	lp5562_clear_AllPrograms();

	// The main program is either 6 or 8 instructions. When ms1 or ms2 is > 1000 ms, then the delay requires 2 instructions.
	lp5562_addCommand_SetPWM(red1, -1);
	lp5562_addDelay(ms1);
	uint8_t colorStep = lp5562_get_StepNum();
	lp5562_addCommand_SetPWM(red2, -1);
	lp5562_addDelay(ms2);
	uint8_t triggerStep = lp5562_get_StepNum();
	lp5562_addCommand_TriggerSend(MASK_ENGINE_2 | MASK_ENGINE_3, -1);
	lp5562_addCommand_GoToStart(-1);
	lp5562_setProgram(1, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_addCommand_SetPWM(green1, 0);
	lp5562_addCommand_SetPWM(green2, colorStep);
	lp5562_addCommand_TriggerWait(MASK_ENGINE_1, triggerStep);
	lp5562_setProgram(2, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_addCommand_SetPWM(blue1, 0);
	lp5562_addCommand_SetPWM(blue2, colorStep);
	lp5562_setProgram(3, lp5562_get_Instructions(), lp5562_get_StepNum(), false);

	lp5562_setLedMapping(REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, REG_LED_MAP_ENGINE_3, REG_LED_MAP_DIRECT);

	lp5562_setEnable(MASK_ENGINE_ALL, REG_ENABLE_RUN);
} // void lp5562_setBlink2(uint8_t red1, uint8_t green1, uint8_t blue1, uint32_t ms1, uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t ms2)


/*============================================================================*/
/**
 * @brief Set breathing mode
 *
 * Because of hardware limitations, breathing mode can only be done for the 7 full brightness colors:
 * red      true, false, false
 * green    false, true, false
 * blue     false, false, true
 * yellow   true, true, false
 * cyan     false, true, true
 * magenta  true, false, true
 * white    true, true, true
 *
 * @param red true to breathe the red channel
 *
 * @param green true to breathe the green channel
 *
 * @param blue true to breathe the blue channel
 *
 * @param stepTimeHalfMs Amount of time between step changes from 1 to 63 in half millisecond increments.
 *
 * @param lowLevel Start at this level (0 - 255). Typically 0.
 *
 * @param highLevel End at this level (0 - 255). Typically 255. lowLevel must be < highLevel.
 */
/*============================================================================*/
void lp5562_setBreathe(bool red, bool green, bool blue, uint8_t stepTimeHalfMs, uint8_t lowLevel, uint8_t highLevel)
{
	lp5562_clear_AllPrograms();

	// Clear all LEDs because if they're not turned on, then we want them to be off.
	lp5562_setPWM_RGB(0, 0, 0);

	// Program is 3 instructions

	// Start at lowLevel
	lp5562_addCommand_SetPWM(lowLevel, -1);

	// Ramp up
	lp5562_addCommand_Ramp(false, stepTimeHalfMs, false, highLevel - lowLevel, -1);

	// Ramp down
	lp5562_addCommand_Ramp(false, stepTimeHalfMs, true, highLevel - lowLevel, -1);

	lp5562_setProgram(1, lp5562_get_Instructions(), lp5562_get_StepNum(), true);

	lp5562_setLedMapping(red ? REG_LED_MAP_ENGINE_1 : REG_LED_MAP_DIRECT,
			green ? REG_LED_MAP_ENGINE_1 : REG_LED_MAP_DIRECT,
			blue ? REG_LED_MAP_ENGINE_1 : REG_LED_MAP_DIRECT,
			REG_LED_MAP_DIRECT);

	lp5562_setEnable(REG_LED_MAP_ENGINE_1, REG_ENABLE_RUN);
} // void lp5562_setBreathe(bool red, bool green, bool blue, uint8_t stepTimeHalfMs, uint8_t lowLevel, uint8_t highLevel)




/*============================================================================*/
/**
 * @brief Add a wait command (ramp/wait with increment of 0)
 *
 * @param prescale false = 0.49 ms cycle time; true = 15.6 ms cycle time
 *
 * @param stepTime Wait this this many cycles (1 - 63)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * Wait times vary depending on prescale. With prescale = false, .49 ms to 7.35 ms.
 * With prescale = true, 15.6 ms to 982.8 ms. You can make even longer wait times by putting
 * a wait in a loop. Since a loop can be executed up to 63 times, you can get a 62 second delay.
 */
/*============================================================================*/
bool lp5562_addCommand_Wait(bool prescale, uint8_t stepTime, int atInst)
{
	return lp5562_addCommand_Ramp(prescale, stepTime, false, 0, atInst);
} // bool lp5562_addCommand_Wait(bool prescale, uint8_t stepTime, int atInst)



/*============================================================================*/
/**
 * @brief Add a ramp
 *
 * @param prescale false = 0.49 ms cycle time; true = 15.6 ms cycle time
 *
 * @param stepTime Wait this this many cycles (1 - 63)
 *
 * @param decrease false = step up, true = step down
 *
 * @param numSteps Number of times the PWM is increased by 1.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * Step times vary depending on prescale. With prescale = false, .49 ms to 7.35 ms.
 * With prescale = true, 15.6 ms to 982.8 ms.
 *
 * The starting and ending point of the ramp depend on the current PWM value when you start,
 * when you are incrementing or decrementing, and the number of steps.
 */
/*============================================================================*/
bool lp5562_addCommand_Ramp(bool prescale, uint8_t stepTime, bool decrease, uint8_t numSteps, int atInst)
{
	uint16_t command = 0;

	if (stepTime > 0x3f) {
		stepTime = 0x3f;
	}

	if (prescale) {
		command |= 0b0100000000000000;
	}
	command |= (uint16_t)stepTime << 8;

	if (decrease) {
		command |= 0b0000000010000000;
	}
	command |= (numSteps & 0x7f);

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_Ramp(bool prescale, uint8_t stepTime, bool decrease, uint8_t numSteps, int atInst)


/*============================================================================*/
/**
 * @brief Set a specific PWM level
 *
 * @param level The level (0 = off, 255 = full brightness)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 */
/*============================================================================*/
bool lp5562_addCommand_SetPWM(uint8_t level, int atInst)
{
	uint16_t command = 0b0100000000000000 | level;

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_SetPWM(uint8_t level, int atInst)


/*============================================================================*/
/**
 * @brief Go to start of program (instruction 0)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * This opcode is 0x0000, which is also what the uninitialize program bytes are set to. So
 * as long as your program is 15 or fewer instructions, you don't need to add this to make
 * your program auto-repeat.
 */
/*============================================================================*/
bool lp5562_addCommand_GoToStart(int atInst)
{
	uint16_t command = 0b0000000000000000;

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_GoToStart(int atInst)


/*============================================================================*/
/**
 * @brief Loop and branch
 *
 * @param loopCount The number of times to loop (1 - 63)
 *
 * @param stepNum The step number to go to when looping (0 - 15)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * After loopCount is reached, then the next statement is executed.
 *
 * Loops can be nested for loops larger than 63.
 *
 * One common thing is to put a wait in a loop, which allows you to wait up to 62 seconds.
 */
/*============================================================================*/
bool lp5562_addCommand_Branch(uint8_t loopCount, uint8_t stepNum, int atInst)
{
	if (loopCount > 0x3f) {
		loopCount = 0x3f;
	}
	if (stepNum > 0xf) {
		// Invalid step number
		return false;
	}

	uint16_t command = 0b1010000000000000 | (((uint16_t)loopCount) << 7) | stepNum;

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_Branch(uint8_t loopCount, uint8_t stepNum, int atInst)


/*============================================================================*/
/**
 * @brief End program (instead of repeating)
 *
 * @param generateInterrupt Generate a software interrupt when reached if this parameter is true
 *
 * @param setPWMto0 If true, set the PWM to 0. If false, leave it unchanged.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * This puts the engine into HOLD mode and stops execution of this engine.
 */
/*============================================================================*/
bool lp5562_addCommand_End(bool generateInterrupt, bool setPWMto0, int atInst)
{
	uint16_t command = 0b110000000000;

	if (generateInterrupt) {
		command |= 0b0001000000000000;
	}
	if (setPWMto0) {
		command |= 0b0000100000000000;
	}

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_End(bool generateInterrupt, bool setPWMto0, int atInst)


/*============================================================================*/
/**
 * @brief Send a trigger to other engines. Used to synchronize the three engines.
 *
 * @param engineMask A mask of the engines to send to. Logical OR the values MASK_ENGINE_1,
 * MASK_ENGINE_2, and MASK_ENGINE_3. You will only send to one or two, you should not send to
 * yourself!
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 *
 * When you send a trigger, this instruction will block until the engines you sent to have
 * hit a wait instruction. It will work if they hit the wait before you send, as well.
 */
/*============================================================================*/
bool lp5562_addCommand_TriggerSend(uint8_t engineMask, int atInst)
{
	uint16_t command = 0b1110000000000000;

	command |= (uint16_t)engineMask << 7;

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_TriggerSend(uint8_t engineMask, int atInst)


/*============================================================================*/
/**
 * @brief Wait for a trigger from another engine. Used to synchronize the three engines.
 *
 * @param engineMask A mask of the engines to wait on. MASK_ENGINE_1,
 * MASK_ENGINE_2, and MASK_ENGINE_3 can be logically ORed together. You should not wait on
 * your own engine. In most cases you should have one engine be the trigger sender and wait
 * on the two other engines since you cannot simultaneously send and wait.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15).
 *
 */
/*============================================================================*/
bool lp5562_addCommand_TriggerWait(uint8_t engineMask, int atInst)
{
	uint16_t command = 0b1110000000000000;

	command |= engineMask << 1;

	return lp5562_addCommand(command, atInst);
} // bool lp5562_addCommand_TriggerWait(uint8_t engineMask, int atInst)


/*============================================================================*/
/**
 * @brief Low level addCommand that takes a specific opcode. Normaly you'd use the high leve interface.
 *
 * @param cmd 16-bit program instruction word.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the lp5562_
 */
/*============================================================================*/
bool lp5562_addCommand(uint16_t cmd, int atInst)
{
	if (atInst >= 0 && (size_t)atInst < MAX_INSTRUCTIONS) {
		if ((size_t)atInst >= m1_lp5562_prog.nextInst) {
			m1_lp5562_prog.nextInst = (size_t)atInst + 1;
		}
		m1_lp5562_prog.instructions[atInst] = cmd;
	}
	else {
		if (m1_lp5562_prog.nextInst >= MAX_INSTRUCTIONS) {
			return false;
		}
		m1_lp5562_prog.instructions[m1_lp5562_prog.nextInst++] = cmd;
	}
	return true;
} // bool lp5562_addCommand(uint16_t cmd, int atInst)


/*============================================================================*/
/**
 * @brief Add a delay in milliseconds
 *
 * @param milliseconds The number of milliseconds to delay (1 - 61916).
 *
 * There is no atInst option for this method because depending on the delay, it may add two
 * instructions: a wait (for up to 1000 milliseconds), or a wait and a loop. Since it has
 * a variable number of instructions, it can't be inserted into arbitrary code, only added
 * at the end.
 *
 * When the delay is > 1000 milliseconds, the resolution is 1 second.
 */
/*============================================================================*/
bool lp5562_addDelay(uint32_t milliseconds)
{
	if (milliseconds < 32) {
		// No prescale, no branch
		// 0.49 milliseconds per cycle
		uint8_t steps = (uint8_t) (milliseconds * 2);

		return lp5562_addCommand_Wait(false, steps, -1);
	}
	else
	if (milliseconds <= 1000) {
		// Prescale, no branch
		// 15.6 milliseconds per cycle
		uint8_t steps = (uint8_t) (milliseconds / 16);

		return lp5562_addCommand_Wait(true, steps, -1);
	}
	else
	if (milliseconds <= 63000) {
		// Prescale and branch

		// Each delay is 1 second (63 steps), up to 63 loops
		uint8_t loopCount = (uint8_t) (milliseconds / 1000);

		uint8_t stepNum = lp5562_get_StepNum();

		bool bResult = lp5562_addCommand_Wait(true, 63, -1);
		if (!bResult) {
			return false;
		}

		return lp5562_addCommand_Branch(loopCount, stepNum, -1);
	}
	else {
		// Too long
		return false;
	}
} // bool lp5562_addDelay(uint32_t milliseconds)


/*============================================================================*/
/**
 * @brief Clear the current program
 */
/*============================================================================*/
void lp5562_clear(void)
{
	for(uint8_t ii = 0; ii < MAX_INSTRUCTIONS; ii++) {
		m1_lp5562_prog.instructions[ii] = 0;
	}
	m1_lp5562_prog.nextInst = 0;
} // void lp5562_clear(void)


/*============================================================================*/
/**
 * @brief Get the current step number
 *
 * Use this before you add a new command (like addCommandSetPWM) to remember the step number
 * you are about to write. This can be used to overwrite the instruction using the atInst
 * optional parameter.
 *
 * This is most commonly done so you can modify a program that's run on multiple engines
 * with different PWM values.
 *
 * Also used to get the number of instructions after the last command has been written.
 */
/*============================================================================*/
uint8_t lp5562_get_StepNum(void)
{
	 return m1_lp5562_prog.nextInst;
} // uint8_t lp5562_get_StepNum(void)



/*============================================================================*/
/**
 * @brief Get access to the instruction buffer (16x 16-bit instruction words)
 */
/*============================================================================*/
uint16_t *lp5562_get_Instructions(void)
{
	 return m1_lp5562_prog.instructions;
} // uint16_t *lp5562_get_Instructions(void)



/*============================================================================*/
/**
 * @brief Initialize the LP5562 and set a default LED indicator
 */
/*============================================================================*/
void lp5562_init(void)
{
	// LED   Color Name   Actual Color   Current
	// 1     Red          Red            20mA
	// 2     Green        Green          20mA
	// 3     Blue         Yellow         20mA
	// 4     White        Red            10mA
	m1_lp5562_ctl.addr = LP5562_I2C_ADD;
	m1_lp5562_ctl.redCurrent = 50; // <=> 5mA
	m1_lp5562_ctl.greenCurrent = 50; // <=> 5mA
	m1_lp5562_ctl.blueCurrent = 50; // <=> 5mA
	m1_lp5562_ctl.whiteCurrent = 50; // <=> 5mA
	m1_lp5562_ctl.useExternalOscillator = FALSE;
	m1_lp5562_ctl.useLogarithmicMode = TRUE;
	m1_lp5562_ctl.highFrequencyMode = FALSE;

	m1_lp5562_prog.nextInst = 0;

	i2c_inf.dev_id = I2C_DEVICE_LP5562;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;

	lp5562_withLEDCurrent(5.0, 3.0, 3.0, 20.0); // 0.1mA increment
	lp5562_begin(); // Set up the I2C device and begin running.

#ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE
	lp5562_testPgm1();
	return;
#endif // #ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE

	// Engine 1 = Blink
	// Engine 2 = Fast Blink
	// Engine 3 = Breathe
	//eng1_on_ms, eng1_off_ms, eng2_on_ms, eng2_off_ms, eng3_breath_ms
	lp5562_set_IndicatorMode(300, 700, 30, 30, 63);

	//lp5562_fastblink_on_RGB(uint8_t pwm_rgb, uint8_t on_off_ms);
	//lp5562_Red_on();
	//lp5562_Green_on();
	//lp5562_Blue_on();

	// All blinking
	//lp5562_set_LedMapping_R(REG_LED_MAP_ENGINE_2, 196);
	//lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_2, 8);
	//lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_2, 8);
	// All on full
	//lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_1, 127);
	//lp5562_set_LedMapping_W(REG_LED_MAP_DIRECT, 127);
	//lp5562_setPWM_W(127); // LCD backlight
/*
	// All blinking
	lp5562_set_LedMapping_R(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_1);
	// Fast blink red
	lp5562_set_LedMapping_R(REG_LED_MAP_ENGINE_2);
	// Breathe all
	lp5562_set_LedMapping_R(REG_LED_MAP_ENGINE_3, 196);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_3);
	lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_3);
	lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_3);
	// Off - Blink - Fast Blink - Breathe
	lp5562_set_LedMapping_R(REG_LED_MAP_DIRECT, 0);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_2);
	lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_3);
	// On - Blink - Fast Blink - Breathe
	lp5562_set_LedMapping_R(REG_LED_MAP_DIRECT, 255);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_2);
	lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_3);
	// Dim - Blink - Off - Fast Blink
	lp5562_set_LedMapping_R(REG_LED_MAP_DIRECT, 64);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_1);
	lp5562_set_LedMapping_B(REG_LED_MAP_DIRECT, 0);
	lp5562_set_LedMapping_W(REG_LED_MAP_ENGINE_2);
	// All on full
	lp5562_set_LedMapping_R(REG_LED_MAP_DIRECT, 255);
	lp5562_set_LedMapping_G(REG_LED_MAP_DIRECT, 255);
	lp5562_set_LedMapping_B(REG_LED_MAP_DIRECT, 255);
	lp5562_set_LedMapping_W(REG_LED_MAP_DIRECT, 255);
*/
} // void lp5562_init(void)


/*============================================================================*/
/**
 * @brief Turn on the White LED used as the backlight for the LCD display
 */
/*============================================================================*/
void lp5562_backlight_on(uint8_t brightness)
{
	lp5562_setPWM_W(brightness);
} // void lp5562_backlight_on(uint8_t brightness)



/*============================================================================*/
/**
 * @brief Turn off RGB leds
 */
/*============================================================================*/
void lp5562_all_off_RGB(void)
{
	lp5562_setLedMapping(REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT, REG_LED_MAP_DIRECT);
	lp5562_setPWM_RGB(0, 0, 0);
} // void lp5562_all_off_RGB(void)



/*============================================================================*/
/**
 * @brief Turn on Red led with given PWM level
 */
/*============================================================================*/
void lp5562_led_on_Red(uint8_t pwm)
{
	lp5562_set_LedMapping_R(REG_LED_MAP_DIRECT, pwm);
} // void lp5562_led_on_Red(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on Green led with given PWM
 */
/*============================================================================*/
void lp5562_led_on_Green(uint8_t pwm)
{
	lp5562_set_LedMapping_G(REG_LED_MAP_DIRECT, pwm);
} // void lp5562_led_on_Green(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on Blue led with given PWM
 */
/*============================================================================*/
void lp5562_led_on_Blue(uint8_t pwm)
{
	lp5562_set_LedMapping_B(REG_LED_MAP_DIRECT, pwm);
} // void lp5562_led_on_Blue(uint8_t pwm)



/*============================================================================*/
/**
 * @brief Turn on fast blink for RGB with given PWM
 */
/*============================================================================*/
void lp5562_fastblink_on_RGB(uint8_t pwm_rgb, uint8_t on_off_ms)
{
	lp5562_clearProgram(REG_LED_MAP_ENGINE_2);

	// Normally fast blink
	lp5562_clear();
	lp5562_addCommand_SetPWM(pwm_rgb, -1); // brightness during ON time
	lp5562_addDelay(on_off_ms); // on time
	lp5562_addCommand_SetPWM(0, -1); // off
	lp5562_addDelay(on_off_ms);  // brightness during OFF time
	lp5562_addCommand_GoToStart(-1);
	lp5562_setProgram(REG_LED_MAP_ENGINE_2, lp5562_get_Instructions(), lp5562_get_StepNum(), true);

	lp5562_set_LedMapping_R(REG_LED_MAP_ENGINE_2, pwm_rgb);
	lp5562_set_LedMapping_G(REG_LED_MAP_ENGINE_2, pwm_rgb);
	lp5562_set_LedMapping_B(REG_LED_MAP_ENGINE_2, pwm_rgb);
} // void lp5562_fastblink_on_RGB(uint8_t pwm_rgb, uint8_t on_off_ms)




#ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE

void lp5562_testPgm1(void)
{
	// This is the test program from the datasheet

	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	// Write to address 01h 0001 0000b (configure engine 1 into 'Load program to SRAM' mode)
	i2c_inf.reg_address = REG_OP_MODE;
	i2c_inf.reg_data = 0b00010000; // 0x10
	m1_i2c_hal_trans_req(&i2c_inf);

	// Ramp prescale=0 stepTime=3 sign=0 increment=7f
	i2c_inf.reg_address = 0x10;
	i2c_inf.reg_data = 0b00000011; // 1st ramp command 8 MSB 0x037f
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = 0x11;
	i2c_inf.reg_data = 0b01111111; // 1st ramp command 8 LSB
	m1_i2c_hal_trans_req(&i2c_inf);

	// Wait prescale=1 stepTime=13 sign=0 increment=0 (wait)
	i2c_inf.reg_address = 0x12;
	i2c_inf.reg_data = 0b01001101; // 1st wait command 8 MSB 0x4d00
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = 0x13;
	i2c_inf.reg_data = 0b00000000; // 1st wait command 8 LSB
	m1_i2c_hal_trans_req(&i2c_inf);

	// Ramp prescale=0 stepTime=3 sign=1 increment=7f
	i2c_inf.reg_address = 0x14;
	i2c_inf.reg_data = 0b00000011; // 2nd ramp command 8 MSB 0x03ff
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = 0x15;
	i2c_inf.reg_data = 0b11111111; // 2nd ramp command 8 LSB
	m1_i2c_hal_trans_req(&i2c_inf);

	// Wait prescale=1 stepTime=32 sign=0 increment=0 (wait)
	i2c_inf.reg_address = 0x16;
	i2c_inf.reg_data = 0b01100000; // 2nd wait command 8 MSB 0x6000
	m1_i2c_hal_trans_req(&i2c_inf);
	i2c_inf.reg_address = 0x17;
	i2c_inf.reg_data = 0b00000000; // 2nd wait command 8 LSB
	m1_i2c_hal_trans_req(&i2c_inf);

	// Configure LED controller operation mode to "Run program" in engine 1
	i2c_inf.reg_address = REG_OP_MODE;
	i2c_inf.reg_data = 0b00100000;
	m1_i2c_hal_trans_req(&i2c_inf);

	// Configure program execution mode from "Hold" to "Run" in engine 1
	i2c_inf.reg_address = REG_ENABLE;
	i2c_inf.reg_data = 0b01100000;
	m1_i2c_hal_trans_req(&i2c_inf);

	// Set red, green, and blue LEDs to program 1
	i2c_inf.reg_address = REG_LED_MAP;
	i2c_inf.reg_data = 0b00010101;
	m1_i2c_hal_trans_req(&i2c_inf);
} // void lp5562_testPgm1(void)


void lp5562_testPgm2(void)
{
	// This is the same as testPgm1, but implemented using the LP5562Program class.
	// Clear any existing programs and put the engines into hold mode.
	lp5562_clear_AllPrograms();
	lp5562_addCommand_Ramp(false, 3, false, 0x7F, -1);
	lp5562_addCommand_Wait(true, 13, -1);
	lp5562_addCommand_Ramp(false, 3, true, 0x7F, -1);
	lp5562_addCommand_Wait(true, 32, -1);
	lp5562_setProgram(1, m1_lp5562_prog.instructions, true, false);
	// Set red, green, and blue LEDs to program 1
	lp5562_setLedMapping(REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_1, REG_LED_MAP_DIRECT);
} // void lp5562_testPgm2(void)
#endif // #ifdef M1_DEBUG_LED_DRIVER_TEST_ENABLE
