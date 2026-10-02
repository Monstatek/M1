/* See COPYING.txt for license details. */

/*
*
* m1_cobs.h
*
* Consistent Overhead Byte Stuffing (COBS) for the MonstaShark capture
* transport. COBS removes all 0x00 bytes from a payload so a single 0x00 can be
* used as an unambiguous, resynchronizable frame delimiter.
*
* Pure logic, host-tested. Shared byte-identical by both processors.
*
* M1 Project
*
*/

#ifndef M1_COBS_H_
#define M1_COBS_H_

#include <stdint.h>
#include <stddef.h>

/* Worst-case encoded size for n payload bytes (excludes the 0x00 delimiter). */
#define M1_COBS_MAX_ENCODED(n) ((n) + ((n) / 254u) + 1u)

/* Encode src[0..len) into dst (no 0x00 bytes, no trailing delimiter). Returns
 * the encoded length, or 0 on overflow. len==0 encodes to a single 0x01 byte. */
size_t m1_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap);

/* Decode a COBS block src[0..len) (the bytes between delimiters, delimiter NOT
 * included) into dst. Returns the decoded length, or 0 on malformed input or
 * overflow. */
size_t m1_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap);

#endif /* M1_COBS_H_ */
