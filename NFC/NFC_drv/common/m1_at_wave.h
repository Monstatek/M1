/*============================================================================*/
/**
 * @file    m1_at_wave.h
 * @brief   ISO14443-A card load-modulation waveform encoder (pure logic).
 *          Host-tested. Used by the transparent-mode {At} bit-bang transmitter.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_AT_WAVE_H
#define M1_AT_WAVE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One "slot" = one half sub-carrier period (fc/32). 8 slots per bit-half, so a
 * bit period is 16 slots. A modulated half toggles 1,0,1,0..; a quiet half is 0. */
#define M1_AT_SLOTS_PER_HALF 8u
#define M1_AT_SLOTS_PER_BIT  (2u * M1_AT_SLOTS_PER_HALF)

/* Build the per-slot modulation pattern for a {data + custom-parity} frame:
 *   1 start bit ('1') + for each byte: 8 data bits LSB-first, then 1 parity bit.
 * slots[] receives 1 (modulated/high) or 0 (quiet/low) per slot.
 * Manchester convention (VALIDATE ON READER/SCOPE): logic 1 -> first half,
 * logic 0 -> second half, start bit -> '1'.
 * Returns the number of slots written, or 0 on overflow / bad args. */
size_t m1_at_wave_build(const uint8_t *data, const uint8_t *parity, uint8_t nbytes,
                        uint8_t *slots, size_t max_slots);

#ifdef __cplusplus
}
#endif

#endif /* M1_AT_WAVE_H */
