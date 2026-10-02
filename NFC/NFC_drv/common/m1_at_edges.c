/*============================================================================*/
/**
 * @file    m1_at_edges.c
 * @brief   ISO14443-A card-response fractional edge encoder. See header.
 *          Host-validated: {At}=03CA9A82/0011 -> 318 edges, 349.25us (-0.00%).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*============================================================================*/
#include "m1_at_edges.h"

#define AT_FC        13560000u          /* 13.56 MHz carrier                    */
#define AT_PB15_HI   (1u << 15)         /* GPIOB->BSRR set   PB15 high          */
#define AT_PB15_LO   (1u << (15 + 16))  /* GPIOB->BSRR reset PB15 low           */

/* Sub-carrier period units, in 1/fc: X8 = 8/fc (half sub-carrier), and the two
 * bit-boundary compensation periods X8_X9 = 72/fc, X8_X8 = 64/fc. */
#define U_X8     8u
#define U_X8_X9  72u
#define U_X8_X8  64u

typedef struct {
    uint32_t *arr;
    uint32_t *bsrr;
    size_t    i;
    size_t    max;
    uint32_t  tmr;      /* timer clock Hz            */
    uint64_t  rem;      /* fractional remainder      */
    uint32_t  tot;      /* summed period cycles      */
} at_ec_t;

static int at_emit(at_ec_t *e, uint32_t units, int level)
{
    if (e->i >= e->max) { return -1; }
    uint64_t num = (uint64_t)units * (uint64_t)e->tmr + e->rem;
    uint32_t cyc = (uint32_t)(num / AT_FC);
    e->rem = num % AT_FC;
    if (cyc == 0u) { cyc = 1u; }
    e->arr[e->i]  = (uint32_t)(cyc - 1u);
    e->bsrr[e->i] = level ? (uint32_t)AT_PB15_HI : (uint32_t)AT_PB15_LO;
    e->tot += cyc;
    e->i++;
    return 0;
}

static int at_emit_bit(at_ec_t *e, int bit)
{
    int lv;
    if (bit) {
        /* '1': sub-carrier in the FIRST half. 7 X8 halves + X8_X9 boundary. */
        lv = 1;
        for (int k = 0; k < 7; k++) { if (at_emit(e, U_X8, lv)) { return -1; } lv ^= 1; }
        if (at_emit(e, U_X8_X9, lv)) { return -1; }
    } else {
        /* '0': sub-carrier in the SECOND half. X8_X8 boundary + 8 X8 halves. */
        lv = 0;
        if (at_emit(e, U_X8_X8, lv)) { return -1; }
        lv ^= 1;
        for (int k = 0; k < 8; k++) { if (at_emit(e, U_X8, lv)) { return -1; } lv ^= 1; }
    }
    return 0;
}

size_t m1_at_edges_build(const uint8_t *data, const uint8_t *par, uint8_t nbytes,
                         uint32_t *arr, uint32_t *bsrr, size_t max_edges,
                         uint32_t timer_hz, uint32_t *total_cyc_out)
{
    if ((data == NULL) || (par == NULL) || (arr == NULL) || (bsrr == NULL)) { return 0; }

    at_ec_t e;
    e.arr = arr; e.bsrr = bsrr; e.i = 0; e.max = max_edges;
    e.tmr = timer_hz; e.rem = 0; e.tot = 0;

    if (at_emit_bit(&e, 1)) { return 0; }                 /* SOF / start bit */

    for (uint8_t b = 0; b < nbytes; b++) {
        for (uint8_t k = 0; k < 8; k++) {                 /* data LSB-first */
            if (at_emit_bit(&e, (data[b] >> k) & 1u)) { return 0; }
        }
        if (at_emit_bit(&e, par[b] & 1u)) { return 0; }   /* encrypted parity */
    }

    if (total_cyc_out != NULL) { *total_cyc_out = e.tot; }
    return e.i;
}
