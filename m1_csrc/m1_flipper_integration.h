/* See COPYING.txt for license details. */

/*
*
*  m1_flipper_integration.h
*
*  Flipper Zero file import/replay integration for M1
*
* M1 Project
*
*/

#ifndef M1_FLIPPER_INTEGRATION_H_
#define M1_FLIPPER_INTEGRATION_H_

#include <stdint.h>
#include <stdbool.h>

/*============================================================================*/
/*  Sub-GHz: Replay Flipper .sub files                                       */
/*============================================================================*/

/**
 * @brief  Load and replay a Flipper .sub file through the M1 Sub-GHz radio.
 *         Opens a file browser at 0:/Flipper/SubGHz/ and lets user pick a file.
 *         Called from the Sub-GHz menu as a menu item function.
 */
void sub_ghz_replay_flipper(void);

/*============================================================================*/
/*  NFC: Import Flipper .nfc files                                           */
/*============================================================================*/

/**
 * @brief  Import a Flipper .nfc file and save it in M1's native NFC format.
 *         Opens a file browser at 0:/Flipper/NFC/ and lets user pick a file.
 *         Called from the NFC menu as a menu item function.
 */
void nfc_import_flipper(void);

/*============================================================================*/
/*  RFID: Import Flipper .rfid files                                         */
/*============================================================================*/

/**
 * @brief  Import a Flipper .rfid file and save it in M1's native RFID format.
 *         Opens a file browser at 0:/Flipper/RFID/ and lets user pick a file.
 *         Called from the RFID menu as a menu item function.
 */
void rfid_import_flipper(void);

#endif /* M1_FLIPPER_INTEGRATION_H_ */
