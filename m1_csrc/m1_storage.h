/* See COPYING.txt for license details. */

/*
*
*  m1_storage.h
*
*  M1 storage functions
*
* M1 Project
*
*/

#ifndef M1_STORAGE_H_
#define M1_STORAGE_H_

#define ESP_FILE_NAME_LEN_MAX	256
#define ESP_FILE_PATH_LEN_MAX	64

#define FW_FILE_NAME_LEN_MAX	256
#define FW_FILE_PATH_LEN_MAX	64

void menu_setting_storage_init(void);
void storage_about(void);
void storage_explore(void);
void storage_mount(void);
void storage_unmount(void);
void storage_format(void);
S_M1_file_info *storage_browse(void);

#endif /* M1_STORAGE_H_ */
