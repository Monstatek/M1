/*
 * m1_capture_transport_test.c - host tests for the capture transport skeleton.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_crc32c.c m1_csrc/m1_cobs.c m1_csrc/m1_capture_transport.c \
 *      m1_csrc/test/m1_capture_transport_test.c -o /tmp/xport_test && /tmp/xport_test
 */
#include "m1_capture_transport.h"
#include "m1_crc32c.h"
#include "m1_cobs.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static void test_crc32c(void)
{
    /* Canonical CRC-32C check value for the ASCII string "123456789". */
    CHECK(m1_crc32c("123456789", 9) == 0xE3069283u, "crc32c check vector");
    CHECK(m1_crc32c("", 0) == 0u, "crc32c empty == 0");
    /* Incremental equals one-shot. */
    {
        uint32_t c = M1_CRC32C_INIT;
        c = m1_crc32c_update(c, "1234", 4);
        c = m1_crc32c_update(c, "56789", 5);
        CHECK(m1_crc32c_final(c) == 0xE3069283u, "crc32c incremental");
    }
}

static void test_cobs(void)
{
    uint8_t enc[64], dec[64];
    size_t n, m;
    static const uint8_t a_in[]  = { 0x00 };
    static const uint8_t a_out[] = { 0x01, 0x01 };
    static const uint8_t b_in[]  = { 0x11, 0x22, 0x00, 0x33 };
    static const uint8_t b_out[] = { 0x03, 0x11, 0x22, 0x02, 0x33 };
    static const uint8_t c_in[]  = { 0x11, 0x22, 0x33, 0x44 };
    static const uint8_t c_out[] = { 0x05, 0x11, 0x22, 0x33, 0x44 };

    n = m1_cobs_encode(a_in, sizeof(a_in), enc, sizeof(enc));
    CHECK(n == sizeof(a_out) && memcmp(enc, a_out, n) == 0, "cobs vec A");
    m = m1_cobs_decode(enc, n, dec, sizeof(dec));
    CHECK(m == sizeof(a_in) && memcmp(dec, a_in, m) == 0, "cobs rt A");

    n = m1_cobs_encode(b_in, sizeof(b_in), enc, sizeof(enc));
    CHECK(n == sizeof(b_out) && memcmp(enc, b_out, n) == 0, "cobs vec B");
    m = m1_cobs_decode(enc, n, dec, sizeof(dec));
    CHECK(m == sizeof(b_in) && memcmp(dec, b_in, m) == 0, "cobs rt B");

    n = m1_cobs_encode(c_in, sizeof(c_in), enc, sizeof(enc));
    CHECK(n == sizeof(c_out) && memcmp(enc, c_out, n) == 0, "cobs vec C");
    m = m1_cobs_decode(enc, n, dec, sizeof(dec));
    CHECK(m == sizeof(c_in) && memcmp(dec, c_in, m) == 0, "cobs rt C");

    /* Encoding into a too-small buffer fails cleanly. */
    {
        uint8_t zeros[300];
        memset(zeros, 0, sizeof(zeros));
        n = m1_cobs_encode(zeros, sizeof(zeros), enc, sizeof(enc)); /* overflows 64 */
        CHECK(n == 0, "cobs overflow -> 0");
    }
}

static void test_frame_roundtrip(void)
{
    uint8_t wire[M1_CAP_MAX_WIRE];
    uint8_t dbuf[M1_CAP_MAX_ENVELOPE];
    m1_cap_msg_t msg;
    static const uint8_t payload[] = { 0x00, 0x01, 0x02, 0xFF, 0x00, 0x88, 0x8E };
    size_t wlen;
    size_t i;

    wlen = m1_cap_frame_encode(M1_CAP_FRAME_BATCH, 0xBEEF, 0x01020304u,
                               payload, sizeof(payload), wire, sizeof(wire));
    CHECK(wlen > 0, "encode ok");
    CHECK(wire[wlen - 1] == 0x00, "trailing delimiter");
    for (i = 0; i + 1 < wlen; i++) { CHECK(wire[i] != 0x00, "no interior zero"); }

    /* Strip the delimiter, decode. */
    CHECK(m1_cap_frame_decode(wire, wlen - 1, dbuf, sizeof(dbuf), &msg) == M1_CAP_OK, "decode ok");
    CHECK(msg.version == M1_CAP_PROTO_VERSION, "version");
    CHECK(msg.msg_type == M1_CAP_FRAME_BATCH, "type");
    CHECK(msg.session_id == 0xBEEF, "session");
    CHECK(msg.sequence == 0x01020304u, "sequence");
    CHECK(msg.payload_len == sizeof(payload), "payload len");
    CHECK(msg.payload && memcmp(msg.payload, payload, sizeof(payload)) == 0, "payload bytes");

    /* Zero-length payload (e.g. CAP_START). */
    wlen = m1_cap_frame_encode(M1_CAP_START, 1, 0, NULL, 0, wire, sizeof(wire));
    CHECK(wlen > 0, "encode empty payload");
    CHECK(m1_cap_frame_decode(wire, wlen - 1, dbuf, sizeof(dbuf), &msg) == M1_CAP_OK, "decode empty");
    CHECK(msg.payload_len == 0 && msg.payload == NULL, "empty payload");
}

static void test_frame_corruption(void)
{
    uint8_t wire[M1_CAP_MAX_WIRE];
    uint8_t dbuf[M1_CAP_MAX_ENVELOPE];
    m1_cap_msg_t msg;
    static const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    size_t wlen = m1_cap_frame_encode(M1_CAP_STATS, 7, 42, payload, sizeof(payload), wire, sizeof(wire));

    CHECK(wlen > 0, "encode for corruption");
    /* Flip a bit in the middle of the COBS block -> CRC must catch it. */
    wire[3] ^= 0x40u;
    {
        m1_cap_status_t st = m1_cap_frame_decode(wire, wlen - 1, dbuf, sizeof(dbuf), &msg);
        CHECK(st == M1_CAP_ERR_CRC || st == M1_CAP_ERR_FORMAT, "corruption detected");
    }

    /* Wrong version byte -> ERR_VERSION (rebuild, then patch decoded version via
     * re-encoding a bad version is not exposed; instead craft a block whose CRC
     * matches but version is wrong is impossible, so check the format guard on a
     * too-short block). */
    CHECK(m1_cap_frame_decode(wire, 3, dbuf, sizeof(dbuf), &msg) == M1_CAP_ERR_FORMAT, "short block format err");
}

static void test_streaming_resync(void)
{
    m1_cap_rx_stream_t rx;
    uint8_t wire1[M1_CAP_MAX_WIRE], wire2[M1_CAP_MAX_WIRE];
    uint8_t block[M1_CAP_MAX_WIRE];
    uint8_t dbuf[M1_CAP_MAX_ENVELOPE];
    m1_cap_msg_t msg;
    size_t w1, w2, i, n;
    int frames_ok = 0;
    static const uint8_t p1[] = { 1, 2, 3 };
    static const uint8_t p2[] = { 9, 8, 7, 6 };

    w1 = m1_cap_frame_encode(M1_CAP_FRAME_BATCH, 5, 100, p1, sizeof(p1), wire1, sizeof(wire1));
    w2 = m1_cap_frame_encode(M1_CAP_FRAME_BATCH, 5, 101, p2, sizeof(p2), wire2, sizeof(wire2));
    CHECK(w1 > 0 && w2 > 0, "encode stream frames");

    m1_cap_rx_init(&rx);

    /* Leading garbage (no delimiter) then a delimiter forces a resync: the
     * partial garbage block is non-empty, so it is delivered but fails to
     * decode; the two real frames after it decode cleanly. */
    {
        static const uint8_t garbage[] = { 0xAA, 0xBB, 0xCC };
        for (i = 0; i < sizeof(garbage); i++)
        {
            (void)m1_cap_rx_push(&rx, garbage[i], block, sizeof(block));
        }
        n = m1_cap_rx_push(&rx, 0x00, block, sizeof(block)); /* close garbage */
        if (n > 0)
        {
            /* garbage decodes to junk or format error - must NOT be OK */
            CHECK(m1_cap_frame_decode(block, n, dbuf, sizeof(dbuf), &msg) != M1_CAP_OK, "garbage rejected");
        }
    }

    for (i = 0; i < w1; i++)
    {
        n = m1_cap_rx_push(&rx, wire1[i], block, sizeof(block));
        if (n > 0 && m1_cap_frame_decode(block, n, dbuf, sizeof(dbuf), &msg) == M1_CAP_OK) { frames_ok++; }
    }
    for (i = 0; i < w2; i++)
    {
        n = m1_cap_rx_push(&rx, wire2[i], block, sizeof(block));
        if (n > 0 && m1_cap_frame_decode(block, n, dbuf, sizeof(dbuf), &msg) == M1_CAP_OK)
        {
            frames_ok++;
            CHECK(msg.sequence == 101u, "second frame seq");
        }
    }
    CHECK(frames_ok == 2, "two frames recovered after resync");
}

static void test_streaming_overrun(void)
{
    m1_cap_rx_stream_t rx;
    uint8_t block[M1_CAP_MAX_WIRE];
    size_t i;
    m1_cap_rx_init(&rx);
    /* Push more non-delimiter bytes than the block buffer holds, then close. */
    for (i = 0; i < sizeof(rx.block) + 50u; i++)
    {
        (void)m1_cap_rx_push(&rx, 0x7Fu, block, sizeof(block));
    }
    CHECK(m1_cap_rx_push(&rx, 0x00, block, sizeof(block)) == 0, "overrun yields no frame");
    CHECK(rx.dropped_frames == 1, "overrun counted");
    /* Stream resynchronizes afterward. */
    {
        uint8_t wire[M1_CAP_MAX_WIRE], dbuf[M1_CAP_MAX_ENVELOPE];
        m1_cap_msg_t msg;
        size_t w = m1_cap_frame_encode(M1_CAP_STOP, 1, 1, NULL, 0, wire, sizeof(wire));
        size_t n = 0; int ok = 0;
        for (i = 0; i < w; i++)
        {
            n = m1_cap_rx_push(&rx, wire[i], block, sizeof(block));
            if (n > 0 && m1_cap_frame_decode(block, n, dbuf, sizeof(dbuf), &msg) == M1_CAP_OK) { ok = 1; }
        }
        CHECK(ok == 1, "resync after overrun");
    }
}

static void test_sequence(void)
{
    m1_cap_seq_t s;
    m1_cap_seq_init(&s);
    CHECK(m1_cap_seq_observe(&s, 10) == M1_SEQ_OK, "first ok");
    CHECK(m1_cap_seq_observe(&s, 11) == M1_SEQ_OK, "next ok");
    CHECK(m1_cap_seq_observe(&s, 15) == M1_SEQ_GAP, "gap");
    CHECK(s.gaps == 3, "gap count 3");        /* 12,13,14 missing */
    CHECK(m1_cap_seq_observe(&s, 16) == M1_SEQ_OK, "resume ok");
    CHECK(m1_cap_seq_observe(&s, 16) == M1_SEQ_DUP, "dup");
    CHECK(s.dups == 1, "dup count 1");
}

static void test_credits(void)
{
    m1_cap_credit_t c;
    m1_cap_credit_init(&c, 64); /* reserve 64 bytes for control */
    m1_cap_credit_grant(&c, 1000);
    CHECK(m1_cap_credit_available(&c) == 1000, "available 1000");
    CHECK(m1_cap_credit_can_send_frame(&c, 936), "frame within pool");
    CHECK(!m1_cap_credit_can_send_frame(&c, 937), "frame respects reserve");
    CHECK(m1_cap_credit_can_send_control(&c, 1000), "control uses all");
    m1_cap_credit_consume(&c, 950);
    CHECK(m1_cap_credit_available(&c) == 50, "available 50");
    /* Below the reserve: no frame bytes, but control (<=50) still allowed. */
    CHECK(!m1_cap_credit_can_send_frame(&c, 1), "starved: no frame");
    CHECK(m1_cap_credit_can_send_control(&c, 50), "starved: control ok");
    m1_cap_credit_grant(&c, 200);
    CHECK(m1_cap_credit_available(&c) == 250, "regrant");
}

static void test_fsm(void)
{
    m1_cap_state_t s = M1_CAP_STATE_CONSOLE;
    /* Happy path. */
    s = m1_cap_fsm_next(s, M1_CAP_EV_SEND_OPEN);   CHECK(s == M1_CAP_STATE_OPENING, "->opening");
    s = m1_cap_fsm_next(s, M1_CAP_EV_RX_READY);    CHECK(s == M1_CAP_STATE_READY, "->ready");
    s = m1_cap_fsm_next(s, M1_CAP_EV_SEND_START);  CHECK(s == M1_CAP_STATE_RUNNING, "->running");
    s = m1_cap_fsm_next(s, M1_CAP_EV_SEND_STOP);   CHECK(s == M1_CAP_STATE_STOPPING, "->stopping");
    s = m1_cap_fsm_next(s, M1_CAP_EV_RX_STOPPED);  CHECK(s == M1_CAP_STATE_CONSOLE, "->console");

    /* Timeout/error from every active state falls back to CONSOLE. */
    CHECK(m1_cap_fsm_next(M1_CAP_STATE_OPENING,  M1_CAP_EV_TIMEOUT) == M1_CAP_STATE_CONSOLE, "opening timeout");
    CHECK(m1_cap_fsm_next(M1_CAP_STATE_READY,    M1_CAP_EV_ERROR)   == M1_CAP_STATE_CONSOLE, "ready error");
    CHECK(m1_cap_fsm_next(M1_CAP_STATE_RUNNING,  M1_CAP_EV_ERROR)   == M1_CAP_STATE_CONSOLE, "running error");
    CHECK(m1_cap_fsm_next(M1_CAP_STATE_STOPPING, M1_CAP_EV_TIMEOUT) == M1_CAP_STATE_CONSOLE, "stopping timeout");

    /* Invalid transition leaves state unchanged. */
    CHECK(m1_cap_fsm_next(M1_CAP_STATE_CONSOLE, M1_CAP_EV_RX_READY) == M1_CAP_STATE_CONSOLE, "invalid ignored");
}

int main(void)
{
    test_crc32c();
    test_cobs();
    test_frame_roundtrip();
    test_frame_corruption();
    test_streaming_resync();
    test_streaming_overrun();
    test_sequence();
    test_credits();
    test_fsm();

    printf("\nm1_capture_transport_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
