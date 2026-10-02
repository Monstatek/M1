/* See COPYING.txt for license details. */
#include "nfc_dict_line.h"
#include <stdbool.h>
#include <stddef.h>

static int nfc_dict_hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return (int)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a' + 10);
    return -1;
}

static bool nfc_dict_is_ws(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

/* Common grammar only; stores, iterators and protocol authentication stay
 * separate. key_size is a caller-supplied byte count (4, 6 or 16 today).
 * Validate the whole line before writing output, including inline comments:
 * '#' after a key requires intervening whitespace. NULL output validates only.
 */
nfc_dict_line_kind_t nfc_dict_parse_line(
    const char *line, uint8_t *out, uint8_t key_size)
{
    if (line == NULL) return NFC_DICT_LINE_SKIP;
    if (key_size == 0 || key_size > 127) return NFC_DICT_LINE_BAD;
    const char *p = line;
    while (*p != '\0' && nfc_dict_is_ws(*p)) p++;
    if (*p == '\0' || *p == '#') return NFC_DICT_LINE_SKIP;

    const size_t hexlen = (size_t)key_size * 2U;
    for (size_t i = 0; i < hexlen; i++) {
        /* NUL is not hex: short lines stop here, without over-reading. */
        if (nfc_dict_hexval(p[i]) < 0) return NFC_DICT_LINE_BAD;
    }
    const char *rest = p + hexlen;
    if (*rest != '\0' && !nfc_dict_is_ws(*rest)) return NFC_DICT_LINE_BAD;
    while (*rest != '\0' && nfc_dict_is_ws(*rest)) rest++;
    if (*rest != '\0' && *rest != '#') return NFC_DICT_LINE_BAD;

    if (out != NULL) {
        for (size_t i = 0; i < key_size; i++) {
            out[i] = (uint8_t)((nfc_dict_hexval(p[2U * i]) << 4) |
                               nfc_dict_hexval(p[2U * i + 1U]));
        }
    }
    return NFC_DICT_LINE_KEY;
}
