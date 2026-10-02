/**
  ******************************************************************************
  * @file    logger.h
  * @brief   Header for logger module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "rfal_platform.h"

#if (USE_LOGGER == LOGGER_OFF && !defined(HAL_UART_MODULE_ENABLED))
  #define UART_HandleTypeDef void
#endif
/** @addtogroup X-CUBE-NFC6_Applications
 *  @{
 */

/** @addtogroup PollingTagDetect
 *  @{
 */

/** @defgroup PTD_Logger
 *  @brief Driver providing printf-like functions to output log messages.
 * @{
 */

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/** @defgroup PTD_Logger_Exported_Constants
 *  @{
 */
#define LOGGER_ON   1                    /*!< Allows activating logger    */
#define LOGGER_OFF  0                    /*!< Allows deactivating logger  */
/**
  * @}
  */

/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
/** @defgroup PTD_Logger_Exported_Functions
 *  @{
 */

/*!
 *****************************************************************************
 *  \brief  helper to convert hex data into formated string
 *
 *  \param[in] data : pointer to buffer to be dumped.
 *
 *  \param[in] dataLen : buffer length
 *
 *  \return hex formated string
 *
 *****************************************************************************
 */
extern char* hex2Str(unsigned char * data, size_t dataLen);

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */

