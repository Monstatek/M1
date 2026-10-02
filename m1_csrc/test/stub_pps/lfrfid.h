/* Host-test stub for lfrfid.h. privateprofilestring.c only calls
 * safe_free() from this header -- the real lfrfid.h transitively pulls in
 * the entire LF RFID protocol/HAL stack (stream_buffer.h, lfrfid_hal.h,
 * t5577.h, every lfrfid_protocol_*.h), none of which this translation unit
 * actually needs. Per the hardening pass's own rule, LF RFID code is not to
 * be touched or ported here -- only its one used declaration is mirrored. */
#ifndef STUB_LFRFID_H_
#define STUB_LFRFID_H_

/* The real lfrfid_hal.h chain transitively pulls in FatFs's ff.h (FIL is
 * used throughout the LF RFID file-backed profile code); m1_file_util.h,
 * included right after this header in privateprofilestring.c, needs
 * FRESULT to already be visible for that same reason. */
#include "ff.h"

void safe_free(void **pp);

#endif /* STUB_LFRFID_H_ */
