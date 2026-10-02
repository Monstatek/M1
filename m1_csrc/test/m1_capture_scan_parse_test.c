/*
 * m1_capture_scan_parse_test.c - host tests for the `scan -a` reply parser.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_capture_scan_parse.c m1_csrc/test/m1_capture_scan_parse_test.c \
 *      -o /tmp/cap_scan_test && /tmp/cap_scan_test
 *
 * Fixtures are built with the *exact* format strings from the ESP32-C6
 * firmware (components/m1_wifi/esp32c6_wifi.c: scan()/print_ap_list()), so
 * the "nominal" cases are byte-identical to real `scan -a` output, not an
 * approximation of it:
 *   sentinel row: "[+] Scan complete. %d AP(s) found.\n"
 *   header:       "\n%-4s %-34s %-4s %-5s %-18s %-8s\n"
 *   row:          "[%02d] %-34s %-4d %-5d %-18s %-8s\n"
 * where the SSID arg is itself pre-wrapped as "\"%s\"" before the %-34s pad.
 */
#include "m1_capture_scan_parse.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* ---- fixture builders, mirroring esp32c6_wifi.c's format strings exactly ---- */

static void append_sentinel(char *buf, size_t cap, int n_found)
{
    char line[96];
    snprintf(line, sizeof(line), "[*] Mode: STA... Scanning\n[+] Scan complete. %d AP(s) found.\n",
              n_found);
    strncat(buf, line, cap - strlen(buf) - 1u);
}

static void append_header(char *buf, size_t cap)
{
    char line[96];
    snprintf(line, sizeof(line), "\n%-4s %-34s %-4s %-5s %-18s %-8s\n",
              "[ID]", "SSID", "CH", "RSSI", "BSSID", "AUTH");
    strncat(buf, line, cap - strlen(buf) - 1u);
}

static void append_row(char *buf, size_t cap, int id, const char *ssid,
                       int ch, int rssi, const char *bssid, const char *auth)
{
    char ssid_display[36];
    char line[128];
    snprintf(ssid_display, sizeof(ssid_display), "\"%s\"", ssid);
    snprintf(line, sizeof(line), "[%02d] %-34s %-4d %-5d %-18s %-8s\n",
              id, ssid_display, ch, rssi, bssid, auth);
    strncat(buf, line, cap - strlen(buf) - 1u);
}

/* ==================================== valid ==================================== */

static void test_nominal_table(void)
{
    char buf[2048] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 4);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "HomeNet",      6,  -47, "AA:BB:CC:DD:EE:01", "WPA2");
    append_row(buf, sizeof(buf), 1, "OfficeWiFi",   11, -62, "AA:BB:CC:DD:EE:02", "WPA3");
    append_row(buf, sizeof(buf), 2, "GuestNet",     1,  -78, "AA:BB:CC:DD:EE:03", "OPEN");
    append_row(buf, sizeof(buf), 3, "IoT-Backhaul", 6,  -55, "AA:BB:CC:DD:EE:04", "WPA2/3");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "nominal: parse ok");
    CHECK(r.valid == true, "nominal: valid");
    CHECK(r.reported == 4u, "nominal: reported == 4");
    CHECK(r.count == 4u, "nominal: all 4 rows stored");

    CHECK(strcmp(r.aps[0].ssid, "HomeNet") == 0, "nominal: row0 ssid");
    CHECK(r.aps[0].channel == 6, "nominal: row0 channel");
    CHECK(r.aps[0].rssi == -47, "nominal: row0 rssi");
    CHECK(strcmp(r.aps[0].bssid, "AA:BB:CC:DD:EE:01") == 0, "nominal: row0 bssid");
    CHECK(strcmp(r.aps[0].auth, "WPA2") == 0, "nominal: row0 auth");

    CHECK(strcmp(r.aps[1].ssid, "OfficeWiFi") == 0, "nominal: row1 ssid");
    CHECK(r.aps[1].channel == 11, "nominal: row1 channel");
    CHECK(r.aps[1].rssi == -62, "nominal: row1 rssi");
    CHECK(strcmp(r.aps[1].auth, "WPA3") == 0, "nominal: row1 auth");

    CHECK(strcmp(r.aps[2].ssid, "GuestNet") == 0, "nominal: row2 ssid");
    CHECK(r.aps[2].rssi == -78, "nominal: row2 rssi");
    CHECK(strcmp(r.aps[2].auth, "OPEN") == 0, "nominal: row2 auth");

    CHECK(strcmp(r.aps[3].ssid, "IoT-Backhaul") == 0, "nominal: row3 ssid");
    CHECK(strcmp(r.aps[3].auth, "WPA2/3") == 0, "nominal: row3 auth");
}

static void test_ssid_with_spaces(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "My Home Network", 6, -50, "11:22:33:44:55:66", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "spaces: parse ok");
    CHECK(r.count == 1u, "spaces: one row stored");
    CHECK(strcmp(r.aps[0].ssid, "My Home Network") == 0, "spaces: ssid preserved verbatim");
    CHECK(r.aps[0].channel == 6, "spaces: channel still correct");
    CHECK(r.aps[0].rssi == -50, "spaces: rssi still correct");
}

static void test_hidden_ssid(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "", 6, -50, "11:22:33:44:55:66", "OPEN");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "hidden: parse ok");
    CHECK(r.count == 1u, "hidden: row stored");
    CHECK(strcmp(r.aps[0].ssid, "") == 0, "hidden: ssid is empty string");
    CHECK(r.aps[0].channel == 6, "hidden: channel still correct");
}

static void test_max_length_ssid(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;
    char ssid32[33];

    memset(ssid32, 'A', 32u);
    ssid32[32] = '\0';

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, ssid32, 6, -50, "11:22:33:44:55:66", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "max-ssid: parse ok");
    CHECK(r.count == 1u, "max-ssid: row stored");
    CHECK(strlen(r.aps[0].ssid) == 32u, "max-ssid: full 32 bytes stored");
    CHECK(memcmp(r.aps[0].ssid, ssid32, 32u) == 0, "max-ssid: bytes match");
    CHECK(r.aps[0].ssid[32] == '\0', "max-ssid: NUL-terminated at [32]");
}

/* ==================================== boundary ==================================== */

static void test_exactly_max_rows(void)
{
    char buf[8192] = {0};
    m1_cap_scan_result_t r;
    uint32_t i;

    append_sentinel(buf, sizeof(buf), (int)M1_CAP_SCAN_MAX_APS);
    append_header(buf, sizeof(buf));
    for (i = 0; i < M1_CAP_SCAN_MAX_APS; i++)
    {
        char ssid[16], bssid[18];
        snprintf(ssid, sizeof(ssid), "AP%u", i);
        snprintf(bssid, sizeof(bssid), "AA:BB:CC:DD:%02X:%02X", (unsigned)(i >> 8), (unsigned)i);
        append_row(buf, sizeof(buf), (int)i, ssid, 1 + (int)(i % 14), -40 - (int)i, bssid, "WPA2");
    }

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "exact-max: parse ok");
    CHECK(r.reported == M1_CAP_SCAN_MAX_APS, "exact-max: reported == MAX");
    CHECK(r.count == M1_CAP_SCAN_MAX_APS, "exact-max: all rows stored");
}

static void test_over_max_rows_clamped(void)
{
    char buf[16384] = {0};
    m1_cap_scan_result_t r;
    const uint32_t total = M1_CAP_SCAN_MAX_APS + 10u;
    uint32_t i;

    append_sentinel(buf, sizeof(buf), (int)total);
    append_header(buf, sizeof(buf));
    for (i = 0; i < total; i++)
    {
        char ssid[16], bssid[18];
        snprintf(ssid, sizeof(ssid), "AP%u", i);
        snprintf(bssid, sizeof(bssid), "AA:BB:CC:DD:%02X:%02X", (unsigned)(i >> 8), (unsigned)i);
        append_row(buf, sizeof(buf), (int)i, ssid, 1 + (int)(i % 14), -40 - (int)(i % 50), bssid, "WPA2");
    }

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "over-max: parse ok");
    CHECK(r.valid == true, "over-max: valid");
    CHECK(r.reported == total, "over-max: reported == total (sentinel unclamped)");
    CHECK(r.count == M1_CAP_SCAN_MAX_APS, "over-max: stored count clamps to MAX");
    /* Last stored row must be the (MAX-1)th input row, not garbage/OOB -- if the
     * clamp were broken this would read as "APMAX" or crash under ASan. */
    {
        char expect[16];
        snprintf(expect, sizeof(expect), "AP%u", M1_CAP_SCAN_MAX_APS - 1u);
        CHECK(strcmp(r.aps[M1_CAP_SCAN_MAX_APS - 1u].ssid, expect) == 0,
              "over-max: last stored row is the (MAX-1)th input, not OOB data");
    }
}

static void test_ssid_over_32_truncated(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;
    /* Hand-built row: a 40-byte SSID (never emitted by real firmware, whose
     * SSID field is bounded to 32, but the parser must still defend itself). */
    const char *long_ssid = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; /* 40 'A' */

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, long_ssid, 6, -50, "11:22:33:44:55:66", "WPA2");

    CHECK(strlen(long_ssid) == 40u, "over-ssid: fixture sanity (40 bytes)");
    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "over-ssid: parse ok");
    CHECK(r.count == 1u, "over-ssid: row stored (truncated, not dropped)");
    CHECK(strlen(r.aps[0].ssid) == 32u, "over-ssid: truncated to 32 bytes");
    CHECK(r.aps[0].ssid[32] == '\0', "over-ssid: NUL-terminated at [32]");
}

static void test_channel_boundaries(void)
{
    char buf[1024] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 4);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "Ch1",  1,  -50, "11:22:33:44:55:01", "WPA2");
    append_row(buf, sizeof(buf), 1, "Ch14", 14, -50, "11:22:33:44:55:02", "WPA2");
    append_row(buf, sizeof(buf), 2, "Ch0",  0,  -50, "11:22:33:44:55:03", "WPA2");
    append_row(buf, sizeof(buf), 3, "Ch15", 15, -50, "11:22:33:44:55:04", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "channel: parse ok");
    CHECK(r.reported == 4u, "channel: reported == 4");
    CHECK(r.count == 2u, "channel: only ch1 and ch14 stored");
    CHECK(strcmp(r.aps[0].ssid, "Ch1") == 0 && r.aps[0].channel == 1, "channel: ch1 accepted");
    CHECK(strcmp(r.aps[1].ssid, "Ch14") == 0 && r.aps[1].channel == 14, "channel: ch14 accepted");
}

/* ==================================== malformed ==================================== */

static void test_no_sentinel(void)
{
    m1_cap_scan_result_t r;
    const char *buf = "[*] Mode: STA... Scanning\nsome unrelated console noise\n";

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == -1, "no-sentinel: returns -1");
    CHECK(r.valid == false, "no-sentinel: valid == false");
    CHECK(r.count == 0u, "no-sentinel: count == 0");
}

static void test_short_mac_skipped(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    /* 16-char MAC (missing the final hex digit). */
    append_row(buf, sizeof(buf), 0, "Broken", 6, -50, "11:22:33:44:55:6", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "short-mac: parse ok (sentinel found)");
    CHECK(r.reported == 1u, "short-mac: reported == 1");
    CHECK(r.count == 0u, "short-mac: row skipped");
}

static void test_nonhex_mac_skipped(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "Broken", 6, -50, "11:22:33:44:55:GG", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "nonhex-mac: parse ok");
    CHECK(r.count == 0u, "nonhex-mac: row skipped");
}

static void test_missing_colon_mac_skipped(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "Broken", 6, -50, "11:22:33:44:5566", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "missing-colon: parse ok");
    CHECK(r.count == 0u, "missing-colon: row skipped");
}

static void test_nonnumeric_channel_skipped(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    strncat(buf, "[00] \"Broken\"                     ??   -50   11:22:33:44:55:66 WPA2\n",
             sizeof(buf) - strlen(buf) - 1u);

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "nonnum-ch: parse ok");
    CHECK(r.count == 0u, "nonnum-ch: row skipped (channel not scannable back from RSSI)");
}

static void test_nonnumeric_rssi_skipped(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));
    strncat(buf, "[00] \"Broken\"                     6    n/a   11:22:33:44:55:66 WPA2\n",
             sizeof(buf) - strlen(buf) - 1u);

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "nonnum-rssi: parse ok");
    CHECK(r.count == 0u, "nonnum-rssi: row skipped (no digits directly before BSSID)");
}

static void test_header_line_self_skips(void)
{
    char buf[512] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 1);
    append_header(buf, sizeof(buf));  /* "[ID] SSID ..." -- must NOT be parsed as a row */
    append_row(buf, sizeof(buf), 0, "Real", 6, -50, "11:22:33:44:55:66", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "header-skip: parse ok");
    CHECK(r.count == 1u, "header-skip: header line produced no row, only the real one");
    CHECK(strcmp(r.aps[0].ssid, "Real") == 0, "header-skip: the stored row is the real one");
}

static void test_truncated_final_line(void)
{
    /* Complete row, no trailing newline: must still parse. */
    {
        char buf[512] = {0};
        m1_cap_scan_result_t r;
        append_sentinel(buf, sizeof(buf), 1);
        append_header(buf, sizeof(buf));
        append_row(buf, sizeof(buf), 0, "Complete", 6, -50, "11:22:33:44:55:66", "WPA2");
        /* strip the trailing '\n' that append_row added */
        buf[strlen(buf) - 1u] = '\0';

        CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "trunc-complete: parse ok");
        CHECK(r.count == 1u, "trunc-complete: complete row without trailing \\n still parses");
    }
    /* Genuinely truncated mid-BSSID, no trailing newline: must be dropped. */
    {
        char buf[512] = {0};
        m1_cap_scan_result_t r;
        append_sentinel(buf, sizeof(buf), 1);
        append_header(buf, sizeof(buf));
        strncat(buf, "[00] \"Cut\"                         6    -50   11:22:33:44:5",
                 sizeof(buf) - strlen(buf) - 1u); /* BSSID cut mid-way, no \n */

        CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "trunc-broken: parse ok (sentinel intact)");
        CHECK(r.count == 0u, "trunc-broken: incomplete final row dropped, not corrupted");
    }
}

static void test_empty_and_single_newline(void)
{
    m1_cap_scan_result_t r;

    CHECK(m1_cap_scan_parse("", 0u, &r) == -1, "empty: returns -1");
    CHECK(r.valid == false, "empty: valid == false");

    CHECK(m1_cap_scan_parse("\n", 1u, &r) == -1, "single-newline: returns -1");
    CHECK(r.valid == false, "single-newline: valid == false");
}

/* -------- the mismatch signal: sentinel count vs. actually-stored count -------- */

static void test_reported_vs_count_mismatch(void)
{
    char buf[1024] = {0};
    m1_cap_scan_result_t r;

    append_sentinel(buf, sizeof(buf), 5);   /* ESP claims 5 APs */
    append_header(buf, sizeof(buf));
    append_row(buf, sizeof(buf), 0, "AP0", 6, -50, "11:22:33:44:55:00", "WPA2");
    append_row(buf, sizeof(buf), 1, "AP1", 6, -50, "11:22:33:44:55:01", "WPA2");
    /* row 2: malformed BSSID (non-hex), skipped */
    strncat(buf, "[02] \"AP2\"                          6    -50   11:22:33:44:55:GG WPA2\n",
             sizeof(buf) - strlen(buf) - 1u);
    append_row(buf, sizeof(buf), 3, "AP3", 6, -50, "11:22:33:44:55:03", "WPA2");
    append_row(buf, sizeof(buf), 4, "AP4", 6, -50, "11:22:33:44:55:04", "WPA2");

    CHECK(m1_cap_scan_parse(buf, (uint32_t)strlen(buf), &r) == 0, "mismatch: parse ok");
    CHECK(r.valid == true, "mismatch: valid == true");
    CHECK(r.reported == 5u, "mismatch: reported == 5 (sentinel claim, unaffected by row failure)");
    CHECK(r.count == 4u, "mismatch: count == 4 (one malformed row dropped)");
}

int main(void)
{
    test_nominal_table();
    test_ssid_with_spaces();
    test_hidden_ssid();
    test_max_length_ssid();

    test_exactly_max_rows();
    test_over_max_rows_clamped();
    test_ssid_over_32_truncated();
    test_channel_boundaries();

    test_no_sentinel();
    test_short_mac_skipped();
    test_nonhex_mac_skipped();
    test_missing_colon_mac_skipped();
    test_nonnumeric_channel_skipped();
    test_nonnumeric_rssi_skipped();
    test_header_line_self_skips();
    test_truncated_final_line();
    test_empty_and_single_newline();

    test_reported_vs_count_mismatch();

    printf("\nm1_capture_scan_parse_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
