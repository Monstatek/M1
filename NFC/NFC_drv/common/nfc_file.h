/* See COPYING.txt for license details. */

#ifndef NFC_DRV_NFC_FILE_H_
#define NFC_DRV_NFC_FILE_H_

#include <stdbool.h>
#include <stdint.h>
#include "m1_file_browser.h"  // For S_M1_file_info
#include "nfc_ctx.h"          // For nfc_run_ctx_t

typedef const nfc_run_ctx_t* PCNFC_RUN_CTX;

/**
 * @brief Load NFC profile from file
 * 
 * Validates file extension and loads NFC card data from file.
 * Uses nfc_storage_load_file() internally to parse the file.
 * 
 * @param f File info structure from storage_browse()
 * @param ext File extension to validate (e.g., "nfc")
 * @return true on success, false on failure
 */
bool nfc_profile_load(const S_M1_file_info *f, const char* ext);

/**
 * @brief Save NFC profile to file
 * 
 * Saves NFC card context data to file in M1 NFC device format.
 * 
 * @param fp Full file path to save to
 * @param ctx NFC context containing card data
 * @return true on success, false on failure
 */
bool nfc_profile_save(const char *fp, PCNFC_RUN_CTX ctx);

/**
 * @brief Mark the NEXT nfc_profile_save() as a manually-created card.
 *
 * Writes a "# Created: manual" provenance comment into the saved file so a
 * manually-added record is distinguishable from a physical full-card read. The
 * flag is one-shot: it is consumed (cleared) by the next save.
 */
void nfc_file_mark_next_save_manual(void);

/**
 * @brief Get filename from user and create full file path
 * 
 * Prompts user for filename using virtual keyboard, validates
 * SD card space, creates directory if needed, and checks for
 * duplicate filenames.
 * 
 * @param filepath Output buffer for full file path (can be NULL)
 * @param filepath_size Actual output capacity, including NUL
 * @return 0 on success
 * @return 1 Limited space available on SD card
 * @return 2 Storage/path error or insufficient output capacity
 * @return 3 User escaped (cancelled)
 */
uint8_t nfc_save_file_keyboard(char *filepath, size_t filepath_size);

#endif /* NFC_DRV_NFC_FILE_H_ */
