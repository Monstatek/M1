/* Host-test stub of rfal_utils.h -- only the symbols m1_desfire.c actually
 * uses. Error-code numeric values are copied verbatim from the real
 * NFC/Middlewares/ST/rfal/Inc/rfal_utils.h so a test asserting on a
 * specific ReturnCode value stays meaningful. Not used by the firmware
 * build -- host analysis only. */
#ifndef RFAL_UTILS_STUB_H_
#define RFAL_UTILS_STUB_H_

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

typedef uint16_t ReturnCode;

#define RFAL_ERR_NONE    ((ReturnCode)0U)
#define RFAL_ERR_BUSY    ((ReturnCode)2U)
#define RFAL_ERR_TIMEOUT ((ReturnCode)4U)
#define RFAL_ERR_PROTO   ((ReturnCode)11U)
#define RFAL_ERR_AGAIN   ((ReturnCode)13U)

static inline void platformLog(const char *fmt, ...)
{
    (void)fmt;
}

#endif
