/*
 * mfc_harvest_test.c - host tests for the .m1h serializer + staging buffer.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run from repo root:
 *   cc -std=c11 -Wall -Wextra -O2 \
 *      NFC/NFC_drv/common/mfc_harvest.c \
 *      NFC/NFC_drv/common/test/mfc_harvest_test.c \
 *      -I NFC/NFC_drv/common -o /tmp/mh && /tmp/mh
 *   (add -fsanitize=address,undefined -fno-sanitize-recover=all for sanitizers)
 *
 * Verifies exact little-endian bytes against the .m1h spec, that CRCs match an
 * INDEPENDENT CRC-32 implementation anchored by the standard known-answer
 * ("123456789" -> 0xCBF43926), and that the 512-byte staging buffer flushes in
 * exact blocks (a 1000-sample hardnested capture).
 */
#include "mfc_harvest.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

/* ---- flush sink ---- */
static uint8_t  g_out[1u << 20];
static size_t   g_out_len;
static uint32_t g_flush_sizes[8192];
static int      g_flush_count;
static bool     g_flush_fail;

static bool flush_cb(void *ctx, const uint8_t *b, size_t n) {
    (void)ctx;
    if (g_flush_fail) return false;
    memcpy(g_out + g_out_len, b, n);
    g_out_len += n;
    g_flush_sizes[g_flush_count++] = (uint32_t)n;
    return true;
}
static void reset_sink(void) { g_out_len = 0; g_flush_count = 0; g_flush_fail = false; }

/* ---- independent CRC-32/IEEE (poly 0xEDB88320), anchored by known answer ---- */
static uint32_t ref_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_crc_known(void) {
    printf("[1] CRC-32 known answer\n");
    CHECK(ref_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u,
          "CRC-32(\"123456789\") == 0xCBF43926");
}

static void test_header(void) {
    printf("[2] file header exact bytes\n");
    reset_sink();
    mfc_harvest_t h;
    mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t info = {0};
    info.uid_len = 4;
    info.uid[0] = 0xDE; info.uid[1] = 0xAD; info.uid[2] = 0xBE; info.uid[3] = 0xEF;
    info.atqa = 0x0004; info.sak = 0x08;
    info.capture_ticks = 0x11223344u; info.capability_flags = 0x00000001u;
    info.record_count = MFC_HARVEST_COUNT_UNKNOWN;
    CHECK(mfc_harvest_begin_file(&h, &info) == MFC_HARVEST_OK, "begin_file OK");
    CHECK(mfc_harvest_finalize(&h) == MFC_HARVEST_OK, "finalize OK");
    CHECK(g_out_len == 38, "header is 38 bytes");
    CHECK(memcmp(g_out, "M1HC", 4) == 0, "magic M1HC");
    CHECK(rd16(g_out + 4) == 1, "format_version 1");
    CHECK(rd16(g_out + 6) == 38, "header_size 38");
    CHECK(rd32(g_out + 8) == 0x00000001u, "capability_flags");
    CHECK(g_out[12] == 4, "uid_len 4");
    CHECK(g_out[13] == 0xDE && g_out[16] == 0xEF, "uid bytes");
    CHECK(rd16(g_out + 23) == 0x0004u, "atqa");
    CHECK(g_out[25] == 0x08u, "sak");
    CHECK(rd32(g_out + 26) == 0x11223344u, "capture_ticks");
    CHECK(rd32(g_out + 30) == MFC_HARVEST_COUNT_UNKNOWN, "record_count");
    CHECK(rd32(g_out + 34) == ref_crc32(g_out, 34), "header_crc32 correct");
}

static void test_mfkey64(void) {
    printf("[3] MFKey64 record exact bytes + CRC\n");
    reset_sink();
    mfc_harvest_t h;
    mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t info = {0};
    info.uid_len = 4; info.record_count = 1;
    mfc_harvest_begin_file(&h, &info);
    mfc_mfkey64_record_t r = {0x3C, 0x60, 0xCE844261u, 0xF8049CCBu,
                              0x0525C84Fu, 0x9431CC40u};
    CHECK(mfc_harvest_write_mfkey64(&h, &r) == MFC_HARVEST_OK, "write_mfkey64 OK");
    CHECK(mfc_harvest_finalize(&h) == MFC_HARVEST_OK, "finalize OK");
    CHECK(g_out_len == 38 + 28, "header + 28-byte record");
    const uint8_t *rec = g_out + 38;
    CHECK(rec[0] == MFC_HARVEST_TYPE_MFKEY64, "type 1");
    CHECK(rec[1] == 1, "record_version 1");
    CHECK(rd32(rec + 2) == 28, "record_len 28");
    CHECK(rec[6] == 0x3C && rec[7] == 0x60, "block/keytype");
    CHECK(rd32(rec + 8) == 0xCE844261u, "nt");
    CHECK(rd32(rec + 12) == 0xF8049CCBu, "nr_enc");
    CHECK(rd32(rec + 16) == 0x0525C84Fu, "ar_enc");
    CHECK(rd32(rec + 20) == 0x9431CC40u, "at_enc");
    CHECK(rd32(rec + 24) == ref_crc32(rec, 24), "record_crc32 correct");
}

static void test_hardnested_1000(void) {
    printf("[4] hardnested 1000 samples: bytes, flush blocks, CRC\n");
    reset_sink();
    mfc_harvest_t h;
    mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t info = {0};
    info.uid_len = 7; info.record_count = 1;
    mfc_harvest_begin_file(&h, &info);

    mfc_card_only_hdr_t hdr = {0};
    hdr.src_block = 3;  hdr.src_keytype = 0x60;
    hdr.tgt_block = 20; hdr.tgt_keytype = 0x61;
    hdr.known_key_ref = 0;
    CHECK(mfc_harvest_begin_hardnested(&h, &hdr, 1000) == MFC_HARVEST_OK,
          "begin_hardnested OK");
    for (uint32_t i = 0; i < 1000; i++) {
        mfc_nonce_sample_t s = { 0xA5000000u | i, (uint8_t)(i & 0x0F) };
        if (mfc_harvest_add_nonce_sample(&h, &s) != MFC_HARVEST_OK) { CHECK(0, "add sample"); break; }
    }
    CHECK(mfc_harvest_end_record(&h) == MFC_HARVEST_OK, "end_record OK");
    CHECK(mfc_harvest_finalize(&h) == MFC_HARVEST_OK, "finalize OK");

    uint32_t rlen = 6 + 9 + 1000 * 5 + 4;             /* 5019 */
    CHECK(rlen == 5019, "record_len math");
    CHECK(g_out_len == 38 + rlen, "total bytes 5057");

    /* staging flushed in exact 512-byte blocks, remainder at finalize */
    bool blocks_ok = (g_flush_count == 10);
    for (int i = 0; i < g_flush_count - 1 && blocks_ok; i++)
        blocks_ok = (g_flush_sizes[i] == 512);
    blocks_ok = blocks_ok && (g_flush_sizes[g_flush_count - 1] == (38 + 5019) - 9 * 512);
    CHECK(blocks_ok, "9x512 blocks + 449-byte finalize flush");

    const uint8_t *rec = g_out + 38;
    CHECK(rec[0] == MFC_HARVEST_TYPE_HARDNESTED, "type 4");
    CHECK(rd32(rec + 2) == rlen, "record_len field");
    CHECK(rec[6] == 3 && rec[7] == 0x60 && rec[8] == 20 && rec[9] == 0x61, "src/tgt");
    CHECK(rec[10] == 0, "known_key_ref 0");
    CHECK(rd32(rec + 11) == 1000, "sample_count u32");
    /* samples begin at rec+15, 5 bytes each */
    const uint8_t *s0 = rec + 15;
    CHECK(rd32(s0) == 0xA5000000u && s0[4] == 0, "sample[0]");
    const uint8_t *s500 = rec + 15 + 500 * 5;
    CHECK(rd32(s500) == (0xA5000000u | 500u) && s500[4] == (500 & 0x0F), "sample[500]");
    const uint8_t *s999 = rec + 15 + 999 * 5;
    CHECK(rd32(s999) == (0xA5000000u | 999u) && s999[4] == (999 & 0x0F), "sample[999]");
    CHECK(rd32(rec + rlen - 4) == ref_crc32(rec, rlen - 4), "hardnested record_crc32");
}

static void test_nested_and_static(void) {
    printf("[5] nested (embedded key + dist) + static in one file\n");
    reset_sink();
    mfc_harvest_t h;
    mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t info = {0};
    info.uid_len = 4; info.record_count = 2;
    mfc_harvest_begin_file(&h, &info);

    mfc_card_only_hdr_t hdr = {0};
    hdr.src_block = 0; hdr.src_keytype = 0x60; hdr.tgt_block = 8; hdr.tgt_keytype = 0x60;
    hdr.known_key_ref = 1;
    for (int i = 0; i < 6; i++) hdr.known_key[i] = (uint8_t)(0xF0 + i);
    CHECK(mfc_harvest_begin_nested(&h, &hdr, 0x11223344u, 3) == MFC_HARVEST_OK, "begin_nested");
    for (uint32_t i = 0; i < 3; i++) {
        mfc_nested_sample_t s = { 0xB0000000u | i, (uint8_t)i, (uint16_t)(1000 + i) };
        mfc_harvest_add_nested_sample(&h, &s);
    }
    CHECK(mfc_harvest_end_record(&h) == MFC_HARVEST_OK, "end nested");

    uint32_t nlen = 6 + (5 + 6) + 4 + 2 + 3 * 7 + 4;   /* framing+hdr+key+anchor+count+3samp+crc */
    size_t off2 = 38 + nlen;

    /* static record after the nested one */
    mfc_card_only_hdr_t hdr2 = {0};
    hdr2.src_block = 0; hdr2.src_keytype = 0x60; hdr2.tgt_block = 12; hdr2.tgt_keytype = 0x61;
    CHECK(mfc_harvest_begin_static(&h, &hdr2, 0x01200145u, 2) == MFC_HARVEST_OK, "begin_static");
    mfc_nonce_sample_t a = {0xC0000000u, 0x0A}, b = {0xC0000001u, 0x0B};
    mfc_harvest_add_nonce_sample(&h, &a);
    mfc_harvest_add_nonce_sample(&h, &b);
    CHECK(mfc_harvest_end_record(&h) == MFC_HARVEST_OK, "end static");
    CHECK(mfc_harvest_finalize(&h) == MFC_HARVEST_OK, "finalize");

    /* verify AFTER finalize (bytes are only in g_out once flushed) */
    const uint8_t *rec = g_out + 38;
    CHECK(rd32(rec + 2) == nlen, "nested record_len");
    CHECK(rec[6] == 0 && rec[10] == 1, "src + known_key_ref=1");
    CHECK(rec[11] == 0xF0 && rec[16] == 0xF5, "embedded key bytes");
    CHECK(rd32(rec + 17) == 0x11223344u, "anchor_nt");
    CHECK(rd16(rec + 21) == 3, "sample_count u16");
    const uint8_t *ns = rec + 23;                      /* first nested sample */
    CHECK(rd32(ns) == 0xB0000000u && ns[4] == 0 && rd16(ns + 5) == 1000, "nested sample[0] (nt,par,dist)");
    CHECK(rd32(rec + nlen - 4) == ref_crc32(rec, nlen - 4), "nested CRC");

    const uint8_t *rs = g_out + off2;
    uint32_t slen = 6 + 5 + 4 + 2 + 2 * 5 + 4;         /* no key */
    CHECK(rs[0] == MFC_HARVEST_TYPE_STATIC && rd32(rs + 2) == slen, "static type+len");
    CHECK(rd32(rs + 11) == 0x01200145u, "static_nt");
    CHECK(rd16(rs + 15) == 2, "static sample_count");
    CHECK(rd32(rs + 17) == 0xC0000000u && rs[21] == 0x0A, "static sample[0]");
    CHECK(rd32(rs + slen - 4) == ref_crc32(rs, slen - 4), "static CRC");
}

static void test_errors(void) {
    printf("[6] error / state / bounds paths\n");
    mfc_harvest_t h;

    /* NULL flush -> INVALID_INPUT sticky */
    mfc_harvest_init(&h, NULL, NULL);
    CHECK(mfc_harvest_status(&h) == MFC_HARVEST_INVALID_INPUT, "NULL flush rejected");

    /* bad uid_len */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t bad = {0}; bad.uid_len = 5;
    CHECK(mfc_harvest_begin_file(&h, &bad) == MFC_HARVEST_INVALID_INPUT, "bad uid_len");

    /* add sample before begin_record -> STATE */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_file_info_t info = {0}; info.uid_len = 4;
    mfc_harvest_begin_file(&h, &info);
    mfc_nonce_sample_t s = {1, 2};
    CHECK(mfc_harvest_add_nonce_sample(&h, &s) == MFC_HARVEST_STATE, "sample before begin");

    /* too many samples -> BOUNDS */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_begin_file(&h, &info);
    mfc_card_only_hdr_t hdr = {0};
    mfc_harvest_begin_hardnested(&h, &hdr, 1);
    mfc_harvest_add_nonce_sample(&h, &s);
    CHECK(mfc_harvest_add_nonce_sample(&h, &s) == MFC_HARVEST_BOUNDS, "overfill rejected");

    /* under-filled record at end -> BOUNDS */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_begin_file(&h, &info);
    mfc_harvest_begin_hardnested(&h, &hdr, 3);
    mfc_harvest_add_nonce_sample(&h, &s);
    CHECK(mfc_harvest_end_record(&h) == MFC_HARVEST_BOUNDS, "underfill rejected");

    /* finalize with a record still open -> STATE */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_begin_file(&h, &info);
    mfc_harvest_begin_hardnested(&h, &hdr, 1);
    CHECK(mfc_harvest_finalize(&h) == MFC_HARVEST_STATE, "finalize while record open");

    /* flush failure propagates (sticky) */
    reset_sink(); mfc_harvest_init(&h, flush_cb, NULL);
    mfc_harvest_begin_file(&h, &info);
    mfc_harvest_begin_hardnested(&h, &hdr, 1000);
    g_flush_fail = true;
    for (uint32_t i = 0; i < 1000; i++) { mfc_nonce_sample_t x = {i, 0}; mfc_harvest_add_nonce_sample(&h, &x); }
    CHECK(mfc_harvest_status(&h) == MFC_HARVEST_FLUSH_FAILED, "flush failure sticky");
}

int main(void) {
    printf("=== .m1h harvester serializer host tests ===\n");
    test_crc_known();
    test_header();
    test_mfkey64();
    test_hardnested_1000();
    test_nested_and_static();
    test_errors();
    printf("===========================================\n");
    printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
