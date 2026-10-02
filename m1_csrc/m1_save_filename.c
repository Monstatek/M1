/* See COPYING.txt for license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "m1_file_util.h"
#include "m1_virtual_kb.h"
#include "uiView.h"
#include "res_string.h"
#include "m1_save_filename.h"

uint8_t m1_save_filename(char *filepath, size_t filepath_size, const char *ensure_dir,
                         const char *path_prefix, const char *name_prefix,
                         const char *extension)
{
    uint8_t fname[50], dname[50];
    int length;
    fs_path_kind_t kind;
    FRESULT result;

    if (!ensure_dir || !path_prefix || !name_prefix || !extension ||
        (filepath && filepath_size == 0)) return 2;

    m1_sdcard_get_info();
    if (m1_sdcard_get_free_capacity() < 4) return 1;
    if (fs_directory_ensure(ensure_dir) != FR_OK) return 2;

    srand(HAL_GetTick());
    while (1) {
        length = snprintf((char *)dname, sizeof(dname), "%s%05u", name_prefix, rand() % 0xFFFFF);
        if (length < 0 || (size_t)length >= sizeof(dname)) return 2;
        memset(fname, 0, sizeof(fname));
        if (!m1_vkb_get_filename((char *)res_string(IDS_ENTER_FILENAME),
                                 (char *)dname, (char *)fname, 1)) return 3;
        if (!memchr(fname, '\0', sizeof(fname))) return 2;
        length = snprintf((char *)dname, sizeof(dname), "%s%s%s", path_prefix, fname, extension);
        if (length < 0 || (size_t)length >= sizeof(dname) ||
            (filepath && (size_t)length >= filepath_size)) return 2;

        result = fs_path_kind((char *)dname, &kind);
        if (result != FR_OK && result != FR_NO_FILE && result != FR_NO_PATH) return 2;
        if (result == FR_OK) {
            m1_message_box(&m1_u8g2, res_string(IDS_DUPLICATE_FILE), NULL,
                           " ", res_string(IDS_BACK));
        } else {
            break;
        }
    }
    if (filepath != NULL) memcpy(filepath, dname, (size_t)length + 1);
    return 0;
}
