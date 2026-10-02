/* See COPYING.txt for license details. */

/*
*
*  m1_nfc.h
*
*  M1 NFC functions
*
* M1 Project
*
*/

#ifndef M1_NFC_H_
#define M1_NFC_H_

void nfc_read(void);
void nfc_saved(void);
/* Authoritative saved-file flow.  NULL/NULL opens the native NFC Saved
 * browser; a non-NULL directory/name opens that exact file and owns the NFC
 * lifecycle for callers outside the NFC parent menu (Home -> LEFT). */
void nfc_saved_launch(const char *dir_name, const char *file_name);
void nfc_detect_reader(void);
#if defined(M1_MFC_RAW_EMULATION)
void nfc_mfc_emulate(void);   /* raw MIFARE Classic emulation (Scope B) */
#endif
void nfc_mfc_keys(void);
void nfc_harvest(void);
void nfc_ulc_keys(void);
void nfc_pwd_keys(void);

/* NFC > Add: manual card creation, one launcher per approved type. */
void nfc_add_mfc1k(void);
void nfc_add_mfc4k(void);
void nfc_add_ultralight(void);
void nfc_add_ntag213(void);
void nfc_add_ntag215(void);
void nfc_add_ntag216(void);

void menu_nfc_init(void);
void menu_nfc_deinit(void);

#endif /* M1_NFC_H_ */
