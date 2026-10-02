/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * irmpsystem.h - system specific includes and defines
 *
 * Copyright (c) 2009-2020 Frank Meyer - frank(at)fli4l.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#ifndef _IRMPSYSTEM_H_
#define _IRMPSYSTEM_H_

#if !defined(_IRMP_H_) && !defined(_IRSND_H_)
#  error please include only irmp.h or irsnd.h, not irmpsystem.h
#endif

#define ARM_STM32_HAL

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "main.h"

#  define PROGMEM
#  define memcpy_P                      memcpy

#ifndef TRUE
#  define TRUE                          1
#  define FALSE                         0
#endif

#  ifndef IRMP_PACKED_STRUCT
#    define IRMP_PACKED_STRUCT          __attribute__ ((__packed__))
#  endif

typedef struct IRMP_PACKED_STRUCT
{
    uint8_t                             protocol;                                   // protocol, e.g. NEC_PROTOCOL
    uint16_t                            address;                                    // address
    uint16_t                            command;                                    // command
    uint8_t                             flags;                                      // flags, e.g. repetition
	uint8_t 							duty_cycle;									// duty cycle, add on
	uint32_t							frequency;									// frequency, add on
} IRMP_DATA;

#endif // _IRMPSYSTEM_H_
