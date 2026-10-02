/* See COPYING.txt for license details. */

/*
 * lfrfid_file.h
 */

#ifndef LFRFID_FILE_H_
#define LFRFID_FILE_H_

#include <stddef.h>

bool lfrfid_profile_load(const S_M1_file_info *f, const char* ext);
bool lfrfid_profile_save(const char *fp, const PLFRFID_TAG_INFO data);
LFRFIDProtocol lfrfid_get_protocol_by_name(const char* name);
uint8_t lfrfid_save_file_keyboard(char *filepath, size_t filepath_size);
#endif /* LFRFID_FILE_H_ */
