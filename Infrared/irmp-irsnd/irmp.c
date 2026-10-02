/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * irmp.c - infrared multi-protocol decoder, supports several remote control protocols
 *
 * Copyright (c) 2009-2020 Frank Meyer - frank(at)fli4l.de
 *
 * Supported AVR mikrocontrollers:
 *
 * ATtiny87,  ATtiny167
 * ATtiny45,  ATtiny85
 * ATtiny44,  ATtiny84
 * ATmega8,   ATmega16,  ATmega32
 * ATmega162
 * ATmega164, ATmega324, ATmega644,  ATmega644P, ATmega1284, ATmega1284P
 * ATmega88,  ATmega88P, ATmega168,  ATmega168P, ATmega328P
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#include "irmp.h"
#include "m1_log_debug.h"
#include "m1_infrared.h"
#include "app_freertos.h"
#include "m1_tasks.h"

#define M1_LOGDB_TAG	"IRRED_RX"

#if IRMP_SUPPORT_GRUNDIG_PROTOCOL==1 || IRMP_SUPPORT_NOKIA_PROTOCOL==1 || IRMP_SUPPORT_IR60_PROTOCOL==1
#  define IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL  1
#else
#  define IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL  0
#endif

#if IRMP_SUPPORT_SIEMENS_PROTOCOL==1 || IRMP_SUPPORT_RUWIDO_PROTOCOL==1
#  define IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL   1
#else
#  define IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL   0
#endif

#if IRMP_SUPPORT_RC5_PROTOCOL==1 ||                   \
    IRMP_SUPPORT_RCII_PROTOCOL==1 ||                  \
    IRMP_SUPPORT_S100_PROTOCOL==1 ||                  \
    IRMP_SUPPORT_RC6_PROTOCOL==1 ||                   \
    IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1 ||    \
    IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1 ||     \
    IRMP_SUPPORT_IR60_PROTOCOL==1 ||                  \
    IRMP_SUPPORT_A1TVBOX_PROTOCOL==1 ||               \
    IRMP_SUPPORT_MERLIN_PROTOCOL==1 ||                \
    IRMP_SUPPORT_ORTEK_PROTOCOL==1
#  define IRMP_SUPPORT_MANCHESTER                   1
#else
#  define IRMP_SUPPORT_MANCHESTER                   0
#endif

#if IRMP_SUPPORT_NETBOX_PROTOCOL==1
#  define IRMP_SUPPORT_SERIAL                       1
#else
#  define IRMP_SUPPORT_SERIAL                       0
#endif

#define IRMP_KEY_REPETITION_LEN                 (uint32_t)(150000)           // autodetect key repetition within 150 msec

#define MIN_TOLERANCE_00                        1.0                           // -0%
#define MAX_TOLERANCE_00                        1.0                           // +0%

#define MIN_TOLERANCE_02                        0.98                          // -2%
#define MAX_TOLERANCE_02                        1.02                          // +2%

#define MIN_TOLERANCE_03                        0.97                          // -3%
#define MAX_TOLERANCE_03                        1.03                          // +3%

#define MIN_TOLERANCE_05                        0.95                          // -5%
#define MAX_TOLERANCE_05                        1.05                          // +5%

#define MIN_TOLERANCE_10                        0.9                           // -10%
#define MAX_TOLERANCE_10                        1.1                           // +10%

#define MIN_TOLERANCE_15                        0.85                          // -15%
#define MAX_TOLERANCE_15                        1.15                          // +15%

#define MIN_TOLERANCE_20                        0.8                           // -20%
#define MAX_TOLERANCE_20                        1.2                           // +20%

#define MIN_TOLERANCE_25                        0.75                          // -25%
#define MAX_TOLERANCE_25                        1.25                          // +25%

#define MIN_TOLERANCE_30                        0.7                           // -30%
#define MAX_TOLERANCE_30                        1.3                           // +30%

#define MIN_TOLERANCE_40                        0.6                           // -40%
#define MAX_TOLERANCE_40                        1.4                           // +40%

#define MIN_TOLERANCE_50                        0.5                           // -50%
#define MAX_TOLERANCE_50                        1.5                           // +50%

#define MIN_TOLERANCE_60                        0.4                           // -60%
#define MAX_TOLERANCE_60                        1.6                           // +60%

#define MIN_TOLERANCE_70                        0.3                           // -70%
#define MAX_TOLERANCE_70                        1.7                           // +70%

#define SIRCS_START_BIT_PULSE_LEN_MIN           ((uint16_t)(SIRCS_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SIRCS_START_BIT_PULSE_LEN_MAX           ((uint16_t)(SIRCS_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SIRCS_START_BIT_PAUSE_LEN_MIN           ((uint16_t)(SIRCS_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#if IRMP_SUPPORT_NETBOX_PROTOCOL                // only 5% to avoid conflict with NETBOX:
#  define SIRCS_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(SIRCS_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5))
#else                                           // only 5% + 1 to avoid conflict with RC6:
#  define SIRCS_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(SIRCS_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#endif // #if IRMP_SUPPORT_NETBOX_PROTOCOL
#define SIRCS_1_PULSE_LEN_MIN                   ((uint16_t)(SIRCS_1_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SIRCS_1_PULSE_LEN_MAX                   ((uint16_t)(SIRCS_1_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SIRCS_0_PULSE_LEN_MIN                   ((uint16_t)(SIRCS_0_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SIRCS_0_PULSE_LEN_MAX                   ((uint16_t)(SIRCS_0_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SIRCS_PAUSE_LEN_MIN                     ((uint16_t)(SIRCS_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SIRCS_PAUSE_LEN_MAX                     ((uint16_t)(SIRCS_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define NEC_START_BIT_PULSE_LEN_MIN             ((uint16_t)(NEC_START_BIT_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_START_BIT_PULSE_LEN_MAX             ((uint16_t)(NEC_START_BIT_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define NEC_START_BIT_PAUSE_LEN_MIN             ((uint16_t)(NEC_START_BIT_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_START_BIT_PAUSE_LEN_MAX             ((uint16_t)(NEC_START_BIT_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define NEC_REPEAT_START_BIT_PAUSE_LEN_MIN      ((uint16_t)(NEC_REPEAT_START_BIT_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_REPEAT_START_BIT_PAUSE_LEN_MAX      ((uint16_t)(NEC_REPEAT_START_BIT_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define NEC_PULSE_LEN_MIN                       ((uint16_t)(NEC_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_PULSE_LEN_MAX                       ((uint16_t)(NEC_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define NEC_1_PAUSE_LEN_MIN                     ((uint16_t)(NEC_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_1_PAUSE_LEN_MAX                     ((uint16_t)(NEC_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define NEC_0_PAUSE_LEN_MIN                     ((uint16_t)(NEC_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define NEC_0_PAUSE_LEN_MAX                     ((uint16_t)(NEC_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
// autodetect nec repetition frame within 50 msec:
// NEC seems to send the first repetition frame after 40ms, further repetition frames after 100 ms
#if 0
#define NEC_FRAME_REPEAT_PAUSE_LEN_MAX          (uint16_t)(NEC_FRAME_REPEAT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5)
#else
#define NEC_FRAME_REPEAT_PAUSE_LEN_MAX          (uint16_t)(100.0 * MAX_TOLERANCE_20 + 0.5)
#endif

#define MELINERA_0_PULSE_LEN_MIN                ((uint16_t)(MELINERA_0_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MELINERA_0_PULSE_LEN_MAX                ((uint16_t)(MELINERA_0_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define MELINERA_0_PAUSE_LEN_MIN                ((uint16_t)(MELINERA_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MELINERA_0_PAUSE_LEN_MAX                ((uint16_t)(MELINERA_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define MELINERA_1_PULSE_LEN_MIN                ((uint16_t)(MELINERA_1_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MELINERA_1_PULSE_LEN_MAX                ((uint16_t)(MELINERA_1_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define MELINERA_1_PAUSE_LEN_MIN                ((uint16_t)(MELINERA_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MELINERA_1_PAUSE_LEN_MAX                ((uint16_t)(MELINERA_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define SAMSUNG_START_BIT_PULSE_LEN_MIN         ((uint16_t)(SAMSUNG_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SAMSUNG_START_BIT_PULSE_LEN_MAX         ((uint16_t)(SAMSUNG_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SAMSUNG_START_BIT_PAUSE_LEN_MIN         ((uint16_t)(SAMSUNG_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SAMSUNG_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(SAMSUNG_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SAMSUNG_PULSE_LEN_MIN                   ((uint16_t)(SAMSUNG_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNG_PULSE_LEN_MAX                   ((uint16_t)(SAMSUNG_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define SAMSUNG_1_PAUSE_LEN_MIN                 ((uint16_t)(SAMSUNG_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNG_1_PAUSE_LEN_MAX                 ((uint16_t)(SAMSUNG_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define SAMSUNG_0_PAUSE_LEN_MIN                 ((uint16_t)(SAMSUNG_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNG_0_PAUSE_LEN_MAX                 ((uint16_t)(SAMSUNG_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define SAMSUNGAH_START_BIT_PULSE_LEN_MIN       ((uint16_t)(SAMSUNGAH_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SAMSUNGAH_START_BIT_PULSE_LEN_MAX       ((uint16_t)(SAMSUNGAH_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SAMSUNGAH_START_BIT_PAUSE_LEN_MIN       ((uint16_t)(SAMSUNGAH_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define SAMSUNGAH_START_BIT_PAUSE_LEN_MAX       ((uint16_t)(SAMSUNGAH_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define SAMSUNGAH_PULSE_LEN_MIN                 ((uint16_t)(SAMSUNGAH_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNGAH_PULSE_LEN_MAX                 ((uint16_t)(SAMSUNGAH_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define SAMSUNGAH_1_PAUSE_LEN_MIN               ((uint16_t)(SAMSUNGAH_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNGAH_1_PAUSE_LEN_MAX               ((uint16_t)(SAMSUNGAH_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define SAMSUNGAH_0_PAUSE_LEN_MIN               ((uint16_t)(SAMSUNGAH_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define SAMSUNGAH_0_PAUSE_LEN_MAX               ((uint16_t)(SAMSUNGAH_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define MATSUSHITA_START_BIT_PULSE_LEN_MIN      ((uint16_t)(MATSUSHITA_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define MATSUSHITA_START_BIT_PULSE_LEN_MAX      ((uint16_t)(MATSUSHITA_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define MATSUSHITA_START_BIT_PAUSE_LEN_MIN      ((uint16_t)(MATSUSHITA_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define MATSUSHITA_START_BIT_PAUSE_LEN_MAX      ((uint16_t)(MATSUSHITA_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define MATSUSHITA_PULSE_LEN_MIN                ((uint16_t)(MATSUSHITA_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define MATSUSHITA_PULSE_LEN_MAX                ((uint16_t)(MATSUSHITA_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define MATSUSHITA_1_PAUSE_LEN_MIN              ((uint16_t)(MATSUSHITA_1_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define MATSUSHITA_1_PAUSE_LEN_MAX              ((uint16_t)(MATSUSHITA_1_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define MATSUSHITA_0_PAUSE_LEN_MIN              ((uint16_t)(MATSUSHITA_0_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define MATSUSHITA_0_PAUSE_LEN_MAX              ((uint16_t)(MATSUSHITA_0_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)

#define KASEIKYO_START_BIT_PULSE_LEN_MIN        ((uint16_t)(KASEIKYO_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define KASEIKYO_START_BIT_PULSE_LEN_MAX        ((uint16_t)(KASEIKYO_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define KASEIKYO_START_BIT_PAUSE_LEN_MIN        ((uint16_t)(KASEIKYO_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define KASEIKYO_START_BIT_PAUSE_LEN_MAX        ((uint16_t)(KASEIKYO_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define KASEIKYO_PULSE_LEN_MIN                  ((uint16_t)(KASEIKYO_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define KASEIKYO_PULSE_LEN_MAX                  ((uint16_t)(KASEIKYO_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define KASEIKYO_1_PAUSE_LEN_MIN                ((uint16_t)(KASEIKYO_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define KASEIKYO_1_PAUSE_LEN_MAX                ((uint16_t)(KASEIKYO_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define KASEIKYO_0_PAUSE_LEN_MIN                ((uint16_t)(KASEIKYO_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define KASEIKYO_0_PAUSE_LEN_MAX                ((uint16_t)(KASEIKYO_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define MITSU_HEAVY_START_BIT_PULSE_LEN_MIN     ((uint16_t)(MITSU_HEAVY_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define MITSU_HEAVY_START_BIT_PULSE_LEN_MAX     ((uint16_t)(MITSU_HEAVY_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define MITSU_HEAVY_START_BIT_PAUSE_LEN_MIN     ((uint16_t)(MITSU_HEAVY_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define MITSU_HEAVY_START_BIT_PAUSE_LEN_MAX     ((uint16_t)(MITSU_HEAVY_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define MITSU_HEAVY_PULSE_LEN_MIN               ((uint16_t)(MITSU_HEAVY_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define MITSU_HEAVY_PULSE_LEN_MAX               ((uint16_t)(MITSU_HEAVY_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define MITSU_HEAVY_1_PAUSE_LEN_MIN             ((uint16_t)(MITSU_HEAVY_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define MITSU_HEAVY_1_PAUSE_LEN_MAX             ((uint16_t)(MITSU_HEAVY_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define MITSU_HEAVY_0_PAUSE_LEN_MIN             ((uint16_t)(MITSU_HEAVY_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define MITSU_HEAVY_0_PAUSE_LEN_MAX             ((uint16_t)(MITSU_HEAVY_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define VINCENT_START_BIT_PULSE_LEN_MIN         ((uint16_t)(VINCENT_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define VINCENT_START_BIT_PULSE_LEN_MAX         ((uint16_t)(VINCENT_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define VINCENT_START_BIT_PAUSE_LEN_MIN         ((uint16_t)(VINCENT_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define VINCENT_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(VINCENT_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define VINCENT_PULSE_LEN_MIN                   ((uint16_t)(VINCENT_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define VINCENT_PULSE_LEN_MAX                   ((uint16_t)(VINCENT_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define VINCENT_1_PAUSE_LEN_MIN                 ((uint16_t)(VINCENT_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define VINCENT_1_PAUSE_LEN_MAX                 ((uint16_t)(VINCENT_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define VINCENT_0_PAUSE_LEN_MIN                 ((uint16_t)(VINCENT_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define VINCENT_0_PAUSE_LEN_MAX                 ((uint16_t)(VINCENT_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define PANASONIC_START_BIT_PULSE_LEN_MIN       ((uint16_t)(PANASONIC_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define PANASONIC_START_BIT_PULSE_LEN_MAX       ((uint16_t)(PANASONIC_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define PANASONIC_START_BIT_PAUSE_LEN_MIN       ((uint16_t)(PANASONIC_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define PANASONIC_START_BIT_PAUSE_LEN_MAX       ((uint16_t)(PANASONIC_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define PANASONIC_PULSE_LEN_MIN                 ((uint16_t)(PANASONIC_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define PANASONIC_PULSE_LEN_MAX                 ((uint16_t)(PANASONIC_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define PANASONIC_1_PAUSE_LEN_MIN               ((uint16_t)(PANASONIC_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PANASONIC_1_PAUSE_LEN_MAX               ((uint16_t)(PANASONIC_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define PANASONIC_0_PAUSE_LEN_MIN               ((uint16_t)(PANASONIC_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PANASONIC_0_PAUSE_LEN_MAX               ((uint16_t)(PANASONIC_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define RECS80_START_BIT_PULSE_LEN_MIN          ((uint16_t)(RECS80_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define RECS80_START_BIT_PULSE_LEN_MAX          ((uint16_t)(RECS80_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define RECS80_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(RECS80_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(RECS80_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RECS80_PULSE_LEN_MIN                    ((uint16_t)(RECS80_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define RECS80_PULSE_LEN_MAX                    ((uint16_t)(RECS80_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define RECS80_1_PAUSE_LEN_MIN                  ((uint16_t)(RECS80_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80_1_PAUSE_LEN_MAX                  ((uint16_t)(RECS80_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RECS80_0_PAUSE_LEN_MIN                  ((uint16_t)(RECS80_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80_0_PAUSE_LEN_MAX                  ((uint16_t)(RECS80_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#if IRMP_SUPPORT_BOSE_PROTOCOL==1 // BOSE conflicts with RC5, so keep tolerance for RC5 minimal here:
#define RC5_START_BIT_LEN_MIN                   ((uint16_t)(RC5_BIT_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RC5_START_BIT_LEN_MAX                   ((uint16_t)(RC5_BIT_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#else
#define RC5_START_BIT_LEN_MIN                   ((uint16_t)(RC5_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC5_START_BIT_LEN_MAX                   ((uint16_t)(RC5_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#endif

#define RC5_BIT_LEN_MIN                         ((uint16_t)(RC5_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC5_BIT_LEN_MAX                         ((uint16_t)(RC5_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define RCII_START_BIT_PULSE_LEN_MIN            ((uint16_t)(RCII_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCII_START_BIT_PULSE_LEN_MAX            ((uint16_t)(RCII_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCII_START_BIT_PAUSE_LEN_MIN            ((uint16_t)(RCII_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCII_START_BIT_PAUSE_LEN_MAX            ((uint16_t)(RCII_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCII_START_BIT2_PULSE_LEN_MIN           ((uint16_t)(RCII_START_BIT2_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCII_START_BIT2_PULSE_LEN_MAX           ((uint16_t)(RCII_START_BIT2_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)

#define RCII_BIT_LEN_MIN                        ((uint16_t)(RCII_BIT_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RCII_BIT_LEN                            ((uint16_t)(RCII_BIT_TIME))
#define RCII_BIT_LEN_MAX                        ((uint16_t)(RCII_BIT_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#if IRMP_SUPPORT_BOSE_PROTOCOL==1 // BOSE conflicts with S100, so keep tolerance for S100 minimal here:
#define S100_START_BIT_LEN_MIN                   ((uint16_t)(S100_BIT_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define S100_START_BIT_LEN_MAX                   ((uint16_t)(S100_BIT_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#else
#define S100_START_BIT_LEN_MIN                   ((uint16_t)(S100_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define S100_START_BIT_LEN_MAX                   ((uint16_t)(S100_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#endif

#define S100_BIT_LEN_MIN                         ((uint16_t)(S100_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define S100_BIT_LEN_MAX                         ((uint16_t)(S100_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define DENON_PULSE_LEN_MIN                     ((uint16_t)(DENON_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define DENON_PULSE_LEN_MAX                     ((uint16_t)(DENON_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define DENON_1_PAUSE_LEN_MIN                   ((uint16_t)(DENON_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define DENON_1_PAUSE_LEN_MAX                   ((uint16_t)(DENON_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
// RUWIDO (see t-home-mediareceiver-15kHz.txt) conflicts here with DENON
#define DENON_0_PAUSE_LEN_MIN                   ((uint16_t)(DENON_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define DENON_0_PAUSE_LEN_MAX                   ((uint16_t)(DENON_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define DENON_AUTO_REPETITION_PAUSE_LEN         ((uint16_t)(DENON_AUTO_REPETITION_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define THOMSON_PULSE_LEN_MIN                   ((uint16_t)(THOMSON_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define THOMSON_PULSE_LEN_MAX                   ((uint16_t)(THOMSON_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define THOMSON_1_PAUSE_LEN_MIN                 ((uint16_t)(THOMSON_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define THOMSON_1_PAUSE_LEN_MAX                 ((uint16_t)(THOMSON_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define THOMSON_0_PAUSE_LEN_MIN                 ((uint16_t)(THOMSON_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define THOMSON_0_PAUSE_LEN_MAX                 ((uint16_t)(THOMSON_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define RC6_START_BIT_PULSE_LEN_MIN             ((uint16_t)(RC6_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC6_START_BIT_PULSE_LEN_MAX             ((uint16_t)(RC6_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RC6_START_BIT_PAUSE_LEN_MIN             ((uint16_t)(RC6_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC6_START_BIT_PAUSE_LEN_MAX             ((uint16_t)(RC6_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RC6_TOGGLE_BIT_LEN_MIN                  ((uint16_t)(RC6_TOGGLE_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC6_TOGGLE_BIT_LEN_MAX                  ((uint16_t)(RC6_TOGGLE_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RC6_BIT_PULSE_LEN_MIN                   ((uint16_t)(RC6_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC6_BIT_PULSE_LEN_MAX                   ((uint16_t)(RC6_BIT_TIME * MAX_TOLERANCE_60 + 0.5) + 1)       // pulses: 300 - 800
#define RC6_BIT_PAUSE_LEN_MIN                   ((uint16_t)(RC6_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RC6_BIT_PAUSE_LEN_MAX                   ((uint16_t)(RC6_BIT_TIME * MAX_TOLERANCE_20 + 0.5) + 1)       // pauses: 300 - 600

#define RECS80EXT_START_BIT_PULSE_LEN_MIN       ((uint16_t)(RECS80EXT_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define RECS80EXT_START_BIT_PULSE_LEN_MAX       ((uint16_t)(RECS80EXT_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define RECS80EXT_START_BIT_PAUSE_LEN_MIN       ((uint16_t)(RECS80EXT_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80EXT_START_BIT_PAUSE_LEN_MAX       ((uint16_t)(RECS80EXT_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RECS80EXT_PULSE_LEN_MIN                 ((uint16_t)(RECS80EXT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define RECS80EXT_PULSE_LEN_MAX                 ((uint16_t)(RECS80EXT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define RECS80EXT_1_PAUSE_LEN_MIN               ((uint16_t)(RECS80EXT_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80EXT_1_PAUSE_LEN_MAX               ((uint16_t)(RECS80EXT_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RECS80EXT_0_PAUSE_LEN_MIN               ((uint16_t)(RECS80EXT_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RECS80EXT_0_PAUSE_LEN_MAX               ((uint16_t)(RECS80EXT_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define NUBERT_START_BIT_PULSE_LEN_MIN          ((uint16_t)(NUBERT_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_START_BIT_PULSE_LEN_MAX          ((uint16_t)(NUBERT_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NUBERT_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(NUBERT_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(NUBERT_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NUBERT_1_PULSE_LEN_MIN                  ((uint16_t)(NUBERT_1_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_1_PULSE_LEN_MAX                  ((uint16_t)(NUBERT_1_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NUBERT_1_PAUSE_LEN_MIN                  ((uint16_t)(NUBERT_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_1_PAUSE_LEN_MAX                  ((uint16_t)(NUBERT_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NUBERT_0_PULSE_LEN_MIN                  ((uint16_t)(NUBERT_0_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_0_PULSE_LEN_MAX                  ((uint16_t)(NUBERT_0_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NUBERT_0_PAUSE_LEN_MIN                  ((uint16_t)(NUBERT_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NUBERT_0_PAUSE_LEN_MAX                  ((uint16_t)(NUBERT_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define FAN_START_BIT_PULSE_LEN_MIN             ((uint16_t)(FAN_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_START_BIT_PULSE_LEN_MAX             ((uint16_t)(FAN_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define FAN_START_BIT_PAUSE_LEN_MIN             ((uint16_t)(FAN_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_START_BIT_PAUSE_LEN_MAX             ((uint16_t)(FAN_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define FAN_1_PULSE_LEN_MIN                     ((uint16_t)(FAN_1_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_1_PULSE_LEN_MAX                     ((uint16_t)(FAN_1_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define FAN_1_PAUSE_LEN_MIN                     ((uint16_t)(FAN_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_1_PAUSE_LEN_MAX                     ((uint16_t)(FAN_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define FAN_0_PULSE_LEN_MIN                     ((uint16_t)(FAN_0_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_0_PULSE_LEN_MAX                     ((uint16_t)(FAN_0_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define FAN_0_PAUSE_LEN_MIN                     ((uint16_t)(FAN_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FAN_0_PAUSE_LEN_MAX                     ((uint16_t)(FAN_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define SPEAKER_START_BIT_PULSE_LEN_MIN         ((uint16_t)(SPEAKER_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_START_BIT_PULSE_LEN_MAX         ((uint16_t)(SPEAKER_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SPEAKER_START_BIT_PAUSE_LEN_MIN         ((uint16_t)(SPEAKER_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(SPEAKER_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SPEAKER_1_PULSE_LEN_MIN                 ((uint16_t)(SPEAKER_1_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_1_PULSE_LEN_MAX                 ((uint16_t)(SPEAKER_1_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SPEAKER_1_PAUSE_LEN_MIN                 ((uint16_t)(SPEAKER_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_1_PAUSE_LEN_MAX                 ((uint16_t)(SPEAKER_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SPEAKER_0_PULSE_LEN_MIN                 ((uint16_t)(SPEAKER_0_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_0_PULSE_LEN_MAX                 ((uint16_t)(SPEAKER_0_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SPEAKER_0_PAUSE_LEN_MIN                 ((uint16_t)(SPEAKER_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define SPEAKER_0_PAUSE_LEN_MAX                 ((uint16_t)(SPEAKER_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define BANG_OLUFSEN_START_BIT1_PULSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT1_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT1_PULSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT1_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT2_PULSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT2_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT2_PULSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT2_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT2_PAUSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT2_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT2_PAUSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT2_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT3_PULSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT3_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT3_PULSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT3_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT3_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MAX   ((PAUSE_LEN)(BANG_OLUFSEN_START_BIT3_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1) // value must be below IRMP_TIMEOUT
#define BANG_OLUFSEN_START_BIT4_PULSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT4_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT4_PULSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT4_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_START_BIT4_PAUSE_LEN_MIN   ((uint16_t)(BANG_OLUFSEN_START_BIT4_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_START_BIT4_PAUSE_LEN_MAX   ((uint16_t)(BANG_OLUFSEN_START_BIT4_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_PULSE_LEN_MIN              ((uint16_t)(BANG_OLUFSEN_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_PULSE_LEN_MAX              ((uint16_t)(BANG_OLUFSEN_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_1_PAUSE_LEN_MIN            ((uint16_t)(BANG_OLUFSEN_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_1_PAUSE_LEN_MAX            ((uint16_t)(BANG_OLUFSEN_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_0_PAUSE_LEN_MIN            ((uint16_t)(BANG_OLUFSEN_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_0_PAUSE_LEN_MAX            ((uint16_t)(BANG_OLUFSEN_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_R_PAUSE_LEN_MIN            ((uint16_t)(BANG_OLUFSEN_R_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_R_PAUSE_LEN_MAX            ((uint16_t)(BANG_OLUFSEN_R_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN_MIN  ((uint16_t)(BANG_OLUFSEN_TRAILER_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN_MAX  ((uint16_t)(BANG_OLUFSEN_TRAILER_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define IR60_TIMEOUT_LEN                        ((uint16_t)(IR60_TIMEOUT_TIME * 0.5))
#define GRUNDIG_NOKIA_IR60_START_BIT_LEN_MIN    ((uint16_t)(GRUNDIG_NOKIA_IR60_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define GRUNDIG_NOKIA_IR60_START_BIT_LEN_MAX    ((uint16_t)(GRUNDIG_NOKIA_IR60_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define GRUNDIG_NOKIA_IR60_BIT_LEN_MIN          ((uint16_t)(GRUNDIG_NOKIA_IR60_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define GRUNDIG_NOKIA_IR60_BIT_LEN_MAX          ((uint16_t)(GRUNDIG_NOKIA_IR60_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MIN    ((uint16_t)(GRUNDIG_NOKIA_IR60_PRE_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) + 1)
#define GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MAX    ((uint16_t)(GRUNDIG_NOKIA_IR60_PRE_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MIN       ((uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PULSE_TIME * MIN_TOLERANCE_00 + 0.5) - 1)
#define SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MAX       ((uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MIN       ((uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PAUSE_TIME * MIN_TOLERANCE_00 + 0.5) - 1)
#define SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MAX       ((uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SIEMENS_OR_RUWIDO_BIT_PULSE_LEN_MIN             ((uint16_t)(SIEMENS_OR_RUWIDO_BIT_PULSE_TIME * MIN_TOLERANCE_00 + 0.5) - 1)
#define SIEMENS_OR_RUWIDO_BIT_PULSE_LEN_MAX             ((uint16_t)(SIEMENS_OR_RUWIDO_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define SIEMENS_OR_RUWIDO_BIT_PAUSE_LEN_MIN             ((uint16_t)(SIEMENS_OR_RUWIDO_BIT_PAUSE_TIME * MIN_TOLERANCE_00 + 0.5) - 1)
#define SIEMENS_OR_RUWIDO_BIT_PAUSE_LEN_MAX             ((uint16_t)(SIEMENS_OR_RUWIDO_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define FDC_START_BIT_PULSE_LEN_MIN             ((uint16_t)(FDC_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)   // 5%: avoid conflict with NETBOX
#define FDC_START_BIT_PULSE_LEN_MAX             ((uint16_t)(FDC_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5))
#define FDC_START_BIT_PAUSE_LEN_MIN             ((uint16_t)(FDC_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define FDC_START_BIT_PAUSE_LEN_MAX             ((uint16_t)(FDC_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5))
#define FDC_PULSE_LEN_MIN                       ((uint16_t)(FDC_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define FDC_PULSE_LEN_MAX                       ((uint16_t)(FDC_PULSE_TIME * MAX_TOLERANCE_50 + 0.5) + 1)
#define FDC_1_PAUSE_LEN_MIN                     ((uint16_t)(FDC_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define FDC_1_PAUSE_LEN_MAX                     ((uint16_t)(FDC_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#if 0
#define FDC_0_PAUSE_LEN_MIN                     ((uint16_t)(FDC_0_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)   // could be negative: 255
#else
#define FDC_0_PAUSE_LEN_MIN                     (1)                                                                         // simply use 1
#endif
#define FDC_0_PAUSE_LEN_MAX                     ((uint16_t)(FDC_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define RCCAR_START_BIT_PULSE_LEN_MIN           ((uint16_t)(RCCAR_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RCCAR_START_BIT_PULSE_LEN_MAX           ((uint16_t)(RCCAR_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RCCAR_START_BIT_PAUSE_LEN_MIN           ((uint16_t)(RCCAR_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RCCAR_START_BIT_PAUSE_LEN_MAX           ((uint16_t)(RCCAR_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RCCAR_PULSE_LEN_MIN                     ((uint16_t)(RCCAR_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define RCCAR_PULSE_LEN_MAX                     ((uint16_t)(RCCAR_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define RCCAR_1_PAUSE_LEN_MIN                   ((uint16_t)(RCCAR_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RCCAR_1_PAUSE_LEN_MAX                   ((uint16_t)(RCCAR_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define RCCAR_0_PAUSE_LEN_MIN                   ((uint16_t)(RCCAR_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RCCAR_0_PAUSE_LEN_MAX                   ((uint16_t)(RCCAR_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define JVC_START_BIT_PULSE_LEN_MIN             ((uint16_t)(JVC_START_BIT_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define JVC_START_BIT_PULSE_LEN_MAX             ((uint16_t)(JVC_START_BIT_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define JVC_REPEAT_START_BIT_PAUSE_LEN_MIN      ((uint16_t)((JVC_FRAME_REPEAT_PAUSE_TIME - IRMP_TIMEOUT_TIME) * MIN_TOLERANCE_40 + 0.5) - 1)  // HACK!
#define JVC_REPEAT_START_BIT_PAUSE_LEN_MAX      ((uint16_t)((JVC_FRAME_REPEAT_PAUSE_TIME - IRMP_TIMEOUT_TIME) * MAX_TOLERANCE_70 + 0.5) - 1)  // HACK!
#define JVC_PULSE_LEN_MIN                       ((uint16_t)(JVC_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define JVC_PULSE_LEN_MAX                       ((uint16_t)(JVC_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define JVC_1_PAUSE_LEN_MIN                     ((uint16_t)(JVC_1_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define JVC_1_PAUSE_LEN_MAX                     ((uint16_t)(JVC_1_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define JVC_0_PAUSE_LEN_MIN                     ((uint16_t)(JVC_0_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define JVC_0_PAUSE_LEN_MAX                     ((uint16_t)(JVC_0_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
// autodetect JVC repetition frame within 50 msec:
#define JVC_FRAME_REPEAT_PAUSE_LEN_MAX          (uint16_t)(JVC_FRAME_REPEAT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5)

#define NIKON_START_BIT_PULSE_LEN_MIN           ((uint16_t)(NIKON_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_START_BIT_PULSE_LEN_MAX           ((uint16_t)(NIKON_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_START_BIT_PAUSE_LEN_MIN           ((uint16_t)(NIKON_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_START_BIT_PAUSE_LEN_MAX           ((uint16_t)(NIKON_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_REPEAT_START_BIT_PAUSE_LEN_MIN    ((uint16_t)(NIKON_REPEAT_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_REPEAT_START_BIT_PAUSE_LEN_MAX    ((uint16_t)(NIKON_REPEAT_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_PULSE_LEN_MIN                     ((uint16_t)(NIKON_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_PULSE_LEN_MAX                     ((uint16_t)(NIKON_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_1_PAUSE_LEN_MIN                   ((uint16_t)(NIKON_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_1_PAUSE_LEN_MAX                   ((uint16_t)(NIKON_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_0_PAUSE_LEN_MIN                   ((uint16_t)(NIKON_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define NIKON_0_PAUSE_LEN_MAX                   ((uint16_t)(NIKON_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define NIKON_FRAME_REPEAT_PAUSE_LEN_MAX        (uint16_t)(NIKON_FRAME_REPEAT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5)

#define KATHREIN_START_BIT_PULSE_LEN_MIN        ((uint16_t)(KATHREIN_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_START_BIT_PULSE_LEN_MAX        ((uint16_t)(KATHREIN_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_START_BIT_PAUSE_LEN_MIN        ((uint16_t)(KATHREIN_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_START_BIT_PAUSE_LEN_MAX        ((uint16_t)(KATHREIN_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_1_PULSE_LEN_MIN                ((uint16_t)(KATHREIN_1_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_1_PULSE_LEN_MAX                ((uint16_t)(KATHREIN_1_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_1_PAUSE_LEN_MIN                ((uint16_t)(KATHREIN_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_1_PAUSE_LEN_MAX                ((uint16_t)(KATHREIN_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_0_PULSE_LEN_MIN                ((uint16_t)(KATHREIN_0_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_0_PULSE_LEN_MAX                ((uint16_t)(KATHREIN_0_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_0_PAUSE_LEN_MIN                ((uint16_t)(KATHREIN_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_0_PAUSE_LEN_MAX                ((uint16_t)(KATHREIN_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define KATHREIN_SYNC_BIT_PAUSE_LEN_MIN         ((uint16_t)(KATHREIN_SYNC_BIT_PAUSE_LEN_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define KATHREIN_SYNC_BIT_PAUSE_LEN_MAX         ((uint16_t)(KATHREIN_SYNC_BIT_PAUSE_LEN_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define NETBOX_START_BIT_PULSE_LEN_MIN          ((uint16_t)(NETBOX_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define NETBOX_START_BIT_PULSE_LEN_MAX          ((uint16_t)(NETBOX_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define NETBOX_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(NETBOX_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define NETBOX_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(NETBOX_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define NETBOX_PULSE_LEN                        ((uint16_t)(NETBOX_PULSE_TIME))
#define NETBOX_PAUSE_LEN                        ((uint16_t)(NETBOX_PAUSE_TIME))
#define NETBOX_PULSE_REST_LEN                   ((uint16_t)(NETBOX_PULSE_TIME / 4))
#define NETBOX_PAUSE_REST_LEN                   ((uint16_t)(NETBOX_PAUSE_TIME / 4))

#define LEGO_START_BIT_PULSE_LEN_MIN            ((uint16_t)(LEGO_START_BIT_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define LEGO_START_BIT_PULSE_LEN_MAX            ((uint16_t)(LEGO_START_BIT_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define LEGO_START_BIT_PAUSE_LEN_MIN            ((uint16_t)(LEGO_START_BIT_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define LEGO_START_BIT_PAUSE_LEN_MAX            ((uint16_t)(LEGO_START_BIT_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define LEGO_PULSE_LEN_MIN                      ((uint16_t)(LEGO_PULSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define LEGO_PULSE_LEN_MAX                      ((uint16_t)(LEGO_PULSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define LEGO_1_PAUSE_LEN_MIN                    ((uint16_t)(LEGO_1_PAUSE_TIME * MIN_TOLERANCE_25 + 0.5) - 1)
#define LEGO_1_PAUSE_LEN_MAX                    ((uint16_t)(LEGO_1_PAUSE_TIME * MAX_TOLERANCE_40 + 0.5) + 1)
#define LEGO_0_PAUSE_LEN_MIN                    ((uint16_t)(LEGO_0_PAUSE_TIME * MIN_TOLERANCE_40 + 0.5) - 1)
#define LEGO_0_PAUSE_LEN_MAX                    ((uint16_t)(LEGO_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define IRMP16_START_BIT_PULSE_LEN_MIN          ((uint16_t)(IRMP16_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define IRMP16_START_BIT_PULSE_LEN_MAX          ((uint16_t)(IRMP16_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define IRMP16_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(IRMP16_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define IRMP16_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(IRMP16_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define IRMP16_PULSE_LEN_MIN                    ((uint16_t)(IRMP16_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define IRMP16_PULSE_LEN_MAX                    ((uint16_t)(IRMP16_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define IRMP16_1_PAUSE_LEN_MIN                  ((uint16_t)(IRMP16_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define IRMP16_1_PAUSE_LEN_MAX                  ((uint16_t)(IRMP16_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define IRMP16_0_PAUSE_LEN_MIN                  ((uint16_t)(IRMP16_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define IRMP16_0_PAUSE_LEN_MAX                  ((uint16_t)(IRMP16_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define GREE_START_BIT_PULSE_LEN_MIN            ((uint16_t)(GREE_START_BIT_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define GREE_START_BIT_PULSE_LEN_MAX            ((uint16_t)(GREE_START_BIT_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define GREE_START_BIT_PAUSE_LEN_MIN            ((uint16_t)(GREE_START_BIT_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define GREE_START_BIT_PAUSE_LEN_MAX            ((uint16_t)(GREE_START_BIT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define GREE_PULSE_LEN_MIN                      ((uint16_t)(GREE_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define GREE_PULSE_LEN_MAX                      ((uint16_t)(GREE_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define GREE_1_PAUSE_LEN_MIN                    ((uint16_t)(GREE_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define GREE_1_PAUSE_LEN_MAX                    ((uint16_t)(GREE_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define GREE_0_PAUSE_LEN_MIN                    ((uint16_t)(GREE_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define GREE_0_PAUSE_LEN_MAX                    ((uint16_t)(GREE_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define BOSE_START_BIT_PULSE_LEN_MIN             ((uint16_t)(BOSE_START_BIT_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define BOSE_START_BIT_PULSE_LEN_MAX             ((uint16_t)(BOSE_START_BIT_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define BOSE_START_BIT_PAUSE_LEN_MIN             ((uint16_t)(BOSE_START_BIT_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define BOSE_START_BIT_PAUSE_LEN_MAX             ((uint16_t)(BOSE_START_BIT_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define BOSE_PULSE_LEN_MIN                       ((uint16_t)(BOSE_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define BOSE_PULSE_LEN_MAX                       ((uint16_t)(BOSE_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define BOSE_1_PAUSE_LEN_MIN                     ((uint16_t)(BOSE_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define BOSE_1_PAUSE_LEN_MAX                     ((uint16_t)(BOSE_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define BOSE_0_PAUSE_LEN_MIN                     ((uint16_t)(BOSE_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define BOSE_0_PAUSE_LEN_MAX                     ((uint16_t)(BOSE_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define BOSE_FRAME_REPEAT_PAUSE_LEN_MAX          (uint16_t)(100.0 * MAX_TOLERANCE_20 + 0.5)

#define A1TVBOX_START_BIT_PULSE_LEN_MIN         ((uint16_t)(A1TVBOX_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define A1TVBOX_START_BIT_PULSE_LEN_MAX         ((uint16_t)(A1TVBOX_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define A1TVBOX_START_BIT_PAUSE_LEN_MIN         ((uint16_t)(A1TVBOX_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define A1TVBOX_START_BIT_PAUSE_LEN_MAX         ((uint16_t)(A1TVBOX_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define A1TVBOX_BIT_PULSE_LEN_MIN               ((uint16_t)(A1TVBOX_BIT_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define A1TVBOX_BIT_PULSE_LEN_MAX               ((uint16_t)(A1TVBOX_BIT_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define A1TVBOX_BIT_PAUSE_LEN_MIN               ((uint16_t)(A1TVBOX_BIT_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define A1TVBOX_BIT_PAUSE_LEN_MAX               ((uint16_t)(A1TVBOX_BIT_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define MERLIN_START_BIT_PULSE_LEN_MIN          ((uint16_t)(MERLIN_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define MERLIN_START_BIT_PULSE_LEN_MAX          ((uint16_t)(MERLIN_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define MERLIN_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(MERLIN_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define MERLIN_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(MERLIN_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define MERLIN_BIT_PULSE_LEN_MIN                ((uint16_t)(MERLIN_BIT_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MERLIN_BIT_PULSE_LEN_MAX                ((uint16_t)(MERLIN_BIT_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define MERLIN_BIT_PAUSE_LEN_MIN                ((uint16_t)(MERLIN_BIT_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define MERLIN_BIT_PAUSE_LEN_MAX                ((uint16_t)(MERLIN_BIT_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define ORTEK_START_BIT_PULSE_LEN_MIN           ((uint16_t)(ORTEK_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ORTEK_START_BIT_PULSE_LEN_MAX           ((uint16_t)(ORTEK_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define ORTEK_START_BIT_PAUSE_LEN_MIN           ((uint16_t)(ORTEK_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ORTEK_START_BIT_PAUSE_LEN_MAX           ((uint16_t)(ORTEK_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define ORTEK_BIT_PULSE_LEN_MIN                 ((uint16_t)(ORTEK_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ORTEK_BIT_PULSE_LEN_MAX                 ((uint16_t)(ORTEK_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define ORTEK_BIT_PAUSE_LEN_MIN                 ((uint16_t)(ORTEK_BIT_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ORTEK_BIT_PAUSE_LEN_MAX                 ((uint16_t)(ORTEK_BIT_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define TELEFUNKEN_START_BIT_PULSE_LEN_MIN      ((uint16_t)(TELEFUNKEN_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define TELEFUNKEN_START_BIT_PULSE_LEN_MAX      ((uint16_t)(TELEFUNKEN_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define TELEFUNKEN_START_BIT_PAUSE_LEN_MIN      ((uint16_t)((TELEFUNKEN_START_BIT_PAUSE_TIME) * MIN_TOLERANCE_10 + 0.5) - 1)
#define TELEFUNKEN_START_BIT_PAUSE_LEN_MAX      ((uint16_t)((TELEFUNKEN_START_BIT_PAUSE_TIME) * MAX_TOLERANCE_10 + 0.5) - 1)
#define TELEFUNKEN_PULSE_LEN_MIN                ((uint16_t)(TELEFUNKEN_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define TELEFUNKEN_PULSE_LEN_MAX                ((uint16_t)(TELEFUNKEN_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define TELEFUNKEN_1_PAUSE_LEN_MIN              ((uint16_t)(TELEFUNKEN_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define TELEFUNKEN_1_PAUSE_LEN_MAX              ((uint16_t)(TELEFUNKEN_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define TELEFUNKEN_0_PAUSE_LEN_MIN              ((uint16_t)(TELEFUNKEN_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define TELEFUNKEN_0_PAUSE_LEN_MAX              ((uint16_t)(TELEFUNKEN_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
// autodetect TELEFUNKEN repetition frame within 50 msec:
// #define TELEFUNKEN_FRAME_REPEAT_PAUSE_LEN_MAX   (uint16_t)(TELEFUNKEN_FRAME_REPEAT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5)

#define ROOMBA_START_BIT_PULSE_LEN_MIN          ((uint16_t)(ROOMBA_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ROOMBA_START_BIT_PULSE_LEN_MAX          ((uint16_t)(ROOMBA_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define ROOMBA_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(ROOMBA_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define ROOMBA_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(ROOMBA_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define ROOMBA_1_PAUSE_LEN_EXACT                ((uint16_t)(ROOMBA_1_PAUSE_TIME + 0.5))
#define ROOMBA_1_PULSE_LEN_MIN                  ((uint16_t)(ROOMBA_1_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define ROOMBA_1_PULSE_LEN_MAX                  ((uint16_t)(ROOMBA_1_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define ROOMBA_1_PAUSE_LEN_MIN                  ((uint16_t)(ROOMBA_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define ROOMBA_1_PAUSE_LEN_MAX                  ((uint16_t)(ROOMBA_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define ROOMBA_0_PAUSE_LEN                      ((uint16_t)(ROOMBA_0_PAUSE_TIME))
#define ROOMBA_0_PULSE_LEN_MIN                  ((uint16_t)(ROOMBA_0_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define ROOMBA_0_PULSE_LEN_MAX                  ((uint16_t)(ROOMBA_0_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define ROOMBA_0_PAUSE_LEN_MIN                  ((uint16_t)(ROOMBA_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define ROOMBA_0_PAUSE_LEN_MAX                  ((uint16_t)(ROOMBA_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define RCMM32_START_BIT_PULSE_LEN_MIN          ((uint16_t)(RCMM32_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_START_BIT_PULSE_LEN_MAX          ((uint16_t)(RCMM32_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(RCMM32_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(RCMM32_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_BIT_PULSE_LEN_MIN                ((uint16_t)(RCMM32_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_BIT_PULSE_LEN_MAX                ((uint16_t)(RCMM32_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_BIT_00_PAUSE_LEN_MIN             ((uint16_t)(RCMM32_00_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_BIT_00_PAUSE_LEN_MAX             ((uint16_t)(RCMM32_00_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_BIT_01_PAUSE_LEN_MIN             ((uint16_t)(RCMM32_01_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_BIT_01_PAUSE_LEN_MAX             ((uint16_t)(RCMM32_01_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_BIT_10_PAUSE_LEN_MIN             ((uint16_t)(RCMM32_10_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_BIT_10_PAUSE_LEN_MAX             ((uint16_t)(RCMM32_10_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RCMM32_BIT_11_PAUSE_LEN_MIN             ((uint16_t)(RCMM32_11_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RCMM32_BIT_11_PAUSE_LEN_MAX             ((uint16_t)(RCMM32_11_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)

#define PENTAX_START_BIT_PULSE_LEN_MIN          ((uint16_t)(PENTAX_START_BIT_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define PENTAX_START_BIT_PULSE_LEN_MAX          ((uint16_t)(PENTAX_START_BIT_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define PENTAX_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(PENTAX_START_BIT_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define PENTAX_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(PENTAX_START_BIT_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define PENTAX_1_PAUSE_LEN_EXACT                ((uint16_t)(PENTAX_1_PAUSE_TIME + 0.5))
#define PENTAX_PULSE_LEN_MIN                    ((uint16_t)(PENTAX_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PENTAX_PULSE_LEN_MAX                    ((uint16_t)(PENTAX_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define PENTAX_1_PAUSE_LEN_MIN                  ((uint16_t)(PENTAX_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PENTAX_1_PAUSE_LEN_MAX                  ((uint16_t)(PENTAX_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define PENTAX_0_PAUSE_LEN                      ((uint16_t)(PENTAX_0_PAUSE_TIME))
#define PENTAX_PULSE_LEN_MIN                    ((uint16_t)(PENTAX_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PENTAX_PULSE_LEN_MAX                    ((uint16_t)(PENTAX_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define PENTAX_0_PAUSE_LEN_MIN                  ((uint16_t)(PENTAX_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define PENTAX_0_PAUSE_LEN_MAX                  ((uint16_t)(PENTAX_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)

#define ACP24_START_BIT_PULSE_LEN_MIN           ((uint16_t)(ACP24_START_BIT_PULSE_TIME * MIN_TOLERANCE_15 + 0.5) - 1)
#define ACP24_START_BIT_PULSE_LEN_MAX           ((uint16_t)(ACP24_START_BIT_PULSE_TIME * MAX_TOLERANCE_15 + 0.5) + 1)
#define ACP24_START_BIT_PAUSE_LEN_MIN           ((uint16_t)(ACP24_START_BIT_PAUSE_TIME * MIN_TOLERANCE_15 + 0.5) - 1)
#define ACP24_START_BIT_PAUSE_LEN_MAX           ((uint16_t)(ACP24_START_BIT_PAUSE_TIME * MAX_TOLERANCE_15 + 0.5) + 1)
#define ACP24_PULSE_LEN_MIN                     ((uint16_t)(ACP24_PULSE_TIME * MIN_TOLERANCE_15 + 0.5) - 1)
#define ACP24_PULSE_LEN_MAX                     ((uint16_t)(ACP24_PULSE_TIME * MAX_TOLERANCE_15 + 0.5) + 1)
#define ACP24_1_PAUSE_LEN_MIN                   ((uint16_t)(ACP24_1_PAUSE_TIME * MIN_TOLERANCE_15 + 0.5) - 1)
#define ACP24_1_PAUSE_LEN_MAX                   ((uint16_t)(ACP24_1_PAUSE_TIME * MAX_TOLERANCE_15 + 0.5) + 1)
#define ACP24_0_PAUSE_LEN_MIN                   ((uint16_t)(ACP24_0_PAUSE_TIME * MIN_TOLERANCE_15 + 0.5) - 1)
#define ACP24_0_PAUSE_LEN_MAX                   ((uint16_t)(ACP24_0_PAUSE_TIME * MAX_TOLERANCE_15 + 0.5) + 1)

#define METZ_START_BIT_PULSE_LEN_MIN            ((uint16_t)(METZ_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define METZ_START_BIT_PULSE_LEN_MAX            ((uint16_t)(METZ_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define METZ_START_BIT_PAUSE_LEN_MIN            ((uint16_t)(METZ_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define METZ_START_BIT_PAUSE_LEN_MAX            ((uint16_t)(METZ_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define METZ_PULSE_LEN_MIN                      ((uint16_t)(METZ_PULSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define METZ_PULSE_LEN_MAX                      ((uint16_t)(METZ_PULSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define METZ_1_PAUSE_LEN_MIN                    ((uint16_t)(METZ_1_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define METZ_1_PAUSE_LEN_MAX                    ((uint16_t)(METZ_1_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define METZ_0_PAUSE_LEN_MIN                    ((uint16_t)(METZ_0_PAUSE_TIME * MIN_TOLERANCE_20 + 0.5) - 1)
#define METZ_0_PAUSE_LEN_MAX                    ((uint16_t)(METZ_0_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5) + 1)
#define METZ_FRAME_REPEAT_PAUSE_LEN_MAX         (uint16_t)(METZ_FRAME_REPEAT_PAUSE_TIME * MAX_TOLERANCE_20 + 0.5)

#define RF_GEN24_1_PAUSE_LEN_EXACT              ((uint16_t)(RF_GEN24_1_PAUSE_TIME + 0.5))
#define RF_GEN24_1_PULSE_LEN_MIN                ((uint16_t)(RF_GEN24_1_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RF_GEN24_1_PULSE_LEN_MAX                ((uint16_t)(RF_GEN24_1_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define RF_GEN24_1_PAUSE_LEN_MIN                ((uint16_t)(RF_GEN24_1_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RF_GEN24_1_PAUSE_LEN_MAX                ((uint16_t)(RF_GEN24_1_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define RF_GEN24_0_PAUSE_LEN                    ((uint16_t)(RF_GEN24_0_PAUSE_TIME))
#define RF_GEN24_0_PULSE_LEN_MIN                ((uint16_t)(RF_GEN24_0_PULSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RF_GEN24_0_PULSE_LEN_MAX                ((uint16_t)(RF_GEN24_0_PULSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)
#define RF_GEN24_0_PAUSE_LEN_MIN                ((uint16_t)(RF_GEN24_0_PAUSE_TIME * MIN_TOLERANCE_30 + 0.5) - 1)
#define RF_GEN24_0_PAUSE_LEN_MAX                ((uint16_t)(RF_GEN24_0_PAUSE_TIME * MAX_TOLERANCE_30 + 0.5) + 1)

#define RF_X10_START_BIT_PULSE_LEN_MIN          ((uint16_t)(RF_X10_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RF_X10_START_BIT_PULSE_LEN_MAX          ((uint16_t)(RF_X10_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RF_X10_START_BIT_PAUSE_LEN_MIN          ((uint16_t)(RF_X10_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RF_X10_START_BIT_PAUSE_LEN_MAX          ((uint16_t)(RF_X10_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RF_X10_1_PAUSE_LEN_EXACT                ((uint16_t)(RF_X10_1_PAUSE_TIME + 0.5))
#define RF_X10_1_PULSE_LEN_MIN                  ((uint16_t)(RF_X10_1_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_X10_1_PULSE_LEN_MAX                  ((uint16_t)(RF_X10_1_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_X10_1_PAUSE_LEN_MIN                  ((uint16_t)(RF_X10_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_X10_1_PAUSE_LEN_MAX                  ((uint16_t)(RF_X10_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_X10_0_PAUSE_LEN                      ((uint16_t)(RF_X10_0_PAUSE_TIME))
#define RF_X10_0_PULSE_LEN_MIN                  ((uint16_t)(RF_X10_0_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_X10_0_PULSE_LEN_MAX                  ((uint16_t)(RF_X10_0_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_X10_0_PAUSE_LEN_MIN                  ((uint16_t)(RF_X10_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_X10_0_PAUSE_LEN_MAX                  ((uint16_t)(RF_X10_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define RF_MEDION_START_BIT_PULSE_LEN_MIN       ((uint16_t)(RF_MEDION_START_BIT_PULSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RF_MEDION_START_BIT_PULSE_LEN_MAX       ((uint16_t)(RF_MEDION_START_BIT_PULSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RF_MEDION_START_BIT_PAUSE_LEN_MIN       ((uint16_t)(RF_MEDION_START_BIT_PAUSE_TIME * MIN_TOLERANCE_05 + 0.5) - 1)
#define RF_MEDION_START_BIT_PAUSE_LEN_MAX       ((uint16_t)(RF_MEDION_START_BIT_PAUSE_TIME * MAX_TOLERANCE_05 + 0.5) + 1)
#define RF_MEDION_1_PAUSE_LEN_EXACT             ((uint16_t)(RF_MEDION_1_PAUSE_TIME + 0.5))
#define RF_MEDION_1_PULSE_LEN_MIN               ((uint16_t)(RF_MEDION_1_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_MEDION_1_PULSE_LEN_MAX               ((uint16_t)(RF_MEDION_1_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_MEDION_1_PAUSE_LEN_MIN               ((uint16_t)(RF_MEDION_1_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_MEDION_1_PAUSE_LEN_MAX               ((uint16_t)(RF_MEDION_1_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_MEDION_0_PAUSE_LEN                   ((uint16_t)(RF_MEDION_0_PAUSE_TIME))
#define RF_MEDION_0_PULSE_LEN_MIN               ((uint16_t)(RF_MEDION_0_PULSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_MEDION_0_PULSE_LEN_MAX               ((uint16_t)(RF_MEDION_0_PULSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)
#define RF_MEDION_0_PAUSE_LEN_MIN               ((uint16_t)(RF_MEDION_0_PAUSE_TIME * MIN_TOLERANCE_10 + 0.5) - 1)
#define RF_MEDION_0_PAUSE_LEN_MAX               ((uint16_t)(RF_MEDION_0_PAUSE_TIME * MAX_TOLERANCE_10 + 0.5) + 1)

#define AUTO_FRAME_REPETITION_LEN               (uint16_t)(AUTO_FRAME_REPETITION_TIME + 0.5)       // use uint16_t!

#define STOP_BIT_PAUSE_TIME_MIN                 3000.0e-6                                                               // minimum stop bit space time: 3.0 msec
#define STOP_BIT_PAUSE_LEN_MIN                  ((uint16_t)(STOP_BIT_PAUSE_TIME_MIN + 0.5) + 1)      // minimum stop bit space len

#define PARITY_CHECK_OK                         1
#define PARITY_CHECK_FAILED                     0

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  Protocol names
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined(UNIX_OR_WINDOWS) || IRMP_PROTOCOL_NAMES==1
static const char proto_unknown[]       = "UNKNOWN";
static const char proto_sircs[]         = "SIRCS";
static const char proto_nec[]           = "NEC";
static const char proto_samsung[]       = "SAMSUNG";
static const char proto_matsushita[]    = "MATSUSH";
static const char proto_kaseikyo[]      = "KASEIKYO";
static const char proto_recs80[]        = "RECS80";
static const char proto_rc5[]           = "RC5";
static const char proto_denon[]         = "DENON";
static const char proto_rc6[]           = "RC6";
static const char proto_samsung32[]     = "SAMSG32";
static const char proto_apple[]         = "APPLE";
static const char proto_recs80ext[]     = "RECS80EX";
static const char proto_nubert[]        = "NUBERT";
static const char proto_bang_olufsen[]  = "BANG OLU";
static const char proto_grundig[]       = "GRUNDIG";
static const char proto_nokia[]         = "NOKIA";
static const char proto_siemens[]       = "SIEMENS";
static const char proto_fdc[]           = "FDC";
static const char proto_rccar[]         = "RCCAR";
static const char proto_jvc[]           = "JVC";
static const char proto_rc6a[]          = "RC6A";
static const char proto_nikon[]         = "NIKON";
static const char proto_ruwido[]        = "RUWIDO";
static const char proto_ir60[]          = "IR60";
static const char proto_kathrein[]      = "KATHREIN";
static const char proto_netbox[]        = "NETBOX";
static const char proto_nec16[]         = "NEC16";
static const char proto_nec42[]         = "NEC42";
static const char proto_lego[]          = "LEGO";
static const char proto_thomson[]       = "THOMSON";
static const char proto_bose[]          = "BOSE";
static const char proto_a1tvbox[]       = "A1TVBOX";
static const char proto_ortek[]         = "ORTEK";
static const char proto_telefunken[]    = "TELEFUNKEN";
static const char proto_roomba[]        = "ROOMBA";
static const char proto_rcmm32[]        = "RCMM32";
static const char proto_rcmm24[]        = "RCMM24";
static const char proto_rcmm12[]        = "RCMM12";
static const char proto_speaker[]       = "SPEAKER";
static const char proto_lgair[]         = "LGAIR";
static const char proto_samsung48[]     = "SAMSG48";
static const char proto_merlin[]        = "MERLIN";
static const char proto_pentax[]        = "PENTAX";
static const char proto_fan[]           = "FAN";
static const char proto_s100[]          = "S100";
static const char proto_acp24[]         = "ACP24";
static const char proto_technics[]      = "TECHNICS";
static const char proto_panasonic[]     = "PANASONIC";
static const char proto_mitsu_heavy[]   = "MITSU_HEAVY";
static const char proto_vincent[]       = "VINCENT";
static const char proto_samsungah[]     = "SAMSUNGAH";
static const char proto_irmp16[]        = "IRMP16";
static const char proto_gree[]          = "GREE";
static const char proto_rcii[]          = "RCII";
static const char proto_metz[]          = "METZ";
static const char proto_onkyo[]         = "ONKYO";

static const char proto_rf_gen24[]      = "RF_GEN24";
static const char proto_rf_x10[]        = "RF_X10";
static const char proto_rf_medion[]     = "RF_MEDION";
static const char proto_melinera[]      = "MELINERA";

static const char proto_nec8[]      	= "NEC8";
static const char proto_rca[]      		= "RCA";
static const char proto_pioneer[]      	= "PIONEER";
static const char proto_sircs15[]       = "SIRCS15";
static const char proto_sircs20[]       = "SIRCS20";
static const char proto_rc5x[]          = "RC5X";
static const char proto_raw[]      		= "RAW";

/*
 * This list order must match that of the enum S_M1_IR_Protocol_Def_t
 */
const char * const
irmp_protocol_names[IRMP_N_PROTOCOLS + 1] =
{
    proto_unknown, // 0
    proto_sircs,
    proto_nec,
    proto_samsung,
    proto_matsushita,
    proto_kaseikyo,
    proto_recs80,
    proto_rc5,
    proto_denon,
    proto_rc6,
    proto_samsung32, // 10
    proto_apple,
    proto_recs80ext,
    proto_nubert,
    proto_bang_olufsen,
    proto_grundig,
    proto_nokia,
    proto_siemens,
    proto_fdc,
    proto_rccar,
    proto_jvc, // 20
    proto_rc6a,
    proto_nikon,
    proto_ruwido,
    proto_ir60,
    proto_kathrein,
    proto_netbox,
    proto_nec16,
    proto_nec42,
    proto_lego,
    proto_thomson, // 30
    proto_bose,
    proto_a1tvbox,
    proto_ortek,
    proto_telefunken,
    proto_roomba,
    proto_rcmm32,
    proto_rcmm24,
    proto_rcmm12,
    proto_speaker,
    proto_lgair, // 40
    proto_samsung48,
    proto_merlin,
    proto_pentax,
    proto_fan,
    proto_s100,
    proto_acp24,
    proto_technics,
    proto_panasonic,
    proto_mitsu_heavy,
    proto_vincent, // 50
    proto_samsungah,
    proto_irmp16,
    proto_gree,
    proto_rcii,
    proto_metz,
    proto_onkyo,
    proto_rf_gen24,
    proto_rf_x10,
    proto_rf_medion,
    proto_melinera, // 60
// Add on
	proto_nec8,
	proto_rca,
	proto_pioneer,
	proto_sircs15,
	proto_sircs20,
	proto_rc5x,
	proto_raw
};

#endif // #if defined(UNIX_OR_WINDOWS) || IRMP_PROTOCOL_NAMES==1

typedef struct
{
    uint8_t    	protocol;                                                // ir protocol
    uint16_t    pulse_1_len_min;                                         // minimum length of pulse with bit value 1
    uint16_t    pulse_1_len_max;                                         // maximum length of pulse with bit value 1
    uint16_t    space_1_len_min;                                         // minimum length of space with bit value 1
    uint16_t    space_1_len_max;                                         // maximum length of space with bit value 1
    uint16_t    pulse_0_len_min;                                         // minimum length of pulse with bit value 0
    uint16_t    pulse_0_len_max;                                         // maximum length of pulse with bit value 0
    uint16_t    space_0_len_min;                                         // minimum length of space with bit value 0
    uint16_t    space_0_len_max;                                         // maximum length of space with bit value 0
    uint8_t    address_offset;                                          // address offset
    uint8_t    address_end;                                             // end of address
    uint8_t    command_offset;                                          // command offset
    uint8_t    command_end;                                             // end of command
    uint8_t    complete_len;                                            // complete length of frame
    uint8_t    stop_bit;                                                // flag: frame has stop bit
    uint8_t    lsb_first;                                               // flag: LSB first
    uint8_t    flags;                                                   // some flags
} IRMP_PARAMETER;


#if IRMP_SUPPORT_SIRCS_PROTOCOL==1

static const IRMP_PARAMETER sircs_param =
{
    IRMP_SIRCS_PROTOCOL,                                                // protocol:        ir protocol
    SIRCS_1_PULSE_LEN_MIN,                                              // pulse_1_len_min: minimum length of pulse with bit value 1
    SIRCS_1_PULSE_LEN_MAX,                                              // pulse_1_len_max: maximum length of pulse with bit value 1
    SIRCS_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    SIRCS_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    SIRCS_0_PULSE_LEN_MIN,                                              // pulse_0_len_min: minimum length of pulse with bit value 0
    SIRCS_0_PULSE_LEN_MAX,                                              // pulse_0_len_max: maximum length of pulse with bit value 0
    SIRCS_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    SIRCS_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    SIRCS_ADDRESS_OFFSET,                                               // address_offset:  address offset
    SIRCS_ADDRESS_OFFSET + SIRCS_ADDRESS_LEN,                           // address_end:     end of address
    SIRCS_COMMAND_OFFSET,                                               // command_offset:  command offset
    SIRCS_COMMAND_OFFSET + SIRCS_COMMAND_LEN,                           // command_end:     end of command
    SIRCS_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    SIRCS_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    SIRCS_LSB,                                                          // lsb_first:       flag: LSB first
    SIRCS_FLAGS,                                                        // flags:           some flags
};

#endif // #if IRMP_SUPPORT_SIRCS_PROTOCOL==1

#if IRMP_SUPPORT_NEC_PROTOCOL==1

static const IRMP_PARAMETER nec_param =
{
    IRMP_NEC_PROTOCOL,                                                  // protocol:        ir protocol
    NEC_PULSE_LEN_MIN,                                                  // pulse_1_len_min: minimum length of pulse with bit value 1
    NEC_PULSE_LEN_MAX,                                                  // pulse_1_len_max: maximum length of pulse with bit value 1
    NEC_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    NEC_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    NEC_PULSE_LEN_MIN,                                                  // pulse_0_len_min: minimum length of pulse with bit value 0
    NEC_PULSE_LEN_MAX,                                                  // pulse_0_len_max: maximum length of pulse with bit value 0
    NEC_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    NEC_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    NEC_ADDRESS_OFFSET,                                                 // address_offset:  address offset
    NEC_ADDRESS_OFFSET + NEC_ADDRESS_LEN,                               // address_end:     end of address
    NEC_COMMAND_OFFSET,                                                 // command_offset:  command offset
    NEC_COMMAND_OFFSET + NEC_COMMAND_LEN,                               // command_end:     end of command
    NEC_COMPLETE_DATA_LEN,                                              // complete_len:    complete length of frame
    NEC_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    NEC_LSB,                                                            // lsb_first:       flag: LSB first
    NEC_FLAGS                                                           // flags:           some flags
};

static const IRMP_PARAMETER nec_rep_param =
{
    IRMP_NEC_PROTOCOL,                                                  // protocol:        ir protocol
    NEC_PULSE_LEN_MIN,                                                  // pulse_1_len_min: minimum length of pulse with bit value 1
    NEC_PULSE_LEN_MAX,                                                  // pulse_1_len_max: maximum length of pulse with bit value 1
    NEC_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    NEC_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    NEC_PULSE_LEN_MIN,                                                  // pulse_0_len_min: minimum length of pulse with bit value 0
    NEC_PULSE_LEN_MAX,                                                  // pulse_0_len_max: maximum length of pulse with bit value 0
    NEC_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    NEC_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    0,                                                                  // address_offset:  address offset
    0,                                                                  // address_end:     end of address
    0,                                                                  // command_offset:  command offset
    0,                                                                  // command_end:     end of command
    0,                                                                  // complete_len:    complete length of frame
    NEC_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    NEC_LSB,                                                            // lsb_first:       flag: LSB first
    NEC_FLAGS                                                           // flags:           some flags
};

#endif // #if IRMP_SUPPORT_NEC_PROTOCOL==1

#if IRMP_SUPPORT_NEC42_PROTOCOL==1

static const IRMP_PARAMETER nec42_param =
{
    IRMP_NEC42_PROTOCOL,                                                // protocol:        ir protocol
    NEC_PULSE_LEN_MIN,                                                  // pulse_1_len_min: minimum length of pulse with bit value 1
    NEC_PULSE_LEN_MAX,                                                  // pulse_1_len_max: maximum length of pulse with bit value 1
    NEC_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    NEC_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    NEC_PULSE_LEN_MIN,                                                  // pulse_0_len_min: minimum length of pulse with bit value 0
    NEC_PULSE_LEN_MAX,                                                  // pulse_0_len_max: maximum length of pulse with bit value 0
    NEC_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    NEC_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    NEC42_ADDRESS_OFFSET,                                               // address_offset:  address offset
    NEC42_ADDRESS_OFFSET + NEC42_ADDRESS_LEN,                           // address_end:     end of address
    NEC42_COMMAND_OFFSET,                                               // command_offset:  command offset
    NEC42_COMMAND_OFFSET + NEC42_COMMAND_LEN,                           // command_end:     end of command
    NEC42_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    NEC_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    NEC_LSB,                                                            // lsb_first:       flag: LSB first
    NEC_FLAGS                                                           // flags:           some flags
};

#endif // #if IRMP_SUPPORT_NEC42_PROTOCOL==1

#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
#if 0 // not needed, switching from NEC

static const IRMP_PARAMETER lgair_param =
{
    IRMP_LGAIR_PROTOCOL,                                                // protocol:        ir protocol
    NEC_PULSE_LEN_MIN,                                                  // pulse_1_len_min: minimum length of pulse with bit value 1
    NEC_PULSE_LEN_MAX,                                                  // pulse_1_len_max: maximum length of pulse with bit value 1
    NEC_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    NEC_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    NEC_PULSE_LEN_MIN,                                                  // pulse_0_len_min: minimum length of pulse with bit value 0
    NEC_PULSE_LEN_MAX,                                                  // pulse_0_len_max: maximum length of pulse with bit value 0
    NEC_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    NEC_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    LGAIR_ADDRESS_OFFSET,                                               // address_offset:  address offset
    LGAIR_ADDRESS_OFFSET + LGAIR_ADDRESS_LEN,                           // address_end:     end of address
    LGAIR_COMMAND_OFFSET,                                               // command_offset:  command offset
    LGAIR_COMMAND_OFFSET + LGAIR_COMMAND_LEN,                           // command_end:     end of command
    LGAIR_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    NEC_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    NEC_LSB,                                                            // lsb_first:       flag: LSB first
    NEC_FLAGS                                                           // flags:           some flags
};

#endif // 0 not needed, switching from NEC
#endif

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1

static const IRMP_PARAMETER samsung_param =
{
    IRMP_SAMSUNG_PROTOCOL,                                              // protocol:        ir protocol
    SAMSUNG_PULSE_LEN_MIN,                                              // pulse_1_len_min: minimum length of pulse with bit value 1
    SAMSUNG_PULSE_LEN_MAX,                                              // pulse_1_len_max: maximum length of pulse with bit value 1
    SAMSUNG_1_PAUSE_LEN_MIN,                                            // space_1_len_min: minimum length of space with bit value 1
    SAMSUNG_1_PAUSE_LEN_MAX,                                            // space_1_len_max: maximum length of space with bit value 1
    SAMSUNG_PULSE_LEN_MIN,                                              // pulse_0_len_min: minimum length of pulse with bit value 0
    SAMSUNG_PULSE_LEN_MAX,                                              // pulse_0_len_max: maximum length of pulse with bit value 0
    SAMSUNG_0_PAUSE_LEN_MIN,                                            // space_0_len_min: minimum length of space with bit value 0
    SAMSUNG_0_PAUSE_LEN_MAX,                                            // space_0_len_max: maximum length of space with bit value 0
    SAMSUNG_ADDRESS_OFFSET,                                             // address_offset:  address offset
    SAMSUNG_ADDRESS_OFFSET + SAMSUNG_ADDRESS_LEN,                       // address_end:     end of address
    SAMSUNG_COMMAND_OFFSET,                                             // command_offset:  command offset
    SAMSUNG_COMMAND_OFFSET + SAMSUNG_COMMAND_LEN,                       // command_end:     end of command
    SAMSUNG_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame
    SAMSUNG_STOP_BIT,                                                   // stop_bit:        flag: frame has stop bit
    SAMSUNG_LSB,                                                        // lsb_first:       flag: LSB first
    SAMSUNG_FLAGS                                                       // flags:           some flags
};

#endif

#if IRMP_SUPPORT_SAMSUNGAH_PROTOCOL==1

static const IRMP_PARAMETER samsungah_param =
{
    IRMP_SAMSUNGAH_PROTOCOL,                                            // protocol:        ir protocol
    SAMSUNGAH_PULSE_LEN_MIN,                                            // pulse_1_len_min: minimum length of pulse with bit value 1
    SAMSUNGAH_PULSE_LEN_MAX,                                            // pulse_1_len_max: maximum length of pulse with bit value 1
    SAMSUNGAH_1_PAUSE_LEN_MIN,                                          // space_1_len_min: minimum length of space with bit value 1
    SAMSUNGAH_1_PAUSE_LEN_MAX,                                          // space_1_len_max: maximum length of space with bit value 1
    SAMSUNGAH_PULSE_LEN_MIN,                                            // pulse_0_len_min: minimum length of pulse with bit value 0
    SAMSUNGAH_PULSE_LEN_MAX,                                            // pulse_0_len_max: maximum length of pulse with bit value 0
    SAMSUNGAH_0_PAUSE_LEN_MIN,                                          // space_0_len_min: minimum length of space with bit value 0
    SAMSUNGAH_0_PAUSE_LEN_MAX,                                          // space_0_len_max: maximum length of space with bit value 0
    SAMSUNGAH_ADDRESS_OFFSET,                                           // address_offset:  address offset
    SAMSUNGAH_ADDRESS_OFFSET + SAMSUNGAH_ADDRESS_LEN,                   // address_end:     end of address
    SAMSUNGAH_COMMAND_OFFSET,                                           // command_offset:  command offset
    SAMSUNGAH_COMMAND_OFFSET + SAMSUNGAH_COMMAND_LEN,                   // command_end:     end of command
    SAMSUNGAH_COMPLETE_DATA_LEN,                                        // complete_len:    complete length of frame
    SAMSUNGAH_STOP_BIT,                                                 // stop_bit:        flag: frame has stop bit
    SAMSUNGAH_LSB,                                                      // lsb_first:       flag: LSB first
    SAMSUNGAH_FLAGS                                                     // flags:           some flags
};

#endif

#if IRMP_SUPPORT_TELEFUNKEN_PROTOCOL==1

static const IRMP_PARAMETER telefunken_param =
{
    IRMP_TELEFUNKEN_PROTOCOL,                                           // protocol:        ir protocol
    TELEFUNKEN_PULSE_LEN_MIN,                                           // pulse_1_len_min: minimum length of pulse with bit value 1
    TELEFUNKEN_PULSE_LEN_MAX,                                           // pulse_1_len_max: maximum length of pulse with bit value 1
    TELEFUNKEN_1_PAUSE_LEN_MIN,                                         // space_1_len_min: minimum length of space with bit value 1
    TELEFUNKEN_1_PAUSE_LEN_MAX,                                         // space_1_len_max: maximum length of space with bit value 1
    TELEFUNKEN_PULSE_LEN_MIN,                                           // pulse_0_len_min: minimum length of pulse with bit value 0
    TELEFUNKEN_PULSE_LEN_MAX,                                           // pulse_0_len_max: maximum length of pulse with bit value 0
    TELEFUNKEN_0_PAUSE_LEN_MIN,                                         // space_0_len_min: minimum length of space with bit value 0
    TELEFUNKEN_0_PAUSE_LEN_MAX,                                         // space_0_len_max: maximum length of space with bit value 0
    TELEFUNKEN_ADDRESS_OFFSET,                                          // address_offset:  address offset
    TELEFUNKEN_ADDRESS_OFFSET + TELEFUNKEN_ADDRESS_LEN,                 // address_end:     end of address
    TELEFUNKEN_COMMAND_OFFSET,                                          // command_offset:  command offset
    TELEFUNKEN_COMMAND_OFFSET + TELEFUNKEN_COMMAND_LEN,                 // command_end:     end of command
    TELEFUNKEN_COMPLETE_DATA_LEN,                                       // complete_len:    complete length of frame
    TELEFUNKEN_STOP_BIT,                                                // stop_bit:        flag: frame has stop bit
    TELEFUNKEN_LSB,                                                     // lsb_first:       flag: LSB first
    TELEFUNKEN_FLAGS                                                    // flags:           some flags
};

#endif

#if IRMP_SUPPORT_MATSUSHITA_PROTOCOL==1

static const IRMP_PARAMETER matsushita_param =
{
    IRMP_MATSUSHITA_PROTOCOL,                                           // protocol:        ir protocol
    MATSUSHITA_PULSE_LEN_MIN,                                           // pulse_1_len_min: minimum length of pulse with bit value 1
    MATSUSHITA_PULSE_LEN_MAX,                                           // pulse_1_len_max: maximum length of pulse with bit value 1
    MATSUSHITA_1_PAUSE_LEN_MIN,                                         // space_1_len_min: minimum length of space with bit value 1
    MATSUSHITA_1_PAUSE_LEN_MAX,                                         // space_1_len_max: maximum length of space with bit value 1
    MATSUSHITA_PULSE_LEN_MIN,                                           // pulse_0_len_min: minimum length of pulse with bit value 0
    MATSUSHITA_PULSE_LEN_MAX,                                           // pulse_0_len_max: maximum length of pulse with bit value 0
    MATSUSHITA_0_PAUSE_LEN_MIN,                                         // space_0_len_min: minimum length of space with bit value 0
    MATSUSHITA_0_PAUSE_LEN_MAX,                                         // space_0_len_max: maximum length of space with bit value 0
    MATSUSHITA_ADDRESS_OFFSET,                                          // address_offset:  address offset
    MATSUSHITA_ADDRESS_OFFSET + MATSUSHITA_ADDRESS_LEN,                 // address_end:     end of address
    MATSUSHITA_COMMAND_OFFSET,                                          // command_offset:  command offset
    MATSUSHITA_COMMAND_OFFSET + MATSUSHITA_COMMAND_LEN,                 // command_end:     end of command
    MATSUSHITA_COMPLETE_DATA_LEN,                                       // complete_len:    complete length of frame
    MATSUSHITA_STOP_BIT,                                                // stop_bit:        flag: frame has stop bit
    MATSUSHITA_LSB,                                                     // lsb_first:       flag: LSB first
    MATSUSHITA_FLAGS                                                    // flags:           some flags
};

#endif

#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1

static const IRMP_PARAMETER kaseikyo_param =
{
    IRMP_KASEIKYO_PROTOCOL,                                             // protocol:        ir protocol
    KASEIKYO_PULSE_LEN_MIN,                                             // pulse_1_len_min: minimum length of pulse with bit value 1
    KASEIKYO_PULSE_LEN_MAX,                                             // pulse_1_len_max: maximum length of pulse with bit value 1
    KASEIKYO_1_PAUSE_LEN_MIN,                                           // space_1_len_min: minimum length of space with bit value 1
    KASEIKYO_1_PAUSE_LEN_MAX,                                           // space_1_len_max: maximum length of space with bit value 1
    KASEIKYO_PULSE_LEN_MIN,                                             // pulse_0_len_min: minimum length of pulse with bit value 0
    KASEIKYO_PULSE_LEN_MAX,                                             // pulse_0_len_max: maximum length of pulse with bit value 0
    KASEIKYO_0_PAUSE_LEN_MIN,                                           // space_0_len_min: minimum length of space with bit value 0
    KASEIKYO_0_PAUSE_LEN_MAX,                                           // space_0_len_max: maximum length of space with bit value 0
    KASEIKYO_ADDRESS_OFFSET,                                            // address_offset:  address offset
    KASEIKYO_ADDRESS_OFFSET + KASEIKYO_ADDRESS_LEN,                     // address_end:     end of address
    KASEIKYO_COMMAND_OFFSET,                                            // command_offset:  command offset
    KASEIKYO_COMMAND_OFFSET + KASEIKYO_COMMAND_LEN,                     // command_end:     end of command
    KASEIKYO_COMPLETE_DATA_LEN,                                         // complete_len:    complete length of frame
    KASEIKYO_STOP_BIT,                                                  // stop_bit:        flag: frame has stop bit
    KASEIKYO_LSB,                                                       // lsb_first:       flag: LSB first
    KASEIKYO_FLAGS                                                      // flags:           some flags
};

#endif

#if IRMP_SUPPORT_PANASONIC_PROTOCOL==1

static const IRMP_PARAMETER panasonic_param =
{
    IRMP_PANASONIC_PROTOCOL,                                            // protocol:        ir protocol
    PANASONIC_PULSE_LEN_MIN,                                            // pulse_1_len_min: minimum length of pulse with bit value 1
    PANASONIC_PULSE_LEN_MAX,                                            // pulse_1_len_max: maximum length of pulse with bit value 1
    PANASONIC_1_PAUSE_LEN_MIN,                                          // space_1_len_min: minimum length of space with bit value 1
    PANASONIC_1_PAUSE_LEN_MAX,                                          // space_1_len_max: maximum length of space with bit value 1
    PANASONIC_PULSE_LEN_MIN,                                            // pulse_0_len_min: minimum length of pulse with bit value 0
    PANASONIC_PULSE_LEN_MAX,                                            // pulse_0_len_max: maximum length of pulse with bit value 0
    PANASONIC_0_PAUSE_LEN_MIN,                                          // space_0_len_min: minimum length of space with bit value 0
    PANASONIC_0_PAUSE_LEN_MAX,                                          // space_0_len_max: maximum length of space with bit value 0
    PANASONIC_ADDRESS_OFFSET,                                           // address_offset:  address offset
    PANASONIC_ADDRESS_OFFSET + PANASONIC_ADDRESS_LEN,                   // address_end:     end of address
    PANASONIC_COMMAND_OFFSET,                                           // command_offset:  command offset
    PANASONIC_COMMAND_OFFSET + PANASONIC_COMMAND_LEN,                   // command_end:     end of command
    PANASONIC_COMPLETE_DATA_LEN,                                        // complete_len:    complete length of frame
    PANASONIC_STOP_BIT,                                                 // stop_bit:        flag: frame has stop bit
    PANASONIC_LSB,                                                      // lsb_first:       flag: LSB first
    PANASONIC_FLAGS                                                     // flags:           some flags
};

#endif

#if IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1

static const IRMP_PARAMETER mitsu_heavy_param =
{
    IRMP_MITSU_HEAVY_PROTOCOL,                                          // protocol:        ir protocol
    MITSU_HEAVY_PULSE_LEN_MIN,                                          // pulse_1_len_min: minimum length of pulse with bit value 1
    MITSU_HEAVY_PULSE_LEN_MAX,                                          // pulse_1_len_max: maximum length of pulse with bit value 1
    MITSU_HEAVY_1_PAUSE_LEN_MIN,                                        // space_1_len_min: minimum length of space with bit value 1
    MITSU_HEAVY_1_PAUSE_LEN_MAX,                                        // space_1_len_max: maximum length of space with bit value 1
    MITSU_HEAVY_PULSE_LEN_MIN,                                          // pulse_0_len_min: minimum length of pulse with bit value 0
    MITSU_HEAVY_PULSE_LEN_MAX,                                          // pulse_0_len_max: maximum length of pulse with bit value 0
    MITSU_HEAVY_0_PAUSE_LEN_MIN,                                        // space_0_len_min: minimum length of space with bit value 0
    MITSU_HEAVY_0_PAUSE_LEN_MAX,                                        // space_0_len_max: maximum length of space with bit value 0
    MITSU_HEAVY_ADDRESS_OFFSET,                                         // address_offset:  address offset
    MITSU_HEAVY_ADDRESS_OFFSET + MITSU_HEAVY_ADDRESS_LEN,               // address_end:     end of address
    MITSU_HEAVY_COMMAND_OFFSET,                                         // command_offset:  command offset
    MITSU_HEAVY_COMMAND_OFFSET + MITSU_HEAVY_COMMAND_LEN,               // command_end:     end of command
    MITSU_HEAVY_COMPLETE_DATA_LEN,                                      // complete_len:    complete length of frame
    MITSU_HEAVY_STOP_BIT,                                               // stop_bit:        flag: frame has stop bit
    MITSU_HEAVY_LSB,                                                    // lsb_first:       flag: LSB first
    MITSU_HEAVY_FLAGS                                                   // flags:           some flags
};

#endif

#if IRMP_SUPPORT_VINCENT_PROTOCOL==1

static const IRMP_PARAMETER vincent_param =
{
    IRMP_VINCENT_PROTOCOL,                                              // protocol:        ir protocol
    VINCENT_PULSE_LEN_MIN,                                              // pulse_1_len_min: minimum length of pulse with bit value 1
    VINCENT_PULSE_LEN_MAX,                                              // pulse_1_len_max: maximum length of pulse with bit value 1
    VINCENT_1_PAUSE_LEN_MIN,                                            // space_1_len_min: minimum length of space with bit value 1
    VINCENT_1_PAUSE_LEN_MAX,                                            // space_1_len_max: maximum length of space with bit value 1
    VINCENT_PULSE_LEN_MIN,                                              // pulse_0_len_min: minimum length of pulse with bit value 0
    VINCENT_PULSE_LEN_MAX,                                              // pulse_0_len_max: maximum length of pulse with bit value 0
    VINCENT_0_PAUSE_LEN_MIN,                                            // space_0_len_min: minimum length of space with bit value 0
    VINCENT_0_PAUSE_LEN_MAX,                                            // space_0_len_max: maximum length of space with bit value 0
    VINCENT_ADDRESS_OFFSET,                                             // address_offset:  address offset
    VINCENT_ADDRESS_OFFSET + VINCENT_ADDRESS_LEN,                       // address_end:     end of address
    VINCENT_COMMAND_OFFSET,                                             // command_offset:  command offset
    VINCENT_COMMAND_OFFSET + VINCENT_COMMAND_LEN,                       // command_end:     end of command
    VINCENT_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame
    VINCENT_STOP_BIT,                                                   // stop_bit:        flag: frame has stop bit
    VINCENT_LSB,                                                        // lsb_first:       flag: LSB first
    VINCENT_FLAGS                                                       // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RECS80_PROTOCOL==1

static const IRMP_PARAMETER recs80_param =
{
    IRMP_RECS80_PROTOCOL,                                               // protocol:        ir protocol
    RECS80_PULSE_LEN_MIN,                                               // pulse_1_len_min: minimum length of pulse with bit value 1
    RECS80_PULSE_LEN_MAX,                                               // pulse_1_len_max: maximum length of pulse with bit value 1
    RECS80_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    RECS80_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    RECS80_PULSE_LEN_MIN,                                               // pulse_0_len_min: minimum length of pulse with bit value 0
    RECS80_PULSE_LEN_MAX,                                               // pulse_0_len_max: maximum length of pulse with bit value 0
    RECS80_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    RECS80_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    RECS80_ADDRESS_OFFSET,                                              // address_offset:  address offset
    RECS80_ADDRESS_OFFSET + RECS80_ADDRESS_LEN,                         // address_end:     end of address
    RECS80_COMMAND_OFFSET,                                              // command_offset:  command offset
    RECS80_COMMAND_OFFSET + RECS80_COMMAND_LEN,                         // command_end:     end of command
    RECS80_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    RECS80_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    RECS80_LSB,                                                         // lsb_first:       flag: LSB first
    RECS80_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RC5_PROTOCOL==1

static const IRMP_PARAMETER rc5_param =
{
    IRMP_RC5_PROTOCOL,                                                  // protocol:        ir protocol
    RC5_BIT_LEN_MIN,                                                    // pulse_1_len_min: here: minimum length of short pulse
    RC5_BIT_LEN_MAX,                                                    // pulse_1_len_max: here: maximum length of short pulse
    RC5_BIT_LEN_MIN,                                                    // space_1_len_min: here: minimum length of short pause
    RC5_BIT_LEN_MAX,                                                    // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    RC5_ADDRESS_OFFSET,                                                 // address_offset:  address offset
    RC5_ADDRESS_OFFSET + RC5_ADDRESS_LEN,                               // address_end:     end of address
    RC5_COMMAND_OFFSET,                                                 // command_offset:  command offset
    RC5_COMMAND_OFFSET + RC5_COMMAND_LEN,                               // command_end:     end of command
    RC5_COMPLETE_DATA_LEN,                                              // complete_len:    complete length of frame
    RC5_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    RC5_LSB,                                                            // lsb_first:       flag: LSB first
    RC5_FLAGS                                                           // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RCII_PROTOCOL==1

static const IRMP_PARAMETER rcii_param =
{
    IRMP_RCII_PROTOCOL,                                                 // protocol:        ir protocol
    RCII_BIT_LEN_MIN,                                                   // pulse_1_len_min: here: minimum length of short pulse
    RCII_BIT_LEN_MAX,                                                   // pulse_1_len_max: here: maximum length of short pulse
    RCII_BIT_LEN_MIN,                                                   // space_1_len_min: here: minimum length of short pause
    RCII_BIT_LEN_MAX,                                                   // space_1_len_max: here: maximum length of short pause
    RCII_BIT_LEN_MIN,                                                                  // pulse_0_len_min: here: not used
    RCII_BIT_LEN_MAX,                                                                  // pulse_0_len_max: here: not used
    RCII_BIT_LEN_MIN,                                                                  // space_0_len_min: here: not used
    RCII_BIT_LEN_MAX,                                                                  // space_0_len_max: here: not used
    RCII_ADDRESS_OFFSET,                                                // address_offset:  address offset
    RCII_ADDRESS_OFFSET + RCII_ADDRESS_LEN,                             // address_end:     end of address
    RCII_COMMAND_OFFSET,                                                // command_offset:  command offset
    RCII_COMMAND_OFFSET + RCII_COMMAND_LEN,                             // command_end:     end of command
    RCII_COMPLETE_DATA_LEN,                                             // complete_len:    complete length of frame
    RCII_STOP_BIT,                                                      // stop_bit:        flag: frame has stop bit
    RCII_LSB,                                                           // lsb_first:       flag: LSB first
    RCII_FLAGS                                                          // flags:           some flags
};

#endif

#if IRMP_SUPPORT_S100_PROTOCOL==1

static const IRMP_PARAMETER s100_param =
{
    IRMP_S100_PROTOCOL,                                                 // protocol:        ir protocol
    S100_BIT_LEN_MIN,                                                   // pulse_1_len_min: here: minimum length of short pulse
    S100_BIT_LEN_MAX,                                                   // pulse_1_len_max: here: maximum length of short pulse
    S100_BIT_LEN_MIN,                                                   // space_1_len_min: here: minimum length of short pause
    S100_BIT_LEN_MAX,                                                   // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    S100_ADDRESS_OFFSET,                                                // address_offset:  address offset
    S100_ADDRESS_OFFSET + S100_ADDRESS_LEN,                             // address_end:     end of address
    S100_COMMAND_OFFSET,                                                // command_offset:  command offset
    S100_COMMAND_OFFSET + S100_COMMAND_LEN,                             // command_end:     end of command
    S100_COMPLETE_DATA_LEN,                                             // complete_len:    complete length of frame
    S100_STOP_BIT,                                                      // stop_bit:        flag: frame has stop bit
    S100_LSB,                                                           // lsb_first:       flag: LSB first
    S100_FLAGS                                                          // flags:           some flags
};

#endif

#if IRMP_SUPPORT_DENON_PROTOCOL==1

static const IRMP_PARAMETER denon_param =
{
    IRMP_DENON_PROTOCOL,                                                // protocol:        ir protocol
    DENON_PULSE_LEN_MIN,                                                // pulse_1_len_min: minimum length of pulse with bit value 1
    DENON_PULSE_LEN_MAX,                                                // pulse_1_len_max: maximum length of pulse with bit value 1
    DENON_1_PAUSE_LEN_MIN,                                              // space_1_len_min: minimum length of space with bit value 1
    DENON_1_PAUSE_LEN_MAX,                                              // space_1_len_max: maximum length of space with bit value 1
    DENON_PULSE_LEN_MIN,                                                // pulse_0_len_min: minimum length of pulse with bit value 0
    DENON_PULSE_LEN_MAX,                                                // pulse_0_len_max: maximum length of pulse with bit value 0
    DENON_0_PAUSE_LEN_MIN,                                              // space_0_len_min: minimum length of space with bit value 0
    DENON_0_PAUSE_LEN_MAX,                                              // space_0_len_max: maximum length of space with bit value 0
    DENON_ADDRESS_OFFSET,                                               // address_offset:  address offset
    DENON_ADDRESS_OFFSET + DENON_ADDRESS_LEN,                           // address_end:     end of address
    DENON_COMMAND_OFFSET,                                               // command_offset:  command offset
    DENON_COMMAND_OFFSET + DENON_COMMAND_LEN,                           // command_end:     end of command
    DENON_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    DENON_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    DENON_LSB,                                                          // lsb_first:       flag: LSB first
    DENON_FLAGS                                                         // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RC6_PROTOCOL==1

static const IRMP_PARAMETER rc6_param =
{
    IRMP_RC6_PROTOCOL,                                                  // protocol:        ir protocol

    RC6_BIT_PULSE_LEN_MIN,                                              // pulse_1_len_min: here: minimum length of short pulse
    RC6_BIT_PULSE_LEN_MAX,                                              // pulse_1_len_max: here: maximum length of short pulse
    RC6_BIT_PAUSE_LEN_MIN,                                              // space_1_len_min: here: minimum length of short pause
    RC6_BIT_PAUSE_LEN_MAX,                                              // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    RC6_ADDRESS_OFFSET,                                                 // address_offset:  address offset
    RC6_ADDRESS_OFFSET + RC6_ADDRESS_LEN,                               // address_end:     end of address
    RC6_COMMAND_OFFSET,                                                 // command_offset:  command offset
    RC6_COMMAND_OFFSET + RC6_COMMAND_LEN,                               // command_end:     end of command
    RC6_COMPLETE_DATA_LEN_SHORT,                                        // complete_len:    complete length of frame
    RC6_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    RC6_LSB,                                                            // lsb_first:       flag: LSB first
    RC6_FLAGS                                                           // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RECS80EXT_PROTOCOL==1

static const IRMP_PARAMETER recs80ext_param =
{
    IRMP_RECS80EXT_PROTOCOL,                                            // protocol:        ir protocol
    RECS80EXT_PULSE_LEN_MIN,                                            // pulse_1_len_min: minimum length of pulse with bit value 1
    RECS80EXT_PULSE_LEN_MAX,                                            // pulse_1_len_max: maximum length of pulse with bit value 1
    RECS80EXT_1_PAUSE_LEN_MIN,                                          // space_1_len_min: minimum length of space with bit value 1
    RECS80EXT_1_PAUSE_LEN_MAX,                                          // space_1_len_max: maximum length of space with bit value 1
    RECS80EXT_PULSE_LEN_MIN,                                            // pulse_0_len_min: minimum length of pulse with bit value 0
    RECS80EXT_PULSE_LEN_MAX,                                            // pulse_0_len_max: maximum length of pulse with bit value 0
    RECS80EXT_0_PAUSE_LEN_MIN,                                          // space_0_len_min: minimum length of space with bit value 0
    RECS80EXT_0_PAUSE_LEN_MAX,                                          // space_0_len_max: maximum length of space with bit value 0
    RECS80EXT_ADDRESS_OFFSET,                                           // address_offset:  address offset
    RECS80EXT_ADDRESS_OFFSET + RECS80EXT_ADDRESS_LEN,                   // address_end:     end of address
    RECS80EXT_COMMAND_OFFSET,                                           // command_offset:  command offset
    RECS80EXT_COMMAND_OFFSET + RECS80EXT_COMMAND_LEN,                   // command_end:     end of command
    RECS80EXT_COMPLETE_DATA_LEN,                                        // complete_len:    complete length of frame
    RECS80EXT_STOP_BIT,                                                 // stop_bit:        flag: frame has stop bit
    RECS80EXT_LSB,                                                      // lsb_first:       flag: LSB first
    RECS80EXT_FLAGS                                                     // flags:           some flags
};

#endif

#if IRMP_SUPPORT_NUBERT_PROTOCOL==1

static const IRMP_PARAMETER nubert_param =
{
    IRMP_NUBERT_PROTOCOL,                                               // protocol:        ir protocol
    NUBERT_1_PULSE_LEN_MIN,                                             // pulse_1_len_min: minimum length of pulse with bit value 1
    NUBERT_1_PULSE_LEN_MAX,                                             // pulse_1_len_max: maximum length of pulse with bit value 1
    NUBERT_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    NUBERT_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    NUBERT_0_PULSE_LEN_MIN,                                             // pulse_0_len_min: minimum length of pulse with bit value 0
    NUBERT_0_PULSE_LEN_MAX,                                             // pulse_0_len_max: maximum length of pulse with bit value 0
    NUBERT_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    NUBERT_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    NUBERT_ADDRESS_OFFSET,                                              // address_offset:  address offset
    NUBERT_ADDRESS_OFFSET + NUBERT_ADDRESS_LEN,                         // address_end:     end of address
    NUBERT_COMMAND_OFFSET,                                              // command_offset:  command offset
    NUBERT_COMMAND_OFFSET + NUBERT_COMMAND_LEN,                         // command_end:     end of command
    NUBERT_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    NUBERT_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    NUBERT_LSB,                                                         // lsb_first:       flag: LSB first
    NUBERT_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_FAN_PROTOCOL==1

static const IRMP_PARAMETER fan_param =
{
    IRMP_FAN_PROTOCOL,                                                  // protocol:        ir protocol
    FAN_1_PULSE_LEN_MIN,                                                // pulse_1_len_min: minimum length of pulse with bit value 1
    FAN_1_PULSE_LEN_MAX,                                                // pulse_1_len_max: maximum length of pulse with bit value 1
    FAN_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    FAN_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    FAN_0_PULSE_LEN_MIN,                                                // pulse_0_len_min: minimum length of pulse with bit value 0
    FAN_0_PULSE_LEN_MAX,                                                // pulse_0_len_max: maximum length of pulse with bit value 0
    FAN_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    FAN_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    FAN_ADDRESS_OFFSET,                                                 // address_offset:  address offset
    FAN_ADDRESS_OFFSET + FAN_ADDRESS_LEN,                               // address_end:     end of address
    FAN_COMMAND_OFFSET,                                                 // command_offset:  command offset
    FAN_COMMAND_OFFSET + FAN_COMMAND_LEN,                               // command_end:     end of command
    FAN_COMPLETE_DATA_LEN,                                              // complete_len:    complete length of frame
    FAN_STOP_BIT,                                                       // stop_bit:        flag: frame has NO stop bit
    FAN_LSB,                                                            // lsb_first:       flag: LSB first
    FAN_FLAGS                                                           // flags:           some flags
};

#endif

#if IRMP_SUPPORT_SPEAKER_PROTOCOL==1

static const IRMP_PARAMETER speaker_param =
{
    IRMP_SPEAKER_PROTOCOL,                                              // protocol:        ir protocol
    SPEAKER_1_PULSE_LEN_MIN,                                            // pulse_1_len_min: minimum length of pulse with bit value 1
    SPEAKER_1_PULSE_LEN_MAX,                                            // pulse_1_len_max: maximum length of pulse with bit value 1
    SPEAKER_1_PAUSE_LEN_MIN,                                            // space_1_len_min: minimum length of space with bit value 1
    SPEAKER_1_PAUSE_LEN_MAX,                                            // space_1_len_max: maximum length of space with bit value 1
    SPEAKER_0_PULSE_LEN_MIN,                                            // pulse_0_len_min: minimum length of pulse with bit value 0
    SPEAKER_0_PULSE_LEN_MAX,                                            // pulse_0_len_max: maximum length of pulse with bit value 0
    SPEAKER_0_PAUSE_LEN_MIN,                                            // space_0_len_min: minimum length of space with bit value 0
    SPEAKER_0_PAUSE_LEN_MAX,                                            // space_0_len_max: maximum length of space with bit value 0
    SPEAKER_ADDRESS_OFFSET,                                             // address_offset:  address offset
    SPEAKER_ADDRESS_OFFSET + SPEAKER_ADDRESS_LEN,                       // address_end:     end of address
    SPEAKER_COMMAND_OFFSET,                                             // command_offset:  command offset
    SPEAKER_COMMAND_OFFSET + SPEAKER_COMMAND_LEN,                       // command_end:     end of command
    SPEAKER_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame
    SPEAKER_STOP_BIT,                                                   // stop_bit:        flag: frame has stop bit
    SPEAKER_LSB,                                                        // lsb_first:       flag: LSB first
    SPEAKER_FLAGS                                                       // flags:           some flags
};

#endif

#if IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1

static const IRMP_PARAMETER bang_olufsen_param =
{
    IRMP_BANG_OLUFSEN_PROTOCOL,                                         // protocol:        ir protocol
    BANG_OLUFSEN_PULSE_LEN_MIN,                                         // pulse_1_len_min: minimum length of pulse with bit value 1
    BANG_OLUFSEN_PULSE_LEN_MAX,                                         // pulse_1_len_max: maximum length of pulse with bit value 1
    BANG_OLUFSEN_1_PAUSE_LEN_MIN,                                       // space_1_len_min: minimum length of space with bit value 1
    BANG_OLUFSEN_1_PAUSE_LEN_MAX,                                       // space_1_len_max: maximum length of space with bit value 1
    BANG_OLUFSEN_PULSE_LEN_MIN,                                         // pulse_0_len_min: minimum length of pulse with bit value 0
    BANG_OLUFSEN_PULSE_LEN_MAX,                                         // pulse_0_len_max: maximum length of pulse with bit value 0
    BANG_OLUFSEN_0_PAUSE_LEN_MIN,                                       // space_0_len_min: minimum length of space with bit value 0
    BANG_OLUFSEN_0_PAUSE_LEN_MAX,                                       // space_0_len_max: maximum length of space with bit value 0
    BANG_OLUFSEN_ADDRESS_OFFSET,                                        // address_offset:  address offset
    BANG_OLUFSEN_ADDRESS_OFFSET + BANG_OLUFSEN_ADDRESS_LEN,             // address_end:     end of address
    BANG_OLUFSEN_COMMAND_OFFSET,                                        // command_offset:  command offset
    BANG_OLUFSEN_COMMAND_OFFSET + BANG_OLUFSEN_COMMAND_LEN,             // command_end:     end of command
    BANG_OLUFSEN_COMPLETE_DATA_LEN,                                     // complete_len:    complete length of frame
    BANG_OLUFSEN_STOP_BIT,                                              // stop_bit:        flag: frame has stop bit
    BANG_OLUFSEN_LSB,                                                   // lsb_first:       flag: LSB first
    BANG_OLUFSEN_FLAGS                                                  // flags:           some flags
};

#endif

#if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1

static uint8_t first_bit;

static const IRMP_PARAMETER grundig_param =
{
    IRMP_GRUNDIG_PROTOCOL,                                              // protocol:        ir protocol

    GRUNDIG_NOKIA_IR60_BIT_LEN_MIN,                                     // pulse_1_len_min: here: minimum length of short pulse
    GRUNDIG_NOKIA_IR60_BIT_LEN_MAX,                                     // pulse_1_len_max: here: maximum length of short pulse
    GRUNDIG_NOKIA_IR60_BIT_LEN_MIN,                                     // space_1_len_min: here: minimum length of short pause
    GRUNDIG_NOKIA_IR60_BIT_LEN_MAX,                                     // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    GRUNDIG_ADDRESS_OFFSET,                                             // address_offset:  address offset
    GRUNDIG_ADDRESS_OFFSET + GRUNDIG_ADDRESS_LEN,                       // address_end:     end of address
    GRUNDIG_COMMAND_OFFSET,                                             // command_offset:  command offset
    GRUNDIG_COMMAND_OFFSET + GRUNDIG_COMMAND_LEN + 1,                   // command_end:     end of command (USE 1 bit MORE to STORE NOKIA DATA!)
    NOKIA_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame, here: NOKIA instead of GRUNDIG!
    GRUNDIG_NOKIA_IR60_STOP_BIT,                                        // stop_bit:        flag: frame has stop bit
    GRUNDIG_NOKIA_IR60_LSB,                                             // lsb_first:       flag: LSB first
    GRUNDIG_NOKIA_IR60_FLAGS                                            // flags:           some flags
};

#endif

#if IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1

static const IRMP_PARAMETER ruwido_param =
{
    IRMP_RUWIDO_PROTOCOL,                                               // protocol:        ir protocol
    SIEMENS_OR_RUWIDO_BIT_PULSE_LEN_MIN,                                // pulse_1_len_min: here: minimum length of short pulse
    SIEMENS_OR_RUWIDO_BIT_PULSE_LEN_MAX,                                // pulse_1_len_max: here: maximum length of short pulse
    SIEMENS_OR_RUWIDO_BIT_PAUSE_LEN_MIN,                                // space_1_len_min: here: minimum length of short pause
    SIEMENS_OR_RUWIDO_BIT_PAUSE_LEN_MAX,                                // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    RUWIDO_ADDRESS_OFFSET,                                              // address_offset:  address offset
    RUWIDO_ADDRESS_OFFSET + RUWIDO_ADDRESS_LEN,                         // address_end:     end of address
    RUWIDO_COMMAND_OFFSET,                                              // command_offset:  command offset
    RUWIDO_COMMAND_OFFSET + RUWIDO_COMMAND_LEN,                         // command_end:     end of command
    SIEMENS_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame, here: SIEMENS instead of RUWIDO!
    SIEMENS_OR_RUWIDO_STOP_BIT,                                         // stop_bit:        flag: frame has stop bit
    SIEMENS_OR_RUWIDO_LSB,                                              // lsb_first:       flag: LSB first
    SIEMENS_OR_RUWIDO_FLAGS                                             // flags:           some flags
};

#endif

#if IRMP_SUPPORT_FDC_PROTOCOL==1

static const IRMP_PARAMETER fdc_param =
{
    IRMP_FDC_PROTOCOL,                                                  // protocol:        ir protocol
    FDC_PULSE_LEN_MIN,                                                  // pulse_1_len_min: minimum length of pulse with bit value 1
    FDC_PULSE_LEN_MAX,                                                  // pulse_1_len_max: maximum length of pulse with bit value 1
    FDC_1_PAUSE_LEN_MIN,                                                // space_1_len_min: minimum length of space with bit value 1
    FDC_1_PAUSE_LEN_MAX,                                                // space_1_len_max: maximum length of space with bit value 1
    FDC_PULSE_LEN_MIN,                                                  // pulse_0_len_min: minimum length of pulse with bit value 0
    FDC_PULSE_LEN_MAX,                                                  // pulse_0_len_max: maximum length of pulse with bit value 0
    FDC_0_PAUSE_LEN_MIN,                                                // space_0_len_min: minimum length of space with bit value 0
    FDC_0_PAUSE_LEN_MAX,                                                // space_0_len_max: maximum length of space with bit value 0
    FDC_ADDRESS_OFFSET,                                                 // address_offset:  address offset
    FDC_ADDRESS_OFFSET + FDC_ADDRESS_LEN,                               // address_end:     end of address
    FDC_COMMAND_OFFSET,                                                 // command_offset:  command offset
    FDC_COMMAND_OFFSET + FDC_COMMAND_LEN,                               // command_end:     end of command
    FDC_COMPLETE_DATA_LEN,                                              // complete_len:    complete length of frame
    FDC_STOP_BIT,                                                       // stop_bit:        flag: frame has stop bit
    FDC_LSB,                                                            // lsb_first:       flag: LSB first
    FDC_FLAGS                                                           // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RCCAR_PROTOCOL==1

static const IRMP_PARAMETER rccar_param =
{
    IRMP_RCCAR_PROTOCOL,                                                // protocol:        ir protocol
    RCCAR_PULSE_LEN_MIN,                                                // pulse_1_len_min: minimum length of pulse with bit value 1
    RCCAR_PULSE_LEN_MAX,                                                // pulse_1_len_max: maximum length of pulse with bit value 1
    RCCAR_1_PAUSE_LEN_MIN,                                              // space_1_len_min: minimum length of space with bit value 1
    RCCAR_1_PAUSE_LEN_MAX,                                              // space_1_len_max: maximum length of space with bit value 1
    RCCAR_PULSE_LEN_MIN,                                                // pulse_0_len_min: minimum length of pulse with bit value 0
    RCCAR_PULSE_LEN_MAX,                                                // pulse_0_len_max: maximum length of pulse with bit value 0
    RCCAR_0_PAUSE_LEN_MIN,                                              // space_0_len_min: minimum length of space with bit value 0
    RCCAR_0_PAUSE_LEN_MAX,                                              // space_0_len_max: maximum length of space with bit value 0
    RCCAR_ADDRESS_OFFSET,                                               // address_offset:  address offset
    RCCAR_ADDRESS_OFFSET + RCCAR_ADDRESS_LEN,                           // address_end:     end of address
    RCCAR_COMMAND_OFFSET,                                               // command_offset:  command offset
    RCCAR_COMMAND_OFFSET + RCCAR_COMMAND_LEN,                           // command_end:     end of command
    RCCAR_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    RCCAR_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    RCCAR_LSB,                                                          // lsb_first:       flag: LSB first
    RCCAR_FLAGS                                                         // flags:           some flags
};

#endif

#if IRMP_SUPPORT_NIKON_PROTOCOL==1

static const IRMP_PARAMETER nikon_param =
{
    IRMP_NIKON_PROTOCOL,                                                // protocol:        ir protocol
    NIKON_PULSE_LEN_MIN,                                                // pulse_1_len_min: minimum length of pulse with bit value 1
    NIKON_PULSE_LEN_MAX,                                                // pulse_1_len_max: maximum length of pulse with bit value 1
    NIKON_1_PAUSE_LEN_MIN,                                              // space_1_len_min: minimum length of space with bit value 1
    NIKON_1_PAUSE_LEN_MAX,                                              // space_1_len_max: maximum length of space with bit value 1
    NIKON_PULSE_LEN_MIN,                                                // pulse_0_len_min: minimum length of pulse with bit value 0
    NIKON_PULSE_LEN_MAX,                                                // pulse_0_len_max: maximum length of pulse with bit value 0
    NIKON_0_PAUSE_LEN_MIN,                                              // space_0_len_min: minimum length of space with bit value 0
    NIKON_0_PAUSE_LEN_MAX,                                              // space_0_len_max: maximum length of space with bit value 0
    NIKON_ADDRESS_OFFSET,                                               // address_offset:  address offset
    NIKON_ADDRESS_OFFSET + NIKON_ADDRESS_LEN,                           // address_end:     end of address
    NIKON_COMMAND_OFFSET,                                               // command_offset:  command offset
    NIKON_COMMAND_OFFSET + NIKON_COMMAND_LEN,                           // command_end:     end of command
    NIKON_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    NIKON_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    NIKON_LSB,                                                          // lsb_first:       flag: LSB first
    NIKON_FLAGS                                                         // flags:           some flags
};

#endif

#if IRMP_SUPPORT_KATHREIN_PROTOCOL==1

static const IRMP_PARAMETER kathrein_param =
{
    IRMP_KATHREIN_PROTOCOL,                                             // protocol:        ir protocol
    KATHREIN_1_PULSE_LEN_MIN,                                           // pulse_1_len_min: minimum length of pulse with bit value 1
    KATHREIN_1_PULSE_LEN_MAX,                                           // pulse_1_len_max: maximum length of pulse with bit value 1
    KATHREIN_1_PAUSE_LEN_MIN,                                           // space_1_len_min: minimum length of space with bit value 1
    KATHREIN_1_PAUSE_LEN_MAX,                                           // space_1_len_max: maximum length of space with bit value 1
    KATHREIN_0_PULSE_LEN_MIN,                                           // pulse_0_len_min: minimum length of pulse with bit value 0
    KATHREIN_0_PULSE_LEN_MAX,                                           // pulse_0_len_max: maximum length of pulse with bit value 0
    KATHREIN_0_PAUSE_LEN_MIN,                                           // space_0_len_min: minimum length of space with bit value 0
    KATHREIN_0_PAUSE_LEN_MAX,                                           // space_0_len_max: maximum length of space with bit value 0
    KATHREIN_ADDRESS_OFFSET,                                            // address_offset:  address offset
    KATHREIN_ADDRESS_OFFSET + KATHREIN_ADDRESS_LEN,                     // address_end:     end of address
    KATHREIN_COMMAND_OFFSET,                                            // command_offset:  command offset
    KATHREIN_COMMAND_OFFSET + KATHREIN_COMMAND_LEN,                     // command_end:     end of command
    KATHREIN_COMPLETE_DATA_LEN,                                         // complete_len:    complete length of frame
    KATHREIN_STOP_BIT,                                                  // stop_bit:        flag: frame has stop bit
    KATHREIN_LSB,                                                       // lsb_first:       flag: LSB first
    KATHREIN_FLAGS                                                      // flags:           some flags
};

#endif

#if IRMP_SUPPORT_NETBOX_PROTOCOL==1

static const IRMP_PARAMETER netbox_param =
{
    IRMP_NETBOX_PROTOCOL,                                               // protocol:        ir protocol
    NETBOX_PULSE_LEN,                                                   // pulse_1_len_min: minimum length of pulse with bit value 1, here: exact value
    NETBOX_PULSE_REST_LEN,                                              // pulse_1_len_max: maximum length of pulse with bit value 1, here: rest value
    NETBOX_PAUSE_LEN,                                                   // space_1_len_min: minimum length of space with bit value 1, here: exact value
    NETBOX_PAUSE_REST_LEN,                                              // space_1_len_max: maximum length of space with bit value 1, here: rest value
    NETBOX_PULSE_LEN,                                                   // pulse_0_len_min: minimum length of pulse with bit value 0, here: exact value
    NETBOX_PULSE_REST_LEN,                                              // pulse_0_len_max: maximum length of pulse with bit value 0, here: rest value
    NETBOX_PAUSE_LEN,                                                   // space_0_len_min: minimum length of space with bit value 0, here: exact value
    NETBOX_PAUSE_REST_LEN,                                              // space_0_len_max: maximum length of space with bit value 0, here: rest value
    NETBOX_ADDRESS_OFFSET,                                              // address_offset:  address offset
    NETBOX_ADDRESS_OFFSET + NETBOX_ADDRESS_LEN,                         // address_end:     end of address
    NETBOX_COMMAND_OFFSET,                                              // command_offset:  command offset
    NETBOX_COMMAND_OFFSET + NETBOX_COMMAND_LEN,                         // command_end:     end of command
    NETBOX_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    NETBOX_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    NETBOX_LSB,                                                         // lsb_first:       flag: LSB first
    NETBOX_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_LEGO_PROTOCOL==1

static const IRMP_PARAMETER lego_param =
{
    IRMP_LEGO_PROTOCOL,                                                 // protocol:        ir protocol
    LEGO_PULSE_LEN_MIN,                                                 // pulse_1_len_min: minimum length of pulse with bit value 1
    LEGO_PULSE_LEN_MAX,                                                 // pulse_1_len_max: maximum length of pulse with bit value 1
    LEGO_1_PAUSE_LEN_MIN,                                               // space_1_len_min: minimum length of space with bit value 1
    LEGO_1_PAUSE_LEN_MAX,                                               // space_1_len_max: maximum length of space with bit value 1
    LEGO_PULSE_LEN_MIN,                                                 // pulse_0_len_min: minimum length of pulse with bit value 0
    LEGO_PULSE_LEN_MAX,                                                 // pulse_0_len_max: maximum length of pulse with bit value 0
    LEGO_0_PAUSE_LEN_MIN,                                               // space_0_len_min: minimum length of space with bit value 0
    LEGO_0_PAUSE_LEN_MAX,                                               // space_0_len_max: maximum length of space with bit value 0
    LEGO_ADDRESS_OFFSET,                                                // address_offset:  address offset
    LEGO_ADDRESS_OFFSET + LEGO_ADDRESS_LEN,                             // address_end:     end of address
    LEGO_COMMAND_OFFSET,                                                // command_offset:  command offset
    LEGO_COMMAND_OFFSET + LEGO_COMMAND_LEN,                             // command_end:     end of command
    LEGO_COMPLETE_DATA_LEN,                                             // complete_len:    complete length of frame
    LEGO_STOP_BIT,                                                      // stop_bit:        flag: frame has stop bit
    LEGO_LSB,                                                           // lsb_first:       flag: LSB first
    LEGO_FLAGS                                                          // flags:           some flags
};

#endif

#if IRMP_SUPPORT_IRMP16_PROTOCOL==1

static const IRMP_PARAMETER irmp16_param =
{
    IRMP_IRMP16_PROTOCOL,                                               // protocol:        ir protocol
    IRMP16_PULSE_LEN_MIN,                                               // pulse_1_len_min: minimum length of pulse with bit value 1
    IRMP16_PULSE_LEN_MAX,                                               // pulse_1_len_max: maximum length of pulse with bit value 1
    IRMP16_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    IRMP16_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    IRMP16_PULSE_LEN_MIN,                                               // pulse_0_len_min: minimum length of pulse with bit value 0
    IRMP16_PULSE_LEN_MAX,                                               // pulse_0_len_max: maximum length of pulse with bit value 0
    IRMP16_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    IRMP16_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    IRMP16_ADDRESS_OFFSET,                                              // address_offset:  address offset
    IRMP16_ADDRESS_OFFSET + IRMP16_ADDRESS_LEN,                         // address_end:     end of address
    IRMP16_COMMAND_OFFSET,                                              // command_offset:  command offset
    IRMP16_COMMAND_OFFSET + IRMP16_COMMAND_LEN,                         // command_end:     end of command
    IRMP16_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    IRMP16_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    IRMP16_LSB,                                                         // lsb_first:       flag: LSB first
    IRMP16_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_GREE_PROTOCOL==1

static const IRMP_PARAMETER gree_param =
{
    IRMP_GREE_PROTOCOL,                                               // protocol:        ir protocol
    GREE_PULSE_LEN_MIN,                                               // pulse_1_len_min: minimum length of pulse with bit value 1
    GREE_PULSE_LEN_MAX,                                               // pulse_1_len_max: maximum length of pulse with bit value 1
    GREE_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    GREE_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    GREE_PULSE_LEN_MIN,                                               // pulse_0_len_min: minimum length of pulse with bit value 0
    GREE_PULSE_LEN_MAX,                                               // pulse_0_len_max: maximum length of pulse with bit value 0
    GREE_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    GREE_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    GREE_ADDRESS_OFFSET,                                              // address_offset:  address offset
    GREE_ADDRESS_OFFSET + GREE_ADDRESS_LEN,                         // address_end:     end of address
    GREE_COMMAND_OFFSET,                                              // command_offset:  command offset
    GREE_COMMAND_OFFSET + GREE_COMMAND_LEN,                         // command_end:     end of command
    GREE_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    GREE_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    GREE_LSB,                                                         // lsb_first:       flag: LSB first
    GREE_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_THOMSON_PROTOCOL==1

static const IRMP_PARAMETER thomson_param =
{
    IRMP_THOMSON_PROTOCOL,                                              // protocol:        ir protocol
    THOMSON_PULSE_LEN_MIN,                                              // pulse_1_len_min: minimum length of pulse with bit value 1
    THOMSON_PULSE_LEN_MAX,                                              // pulse_1_len_max: maximum length of pulse with bit value 1
    THOMSON_1_PAUSE_LEN_MIN,                                            // space_1_len_min: minimum length of space with bit value 1
    THOMSON_1_PAUSE_LEN_MAX,                                            // space_1_len_max: maximum length of space with bit value 1
    THOMSON_PULSE_LEN_MIN,                                              // pulse_0_len_min: minimum length of pulse with bit value 0
    THOMSON_PULSE_LEN_MAX,                                              // pulse_0_len_max: maximum length of pulse with bit value 0
    THOMSON_0_PAUSE_LEN_MIN,                                            // space_0_len_min: minimum length of space with bit value 0
    THOMSON_0_PAUSE_LEN_MAX,                                            // space_0_len_max: maximum length of space with bit value 0
    THOMSON_ADDRESS_OFFSET,                                             // address_offset:  address offset
    THOMSON_ADDRESS_OFFSET + THOMSON_ADDRESS_LEN,                       // address_end:     end of address
    THOMSON_COMMAND_OFFSET,                                             // command_offset:  command offset
    THOMSON_COMMAND_OFFSET + THOMSON_COMMAND_LEN,                       // command_end:     end of command
    THOMSON_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame
    THOMSON_STOP_BIT,                                                   // stop_bit:        flag: frame has stop bit
    THOMSON_LSB,                                                        // lsb_first:       flag: LSB first
    THOMSON_FLAGS                                                       // flags:           some flags
};

#endif

#if IRMP_SUPPORT_BOSE_PROTOCOL==1

static const IRMP_PARAMETER bose_param =
{
    IRMP_BOSE_PROTOCOL,                                                 // protocol:        ir protocol
    BOSE_PULSE_LEN_MIN,                                                 // pulse_1_len_min: minimum length of pulse with bit value 1
    BOSE_PULSE_LEN_MAX,                                                 // pulse_1_len_max: maximum length of pulse with bit value 1
    BOSE_1_PAUSE_LEN_MIN,                                               // space_1_len_min: minimum length of space with bit value 1
    BOSE_1_PAUSE_LEN_MAX,                                               // space_1_len_max: maximum length of space with bit value 1
    BOSE_PULSE_LEN_MIN,                                                 // pulse_0_len_min: minimum length of pulse with bit value 0
    BOSE_PULSE_LEN_MAX,                                                 // pulse_0_len_max: maximum length of pulse with bit value 0
    BOSE_0_PAUSE_LEN_MIN,                                               // space_0_len_min: minimum length of space with bit value 0
    BOSE_0_PAUSE_LEN_MAX,                                               // space_0_len_max: maximum length of space with bit value 0
    BOSE_ADDRESS_OFFSET,                                                // address_offset:  address offset
    BOSE_ADDRESS_OFFSET + BOSE_ADDRESS_LEN,                             // address_end:     end of address
    BOSE_COMMAND_OFFSET,                                                // command_offset:  command offset
    BOSE_COMMAND_OFFSET + BOSE_COMMAND_LEN,                             // command_end:     end of command
    BOSE_COMPLETE_DATA_LEN,                                             // complete_len:    complete length of frame
    BOSE_STOP_BIT,                                                      // stop_bit:        flag: frame has stop bit
    BOSE_LSB,                                                           // lsb_first:       flag: LSB first
    BOSE_FLAGS                                                          // flags:           some flags
};

#endif

#if IRMP_SUPPORT_A1TVBOX_PROTOCOL==1

static const IRMP_PARAMETER a1tvbox_param =
{
    IRMP_A1TVBOX_PROTOCOL,                                              // protocol:        ir protocol

    A1TVBOX_BIT_PULSE_LEN_MIN,                                          // pulse_1_len_min: here: minimum length of short pulse
    A1TVBOX_BIT_PULSE_LEN_MAX,                                          // pulse_1_len_max: here: maximum length of short pulse
    A1TVBOX_BIT_PAUSE_LEN_MIN,                                          // space_1_len_min: here: minimum length of short pause
    A1TVBOX_BIT_PAUSE_LEN_MAX,                                          // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    A1TVBOX_ADDRESS_OFFSET,                                             // address_offset:  address offset
    A1TVBOX_ADDRESS_OFFSET + A1TVBOX_ADDRESS_LEN,                       // address_end:     end of address
    A1TVBOX_COMMAND_OFFSET,                                             // command_offset:  command offset
    A1TVBOX_COMMAND_OFFSET + A1TVBOX_COMMAND_LEN,                       // command_end:     end of command
    A1TVBOX_COMPLETE_DATA_LEN,                                          // complete_len:    complete length of frame
    A1TVBOX_STOP_BIT,                                                   // stop_bit:        flag: frame has stop bit
    A1TVBOX_LSB,                                                        // lsb_first:       flag: LSB first
    A1TVBOX_FLAGS                                                       // flags:           some flags
};

#endif

#if IRMP_SUPPORT_MERLIN_PROTOCOL==1

static const IRMP_PARAMETER merlin_param =
{
    IRMP_MERLIN_PROTOCOL,                                               // protocol:        ir protocol

    MERLIN_BIT_PULSE_LEN_MIN,                                           // pulse_1_len_min: here: minimum length of short pulse
    MERLIN_BIT_PULSE_LEN_MAX,                                           // pulse_1_len_max: here: maximum length of short pulse
    MERLIN_BIT_PAUSE_LEN_MIN,                                           // space_1_len_min: here: minimum length of short pause
    MERLIN_BIT_PAUSE_LEN_MAX,                                           // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    MERLIN_ADDRESS_OFFSET,                                              // address_offset:  address offset
    MERLIN_ADDRESS_OFFSET + MERLIN_ADDRESS_LEN,                         // address_end:     end of address
    MERLIN_COMMAND_OFFSET,                                              // command_offset:  command offset
    MERLIN_COMMAND_OFFSET + MERLIN_COMMAND_LEN,                         // command_end:     end of command
    MERLIN_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    MERLIN_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    MERLIN_LSB,                                                         // lsb_first:       flag: LSB first
    MERLIN_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1

static const IRMP_PARAMETER ortek_param =
{
    IRMP_ORTEK_PROTOCOL,                                                // protocol:        ir protocol

    ORTEK_BIT_PULSE_LEN_MIN,                                            // pulse_1_len_min: here: minimum length of short pulse
    ORTEK_BIT_PULSE_LEN_MAX,                                            // pulse_1_len_max: here: maximum length of short pulse
    ORTEK_BIT_PAUSE_LEN_MIN,                                            // space_1_len_min: here: minimum length of short pause
    ORTEK_BIT_PAUSE_LEN_MAX,                                            // space_1_len_max: here: maximum length of short pause
    0,                                                                  // pulse_0_len_min: here: not used
    0,                                                                  // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    ORTEK_ADDRESS_OFFSET,                                               // address_offset:  address offset
    ORTEK_ADDRESS_OFFSET + ORTEK_ADDRESS_LEN,                           // address_end:     end of address
    ORTEK_COMMAND_OFFSET,                                               // command_offset:  command offset
    ORTEK_COMMAND_OFFSET + ORTEK_COMMAND_LEN,                           // command_end:     end of command
    ORTEK_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    ORTEK_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    ORTEK_LSB,                                                          // lsb_first:       flag: LSB first
    ORTEK_FLAGS                                                         // flags:           some flags
};

#endif

#if IRMP_SUPPORT_ROOMBA_PROTOCOL==1

static const IRMP_PARAMETER roomba_param =
{
    IRMP_ROOMBA_PROTOCOL,                                               // protocol:        ir protocol
    ROOMBA_1_PULSE_LEN_MIN,                                             // pulse_1_len_min: minimum length of pulse with bit value 1
    ROOMBA_1_PULSE_LEN_MAX,                                             // pulse_1_len_max: maximum length of pulse with bit value 1
    ROOMBA_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    ROOMBA_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    ROOMBA_0_PULSE_LEN_MIN,                                             // pulse_0_len_min: minimum length of pulse with bit value 0
    ROOMBA_0_PULSE_LEN_MAX,                                             // pulse_0_len_max: maximum length of pulse with bit value 0
    ROOMBA_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    ROOMBA_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    ROOMBA_ADDRESS_OFFSET,                                              // address_offset:  address offset
    ROOMBA_ADDRESS_OFFSET + ROOMBA_ADDRESS_LEN,                         // address_end:     end of address
    ROOMBA_COMMAND_OFFSET,                                              // command_offset:  command offset
    ROOMBA_COMMAND_OFFSET + ROOMBA_COMMAND_LEN,                         // command_end:     end of command
    ROOMBA_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    ROOMBA_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    ROOMBA_LSB,                                                         // lsb_first:       flag: LSB first
    ROOMBA_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RCMM_PROTOCOL==1

static const IRMP_PARAMETER rcmm_param =
{
    IRMP_RCMM32_PROTOCOL,                                               // protocol:        ir protocol

    RCMM32_BIT_PULSE_LEN_MIN,                                           // pulse_1_len_min: here: minimum length of short pulse
    RCMM32_BIT_PULSE_LEN_MAX,                                           // pulse_1_len_max: here: maximum length of short pulse
    0,                                                                  // space_1_len_min: here: minimum length of short pause
    0,                                                                  // space_1_len_max: here: maximum length of short pause
    RCMM32_BIT_PULSE_LEN_MIN,                                           // pulse_0_len_min: here: not used
    RCMM32_BIT_PULSE_LEN_MAX,                                           // pulse_0_len_max: here: not used
    0,                                                                  // space_0_len_min: here: not used
    0,                                                                  // space_0_len_max: here: not used
    RCMM32_ADDRESS_OFFSET,                                              // address_offset:  address offset
    RCMM32_ADDRESS_OFFSET + RCMM32_ADDRESS_LEN,                         // address_end:     end of address
    RCMM32_COMMAND_OFFSET,                                              // command_offset:  command offset
    RCMM32_COMMAND_OFFSET + RCMM32_COMMAND_LEN,                         // command_end:     end of command
    RCMM32_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    RCMM32_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    RCMM32_LSB,                                                         // lsb_first:       flag: LSB first
    RCMM32_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_PENTAX_PROTOCOL==1

static const IRMP_PARAMETER pentax_param =
{
    IRMP_PENTAX_PROTOCOL,                                               // protocol:        ir protocol
    PENTAX_PULSE_LEN_MIN,                                               // pulse_1_len_min: minimum length of pulse with bit value 1
    PENTAX_PULSE_LEN_MAX,                                               // pulse_1_len_max: maximum length of pulse with bit value 1
    PENTAX_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    PENTAX_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    PENTAX_PULSE_LEN_MIN,                                               // pulse_0_len_min: minimum length of pulse with bit value 0
    PENTAX_PULSE_LEN_MAX,                                               // pulse_0_len_max: maximum length of pulse with bit value 0
    PENTAX_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    PENTAX_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    PENTAX_ADDRESS_OFFSET,                                              // address_offset:  address offset
    PENTAX_ADDRESS_OFFSET + PENTAX_ADDRESS_LEN,                         // address_end:     end of address
    PENTAX_COMMAND_OFFSET,                                              // command_offset:  command offset
    PENTAX_COMMAND_OFFSET + PENTAX_COMMAND_LEN,                         // command_end:     end of command
    PENTAX_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    PENTAX_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    PENTAX_LSB,                                                         // lsb_first:       flag: LSB first
    PENTAX_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_ACP24_PROTOCOL==1

static const IRMP_PARAMETER acp24_param =
{
    IRMP_ACP24_PROTOCOL,                                                // protocol:        ir protocol
    ACP24_PULSE_LEN_MIN,                                                // pulse_1_len_min: minimum length of pulse with bit value 1
    ACP24_PULSE_LEN_MAX,                                                // pulse_1_len_max: maximum length of pulse with bit value 1
    ACP24_1_PAUSE_LEN_MIN,                                              // space_1_len_min: minimum length of space with bit value 1
    ACP24_1_PAUSE_LEN_MAX,                                              // space_1_len_max: maximum length of space with bit value 1
    ACP24_PULSE_LEN_MIN,                                                // pulse_0_len_min: minimum length of pulse with bit value 0
    ACP24_PULSE_LEN_MAX,                                                // pulse_0_len_max: maximum length of pulse with bit value 0
    ACP24_0_PAUSE_LEN_MIN,                                              // space_0_len_min: minimum length of space with bit value 0
    ACP24_0_PAUSE_LEN_MAX,                                              // space_0_len_max: maximum length of space with bit value 0
    ACP24_ADDRESS_OFFSET,                                               // address_offset:  address offset
    ACP24_ADDRESS_OFFSET + ACP24_ADDRESS_LEN,                           // address_end:     end of address
    ACP24_COMMAND_OFFSET,                                               // command_offset:  command offset
    ACP24_COMMAND_OFFSET + ACP24_COMMAND_LEN,                           // command_end:     end of command
    ACP24_COMPLETE_DATA_LEN,                                            // complete_len:    complete length of frame
    ACP24_STOP_BIT,                                                     // stop_bit:        flag: frame has stop bit
    ACP24_LSB,                                                          // lsb_first:       flag: LSB first
    ACP24_FLAGS                                                         // flags:           some flags
};

#endif

#if IRMP_SUPPORT_METZ_PROTOCOL==1

static const IRMP_PARAMETER metz_param =
{
    IRMP_METZ_PROTOCOL,                                                 // protocol:        ir protocol
    METZ_PULSE_LEN_MIN,                                                 // pulse_1_len_min: minimum length of pulse with bit value 1
    METZ_PULSE_LEN_MAX,                                                 // pulse_1_len_max: maximum length of pulse with bit value 1
    METZ_1_PAUSE_LEN_MIN,                                               // space_1_len_min: minimum length of space with bit value 1
    METZ_1_PAUSE_LEN_MAX,                                               // space_1_len_max: maximum length of space with bit value 1
    METZ_PULSE_LEN_MIN,                                                 // pulse_0_len_min: minimum length of pulse with bit value 0
    METZ_PULSE_LEN_MAX,                                                 // pulse_0_len_max: maximum length of pulse with bit value 0
    METZ_0_PAUSE_LEN_MIN,                                               // space_0_len_min: minimum length of space with bit value 0
    METZ_0_PAUSE_LEN_MAX,                                               // space_0_len_max: maximum length of space with bit value 0
    METZ_ADDRESS_OFFSET,                                                // address_offset:  address offset
    METZ_ADDRESS_OFFSET + METZ_ADDRESS_LEN,                             // address_end:     end of address
    METZ_COMMAND_OFFSET,                                                // command_offset:  command offset
    METZ_COMMAND_OFFSET + METZ_COMMAND_LEN,                             // command_end:     end of command
    METZ_COMPLETE_DATA_LEN,                                             // complete_len:    complete length of frame
    METZ_STOP_BIT,                                                      // stop_bit:        flag: frame has stop bit
    METZ_LSB,                                                           // lsb_first:       flag: LSB first
    METZ_FLAGS                                                          // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RF_GEN24_PROTOCOL==1

static const IRMP_PARAMETER rf_gen24_param =
{
    RF_GEN24_PROTOCOL,                                                  // protocol:        ir protocol

    RF_GEN24_1_PULSE_LEN_MIN,                                           // pulse_1_len_min: minimum length of pulse with bit value 1
    RF_GEN24_1_PULSE_LEN_MAX,                                           // pulse_1_len_max: maximum length of pulse with bit value 1
    RF_GEN24_1_PAUSE_LEN_MIN,                                           // space_1_len_min: minimum length of space with bit value 1
    RF_GEN24_1_PAUSE_LEN_MAX,                                           // space_1_len_max: maximum length of space with bit value 1
    RF_GEN24_0_PULSE_LEN_MIN,                                           // pulse_0_len_min: minimum length of pulse with bit value 0
    RF_GEN24_0_PULSE_LEN_MAX,                                           // pulse_0_len_max: maximum length of pulse with bit value 0
    RF_GEN24_0_PAUSE_LEN_MIN,                                           // space_0_len_min: minimum length of space with bit value 0
    RF_GEN24_0_PAUSE_LEN_MAX,                                           // space_0_len_max: maximum length of space with bit value 0
    RF_GEN24_ADDRESS_OFFSET,                                            // address_offset:  address offset
    RF_GEN24_ADDRESS_OFFSET + RF_GEN24_ADDRESS_LEN,                     // address_end:     end of address
    RF_GEN24_COMMAND_OFFSET,                                            // command_offset:  command offset
    RF_GEN24_COMMAND_OFFSET + RF_GEN24_COMMAND_LEN,                     // command_end:     end of command
    RF_GEN24_COMPLETE_DATA_LEN,                                         // complete_len:    complete length of frame
    RF_GEN24_STOP_BIT,                                                  // stop_bit:        flag: frame has stop bit
    RF_GEN24_LSB,                                                       // lsb_first:       flag: LSB first
    RF_GEN24_FLAGS                                                      // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RF_X10_PROTOCOL==1

static const IRMP_PARAMETER rf_x10_param =
{
    RF_X10_PROTOCOL,                                                    // protocol:        ir protocol

    RF_X10_1_PULSE_LEN_MIN,                                             // pulse_1_len_min: minimum length of pulse with bit value 1
    RF_X10_1_PULSE_LEN_MAX,                                             // pulse_1_len_max: maximum length of pulse with bit value 1
    RF_X10_1_PAUSE_LEN_MIN,                                             // space_1_len_min: minimum length of space with bit value 1
    RF_X10_1_PAUSE_LEN_MAX,                                             // space_1_len_max: maximum length of space with bit value 1
    RF_X10_0_PULSE_LEN_MIN,                                             // pulse_0_len_min: minimum length of pulse with bit value 0
    RF_X10_0_PULSE_LEN_MAX,                                             // pulse_0_len_max: maximum length of pulse with bit value 0
    RF_X10_0_PAUSE_LEN_MIN,                                             // space_0_len_min: minimum length of space with bit value 0
    RF_X10_0_PAUSE_LEN_MAX,                                             // space_0_len_max: maximum length of space with bit value 0
    RF_X10_ADDRESS_OFFSET,                                              // address_offset:  address offset
    RF_X10_ADDRESS_OFFSET + RF_X10_ADDRESS_LEN,                         // address_end:     end of address
    RF_X10_COMMAND_OFFSET,                                              // command_offset:  command offset
    RF_X10_COMMAND_OFFSET + RF_X10_COMMAND_LEN,                         // command_end:     end of command
    RF_X10_COMPLETE_DATA_LEN,                                           // complete_len:    complete length of frame
    RF_X10_STOP_BIT,                                                    // stop_bit:        flag: frame has stop bit
    RF_X10_LSB,                                                         // lsb_first:       flag: LSB first
    RF_X10_FLAGS                                                        // flags:           some flags
};

#endif

#if IRMP_SUPPORT_RF_MEDION_PROTOCOL==1

static const IRMP_PARAMETER rf_medion_param =
{
    RF_MEDION_PROTOCOL,                                                 // protocol:        ir protocol

    RF_MEDION_1_PULSE_LEN_MIN,                                          // pulse_1_len_min: minimum length of pulse with bit value 1
    RF_MEDION_1_PULSE_LEN_MAX,                                          // pulse_1_len_max: maximum length of pulse with bit value 1
    RF_MEDION_1_PAUSE_LEN_MIN,                                          // space_1_len_min: minimum length of space with bit value 1
    RF_MEDION_1_PAUSE_LEN_MAX,                                          // space_1_len_max: maximum length of space with bit value 1
    RF_MEDION_0_PULSE_LEN_MIN,                                          // pulse_0_len_min: minimum length of pulse with bit value 0
    RF_MEDION_0_PULSE_LEN_MAX,                                          // pulse_0_len_max: maximum length of pulse with bit value 0
    RF_MEDION_0_PAUSE_LEN_MIN,                                          // space_0_len_min: minimum length of space with bit value 0
    RF_MEDION_0_PAUSE_LEN_MAX,                                          // space_0_len_max: maximum length of space with bit value 0
    RF_MEDION_ADDRESS_OFFSET,                                           // address_offset:  address offset
    RF_MEDION_ADDRESS_OFFSET + RF_MEDION_ADDRESS_LEN,                   // address_end:     end of address
    RF_MEDION_COMMAND_OFFSET,                                           // command_offset:  command offset
    RF_MEDION_COMMAND_OFFSET + RF_MEDION_COMMAND_LEN,                   // command_end:     end of command
    RF_MEDION_COMPLETE_DATA_LEN,                                        // complete_len:    complete length of frame
    RF_MEDION_STOP_BIT,                                                 // stop_bit:        flag: frame has stop bit
    RF_MEDION_LSB,                                                      // lsb_first:       flag: LSB first
    RF_MEDION_FLAGS                                                     // flags:           some flags
};

#endif // #if IRMP_SUPPORT_RF_MEDION_PROTOCOL==1

/***************************** V A R I A B L E S ******************************/

static uint8_t                      ir_rx_bit_count;               // current bit position
static IRMP_PARAMETER               irmp_param;

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
static IRMP_PARAMETER                           irmp_param2;
#endif // #if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)

static uint8_t						ir_rx_start_pulse_detected;
static uint8_t                    	ir_rx_frame_detected;
static uint8_t                    	ir_decoded_protocol;
static uint16_t                   	ir_decoded_address;
static uint16_t                   	ir_decoded_command;
static uint16_t                   	ir_decoded_id;                // only used for SAMSUNG protocol
static uint8_t                    	irmp_flags;
// static volatile uint8_t                 irmp_busy_flag;
static uint16_t 					irmp_tmp_address;           // ir address
static uint16_t 					irmp_tmp_command;           // ir command
static uint32_t						irmp_tmp_raw_data[2], irmp_last_raw_data[2]; // 64 bits address + data

#if (IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)) || IRMP_SUPPORT_NEC42_PROTOCOL==1
static uint16_t irmp_tmp_address2;                                     // ir address
static uint16_t irmp_tmp_command2;                                     // ir command
#endif

#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
static uint16_t irmp_lgair_address;                                    // ir address
static uint16_t irmp_lgair_command;                                    // ir command
#endif

#if IRMP_SUPPORT_MELINERA_PROTOCOL==1
static uint16_t irmp_melinera_command;                                 // ir command
#endif

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
static uint16_t irmp_tmp_id;                                           // ir id (only SAMSUNG)
#endif
#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1
static uint8_t      xor_check[6];                                           // check kaseikyo "parity" bits
static uint8_t genre2;                                                 // save genre2 bits here, later copied to MSB in flags
#endif

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1
static uint8_t  parity;                                                // number of '1' of the first 14 bits, check if even.
#endif

#if IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1
static uint8_t  check;                                                 // number of '1' of the first 14 bits, check if even.
static uint8_t  mitsu_parity;                                          // number of '1' of the first 14 bits, check if even.
#endif

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void irmp_init(void);
uint8_t irmp_start_bit_is_detected(void);
static void irmp_decode_bit(uint8_t value);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
 * This function initializes all variables for the Ir Rx activity
 */
/*============================================================================*/
void irmp_init(void)
{
	ir_rx_frame_detected = FALSE;
	ir_rx_start_pulse_detected = FALSE;
	ir_decoded_protocol = IRMP_UNKNOWN_PROTOCOL;
} // void irmp_init(void)



/*============================================================================*/
/*
 * This function returns TRUE if a valid start bit has been detected
 */
/*============================================================================*/
uint8_t irmp_start_bit_is_detected(void)
{
	return ir_rx_start_pulse_detected;

} // uint8_t irmp_start_bit_is_detected(void)



/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  Get IRMP data
 *  @details  gets decoded IRMP data
 *  @param    pointer in order to store IRMP data
 *  @return    TRUE: successful, FALSE: failed
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint8_t irmp_get_data(IRMP_DATA *irmp_data_p)
{
    uint8_t   rtc = FALSE;
#if IRMP_SUPPORT_MERLIN_PROTOCOL==1
    uint8_t   cmd_len = 0;
#endif

    if (ir_rx_frame_detected)
    {
        switch (ir_decoded_protocol)
        {
#if IRMP_SUPPORT_SIRCS_PROTOCOL==1
        	case IRMP_SIRCS_PROTOCOL:
        		rtc = TRUE;
        	    if ( ir_rx_bit_count==12 )
        	    {
        	    	ir_decoded_address = (irmp_last_raw_data[0] >> 7) & 0x1F;
        	        ir_decoded_command = irmp_last_raw_data[0] & 0x7F;
        	    }
        	    else if ( ir_rx_bit_count==15 )
        	    {
        	    	ir_decoded_address = (irmp_last_raw_data[0] >> 7) & 0xFF;
        	        ir_decoded_command = irmp_last_raw_data[0] & 0x7F;
        	        //ir_decoded_protocol = IRMP_SIRCS_PROTOCOL15;
        	    }
        	    else if ( ir_rx_bit_count==20 )
        	    {
        	    	ir_decoded_address = (irmp_last_raw_data[0] >> 7) & 0x1FFF;
        	        ir_decoded_command = irmp_last_raw_data[0] & 0x7F;
        	        //ir_decoded_protocol = IRMP_SIRCS_PROTOCOL20;
        	    }
        	    else
        	    {
        	    	rtc = FALSE;
        	    }
        		break;
#endif // #if IRMP_SUPPORT_SIRCS_PROTOCOL==1
#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
            case IRMP_SAMSUNG_PROTOCOL:
                if ((ir_decoded_command >> 8)==(~ir_decoded_command & 0x00FF))
                {
                    ir_decoded_command &= 0xff;
                    ir_decoded_command |= ir_decoded_id << 8;
                    rtc = TRUE;
                }
                break;

#if IRMP_SUPPORT_SAMSUNG48_PROTOCOL==1
            case IRMP_SAMSUNG48_PROTOCOL:
                ir_decoded_command = (ir_decoded_command & 0x00FF) | ((ir_decoded_id & 0x00FF) << 8);
                rtc = TRUE;
                break;
#endif
#endif

#if IRMP_SUPPORT_NEC_PROTOCOL==1
            case IRMP_NEC_PROTOCOL:
                if ((ir_decoded_command >> 8)==(~ir_decoded_command & 0x00FF))
                {
                    ir_decoded_command &= 0xff;
                    rtc = TRUE;
                }
                else if (ir_decoded_address==0x87EE)
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Switching to APPLE protocol\r\n");
                    ir_decoded_protocol = IRMP_APPLE_PROTOCOL;
                    ir_decoded_address = (ir_decoded_command & 0xFF00) >> 8;
                    ir_decoded_command &= 0x00FF;
                    rtc = TRUE;
                }
                else
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Switching to ONKYO protocol\r\n");
                    ir_decoded_protocol = IRMP_ONKYO_PROTOCOL;
                    rtc = TRUE;
                }
                break;
#endif


#if IRMP_SUPPORT_NEC_PROTOCOL==1
            case IRMP_VINCENT_PROTOCOL:
                if ((ir_decoded_command >> 8)==(ir_decoded_command & 0x00FF))
                {
                    ir_decoded_command &= 0xff;
                    rtc = TRUE;
                }
                break;
#endif

#if IRMP_SUPPORT_BOSE_PROTOCOL==1
            case IRMP_BOSE_PROTOCOL:
                if ((ir_decoded_command >> 8)==(~ir_decoded_command & 0x00FF))
                {
                    ir_decoded_command &= 0xff;
                    rtc = TRUE;
                }
                break;
#endif

#if IRMP_SUPPORT_MERLIN_PROTOCOL==1
            case IRMP_MERLIN_PROTOCOL:
                if (ir_rx_bit_count==10)
                {
                    rtc = TRUE;
                }
                else if (ir_rx_bit_count >= 19 && ((ir_rx_bit_count - 3) % 8==0))
                {
                    if (((ir_decoded_command >> 1) & 1) != (ir_decoded_command & 1))
                    {
                        ir_decoded_command >>= 1;
                        ir_decoded_command |= ((ir_decoded_address & 1) << (ir_rx_bit_count - 12));
                        ir_decoded_address >>= 1;
                        cmd_len = (ir_rx_bit_count - 11) >> 3;
                        rtc = TRUE;
                    }
                }
                break;
#endif

#if IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1
            case IRMP_SIEMENS_PROTOCOL:
            case IRMP_RUWIDO_PROTOCOL:
                if (((ir_decoded_command >> 1) & 0x0001)==(~ir_decoded_command & 0x0001))
                {
                    ir_decoded_command >>= 1;
                    rtc = TRUE;
                }
                break;
#endif
#if IRMP_SUPPORT_KATHREIN_PROTOCOL==1
            case IRMP_KATHREIN_PROTOCOL:
                if (ir_decoded_command != 0x0000)
                {
                    rtc = TRUE;
                }
                break;
#endif
#if IRMP_SUPPORT_RC5_PROTOCOL==1
            case IRMP_RC5_PROTOCOL:
                ir_decoded_address &= ~0x20;                              // clear toggle bit
                rtc = TRUE;
                break;
#endif
#if IRMP_SUPPORT_S100_PROTOCOL==1
            case IRMP_S100_PROTOCOL:
                ir_decoded_address &= ~0x20;                              // clear toggle bit
                rtc = TRUE;
                break;
#endif
#if IRMP_SUPPORT_IR60_PROTOCOL==1
            case IRMP_IR60_PROTOCOL:
                if (ir_decoded_command != 0x007d)                         // 0x007d (== 62<<1 + 1) is start instruction frame
                {
                    rtc = TRUE;
                }
                else
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Info IR60: got start instruction frame\r\n");
                }
                break;
#endif
#if IRMP_SUPPORT_RCCAR_PROTOCOL==1
            case IRMP_RCCAR_PROTOCOL:
                // frame in irmp_data:
                // Bit 12 11 10 9  8  7  6  5  4  3  2  1  0
                //     V  D7 D6 D5 D4 D3 D2 D1 D0 A1 A0 C1 C0   //         10 9  8  7  6  5  4  3  2  1  0
                ir_decoded_address = (ir_decoded_command & 0x000C) >> 2;    // addr:   0  0  0  0  0  0  0  0  0  A1 A0
                ir_decoded_command = ((ir_decoded_command & 0x1000) >> 2) | // V-Bit:  V  0  0  0  0  0  0  0  0  0  0
                               ((ir_decoded_command & 0x0003) << 8) | // C-Bits: 0  C1 C0 0  0  0  0  0  0  0  0
                               ((ir_decoded_command & 0x0FF0) >> 4);  // D-Bits:          D7 D6 D5 D4 D3 D2 D1 D0
                rtc = TRUE;                                     // Summe:  V  C1 C0 D7 D6 D5 D4 D3 D2 D1 D0
                break;
#endif

#if IRMP_SUPPORT_NETBOX_PROTOCOL==1                           // squeeze code to 8 bit, upper bit indicates release-key
            case IRMP_NETBOX_PROTOCOL:
                if (ir_decoded_command & 0x1000)                      // last bit set?
                {
                    if ((ir_decoded_command & 0x1f)==0x15)          // key pressed: 101 01 (LSB)
                    {
                        ir_decoded_command >>= 5;
                        ir_decoded_command &= 0x7F;
                        rtc = TRUE;
                    }
                    else if ((ir_decoded_command & 0x1f)==0x10)     // key released: 000 01 (LSB)
                    {
                        ir_decoded_command >>= 5;
                        ir_decoded_command |= 0x80;
                        rtc = TRUE;
                    }
                    else
                    {
                        M1_LOG_E(M1_LOGDB_TAG, "ENETBOX: bit6/7 must be 0/1\r\n");
                    }
                }
                else
                {
                    M1_LOG_E(M1_LOGDB_TAG, "ENETBOX: last bit not set\r\n");
                }
                break;
#endif
#if IRMP_SUPPORT_LEGO_PROTOCOL==1
            case IRMP_LEGO_PROTOCOL:
                uint8_t crc = 0x0F ^ ((ir_decoded_command & 0xF000) >> 12) ^ ((ir_decoded_command & 0x0F00) >> 8) ^ ((ir_decoded_command & 0x00F0) >> 4);

                if ((ir_decoded_command & 0x000F)==crc)
                {
                    ir_decoded_command >>= 4;
                    rtc = TRUE;
                }
                else
                {
                    M1_LOG_D(M1_LOGDB_TAG, "CRC error in LEGO protocol\r\n");
                    // rtc = TRUE;                              // don't accept codes with CRC errors
                }
                break;
#endif

#if IRMP_SUPPORT_METZ_PROTOCOL==1
            case IRMP_METZ_PROTOCOL:
                ir_decoded_address &= ~0x40;                              // clear toggle bit
                if (((~ir_decoded_address) & 0x07)==(ir_decoded_address >> 3) && ((~ir_decoded_command) & 0x3f)==(ir_decoded_command >> 6))
                {
                    ir_decoded_address >>= 3;
                    ir_decoded_command >>= 6;
                    rtc = TRUE;
                }
                break;
#endif

#if IRMP_SUPPORT_RF_X10_PROTOCOL==1 || IRMP_SUPPORT_RF_MEDION_PROTOCOL==1
            case RF_X10_PROTOCOL:
            case RF_MEDION_PROTOCOL:
                uint8_t channel;
                uint8_t checksum;

                channel = ir_decoded_command & 0x0F;                // lower nibble: RF channel 0-15
                ir_decoded_command >>= 4;                           // shift out channel

                checksum = ir_decoded_address;                      // get checksum

                if (((ir_decoded_command + (0x0055 + (channel << 4))) & 0x7F)==checksum)
                {                                                   // checksum correct?
                    ir_decoded_address = channel + 1;               // set address to channel + 1
                    rtc = TRUE;
                }
                break;
#endif

            default:
                rtc = TRUE;
                break;
        } // switch (ir_decoded_protocol)

        if (rtc)
        {
            irmp_data_p->protocol = ir_decoded_protocol;
            irmp_data_p->address  = ir_decoded_address;
            irmp_data_p->command  = ir_decoded_command;
            irmp_data_p->flags    = irmp_flags;
        }
        else
        {
            ir_decoded_protocol = IRMP_UNKNOWN_PROTOCOL;
        }

        ir_decoded_command  = 0;								// don't reset ir_decoded_protocol here, needed for detection of NEC & JVC repetition frames!
        ir_decoded_address  = 0;
        irmp_flags    = 0;

        ir_rx_frame_detected = FALSE;
    } // if (ir_rx_frame_detected)

    return rtc;
} // uint8_t irmp_get_data(IRMP_DATA *irmp_data_p)



/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  store bit
 *  @details  store bit in temp address or temp command
 *  @param    value to store: 0 or 1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
// verhindert, dass irmp_decode_bit() inline compiliert wird:
// static void irmp_decode_bit(uint8_t) __attribute__ ((noinline));

static void irmp_decode_bit(uint8_t value)
{
#if IRMP_SUPPORT_ACP24_PROTOCOL==1
    if (irmp_param.protocol==IRMP_ACP24_PROTOCOL)                                                 // squeeze 64 bits into 16 bits:
    {
        if (value)
        {
            // ACP24-Frame:
            //           1         2         3         4         5         6
            // 0123456789012345678901234567890123456789012345678901234567890123456789
            // N VVMMM    ? ???    t vmA x                 y                     TTTT
            //
            // irmp_data_p->command:
            //
            //         5432109876543210
            //         NAVVvMMMmtxyTTTT

            switch (ir_rx_bit_count)
            {
                case  0: irmp_tmp_command |= (1<<15); break;                                        // N
                case  2: irmp_tmp_command |= (1<<13); break;                                        // V
                case  3: irmp_tmp_command |= (1<<12); break;                                        // V
                case  4: irmp_tmp_command |= (1<<10); break;                                        // M
                case  5: irmp_tmp_command |= (1<< 9); break;                                        // M
                case  6: irmp_tmp_command |= (1<< 8); break;                                        // M
                case 20: irmp_tmp_command |= (1<< 6); break;                                        // t
                case 22: irmp_tmp_command |= (1<<11); break;                                        // v
                case 23: irmp_tmp_command |= (1<< 7); break;                                        // m
                case 24: irmp_tmp_command |= (1<<14); break;                                        // A
                case 26: irmp_tmp_command |= (1<< 5); break;                                        // x
                case 44: irmp_tmp_command |= (1<< 4); break;                                        // y
                case 66: irmp_tmp_command |= (1<< 3); break;                                        // T
                case 67: irmp_tmp_command |= (1<< 2); break;                                        // T
                case 68: irmp_tmp_command |= (1<< 1); break;                                        // T
                case 69: irmp_tmp_command |= (1<< 0); break;                                        // T
            }
        }
    }
    else
#endif // IRMP_SUPPORT_ACP24_PROTOCOL

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1
    if (irmp_param.protocol==IRMP_ORTEK_PROTOCOL)
    {
        if (ir_rx_bit_count < 14)
        {
            if (value)
            {
                parity++;
            }
        }
        else if (ir_rx_bit_count==14)
        {
            if (value)                                                                                      // value==1: even parity
            {
                if (parity & 0x01)
                {
                    parity = PARITY_CHECK_FAILED;
                }
                else
                {
                    parity = PARITY_CHECK_OK;
                }
            }
            else
            {
                if (parity & 0x01)                                                                          // value==0: odd parity
                {
                    parity = PARITY_CHECK_OK;
                }
                else
                {
                    parity = PARITY_CHECK_FAILED;
                }
            }
        }
    }
    else
#endif // #if IRMP_SUPPORT_ORTEK_PROTOCOL==1
    {
        ;
    }

#if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1
    if (ir_rx_bit_count==0 && irmp_param.protocol==IRMP_GRUNDIG_PROTOCOL)
    {
        first_bit = value;
    }
    else
#endif // #if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1

    if (ir_rx_bit_count >= irmp_param.address_offset && ir_rx_bit_count < irmp_param.address_end)
    {
        if (irmp_param.lsb_first)
        {
            irmp_tmp_address |= (((uint16_t) (value)) << (ir_rx_bit_count - irmp_param.address_offset));      // CV wants cast
        }
        else
        {
            irmp_tmp_address <<= 1;
            irmp_tmp_address |= value;
        }
    }
    else if (ir_rx_bit_count >= irmp_param.command_offset && ir_rx_bit_count < irmp_param.command_end)
    {
        if (irmp_param.lsb_first)
        {
#if IRMP_SUPPORT_SAMSUNG48_PROTOCOL==1
            if (irmp_param.protocol==IRMP_SAMSUNG48_PROTOCOL && ir_rx_bit_count >= 32)
            {
                irmp_tmp_id |= (((uint16_t) (value)) << (ir_rx_bit_count - 32));                              // CV wants cast
            }
            else
#endif
            {
                irmp_tmp_command |= (((uint16_t) (value)) << (ir_rx_bit_count - irmp_param.command_offset));  // CV wants cast
            }
        }
        else
        {
            irmp_tmp_command <<= 1;
            irmp_tmp_command |= value;
        }
    } // else if (ir_rx_bit_count >= irmp_param.command_offset && ir_rx_bit_count < irmp_param.command_end)

    uint8_t shift_bits, hi_bits = 0;
    if ( ir_rx_bit_count > 31 )
    	hi_bits = 1;
    shift_bits = 32*hi_bits;
    if ( irmp_param.lsb_first )
    {
    	irmp_tmp_raw_data[hi_bits] |= (((uint32_t) (value)) << (ir_rx_bit_count - shift_bits));
    } // if ( irmp_param.lsb_first )
    else
    {
    	irmp_tmp_raw_data[hi_bits] <<= 1;
    	irmp_tmp_raw_data[hi_bits] |= value;
    } // else

#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
    if (irmp_param.protocol==IRMP_NEC_PROTOCOL || irmp_param.protocol==IRMP_NEC42_PROTOCOL)
    {
        if (ir_rx_bit_count < 8)
        {
            irmp_lgair_address <<= 1;                                                                       // LGAIR uses MSB
            irmp_lgair_address |= value;
        }
        else if (ir_rx_bit_count < 24)
        {
            irmp_lgair_command <<= 1;                                                                       // LGAIR uses MSB
            irmp_lgair_command |= value;
        }
    }
    // NO else!
#endif

#if IRMP_SUPPORT_MELINERA_PROTOCOL==1
    if (irmp_param.protocol==IRMP_NEC_PROTOCOL || irmp_param.protocol==IRMP_NEC42_PROTOCOL || irmp_param.protocol==IRMP_MELINERA_PROTOCOL)
    {
        irmp_melinera_command <<= 1;                                                                        // MELINERA uses MSB
        irmp_melinera_command |= value;
    }
#endif

#if IRMP_SUPPORT_NEC42_PROTOCOL==1
    if (irmp_param.protocol==IRMP_NEC42_PROTOCOL && ir_rx_bit_count >= 13 && ir_rx_bit_count < 26)
    {
        irmp_tmp_address2 |= (((uint16_t) (value)) << (ir_rx_bit_count - 13));                                // CV wants cast
    }
    else
#endif

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
    if (irmp_param.protocol==IRMP_SAMSUNG_PROTOCOL && ir_rx_bit_count >= SAMSUNG_ID_OFFSET && ir_rx_bit_count < SAMSUNG_ID_OFFSET + SAMSUNG_ID_LEN)
    {
        irmp_tmp_id |= (((uint16_t) (value)) << (ir_rx_bit_count - SAMSUNG_ID_OFFSET));                       // store with LSB first
    }
    else
#endif

#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1
    if (irmp_param.protocol==IRMP_KASEIKYO_PROTOCOL)
    {
        if (ir_rx_bit_count >= 20 && ir_rx_bit_count < 24)
        {
            irmp_tmp_command |= (((uint16_t) (value)) << (ir_rx_bit_count - 8));  // store 4 system bits (genre 1) in upper nibble with LSB first
        }
        else if (ir_rx_bit_count >= 24 && ir_rx_bit_count < 28)
        {
            genre2 |= (((uint8_t) (value)) << (ir_rx_bit_count - 20));            // store 4 system bits (genre 2) in upper nibble with LSB first
        }

        if (ir_rx_bit_count < KASEIKYO_COMPLETE_DATA_LEN)
        {
            if (value)
            {
                xor_check[ir_rx_bit_count / 8] |= 1 << (ir_rx_bit_count % 8);
            }
            else
            {
                xor_check[ir_rx_bit_count / 8] &= ~(1 << (ir_rx_bit_count % 8));
            }
        }
    }
    else
#endif

#if IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1
    if (irmp_param.protocol==IRMP_MITSU_HEAVY_PROTOCOL)                           // squeeze 64 bits into 16 bits:
    {
        if (ir_rx_bit_count==72 )
        {                                                                           // irmp_tmp_address, irmp_tmp_command received: check parity & compress
            mitsu_parity = PARITY_CHECK_OK;

            check = irmp_tmp_address >> 8;                                          // inverted upper byte==lower byte?
            check = ~ check;

            if (check==(irmp_tmp_address & 0xFF))
            {                                                                       // ok:
                irmp_tmp_address <<= 8;                                             // throw away upper byte
            }
            else
            {
                mitsu_parity = PARITY_CHECK_FAILED;
            }

            check = irmp_tmp_command >> 8;                                          // inverted upper byte==lower byte?
            check = ~ check;
            if (check==(irmp_tmp_command & 0xFF))
            {                                                                       // ok:  pack together
                irmp_tmp_address |= irmp_tmp_command & 0xFF;                        // byte 1, byte2 in irmp_tmp_address, irmp_tmp_command can be used for byte 3
            }
            else
            {
                mitsu_parity = PARITY_CHECK_FAILED;
            }
            irmp_tmp_command = 0;
        }

        if (ir_rx_bit_count >= 72 )
        {                                                                           // receive 3. word in irmp_tmp_command
            irmp_tmp_command <<= 1;
            irmp_tmp_command |= value;
        }
    }
    else
#endif // IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL
    {
        ;
    }

    ir_rx_bit_count++;
} // static void irmp_decode_bit(uint8_t value)



/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  store bit
 *  @details  store bit in temp address or temp command
 *  @param    value to store: 0 or 1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
static void
irmp_store_bit2 (uint8_t value)
{
    uint8_t irmp_bit2;

    if (irmp_param.protocol)
    {
        irmp_bit2 = ir_rx_bit_count - 2;
    }
    else
    {
        irmp_bit2 = ir_rx_bit_count - 1;
    }

    if (irmp_bit2 >= irmp_param2.address_offset && irmp_bit2 < irmp_param2.address_end)
    {
        irmp_tmp_address2 |= (((uint16_t) (value)) << (irmp_bit2 - irmp_param2.address_offset));   // CV wants cast
    }
    else if (irmp_bit2 >= irmp_param2.command_offset && irmp_bit2 < irmp_param2.command_end)
    {
        irmp_tmp_command2 |= (((uint16_t) (value)) << (irmp_bit2 - irmp_param2.command_offset));   // CV wants cast
    }
}
#endif // IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  IR data bit handler
 *  @details  	pulse_len = input capture value in us
 *  			rx_logic_high = 1 for rising edge, 0 for falling edge
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void irmp_data_sampler(uint32_t pulse_len, uint8_t rx_logic_high)
{
    //static uint8_t     ir_rx_start_pulse_detected = FALSE;         // flag: start bit detected
    static uint8_t     	ir_data_wait_for_falling_edge;               // flag: wait for data bit space
    static uint8_t     	ir_start_wait_for_falling_edge;              // flag: wait for start bit space
    static uint16_t    	ir_data_pulse_time;                   // count bit time for pulse
    static uint16_t   	ir_data_space_time;                   // count bit time for pause
    static uint16_t    	irmp_last_address = 0xFFFF;           // save last irmp address to recognize key repetition
#if IRMP_ENABLE_RELEASE_DETECTION==1
    static uint8_t     key_released = TRUE;
#endif
    static uint16_t    irmp_last_command = 0xFFFF;            // save last irmp command to recognize key repetition
    static uint16_t    key_repetition_len;                    // SIRCS repeats frame 2-5 times with 45 ms pause
    static uint8_t     repetition_frame_number;
#if IRMP_SUPPORT_DENON_PROTOCOL==1
    static uint16_t    last_irmp_denon_command;               // save last irmp command to recognize DENON frame repetition
    static uint16_t    denon_repetition_len = 0xFFFF;         // denon repetition len of 2nd auto generated frame
#endif
#if IRMP_SUPPORT_RC5_PROTOCOL==1 || IRMP_SUPPORT_S100_PROTOCOL==1
    static uint8_t     rc5_cmd_bit6;                          // bit 6 of RC5 command is the inverted 2nd start bit
#endif
#if IRMP_SUPPORT_MANCHESTER==1
    static uint16_t    last_space;                            // last space value
#endif
#if IRMP_SUPPORT_MANCHESTER==1 || IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
    static uint8_t     last_value;                            // last bit value
#endif
#if IRMP_SUPPORT_RCII_PROTOCOL==1
    static uint8_t     waiting_for_2nd_pulse = 0;
#endif
#if IRMP_SUPPORT_RF_GEN24_PROTOCOL==1
    uint8_t            bit_0 = 0;
#endif

    if (!ir_rx_frame_detected)                              // ir code already detected?
    {                                                       // no...
        if (!ir_rx_start_pulse_detected)                    // start bit detected?
        {                                                   // no...
            if (!rx_logic_high)                       		// receiving burst?
            {                                  				// yes...
                ir_data_pulse_time = pulse_len; 			// first falling edge, start the counter (pulse_len should be 0)
            } // if (!rx_logic_high)
            else // Rising edge detected for start pulse
            {                                                 // no...
            	ir_data_pulse_time += pulse_len; // update real pulse counter when the edge changes from falling to rising
            	if (ir_data_pulse_time)                       // it's dark....
                {                                             // set flags for counting the time of darkness...
                    ir_rx_start_pulse_detected = 1;
                    ir_start_wait_for_falling_edge = 1;
                    ir_data_wait_for_falling_edge  = 0;
                    ir_rx_bit_count                = 0xFF;
                    ir_data_space_time      = 1;              // 1st space: set to 1, not to 0!
                    irmp_tmp_command        = 0;
                    irmp_tmp_address        = 0;
                    irmp_tmp_raw_data[0]		= 0; 				// B.N
                    irmp_tmp_raw_data[1]		= 0;

#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1
                    genre2                  = 0;
#endif // #if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
                    irmp_tmp_id = 0;
#endif // #if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1) || IRMP_SUPPORT_NEC42_PROTOCOL==1
                    irmp_tmp_command2       = 0;
                    irmp_tmp_address2       = 0;
#endif // #if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1) || IRMP_SUPPORT_NEC42_PROTOCOL==1

#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
                    irmp_lgair_command      = 0;
                    irmp_lgair_address      = 0;
#endif // #if IRMP_SUPPORT_LGAIR_PROTOCOL==1

#if IRMP_SUPPORT_MELINERA_PROTOCOL==1
                    irmp_melinera_command   = 0;
#endif // #if IRMP_SUPPORT_MELINERA_PROTOCOL==1

#if IRMP_SUPPORT_RC5_PROTOCOL==1 || IRMP_SUPPORT_S100_PROTOCOL==1
                    rc5_cmd_bit6            = 0;              // fm 2010-03-07: bugfix: reset it after incomplete RC5 frame!
#endif // #if IRMP_SUPPORT_RC5_PROTOCOL==1 || IRMP_SUPPORT_S100_PROTOCOL==1

                } // if (ir_data_pulse_time)
                else
                {
                    if (key_repetition_len < 0xFFFF)          // avoid overflow of counter
                    {
                        key_repetition_len = pulse_len;

#if IRMP_ENABLE_RELEASE_DETECTION==1
                        if (! key_released && key_repetition_len > IRMP_KEY_RELEASE_LEN)
                        {
                            ir_decoded_address        = irmp_last_address;
                            ir_decoded_command        = irmp_last_command;
                            irmp_flags          = 0x02;
                            ir_rx_frame_detected    = TRUE;
                            key_released        = TRUE;
                        }
#endif // #if IRMP_ENABLE_RELEASE_DETECTION==1

#if IRMP_SUPPORT_DENON_PROTOCOL==1
                        if (denon_repetition_len < 0xFFFF)    // avoid overflow of counter
                        {
                            denon_repetition_len = pulse_len;

                            if (denon_repetition_len >= DENON_AUTO_REPETITION_PAUSE_LEN && last_irmp_denon_command != 0)
                            {
                                M1_LOG_W(M1_LOGDB_TAG, "warning: did not receive inverted command repetition\r\n");
                                last_irmp_denon_command = 0;
                                denon_repetition_len = 0xFFFF;
                            }
                        } // if (denon_repetition_len < 0xFFFF)
#endif // IRMP_SUPPORT_DENON_PROTOCOL==1
                    } // if (key_repetition_len < 0xFFFF)
                } // else
                	// if (ir_data_pulse_time)
            } // else
            	// if (!rx_logic_high)
        } // if (!ir_rx_start_pulse_detected)
        else
        {
            if (ir_start_wait_for_falling_edge)                      // we have received start bit...
            {                                                 // ...and are counting the time of darkness
                if (rx_logic_high)                        // still dark?
                {                                             // yes
                	// program reaches here only when it's timeout (given in pulse_len) after the start (pulse) bit has been detected
                    ir_data_space_time = pulse_len;

#if IRMP_SUPPORT_NIKON_PROTOCOL==1
                    if (((ir_data_pulse_time < NIKON_START_BIT_PULSE_LEN_MIN || ir_data_pulse_time > NIKON_START_BIT_PULSE_LEN_MAX) && ir_data_space_time > IRMP_TIMEOUT_LEN) ||
                         ir_data_space_time > IRMP_TIMEOUT_NIKON_LEN)
#else
                    if (ir_data_space_time > IRMP_TIMEOUT_LEN)// timeout?
#endif // #if IRMP_SUPPORT_NIKON_PROTOCOL==1
                    {                                         // yes...
#if IRMP_SUPPORT_JVC_PROTOCOL==1
                        if (ir_decoded_protocol==IRMP_JVC_PROTOCOL)  // don't show eror if JVC protocol, ir_data_pulse_time has been set below!
                        {
                            ;
                        }
                        else
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1
                        {
                            M1_LOG_E(M1_LOGDB_TAG, "E1: space after start bit pulse %d too long: %d\r\n", ir_data_pulse_time, ir_data_space_time);
                        }

                        ir_rx_start_pulse_detected = FALSE;          // reset flags, let's wait for another start bit
                        ir_data_pulse_time         = 0;
                        ir_data_space_time         = 0;
                    } // if (ir_data_space_time > IRMP_TIMEOUT_LEN)
                } // if (rx_logic_high)

                else // falling edge detected
                {                                    // receiving first data pulse!
                	ir_data_space_time += pulse_len; // update real space counter when the edge changes from rising to falling

                    IRMP_PARAMETER * irmp_param_p;
                    irmp_param_p = (IRMP_PARAMETER *) 0;
                    ir_rx_bit_count = 0;

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
                    irmp_param2.protocol = 0;
#endif

                    M1_LOG_D(M1_LOGDB_TAG, "[start-bit: pulse = %2d, space = %2d]\r\n", ir_data_pulse_time, ir_data_space_time);

#if IRMP_SUPPORT_SIRCS_PROTOCOL==1
                    if (ir_data_pulse_time >= SIRCS_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= SIRCS_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= SIRCS_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SIRCS_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's SIRCS
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = SIRCS, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        SIRCS_START_BIT_PULSE_LEN_MIN, SIRCS_START_BIT_PULSE_LEN_MAX,
                                        SIRCS_START_BIT_PAUSE_LEN_MIN, SIRCS_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &sircs_param;
                    }
                    else
#endif // IRMP_SUPPORT_SIRCS_PROTOCOL==1

#if IRMP_SUPPORT_JVC_PROTOCOL==1
                    if (ir_decoded_protocol==IRMP_JVC_PROTOCOL &&                                                       // last protocol was JVC, awaiting repeat frame
                        ir_data_pulse_time >= JVC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= JVC_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= JVC_REPEAT_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= JVC_REPEAT_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NEC or JVC (type 1) repeat frame, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        JVC_START_BIT_PULSE_LEN_MIN, JVC_START_BIT_PULSE_LEN_MAX,
                                        JVC_REPEAT_START_BIT_PAUSE_LEN_MIN, JVC_REPEAT_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nec_param;
                    }
                    else
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1

#if IRMP_SUPPORT_NEC_PROTOCOL==1
                    if (ir_data_pulse_time >= NEC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= NEC_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= NEC_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NEC_START_BIT_PAUSE_LEN_MAX)
                    {
#if IRMP_SUPPORT_NEC42_PROTOCOL==1
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NEC42, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        NEC_START_BIT_PULSE_LEN_MIN, NEC_START_BIT_PULSE_LEN_MAX,
                                        NEC_START_BIT_PAUSE_LEN_MIN, NEC_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nec42_param;
#else
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NEC, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        NEC_START_BIT_PULSE_LEN_MIN, NEC_START_BIT_PULSE_LEN_MAX,
                                        NEC_START_BIT_PAUSE_LEN_MIN, NEC_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nec_param;
#endif
                    }
                    else if (ir_data_pulse_time >= NEC_START_BIT_PULSE_LEN_MIN        && ir_data_pulse_time <= NEC_START_BIT_PULSE_LEN_MAX &&
                             ir_data_space_time >= NEC_REPEAT_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NEC_REPEAT_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's NEC
#if IRMP_SUPPORT_JVC_PROTOCOL==1
                        if (ir_decoded_protocol==IRMP_JVC_PROTOCOL)                 // last protocol was JVC, awaiting repeat frame
                        {                                                       // some jvc remote controls use nec repetition frame for jvc repetition frame
                            M1_LOG_D(M1_LOGDB_TAG, "protocol = JVC repeat frame type 2, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            NEC_START_BIT_PULSE_LEN_MIN, NEC_START_BIT_PULSE_LEN_MAX,
                                            NEC_REPEAT_START_BIT_PAUSE_LEN_MIN, NEC_REPEAT_START_BIT_PAUSE_LEN_MAX);
                            irmp_param_p = (IRMP_PARAMETER *) &nec_param;
                        }
                        else
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "protocol = NEC (repetition frame), start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            NEC_START_BIT_PULSE_LEN_MIN, NEC_START_BIT_PULSE_LEN_MAX,
                                            NEC_REPEAT_START_BIT_PAUSE_LEN_MIN, NEC_REPEAT_START_BIT_PAUSE_LEN_MAX);

                            irmp_param_p = (IRMP_PARAMETER *) &nec_rep_param;
                        }
                    }
                    else

#if IRMP_SUPPORT_JVC_PROTOCOL==1
                    if (ir_decoded_protocol==IRMP_JVC_PROTOCOL &&                   // last protocol was JVC, awaiting repeat frame
                        ir_data_pulse_time >= NEC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= NEC_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= NEC_0_PAUSE_LEN_MIN         && ir_data_space_time <= NEC_0_PAUSE_LEN_MAX)
                    {                                                           // it's JVC repetition type 3
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = JVC repeat frame type 3, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        NEC_START_BIT_PULSE_LEN_MIN, NEC_START_BIT_PULSE_LEN_MAX,
                                        NEC_0_PAUSE_LEN_MIN, NEC_0_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nec_param;
                    }
                    else
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1

#endif // IRMP_SUPPORT_NEC_PROTOCOL==1

#if IRMP_SUPPORT_TELEFUNKEN_PROTOCOL==1
                    if (ir_data_pulse_time >= TELEFUNKEN_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= TELEFUNKEN_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= TELEFUNKEN_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= TELEFUNKEN_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = TELEFUNKEN, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        TELEFUNKEN_START_BIT_PULSE_LEN_MIN, TELEFUNKEN_START_BIT_PULSE_LEN_MAX,
                                        TELEFUNKEN_START_BIT_PAUSE_LEN_MIN, TELEFUNKEN_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &telefunken_param;
                    }
                    else
#endif // IRMP_SUPPORT_TELEFUNKEN_PROTOCOL==1

#if IRMP_SUPPORT_ROOMBA_PROTOCOL==1
                    if (ir_data_pulse_time >= ROOMBA_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= ROOMBA_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= ROOMBA_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= ROOMBA_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = ROOMBA, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        ROOMBA_START_BIT_PULSE_LEN_MIN, ROOMBA_START_BIT_PULSE_LEN_MAX,
                                        ROOMBA_START_BIT_PAUSE_LEN_MIN, ROOMBA_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &roomba_param;
                    }
                    else
#endif // IRMP_SUPPORT_ROOMBA_PROTOCOL==1

#if IRMP_SUPPORT_ACP24_PROTOCOL==1
                    if (ir_data_pulse_time >= ACP24_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= ACP24_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= ACP24_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= ACP24_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = ACP24, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        ACP24_START_BIT_PULSE_LEN_MIN, ACP24_START_BIT_PULSE_LEN_MAX,
                                        ACP24_START_BIT_PAUSE_LEN_MIN, ACP24_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &acp24_param;
                    }
                    else
#endif // IRMP_SUPPORT_ROOMBA_PROTOCOL==1

#if IRMP_SUPPORT_PENTAX_PROTOCOL==1
                    if (ir_data_pulse_time >= PENTAX_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= PENTAX_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= PENTAX_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= PENTAX_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = PENTAX, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        PENTAX_START_BIT_PULSE_LEN_MIN, PENTAX_START_BIT_PULSE_LEN_MAX,
                                        PENTAX_START_BIT_PAUSE_LEN_MIN, PENTAX_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &pentax_param;
                    }
                    else
#endif // IRMP_SUPPORT_PENTAX_PROTOCOL==1

#if IRMP_SUPPORT_NIKON_PROTOCOL==1
                    if (ir_data_pulse_time >= NIKON_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= NIKON_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= NIKON_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NIKON_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NIKON, start bit timings: pulse: %3d - %3d, space: %3u - %3u\r\n",
                                        NIKON_START_BIT_PULSE_LEN_MIN, NIKON_START_BIT_PULSE_LEN_MAX,
                                        (unsigned int) NIKON_START_BIT_PAUSE_LEN_MIN, (unsigned int) NIKON_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nikon_param;
                    }
                    else
#endif // IRMP_SUPPORT_NIKON_PROTOCOL==1

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
                    if (ir_data_pulse_time >= SAMSUNG_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= SAMSUNG_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= SAMSUNG_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SAMSUNG_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's SAMSUNG
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = SAMSUNG, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        SAMSUNG_START_BIT_PULSE_LEN_MIN, SAMSUNG_START_BIT_PULSE_LEN_MAX,
                                        SAMSUNG_START_BIT_PAUSE_LEN_MIN, SAMSUNG_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &samsung_param;
                    }
                    else
#endif // IRMP_SUPPORT_SAMSUNG_PROTOCOL==1

#if IRMP_SUPPORT_SAMSUNGAH_PROTOCOL==1
                    if (ir_data_pulse_time >= SAMSUNGAH_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= SAMSUNGAH_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= SAMSUNGAH_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SAMSUNGAH_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's SAMSUNGAH
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = SAMSUNGAH, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        SAMSUNGAH_START_BIT_PULSE_LEN_MIN, SAMSUNGAH_START_BIT_PULSE_LEN_MAX,
                                        SAMSUNGAH_START_BIT_PAUSE_LEN_MIN, SAMSUNGAH_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &samsungah_param;
                    }
                    else
#endif // IRMP_SUPPORT_SAMSUNGAH_PROTOCOL==1

#if IRMP_SUPPORT_MATSUSHITA_PROTOCOL==1
                    if (ir_data_pulse_time >= MATSUSHITA_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= MATSUSHITA_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= MATSUSHITA_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= MATSUSHITA_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's MATSUSHITA
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = MATSUSHITA, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        MATSUSHITA_START_BIT_PULSE_LEN_MIN, MATSUSHITA_START_BIT_PULSE_LEN_MAX,
                                        MATSUSHITA_START_BIT_PAUSE_LEN_MIN, MATSUSHITA_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &matsushita_param;
                    }
                    else
#endif // IRMP_SUPPORT_MATSUSHITA_PROTOCOL==1

#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1
                    if (ir_data_pulse_time >= KASEIKYO_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= KASEIKYO_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= KASEIKYO_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= KASEIKYO_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's KASEIKYO
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = KASEIKYO, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        KASEIKYO_START_BIT_PULSE_LEN_MIN, KASEIKYO_START_BIT_PULSE_LEN_MAX,
                                        KASEIKYO_START_BIT_PAUSE_LEN_MIN, KASEIKYO_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &kaseikyo_param;
                    }
                    else
#endif // IRMP_SUPPORT_KASEIKYO_PROTOCOL==1

#if IRMP_SUPPORT_PANASONIC_PROTOCOL==1
                    if (ir_data_pulse_time >= PANASONIC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= PANASONIC_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= PANASONIC_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= PANASONIC_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's PANASONIC
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = PANASONIC, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        PANASONIC_START_BIT_PULSE_LEN_MIN, PANASONIC_START_BIT_PULSE_LEN_MAX,
                                        PANASONIC_START_BIT_PAUSE_LEN_MIN, PANASONIC_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &panasonic_param;
                    }
                    else
#endif // IRMP_SUPPORT_PANASONIC_PROTOCOL==1

#if IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1
                    if (ir_data_pulse_time >= MITSU_HEAVY_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= MITSU_HEAVY_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= MITSU_HEAVY_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= MITSU_HEAVY_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's MITSU_HEAVY
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = MITSU_HEAVY, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        MITSU_HEAVY_START_BIT_PULSE_LEN_MIN, MITSU_HEAVY_START_BIT_PULSE_LEN_MAX,
                                        MITSU_HEAVY_START_BIT_PAUSE_LEN_MIN, MITSU_HEAVY_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &mitsu_heavy_param;
                    }
                    else
#endif // IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1

#if IRMP_SUPPORT_VINCENT_PROTOCOL==1
                    if (ir_data_pulse_time >= VINCENT_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= VINCENT_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= VINCENT_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= VINCENT_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's VINCENT
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = VINCENT, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        VINCENT_START_BIT_PULSE_LEN_MIN, VINCENT_START_BIT_PULSE_LEN_MAX,
                                        VINCENT_START_BIT_PAUSE_LEN_MIN, VINCENT_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &vincent_param;
                    }
                    else
#endif // IRMP_SUPPORT_VINCENT_PROTOCOL==1

#if IRMP_SUPPORT_METZ_PROTOCOL==1
                    if (ir_data_pulse_time >= METZ_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= METZ_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= METZ_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= METZ_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = METZ, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        METZ_START_BIT_PULSE_LEN_MIN, METZ_START_BIT_PULSE_LEN_MAX,
                                        METZ_START_BIT_PAUSE_LEN_MIN, METZ_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &metz_param;
                    }
                    else
#endif // IRMP_SUPPORT_METZ_PROTOCOL==1

#if IRMP_SUPPORT_RF_GEN24_PROTOCOL==1                                         // RF_GEN24 has no start bit
                    if (ir_data_pulse_time >= RF_GEN24_0_PULSE_LEN_MIN && ir_data_pulse_time <= RF_GEN24_0_PULSE_LEN_MAX &&
                        ir_data_space_time >= RF_GEN24_0_PAUSE_LEN_MIN && ir_data_space_time <= RF_GEN24_0_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RF_GEN24\r\n");
                        irmp_param_p = (IRMP_PARAMETER *) &rf_gen24_param;
                        bit_0 = 0;
                    }
                    else
                    if (ir_data_pulse_time >= RF_GEN24_1_PULSE_LEN_MIN && ir_data_pulse_time <= RF_GEN24_1_PULSE_LEN_MAX &&
                        ir_data_space_time >= RF_GEN24_1_PAUSE_LEN_MIN && ir_data_space_time <= RF_GEN24_1_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RF_GEN24\r\n");
                        irmp_param_p = (IRMP_PARAMETER *) &rf_gen24_param;
                        bit_0 = 1;
                    }
                    else
#endif // IRMP_SUPPORT_RF_GEN24_PROTOCOL==1

#if IRMP_SUPPORT_RF_X10_PROTOCOL==1
                    if (ir_data_pulse_time >= RF_X10_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RF_X10_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RF_X10_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RF_X10_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RF_X10, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RF_X10_START_BIT_PULSE_LEN_MIN, RF_X10_START_BIT_PULSE_LEN_MAX,
                                        RF_X10_START_BIT_PAUSE_LEN_MIN, RF_X10_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &rf_x10_param;
                    }
                    else
#endif // IRMP_SUPPORT_RF_X10_PROTOCOL==1

#if IRMP_SUPPORT_RF_MEDION_PROTOCOL==1
                    if (ir_data_pulse_time >= RF_MEDION_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RF_MEDION_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RF_MEDION_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RF_MEDION_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RF_MEDION, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RF_MEDION_START_BIT_PULSE_LEN_MIN, RF_MEDION_START_BIT_PULSE_LEN_MAX,
                                        RF_MEDION_START_BIT_PAUSE_LEN_MIN, RF_MEDION_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &rf_medion_param;
                    }
                    else
#endif // IRMP_SUPPORT_RF_MEDION_PROTOCOL==1

#if IRMP_SUPPORT_RECS80_PROTOCOL==1
                    if (ir_data_pulse_time >= RECS80_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RECS80_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RECS80_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RECS80_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's RECS80
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RECS80, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RECS80_START_BIT_PULSE_LEN_MIN, RECS80_START_BIT_PULSE_LEN_MAX,
                                        RECS80_START_BIT_PAUSE_LEN_MIN, RECS80_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &recs80_param;
                    }
                    else
#endif // IRMP_SUPPORT_RECS80_PROTOCOL==1

#if IRMP_SUPPORT_S100_PROTOCOL==1
                    if (((ir_data_pulse_time >= S100_START_BIT_LEN_MIN     && ir_data_pulse_time <= S100_START_BIT_LEN_MAX) ||
                         (ir_data_pulse_time >= 2 * S100_START_BIT_LEN_MIN && ir_data_pulse_time <= 2 * S100_START_BIT_LEN_MAX)) &&
                        ((ir_data_space_time >= S100_START_BIT_LEN_MIN     && ir_data_space_time <= S100_START_BIT_LEN_MAX) ||
                         (ir_data_space_time >= 2 * S100_START_BIT_LEN_MIN && ir_data_space_time <= 2 * S100_START_BIT_LEN_MAX)))
                    {                                                           // it's S100
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = S100, start bit timings: pulse: %3d - %3d, space: %3d - %3d or pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        S100_START_BIT_LEN_MIN, S100_START_BIT_LEN_MAX,
                                        2 * S100_START_BIT_LEN_MIN, 2 * S100_START_BIT_LEN_MAX,
                                        S100_START_BIT_LEN_MIN, S100_START_BIT_LEN_MAX,
                                        2 * S100_START_BIT_LEN_MIN, 2 * S100_START_BIT_LEN_MAX);

                        irmp_param_p = (IRMP_PARAMETER *) &s100_param;
                        last_space = ir_data_space_time;

                        if ((ir_data_pulse_time > S100_START_BIT_LEN_MAX && ir_data_pulse_time <= 2 * S100_START_BIT_LEN_MAX) ||
                            (ir_data_space_time > S100_START_BIT_LEN_MAX && ir_data_space_time <= 2 * S100_START_BIT_LEN_MAX))
                        {
                          last_value  = 0;
                          rc5_cmd_bit6 = 1<<6;
                        }
                        else
                        {
                          last_value  = 1;
                        }
                    }
                    else
#endif // IRMP_SUPPORT_S100_PROTOCOL==1

#if IRMP_SUPPORT_RC5_PROTOCOL==1
                    if (((ir_data_pulse_time >= RC5_START_BIT_LEN_MIN     && ir_data_pulse_time <= RC5_START_BIT_LEN_MAX) ||
                         (ir_data_pulse_time >= 2 * RC5_START_BIT_LEN_MIN && ir_data_pulse_time <= 2 * RC5_START_BIT_LEN_MAX)) &&
                        ((ir_data_space_time >= RC5_START_BIT_LEN_MIN     && ir_data_space_time <= RC5_START_BIT_LEN_MAX) ||
                         (ir_data_space_time >= 2 * RC5_START_BIT_LEN_MIN && ir_data_space_time <= 2 * RC5_START_BIT_LEN_MAX)))
                    {                                                           // it's RC5
#if IRMP_SUPPORT_FDC_PROTOCOL==1
                        if (ir_data_pulse_time >= FDC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= FDC_START_BIT_PULSE_LEN_MAX &&
                            ir_data_space_time >= FDC_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= FDC_START_BIT_PAUSE_LEN_MAX)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "protocol = RC5 or FDC\r\n");
                            M1_LOG_D(M1_LOGDB_TAG, "FDC start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            FDC_START_BIT_PULSE_LEN_MIN, FDC_START_BIT_PULSE_LEN_MAX,
                                            FDC_START_BIT_PAUSE_LEN_MIN, FDC_START_BIT_PAUSE_LEN_MAX);
                            M1_LOG_D(M1_LOGDB_TAG, "RC5 start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX,
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX);
                            memcpy_P (&irmp_param2, &fdc_param, sizeof (IRMP_PARAMETER));
                        }
                        else
#endif // IRMP_SUPPORT_FDC_PROTOCOL==1

#if IRMP_SUPPORT_RCCAR_PROTOCOL==1
                        if (ir_data_pulse_time >= RCCAR_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RCCAR_START_BIT_PULSE_LEN_MAX &&
                            ir_data_space_time >= RCCAR_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_START_BIT_PAUSE_LEN_MAX)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "protocol = RC5 or RCCAR\r\n");
                            M1_LOG_D(M1_LOGDB_TAG, "RCCAR start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            RCCAR_START_BIT_PULSE_LEN_MIN, RCCAR_START_BIT_PULSE_LEN_MAX,
                                            RCCAR_START_BIT_PAUSE_LEN_MIN, RCCAR_START_BIT_PAUSE_LEN_MAX);
                            M1_LOG_D(M1_LOGDB_TAG, "RC5 start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX,
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX);
                            memcpy_P (&irmp_param2, &rccar_param, sizeof (IRMP_PARAMETER));
                        }
                        else
#endif // IRMP_SUPPORT_RCCAR_PROTOCOL==1
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "protocol = RC5, start bit timings: pulse: %3d - %3d, space: %3d - %3d or pulse: %3d - %3d, space: %3d - %3d\r\n",
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX,
                                            2 * RC5_START_BIT_LEN_MIN, 2 * RC5_START_BIT_LEN_MAX,
                                            RC5_START_BIT_LEN_MIN, RC5_START_BIT_LEN_MAX,
                                            2 * RC5_START_BIT_LEN_MIN, 2 * RC5_START_BIT_LEN_MAX);
                        }

                        irmp_param_p = (IRMP_PARAMETER *) &rc5_param;
                        last_space = ir_data_space_time;

                        if ((ir_data_pulse_time > RC5_START_BIT_LEN_MAX && ir_data_pulse_time <= 2 * RC5_START_BIT_LEN_MAX) ||
                            (ir_data_space_time > RC5_START_BIT_LEN_MAX && ir_data_space_time <= 2 * RC5_START_BIT_LEN_MAX))
                        {
                            last_value  = 0;
                            rc5_cmd_bit6 = 1<<6;
                        }
                        else
                        {
                            last_value  = 1;
                        }
                    }
                    else
#endif // IRMP_SUPPORT_RC5_PROTOCOL==1

#if IRMP_SUPPORT_RCII_PROTOCOL==1
                    if ((ir_data_pulse_time >= RCII_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RCII_START_BIT_PULSE_LEN_MAX) &&
                        (ir_data_space_time >= RCII_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RCII_START_BIT_PAUSE_LEN_MAX))
                    {                                                           // it's RCII
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RCII, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RCII_START_BIT_PULSE_LEN_MIN, RCII_START_BIT_PULSE_LEN_MAX,
                                        RCII_START_BIT_PAUSE_LEN_MIN, RCII_START_BIT_PAUSE_LEN_MAX)
                        irmp_param_p = (IRMP_PARAMETER *) &rcii_param;
                        last_space = ir_data_space_time;
                        waiting_for_2nd_pulse = 1;
                        last_value  = 1;
                    }
                    else
#endif // IRMP_SUPPORT_RCII_PROTOCOL==1

#if IRMP_SUPPORT_DENON_PROTOCOL==1
                    if ( (ir_data_pulse_time >= DENON_PULSE_LEN_MIN && ir_data_pulse_time <= DENON_PULSE_LEN_MAX) &&
                        ((ir_data_space_time >= DENON_1_PAUSE_LEN_MIN && ir_data_space_time <= DENON_1_PAUSE_LEN_MAX) ||
                         (ir_data_space_time >= DENON_0_PAUSE_LEN_MIN && ir_data_space_time <= DENON_0_PAUSE_LEN_MAX)))
                    {                                                           // it's DENON
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = DENON, start bit timings: pulse: %3d - %3d, space: %3d - %3d or %3d - %3d\r\n",
                                        DENON_PULSE_LEN_MIN, DENON_PULSE_LEN_MAX,
                                        DENON_1_PAUSE_LEN_MIN, DENON_1_PAUSE_LEN_MAX,
                                        DENON_0_PAUSE_LEN_MIN, DENON_0_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &denon_param;
                    }
                    else
#endif // IRMP_SUPPORT_DENON_PROTOCOL==1

#if IRMP_SUPPORT_THOMSON_PROTOCOL==1
                    if ( (ir_data_pulse_time >= THOMSON_PULSE_LEN_MIN && ir_data_pulse_time <= THOMSON_PULSE_LEN_MAX) &&
                        ((ir_data_space_time >= THOMSON_1_PAUSE_LEN_MIN && ir_data_space_time <= THOMSON_1_PAUSE_LEN_MAX) ||
                         (ir_data_space_time >= THOMSON_0_PAUSE_LEN_MIN && ir_data_space_time <= THOMSON_0_PAUSE_LEN_MAX)))
                    {                                                           // it's THOMSON
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = THOMSON, start bit timings: pulse: %3d - %3d, space: %3d - %3d or %3d - %3d\r\n",
                                        THOMSON_PULSE_LEN_MIN, THOMSON_PULSE_LEN_MAX,
                                        THOMSON_1_PAUSE_LEN_MIN, THOMSON_1_PAUSE_LEN_MAX,
                                        THOMSON_0_PAUSE_LEN_MIN, THOMSON_0_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &thomson_param;
                    }
                    else
#endif // IRMP_SUPPORT_THOMSON_PROTOCOL==1

#if IRMP_SUPPORT_BOSE_PROTOCOL==1
                    if (ir_data_pulse_time >= BOSE_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= BOSE_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= BOSE_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= BOSE_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = BOSE, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        BOSE_START_BIT_PULSE_LEN_MIN, BOSE_START_BIT_PULSE_LEN_MAX,
                                        BOSE_START_BIT_PAUSE_LEN_MIN, BOSE_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &bose_param;
                    }
                    else
#endif // IRMP_SUPPORT_BOSE_PROTOCOL==1

#if IRMP_SUPPORT_RC6_PROTOCOL==1
                    if (ir_data_pulse_time >= RC6_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RC6_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RC6_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RC6_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's RC6
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RC6, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RC6_START_BIT_PULSE_LEN_MIN, RC6_START_BIT_PULSE_LEN_MAX,
                                        RC6_START_BIT_PAUSE_LEN_MIN, RC6_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &rc6_param;
                        last_space = 0;
                        last_value = 1;
                    }
                    else
#endif // IRMP_SUPPORT_RC6_PROTOCOL==1

#if IRMP_SUPPORT_RECS80EXT_PROTOCOL==1
                    if (ir_data_pulse_time >= RECS80EXT_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RECS80EXT_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RECS80EXT_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RECS80EXT_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's RECS80EXT
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RECS80EXT, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RECS80EXT_START_BIT_PULSE_LEN_MIN, RECS80EXT_START_BIT_PULSE_LEN_MAX,
                                        RECS80EXT_START_BIT_PAUSE_LEN_MIN, RECS80EXT_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &recs80ext_param;
                    }
                    else
#endif // IRMP_SUPPORT_RECS80EXT_PROTOCOL==1

#if IRMP_SUPPORT_NUBERT_PROTOCOL==1
                    if (ir_data_pulse_time >= NUBERT_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= NUBERT_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= NUBERT_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NUBERT_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's NUBERT
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NUBERT, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        NUBERT_START_BIT_PULSE_LEN_MIN, NUBERT_START_BIT_PULSE_LEN_MAX,
                                        NUBERT_START_BIT_PAUSE_LEN_MIN, NUBERT_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &nubert_param;
                    }
                    else
#endif // IRMP_SUPPORT_NUBERT_PROTOCOL==1

#if IRMP_SUPPORT_FAN_PROTOCOL==1
                    if (ir_data_pulse_time >= FAN_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= FAN_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= FAN_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= FAN_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's FAN
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = FAN, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        FAN_START_BIT_PULSE_LEN_MIN, FAN_START_BIT_PULSE_LEN_MAX,
                                        FAN_START_BIT_PAUSE_LEN_MIN, FAN_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &fan_param;
                    }
                    else
#endif // IRMP_SUPPORT_FAN_PROTOCOL==1

#if IRMP_SUPPORT_SPEAKER_PROTOCOL==1
                    if (ir_data_pulse_time >= SPEAKER_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= SPEAKER_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= SPEAKER_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SPEAKER_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's SPEAKER
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = SPEAKER, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        SPEAKER_START_BIT_PULSE_LEN_MIN, SPEAKER_START_BIT_PULSE_LEN_MAX,
                                        SPEAKER_START_BIT_PAUSE_LEN_MIN, SPEAKER_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &speaker_param;
                    }
                    else
#endif // IRMP_SUPPORT_SPEAKER_PROTOCOL==1

#if IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
                    if (ir_data_pulse_time >= BANG_OLUFSEN_START_BIT1_PULSE_LEN_MIN && ir_data_pulse_time <= BANG_OLUFSEN_START_BIT1_PULSE_LEN_MAX &&
                        ir_data_space_time >= BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MAX)
                    {                                                           // it's BANG_OLUFSEN
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = BANG_OLUFSEN\r\n");
                        M1_LOG_D(M1_LOGDB_TAG, "start bit 1 timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        BANG_OLUFSEN_START_BIT1_PULSE_LEN_MIN, BANG_OLUFSEN_START_BIT1_PULSE_LEN_MAX,
                                        BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MIN, BANG_OLUFSEN_START_BIT1_PAUSE_LEN_MAX);
                        M1_LOG_D(M1_LOGDB_TAG, "start bit 2 timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        BANG_OLUFSEN_START_BIT2_PULSE_LEN_MIN, BANG_OLUFSEN_START_BIT2_PULSE_LEN_MAX,
                                        BANG_OLUFSEN_START_BIT2_PAUSE_LEN_MIN, BANG_OLUFSEN_START_BIT2_PAUSE_LEN_MAX);
                        M1_LOG_D(M1_LOGDB_TAG, "start bit 3 timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        BANG_OLUFSEN_START_BIT3_PULSE_LEN_MIN, BANG_OLUFSEN_START_BIT3_PULSE_LEN_MAX,
                                        BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MIN, BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MAX);
                        M1_LOG_D(M1_LOGDB_TAG, "start bit 4 timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        BANG_OLUFSEN_START_BIT4_PULSE_LEN_MIN, BANG_OLUFSEN_START_BIT4_PULSE_LEN_MAX,
                                        BANG_OLUFSEN_START_BIT4_PAUSE_LEN_MIN, BANG_OLUFSEN_START_BIT4_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &bang_olufsen_param;
                        last_value = 0;
                    }
                    else
#endif // IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1

#if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1
                    if (ir_data_pulse_time >= GRUNDIG_NOKIA_IR60_START_BIT_LEN_MIN && ir_data_pulse_time <= GRUNDIG_NOKIA_IR60_START_BIT_LEN_MAX &&
                        ir_data_space_time >= GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MIN && ir_data_space_time <= GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MAX)
                    {                                                           // it's GRUNDIG
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = GRUNDIG, pre bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        GRUNDIG_NOKIA_IR60_START_BIT_LEN_MIN, GRUNDIG_NOKIA_IR60_START_BIT_LEN_MAX,
                                        GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MIN, GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &grundig_param;
                        last_space = ir_data_space_time;
                        last_value  = 1;
                    }
                    else
#endif // IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1

#if IRMP_SUPPORT_MERLIN_PROTOCOL==1 // check MERLIN before RUWIDO!
                    if (ir_data_pulse_time >= MERLIN_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= MERLIN_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= MERLIN_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= MERLIN_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's MERLIN
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = MERLIN, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        MERLIN_START_BIT_PULSE_LEN_MIN, MERLIN_START_BIT_PULSE_LEN_MAX,
                                        MERLIN_START_BIT_PAUSE_LEN_MIN, MERLIN_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &merlin_param;
                        last_space = ir_data_space_time;
                        last_value = 1;
                    }
                    else
#endif // IRMP_SUPPORT_MERLIN_PROTOCOL==1

#if IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1
                    if (((ir_data_pulse_time >= SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MAX) ||
                         (ir_data_pulse_time >= 2 * SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= 2 * SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MAX)) &&
                        ((ir_data_space_time >= SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MAX) ||
                         (ir_data_space_time >= 2 * SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= 2 * SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MAX)))
                    {                                                           // it's RUWIDO or SIEMENS
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RUWIDO, start bit timings: pulse: %3d - %3d or %3d - %3d, space: %3d - %3d or %3d - %3d\r\n",
                                        SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MIN,   SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MAX,
                                        2 * SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MIN, 2 * SIEMENS_OR_RUWIDO_START_BIT_PULSE_LEN_MAX,
                                        SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MIN,   SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MAX,
                                        2 * SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MIN, 2 * SIEMENS_OR_RUWIDO_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &ruwido_param;
                        last_space = ir_data_space_time;
                        last_value  = 1;
                    }
                    else
#endif // IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1

#if IRMP_SUPPORT_FDC_PROTOCOL==1
                    if (ir_data_pulse_time >= FDC_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= FDC_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= FDC_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= FDC_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = FDC, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        FDC_START_BIT_PULSE_LEN_MIN, FDC_START_BIT_PULSE_LEN_MAX,
                                        FDC_START_BIT_PAUSE_LEN_MIN, FDC_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &fdc_param;
                    }
                    else
#endif // IRMP_SUPPORT_FDC_PROTOCOL==1

#if IRMP_SUPPORT_RCCAR_PROTOCOL==1
                    if (ir_data_pulse_time >= RCCAR_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RCCAR_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RCCAR_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RCCAR, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RCCAR_START_BIT_PULSE_LEN_MIN, RCCAR_START_BIT_PULSE_LEN_MAX,
                                        RCCAR_START_BIT_PAUSE_LEN_MIN, RCCAR_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &rccar_param;
                    }
                    else
#endif // IRMP_SUPPORT_RCCAR_PROTOCOL==1

#if IRMP_SUPPORT_KATHREIN_PROTOCOL==1
                    if (ir_data_pulse_time >= KATHREIN_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= KATHREIN_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= KATHREIN_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= KATHREIN_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's KATHREIN
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = KATHREIN, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        KATHREIN_START_BIT_PULSE_LEN_MIN, KATHREIN_START_BIT_PULSE_LEN_MAX,
                                        KATHREIN_START_BIT_PAUSE_LEN_MIN, KATHREIN_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &kathrein_param;
                    }
                    else
#endif // IRMP_SUPPORT_KATHREIN_PROTOCOL==1

#if IRMP_SUPPORT_NETBOX_PROTOCOL==1
                    if (ir_data_pulse_time >= NETBOX_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= NETBOX_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= NETBOX_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NETBOX_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's NETBOX
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = NETBOX, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        NETBOX_START_BIT_PULSE_LEN_MIN, NETBOX_START_BIT_PULSE_LEN_MAX,
                                        NETBOX_START_BIT_PAUSE_LEN_MIN, NETBOX_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &netbox_param;
                    }
                    else
#endif // IRMP_SUPPORT_NETBOX_PROTOCOL==1

#if IRMP_SUPPORT_LEGO_PROTOCOL==1
                    if (ir_data_pulse_time >= LEGO_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= LEGO_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= LEGO_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= LEGO_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = LEGO, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        LEGO_START_BIT_PULSE_LEN_MIN, LEGO_START_BIT_PULSE_LEN_MAX,
                                        LEGO_START_BIT_PAUSE_LEN_MIN, LEGO_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &lego_param;
                    }
                    else
#endif // IRMP_SUPPORT_LEGO_PROTOCOL==1

#if IRMP_SUPPORT_IRMP16_PROTOCOL==1
                    if (ir_data_pulse_time >= IRMP16_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= IRMP16_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= IRMP16_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= IRMP16_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = IRMP16, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        IRMP16_START_BIT_PULSE_LEN_MIN, IRMP16_START_BIT_PULSE_LEN_MAX,
                                        IRMP16_START_BIT_PAUSE_LEN_MIN, IRMP16_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &irmp16_param;
                    }
                    else
#endif // IRMP_SUPPORT_IRMP16_PROTOCOL==1

#if IRMP_SUPPORT_GREE_PROTOCOL==1
                    if (ir_data_pulse_time >= GREE_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= GREE_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= GREE_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= GREE_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = GREE, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        GREE_START_BIT_PULSE_LEN_MIN, GREE_START_BIT_PULSE_LEN_MAX,
                                        GREE_START_BIT_PAUSE_LEN_MIN, GREE_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &gree_param;
                    }
                    else
#endif // IRMP_SUPPORT_GREE_PROTOCOL==1

#if IRMP_SUPPORT_A1TVBOX_PROTOCOL==1
                    if (ir_data_pulse_time >= A1TVBOX_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= A1TVBOX_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= A1TVBOX_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= A1TVBOX_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's A1TVBOX
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = A1TVBOX, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        A1TVBOX_START_BIT_PULSE_LEN_MIN, A1TVBOX_START_BIT_PULSE_LEN_MAX,
                                        A1TVBOX_START_BIT_PAUSE_LEN_MIN, A1TVBOX_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &a1tvbox_param;
                        last_space = 0;
                        last_value = 1;
                    }
                    else
#endif // IRMP_SUPPORT_A1TVBOX_PROTOCOL==1

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1
                    if (ir_data_pulse_time >= ORTEK_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= ORTEK_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= ORTEK_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= ORTEK_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's ORTEK (Hama)
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = ORTEK, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        ORTEK_START_BIT_PULSE_LEN_MIN, ORTEK_START_BIT_PULSE_LEN_MAX,
                                        ORTEK_START_BIT_PAUSE_LEN_MIN, ORTEK_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &ortek_param;
                        last_space  = 0;
                        last_value  = 1;
                        parity      = 0;
                    }
                    else
#endif // IRMP_SUPPORT_ORTEK_PROTOCOL==1

#if IRMP_SUPPORT_RCMM_PROTOCOL==1
                    if (ir_data_pulse_time >= RCMM32_START_BIT_PULSE_LEN_MIN && ir_data_pulse_time <= RCMM32_START_BIT_PULSE_LEN_MAX &&
                        ir_data_space_time >= RCMM32_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= RCMM32_START_BIT_PAUSE_LEN_MAX)
                    {                                                           // it's RCMM
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = RCMM, start bit timings: pulse: %3d - %3d, space: %3d - %3d\r\n",
                                        RCMM32_START_BIT_PULSE_LEN_MIN, RCMM32_START_BIT_PULSE_LEN_MAX,
                                        RCMM32_START_BIT_PAUSE_LEN_MIN, RCMM32_START_BIT_PAUSE_LEN_MAX);
                        irmp_param_p = (IRMP_PARAMETER *) &rcmm_param;
                    }
                    else
#endif // IRMP_SUPPORT_RCMM_PROTOCOL==1
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "protocol = UNKNOWN\r\n");
                        ir_rx_start_pulse_detected = FALSE;                     // wait for another start bit...
                        irmp_param.protocol = 0;                                // reset protocol
                    }

                    if (ir_rx_start_pulse_detected)
                    {
                    	memcpy_P (&irmp_param, irmp_param_p, sizeof (IRMP_PARAMETER));

                        if (! (irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER))
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_1: %3d - %3d\r\n", irmp_param.pulse_1_len_min, irmp_param.pulse_1_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space_1: %3d - %3d\r\n", irmp_param.space_1_len_min, irmp_param.space_1_len_max);
                        }
                        else
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse: %3d - %3d or %3d - %3d\r\n", irmp_param.pulse_1_len_min, irmp_param.pulse_1_len_max,
                                            2 * irmp_param.pulse_1_len_min, 2 * irmp_param.pulse_1_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space: %3d - %3d or %3d - %3d\r\n", irmp_param.space_1_len_min, irmp_param.space_1_len_max,
                                            2 * irmp_param.space_1_len_min, 2 * irmp_param.space_1_len_max);
                        }

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
                        if (irmp_param2.protocol)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_0: %3d - %3d\r\n", irmp_param2.pulse_0_len_min, irmp_param2.pulse_0_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space_0: %3d - %3d\r\n", irmp_param2.space_0_len_min, irmp_param2.space_0_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_1: %3d - %3d\r\n", irmp_param2.pulse_1_len_min, irmp_param2.pulse_1_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space_1: %3d - %3d\r\n", irmp_param2.space_1_len_min, irmp_param2.space_1_len_max);
                        }
#endif


#if IRMP_SUPPORT_RC6_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_RC6_PROTOCOL)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_toggle: %3d - %3d\r\n", RC6_TOGGLE_BIT_LEN_MIN, RC6_TOGGLE_BIT_LEN_MAX);
                        }
#endif

                        if (! (irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER))
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_0: %3d - %3d\r\n", irmp_param.pulse_0_len_min, irmp_param.pulse_0_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space_0: %3d - %3d\r\n", irmp_param.space_0_len_min, irmp_param.space_0_len_max);
                        }
                        else
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse: %3d - %3d or %3d - %3d\r\n", irmp_param.pulse_0_len_min, irmp_param.pulse_0_len_max,
                                            2 * irmp_param.pulse_0_len_min, 2 * irmp_param.pulse_0_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space: %3d - %3d or %3d - %3d\r\n", irmp_param.space_0_len_min, irmp_param.space_0_len_max,
                                            2 * irmp_param.space_0_len_min, 2 * irmp_param.space_0_len_max);
                        }

#if IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_BANG_OLUFSEN_PROTOCOL)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "pulse_r: %3d - %3d\r\n", irmp_param.pulse_0_len_min, irmp_param.pulse_0_len_max);
                            M1_LOG_D(M1_LOGDB_TAG, "space_r: %3d - %3d\r\n", BANG_OLUFSEN_R_PAUSE_LEN_MIN, BANG_OLUFSEN_R_PAUSE_LEN_MAX);
                        }
#endif

                        M1_LOG_D(M1_LOGDB_TAG, "command_offset: %2d\r\n", irmp_param.command_offset);
                        M1_LOG_D(M1_LOGDB_TAG, "command_len:    %3d\r\n", irmp_param.command_end - irmp_param.command_offset);
                        M1_LOG_D(M1_LOGDB_TAG, "complete_len:   %3d\r\n", irmp_param.complete_len);
                        M1_LOG_D(M1_LOGDB_TAG, "stop_bit:       %3d\r\n", irmp_param.stop_bit);

                        M1_LOG_D(M1_LOGDB_TAG, "Starting decoding data field...\r\n");
                    } // if (ir_rx_start_pulse_detected)

#if IRMP_SUPPORT_MANCHESTER==1
                    if ((irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER) &&
                         irmp_param.protocol != IRMP_RUWIDO_PROTOCOL && // Manchester, but not RUWIDO
                         irmp_param.protocol != IRMP_RC6_PROTOCOL /*** &&    // Manchester, but not RC6
                         irmp_param.protocol != IRMP_RCII_PROTOCOL ****/)     // Manchester, but not RCII
                    {
                        if (ir_data_space_time > irmp_param.pulse_1_len_max && ir_data_space_time <= 2 * irmp_param.pulse_1_len_max)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                            M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", (irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? '0' : '1');
                            irmp_decode_bit((irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? 0 : 1);
                        }
                        else if (! last_value)  // && ir_data_space_time >= irmp_param.space_1_len_min && ir_data_space_time <= irmp_param.space_1_len_max)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                            M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", (irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? '1' : '0');
                            irmp_decode_bit((irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? 1 : 0);
                        }
                    }
                    else
#endif // IRMP_SUPPORT_MANCHESTER==1

#if IRMP_SUPPORT_SERIAL==1
                    if (irmp_param.flags & IRMP_PARAM_FLAG_IS_SERIAL)
                    {
                        ; // do nothing
                    }
                    else
#endif // IRMP_SUPPORT_SERIAL==1


#if IRMP_SUPPORT_DENON_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_DENON_PROTOCOL)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);

                        if (ir_data_space_time >= DENON_1_PAUSE_LEN_MIN && ir_data_space_time <= DENON_1_PAUSE_LEN_MAX)
                        {                                                       // space timings correct for "1"?
                        	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");						// yes, store 1
                            irmp_decode_bit(1);
                        }
                        else // if (ir_data_space_time >= DENON_0_PAUSE_LEN_MIN && ir_data_space_time <= DENON_0_PAUSE_LEN_MAX)
                        {                                                       // space timings correct for "0"?
                        	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");                              // yes, store 0
                            irmp_decode_bit(0);
                        }
                    }
                    else
#endif // IRMP_SUPPORT_DENON_PROTOCOL==1
#if IRMP_SUPPORT_THOMSON_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_THOMSON_PROTOCOL)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);

                        if (ir_data_space_time >= THOMSON_1_PAUSE_LEN_MIN && ir_data_space_time <= THOMSON_1_PAUSE_LEN_MAX)
                        {                                     // space timings correct for "1"?
                        	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");    // yes, store 1
                        	irmp_decode_bit(1);
                        }
                        else // if (ir_data_space_time >= THOMSON_0_PAUSE_LEN_MIN && ir_data_space_time <= THOMSON_0_PAUSE_LEN_MAX)
                        {                                     // space timings correct for "0"?
                        	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");    // yes, store 0
                          irmp_decode_bit(0);
                        }
                    }
                    else
#endif // IRMP_SUPPORT_THOMSON_PROTOCOL==1
#if IRMP_SUPPORT_RF_GEN24_PROTOCOL==1
                    if (irmp_param.protocol==RF_GEN24_PROTOCOL)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                        M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", bit_0 + '0');
                        irmp_decode_bit(bit_0);                // start bit is data bit
                    }
                    else
#endif // IRMP_SUPPORT_RF_GEN24_PROTOCOL==1
                    {
                        ;                                     // else do nothing
                    }

                    ir_data_pulse_time = 0;
                    ir_data_space_time = 0;
                    ir_start_wait_for_falling_edge = 0;
                } // else
                	// if (rx_logic_high)
            } // if (ir_start_wait_for_falling_edge)

            else if (ir_data_wait_for_falling_edge)           // the data section....
            {
                uint8_t got_light = FALSE;
                ir_data_space_time = pulse_len; // update counter
                if (rx_logic_high)                      // No falling edge found?
                {
                    // Program reaches here only when it's timeout (given in pulse_len)

                	/* Check for the end bit to complete a frame */
                	if (ir_rx_bit_count==irmp_param.complete_len && irmp_param.stop_bit==1 && ir_data_space_time > ((irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER) ? 0 : STOP_BIT_PAUSE_LEN_MIN))
                	{
                		if (
#if IRMP_SUPPORT_MANCHESTER==1
                				(irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER) ||
#endif // #if IRMP_SUPPORT_MANCHESTER==1
#if IRMP_SUPPORT_SERIAL==1
								(irmp_param.flags & IRMP_PARAM_FLAG_IS_SERIAL) ||
#endif // #if IRMP_SUPPORT_SERIAL==1
								(ir_data_pulse_time >= irmp_param.pulse_0_len_min && ir_data_pulse_time <= irmp_param.pulse_0_len_max))
                		{
                			if (! (irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER))
                			{
                				M1_LOG_D(M1_LOGDB_TAG, "stop bit detected\r\n");
#if IRMP_SUPPORT_MELINERA_PROTOCOL==1
                				if (irmp_param.protocol==IRMP_MELINERA_PROTOCOL)
                				{
                					irmp_tmp_command = irmp_melinera_command;  // set command
                					irmp_tmp_address = 0;                      // no address
                				}
#endif // #if IRMP_SUPPORT_MELINERA_PROTOCOL==1
                			}

                			irmp_param.stop_bit = 0;
                		}
                		else
                		{
                			M1_LOG_E(M1_LOGDB_TAG, "error: stop bit timing wrong, bits rx = %d, ir_data_pulse_time = %d, pulse_0_len_min = %d, pulse_0_len_max = %d\r\n",
                					ir_rx_bit_count, ir_data_pulse_time, irmp_param.pulse_0_len_min, irmp_param.pulse_0_len_max);
                			ir_rx_start_pulse_detected = FALSE;                        // wait for another start bit...
                			ir_data_pulse_time         = 0;
                			ir_data_space_time         = 0;
                		}
                	} // if (ir_rx_bit_count==irmp_param.complete_len && irmp_param.stop_bit==1 && ir_data_space_time > ((irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER) ? 0 : STOP_BIT_PAUSE_LEN_MIN))
                	else
#if IRMP_SUPPORT_SIRCS_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_SIRCS_PROTOCOL &&                           	// Sony has a variable number of bits:
                			ir_data_space_time > SIRCS_PAUSE_LEN_MAX &&                         // minimum is 12
							ir_rx_bit_count >= SIRCS_MINIMUM_DATA_LEN - 1)                      // space too long?
                	{                                                                           // yes, break and close this frame
                		irmp_param.complete_len = ir_rx_bit_count + 1;                          // set new complete length
                		irmp_tmp_address |= (ir_rx_bit_count - SIRCS_MINIMUM_DATA_LEN + 1) << 8;       // new: store number of additional bits in upper byte of address!
                		irmp_param.command_end = irmp_param.command_offset + ir_rx_bit_count + 1;      // correct command length
                		ir_data_space_time = SIRCS_PAUSE_LEN_MAX - 1;                           // correct space length
                		got_light = TRUE;                                                       // this is a lie, but helps (generates stop bit)
                	}
                	else
#endif // #if IRMP_SUPPORT_SIRCS_PROTOCOL==1
#if IRMP_SUPPORT_MERLIN_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_MERLIN_PROTOCOL &&                          	// Merlin has a variable number of bits:
                			ir_data_space_time > MERLIN_START_BIT_PAUSE_LEN_MAX &&              // minimum is 8
							ir_rx_bit_count >= 8 - 1)                                           // space too long?
                	{                                                                           // yes, break and close this frame
                		irmp_param.complete_len = ir_rx_bit_count;                              // set new complete length
                		ir_data_space_time = MERLIN_BIT_PAUSE_LEN_MAX - 1;                      // correct space length
                		got_light = TRUE;                                                       // this is a lie, but helps (generates stop bit)
                	}
                	else
#endif // #if IRMP_SUPPORT_MERLIN_PROTOCOL==1
#if IRMP_SUPPORT_FAN_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_FAN_PROTOCOL &&                             	// FAN has no stop bit.
                			ir_rx_bit_count >= FAN_COMPLETE_DATA_LEN - 1)                       // last bit in frame
                	{                                                                           // yes, break and close this frame
                		if (ir_data_pulse_time <= FAN_0_PULSE_LEN_MAX && ir_data_space_time >= FAN_0_PAUSE_LEN_MIN)
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Generating virtual stop bit\r\n");
                			got_light = TRUE;                                                   // this is a lie, but helps (generates stop bit)
                		}
                		else if (ir_data_pulse_time >= FAN_1_PULSE_LEN_MIN && ir_data_space_time >= FAN_1_PAUSE_LEN_MIN)
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Generating virtual stop bit\r\n");
                			got_light = TRUE;                                                   // this is a lie, but helps (generates stop bit)
                		}
                	}
                	else
#endif // #if IRMP_SUPPORT_FAN_PROTOCOL==1
#if IRMP_SUPPORT_SERIAL==1
                	// NETBOX generates no stop bit, here is the timeout condition:
                	if ((irmp_param.flags & IRMP_PARAM_FLAG_IS_SERIAL) && irmp_param.protocol==IRMP_NETBOX_PROTOCOL &&
                			ir_data_space_time >= NETBOX_PULSE_LEN * (NETBOX_COMPLETE_DATA_LEN - ir_rx_bit_count))
                	{
                		got_light = TRUE;                                                       // this is a lie, but helps (generates stop bit)
                	}
                	else
#endif // #if IRMP_SUPPORT_SERIAL==1
#if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_GRUNDIG_PROTOCOL && !irmp_param.stop_bit)
                	{
                		if (ir_data_space_time > IR60_TIMEOUT_LEN && (ir_rx_bit_count==5 || ir_rx_bit_count==6))
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to IR60 protocol\r\n");
                			got_light = TRUE;                                       // this is a lie, but generates a stop bit ;-)
                			irmp_param.stop_bit = TRUE;                             // set flag

                			irmp_param.protocol         = IRMP_IR60_PROTOCOL;       // change protocol
                			irmp_param.complete_len     = IR60_COMPLETE_DATA_LEN;   // correct complete len
                			irmp_param.address_offset   = IR60_ADDRESS_OFFSET;
                			irmp_param.address_end      = IR60_ADDRESS_OFFSET + IR60_ADDRESS_LEN;
                			irmp_param.command_offset   = IR60_COMMAND_OFFSET;
                			irmp_param.command_end      = IR60_COMMAND_OFFSET + IR60_COMMAND_LEN;

                			irmp_tmp_command <<= 1;
                			irmp_tmp_command |= first_bit;
                		}
                		else if (ir_data_space_time >= 2 * irmp_param.space_1_len_max && ir_rx_bit_count >= GRUNDIG_COMPLETE_DATA_LEN - 2)
                		{                                                           // special manchester decoder
                			irmp_param.complete_len = GRUNDIG_COMPLETE_DATA_LEN;    // correct complete len
                			got_light = TRUE;                                       // this is a lie, but generates a stop bit ;-)
                			irmp_param.stop_bit = TRUE;                             // set flag
                		}
                		else if (ir_rx_bit_count >= GRUNDIG_COMPLETE_DATA_LEN)
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to NOKIA protocol, bits rx = %d\r\n", ir_rx_bit_count);
                			irmp_param.protocol         = IRMP_NOKIA_PROTOCOL;      // change protocol
                			irmp_param.address_offset   = NOKIA_ADDRESS_OFFSET;
                			irmp_param.address_end      = NOKIA_ADDRESS_OFFSET + NOKIA_ADDRESS_LEN;
                			irmp_param.command_offset   = NOKIA_COMMAND_OFFSET;
                			irmp_param.command_end      = NOKIA_COMMAND_OFFSET + NOKIA_COMMAND_LEN;

                			if (irmp_tmp_command & 0x300)
                			{
                				irmp_tmp_address = (irmp_tmp_command >> 8);
                				irmp_tmp_command &= 0xFF;
                			}
                		}
                	} // if (irmp_param.protocol==IRMP_GRUNDIG_PROTOCOL && !irmp_param.stop_bit)
                	else
#endif // #if IRMP_SUPPORT_GRUNDIG_NOKIA_IR60_PROTOCOL==1
#if IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_RUWIDO_PROTOCOL && !irmp_param.stop_bit)
                	{
                		if (ir_data_space_time >= 2 * irmp_param.space_1_len_max && ir_rx_bit_count >= RUWIDO_COMPLETE_DATA_LEN - 2)
                		{                                                           // special manchester decoder
                			irmp_param.complete_len = RUWIDO_COMPLETE_DATA_LEN;     // correct complete len
                			got_light = TRUE;                                       // this is a lie, but generates a stop bit ;-)
                			irmp_param.stop_bit = TRUE;                             // set flag
                		}
                		else if (ir_rx_bit_count >= RUWIDO_COMPLETE_DATA_LEN)
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to SIEMENS protocol\r\n");
                			irmp_param.protocol         = IRMP_SIEMENS_PROTOCOL;    // change protocol
                			irmp_param.address_offset   = SIEMENS_ADDRESS_OFFSET;
                			irmp_param.address_end      = SIEMENS_ADDRESS_OFFSET + SIEMENS_ADDRESS_LEN;
                			irmp_param.command_offset   = SIEMENS_COMMAND_OFFSET;
                			irmp_param.command_end      = SIEMENS_COMMAND_OFFSET + SIEMENS_COMMAND_LEN;
                			//                   76543210
                			// RUWIDO:  AAAAAAAAACCCCCCCp
                			// SIEMENS: AAAAAAAAAAACCCCCCCCCCp
                			irmp_tmp_address <<= 2;
                			irmp_tmp_address |= (irmp_tmp_command >> 6);
                			irmp_tmp_command &= 0x003F;
                			irmp_tmp_command |= last_value;
                		}
                	} // if (irmp_param.protocol==IRMP_RUWIDO_PROTOCOL && !irmp_param.stop_bit)
                	else
#endif // #if IRMP_SUPPORT_SIEMENS_OR_RUWIDO_PROTOCOL==1
#if IRMP_SUPPORT_ROOMBA_PROTOCOL==1
                	if (irmp_param.protocol==IRMP_ROOMBA_PROTOCOL &&		// Roomba has no stop bit
                			ir_rx_bit_count >= ROOMBA_COMPLETE_DATA_LEN - 1)// it's the last data bit...
                	{                                                       // break and close this frame
                		if (ir_data_pulse_time >= ROOMBA_1_PULSE_LEN_MIN && ir_data_pulse_time <= ROOMBA_1_PULSE_LEN_MAX)
                		{
                			ir_data_space_time = ROOMBA_1_PAUSE_LEN_EXACT;
                		}
                		else if (ir_data_pulse_time >= ROOMBA_0_PULSE_LEN_MIN && ir_data_pulse_time <= ROOMBA_0_PULSE_LEN_MAX)
                		{
                			ir_data_space_time = ROOMBA_0_PAUSE_LEN;
                		}

                		got_light = TRUE;                                   // this is a lie, but helps (generates stop bit)
                	}
                	else
#endif // #if IRMP_SUPPORT_ROOMBA_PROTOCOL==1
#if IRMP_SUPPORT_MANCHESTER==1
                	if ((irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER) &&
                			ir_data_space_time >= 2 * irmp_param.space_1_len_max && ir_rx_bit_count >= irmp_param.complete_len - 2 && !irmp_param.stop_bit)
                	{                                                       // special manchester decoder
                		got_light = TRUE;                                   // this is a lie, but generates a stop bit ;-)
                		irmp_param.stop_bit = TRUE;                         // set flag
                	}
                	else
#endif // IRMP_SUPPORT_MANCHESTER==1
                	if (ir_data_space_time > IRMP_TIMEOUT_LEN)                 // timeout?
                	{
                		if (ir_rx_bit_count==irmp_param.complete_len - 1 && irmp_param.stop_bit==0)
                		{
                			ir_rx_bit_count++;
                		}
#if IRMP_SUPPORT_NEC_PROTOCOL==1
                		else if ((irmp_param.protocol==IRMP_NEC_PROTOCOL || irmp_param.protocol==IRMP_NEC42_PROTOCOL) && ir_rx_bit_count==0)
                		{                                                               // it was a non-standard repetition frame
                			M1_LOG_D(M1_LOGDB_TAG, "Detected non-standard repetition frame, switching to NEC repetition\r\n");
                			if (key_repetition_len < NEC_FRAME_REPEAT_PAUSE_LEN_MAX)
                			{
                				irmp_param.stop_bit     = TRUE;                         // set flag
                				irmp_param.protocol     = IRMP_NEC_PROTOCOL;            // switch protocol
                				irmp_param.complete_len = ir_rx_bit_count;              // patch length: 16 or 17
                				irmp_tmp_address = irmp_last_address;                   // address is last address
                				irmp_tmp_command = irmp_last_command;                   // command is last command
                				irmp_flags |= IRMP_FLAG_REPETITION;
                				key_repetition_len = 0;
                			}
                			else
                			{
                				M1_LOG_D(M1_LOGDB_TAG, "Ignoring NEC repetition frame: timeout occured, key_repetition_len = %u > %u\r\n",
                        			(unsigned int) key_repetition_len, (unsigned int) NEC_FRAME_REPEAT_PAUSE_LEN_MAX);
                				ir_rx_start_pulse_detected = FALSE; // ir_rx_frame_detected = FALSE (?)
                			}
                		}
#endif // IRMP_SUPPORT_NEC_PROTOCOL==1
#if IRMP_SUPPORT_JVC_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_NEC_PROTOCOL && (ir_rx_bit_count==16 || ir_rx_bit_count==17))      // it was a JVC stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to JVC protocol, bits rx = %d\r\n", ir_rx_bit_count);
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.protocol     = IRMP_JVC_PROTOCOL;                        // switch protocol
                			irmp_param.complete_len = ir_rx_bit_count;                                 // patch length: 16 or 17
                			irmp_tmp_command        = (irmp_tmp_address >> 4);                  // set command: upper 12 bits are command bits
                			irmp_tmp_address        = irmp_tmp_address & 0x000F;                // lower 4 bits are address bits
                			//ir_rx_start_pulse_detected = 1;
                		}
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1
#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_NEC_PROTOCOL && (ir_rx_bit_count==28 || ir_rx_bit_count==29))      // it was a LGAIR stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to LGAIR protocol, bits rx = %d\r\n", ir_rx_bit_count);
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.protocol     = IRMP_LGAIR_PROTOCOL;                      // switch protocol
                			irmp_param.complete_len = ir_rx_bit_count;                                 // patch length: 16 or 17
                			irmp_tmp_command        = irmp_lgair_command;                       // set command: upper 8 bits are command bits
                			irmp_tmp_address        = irmp_lgair_address;                       // lower 4 bits are address bits
                			//ir_rx_start_pulse_detected = 1;
                		}
#endif // IRMP_SUPPORT_LGAIR_PROTOCOL==1

#if IRMP_SUPPORT_NEC42_PROTOCOL==1
#if IRMP_SUPPORT_NEC_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_NEC42_PROTOCOL && ir_rx_bit_count==32)      // it was a NEC stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to NEC protocol\r\n");
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.protocol     = IRMP_NEC_PROTOCOL;                        // switch protocol
                			irmp_param.complete_len = ir_rx_bit_count;                                 // patch length: 16 or 17
                			//        0123456789ABC0123456789ABC0123456701234567
                			// NEC42: AAAAAAAAAAAAAaaaaaaaaaaaaaCCCCCCCCcccccccc
                			// NEC:   AAAAAAAAaaaaaaaaCCCCCCCCcccccccc
                			irmp_tmp_address        |= (irmp_tmp_address2 & 0x0007) << 13;      // fm 2012-02-13: 12 -> 13
                			irmp_tmp_command        = (irmp_tmp_address2 >> 3) | (irmp_tmp_command << 10);
                		}
#endif // IRMP_SUPPORT_NEC_PROTOCOL==1

#if IRMP_SUPPORT_LGAIR_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_NEC42_PROTOCOL && ir_rx_bit_count==28)      // it was a NEC stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to LGAIR protocol\r\n");
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.protocol     = IRMP_LGAIR_PROTOCOL;                      // switch protocol
                			irmp_param.complete_len = ir_rx_bit_count;                                 // patch length: 16 or 17
                			irmp_tmp_address        = irmp_lgair_address;
                			irmp_tmp_command        = irmp_lgair_command;
                		}
#endif // IRMP_SUPPORT_LGAIR_PROTOCOL==1

#if IRMP_SUPPORT_JVC_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_NEC42_PROTOCOL && (ir_rx_bit_count==16 || ir_rx_bit_count==17))  // it was a JVC stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to JVC protocol, bits rx = %d\r\n", ir_rx_bit_count);
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.protocol     = IRMP_JVC_PROTOCOL;                        // switch protocol
                			irmp_param.complete_len = ir_rx_bit_count;                                 // patch length: 16 or 17
                			//        0123456789ABC0123456789ABC0123456701234567
                			// NEC42: AAAAAAAAAAAAAaaaaaaaaaaaaaCCCCCCCCcccccccc
                			// JVC:   AAAACCCCCCCCCCCC
                			irmp_tmp_command        = (irmp_tmp_address >> 4) | (irmp_tmp_address2 << 9);   // set command: upper 12 bits are command bits
                			irmp_tmp_address        = irmp_tmp_address & 0x000F;                            // lower 4 bits are address bits
                		}
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1
#endif // IRMP_SUPPORT_NEC42_PROTOCOL==1

#if IRMP_SUPPORT_SAMSUNG48_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_SAMSUNG48_PROTOCOL && ir_rx_bit_count==32)          // it was a SAMSUNG32 stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to SAMSUNG32 protocol\r\n");
                			irmp_param.protocol         = IRMP_SAMSUNG32_PROTOCOL;
                			irmp_param.command_offset   = SAMSUNG32_COMMAND_OFFSET;
                			irmp_param.command_end      = SAMSUNG32_COMMAND_OFFSET + SAMSUNG32_COMMAND_LEN;
                			irmp_param.complete_len     = SAMSUNG32_COMPLETE_DATA_LEN;
                		}
#endif // IRMP_SUPPORT_SAMSUNG_PROTOCOL==1

#if IRMP_SUPPORT_RCMM_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_RCMM32_PROTOCOL && (ir_rx_bit_count==12 || ir_rx_bit_count==24))  // it was a RCMM stop bit
                		{
                			if (ir_rx_bit_count==12)
                			{
                				irmp_tmp_command = (irmp_tmp_address & 0xFF);                   // set command: lower 8 bits are command bits
                				irmp_tmp_address >>= 8;                                         // upper 4 bits are address bits
                				M1_LOG_D(M1_LOGDB_TAG, "Switching to RCMM12 protocol, bits rx = %d\r\n", ir_rx_bit_count);
                				irmp_param.protocol     = IRMP_RCMM12_PROTOCOL;                 // switch protocol
                			}
                			else // if ((ir_rx_bit_count==24)
                			{
                				M1_LOG_D(M1_LOGDB_TAG, "Switching to RCMM24 protocol, bits rx = %d\r\n", ir_rx_bit_count);
                				irmp_param.protocol     = IRMP_RCMM24_PROTOCOL;                 // switch protocol
                			}
                			irmp_param.stop_bit     = TRUE;                                     // set flag
                			irmp_param.complete_len = ir_rx_bit_count;                          // patch length
                		}
#endif // IRMP_SUPPORT_RCMM_PROTOCOL==1

#if IRMP_SUPPORT_TECHNICS_PROTOCOL==1
                		else if (irmp_param.protocol==IRMP_MATSUSHITA_PROTOCOL && ir_rx_bit_count==22)  // it was a TECHNICS stop bit
                		{
                			M1_LOG_D(M1_LOGDB_TAG, "Switching to TECHNICS protocol, bits rx = %d\r\n", ir_rx_bit_count);
                			// Situation:
                			// The first 12 bits have been stored in irmp_tmp_command (LSB first)
                			// The following 10 bits have been stored in irmp_tmp_address (LSB first)
                			// The code of TECHNICS is:
                			//   cccccccccccCCCCCCCCCCC (11 times c and 11 times C)
                			//   ccccccccccccaaaaaaaaaa
                			// where C is inverted value of c

                			irmp_tmp_address <<= 1;
                			if (irmp_tmp_command & (1<<11))
                			{
                				irmp_tmp_address |= 1;
                				irmp_tmp_command &= ~(1<<11);
                			}

                			if (irmp_tmp_command==((~irmp_tmp_address) & 0x07FF))
                			{
                				irmp_tmp_address = 0;
                				irmp_param.protocol     = IRMP_TECHNICS_PROTOCOL;                   // switch protocol
                				irmp_param.complete_len = ir_rx_bit_count;                                 // patch length
                			}
                			else
                			{
                				M1_LOG_E(M1_LOGDB_TAG, "E8: TECHNICS frame error\r\n");
                				ir_rx_start_pulse_detected = FALSE;                    // wait for another start bit...
                				ir_data_pulse_time         = 0;
                				ir_data_space_time         = 0;
                			}
                		} // else if (irmp_param.protocol==IRMP_MATSUSHITA_PROTOCOL && ir_rx_bit_count==22)  // it was a TECHNICS stop bit
#endif // IRMP_SUPPORT_TECHNICS_PROTOCOL==1
                		else
                		{
                			//M1_LOG_E(M1_LOGDB_TAG, "E2: space %d after data bit %d too long\r\n", ir_data_space_time, ir_rx_bit_count);
                			M1_LOG_E(M1_LOGDB_TAG, "E2: UNKNOWN protocol. %d bits have been received!\r\n", ir_rx_bit_count);
                			ir_rx_start_pulse_detected = FALSE;                    // wait for another start bit...
                			ir_data_pulse_time         = 0;
                			ir_data_space_time         = 0;
                		}
                	} // if (ir_data_space_time > IRMP_TIMEOUT_LEN)
                } // if (rx_logic_high)
                else
                {
                	got_light = TRUE;
                } // else
                	// if (rx_logic_high)

                if (got_light)
                {
                    M1_LOG_D(M1_LOGDB_TAG, "[bit %2d: pulse = %3d, space = %3d] ", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);

#if IRMP_SUPPORT_MANCHESTER==1
                    if ((irmp_param.flags & IRMP_PARAM_FLAG_IS_MANCHESTER))                                     // Manchester
                    {
#if IRMP_SUPPORT_MERLIN_PROTOCOL==1
                        if (irmp_param.complete_len==ir_rx_bit_count && irmp_param.protocol==IRMP_MERLIN_PROTOCOL)
                        {
                            if (last_value==0)
                            {
                                if (ir_data_pulse_time >= 2 * irmp_param.pulse_1_len_min && ir_data_pulse_time <= 2 * irmp_param.pulse_1_len_max &&
                                    last_space >= irmp_param.space_1_len_min && last_space <= irmp_param.pulse_1_len_max)
                                {
                                    irmp_param.complete_len += 2;
                                    irmp_decode_bit(0);
                                    irmp_decode_bit(1);
                                    M1_LOG_D(M1_LOGDB_TAG, "01\r\n");
                                }
                            }
                            else
                            {
                                if (last_space >= 2 * irmp_param.space_1_len_min && last_space <= 2 * irmp_param.pulse_1_len_max)
                                {
                                    if (ir_data_pulse_time >= irmp_param.pulse_1_len_min && ir_data_pulse_time <= irmp_param.pulse_1_len_max)
                                    {
                                        irmp_param.complete_len++;
                                        irmp_decode_bit(0);
                                        M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                                    }
                                    else if (ir_data_pulse_time >= 2 * irmp_param.pulse_1_len_min && ir_data_pulse_time <= 2 * irmp_param.pulse_1_len_max)
                                    {
                                        irmp_param.complete_len += 2;
                                        irmp_decode_bit(0);
                                        irmp_decode_bit(1);
                                        M1_LOG_D(M1_LOGDB_TAG, "01\r\n");
                                    }
                                }
                            }
                        }
                        else
#endif // #if IRMP_SUPPORT_MERLIN_PROTOCOL==1
#if 1
                        if (ir_data_pulse_time > irmp_param.pulse_1_len_max /* && ir_data_pulse_time <= 2 * irmp_param.pulse_1_len_max */)
#else // better, but some IR-RCs use asymmetric timings :-/
                        if (ir_data_pulse_time > irmp_param.pulse_1_len_max && ir_data_pulse_time <= 2 * irmp_param.pulse_1_len_max &&
                            ir_data_space_time <= 2 * irmp_param.space_1_len_max)
#endif
                        {
#if IRMP_SUPPORT_RC6_PROTOCOL==1
                            if (irmp_param.protocol==IRMP_RC6_PROTOCOL && ir_rx_bit_count==4 && ir_data_pulse_time > RC6_TOGGLE_BIT_LEN_MIN)         // RC6 toggle bit
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "T");
                                if (irmp_param.complete_len==RC6_COMPLETE_DATA_LEN_LONG)                      // RC6 mode 6A
                                {
                                    irmp_decode_bit(1);
                                    last_value = 1;
                                }
                                else                                                                            // RC6 mode 0
                                {
                                    irmp_decode_bit(0);
                                    last_value = 0;
                                }
                                M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                            }
                            else
#endif // IRMP_SUPPORT_RC6_PROTOCOL==1
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", (irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? '0' : '1');
                                irmp_decode_bit((irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? 0  :  1 );

#if IRMP_SUPPORT_RC6_PROTOCOL==1
                                if (irmp_param.protocol==IRMP_RC6_PROTOCOL && ir_rx_bit_count==4 && ir_data_pulse_time > RC6_TOGGLE_BIT_LEN_MIN)      // RC6 toggle bit
                                {
                                	M1_LOG_D(M1_LOGDB_TAG, "T");
                                    irmp_decode_bit(1);

                                    if (ir_data_space_time > 2 * irmp_param.space_1_len_max)
                                    {
                                        last_value = 0;
                                    }
                                    else
                                    {
                                        last_value = 1;
                                    }
                                    M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                                }
                                else
#endif // IRMP_SUPPORT_RC6_PROTOCOL==1
                                {
                                	M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", (irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? '1' : '0');
                                    irmp_decode_bit((irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? 1 :   0 );

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && IRMP_SUPPORT_RCII_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
                                    if (! irmp_param2.protocol)
#endif
                                    {
                                    	M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                                    }
                                    last_value = (irmp_param.flags & IRMP_PARAM_FLAG_1ST_PULSE_IS_1) ? 1 : 0;
                                }
                            }
                        }
                        else if (ir_data_pulse_time >= irmp_param.pulse_1_len_min && ir_data_pulse_time <= irmp_param.pulse_1_len_max
                                 /* && ir_data_space_time <= 2 * irmp_param.space_1_len_max */)
                        {
                            uint8_t manchester_value;

                            if (last_space > irmp_param.space_1_len_max && last_space <= 2 * irmp_param.space_1_len_max)
                            {
                                manchester_value = last_value ? 0 : 1;
                                last_value  = manchester_value;
                            }
                            else
                            {
                                manchester_value = last_value;
                            }

                            M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", manchester_value + '0');

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && (IRMP_SUPPORT_FDC_PROTOCOL==1 || IRMP_SUPPORT_RCCAR_PROTOCOL==1)
                            if (! irmp_param2.protocol)
#endif
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                            }

#if IRMP_SUPPORT_RC6_PROTOCOL==1
                            if (irmp_param.protocol==IRMP_RC6_PROTOCOL && ir_rx_bit_count==1 && manchester_value==1)     // RC6 mode != 0 ???
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Switching to RC6A protocol\r\n");
                                irmp_param.complete_len = RC6_COMPLETE_DATA_LEN_LONG;
                                irmp_param.address_offset = 5;
                                irmp_param.address_end = irmp_param.address_offset + 15;
                                irmp_param.command_offset = irmp_param.address_end + 1;                                 // skip 1 system bit, changes like a toggle bit
                                irmp_param.command_end = irmp_param.command_offset + 16 - 1;
                                irmp_tmp_address = 0;
                            }
#endif // IRMP_SUPPORT_RC6_PROTOCOL==1

                            irmp_decode_bit(manchester_value);
                        }
                        else
                        {
#if IRMP_SUPPORT_RC5_PROTOCOL==1 && IRMP_SUPPORT_FDC_PROTOCOL==1
                            if (irmp_param2.protocol==IRMP_FDC_PROTOCOL &&
                                ir_data_pulse_time >= FDC_PULSE_LEN_MIN && ir_data_pulse_time <= FDC_PULSE_LEN_MAX &&
                                ((ir_data_space_time >= FDC_1_PAUSE_LEN_MIN && ir_data_space_time <= FDC_1_PAUSE_LEN_MAX) ||
                                 (ir_data_space_time >= FDC_0_PAUSE_LEN_MIN && ir_data_space_time <= FDC_0_PAUSE_LEN_MAX)))
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "?");
                                irmp_param.protocol = 0;                // switch to FDC, see below
                            }
                            else
#endif // IRMP_SUPPORT_FDC_PROTOCOL==1
#if IRMP_SUPPORT_RC5_PROTOCOL==1 && IRMP_SUPPORT_RCCAR_PROTOCOL==1
                            if (irmp_param2.protocol==IRMP_RCCAR_PROTOCOL &&
                                ir_data_pulse_time >= RCCAR_PULSE_LEN_MIN && ir_data_pulse_time <= RCCAR_PULSE_LEN_MAX &&
                                ((ir_data_space_time >= RCCAR_1_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_1_PAUSE_LEN_MAX) ||
                                 (ir_data_space_time >= RCCAR_0_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_0_PAUSE_LEN_MAX)))
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "?");
                                irmp_param.protocol = 0;                // switch to RCCAR, see below
                            }
                            else
#endif // IRMP_SUPPORT_RCCAR_PROTOCOL==1
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "?\r\n");
                                M1_LOG_E(M1_LOGDB_TAG, "E3 manchester: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                                ir_rx_start_pulse_detected = FALSE;                            // reset flags and wait for next start bit
                                ir_data_space_time         = 0;
                            }
                        }

#if IRMP_SUPPORT_RC5_PROTOCOL==1 && IRMP_SUPPORT_FDC_PROTOCOL==1
                        if (irmp_param2.protocol==IRMP_FDC_PROTOCOL && ir_data_pulse_time >= FDC_PULSE_LEN_MIN && ir_data_pulse_time <= FDC_PULSE_LEN_MAX)
                        {
                            if (ir_data_space_time >= FDC_1_PAUSE_LEN_MIN && ir_data_space_time <= FDC_1_PAUSE_LEN_MAX)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "   1 (FDC)\r\n");
                                irmp_store_bit2 (1);
                            }
                            else if (ir_data_space_time >= FDC_0_PAUSE_LEN_MIN && ir_data_space_time <= FDC_0_PAUSE_LEN_MAX)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "   0 (FDC)\r\n");
                                irmp_store_bit2 (0);
                            }

                            if (! irmp_param.protocol)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Switching to FDC protocol\r\n");
                                memcpy (&irmp_param, &irmp_param2, sizeof (IRMP_PARAMETER));
                                irmp_param2.protocol = 0;
                                irmp_tmp_address = irmp_tmp_address2;
                                irmp_tmp_command = irmp_tmp_command2;
                            }
                        }
#endif // IRMP_SUPPORT_FDC_PROTOCOL==1
#if IRMP_SUPPORT_RC5_PROTOCOL==1 && IRMP_SUPPORT_RCCAR_PROTOCOL==1
                        if (irmp_param2.protocol==IRMP_RCCAR_PROTOCOL && ir_data_pulse_time >= RCCAR_PULSE_LEN_MIN && ir_data_pulse_time <= RCCAR_PULSE_LEN_MAX)
                        {
                            if (ir_data_space_time >= RCCAR_1_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_1_PAUSE_LEN_MAX)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "   1 (RCCAR)\r\n");
                                irmp_store_bit2 (1);
                            }
                            else if (ir_data_space_time >= RCCAR_0_PAUSE_LEN_MIN && ir_data_space_time <= RCCAR_0_PAUSE_LEN_MAX)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "   0 (RCCAR)\r\n");
                                irmp_store_bit2 (0);
                            }

                            if (! irmp_param.protocol)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Switching to RCCAR protocol\r\n");
                                memcpy (&irmp_param, &irmp_param2, sizeof (IRMP_PARAMETER));
                                irmp_param2.protocol = 0;
                                irmp_tmp_address = irmp_tmp_address2;
                                irmp_tmp_command = irmp_tmp_command2;
                            }
                        }
#endif // IRMP_SUPPORT_RCCAR_PROTOCOL==1

                        last_space      = ir_data_space_time;
                        ir_data_wait_for_falling_edge  = 0;
                    }
                    else
#endif // IRMP_SUPPORT_MANCHESTER==1

#if IRMP_SUPPORT_SERIAL==1
                    if (irmp_param.flags & IRMP_PARAM_FLAG_IS_SERIAL)
                    {
                        while (ir_rx_bit_count < irmp_param.complete_len && ir_data_pulse_time > irmp_param.pulse_1_len_max)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");
                            irmp_decode_bit(1);

                            if (ir_data_pulse_time >= irmp_param.pulse_1_len_min)
                            {
                                ir_data_pulse_time -= irmp_param.pulse_1_len_min;
                            }
                            else
                            {
                                ir_data_pulse_time = 0;
                            }
                        }

                        while (ir_rx_bit_count < irmp_param.complete_len && ir_data_space_time > irmp_param.space_1_len_max)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                            irmp_decode_bit(0);

                            if (ir_data_space_time >= irmp_param.space_1_len_min)
                            {
                                ir_data_space_time -= irmp_param.space_1_len_min;
                            }
                            else
                            {
                                ir_data_space_time = 0;
                            }
                        }
                        M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#endif // IRMP_SUPPORT_SERIAL==1

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_SAMSUNG_PROTOCOL && ir_rx_bit_count==16)       // Samsung: 16th bit
                    {
                        if (ir_data_pulse_time >= SAMSUNG_PULSE_LEN_MIN && ir_data_pulse_time <= SAMSUNG_PULSE_LEN_MAX &&
                            ir_data_space_time >= SAMSUNG_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= SAMSUNG_START_BIT_PAUSE_LEN_MAX)
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "SYNC\r\n");
                            ir_data_wait_for_falling_edge = 0;
                            ir_rx_bit_count++;
                        }
                        else  if (ir_data_pulse_time >= SAMSUNG_PULSE_LEN_MIN && ir_data_pulse_time <= SAMSUNG_PULSE_LEN_MAX)
                        {
#if IRMP_SUPPORT_SAMSUNG48_PROTOCOL==1
                            M1_LOG_D(M1_LOGDB_TAG, "Switching to SAMSUNG48 protocol ");
                            irmp_param.protocol         = IRMP_SAMSUNG48_PROTOCOL;
                            irmp_param.command_offset   = SAMSUNG48_COMMAND_OFFSET;
                            irmp_param.command_end      = SAMSUNG48_COMMAND_OFFSET + SAMSUNG48_COMMAND_LEN;
                            irmp_param.complete_len     = SAMSUNG48_COMPLETE_DATA_LEN;
#else
                            M1_LOG_D(M1_LOGDB_TAG, "Switching to SAMSUNG32 protocol ");
                            irmp_param.protocol         = IRMP_SAMSUNG32_PROTOCOL;
                            irmp_param.command_offset   = SAMSUNG32_COMMAND_OFFSET;
                            irmp_param.command_end      = SAMSUNG32_COMMAND_OFFSET + SAMSUNG32_COMMAND_LEN;
                            irmp_param.complete_len     = SAMSUNG32_COMPLETE_DATA_LEN;
#endif
                            if (ir_data_space_time >= SAMSUNG_1_PAUSE_LEN_MIN && ir_data_space_time <= SAMSUNG_1_PAUSE_LEN_MAX)
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");
                                irmp_decode_bit(1);
                                ir_data_wait_for_falling_edge = 0;
                            }
                            else
                            {
                            	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                                irmp_decode_bit(0);
                                ir_data_wait_for_falling_edge = 0;
                            }
                        }
                        else
                        {                                                           // timing incorrect!
                            M1_LOG_E(M1_LOGDB_TAG, "E3 Samsung: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                            ir_rx_start_pulse_detected = FALSE;                            // reset flags and wait for next start bit
                            ir_data_space_time         = 0;
                        }
                    }
                    else
#endif // IRMP_SUPPORT_SAMSUNG_PROTOCOL

#if IRMP_SUPPORT_NEC16_PROTOCOL
#if IRMP_SUPPORT_NEC42_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_NEC42_PROTOCOL &&
#else // IRMP_SUPPORT_NEC_PROTOCOL instead
                    if (irmp_param.protocol==IRMP_NEC_PROTOCOL &&
#endif // IRMP_SUPPORT_NEC42_PROTOCOL==1
                        ir_rx_bit_count==8 && ir_data_space_time >= NEC_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= NEC_START_BIT_PAUSE_LEN_MAX)
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "Switching to NEC16 protocol\r\n");
                        irmp_param.protocol         = IRMP_NEC16_PROTOCOL;
                        irmp_param.address_offset   = NEC16_ADDRESS_OFFSET;
                        irmp_param.address_end      = NEC16_ADDRESS_OFFSET + NEC16_ADDRESS_LEN;
                        irmp_param.command_offset   = NEC16_COMMAND_OFFSET;
                        irmp_param.command_end      = NEC16_COMMAND_OFFSET + NEC16_COMMAND_LEN;
                        irmp_param.complete_len     = NEC16_COMPLETE_DATA_LEN;
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#endif // IRMP_SUPPORT_NEC16_PROTOCOL

#if IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_BANG_OLUFSEN_PROTOCOL)
                    {
                        if (ir_data_pulse_time >= BANG_OLUFSEN_PULSE_LEN_MIN && ir_data_pulse_time <= BANG_OLUFSEN_PULSE_LEN_MAX)
                        {
                            if (ir_rx_bit_count==1)                                      // Bang & Olufsen: 3rd bit
                            {
                                if (ir_data_space_time >= BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_START_BIT3_PAUSE_LEN_MAX)
                                {
                                    M1_LOG_D(M1_LOGDB_TAG, "3rd start bit\r\n");
                                    ir_data_wait_for_falling_edge = 0;
                                    ir_rx_bit_count++;
                                }
                                else
                                {                                                   // timing incorrect!
                                    M1_LOG_E(M1_LOGDB_TAG, "E3a B&O: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                                    ir_rx_start_pulse_detected = FALSE;                    // reset flags and wait for next start bit
                                    ir_data_space_time         = 0;
                                }
                            }
                            else if (ir_rx_bit_count==19)                                // Bang & Olufsen: trailer bit
                            {
                                if (ir_data_space_time >= BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN_MAX)
                                {
                                    M1_LOG_D(M1_LOGDB_TAG, "trailer bit\r\n");
                                    ir_data_wait_for_falling_edge = 0;
                                    ir_rx_bit_count++;
                                }
                                else
                                {                                                   // timing incorrect!
                                    M1_LOG_E(M1_LOGDB_TAG, "E3b B&O: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                                    ir_rx_start_pulse_detected = FALSE;                    // reset flags and wait for next start bit
                                    ir_data_space_time         = 0;
                                }
                            }
                            else
                            {
                                if (ir_data_space_time >= BANG_OLUFSEN_1_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_1_PAUSE_LEN_MAX)
                                {                                                   // pulse & space timings correct for "1"?
                                	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");
                                    irmp_decode_bit(1);
                                    last_value = 1;
                                    ir_data_wait_for_falling_edge = 0;
                                }
                                else if (ir_data_space_time >= BANG_OLUFSEN_0_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_0_PAUSE_LEN_MAX)
                                {                                                   // pulse & space timings correct for "0"?
                                	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                                    irmp_decode_bit(0);
                                    last_value = 0;
                                    ir_data_wait_for_falling_edge = 0;
                                }
                                else if (ir_data_space_time >= BANG_OLUFSEN_R_PAUSE_LEN_MIN && ir_data_space_time <= BANG_OLUFSEN_R_PAUSE_LEN_MAX)
                                {
                                	M1_LOG_D(M1_LOGDB_TAG, "%c\r\n", last_value + '0');
                                    irmp_decode_bit(last_value);
                                    ir_data_wait_for_falling_edge = 0;
                                }
                                else
                                {                                                   // timing incorrect!
                                    M1_LOG_E(M1_LOGDB_TAG, "E3c B&O: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                                    ir_rx_start_pulse_detected = FALSE;                    // reset flags and wait for next start bit
                                    ir_data_space_time         = 0;
                                }
                            }
                        }
                        else
                        {                                                           // timing incorrect!
                            M1_LOG_E(M1_LOGDB_TAG, "E3d B&O: incorrect timing: data bit %d,  pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                            ir_rx_start_pulse_detected = FALSE;                            // reset flags and wait for next start bit
                            ir_data_space_time         = 0;
                        }
                    }
                    else
#endif // IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL

#if IRMP_SUPPORT_RCMM_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_RCMM32_PROTOCOL)
                    {
                        if (ir_data_space_time >= RCMM32_BIT_00_PAUSE_LEN_MIN && ir_data_space_time <= RCMM32_BIT_00_PAUSE_LEN_MAX)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "00\r\n");
                            irmp_decode_bit(0);
                            irmp_decode_bit(0);
                        }
                        else if (ir_data_space_time >= RCMM32_BIT_01_PAUSE_LEN_MIN && ir_data_space_time <= RCMM32_BIT_01_PAUSE_LEN_MAX)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "01\r\n");
                            irmp_decode_bit(0);
                            irmp_decode_bit(1);
                        }
                        else if (ir_data_space_time >= RCMM32_BIT_10_PAUSE_LEN_MIN && ir_data_space_time <= RCMM32_BIT_10_PAUSE_LEN_MAX)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "10\r\n");
                            irmp_decode_bit(1);
                            irmp_decode_bit(0);
                        }
                        else if (ir_data_space_time >= RCMM32_BIT_11_PAUSE_LEN_MIN && ir_data_space_time <= RCMM32_BIT_11_PAUSE_LEN_MAX)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "11\r\n");
                            irmp_decode_bit(1);
                            irmp_decode_bit(1);
                        }

                        M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#endif // #if IRMP_SUPPORT_RCMM_PROTOCOL==1

                    if (ir_data_pulse_time >= irmp_param.pulse_1_len_min && ir_data_pulse_time <= irmp_param.pulse_1_len_max &&
                        ir_data_space_time >= irmp_param.space_1_len_min && ir_data_space_time <= irmp_param.space_1_len_max)
                    {                                                               // pulse & space timings correct for "1"?
                    	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");
                        irmp_decode_bit(1);
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else if (ir_data_pulse_time >= irmp_param.pulse_0_len_min && ir_data_pulse_time <= irmp_param.pulse_0_len_max &&
                             ir_data_space_time >= irmp_param.space_0_len_min && ir_data_space_time <= irmp_param.space_0_len_max)
                    {                                                               // pulse & space timings correct for "0"?
                    	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                        irmp_decode_bit(0);
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#if IRMP_SUPPORT_KATHREIN_PROTOCOL

                    if (irmp_param.protocol==IRMP_KATHREIN_PROTOCOL &&
                        ir_data_pulse_time >= KATHREIN_1_PULSE_LEN_MIN && ir_data_pulse_time <= KATHREIN_1_PULSE_LEN_MAX &&
                        (((ir_rx_bit_count==8 || ir_rx_bit_count==6) &&
                                ir_data_space_time >= KATHREIN_SYNC_BIT_PAUSE_LEN_MIN && ir_data_space_time <= KATHREIN_SYNC_BIT_PAUSE_LEN_MAX) ||
                         (ir_rx_bit_count==12 &&
                                ir_data_space_time >= KATHREIN_START_BIT_PAUSE_LEN_MIN && ir_data_space_time <= KATHREIN_START_BIT_PAUSE_LEN_MAX)))

                    {
                        if (ir_rx_bit_count==8)
                        {
                            ir_rx_bit_count++;
                            M1_LOG_D(M1_LOGDB_TAG, "S\r\n");
                            irmp_tmp_command <<= 1;
                        }
                        else
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "S\r\n");
                            irmp_decode_bit(1);
                        }
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#endif // IRMP_SUPPORT_KATHREIN_PROTOCOL
#if IRMP_SUPPORT_MELINERA_PROTOCOL==1
#if IRMP_SUPPORT_NEC42_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_NEC42_PROTOCOL &&
#else // IRMP_SUPPORT_NEC_PROTOCOL instead
                    if (irmp_param.protocol==IRMP_NEC_PROTOCOL &&
#endif // IRMP_SUPPORT_NEC42_PROTOCOL==1
                       (
                        (ir_data_pulse_time >= MELINERA_0_PULSE_LEN_MIN && ir_data_pulse_time <= MELINERA_0_PULSE_LEN_MAX &&
                         ir_data_space_time >= MELINERA_0_PAUSE_LEN_MIN && ir_data_space_time <= MELINERA_0_PAUSE_LEN_MAX) ||
                        (ir_data_pulse_time >= MELINERA_1_PULSE_LEN_MIN && ir_data_pulse_time <= MELINERA_1_PULSE_LEN_MAX &&
                         ir_data_space_time >= MELINERA_1_PAUSE_LEN_MIN && ir_data_space_time <= MELINERA_1_PAUSE_LEN_MAX)
                       ))
                    {
                        M1_LOG_D(M1_LOGDB_TAG, "Switching to MELINERA protocol ");
                        irmp_param.protocol         = IRMP_MELINERA_PROTOCOL;
                        irmp_param.pulse_0_len_min  = MELINERA_0_PULSE_LEN_MIN;
                        irmp_param.pulse_0_len_max  = MELINERA_0_PULSE_LEN_MAX;
                        irmp_param.space_0_len_min  = MELINERA_0_PAUSE_LEN_MIN;
                        irmp_param.pulse_0_len_max  = MELINERA_0_PAUSE_LEN_MAX;
                        irmp_param.pulse_1_len_min  = MELINERA_1_PULSE_LEN_MIN;
                        irmp_param.pulse_1_len_max  = MELINERA_1_PULSE_LEN_MAX;
                        irmp_param.space_1_len_min  = MELINERA_1_PAUSE_LEN_MIN;
                        irmp_param.pulse_1_len_max  = MELINERA_1_PAUSE_LEN_MAX;
                        irmp_param.address_offset   = MELINERA_ADDRESS_OFFSET;
                        irmp_param.address_end      = MELINERA_ADDRESS_OFFSET + MELINERA_ADDRESS_LEN;
                        irmp_param.command_offset   = MELINERA_COMMAND_OFFSET;
                        irmp_param.command_end      = MELINERA_COMMAND_OFFSET + MELINERA_COMMAND_LEN;
                        irmp_param.complete_len     = MELINERA_COMPLETE_DATA_LEN;

                        if (ir_data_space_time >= MELINERA_0_PAUSE_LEN_MIN && ir_data_space_time <= MELINERA_0_PAUSE_LEN_MAX)
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "0\r\n");
                            irmp_decode_bit(0);
                        }
                        else
                        {
                        	M1_LOG_D(M1_LOGDB_TAG, "1\r\n");
                            irmp_decode_bit(1);
                        }

                        M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                        ir_data_wait_for_falling_edge = 0;
                    }
                    else
#endif // IRMP_SUPPORT_MELINERA_PROTOCOL
                    {                                                               // timing incorrect!
                        M1_LOG_E(M1_LOGDB_TAG, "E3: incorrect timing: data bit %d, pulse: %d, space: %d\r\n", ir_rx_bit_count, ir_data_pulse_time, ir_data_space_time);
                        ir_rx_start_pulse_detected = FALSE;                                // reset flags and wait for next start bit
                        ir_data_space_time = 0;
                    }

                    ir_data_pulse_time = 0;
                } // if (got_light)
            } // else if (ir_data_wait_for_falling_edge)

            // Not (ir_start_wait_for_falling_edge), not (ir_data_wait_for_falling_edge)
            // Either a rising edge or timeout will reach here
            else
            {
            	ir_data_pulse_time = pulse_len; // update counter
                if (!rx_logic_high)
                {
                    // Program reaches here only when it's timeout (given in pulse_len) for data bit
                	M1_LOG_E(M1_LOGDB_TAG, "E9: pulse too long: %d, bits received: %d\r\n", ir_data_pulse_time, ir_rx_bit_count);
                    ir_data_pulse_time         = 0;
                    ir_data_space_time         = 0;
                    ir_rx_start_pulse_detected = FALSE;       // reset flags, let's wait for another start bit
                } // if (!rx_logic_high)
                else
                {
                    ir_data_wait_for_falling_edge  = 1;  // let's count the time (see above)
                    ir_data_space_time = 0;
#if IRMP_SUPPORT_RCII_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_RCII_PROTOCOL && waiting_for_2nd_pulse)
                    {
                        // fm: output is "1000 466" or "1533 466"
                        // printf ("fm: %d %d\r\n", ir_data_pulse_time * 1000000 / F_INTERRUPTS, RCII_BIT_LEN * 1000000 / F_INTERRUPTS);
                        ir_data_pulse_time -= RCII_BIT_LEN;
                        last_value = 0;

                        M1_LOG_D(M1_LOGDB_TAG, "RCII: got 2nd pulse, ir_data_pulse_time = %d\r\n", ir_data_pulse_time);
                        waiting_for_2nd_pulse = 0;
                    }
#endif
                } // else
                	// if (!rx_logic_high)
            } // else
            	// else if (ir_data_wait_for_falling_edge)

            // If enough bits have been received, let check them here to complete

            if (ir_rx_start_pulse_detected && ir_rx_bit_count==irmp_param.complete_len && irmp_param.stop_bit==0)    // enough bits received?
            {
                if (irmp_last_command==irmp_tmp_command && key_repetition_len < AUTO_FRAME_REPETITION_LEN)
                {
                    repetition_frame_number++;
                }
                else
                {
                    repetition_frame_number = 0;
                }

#if IRMP_SUPPORT_SIRCS_PROTOCOL==1
                // if SIRCS protocol and the code will be repeated within 50 ms, we will ignore 2nd and 3rd repetition frame
                if (irmp_param.protocol==IRMP_SIRCS_PROTOCOL && (repetition_frame_number==1 || repetition_frame_number==2))
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: SIRCS auto repetition frame #%d, counter = %u, auto repetition len = %u\r\n",
                                    repetition_frame_number + 1, (unsigned int) key_repetition_len, (unsigned int) AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1
                // if ORTEK protocol and the code will be repeated within 50 ms, we will ignore 2nd repetition frame
                if (irmp_param.protocol==IRMP_ORTEK_PROTOCOL && repetition_frame_number==1)
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: ORTEK auto repetition frame #%d, counter = %d, auto repetition len = %d\r\n",
                                    repetition_frame_number + 1, key_repetition_len, AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

#if 0 && IRMP_SUPPORT_KASEIKYO_PROTOCOL==1    // fm 2015-12-02: don't ignore every 2nd frame
                // if KASEIKYO protocol and the code will be repeated within 50 ms, we will ignore 2nd repetition frame
                if (irmp_param.protocol==IRMP_KASEIKYO_PROTOCOL && repetition_frame_number==1)
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: KASEIKYO auto repetition frame #%d, counter = %d, auto repetition len = %d\r\n",
                                    repetition_frame_number + 1, key_repetition_len, AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

#if 0 && IRMP_SUPPORT_SAMSUNG_PROTOCOL==1     // fm 2015-12-02: don't ignore every 2nd frame
                // if SAMSUNG32 or SAMSUNG48 protocol and the code will be repeated within 50 ms, we will ignore every 2nd frame
                if ((irmp_param.protocol==IRMP_SAMSUNG32_PROTOCOL || irmp_param.protocol==IRMP_SAMSUNG48_PROTOCOL) && (repetition_frame_number & 0x01))
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: SAMSUNG32/SAMSUNG48 auto repetition frame #%d, counter = %d, auto repetition len = %d\r\n",
                                    repetition_frame_number + 1, key_repetition_len, AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

#if IRMP_SUPPORT_NUBERT_PROTOCOL==1
                // if NUBERT protocol and the code will be repeated within 50 ms, we will ignore every 2nd frame
                if (irmp_param.protocol==IRMP_NUBERT_PROTOCOL && (repetition_frame_number & 0x01))
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: NUBERT auto repetition frame #%d, counter = %u, auto repetition len = %u\r\n",
                                    repetition_frame_number + 1, (unsigned int) key_repetition_len, (unsigned int) AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

#if IRMP_SUPPORT_SPEAKER_PROTOCOL==1
                // if SPEAKER protocol and the code will be repeated within 50 ms, we will ignore every 2nd frame
                if (irmp_param.protocol==IRMP_SPEAKER_PROTOCOL && (repetition_frame_number & 0x01))
                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code skipped: SPEAKER auto repetition frame #%d, counter = %u, auto repetition len = %u\r\n",
                                    repetition_frame_number + 1, (unsigned int) key_repetition_len, (unsigned int) AUTO_FRAME_REPETITION_LEN);
                    key_repetition_len = 0;
                }
                else
#endif

                {
                    M1_LOG_D(M1_LOGDB_TAG, "Code detected, bits received = %d\r\n", ir_rx_bit_count);
                    ir_rx_frame_detected = TRUE;

#if IRMP_SUPPORT_KATHREIN_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_KATHREIN_PROTOCOL && irmp_tmp_command==0x0000)
                    {
                        if (irmp_tmp_command==0x0000)                             // KATHREIN sends key release with command = 0x0000, ignore it
                        {
                            M1_LOG_D(M1_LOGDB_TAG, "got KATHREIN release command, ignore it\r\n");
                            ir_rx_frame_detected = FALSE;
                        }
                    }
                    else
#endif // IRMP_SUPPORT_KATHREIN_PROTOCOL
#if IRMP_SUPPORT_DENON_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_DENON_PROTOCOL)
                    {                                                               // check for repetition frame
                        if ((~irmp_tmp_command & 0x3FF)==last_irmp_denon_command) // command bits must be inverted
                        {
                            irmp_tmp_command = last_irmp_denon_command;             // use command received before!
                            last_irmp_denon_command = 0;

                            ir_decoded_protocol = irmp_param.protocol;                    // store protocol
                            ir_decoded_address = irmp_tmp_address;                        // store address
                            ir_decoded_command = irmp_tmp_command;                        // store command
                        }
                        else
                        {
                            if ((irmp_tmp_command & 0x01)==0x00)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "info Denon: waiting for inverted command repetition\r\n");
                                last_irmp_denon_command = irmp_tmp_command;
                                denon_repetition_len = 0;
                                ir_rx_frame_detected = FALSE;
                            }
                            else
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "warning Denon: got unexpected inverted command, ignoring it\r\n");
                                last_irmp_denon_command = 0;
                                ir_rx_frame_detected = FALSE;
                            }
                        }
                    }
                    else
#endif // IRMP_SUPPORT_DENON_PROTOCOL

#if IRMP_SUPPORT_GRUNDIG_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_GRUNDIG_PROTOCOL && irmp_tmp_command==0x01ff)
                    {                                                               // Grundig start frame?
                        M1_LOG_D(M1_LOGDB_TAG, "Detected GRUNDIG start frame, ignoring it\r\n");
                        ir_rx_frame_detected = FALSE;
                    }
                    else
#endif // IRMP_SUPPORT_GRUNDIG_PROTOCOL

#if IRMP_SUPPORT_NOKIA_PROTOCOL==1
                    if (irmp_param.protocol==IRMP_NOKIA_PROTOCOL && irmp_tmp_address==0x00ff && irmp_tmp_command==0x00fe)
                    {                                                               // Nokia start frame?
                        M1_LOG_D(M1_LOGDB_TAG, "Detected NOKIA start frame, ignoring it\r\n");
                        ir_rx_frame_detected = FALSE;
                    }
                    else
#endif // IRMP_SUPPORT_NOKIA_PROTOCOL
                    {
#if IRMP_SUPPORT_NEC_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_NEC_PROTOCOL && ir_rx_bit_count==0)  // repetition frame
                        {
                            if (key_repetition_len < NEC_FRAME_REPEAT_PAUSE_LEN_MAX)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Detected NEC repetition frame, key_repetition_len = %u\r\n", (unsigned int) key_repetition_len);
                                M1_LOG_D(M1_LOGDB_TAG, "REPETETION FRAME                \r\n");
                                irmp_tmp_address = irmp_last_address;                   // address is last address
                                irmp_tmp_command = irmp_last_command;                   // command is last command
                                irmp_flags |= IRMP_FLAG_REPETITION;
                                key_repetition_len = 0;
                            }
                            else
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Detected NEC repetition frame, ignoring it: timeout occured, key_repetition_len = %u > %u\r\n",
                                                (unsigned int) key_repetition_len, (unsigned int) NEC_FRAME_REPEAT_PAUSE_LEN_MAX);
                                ir_rx_frame_detected = FALSE;
                            }
                        }
#endif // IRMP_SUPPORT_NEC_PROTOCOL

#if IRMP_SUPPORT_KASEIKYO_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_KASEIKYO_PROTOCOL)
                        {
                            uint8_t xor_value;

                            xor_value = (xor_check[0] & 0x0F) ^ ((xor_check[0] & 0xF0) >> 4) ^ (xor_check[1] & 0x0F) ^ ((xor_check[1] & 0xF0) >> 4);

                            if (xor_value != (xor_check[2] & 0x0F))
                            {
                                M1_LOG_E(M1_LOGDB_TAG, "E4: wrong XOR check for customer id: 0x%1x 0x%1x\r\n", xor_value, xor_check[2] & 0x0F);
                                ir_rx_frame_detected = FALSE;
                            }

                            xor_value = xor_check[2] ^ xor_check[3] ^ xor_check[4];

                            if (xor_value != xor_check[5])
                            {
                                M1_LOG_E(M1_LOGDB_TAG, "E5: wrong XOR check for data bits: 0x%02x 0x%02x\r\n", xor_value, xor_check[5]);
                                ir_rx_frame_detected = FALSE;
                            }

                            irmp_flags |= genre2;       // write the genre2 bits into MSB of the flag byte
                        }
#endif // IRMP_SUPPORT_KASEIKYO_PROTOCOL==1

#if IRMP_SUPPORT_ORTEK_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_ORTEK_PROTOCOL)
                        {
                            if (parity==PARITY_CHECK_FAILED)
                            {
                                M1_LOG_E(M1_LOGDB_TAG, "E6: parity check failed\r\n");
                                ir_rx_frame_detected = FALSE;
                            }

                            if ((irmp_tmp_address & 0x03)==0x02)
                            {
                                M1_LOG_D(M1_LOGDB_TAG, "Code skipped: ORTEK end of transmission frame (key release)\r\n");
                                ir_rx_frame_detected = FALSE;
                            }
                            irmp_tmp_address >>= 2;
                        }
#endif // IRMP_SUPPORT_ORTEK_PROTOCOL==1

#if IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_MITSU_HEAVY_PROTOCOL)
                        {
                            check = irmp_tmp_command >> 8;                    // inverted upper byte==lower byte?
                            check = ~ check;
                            if (check==(irmp_tmp_command & 0xFF)) {         //ok:
                              irmp_tmp_command &= 0xFF;
                            }
                            else  mitsu_parity = PARITY_CHECK_FAILED;
                            if (mitsu_parity==PARITY_CHECK_FAILED)
                            {
                                M1_LOG_E(M1_LOGDB_TAG, "E7: parity check failed\r\n");
                                ir_rx_frame_detected = FALSE;
                            }
                        }
#endif // IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL

#if IRMP_SUPPORT_RC6_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_RC6_PROTOCOL && irmp_param.complete_len==RC6_COMPLETE_DATA_LEN_LONG)     // RC6 mode = 6?
                        {
                            ir_decoded_protocol = IRMP_RC6A_PROTOCOL;
                        }
                        else
#endif // IRMP_SUPPORT_RC6_PROTOCOL==1
                        {
                            ir_decoded_protocol = irmp_param.protocol;
                        }

#if IRMP_SUPPORT_FDC_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_FDC_PROTOCOL)
                        {
                            if (irmp_tmp_command & 0x000F)                          // released key?
                            {
                                irmp_tmp_command = (irmp_tmp_command >> 4) | 0x80;  // yes, set bit 7
                            }
                            else
                            {
                                irmp_tmp_command >>= 4;                             // no, it's a pressed key
                            }
                            irmp_tmp_command |= (irmp_tmp_address << 2) & 0x0F00;   // 000000CCCCAAAAAA -> 0000CCCC00000000
                            irmp_tmp_address &= 0x003F;
                        }
#endif

                        ir_decoded_address = irmp_tmp_address;                            // store address
#if IRMP_SUPPORT_NEC_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_NEC_PROTOCOL)
                        {
                            irmp_last_address = irmp_tmp_address;                   // store as last address, too
                        }
#endif

#if IRMP_SUPPORT_RC5_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_RC5_PROTOCOL)
                        {
                            irmp_tmp_command |= rc5_cmd_bit6;                       // store bit 6
                        }
#endif
#if IRMP_SUPPORT_S100_PROTOCOL==1
                        if (irmp_param.protocol==IRMP_S100_PROTOCOL)
                        {
                            irmp_tmp_command |= rc5_cmd_bit6;                       // store bit 6
                        }
#endif
                        ir_decoded_command = irmp_tmp_command;                            // store command

#if IRMP_SUPPORT_SAMSUNG_PROTOCOL==1
                        ir_decoded_id = irmp_tmp_id;
#endif
                    }
                } // else

                if (ir_rx_frame_detected)
                {
                    if (irmp_last_command==irmp_tmp_command &&
                        irmp_last_address==irmp_tmp_address &&
                        key_repetition_len < IRMP_KEY_REPETITION_LEN)
                    {
                        irmp_flags |= IRMP_FLAG_REPETITION;
                    }

                    irmp_last_address   = irmp_tmp_address;   // store as last address, too
                    irmp_last_command   = irmp_tmp_command;   // store as last command, too
                    irmp_last_raw_data[0]	= irmp_tmp_raw_data[0];
                    irmp_last_raw_data[1]	= irmp_tmp_raw_data[1];

#if IRMP_ENABLE_RELEASE_DETECTION==1
                    key_released        = FALSE;
#endif
                    key_repetition_len  = 0;
                } // if (ir_rx_frame_detected)
                else
                {
                    M1_LOG_D(M1_LOGDB_TAG, "\r\n");
                }

                ir_rx_start_pulse_detected = FALSE;                 // and wait for next start bit
                irmp_tmp_command        = 0;
                irmp_tmp_address		= 0;
                irmp_tmp_raw_data[0]		= 0;
                irmp_tmp_raw_data[1]		= 0;
                ir_data_pulse_time      = 0;
                ir_data_space_time      = 0;

#if IRMP_SUPPORT_JVC_PROTOCOL==1
                if (ir_decoded_protocol==IRMP_JVC_PROTOCOL)      // the stop bit of JVC frame is also start bit of next frame
                {                                            // set pulse time here!
                    ir_data_pulse_time = ((uint16_t)(JVC_START_BIT_PULSE_TIME));
                }
#endif // IRMP_SUPPORT_JVC_PROTOCOL==1
            } // if (ir_rx_start_pulse_detected && ir_rx_bit_count==irmp_param.complete_len && irmp_param.stop_bit==0)
        } // else
        	// if (!ir_rx_start_pulse_detected)
    } // if (!ir_rx_frame_detected)


#if IRMP_USE_IDLE_CALL==1
    // check if there is no ongoing transmission or repetition
    if (!ir_rx_start_pulse_detected && !ir_data_pulse_time
        && key_repetition_len > IRMP_KEY_REPETITION_LEN)
    {
        // no ongoing transmission
        // enough time passed since last decoded signal that a repetition won't affect our output

        irmp_idle();
    }
#endif // IRMP_USE_IDLE_CALL

} // void irmp_data_sampler(uint32_t pulse_len, uint8_t rx_logic_high)
