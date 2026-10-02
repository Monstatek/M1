/* Host-test stub of cmsis_os2.h -- only osDelay(), which m1_desfire.c
 * calls while polling for a transceive to finish. The mock always
 * completes synchronously (see rfal_mock.c), so this is never actually
 * invoked in a passing test run, but must exist to link. Not used by the
 * firmware build -- host analysis only. */
#ifndef CMSIS_OS2_STUB_H_
#define CMSIS_OS2_STUB_H_

#include <stdint.h>

static inline void osDelay(uint32_t ms)
{
    (void)ms;
}

#endif
