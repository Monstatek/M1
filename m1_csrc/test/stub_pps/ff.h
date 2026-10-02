/* Host-test stub for FatFs's ff.h.
 *
 * Backs the REAL, unmodified m1_csrc/privateprofilestring.c and
 * m1_csrc/m1_file_util.c with POSIX file I/O (see ff_stub.c in this same
 * directory) so both files' actual production logic -- not a reimplementation
 * of it -- is what gets host-tested. Covers exactly the FatFs surface those
 * two translation units reference (confirmed by grep against both files);
 * nothing beyond that is declared.
 */
#ifndef STUB_FF_H_
#define STUB_FF_H_

#include <stdio.h>
#include <stdint.h>

typedef int FRESULT;
#define FR_OK           (0)
#define FR_DISK_ERR     (1)
#define FR_EXIST        (8)
#define FR_NO_FILE      (4)
#define FR_NO_PATH      (5)

typedef uint32_t DWORD;
typedef uint8_t  BYTE;

/* Single sector-size configuration -- keeps fs_get_free_space()'s
 * FF_MAX_SS != FF_MIN_SS branch (which reads fs->ssize) compiled out,
 * matching a common single-sector-size FatFs configuration. */
#define FF_MAX_SS (512)
#define FF_MIN_SS (512)

#define AM_DIR (0x10)

#define FA_READ          (0x01)
#define FA_WRITE         (0x02)
#define FA_CREATE_NEW    (0x04)
#define FA_CREATE_ALWAYS (0x08)

typedef struct {
    FILE *posix_fp;
} FIL;

typedef struct {
    DWORD  fsize;
    BYTE   fattrib;
    char   fname[64];
} FILINFO;

typedef struct {
    DWORD csize;
} FATFS;

FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_close(FIL *fp);
char   *f_gets(char *buf, int size, FIL *fp);
int     f_printf(FIL *fp, const char *fmt, ...);
FRESULT f_sync(FIL *fp);
FRESULT f_unlink(const char *path);
FRESULT f_rename(const char *old_path, const char *new_path);
FRESULT f_stat(const char *path, FILINFO *fno);
FRESULT f_mkdir(const char *path);
FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs);

#endif /* STUB_FF_H_ */
