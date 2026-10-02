/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * @file irsnd.c
 *
 * Copyright (c) 2010-2020 Frank Meyer - frank(at)fli4l.de
 *
 * Supported AVR mikrocontrollers:
 *
 * ATtiny87,  ATtiny167
 * ATtiny45,  ATtiny85
 * ATtiny44   ATtiny84
 * ATtiny2313 ATtiny4313
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

#include "irsnd.h"
#include "m1_ir_signals.h"
#include "m1_infrared.h"

#define IRMP_NEC_REPETITION_PROTOCOL                0xFF            // pseudo protocol: NEC repetition frame
#define IRMP_SAMSUNG32_REPETITION_PROTOCOL			0xFE			// pseudo protocol: SAMSUNG32 repetition frame - Add on

typedef uint16_t     IRSND_PAUSE_LEN;


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  IR timings
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define IRSND_SIRCS_START_BIT_PULSE_LEN             (uint16_t)(SIRCS_START_BIT_PULSE_TIME + 0.5)
#define IRSND_SIRCS_START_BIT_PAUSE_LEN             (uint16_t)(SIRCS_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_SIRCS_1_PULSE_LEN                     (uint16_t)(SIRCS_1_PULSE_TIME + 0.5)
#define IRSND_SIRCS_0_PULSE_LEN                     (uint16_t)(SIRCS_0_PULSE_TIME + 0.5)
#define IRSND_SIRCS_PAUSE_LEN                       (uint16_t)(SIRCS_PAUSE_TIME + 0.5)
#define IRSND_SIRCS_AUTO_REPETITION_PAUSE_LEN       (uint16_t)(SIRCS_AUTO_REPETITION_PAUSE_TIME + 0.5)  // use uint16_t!
#define IRSND_SIRCS_FRAME_REPEAT_PAUSE_LEN          (uint16_t)(SIRCS_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_NEC_START_BIT_PULSE_LEN               (uint16_t)(NEC_START_BIT_PULSE_TIME + 0.5)
#define IRSND_NEC_START_BIT_PAUSE_LEN               (uint16_t)(NEC_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_NEC_REPEAT_START_BIT_PAUSE_LEN        (uint16_t)(NEC_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_NEC_PULSE_LEN                         (uint16_t)(NEC_PULSE_TIME + 0.5)
#define IRSND_NEC_1_PAUSE_LEN                       (uint16_t)(NEC_1_PAUSE_TIME + 0.5)
#define IRSND_NEC_0_PAUSE_LEN                       (uint16_t)(NEC_0_PAUSE_TIME + 0.5)
#define IRSND_NEC_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(NEC_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_MELINERA_START_BIT_PULSE_LEN          (uint16_t)(MELINERA_START_BIT_PULSE_TIME + 0.5)
#define IRSND_MELINERA_START_BIT_PAUSE_LEN          (uint16_t)(MELINERA_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_MELINERA_REPEAT_START_BIT_PAUSE_LEN   (uint16_t)(MELINERA_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_MELINERA_1_PULSE_LEN                  (uint16_t)(MELINERA_1_PULSE_TIME + 0.5)
#define IRSND_MELINERA_0_PULSE_LEN                  (uint16_t)(MELINERA_0_PULSE_TIME + 0.5)
#define IRSND_MELINERA_1_PAUSE_LEN                  (uint16_t)(MELINERA_1_PAUSE_TIME + 0.5)
#define IRSND_MELINERA_0_PAUSE_LEN                  (uint16_t)(MELINERA_0_PAUSE_TIME + 0.5)
#define IRSND_MELINERA_FRAME_REPEAT_PAUSE_LEN       (uint16_t)(MELINERA_FRAME_REPEAT_PAUSE_TIME + 0.5)  // use uint16_t!

#define IRSND_SAMSUNG_START_BIT_PULSE_LEN           (uint16_t)(SAMSUNG_START_BIT_PULSE_TIME + 0.5)
#define IRSND_SAMSUNG_START_BIT_PAUSE_LEN           (uint16_t)(SAMSUNG_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_SAMSUNG_PULSE_LEN                     (uint16_t)(SAMSUNG_PULSE_TIME + 0.5)
#define IRSND_SAMSUNG_1_PAUSE_LEN                   (uint16_t)(SAMSUNG_1_PAUSE_TIME + 0.5)
#define IRSND_SAMSUNG_0_PAUSE_LEN                   (uint16_t)(SAMSUNG_0_PAUSE_TIME + 0.5)
#define IRSND_SAMSUNG_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(SAMSUNG_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_SAMSUNG32_AUTO_REPETITION_PAUSE_LEN   (uint16_t)(SAMSUNG32_AUTO_REPETITION_PAUSE_TIME + 0.5)           // use uint16_t!
#define IRSND_SAMSUNG32_FRAME_REPEAT_PAUSE_LEN      (uint16_t)(SAMSUNG32_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_SAMSUNG48_AUTO_REPETITION_PAUSE_LEN   (uint16_t)(SAMSUNG48_AUTO_REPETITION_PAUSE_TIME + 0.5)           // use uint16_t!
#define IRSND_SAMSUNG48_FRAME_REPEAT_PAUSE_LEN      (uint16_t)(SAMSUNG48_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_MATSUSHITA_START_BIT_PULSE_LEN        (uint16_t)(MATSUSHITA_START_BIT_PULSE_TIME + 0.5)
#define IRSND_MATSUSHITA_START_BIT_PAUSE_LEN        (uint16_t)(MATSUSHITA_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_MATSUSHITA_PULSE_LEN                  (uint16_t)(MATSUSHITA_PULSE_TIME + 0.5)
#define IRSND_MATSUSHITA_1_PAUSE_LEN                (uint16_t)(MATSUSHITA_1_PAUSE_TIME + 0.5)
#define IRSND_MATSUSHITA_0_PAUSE_LEN                (uint16_t)(MATSUSHITA_0_PAUSE_TIME + 0.5)
#define IRSND_MATSUSHITA_FRAME_REPEAT_PAUSE_LEN     (uint16_t)(MATSUSHITA_FRAME_REPEAT_PAUSE_TIME + 0.5)             // use uint16_t!

#define IRSND_KASEIKYO_START_BIT_PULSE_LEN          (uint16_t)(KASEIKYO_START_BIT_PULSE_TIME + 0.5)
#define IRSND_KASEIKYO_START_BIT_PAUSE_LEN          (uint16_t)(KASEIKYO_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_KASEIKYO_PULSE_LEN                    (uint16_t)(KASEIKYO_PULSE_TIME + 0.5)
#define IRSND_KASEIKYO_1_PAUSE_LEN                  (uint16_t)(KASEIKYO_1_PAUSE_TIME + 0.5)
#define IRSND_KASEIKYO_0_PAUSE_LEN                  (uint16_t)(KASEIKYO_0_PAUSE_TIME + 0.5)
#define IRSND_KASEIKYO_AUTO_REPETITION_PAUSE_LEN    (uint16_t)(KASEIKYO_AUTO_REPETITION_PAUSE_TIME + 0.5)            // use uint16_t!
#define IRSND_KASEIKYO_FRAME_REPEAT_PAUSE_LEN       (uint16_t)(KASEIKYO_FRAME_REPEAT_PAUSE_TIME + 0.5)  // use uint16_t!

#define IRSND_PANASONIC_START_BIT_PULSE_LEN         (uint16_t)(PANASONIC_START_BIT_PULSE_TIME + 0.5)
#define IRSND_PANASONIC_START_BIT_PAUSE_LEN         (uint16_t)(PANASONIC_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_PANASONIC_PULSE_LEN                   (uint16_t)(PANASONIC_PULSE_TIME + 0.5)
#define IRSND_PANASONIC_1_PAUSE_LEN                 (uint16_t)(PANASONIC_1_PAUSE_TIME + 0.5)
#define IRSND_PANASONIC_0_PAUSE_LEN                 (uint16_t)(PANASONIC_0_PAUSE_TIME + 0.5)
#define IRSND_PANASONIC_AUTO_REPETITION_PAUSE_LEN   (uint16_t)(PANASONIC_AUTO_REPETITION_PAUSE_TIME + 0.5)           // use uint16_t!
#define IRSND_PANASONIC_FRAME_REPEAT_PAUSE_LEN      (uint16_t)(PANASONIC_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_MITSU_HEAVY_START_BIT_PULSE_LEN       (uint16_t)(MITSU_HEAVY_START_BIT_PULSE_TIME + 0.5)
#define IRSND_MITSU_HEAVY_START_BIT_PAUSE_LEN       (uint16_t)(MITSU_HEAVY_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_MITSU_HEAVY_PULSE_LEN                 (uint16_t)(MITSU_HEAVY_PULSE_TIME + 0.5)
#define IRSND_MITSU_HEAVY_1_PAUSE_LEN               (uint16_t)(MITSU_HEAVY_1_PAUSE_TIME + 0.5)
#define IRSND_MITSU_HEAVY_0_PAUSE_LEN               (uint16_t)(MITSU_HEAVY_0_PAUSE_TIME + 0.5)
#define IRSND_MITSU_HEAVY_FRAME_REPEAT_PAUSE_LEN    (uint16_t)(MITSU_HEAVY_FRAME_REPEAT_PAUSE_TIME + 0.5)             // use uint16_t!

#define IRSND_RECS80_START_BIT_PULSE_LEN            (uint16_t)(RECS80_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RECS80_START_BIT_PAUSE_LEN            (uint16_t)(RECS80_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RECS80_PULSE_LEN                      (uint16_t)(RECS80_PULSE_TIME + 0.5)
#define IRSND_RECS80_1_PAUSE_LEN                    (uint16_t)(RECS80_1_PAUSE_TIME + 0.5)
#define IRSND_RECS80_0_PAUSE_LEN                    (uint16_t)(RECS80_0_PAUSE_TIME + 0.5)
#define IRSND_RECS80_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(RECS80_FRAME_REPEAT_PAUSE_TIME + 0.5)    // use uint16_t!

#define IRSND_RC5_START_BIT_LEN                     (uint16_t)(RC5_BIT_TIME + 0.5)
#define IRSND_RC5_BIT_LEN                           (uint16_t)(RC5_BIT_TIME + 0.5)
#define IRSND_RC5_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(RC5_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_RC6_START_BIT_PULSE_LEN               (uint16_t)(RC6_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RC6_START_BIT_PAUSE_LEN               (uint16_t)(RC6_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RC6_BIT_LEN                           (uint16_t)(RC6_BIT_TIME + 0.5)
#define IRSND_RC6_BIT_2_LEN                         (uint16_t)(RC6_BIT_2_TIME + 0.5)
#define IRSND_RC6_BIT_3_LEN                         (uint16_t)(RC6_BIT_3_TIME + 0.5)
#define IRSND_RC6_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(RC6_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_DENON_PULSE_LEN                       (uint16_t)(DENON_PULSE_TIME + 0.5)
#define IRSND_DENON_1_PAUSE_LEN                     (uint16_t)(DENON_1_PAUSE_TIME + 0.5)
#define IRSND_DENON_0_PAUSE_LEN                     (uint16_t)(DENON_0_PAUSE_TIME + 0.5)
#define IRSND_DENON_AUTO_REPETITION_PAUSE_LEN       (uint16_t)(DENON_AUTO_REPETITION_PAUSE_TIME + 0.5)  // use uint16_t!
#define IRSND_DENON_FRAME_REPEAT_PAUSE_LEN          (uint16_t)(DENON_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_THOMSON_PULSE_LEN                     (uint16_t)(THOMSON_PULSE_TIME + 0.5)
#define IRSND_THOMSON_1_PAUSE_LEN                   (uint16_t)(THOMSON_1_PAUSE_TIME + 0.5)
#define IRSND_THOMSON_0_PAUSE_LEN                   (uint16_t)(THOMSON_0_PAUSE_TIME + 0.5)
#define IRSND_THOMSON_AUTO_REPETITION_PAUSE_LEN     (uint16_t)(THOMSON_AUTO_REPETITION_PAUSE_TIME + 0.5)             // use uint16_t!
#define IRSND_THOMSON_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(THOMSON_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_RECS80EXT_START_BIT_PULSE_LEN         (uint16_t)(RECS80EXT_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RECS80EXT_START_BIT_PAUSE_LEN         (uint16_t)(RECS80EXT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RECS80EXT_PULSE_LEN                   (uint16_t)(RECS80EXT_PULSE_TIME + 0.5)
#define IRSND_RECS80EXT_1_PAUSE_LEN                 (uint16_t)(RECS80EXT_1_PAUSE_TIME + 0.5)
#define IRSND_RECS80EXT_0_PAUSE_LEN                 (uint16_t)(RECS80EXT_0_PAUSE_TIME + 0.5)
#define IRSND_RECS80EXT_FRAME_REPEAT_PAUSE_LEN      (uint16_t)(RECS80EXT_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_TELEFUNKEN_START_BIT_PULSE_LEN        (uint16_t)(TELEFUNKEN_START_BIT_PULSE_TIME + 0.5)
#define IRSND_TELEFUNKEN_START_BIT_PAUSE_LEN        (uint16_t)(TELEFUNKEN_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_TELEFUNKEN_PULSE_LEN                  (uint16_t)(TELEFUNKEN_PULSE_TIME + 0.5)
#define IRSND_TELEFUNKEN_1_PAUSE_LEN                (uint16_t)(TELEFUNKEN_1_PAUSE_TIME + 0.5)
#define IRSND_TELEFUNKEN_0_PAUSE_LEN                (uint16_t)(TELEFUNKEN_0_PAUSE_TIME + 0.5)
#define IRSND_TELEFUNKEN_AUTO_REPETITION_PAUSE_LEN  (uint16_t)(TELEFUNKEN_AUTO_REPETITION_PAUSE_TIME + 0.5)          // use uint16_t!
#define IRSND_TELEFUNKEN_FRAME_REPEAT_PAUSE_LEN     (uint16_t)(TELEFUNKEN_FRAME_REPEAT_PAUSE_TIME + 0.5)             // use uint16_t!

#define IRSND_BOSE_START_BIT_PULSE_LEN              (uint16_t)(BOSE_START_BIT_PULSE_TIME + 0.5)
#define IRSND_BOSE_START_BIT_PAUSE_LEN              (uint16_t)(BOSE_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_BOSE_PULSE_LEN                        (uint16_t)(BOSE_PULSE_TIME + 0.5)
#define IRSND_BOSE_1_PAUSE_LEN                      (uint16_t)(BOSE_1_PAUSE_TIME + 0.5)
#define IRSND_BOSE_0_PAUSE_LEN                      (uint16_t)(BOSE_0_PAUSE_TIME + 0.5)
#define IRSND_BOSE_AUTO_REPETITION_PAUSE_LEN        (uint16_t)(BOSE_AUTO_REPETITION_PAUSE_TIME + 0.5) // use uint16_t!
#define IRSND_BOSE_FRAME_REPEAT_PAUSE_LEN           (uint16_t)(BOSE_FRAME_REPEAT_PAUSE_TIME + 0.5)    // use uint16_t!

#define IRSND_NUBERT_START_BIT_PULSE_LEN            (uint16_t)(NUBERT_START_BIT_PULSE_TIME + 0.5)
#define IRSND_NUBERT_START_BIT_PAUSE_LEN            (uint16_t)(NUBERT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_NUBERT_1_PULSE_LEN                    (uint16_t)(NUBERT_1_PULSE_TIME + 0.5)
#define IRSND_NUBERT_1_PAUSE_LEN                    (uint16_t)(NUBERT_1_PAUSE_TIME + 0.5)
#define IRSND_NUBERT_0_PULSE_LEN                    (uint16_t)(NUBERT_0_PULSE_TIME + 0.5)
#define IRSND_NUBERT_0_PAUSE_LEN                    (uint16_t)(NUBERT_0_PAUSE_TIME + 0.5)
#define IRSND_NUBERT_AUTO_REPETITION_PAUSE_LEN      (uint16_t)(NUBERT_AUTO_REPETITION_PAUSE_TIME + 0.5) // use uint16_t!
#define IRSND_NUBERT_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(NUBERT_FRAME_REPEAT_PAUSE_TIME + 0.5)    // use uint16_t!

#define IRSND_FAN_START_BIT_PULSE_LEN               (uint16_t)(FAN_START_BIT_PULSE_TIME + 0.5)
#define IRSND_FAN_START_BIT_PAUSE_LEN               (uint16_t)(FAN_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_FAN_1_PULSE_LEN                       (uint16_t)(FAN_1_PULSE_TIME + 0.5)
#define IRSND_FAN_1_PAUSE_LEN                       (uint16_t)(FAN_1_PAUSE_TIME + 0.5)
#define IRSND_FAN_0_PULSE_LEN                       (uint16_t)(FAN_0_PULSE_TIME + 0.5)
#define IRSND_FAN_0_PAUSE_LEN                       (uint16_t)(FAN_0_PAUSE_TIME + 0.5)
#define IRSND_FAN_AUTO_REPETITION_PAUSE_LEN         (uint16_t)(FAN_AUTO_REPETITION_PAUSE_TIME + 0.5) // use uint16_t!
#define IRSND_FAN_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(FAN_FRAME_REPEAT_PAUSE_TIME + 0.5)    // use uint16_t!

#define IRSND_SPEAKER_START_BIT_PULSE_LEN           (uint16_t)(SPEAKER_START_BIT_PULSE_TIME + 0.5)
#define IRSND_SPEAKER_START_BIT_PAUSE_LEN           (uint16_t)(SPEAKER_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_SPEAKER_1_PULSE_LEN                   (uint16_t)(SPEAKER_1_PULSE_TIME + 0.5)
#define IRSND_SPEAKER_1_PAUSE_LEN                   (uint16_t)(SPEAKER_1_PAUSE_TIME + 0.5)
#define IRSND_SPEAKER_0_PULSE_LEN                   (uint16_t)(SPEAKER_0_PULSE_TIME + 0.5)
#define IRSND_SPEAKER_0_PAUSE_LEN                   (uint16_t)(SPEAKER_0_PAUSE_TIME + 0.5)
#define IRSND_SPEAKER_AUTO_REPETITION_PAUSE_LEN     (uint16_t)(SPEAKER_AUTO_REPETITION_PAUSE_TIME + 0.5)             // use uint16_t!
#define IRSND_SPEAKER_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(SPEAKER_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_BANG_OLUFSEN_START_BIT1_PULSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT1_PULSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_START_BIT1_PAUSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT1_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_START_BIT2_PULSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT2_PULSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_START_BIT2_PAUSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT2_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_START_BIT3_PULSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT3_PULSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_START_BIT3_PAUSE_LEN     (uint16_t)(BANG_OLUFSEN_START_BIT3_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_PULSE_LEN                (uint16_t)(BANG_OLUFSEN_PULSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_1_PAUSE_LEN              (uint16_t)(BANG_OLUFSEN_1_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_0_PAUSE_LEN              (uint16_t)(BANG_OLUFSEN_0_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_R_PAUSE_LEN              (uint16_t)(BANG_OLUFSEN_R_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN    (uint16_t)(BANG_OLUFSEN_TRAILER_BIT_PAUSE_TIME + 0.5)
#define IRSND_BANG_OLUFSEN_FRAME_REPEAT_PAUSE_LEN   (uint16_t)(BANG_OLUFSEN_FRAME_REPEAT_PAUSE_TIME + 0.5)           // use uint16_t!

#define IRSND_GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN      (uint16_t)(GRUNDIG_NOKIA_IR60_PRE_PAUSE_TIME + 0.5)
#define IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN            (uint16_t)(GRUNDIG_NOKIA_IR60_BIT_TIME + 0.5)
#define IRSND_GRUNDIG_AUTO_REPETITION_PAUSE_LEN     (uint16_t)(GRUNDIG_AUTO_REPETITION_PAUSE_TIME + 0.5)             // use uint16_t!
#define IRSND_NOKIA_AUTO_REPETITION_PAUSE_LEN       (uint16_t)(NOKIA_AUTO_REPETITION_PAUSE_TIME + 0.5)  // use uint16_t!
#define IRSND_GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_LEN (uint16_t)(GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_IR60_AUTO_REPETITION_PAUSE_LEN        (uint16_t)(IR60_AUTO_REPETITION_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_SIEMENS_START_BIT_LEN                 (uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PULSE_TIME + 0.5)
#define IRSND_SIEMENS_BIT_LEN                       (uint16_t)(SIEMENS_OR_RUWIDO_BIT_PULSE_TIME + 0.5)
#define IRSND_SIEMENS_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(SIEMENS_OR_RUWIDO_FRAME_REPEAT_PAUSE_TIME + 0.5)      // use uint16_t!

#define IRSND_RUWIDO_START_BIT_PULSE_LEN            (uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RUWIDO_START_BIT_PAUSE_LEN            (uint16_t)(SIEMENS_OR_RUWIDO_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RUWIDO_BIT_PULSE_LEN                  (uint16_t)(SIEMENS_OR_RUWIDO_BIT_PULSE_TIME + 0.5)
#define IRSND_RUWIDO_BIT_PAUSE_LEN                  (uint16_t)(SIEMENS_OR_RUWIDO_BIT_PAUSE_TIME + 0.5)
#define IRSND_RUWIDO_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(SIEMENS_OR_RUWIDO_FRAME_REPEAT_PAUSE_TIME + 0.5)      // use uint16_t!

#define IRSND_FDC_START_BIT_PULSE_LEN               (uint16_t)(FDC_START_BIT_PULSE_TIME + 0.5)
#define IRSND_FDC_START_BIT_PAUSE_LEN               (uint16_t)(FDC_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_FDC_PULSE_LEN                         (uint16_t)(FDC_PULSE_TIME + 0.5)
#define IRSND_FDC_1_PAUSE_LEN                       (uint16_t)(FDC_1_PAUSE_TIME + 0.5)
#define IRSND_FDC_0_PAUSE_LEN                       (uint16_t)(FDC_0_PAUSE_TIME + 0.5)
#define IRSND_FDC_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(FDC_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_RCCAR_START_BIT_PULSE_LEN             (uint16_t)(RCCAR_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RCCAR_START_BIT_PAUSE_LEN             (uint16_t)(RCCAR_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RCCAR_PULSE_LEN                       (uint16_t)(RCCAR_PULSE_TIME + 0.5)
#define IRSND_RCCAR_1_PAUSE_LEN                     (uint16_t)(RCCAR_1_PAUSE_TIME + 0.5)
#define IRSND_RCCAR_0_PAUSE_LEN                     (uint16_t)(RCCAR_0_PAUSE_TIME + 0.5)
#define IRSND_RCCAR_FRAME_REPEAT_PAUSE_LEN          (uint16_t)(RCCAR_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_JVC_START_BIT_PULSE_LEN               (uint16_t)(JVC_START_BIT_PULSE_TIME + 0.5)
#define IRSND_JVC_START_BIT_PAUSE_LEN               (uint16_t)(JVC_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_JVC_REPEAT_START_BIT_PAUSE_LEN        (uint16_t)(JVC_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_JVC_PULSE_LEN                         (uint16_t)(JVC_PULSE_TIME + 0.5)
#define IRSND_JVC_1_PAUSE_LEN                       (uint16_t)(JVC_1_PAUSE_TIME + 0.5)
#define IRSND_JVC_0_PAUSE_LEN                       (uint16_t)(JVC_0_PAUSE_TIME + 0.5)
#define IRSND_JVC_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(JVC_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_NIKON_START_BIT_PULSE_LEN             (uint16_t)(NIKON_START_BIT_PULSE_TIME + 0.5)
#define IRSND_NIKON_START_BIT_PAUSE_LEN             (uint16_t)(NIKON_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_NIKON_REPEAT_START_BIT_PAUSE_LEN      (uint16_t)(NIKON_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_NIKON_PULSE_LEN                       (uint16_t)(NIKON_PULSE_TIME + 0.5)
#define IRSND_NIKON_1_PAUSE_LEN                     (uint16_t)(NIKON_1_PAUSE_TIME + 0.5)
#define IRSND_NIKON_0_PAUSE_LEN                     (uint16_t)(NIKON_0_PAUSE_TIME + 0.5)
#define IRSND_NIKON_FRAME_REPEAT_PAUSE_LEN          (uint16_t)(NIKON_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_LEGO_START_BIT_PULSE_LEN              (uint16_t)(LEGO_START_BIT_PULSE_TIME + 0.5)
#define IRSND_LEGO_START_BIT_PAUSE_LEN              (uint16_t)(LEGO_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_LEGO_REPEAT_START_BIT_PAUSE_LEN       (uint16_t)(LEGO_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_LEGO_PULSE_LEN                        (uint16_t)(LEGO_PULSE_TIME + 0.5)
#define IRSND_LEGO_1_PAUSE_LEN                      (uint16_t)(LEGO_1_PAUSE_TIME + 0.5)
#define IRSND_LEGO_0_PAUSE_LEN                      (uint16_t)(LEGO_0_PAUSE_TIME + 0.5)
#define IRSND_LEGO_FRAME_REPEAT_PAUSE_LEN           (uint16_t)(LEGO_FRAME_REPEAT_PAUSE_TIME + 0.5)  // use uint16_t!

#define IRSND_IRMP16_START_BIT_PULSE_LEN            (uint16_t)(IRMP16_START_BIT_PULSE_TIME + 0.5)
#define IRSND_IRMP16_START_BIT_PAUSE_LEN            (uint16_t)(IRMP16_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_IRMP16_REPEAT_START_BIT_PAUSE_LEN     (uint16_t)(IRMP16_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_IRMP16_PULSE_LEN                      (uint16_t)(IRMP16_PULSE_TIME + 0.5)
#define IRSND_IRMP16_1_PAUSE_LEN                    (uint16_t)(IRMP16_1_PAUSE_TIME + 0.5)
#define IRSND_IRMP16_0_PAUSE_LEN                    (uint16_t)(IRMP16_0_PAUSE_TIME + 0.5)
#define IRSND_IRMP16_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(IRMP16_FRAME_REPEAT_PAUSE_TIME + 0.5)  // use uint16_t!

#define IRSND_A1TVBOX_START_BIT_PULSE_LEN           (uint16_t)(A1TVBOX_START_BIT_PULSE_TIME + 0.5)
#define IRSND_A1TVBOX_START_BIT_PAUSE_LEN           (uint16_t)(A1TVBOX_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_A1TVBOX_BIT_PULSE_LEN                 (uint16_t)(A1TVBOX_BIT_PULSE_TIME + 0.5)
#define IRSND_A1TVBOX_BIT_PAUSE_LEN                 (uint16_t)(A1TVBOX_BIT_PAUSE_TIME + 0.5)
#define IRSND_A1TVBOX_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(A1TVBOX_FRAME_REPEAT_PAUSE_TIME + 0.5)            // use uint16_t!
#define IRSND_A1TVBOX_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(A1TVBOX_FRAME_REPEAT_PAUSE_TIME + 0.5)            // use uint16_t!

#define IRSND_ROOMBA_START_BIT_PULSE_LEN            (uint16_t)(ROOMBA_START_BIT_PULSE_TIME + 0.5)
#define IRSND_ROOMBA_START_BIT_PAUSE_LEN            (uint16_t)(ROOMBA_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_ROOMBA_1_PULSE_LEN                    (uint16_t)(ROOMBA_1_PULSE_TIME + 0.5)
#define IRSND_ROOMBA_0_PULSE_LEN                    (uint16_t)(ROOMBA_0_PULSE_TIME + 0.5)
#define IRSND_ROOMBA_1_PAUSE_LEN                    (uint16_t)(ROOMBA_1_PAUSE_TIME + 0.5)
#define IRSND_ROOMBA_0_PAUSE_LEN                    (uint16_t)(ROOMBA_0_PAUSE_TIME + 0.5)
#define IRSND_ROOMBA_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(ROOMBA_FRAME_REPEAT_PAUSE_TIME + 0.5)  // use uint16_t!

#define IRSND_PENTAX_START_BIT_PULSE_LEN            (uint16_t)(PENTAX_START_BIT_PULSE_TIME + 0.5)
#define IRSND_PENTAX_START_BIT_PAUSE_LEN            (uint16_t)(PENTAX_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_PENTAX_REPEAT_START_BIT_PAUSE_LEN     (uint16_t)(PENTAX_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_PENTAX_PULSE_LEN                      (uint16_t)(PENTAX_PULSE_TIME + 0.5)
#define IRSND_PENTAX_1_PAUSE_LEN                    (uint16_t)(PENTAX_1_PAUSE_TIME + 0.5)
#define IRSND_PENTAX_0_PAUSE_LEN                    (uint16_t)(PENTAX_0_PAUSE_TIME + 0.5)
#define IRSND_PENTAX_FRAME_REPEAT_PAUSE_LEN         (uint16_t)(PENTAX_FRAME_REPEAT_PAUSE_TIME + 0.5) // use uint16_t!

#define IRSND_ACP24_START_BIT_PULSE_LEN             (uint16_t)(ACP24_START_BIT_PULSE_TIME + 0.5)
#define IRSND_ACP24_START_BIT_PAUSE_LEN             (uint16_t)(ACP24_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_ACP24_REPEAT_START_BIT_PAUSE_LEN      (uint16_t)(ACP24_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_ACP24_PULSE_LEN                       (uint16_t)(ACP24_PULSE_TIME + 0.5)
#define IRSND_ACP24_1_PAUSE_LEN                     (uint16_t)(ACP24_1_PAUSE_TIME + 0.5)
#define IRSND_ACP24_0_PAUSE_LEN                     (uint16_t)(ACP24_0_PAUSE_TIME + 0.5)
#define IRSND_ACP24_FRAME_REPEAT_PAUSE_LEN          (uint16_t)(ACP24_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

// Add on RCA
#define IRSND_RCA_START_BIT_PULSE_LEN               (uint16_t)(RCA_START_BIT_PULSE_TIME + 0.5)
#define IRSND_RCA_START_BIT_PAUSE_LEN               (uint16_t)(RCA_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RCA_REPEAT_START_BIT_PAUSE_LEN        (uint16_t)(RCA_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_RCA_PULSE_LEN                         (uint16_t)(RCA_PULSE_TIME + 0.5)
#define IRSND_RCA_1_PAUSE_LEN                       (uint16_t)(RCA_1_PAUSE_TIME + 0.5)
#define IRSND_RCA_0_PAUSE_LEN                       (uint16_t)(RCA_0_PAUSE_TIME + 0.5)
#define IRSND_RCA_FRAME_REPEAT_PAUSE_LEN            (uint16_t)(RCA_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

// Add on Pioneer
#define IRSND_PIONEER_START_BIT_PULSE_LEN           (uint16_t)(PIONEER_START_BIT_PULSE_TIME + 0.5)
#define IRSND_PIONEER_START_BIT_PAUSE_LEN           (uint16_t)(PIONEER_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_PIONEER_REPEAT_START_BIT_PAUSE_LEN    (uint16_t)(PIONEER_REPEAT_START_BIT_PAUSE_TIME + 0.5)
#define IRSND_PIONEER_PULSE_LEN                     (uint16_t)(PIONEER_PULSE_TIME + 0.5)
#define IRSND_PIONEER_1_PAUSE_LEN                   (uint16_t)(PIONEER_1_PAUSE_TIME + 0.5)
#define IRSND_PIONEER_0_PAUSE_LEN                   (uint16_t)(PIONEER_0_PAUSE_TIME + 0.5)
#define IRSND_PIONEER_FRAME_REPEAT_PAUSE_LEN        (uint16_t)(PIONEER_FRAME_REPEAT_PAUSE_TIME + 0.5)   // use uint16_t!

#define IRSND_FREQ_TYPE                       uint32_t
#define IRSND_FREQ_30_KHZ                     (IRSND_FREQ_TYPE) (30000)
#define IRSND_FREQ_32_KHZ                     (IRSND_FREQ_TYPE) (32000)
#define IRSND_FREQ_36_KHZ                     (IRSND_FREQ_TYPE) (36000)
#define IRSND_FREQ_38_KHZ                     (IRSND_FREQ_TYPE) (38000)
#define IRSND_FREQ_40_KHZ                     (IRSND_FREQ_TYPE) (40000)
#define IRSND_FREQ_56_KHZ                     (IRSND_FREQ_TYPE) (56000)
#define IRSND_FREQ_455_KHZ                    (IRSND_FREQ_TYPE) (455000)

#define IR_TX_OTA_BITS_FRAME_LEN_MAX			100 // bits

static uint8_t      ir_tx_active;
static uint8_t      ir_has_stop_bit;
static uint8_t      ir_has_start_bit;
static uint8_t      ir_frame_current_bit;
static uint16_t     ir_bit_pulse_time;
static uint16_t     ir_bit_space_time;
static uint16_t     startbit_pulse_time;
static uint16_t     startbit_space_time;
static uint16_t     pulse_1_len;
static uint16_t     space_1_len;
static uint16_t     pulse_0_len;
static uint16_t     space_0_len;
static uint8_t      complete_data_len;
static uint8_t      n_repeat_frames;
static uint8_t      n_auto_repetitions;
static uint32_t     auto_repetition_pause_time;
static uint32_t     repeat_frame_pause_time;
static uint32_t     ir_frame_pause_time;
static uint8_t      auto_repetition_counter;
static uint8_t      repeat_counter;
static uint8_t		packet_repeat_pause_passed;
#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
static uint8_t      last_bit_value;
#endif

static uint8_t ir_tx_protocol;
static uint8_t ir_tx_duty_cycle;
static uint32_t ir_tx_frequency;
static uint8_t ir_tx_n_repeat;
static uint8_t ir_tx_buffer[11] = {0};

static uint8_t ir_ota_buffer_ok;
static uint16_t ir_ota_frame_len;
static uint16_t *ir_ota_pkt_buffer = NULL;
static uint32_t ir_carrier_pwm_channel;
static IRSND_FREQ_TYPE prev_pwm_freq = 0;

static TIM_HandleTypeDef *pir_timhdl_carrier;

#if IRSND_USE_CALLBACK==1
static void (*irsnd_callback_ptr) (uint8_t);
#endif // IRSND_USE_CALLBACK==1

//************************** S T R U C T U R E S *******************************

typedef enum
{
	OTA_MANCHESTER_CODING_IEEE = 0,// Logic 1 = L H, logic 0 = H L
	OTA_MANCHESTER_CODING_THOMAS // Logic 0 = L H, logic 1 = H L
} S_M1_OTA_Manchester_Coding_Type_t;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t irsnd_generate_tx_data(IRMP_DATA irmp_data);
uint8_t m1_make_ir_ota_multiframes(void);
void m1_set_ir_ota_bit_times(void);
uint8_t m1_make_ir_ota_frame(void);
uint8_t m1_ir_ota_frame_repeat_handler(uint8_t ir_protocol);
static uint8_t m1_ir_ota_manchester_coding(uint8_t prev_data_index, uint8_t prev_bit, uint8_t cur_bit, uint16_t bit_time, uint8_t manchester_code);
void irsnd_init(TIM_HandleTypeDef *pTimHdl_Carrier, uint32_t tim_pwm_channel);
static void irsnd_pwm_init(IRSND_FREQ_TYPE freq);
void irsnd_pwm_deinit(void);
void irsnd_on(void);
void irsnd_off(void);
void irsnd_toggle(uint8_t pulse_toggle);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
 * This function returns the pointer to the allocated ota buffer.
*/
/*============================================================================*/
uint16_t *m1_get_ir_ota_buffer_ptr(void)
{
	return ir_ota_pkt_buffer;
} // uint16_t *m1_get_ir_ota_buffer_ptr(void)



/*============================================================================*/
/*
 * This function returns the length of the ir frame
*/
/*============================================================================*/
uint16_t m1_get_ir_ota_frame_len(void)
{
	return ir_ota_frame_len;
} // uint16_t m1_get_ir_ota_frame_len(void)



/*============================================================================*/
/*
 * This function returns the status of memory allocation for the ir ota buffer
*/
/*============================================================================*/
uint8_t m1_check_ir_ota_frame_status(void)
{
	return ir_ota_buffer_ok;
} // uint8_t m1_check_ir_ota_frame_status(void)



/*============================================================================*/
/*
 * This function initializes the PWM
*/
/*============================================================================*/
static void irsnd_pwm_init(IRSND_FREQ_TYPE freq)
{
	TIM_OC_InitTypeDef sConfigOC;

	if ( prev_pwm_freq==freq ) // Same frequency?
	{
		/* Enable the Main Output */
		__HAL_TIM_MOE_ENABLE(pir_timhdl_carrier);
		/* Enable the Peripheral, except in trigger mode where enable is automatically done with trigger */
		__HAL_TIM_ENABLE(pir_timhdl_carrier);
		return; // Do nothing
	} // if ( prev_pwm_freq==freq )
	prev_pwm_freq = freq; // Update new frequency

	uint32_t uwPrescalerValue = (uint32_t) (HAL_RCC_GetPCLK2Freq() / (freq*IR_ENCODE_CARRIER_PRESCALE_FACTOR) - 1);

	pir_timhdl_carrier->Init.Prescaler = uwPrescalerValue;
	pir_timhdl_carrier->Init.CounterMode = TIM_COUNTERMODE_UP;
	pir_timhdl_carrier->Init.Period = IR_ENCODE_CARRIER_PRESCALE_FACTOR - 1;
	pir_timhdl_carrier->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	pir_timhdl_carrier->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_PWM_Init(pir_timhdl_carrier) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	if ( (ir_tx_duty_cycle<10) || (ir_tx_duty_cycle>100) ) // Valid range: 10(%) - 100(%)
		ir_tx_duty_cycle = 33; // Default = 33%
	ir_tx_duty_cycle = 1000/ir_tx_duty_cycle; // Values ranging from 100(10%) to 10(100%)
	sConfigOC.Pulse = (pir_timhdl_carrier->Init.Period*10)/ir_tx_duty_cycle;

	sConfigOC.OCNPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
	if (HAL_TIM_PWM_ConfigChannel(pir_timhdl_carrier, &sConfigOC, ir_carrier_pwm_channel) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	pir_timhdl_carrier->Instance->CCER = 0; // Reset unexpected CCxE, CCxP, CCxNE, CCxNP settings

	/* Enable the Main Output */
	__HAL_TIM_MOE_ENABLE(pir_timhdl_carrier);

	/* Enable the Peripheral, except in trigger mode where enable is automatically done with trigger */
	__HAL_TIM_ENABLE(pir_timhdl_carrier);

} // static void irsnd_pwm_init(IRSND_FREQ_TYPE freq)



/*============================================================================*/
/*
 * This function deinitializes the PWM
*/
/*============================================================================*/
void irsnd_pwm_deinit(void)
{
	prev_pwm_freq = 0;

	irsnd_off();

	if (HAL_TIM_PWM_DeInit(pir_timhdl_carrier) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}
} // void irsnd_pwm_deinit(void)



/*============================================================================*/
/*
 * Toggle PWM on for Carrier
 */
/*============================================================================*/
void irsnd_toggle(uint8_t pulse_toggle)
{
	uint32_t tmp;

	pulse_toggle <<= 2; // 00000x00
	pulse_toggle &= TIM_CCxN_ENABLE; // 00000100

	/* Disable the complementary PWM output  */
	tmp = TIM_CCER_CC1NE << (ir_carrier_pwm_channel & 0xFU); /* 0xFU = 15 bits max shift */
	/* Reset the CCxNE Bit */
	pir_timhdl_carrier->Instance->CCER &= ~tmp;
	/* Enable/Disable the complementary PWM output  */
	/* Set/Reset the CCxNE Bit */
	pir_timhdl_carrier->Instance->CCER |= (uint32_t)(pulse_toggle << (ir_carrier_pwm_channel & 0xFU)); /* 0xFU = 15 bits max shift */
/*
	if ( pulse_toggle )
	{
		// Enable the Main Output
		__HAL_TIM_MOE_ENABLE(pir_timhdl_carrier);
		// Enable the Peripheral, except in trigger mode where enable is automatically done with trigger
		__HAL_TIM_ENABLE(pir_timhdl_carrier);
	}
	else
	{
		// Disable the Main Output
		__HAL_TIM_MOE_DISABLE(pir_timhdl_carrier);
		// Disable the Peripheral
		__HAL_TIM_DISABLE(pir_timhdl_carrier);
	}
*/
} // void irsnd_toggle(uint8_t pulse_toggle)



/*============================================================================*/
/*
 * Switch PWM on for Carrier
 */
/*============================================================================*/
void irsnd_on(void)
{
	//HAL_TIM_PWM_Start(pir_timhdl_carrier, ir_carrier_pwm_channel);
	//HAL_TIMEx_PWMN_Start(pir_timhdl_carrier, ir_carrier_pwm_channel);

	/* Enable the complementary PWM output  */
	/* Set the CCxNE Bit */
	pir_timhdl_carrier->Instance->CCER |= (uint32_t)(TIM_CCxN_ENABLE << (ir_carrier_pwm_channel & 0xFU)); /* 0xFU = 15 bits max shift */
} // static void irsnd_on (void)



/*============================================================================*/
/*
 * Switch PWM off for Carrier
 */
/*============================================================================*/
void irsnd_off(void)
{
	uint32_t tmp;

	//HAL_TIM_PWM_Stop(pir_timhdl_carrier, ir_carrier_pwm_channel);
	//HAL_TIMEx_PWMN_Stop(pir_timhdl_carrier, ir_carrier_pwm_channel);

	/* Disable the complementary PWM output  */
	tmp = TIM_CCER_CC1NE << (ir_carrier_pwm_channel & 0xFU); /* 0xFU = 15 bits max shift */
	/* Reset the CCxNE Bit */
	pir_timhdl_carrier->Instance->CCER &= ~tmp;

	/* Disable the Main Output */
	__HAL_TIM_MOE_DISABLE(pir_timhdl_carrier);

	/* Disable the Peripheral */
	__HAL_TIM_DISABLE(pir_timhdl_carrier);

} // static void irsnd_off (void)



/*============================================================================*/
/*
 *  Initialize the PWM
 *  @details  Configure PWM channels
 */
/*============================================================================*/
void irsnd_init(TIM_HandleTypeDef *pTimHdl_Carrier, uint32_t tim_pwm_channel)
{
	pir_timhdl_carrier = pTimHdl_Carrier;
	ir_carrier_pwm_channel = tim_pwm_channel;

	ir_tx_active = FALSE;
	ir_has_stop_bit = 0;
	ir_has_start_bit = TRUE;
	ir_frame_pause_time = 0;
	ir_frame_current_bit = 0;
	ir_bit_pulse_time = 0xFFFF;
	ir_bit_space_time = 0xFFFF;
	startbit_pulse_time = 0;
	startbit_space_time = 0;
	pulse_1_len = 0;
	space_1_len = 0;
	pulse_0_len = 0;
	space_0_len = 0;
	complete_data_len = 0;
	n_repeat_frames = 0;  // number of repetition frames
	n_auto_repetitions = 0;  // number of auto_repetitions
	auto_repetition_pause_time = 0;  // pause before auto_repetition, uint16_t!
	repeat_frame_pause_time = 0;  // pause before repeat, uint16_t!
	auto_repetition_counter = 0;  // auto_repetition counter
	repeat_counter = 0;  // repeat counter
	packet_repeat_pause_passed = 0;  // pause before repeat

	ir_tx_protocol = 0;
	ir_tx_n_repeat = 0;
	ir_ota_buffer_ok = FALSE;
	ir_ota_frame_len = 0;

	//irsnd_pwm_init (IRSND_FREQ_36_KHZ);   // default frequency
} // void irsnd_init(TIM_HandleTypeDef *pTimHdl_Carrier, uint32_t tim_pwm_channel)



/*============================================================================*/
/*
 *  Return the status of the Ir transmitter
 */
/*============================================================================*/
uint8_t irsnd_is_busy (void)
{
    return ir_tx_active;
}



/*============================================================================*/
/*
 *  Reverse bits order, MSB <--> LSB
 */
/*============================================================================*/
static uint16_t bitsreverse (uint16_t x, uint8_t len)
{
    uint16_t ret = 0;

    while(len--)
    {
    	ret <<= 1;
    	if (x & 1)
        {
            ret |= 1;
        }
        x >>= 1;
    } // while(len--)
    return ret;
} // static uint16_t bitsreverse (uint16_t x, uint8_t len)



#if IRSND_SUPPORT_SIRCS_PROTOCOL==1
static uint8_t  sircs_additional_bitlen;
#endif // IRSND_SUPPORT_SIRCS_PROTOCOL==1


/*============================================================================*/
/*
 *  Generate tx data based on given protocol and place data in the tx buffer
 */
/*============================================================================*/
uint8_t irsnd_generate_tx_data(IRMP_DATA irmp_data)
{
#if IRSND_SUPPORT_RECS80_PROTOCOL==1
    static uint8_t  toggle_bit_recs80;
#endif
#if IRSND_SUPPORT_RECS80EXT_PROTOCOL==1
    static uint8_t  toggle_bit_recs80ext;
#endif
#if IRSND_SUPPORT_RC5_PROTOCOL==1
    static uint8_t  toggle_bit_rc5 = 0;
#endif
#if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1
    static uint8_t  toggle_bit_rc6 = 0;
#endif
#if IRSND_SUPPORT_THOMSON_PROTOCOL==1
    static uint8_t  toggle_bit_thomson;
#endif
    uint16_t        address;
    uint16_t        command;

    if (ir_tx_active)
    {
        return (FALSE);
    }

    ir_tx_protocol  = irmp_data.protocol;
    ir_tx_duty_cycle = irmp_data.duty_cycle;
    ir_tx_frequency = irmp_data.frequency;
    ir_tx_n_repeat    = irmp_data.flags & IRSND_REPETITION_MASK;

    switch (ir_tx_protocol)
    {
#if IRSND_SUPPORT_SIRCS_PROTOCOL==1
        case IRMP_SIRCS_PROTOCOL:
/*
12-bit version, 7 command bits, 5 address bits.
15-bit version, 7 command bits, 8 address bits.
20-bit version, 7 command bits, 5 address bits, 8 extended bits.
*/
        	sircs_additional_bitlen = 0;
            command = bitsreverse(irmp_data.command, 7);
            address = bitsreverse (irmp_data.address, 5);
            ir_tx_buffer[0] = (command & 0x7F) << 1; // CCCCCCC0
            ir_tx_buffer[0] |= (address & 0x1F) >> 4; // CCCCCCCA
            ir_tx_buffer[1] = (address & 0x000F) << 4; // AAAA0000
            ir_tx_active = TRUE;
            break;

        case IRMP_SIRCS15_PROTOCOL: // Add on
        	sircs_additional_bitlen = 3;
            command = bitsreverse(irmp_data.command, 7);
            address = bitsreverse (irmp_data.address, 8);
            ir_tx_buffer[0] = (command & 0x7F) << 1; // CCCCCCC0
            ir_tx_buffer[0] |= (address & 0xFF) >> 7; // CCCCCCCA
            ir_tx_buffer[1] = (address & 0xFF) << 1; // AAAAAAA0
            ir_tx_active = TRUE;
        	break;

        case IRMP_SIRCS20_PROTOCOL: // Add on
        	sircs_additional_bitlen = 8;
            command = bitsreverse(irmp_data.command, 7);
            address = bitsreverse (irmp_data.address, 13);
            ir_tx_buffer[0] = (command & 0x7F) << 1; // CCCCCCC0
            ir_tx_buffer[0] |= (address & 0x1FFF) >> 12; // CCCCCCCA
            ir_tx_buffer[1] = (address & 0x0FFF) >> 4; // AAAAAAAA
            ir_tx_buffer[2] = (address & 0x000F) << 4; // AAAA0000
            ir_tx_active = TRUE;
        	break;
#endif // #if IRSND_SUPPORT_SIRCS_PROTOCOL==1
#if IRSND_SUPPORT_NEC_PROTOCOL==1
        case IRMP_APPLE_PROTOCOL:
            command = irmp_data.command | (irmp_data.address << 8); // store address as ID in upper byte of command
            address = 0x87EE;         // set fixed NEC-lookalike address (customer ID of apple)

            address = bitsreverse (address, NEC_ADDRESS_LEN);
            command = bitsreverse (command, NEC_COMMAND_LEN);

            ir_tx_protocol = IRMP_NEC_PROTOCOL;        // APPLE protocol is NEC with id instead of inverted command

            ir_tx_buffer[0] = (address & 0xFF00) >> 8;         // AAAAAAAA
            ir_tx_buffer[1] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[2] = (command & 0xFF00) >> 8;         // CCCCCCCC
            ir_tx_buffer[3] = (command & 0x00FF); // cccccccc (ID)
            ir_tx_active = TRUE;
            break;

        case IRMP_NEC8_PROTOCOL: // Add on
        case IRMP_PIONEER_PROTOCOL: // Add on
            if (irmp_data.flags & IRSND_RAW_REPETITION_FRAME)
            {
                ir_tx_protocol = IRMP_NEC_REPETITION_PROTOCOL;    // send a raw repetition frame
                ir_tx_buffer[0] = 0x00;   // no address, no command
            }
            else
            {
                address = bitsreverse(irmp_data.address, NEC16_ADDRESS_LEN);
                command = bitsreverse(irmp_data.command, NEC16_COMMAND_LEN);

                ir_tx_buffer[0] = address & 0xFF; // AAAAAAAA
                ir_tx_buffer[1] = ~address ;  // aaaaaaaa
                ir_tx_buffer[2] = command; // CCCCCCCC
                ir_tx_buffer[3] = ~command;     // cccccccc
            }
            ir_tx_active = TRUE;
            break;

        case IRMP_NEC_PROTOCOL:
            if (irmp_data.flags & IRSND_RAW_REPETITION_FRAME)
            {
                ir_tx_protocol = IRMP_NEC_REPETITION_PROTOCOL;    // send a raw repetition frame
                ir_tx_buffer[0] = 0x00;   // no address, no command
            }
            else
            {
                address = bitsreverse (irmp_data.address, NEC_ADDRESS_LEN);
                command = bitsreverse (irmp_data.command, NEC_COMMAND_LEN);

                ir_tx_buffer[0] = (address & 0xFF00) >> 8; // AAAAAAAA(H)
                ir_tx_buffer[1] = (address & 0x00FF);  // AAAAAAAA(L)
                ir_tx_buffer[2] = (command & 0xFF00) >> 8; // CCCCCCCC(H)
                ir_tx_buffer[3] = ~((command & 0xFF00) >> 8);     // cccccccc
            }
            ir_tx_active = TRUE;
            break;

        case IRMP_ONKYO_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, NEC_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, NEC_COMMAND_LEN);

            ir_tx_buffer[0] = (address & 0xFF00) >> 8;         // AAAAAAAA
            ir_tx_buffer[1] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[2] = (command & 0xFF00) >> 8;         // CCCCCCCC
            ir_tx_buffer[3] = (command & 0x00FF); // CCCCCCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_NEC16_PROTOCOL==1
        case IRMP_NEC16_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, NEC16_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, NEC16_COMMAND_LEN);

            ir_tx_buffer[0] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[1] = (command & 0x00FF); // CCCCCCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_NEC42_PROTOCOL==1
        case IRMP_NEC42_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, NEC42_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, NEC42_COMMAND_LEN);

            ir_tx_buffer[0] = ( (address & 0x1FE0) >> 5);      // AAAAAAAA
            ir_tx_buffer[1] = ( (address & 0x001F) << 3) | ((~address & 0x1C00) >> 10);        // AAAAAaaa
            ir_tx_buffer[2] =                              ((~address & 0x03FC) >> 2);         // aaaaaaaa
            ir_tx_buffer[3] = ((~address & 0x0003) << 6) | ( (command & 0x00FC) >> 2);         // aaCCCCCC
            ir_tx_buffer[4] = ( (command & 0x0003) << 6) | ((~command & 0x00FC) >> 2);         // CCcccccc
            ir_tx_buffer[5] = ((~command & 0x0003) << 6);      // cc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_MELINERA_PROTOCOL==1
        case IRMP_MELINERA_PROTOCOL:
        {
            command = irmp_data.command;

            ir_tx_buffer[0] = (command & 0x00FF); // CCCCCCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_LGAIR_PROTOCOL==1
        case IRMP_LGAIR_PROTOCOL:
        {
            address = irmp_data.address;
            command = irmp_data.command;

            ir_tx_buffer[0] = ( (address & 0x00FF));           // AAAAAAAA
            ir_tx_buffer[1] = ( (command & 0xFF00) >> 8);      // CCCCCCCC
            ir_tx_buffer[2] = ( (command & 0x00FF));           // CCCCCCCC
            ir_tx_buffer[3] = (( ((command & 0xF000) >> 12) +  // checksum
                                 ((command & 0x0F00) >> 8) +
                                 ((command & 0x00F0) >>4 ) +
                                 ((command & 0x000F))) & 0x000F) << 4;
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1
        case IRMP_SAMSUNG_PROTOCOL:
            address = bitsreverse (irmp_data.address, GENERAL_PROTOCOL_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, GENERAL_PROTOCOL_COMMAND_LEN);

            ir_tx_buffer[0] =  (address & 0x00FF);        // AAAAAAAA
            ir_tx_buffer[1] =  (address & 0x00FF);        // AAAAAAAA
            ir_tx_buffer[2] =  (command & 0x00F0) | ((command & 0x00F0) >> 4);   // IIIICCCC
            ir_tx_buffer[3] = ((command & 0x00F0) | (~(command & 0x00F0) >> 4) );    // CCCCcccc
            ir_tx_buffer[4] = (~(command & 0x00F0));   // cccc0000
            ir_tx_active = TRUE;
            break;

        case IRMP_SAMSUNG32_PROTOCOL:
        	if ( irmp_data.flags & IRSND_RAW_REPETITION_FRAME) // Add on
            {
            	ir_tx_protocol = IRMP_SAMSUNG32_REPETITION_PROTOCOL;
            } // if ( irmp_data.flags & IRSND_RAW_REPETITION_FRAME)
            else
            {
                address = bitsreverse (irmp_data.address, GENERAL_PROTOCOL_ADDRESS_LEN);
                command = bitsreverse (irmp_data.command, GENERAL_PROTOCOL_COMMAND_LEN);

                ir_tx_buffer[0] = (address & 0x00FF);   // AAAAAAAA
                ir_tx_buffer[1] = (address & 0x00FF); // AAAAAAAA
                ir_tx_buffer[2] = (command & 0x00FF); // CCCCCCCC
                ir_tx_buffer[3] = ~(command & 0x00FF); // cccccccc
            }
            ir_tx_active = TRUE;
            break;
#endif
#if IRSND_SUPPORT_SAMSUNG48_PROTOCOL==1
        case IRMP_SAMSUNG48_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, SAMSUNG_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, 16);

            ir_tx_buffer[0] = (address & 0xFF00) >> 8;         // AAAAAAAA
            ir_tx_buffer[1] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[2] = ((command & 0xFF00) >> 8);       // CCCCCCCC
            ir_tx_buffer[3] = ~((command & 0xFF00) >> 8);      // cccccccc
            ir_tx_buffer[4] = (command & 0x00FF); // CCCCCCCC
            ir_tx_buffer[5] = ~(command & 0x00FF);             // cccccccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
        case IRMP_MATSUSHITA_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, MATSUSHITA_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, MATSUSHITA_COMMAND_LEN);

            ir_tx_buffer[0] = (command & 0x0FF0) >> 4;         // CCCCCCCC
            ir_tx_buffer[1] = ((command & 0x000F) << 4) | ((address & 0x0F00) >> 8);           // CCCCAAAA
            ir_tx_buffer[2] = (address & 0x00FF); // AAAAAAAA
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_TECHNICS_PROTOCOL==1
        case IRMP_TECHNICS_PROTOCOL:
        {
            command = bitsreverse (irmp_data.command, TECHNICS_COMMAND_LEN);

            ir_tx_buffer[0] = (command & 0x07FC) >> 3;         // CCCCCCCC
            ir_tx_buffer[1] = ((command & 0x0007) << 5) | ((~command & 0x07C0) >> 6);          // CCCccccc
            ir_tx_buffer[2] = (~command & 0x003F) << 2;        // cccccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_KASEIKYO_PROTOCOL==1
        case IRMP_KASEIKYO_PROTOCOL:
        {
            uint8_t xor_value;
            uint16_t genre2;

            address = bitsreverse (irmp_data.address, KASEIKYO_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, KASEIKYO_COMMAND_LEN + 4);
            genre2 = bitsreverse ((irmp_data.flags & ~IRSND_REPETITION_MASK) >> 4, 4);

            xor_value = ((address & 0x000F) ^ ((address & 0x00F0) >> 4) ^ ((address & 0x0F00) >> 8) ^ ((address & 0xF000) >> 12)) & 0x0F;

            ir_tx_buffer[0] = (address & 0xFF00) >> 8;         // AAAAAAAA
            ir_tx_buffer[1] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[2] = xor_value << 4 | (command & 0x000F); // XXXXCCCC
            ir_tx_buffer[3] = (genre2 << 4) | (command & 0xF000) >> 12;   // ggggCCCC
            ir_tx_buffer[4] = (command & 0x0FF0) >> 4;         // CCCCCCCC

            xor_value = ir_tx_buffer[2] ^ ir_tx_buffer[3] ^ ir_tx_buffer[4];

            ir_tx_buffer[5] = xor_value;
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_PANASONIC_PROTOCOL==1
        case IRMP_PANASONIC_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, PANASONIC_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, PANASONIC_COMMAND_LEN);

            ir_tx_buffer[0] = 0x40;           // 01000000
            ir_tx_buffer[1] = 0x04;           // 00000100
            ir_tx_buffer[2] = 0x01;           // 00000001
            ir_tx_buffer[3] = (address & 0xFF00) >> 8;         // AAAAAAAA
            ir_tx_buffer[4] = (address & 0x00FF); // AAAAAAAA
            ir_tx_buffer[5] = (command & 0xFF00) >> 8;         // CCCCCCCC
            ir_tx_buffer[6] = (command & 0x00FF); // CCCCCCCC

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL==1
        case IRMP_MITSU_HEAVY_PROTOCOL:
        {
            address = irmp_data.address;
            command = irmp_data.command;

            ir_tx_buffer[0] = 0x4A;
            ir_tx_buffer[1] = 0x75;
            ir_tx_buffer[2] = 0xC3;
            ir_tx_buffer[3] = 0x64;
            ir_tx_buffer[4] = 0x9B;
            ir_tx_buffer[5] = ~(address & 0xFF00) >> 8;
            ir_tx_buffer[6] = (address & 0xFF00) >> 8;
            ir_tx_buffer[7] = ~(address & 0x00FF);
            ir_tx_buffer[8] = (address & 0x00FF);
            ir_tx_buffer[9] = ~(command & 0x00FF);
            ir_tx_buffer[10] = (command & 0x00FF);

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RECS80_PROTOCOL==1
        case IRMP_RECS80_PROTOCOL:
        {
            toggle_bit_recs80 = toggle_bit_recs80 ? 0x00 : 0x80;

            ir_tx_buffer[0] = toggle_bit_recs80 | ((irmp_data.address & 0x000F) << 4) |
                              ((irmp_data.command & 0x003C) >> 2);     // TAAACCCC
            ir_tx_buffer[1] = (irmp_data.command & 0x03) << 6;  // CC000000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RECS80EXT_PROTOCOL==1
        case IRMP_RECS80EXT_PROTOCOL:
        {
            toggle_bit_recs80ext = toggle_bit_recs80ext ? 0x00 : 0x40;

            ir_tx_buffer[0] = 0x80 | toggle_bit_recs80ext | ((irmp_data.address & 0x000F) << 2) |
                                ((irmp_data.command & 0x0030) >> 4);   // STAAAACC
            ir_tx_buffer[1] = (irmp_data.command & 0x0F) << 4;  // CCCC0000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RC5_PROTOCOL==1
        case IRMP_RC5_PROTOCOL:
        case IRMP_RC5X_PROTOCOL:
        {
        	if ( !repeat_counter ) // Toggle bit will retain the same logical level during repeated messages
        		toggle_bit_rc5 ^= 0x40;

        	ir_tx_buffer[0] = 0x80 | toggle_bit_rc5 | ((irmp_data.address & 0x001F) << 1) |  // STAAAAAC
            										((irmp_data.command & 0x20) >> 5);
        	if ( ir_tx_protocol==IRMP_RC5X_PROTOCOL )
        	{
        		ir_tx_buffer[0] &= 0x7F; // Clear S bit
        	} // if ( ir_tx_protocol==IRMP_RC5X_PROTOCOL )
            ir_tx_buffer[1] = (irmp_data.command & 0x1F) << 3;  // CCCCC000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RC6_PROTOCOL==1
        case IRMP_RC6_PROTOCOL:
        {
            toggle_bit_rc6 ^= 0x08;

            ir_tx_buffer[0] = 0x80 | toggle_bit_rc6 | ((irmp_data.address & 0x00E0) >> 5);  // 1MMMTAAA, MMM = 000
            ir_tx_buffer[1] = ((irmp_data.address & 0x001F) << 3) | ((irmp_data.command & 0xE0) >> 5);    // AAAAACCC
            ir_tx_buffer[2] = (irmp_data.command & 0x1F) << 3;  // CCCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RC6A_PROTOCOL==1
        case IRMP_RC6A_PROTOCOL:
        {
            toggle_bit_rc6 = toggle_bit_rc6 ? 0x00 : 0x08;

            ir_tx_buffer[0] = 0x80 | 0x60 | ((irmp_data.address & 0x3000) >> 12);   // 1MMMT0AA, MMM = 110
            ir_tx_buffer[1] = ((irmp_data.address & 0x0FFF) >> 4) ;             // AAAAAAAA
            ir_tx_buffer[2] = ((irmp_data.address & 0x000F) << 4) | ((irmp_data.command & 0xF000) >> 12) | toggle_bit_rc6;    // AAAACCCC
            ir_tx_buffer[3] = (irmp_data.command & 0x0FF0) >> 4;   // CCCCCCCC
            ir_tx_buffer[4] = (irmp_data.command & 0x000F) << 4;   // CCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_DENON_PROTOCOL==1
        case IRMP_DENON_PROTOCOL:
        {
            ir_tx_buffer[0] = ((irmp_data.address & 0x1F) << 3) | ((irmp_data.command & 0x0380) >> 7);    // AAAAACCC (1st frame)
            ir_tx_buffer[1] = (irmp_data.command & 0x7F) << 1;  // CCCCCCC
            ir_tx_buffer[2] = ((irmp_data.address & 0x1F) << 3) | (((~irmp_data.command) & 0x0380) >> 7); // AAAAAccc (2nd frame)
            ir_tx_buffer[3] = (~(irmp_data.command) & 0x7F) << 1;      // ccccccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_THOMSON_PROTOCOL==1
        case IRMP_THOMSON_PROTOCOL:
        {
            toggle_bit_thomson = toggle_bit_thomson ? 0x00 : 0x08;

            ir_tx_buffer[0] = ((irmp_data.address & 0x0F) << 4) | toggle_bit_thomson | ((irmp_data.command & 0x0070) >> 4);   // AAAATCCC (1st frame)
            ir_tx_buffer[1] = (irmp_data.command & 0x0F) << 4; // CCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_BOSE_PROTOCOL==1
        case IRMP_BOSE_PROTOCOL:
        {
            command = bitsreverse (irmp_data.command, BOSE_COMMAND_LEN);

            ir_tx_buffer[0] = (command & 0xFF00) >> 8;    // CCCCCCCC
            ir_tx_buffer[1] = ~((command & 0xFF00) >> 8); // cccccccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_NUBERT_PROTOCOL==1
        case IRMP_NUBERT_PROTOCOL:
        {
            ir_tx_buffer[0] = irmp_data.command >> 2;       // CCCCCCCC
            ir_tx_buffer[1] = (irmp_data.command & 0x0003) << 6; // CC000000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_FAN_PROTOCOL==1
        case IRMP_FAN_PROTOCOL:
        {
            ir_tx_buffer[0] = irmp_data.command >> 3;       // CCCCCCCC
            ir_tx_buffer[1] = (irmp_data.command & 0x0007) << 5; // CCC00000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_SPEAKER_PROTOCOL==1
        case IRMP_SPEAKER_PROTOCOL:
        {
            ir_tx_buffer[0] = irmp_data.command >> 2;       // CCCCCCCC
            ir_tx_buffer[1] = (irmp_data.command & 0x0003) << 6; // CC000000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
        case IRMP_BANG_OLUFSEN_PROTOCOL:
        {
            ir_tx_buffer[0] = irmp_data.command >> 11;      // SXSCCCCC
            ir_tx_buffer[1] = irmp_data.command >> 3;       // CCCCCCCC
            ir_tx_buffer[2] = (irmp_data.command & 0x0007) << 5; // CCC00000
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1
        case IRMP_GRUNDIG_PROTOCOL:
        {
            command = bitsreverse (irmp_data.command, GRUNDIG_COMMAND_LEN);

            ir_tx_buffer[0] = 0xFF;           // S1111111 (1st frame)
            ir_tx_buffer[1] = 0xC0;           // 11
            ir_tx_buffer[2] = 0x80 | (command >> 2);           // SCCCCCCC (2nd frame)
            ir_tx_buffer[3] = (command << 6) & 0xC0;           // CC

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_TELEFUNKEN_PROTOCOL==1
        case IRMP_TELEFUNKEN_PROTOCOL:
        {
            ir_tx_buffer[0] = irmp_data.command >> 7;       // CCCCCCCC
            ir_tx_buffer[1] = (irmp_data.command << 1) & 0xff;  // CCCCCCC

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_IR60_PROTOCOL==1
        case IRMP_IR60_PROTOCOL:
        {
            command = (bitsreverse (0x7d, IR60_COMMAND_LEN) << 7) | bitsreverse (irmp_data.command, IR60_COMMAND_LEN);
#if 0
            ir_tx_buffer[0] = command >> 6 | 0x01;             // 1011111S (start instruction frame)
            ir_tx_buffer[1] = (command & 0x7F) << 1;           // CCCCCCC_ (2nd frame)
#else
            ir_tx_buffer[0] = ((command & 0x7F) << 1) | 0x01;  // CCCCCCCS (1st frame)
            ir_tx_buffer[1] = command >> 6;   // 1011111_ (start instruction frame)
#endif

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_NOKIA_PROTOCOL==1
        case IRMP_NOKIA_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, NOKIA_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, NOKIA_COMMAND_LEN);

            ir_tx_buffer[0] = 0xBF;           // S0111111 (1st + 3rd frame)
            ir_tx_buffer[1] = 0xFF;           // 11111111
            ir_tx_buffer[2] = 0x80;           // 1
            ir_tx_buffer[3] = 0x80 | command >> 1;             // SCCCCCCC (2nd frame)
            ir_tx_buffer[4] = (command << 7) | (address >> 1); // CAAAAAAA
            ir_tx_buffer[5] = (address << 7); // A

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_SIEMENS_PROTOCOL==1
        case IRMP_SIEMENS_PROTOCOL:
        {
            ir_tx_buffer[0] = ((irmp_data.address & 0x07FF) >> 3);     // AAAAAAAA
            ir_tx_buffer[1] = ((irmp_data.address & 0x0007) << 5) | ((irmp_data.command >> 5) & 0x1F);    // AAACCCCC
            ir_tx_buffer[2] = ((irmp_data.command & 0x001F) << 3) | ((~irmp_data.command & 0x01) << 2);   // CCCCCc

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RUWIDO_PROTOCOL==1
        case IRMP_RUWIDO_PROTOCOL:
        {
            ir_tx_buffer[0] = ((irmp_data.address & 0x01FF) >> 1);     // AAAAAAAA
            ir_tx_buffer[1] = ((irmp_data.address & 0x0001) << 7) | ((irmp_data.command & 0x7F));         // ACCCCCCC
            ir_tx_buffer[2] = ((~irmp_data.command & 0x01) << 7);      // c
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_FDC_PROTOCOL==1
        case IRMP_FDC_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, FDC_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, FDC_COMMAND_LEN);

            ir_tx_buffer[0] = (address & 0xFF);   // AAAAAAAA
            ir_tx_buffer[1] = 0; // 00000000
            ir_tx_buffer[2] = 0; // 0000RRRR
            ir_tx_buffer[3] = (command & 0xFF);   // CCCCCCCC
            ir_tx_buffer[4] = ~(command & 0xFF);  // cccccccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_RCCAR_PROTOCOL==1
        case IRMP_RCCAR_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, 2);  //                            A0 A1
            command = bitsreverse (irmp_data.command, RCCAR_COMMAND_LEN - 2); // D0 D1 D2 D3 D4 D5 D6 D7 C0 C1 V

            ir_tx_buffer[0] = ((command & 0x06) << 5) | ((address & 0x0003) << 4) | ((command & 0x0780) >> 7);  //          C0 C1 A0 A1 D0 D1 D2 D3
            ir_tx_buffer[1] = ((command & 0x78) << 1) | ((command & 0x0001) << 3);             //          D4 D5 D6 D7 V  0  0  0

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_JVC_PROTOCOL==1
        case IRMP_JVC_PROTOCOL:
        {
            address = bitsreverse (irmp_data.address, JVC_ADDRESS_LEN);
            command = bitsreverse (irmp_data.command, JVC_COMMAND_LEN);

            ir_tx_buffer[0] = ((address & 0x000F) << 4) | (command & 0x0F00) >> 8;             // AAAACCCC
            ir_tx_buffer[1] = (command & 0x00FF); // CCCCCCCC

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_NIKON_PROTOCOL==1
        case IRMP_NIKON_PROTOCOL:
        {
            ir_tx_buffer[0] = (irmp_data.command & 0x0003) << 6; // CC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_LEGO_PROTOCOL==1
        case IRMP_LEGO_PROTOCOL:
        {
            uint8_t crc = 0x0F ^ ((irmp_data.command & 0x0F00) >> 8) ^ ((irmp_data.command & 0x00F0) >> 4) ^ (irmp_data.command & 0x000F);

            ir_tx_buffer[0] = (irmp_data.command & 0x0FF0) >> 4; // CCCCCCCC
            ir_tx_buffer[1] = ((irmp_data.command & 0x000F) << 4) | crc;       // CCCCcccc
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_IRMP16_PROTOCOL==1
        case IRMP_IRMP16_PROTOCOL:
        {
            command = bitsreverse (irmp_data.command, IRMP16_COMMAND_LEN);

            ir_tx_buffer[0] = (command & 0xFF00) >> 8;         // CCCCCCCC
            ir_tx_buffer[1] = (command & 0x00FF); // CCCCCCCC
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_A1TVBOX_PROTOCOL==1
        case IRMP_A1TVBOX_PROTOCOL:
        {
            ir_tx_buffer[0] = 0x80 | (irmp_data.address >> 2);  // 10AAAAAA
            ir_tx_buffer[1] = (irmp_data.address << 6) | (irmp_data.command >> 2);       // AACCCCCC
            ir_tx_buffer[2] = (irmp_data.command << 6);     // CC

            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_ROOMBA_PROTOCOL==1
        case IRMP_ROOMBA_PROTOCOL:
        {
            ir_tx_buffer[0] = (irmp_data.command & 0x7F) << 1;  // CCCCCCC.
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_PENTAX_PROTOCOL==1
        case IRMP_PENTAX_PROTOCOL:
        {
            ir_tx_buffer[0] = (irmp_data.command & 0x3F) << 2;  // CCCCCC..
            ir_tx_active = TRUE;
            break;
        }
#endif
#if IRSND_SUPPORT_ACP24_PROTOCOL==1
#       define ACP_SET_BIT(acp24_bitno, c, irmp_bitno)                                          \
        do                                                                                      \
        {                                                                                       \
            if ((c) & (1<<(irmp_bitno)))                                                        \
            {                                                                                   \
                ir_tx_buffer[((acp24_bitno)>>3)] |= 1 << (((7 - (acp24_bitno)) & 0x07));        \
            }                                                                                   \
        } while (0)

        case IRMP_ACP24_PROTOCOL:
        {
            uint16_t    cmd = irmp_data.command;
            uint8_t     i;

            address = bitsreverse (irmp_data.address, ACP24_ADDRESS_LEN);

            for (i = 0; i < 8; i++)
            {
                ir_tx_buffer[i] = 0x00;       // CCCCCCCC
            }

            // ACP24-Frame:
            //           1         2         3         4         5         6
            // 0123456789012345678901234567890123456789012345678901234567890123456789
            // N VVMMM    ? ???    t vmA x                 y                     TTTT
            //
            // irmp_data.command:
            //
            //         5432109876543210
            //         NAVVvMMMmtxyTTTT

            ACP_SET_BIT( 0, cmd, 15);
            ACP_SET_BIT(24, cmd, 14);
            ACP_SET_BIT( 2, cmd, 13);
            ACP_SET_BIT( 3, cmd, 12);
            ACP_SET_BIT(22, cmd, 11);
            ACP_SET_BIT( 4, cmd, 10);
            ACP_SET_BIT( 5, cmd,  9);
            ACP_SET_BIT( 6, cmd,  8);
            ACP_SET_BIT(23, cmd,  7);
            ACP_SET_BIT(20, cmd,  6);
            ACP_SET_BIT(26, cmd,  5);
            ACP_SET_BIT(44, cmd,  4);
            ACP_SET_BIT(66, cmd,  3);
            ACP_SET_BIT(67, cmd,  2);
            ACP_SET_BIT(68, cmd,  1);
            ACP_SET_BIT(69, cmd,  0);

            ir_tx_active = TRUE;
            break;
        }
#endif

        case IRMP_RCA_PROTOCOL:
        {
            address = irmp_data.address & 0x0F; // 0000AAAA
            command = irmp_data.command & 0x00FF; // CCCCCCCC

            ir_tx_buffer[0] = address << 4;    // AAAA0000
            ir_tx_buffer[0] |= command >> 4; // AAAACCCC
            ir_tx_buffer[1] = (command & 0x0F) << 4; // CCCC0000
            ir_tx_buffer[1] |= (~address) & 0x0F;  // CCCCaaaa
            ir_tx_buffer[2] = (~command) & 0x00FF; // cccccccc
            ir_tx_active = TRUE;
            break;
        }

        case IRMP_RAW_PROTOCOL: // Add on for RAW protocol
        	{
        		ir_tx_buffer[0] = 0xAA; // Dummy data
        		ir_tx_buffer[1] = 0x55;
        		ir_tx_active = TRUE;
        		break;
        	}

        default:
        	{
        		break;
        	}
    } // switch (ir_tx_protocol)

    return ir_tx_active;
} // uint8_t irsnd_generate_tx_data(IRMP_DATA irmp_data)



/*============================================================================*/
/*
 *  Stop repeated frames
 */
/*============================================================================*/
void irsnd_stop(void)
{
    ir_tx_n_repeat = 0;
}


/*============================================================================*/
/*
 *  Generate bit pulse and space lengths based on the given protocol
 */
/*============================================================================*/
void m1_set_ir_ota_bit_times(void)
{
    switch (ir_tx_protocol)
    {
#if IRSND_SUPPORT_SIRCS_PROTOCOL==1
        case IRMP_SIRCS_PROTOCOL:
        case IRMP_SIRCS15_PROTOCOL: // Add on
        case IRMP_SIRCS20_PROTOCOL: // Add on
            startbit_pulse_time          = IRSND_SIRCS_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_SIRCS_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_SIRCS_1_PULSE_LEN;
            space_1_len                 = IRSND_SIRCS_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_SIRCS_0_PULSE_LEN;
            space_0_len                 = IRSND_SIRCS_PAUSE_LEN - 1;
            ir_has_stop_bit             = SIRCS_STOP_BIT;
            complete_data_len           = SIRCS_MINIMUM_DATA_LEN + sircs_additional_bitlen;
            n_auto_repetitions          = (repeat_counter==0) ? SIRCS_FRAMES : 1;             // 3 frames auto repetition if first frame
            auto_repetition_pause_time   = IRSND_SIRCS_AUTO_REPETITION_PAUSE_LEN;   // 25ms pause
            repeat_frame_pause_time      = IRSND_SIRCS_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_40_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NEC_PROTOCOL==1
        case IRMP_NEC_PROTOCOL:
        case IRMP_NEC8_PROTOCOL: // Add on
        case IRMP_NEC_REPETITION_PROTOCOL:
        case IRMP_ONKYO_PROTOCOL:
            startbit_pulse_time          = IRSND_NEC_START_BIT_PULSE_LEN;

            if (repeat_counter > 0 || ir_tx_protocol==IRMP_NEC_REPETITION_PROTOCOL)
            {
                startbit_space_time      = IRSND_NEC_REPEAT_START_BIT_PAUSE_LEN - 1;
                complete_data_len       = 0;
            }
            else
            {
                startbit_space_time      = IRSND_NEC_START_BIT_PAUSE_LEN - 1;
                complete_data_len       = NEC_COMPLETE_DATA_LEN;
            }

            pulse_1_len                 = IRSND_NEC_PULSE_LEN;
            space_1_len                 = IRSND_NEC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NEC_PULSE_LEN;
            space_0_len                 = IRSND_NEC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NEC_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_NEC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NEC16_PROTOCOL==1
        case IRMP_NEC16_PROTOCOL:
            startbit_pulse_time          = IRSND_NEC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_NEC_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_NEC_PULSE_LEN;
            space_1_len                 = IRSND_NEC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NEC_PULSE_LEN;
            space_0_len                 = IRSND_NEC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NEC_STOP_BIT;
            complete_data_len           = NEC16_COMPLETE_DATA_LEN + 1;         // 1 more: sync bit
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_NEC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NEC42_PROTOCOL==1
        case IRMP_NEC42_PROTOCOL:
            startbit_pulse_time          = IRSND_NEC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_NEC_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_NEC_PULSE_LEN;
            space_1_len                 = IRSND_NEC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NEC_PULSE_LEN;
            space_0_len                 = IRSND_NEC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NEC_STOP_BIT;
            complete_data_len           = NEC42_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_NEC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_MELINERA_PROTOCOL==1
        case IRMP_MELINERA_PROTOCOL:
            startbit_pulse_time          = IRSND_MELINERA_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_MELINERA_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_MELINERA_1_PULSE_LEN;
            space_1_len                 = IRSND_MELINERA_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_MELINERA_0_PULSE_LEN;
            space_0_len                 = IRSND_MELINERA_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = MELINERA_STOP_BIT;
            complete_data_len           = MELINERA_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_MELINERA_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_LGAIR_PROTOCOL==1
        case IRMP_LGAIR_PROTOCOL:
            startbit_pulse_time          = IRSND_NEC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_NEC_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_NEC_PULSE_LEN;
            space_1_len                 = IRSND_NEC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NEC_PULSE_LEN;
            space_0_len                 = IRSND_NEC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NEC_STOP_BIT;
            complete_data_len           = LGAIR_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_NEC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1
        case IRMP_SAMSUNG_PROTOCOL:
            startbit_pulse_time          = IRSND_SAMSUNG_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_SAMSUNG_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_1_len                 = IRSND_SAMSUNG_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_0_len                 = IRSND_SAMSUNG_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = SAMSUNG_STOP_BIT;
            complete_data_len           = SAMSUNG_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_SAMSUNG_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;

        case IRMP_SAMSUNG32_PROTOCOL:
        case IRMP_SAMSUNG32_REPETITION_PROTOCOL:
            startbit_pulse_time          = IRSND_SAMSUNG_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_SAMSUNG_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_1_len                 = IRSND_SAMSUNG_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_0_len                 = IRSND_SAMSUNG_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = SAMSUNG_STOP_BIT;
            complete_data_len           = SAMSUNG32_COMPLETE_DATA_LEN;
            n_auto_repetitions          = SAMSUNG32_FRAMES;       // 1 frame
            auto_repetition_pause_time   = IRSND_SAMSUNG32_AUTO_REPETITION_PAUSE_LEN;            // 47 ms pause
            repeat_frame_pause_time      = IRSND_SAMSUNG32_FRAME_REPEAT_PAUSE_LEN;
            if ( (repeat_counter > 0) || (ir_tx_protocol==IRMP_SAMSUNG32_REPETITION_PROTOCOL) ) // Add on
            {
                ir_tx_buffer[0] = 0x80;   // no address, no command, 1 high bit
            	complete_data_len = 1;
            }
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_SAMSUNG48_PROTOCOL==1
        case IRMP_SAMSUNG48_PROTOCOL:
            startbit_pulse_time          = IRSND_SAMSUNG_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_SAMSUNG_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_1_len                 = IRSND_SAMSUNG_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_SAMSUNG_PULSE_LEN;
            space_0_len                 = IRSND_SAMSUNG_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = SAMSUNG_STOP_BIT;
            complete_data_len           = SAMSUNG48_COMPLETE_DATA_LEN;
            n_auto_repetitions          = SAMSUNG48_FRAMES;       // 1 frame
            auto_repetition_pause_time   = IRSND_SAMSUNG48_AUTO_REPETITION_PAUSE_LEN;            // 47 ms pause
            repeat_frame_pause_time      = IRSND_SAMSUNG48_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
        case IRMP_MATSUSHITA_PROTOCOL:
            startbit_pulse_time          = IRSND_MATSUSHITA_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_MATSUSHITA_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_MATSUSHITA_PULSE_LEN;
            space_1_len                 = IRSND_MATSUSHITA_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_MATSUSHITA_PULSE_LEN;
            space_0_len                 = IRSND_MATSUSHITA_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = MATSUSHITA_STOP_BIT;
            complete_data_len           = MATSUSHITA_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_MATSUSHITA_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_TECHNICS_PROTOCOL==1
        case IRMP_TECHNICS_PROTOCOL:
            startbit_pulse_time          = IRSND_MATSUSHITA_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_MATSUSHITA_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_MATSUSHITA_PULSE_LEN;
            space_1_len                 = IRSND_MATSUSHITA_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_MATSUSHITA_PULSE_LEN;
            space_0_len                 = IRSND_MATSUSHITA_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = MATSUSHITA_STOP_BIT;
            complete_data_len           = TECHNICS_COMPLETE_DATA_LEN;          // here TECHNICS
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_MATSUSHITA_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_KASEIKYO_PROTOCOL==1
        case IRMP_KASEIKYO_PROTOCOL:
            startbit_pulse_time          = IRSND_KASEIKYO_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_KASEIKYO_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_KASEIKYO_PULSE_LEN;
            space_1_len                 = IRSND_KASEIKYO_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_KASEIKYO_PULSE_LEN;
            space_0_len                 = IRSND_KASEIKYO_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = KASEIKYO_STOP_BIT;
            complete_data_len           = KASEIKYO_COMPLETE_DATA_LEN;
            n_auto_repetitions          = (repeat_counter==0) ? KASEIKYO_FRAMES : 1;          // 2 frames auto repetition if first frame
            auto_repetition_pause_time   = IRSND_KASEIKYO_AUTO_REPETITION_PAUSE_LEN;             // 75 ms pause
            repeat_frame_pause_time      = IRSND_KASEIKYO_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_PANASONIC_PROTOCOL==1
        case IRMP_PANASONIC_PROTOCOL:
            startbit_pulse_time          = IRSND_PANASONIC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_PANASONIC_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_PANASONIC_PULSE_LEN;
            space_1_len                 = IRSND_PANASONIC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_PANASONIC_PULSE_LEN;
            space_0_len                 = IRSND_PANASONIC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = PANASONIC_STOP_BIT;
            complete_data_len           = PANASONIC_COMPLETE_DATA_LEN;
            n_auto_repetitions          = PANASONIC_FRAMES;       // 1 frame
            auto_repetition_pause_time   = IRSND_PANASONIC_AUTO_REPETITION_PAUSE_LEN;            // 40 ms pause
            repeat_frame_pause_time      = IRSND_PANASONIC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL==1
        case IRMP_MITSU_HEAVY_PROTOCOL:
            startbit_pulse_time          = IRSND_MITSU_HEAVY_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_MITSU_HEAVY_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_MITSU_HEAVY_PULSE_LEN;
            space_1_len                 = IRSND_MITSU_HEAVY_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_MITSU_HEAVY_PULSE_LEN;
            space_0_len                 = IRSND_MITSU_HEAVY_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = MITSU_HEAVY_STOP_BIT;
            complete_data_len           = MITSU_HEAVY_COMPLETE_DATA_LEN;
            n_auto_repetitions          = MITSU_HEAVY_FRAMES;     // 1 frame
            auto_repetition_pause_time   = 0;;
            repeat_frame_pause_time      = IRSND_MITSU_HEAVY_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_40_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RECS80_PROTOCOL==1
        case IRMP_RECS80_PROTOCOL:
            startbit_pulse_time          = IRSND_RECS80_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RECS80_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_RECS80_PULSE_LEN;
            space_1_len                 = IRSND_RECS80_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_RECS80_PULSE_LEN;
            space_0_len                 = IRSND_RECS80_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = RECS80_STOP_BIT;
            complete_data_len           = RECS80_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RECS80_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RECS80EXT_PROTOCOL==1
        case IRMP_RECS80EXT_PROTOCOL:
            startbit_pulse_time          = IRSND_RECS80EXT_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RECS80EXT_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_RECS80EXT_PULSE_LEN;
            space_1_len                 = IRSND_RECS80EXT_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_RECS80EXT_PULSE_LEN;
            space_0_len                 = IRSND_RECS80EXT_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = RECS80EXT_STOP_BIT;
            complete_data_len           = RECS80EXT_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RECS80EXT_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_TELEFUNKEN_PROTOCOL==1
        case IRMP_TELEFUNKEN_PROTOCOL:
            startbit_pulse_time          = IRSND_TELEFUNKEN_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_TELEFUNKEN_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_TELEFUNKEN_PULSE_LEN;
            space_1_len                 = IRSND_TELEFUNKEN_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_TELEFUNKEN_PULSE_LEN;
            space_0_len                 = IRSND_TELEFUNKEN_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = TELEFUNKEN_STOP_BIT;
            complete_data_len           = TELEFUNKEN_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frames
            auto_repetition_pause_time   = 0;   // IRSND_TELEFUNKEN_AUTO_REPETITION_PAUSE_LEN; xx ms pause
            repeat_frame_pause_time      = IRSND_TELEFUNKEN_FRAME_REPEAT_PAUSE_LEN; // 117 msec pause
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RC5_PROTOCOL==1
        case IRMP_RC5_PROTOCOL:
        case IRMP_RC5X_PROTOCOL: // Add on
            startbit_pulse_time          = IRSND_RC5_BIT_LEN;
            startbit_space_time          = IRSND_RC5_BIT_LEN;
            ir_bit_pulse_time            = IRSND_RC5_BIT_LEN;
            ir_bit_space_time            = IRSND_RC5_BIT_LEN;
            ir_has_stop_bit             = RC5_STOP_BIT;
            complete_data_len           = RC5_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RC5_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RC6_PROTOCOL==1
        case IRMP_RC6_PROTOCOL:
            startbit_pulse_time          = IRSND_RC6_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RC6_START_BIT_PAUSE_LEN - 1;
            ir_bit_pulse_time            = IRSND_RC6_BIT_LEN;
            ir_bit_space_time            = IRSND_RC6_BIT_LEN;
            ir_has_stop_bit             = RC6_STOP_BIT;
            complete_data_len           = RC6_COMPLETE_DATA_LEN_SHORT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RC6_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RC6A_PROTOCOL==1
        case IRMP_RC6A_PROTOCOL:
            startbit_pulse_time          = IRSND_RC6_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RC6_START_BIT_PAUSE_LEN - 1;
            ir_bit_pulse_time            = IRSND_RC6_BIT_LEN;
            ir_bit_space_time            = IRSND_RC6_BIT_LEN;
            ir_has_stop_bit             = RC6_STOP_BIT;
            complete_data_len           = RC6_COMPLETE_DATA_LEN_LONG;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RC6_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_DENON_PROTOCOL==1
        case IRMP_DENON_PROTOCOL:
            startbit_pulse_time          = 0x00;
            startbit_space_time          = 0x00;
            pulse_1_len                 = IRSND_DENON_PULSE_LEN;
            space_1_len                 = IRSND_DENON_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_DENON_PULSE_LEN;
            space_0_len                 = IRSND_DENON_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = DENON_STOP_BIT;
            complete_data_len           = DENON_COMPLETE_DATA_LEN;
            n_auto_repetitions          = DENON_FRAMES;   // 2 frames, 2nd with inverted command
            auto_repetition_pause_time   = IRSND_DENON_AUTO_REPETITION_PAUSE_LEN;   // 65 ms pause after 1st frame
            repeat_frame_pause_time      = IRSND_DENON_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);    // in theory 32kHz, in practice 36kHz is better
            break;
#endif
#if IRSND_SUPPORT_THOMSON_PROTOCOL==1
        case IRMP_THOMSON_PROTOCOL:
            startbit_pulse_time          = 0x00;
            startbit_space_time          = 0x00;
            pulse_1_len                 = IRSND_THOMSON_PULSE_LEN;
            space_1_len                 = IRSND_THOMSON_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_THOMSON_PULSE_LEN;
            space_0_len                 = IRSND_THOMSON_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = THOMSON_STOP_BIT;
            complete_data_len           = THOMSON_COMPLETE_DATA_LEN;
            n_auto_repetitions          = THOMSON_FRAMES; // only 1 frame
            auto_repetition_pause_time   = IRSND_THOMSON_AUTO_REPETITION_PAUSE_LEN;
            repeat_frame_pause_time      = IRSND_THOMSON_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_BOSE_PROTOCOL==1
        case IRMP_BOSE_PROTOCOL:
            startbit_pulse_time          = IRSND_BOSE_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_BOSE_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_BOSE_PULSE_LEN;
            space_1_len                 = IRSND_BOSE_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_BOSE_PULSE_LEN;
            space_0_len                 = IRSND_BOSE_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = BOSE_STOP_BIT;
            complete_data_len           = BOSE_COMPLETE_DATA_LEN;
            n_auto_repetitions          = BOSE_FRAMES;    // 1 frame
            auto_repetition_pause_time   = IRSND_BOSE_AUTO_REPETITION_PAUSE_LEN;    // 40 ms pause
            repeat_frame_pause_time      = IRSND_BOSE_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NUBERT_PROTOCOL==1
        case IRMP_NUBERT_PROTOCOL:
            startbit_pulse_time          = IRSND_NUBERT_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_NUBERT_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_NUBERT_1_PULSE_LEN;
            space_1_len                 = IRSND_NUBERT_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NUBERT_0_PULSE_LEN;
            space_0_len                 = IRSND_NUBERT_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NUBERT_STOP_BIT;
            complete_data_len           = NUBERT_COMPLETE_DATA_LEN;
            n_auto_repetitions          = NUBERT_FRAMES;  // 2 frames
            auto_repetition_pause_time   = IRSND_NUBERT_AUTO_REPETITION_PAUSE_LEN;  // 35 ms pause
            repeat_frame_pause_time      = IRSND_NUBERT_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_FAN_PROTOCOL==1
        case IRMP_FAN_PROTOCOL:
            startbit_pulse_time          = IRSND_FAN_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_FAN_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_FAN_1_PULSE_LEN;
            space_1_len                 = IRSND_FAN_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_FAN_0_PULSE_LEN;
            space_0_len                 = IRSND_FAN_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = FAN_STOP_BIT;
            complete_data_len           = FAN_COMPLETE_DATA_LEN;
            n_auto_repetitions          = FAN_FRAMES;     // only 1 frame
            auto_repetition_pause_time   = IRSND_FAN_AUTO_REPETITION_PAUSE_LEN; // 35 ms pause
            repeat_frame_pause_time      = IRSND_FAN_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_SPEAKER_PROTOCOL==1
        case IRMP_SPEAKER_PROTOCOL:
            startbit_pulse_time          = IRSND_SPEAKER_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_SPEAKER_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_SPEAKER_1_PULSE_LEN;
            space_1_len                 = IRSND_SPEAKER_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_SPEAKER_0_PULSE_LEN;
            space_0_len                 = IRSND_SPEAKER_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = SPEAKER_STOP_BIT;
            complete_data_len           = SPEAKER_COMPLETE_DATA_LEN;
            n_auto_repetitions          = SPEAKER_FRAMES; // 2 frames
            auto_repetition_pause_time   = IRSND_SPEAKER_AUTO_REPETITION_PAUSE_LEN; // 35 ms pause
            repeat_frame_pause_time      = IRSND_SPEAKER_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
        case IRMP_BANG_OLUFSEN_PROTOCOL:
            startbit_pulse_time          = IRSND_BANG_OLUFSEN_START_BIT1_PULSE_LEN;
            startbit_space_time          = IRSND_BANG_OLUFSEN_START_BIT1_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_BANG_OLUFSEN_PULSE_LEN;
            space_1_len                 = IRSND_BANG_OLUFSEN_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_BANG_OLUFSEN_PULSE_LEN;
            space_0_len                 = IRSND_BANG_OLUFSEN_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = BANG_OLUFSEN_STOP_BIT;
            complete_data_len           = BANG_OLUFSEN_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_BANG_OLUFSEN_FRAME_REPEAT_PAUSE_LEN;
            last_bit_value              = 0;
            irsnd_pwm_init (IRSND_FREQ_455_KHZ);
            break;
#endif
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1
        case IRMP_GRUNDIG_PROTOCOL:
            startbit_pulse_time          = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            startbit_space_time          = IRSND_GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN - 1;
            ir_bit_pulse_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_bit_space_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_has_stop_bit             = GRUNDIG_NOKIA_IR60_STOP_BIT;
            complete_data_len           = GRUNDIG_COMPLETE_DATA_LEN;
            n_auto_repetitions          = GRUNDIG_FRAMES; // 2 frames
            auto_repetition_pause_time   = IRSND_GRUNDIG_AUTO_REPETITION_PAUSE_LEN; // 20m sec pause
            repeat_frame_pause_time      = IRSND_GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_LEN;      // 117 msec pause
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_IR60_PROTOCOL==1
        case IRMP_IR60_PROTOCOL:
            startbit_pulse_time          = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            startbit_space_time          = IRSND_GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN - 1;
            ir_bit_pulse_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_bit_space_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_has_stop_bit             = GRUNDIG_NOKIA_IR60_STOP_BIT;
            complete_data_len           = IR60_COMPLETE_DATA_LEN;
            n_auto_repetitions          = IR60_FRAMES;    // 2 frames
            auto_repetition_pause_time   = IRSND_IR60_AUTO_REPETITION_PAUSE_LEN;    // 20m sec pause
            repeat_frame_pause_time      = IRSND_GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_LEN;      // 117 msec pause
            irsnd_pwm_init (IRSND_FREQ_30_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NOKIA_PROTOCOL==1
        case IRMP_NOKIA_PROTOCOL:
            startbit_pulse_time          = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            startbit_space_time          = IRSND_GRUNDIG_NOKIA_IR60_PRE_PAUSE_LEN - 1;
            ir_bit_pulse_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_bit_space_time            = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
            ir_has_stop_bit             = GRUNDIG_NOKIA_IR60_STOP_BIT;
            complete_data_len           = NOKIA_COMPLETE_DATA_LEN;
            n_auto_repetitions          = NOKIA_FRAMES;   // 2 frames
            auto_repetition_pause_time   = IRSND_NOKIA_AUTO_REPETITION_PAUSE_LEN;   // 20 msec pause
            repeat_frame_pause_time      = IRSND_GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_LEN;      // 117 msec pause
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_SIEMENS_PROTOCOL==1
        case IRMP_SIEMENS_PROTOCOL:
            startbit_pulse_time          = IRSND_SIEMENS_BIT_LEN;
            startbit_space_time          = IRSND_SIEMENS_BIT_LEN;
            ir_bit_pulse_time            = IRSND_SIEMENS_BIT_LEN;
            ir_bit_space_time            = IRSND_SIEMENS_BIT_LEN;
            ir_has_stop_bit             = SIEMENS_OR_RUWIDO_STOP_BIT;
            complete_data_len           = SIEMENS_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_SIEMENS_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RUWIDO_PROTOCOL==1
        case IRMP_RUWIDO_PROTOCOL:
            startbit_pulse_time          = IRSND_RUWIDO_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RUWIDO_START_BIT_PAUSE_LEN;
            ir_bit_pulse_time            = IRSND_RUWIDO_BIT_PULSE_LEN;
            ir_bit_space_time            = IRSND_RUWIDO_BIT_PAUSE_LEN;
            ir_has_stop_bit             = SIEMENS_OR_RUWIDO_STOP_BIT;
            complete_data_len           = RUWIDO_COMPLETE_DATA_LEN;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RUWIDO_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_36_KHZ);
            break;
#endif
#if IRSND_SUPPORT_FDC_PROTOCOL==1
        case IRMP_FDC_PROTOCOL:
            startbit_pulse_time          = IRSND_FDC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_FDC_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = FDC_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_FDC_PULSE_LEN;
            space_1_len                 = IRSND_FDC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_FDC_PULSE_LEN;
            space_0_len                 = IRSND_FDC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = FDC_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_FDC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_RCCAR_PROTOCOL==1
        case IRMP_RCCAR_PROTOCOL:
            startbit_pulse_time          = IRSND_RCCAR_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RCCAR_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = RCCAR_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_RCCAR_PULSE_LEN;
            space_1_len                 = IRSND_RCCAR_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_RCCAR_PULSE_LEN;
            space_0_len                 = IRSND_RCCAR_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = RCCAR_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RCCAR_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_JVC_PROTOCOL==1
        case IRMP_JVC_PROTOCOL:
            if (repeat_counter != 0)           // skip start bit if repetition frame
            {
                ir_has_start_bit = FALSE;
            }

            startbit_pulse_time          = IRSND_JVC_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_JVC_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = JVC_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_JVC_PULSE_LEN;
            space_1_len                 = IRSND_JVC_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_JVC_PULSE_LEN;
            space_0_len                 = IRSND_JVC_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = JVC_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_JVC_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_NIKON_PROTOCOL==1
        case IRMP_NIKON_PROTOCOL:
            startbit_pulse_time          = IRSND_NIKON_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_NIKON_START_BIT_PAUSE_LEN;
            complete_data_len           = NIKON_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_NIKON_PULSE_LEN;
            space_1_len                 = IRSND_NIKON_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_NIKON_PULSE_LEN;
            space_0_len                 = IRSND_NIKON_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = NIKON_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_NIKON_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_LEGO_PROTOCOL==1
        case IRMP_LEGO_PROTOCOL:
            startbit_pulse_time          = IRSND_LEGO_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_LEGO_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = LEGO_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_LEGO_PULSE_LEN;
            space_1_len                 = IRSND_LEGO_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_LEGO_PULSE_LEN;
            space_0_len                 = IRSND_LEGO_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = LEGO_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_LEGO_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_IRMP16_PROTOCOL==1
        case IRMP_IRMP16_PROTOCOL:
            startbit_pulse_time          = IRSND_IRMP16_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_IRMP16_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = IRMP16_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_IRMP16_PULSE_LEN;
            space_1_len                 = IRSND_IRMP16_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_IRMP16_PULSE_LEN;
            space_0_len                 = IRSND_IRMP16_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = IRMP16_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_IRMP16_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_A1TVBOX_PROTOCOL==1
        case IRMP_A1TVBOX_PROTOCOL:
            startbit_pulse_time          = IRSND_A1TVBOX_BIT_PULSE_LEN;         // don't use A1TVBOX_START_BIT_PULSE_LEN
            startbit_space_time          = IRSND_A1TVBOX_BIT_PAUSE_LEN;         // don't use A1TVBOX_START_BIT_PAUSE_LEN
            ir_bit_pulse_time            = IRSND_A1TVBOX_BIT_PULSE_LEN;
            ir_bit_space_time            = IRSND_A1TVBOX_BIT_PAUSE_LEN;
            ir_has_stop_bit             = A1TVBOX_STOP_BIT;
            complete_data_len           = A1TVBOX_COMPLETE_DATA_LEN + 1;       // we send stop bit as data
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_A1TVBOX_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_ROOMBA_PROTOCOL==1
        case IRMP_ROOMBA_PROTOCOL:
            startbit_pulse_time          = IRSND_ROOMBA_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_ROOMBA_START_BIT_PAUSE_LEN;
            pulse_1_len                 = IRSND_ROOMBA_1_PULSE_LEN;
            space_1_len                 = IRSND_ROOMBA_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_ROOMBA_0_PULSE_LEN;
            space_0_len                 = IRSND_ROOMBA_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = ROOMBA_STOP_BIT;
            complete_data_len           = ROOMBA_COMPLETE_DATA_LEN;
            n_auto_repetitions          = ROOMBA_FRAMES;  // 8 frames
            auto_repetition_pause_time   = IRSND_ROOMBA_FRAME_REPEAT_PAUSE_LEN;
            repeat_frame_pause_time      = IRSND_ROOMBA_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_PENTAX_PROTOCOL==1
        case IRMP_PENTAX_PROTOCOL:
            startbit_pulse_time          = IRSND_PENTAX_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_PENTAX_START_BIT_PAUSE_LEN;
            complete_data_len           = PENTAX_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_PENTAX_PULSE_LEN;
            space_1_len                 = IRSND_PENTAX_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_PENTAX_PULSE_LEN;
            space_0_len                 = IRSND_PENTAX_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = PENTAX_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_PENTAX_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif
#if IRSND_SUPPORT_ACP24_PROTOCOL==1
        case IRMP_ACP24_PROTOCOL:
            startbit_pulse_time          = IRSND_ACP24_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_ACP24_START_BIT_PAUSE_LEN - 1;
            complete_data_len           = ACP24_COMPLETE_DATA_LEN;
            pulse_1_len                 = IRSND_ACP24_PULSE_LEN;
            space_1_len                 = IRSND_ACP24_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_ACP24_PULSE_LEN;
            space_0_len                 = IRSND_ACP24_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = ACP24_STOP_BIT;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_ACP24_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_38_KHZ);
            break;
#endif

        case IRMP_RCA_PROTOCOL: // Add on
            startbit_pulse_time          = IRSND_RCA_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_RCA_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_RCA_PULSE_LEN;
            space_1_len                 = IRSND_RCA_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_RCA_PULSE_LEN;
            space_0_len                 = IRSND_RCA_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = RCA_STOP_BIT;
            complete_data_len           = RCA_COMPLETE_DATA_LEN + 1;         // 1 more: sync bit
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_RCA_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_56_KHZ);
            break;

        case IRMP_PIONEER_PROTOCOL: // Add on
            startbit_pulse_time          = IRSND_PIONEER_START_BIT_PULSE_LEN;
            startbit_space_time          = IRSND_PIONEER_START_BIT_PAUSE_LEN - 1;
            pulse_1_len                 = IRSND_PIONEER_PULSE_LEN;
            space_1_len                 = IRSND_PIONEER_1_PAUSE_LEN - 1;
            pulse_0_len                 = IRSND_PIONEER_PULSE_LEN;
            space_0_len                 = IRSND_PIONEER_0_PAUSE_LEN - 1;
            ir_has_stop_bit             = PIONEER_STOP_BIT;
            complete_data_len           = PIONEER_COMPLETE_DATA_LEN + 1;         // 1 more: sync bit
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = IRSND_PIONEER_FRAME_REPEAT_PAUSE_LEN;
            irsnd_pwm_init (IRSND_FREQ_40_KHZ);
            break;

        case IRMP_RAW_PROTOCOL: // Add on for RAW protocol
            startbit_pulse_time          = 0;
            startbit_space_time      	= 0;
            complete_data_len       	= ir_universal_payload.rawdata_counter;
            pulse_1_len                 = 0;
            space_1_len                 = 0;
            pulse_0_len                 = 0;
            space_0_len                 = 0;
            ir_has_stop_bit             = 1;
            n_auto_repetitions          = 1;   // 1 frame
            auto_repetition_pause_time   = 0;
            repeat_frame_pause_time      = 0;
            irsnd_pwm_init (ir_tx_frequency);
        	break;

        default:
            ir_tx_active = FALSE;
            break;
    } // switch (ir_tx_protocol)
} // void m1_set_ir_ota_bit_times(void)



/*============================================================================*/
/*
 * This function converts logical data bits to ota data bits and puts in the ota tx buffer.
 * Return: TRUE if the OTA buffer is ready with data to transmit
 */
/*============================================================================*/
uint8_t m1_make_ir_ota_frame(void)
{
	uint8_t prev_bit, cur_bit;
	uint8_t allocation_ok;
	uint16_t i;

	allocation_ok = FALSE;

	switch (ir_tx_protocol)
	{
#if IRSND_SUPPORT_SIRCS_PROTOCOL==1
		case IRMP_SIRCS_PROTOCOL:
		case IRMP_SIRCS15_PROTOCOL: // Add on
		case IRMP_SIRCS20_PROTOCOL: // Add on
#endif
#if IRSND_SUPPORT_NEC_PROTOCOL==1
		case IRMP_NEC_PROTOCOL:
		case IRMP_NEC8_PROTOCOL: // Add on
		case IRMP_NEC_REPETITION_PROTOCOL:
		case IRMP_ONKYO_PROTOCOL:
#endif
#if IRSND_SUPPORT_NEC16_PROTOCOL==1
		case IRMP_NEC16_PROTOCOL:
#endif
#if IRSND_SUPPORT_NEC42_PROTOCOL==1
		case IRMP_NEC42_PROTOCOL:
#endif
#if IRSND_SUPPORT_MELINERA_PROTOCOL==1
		case IRMP_MELINERA_PROTOCOL:
#endif
#if IRSND_SUPPORT_LGAIR_PROTOCOL==1
		case IRMP_LGAIR_PROTOCOL:
#endif
#if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1
		case IRMP_SAMSUNG_PROTOCOL:
		case IRMP_SAMSUNG32_PROTOCOL:
		case IRMP_SAMSUNG32_REPETITION_PROTOCOL: // Add on
#endif
#if IRSND_SUPPORT_SAMSUNG48_PROTOCOL==1
		case IRMP_SAMSUNG48_PROTOCOL:
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
		case IRMP_MATSUSHITA_PROTOCOL:
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
		case IRMP_TECHNICS_PROTOCOL:
#endif
#if IRSND_SUPPORT_KASEIKYO_PROTOCOL==1
		case IRMP_KASEIKYO_PROTOCOL:
#endif
#if IRSND_SUPPORT_PANASONIC_PROTOCOL==1
		case IRMP_PANASONIC_PROTOCOL:
#endif
#if IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL==1
		case IRMP_MITSU_HEAVY_PROTOCOL:
#endif
#if IRSND_SUPPORT_RECS80_PROTOCOL==1
		case IRMP_RECS80_PROTOCOL:
#endif
#if IRSND_SUPPORT_RECS80EXT_PROTOCOL==1
		case IRMP_RECS80EXT_PROTOCOL:
#endif
#if IRSND_SUPPORT_TELEFUNKEN_PROTOCOL==1
		case IRMP_TELEFUNKEN_PROTOCOL:
#endif
#if IRSND_SUPPORT_DENON_PROTOCOL==1
		case IRMP_DENON_PROTOCOL:
#endif
#if IRSND_SUPPORT_BOSE_PROTOCOL==1
		case IRMP_BOSE_PROTOCOL:
#endif
#if IRSND_SUPPORT_NUBERT_PROTOCOL==1
		case IRMP_NUBERT_PROTOCOL:
#endif
#if IRSND_SUPPORT_FAN_PROTOCOL==1
		case IRMP_FAN_PROTOCOL:
#endif
#if IRSND_SUPPORT_SPEAKER_PROTOCOL==1
		case IRMP_SPEAKER_PROTOCOL:
#endif
#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
		case IRMP_BANG_OLUFSEN_PROTOCOL:
#endif
#if IRSND_SUPPORT_FDC_PROTOCOL==1
		case IRMP_FDC_PROTOCOL:
#endif
#if IRSND_SUPPORT_RCCAR_PROTOCOL==1
		case IRMP_RCCAR_PROTOCOL:
#endif
#if IRSND_SUPPORT_JVC_PROTOCOL==1
		case IRMP_JVC_PROTOCOL:
#endif
#if IRSND_SUPPORT_NIKON_PROTOCOL==1
		case IRMP_NIKON_PROTOCOL:
#endif
#if IRSND_SUPPORT_LEGO_PROTOCOL==1
		case IRMP_LEGO_PROTOCOL:
#endif
#if IRSND_SUPPORT_IRMP16_PROTOCOL==1
		case IRMP_IRMP16_PROTOCOL:
#endif
#if IRSND_SUPPORT_THOMSON_PROTOCOL==1
		case IRMP_THOMSON_PROTOCOL:
#endif
#if IRSND_SUPPORT_ROOMBA_PROTOCOL==1
		case IRMP_ROOMBA_PROTOCOL:
#endif
#if IRSND_SUPPORT_PENTAX_PROTOCOL==1
		case IRMP_PENTAX_PROTOCOL:
#endif
#if IRSND_SUPPORT_ACP24_PROTOCOL==1
		case IRMP_ACP24_PROTOCOL:
#endif
		case IRMP_RCA_PROTOCOL: // Add on
		case IRMP_PIONEER_PROTOCOL: // Add on

	#if IRSND_SUPPORT_SIRCS_PROTOCOL==1  || IRSND_SUPPORT_NEC_PROTOCOL==1 || IRSND_SUPPORT_NEC16_PROTOCOL==1 || IRSND_SUPPORT_NEC42_PROTOCOL==1 || \
	    IRSND_SUPPORT_LGAIR_PROTOCOL==1 || IRSND_SUPPORT_SAMSUNG_PROTOCOL==1 || IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1 || IRSND_SUPPORT_TECHNICS_PROTOCOL==1 || \
	    IRSND_SUPPORT_KASEIKYO_PROTOCOL==1 || IRSND_SUPPORT_RECS80_PROTOCOL==1 || IRSND_SUPPORT_RECS80EXT_PROTOCOL==1 || IRSND_SUPPORT_DENON_PROTOCOL==1 || \
	    IRSND_SUPPORT_NUBERT_PROTOCOL==1 || IRSND_SUPPORT_FAN_PROTOCOL==1 || IRSND_SUPPORT_SPEAKER_PROTOCOL==1 || IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1 || \
	    IRSND_SUPPORT_FDC_PROTOCOL==1 || IRSND_SUPPORT_RCCAR_PROTOCOL==1 || IRSND_SUPPORT_JVC_PROTOCOL==1 || IRSND_SUPPORT_NIKON_PROTOCOL==1 || \
	    IRSND_SUPPORT_LEGO_PROTOCOL==1 || IRSND_SUPPORT_THOMSON_PROTOCOL==1 || IRSND_SUPPORT_ROOMBA_PROTOCOL==1 || IRSND_SUPPORT_TELEFUNKEN_PROTOCOL==1 || \
	    IRSND_SUPPORT_PENTAX_PROTOCOL==1 || IRSND_SUPPORT_ACP24_PROTOCOL==1 || IRSND_SUPPORT_PANASONIC_PROTOCOL==1 || IRSND_SUPPORT_BOSE_PROTOCOL==1 || \
	    IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL==1 || IRSND_SUPPORT_IRMP16_PROTOCOL==1 || IRSND_SUPPORT_MELINERA_PROTOCOL

	// Start building tx packet by constructing a buffer of bits
	/*
	 * Packet len = (MSB)startbit_pulse_time + startbit_space_time + (complete_data_len of pulse and space) + (ir_has_stop_bit of pulse and space)
	 * If ir_has_start_bit==TRUE, startbit pulse and space are included, else no startbit.
	 * Buffer len = 2*Packet len of uint16_t, max = 100*2*uint16_t
	 */
			i = 0; // index of first data bit in the ota frame
			ir_ota_frame_len = 0;
			if ( ir_has_start_bit ) // start bit?
			{
				ir_ota_frame_len++;
			} // if ( ir_has_start_bit )

			ir_ota_frame_len += complete_data_len;

			if (ir_has_stop_bit)
				ir_ota_frame_len++;

			ir_ota_frame_len <<= 1; // Each data bit will be converted to two over-the-air (OTA) bits

			if ( ir_frame_pause_time ) // Pause time before the start of the frame?
				ir_ota_frame_len++; // Single OTA bit

			if ( ir_ota_frame_len >= IR_TX_OTA_BITS_FRAME_LEN_MAX )
				ir_ota_frame_len = IR_TX_OTA_BITS_FRAME_LEN_MAX; // length cut

			if ( ir_ota_pkt_buffer!=NULL ) // previously allocated without being freed?
				free(ir_ota_pkt_buffer);
			ir_ota_pkt_buffer = (uint16_t *) malloc(ir_ota_frame_len*2); // Each OTA bit is a 16-bit integer
			if ( ir_ota_pkt_buffer!=NULL )
			{
				allocation_ok = TRUE;

				if ( ir_frame_pause_time ) // Pause time before the start of the frame?
				{
					ir_ota_pkt_buffer[i++] = ir_frame_pause_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
				} // if ( ir_frame_pause_time )

				if ( ir_has_start_bit ) // frame has start bit?
				{
					ir_ota_pkt_buffer[i++] = startbit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
					ir_ota_pkt_buffer[i++] = startbit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
					ir_has_start_bit = FALSE; // reset
				}
				while ( ir_frame_current_bit < complete_data_len )
				{
#if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1
					if (ir_tx_protocol==IRMP_SAMSUNG_PROTOCOL)
					{
						if (ir_frame_current_bit < SAMSUNG_ADDRESS_LEN)    // send address bits
						{
							ir_bit_pulse_time = IRSND_SAMSUNG_PULSE_LEN;
							ir_bit_space_time = (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7)))) ?
										(IRSND_SAMSUNG_1_PAUSE_LEN - 1) : (IRSND_SAMSUNG_0_PAUSE_LEN - 1);
						}
						else if (ir_frame_current_bit==SAMSUNG_ADDRESS_LEN)           // send SYNC bit (16th bit)
						{
							ir_bit_pulse_time = IRSND_SAMSUNG_PULSE_LEN;
							ir_bit_space_time = IRSND_SAMSUNG_START_BIT_PAUSE_LEN - 1;
						}
						else if (ir_frame_current_bit < SAMSUNG_COMPLETE_DATA_LEN)      // send n'th bit
						{
							cur_bit = ir_frame_current_bit - 1;    // sync skipped, offset = -1 !

							ir_bit_pulse_time = IRSND_SAMSUNG_PULSE_LEN;
							ir_bit_space_time = (ir_tx_buffer[cur_bit >> 3] & (1<<(7-(cur_bit & 7)))) ?
										(IRSND_SAMSUNG_1_PAUSE_LEN - 1) : (IRSND_SAMSUNG_0_PAUSE_LEN - 1);
						}
					} // if (ir_tx_protocol==IRMP_SAMSUNG_PROTOCOL)
					else
#endif // #if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1

#if IRSND_SUPPORT_NEC16_PROTOCOL==1
					if (ir_tx_protocol==IRMP_NEC16_PROTOCOL)
					{
						if (ir_frame_current_bit < NEC16_ADDRESS_LEN)      // send address bits
						{
							ir_bit_pulse_time = IRSND_NEC_PULSE_LEN;
							ir_bit_space_time = (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7)))) ?
										(IRSND_NEC_1_PAUSE_LEN - 1) : (IRSND_NEC_0_PAUSE_LEN - 1);
						}
						else if (ir_frame_current_bit==NEC16_ADDRESS_LEN)             // send SYNC bit (8th bit)
						{
							ir_bit_pulse_time = IRSND_NEC_PULSE_LEN;
							ir_bit_space_time = IRSND_NEC_START_BIT_PAUSE_LEN - 1;
						}
						else if (ir_frame_current_bit < NEC16_COMPLETE_DATA_LEN + 1)    // send n'th bit
						{
							cur_bit = ir_frame_current_bit - 1;    // sync skipped, offset = -1 !

							ir_bit_pulse_time = IRSND_NEC_PULSE_LEN;
							ir_bit_space_time = (ir_tx_buffer[cur_bit >> 3] & (1<<(7-(cur_bit & 7)))) ?
										(IRSND_NEC_1_PAUSE_LEN - 1) : (IRSND_NEC_0_PAUSE_LEN - 1);
						}
					} // if (ir_tx_protocol==IRMP_NEC16_PROTOCOL)
	                else
#endif // #if IRSND_SUPPORT_NEC16_PROTOCOL==1

#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
	                if (ir_tx_protocol==IRMP_BANG_OLUFSEN_PROTOCOL)
	                {
	                	if (ir_frame_current_bit==0)  // send 2nd start bit
	                    {
	                		ir_bit_pulse_time = IRSND_BANG_OLUFSEN_START_BIT2_PULSE_LEN;
	                        ir_bit_space_time = IRSND_BANG_OLUFSEN_START_BIT2_PAUSE_LEN - 1;
	                    }
	                    else if (ir_frame_current_bit==1) // send 3rd start bit
	                    {
	                        ir_bit_pulse_time = IRSND_BANG_OLUFSEN_START_BIT3_PULSE_LEN;
	                        ir_bit_space_time = IRSND_BANG_OLUFSEN_START_BIT3_PAUSE_LEN - 1;
	                    }
	                    else if (ir_frame_current_bit==2) // send 4th start bit
	                    {
	                        ir_bit_pulse_time = IRSND_BANG_OLUFSEN_START_BIT2_PULSE_LEN;
	                        ir_bit_space_time = IRSND_BANG_OLUFSEN_START_BIT2_PAUSE_LEN - 1;
	                    }
	                    else if (ir_frame_current_bit==19) // send trailer bit
	                    {
	                        ir_bit_pulse_time = IRSND_BANG_OLUFSEN_PULSE_LEN;
	                        ir_bit_space_time = IRSND_BANG_OLUFSEN_TRAILER_BIT_PAUSE_LEN - 1;
	                    }
	                    else if (ir_frame_current_bit < BANG_OLUFSEN_COMPLETE_DATA_LEN) // send n'th bit
	                    {
	                        cur_bit = (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7)))) ? 1 : 0;
	                        ir_bit_pulse_time = IRSND_BANG_OLUFSEN_PULSE_LEN;

	                        if (cur_bit==last_bit_value)
	                        {
	                        	ir_bit_space_time = IRSND_BANG_OLUFSEN_R_PAUSE_LEN - 1;
	                        }
	                        else
	                        {
	                            ir_bit_space_time = cur_bit ? (IRSND_BANG_OLUFSEN_1_PAUSE_LEN - 1) : (IRSND_BANG_OLUFSEN_0_PAUSE_LEN - 1);
	                            last_bit_value = cur_bit;
	                        }
	                    }
	                } // if (ir_tx_protocol==IRMP_BANG_OLUFSEN_PROTOCOL)
	                else
#endif // #if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
	                if (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7))))
	                {
	                	ir_bit_pulse_time = pulse_1_len;
	                    ir_bit_space_time = space_1_len;
	                }
	                else
	                {
	                	ir_bit_pulse_time = pulse_0_len;
	                    ir_bit_space_time = space_0_len;
	                }

	                ir_ota_pkt_buffer[i++] = ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
	                ir_ota_pkt_buffer[i++] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;

	                ir_frame_current_bit++;
				} // while ( ir_frame_current_bit < complete_data_len )

	            if (ir_has_stop_bit)
	            {
	            	ir_bit_pulse_time = pulse_0_len;
	                if (auto_repetition_counter < n_auto_repetitions)
	                {
	                	ir_bit_space_time = space_0_len;
	                }
	                else
	                {
	                	ir_bit_space_time = 0xFFFF;       // last frame: pause of 255
	                }

	                ir_ota_pkt_buffer[ir_ota_frame_len-2] = ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
	                ir_ota_pkt_buffer[ir_ota_frame_len-1] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
	            } // if (ir_has_stop_bit)
			} // if ( ir_ota_pkt_buffer!=NULL )

			// m1_ir_ota_frame_repeat_handler(IRMP_NEC_PROTOCOL) should be called after this ota frame has been transmitted completely
	        break;
#endif // #if IRSND_SUPPORT_SIRCS_PROTOCOL==1  || IRSND_SUPPORT_NEC_PROTOCOL==1 ||

#if IRSND_SUPPORT_RC5_PROTOCOL==1
		case IRMP_RC5_PROTOCOL:
		case IRMP_RC5X_PROTOCOL: // Add on
#endif
#if IRSND_SUPPORT_RC6_PROTOCOL==1
		case IRMP_RC6_PROTOCOL:
#endif
#if IRSND_SUPPORT_RC6A_PROTOCOL==1
		case IRMP_RC6A_PROTOCOL:
#endif
#if IRSND_SUPPORT_SIEMENS_PROTOCOL==1
		case IRMP_SIEMENS_PROTOCOL:
#endif
#if IRSND_SUPPORT_RUWIDO_PROTOCOL==1
		case IRMP_RUWIDO_PROTOCOL:
#endif
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1
		case IRMP_GRUNDIG_PROTOCOL:
#endif
#if IRSND_SUPPORT_IR60_PROTOCOL==1
		case IRMP_IR60_PROTOCOL:
#endif
#if IRSND_SUPPORT_NOKIA_PROTOCOL==1
		case IRMP_NOKIA_PROTOCOL:
#endif
#if IRSND_SUPPORT_A1TVBOX_PROTOCOL==1
		case IRMP_A1TVBOX_PROTOCOL:
#endif

#if IRSND_SUPPORT_RC5_PROTOCOL     ==1 || \
	    IRSND_SUPPORT_RC6_PROTOCOL     ==1 || \
	    IRSND_SUPPORT_RC6A_PROTOCOL    ==1 || \
	    IRSND_SUPPORT_RUWIDO_PROTOCOL  ==1 || \
	    IRSND_SUPPORT_SIEMENS_PROTOCOL ==1 || \
	    IRSND_SUPPORT_GRUNDIG_PROTOCOL ==1 || \
	    IRSND_SUPPORT_IR60_PROTOCOL    ==1 || \
	    IRSND_SUPPORT_NOKIA_PROTOCOL   ==1 || \
	    IRSND_SUPPORT_A1TVBOX_PROTOCOL ==1

	// Start building tx packet by constructing a buffer of bits
	/*
	* Packet len = (MSB)startbit_pulse_time + startbit_space_time + (complete_data_len of pulse and space) + (ir_has_stop_bit of pulse and space)
	* If ir_has_start_bit==TRUE, startbit pulse and space are included, else no startbit.
	* Buffer len = 2*Packet len of uint16_t, max = 100*2*uint16_t
	*/
			i = 0; // index of first data bit in the ota frame
			ir_ota_frame_len = 0;
			if ( ir_has_start_bit ) // start bit?
			{
				ir_ota_frame_len++;
			} // if ( ir_has_start_bit )

			ir_ota_frame_len += complete_data_len;
			ir_ota_frame_len <<= 1; // Each data bit will be converted to two over-the-air (OTA) bits

			if ( ir_frame_pause_time ) // Pause time before the start of the frame?
				ir_ota_frame_len++; // Single OTA bit

			if ( ir_ota_frame_len >= IR_TX_OTA_BITS_FRAME_LEN_MAX )
				ir_ota_frame_len = IR_TX_OTA_BITS_FRAME_LEN_MAX; // length cut

			if ( ir_ota_pkt_buffer!=NULL ) // previously allocated without being freed?
				free(ir_ota_pkt_buffer);
			ir_ota_pkt_buffer = (uint16_t *) malloc(ir_ota_frame_len*2); // Each OTA bit is a 16-bit integer
			if ( ir_ota_pkt_buffer!=NULL )
			{
				allocation_ok = TRUE;
				memset(ir_ota_pkt_buffer, '\0', ir_ota_frame_len*2); // Clear buffer

				if ( ir_frame_pause_time ) // Pause time before the start of the frame?
				{
					ir_ota_pkt_buffer[i++] = ir_frame_pause_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
				} // if ( ir_frame_pause_time )

				while ( ir_frame_current_bit < complete_data_len )
				{
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1 || IRSND_SUPPORT_IR60_PROTOCOL==1 || IRSND_SUPPORT_NOKIA_PROTOCOL==1
					if (ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL || ir_tx_protocol==IRMP_IR60_PROTOCOL || ir_tx_protocol==IRMP_NOKIA_PROTOCOL)
					{
						if (ir_has_start_bit ||    // start bit of start-frame
								(ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL && ir_frame_current_bit==15) ||      // start bit of info-frame (Grundig)
								(ir_tx_protocol==IRMP_IR60_PROTOCOL && ir_frame_current_bit==7) ||          // start bit of data frame (IR60)
								(ir_tx_protocol==IRMP_NOKIA_PROTOCOL && (ir_frame_current_bit==23 || ir_frame_current_bit==47)))    // start bit of info- or stop-frame (Nokia)
						{
							ir_bit_pulse_time = startbit_pulse_time;
	                    	ir_bit_space_time = startbit_space_time;
	                    	if  ( ir_has_start_bit )
	                    		ir_frame_current_bit--; // This instruction will make ir_frame_current_bit start with 0 in the next loop, instead of 1!
	                    	ir_has_start_bit = FALSE; // reset
	                    }
	                    else // send n'th bit
	                    {
	                    	ir_bit_pulse_time = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
	                    	ir_bit_space_time = IRSND_GRUNDIG_NOKIA_IR60_BIT_LEN;
	                    }
					}
					else // if (ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL || ir_tx_protocol==IRMP_IR60_PROTOCOL || ir_tx_protocol==IRMP_NOKIA_PROTOCOL)
#endif // #if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1 || IRSND_SUPPORT_IR60_PROTOCOL==1 || IRSND_SUPPORT_NOKIA_PROTOCOL==1
					{
						if (ir_has_start_bit)  // 1 start bit
						{
#if IRSND_SUPPORT_RC5_PROTOCOL==1
							if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) ||  (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
	                    	{
								if ( i==1 ) // A frame pause is just added?
								{
									ir_ota_pkt_buffer[0] += ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Space of the start bit
								} // if ( i==1 )
								else
								{
									ir_ota_pkt_buffer[i++] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Space of the start bit
								}
								ir_ota_pkt_buffer[i++] = ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Pulse of the start bit
								prev_bit = 1;
	                    	} // if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) ||  (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
	                    	else
#endif // #if IRSND_SUPPORT_RC5_PROTOCOL==1
#if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1
							if (ir_tx_protocol==IRMP_RC6_PROTOCOL || ir_tx_protocol==IRMP_RC6A_PROTOCOL)
	                    	{
								ir_ota_pkt_buffer[i++] = startbit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // H start pulse
								ir_ota_pkt_buffer[i++] = startbit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // L start pulse
								prev_bit = 1;
	                    	}
	                    	else
#endif
#if IRSND_SUPPORT_A1TVBOX_PROTOCOL==1
	                    	if (ir_tx_protocol==IRMP_A1TVBOX_PROTOCOL)
	                    	{
	                    		ir_ota_frame_len--;
	                    	}
	                    	else
#endif
	                    	{
	                    		;
	                    	}

	                    	ir_has_start_bit = FALSE; // reset
	                    	ir_frame_current_bit--; // This instruction will make ir_frame_current_bit start with 0 in the next loop, instead of 1!
						} // if (ir_has_start_bit)
	                    else // send n'th bit
	                    {
#if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1
	                    	if (ir_tx_protocol==IRMP_RC6_PROTOCOL || ir_tx_protocol==IRMP_RC6A_PROTOCOL)
	                    	{
								cur_bit = (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7)))) ? 1 : 0; // MSB -> LSB
	                    		if (ir_tx_protocol==IRMP_RC6_PROTOCOL)
	                    		{
	                    			if (ir_frame_current_bit==4)          // Trailer bit? Double bit time
	                    			{
	                    				ir_bit_pulse_time = IRSND_RC6_BIT_2_LEN;      // = 2 * RC_BIT_LEN
	                    				ir_bit_space_time = IRSND_RC6_BIT_2_LEN;      // = 2 * RC_BIT_LEN
	                    			}
	                    		}
#if IRSND_SUPPORT_RC6A_PROTOCOL==1
	                    		else // if (ir_tx_protocol==IRMP_RC6A_PROTOCOL)
	                    		{
	                    			if (ir_frame_current_bit==4)          // toggle bit (double len)
	                    			{
	                    				ir_bit_pulse_time = IRSND_RC6_BIT_3_LEN;      // = 3 * RC6_BIT_LEN
	                    				ir_bit_space_time = IRSND_RC6_BIT_2_LEN;      // = 2 * RC6_BIT_LEN
	                    			}
	                    			else if (ir_frame_current_bit==5)     // toggle bit (double len)
	                    			{
	                    				ir_bit_space_time = IRSND_RC6_BIT_2_LEN;      // = 2 * RC6_BIT_LEN
	                    			}
	                    		}
#endif // #if IRSND_SUPPORT_RC6A_PROTOCOL==1
								i += m1_ir_ota_manchester_coding(i - 1, prev_bit, cur_bit, ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR, OTA_MANCHESTER_CODING_THOMAS);
								prev_bit = cur_bit; // Update
								if ( ir_frame_current_bit==4 || ir_frame_current_bit==5 )
	                    		{
		                    		ir_bit_pulse_time = IRSND_RC6_BIT_LEN; // Restore default bit time
		                    		ir_bit_space_time = IRSND_RC6_BIT_LEN;
	                    		} // if ( ir_frame_current_bit==4 || ir_frame_current_bit==5 )
	                    	} // if (ir_tx_protocol==IRMP_RC6_PROTOCOL || ir_tx_protocol==IRMP_RC6A_PROTOCOL)
#endif // #if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1
#if IRSND_SUPPORT_RC5_PROTOCOL==1 // Add on
// ir_tx_buffer[0] = CTAAAAAC
// ir_tx_buffer[1] = CCCCC000
// [H L] = logic 0, [L H] = logic 1
	                    	else if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) || (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
	                    	{
	                    		cur_bit = (ir_tx_buffer[ir_frame_current_bit >> 3] & (1<<(7-(ir_frame_current_bit & 7)))) ? 1 : 0; // MSB -> LSB
	                    		i += m1_ir_ota_manchester_coding(i - 1, prev_bit, cur_bit, ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR, OTA_MANCHESTER_CODING_IEEE);
	                    		prev_bit = cur_bit; // Update
	                    	} // else if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) || (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
#endif // #if IRSND_SUPPORT_RC5_PROTOCOL==1
	                    	else
	                    	{
	                    		ir_ota_pkt_buffer[i++] = ir_bit_pulse_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
	                    		ir_ota_pkt_buffer[i++] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR;
	                    	} // else
	                    } // else
							// if (ir_has_start_bit)
					} // else
	                	// if (ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL || ir_tx_protocol==IRMP_IR60_PROTOCOL || ir_tx_protocol==IRMP_NOKIA_PROTOCOL)

					ir_frame_current_bit++;
				} // while ( ir_frame_current_bit < complete_data_len )

#if IRSND_SUPPORT_RC5_PROTOCOL==1
				if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) || (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
				{
					ir_ota_frame_len = i; // Update new ota frame length
					if ( !(ir_ota_frame_len & 0x01) ) // Length is not an odd number?
					{
						// ir_ota_pkt_buffer[0] is always a space (or pause) time.
						// So ir_ota_frame_len must always be an odd number for the OTA data to be transmitted correctly!
						// The OTA transmission is handled by the function infrared_transmit().
						ir_ota_pkt_buffer[i] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Add last bit time
						ir_ota_frame_len++; // Make odd number
					} // if ( !(ir_ota_frame_len & 0x01) )
				} // if ( (ir_tx_protocol==IRMP_RC5_PROTOCOL) || (ir_tx_protocol==IRMP_RC5X_PROTOCOL) )
#endif // #if IRSND_SUPPORT_RC5_PROTOCOL==1
				else if ( ir_tx_protocol==IRMP_RC6_PROTOCOL || ir_tx_protocol==IRMP_RC6A_PROTOCOL )
				{
					ir_ota_frame_len = i; // Update new ota frame length
					if ( ir_frame_pause_time ) // Pause time before the start of the frame?
					{
						// Length must be an odd number
						if ( !(ir_ota_frame_len & 0x01) ) // Length is not an odd number?
						{
							ir_ota_pkt_buffer[i] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Add last bit time
							ir_ota_frame_len++; // Make odd number
						} // if ( !(ir_ota_frame_len & 0x01) )
					} // if ( ir_frame_pause_time )
					else
					{
						// Length must be an even number
						if ( ir_ota_frame_len & 0x01 ) // Length is an odd number?
						{
							ir_ota_pkt_buffer[i] = ir_bit_space_time/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Add last bit time
							ir_ota_frame_len++; // Make even number
						} // if ( !(ir_ota_frame_len & 0x01) )
					} // else
				} // else if ( ir_tx_protocol==IRMP_RC6_PROTOCOL || ir_tx_protocol==IRMP_RC6A_PROTOCOL )
#if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1
#endif // #if IRSND_SUPPORT_RC6_PROTOCOL==1 || IRSND_SUPPORT_RC6A_PROTOCOL==1

				// m1_ir_ota_frame_repeat_handler(IRMP_RC5_PROTOCOL) should be called after this ota frame has been transmitted completely
			} // if ( ir_ota_pkt_buffer!=NULL )
	        break;
#endif // IRSND_SUPPORT_RC5_PROTOCOL==1 || IRSND_SUPPORT_RC6_PROTOCOL==1 || || IRSND_SUPPORT_RC6A_PROTOCOL==1 || IRSND_SUPPORT_SIEMENS_PROTOCOL==1 ||
	       // IRSND_SUPPORT_RUWIDO_PROTOCOL==1 || IRSND_SUPPORT_GRUNDIG_PROTOCOL==1 || IRSND_SUPPORT_IR60_PROTOCOL==1 || IRSND_SUPPORT_NOKIA_PROTOCOL==1

		case IRMP_RAW_PROTOCOL: // Add on for RAW protocol
			ir_ota_frame_len = ir_universal_payload.rawdata_counter;
			ir_ota_pkt_buffer = ir_universal_payload.pprawdata;
			// An extra pause time is added automatically to the raw buffer in the function ir_remote_file_raw_data_parse()
			// So the frame length must be an odd number for the frame to be OTA transmitted correctly!
			if ( !(ir_ota_frame_len & 0x01) ) // Length is not an odd number?
			{
				ir_ota_pkt_buffer[ir_ota_frame_len] = NEC_PULSE_TIME/IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Add last bit time, any value should work!
				ir_ota_frame_len++; // Make odd number
			} // if ( !(ir_ota_frame_len & 0x01) )
			allocation_ok = TRUE;
			break;

		default:
	        ir_tx_active = FALSE;
	        break;
	} // switch (ir_tx_protocol)

	return allocation_ok;
} // uint8_t m1_make_ir_ota_frame(void)



/*============================================================================*/
/*
 * This function prepares the potential repeated frames after the first ota frame has been transmitted.
 * Parameters: ir_protocol will be used to decide how many repeated frames will be created
*/
/*============================================================================*/
uint8_t m1_ir_ota_frame_repeat_handler(uint8_t ir_protocol)
{
	uint8_t do_repeat = 0;

	switch (ir_protocol)
	{
#if IRSND_SUPPORT_SIRCS_PROTOCOL==1
		case IRMP_SIRCS_PROTOCOL:
		case IRMP_SIRCS15_PROTOCOL: // Add on
		case IRMP_SIRCS20_PROTOCOL: // Add on
#endif
#if IRSND_SUPPORT_NEC_PROTOCOL==1
		case IRMP_NEC_PROTOCOL:
		case IRMP_NEC8_PROTOCOL: // Add on
		case IRMP_NEC_REPETITION_PROTOCOL:
		case IRMP_ONKYO_PROTOCOL:
#endif
#if IRSND_SUPPORT_NEC16_PROTOCOL==1
		case IRMP_NEC16_PROTOCOL:
#endif
#if IRSND_SUPPORT_NEC42_PROTOCOL==1
		case IRMP_NEC42_PROTOCOL:
#endif
#if IRSND_SUPPORT_MELINERA_PROTOCOL==1
		case IRMP_MELINERA_PROTOCOL:
#endif
#if IRSND_SUPPORT_LGAIR_PROTOCOL==1
		case IRMP_LGAIR_PROTOCOL:
#endif
#if IRSND_SUPPORT_SAMSUNG_PROTOCOL==1
		case IRMP_SAMSUNG_PROTOCOL:
		case IRMP_SAMSUNG32_PROTOCOL:
		case IRMP_SAMSUNG32_REPETITION_PROTOCOL: // Add on
			if ( repeat_counter > 0 )
			{
				ir_tx_protocol = IRMP_SAMSUNG32_REPETITION_PROTOCOL;
			} // if ( repeat_counter > 0 )
#endif
#if IRSND_SUPPORT_SAMSUNG48_PROTOCOL==1
		case IRMP_SAMSUNG48_PROTOCOL:
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
		case IRMP_MATSUSHITA_PROTOCOL:
#endif
#if IRSND_SUPPORT_MATSUSHITA_PROTOCOL==1
		case IRMP_TECHNICS_PROTOCOL:
#endif
#if IRSND_SUPPORT_KASEIKYO_PROTOCOL==1
		case IRMP_KASEIKYO_PROTOCOL:
#endif
#if IRSND_SUPPORT_PANASONIC_PROTOCOL==1
		case IRMP_PANASONIC_PROTOCOL:
#endif
#if IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL==1
		case IRMP_MITSU_HEAVY_PROTOCOL:
#endif
#if IRSND_SUPPORT_RECS80_PROTOCOL==1
		case IRMP_RECS80_PROTOCOL:
#endif
#if IRSND_SUPPORT_RECS80EXT_PROTOCOL==1
		case IRMP_RECS80EXT_PROTOCOL:
#endif
#if IRSND_SUPPORT_TELEFUNKEN_PROTOCOL==1
		case IRMP_TELEFUNKEN_PROTOCOL:
#endif
#if IRSND_SUPPORT_DENON_PROTOCOL==1
		case IRMP_DENON_PROTOCOL:
#endif
#if IRSND_SUPPORT_BOSE_PROTOCOL==1
		case IRMP_BOSE_PROTOCOL:
#endif
#if IRSND_SUPPORT_NUBERT_PROTOCOL==1
		case IRMP_NUBERT_PROTOCOL:
#endif
#if IRSND_SUPPORT_FAN_PROTOCOL==1
		case IRMP_FAN_PROTOCOL:
#endif
#if IRSND_SUPPORT_SPEAKER_PROTOCOL==1
		case IRMP_SPEAKER_PROTOCOL:
#endif
#if IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL==1
		case IRMP_BANG_OLUFSEN_PROTOCOL:
#endif
#if IRSND_SUPPORT_FDC_PROTOCOL==1
		case IRMP_FDC_PROTOCOL:
#endif
#if IRSND_SUPPORT_RCCAR_PROTOCOL==1
		case IRMP_RCCAR_PROTOCOL:
#endif
#if IRSND_SUPPORT_JVC_PROTOCOL==1
		case IRMP_JVC_PROTOCOL:
#endif
#if IRSND_SUPPORT_NIKON_PROTOCOL==1
		case IRMP_NIKON_PROTOCOL:
#endif
#if IRSND_SUPPORT_LEGO_PROTOCOL==1
		case IRMP_LEGO_PROTOCOL:
#endif
#if IRSND_SUPPORT_IRMP16_PROTOCOL==1
		case IRMP_IRMP16_PROTOCOL:
#endif
#if IRSND_SUPPORT_THOMSON_PROTOCOL==1
		case IRMP_THOMSON_PROTOCOL:
#endif
#if IRSND_SUPPORT_ROOMBA_PROTOCOL==1
		case IRMP_ROOMBA_PROTOCOL:
#endif
#if IRSND_SUPPORT_PENTAX_PROTOCOL==1
		case IRMP_PENTAX_PROTOCOL:
#endif
#if IRSND_SUPPORT_ACP24_PROTOCOL==1
		case IRMP_ACP24_PROTOCOL:
#endif
			ir_has_start_bit = TRUE;
			ir_frame_current_bit = 0; // reset
			do_repeat = 1;
			break;

#if IRSND_SUPPORT_RC5_PROTOCOL==1
		case IRMP_RC5_PROTOCOL:
		case IRMP_RC5X_PROTOCOL: // Add on
#endif
#if IRSND_SUPPORT_RC6_PROTOCOL==1
		case IRMP_RC6_PROTOCOL:
#endif
#if IRSND_SUPPORT_RC6A_PROTOCOL==1
		case IRMP_RC6A_PROTOCOL:
#endif
#if IRSND_SUPPORT_SIEMENS_PROTOCOL==1
		case IRMP_SIEMENS_PROTOCOL:
#endif
#if IRSND_SUPPORT_RUWIDO_PROTOCOL==1
		case IRMP_RUWIDO_PROTOCOL:
#endif
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1
		case IRMP_GRUNDIG_PROTOCOL:
#endif
#if IRSND_SUPPORT_IR60_PROTOCOL==1
		case IRMP_IR60_PROTOCOL:
#endif
#if IRSND_SUPPORT_NOKIA_PROTOCOL==1
		case IRMP_NOKIA_PROTOCOL:
#endif
#if IRSND_SUPPORT_A1TVBOX_PROTOCOL==1
		case IRMP_A1TVBOX_PROTOCOL:
#endif
			ir_has_start_bit = TRUE;
			ir_frame_current_bit = 0; // reset

#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1 || IRSND_SUPPORT_IR60_PROTOCOL==1 || IRSND_SUPPORT_NOKIA_PROTOCOL==1
			if (ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL || ir_tx_protocol==IRMP_IR60_PROTOCOL || ir_tx_protocol==IRMP_NOKIA_PROTOCOL)
			{
				auto_repetition_counter++;

				if (repeat_counter > 0)
				{     // set 117 msec pause time
					auto_repetition_pause_time = IRSND_GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_LEN;
                }

                if (repeat_counter < n_repeat_frames)       // tricky: repeat n info frames per auto repetition before sending last stop frame
                {
                	n_auto_repetitions++;  // increment number of auto repetitions
                	repeat_counter++;
                }
                else if (auto_repetition_counter==n_auto_repetitions)
                {
                	ir_tx_active = FALSE;
                    auto_repetition_counter = 0;
                }
			}
            else
#endif // #if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1 ||
            {
            	do_repeat = 1;
            }
			break;

		case IRMP_RAW_PROTOCOL: // Add on for RAW protocol
			ir_has_start_bit = TRUE;
			ir_frame_current_bit = 0; // reset
			ir_tx_active = FALSE;
			break;

		default:
			ir_tx_active = FALSE;
			if ( ir_ota_pkt_buffer!=NULL )
			{
				if ( ir_tx_protocol != IRMP_RAW_PROTOCOL ) // Add on
					free(ir_ota_pkt_buffer);
				ir_ota_pkt_buffer = NULL;
			}
			ir_ota_buffer_ok = FALSE;
			irsnd_pwm_deinit();
			break;
	} // switch (ir_protocol)

	if ( do_repeat )
	{
		if ( repeat_counter==0 )
		{
			auto_repetition_counter++;
			if (auto_repetition_counter >= n_auto_repetitions)
			{
				ir_tx_active = FALSE;
				auto_repetition_counter = 0;
			}
		} // if ( repeat_counter==0 )
		if ( auto_repetition_counter==0 )
		{
			repeat_counter++;
			if (repeat_counter < n_repeat_frames)
			{
#if IRSND_SUPPORT_FDC_PROTOCOL==1
				if (ir_tx_protocol==IRMP_FDC_PROTOCOL)
				{
					ir_tx_buffer[2] |= 0x0F;
				}
#endif
				ir_tx_active = TRUE;
			} // if (repeat_counter < n_repeat_frames)
			else
			{
				ir_tx_active = FALSE;
				n_repeat_frames = 0;
				repeat_counter = 0;
			}
		} // if ( auto_repetition_counter==0 )
	} // if ( do_repeat )

	return ir_tx_active;
} // uint8_t m1_ir_ota_frame_repeat_handler(uint8_t ir_protocol)



/*============================================================================*/
/*
 * This function generates multiple ota data frames for repeated transmission.
 * It has to be called multiple times after each single ota transmission has completed.
 * Return: 	FALSE is the transmission has completed
*/
/*============================================================================*/
uint8_t m1_make_ir_ota_multiframes(void)
{
	ir_frame_pause_time = 0; // Reset

	if (ir_tx_active)
    {
        if (ir_has_start_bit) // start of transmission...
        {
            if (auto_repetition_counter > 0)
            {
            	if ( auto_repetition_pause_time )
                {
            		ir_frame_pause_time = auto_repetition_pause_time;

#if IRSND_SUPPORT_DENON_PROTOCOL==1
                    if (ir_tx_protocol==IRMP_DENON_PROTOCOL)             // n'th denon frame
                    {
                    	ir_has_start_bit = FALSE;
                    	ir_frame_current_bit = 16;
                        complete_data_len   = 2 * DENON_COMPLETE_DATA_LEN + 1;
                    }
                    else
#endif
#if IRSND_SUPPORT_GRUNDIG_PROTOCOL==1
                    if (ir_tx_protocol==IRMP_GRUNDIG_PROTOCOL)           // n'th grundig frame
                    {
                    	ir_has_start_bit = FALSE;
                    	ir_frame_current_bit = 15;
                        complete_data_len   = 16 + GRUNDIG_COMPLETE_DATA_LEN;
                    }
                    else
#endif
#if IRSND_SUPPORT_IR60_PROTOCOL==1
                    if (ir_tx_protocol==IRMP_IR60_PROTOCOL) // n'th IR60 frame
                    {
                    	ir_has_start_bit = FALSE;
                    	ir_frame_current_bit = 7;
                        complete_data_len   = 2 * IR60_COMPLETE_DATA_LEN + 1;
                    }
                    else
#endif
#if IRSND_SUPPORT_NOKIA_PROTOCOL==1
                    if (ir_tx_protocol==IRMP_NOKIA_PROTOCOL)             // n'th nokia frame
                    {
                        if (auto_repetition_counter + 1 < n_auto_repetitions)
                        {
                        	ir_has_start_bit = FALSE;
                        	ir_frame_current_bit = 23;
                            complete_data_len   = 24 + NOKIA_COMPLETE_DATA_LEN;
                        }
                        else  // nokia stop frame
                        {
                            ir_has_start_bit = TRUE;
                            complete_data_len   = NOKIA_COMPLETE_DATA_LEN;
                        }
                    }
                    else
#endif
                    {
                        ;
                    }
                } // if ( auto_repetition_pause_time )
            } // if (auto_repetition_counter > 0)

            else if ( repeat_frame_pause_time && (!packet_repeat_pause_passed) )
            {
            	packet_repeat_pause_passed = 1;
            	ir_frame_pause_time = repeat_frame_pause_time;
            } // else if ( repeat_frame_pause_time && (!packet_repeat_pause_passed) )

            n_repeat_frames = ir_tx_n_repeat;
            if (n_repeat_frames==IRSND_ENDLESS_REPETITION)
            {
                n_repeat_frames = 255;
            }
            m1_set_ir_ota_bit_times();
        } // if (ir_has_start_bit)

        if ( ir_tx_active )
    	{
            ir_ota_buffer_ok = m1_make_ir_ota_frame();
            packet_repeat_pause_passed = 0; // Reset
    	} // if ( ir_tx_active )
    } // if (ir_tx_active)
    else
    {
		if ( ir_ota_pkt_buffer!=NULL )
		{
			if ( ir_tx_protocol != IRMP_RAW_PROTOCOL ) // Add on
				free(ir_ota_pkt_buffer);
			ir_ota_pkt_buffer = NULL;
		}
		ir_ota_buffer_ok = FALSE;
		irsnd_pwm_deinit();
    } // else

    return ir_tx_active;
} // uint8_t m1_make_ir_ota_multiframes(void)



/*============================================================================*/
/*
 * This function creates ota data for Manchester coded data bit.
 * Param: prev_data_index is the pointer index to the previous data
 *
 * Return: increment for the ota buffer
*/
/*============================================================================*/
static uint8_t m1_ir_ota_manchester_coding(uint8_t prev_data_index, uint8_t prev_bit, uint8_t cur_bit, uint16_t bit_time, uint8_t manchester_code)
{
	uint8_t bit_inc = 0;

	cur_bit ^= manchester_code;
	prev_bit ^= manchester_code;

	if ( !cur_bit ) // This bit is a 0 (IEEE) or 1 (Thomas)?
	{
		if ( prev_bit ) // Previous bit is a 1 (IEEE) or 0 (Thomas)?
		{
			bit_inc = 1;
			// Previous bit = 1 = [L H], Current bit = 0 = [H L] for OTA_MANCHESTER_CODING_IEEE
			// Previous bit = 1 = [H L], Current bit = 0 = [L H] for OTA_MANCHESTER_CODING_THOMAS
		} // if ( prev_bit )
		else // Previous bit is a 0 (IEEE) or 1 (Thomas)
		{
			bit_inc = 2;
			// Previous bit = 0 = [H L], Current bit = 0 = [H L] for OTA_MANCHESTER_CODING_IEEE
			// Previous bit = 0 = [L H], Current bit = 0 = [L H] for OTA_MANCHESTER_CODING_THOMAS
		} // else
	} // if ( !cur_bit )
	else // This bit is a 1 (IEEE) or 0 (Thomas)
	{
		if ( !prev_bit ) // Previous bit is a 0 (IEEE) or 1 (Thomas)?
		{
			bit_inc = 1;
			// Previous bit = 0 = [H L], Current bit = 1 = [L H] for OTA_MANCHESTER_CODING_IEEE
			// Previous bit = 0 = [L H], Current bit = 1 = [H L] for OTA_MANCHESTER_CODING_THOMAS
		} // if ( !prev_bit )
		else // Previous bit is a 1 (IEEE) or 0 (Thomas)
		{
			bit_inc = 2;
			// Previous bit = 1 = [L H], Current bit = 1 = [L H] for OTA_MANCHESTER_CODING_IEEE
			// Previous bit = 1 = [H L], Current bit = 1 = [H L] for OTA_MANCHESTER_CODING_THOMAS
		} // else
	} // else

	if ( bit_inc==1 )
	{
		ir_ota_pkt_buffer[prev_data_index++] += bit_time; // Combine the H halves (IEEE) or L halves (Thomas)
		ir_ota_pkt_buffer[prev_data_index] = bit_time; // L half (IEEE) or H half (Thomas)
	} // if ( bit_inc==1 )
	else // bit_inc==2
	{
		ir_ota_pkt_buffer[++prev_data_index] = bit_time; // L half (IEEE) or H half (Thomas)
		ir_ota_pkt_buffer[++prev_data_index] = bit_time; // H half (IEEE) or L half (Thomas)
	} // else

	return bit_inc;
} // static uint8_t m1_ir_ota_manchester_coding(uint8_t prev_data_index, uint8_t prev_bit, uint8_t cur_bit, uint16_t bit_time, uint8_t manchester_code)

