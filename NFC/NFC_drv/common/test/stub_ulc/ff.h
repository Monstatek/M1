#ifndef TEST_STUB_FF_H
#define TEST_STUB_FF_H

#include <stddef.h>

typedef unsigned char BYTE;
typedef int FRESULT;

typedef struct {
    int slot;
    size_t line;
} FIL;

#define FR_OK       0
#define FR_NO_FILE  1
#define FR_NO_PATH  2
#define FR_DISK_ERR 3
#define FA_READ     0x01U

FRESULT f_open(FIL* fp, const char* path, BYTE mode);
FRESULT f_close(FIL* fp);

#endif
