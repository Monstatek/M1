/*
 * ce_mfc_xport.c - MIFARE Classic card-emulation transport framing.
 *                  See ce_mfc_xport.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bit ordering is identical to the initiator-side framing in nfc_poller.c
 * (mfc_pack_bits / mfc_unpack_bits), which is hardware-proven on the ST25R3916,
 * so the same raw-parity transceive flags apply on the listener side.
 */
#include "ce_mfc_xport.h"
#include <string.h>

uint8_t ce_mfc_odd_parity(uint8_t b)
{
    /* odd parity = 1 when the number of set bits is even. */
    b ^= b >> 4;
    b ^= b >> 2;
    b ^= b >> 1;
    return (uint8_t)((~b) & 1u);
}

uint16_t ce_mfc_pack_bits(uint8_t *out, const uint8_t *data, const uint8_t *par, uint8_t n)
{
    uint16_t bit = 0;
    if (out == NULL || data == NULL || par == NULL) return 0;
    (void)memset(out, 0x00, (size_t)((n * 9 + 7) / 8));
    for (uint8_t i = 0; i < n; i++) {
        for (uint8_t j = 0; j < 8; j++) {
            if (((data[i] >> j) & 1U) != 0U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
            bit++;
        }
        if ((par[i] & 1U) != 0U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
        bit++;
    }
    return bit;
}

uint8_t ce_mfc_unpack_bits(const uint8_t *in, uint16_t rxbits,
                           uint8_t *data, uint8_t *par, uint8_t maxn)
{
    uint8_t  n   = 0;
    uint16_t bit = 0;
    if (in == NULL || data == NULL || par == NULL) return 0;
    while (((bit + 9U) <= rxbits) && (n < maxn)) {
        uint8_t b = 0;
        for (uint8_t j = 0; j < 8; j++) {
            if (((in[bit >> 3] >> (bit & 7U)) & 1U) != 0U) b |= (uint8_t)(1U << j);
            bit++;
        }
        data[n] = b;
        par[n]  = (uint8_t)((in[bit >> 3] >> (bit & 7U)) & 1U);
        bit++;
        n++;
    }
    return n;
}

uint16_t ce_mfc_pack_ack4(uint8_t *out, uint8_t nibble)
{
    if (out == NULL) return 0;
    out[0] = (uint8_t)(nibble & 0x0Fu);   /* 4 bits, LSB first */
    return 4;
}
