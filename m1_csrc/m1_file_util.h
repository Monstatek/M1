/* See COPYING.txt for license details. */

/*
 * m1_file_util.h
 */

#ifndef M1_FILE_UTIL_H_
#define M1_FILE_UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include "ff.h"

typedef enum { FS_PATH_NONE, FS_PATH_FILE, FS_PATH_DIRECTORY } fs_path_kind_t;
/* Returns the actual FatFs result. On any error, kind is FS_PATH_NONE.
 * FR_NO_FILE/FR_NO_PATH mean missing; other errors must not mean missing. */
FRESULT fs_path_kind(const char *path, fs_path_kind_t *kind);

void fu_get_filename_without_ext(const char *path, char *outName, size_t outSize);
const char* fu_get_filename(const char *path);
const char* fu_get_file_extension(const char *filename);
void fu_get_directory_path(const char *fullPath, char *outDir, size_t dirSize);
void fu_path_combine(char *out, size_t outSize, const char *path, const char *file);
/* Legacy predicates: 0 still means missing OR error. Use fs_path_kind when
 * deciding whether a new file may be created. */
int fs_file_exists(const char *path);
int fs_directory_exists(const char *path);
FRESULT fs_directory_ensure(const char *path);
FRESULT fs_get_free_space(uint64_t *pFree);
FRESULT fs_save_file_safe(const char *path, const void *buf, uint32_t size);

#endif /* M1_FILE_UTIL_H_ */
