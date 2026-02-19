/* See COPYING.txt for license details. */

/*
*
* m1_fw_update.h
*
* Firmware update functionality
*
* M1 Project
*
*/

#ifndef M1_FW_UPDATE_H_
#define M1_FW_UPDATE_H_

#include "m1_display.h"

void firmware_update_init(void);
void firmware_update_exit(void);
void firmware_update_get_image_file(void);
void firmware_update_start(void);
void firmware_update_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void firmware_update_usb_dfu_mode(void);
void firmware_update_enter_usb_dfu(void);

#endif /* M1_FW_UPDATE_H_ */
