/* Host-test stub implementation -- see m1_file_browser.h / ff.h in this
 * directory. Unreachable at runtime (m1_t2t_emu_image_save() is never
 * called by t2t_emu_image_test.c) -- these exist only so the linker can
 * resolve m1_t2t_emu_image.c's references. */
#include "m1_file_browser.h"

uint8_t m1_fb_open_new_file(FIL *file, const char *filename)
{
    (void)file; (void)filename;
    return 1;
}

uint8_t m1_fb_close_file(FIL *file)
{
    (void)file;
    return 0;
}

uint8_t m1_fb_delete_file(const char *filename)
{
    (void)filename;
    return 1;
}

uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size)
{
    (void)pfile; (void)buffer; (void)size;
    return 0;
}

FRESULT f_rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path;
    return FR_DISK_ERR;
}
