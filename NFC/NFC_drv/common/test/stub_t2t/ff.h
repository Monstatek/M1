/* Minimal host-test stub of FatFS's ff.h -- just enough for
 * m1_t2t_emu_image.c to compile/link. m1_t2t_emu_image_save() (the only
 * user of these) is never called by t2t_emu_image_test.c, so behavior here
 * is irrelevant -- only the symbols need to resolve. */
#ifndef FF_STUB_H_
#define FF_STUB_H_

typedef struct { int unused; } FIL;

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
} FRESULT;

FRESULT f_rename(const char *old_path, const char *new_path);

#endif
