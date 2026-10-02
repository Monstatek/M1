/*
*
*  m1_lp5562.h
*
* Library for LP5562 RGB led driver
*
*  B.N @ MonstaTek, Nov 2024
*
* Reference: https://github.com/rickkas7/LP5562-RK
*/
#ifndef M1_LP5562_H_
#define M1_LP5562_H_

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "m1_types_def.h"

#define REG_ENABLE 				0x00				//!< Enable register (0x00)
#define REG_ENABLE_LOG_EN 		0x80		//!< The logarithmic mode for PWM brightness when set (instead of linear)
#define REG_ENABLE_CHIP_EN 		0x40		//!< Enable the chip. Power-up default is off. Make sure you set the current before enabling!

	/**
	 * @brief Put the engine in hold mode (stops execution)
	 *
	 * This constant is also passed setEnable() to specify which enable mode you want the engine to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENABLE_HOLD, REG_ENABLE_STEP, REG_ENABLE_RUN, REG_ENABLE_EXEC.
	 */
#define REG_ENABLE_HOLD 		0b00

	/**
	 * @brief Put the engine in single step mode. It will run the current instruction, increment the PC, then hold.
	 *
	 * This constant is also passed setEnable() to specify which enable mode you want the engine to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENABLE_HOLD, REG_ENABLE_STEP, REG_ENABLE_RUN, REG_ENABLE_EXEC.
	 */
#define REG_ENABLE_STEP 		0b01

	/**
	 * @brief Put the engine in run mode. It will run the current code, until the code itself
	 * decides to halt or you change the run mode manually.
	 *
	 * This constant is also passed setEnable() to specify which enable mode you want the engine to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENABLE_HOLD, REG_ENABLE_STEP, REG_ENABLE_RUN, REG_ENABLE_EXEC.
	 */
#define REG_ENABLE_RUN 			0b10


	/**
	 * @brief Put the engine in direct execute mode. It will run the current instruction, then hold.
	 *
	 * This constant is also passed setEnable() to specify which enable mode you want the engine to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENABLE_HOLD, REG_ENABLE_STEP, REG_ENABLE_RUN, REG_ENABLE_EXEC.
	 */
#define REG_ENABLE_EXEC 		0b11

#define REG_OP_MODE 			0x01			//!< Operation mode register (0x01)

	/**
	 * @brief Put the engine in disabled mode, and will no longer run.
	 *
	 * This constant is also passed setOpMode() to specify which operation mode you want it to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENGINE_DISABLED, REG_ENGINE_LOAD, REG_ENGINE_RUN, REG_ENGINE_DIRECT.
	 */
#define REG_ENGINE_DISABLED 	0b00	//!< Put the engine in disabled mode. This bit mask is shifted left depending on which engine you are setting in the op register.

	/**
	 * @brief Put the engine in load mode. Required to load code. Handled automatically by setProgram().
	 *
	 * This constant is also passed setOpMode() to specify which operation mode you want it to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENGINE_DISABLED, REG_ENGINE_LOAD, REG_ENGINE_RUN, REG_ENGINE_DIRECT.
	 */
#define REG_ENGINE_LOAD 		0b01

	/**
	 * @brief Put the engine in run mode.
	 *
	 * This constant is also passed setOpMode() to specify which operation mode you want it to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENGINE_DISABLED, REG_ENGINE_LOAD, REG_ENGINE_RUN, REG_ENGINE_DIRECT.
	 */
#define REG_ENGINE_RUN 			0b10

	/**
	 * @brief Put the engine in direct mode.
	 *
	 * This constant is also passed setOpMode() to specify which operation mode you want it to be in.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * Engine 1: Left shift 4
	 * Engine 2: Left shift 2
	 * Engine 3: No shift
	 *
	 * This is part of a set of mutually exclusive options (for a given engine):
	 * REG_ENGINE_DISABLED, REG_ENGINE_LOAD, REG_ENGINE_RUN, REG_ENGINE_DIRECT.
	 */
#define REG_ENGINE_DIRECT 		0b11		//!< Put the engine in disabled mode. This bit mask is shifted left depending on which engine you are setting in the op register.

#define REG_B_PWM 				0x02				//!< Blue channel PWM direct register (0x02)

#define REG_G_PWM 				0x03				//!< Green channel PWM direct register (0x03)

#define REG_R_PWM 				0x04				//!< Red channel PWM direct register (0x04)

#define REG_B_CURRENT 			0x05			//!< Blue channel current register in 0.1 mA units (0x05)

#define REG_G_CURRENT 			0x06			//!< Green channel current register in 0.1 mA units (0x06)

#define REG_R_CURRENT 			0x07			//!< Red channel current register in 0.1 mA units (0x07)

#define REG_CONFIG 				0x08				//!< Config register
#define REG_CONFIG_HF 			0x40			//!< Config register HF (high frequency mode) enabled bit
#define REG_CONFIG_PS_EN 		0x20		//!< Config register powersave enabled bit
#define REG_CONFIG_CLK_DET_EN 	0x02	//!< Config register clock detect enable bit
#define REG_CONFIG_INT_CLK_EN 	0x01	//!< Config register internal clock bit

#define REG_ENG1_PC 			0x09			//!< Program counter for engine 1 (0 - 15)

#define REG_ENG2_PC 			0x0A			//!< Program counter for engine 2 (0 - 15)

#define REG_ENG3_PC 			0x0B			//!< Program counter for engine 3 (0 - 15)

#define REG_STATUS 				0x0C				//!< Status register
#define REG_STATUS_EXT_CLK_USED 0x08 //!< Status register external clock used bit
#define REG_STATUS_ENG1_INT 	0x04	//!< Status register engine 1 generated an interrupt if bit is set
#define REG_STATUS_ENG2_INT 	0x02	//!< Status register engine 2 generated an interrupt if bit is set
#define REG_STATUS_ENG3_INT 	0x01	//!< Status register engine 3 generated an interrupt if bit is set

#define REG_RESET 				0x0D				//!< Reset register. Write 0xff to clear all register to default values

#define REG_W_PWM 				0x0E				//!< White channel PWM direct register (0x0e)

#define REG_W_CURRENT 			0x0F			//!< Blue channel current register in 0.1 mA units (0x0f)

#define REG_PROGRAM_1 			0x10 			//!< Engine 1 instructions 0x10 to 0x2f (0x20 bytes = 16 16-bit instructions)
#define REG_PROGRAM_2 			0x30 			//!< Engine 2 instructions 0x30 to 0x4f (0x20 bytes = 16 16-bit instructions)
#define REG_PROGRAM_3 			0x50 			//!< Engine 3 instructions 0x50 to 0x6f (0x20 bytes = 16 16-bit instructions)

#define REG_LED_MAP 			0x70			//!< LED mapping engine (direct or assigned to an engine)

	/**
	 * @brief Sets the LED to direct PWM setting mode
	 *
	 * This constant is also passed setLedMapping() to specify how you want to control a specific LED.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * White LED: left shift 6
	 * Red LED: left shift 4
	 * Green LED: left shift 2
	 * Blue LED: don't shift
	 *
	 * This is part of a set of mutually exclusive values: REG_LED_MAP_DIRECT, REG_LED_MAP_ENGINE_1,
	 * REG_LED_MAP_ENGINE_2, and REG_LED_MAP_ENGINE_3.
	 */
#define REG_LED_MAP_DIRECT 		0b00

	/**
	 * @brief Sets the LED to be controlled by engine 1
	 *
	 * This constant is also passed setLedMapping() to specify how you want to control a specific LED.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * White LED: left shift 6
	 * Red LED: left shift 4
	 * Green LED: left shift 2
	 * Blue LED: don't shift
	 *
	 * You can connect multiple LEDs to a single engine. For example, if you wanted to breathe
	 * cyan you could connect blue and green to a single engine that's ramping up and down. Or
	 * connect R, G, and B to have the LED run the program in white.
	 *
	 * This is part of a set of mutually exclusive values: REG_LED_MAP_DIRECT, REG_LED_MAP_ENGINE_1,
	 * REG_LED_MAP_ENGINE_2, and REG_LED_MAP_ENGINE_3.
	 */
#define REG_LED_MAP_ENGINE_1 	0b01

	/**
	 * @brief Sets the LED to be controlled by engine 2
	 *
	 * This constant is also passed setLedMapping() to specify how you want to control a specific LED.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * White LED: left shift 6
	 * Red LED: left shift 4
	 * Green LED: left shift 2
	 * Blue LED: don't shift
	 *
	 * You can connect multiple LEDs to a single engine. For example, if you wanted to breathe
	 * cyan you could connect blue and green to a single engine that's ramping up and down. Or
	 * connect R, G, and B to have the LED run the program in white.
	 *
	 * This is part of a set of mutually exclusive values: REG_LED_MAP_DIRECT, REG_LED_MAP_ENGINE_1,
	 * REG_LED_MAP_ENGINE_2, and REG_LED_MAP_ENGINE_3.
	 */
#define REG_LED_MAP_ENGINE_2 	0b10

	/**
	 * @brief Sets the LED to be controlled by engine 3
	 *
	 * This constant is also passed setLedMapping() to specify how you want to control a specific LED.
	 *
	 * This value is shifted depending on the engine when storing directly in the enable register:
	 *
	 * White LED: left shift 6
	 * Red LED: left shift 4
	 * Green LED: left shift 2
	 * Blue LED: don't shift
	 *
	 * You can connect multiple LEDs to a single engine. For example, if you wanted to breathe
	 * cyan you could connect blue and green to a single engine that's ramping up and down. Or
	 * connect R, G, and B to have the LED run the program in white.
	 *
	 * This is part of a set of mutually exclusive values: REG_LED_MAP_DIRECT, REG_LED_MAP_ENGINE_1,
	 * REG_LED_MAP_ENGINE_2, and REG_LED_MAP_ENGINE_3.
	 */
#define REG_LED_MAP_ENGINE_3 	0b11

	/**
	 * @brief Mask value to pass to set setEnable to change the enable mode for one or more engines at once.
	 *
	 * Logically OR MASK_ENGINE_1 | MASK_ENGINE_2 | MASK_ENGINE_3 as desired for which engines you want.
	 * You can also use MASK_ENGINE_ALL to act on all engines.
	 */
#define MASK_ENGINE_1 			0b001
	/**
	 * @brief Mask value to pass to set setEnable to change the enable mode for one or more engines at once.
	 *
	 * Logically OR MASK_ENGINE_1 | MASK_ENGINE_2 | MASK_ENGINE_3 as desired for which engines you want.
	 * You can also use MASK_ENGINE_ALL to act on all engines.
	 */
#define MASK_ENGINE_2 			0b010
	/**
	 * @brief Mask value to pass to set setEnable to change the enable mode for one or more engines at once.
	 *
	 * Logically OR MASK_ENGINE_1 | MASK_ENGINE_2 | MASK_ENGINE_3 as desired for which engines you want.
	 * You can also use MASK_ENGINE_ALL to act on all engines.
	 */
#define MASK_ENGINE_3 			0b100

	/**
	 * @brief Mask value to pass to set setEnable to change the enable mode for all engines at once.
	 *
	 * The same as MASK_ENGINE_1 | MASK_ENGINE_2 | MASK_ENGINE_3.
	 */
#define MASK_ENGINE_ALL 		0b111

#define MAX_INSTRUCTIONS		16

#define LP5562_I2C_ADD			0x60

#define LP5562_I2C_WR_TIMEOUT	200 // Timeout for I2C Write/Read access in ms


typedef struct {
	/**
	 * @brief The next instruction to write to, or after all have been written, the number
	 * of instructions in this program. Will always be 0 <= nextInst <= MAX_INSTRUCTIONS.
	 */
	uint8_t nextInst; // = 0;

	/**
	 * @brief The array of program instructions. Each instruction is a 16-bit word.
	 */
	uint16_t instructions[MAX_INSTRUCTIONS];
} S_M1_LP5562_PROG;


/**
 * @brief Add a wait command (ramp/wait with increment of 0)
 *
 * @param prescale false = 0.49 ms cycle time; true = 15.6 ms cycle time
 *
 * @param stepTime Wait this this many cycles (1 - 63)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * Wait times vary depending on prescale. With prescale = false, .49 ms to 7.35 ms.
 * With prescale = true, 15.6 ms to 982.8 ms. You can make even longer wait times by putting
 * a wait in a loop. Since a loop can be executed up to 63 times, you can get a 62 second delay.
 */
bool lp5562_addCommand_Wait(bool prescale, uint8_t stepTime, int atInst);

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
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * Step times vary depending on prescale. With prescale = false, .49 ms to 7.35 ms.
 * With prescale = true, 15.6 ms to 982.8 ms.
 *
 * The starting and ending point of the ramp depend on the current PWM value when you start,
 * when you are incrementing or decrementing, and the number of steps.
 */
bool lp5562_addCommand_Ramp(bool prescale, uint8_t stepTime, bool decrease, uint8_t numSteps, int atInst);

/**
 * @brief Set a specific PWM level
 *
 * @param level The level (0 = off, 255 = full brightness)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 */
bool lp5562_addCommand_SetPWM(uint8_t level, int atInst);

/**
 * @brief Go to start of program (instruction 0)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * This opcode is 0x0000, which is also what the uninitialize program bytes are set to. So
 * as long as your program is 15 or fewer instructions, you don't need to add this to make
 * your program auto-repeat.
 */
bool lp5562_addCommand_GoToStart(int atInst);

/**
 * @brief Loop and branch
 *
 * @param loopCount The number of times to loop (1 - 63)
 *
 * @param stepNum The step number to go to when looping (0 - 15)
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * After loopCount is reached, then the next statement is executed.
 *
 * Loops can be nested for loops larger than 63.
 *
 * One common thing is to put a wait in a loop, which allows you to wait up to 62 seconds.
 */
bool lp5562_addCommand_Branch(uint8_t loopCount, uint8_t stepNum, int atInst);

/**
 * @brief End program (instead of repeating)
 *
 * @param generateInterrupt Generate a software interrupt when reached if this parameter is true
 *
 * @param setPWMto0 If true, set the PWM to 0. If false, leave it unchanged.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * This puts the engine into HOLD mode and stops execution of this engine.
 */
bool lp5562_addCommand_End(bool generateInterrupt, bool setPWMto0, int atInst);

/**
 * @brief Send a trigger to other engines. Used to synchronize the three engines.
 *
 * @param engineMask A mask of the engines to send to. Logical OR the values MASK_ENGINE_1,
 * MASK_ENGINE_2, and MASK_ENGINE_3. You will only send to one or two, you should not send to
 * yourself!
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 *
 * When you send a trigger, this instruction will block until the engines you sent to have
 * hit a wait instruction. It will work if they hit the wait before you send, as well.
 */
bool lp5562_addCommand_TriggerSend(uint8_t engineMask, int atInst);

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
bool lp5562_addCommand_TriggerWait(uint8_t engineMask, int atInst);

/**
 * @brief Low level addCommand that takes a specific opcode. Normaly you'd use the high leve interface.
 *
 * @param cmd 16-bit program instruction word.
 *
 * @param atInst (can omit) Normally instructions are added at the current end of the program
 * but you can use the atInst parameter to set a specific instruction (0 - 15) in the program.
 */
bool lp5562_addCommand(uint16_t cmd, int atInst);


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
bool lp5562_addDelay(uint32_t milliseconds);

/**
 * @brief Clear the current program
 */
void lp5562_clear(void);

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
uint8_t lp5562_get_StepNum(void);

/**
 * @brief Get access to the instruction buffer (16x 16-bit instruction words)
 */
uint16_t *lp5562_get_Instructions(void);



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

	/**
	 * @brief Whether to use the internal oscillator (true) or external (false). Default is internal.
	 */
	bool useExternalOscillator; // = false;

	/**
	 * @brief Whether to use logarithmic mode for PWM values (true, default) or linear (false).
	 *
	 * Logarithmic varies the PWM values to their perceived brightness by the human eye.
	 */
	bool useLogarithmicMode; // = true;

	/**
	 * @brief Whether to use low or high frequency for the PWM. Default is low (256 Hz).
	 *
	 * Low frequency (default) is 256 Hz. High frequency is 558 Hz.
	 */
	bool highFrequencyMode; // = false;
} S_M1_LP5562;


/**
 * @brief Sets the LED current
 *
 * @param all The current for all LEDs (R, G, B, and W). You can set specific different currents with
 * the other overload. The default is 5 mA. The range of from 0.1 to 25.5 mA in increments of 0.1 mA.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
void lp5562_withLEDCurrent_all(float all);


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
void lp5562_withLEDCurrent(float red, float green, float blue, float white);

/**
 * @brief Set external oscillator mode. Default is internal.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
void lp5562_withUseExternalOscillator(bool value);

/**
 * @brief Use Logarithmic Mode for PWM brightness. Default = true.
 *
 * This adjusts the PWM value for the perceived brightness from the human eye vs. actual linear values.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
void lp5562_withUseLogarithmicMode(bool value);

/**
 * @brief Enable high frequency PWM. Default = false.
 *
 * Low frequency (default) is 256 Hz. High frequency is 558 Hz.
 *
 * This method returns a LP5562 object so you can chain multiple configuration calls together, fluent-style.
 */
void lp5562_withHighFrequencyMode(bool value);


/**
 * @brief Set up the I2C device and begin running.
 *
 * You cannot do this from STARTUP or global object construction time. It should only be done from setup
 * or loop (once).
 *
 * Make sure you set the LED current using withLEDCurrent() before calling begin if your LED has a
 * current other than the default of 5 mA.
 */
bool lp5562_begin(void);


/**
 * @brief Sets the PWM for the red channel
 *
 * @param red value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
void lp5562_setPWM_R(uint8_t red);

/**
 * @brief Sets the PWM for the green channel
 *
 * @param green value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
void lp5562_setPWM_G(uint8_t green);

/**
 * @brief Sets the PWM for the blue channel
 *
 * @param blue value 0 - 255. 0 = off, 255 = full brightness.
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
void lp5562_setPWM_B(uint8_t blue);


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
void lp5562_setPWM_RGB(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Sets the PWM for the R, G, and B channels.
 *
 * @param rgb Value in the form of 0x00RRGGBB. Each of RR, GG, and BB are from
 * 0x00 (off) to 0xFF (full brightness).
 *
 * If you were previously using a program (setProgram, setBlink, setBlink2, or setBreathe,
 * you must stop the program using useDirectRGB() before you can set the RGB values.
 */
void lp5562_setPWM_all(uint32_t rgb);


/**
 * @brief Sets the W channel to the specified PWM value
 *
 * @param white value 0 - 255. 0 = off, 255 = full brightness.
 */
void lp5562_setPWM_W(uint8_t white);

/**
 * @brief Use direct mode on RGB LED. Changes the LED mapping register and if a program is running
 * on the R, G, or B LEDs, stops it.
 *
 * If you call setProgram() or functions like setBlink(), setBlink2(), or setBreathe() you must
 * call this before calling setRGB() or the program will continue to run and override your manual
 * setting!
 */
void lp5562_useDirect_RGB(void);

/**
 * @brief Use direct mode on W LED. Changes the LED mapping register and if a program is running
 * on the W LED, stops it.
 *
 * If you call setProgram() or functions like setBlink(), setBlink2(), or setBreathe() you must
 * call this before calling setW() or the program will continue to run and override your manual
 * setting!
 */
void lp5562_useDirect_W(void);

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
void lp5562_setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t on_ms, uint32_t off_ms);

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
void lp5562_setBlink_rgb(uint32_t rgb, uint32_t on_ms, uint32_t off_ms);

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
void lp5562_setBlink2(uint8_t red1, uint8_t green1, uint8_t blue1, uint32_t ms1, uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t ms2);

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
void lp5562_setBlink2_rgb(uint32_t rgb1, uint32_t ms1, uint32_t rgb2, uint32_t ms2);

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
void lp5562_setBreathe(bool red, bool green, bool blue, uint8_t stepTimeHalfMs, uint8_t lowLevel, uint8_t highLevel);

/**
 * @brief Set indicator mode
 *
 * Engine 1 = Blink
 * Engine 2 = Fast Blink
 * Engine 3 = Breathe
 */
void lp5562_set_IndicatorMode(uint32_t eng1_on_ms, uint32_t eng1_off_ms, uint32_t eng2_on_ms, uint32_t eng2_off_ms, uint8_t eng3_breath_ms);

/**
 * @brief Set ledMapping to program. Not normally necessary.
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
bool lp5562_setLedMapping(uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

/**
 * @brief Set the led mapping for the red LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
bool lp5562_set_LedMapping_R(uint8_t mode, uint8_t value);


/**
 * @brief Set the led mapping for the green LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
bool lp5562_set_LedMapping_G(uint8_t mode, uint8_t value);

/**
 * @brief Set the led mapping for the blue LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
bool lp5562_set_LedMapping_B(uint8_t mode, uint8_t value);

/**
 * @brief Set the led mapping for the white LED. Typically used in indicator mode to set direct or program mode.
 *
 * @brief mode REG_LED_MAP_DIRECT (direct RGB, default), REG_LED_MAP_ENGINE_1, REG_LED_MAP_ENGINE_2, or REG_LED_MAP_ENGINE_3
 *
 * @brief value If using REG_LED_MAP_DIRECT, the intensity value 0 = off, 255 = full brightness
 */
bool lp5562_set_LedMapping_W(uint8_t mode, uint8_t value);

/**
 * @brief Get the value of the LED mapping register (0x70)
 */
uint8_t lp5562_get_LedMapping(void);

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
bool lp5562_setEnable(uint8_t engineMask, uint8_t engineMode);

/**
 * @brief Get the value of the enable register (0x00)
 */
uint8_t lp5562_getEnable(void);

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
uint8_t lp5562_engineNumToMask(size_t engine);

/**
 * @brief Sets the operation mode register (0x01)
 *
 * @param engine The engine number to set (1, 2, or 3)
 *
 * @param engineMode The mode to set: REG_ENGINE_DISABLED, REG_ENGINE_LOAD (also resets PC), REG_ENGINE_RUN, REG_ENGINE_DIRECT
 */
bool lp5562_setOpMode(size_t engine, uint8_t engineMode);

/**
 * @brief Get the value of the operation mode register (0x01)
 */
uint8_t lp5562_getOpMode(void);

/**
 * @brief Get the value of the status/interrupt register
 *
 * Reading the status/interrupt register will clear any interrupts that are set.
 */
uint8_t lp5562_getStatus(void);

/**
 * @brief Clears a program on the specified engine
 *
 * @param engine An engine number 1 <= engine <= 3
 */
bool lp5562_clearProgram(size_t engine);

/**
 * @brief Clears programs on engines 1, 2, and 3 (all engines)
 */
bool lp5562_clear_AllPrograms(void);

/**
 * @brief Sets a program on a specified engine from a LP5562Program object
 *
 * @param engine An engine number 1 <= engine <= 3
 *
 * @param program The program to set
 *
 * @param startRunning true to start the program running immediately or false to leave it in halt mode
 */
//		bool lp5562_setProgram(size_t engine, const LP5562Program &program, bool startRunning) { return setProgram(engine, program.getInstructions(), program.getStepNum(), startRunning); };

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
bool lp5562_setProgram(size_t engine, const uint16_t *instructions, size_t numInstruction, bool startRunning);

/**
 * @brief Convert a current value in mA to the format used by the LP5562
 *
 * @param value Value in mA
 *
 * @return A uint8_t value in tenths of a mA. For example, passing 5 (mA) will return 50.
 */
uint8_t lp5562_floatToCurrent(float value);

/**
 * @brief Low-level call to read a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 */
uint8_t lp5562_readRegister(uint8_t reg);

/**
 * @brief Low-level call to write a register value
 *
 * @param reg The register to read (0x00 to 0x70)
 *
 * @param value The value to set
 *
 * Note that setProgram bypasses this to write multiple bytes at once, to improve efficiency.
 */
bool lp5562_writeRegister(uint8_t reg, uint8_t value);

void lp5562_init(void);
void lp5562_backlight_on(uint8_t brightness);
void lp5562_all_off_RGB(void);
void lp5562_led_on_Red(uint8_t pwm);
void lp5562_led_on_Green(uint8_t pwm);
void lp5562_led_on_Blue(uint8_t pwm);
void lp5562_fastblink_on_RGB(uint8_t pwm_rgb, uint8_t on_off_ms);

#endif /* M1_LP5562_H_ */
