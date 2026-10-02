/* See COPYING.txt for license details. */
#ifndef NFC_DICT_LINE_H_
#define NFC_DICT_LINE_H_

#include <stdint.h>

typedef enum {
    NFC_DICT_LINE_SKIP,
    NFC_DICT_LINE_KEY,
    NFC_DICT_LINE_BAD
} nfc_dict_line_kind_t;

/* Parse exactly key_size bytes of hex (1..127). Blank/comment/NULL lines
 * are SKIP. A trailing comment requires whitespace after the hex token.
 * Invalid lines leave out untouched; NULL out performs validation only.
 * Stores, iteration, credential sizes and authentication remain separate. */
nfc_dict_line_kind_t nfc_dict_parse_line(const char *line, uint8_t *out,
                                         uint8_t key_size);

#endif
