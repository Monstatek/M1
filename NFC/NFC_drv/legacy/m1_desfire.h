/*============================================================================*/
/**
 * @file    m1_desfire.h
 * @brief   MIFARE DESFire read-only identification and deeper inspection.
 *
 * Tier 1 (unchanged): sends the DESFire native GetVersion (0x60) command,
 * chained via the "additional frame" status (0xAF), over the ISO-DEP link
 * RFAL already activated for a Type 4A device. Parses the 28-byte version
 * tuple into the shared NFC context (generation, capacity, UID, batch,
 * production date).
 *
 * Deeper read (new): immediately after a successful GetVersion, while the
 * card is still selected, additionally attempts GetFreeMemory, PICC-level
 * GetKeySettings, GetApplicationIDs, and per-application SelectApplication +
 * GetKeySettings + GetKeyVersion(s) + GetFileIDs + per-file GetFileSettings,
 * reading Standard/Backup/Value/Linear-Record/Cyclic-Record file content
 * only when a file's access rights already grant free, plain (unencrypted)
 * read to an unauthenticated reader.
 *
 * Licensed under the GNU General Public License v3.0 (GPLv3).
 * Copyright (C) 2026 Monstatek. See COPYING.txt for the full license text.
 *
 * READ-ONLY, ALWAYS: no authentication is ever attempted (GetVersion and
 * every enumeration command used here are public/unauthenticated), no file
 * is read unless its access rights already grant free plaintext read, no
 * writes, no emulation, no key recovery.
 */
/*============================================================================*/
#ifndef M1_DESFIRE_H
#define M1_DESFIRE_H

#include <stdint.h>
#include <stdbool.h>
#include "rfal_nfc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Perform DESFire GetVersion identification on an activated Type 4A device,
 * then -- only on GetVersion success, while the card is still selected --
 * attempt the deeper read described above. Requires the device to be
 * ISO-DEP activated (dev->rfInterface == ISO-DEP). On GetVersion success,
 * stores the parsed version in the NFC context and returns true; the deeper
 * read's own success/partial/protected/failed outcome is tracked separately
 * per item in nfc_ctx_get_desfire_deep() and never changes this return
 * value. Non-fatal: any GetVersion failure (not ISO-DEP, no/short response,
 * non-NXP vendor, bad length) leaves the DESFire info marked "not present"
 * and returns false; the caller keeps the generic "Type 4A (ISO-DEP)"
 * identity, and no deeper read is attempted. */
bool m1_desfire_read(const rfalNfcDevice *dev);

/* Read-only display decode of the version bytes. */
const char *m1_desfire_gen_name(uint8_t hw_major);    /* "EV1" / "EV2" / ... */
const char *m1_desfire_size_name(uint8_t hw_storage); /* "2K" / "4K" / ...   */

#ifdef __cplusplus
}
#endif

#endif /* M1_DESFIRE_H */
