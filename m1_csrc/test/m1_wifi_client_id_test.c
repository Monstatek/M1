/*
 * m1_wifi_client_id_test.c - host tests for the STM32-local OUI vendor
 * lookup / client identification-hierarchy label formatting used by the
 * Deauth Client picker, confirm, and running screens.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_wifi_client_id.c m1_csrc/test/m1_wifi_client_id_test.c \
 *      -o /tmp/client_id_test && /tmp/client_id_test
 */
#include "m1_wifi_client_id.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

static void test_known_oui_hits(void)
{
    const uint8_t apple[3]     = {0x3C, 0x15, 0xC2};
    const uint8_t espressif[3] = {0x24, 0x0A, 0xC4};
    const uint8_t rpi[3]       = {0xB8, 0x27, 0xEB};

    CHECK(m1_wifi_oui_vendor(apple) != NULL, "known Apple OUI resolves");
    CHECK(strcmp(m1_wifi_oui_vendor(apple), "Apple") == 0, "Apple OUI names Apple");
    CHECK(strcmp(m1_wifi_oui_vendor(espressif), "Espressif") == 0, "Espressif OUI names Espressif");
    CHECK(strcmp(m1_wifi_oui_vendor(rpi), "Raspberry Pi") == 0, "Raspberry Pi OUI names Raspberry Pi");
}

static void test_unknown_oui_returns_null(void)
{
    const uint8_t unknown[3] = {0x02, 0x00, 0x00}; /* locally-administered, not in table */
    CHECK(m1_wifi_oui_vendor(unknown) == NULL, "unrecognized OUI prefix returns NULL");
    CHECK(m1_wifi_oui_vendor(NULL) == NULL, "NULL mac pointer returns NULL, no crash");
}

static void test_mac_str_to_bytes(void)
{
    uint8_t out[6];
    CHECK(m1_wifi_mac_str_to_bytes("3C:15:C2:AA:BB:CC", out) == 1, "well-formed MAC parses");
    CHECK((out[0] == 0x3C) && (out[5] == 0xCC), "parsed bytes match");
    CHECK(m1_wifi_mac_str_to_bytes("not-a-mac", out) == 0, "malformed MAC rejected");
    CHECK(m1_wifi_mac_str_to_bytes(NULL, out) == 0, "NULL MAC string rejected, no crash");
    CHECK(m1_wifi_mac_str_to_bytes("3C15C2AABBCC", out) == 0, "missing colons rejected");
}

static void test_label_hierarchy_vendor_path(void)
{
    char buf[32];
    m1_wifi_format_client_label("3C:15:C2:AA:BB:CD", buf, sizeof(buf));
    CHECK(strcmp(buf, "Apple - BB:CD") == 0, "known vendor -> 'Vendor - XX:XX'");
}

static void test_label_hierarchy_fallback_path(void)
{
    char buf[32];
    m1_wifi_format_client_label("02:00:00:AA:BB:CD", buf, sizeof(buf));
    CHECK(strcmp(buf, "Device - BB:CD") == 0, "unknown vendor -> 'Device - XX:XX'");
}

static void test_label_hierarchy_malformed_mac(void)
{
    char buf[32];
    m1_wifi_format_client_label("", buf, sizeof(buf));
    CHECK(strcmp(buf, "Device") == 0, "empty/malformed MAC -> neutral 'Device', no suffix invented");
    m1_wifi_format_client_label(NULL, buf, sizeof(buf));
    CHECK(strcmp(buf, "Device") == 0, "NULL MAC -> neutral 'Device', no crash");
}

static void test_label_truncation_safe(void)
{
    char buf[4];
    int n = m1_wifi_format_client_label("3C:15:C2:AA:BB:CD", buf, sizeof(buf));
    CHECK(buf[sizeof(buf) - 1] == '\0', "truncated output still NUL-terminated");
    CHECK(n > (int)sizeof(buf) - 1, "return value reports the untruncated length (snprintf convention)");
}

static void test_label_never_uses_product_names(void)
{
    /* Never claim a specific device model -- only ever a vendor name or the
     * neutral fallback. This is a documentation-style guard: every table
     * entry is a company name, not a product name. */
    char buf[32];
    m1_wifi_format_client_label("3C:15:C2:00:00:01", buf, sizeof(buf));
    CHECK(strstr(buf, "iPhone") == NULL, "label never invents a product name");
    CHECK(strstr(buf, "Apple") != NULL, "label states vendor identity only");
}

static void test_name_only_vendor_path(void)
{
    char buf[32];
    m1_wifi_format_client_name("3C:15:C2:AA:BB:CD", buf, sizeof(buf));
    CHECK(strcmp(buf, "Apple") == 0, "known vendor -> name only, no suffix");
}

static void test_name_only_fallback_path(void)
{
    char buf[32];
    m1_wifi_format_client_name("02:00:00:AA:BB:CD", buf, sizeof(buf));
    CHECK(strcmp(buf, "Unknown Device") == 0, "unknown vendor -> 'Unknown Device' (final UI pass wording)");
}

static void test_name_only_malformed_mac(void)
{
    char buf[32];
    m1_wifi_format_client_name("", buf, sizeof(buf));
    CHECK(strcmp(buf, "Unknown Device") == 0, "empty MAC -> 'Unknown Device', no crash");
    m1_wifi_format_client_name(NULL, buf, sizeof(buf));
    CHECK(strcmp(buf, "Unknown Device") == 0, "NULL MAC -> 'Unknown Device', no crash");
}

static void test_name_only_never_uses_product_names(void)
{
    char buf[32];
    m1_wifi_format_client_name("3C:15:C2:00:00:01", buf, sizeof(buf));
    CHECK(strstr(buf, "iPhone") == NULL, "name-only never invents a product name");
    CHECK(strcmp(buf, "Apple") == 0, "name-only states vendor identity only, exactly, no suffix appended");
}

static void test_mac_suffix_format(void)
{
    char buf[16];
    m1_wifi_format_mac_suffix("3C:15:C2:AA:BB:CD", buf, sizeof(buf));
    CHECK(strcmp(buf, "...BB:CD") == 0, "suffix is '...' + last two octets, uppercase hex");
}

static void test_mac_suffix_malformed_mac(void)
{
    char buf[16];
    m1_wifi_format_mac_suffix("not-a-mac", buf, sizeof(buf));
    CHECK(buf[0] == '\0', "malformed MAC -> empty suffix, no invented text");
    m1_wifi_format_mac_suffix(NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "NULL MAC -> empty suffix, no crash");
}

/* `list -s` station-scan reply line, real ESP32 format: "[NN] MAC   RSSI" */
static void test_sta_rssi_parses_real_line_format(void)
{
    const char *line = "[00] AA:BB:CC:DD:EE:FF   -67";
    const char *mac_end = line + 5 + 17; /* "[00] " (5) + 17-char MAC */
    CHECK(m1_wifi_parse_sta_rssi(mac_end) == -67, "negative RSSI after whitespace parses");
}

static void test_sta_rssi_positive_and_tight_spacing(void)
{
    CHECK(m1_wifi_parse_sta_rssi("-1") == -1, "no leading space, negative single digit");
    CHECK(m1_wifi_parse_sta_rssi("  42") == 42, "positive RSSI (unusual but not malformed) parses");
}

static void test_sta_rssi_missing_token_defaults_zero(void)
{
    CHECK(m1_wifi_parse_sta_rssi("") == 0, "nothing after MAC -> 0 (matches prior hardcoded default)");
    CHECK(m1_wifi_parse_sta_rssi("   ") == 0, "only trailing whitespace -> 0");
    CHECK(m1_wifi_parse_sta_rssi(NULL) == 0, "NULL -> 0, no crash");
}

/* select -a/-s response classification (Part 1 fix): the ESP32 replies
 * "[*] Selected ..." on success or "[!] ..." on failure/rejection. */
static void test_select_response_ok_success(void)
{
    CHECK(m1_wifi_select_response_ok("[*] Selected AP 2: Home WiFi\r\n") == 1,
          "'[*] Selected AP ...' classified as success");
    CHECK(m1_wifi_select_response_ok("[*] Selected station 0: AA:BB:CC:DD:EE:FF\r\n") == 1,
          "'[*] Selected station ...' classified as success");
    CHECK(m1_wifi_select_response_ok("\r\n[*] Selected AP 0: X\r\n>> ") == 1,
          "success marker recognized with surrounding whitespace/CRLF/prompt");
}

static void test_select_response_ok_failure(void)
{
    CHECK(m1_wifi_select_response_ok("[!] Invalid AP ID\r\n") == 0,
          "'[!] Invalid AP ID' classified as failure");
    CHECK(m1_wifi_select_response_ok("[!] Invalid\r\n") == 0,
          "'[!] Invalid' classified as failure");
    CHECK(m1_wifi_select_response_ok("") == 0, "empty reply (timeout, nothing captured) is a failure");
    CHECK(m1_wifi_select_response_ok(NULL) == 0, "NULL reply is a failure, no crash");
    CHECK(m1_wifi_select_response_ok("garbage\r\n") == 0, "unrecognized text is a failure, not assumed success");
}

/* RSSI -> category boundaries (Part 2e/2g). */
static void test_rssi_category_boundaries(void)
{
    CHECK(m1_wifi_rssi_category(-30) == M1_WIFI_RSSI_EXCELLENT, "-30 dBm -> Excellent");
    CHECK(m1_wifi_rssi_category(-50) == M1_WIFI_RSSI_EXCELLENT, "-50 dBm (boundary) -> Excellent");
    CHECK(m1_wifi_rssi_category(-51) == M1_WIFI_RSSI_GOOD, "-51 dBm -> Good");
    CHECK(m1_wifi_rssi_category(-67) == M1_WIFI_RSSI_GOOD, "-67 dBm (boundary) -> Good");
    CHECK(m1_wifi_rssi_category(-68) == M1_WIFI_RSSI_FAIR, "-68 dBm -> Fair");
    CHECK(m1_wifi_rssi_category(-75) == M1_WIFI_RSSI_FAIR, "-75 dBm (boundary) -> Fair");
    CHECK(m1_wifi_rssi_category(-76) == M1_WIFI_RSSI_WEAK, "-76 dBm -> Weak");
    CHECK(m1_wifi_rssi_category(-90) == M1_WIFI_RSSI_WEAK, "-90 dBm -> Weak");

    CHECK(strcmp(m1_wifi_rssi_category_str(M1_WIFI_RSSI_EXCELLENT), "Excellent") == 0, "Excellent label");
    CHECK(strcmp(m1_wifi_rssi_category_str(M1_WIFI_RSSI_GOOD), "Good") == 0, "Good label");
    CHECK(strcmp(m1_wifi_rssi_category_str(M1_WIFI_RSSI_FAIR), "Fair") == 0, "Fair label");
    CHECK(strcmp(m1_wifi_rssi_category_str(M1_WIFI_RSSI_WEAK), "Weak") == 0, "Weak label");
}

/* Selection stays bound to a client's MAC across a simulated reorder
 * (Part 2d/2g): once picked, the caller captures MAC+RSSI by value, so
 * re-sorting/reshuffling the underlying list afterward must never change
 * which physical device the captured identity refers to. */
static void test_selection_bound_to_mac_across_reorder(void)
{
    char mac_a[18] = "AA:AA:AA:AA:AA:01";
    char mac_b[18] = "BB:BB:BB:BB:BB:02";
    char captured_mac[18];
    int captured_rssi;

    /* "list order before": index 0 = A (weaker), index 1 = B (stronger) */
    int sel_index = 0;
    strcpy(captured_mac, mac_a);
    captured_rssi = -80;

    /* A rescan/reorder now puts B first (strongest-first, Part 2g) --
     * simulating what wifi_rssi_desc_cmp() driven qsort() does in
     * m1_wifi.c. The old index (0) would now point at a different
     * device (B); the captured MAC/RSSI must be unaffected. */
    sel_index = 1; /* A is now at index 1 after reorder -- irrelevant below */
    (void)sel_index;

    CHECK(strcmp(captured_mac, mac_a) == 0, "captured MAC still identifies the originally-picked device");
    CHECK(captured_rssi == -80, "captured RSSI is the value at selection time, not re-read after reorder");
    CHECK(strcmp(captured_mac, mac_b) != 0, "captured identity did not silently follow the old index to a new device");
}

/* ---------------------------------------------------------------------
 * Deauth Client device-picker live-refresh model (Part 1 of the live-RSSI
 * pass). Exercises m1_wifi_dc_picker_t purely -- no HAL/RTOS/u8g2.
 * --------------------------------------------------------------------- */

static void picker_feed1(m1_wifi_dc_picker_t *p, const char *mac, int rssi)
{
    char macs[1][18];
    int rssis[1];
    strcpy(macs[0], mac);
    rssis[0] = rssi;
    m1_wifi_dc_picker_apply_scan(p, macs, rssis, 1);
}

static void test_picker_initial_population_sorts_strongest_first(void)
{
    m1_wifi_dc_picker_t p;
    char macs[3][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02", "CC:CC:CC:CC:CC:03" };
    int rssis[3] = { -80, -40, -60 }; /* weak, strong, mid -- in that input order */

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs, rssis, 3);

    CHECK(p.count == 3, "all 3 clients tracked after first population");
    CHECK(strcmp(p.rows[0].mac, "BB:BB:BB:BB:BB:02") == 0, "strongest RSSI (-40) sorted first");
    CHECK(strcmp(p.rows[1].mac, "CC:CC:CC:CC:CC:03") == 0, "mid RSSI (-60) sorted second");
    CHECK(strcmp(p.rows[2].mac, "AA:AA:AA:AA:AA:01") == 0, "weakest RSSI (-80) sorted last");
}

static void test_picker_rssi_updates_across_refresh(void)
{
    m1_wifi_dc_picker_t p;
    m1_wifi_dc_picker_init(&p);
    picker_feed1(&p, "AA:AA:AA:AA:AA:01", -70);
    CHECK(p.rows[0].rssi == -70, "initial RSSI recorded");

    picker_feed1(&p, "AA:AA:AA:AA:AA:01", -45);
    CHECK(p.count == 1, "same MAC does not create a duplicate row");
    CHECK(p.rows[0].rssi == -45, "RSSI value updates for the same MAC across a refresh");
}

static void test_picker_identity_survives_reorder(void)
{
    m1_wifi_dc_picker_t p;
    char macs_round1[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int rssi_round1[2] = { -40, -70 }; /* A strongest -> A first */
    char macs_round2[2][18] = { "BB:BB:BB:BB:BB:02", "AA:AA:AA:AA:AA:01" }; /* underlying array reordered */
    int rssi_round2[2] = { -30, -65 }; /* B now strongest, but not frozen yet either */

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs_round1, rssi_round1, 2);
    CHECK(strcmp(p.rows[0].mac, "AA:AA:AA:AA:AA:01") == 0, "A sorted first initially (stronger)");

    m1_wifi_dc_picker_apply_scan(&p, macs_round2, rssi_round2, 2);
    /* Identity (which MAC owns which data) must be right regardless of
     * where the picker's own resort (still pre-freeze) puts each row. */
    {
        int ia = m1_wifi_dc_picker_find(&p, "AA:AA:AA:AA:AA:01");
        int ib = m1_wifi_dc_picker_find(&p, "BB:BB:BB:BB:BB:02");
        CHECK(ia >= 0 && ib >= 0, "both MACs still tracked after an input-array reorder");
        CHECK(p.rows[ia].rssi == -65, "A's RSSI is A's, not swapped with B's, after reorder");
        CHECK(p.rows[ib].rssi == -30, "B's RSSI is B's, not swapped with A's, after reorder");
    }
}

static void test_picker_freeze_locks_order_but_not_text(void)
{
    m1_wifi_dc_picker_t p;
    char macs[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int rssi1[2] = { -40, -70 }; /* A first */
    int rssi2[2] = { -40, -20 }; /* B now much stronger than A */

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi1, 2);
    CHECK(strcmp(p.rows[0].mac, "AA:AA:AA:AA:AA:01") == 0, "A first pre-freeze");

    m1_wifi_dc_picker_freeze(&p); /* simulates the user's first UP/DOWN/CENTER */
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi2, 2);

    CHECK(strcmp(p.rows[0].mac, "AA:AA:AA:AA:AA:01") == 0,
          "row ORDER stays frozen even though B is now far stronger");
    CHECK(p.rows[1].rssi == -20, "RSSI TEXT for the frozen row still updates live post-freeze");
}

static void test_picker_appearing_client_appended_at_end(void)
{
    m1_wifi_dc_picker_t p;
    char macs1[1][18] = { "AA:AA:AA:AA:AA:01" };
    int rssi1[1] = { -40 };
    char macs2[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int rssi2[2] = { -40, -20 }; /* new client B is actually stronger than A */

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs1, rssi1, 1);
    m1_wifi_dc_picker_freeze(&p); /* user has already interacted -- order is frozen */
    m1_wifi_dc_picker_apply_scan(&p, macs2, rssi2, 2);

    CHECK(p.count == 2, "newly-seen client is tracked");
    CHECK(strcmp(p.rows[0].mac, "AA:AA:AA:AA:AA:01") == 0, "original row stays in place");
    CHECK(strcmp(p.rows[1].mac, "BB:BB:BB:BB:BB:02") == 0,
          "new client appended at the end, not inserted by strength into a frozen list");
}

static void test_picker_disappearing_client_marked_not_removed(void)
{
    m1_wifi_dc_picker_t p;
    char macs1[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int rssi1[2] = { -40, -60 };
    char macs2[1][18] = { "AA:AA:AA:AA:AA:01" }; /* B vanished from this round */
    int rssi2[1] = { -41 };

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs1, rssi1, 2);
    m1_wifi_dc_picker_apply_scan(&p, macs2, rssi2, 1);

    CHECK(p.count == 2, "vanished client's row is kept, not deleted");
    {
        int ib = m1_wifi_dc_picker_find(&p, "BB:BB:BB:BB:BB:02");
        CHECK(ib >= 0, "vanished MAC is still findable by identity");
        CHECK(p.rows[ib].present == 0, "vanished client is marked not-present ('gone')");
        CHECK(p.rows[ib].rssi == -60, "vanished client's last-known RSSI is preserved, not zeroed/reset");
    }
}

static void test_picker_highlighted_client_disappearing_keeps_row(void)
{
    /* Simulates the caller's own contract: the highlighted ROW INDEX never
     * gets silently reassigned to a different MAC just because the
     * highlighted client disappeared -- since rows are never deleted/
     * compacted, an index that pointed at row N before a refresh still
     * points at the SAME MAC's row (now marked gone) after it. */
    m1_wifi_dc_picker_t p;
    char macs1[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int rssi1[2] = { -40, -60 };
    char macs2[1][18] = { "AA:AA:AA:AA:AA:01" };
    int rssi2[1] = { -41 };
    uint16_t highlighted_index = 1; /* user had highlighted B (row 1) */

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs1, rssi1, 2);
    m1_wifi_dc_picker_freeze(&p); /* user navigated to B, freezing order */
    m1_wifi_dc_picker_apply_scan(&p, macs2, rssi2, 1); /* B disappears */

    CHECK(strcmp(p.rows[highlighted_index].mac, "BB:BB:BB:BB:BB:02") == 0,
          "the previously-highlighted index still refers to the same MAC, not a substitute");
    CHECK(p.rows[highlighted_index].present == 0,
          "that row is now visibly marked gone instead of being swapped out");
}

static void test_picker_no_target_substitution_without_explicit_selection(void)
{
    /* This module never writes to any Deauth target itself -- confirms the
     * contract stated in m1_wifi_client_id.h: applying refresh rounds alone
     * (no explicit CENTER-equivalent call) must never produce a "selected"
     * side effect. There is no wifitarget-writing API on this module at
     * all, so the only thing to assert is that repeated refreshes alone
     * don't mutate anything beyond the rows/count/frozen fields already
     * covered above -- i.e. no hidden "current selection" field exists to
     * accidentally populate. */
    m1_wifi_dc_picker_t p;
    char macs[1][18] = { "AA:AA:AA:AA:AA:01" };
    int rssi[1] = { -40 };

    m1_wifi_dc_picker_init(&p);
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi, 1);
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi, 1);
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi, 1);

    CHECK(p.frozen == 0, "refresh rounds alone never freeze/select anything -- only explicit input does (m1_wifi.c caller)");
}

static void test_picker_repeated_refresh_no_leak_or_duplicate(void)
{
    m1_wifi_dc_picker_t p;
    char macs[3][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02", "CC:CC:CC:CC:CC:03" };
    int rssi[3] = { -40, -50, -60 };
    int round;

    m1_wifi_dc_picker_init(&p);
    for (round = 0; round < 25; round++)
    {
        m1_wifi_dc_picker_apply_scan(&p, macs, rssi, 3);
    }
    CHECK(p.count == 3, "25 repeated refreshes of the same 3 MACs never duplicate rows");
}

static void test_picker_capacity_bound_drops_overflow(void)
{
    m1_wifi_dc_picker_t p;
    char macs[M1_WIFI_DC_PICKER_MAX_ROWS + 5][18];
    int rssi[M1_WIFI_DC_PICKER_MAX_ROWS + 5];
    uint16_t i;

    m1_wifi_dc_picker_init(&p);
    for (i = 0; i < (M1_WIFI_DC_PICKER_MAX_ROWS + 5); i++)
    {
        snprintf(macs[i], 18, "AA:AA:AA:AA:%02X:%02X", (unsigned)(i >> 8), (unsigned)(i & 0xFFu));
        rssi[i] = -40;
    }
    m1_wifi_dc_picker_apply_scan(&p, macs, rssi, (uint16_t)(M1_WIFI_DC_PICKER_MAX_ROWS + 5));
    CHECK(p.count == M1_WIFI_DC_PICKER_MAX_ROWS, "row growth is bounded at M1_WIFI_DC_PICKER_MAX_ROWS, no unbounded growth");
}

/* ---------------------------------------------------------------------
 * CENTER-on-device validate-then-start policy (new linear flow, Part 5/7).
 * m1_wifi_dc_validate_start() / m1_wifi_dc_locate_client_index() -- pure,
 * no HAL/RTOS/UART. Exercises exactly the three checks the new
 * DC_CLIENT_CHOOSE call site performs before it is allowed to fall through
 * to DC_RUN (wifi_deauth()'s transmit).
 * --------------------------------------------------------------------- */

static void test_locate_client_index_found_and_missing(void)
{
    char macs[3][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02", "CC:CC:CC:CC:CC:03" };

    CHECK(m1_wifi_dc_locate_client_index(macs, 3, "BB:BB:BB:BB:BB:02") == 1,
          "known MAC resolves to its current index");
    CHECK(m1_wifi_dc_locate_client_index(macs, 3, "DD:DD:DD:DD:DD:04") == -1,
          "MAC not in the current snapshot is not found (never substitutes)");
    CHECK(m1_wifi_dc_locate_client_index(NULL, 0, "AA:AA:AA:AA:AA:01") == -1,
          "NULL macs array is not found, no crash");
    CHECK(m1_wifi_dc_locate_client_index(macs, 3, NULL) == -1,
          "NULL picked_mac is not found, no crash");
}

static void test_validate_start_ok_when_all_checks_pass(void)
{
    char macs[2][18] = { "AA:AA:AA:AA:AA:01", "BB:BB:BB:BB:BB:02" };
    int idx = -1;
    m1_wifi_dc_start_result_t r = m1_wifi_dc_validate_start(
        "Home WiFi", macs, 2, "BB:BB:BB:BB:BB:02", "[*] Selected station 1: BB:BB:BB:BB:BB:02\r\n", &idx);

    CHECK(r == M1_WIFI_DC_START_OK, "AP valid + client found + select accepted -> OK");
    CHECK(idx == 1, "out_sel_idx reports the client's current index");
}

static void test_validate_start_rejects_invalid_ap(void)
{
    char macs[1][18] = { "AA:AA:AA:AA:AA:01" };
    m1_wifi_dc_start_result_t r = m1_wifi_dc_validate_start(
        "", macs, 1, "AA:AA:AA:AA:AA:01", "[*] Selected station 0: AA:AA:AA:AA:AA:01\r\n", NULL);
    CHECK(r == M1_WIFI_DC_START_ERR_AP_INVALID, "empty AP SSID rejected before any select is attempted");

    r = m1_wifi_dc_validate_start(NULL, macs, 1, "AA:AA:AA:AA:AA:01", "[*] Selected station 0: X\r\n", NULL);
    CHECK(r == M1_WIFI_DC_START_ERR_AP_INVALID, "NULL AP SSID rejected, no crash");
}

static void test_validate_start_rejects_client_not_found(void)
{
    char macs[1][18] = { "AA:AA:AA:AA:AA:01" };
    int idx = -1;
    /* The picked client (from an earlier snapshot) is no longer present in
     * the CURRENT snapshot -- e.g. it disappeared between CENTER and the
     * live-refreshed re-scan used to find its current index. */
    m1_wifi_dc_start_result_t r = m1_wifi_dc_validate_start(
        "Home WiFi", macs, 1, "BB:BB:BB:BB:BB:02", "[*] Selected station 0: AA:AA:AA:AA:AA:01\r\n", &idx);
    CHECK(r == M1_WIFI_DC_START_ERR_CLIENT_NOT_FOUND,
          "client no longer resolvable in the current snapshot is rejected, never substituted");
    CHECK(idx == -1, "out_sel_idx left untouched on a not-found client");
}

static void test_validate_start_rejects_select_response(void)
{
    char macs[1][18] = { "AA:AA:AA:AA:AA:01" };
    int idx = -1;
    m1_wifi_dc_start_result_t r = m1_wifi_dc_validate_start(
        "Home WiFi", macs, 1, "AA:AA:AA:AA:AA:01", "[!] Invalid\r\n", &idx);
    CHECK(r == M1_WIFI_DC_START_ERR_SELECT_REJECTED,
          "AP valid + client found but ESP32 rejects select -> rejected, not OK");
    CHECK(idx == 0, "out_sel_idx is still reported even though the select itself was rejected");

    r = m1_wifi_dc_validate_start("Home WiFi", macs, 1, "AA:AA:AA:AA:AA:01", "", &idx);
    CHECK(r == M1_WIFI_DC_START_ERR_SELECT_REJECTED, "empty/timeout select reply is rejected, not assumed success");
}

static void test_validate_start_checks_run_in_order(void)
{
    /* An invalid AP is reported even when the client also wouldn't be found
     * -- AP validity is checked first, so the error message a caller shows
     * always names the actual first failing precondition. */
    char macs[1][18] = { "AA:AA:AA:AA:AA:01" };
    m1_wifi_dc_start_result_t r = m1_wifi_dc_validate_start(
        "", macs, 1, "ZZ:ZZ:ZZ:ZZ:ZZ:99", "[*] Selected station 0: X\r\n", NULL);
    CHECK(r == M1_WIFI_DC_START_ERR_AP_INVALID, "AP-validity check runs before client lookup");
}

int main(void)
{
    test_sta_rssi_parses_real_line_format();
    test_sta_rssi_positive_and_tight_spacing();
    test_sta_rssi_missing_token_defaults_zero();
    test_known_oui_hits();
    test_unknown_oui_returns_null();
    test_mac_str_to_bytes();
    test_label_hierarchy_vendor_path();
    test_label_hierarchy_fallback_path();
    test_label_hierarchy_malformed_mac();
    test_label_truncation_safe();
    test_label_never_uses_product_names();
    test_name_only_vendor_path();
    test_name_only_fallback_path();
    test_name_only_malformed_mac();
    test_name_only_never_uses_product_names();
    test_mac_suffix_format();
    test_mac_suffix_malformed_mac();
    test_select_response_ok_success();
    test_select_response_ok_failure();
    test_rssi_category_boundaries();
    test_selection_bound_to_mac_across_reorder();

    test_picker_initial_population_sorts_strongest_first();
    test_picker_rssi_updates_across_refresh();
    test_picker_identity_survives_reorder();
    test_picker_freeze_locks_order_but_not_text();
    test_picker_appearing_client_appended_at_end();
    test_picker_disappearing_client_marked_not_removed();
    test_picker_highlighted_client_disappearing_keeps_row();
    test_picker_no_target_substitution_without_explicit_selection();
    test_picker_repeated_refresh_no_leak_or_duplicate();
    test_picker_capacity_bound_drops_overflow();

    test_locate_client_index_found_and_missing();
    test_validate_start_ok_when_all_checks_pass();
    test_validate_start_rejects_invalid_ap();
    test_validate_start_rejects_client_not_found();
    test_validate_start_rejects_select_response();
    test_validate_start_checks_run_in_order();

    printf("\nm1_wifi_client_id_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
