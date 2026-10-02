/* See COPYING.txt for license details. */

/*
*
*  m1_rfid.h
*
*  M1 RFID functions
*
* M1 Project
*
*/
#ifndef M1_RFID_H_
#define M1_RFID_H_


void menu_125khz_rfid_init(void);
void menu_125khz_rfid_deinit(void);

void rfid_125khz_read(void);
void rfid_125khz_saved(void);
/* Authoritative saved-file flow.  NULL/NULL opens the native RFID Saved
 * browser; a non-NULL directory/name opens that exact file and owns the RFID
 * lifecycle for callers outside the RFID parent menu (Home -> LEFT). */
void rfid_125khz_saved_launch(const char *dir_name, const char *file_name);
void rfid_125khz_add_manually(void);
void rfid_125khz_utilities(void);


/////////////////////////////////////////////////////////////////////////////




#endif /* M1_RFID_H_ */
