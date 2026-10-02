/*
 * m1_pcapng_test.c - host golden tests for the PCAPNG + Radiotap writers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_pcapng.c m1_csrc/m1_radiotap.c \
 *      m1_csrc/test/m1_pcapng_test.c -o /tmp/pcapng_test \
 *   && /tmp/pcapng_test [out.pcapng]
 *
 * Verifies: byte-exact Radiotap output; then builds a full capture (SHB, IDB,
 * four representative 802.11 frames as EPBs, ISB), re-parses it with an
 * INDEPENDENT validator (not the writer) checking every block's mirrored
 * length/type/magic/linktype, and writes the sample file for tshark/capinfos.
 */
#include "m1_pcapng.h"
#include "m1_radiotap.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/*============================ Radiotap golden ==============================*/

static void test_radiotap_bytes(void)
{
    m1_radiotap_info_t info;
    uint8_t out[M1_RADIOTAP_MAX_LEN];
    size_t n;
    /* Channel 6 (2437 MHz), 2.4 GHz + OFDM, RSSI -42 dBm. */
    static const uint8_t expect[13] = {
        0x00, 0x00,             /* version, pad                 */
        0x0D, 0x00,             /* it_len = 13                  */
        0x28, 0x00, 0x00, 0x00, /* present = CHANNEL|DBM_SIGNAL */
        0x85, 0x09,             /* freq = 2437                  */
        0xC0, 0x00,             /* flags = 2GHZ|OFDM (0x00C0)   */
        0xD6                    /* dBm signal = -42             */
    };

    CHECK(m1_radiotap_chan_to_freq(6) == 2437, "ch6 -> 2437");
    CHECK(m1_radiotap_chan_to_freq(1) == 2412, "ch1 -> 2412");
    CHECK(m1_radiotap_chan_to_freq(14) == 2484, "ch14 -> 2484");
    CHECK(m1_radiotap_chan_to_freq(15) == 0, "ch15 invalid");

    memset(&info, 0, sizeof(info));
    info.has_channel = true;
    info.channel_freq_mhz = m1_radiotap_chan_to_freq(6);
    info.channel_flags = M1_RADIOTAP_CHAN_2GHZ | M1_RADIOTAP_CHAN_OFDM;
    info.has_dbm_signal = true;
    info.dbm_signal = -42;

    n = m1_radiotap_build(&info, out, sizeof(out));
    CHECK(n == 13, "radiotap len 13");
    CHECK(memcmp(out, expect, 13) == 0, "radiotap byte-exact");

    /* Signal-only header: version+pad+len+present(4) + 1 byte = 9. */
    memset(&info, 0, sizeof(info));
    info.has_dbm_signal = true;
    info.dbm_signal = -70;
    n = m1_radiotap_build(&info, out, sizeof(out));
    CHECK(n == 9, "signal-only len 9");
    CHECK(out[4] == 0x20 && out[5] == 0 && out[6] == 0 && out[7] == 0, "present = DBM only");
    CHECK(out[8] == 0xBA, "-70 -> 0xBA");

    /* Empty info emits nothing. */
    memset(&info, 0, sizeof(info));
    CHECK(m1_radiotap_build(&info, out, sizeof(out)) == 0, "empty -> 0");
    /* Tiny buffer refused cleanly. */
    info.has_channel = true; info.channel_freq_mhz = 2412; info.has_dbm_signal = true;
    CHECK(m1_radiotap_build(&info, out, 4) == 0, "overflow -> 0");
}

/*======================= Representative 802.11 frames ======================*/

/* A plausible beacon: FC=0x80,0x00; broadcast DA; BSSID addr2/addr3; fixed
 * params (timestamp, interval, caps); SSID + DS-parameter tags. */
static const uint8_t frame_beacon[] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* addr1 broadcast */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,             /* addr2 BSSID     */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,             /* addr3 BSSID     */
    0x10, 0x00,                                     /* seq/frag        */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,        /* timestamp       */
    0x64, 0x00,                                     /* beacon interval */
    0x01, 0x04,                                     /* capability      */
    0x00, 0x09, 'M','o','n','s','t','a','N','e','t',/* SSID = MonstaNet */
    0x03, 0x01, 0x06                                /* DS param: ch 6  */
};

/* Probe request: FC=0x40,0x00; broadcast DA/BSSID; client addr2; wildcard SSID. */
static const uint8_t frame_probe_req[] = {
    0x40, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x0A, 0x11, 0x22, 0x33, 0x44, 0x55,             /* client */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x20, 0x00,
    0x00, 0x00,                                     /* SSID len 0 (wildcard) */
    0x01, 0x04, 0x02, 0x04, 0x0B, 0x16             /* supported rates */
};

/* Deauthentication: FC=0xC0,0x00; to client; from BSSID; reason code. */
static const uint8_t frame_deauth[] = {
    0xC0, 0x00, 0x00, 0x00,
    0x0A, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr1 client */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,             /* addr2 BSSID  */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,             /* addr3 BSSID  */
    0x30, 0x00,
    0x07, 0x00                                      /* reason 7     */
};

/* Data frame (FromDS) carrying EAPOL-Key (LLC/SNAP + EtherType 0x888E) so the
 * Wireshark `eapol` filter matches. */
static const uint8_t frame_eapol[] = {
    0x08, 0x02, 0x00, 0x00,                         /* FC data, FromDS */
    0x0A, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr1 DA client */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01,             /* addr2 BSSID     */
    0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x02,             /* addr3 SA        */
    0x40, 0x00,                                     /* seq             */
    0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E, /* LLC/SNAP EAPOL  */
    0x02, 0x03, 0x00, 0x5F,                         /* 802.1X: EAPOL-Key */
    0x02, 0x00, 0x8A, 0x00, 0x10                    /* key info (truncated) */
};

/*==================== Independent PCAPNG re-parser =========================*/

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Walk the blocks, validating structure. Returns 1 on success. Fills counts. */
static int validate_pcapng(const uint8_t *buf, size_t len,
                           int *out_epb, int *out_isb, int *out_shb, int *out_idb)
{
    size_t off = 0;
    int epb = 0, isb = 0, shb = 0, idb = 0;
    int ok = 1;

    while (off + 12u <= len)
    {
        uint32_t type = rd_u32(&buf[off]);
        uint32_t total = rd_u32(&buf[off + 4]);
        uint32_t trailing;

        if (total < 12u || (total & 3u) != 0u || off + total > len)
        {
            printf("  parse: bad block length %u at off %zu\n", total, off);
            ok = 0;
            break;
        }
        trailing = rd_u32(&buf[off + total - 4u]);
        if (trailing != total)
        {
            printf("  parse: mirrored length mismatch (%u vs %u) at off %zu\n",
                   total, trailing, off);
            ok = 0;
            break;
        }

        switch (type)
        {
        case 0x0A0D0D0Au: /* SHB */
            shb++;
            if (rd_u32(&buf[off + 8]) != 0x1A2B3C4Du)
            {
                printf("  parse: bad SHB byte-order magic\n");
                ok = 0;
            }
            break;
        case 0x00000001u: /* IDB */
            idb++;
            if (rd_u16(&buf[off + 8]) != 127u)
            {
                printf("  parse: IDB linktype != 127\n");
                ok = 0;
            }
            break;
        case 0x00000006u: /* EPB */
        {
            uint32_t caplen = rd_u32(&buf[off + 20]);
            uint32_t origlen = rd_u32(&buf[off + 24]);
            epb++;
            if (caplen > origlen)
            {
                printf("  parse: EPB caplen > origlen\n");
                ok = 0;
            }
            break;
        }
        case 0x00000005u: /* ISB */
            isb++;
            break;
        default:
            /* unknown block types are allowed by spec; just skip */
            break;
        }
        off += total;
    }

    if (off != len)
    {
        printf("  parse: trailing bytes (%zu of %zu consumed)\n", off, len);
        ok = 0;
    }
    *out_epb = epb; *out_isb = isb; *out_shb = shb; *out_idb = idb;
    return ok;
}

/*============================ Build a full file ============================*/

/* Compose [radiotap][frame] into dst, return total length. */
static uint32_t make_packet(uint8_t *dst, size_t dstcap,
                            const uint8_t *frame, size_t flen,
                            uint8_t channel, int8_t rssi)
{
    m1_radiotap_info_t info;
    size_t rlen;
    memset(&info, 0, sizeof(info));
    info.has_channel = true;
    info.channel_freq_mhz = m1_radiotap_chan_to_freq(channel);
    info.channel_flags = M1_RADIOTAP_CHAN_2GHZ | M1_RADIOTAP_CHAN_OFDM;
    info.has_dbm_signal = true;
    info.dbm_signal = rssi;
    rlen = m1_radiotap_build(&info, dst, dstcap);
    if (rlen == 0 || rlen + flen > dstcap) { return 0; }
    memcpy(dst + rlen, frame, flen);
    return (uint32_t)(rlen + flen);
}

static size_t build_sample(uint8_t *buf, size_t cap)
{
    m1_pcapng_writer_t w;
    m1_pcapng_section_info_t sec;
    m1_pcapng_iface_info_t idb;
    m1_pcapng_stats_t stats;
    uint8_t pkt[256];
    uint32_t n;

    m1_pcapng_writer_init(&w, buf, cap);

    memset(&sec, 0, sizeof(sec));
    sec.shb_hardware = "MonstaTek M1 / ESP32-C6";
    sec.shb_os = "MonstaTek M1 firmware";
    sec.shb_userappl = "MonstaTek M1 Wi-Fi Capture";
    sec.comment = "relative clock; absolute time unavailable; profile=Full Frames";
    (void)m1_pcapng_write_shb(&w, &sec);

    memset(&idb, 0, sizeof(idb));
    idb.snaplen = 262144; /* concrete snaplen; some tools reject 0 ("no limit") */
    idb.if_name = "esp32c6-monitor";
    idb.if_description = "MonstaShark fixed channel 6";
    idb.tsresol = 6;
    (void)m1_pcapng_write_idb(&w, &idb);

    n = make_packet(pkt, sizeof(pkt), frame_beacon, sizeof(frame_beacon), 6, -40);
    (void)m1_pcapng_write_epb(&w, 0, 0u, n, pkt, n, NULL);

    n = make_packet(pkt, sizeof(pkt), frame_probe_req, sizeof(frame_probe_req), 6, -63);
    (void)m1_pcapng_write_epb(&w, 0, 100000u, n, pkt, n, NULL);

    /* Marker attached to the deauth frame. */
    n = make_packet(pkt, sizeof(pkt), frame_deauth, sizeof(frame_deauth), 6, -55);
    (void)m1_pcapng_write_epb(&w, 0, 250000u, n, pkt, n, "M1 Marker 1 - 00:00:00.250");

    n = make_packet(pkt, sizeof(pkt), frame_eapol, sizeof(frame_eapol), 6, -48);
    (void)m1_pcapng_write_epb(&w, 0, 500000u, n, pkt, n, NULL);

    memset(&stats, 0, sizeof(stats));
    stats.has_starttime = true; stats.isb_starttime_usec = 0u;
    stats.has_endtime = true;   stats.isb_endtime_usec = 500000u;
    stats.has_ifrecv = true;       stats.isb_ifrecv = 4;
    stats.has_filteraccept = true; stats.isb_filteraccept = 4;
    stats.has_ifdrop = true;       stats.isb_ifdrop = 0;
    stats.has_osdrop = true;       stats.isb_osdrop = 0;
    stats.has_usrdeliv = true;     stats.isb_usrdeliv = 4;
    (void)m1_pcapng_write_isb(&w, 0, 500000u, &stats);

    CHECK(m1_pcapng_writer_ok(&w), "writer no overflow");
    return w.len;
}

static void test_pcapng_file(const char *out_path)
{
    static uint8_t buf[4096];
    size_t len;
    int epb = 0, isb = 0, shb = 0, idb = 0;

    len = build_sample(buf, sizeof(buf));
    CHECK(len > 0 && len <= sizeof(buf), "sample built");

    /* Independent structural validation. */
    CHECK(validate_pcapng(buf, len, &epb, &isb, &shb, &idb), "pcapng structure valid");
    CHECK(shb == 1, "one SHB");
    CHECK(idb == 1, "one IDB");
    CHECK(epb == 4, "four EPBs");
    CHECK(isb == 1, "one ISB");

    /* Byte-exact SHB preamble. */
    CHECK(buf[0] == 0x0A && buf[1] == 0x0D && buf[2] == 0x0D && buf[3] == 0x0A, "SHB type");
    CHECK(buf[8] == 0x4D && buf[9] == 0x3C && buf[10] == 0x2B && buf[11] == 0x1A, "SHB magic LE");

    if (out_path != NULL)
    {
        FILE *f = fopen(out_path, "wb");
        if (f != NULL)
        {
            size_t wr = fwrite(buf, 1, len, f);
            fclose(f);
            CHECK(wr == len, "sample written to disk");
            printf("  wrote %zu bytes -> %s\n", len, out_path);
        }
        else
        {
            printf("  (could not open %s for writing)\n", out_path);
        }
    }
}

int main(int argc, char **argv)
{
    const char *out = (argc > 1) ? argv[1] : NULL;

    test_radiotap_bytes();
    test_pcapng_file(out);

    printf("\nm1_pcapng_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
