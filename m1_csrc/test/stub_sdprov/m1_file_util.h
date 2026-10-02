/* Host-test stub for m1_file_util.h, used only to host-compile the REAL,
 * unmodified m1_sdcard_provision.c. Declares exactly the one function
 * m1_sdcard_provision.c actually calls; the test file itself supplies the
 * definition, so the test controls FatFs outcomes directly instead of
 * needing a real FatFs disk backend. See m1_sdcard_provision_test.c. */
#ifndef M1_FILE_UTIL_H_
#define M1_FILE_UTIL_H_

#include "ff.h"

FRESULT fs_directory_ensure(const char *path);

#endif /* M1_FILE_UTIL_H_ */
