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

#define ESP_FILE_NAME_LEN_MAX	32
#define ESP_FILE_PATH_LEN_MAX	64

#define FW_FILE_NAME_LEN_MAX	32
#define FW_FILE_PATH_LEN_MAX	64

extern uint8_t usbmsc_sd_enable;
/* Set by the SCSI START_STOP_UNIT eject callback (USB context); consumed and
 * cleared by the storage task, which performs the filesystem remount. */
extern volatile uint8_t usbmsc_host_ejected;

void menu_setting_storage_init(void);
void storage_about(void);
void storage_explore(void);
void storage_usbmsc(void);
void storage_format(void);
S_M1_file_info *storage_browse(void);

/* General-browser -> Sub-GHz replay origin context. NFC and RFID now accept
 * exact paths through their own lifecycle-aware saved-file launchers. */
void m1_browser_request_explore_open(const S_M1_file_info *f);
S_M1_file_info *m1_browser_explore_take_pending(void);
int  m1_browser_explore_active(void);
void m1_browser_explore_end(void);

#endif /* M1_STORAGE_H_ */
