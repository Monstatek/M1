/* Host-test stub for the project-wide main.h that privateprofilestring.c
 * includes. Its only externally-visible requirement for this translation
 * unit is the S_M1_file_info type (used by privateprofilestring.h's
 * IsValidFileSpec() prototype) -- normally pulled in transitively through
 * main.h's real project-wide include chain via m1_file_browser.h. That
 * header itself needs the GUI stack (u8g2.h/mui.h), which is irrelevant to
 * this test, so only the plain struct/enum is reproduced here verbatim
 * from m1_csrc/m1_file_browser.h. */
#ifndef STUB_MAIN_H_
#define STUB_MAIN_H_

#include <stdbool.h>

typedef enum
{
    FB_OK = 0,
    FB_ERR_SDCARD,
    FB_ERR_GUI
} S_M1_file_browser_code;

typedef struct
{
    char *dir_name;
    char *file_name;
    bool file_is_selected;
    S_M1_file_browser_code status;
} S_M1_file_info;

#endif /* STUB_MAIN_H_ */
