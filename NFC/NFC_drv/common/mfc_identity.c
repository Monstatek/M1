/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_identity.c
 * @brief   See mfc_identity.h.
 */
/*============================================================================*/
#include "mfc_identity.h"
#include <string.h>

bool mfc_identity_matches(const uint8_t *a, uint8_t a_len,
                          const uint8_t *b, uint8_t b_len)
{
    if ((a == NULL) || (b == NULL)) return false;
    if (a_len != b_len) return false;
    if (a_len == 0U) return false;   /* an empty UID never counts as a match */
    return memcmp(a, b, a_len) == 0;
}
