/*
 * m1_capture_frame_test.c - host tests for the per-frame capture record codec.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_capture_frame.c m1_csrc/test/m1_capture_frame_test.c \
 *      -o /tmp/capframe_test && /tmp/capframe_test
 */
#include "m1_capture_frame.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* A short but plausible beacon frame. */
static const uint8_t beacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,
    0x10, 0x00
};

static m1_capframe_meta_t mk_meta(uint32_t seq, uint16_t cap, uint16_t orig, uint8_t ch, int8_t rssi)
{
    m1_capframe_meta_t m;
    memset(&m, 0, sizeof(m));
    m.pkt_class = M1_CAPFRAME_CLASS_MGMT;
    m.primary_channel = ch;
    m.sec_channel = M1_CAPFRAME_SEC_NONE;
    m.rssi_dbm = rssi;
    m.phy_mode = M1_CAPFRAME_PHY_11G;
    m.rate_500kbps = 12; /* 6 Mbps */
    m.flags = M1_CAPFRAME_FLAG_FCS_PRESENT;
    m.orig_len = orig;
    m.cap_len = cap;
    m.frame_seq = seq;
    m.ts_usec = 0x0000000123456789ULL;
    return m;
}

static void test_roundtrip(void)
{
    uint8_t out[M1_CAPFRAME_MAX_LEN];
    m1_capframe_meta_t in = mk_meta(42, (uint16_t)sizeof(beacon), (uint16_t)sizeof(beacon), 6, -47);
    size_t n = m1_capframe_encode(&in, beacon, out, sizeof(out));
    CHECK(n == (size_t)M1_CAPFRAME_HDR_LEN + sizeof(beacon), "encode len");
    CHECK(out[0] == M1_CAPFRAME_VERSION, "version byte");

    m1_capframe_meta_t got;
    const uint8_t *raw = NULL;
    CHECK(m1_capframe_decode(out, n, &got, &raw), "decode ok");
    CHECK(got.frame_seq == 42, "seq");
    CHECK(got.primary_channel == 6, "channel");
    CHECK(got.rssi_dbm == -47, "rssi");
    CHECK(got.pkt_class == M1_CAPFRAME_CLASS_MGMT, "class");
    CHECK(got.phy_mode == M1_CAPFRAME_PHY_11G, "phy");
    CHECK(got.rate_500kbps == 12, "rate");
    CHECK(got.flags == M1_CAPFRAME_FLAG_FCS_PRESENT, "flags");
    CHECK(got.orig_len == sizeof(beacon), "orig_len");
    CHECK(got.cap_len == sizeof(beacon), "cap_len");
    CHECK(got.ts_usec == 0x0000000123456789ULL, "timestamp");
    CHECK(raw != NULL && memcmp(raw, beacon, sizeof(beacon)) == 0, "raw bytes");
}

static void test_truncated_and_empty(void)
{
    uint8_t out[M1_CAPFRAME_MAX_LEN];
    /* Truncated: cap_len < orig_len, truncated flag set. */
    m1_capframe_meta_t in = mk_meta(1, 10, 1500, 11, -70);
    in.flags |= M1_CAPFRAME_FLAG_TRUNCATED;
    size_t n = m1_capframe_encode(&in, beacon, out, sizeof(out));
    CHECK(n == (size_t)M1_CAPFRAME_HDR_LEN + 10u, "truncated len");

    m1_capframe_meta_t got;
    const uint8_t *raw = NULL;
    CHECK(m1_capframe_decode(out, n, &got, &raw), "decode truncated");
    CHECK(got.cap_len == 10 && got.orig_len == 1500, "trunc fields");
    CHECK((got.flags & M1_CAPFRAME_FLAG_TRUNCATED) != 0, "trunc flag");

    /* Zero-length capture (metadata-only, e.g. Headers Only edge). */
    m1_capframe_meta_t z = mk_meta(2, 0, 0, 1, -50);
    n = m1_capframe_encode(&z, NULL, out, sizeof(out));
    CHECK(n == (size_t)M1_CAPFRAME_HDR_LEN, "empty len");
    CHECK(m1_capframe_decode(out, n, &got, &raw), "decode empty");
    CHECK(got.cap_len == 0 && raw == NULL, "empty raw NULL");
}

static void test_bounds(void)
{
    uint8_t out[M1_CAPFRAME_MAX_LEN];
    m1_capframe_meta_t got;
    m1_capframe_meta_t in = mk_meta(1, (uint16_t)sizeof(beacon), (uint16_t)sizeof(beacon), 6, -47);

    /* Encode into a too-small buffer fails cleanly. */
    CHECK(m1_capframe_encode(&in, beacon, out, M1_CAPFRAME_HDR_LEN) == 0, "encode overflow");

    /* Oversized cap_len rejected on encode. */
    in.cap_len = M1_CAPFRAME_MAX_RAW + 1u;
    CHECK(m1_capframe_encode(&in, beacon, out, sizeof(out)) == 0, "encode oversize");

    /* Decode of a short buffer / bad version / lying cap_len. */
    size_t n = m1_capframe_encode(&(m1_capframe_meta_t){ .cap_len = (uint16_t)sizeof(beacon),
        .orig_len = (uint16_t)sizeof(beacon), .primary_channel = 6 }, beacon, out, sizeof(out));
    CHECK(n > 0, "encode for decode-bounds");
    CHECK(!m1_capframe_decode(out, M1_CAPFRAME_HDR_LEN - 1u, &got, NULL), "short buf rejected");
    out[0] = 0x7F; /* bad version */
    CHECK(!m1_capframe_decode(out, n, &got, NULL), "bad version rejected");
    out[0] = M1_CAPFRAME_VERSION;
    out[10] = 0xFF; out[11] = 0xFF; /* cap_len claims 65535 */
    CHECK(!m1_capframe_decode(out, n, &got, NULL), "lying cap_len rejected");
}

/* A CAP_FRAME_BATCH payload can concatenate several records; the STM walks
 * them back to back. */
static void test_batch_walk(void)
{
    uint8_t batch[512];
    size_t off = 0;
    uint32_t i;

    for (i = 0; i < 3; i++)
    {
        m1_capframe_meta_t m = mk_meta(100 + i, (uint16_t)sizeof(beacon),
                                       (uint16_t)sizeof(beacon), (uint8_t)(1 + i), (int8_t)(-40 - (int)i));
        size_t n = m1_capframe_encode(&m, beacon, &batch[off], sizeof(batch) - off);
        CHECK(n > 0, "batch encode");
        off += n;
    }

    /* Walk. */
    {
        size_t p = 0;
        uint32_t count = 0;
        while (p < off)
        {
            m1_capframe_meta_t m;
            const uint8_t *raw = NULL;
            if (!m1_capframe_decode(&batch[p], off - p, &m, &raw)) { break; }
            CHECK(m.frame_seq == 100 + count, "batch seq order");
            CHECK(m.primary_channel == (uint8_t)(1 + count), "batch channel");
            p += (size_t)M1_CAPFRAME_HDR_LEN + m.cap_len;
            count++;
        }
        CHECK(count == 3 && p == off, "walked all 3 records");
    }
}

int main(void)
{
    test_roundtrip();
    test_truncated_and_empty();
    test_bounds();
    test_batch_walk();
    printf("\nm1_capture_frame_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
