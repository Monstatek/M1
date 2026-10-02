/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    nfc_hex_utils.h
 * @brief   Contiguous-hex-string-to-bytes conversion for the shared virtual
 *          hex-key keyboard (m1_vkbs_get_hexkey()).
 *
 * m1_vkbs_get_hexkey() returns a CONTIGUOUS hex string of exactly 2*len
 * characters, no separators (proven by its own append code:
 * out_hex[len]=digit; out_hex[len+1]='\0'). m1_strtob_with_base() is NOT
 * this: it space-tokenizes (strtok(str, " ")) and parses each whitespace-
 * separated token as one strtol() value, so a contiguous string like
 * "12345678" is seen as a single token and parsed as one (silently
 * overflowing/truncated) value, never as 4 separate bytes -- that mismatch
 * previously made every password/key/UID entry through several Add
 * workflows fail validation regardless of what was actually typed.
 *
 * Pure string/strtol logic, no HAL/RTOS dependency -- links and runs
 * unmodified in a host test binary, unlike the UI files (m1_nfc.c) that
 * call it.
 */
/*============================================================================*/
#ifndef NFC_DRV_NFC_HEX_UTILS_H_
#define NFC_DRV_NFC_HEX_UTILS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Converts `hex`, a contiguous string of exactly 2*len hex characters (no
 * separators, case-insensitive), into `len` raw bytes in `out`. Returns
 * false (out left partially written) if any character is not a valid hex
 * digit; true otherwise. */
bool nfc_hex_nibbles_to_bytes(const char *hex, uint8_t *out, int len);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_NFC_HEX_UTILS_H_ */
