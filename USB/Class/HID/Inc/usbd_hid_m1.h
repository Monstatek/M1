/**
  ******************************************************************************
  * @file    usbd_hid_m1.h
  * @author  MCD Application Team
  * @brief   Header file for the usbd_hid_m1.c file.
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

/*
 * ============================ MonstaTek modification note ===================
 * Derived from the official STMicroelectronics USB Device Library HID class
 * (stm32_mw_usb_device v2.11.1: Class/HID/{Src/usbd_hid.c, Inc/usbd_hid.h}),
 * adapted by MonstaTek for a USB HID *boot keyboard* foundation:
 *   - the ST mouse report descriptor and mouse-specific interface protocol are
 *     removed and replaced with a standard boot-keyboard report descriptor;
 *   - the interface protocol is set to keyboard (0x01) and the interrupt IN
 *     endpoint size to 8 bytes (boot-keyboard report length);
 *   - the class transport (USBD_HID_SendReport) is kept report-agnostic
 *     (report buffer + explicit length), so report-format specifics live in the
 *     current descriptor and the separate M1 keyboard wrapper, not in the class
 *     machinery. This keeps the class extendable later (report IDs for
 *     keyboard/mouse/consumer) without rewriting the class core.
 * The original ST copyright/license header above is preserved. Like the sibling
 * ST CDC/MSC sources in this repository, this file is distributed under the
 * repository LICENSE (see COPYING.txt / LICENSE).
 * ===========================================================================
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_HID_M1_H
#define __USB_HID_M1_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include  "usbd_ioreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_HID
  * @brief This file is the Header file for usbd_hid_m1.c
  * @{
  */


/** @defgroup USBD_HID_Exported_Defines
  * @{
  */
#ifndef HID_EPIN_ADDR
#define HID_EPIN_ADDR                              0x81U
#endif /* HID_EPIN_ADDR */
/* MonstaTek: boot-keyboard IN report is 8 bytes (was 4 for the ST mouse). */
#define HID_EPIN_SIZE                              0x08U

#define USB_HID_CONFIG_DESC_SIZ                    34U
#define USB_HID_DESC_SIZ                           9U
/* MonstaTek: boot-keyboard report descriptor length (was HID_MOUSE_REPORT_DESC_SIZE). */
#define HID_KEYBOARD_REPORT_DESC_SIZE              63U

#define HID_DESCRIPTOR_TYPE                        0x21U
#define HID_REPORT_DESC                            0x22U

#ifndef HID_HS_BINTERVAL
#define HID_HS_BINTERVAL                           0x07U
#endif /* HID_HS_BINTERVAL */

#ifndef HID_FS_BINTERVAL
#define HID_FS_BINTERVAL                           0x0AU
#endif /* HID_FS_BINTERVAL */

#define USBD_HID_REQ_SET_PROTOCOL                       0x0BU
#define USBD_HID_REQ_GET_PROTOCOL                       0x03U

#define USBD_HID_REQ_SET_IDLE                           0x0AU
#define USBD_HID_REQ_GET_IDLE                           0x02U

#define USBD_HID_REQ_SET_REPORT                         0x09U
#define USBD_HID_REQ_GET_REPORT                         0x01U
/**
  * @}
  */


/** @defgroup USBD_CORE_Exported_TypesDefinitions
  * @{
  */
typedef enum
{
  USBD_HID_IDLE = 0,
  USBD_HID_BUSY,
} USBD_HID_StateTypeDef;


typedef struct
{
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  USBD_HID_StateTypeDef state;
} USBD_HID_HandleTypeDef;

/*
 * HID Class specification version 1.1
 * 6.2.1 HID Descriptor
 */

typedef struct
{
  uint8_t           bLength;
  uint8_t           bDescriptorType;
  uint16_t          bcdHID;
  uint8_t           bCountryCode;
  uint8_t           bNumDescriptors;
  uint8_t           bHIDDescriptorType;
  uint16_t          wItemLength;
} __PACKED USBD_HIDDescTypeDef;

/**
  * @}
  */



/** @defgroup USBD_CORE_Exported_Macros
  * @{
  */

/**
  * @}
  */

/** @defgroup USBD_CORE_Exported_Variables
  * @{
  */

extern USBD_ClassTypeDef USBD_HID;
#define USBD_HID_CLASS &USBD_HID
/**
  * @}
  */

/** @defgroup USB_CORE_Exported_Functions
  * @{
  */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len, uint8_t ClassId);
#else
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
#endif /* USE_USBD_COMPOSITE */
uint32_t USBD_HID_GetPollingInterval(USBD_HandleTypeDef *pdev);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif  /* __USB_HID_M1_H */
/**
  * @}
  */

/**
  * @}
  */
