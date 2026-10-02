/*
 * at_edges_equiv_test.c - independent waveform-equivalence test for the
 * production ISO14443-A {At} edge encoder (m1_at_edges_build).
 *
 * It does not use the production generator as its own oracle. The reference
 * implements the expected ISO14443-A load-modulation waveform:
 *   set_bit(sig, bit): start_level = bit; then add_period() appends a segment at
 *   the current level and toggles. bit '1' = 7 * T_SIG_X8(8/fc) [start high] +
 *   T_SIG_X8_X9(72/fc); bit '0' = T_SIG_X8_X8(64/fc) [start low] + 8 * T_SIG_X8.
 *   Frame = SOF('1') then, per byte, 8 data bits LSB-first + 1 parity bit.
 *
 * Production comparison uses timer_hz = fc (13.56 MHz) so the fractional encoder
 * yields arr[i]+1 == duration in 1/fc units exactly (num = units*fc, rem stays 0).
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/m1_at_edges.c \
 *      NFC/NFC_drv/common/test/at_edges_equiv_test.c -o /tmp/at_equiv && /tmp/at_equiv
 */
#include "m1_at_edges.h"
#include <stdio.h>
#include <string.h>

#define FC        13560000u              /* carrier: compare in 1/fc units      */
#define PB15_HI   (1u << 15)             /* GPIOB->BSRR set   PB15 high          */
#define PB15_LO   (1u << (15 + 16))      /* GPIOB->BSRR reset PB15 low           */
#define MAXSEG    512

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

typedef struct { int level; unsigned units; } seg_t;

/* ---- Independent waveform reference (segments in level + 1/fc units) ---- */
static void ref_bit(seg_t *s, int *n, int bit)
{
    int lv = bit;                                   /* start_level = bit */
    if (bit) {
        for (int k = 0; k < 7; k++) { s[*n].level = lv; s[*n].units = 8u; (*n)++; lv ^= 1; }
        s[*n].level = lv; s[*n].units = 72u; (*n)++;    /* lv == 0 here            */
    } else {
        s[*n].level = lv; s[*n].units = 64u; (*n)++;    /* lv == 0                 */
        lv ^= 1;                                        /* -> 1                    */
        for (int k = 0; k < 8; k++) { s[*n].level = lv; s[*n].units = 8u; (*n)++; lv ^= 1; }
    }
}

static int ref_build(const uint8_t *data, const uint8_t *par, int nbytes, seg_t *s)
{
    int n = 0;
    ref_bit(s, &n, 1);                                  /* SOF = '1'               */
    for (int b = 0; b < nbytes; b++) {
        for (int k = 0; k < 8; k++) { ref_bit(s, &n, (data[b] >> k) & 1); }  /* LSB-first */
        ref_bit(s, &n, par[b] & 1);                     /* parity after each byte  */
    }
    return n;
}

/* Coalesce adjacent equal-level segments -> actual level-changing edges. */
static int coalesce(const seg_t *in, int n, seg_t *out)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m > 0 && out[m - 1].level == in[i].level) { out[m - 1].units += in[i].units; }
        else { out[m].level = in[i].level; out[m].units = in[i].units; m++; }
    }
    return m;
}

/* Convert one production build to a segment list; returns count or -1 on bad level. */
static int prod_build(const uint8_t *data, const uint8_t *par, int nbytes, seg_t *s)
{
    uint32_t arr[MAXSEG], bsrr[MAXSEG], tot = 0;
    size_t n = m1_at_edges_build(data, par, (uint8_t)nbytes, arr, bsrr, MAXSEG, FC, &tot);
    if (n < 2) { return -1; }
    for (size_t i = 0; i < n; i++) {
        int level;
        if (bsrr[i] == PB15_HI) { level = 1; }
        else if (bsrr[i] == PB15_LO) { level = 0; }
        else { return -2; }                             /* polarity/mapping error  */
        s[i].level = level;
        s[i].units = arr[i] + 1u;                       /* timer_hz==fc => exact   */
    }
    return (int)n;
}

static void run_vector(const char *name, const uint8_t data[4], const uint8_t par[4])
{
    printf("[vector] %s data=%02X%02X%02X%02X par=%d%d%d%d\n",
           name, data[0], data[1], data[2], data[3], par[0], par[1], par[2], par[3]);

    seg_t ref[MAXSEG], prod[MAXSEG];
    int nr = ref_build(data, par, 4, ref);
    int np = prod_build(data, par, 4, prod);

    CHECK(np > 0, "production build succeeded with a valid HI/LO polarity mapping");
    CHECK(np == nr, "production segment count == independent reference");

    /* Full ordered segment equality: level AND duration, every segment. */
    int mismatch = -1;
    if (np == nr) {
        for (int i = 0; i < nr; i++) {
            if (prod[i].level != ref[i].level || prod[i].units != ref[i].units) { mismatch = i; break; }
        }
    }
    if (mismatch >= 0) {
        printf("    first divergence at segment %d: prod(level=%d,units=%u) vs ref(level=%d,units=%u)\n",
               mismatch, prod[mismatch].level, prod[mismatch].units,
               ref[mismatch].level, ref[mismatch].units);
    }
    CHECK(mismatch < 0, "every segment matches the reference in level + duration");

    /* Property checks on the production stream. */
    CHECK(prod[0].level == 1, "initial MOSI level is HIGH (SOF rising edge vs preload-low)");
    CHECK(prod[np - 1].level == 0, "final MOSI level is LOW");

    int zero_dur = 0;
    for (int i = 0; i < np; i++) { if (prod[i].units == 0) { zero_dur = 1; } }
    CHECK(!zero_dur, "no zero-duration segment");

    unsigned tot_prod = 0, tot_ref = 0;
    for (int i = 0; i < np; i++) { tot_prod += prod[i].units; }
    for (int i = 0; i < nr; i++) { tot_ref += ref[i].units; }
    unsigned nbits = 1u + 4u * 9u;                      /* SOF + 4*(8 data + parity) */
    CHECK(tot_prod == nbits * 128u, "total duration == nbits * 128/fc");
    CHECK(tot_prod == tot_ref, "total duration equals reference");

    /* Coalesced (actual level-changing) edges must also match, and contain no
     * redundant consecutive same-level entries. */
    seg_t cp[MAXSEG], cr[MAXSEG];
    int mcp = coalesce(prod, np, cp);
    int mcr = coalesce(ref, nr, cr);
    CHECK(mcp == mcr, "coalesced edge count == reference");
    int cmatch = 1, redundant = 0;
    for (int i = 0; i < mcp && i < mcr; i++) {
        if (cp[i].level != cr[i].level || cp[i].units != cr[i].units) { cmatch = 0; }
        if (i > 0 && cp[i].level == cp[i - 1].level) { redundant = 1; }
    }
    CHECK(cmatch, "coalesced level/duration sequence matches reference");
    CHECK(!redundant, "no consecutive redundant same-level entries after coalescing");

    /* SOF is exactly a '1' bit (first 8 raw segments). */
    seg_t sof[16]; int ns = 0; ref_bit(sof, &ns, 1);
    int sof_ok = (np >= ns);
    for (int i = 0; i < ns && sof_ok; i++) {
        if (prod[i].level != sof[i].level || prod[i].units != sof[i].units) { sof_ok = 0; }
    }
    CHECK(sof_ok, "SOF constructed as a logic-'1' bit");
    printf("    segments=%d coalesced=%d total=%u/fc (~%.2f us)\n",
           np, mcp, tot_prod, (double)tot_prod / 13.56);
}

int main(void)
{
    /* Critical known vector: {At} = 03 CA 9A 82, encrypted parity 0 0 1 1. */
    { const uint8_t d[4] = {0x03,0xCA,0x9A,0x82}; const uint8_t p[4] = {0,0,1,1};
      run_vector("At=03CA9A82", d, p); }

    /* Sanity vectors so the reference cannot pass only for 03CA9A82. */
    { const uint8_t d[4] = {0x00,0x00,0x00,0x00}; const uint8_t p[4] = {0,0,0,0};
      run_vector("all-zero", d, p); }
    { const uint8_t d[4] = {0xFF,0xFF,0xFF,0xFF}; const uint8_t p[4] = {1,1,1,1};
      run_vector("all-one", d, p); }

    /* POSTAUTH-1: an 18-byte payload (16 block + CRC) must build within bounds,
     * and the encoder must refuse (return 0) rather than overflow a small buffer. */
    {
        uint8_t d[18], p[18];
        for (int i = 0; i < 18; i++) { d[i] = (uint8_t)(i * 17 + 3); p[i] = (uint8_t)(i & 1); }
        uint32_t arr[2048], bsrr[2048], tot = 0;
        size_t n = m1_at_edges_build(d, p, 18, arr, bsrr, 2048, FC, &tot);
        printf("[vector] 18-byte payload: segments=%zu total=%u/fc (~%.2f us)\n",
               n, tot, (double)tot / 13.56);
        unsigned nbits18 = 1u + 18u * 9u;                 /* SOF + 18*(8 data+par) */
        CHECK(n >= 2 && n <= 1536, "18-byte payload builds within the 1536-edge bound");
        CHECK(tot == nbits18 * 128u, "18-byte payload total == nbits*128/fc");
        /* Bounded: a too-small edge budget must fail cleanly, never overflow. */
        size_t nz = m1_at_edges_build(d, p, 18, arr, bsrr, 100, FC, &tot);
        CHECK(nz == 0, "overflow guard: undersized buffer returns 0 (no overflow)");
        /* First byte of a 4-byte build is unaffected by the generalization. */
        const uint8_t a4[4] = {0x03,0xCA,0x9A,0x82}, p4[4] = {0,0,1,1};
        uint32_t a2[512], b2[512], t2 = 0;
        size_t n4 = m1_at_edges_build(a4, p4, 4, a2, b2, 512, FC, &t2);
        CHECK(n4 == 318 && t2 == 37u * 128u, "4-byte {At} still 318 edges / 4736 fc");
    }

    printf("\n%s: %d passed, %d failed\n", g_fail ? "RESULT" : "OK", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
