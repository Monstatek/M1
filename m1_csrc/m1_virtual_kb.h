/* See COPYING.txt for license details. */

/*
*
* m1_virtual_kb.h
*
* Library for the virtual keyboard
*
* M1 Project
*
*/

#ifndef M1_VIRTUAL_KB_H_
#define M1_VIRTUAL_KB_H_

/* default_is_generated: nonzero if default_name is a DEVICE-generated default
 * (treated as selected text -- one Backspace clears it, first char replaces it).
 * Pass 0 when editing an existing/user-entered name (normal per-char deletion). */
uint8_t m1_vkb_get_filename(char *description, char *default_name, char *new_name, uint8_t default_is_generated);
uint8_t m1_vkbs_get_data(char *description, char *new_data);

/* Append 1..max_bytes of whole-byte hex; Enter rejects empty/odd input in place.
 * Returns nibble count, or 0 on BACK. out_hex needs 2*max_bytes+1 bytes;
 * max_bytes must be 1..64. */
uint8_t m1_vkbs_get_hex_bytes(char *description, char *out_hex, uint8_t max_bytes);

/* Growing fixed-length hex-key editor (e.g. a 32-nibble Ultralight C key). The
 * user appends hex digits (0..req_nibbles); Save (Enter) only succeeds at exactly
 * req_nibbles -- otherwise a validation message is shown IN-EDITOR and entry
 * continues with all input preserved (no eject, no reset). Physical BACK cancels.
 * Returns 1 with req_nibbles uppercase hex chars in out_hex (null-terminated) on
 * success, 0 on cancel. out_hex must hold at least req_nibbles+1 bytes; req<=62. */
uint8_t m1_vkbs_get_hexkey(char *description, char *out_hex, uint8_t req_nibbles);

#endif /* M1_VIRTUAL_KB_H_ */
