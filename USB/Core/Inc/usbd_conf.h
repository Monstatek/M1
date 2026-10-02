/**
  ******************************************************************************
  * @file    usbd_conf_template.h
  * @author  MCD Application Team
  * @brief   Header file for the usbd_conf_template.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBD_CONF_TEMPLATE_H
#define __USBD_CONF_TEMPLATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx.h"  /* replace 'stm32xxx' with your HAL driver header filename, ex: stm32f4xx.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_CONF
  * @brief USB device low level driver configuration file
  * @{
  */

  /*
   *  USB Operation Mode Configuration
   */
#define M1_CFG_USB_NONE     0
#define M1_CFG_USB_CDC_MSC  1
#define M1_CFG_USB_CDC      2
#define M1_CFG_USB_MSC      3

#if 1 // Enable CDC+MSC composite mode
#define M1_USB_CONFIG       M1_CFG_USB_CDC_MSC
#elif 0
#define M1_USB_CONFIG       M1_CFG_USB_MSC
#else
#define M1_USB_CONFIG       M1_CFG_USB_CDC
#endif

/** @defgroup USBD_CONF_Exported_Defines
  * @{
  */

/* Common Config */
#if M1_USB_CONFIG == M1_CFG_USB_CDC_MSC
  /*
   * Compisite CDC+MSC
   *
  **/
#define USBD_MAX_NUM_INTERFACES                     5u //1U
#define USBD_MAX_NUM_CONFIGURATION                  1U
#define USBD_MAX_SUPPORTED_CLASS                    2U
#define USBD_MAX_CLASS_ENDPOINTS                    3U
#define USBD_MAX_CLASS_INTERFACES                   2U
#define USBD_MAX_STR_DESC_SIZ                       0x200U
#define USBD_SELF_POWERED                           1U
#define USBD_DEBUG_LEVEL                            0U
#define USBD_SUPPORT_USER_STRING_DESC               2U

/* MSC Class Config */
//#define MSC_MEDIA_PACKET                            512U
#define MSC_MEDIA_PACKET                            (8 * 1024)

/* Activate the IAD option */
#define USBD_COMPOSITE_USE_IAD                      1U

/* Activate the composite builder */
#define USE_USBD_COMPOSITE

/* Activate CDC and MSC classes in composite builder */
#define USBD_CMPSIT_ACTIVATE_CDC                    1U
#define USBD_CMPSIT_ACTIVATE_MSC                    1U

#define MSC_IN_EP                                   0x81U   /* Bulk IN, MSC */
#define MSC_OUT_EP                                  0x01U   /* Bulk OUT, MSC */

#define CDC_IN_EP                                   0x82U   /* Bulk IN, CDC */
#define CDC_OUT_EP                                  0x02U   /* Bulk OUT, CDC */
#define CDC_CMD_EP                                  0x83U   /* Interrupt, CDC commands */

#elif M1_USB_CONFIG == M1_CFG_USB_MSC
  /*
   * MSC only
   *
  **/
#define USBD_MAX_NUM_INTERFACES                     1U
#define USBD_MAX_NUM_CONFIGURATION                  1U
#define USBD_MAX_STR_DESC_SIZ                       0x100U
#define USBD_SELF_POWERED                           1U
#define USBD_DEBUG_LEVEL                            0U

/* MSC Class Config */
//#define MSC_MEDIA_PACKET                            512U
#define MSC_MEDIA_PACKET                            (8 * 1024)

#define MSC_IN_EP                                   0x81U   /* Bulk IN, MSC */
#define MSC_OUT_EP                                  0x01U   /* Bulk OUT, MSC */

// not used
#define CDC_IN_EP                                   0x82U   /* Bulk IN, CDC */
#define CDC_OUT_EP                                  0x02U   /* Bulk OUT, CDC */
#define CDC_CMD_EP                                  0x85U   /* Interrupt, CDC commands */

#elif M1_USB_CONFIG == M1_CFG_USB_CDC
  /*
   * CDC only
   *
  **/
#define USBD_MAX_NUM_INTERFACES                     2U
#define USBD_MAX_NUM_CONFIGURATION                  1U
#define USBD_MAX_STR_DESC_SIZ                       0x100U
#define USBD_SELF_POWERED                           1U
#define USBD_DEBUG_LEVEL                            0U

#define CDC_IN_EP                                   0x81U   /* Bulk IN, CDC */
#define CDC_OUT_EP                                  0x01U   /* Bulk OUT, CDC */
#define CDC_CMD_EP                                  0x82U   /* Interrupt, CDC commands */

#endif

///* ECM, RNDIS, DFU Class Config */
//#define USBD_SUPPORT_USER_STRING_DESC               0U

/* BillBoard Class Config */
#define USBD_CLASS_USER_STRING_DESC                 0U
#define USBD_CLASS_BOS_ENABLED                      0U
#define USB_BB_MAX_NUM_ALT_MODE                     0x2U

/* CDC Class Config */
#define USBD_CDC_INTERVAL                           2000U

/** @defgroup USBD_Exported_Macros
  * @{
  */
/* Memory management macros make sure to use static memory allocation */
/** Alias for memory allocation. */
#define USBD_malloc         (void *)USBD_static_malloc

/** Alias for memory release. */
#define USBD_free           USBD_static_free

/** Alias for memory set. */
#define USBD_memset         memset

/** Alias for memory copy. */
#define USBD_memcpy         memcpy

/** Alias for delay. */
#define USBD_Delay          HAL_Delay

/* DEBUG macros */
#if (USBD_DEBUG_LEVEL > 0U)
#define  USBD_UsrLog(...)   do { \
                                 printf(__VA_ARGS__); \
                                 printf("\n"); \
                               } while (0)
#else
#define USBD_UsrLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 0U) */

#if (USBD_DEBUG_LEVEL > 1U)

#define  USBD_ErrLog(...) do { \
                               printf("ERROR: ") ; \
                               printf(__VA_ARGS__); \
                               printf("\n"); \
                             } while (0)
#else
#define USBD_ErrLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 1U) */

#if (USBD_DEBUG_LEVEL > 2U)
#define  USBD_DbgLog(...)   do { \
                                 printf("DEBUG : ") ; \
                                 printf(__VA_ARGS__); \
                                 printf("\n"); \
                               } while (0)
#else
#define USBD_DbgLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 2U) */

/**
  * @}
  */



/**
  * @}
  */


/** @defgroup USBD_CONF_Exported_Types
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_CONF_Exported_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBD_CONF_Exported_Variables
  * @{
  */
/**
  * @}
  */

/** @defgroup USBD_CONF_Exported_FunctionsPrototype
  * @{
  */
/* Exported functions -------------------------------------------------------*/
void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);
/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_TEMPLATE_H */


/**
  * @}
  */

/**
  * @}
  */
