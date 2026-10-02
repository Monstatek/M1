/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * irmpprotocols.h - irmp protocols
 *
 * DO NOT INCLUDE THIS FILE, WILL BE INCLUDED BY IRMP.H or IRSND.H!
 *
 * Copyright (c) 2013-2020 Frank Meyer - frank(at)fli4l.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#ifndef _IRMP_PROTOCOLS_H_
#define _IRMP_PROTOCOLS_H_

#if !defined(_IRMP_H_) && !defined(_IRSND_H_)
#  error please include only irmp.h or irsnd.h, not irmpprotocols.h
#endif

#include <stdint.h>

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * IR protocols:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
typedef enum
{
	IRMP_UNKNOWN_PROTOCOL = 0,      // unknown protocol
	IRMP_SIRCS_PROTOCOL,			// Sony
	IRMP_NEC_PROTOCOL,              // NEC, Pioneer, JVC, Toshiba, NoName etc.
	IRMP_SAMSUNG_PROTOCOL,	        // Samsung
	IRMP_MATSUSHITA_PROTOCOL,       // Matsushita
	IRMP_KASEIKYO_PROTOCOL,	        // Kaseikyo (Panasonic etc)
	IRMP_RECS80_PROTOCOL,           // Philips, Thomson, Nordmende, Telefunken, Saba
	IRMP_RC5_PROTOCOL,              // Philips etc
	IRMP_DENON_PROTOCOL,            // Denon, Sharp
	IRMP_RC6_PROTOCOL,              // Philips etc
	IRMP_SAMSUNG32_PROTOCOL,        // Samsung32: no sync pulse at bit 16, length 32 instead of 37
	IRMP_APPLE_PROTOCOL,            // Apple, very similar to NEC
	IRMP_RECS80EXT_PROTOCOL,        // Philips, Technisat, Thomson, Nordmende, Telefunken, Saba
	IRMP_NUBERT_PROTOCOL,           // Nubert
	IRMP_BANG_OLUFSEN_PROTOCOL,     // Bang & Olufsen
	IRMP_GRUNDIG_PROTOCOL,          // Grundig
	IRMP_NOKIA_PROTOCOL,            // Nokia
	IRMP_SIEMENS_PROTOCOL,          // Siemens, e.g. Gigaset
	IRMP_FDC_PROTOCOL,              // FDC keyboard
	IRMP_RCCAR_PROTOCOL,            // RC Car
	IRMP_JVC_PROTOCOL,              // JVC (NEC with 16 bits)
	IRMP_RC6A_PROTOCOL,             // RC6A, e.g. Kathrein, XBOX
	IRMP_NIKON_PROTOCOL,            // Nikon
	IRMP_RUWIDO_PROTOCOL,           // Ruwido, e.g. T-Home Mediareceiver
	IRMP_IR60_PROTOCOL,             // IR60 (SDA2008)
	IRMP_KATHREIN_PROTOCOL,         // Kathrein
	IRMP_NETBOX_PROTOCOL,           // Netbox keyboard (bitserial)
	IRMP_NEC16_PROTOCOL,            // NEC with 16 bits (incl. sync)
	IRMP_NEC42_PROTOCOL,            // NEC with 42 bits
	IRMP_LEGO_PROTOCOL,             // LEGO Power Functions RC
	IRMP_THOMSON_PROTOCOL,          // Thomson
	IRMP_BOSE_PROTOCOL,             // BOSE
	IRMP_A1TVBOX_PROTOCOL,          // A1 TV Box
	IRMP_ORTEK_PROTOCOL,            // ORTEK - Hama
	IRMP_TELEFUNKEN_PROTOCOL,       // Telefunken (1560)
	IRMP_ROOMBA_PROTOCOL,           // iRobot Roomba vacuum cleaner
	IRMP_RCMM32_PROTOCOL,           // Fujitsu-Siemens (Activy remote control)
	IRMP_RCMM24_PROTOCOL,           // Fujitsu-Siemens (Activy keyboard)
	IRMP_RCMM12_PROTOCOL,           // Fujitsu-Siemens (Activy keyboard)
	IRMP_SPEAKER_PROTOCOL,          // Another loudspeaker protocol, similar to Nubert
	IRMP_LGAIR_PROTOCOL,            // LG air conditioner
	IRMP_SAMSUNG48_PROTOCOL,        // air conditioner with SAMSUNG protocol (48 bits)
	IRMP_MERLIN_PROTOCOL,           // Merlin (Pollin 620 185)
	IRMP_PENTAX_PROTOCOL,           // Pentax camera
	IRMP_FAN_PROTOCOL,              // FAN (ventilator), very similar to NUBERT, but last bit is data bit instead of stop bit
	IRMP_S100_PROTOCOL,             // very similar to RC5, but 14 instead of 13 data bits
	IRMP_ACP24_PROTOCOL,            // Stiebel Eltron ACP24 air conditioner
	IRMP_TECHNICS_PROTOCOL,         // Technics, similar to Matsushita, but 22 instead of 24 bits
	IRMP_PANASONIC_PROTOCOL,        // Panasonic (Beamer), start bits similar to KASEIKYO
	IRMP_MITSU_HEAVY_PROTOCOL,      // Mitsubishi-Heavy Aircondition, similar timing as Panasonic beamer
	IRMP_VINCENT_PROTOCOL,          // Vincent
	IRMP_SAMSUNGAH_PROTOCOL,        // SAMSUNG AH
	IRMP_IRMP16_PROTOCOL,           // IRMP specific protocol for data transfer, e.g. between two microcontrollers via IR
	IRMP_GREE_PROTOCOL,             // Gree climate
	IRMP_RCII_PROTOCOL,             // RC II Infra Red Remote Control Protocol for FM8
	IRMP_METZ_PROTOCOL,     	    // METZ
	IRMP_ONKYO_PROTOCOL,

	RF_GEN24_PROTOCOL,  	        // RF Generic, 24 Bits (Pollin 550666, EAN 4049702006022 and many other similar RF remote controls))
	RF_X10_PROTOCOL,	            // RF PC X10 Remote Control (Medion, Pollin 721815)
	RF_MEDION_PROTOCOL,             // RF PC Medion Remote Control (Medion)

	IRMP_MELINERA_PROTOCOL,

// Add on
	IRMP_NEC8_PROTOCOL,             // NEC8
	IRMP_RCA_PROTOCOL,				// RCA
	IRMP_PIONEER_PROTOCOL,			// Pioneer
	IRMP_SIRCS15_PROTOCOL,	        // Sony
	IRMP_SIRCS20_PROTOCOL,	        // Sony
	IRMP_RC5X_PROTOCOL,             // RC5 extended
	IRMP_RAW_PROTOCOL              // Protocol with raw data, must be at the end of the list
} S_M1_IR_Protocol_Def_t;

#define IRMP_N_PROTOCOLS           	IRMP_RAW_PROTOCOL // Number of supported protocols

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * timing constants:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
// fm 229.2011: may not be more than 16000L, otherwise some JVC codes will not be accepted
#define IRMP_TIMEOUT_TIME                       15500                  // timeout after 15.5 ms darkness
#define IRMP_TIMEOUT_TIME_MS                    15500L                      // timeout after 15.5 ms darkness

#if IRMP_SUPPORT_NIKON_PROTOCOL == 1
#  define IRMP_TIMEOUT_NIKON_TIME               29500                  // 2nd timeout after 29.5 ms darkness (only for NIKON!)
#  define IRMP_TIMEOUT_NIKON_TIME_MS            29500L                      // 2nd timeout after 29.5 ms darkness
typedef uint16_t    PAUSE_LEN;
#  define IRMP_TIMEOUT_NIKON_LEN                (PAUSE_LEN)(IRMP_TIMEOUT_NIKON_TIME)
#else
#  if (IRMP_TIMEOUT_TIME_MS) / 1000000 >= 254
typedef uint16_t    PAUSE_LEN;
#  else
typedef uint16_t     PAUSE_LEN;
#  endif
#endif

#define IRMP_TIMEOUT_LEN                        (PAUSE_LEN)(IRMP_TIMEOUT_TIME)

#define IRMP_KEY_RELEASE_TIME                   25000                  // key release timeout detection after 25 ms darkness
#define IRMP_KEY_RELEASE_LEN                    (uint16_t)(IRMP_KEY_RELEASE_TIME)

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * flags of struct IRMP_PARAMETER:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define IRMP_PARAM_FLAG_IS_MANCHESTER           0x01
#define IRMP_PARAM_FLAG_1ST_PULSE_IS_1          0x02
#define IRMP_PARAM_FLAG_IS_SERIAL               0x04

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SIRCS:
 *
 * Sony SIRC Protocol
 * 12-bit version, 7 command bits, 5 address bits.
 * 15-bit version, 7 command bits, 8 address bits.
 * 20-bit version, 7 command bits, 5 address bits, 8 extended bits.
 * Pulse width modulation.
 * Carrier frequency of 40kHz.
 * Bit time of 1.2ms or 0.6ms.
 *
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#define SIRCS_START_BIT_PULSE_TIME              2400                       // 2400 usec pulse
#define SIRCS_START_BIT_PAUSE_TIME              600                       //  600 usec pause
#define SIRCS_1_PULSE_TIME                      1200                       // 1200 usec pulse
#define SIRCS_0_PULSE_TIME                      600                       //  600 usec pulse
#define SIRCS_PAUSE_TIME                        600                       //  600 usec pause
#define SIRCS_FRAMES                            3                               // SIRCS sends each frame 3 times
#define SIRCS_AUTO_REPETITION_PAUSE_TIME        25000                       // auto repetition after 25ms
#define SIRCS_FRAME_REPEAT_PAUSE_TIME           25000                       // frame repeat after 25ms
#define SIRCS_ADDRESS_OFFSET                    15                              // skip 15 bits
#define SIRCS_ADDRESS_LEN                       5                               // read up to 5 address bits
#define SIRCS_COMMAND_OFFSET                    0                               // skip 0 bits
#define SIRCS_COMMAND_LEN                       15                              // read 12-15 command bits
#define SIRCS_MINIMUM_DATA_LEN                  12                              // minimum data length
#define SIRCS_COMPLETE_DATA_LEN                 20                              // complete length - may be up to 20
#define SIRCS_STOP_BIT                          0                               // has no stop bit
#define SIRCS_LSB                               1                               // LSB...MSB
#define SIRCS_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
+ * NEC & NEC42 & NEC16 & LGAIR & MELINERA:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define NEC_START_BIT_PULSE_TIME                9000                       // 9000 usec pulse
#define NEC_START_BIT_PAUSE_TIME                4500                       // 4500 usec pause
#define NEC_REPEAT_START_BIT_PAUSE_TIME         2250                       // 2250 usec pause
#define NEC_PULSE_TIME                          560                       //  560 usec pulse
#define NEC_1_PAUSE_TIME                        1690                       // 1690 usec pause
#define NEC_0_PAUSE_TIME                        560                       //  560 usec pause
#define NEC_FRAME_REPEAT_PAUSE_TIME				40000                       // frame repeat after 40ms
#define NEC_ADDRESS_OFFSET                      0                              // skip 0 bits
#define NEC_ADDRESS_LEN                         16                              // read 16 address bits
#define NEC_COMMAND_OFFSET                      16                              // skip 16 bits (8 address + 8 /address)
#define NEC_COMMAND_LEN                         16                              // read 16 bits (8 command + 8 /command)
#define NEC_COMPLETE_DATA_LEN                   32                              // complete length
#define NEC_STOP_BIT                            1                               // has stop bit
#define NEC_LSB                                 1                               // LSB...MSB
#define NEC_FLAGS                               0                               // flags

#define NEC42_ADDRESS_OFFSET                    0                               // skip 0 bits
#define NEC42_ADDRESS_LEN                      13                               // read 13 address bits
#define NEC42_COMMAND_OFFSET                   26                               // skip 26 bits (2 x 13 address bits)
#define NEC42_COMMAND_LEN                       8                               // read 8 command bits
#define NEC42_COMPLETE_DATA_LEN                42                               // complete length (2 x 13 + 2 x 8)

#define LGAIR_ADDRESS_OFFSET                    0                               // skip 0 bits
#define LGAIR_ADDRESS_LEN                       8                               // read 8 address bits
#define LGAIR_COMMAND_OFFSET                    8                               // skip 8 bits (8 address)
#define LGAIR_COMMAND_LEN                      16                               // read 16 bits (16 command)
#define LGAIR_COMPLETE_DATA_LEN                28                               // complete length (8 address + 16 command + 4 checksum)

#define NEC16_ADDRESS_OFFSET                    0                               // skip 0 bits
#define NEC16_ADDRESS_LEN                       8                               // read 8 address bits
#define NEC16_COMMAND_OFFSET                    8                               // skip 8 bits (8 address)
#define NEC16_COMMAND_LEN                       8                               // read 8 bits (8 command)
#define NEC16_COMPLETE_DATA_LEN                 16                              // complete length

#define MELINERA_START_BIT_PULSE_TIME           8800                       // 8800 usec pulse
#define MELINERA_START_BIT_PAUSE_TIME           4100                       // 4100 usec pause
#define MELINERA_0_PULSE_TIME                    440                       //  430 usec pause
#define MELINERA_0_PAUSE_TIME                    590                       //  600 usec pause
#define MELINERA_1_PULSE_TIME                    970                       //  910 usec pause
#define MELINERA_1_PAUSE_TIME                   1140                       // 1160 usec pause
#define MELINERA_FRAME_REPEAT_PAUSE_TIME        40000                       // frame repeat after 40ms
#define MELINERA_ADDRESS_OFFSET                 0                               // skip 0 bits
#define MELINERA_ADDRESS_LEN                    0                               // read 0 address bits
#define MELINERA_COMMAND_OFFSET                 0                               // skip 0 bits (0 address)
#define MELINERA_COMMAND_LEN                    8                               // read 8 bits (8 command)
#define MELINERA_COMPLETE_DATA_LEN              8                               // complete length
#define MELINERA_STOP_BIT                       1                               // has stop bit
#define MELINERA_LSB                            0                               // MSB
#define MELINERA_FLAGS                          0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SAMSUNG & SAMSUNG32 & SAMSUNG48:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define SAMSUNG_START_BIT_PULSE_TIME            4500                       // 4500 usec pulse
#define SAMSUNG_START_BIT_PAUSE_TIME            4500                       // 4500 usec pause
#define SAMSUNG_PULSE_TIME                       550                       //  550 usec pulse
#define SAMSUNG_1_PAUSE_TIME                    1650                       // 1550 usec pause //1650
#define SAMSUNG_0_PAUSE_TIME                     550                       //  500 usec pause //550

#define SAMSUNG_FRAME_REPEAT_PAUSE_TIME         25000                       // frame repeat after 25ms
#define SAMSUNG_ADDRESS_OFFSET                   0                              // skip 0 bits
#define SAMSUNG_ADDRESS_LEN                     16                              // read 16 address bits
#define SAMSUNG_ID_OFFSET                       17                              // skip 16 + 1 sync bit
#define SAMSUNG_ID_LEN                          4                               // read 4 id bits
#define SAMSUNG_COMMAND_OFFSET                  21                              // skip 16 + 1 sync + 4 data bits
#define SAMSUNG_COMMAND_LEN                     16                              // read 16 command bits
#define SAMSUNG_COMPLETE_DATA_LEN               37                              // complete length
#define SAMSUNG_STOP_BIT                        1                               // has stop bit
#define SAMSUNG_LSB                             1                               // LSB...MSB?
#define SAMSUNG_FLAGS                           0                               // flags

#define SAMSUNG32_COMMAND_OFFSET                16                              // skip 16 bits
#define SAMSUNG32_COMMAND_LEN                   16                              // read 16 command bits
#define SAMSUNG32_COMPLETE_DATA_LEN             32                              // complete length
#define SAMSUNG32_FRAMES                        1                               // SAMSUNG32 sends one frame
#define SAMSUNG32_AUTO_REPETITION_PAUSE_TIME    47000                         // repetition after 47 ms
#define SAMSUNG32_FRAME_REPEAT_PAUSE_TIME       47000                         // frame repeat after 47ms

#define SAMSUNG48_COMMAND_OFFSET                16                              // skip 16 bits
#define SAMSUNG48_COMMAND_LEN                   32                              // read 32 command bits
#define SAMSUNG48_COMPLETE_DATA_LEN             48                              // complete length
#define SAMSUNG48_FRAMES                        2                               // SAMSUNG48 sends each frame 2 times
#define SAMSUNG48_AUTO_REPETITION_PAUSE_TIME    5000                          // repetition after 5 ms
#define SAMSUNG48_FRAME_REPEAT_PAUSE_TIME       47000                         // frame repeat after 47ms

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SAMSUNGAH:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define SAMSUNGAH_START_BIT_PULSE_TIME          2500                       // 2500 usec pulse
#define SAMSUNGAH_START_BIT_PAUSE_TIME          1900                       // 1900 usec pause
#define SAMSUNGAH_PULSE_TIME                     450                       //  450 usec pulse
#define SAMSUNGAH_1_PAUSE_TIME                  1100                       // 1100 usec pause
#define SAMSUNGAH_0_PAUSE_TIME                   450                       //  450 usec pause
#define SAMSUNGAH_FRAME_REPEAT_PAUSE_TIME       40000                       // frame repeat after 40ms
#define SAMSUNGAH_ADDRESS_OFFSET                 0                              // skip 0 bits
#define SAMSUNGAH_ADDRESS_LEN                   16                              // read 16 address bits, ignore 17..31
#define SAMSUNGAH_COMMAND_OFFSET                32                              // skip 32 bits
#define SAMSUNGAH_COMMAND_LEN                   16                              // read 32 bits
#define SAMSUNGAH_COMPLETE_DATA_LEN             48                              // complete length
#define SAMSUNGAH_STOP_BIT                      1                               // has stop bit
#define SAMSUNGAH_LSB                           1                               // LSB...MSB?
#define SAMSUNGAH_FLAGS                         0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MATSUSHITA:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MATSUSHITA_START_BIT_PULSE_TIME         3488                       // 3488 usec pulse
#define MATSUSHITA_START_BIT_PAUSE_TIME         3488                       // 3488 usec pause
#define MATSUSHITA_PULSE_TIME                    872                       //  872 usec pulse
#define MATSUSHITA_1_PAUSE_TIME                 2616                       // 2616 usec pause
#define MATSUSHITA_0_PAUSE_TIME                  872                       //  872 usec pause
#define MATSUSHITA_FRAME_REPEAT_PAUSE_TIME      40000                       // frame repeat after 40ms
#define MATSUSHITA_ADDRESS_OFFSET               12                              // skip 12 bits
#define MATSUSHITA_ADDRESS_LEN                  12                              // read 12 address bits
#define MATSUSHITA_COMMAND_OFFSET               0                               // skip 0 bits
#define MATSUSHITA_COMMAND_LEN                  12                              // read 12 bits (6 custom + 6 command)
#define MATSUSHITA_COMPLETE_DATA_LEN            24                              // complete length
#define MATSUSHITA_STOP_BIT                     1                               // has stop bit
#define MATSUSHITA_LSB                          1                               // LSB...MSB?
#define MATSUSHITA_FLAGS                        0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * TECHNICS: same timings as MATSUSHITA
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define TECHNICS_ADDRESS_LEN                    0                               // read 0 address bits
#define TECHNICS_COMMAND_LEN                    11                              // read 11 bits
#define TECHNICS_COMPLETE_DATA_LEN              22                              // complete length

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * KASEIKYO:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define KASEIKYO_START_BIT_PULSE_TIME           3380                       // 3380 usec pulse
#define KASEIKYO_START_BIT_PAUSE_TIME           1690                       // 1690 usec pause
#define KASEIKYO_PULSE_TIME                      423                       //  525 usec pulse
#define KASEIKYO_1_PAUSE_TIME                   1269                       //  525 usec pause
#define KASEIKYO_0_PAUSE_TIME                    423                       // 1690 usec pause
#define KASEIKYO_AUTO_REPETITION_PAUSE_TIME     74000                       // repetition after 74 ms
#define KASEIKYO_FRAME_REPEAT_PAUSE_TIME        74000                       // frame repeat after 74 ms
#define KASEIKYO_ADDRESS_OFFSET                  0                              // skip 0 bits
#define KASEIKYO_ADDRESS_LEN                    16                              // read 16 address bits
#define KASEIKYO_COMMAND_OFFSET                 28                              // skip 28 bits (16 manufacturer & 4 parity & 8 genre)
#define KASEIKYO_COMMAND_LEN                    12                              // read 12 command bits (10 real command & 2 id)
#define KASEIKYO_COMPLETE_DATA_LEN              48                              // complete length
#define KASEIKYO_STOP_BIT                       1                               // has stop bit
#define KASEIKYO_LSB                            1                               // LSB...MSB?
#define KASEIKYO_FRAMES                         1                               // KASEIKYO sends 1 frame
#define KASEIKYO_FLAGS                          0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PANASONIC (Beamer), start bit timings similar to KASEIKYO
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define PANASONIC_START_BIT_PULSE_TIME           3600                      // 3600 usec pulse
#define PANASONIC_START_BIT_PAUSE_TIME           1600                      // 1690 usec pause
#define PANASONIC_PULSE_TIME                      565                      //  565 usec pulse
#define PANASONIC_1_PAUSE_TIME                   1140                      // 1140 usec pause
#define PANASONIC_0_PAUSE_TIME                    316                      //  316 usec pause
#define PANASONIC_AUTO_REPETITION_PAUSE_TIME     40000                      // repetition after 40 ms?
#define PANASONIC_FRAME_REPEAT_PAUSE_TIME        40000                      // frame repeat after 40 ms
#define PANASONIC_ADDRESS_OFFSET                 24                             // skip 24 bits: 010000000000010000000001
#define PANASONIC_ADDRESS_LEN                    16                             // read 16 address bits
#define PANASONIC_COMMAND_OFFSET                 40                             // skip 40 bits
#define PANASONIC_COMMAND_LEN                    16                             // read 16 command bits
#define PANASONIC_COMPLETE_DATA_LEN              56                             // complete length
#define PANASONIC_STOP_BIT                       1                              // has stop bit
#define PANASONIC_LSB                            1                              // LSB...MSB?
#define PANASONIC_FRAMES                         1                              // PANASONIC sends 1 frame
#define PANASONIC_FLAGS                          0                              // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MITSUBISHI-Heavy Aircondition, timings similar to PANASONIC beamer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MITSU_HEAVY_START_BIT_PULSE_TIME          3200                     // 3600 usec pulse
#define MITSU_HEAVY_START_BIT_PAUSE_TIME          1560                     // 1690 usec pause
#define MITSU_HEAVY_PULSE_TIME                     400                     //  565 usec pulse
#define MITSU_HEAVY_1_PAUSE_TIME                  1200                     // 1140 usec pause
#define MITSU_HEAVY_0_PAUSE_TIME                   430                     //  316 usec pause
#define MITSU_HEAVY_FRAME_REPEAT_PAUSE_TIME       40000                     // frame repeat after 40 ms
#define MITSU_HEAVY_ADDRESS_OFFSET                 40                           // skip 24 bits: 010000000000010000000001
#define MITSU_HEAVY_ADDRESS_LEN                    16                           // read 16 address bits
#define MITSU_HEAVY_COMMAND_OFFSET                 56                           // skip 40 bits
#define MITSU_HEAVY_COMMAND_LEN                    16                           // read 16 command bits
#define MITSU_HEAVY_COMPLETE_DATA_LEN              88                           // complete length
#define MITSU_HEAVY_STOP_BIT                       1                            // has stop bit
#define MITSU_HEAVY_LSB                            0                            // LSB...MSB?
#define MITSU_HEAVY_FRAMES                         1                            // PANASONIC sends 1 frame
#define MITSU_HEAVY_FLAGS                          0                            // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * VINCENT
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define VINCENT_START_BIT_PULSE_TIME            2500                       // 2500 usec pulse
#define VINCENT_START_BIT_PAUSE_TIME            4600                       // 4600 usec pause
#define VINCENT_PULSE_TIME                       550                       //  550 usec pulse
#define VINCENT_1_PAUSE_TIME                    1540                       // 1540 usec pause
#define VINCENT_0_PAUSE_TIME                     550                       //  550 usec pause
#define VINCENT_FRAME_REPEAT_PAUSE_TIME         40000                       // frame repeat after 40 ms ?
#define VINCENT_ADDRESS_OFFSET                     0                            // skip 0 bits
#define VINCENT_ADDRESS_LEN                       16                            // read 16 address bits
#define VINCENT_COMMAND_OFFSET                    16                            // skip 16 bits
#define VINCENT_COMMAND_LEN                       16                            // read 16 command bits
#define VINCENT_COMPLETE_DATA_LEN                 32                            // complete length
#define VINCENT_STOP_BIT                           1                            // has stop bit
#define VINCENT_LSB                                0                            // LSB...MSB?
#define VINCENT_FRAMES                             1                            // VINCENT sends 1 frame
#define VINCENT_FLAGS                              0                            // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RECS80:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RECS80_START_BIT_PULSE_TIME              158                       //  158 usec pulse
#define RECS80_START_BIT_PAUSE_TIME             7432                       // 7432 usec pause
#define RECS80_PULSE_TIME                        158                       //  158 usec pulse
#define RECS80_1_PAUSE_TIME                     7432                       // 7432 usec pause
#define RECS80_0_PAUSE_TIME                     4902                       // 4902 usec pause
#define RECS80_FRAME_REPEAT_PAUSE_TIME          45000                       // frame repeat after 45ms
#define RECS80_ADDRESS_OFFSET                   1                               // skip 1 bit (toggle bit)
#define RECS80_ADDRESS_LEN                      3                               // read 3 address bits
#define RECS80_COMMAND_OFFSET                   4                               // skip 4 bits (1 toggle + 3 address)
#define RECS80_COMMAND_LEN                      6                               // read 6 command bits
#define RECS80_COMPLETE_DATA_LEN                10                              // complete length
#define RECS80_STOP_BIT                         1                               // has stop bit
#define RECS80_LSB                              0                               // MSB...LSB
#define RECS80_FLAGS                            0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RC5:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RC5_BIT_TIME                             889                       // 889 usec pulse/pause
#define RC5_FRAME_REPEAT_PAUSE_TIME              88900                       // frame repeat after 88.9ms

#define RC5_ADDRESS_OFFSET                      1                               // skip 1 bit (2nd start)
#define RC5_ADDRESS_LEN                         6                               // read 1 toggle bit (for key repetition detection) + 5 address bits
#define RC5_COMMAND_OFFSET                      7                               // skip 5 bits (2nd start + 1 toggle + 5 address)
#define RC5_COMMAND_LEN                         6                               // read 6 command bits
#define RC5_COMPLETE_DATA_LEN                   13                              // complete length
#define RC5_STOP_BIT                            0                               // has no stop bit
#define RC5_LSB                                 0                               // MSB...LSB
#define RC5_FLAGS                               IRMP_PARAM_FLAG_IS_MANCHESTER   // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RCII:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RCII_START_BIT_PULSE_TIME                512                       //  512 usec pulse
#define RCII_START_BIT_PAUSE_TIME               2560                       // 2560 usec pause
#define RCII_START_BIT2_PULSE_TIME              1024                       // 1024 usec pulse

#define RCII_BIT_TIME                            512                       // 512 usec pulse/pause
#define RCII_FRAME_REPEAT_PAUSE_TIME            117760                      // frame repeat after 117.76ms

#define RCII_ADDRESS_OFFSET                     0                               // skip 1 bit (2nd start)
#define RCII_ADDRESS_LEN                        0                               // no address
#define RCII_COMMAND_OFFSET                     0                               // command offset is 0
#define RCII_COMMAND_LEN                        10                              // read 1 + 9 command bits
#define RCII_COMPLETE_DATA_LEN                  10                              // complete length
#define RCII_STOP_BIT                           0                               // has no stop bit
#define RCII_LSB                                0                               // MSB...LSB
#define RCII_FLAGS                              (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1)  // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * S100: very similar to RC5, but 14 insted of 13 bits
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define S100_BIT_TIME                             889                       // 889 usec pulse/pause
#define S100_FRAME_REPEAT_PAUSE_TIME              88900                       // frame repeat after 88.9ms

#define S100_ADDRESS_OFFSET                      1                               // skip 1 bit (2nd start)
#define S100_ADDRESS_LEN                         6                               // read 1 toggle bit (for key repetition detection) + 5 address bits
#define S100_COMMAND_OFFSET                      7                               // skip 5 bits (2nd start + 1 toggle + 5 address)
#define S100_COMMAND_LEN                         7                               // read 7 command bits
#define S100_COMPLETE_DATA_LEN                   14                              // complete length
#define S100_STOP_BIT                            0                               // has no stop bit
#define S100_LSB                                 0                               // MSB...LSB
#define S100_FLAGS                               IRMP_PARAM_FLAG_IS_MANCHESTER   // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * DENON:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define DENON_PULSE_TIME                         310                       //  310 usec pulse in practice,  275 in theory
#define DENON_1_PAUSE_TIME                      1780                       // 1780 usec pause in practice, 1900 in theory
#define DENON_0_PAUSE_TIME                       745                       //  745 usec pause in practice,  775 in theory
#define DENON_FRAMES                            2                               // DENON sends each frame 2 times
#define DENON_AUTO_REPETITION_PAUSE_TIME        45000                       // inverted repetition after 45ms
#define DENON_FRAME_REPEAT_PAUSE_TIME           45000                       // frame repeat after 45ms
#define DENON_ADDRESS_OFFSET                    0                               // skip 0 bits
#define DENON_ADDRESS_LEN                       5                               // read 5 address bits
#define DENON_COMMAND_OFFSET                    5                               // skip 5
#define DENON_COMMAND_LEN                       10                              // read 10 command bits
#define DENON_COMPLETE_DATA_LEN                 15                              // complete length
#define DENON_STOP_BIT                          1                               // has stop bit
#define DENON_LSB                               0                               // MSB...LSB
#define DENON_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RC6:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RC6_START_BIT_PULSE_TIME                2666                       // 2.666 msec pulse
#define RC6_START_BIT_PAUSE_TIME                 889                       // 889 usec pause
#define RC6_TOGGLE_BIT_TIME                      889                       // 889 msec pulse/pause
#define RC6_BIT_TIME                             444                       // 444 usec pulse/pause
#define RC6_BIT_2_TIME                           889                       // 889 usec pulse/pause
#define RC6_BIT_3_TIME                          1333                       // 1333 usec pulse/pause
#define RC6_FRAME_REPEAT_PAUSE_TIME             45000                       // frame repeat after 45ms
#define RC6_ADDRESS_OFFSET                      5                               // skip "1" + 3 mode bits + 1 toggle bit
#define RC6_ADDRESS_LEN                         8                               // read 8 address bits
#define RC6_COMMAND_OFFSET                      13                              // skip 12 bits ("1" + 3 mode + 1 toggle + 8 address)
#define RC6_COMMAND_LEN                         8                               // read 8 command bits
#define RC6_COMPLETE_DATA_LEN_SHORT             21                              // complete length
#define RC6_COMPLETE_DATA_LEN_LONG              36                              // complete length
#define RC6_STOP_BIT                            0                               // has no stop bit
#define RC6_LSB                                 0                               // MSB...LSB
#define RC6_FLAGS                               (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1)   // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RECS80EXT:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RECS80EXT_START_BIT_PULSE_TIME           158                       //  158 usec pulse
#define RECS80EXT_START_BIT_PAUSE_TIME          3637                       // 3637 usec pause
#define RECS80EXT_PULSE_TIME                     158                       //  158 usec pulse
#define RECS80EXT_1_PAUSE_TIME                  7432                       // 7432 usec pause
#define RECS80EXT_0_PAUSE_TIME                  4902                       // 4902 usec pause
#define RECS80EXT_FRAME_REPEAT_PAUSE_TIME       45000                       // frame repeat after 45ms
#define RECS80EXT_ADDRESS_OFFSET                2                               // skip 2 bits (2nd start + 1 toggle)
#define RECS80EXT_ADDRESS_LEN                   4                               // read 4 address bits
#define RECS80EXT_COMMAND_OFFSET                6                               // skip 6 bits (2nd start + 1 toggle + 4 address)
#define RECS80EXT_COMMAND_LEN                   6                               // read 6 command bits
#define RECS80EXT_COMPLETE_DATA_LEN             12                              // complete length
#define RECS80EXT_STOP_BIT                      1                               // has stop bit
#define RECS80EXT_LSB                           0                               // MSB...LSB
#define RECS80EXT_FLAGS                         0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * NUBERT:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define NUBERT_START_BIT_PULSE_TIME             1340                       // 1340 usec pulse
#define NUBERT_START_BIT_PAUSE_TIME              340                       //  340 usec pause
#define NUBERT_1_PULSE_TIME                     1340                       // 1340 usec pulse
#define NUBERT_1_PAUSE_TIME                      340                       //  340 usec pause
#define NUBERT_0_PULSE_TIME                      500                       //  500 usec pulse
#define NUBERT_0_PAUSE_TIME                     1300                       // 1300 usec pause
#define NUBERT_FRAMES                           2                               // Nubert sends 2 frames
#define NUBERT_AUTO_REPETITION_PAUSE_TIME       35000                       // auto repetition after 35ms
#define NUBERT_FRAME_REPEAT_PAUSE_TIME          35000                       // frame repeat after 45ms
#define NUBERT_ADDRESS_OFFSET                   0                               // skip 0 bits
#define NUBERT_ADDRESS_LEN                      0                               // read 0 address bits
#define NUBERT_COMMAND_OFFSET                   0                               // skip 0 bits
#define NUBERT_COMMAND_LEN                      10                              // read 10 bits
#define NUBERT_COMPLETE_DATA_LEN                10                              // complete length
#define NUBERT_STOP_BIT                         1                               // has stop bit
#define NUBERT_LSB                              0                               // MSB?
#define NUBERT_FLAGS                            0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * FAN: (ventilator)
 *
 * Similar to NUBERT, but
 *   - has data bit instead of stop bit
 *   - has NO frame repetition
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define FAN_START_BIT_PULSE_TIME                1280                       // 1280 usec pulse
#define FAN_START_BIT_PAUSE_TIME                 380                       //  380 usec pause
#define FAN_1_PULSE_TIME                        1280                       // 1280 usec pulse
#define FAN_1_PAUSE_TIME                         380                       //  380 usec pause
#define FAN_0_PULSE_TIME                         380                       //  380 usec pulse
#define FAN_0_PAUSE_TIME                        1280                       // 1280 usec pause
#define FAN_FRAMES                              1                               // FAN sends only 1 frame (NUBERT sends 2)
#define FAN_AUTO_REPETITION_PAUSE_TIME          6600                        // auto repetition after 6.6ms
#define FAN_FRAME_REPEAT_PAUSE_TIME             6600                        // frame repeat after 6.6ms
#define FAN_ADDRESS_OFFSET                      0                               // skip 0 bits
#define FAN_ADDRESS_LEN                         0                               // read 0 address bits
#define FAN_COMMAND_OFFSET                      0                               // skip 0 bits
#define FAN_COMMAND_LEN                         11                              // read 10 bits
#define FAN_COMPLETE_DATA_LEN                   11                              // complete length
#define FAN_STOP_BIT                            0                               // has NO stop bit (fm: this seems to be wrong)
#define FAN_LSB                                 0                               // MSB
#define FAN_FLAGS                               0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SPEAKER:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define SPEAKER_START_BIT_PULSE_TIME             440                       //  440 usec pulse
#define SPEAKER_START_BIT_PAUSE_TIME            1250                       // 1250 usec pause
#define SPEAKER_1_PULSE_TIME                    1250                       // 1250 usec pulse
#define SPEAKER_1_PAUSE_TIME                     440                       //  440 usec pause
#define SPEAKER_0_PULSE_TIME                     440                       //  440 usec pulse
#define SPEAKER_0_PAUSE_TIME                    1250                       // 1250 usec pause
#define SPEAKER_FRAMES                          2                               // SPEAKER sends 2 frames
#define SPEAKER_AUTO_REPETITION_PAUSE_TIME      35000                       // auto repetition after 35ms
#define SPEAKER_FRAME_REPEAT_PAUSE_TIME         35000                       // frame repeat after 45ms
#define SPEAKER_ADDRESS_OFFSET                  0                               // skip 0 bits
#define SPEAKER_ADDRESS_LEN                     0                               // read 0 address bits
#define SPEAKER_COMMAND_OFFSET                  0                               // skip 0 bits
#define SPEAKER_COMMAND_LEN                     10                              // read 10 bits
#define SPEAKER_COMPLETE_DATA_LEN               10                              // complete length
#define SPEAKER_STOP_BIT                        1                               // has stop bit
#define SPEAKER_LSB                             0                               // MSB?
#define SPEAKER_FLAGS                           0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * BANG_OLUFSEN:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define BANG_OLUFSEN_START_BIT1_PULSE_TIME       200                       //   200 usec pulse
#define BANG_OLUFSEN_START_BIT1_PAUSE_TIME      3125                       //  3125 usec pause
#define BANG_OLUFSEN_START_BIT2_PULSE_TIME       200                       //   200 usec pulse
#define BANG_OLUFSEN_START_BIT2_PAUSE_TIME      3125                       //  3125 usec pause
#define BANG_OLUFSEN_START_BIT3_PULSE_TIME       200                       //   200 usec pulse
#define BANG_OLUFSEN_START_BIT3_PAUSE_TIME     15625                       // 15625 usec pause
#define BANG_OLUFSEN_START_BIT4_PULSE_TIME       200                       //   200 usec pulse
#define BANG_OLUFSEN_START_BIT4_PAUSE_TIME      3125                       //  3125 usec pause
#define BANG_OLUFSEN_PULSE_TIME                  200                       //   200 usec pulse
#define BANG_OLUFSEN_1_PAUSE_TIME               9375                       //  9375 usec pause
#define BANG_OLUFSEN_0_PAUSE_TIME               3125                       //  3125 usec pause
#define BANG_OLUFSEN_R_PAUSE_TIME               6250                       //  6250 usec pause (repeat last bit)
#define BANG_OLUFSEN_TRAILER_BIT_PAUSE_TIME    12500                       // 12500 usec pause (trailer bit)
#define BANG_OLUFSEN_FRAME_REPEAT_PAUSE_TIME   45000                       // frame repeat after 45ms
#define BANG_OLUFSEN_ADDRESS_OFFSET             0                               // no address bits
#define BANG_OLUFSEN_ADDRESS_LEN                0                               // no address bits
#define BANG_OLUFSEN_COMMAND_OFFSET             3                               // skip startbits 2, 3, 4
#define BANG_OLUFSEN_COMMAND_LEN                16                              // read 16 command bits
#define BANG_OLUFSEN_COMPLETE_DATA_LEN          20                              // complete length: startbits 2, 3, 4 + 16 data bits + trailer bit
#define BANG_OLUFSEN_STOP_BIT                   1                               // has stop bit
#define BANG_OLUFSEN_LSB                        0                               // MSB...LSB
#define BANG_OLUFSEN_FLAGS                      0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * GRUNDIG & NOKIA
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define GRUNDIG_NOKIA_IR60_BIT_TIME             528                        // 528 usec pulse/pause
#define GRUNDIG_NOKIA_IR60_PRE_PAUSE_TIME       2639                       // 2639 usec pause after pre bit
#define GRUNDIG_NOKIA_IR60_FRAME_REPEAT_PAUSE_TIME  117760                   // info frame repeat after 117.76 ms
#define GRUNDIG_NOKIA_IR60_STOP_BIT             0                               // has no stop bit
#define GRUNDIG_NOKIA_IR60_LSB                  1                               // MSB...LSB
#define GRUNDIG_NOKIA_IR60_FLAGS                (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1)  // flags

#define GRUNDIG_FRAMES                          2                               // GRUNDIG sends each frame 1+1 times
#define GRUNDIG_AUTO_REPETITION_PAUSE_TIME      20000                         // repetition after 20ms
#define GRUNDIG_ADDRESS_OFFSET                  0                               // no address
#define GRUNDIG_ADDRESS_LEN                     0                               // no address
#define GRUNDIG_COMMAND_OFFSET                  1                               // skip 1 start bit
#define GRUNDIG_COMMAND_LEN                     9                               // read 9 command bits
#define GRUNDIG_COMPLETE_DATA_LEN               10                              // complete length: 1 start bit + 9 data bits

#define NOKIA_FRAMES                            3                               // NOKIA sends each frame 1 + 1 + 1 times
#define NOKIA_AUTO_REPETITION_PAUSE_TIME        20000                         // repetition after 20ms
#define NOKIA_ADDRESS_OFFSET                    9                               // skip 9 bits (1 start bit + 8 data bits)
#define NOKIA_ADDRESS_LEN                       8                               // 7 address bits
#define NOKIA_COMMAND_OFFSET                    1                               // skip 1 bit (1 start bit)
#define NOKIA_COMMAND_LEN                       8                               // read 8 command bits
#define NOKIA_COMPLETE_DATA_LEN                 17                              // complete length: 1 start bit + 8 address bits + 8 command bits

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * IR60:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define IR60_FRAMES                             2                               // IR60 sends each frame 1+1 times
#define IR60_AUTO_REPETITION_PAUSE_TIME         22200                         // repetition after 22.2ms
#define IR60_TIMEOUT_TIME                       5000                       // timeout grundig frame, switch to IR60
#define IR60_ADDRESS_OFFSET                     0                               // skip 1 bits
#define IR60_ADDRESS_LEN                        0                               // read 0 address bits
#define IR60_COMMAND_OFFSET                     0                               // skip 1 bit (start bit after pre bit, always 1)
#define IR60_COMMAND_LEN                        7                               // read 6 command bits
#define IR60_COMPLETE_DATA_LEN                  7                               // complete length

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SIEMENS & RUWIDO:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#define SIEMENS_OR_RUWIDO_START_BIT_PULSE_TIME  275                        //  275 usec pulse
#define SIEMENS_OR_RUWIDO_START_BIT_PAUSE_TIME  550                        //  550 usec pause
#define SIEMENS_OR_RUWIDO_BIT_PULSE_TIME        275                        //  275 usec short pulse
#define SIEMENS_OR_RUWIDO_BIT_PULSE_TIME_2      550                        //  550 usec long pulse
#define SIEMENS_OR_RUWIDO_BIT_PAUSE_TIME        275                        //  275 usec short pause
#define SIEMENS_OR_RUWIDO_BIT_PAUSE_TIME_2      550                        //  550 usec long pause

#define SIEMENS_OR_RUWIDO_FRAME_REPEAT_PAUSE_TIME 45000                       // frame repeat after 45ms
#define SIEMENS_OR_RUWIDO_STOP_BIT              0                               // has no stop bit
#define SIEMENS_OR_RUWIDO_LSB                   0                               // MSB...LSB
#define SIEMENS_OR_RUWIDO_FLAGS                 (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1)  // flags

#define RUWIDO_ADDRESS_OFFSET                   0                               // skip 0 bits
#define RUWIDO_ADDRESS_LEN                      9                               // read 9 address bits
#define RUWIDO_COMMAND_OFFSET                   9                               // skip 9 bits
#define RUWIDO_COMMAND_LEN                      8                               // read 7 + 1 command bits, last bit is only check bit
#define RUWIDO_COMPLETE_DATA_LEN                17                              // complete length

#define SIEMENS_ADDRESS_OFFSET                  0                               // skip 0 bits
#define SIEMENS_ADDRESS_LEN                     11                              // read 11 bits
#define SIEMENS_COMMAND_OFFSET                  11                              // skip 11 bits
#define SIEMENS_COMMAND_LEN                     11                              // read 10 + 1 command bits, last bit is only check bit
#define SIEMENS_COMPLETE_DATA_LEN               22                              // complete length

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * FDC:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define FDC_START_BIT_PULSE_TIME                 2085                      // 2085 usec pulse
#define FDC_START_BIT_PAUSE_TIME                  966                      //  966 usec pause
#define FDC_PULSE_TIME                            300                      //  300 usec pulse
#define FDC_1_PAUSE_TIME                          715                      //  715 usec pause
#define FDC_0_PAUSE_TIME                          220                      //  220 usec pause
#define FDC_FRAME_REPEAT_PAUSE_TIME              60000                      // frame repeat after 60ms
#define FDC_ADDRESS_OFFSET                       0                              // skip 0 bits
#define FDC_ADDRESS_LEN                         14                              // read 14 address bits, but use only 6, shift 8 into command
#define FDC_COMMAND_OFFSET                      20                              // skip 20 bits
#define FDC_COMMAND_LEN                         12                              // read 12 bits
#define FDC_COMPLETE_DATA_LEN                   40                              // complete length
#define FDC_STOP_BIT                            1                               // has stop bit
#define FDC_LSB                                 1                               // LSB...MSB
#define FDC_FLAGS                               0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RCCAR:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RCCAR_START_BIT_PULSE_TIME               2000                      // 2000 usec pulse
#define RCCAR_START_BIT_PAUSE_TIME               2000                      // 2000 usec pause
#define RCCAR_PULSE_TIME                          600                      //  360 usec pulse
#define RCCAR_1_PAUSE_TIME                        450                      //  650 usec pause
#define RCCAR_0_PAUSE_TIME                        900                      //  180 usec pause
#define RCCAR_FRAME_REPEAT_PAUSE_TIME            40000                      // frame repeat after 40ms
#define RCCAR_ADDRESS_OFFSET                     0                              // skip 0 bits
#define RCCAR_ADDRESS_LEN                        0                              // read 0 address bits
#define RCCAR_COMMAND_OFFSET                     0                              // skip 0 bits
#define RCCAR_COMMAND_LEN                       13                              // read 13 bits
#define RCCAR_COMPLETE_DATA_LEN                 13                              // complete length
#define RCCAR_STOP_BIT                          1                               // has stop bit
#define RCCAR_LSB                               1                               // LSB...MSB
#define RCCAR_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * JVC:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define JVC_START_BIT_PULSE_TIME                9000                       // 9000 usec pulse
#define JVC_START_BIT_PAUSE_TIME                4500                       // 4500 usec pause
#define JVC_PULSE_TIME                           560                       //  560 usec pulse
#define JVC_1_PAUSE_TIME                        1690                       // 1690 usec pause
#define JVC_0_PAUSE_TIME                         560                       //  560 usec pause
#define JVC_FRAME_REPEAT_PAUSE_TIME             22000                       // frame repeat after 22ms
#define JVC_ADDRESS_OFFSET                       0                              // skip 0 bits
#define JVC_ADDRESS_LEN                          4                              // read 4 address bits
#define JVC_COMMAND_OFFSET                       4                              // skip 4 bits
#define JVC_COMMAND_LEN                         12                              // read 12 bits
#define JVC_COMPLETE_DATA_LEN                   16                              // complete length
#define JVC_STOP_BIT                            1                               // has stop bit
#define JVC_LSB                                 1                               // LSB...MSB
#define JVC_FLAGS                               0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * NIKON:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define NIKON_START_BIT_PULSE_TIME              2200                       //  2200 usec pulse
#define NIKON_START_BIT_PAUSE_TIME             27100                       // 27100 usec pause
#define NIKON_PULSE_TIME                         500                       //   500 usec pulse
#define NIKON_1_PAUSE_TIME                      3500                       //  3500 usec pause
#define NIKON_0_PAUSE_TIME                      1500                       //  1500 usec pause
#define NIKON_FRAME_REPEAT_PAUSE_TIME          60000                       // frame repeat after 60ms
#define NIKON_ADDRESS_OFFSET                    0                               // skip 0 bits
#define NIKON_ADDRESS_LEN                       0                               // read 0 address bits
#define NIKON_COMMAND_OFFSET                    0                               // skip 0 bits
#define NIKON_COMMAND_LEN                       2                               // read 2 bits
#define NIKON_COMPLETE_DATA_LEN                 2                               // complete length
#define NIKON_STOP_BIT                          1                               // has stop bit
#define NIKON_LSB                               0                               // LSB...MSB
#define NIKON_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * KATHREIN:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define KATHREIN_START_BIT_PULSE_TIME            210                       // 1340 usec pulse
#define KATHREIN_START_BIT_PAUSE_TIME           6218                       //  340 usec pause
#define KATHREIN_1_PULSE_TIME                    210                       // 1340 usec pulse
#define KATHREIN_1_PAUSE_TIME                   3000                       //  340 usec pause
#define KATHREIN_0_PULSE_TIME                    210                       //  500 usec pulse
#define KATHREIN_0_PAUSE_TIME                   1400                       // 1300 usec pause
#define KATHREIN_SYNC_BIT_PAUSE_LEN_TIME        4600                       // 4600 usec sync (on 6th and/or 8th bit)
#define KATHREIN_FRAMES                         1                               // Kathrein sends 1 frame
#define KATHREIN_AUTO_REPETITION_PAUSE_TIME     35000                         // auto repetition after 35ms
#define KATHREIN_FRAME_REPEAT_PAUSE_TIME        35000                         // frame repeat after 35ms
#define KATHREIN_ADDRESS_OFFSET                 1                               // skip 1 bits
#define KATHREIN_ADDRESS_LEN                    4                               // read 4 address bits
#define KATHREIN_COMMAND_OFFSET                 5                               // skip 5 bits
#define KATHREIN_COMMAND_LEN                    7                               // read 7 bits
#define KATHREIN_COMPLETE_DATA_LEN              13                              // complete length
#define KATHREIN_STOP_BIT                       1                               // has stop bit
#define KATHREIN_LSB                            0                               // MSB
#define KATHREIN_FLAGS                          0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * NETBOX:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define NETBOX_START_BIT_PULSE_TIME             2400                       // 2400 usec pulse
#define NETBOX_START_BIT_PAUSE_TIME              800                       //  800 usec pause
#define NETBOX_PULSE_TIME                        800                       //  800 usec pulse
#define NETBOX_PAUSE_TIME                        800                       //  800 usec pause
#define NETBOX_FRAMES                           1                               // Netbox sends 1 frame
#define NETBOX_AUTO_REPETITION_PAUSE_TIME       35000                         // auto repetition after 35ms
#define NETBOX_FRAME_REPEAT_PAUSE_TIME          35000                         // frame repeat after 35ms
#define NETBOX_ADDRESS_OFFSET                   0                               // skip 0 bits
#define NETBOX_ADDRESS_LEN                      3                               // read 3 address bits
#define NETBOX_COMMAND_OFFSET                   3                               // skip 3 bits
#define NETBOX_COMMAND_LEN                      13                              // read 13 bits
#define NETBOX_COMPLETE_DATA_LEN                16                              // complete length
#define NETBOX_STOP_BIT                         0                               // has no stop bit
#define NETBOX_LSB                              1                               // LSB
#define NETBOX_FLAGS                            IRMP_PARAM_FLAG_IS_SERIAL       // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * LEGO:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define LEGO_START_BIT_PULSE_TIME                158                       //  158 usec pulse ( 6 x 1/38kHz)
#define LEGO_START_BIT_PAUSE_TIME               1026                       // 1026 usec pause (39 x 1/38kHz)
#define LEGO_PULSE_TIME                          158                       //  158 usec pulse ( 6 x 1/38kHz)
#define LEGO_1_PAUSE_TIME                        553                       //  553 usec pause (21 x 1/38kHz)
#define LEGO_0_PAUSE_TIME                        263                       //  263 usec pause (10 x 1/38kHz)
#define LEGO_FRAME_REPEAT_PAUSE_TIME            40000                       // frame repeat after 40ms
#define LEGO_ADDRESS_OFFSET                     0                               // skip 0 bits
#define LEGO_ADDRESS_LEN                        0                               // read 0 address bits
#define LEGO_COMMAND_OFFSET                     0                               // skip 0 bits
#define LEGO_COMMAND_LEN                        16                              // read 16 bits (12 command + 4 CRC)
#define LEGO_COMPLETE_DATA_LEN                  16                              // complete length
#define LEGO_STOP_BIT                           1                               // has stop bit
#define LEGO_LSB                                0                               // MSB...LSB
#define LEGO_FLAGS                              0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * THOMSON:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define THOMSON_PULSE_TIME                       550                       //  550 usec pulse
#define THOMSON_1_PAUSE_TIME                    4500                       // 4500 usec pause
#define THOMSON_0_PAUSE_TIME                    2000                       // 2000 usec pause
#define THOMSON_FRAMES                          1                               // THOMSON sends 1 frame
#define THOMSON_AUTO_REPETITION_PAUSE_TIME      35000                       // repetition after 35ms
#define THOMSON_FRAME_REPEAT_PAUSE_TIME         35000                       // frame repeat after 35ms
#define THOMSON_ADDRESS_OFFSET                  0                               // skip 0 bits
#define THOMSON_ADDRESS_LEN                     4                               // read 4 address bits
#define THOMSON_COMMAND_OFFSET                  5                               // skip 4 address bits + 1 toggle bit
#define THOMSON_COMMAND_LEN                     7                               // read 7 command bits
#define THOMSON_COMPLETE_DATA_LEN               12                              // complete length
#define THOMSON_STOP_BIT                        1                               // has stop bit
#define THOMSON_LSB                             0                               // MSB...LSB
#define THOMSON_FLAGS                           0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * BOSE:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define BOSE_START_BIT_PULSE_TIME               1060                       // 1060 usec pulse
#define BOSE_START_BIT_PAUSE_TIME               1425                       // 1425 usec pause
#define BOSE_PULSE_TIME                          550                       //  550 usec pulse
#define BOSE_1_PAUSE_TIME                       1425                       // 1425 usec pause
#define BOSE_0_PAUSE_TIME                        437                       //  437 usec pause
#define BOSE_FRAMES                             1
#define BOSE_AUTO_REPETITION_PAUSE_TIME         40000                       // repetition after 40ms?
#define BOSE_FRAME_REPEAT_PAUSE_TIME            40000                       // frame repeat after 40ms?
#define BOSE_ADDRESS_OFFSET                      0                              // skip 0 bits
#define BOSE_ADDRESS_LEN                         0                              // read 16 address bits
#define BOSE_COMMAND_OFFSET                      0                              // skip 16 bits (8 address + 8 /address)
#define BOSE_COMMAND_LEN                        16                              // read 16 bits (8 command + 8 /command)
#define BOSE_COMPLETE_DATA_LEN                  16                              // complete length
#define BOSE_STOP_BIT                           1                               // has stop bit
#define BOSE_LSB                                1                               // LSB...MSB
#define BOSE_FLAGS                              0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * A1TVBOX:
 * In reality A1 TV Box has no start bit with 300/340 usec. There are 2 start bits "10" with 250us pulse + 150us pause + 150us pause + 250us pulse
 * This is not very easy to detect, because 1st and 2nd pause of both start bits are closely spaced.
 * So IRMP looks for pseudo start bit with 300/340 usec and ignores the second half of the 2nd bit (250us pulse)
 * This method only works because the first data bit (which is the 3rd bit) following is always "1":
 * IRMP treats the first "long" pulse (250us of 2nd start bit + 250us of 1st data bit) of this "1" as a first _short_ pulse.
 * This is a bug in IRMP's manchester decoder, but a good feature here ;-)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define A1TVBOX_START_BIT_PULSE_TIME            300                        // 300 usec pulse
#define A1TVBOX_START_BIT_PAUSE_TIME            340                        // 340 usec pause
#define A1TVBOX_BIT_PULSE_TIME                  250                        // 250 usec pulse
#define A1TVBOX_BIT_PAUSE_TIME                  150                        // 150 usec pulse
#define A1TVBOX_STOP_BIT                        0                               // has no stop bit
#define A1TVBOX_LSB                             0                               // MSB...LSB
#define A1TVBOX_FLAGS                           (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1 )  // flags
#define A1TVBOX_FRAMES                          1                               // A1TVBOX sends each frame 1 times
#define A1TVBOX_ADDRESS_OFFSET                  1                               // skip 1 bits
#define A1TVBOX_ADDRESS_LEN                     8                               // read 8 address bits
#define A1TVBOX_COMMAND_OFFSET                  9                               // skip 9 bits (start bit + address)
#define A1TVBOX_COMMAND_LEN                     8                               // read 8 command bits
#define A1TVBOX_COMPLETE_DATA_LEN               17                              // complete length incl. start bit
#define A1TVBOX_FRAME_REPEAT_PAUSE_TIME         50                         // 50 msec pause between frames, don't know if it is correct

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MERLIN:
 * See notes for A1TVBOX
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MERLIN_START_BIT_PULSE_TIME            210                         // 210 usec pulse
#define MERLIN_START_BIT_PAUSE_TIME            420                         // 429 usec pause
#define MERLIN_BIT_PULSE_TIME                  210                         // 210 usec pulse
#define MERLIN_BIT_PAUSE_TIME                  210                         // 210 usec pulse
#define MERLIN_STOP_BIT                        0                                // has no stop bit
#define MERLIN_LSB                             0                                // MSB...LSB
#define MERLIN_FLAGS                           (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1 )  // flags
#define MERLIN_FRAMES                          1                                // MERLIN sends each frame 1 times
#define MERLIN_ADDRESS_OFFSET                  2                                // skip 1 bits
#define MERLIN_ADDRESS_LEN                     9                                // read 9 address bits
#define MERLIN_COMMAND_OFFSET                  11                               // skip 11 bits (start bit + address)
#define MERLIN_COMMAND_LEN                     16                               // read up to 16 command bits (could be up to 32)
#define MERLIN_COMPLETE_DATA_LEN               27                               // complete length incl. start bit
#define MERLIN_FRAME_REPEAT_PAUSE_TIME         50                          // 50 msec pause between frames, don't know if it is correct

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ORTEK (Hama): 6 address bits + 2 frame type bits + 6 command bits + 1 parity bit + 1 unknown bit + "1" + "0"
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define ORTEK_START_BIT_PULSE_TIME              2000                       // 2000 usec pulse
#define ORTEK_START_BIT_PAUSE_TIME              1000                       // 1000 usec pause
#define ORTEK_BIT_TIME                           500                       //  500 usec pulse/pause
#define ORTEK_FRAME_REPEAT_PAUSE_TIME             45                       // frame repeat after 45ms
#define ORTEK_ADDRESS_OFFSET                    0                               // skip 0 bits
#define ORTEK_ADDRESS_LEN                       8                               // read 6 address bits + 2 special bits
#define ORTEK_COMMAND_OFFSET                    8                               // skip 6 address bits + 2 special bits
#define ORTEK_COMMAND_LEN                       6                               // read 6 command bits
#define ORTEK_COMPLETE_DATA_LEN                 18                              // complete length
#define ORTEK_STOP_BIT                          0                               // has no stop bit
#define ORTEK_LSB                               0                               // MSB...LSB
#define ORTEK_FLAGS                             (IRMP_PARAM_FLAG_IS_MANCHESTER | IRMP_PARAM_FLAG_1ST_PULSE_IS_1)   // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * TELEFUNKEN:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define TELEFUNKEN_START_BIT_PULSE_TIME          600                       //  600 usec pulse
#define TELEFUNKEN_START_BIT_PAUSE_TIME         1500                       // 1500 usec pause
#define TELEFUNKEN_PULSE_TIME                    600                       //  600 usec pulse
#define TELEFUNKEN_1_PAUSE_TIME                 1500                       // 1500 usec pause
#define TELEFUNKEN_0_PAUSE_TIME                  600                       //  600 usec pause
#define TELEFUNKEN_FRAME_REPEAT_PAUSE_TIME        22                       // frame repeat after XX ms ?????
#define TELEFUNKEN_ADDRESS_OFFSET                0                              // skip 0 bits
#define TELEFUNKEN_ADDRESS_LEN                   0                              // read 0 address bits
#define TELEFUNKEN_COMMAND_OFFSET                0                              // skip 0 bits
#define TELEFUNKEN_COMMAND_LEN                  15                              // read 15 bits
#define TELEFUNKEN_COMPLETE_DATA_LEN            15                              // complete length
#define TELEFUNKEN_STOP_BIT                     1                               // has stop bit
#define TELEFUNKEN_LSB                          0                               // LSB...MSB
#define TELEFUNKEN_FLAGS                        0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ROOMBA
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define ROOMBA_START_BIT_PULSE_TIME             2790                       // 2790 usec pulse
#define ROOMBA_START_BIT_PAUSE_TIME              930                       //  930 usec pause
#define ROOMBA_0_PULSE_TIME                      930                       //  930 usec pulse
#define ROOMBA_1_PULSE_TIME                     2790                       // 2790 usec pulse
#define ROOMBA_0_PAUSE_TIME                     2790                       // 2790 usec pause
#define ROOMBA_1_PAUSE_TIME                      930                       //  930 usec pause
#define ROOMBA_FRAME_REPEAT_PAUSE_TIME          18000                       // frame repeat after 18ms
#define ROOMBA_ADDRESS_OFFSET                    0                              // skip 0 bits
#define ROOMBA_ADDRESS_LEN                       0                              // read 0 address bits
#define ROOMBA_COMMAND_OFFSET                    0                              // skip 0 bits
#define ROOMBA_COMMAND_LEN                       7                              // read 7 bits
#define ROOMBA_COMPLETE_DATA_LEN                 7                              // complete length
#define ROOMBA_STOP_BIT                         0                               // has stop bit (fm: sure?)
#define ROOMBA_LSB                              0                               // MSB...LSB
#define ROOMBA_FLAGS                            0                               // flags
#define ROOMBA_FRAMES                           8                               // ROOMBA sends 8 frames (this is a lie, but more comfortable)

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RC-MM (32, 24, or 12 bit)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RCMM32_START_BIT_PULSE_TIME              500                       // 500 usec pulse
#define RCMM32_START_BIT_PAUSE_TIME              220                       // 220 usec pause
#define RCMM32_PULSE_TIME                        230                       // 230 usec pulse
#define RCMM32_00_PAUSE_TIME                     220                       // 220 usec pause
#define RCMM32_01_PAUSE_TIME                     370                       // 370 usec pause
#define RCMM32_10_PAUSE_TIME                     540                       // 540 usec pause
#define RCMM32_11_PAUSE_TIME                     720                       // 720 usec pause

#define RCMM32_FRAME_REPEAT_PAUSE_TIME           80000                       // frame repeat after 80 ms
#define RCMM32_ADDRESS_OFFSET                    0                              // skip 0 bits
#define RCMM32_ADDRESS_LEN                      16                              //  read 16 address bits
#define RCMM32_COMMAND_OFFSET                   17                              // skip 17 bits
#define RCMM32_COMMAND_LEN                      15                              // read 15 bits
#define RCMM32_COMPLETE_DATA_LEN                32                              // complete length
#define RCMM32_STOP_BIT                         1                               // has stop bit
#define RCMM32_LSB                              0                               // LSB...MSB
#define RCMM32_FLAGS                            0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PENTAX:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define PENTAX_START_BIT_PULSE_TIME             13000                      // 13 msec pulse
#define PENTAX_START_BIT_PAUSE_TIME              3000                      // 3 msec pause
#define PENTAX_PULSE_TIME                        1000                      // 1 msec pulse
#define PENTAX_1_PAUSE_TIME                      3000                      // 3 msec pause
#define PENTAX_0_PAUSE_TIME                      1000                      // 1 msec pause
#define PENTAX_FRAME_REPEAT_PAUSE_TIME         	60000                      // frame repeat after 60ms
#define PENTAX_ADDRESS_OFFSET                  0                                // skip 0 bits
#define PENTAX_ADDRESS_LEN                     0                                // read 0 address bits
#define PENTAX_COMMAND_OFFSET                  0                                // skip 0 bits
#define PENTAX_COMMAND_LEN                     6                                // read 6 bits
#define PENTAX_COMPLETE_DATA_LEN               6                                // complete length
#define PENTAX_STOP_BIT                        1                                // has stop bit
#define PENTAX_LSB                             0                                // LSB...MSB
#define PENTAX_FLAGS                           0                                // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ACP24: Stiebel Eltron ACP24 air conditioner
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define ACP24_START_BIT_PULSE_TIME               390                       //  390 usec pulse
#define ACP24_START_BIT_PAUSE_TIME               950                       //  950 usec pause
#define ACP24_PULSE_TIME                         390                       //  390 usec pulse
#define ACP24_1_PAUSE_TIME                      1300                       // 1300 usec pause
#define ACP24_0_PAUSE_TIME                       950                       //  950 usec pause
#define ACP24_FRAME_REPEAT_PAUSE_TIME           22000                       // frame repeat after 22ms?
#define ACP24_ADDRESS_OFFSET                     0                              // skip 0 bits
#define ACP24_ADDRESS_LEN                        0                              // read 6 address bits
#define ACP24_COMMAND_OFFSET                     0                              // skip 6 bits
#define ACP24_COMMAND_LEN                        0                              // read 0 bits (70 bits will be read and compressed by special routine)
#define ACP24_COMPLETE_DATA_LEN                 70                              // complete length
#define ACP24_STOP_BIT                          1                               // has stop bit
#define ACP24_LSB                               0                               // LSB...MSB
#define ACP24_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * IRMP16:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define IRMP16_START_BIT_PULSE_TIME              842                       //  842 usec pulse (32 x 1/38kHz)
#define IRMP16_START_BIT_PAUSE_TIME             1052                       // 1052 usec pause (40 x 1/38kHz)
#define IRMP16_PULSE_TIME                        421                       //  421 usec pulse (16 x 1/38kHz)
#define IRMP16_1_PAUSE_TIME                      842                       //  842 usec pause (32 x 1/38kHz)
#define IRMP16_0_PAUSE_TIME                      421                       //  421 usec pause (16 x 1/38kHz)
#define IRMP16_FRAME_REPEAT_PAUSE_TIME          40000                       // frame repeat after 40ms
#define IRMP16_ADDRESS_OFFSET                   0                               // skip 0 bits
#define IRMP16_ADDRESS_LEN                      0                               // read 0 address bits
#define IRMP16_COMMAND_OFFSET                   0                               // skip 0 bits
#define IRMP16_COMMAND_LEN                      16                              // read 16 bits (12 command + 4 CRC)
#define IRMP16_COMPLETE_DATA_LEN                16                              // complete length
#define IRMP16_STOP_BIT                         1                               // has stop bit
#define IRMP16_LSB                              1                               // LSB...MSB
#define IRMP16_FLAGS                            0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * GREE - climate:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define GREE_START_BIT_PULSE_TIME              12000                       // 12000 usec pulse (32 x 1/38kHz)
#define GREE_START_BIT_PAUSE_TIME               6000                       //  6000 usec pause (40 x 1/38kHz)
#define GREE_PULSE_TIME                          900                       //   900 usec pulse (16 x 1/38kHz)
#define GREE_1_PAUSE_TIME                        700                       //   700 usec pause (32 x 1/38kHz)
#define GREE_0_PAUSE_TIME                       2100                       //  2100 usec pause (16 x 1/38kHz)
#define GREE_FRAME_REPEAT_PAUSE_TIME           40000                       // frame repeat after 40ms
#define GREE_ADDRESS_OFFSET                     0                               // skip 0 bits
#define GREE_ADDRESS_LEN                        16                              // read 16 address bits
#define GREE_COMMAND_OFFSET                     16                              // skip 16 bits
#define GREE_COMMAND_LEN                        16                              // read 16 bits
#define GREE_COMPLETE_DATA_LEN                  32                              // complete length
#define GREE_STOP_BIT                           1                               // has stop bit
#define GREE_LSB                                1                               // LSB...MSB
#define GREE_FLAGS                              0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * METZ:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define METZ_START_BIT_PULSE_TIME                870                       //  870 usec pulse
#define METZ_START_BIT_PAUSE_TIME               2300                       // 2300 usec pause
#define METZ_PULSE_TIME                          435                       //  435 usec pulse
#define METZ_1_PAUSE_TIME                       1680                       // 1680 usec pause
#define METZ_0_PAUSE_TIME                        960                       //  960 usec pause
#define METZ_FRAME_REPEAT_PAUSE_TIME          122000                       // frame repeat after 122ms
#define METZ_ADDRESS_OFFSET                      1                              // skip 1 bit (toggle bit)
#define METZ_ADDRESS_LEN                         6                              // read 6 address bits
#define METZ_COMMAND_OFFSET                      7                              // skip 7 bits
#define METZ_COMMAND_LEN                        13                              // read 13 bits
#define METZ_COMPLETE_DATA_LEN                  20                              // complete length
#define METZ_STOP_BIT                            0                              // has no stop bit
#define METZ_LSB                                 0                              // MSB...LSB
#define METZ_FLAGS                               0                              // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RF GEN24 generic remote control (Pollin 550666, EAN 4049702006022 and many other similar RF remote controls)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RF_GEN24_0_PULSE_TIME                   400                        //  400 usec pulse
#define RF_GEN24_0_PAUSE_TIME                  1066                        // 1066 usec pause
#define RF_GEN24_1_PULSE_TIME                  1066                        // 1066 usec pulse
#define RF_GEN24_1_PAUSE_TIME                   400                        //  400 usec pause

#define RF_GEN24_FRAME_REPEAT_PAUSE_TIME      10000                        // frame repeat after 10 msec
#define RF_GEN24_ADDRESS_OFFSET                  0                              // skip 0 bits
#define RF_GEN24_ADDRESS_LEN                    10                              // read 10 address bits
#define RF_GEN24_COMMAND_OFFSET                 10                              // skip 0 + 10 bits
#define RF_GEN24_COMMAND_LEN                    14                              // read 14 command bits
#define RF_GEN24_COMPLETE_DATA_LEN              24                              // complete length
#define RF_GEN24_STOP_BIT                        1                              // has stop bit
#define RF_GEN24_LSB                             0                              // MSB...LSB
#define RF_GEN24_FLAGS                           0                              // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RF X10 remote control (MEDION, Pollin 721815)
 *
 * Frame:
 * 1 toggle bit + 7 checksum bits + 1 toggle bit + 7 command bits + 4 channel bits
 *
 * Rule:
 * checksum = (command + 0x0055 + (channel << 4)) & 0x7F
 *
 * Here we store checksum in address, command incl. 4 channel bits in command
 *
 * In irmp_get_data():
 *  irmp_command = command << 4
 *  irmp_address = channel + 1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RF_X10_START_BIT_PULSE_TIME             2850                        // 2850 usec pulse
#define RF_X10_START_BIT_PAUSE_TIME             1710                        // 1710 usec pulse
#define RF_X10_0_PULSE_TIME                      570                        //  570 usec pulse
#define RF_X10_0_PAUSE_TIME                      570                        //  570 usec pause
#define RF_X10_1_PULSE_TIME                      570                        //  570 usec pulse
#define RF_X10_1_PAUSE_TIME                     1710                        // 1710 usec pause

#define RF_X10_FRAME_REPEAT_PAUSE_TIME          4456                        // frame repeat after 4460 usec
#define RF_X10_ADDRESS_OFFSET                    1                               // skip 1st toggle bit
#define RF_X10_ADDRESS_LEN                       7                               // store 7 command bits in address
#define RF_X10_COMMAND_OFFSET                    9                               // skip 1st toggle bit + 7 command bits + 2nd toggle bit
#define RF_X10_COMMAND_LEN                      11                               // read 7 alternative command bits plus 4 0-bits
#define RF_X10_COMPLETE_DATA_LEN                20                               // complete length
#define RF_X10_STOP_BIT                          1                               // has stop bit
#define RF_X10_LSB                               0                               // MSB...LSB
#define RF_X10_FLAGS                             0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RF MEDION PC remote control (MEDION)
 *
 * Frame is simular to RF_X10, see above. Only the start bit timing differs.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RF_MEDION_START_BIT_PULSE_TIME          3960                        // 3960 usec pulse
#define RF_MEDION_START_BIT_PAUSE_TIME           610                        //  610 usec pulse
#define RF_MEDION_0_PULSE_TIME                   570                        //  570 usec pulse
#define RF_MEDION_0_PAUSE_TIME                   570                        //  570 usec pause
#define RF_MEDION_1_PULSE_TIME                   570                        //  570 usec pulse
#define RF_MEDION_1_PAUSE_TIME                  1710                        // 1710 usec pause

#define RF_MEDION_FRAME_REPEAT_PAUSE_TIME       5000                        // frame repeat after 5000 usec
#define RF_MEDION_ADDRESS_OFFSET                 1                               // skip 1st toggle bit
#define RF_MEDION_ADDRESS_LEN                    7                               // store 7 command bits in address
#define RF_MEDION_COMMAND_OFFSET                 9                               // skip 1st toggle bit + 7 command bits + 2nd toggle bit
#define RF_MEDION_COMMAND_LEN                   11                               // read 7 alternative command bits plus 4 0-bits
#define RF_MEDION_COMPLETE_DATA_LEN             20                               // complete length
#define RF_MEDION_STOP_BIT                       1                               // has stop bit
#define RF_MEDION_LSB                            0                               // MSB...LSB
#define RF_MEDION_FLAGS                          0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RCA:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RCA_START_BIT_PULSE_TIME                4000                       	// 4000 usec pulse
#define RCA_START_BIT_PAUSE_TIME                4000                       	// 4500 usec pause
#define RCA_REPEAT_START_BIT_PAUSE_TIME         2250                       	// 2250 usec pause (?)
#define RCA_PULSE_TIME                          500                       	//  500 usec pulse
#define RCA_1_PAUSE_TIME                        2000                       	// 2000 usec pause
#define RCA_0_PAUSE_TIME                        500                       	//  500 usec pause
#define RCA_FRAME_REPEAT_PAUSE_TIME				64000                       // frame repeat after 64ms
#define RCA_ADDRESS_OFFSET                      0                              // skip 0 bits
#define RCA_ADDRESS_LEN                         4                              // read 4 address bits
#define RCA_COMMAND_OFFSET                      4                              // skip 4 bits (4 address)
#define RCA_COMMAND_LEN                         8                              // read 8 bits (8 command)
#define RCA_COMPLETE_DATA_LEN                   24                              // complete length
#define RCA_STOP_BIT                            1                               // has stop bit
#define RCA_LSB                                 0                               // MSB...LSB
#define RCA_FLAGS                               0                               // flags

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * Pioneer:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define PIONEER_START_BIT_PULSE_TIME    		8500
#define PIONEER_START_BIT_PAUSE_TIME     		4225
#define PIONEER_PULSE_TIME         				500
#define PIONEER_1_PAUSE_TIME         			1500
#define PIONEER_0_PAUSE_TIME         			500
#define PIONEER_FRAME_REPEAT_PAUSE_TIME			26000
#define PIONEER_ADDRESS_OFFSET                  0                              // skip 0 bits
#define PIONEER_ADDRESS_LEN                     8                              // read 8 address bits
#define PIONEER_COMMAND_OFFSET                  16                             // skip 16 bits (8 address + 8 /address)
#define PIONEER_COMMAND_LEN                     8                              // read 8 bits (8 command)
#define PIONEER_COMPLETE_DATA_LEN               32                              // complete length
#define PIONEER_STOP_BIT                        1                               // has stop bit
#define PIONEER_LSB                             1                               // LSB...MSB
#define PIONEER_FLAGS                           0                               // flags


#define SAMSUNG_ADDRESS_LEN                     16                              // read 16 address bits
#define SAMSUNG_ID_OFFSET                       17                              // skip 16 + 1 sync bit
#define SAMSUNG_ID_LEN                          4                               // read 4 id bits
#define SAMSUNG_COMMAND_OFFSET                  21                              // skip 16 + 1 sync + 4 data bits
#define SAMSUNG_COMMAND_LEN                     16                              // read 16 command bits

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * General protocol: // Add on
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define GENERAL_PROTOCOL_ADDRESS_LEN           8
#define GENERAL_PROTOCOL_COMMAND_LEN           8

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * Frame Repetitions:
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
//#define AUTO_FRAME_REPETITION_TIME              80000                         // SIRCS/SAMSUNG32/NUBERT: automatic repetition after 25-50ms
#define AUTO_FRAME_REPETITION_TIME              65530                         // Fit in the 16-bit value

#endif // _IRMP_PROTOCOLS_H_
