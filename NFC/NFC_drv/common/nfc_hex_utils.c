/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    nfc_hex_utils.c
 * @brief   See nfc_hex_utils.h.
 */
/*============================================================================*/
#include "nfc_hex_utils.h"
#include <stdlib.h>

bool nfc_hex_nibbles_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len; i++) {
        char pair[3] = { hex[i * 2], hex[(i * 2) + 1], '\0' };
        char *end = NULL;
        long v = strtol(pair, &end, 16);
        if (end != &pair[2]) { return false; }   /* a non-hex character got through */
        out[i] = (uint8_t)v;
    }
    return true;
}
