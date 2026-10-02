/* Host-test stub of logger.h -- discards platformLog() calls (no serial
 * port on the host); m1_t2t_transport.c's log lines are status-only,
 * never load-bearing for behavior. */
#ifndef LOGGER_STUB_H_
#define LOGGER_STUB_H_

#include <stdarg.h>
#include <stdio.h>

static inline void platformLog(const char *fmt, ...)
{
    (void)fmt;
}

#endif
