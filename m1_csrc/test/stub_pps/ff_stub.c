/* POSIX-backed implementation of the ff.h stub used to host-test the REAL,
 * unmodified m1_csrc/privateprofilestring.c and m1_csrc/m1_file_util.c.
 * Paths are used as real POSIX paths (the test fixture passes real
 * on-disk paths under a scratch directory) -- there is no translation
 * layer, so f_open/f_close/etc. below just wrap fopen/fclose/etc. plus
 * the fault-injection hooks declared in ff_fault_inject.h. */
#include "ff.h"
#include "ff_fault_inject.h"

#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

int g_ff_fail_open_write = 0;
int g_ff_fail_sync = 0;
int g_ff_fail_close = 0;
int g_ff_fail_rename = 0;
int g_ff_fail_unlink = 0;
int g_ff_card_removed = 0;
int g_ff_card_removed_after_call = 0;
int g_ff_call_count = 0;

void ff_fault_inject_reset(void)
{
    g_ff_fail_open_write = 0;
    g_ff_fail_sync = 0;
    g_ff_fail_close = 0;
    g_ff_fail_rename = 0;
    g_ff_fail_unlink = 0;
    g_ff_card_removed = 0;
    g_ff_card_removed_after_call = 0;
    g_ff_call_count = 0;
}

/* Called at the top of every f_* entry point below. */
static void ff_stub_on_call(void)
{
    g_ff_call_count++;
    if (g_ff_card_removed_after_call > 0 &&
        g_ff_call_count >= g_ff_card_removed_after_call) {
        g_ff_card_removed = 1;
    }
}

FRESULT f_open(FIL *fp, const char *path, BYTE mode)
{
    ff_stub_on_call();

    const char *posix_mode;

    fp->posix_fp = NULL;

    if (g_ff_card_removed)
        return FR_DISK_ERR;

    if ((mode & FA_WRITE) && g_ff_fail_open_write)
        return FR_DISK_ERR;

    if (mode & FA_CREATE_NEW) {
        struct stat st;
        if (stat(path, &st) == 0)
            return FR_EXIST;
        posix_mode = "w+";
    } else if (mode & FA_CREATE_ALWAYS) {
        posix_mode = "w+";
    } else if (mode & FA_READ) {
        posix_mode = "r";
    } else {
        return FR_DISK_ERR;
    }

    fp->posix_fp = fopen(path, posix_mode);
    if (fp->posix_fp == NULL) {
        return (mode & FA_READ) ? FR_NO_FILE : FR_DISK_ERR;
    }

    return FR_OK;
}

FRESULT f_close(FIL *fp)
{
    ff_stub_on_call();

    int rc = 0;

    if (fp->posix_fp != NULL) {
        rc = fclose(fp->posix_fp);
        fp->posix_fp = NULL;
    }

    if (g_ff_card_removed || g_ff_fail_close)
        return FR_DISK_ERR;

    return (rc == 0) ? FR_OK : FR_DISK_ERR;
}

char *f_gets(char *buf, int size, FIL *fp)
{
    ff_stub_on_call();

    if (g_ff_card_removed || fp->posix_fp == NULL)
        return NULL;
    return fgets(buf, size, fp->posix_fp);
}

int f_printf(FIL *fp, const char *fmt, ...)
{
    ff_stub_on_call();

    va_list ap;
    int n;

    if (g_ff_card_removed || fp->posix_fp == NULL)
        return -1;

    va_start(ap, fmt);
    n = vfprintf(fp->posix_fp, fmt, ap);
    va_end(ap);

    return n;
}

FRESULT f_sync(FIL *fp)
{
    ff_stub_on_call();

    if (g_ff_card_removed || g_ff_fail_sync)
        return FR_DISK_ERR;

    if (fp->posix_fp == NULL)
        return FR_DISK_ERR;

    return (fflush(fp->posix_fp) == 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT f_unlink(const char *path)
{
    ff_stub_on_call();

    if (g_ff_card_removed || g_ff_fail_unlink)
        return FR_DISK_ERR;

    return (remove(path) == 0) ? FR_OK : FR_NO_FILE;
}

FRESULT f_rename(const char *old_path, const char *new_path)
{
    ff_stub_on_call();

    struct stat st;

    if (g_ff_card_removed || g_ff_fail_rename)
        return FR_DISK_ERR;

    if (stat(new_path, &st) == 0)
        return FR_EXIST;

    return (rename(old_path, new_path) == 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT f_stat(const char *path, FILINFO *fno)
{
    ff_stub_on_call();

    struct stat st;

    if (g_ff_card_removed)
        return FR_DISK_ERR;

    if (stat(path, &st) != 0)
        return FR_NO_FILE;

    memset(fno, 0, sizeof(*fno));
    fno->fsize = (DWORD)st.st_size;
    fno->fattrib = S_ISDIR(st.st_mode) ? AM_DIR : 0;

    return FR_OK;
}

FRESULT f_mkdir(const char *path)
{
    ff_stub_on_call();

    if (g_ff_card_removed)
        return FR_DISK_ERR;

    return (mkdir(path, 0755) == 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs)
{
    ff_stub_on_call();

    (void)path;

    static FATFS s_fake_fs = { .csize = 1 };

    if (g_ff_card_removed)
        return FR_DISK_ERR;

    *nclst = 0;
    *fatfs = &s_fake_fs;

    return FR_OK;
}
