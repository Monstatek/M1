/*============================================================================*/
/**
 * @file    m1_at_edges.h
 * @brief   ISO14443-A card-response edge encoder for the timer+DMA transport.
 *          Fractional periods use a remainder accumulator rather than fixed
 *          half-slots. Host-validated.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#ifndef M1_AT_EDGES_H
#define M1_AT_EDGES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the per-edge timer+DMA lists for the card response:
 *   frame = SOF ('1') + for each byte: 8 data bits LSB-first, then parity.
 *   bit '1' = 7*X8 + X8_X9 ; bit '0' = X8_X8 + 8*X8   (X8 = 8/fc sub-carrier half)
 * Durations use a remainder accumulator so the average matches fc exactly.
 *   arr[i]  = TIM ARR reload (period_cycles - 1) for edge i
 *   bsrr[i] = GPIOB->BSRR word for edge i (PB15 set high / reset low)
 * timer_hz  = timer input clock (Hz). *total_cyc_out = summed period cycles.
 * Returns the edge count, or 0 on overflow / bad args. */
size_t m1_at_edges_build(const uint8_t *data, const uint8_t *par, uint8_t nbytes,
                         uint32_t *arr, uint32_t *bsrr, size_t max_edges,
                         uint32_t timer_hz, uint32_t *total_cyc_out);

#ifdef __cplusplus
}
#endif

#endif /* M1_AT_EDGES_H */
