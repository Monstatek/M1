/* Host tests for the Clipper transit-interpretation persistence format
 * added to nfc_file.c (save) and nfc_storage.c (load).
 *
 * DISCLOSED LIMITATION (same as nfc_desfire_persistence_test.c and every
 * other FatFs/HAL-coupled file in this project): nfc_profile_save()/
 * nfc_storage_load_file() cannot be host-compiled directly. This suite:
 *   1. Verifies the exact save-line formats and parse-dispatch literals are
 *      present in the REAL, committed nfc_file.c/nfc_storage.c.
 *   2. Transcribes the writer/parser as pure in-memory functions (format
 *      strings copy-verified against step 1) and round-trips a
 *      representative nfc_transit_result_t -- including a negative
 *      balance, multiple rides, and an unrecognized (Unknown) card that
 *      must produce zero output lines.
 *
 * Build (from repo root):
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     -I NFC/NFC_drv/common \
 *     NFC/NFC_drv/common/test/nfc_transit_persistence_test.c \
 *     NFC/NFC_drv/common/nfc_transit_clipper.c \
 *     NFC/NFC_drv/common/mf_desfire_parse.c \
 *     -o /tmp/nfc_transit_persistence_test && /tmp/nfc_transit_persistence_test
 */
#include "nfc_transit_clipper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}

static bool contains(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

static void test_source_matches_shipped(void)
{
    char *file_c    = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_file.c");
    char *storage_c = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_storage.c");
    CHECK(file_c != NULL, "nfc_file.c readable");
    CHECK(storage_c != NULL, "nfc_storage.c readable");

    CHECK(contains(file_c, "\"Clipper Card: 1\\r\\n\""), "nfc_file.c: Clipper Card save line present");
    CHECK(contains(file_c, "\"Clipper Type: %s\\r\\n\""), "nfc_file.c: Clipper Type save line format present");
    CHECK(contains(file_c, "\"Clipper Serial: %lu\\r\\n\""), "nfc_file.c: Clipper Serial save line format present");
    CHECK(contains(file_c, "\"Clipper Balance: %d\\r\\n\""), "nfc_file.c: Clipper Balance save line format present");
    CHECK(contains(file_c, "\"Clipper Ride %02u: %04X %d %u %lu %lu %u %u\\r\\n\""),
          "nfc_file.c: Clipper Ride save line format present");

    CHECK(contains(storage_c, "GetPrivateProfileUintS(&data, \"Clipper Card\", &sess)"),
          "nfc_storage.c: Clipper Card parsed via header uint reader");
    CHECK(contains(storage_c, "strncmp(line, \"Clipper Ride \", 13) == 0"),
          "nfc_storage.c: Clipper Ride line dispatch present");
    CHECK(contains(storage_c, "r->has_time_on   = (time_on != 0UL);"),
          "nfc_storage.c: has_time_on re-derived from time_on, not separately persisted");
}

/*============================ transcribed round trip =============================*/

#define MAX_LINES 64
#define MAX_LINE_LEN 96
typedef struct { char lines[MAX_LINES][MAX_LINE_LEN]; int count; } line_buf_t;

static void lb_add(line_buf_t *lb, const char *fmt, ...)
{
    if (lb->count >= MAX_LINES) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(lb->lines[lb->count], MAX_LINE_LEN, fmt, ap);
    va_end(ap);
    lb->count++;
}

static void transit_write(line_buf_t *lb, const nfc_transit_result_t *t)
{
    if (t->card_id != NfcTransitCardClipper) return;

    lb_add(lb, "Clipper Card: 1");
    if (t->card_type_label != NULL) lb_add(lb, "Clipper Type: %s", t->card_type_label);
    if (t->serial_valid) lb_add(lb, "Clipper Serial: %lu", (unsigned long)t->serial_number);
    if (t->balance_valid) lb_add(lb, "Clipper Balance: %d", (int)t->balance_cents);
    if (t->last_update_valid) lb_add(lb, "Clipper Last Update: %lu", (unsigned long)t->last_update_1900);
    if (t->last_terminal_valid) lb_add(lb, "Clipper Terminal: %u", (unsigned)t->last_terminal_id);
    if (t->last_txn_valid) lb_add(lb, "Clipper Txn: %u", (unsigned)t->last_txn_id);
    if (t->counter_valid) lb_add(lb, "Clipper Counter: %u", (unsigned)t->counter);
    if (t->rides_truncated) lb_add(lb, "Clipper Rides Truncated: 1");
    for (uint8_t i = 0; i < t->ride_count && i < NFC_TRANSIT_MAX_RIDES; i++) {
        const nfc_transit_ride_t *r = &t->rides[i];
        lb_add(lb, "Clipper Ride %02u: %04X %d %u %lu %lu %u %u",
               (unsigned)i, (unsigned)r->agency_id, (int)r->fare_cents, (unsigned)r->vehicle_id,
               (unsigned long)r->time_on_1900, (unsigned long)r->time_off_1900,
               (unsigned)r->zone_on_id, (unsigned)r->zone_off_id);
    }
}

static void transit_parse_line(const char *line, nfc_transit_result_t *t)
{
    unsigned u32v;
    if (sscanf(line, "Clipper Card: %u", &u32v) == 1) {
        if (u32v != 0) t->card_id = NfcTransitCardClipper;
        return;
    }
    char sbuf[24];
    if (sscanf(line, "Clipper Type: %23[^\r\n]", sbuf) == 1) {
        if (strcmp(sbuf, "Mobile Device") == 0) t->card_type_label = "Mobile Device";
        else if (strcmp(sbuf, "Card") == 0) t->card_type_label = "Card";
        return;
    }
    if (sscanf(line, "Clipper Serial: %u", &u32v) == 1) { t->serial_valid = true; t->serial_number = u32v; return; }
    int ival;
    if (sscanf(line, "Clipper Balance: %d", &ival) == 1) { t->balance_valid = true; t->balance_cents = (int16_t)ival; return; }
    if (sscanf(line, "Clipper Last Update: %u", &u32v) == 1) { t->last_update_valid = true; t->last_update_1900 = u32v; return; }
    if (sscanf(line, "Clipper Terminal: %u", &u32v) == 1) { t->last_terminal_valid = true; t->last_terminal_id = (uint16_t)u32v; return; }
    if (sscanf(line, "Clipper Txn: %u", &u32v) == 1) { t->last_txn_valid = true; t->last_txn_id = (uint16_t)u32v; return; }
    if (sscanf(line, "Clipper Counter: %u", &u32v) == 1) { t->counter_valid = true; t->counter = (uint16_t)u32v; return; }
    if (sscanf(line, "Clipper Rides Truncated: %u", &u32v) == 1) { if (u32v != 0) t->rides_truncated = true; return; }

    if (strncmp(line, "Clipper Ride ", 13) == 0) {
        unsigned idx = 0, agency = 0, vehicle = 0, zone_on = 0, zone_off = 0;
        int fare = 0;
        unsigned long time_on = 0, time_off = 0;
        int fields = sscanf(line + 13, "%u: %x %d %u %lu %lu %u %u",
                             &idx, &agency, &fare, &vehicle, &time_on, &time_off, &zone_on, &zone_off);
        if ((fields == 8) && (t->ride_count < NFC_TRANSIT_MAX_RIDES)) {
            nfc_transit_ride_t *r = &t->rides[t->ride_count++];
            memset(r, 0, sizeof(*r));
            r->agency_id = (uint16_t)agency;
            r->fare_cents = (int16_t)fare;
            r->vehicle_id = (uint16_t)vehicle;
            r->time_on_1900 = (uint32_t)time_on;
            r->has_time_on = (time_on != 0UL);
            r->time_off_1900 = (uint32_t)time_off;
            r->has_time_off = (time_off != 0UL);
            r->zone_on_id = (uint16_t)zone_on;
            r->zone_off_id = (uint16_t)zone_off;
        }
    }
}

static void test_round_trip_recognized_card(void)
{
    nfc_transit_result_t original, reloaded;
    nfc_transit_result_reset(&original);
    nfc_transit_result_reset(&reloaded);

    original.card_id = NfcTransitCardClipper;
    original.card_type_label = "Mobile Device";
    original.serial_valid = true; original.serial_number = 1234567890U;
    original.balance_valid = true; original.balance_cents = -100;
    original.last_update_valid = true; original.last_update_1900 = 4000000000U;
    original.last_terminal_valid = true; original.last_terminal_id = 0x1234;
    original.last_txn_valid = true; original.last_txn_id = 0x5678;
    original.counter_valid = true; original.counter = 42;
    original.rides_truncated = true;
    original.ride_count = 2;
    original.rides[0] = (nfc_transit_ride_t){ .agency_id = 0x0004, .fare_cents = 250, .vehicle_id = 0,
                                               .has_time_on = true, .time_on_1900 = 4000000000U,
                                               .has_time_off = false, .time_off_1900 = 0,
                                               .zone_on_id = 1, .zone_off_id = 2 };
    original.rides[1] = (nfc_transit_ride_t){ .agency_id = 0x0012, .fare_cents = -50, .vehicle_id = 99,
                                               .has_time_on = true, .time_on_1900 = 3999999999U,
                                               .has_time_off = true, .time_off_1900 = 4000000500U,
                                               .zone_on_id = 5, .zone_off_id = 6 };

    line_buf_t lb = { .count = 0 };
    transit_write(&lb, &original);
    CHECK(lb.count > 0, "writer produced lines for a recognized card");

    for (int i = 0; i < lb.count; i++) transit_parse_line(lb.lines[i], &reloaded);

    CHECK(reloaded.card_id == NfcTransitCardClipper, "card_id round-trips");
    CHECK(reloaded.card_type_label != NULL && strcmp(reloaded.card_type_label, "Mobile Device") == 0, "card_type_label round-trips");
    CHECK(reloaded.serial_valid && reloaded.serial_number == 1234567890U, "serial round-trips");
    CHECK(reloaded.balance_valid && reloaded.balance_cents == -100, "negative balance round-trips exactly");
    CHECK(reloaded.last_update_valid && reloaded.last_update_1900 == 4000000000U, "last_update round-trips");
    CHECK(reloaded.last_terminal_valid && reloaded.last_terminal_id == 0x1234, "terminal round-trips");
    CHECK(reloaded.last_txn_valid && reloaded.last_txn_id == 0x5678, "txn round-trips");
    CHECK(reloaded.counter_valid && reloaded.counter == 42, "counter round-trips");
    CHECK(reloaded.rides_truncated, "rides_truncated round-trips");
    CHECK(reloaded.ride_count == 2, "ride_count round-trips");
    CHECK(reloaded.rides[0].agency_id == 0x0004 && reloaded.rides[0].fare_cents == 250, "ride[0] scalar fields round-trip");
    CHECK(reloaded.rides[0].has_time_on && !reloaded.rides[0].has_time_off, "ride[0] has_time_on/off re-derived correctly");
    CHECK(reloaded.rides[1].agency_id == 0x0012 && reloaded.rides[1].fare_cents == -50, "ride[1] negative fare round-trips");
    CHECK(reloaded.rides[1].has_time_on && reloaded.rides[1].has_time_off, "ride[1] both timestamps present");
}

static void test_unknown_card_produces_no_lines(void)
{
    nfc_transit_result_t t;
    nfc_transit_result_reset(&t);
    /* card_id stays NfcTransitCardUnknown */
    line_buf_t lb = { .count = 0 };
    transit_write(&lb, &t);
    CHECK(lb.count == 0, "an unrecognized card writes zero Clipper lines");
}

int main(void)
{
    test_source_matches_shipped();
    test_round_trip_recognized_card();
    test_unknown_card_produces_no_lines();

    printf("nfc_transit_persistence_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
