/* See COPYING.txt for license details. */

/*
*
*  m1_compile_cfg.h
*
*  Directives and defines defined for debug and other purposes
*
* M1 Project
*
*/

#ifndef M1_COMPILE_CFG_H_
#define M1_COMPILE_CFG_H_

#include "cmsis_os.h"

/*============================================================================*/
/*						A P P   D I R E C T I V E S		  					*/
/*============================================================================*/

#define M1_APP_GPIO_OWN_DEFINES // Let use own GPIO defines with better format in main.h, instead of default defines by STM32CubeIDE

//#define M1_APP_BUZZER_USE_TIMER3	// Using Timer3 for buzzer control
#ifndef M1_APP_BUZZER_USE_TIMER3
#define M1_APP_BUZZER_USE_TIMER8	// Using Timer8 for buzzer control
#endif // #define M1_APP_BUZZER_USE_TIMER3

#define M1_APP_RADIO_POLL_CTS_ON_GPIO	// The CTS function is assigned to a GPIO pin of the radio chip

#define M1_APP_ESP_RESPONSE_PRINT_ENABLE // Send the AT command response to UART log port

#define M1_APP_SUB_GHZ_RAW_DATA_RX_NOISE_FILTER_ENABLE	// Ignore rx signal with short pulse width (<M1_APP_SUB_GHZ_RAW_DATA_NOISE_PULSE_WIDTH)
#ifdef M1_APP_SUB_GHZ_RAW_DATA_RX_NOISE_FILTER_ENABLE
#define M1_APP_SUB_GHZ_RAW_DATA_NOISE_PULSE_WIDTH		80 //us
#endif // #ifdef M1_APP_SUB_GHZ_RAW_DATA_RX_NOISE_FILTER_ENABLE

//#define M1_APP_IWDT_IN_DEBUG_MODE_ENABLE // Enable IWDT in debug mode

// Expand the "assert_param" macro in the HAL drivers code
// Defined in stm32h5xx_hal_conf.h, line 196
#ifndef USE_FULL_ASSERT
//#error "USE_FULL_ASSERT must be defined for debug purpose in the file stm32h5xx_hal_conf.h"
/**
  * @brief  The assert_param macro is used for function's parameters check.
  * @param  expr: If expr is false, it calls assert_failed function
  *         which reports the name of the source file and the source
  *         line number of the call that failed.
  *         If expr is true, it returns no value.
  * @retval None
  */
#define USE_FULL_ASSERT
#if defined assert_param
#undef assert_param
#endif // #if defined assert_param
extern void assert_failed();
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#endif // #ifndef USE_FULL_ASSERT

#if (osCMSIS <= 0x20000U)
#error "osCMSIS version newer than 2.0 is recommended!"
#endif // #if (osCMSIS <= 0x20000U)

/*============================================================================*/
/*					D E B U G   D I R E C T I V E S		  					*/
/*============================================================================*/

#define M1_DEBUG_CLI_ENABLE	// Enable the CLI function for debugging and testing

/*============================================================================*/
/*				E N H A N C E D   F I R M W A R E   F L A G S				*/
/*============================================================================*/

#define M1_APP_CRC_EXT_ENABLE          /* Enable extended CRC boot verification */
#define M1_APP_DFU_HWSTRAP_ENABLE      /* Enable PE11 DFU hardware strap */
#define M1_APP_FLIPPER_COMPAT_ENABLE   /* Enable Flipper file format support */
#define M1_APP_CRYPTO_ENABLE           /* Enable WiFi credential encryption */

#endif /* M1_COMPILE_CFG_H_ */
