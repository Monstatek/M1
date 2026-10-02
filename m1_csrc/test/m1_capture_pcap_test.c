/*
 * m1_capture_pcap_test.c - host tests for the CAP_FRAME_BATCH -> PCAPNG pipeline.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_capture_pcap.c m1_csrc/m1_pcapng.c m1_csrc/m1_radiotap.c \
 *      m1_csrc/m1_capture_frame.c m1_csrc/test/m1_capture_pcap_test.c \
 *      -o /tmp/cap_pcap_test && /tmp/cap_pcap_test
 *
 * Verifies: a batch of concatenated capframe records is decoded into the right
 * number of EPBs; metadata maps into a radiotap header (channel + dBm signal);
 * a truncated record is reported as a decode error; and the produced pcapng
 * re-parses (SHB/IDB/EPB block magics + mirrored block lengths).
 */
#include "m1_capture_pcap.h"
#include "m1_capture_frame.h"
#include "m1_pcapng.h"
#include "m1_radiotap.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A short beacon-ish frame. */
static const uint8_t frame_a[] = {
    0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,
    0x10, 0x00, 0x01, 0x02, 0x03, 0x04
};
/* A short data-ish frame. */
static const uint8_t frame_b[] = {
    0x08, 0x41, 0x2C, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE
};

static m1_capframe_meta_t mk_meta(uint16_t len, uint8_t ch, int8_t rssi, uint8_t phy)
{
    m1_capframe_meta_t m;
    memset(&m, 0, sizeof(m));
    m.pkt_class = M1_CAPFRAME_CLASS_MGMT;
    m.primary_channel = ch;
    m.sec_channel = M1_CAPFRAME_SEC_NONE;
    m.rssi_dbm = rssi;
    m.phy_mode = phy;
    m.orig_len = len;
    m.cap_len = len;
    m.frame_seq = 0;
    m.ts_usec = 0x0102030405ULL;
    return m;
}

/* Build a CAP_FRAME_BATCH payload of two concatenated capframe records. */
static size_t build_batch(uint8_t *out, size_t cap)
{
    size_t off = 0;
    m1_capframe_meta_t ma = mk_meta((uint16_t)sizeof(frame_a), 6, -42, M1_CAPFRAME_PHY_11G);
    m1_capframe_meta_t mb = mk_meta((uint16_t)sizeof(frame_b), 6, -71, M1_CAPFRAME_PHY_11B);
    size_t n;
    n = m1_capframe_encode(&ma, frame_a, &out[off], cap - off); off += n;
    n = m1_capframe_encode(&mb, frame_b, &out[off], cap - off); off += n;
    return off;
}

static void test_radiotap_mapping(void)
{
    m1_capframe_meta_t m = mk_meta(10, 6, -55, M1_CAPFRAME_PHY_11G);
    m1_radiotap_info_t rt;
    m1_capture_pcap_radiotap_from_meta(&m, &rt);
    CHECK(rt.has_channel, "channel present");
    CHECK(rt.channel_freq_mhz == 2437, "ch6 -> 2437 MHz");
    CHECK((rt.channel_flags & M1_RADIOTAP_CHAN_2GHZ) != 0, "2GHz flag");
    CHECK((rt.channel_flags & M1_RADIOTAP_CHAN_OFDM) != 0, "11g -> OFDM");
    CHECK(rt.has_dbm_signal && rt.dbm_signal == -55, "dBm signal");

    m.phy_mode = M1_CAPFRAME_PHY_11B;
    m1_capture_pcap_radiotap_from_meta(&m, &rt);
    CHECK((rt.channel_flags & M1_RADIOTAP_CHAN_CCK) != 0, "11b -> CCK");
}

static void test_batch_to_epbs(void)
{
    uint8_t batch[512];
    uint8_t out[4096];
    uint8_t scratch[M1_CAPTURE_PCAP_SCRATCH_MIN];
    size_t blen = build_batch(batch, sizeof(batch));
    m1_pcapng_writer_t w;
    m1_capture_pcap_counts_t c;
    m1_pcapng_section_info_t si = { "MonstaTek M1 / ESP32-C6", "fw", "Wi-Fi Capture", NULL };
    m1_pcapng_iface_info_t ii = { 262144, "esp32c6-monitor", NULL, 6 };
    uint32_t appended;

    memset(&c, 0, sizeof(c));
    m1_pcapng_writer_init(&w, out, sizeof(out));
    CHECK(m1_pcapng_write_shb(&w, &si) == 0, "SHB");
    CHECK(m1_pcapng_write_idb(&w, &ii) == 0, "IDB");
    size_t after_headers = w.len;

    appended = m1_capture_pcap_batch(&w, 0, batch, (uint16_t)blen, scratch, sizeof(scratch), &c);
    CHECK(appended == 2, "two frames appended");
    CHECK(c.frames == 2, "count frames == 2");
    CHECK(c.decode_errors == 0, "no decode errors");
    CHECK(c.overflow == 0, "no overflow");
    CHECK(m1_pcapng_writer_ok(&w), "writer ok");
    CHECK(w.len > after_headers, "bytes grew");

    /* SHB magic (block type 0x0A0D0D0A) then first block after headers is an
     * EPB (block type 0x00000006). */
    CHECK(rd_u32(&out[0]) == 0x0A0D0D0Au, "SHB block type");
    CHECK(rd_u32(&out[after_headers]) == 0x00000006u, "first packet is EPB");
    /* EPB total length field is mirrored at the block's tail. */
    {
        uint32_t blk_len = rd_u32(&out[after_headers + 4]);
        CHECK(blk_len >= 32u && (after_headers + blk_len) <= w.len, "EPB length sane");
        CHECK(rd_u32(&out[after_headers + blk_len - 4]) == blk_len, "EPB length mirrored");
    }
}

static void test_truncated_record(void)
{
    uint8_t batch[512];
    uint8_t out[4096];
    uint8_t scratch[M1_CAPTURE_PCAP_SCRATCH_MIN];
    size_t blen = build_batch(batch, sizeof(batch));
    m1_pcapng_writer_t w;
    m1_capture_pcap_counts_t c;

    memset(&c, 0, sizeof(c));
    m1_pcapng_writer_init(&w, out, sizeof(out));

    /* Chop the batch mid-way through the second record: first decodes, second
     * is malformed -> one frame + one decode error. */
    uint32_t appended = m1_capture_pcap_batch(&w, 0, batch, (uint16_t)(blen - 5),
                                              scratch, sizeof(scratch), &c);
    CHECK(appended == 1, "one frame before truncation");
    CHECK(c.decode_errors == 1, "truncation counted");
}

static void test_writer_overflow(void)
{
    uint8_t batch[512];
    uint8_t tiny[40]; /* too small even for one EPB */
    uint8_t scratch[M1_CAPTURE_PCAP_SCRATCH_MIN];
    size_t blen = build_batch(batch, sizeof(batch));
    m1_pcapng_writer_t w;
    m1_capture_pcap_counts_t c;

    memset(&c, 0, sizeof(c));
    m1_pcapng_writer_init(&w, tiny, sizeof(tiny));
    uint32_t appended = m1_capture_pcap_batch(&w, 0, batch, (uint16_t)blen,
                                              scratch, sizeof(scratch), &c);
    CHECK(appended == 0, "nothing fits");
    CHECK(c.overflow == 1, "overflow reported");
}

int main(void)
{
    test_radiotap_mapping();
    test_batch_to_epbs();
    test_truncated_record();
    test_writer_overflow();
    printf("m1_capture_pcap: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
