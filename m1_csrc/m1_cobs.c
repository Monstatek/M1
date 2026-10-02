/* See COPYING.txt for license details. */

/*
*
* m1_cobs.c
*
* COBS encode/decode (see m1_cobs.h). Pure, host-tested.
*
* M1 Project
*
*/

#include "m1_cobs.h"

size_t m1_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap)
{
    size_t read = 0;
    size_t out = 1;      /* dst[0] is the first code byte */
    size_t code_pos = 0; /* index of the code byte being filled */
    uint8_t code = 1;

    if (dst == NULL || dstcap == 0)
    {
        return 0;
    }

    for (read = 0; read < len; read++)
    {
        if (src[read] == 0u)
        {
            dst[code_pos] = code;
            code_pos = out++;
            if (out > dstcap) { return 0; }
            code = 1;
        }
        else
        {
            if (out >= dstcap) { return 0; }
            dst[out++] = src[read];
            code++;
            if (code == 0xFFu)
            {
                dst[code_pos] = code;
                code_pos = out++;
                if (out > dstcap) { return 0; }
                code = 1;
            }
        }
    }

    if (code_pos >= dstcap) { return 0; }
    dst[code_pos] = code;
    return out;
}

size_t m1_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap)
{
    size_t read = 0;
    size_t write = 0;

    if (dst == NULL)
    {
        return 0;
    }

    while (read < len)
    {
        uint8_t code = src[read++];
        uint8_t i;

        if (code == 0u)
        {
            return 0; /* stray delimiter inside a block: malformed */
        }
        if ((size_t)(code - 1u) > (len - read))
        {
            return 0; /* run extends past the block */
        }
        for (i = 1; i < code; i++)
        {
            if (write >= dstcap) { return 0; }
            dst[write++] = src[read++];
        }
        /* Implicit zero between groups, except after the final group. */
        if (code < 0xFFu && read < len)
        {
            if (write >= dstcap) { return 0; }
            dst[write++] = 0u;
        }
    }

    return write;
}
