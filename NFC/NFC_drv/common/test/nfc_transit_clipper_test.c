/*
 * nfc_transit_clipper_test.c
 *
 * Host-compiled behavioral tests for nfc_transit_clipper.c. Every expected
 * value is computed independently from the supported byte layouts, with
 * date-conversion golden vectors computed via Python's date/timedelta
 * arithmetic (a completely independent implementation from the C port).
 *
 * Build (from repo root):
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     -I NFC/NFC_drv/common \
 *     NFC/NFC_drv/common/test/nfc_transit_clipper_test.c \
 *     NFC/NFC_drv/common/nfc_transit_clipper.c \
 *     NFC/NFC_drv/common/mf_desfire_parse.c \
 *     -o /tmp/nfc_transit_clipper_test && /tmp/nfc_transit_clipper_test
 */
#include "nfc_transit_clipper.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/*============================ detection ========================================*/

static void test_detect_card_aid(void)
{
    mf_desfire_deep_t deep;
    mf_desfire_deep_reset(&deep);
    deep.app_count = 1;
    deep.apps[0].id.id[0] = 0x90; deep.apps[0].id.id[1] = 0x11; deep.apps[0].id.id[2] = 0xF2;

    uint8_t idx = 0xFF; const char *label = NULL;
    CHECK(nfc_transit_detect_clipper(&deep, &idx, &label), "Clipper card AID detected");
    CHECK(idx == 0, "detected at app index 0");
    CHECK(label != NULL && strcmp(label, "Card") == 0, "label is Card");
}

static void test_detect_mobile_aid(void)
{
    mf_desfire_deep_t deep;
    mf_desfire_deep_reset(&deep);
    deep.app_count = 2;
    deep.apps[0].id.id[0] = 0xAA; deep.apps[0].id.id[1] = 0xBB; deep.apps[0].id.id[2] = 0xCC;
    deep.apps[1].id.id[0] = 0x91; deep.apps[1].id.id[1] = 0x11; deep.apps[1].id.id[2] = 0xF2;

    uint8_t idx = 0xFF; const char *label = NULL;
    CHECK(nfc_transit_detect_clipper(&deep, &idx, &label), "Clipper mobile AID detected among other apps");
    CHECK(idx == 1, "detected at app index 1, not 0");
    CHECK(label != NULL && strcmp(label, "Mobile Device") == 0, "label is Mobile Device");
}

static void test_detect_one_byte_mismatch_rejected(void)
{
    mf_desfire_deep_t deep;
    mf_desfire_deep_reset(&deep);
    deep.app_count = 1;
    /* Last byte differs from the real Card AID (0x90 0x11 0xF2) by one bit. */
    deep.apps[0].id.id[0] = 0x90; deep.apps[0].id.id[1] = 0x11; deep.apps[0].id.id[2] = 0xF3;

    uint8_t idx = 0xFF; const char *label = NULL;
    CHECK(!nfc_transit_detect_clipper(&deep, &idx, &label), "one-byte AID mismatch must NOT false-positive as Clipper");
}

static void test_detect_no_apps_unknown(void)
{
    mf_desfire_deep_t deep;
    mf_desfire_deep_reset(&deep);
    deep.app_count = 0;
    uint8_t idx = 0xFF; const char *label = NULL;
    CHECK(!nfc_transit_detect_clipper(&deep, &idx, &label), "no applications at all -> not detected (unknown card fallback)");
}

/*============================ identity file ====================================*/

static void test_parse_identity_valid(void)
{
    uint8_t buf[5] = { 0xFF, 0x49, 0x96, 0x02, 0xD2 };
    uint32_t serial = 0;
    CHECK(nfc_transit_clipper_parse_identity(buf, sizeof(buf), &serial), "identity parse succeeds at exactly 5 bytes");
    CHECK(serial == 1234567890U, "serial == 1234567890");
}

static void test_parse_identity_too_short(void)
{
    uint8_t buf[4] = { 0xFF, 0x00, 0x00, 0x01 };
    uint32_t serial = 0xDEADBEEF;
    CHECK(!nfc_transit_clipper_parse_identity(buf, sizeof(buf), &serial), "4-byte identity body rejected, never guessed");
}

/*============================ ecash file =======================================*/

static void test_parse_cash_valid_negative_balance(void)
{
    uint8_t buf[0x14] = {0};
    buf[2] = 0x00; buf[3] = 0x64;                          /* counter = 100 */
    buf[4] = 0xEE; buf[5] = 0x6B; buf[6] = 0x28; buf[7] = 0x00; /* last_update = 4,000,000,000 */
    buf[8] = 0x12; buf[9] = 0x34;                          /* terminal = 0x1234 */
    buf[0x10] = 0x56; buf[0x11] = 0x78;                    /* txn_id = 0x5678 */
    buf[0x12] = 0xFF; buf[0x13] = 0x9C;                    /* balance = -100 */

    uint16_t counter = 0, terminal = 0, txn = 0;
    uint32_t last_update = 0;
    int16_t balance = 0;
    CHECK(nfc_transit_clipper_parse_cash(buf, sizeof(buf), &counter, &last_update, &terminal, &txn, &balance),
          "cash parse succeeds at exactly 20 bytes");
    CHECK(counter == 100U, "counter == 100");
    CHECK(last_update == 4000000000U, "last_update == 4,000,000,000");
    CHECK(terminal == 0x1234U, "terminal == 0x1234");
    CHECK(txn == 0x5678U, "txn_id == 0x5678");
    CHECK(balance == -100, "balance == -100 cents");
}

static void test_parse_cash_too_short(void)
{
    uint8_t buf[0x13] = {0}; /* one byte short of the required 0x14 */
    uint16_t counter, terminal, txn; uint32_t lu; int16_t bal;
    CHECK(!nfc_transit_clipper_parse_cash(buf, sizeof(buf), &counter, &lu, &terminal, &txn, &bal),
          "19-byte cash body rejected");
}

/*============================ ride history ======================================*/

static void build_ride_record(uint8_t *rec, uint16_t agency, int16_t fare, uint16_t vehicle,
                               uint32_t time_on, uint32_t time_off, uint16_t zone_on, uint16_t zone_off)
{
    memset(rec, 0, 32);
    rec[0] = 0x10;
    rec[2] = (uint8_t)(agency >> 8); rec[3] = (uint8_t)agency;
    rec[6] = (uint8_t)((uint16_t)fare >> 8); rec[7] = (uint8_t)fare;
    rec[0x0A] = (uint8_t)(vehicle >> 8); rec[0x0B] = (uint8_t)vehicle;
    rec[0x0C] = (uint8_t)(time_on >> 24); rec[0x0D] = (uint8_t)(time_on >> 16);
    rec[0x0E] = (uint8_t)(time_on >> 8);  rec[0x0F] = (uint8_t)time_on;
    rec[0x10] = (uint8_t)(time_off >> 24); rec[0x11] = (uint8_t)(time_off >> 16);
    rec[0x12] = (uint8_t)(time_off >> 8);  rec[0x13] = (uint8_t)time_off;
    rec[0x14] = (uint8_t)(zone_on >> 8); rec[0x15] = (uint8_t)zone_on;
    rec[0x16] = (uint8_t)(zone_off >> 8); rec[0x17] = (uint8_t)zone_off;
}

static void test_parse_rides_single_record(void)
{
    uint8_t history[64]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0], 0x0004 /*BART*/, 250, 0, 4000000000U, 0, 0x0001, 0x0002);

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; /* points at record 0 */

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 0; bool trunc = true;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, &trunc),
          "ride parse succeeds");
    CHECK(count == 1, "one ride decoded");
    CHECK(!trunc, "not truncated");
    CHECK(rides[0].agency_id == 0x0004U, "agency_id == BART");
    CHECK(rides[0].fare_cents == 250, "fare == 250 cents");
    CHECK(rides[0].has_time_on && rides[0].time_on_1900 == 4000000000U, "time_on decoded");
    CHECK(!rides[0].has_time_off, "time_off absent (zero) -> has_time_off false");
    CHECK(rides[0].zone_on_id == 1 && rides[0].zone_off_id == 2, "zones decoded");
}

static void test_parse_rides_empty_index(void)
{
    uint8_t history[64] = {0};
    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx)); /* immediate sentinel */
    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 5; bool trunc = true;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, &trunc),
          "empty index parses as zero rides, not a failure");
    CHECK(count == 0, "count == 0");
    CHECK(!trunc, "not truncated");
}

static void test_parse_rides_out_of_bounds_record_skipped(void)
{
    /* Only 32 bytes captured (one record's worth), but histidx points at
     * record_num=5 (offset 160) -- fully beyond history_len. Must be
     * skipped and flagged truncated, never fabricated. */
    uint8_t history[32]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0], 0x0004, 100, 0, 1, 0, 0, 0);

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 5;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 0; bool trunc = false;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, &trunc),
          "parse call itself still succeeds");
    CHECK(count == 0, "out-of-bounds record contributes no ride");
    CHECK(trunc, "truncated flag set for the skipped out-of-bounds record");
}

static void test_parse_rides_bad_marker_skipped(void)
{
    uint8_t history[32]; memset(history, 0, sizeof(history));
    history[0] = 0x11; /* not the 0x10 ride-record marker */
    history[2] = 0x00; history[3] = 0x04;

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9; bool trunc = true;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, &trunc),
          "parse succeeds");
    CHECK(count == 0, "bad marker byte -> record skipped, not decoded");
}

static void test_parse_rides_zero_agency_skipped(void)
{
    uint8_t history[32]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0], 0x0000 /* empty-record marker */, 100, 0, 1, 0, 0, 0);

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, NULL), "parse succeeds");
    CHECK(count == 0, "agency_id==0 record is skipped as empty");
}

static void test_parse_rides_max_rides_cap_truncates(void)
{
    uint8_t history[32 * 3]; memset(history, 0, sizeof(history));
    for (int i = 0; i < 3; i++) build_ride_record(&history[i * 32], 0x0004, 100, 0, (uint32_t)(i + 1), 0, 0, 0);

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; histidx[1] = 1; histidx[2] = 2;

    nfc_transit_ride_t rides[2]; /* cap smaller than the 3 available rides */
    uint8_t count = 0; bool trunc = false;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, 2, &count, &trunc), "parse succeeds");
    CHECK(count == 2, "capped at max_rides");
    CHECK(trunc, "truncated flag set when more rides existed than max_rides");
}

static void test_parse_rides_bad_marker_stops_walk_not_skips(void)
{
    /* Reproduces the round-4-audit-proven mismatch: an earlier version of
     * nfc_transit_clipper_parse_rides() used `continue` on a bad marker,
     * skipping past it to keep decoding later index entries. The required
     * behavior stops the whole walk the moment
     * dump_ride_event() returns false for a bad marker -- so a good record
     * listed AFTER a bad one must never be decoded, even though a good
     * record listed BEFORE it must be. None of the pre-existing
     * "*_skipped" tests above catch this because they only ever place a
     * single bad record with nothing after it. */
    uint8_t history[32 * 3]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0 * 32], 0x0004, 100, 0, 1, 0, 0, 0);   /* good, before */
    history[1 * 32 + 0] = 0x11;                                        /* bad marker */
    build_ride_record(&history[2 * 32], 0x0004, 200, 0, 2, 0, 0, 0);   /* good, after -- must be dropped */

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; histidx[1] = 1; histidx[2] = 2;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, NULL), "parse succeeds");
    CHECK(count == 1, "walk stops at the bad marker -- the good record after it is never decoded");
    CHECK(rides[0].fare_cents == 100, "only the good record BEFORE the bad marker survives");
}

static void test_parse_rides_zero_agency_stops_walk_not_skips(void)
{
    uint8_t history[32 * 3]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0 * 32], 0x0004, 100, 0, 1, 0, 0, 0);   /* good, before */
    build_ride_record(&history[1 * 32], 0x0000, 150, 0, 0, 0, 0, 0);   /* agency_id==0 */
    build_ride_record(&history[2 * 32], 0x0004, 200, 0, 2, 0, 0, 0);   /* good, after -- must be dropped */

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; histidx[1] = 1; histidx[2] = 2;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, NULL), "parse succeeds");
    CHECK(count == 1, "walk stops at the zero-agency record -- the good record after it is never decoded");
    CHECK(rides[0].fare_cents == 100, "only the good record BEFORE the empty record survives");
}

static void test_parse_rides_out_of_bounds_stops_walk_not_skips(void)
{
    /* Only 64 bytes captured (two records' worth); histidx's second entry
     * points at record_num=5 (fully OOB), its third at record_num=1
     * (in-bounds and well-formed, but listed AFTER the OOB entry). */
    uint8_t history[64]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0 * 32], 0x0004, 100, 0, 1, 0, 0, 0);   /* good, before */
    build_ride_record(&history[1 * 32], 0x0004, 200, 0, 2, 0, 0, 0);   /* good bytes, but never reached */

    uint8_t histidx[16]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; histidx[1] = 5; histidx[2] = 1;

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9; bool trunc = false;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, &trunc), "parse succeeds");
    CHECK(count == 1, "walk stops at the out-of-bounds record -- the good record after it is never decoded");
    CHECK(rides[0].fare_cents == 100, "only the good record BEFORE the out-of-bounds entry survives");
    CHECK(trunc, "truncated flag set for the out-of-bounds stop");
}

static void test_parse_rides_wrong_histidx_len_rejected(void)
{
    uint8_t history[32] = {0};
    uint8_t histidx[15] = {0}; /* wrong length */
    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 9;
    CHECK(!nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                            rides, NFC_TRANSIT_MAX_RIDES, &count, NULL),
          "wrong-length histidx rejected outright");
    CHECK(count == 0, "count reset to 0 on rejection");
}

static void test_parse_rides_longer_than_min_histidx_still_parses(void)
{
    /* Acquisition checks `count < min_size`, i.e. "at least 16 bytes",
     * and only ever reads
     * the first 16 regardless of the file's true declared size. A real
     * card's histidx file is not guaranteed to declare exactly 16 bytes --
     * this reproduces the real hardware bug where a histidx captured at,
     * e.g., 64 bytes (data_len == MF_DESFIRE_DEEP_FILE_DATA_CAP, a
     * perfectly valid real-world size) caused the previous strict
     * `histidx_len != 16` check to reject the file outright and silently
     * zero ride_count, even though the first 16 bytes -- all this function
     * ever reads -- contained a genuine, fully decodable ride. */
    uint8_t history[64]; memset(history, 0, sizeof(history));
    build_ride_record(&history[0], 0x0004 /*BART*/, 250, 0, 4000000000U, 0, 0x0001, 0x0002);

    uint8_t histidx[64]; memset(histidx, 0xFF, sizeof(histidx));
    histidx[0] = 0; /* points at record 0 -- within the first 16 bytes this function reads */

    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
    uint8_t count = 0;
    CHECK(nfc_transit_clipper_parse_rides(histidx, sizeof(histidx), history, sizeof(history),
                                           rides, NFC_TRANSIT_MAX_RIDES, &count, NULL),
          "a histidx longer than the 16-byte minimum (64 bytes, a real captured size) is still accepted");
    CHECK(count == 1, "the ride within the first 16 index bytes still decodes correctly");
    CHECK(rides[0].agency_id == 0x0004U, "agency_id == BART");
}

/*============================ name lookups ======================================*/

static void test_agency_name_known(void)
{
    const char *n = nfc_transit_clipper_agency_name(0x0004);
    CHECK(n != NULL && strcmp(n, "BART") == 0, "agency 0x0004 == BART");
    n = nfc_transit_clipper_agency_name(0x0012);
    CHECK(n != NULL && strcmp(n, "Muni") == 0, "agency 0x0012 == Muni");
}

static void test_agency_name_unknown(void)
{
    CHECK(nfc_transit_clipper_agency_name(0x9999) == NULL, "unrecognized agency id returns NULL, never guessed");
}

static void test_zone_name_known_and_agency_scoped(void)
{
    const char *n = nfc_transit_clipper_zone_name(0x0004, 0x0004);
    CHECK(n != NULL && strcmp(n, "Glen Park") == 0, "BART zone 0x0004 == Glen Park");
    /* Same zone id under a DIFFERENT agency must not accidentally match. */
    n = nfc_transit_clipper_zone_name(0x0006 /*Caltrain*/, 0x0004);
    CHECK(n != NULL && strcmp(n, "Zone 4") == 0, "Caltrain zone 0x0004 == Zone 4, independent table from BART");
}

static void test_zone_name_unknown_agency(void)
{
    CHECK(nfc_transit_clipper_zone_name(0x9999, 0x0001) == NULL, "unknown agency has no zone table");
}

/*============================ USD split =========================================*/

static void test_usd_split_positive(void)
{
    bool neg; int16_t dollars; uint16_t cents;
    nfc_transit_usd_split(1234, &neg, &dollars, &cents);
    CHECK(!neg, "1234 cents is not negative");
    CHECK(dollars == 12 && cents == 34, "1234 cents -> $12.34");
}

static void test_usd_split_negative(void)
{
    bool neg; int16_t dollars; uint16_t cents;
    nfc_transit_usd_split(-32768, &neg, &dollars, &cents); /* int16 minimum */
    CHECK(neg, "negative flag set even though the display does not use it");
    /* Required semantics: dollars = cents/100 (signed, truncated toward
     * zero) = -327; cents_abs = 68. Displayed as "$-327.68" via "$%d.%02u",
     * preserving the established output format. */
    CHECK(dollars == -327 && cents == 68, "-32768 cents -> dollars=-327 (signed), cents=68");
}

static void test_usd_split_negative_one_cent(void)
{
    bool neg; int16_t dollars; uint16_t cents;
    nfc_transit_usd_split(-1, &neg, &dollars, &cents);
    CHECK(neg, "negative flag set for -1 cent");
    CHECK(dollars == 0 && cents == 1, "-1 cent -> dollars=0, cents=1 (displayed \"$0.01\"; sub-dollar sign is not displayed)");
}

static void test_usd_split_zero(void)
{
    bool neg; int16_t dollars; uint16_t cents;
    nfc_transit_usd_split(0, &neg, &dollars, &cents);
    CHECK(!neg && dollars == 0 && cents == 0, "0 cents -> $0.00, not negative");
}

/*============================ date conversion ===================================*/
/* Golden vectors independently computed via Python:
 *   date(1900,1,1) + timedelta(seconds=X) */

static void check_date(uint32_t seconds, uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s, const char *label)
{
    uint16_t oy; uint8_t om, od, oh, omin, osec;
    nfc_transit_epoch1900_to_ymdhms(seconds, &oy, &om, &od, &oh, &omin, &osec);
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: got %04u-%02u-%02u %02u:%02u:%02u want %04u-%02u-%02u %02u:%02u:%02u",
             label, oy, om, od, oh, omin, osec, y, mo, d, h, mi, s);
    CHECK(oy == y && om == mo && od == d && oh == h && omin == mi && osec == s, msg);
}

static void test_epoch1900_golden_vectors(void)
{
    check_date(0,          1900, 1, 1,  0, 0, 0,  "epoch zero");
    check_date(1,          1900, 1, 1,  0, 0, 1,  "epoch+1s");
    check_date(86399,      1900, 1, 1, 23,59,59,  "last second of day 1");
    check_date(86400,      1900, 1, 2,  0, 0, 0,  "first second of day 2");
    check_date(31536000,   1901, 1, 1,  0, 0, 0,  "one non-leap year later (1900 is not a leap year)");
    check_date(3155760000U,2000, 1, 2,  0, 0, 0,  "century boundary + leap days");
    check_date(4000000000U,2026,10, 3,  7, 6,40,  "large 32-bit value near the field's practical range");
    check_date(4102444799U,2029,12,31, 23,59,59,  "another large boundary value");
}

int main(void)
{
    test_detect_card_aid();
    test_detect_mobile_aid();
    test_detect_one_byte_mismatch_rejected();
    test_detect_no_apps_unknown();

    test_parse_identity_valid();
    test_parse_identity_too_short();

    test_parse_cash_valid_negative_balance();
    test_parse_cash_too_short();

    test_parse_rides_single_record();
    test_parse_rides_empty_index();
    test_parse_rides_out_of_bounds_record_skipped();
    test_parse_rides_bad_marker_skipped();
    test_parse_rides_zero_agency_skipped();
    test_parse_rides_max_rides_cap_truncates();
    test_parse_rides_bad_marker_stops_walk_not_skips();
    test_parse_rides_zero_agency_stops_walk_not_skips();
    test_parse_rides_out_of_bounds_stops_walk_not_skips();
    test_parse_rides_wrong_histidx_len_rejected();
    test_parse_rides_longer_than_min_histidx_still_parses();

    test_agency_name_known();
    test_agency_name_unknown();
    test_zone_name_known_and_agency_scoped();
    test_zone_name_unknown_agency();

    test_usd_split_positive();
    test_usd_split_negative();
    test_usd_split_negative_one_cent();
    test_usd_split_zero();

    test_epoch1900_golden_vectors();

    printf("nfc_transit_clipper_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
