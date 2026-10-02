/* See COPYING.txt for license details. */
#ifndef M1_SAVE_FILENAME_H_
#define M1_SAVE_FILENAME_H_

#include <stdint.h>
#include <stddef.h>

/* Shared NFC/RFID/Wi-Fi save-name dialog. Existing wrappers supply trusted
 * directory/prefix/extension constants; the keyboard limits names to 20 chars.
 * path_prefix includes the trailing slash. ensure_dir is kept separate because
 * RFID historically ensures "/rfid" but returns a path starting "0:/rfid/".
 * Each assembled default/path is bounded to 50 bytes including NUL.
 * filepath may be NULL; otherwise filepath_size is its actual capacity.
 * Returns 0 success, 1 low space, 2 storage/path/capacity error, 3 cancelled.
 * On failure filepath is untouched. No file is created by this dialog. */
uint8_t m1_save_filename(char *filepath, size_t filepath_size, const char *ensure_dir,
                         const char *path_prefix, const char *name_prefix,
                         const char *extension);

#endif
