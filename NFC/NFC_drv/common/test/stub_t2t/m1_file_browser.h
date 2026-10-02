/* Minimal host-test stub of m1_file_browser.h -- just enough for
 * m1_t2t_emu_image.c to compile/link. m1_t2t_emu_image_save() (the only
 * user of these) is never called by t2t_emu_image_test.c, so behavior here
 * is irrelevant -- only the symbols need to resolve. */
#ifndef M1_FILE_BROWSER_STUB_H_
#define M1_FILE_BROWSER_STUB_H_

#include <stdint.h>
#include "ff.h"

uint8_t m1_fb_open_new_file(FIL *file, const char *filename);
uint8_t m1_fb_close_file(FIL *file);
uint8_t m1_fb_delete_file(const char *filename);
uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size);

#endif
