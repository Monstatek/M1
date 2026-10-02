/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_block_render.c
 * @brief   See mfc_block_render.h.
 */
/*============================================================================*/
#include "mfc_block_render.h"
#include <stdio.h>
#include <string.h>

void mfc_block_format_rows(const uint8_t *block16, char rows[2][24])
{
    for (int r = 0; r < 2; r++) {
        const uint8_t *d = (block16 != NULL) ? (block16 + (r * 8)) : NULL;
        char *p = rows[r];

        for (int g = 0; g < 4; g++) {
            if (g > 0) { *p++ = ' '; }
            if (d != NULL) {
                snprintf(p, 5, "%02X%02X", d[g * 2], d[(g * 2) + 1]);
            } else {
                memcpy(p, "????", 4);
            }
            p += 4;
        }
        *p = '\0';
    }
}
