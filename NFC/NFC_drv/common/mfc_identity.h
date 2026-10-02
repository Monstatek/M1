/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_identity.h
 * @brief   Physical-card identity comparison for a MIFARE Classic
 *          acquisition continuation (Find Missing Keys).
 *
 * A continuation must refuse to touch the existing nfc_mfc_info_t/
 * nfc_mfc_scan_t state (proven keys, recovered blocks, progress) unless the
 * card currently on the reader is provably the SAME physical card the
 * original read/continuation was built from -- otherwise a different card's
 * data would silently corrupt the first card's partial result. This module
 * answers exactly that one question, from two already-read UIDs; it knows
 * nothing about RF, sectors, keys, or acquisition state.
 *
 * Pure byte comparison, no I/O dependency -- links and runs unmodified in a
 * host test binary.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_IDENTITY_H_
#define NFC_DRV_MFC_IDENTITY_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True only if both UIDs have the same length and identical bytes. A NULL
 * pointer with a nonzero length is treated as non-matching (never
 * dereferenced), so a caller that has not yet captured a UID can pass
 * {NULL, 0} safely and it simply never matches a real card. */
bool mfc_identity_matches(const uint8_t *a, uint8_t a_len,
                          const uint8_t *b, uint8_t b_len);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_IDENTITY_H_ */
