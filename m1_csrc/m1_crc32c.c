/* See COPYING.txt for license details. */

/*
*
* m1_crc32c.c
*
* CRC-32C (Castagnoli) — bitwise, table-free (see m1_crc32c.h). Pure, tested.
*
* M1 Project
*
*/

#include "m1_crc32c.h"

#define CRC32C_POLY_REV 0x82F63B78u /* reversed 0x1EDC6F41 */

uint32_t m1_crc32c_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    int b;

    for (i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (CRC32C_POLY_REV & mask);
        }
    }
    return crc;
}

uint32_t m1_crc32c(const void *data, size_t len)
{
    return m1_crc32c_final(m1_crc32c_update(M1_CRC32C_INIT, data, len));
}
