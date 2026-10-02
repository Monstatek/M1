/*
 * m1_wifi_session_cache_test.c - host tests for the passive Wi-Fi AP scan
 * cache shared by Networks, Handshake, Deauth Client, and PCAP capture.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_wifi_session_cache.c m1_csrc/test/m1_wifi_session_cache_test.c \
 *      -o /tmp/session_cache_test && /tmp/session_cache_test
 */
#include "m1_wifi_session_cache.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static wifi_session_ap_t mk_ap(const char *ssid, const char *bssid, int rssi, int ch, int enc)
{
    wifi_session_ap_t a;
    memset(&a, 0, sizeof(a));
    strncpy((char *)a.ssid, ssid, sizeof(a.ssid) - 1u);
    strncpy((char *)a.bssid, bssid, sizeof(a.bssid) - 1u);
    a.rssi = rssi;
    a.channel = ch;
    a.encryption_mode = enc;
    return a;
}

static void test_init_is_empty_and_not_fresh(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t out[4];

    wifi_session_cache_init(&c);
    CHECK(!wifi_session_cache_is_fresh(&c, 0u, WIFI_SESSION_CACHE_TTL_MS), "init: not fresh");
    CHECK(wifi_session_cache_snapshot(&c, out, 4u) == 0u, "init: snapshot returns 0 entries");
    CHECK(wifi_session_cache_age_ms(&c, 12345u) == 0u, "init: age is 0 when invalid");
}

static void test_publish_then_snapshot_round_trips(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[3];
    wifi_session_ap_t out[8];
    uint16_t n;

    wifi_session_cache_init(&c);
    in[0] = mk_ap("HomeNet", "AA:BB:CC:DD:EE:01", -47, 6, 3);
    in[1] = mk_ap("", "AA:BB:CC:DD:EE:02", -70, 11, 0); /* hidden SSID */
    in[2] = mk_ap("Office", "AA:BB:CC:DD:EE:03", -55, 1, 4);

    wifi_session_cache_publish(&c, in, 3u, 1000u);

    CHECK(wifi_session_cache_is_fresh(&c, 1000u, WIFI_SESSION_CACHE_TTL_MS), "fresh immediately after publish");
    n = wifi_session_cache_snapshot(&c, out, 8u);
    CHECK(n == 3u, "snapshot returns exactly the published count");
    CHECK(memcmp(&out[0], &in[0], sizeof(in[0])) == 0, "entry 0 round-trips exactly");
    CHECK(memcmp(&out[1], &in[1], sizeof(in[1])) == 0, "entry 1 (hidden ssid) round-trips exactly");
    CHECK(memcmp(&out[2], &in[2], sizeof(in[2])) == 0, "entry 2 round-trips exactly");
}

static void test_publish_clamps_over_max_no_overflow(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[WIFI_SESSION_CACHE_MAX_APS + 10u];
    wifi_session_ap_t out[WIFI_SESSION_CACHE_MAX_APS + 10u];
    uint16_t i, n;
    char ssid[16], bssid[18];

    wifi_session_cache_init(&c);
    for (i = 0; i < WIFI_SESSION_CACHE_MAX_APS + 10u; i++)
    {
        snprintf(ssid, sizeof(ssid), "AP%u", (unsigned)i);
        snprintf(bssid, sizeof(bssid), "AA:BB:CC:DD:%02X:%02X", (unsigned)(i >> 8), (unsigned)i);
        in[i] = mk_ap(ssid, bssid, -40 - (int)(i % 50u), 1 + (int)(i % 14u), 3);
    }

    wifi_session_cache_publish(&c, in, WIFI_SESSION_CACHE_MAX_APS + 10u, 500u);

    CHECK(c.count == WIFI_SESSION_CACHE_MAX_APS, "publish clamps stored count to MAX");
    n = wifi_session_cache_snapshot(&c, out, WIFI_SESSION_CACHE_MAX_APS + 10u);
    CHECK(n == WIFI_SESSION_CACHE_MAX_APS, "snapshot never returns more than MAX, no overflow");
    /* Under ASan: reading exactly n valid entries proves no OOB write happened during publish. */
    CHECK(memcmp(&out[WIFI_SESSION_CACHE_MAX_APS - 1u], &in[WIFI_SESSION_CACHE_MAX_APS - 1u],
                 sizeof(in[0])) == 0, "last stored entry is the (MAX-1)th input, not garbage");
}

static void test_snapshot_respects_caller_capacity(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[5];
    wifi_session_ap_t out[2];
    uint16_t i, n;
    char ssid[8];

    wifi_session_cache_init(&c);
    for (i = 0; i < 5u; i++)
    {
        snprintf(ssid, sizeof(ssid), "N%u", (unsigned)i);
        in[i] = mk_ap(ssid, "11:22:33:44:55:66", -50, 6, 3);
    }
    wifi_session_cache_publish(&c, in, 5u, 0u);

    n = wifi_session_cache_snapshot(&c, out, 2u);
    CHECK(n == 2u, "snapshot bounded by the caller's out_cap, not the stored count");
    CHECK(memcmp(&out[0], &in[0], sizeof(in[0])) == 0, "first of the truncated entries is correct");
}

static void test_ttl_boundary(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[1];

    wifi_session_cache_init(&c);
    in[0] = mk_ap("X", "11:22:33:44:55:66", -50, 6, 3);
    wifi_session_cache_publish(&c, in, 1u, 10000u);

    CHECK(wifi_session_cache_is_fresh(&c, 10000u, 15000u), "age 0: fresh");
    CHECK(wifi_session_cache_is_fresh(&c, 10000u + 14999u, 15000u), "age 14999 < ttl 15000: fresh");
    CHECK(!wifi_session_cache_is_fresh(&c, 10000u + 15000u, 15000u), "age == ttl exactly: NOT fresh (strict <)");
    CHECK(!wifi_session_cache_is_fresh(&c, 10000u + 20000u, 15000u), "age well past ttl: not fresh");
    CHECK(wifi_session_cache_age_ms(&c, 10000u + 3000u) == 3000u, "age_ms reports elapsed time correctly");
}

static void test_invalidate_forces_stale_without_touching_data(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[1];
    wifi_session_ap_t out[1];

    wifi_session_cache_init(&c);
    in[0] = mk_ap("Keep", "11:22:33:44:55:66", -50, 6, 3);
    wifi_session_cache_publish(&c, in, 1u, 0u);
    CHECK(wifi_session_cache_is_fresh(&c, 0u, WIFI_SESSION_CACHE_TTL_MS), "fresh before invalidate");

    wifi_session_cache_invalidate(&c);
    CHECK(!wifi_session_cache_is_fresh(&c, 0u, WIFI_SESSION_CACHE_TTL_MS), "invalidate: no longer fresh");
    CHECK(wifi_session_cache_snapshot(&c, out, 1u) == 0u,
          "invalidate: snapshot refuses to hand back stale-marked data");

    /* A failed refresh (publish never called again) must leave it invalid --
     * simulated here simply by not calling publish() after invalidate(). */
    CHECK(!wifi_session_cache_is_fresh(&c, 999999u, WIFI_SESSION_CACHE_TTL_MS),
          "still invalid arbitrarily far later: a failed refresh never looks fresh");
}

static void test_republish_after_invalidate_recovers(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in1[1], in2[1];

    wifi_session_cache_init(&c);
    in1[0] = mk_ap("Old", "11:11:11:11:11:11", -50, 1, 0);
    wifi_session_cache_publish(&c, in1, 1u, 0u);

    wifi_session_cache_invalidate(&c); /* manual refresh begins */
    in2[0] = mk_ap("New", "22:22:22:22:22:22", -60, 11, 3);
    wifi_session_cache_publish(&c, in2, 1u, 100u); /* refresh scan succeeded */

    CHECK(wifi_session_cache_is_fresh(&c, 100u, WIFI_SESSION_CACHE_TTL_MS), "fresh again after successful refresh");
    {
        wifi_session_ap_t out[1];
        CHECK(wifi_session_cache_snapshot(&c, out, 1u) == 1u, "one entry present after refresh");
        CHECK(memcmp(&out[0], &in2[0], sizeof(in2[0])) == 0, "refreshed data is the NEW scan, not stale old data");
    }
}

static void test_wraparound_safety(void)
{
    wifi_session_cache_t c;
    wifi_session_ap_t in[1];
    uint32_t near_wrap = 0xFFFFFFFFu - 100u;
    uint32_t after_wrap = near_wrap + 200u; /* wraps past 0 */

    wifi_session_cache_init(&c);
    in[0] = mk_ap("X", "11:22:33:44:55:66", -50, 6, 3);
    wifi_session_cache_publish(&c, in, 1u, near_wrap);

    CHECK(after_wrap < near_wrap, "sanity: this timestamp did wrap");
    CHECK(wifi_session_cache_age_ms(&c, after_wrap) == 200u,
          "age computed correctly across a 32-bit wraparound");
    CHECK(wifi_session_cache_is_fresh(&c, after_wrap, WIFI_SESSION_CACHE_TTL_MS),
          "freshness check correct across wraparound (200ms < 15000ms ttl)");
}

static void test_selected_ap_context(void)
{
    wifi_session_selected_t sel;
    wifi_session_ap_t ap = mk_ap("Target", "AA:BB:CC:DD:EE:FF", -42, 6, 3);
    wifi_session_ap_t out;

    memset(&sel, 0xAA, sizeof(sel)); /* poison, to prove clear() really resets it */
    wifi_session_selected_clear(&sel);
    CHECK(!sel.valid, "clear: not valid");

    memset(&out, 0x55, sizeof(out));
    CHECK(!wifi_session_selected_get(&sel, &out), "get on cleared selection: returns false");

    wifi_session_selected_set(&sel, &ap);
    CHECK(sel.valid, "set: valid");
    CHECK(memcmp(&sel.ap, &ap, sizeof(ap)) == 0, "set: stored AP matches exactly (ssid/bssid/rssi/channel/enc)");

    memset(&out, 0, sizeof(out));
    CHECK(wifi_session_selected_get(&sel, &out), "get after set: returns true");
    CHECK(memcmp(&out, &ap, sizeof(ap)) == 0, "get after set: copied AP matches exactly");

    wifi_session_selected_clear(&sel);
    CHECK(!sel.valid, "clear after set: not valid again");

    memset(&out, 0x55, sizeof(out));
    CHECK(!wifi_session_selected_get(&sel, &out), "get after clear: returns false again");
}

static void test_canonical_singleton(void)
{
    wifi_session_cache_t *c1 = wifi_session_cache_get();
    wifi_session_cache_t *c2;
    wifi_session_ap_t in[1];

    CHECK(c1 != NULL, "get(): non-NULL");
    CHECK(!wifi_session_cache_is_fresh(c1, 0u, WIFI_SESSION_CACHE_TTL_MS),
          "get(): starts empty/invalid");

    in[0] = mk_ap("Canon", "AA:AA:AA:AA:AA:AA", -40, 1, 4);
    wifi_session_cache_publish(c1, in, 1u, 1000u);

    c2 = wifi_session_cache_get();
    CHECK(c2 == c1, "get(): same instance returned on every call");
    CHECK(wifi_session_cache_is_fresh(c2, 1000u, WIFI_SESSION_CACHE_TTL_MS),
          "get(): a publish() through one handle is visible through another");

    wifi_session_cache_get_reset();
    CHECK(!wifi_session_cache_is_fresh(wifi_session_cache_get(), 1000u, WIFI_SESSION_CACHE_TTL_MS),
          "get_reset(): clears the canonical instance back to empty/invalid");
    CHECK(wifi_session_cache_get()->count == 0u, "get_reset(): count cleared too");
}

int main(void)
{
    test_init_is_empty_and_not_fresh();
    test_publish_then_snapshot_round_trips();
    test_publish_clamps_over_max_no_overflow();
    test_snapshot_respects_caller_capacity();
    test_ttl_boundary();
    test_invalidate_forces_stale_without_touching_data();
    test_republish_after_invalidate_recovers();
    test_wraparound_safety();
    test_selected_ap_context();
    test_canonical_singleton();

    printf("\nm1_wifi_session_cache_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
