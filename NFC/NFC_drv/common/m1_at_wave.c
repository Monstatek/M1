/*============================================================================*/
/**
 * @file    m1_at_wave.c
 * @brief   ISO14443-A card load-modulation waveform encoder. See m1_at_wave.h.
 *          Pure logic (no hardware) -- host-tested with {At}=FA EC 34 67 / 0000.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#include "m1_at_wave.h"

static size_t emit_half(uint8_t *s, size_t i, size_t max, int modulated)
{
    for (unsigned k = 0; k < M1_AT_SLOTS_PER_HALF; k++) {
        if (i >= max) return (size_t)-1;
        s[i++] = (uint8_t)(modulated ? (((k & 1u) == 0u) ? 1 : 0) : 0);
    }
    return i;
}

static size_t emit_bit(uint8_t *s, size_t i, size_t max, int bit)
{
    /* bit '1' modulates the FIRST half; bit '0' the SECOND half */
    i = emit_half(s, i, max, bit ? 1 : 0);   if (i == (size_t)-1) return i;
    i = emit_half(s, i, max, bit ? 0 : 1);   return i;
}

size_t m1_at_wave_build(const uint8_t *data, const uint8_t *parity, uint8_t nbytes,
                        uint8_t *slots, size_t max_slots)
{
    if ((data == NULL) || (parity == NULL) || (slots == NULL)) return 0;
    size_t i = 0;

    i = emit_bit(slots, i, max_slots, 1);              /* start bit */
    if (i == (size_t)-1) return 0;

    for (uint8_t b = 0; b < nbytes; b++) {
        for (uint8_t k = 0; k < 8; k++) {              /* data LSB-first */
            i = emit_bit(slots, i, max_slots, (data[b] >> k) & 1u);
            if (i == (size_t)-1) return 0;
        }
        i = emit_bit(slots, i, max_slots, parity[b] & 1u);  /* custom parity */
        if (i == (size_t)-1) return 0;
    }
    return i;
}
