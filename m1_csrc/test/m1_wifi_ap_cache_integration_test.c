/*
 * m1_wifi_ap_cache_integration_test.c - host tests proving the shared
 * Wi-Fi AP cache integration policy used by Networks/Handshake/Deauth
 * Client (m1_wifi.c: wifi_ap_scan_list_cached()) and PCAP/Wireshark capture
 * (m1_capture_link.c: cap_scan_networks() / m1_capture_network_screen()).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_wifi_session_cache.c m1_csrc/test/m1_wifi_ap_cache_integration_test.c \
 *      -o /tmp/wifi_ap_cache_it_test && /tmp/wifi_ap_cache_it_test
 *
 * m1_wifi.c and m1_capture_link.c can't be host-built (STM32 HAL/FreeRTOS/
 * ESP-UART dependencies), so this file can't call their real functions
 * directly. Instead, ap_scan_cached() below is a byte-for-byte mirror of
 * wifi_ap_scan_list_cached()'s decision policy against the pure,
 * host-testable m1_wifi_session_cache.c API:
 *
 *   if (!force_refresh && is_fresh(cache)) { snapshot(); return; }
 *   invalidate(cache);
 *   if (!real_scan()) { return failure; }   // cache stays invalid
 *   publish(cache);
 *
 * Every real call site (Networks' rescan block, Handshake's HS_NET_SCAN/
 * HS_USE_SELECTED, Deauth Client's DC_NET_SCAN entry (which now always
 * reuses-if-fresh -- there is no separate "Use Selected" gate/state), and
 * m1_capture_link.c's cap_scan_networks()) follows exactly this shape, so
 * this proves the shared policy is correct; the firmware-side glue (which
 * ESP UART command runs, which ctrl_cmd_t struct gets populated) is
 * verified by the target build succeeding and by direct code inspection,
 * not by this host test. If wifi_ap_scan_list_cached() or
 * cap_scan_networks() are ever restructured, keep ap_scan_cached() here in
 * sync or these tests stop proving anything about the real code.
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

/* Stand-in for "the ESP32 round trip" (scan -a + list -a in m1_wifi.c,
 * CAP_SCAN_CMD in m1_capture_link.c). Counts calls so tests can assert
 * exactly how many real scans happened. */
typedef struct {
    int calls;
    bool fail;
    wifi_session_ap_t aps[8];
    uint16_t n;
} mock_ap_scan_t;

static bool mock_ap_scan_run(mock_ap_scan_t *m, wifi_session_ap_t *out, uint16_t *out_n)
{
    m->calls++;
    if (m->fail) { return false; }
    memcpy(out, m->aps, (size_t)m->n * sizeof(out[0]));
    *out_n = m->n;
    return true;
}

/* Mirrors wifi_ap_scan_list_cached(app_req, force_refresh) / the read path
 * in cap_scan_networks()+m1_capture_network_screen(). Returns true/SUCCESS
 * on the same conditions the real functions would. */
static bool ap_scan_cached(wifi_session_cache_t *cache, bool force_refresh, uint32_t now,
                           mock_ap_scan_t *mock, wifi_session_ap_t *out, uint16_t *out_n)
{
    if (!force_refresh && wifi_session_cache_is_fresh(cache, now, WIFI_SESSION_CACHE_TTL_MS))
    {
        *out_n = wifi_session_cache_snapshot(cache, out, 8u);
        return true;
    }

    wifi_session_cache_invalidate(cache);
    if (!mock_ap_scan_run(mock, out, out_n))
    {
        return false;
    }
    wifi_session_cache_publish(cache, out, *out_n, now);
    return true;
}

/* Stand-in for wifi_scan_devices() (m1_wifi.c) / the targeted client scan --
 * a completely separate mock with its own counter, NEVER touched by
 * ap_scan_cached()/the AP cache, proving the two are independent. */
static uint16_t mock_client_scan_run(mock_ap_scan_t *m)
{
    m->calls++;
    return m->n;
}

/* 1. Networks scan populates cache. */
static void test_1_networks_scan_populates_cache(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("CoffeeShop", "AA:AA:AA:AA:AA:01", -50, 6, 3);
    mock.n = 1;

    ok = ap_scan_cached(&cache, false, 1000u, &mock, out, &n);

    CHECK(ok, "Networks: scan succeeds");
    CHECK(mock.calls == 1, "Networks: exactly one real scan on first entry (empty cache)");
    CHECK(n == 1u, "Networks: result has the scanned AP");
    CHECK(wifi_session_cache_is_fresh(&cache, 1000u, WIFI_SESSION_CACHE_TTL_MS),
          "Networks: cache is fresh immediately after scan");
    CHECK(cache.count == 1u, "Networks: cache published the AP");
}

/* 2. Handshake entered immediately afterward does not issue another AP scan. */
static void test_2_handshake_reuses_cache_no_rescan(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("HomeNet", "AA:AA:AA:AA:AA:02", -40, 11, 4);
    mock.n = 1;
    ok = ap_scan_cached(&cache, false, 2000u, &mock, out, &n); /* Networks' scan */
    CHECK(ok && mock.calls == 1, "setup: Networks scanned once");

    /* Handshake opened "immediately afterward" -- same tick, HS_USE_SELECTED
     * or HS_NET_SCAN's non-forced call. */
    n = 0;
    ok = ap_scan_cached(&cache, false, 2000u, &mock, out, &n);

    CHECK(ok, "Handshake: cache read succeeds");
    CHECK(mock.calls == 1, "Handshake: no additional scan -- still exactly one real scan total");
    CHECK(n == 1u, "Handshake: sees the same AP Networks scanned");
    CHECK(strcmp((const char *)out[0].bssid, "AA:AA:AA:AA:AA:02") == 0,
          "Handshake: reused list matches Networks' scan exactly");
}

/* 3. Deauth Client entered immediately afterward reuses AP cache, then
 * performs only its targeted client scan. */
static void test_3_deauth_reuses_cache_then_own_client_scan(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t ap_mock = {0};
    mock_ap_scan_t client_mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    ap_mock.aps[0] = mk_ap("Office", "AA:AA:AA:AA:AA:03", -55, 1, 4);
    ap_mock.n = 1;
    ok = ap_scan_cached(&cache, false, 3000u, &ap_mock, out, &n); /* Networks' scan */
    CHECK(ok && ap_mock.calls == 1, "setup: Networks scanned once");

    /* Deauth Client's DC_NET_SCAN(force=false) entry equivalent. */
    n = 0;
    ok = ap_scan_cached(&cache, false, 3000u, &ap_mock, out, &n);
    CHECK(ok, "Deauth: AP-list read succeeds");
    CHECK(ap_mock.calls == 1, "Deauth: AP scan NOT reissued -- reused the cache");

    /* DC_CLIENT_SCAN: always its own fresh scan, a completely separate mock
     * untouched by the AP cache logic above. */
    client_mock.n = 2;
    {
        uint16_t dcount = mock_client_scan_run(&client_mock);
        CHECK(dcount == 2u, "Deauth: targeted client scan returns its own result");
    }
    CHECK(client_mock.calls == 1, "Deauth: exactly one targeted client scan performed");
    CHECK(ap_mock.calls == 1, "Deauth: AP-scan count is unaffected by the client scan");
}

/* 4. PCAP capture entered immediately afterward reuses AP cache. */
static void test_4_pcap_reuses_cache_no_rescan(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("GuestWiFi", "AA:AA:AA:AA:AA:04", -60, 9, 0);
    mock.n = 1;
    ok = ap_scan_cached(&cache, false, 4000u, &mock, out, &n); /* Networks' scan */
    CHECK(ok && mock.calls == 1, "setup: Networks scanned once");

    /* m1_capture_network_screen()'s read path: is_fresh() -> snapshot(),
     * no CAP_SCAN_CMD round trip. */
    n = 0;
    ok = ap_scan_cached(&cache, false, 4000u, &mock, out, &n);
    CHECK(ok, "PCAP: cache read succeeds");
    CHECK(mock.calls == 1, "PCAP: no scan -a round trip -- reused the cache");
    CHECK(n == 1u, "PCAP: sees the same AP list");
}

/* 5. Manual refresh invalidates and replaces cache. */
static void test_5_manual_refresh_replaces_cache(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("OldAP", "AA:AA:AA:AA:AA:05", -70, 3, 1);
    mock.n = 1;
    ok = ap_scan_cached(&cache, false, 5000u, &mock, out, &n);
    CHECK(ok && mock.calls == 1, "setup: initial scan");

    /* wifi_browse() returning 2 / Capture Network's UP-rescan: explicit
     * manual refresh, force_refresh=true, still within TTL. */
    mock.aps[0] = mk_ap("NewAP", "AA:AA:AA:AA:AA:06", -30, 7, 4);
    mock.n = 1;
    n = 0;
    ok = ap_scan_cached(&cache, true, 5500u, &mock, out, &n);

    CHECK(ok, "manual refresh: scan succeeds");
    CHECK(mock.calls == 2, "manual refresh: a real scan WAS issued despite a still-fresh cache");
    CHECK(n == 1u && strcmp((const char *)out[0].bssid, "AA:AA:AA:AA:AA:06") == 0,
          "manual refresh: result is the NEW scan, not the stale cached one");
    CHECK(cache.count == 1u && strcmp((const char *)cache.aps[0].bssid, "AA:AA:AA:AA:AA:06") == 0,
          "manual refresh: cache storage replaced with the new AP");
    CHECK(wifi_session_cache_is_fresh(&cache, 5500u, WIFI_SESSION_CACHE_TTL_MS),
          "manual refresh: cache is fresh again after the successful replace");
}

/* 6. Expiry triggers exactly one normal foreground AP scan. */
static void test_6_expiry_triggers_one_scan(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;
    uint32_t t0 = 10000u;
    uint32_t t1 = t0 + WIFI_SESSION_CACHE_TTL_MS + 1u; /* just past the TTL */

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("Expiring", "AA:AA:AA:AA:AA:07", -45, 4, 3);
    mock.n = 1;
    ok = ap_scan_cached(&cache, false, t0, &mock, out, &n);
    CHECK(ok && mock.calls == 1, "setup: initial scan");
    CHECK(!wifi_session_cache_is_fresh(&cache, t1, WIFI_SESSION_CACHE_TTL_MS),
          "sanity: cache is expired at t1");

    n = 0;
    ok = ap_scan_cached(&cache, false, t1, &mock, out, &n); /* no explicit refresh requested */

    CHECK(ok, "expiry: scan succeeds");
    CHECK(mock.calls == 2, "expiry: exactly one additional real scan (total 2), not zero, not more");
    CHECK(wifi_session_cache_is_fresh(&cache, t1, WIFI_SESSION_CACHE_TTL_MS),
          "expiry: cache is fresh again after the scan");
}

/* 7. Failed/cancelled refresh never leaves a cache marked fresh. */
static void test_7_failed_refresh_never_fresh(void)
{
    wifi_session_cache_t cache;
    mock_ap_scan_t mock = {0};
    wifi_session_ap_t out[8];
    uint16_t n = 0;
    bool ok;

    wifi_session_cache_init(&cache);
    mock.aps[0] = mk_ap("StillHere", "AA:AA:AA:AA:AA:08", -48, 2, 4);
    mock.n = 1;
    ok = ap_scan_cached(&cache, false, 6000u, &mock, out, &n);
    CHECK(ok, "setup: initial scan succeeds");
    CHECK(wifi_session_cache_is_fresh(&cache, 6000u, WIFI_SESSION_CACHE_TTL_MS),
          "setup: cache is fresh before the failed refresh");

    /* Manual refresh (force=true) that then fails (ESP no-reply / radio
     * busy / user cancels the retry loop). */
    mock.fail = true;
    n = 0;
    ok = ap_scan_cached(&cache, true, 6100u, &mock, out, &n);

    CHECK(!ok, "failed refresh: reports failure");
    CHECK(!wifi_session_cache_is_fresh(&cache, 6100u, WIFI_SESSION_CACHE_TTL_MS),
          "failed refresh: cache is NOT left fresh -- invalidate-before-scan, no publish on failure");
    CHECK(!cache.valid, "failed refresh: cache.valid is false, not silently still true");

    /* A subsequent non-forced read must therefore take a real scan again --
     * proves nothing was silently served as fresh after the failure. */
    mock.fail = false;
    n = 0;
    ok = ap_scan_cached(&cache, false, 6200u, &mock, out, &n);
    /* calls: 1 (setup) + 1 (failed refresh attempt) + 1 (this retry) = 3 */
    CHECK(ok && mock.calls == 3, "post-failure: next read performs a real scan, not a stale cache hit");
}

/* 8. Leaving the Wi-Fi area clears the session cache and selected AP. */
static void test_8_leaving_wifi_area_clears_cache_and_selection(void)
{
    wifi_session_cache_t *cache = wifi_session_cache_get();
    wifi_session_selected_t sel;
    wifi_session_ap_t ap = mk_ap("LeavingTest", "AA:AA:AA:AA:AA:09", -52, 5, 3);
    wifi_session_ap_t out;

    wifi_session_cache_publish(cache, &ap, 1u, 7000u);
    wifi_session_selected_set(&sel, &ap);
    CHECK(wifi_session_cache_is_fresh(cache, 7000u, WIFI_SESSION_CACHE_TTL_MS),
          "setup: canonical cache is fresh before leaving");
    CHECK(wifi_session_selected_get(&sel, &out), "setup: selected AP is set before leaving");

    /* menu_wifi_exit()'s effect: wifi_session_cache_get_reset() on the
     * canonical cache, wifi_session_selected_clear() (or the m1_wifi.c
     * equivalent, memset(&wifi_target_storage, ...)) on the selection. */
    wifi_session_cache_get_reset();
    wifi_session_selected_clear(&sel);

    CHECK(!wifi_session_cache_is_fresh(wifi_session_cache_get(), 7000u, WIFI_SESSION_CACHE_TTL_MS),
          "leaving Wi-Fi area: canonical cache is no longer fresh");
    CHECK(wifi_session_cache_get()->count == 0u, "leaving Wi-Fi area: cache entries cleared");
    CHECK(!wifi_session_selected_get(&sel, &out), "leaving Wi-Fi area: selected AP cleared");

    /* Re-entering afterward must take a real foreground scan -- proves the
     * clear isn't cosmetic (valid=false) while stale data/timestamp linger
     * in a way that could fool a differently-written freshness check. */
    {
        mock_ap_scan_t mock = {0};
        wifi_session_ap_t scan_out[8];
        uint16_t n = 0;
        mock.aps[0] = mk_ap("ReEntry", "AA:AA:AA:AA:AA:0A", -58, 8, 4);
        mock.n = 1;
        CHECK(ap_scan_cached(wifi_session_cache_get(), false, 7100u, &mock, scan_out, &n),
              "re-entry: scan succeeds");
        CHECK(mock.calls == 1, "re-entry: exactly one real scan after the cache clear");
    }
}

int main(void)
{
    test_1_networks_scan_populates_cache();
    test_2_handshake_reuses_cache_no_rescan();
    test_3_deauth_reuses_cache_then_own_client_scan();
    test_4_pcap_reuses_cache_no_rescan();
    test_5_manual_refresh_replaces_cache();
    test_6_expiry_triggers_one_scan();
    test_7_failed_refresh_never_fresh();
    test_8_leaving_wifi_area_clears_cache_and_selection();

    printf("\nm1_wifi_ap_cache_integration_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
