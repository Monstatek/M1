/* See COPYING.txt for license details. */

/*
*
* m1_crc32c.h
*
* CRC-32C (Castagnoli, poly 0x1EDC6F41 / reversed 0x82F63B78) for the
* MonstaShark ESP32-C6 <-> STM32 capture transport. Every transport envelope
* carries a CRC-32C so corruption is detected and counted.
*
* Pure logic, host-tested. Shared, byte-identical, by both processors.
*
* M1 Project
*
*/

#ifndef M1_CRC32C_H_
#define M1_CRC32C_H_

#include <stdint.h>
#include <stddef.h>

/* One-shot CRC-32C with standard init (0xFFFFFFFF) and final XOR. */
uint32_t m1_crc32c(const void *data, size_t len);

/* Incremental CRC-32C. Seed the first call with M1_CRC32C_INIT; do NOT apply a
 * final XOR between chunks — call m1_crc32c_final() on the accumulator to get
 * the value m1_crc32c() would return. */
#define M1_CRC32C_INIT 0xFFFFFFFFu
uint32_t m1_crc32c_update(uint32_t crc, const void *data, size_t len);
static inline uint32_t m1_crc32c_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

#endif /* M1_CRC32C_H_ */
