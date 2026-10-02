/*
 * m1_capture_scan_parse.h - parser for the ESP32-C6 `scan -a` ASCII reply.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Turns the console reply of `scan -a` (see esp32c6_wifi.c: scan()/print_ap_list())
 * into a bounded, in-session AP list for the Capture Network workflow. Pure
 * logic, no UART/HAL — feed it the captured reply buffer, get a result back.
 *
 * Expected reply shape (verbatim from esp32c6_wifi.c):
 *   [*] Mode: STA... Scanning
 *   [+] Scan complete. <N> AP(s) found.
 *
 *   [ID] SSID                               CH   RSSI  BSSID              AUTH
 *   [00] "My Network"                       6    -47   AA:BB:CC:DD:EE:FF  WPA2
 *   ...
 *
 * Column order is ID, SSID, CH, RSSI, BSSID, AUTH — SSID is NOT the line
 * remainder (AUTH is last), so fields are located by anchoring on the
 * colon-hex BSSID and walking backward/forward from there, not by column
 * position. This mirrors the field walk already proven in m1_wifi.c's
 * parse_wifi_list_a_lines() for the frozen Networks/Deauth/Handshake tools.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M1_CAP_SCAN_SSID_MAX   33u  /* 32-byte SSID + NUL (802.11 max) */
#define M1_CAP_SCAN_BSSID_LEN  18u  /* "XX:XX:XX:XX:XX:XX" + NUL */
#define M1_CAP_SCAN_AUTH_MAX   16u  /* longest ESP auth token + NUL, generous bound */
#define M1_CAP_SCAN_MAX_APS    32u  /* in-session bound; not the ESP's own MAX_AP_RECORDS */

typedef struct {
    char ssid[M1_CAP_SCAN_SSID_MAX];
    char bssid[M1_CAP_SCAN_BSSID_LEN];
    char auth[M1_CAP_SCAN_AUTH_MAX];
    int  channel;   /* 1-14 */
    int  rssi;      /* dBm, negative */
} m1_cap_scan_ap_t;

typedef struct {
    m1_cap_scan_ap_t aps[M1_CAP_SCAN_MAX_APS];
    uint32_t count;     /* rows actually stored, <= M1_CAP_SCAN_MAX_APS */
    uint32_t reported;  /* AP count claimed by the "N AP(s) found" sentinel */
    bool     valid;     /* true iff the sentinel line was found and parsed */
} m1_cap_scan_result_t;

/*
 * Parses `buf[0..len)` (need not be NUL-terminated; never read past buf+len).
 * `out` is fully (re)initialised on every call.
 *
 * Returns 0 if the "N AP(s) found" sentinel was located (out->valid = true;
 * out->count may still be less than out->reported if individual rows were
 * malformed or the M1_CAP_SCAN_MAX_APS bound was hit -- that mismatch is the
 * caller's signal to treat the list as short rather than complete).
 *
 * Returns -1 if the sentinel line is missing entirely (out->valid = false,
 * out->count = 0) -- e.g. empty buffer, unrelated text, or a reply that
 * never reached the "Scan complete" line.
 */
int m1_cap_scan_parse(const char *buf, uint32_t len, m1_cap_scan_result_t *out);

#ifdef __cplusplus
}
#endif
