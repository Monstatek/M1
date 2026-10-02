/* See COPYING.txt for license details. */

/*
 * m1_wifi_client_id.h
 *
 * Local, STM32-only Wi-Fi client identification helpers used by the Deauth
 * Client picker/confirm/running screens.
 *
 * The wire protocol between the ESP32 and STM32 carries exactly MAC (6
 * bytes) + RSSI (1 signed byte) per scanned station -- no channel, no
 * AP-BSSID echo, no hostname, no IP, no vendor/OUI, no device name. This
 * module never claims more than that: it only maps the MAC's OUI (vendor)
 * prefix, when recognized, to a manufacturer name via a small static table
 * of well-known IEEE OUI assignments. A vendor hit is always a vendor
 * identity ("Apple"), never a guessed product name ("iPhone").
 *
 * Pure data + logic, NO RTOS, HAL, u8g2, or ESP dependencies -- host
 * testable (see test/m1_wifi_client_id_test.c).
 *
 * M1 Project
 */

#ifndef M1_WIFI_CLIENT_ID_H_
#define M1_WIFI_CLIENT_ID_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Looks up the vendor name for a MAC's first 3 octets (the IEEE OUI) in the
 * local static table. mac[0..2] are the OUI bytes. Returns a pointer to a
 * static, NUL-terminated vendor name string (e.g. "Apple") on a known
 * prefix, or NULL if the prefix is not in the table. Never allocates. */
const char *m1_wifi_oui_vendor(const uint8_t mac[3]);

/* Parses a colon-separated MAC address string ("AA:BB:CC:DD:EE:FF") into 6
 * raw bytes. Returns 1 on success, 0 if mac_str is not a well-formed MAC
 * (also 0 on a NULL argument). out_bytes must have room for 6 bytes. */
int m1_wifi_mac_str_to_bytes(const char *mac_str, uint8_t out_bytes[6]);

/* Builds the client identification-hierarchy label for display:
 *   - known OUI vendor  -> "<Vendor> - XX:XX"  (last two octets, uppercase hex)
 *   - unknown/malformed -> "Device - XX:XX"
 * mac_str is the client's MAC as a colon-separated string. out/out_cap is
 * the caller's buffer; the result is always NUL-terminated and truncated to
 * fit if out_cap is small. Returns the number of bytes that would have been
 * written (excluding the NUL), same convention as snprintf, or 0 if out is
 * NULL or out_cap is 0. Never invents a hostname or product name. */
int m1_wifi_format_client_label(const char *mac_str, char *out, size_t out_cap);

/* Select Device screen presentation (final UI pass): the name and the MAC
 * suffix are shown as two SEPARATE lines there (not combined like
 * m1_wifi_format_client_label() above, which stays as-is for the running/
 * confirm screens) -- these two helpers produce each line's text on its own.
 * Never invents a hostname or product name; a known vendor is a vendor name
 * only ("Samsung"), never a guessed product ("Samsung TV"). */

/* Name line only: known OUI vendor -> "<Vendor>" (e.g. "Samsung"), unknown or
 * malformed MAC -> "Unknown Device". Same NUL-termination/truncation/return
 * convention as m1_wifi_format_client_label(). */
int m1_wifi_format_client_name(const char *mac_str, char *out, size_t out_cap);

/* Shortened MAC suffix line only: "...XX:XX" (last two octets, uppercase
 * hex, literal "..." prefix marking it as a truncated identifier -- the full
 * MAC is never shown on this screen, only kept internally as the stable
 * targeting identity). Empty string if mac_str is not a well-formed MAC.
 * Same NUL-termination/truncation/return convention as the label above. */
int m1_wifi_format_mac_suffix(const char *mac_str, char *out, size_t out_cap);

/* Parses the trailing RSSI token on an ESP32 `list -s` station-scan reply
 * line, e.g. "[NN] AA:BB:CC:DD:EE:FF   -67", given a pointer to just past
 * the 17-character MAC text. Returns the parsed signed RSSI, or 0 if no
 * numeric token follows (matches the STM32 parser's previous hardcoded
 * default when nothing follows the MAC, so picking up a real RSSI is purely
 * additive). Never dereferences past mac_end's NUL terminator. */
int m1_wifi_parse_sta_rssi(const char *mac_end);

/* Classifies an ESP32 `select -a`/`select -s` UART reply as success or
 * failure. The ESP32's handle_select_a/handle_select_s reply
 * "[*] Selected AP %d: %s" / "[*] Selected station %d: %s" on success, or
 * "[!] Invalid AP ID" / "[!] Invalid" (or similar) on failure. Returns 1
 * only when resp contains the "[*]" success marker; returns 0 for anything
 * else, including an explicit "[!]" rejection, an empty string, or NULL
 * (e.g. a read timeout with nothing captured). Pure text classification --
 * callers are responsible for the UART write/read and its own timeout. */
int m1_wifi_select_response_ok(const char *resp);

/* Live-RSSI signal-strength categories shown on the Deauth Client device
 * picker (m1_wifi_rssi_category_str() below gives the display label). */
typedef enum {
    M1_WIFI_RSSI_EXCELLENT = 0,
    M1_WIFI_RSSI_GOOD,
    M1_WIFI_RSSI_FAIR,
    M1_WIFI_RSSI_WEAK
} m1_wifi_rssi_category_t;

/* Categorizes an RSSI reading in dBm (e.g. -46) into one of the four bands
 * above, using fixed, documented thresholds (stronger signal = less
 * negative dBm):
 *   rssi >= -50             -> Excellent
 *   -67 <= rssi <  -50       -> Good
 *   -75 <= rssi <  -67       -> Fair
 *   rssi <  -75              -> Weak
 * Pure function -- no I/O, no HAL. */
m1_wifi_rssi_category_t m1_wifi_rssi_category(int rssi);

/* Human-readable label for a category, e.g. "Excellent". Never NULL. */
const char *m1_wifi_rssi_category_str(m1_wifi_rssi_category_t cat);

/* ---------------------------------------------------------------------
 * Deauth Client device-picker live-refresh model (Part 1).
 *
 * Pure data + logic, no RTOS/HAL/u8g2 dependency -- host testable (see
 * test/m1_wifi_client_id_test.c). This is the MAC-bound identity/ordering
 * state that survives a live re-scan while the picker is on screen:
 *   - rows are keyed by MAC string, never by array index;
 *   - the FIRST population is sorted strongest-RSSI-first;
 *   - once frozen (the caller's first UP/DOWN/CENTER), row ORDER never
 *     changes again -- RSSI/presence within a row still updates live;
 *   - a MAC seen in a refresh round that wasn't known before is appended
 *     at the end, never inserted "by strength" into a frozen region;
 *   - a MAC missing from a refresh round is never deleted -- it is kept,
 *     marked not-present ("gone"), so a highlighted row is never silently
 *     yanked out from under the cursor or replaced by another MAC.
 * The caller (m1_wifi.c's device picker) owns capturing the selected row's
 * MAC/RSSI by value at CENTER time -- this module never writes to any
 * Deauth target itself.
 * --------------------------------------------------------------------- */

/* Practical cap on simultaneously-tracked stations in one picker session.
 * Not a wire-format limit (the ESP32<->STM32 "list -s" reply has none) --
 * just a fixed, bounded backing array so a picker left open indefinitely
 * cannot grow without limit. Comfortably above any realistic single-AP
 * station count. */
#define M1_WIFI_DC_PICKER_MAX_ROWS 64u

typedef struct {
    char mac[18];    /* "AA:BB:CC:DD:EE:FF" + NUL */
    int rssi;
    uint8_t present; /* 1 = seen in the most recent refresh round, 0 = "gone" */
} m1_wifi_dc_row_t;

typedef struct {
    m1_wifi_dc_row_t rows[M1_WIFI_DC_PICKER_MAX_ROWS];
    uint16_t count;
    uint8_t frozen; /* 1 once the caller's first UP/DOWN/CENTER has happened */
} m1_wifi_dc_picker_t;

/* Resets picker to empty/unfrozen. Call once when the picker screen opens. */
void m1_wifi_dc_picker_init(m1_wifi_dc_picker_t *picker);

/* Marks row order frozen from now on. Idempotent -- safe to call on every
 * UP/DOWN/CENTER event, not just the first. NULL-safe (no-op). */
void m1_wifi_dc_picker_freeze(m1_wifi_dc_picker_t *picker);

/* Applies one refresh round (a fresh "scan -s"/"list -s" snapshot) to the
 * picker: updates RSSI + presence for MACs already known (matched by MAC
 * string, not position), appends any newly-seen MAC at the end (bounded by
 * M1_WIFI_DC_PICKER_MAX_ROWS -- a round that would exceed it silently drops
 * the overflow rather than growing unbounded), and marks any previously
 * known MAC absent from this round as not-present without removing its
 * row. While picker->frozen is false (including the very first call, which
 * is how the initial "strongest-RSSI-first" population happens), rows are
 * fully re-sorted strongest-first after applying the round. Once frozen,
 * this function never reorders rows -- only in-place RSSI/presence updates
 * and end-of-list appends happen.
 * macs/rssis are parallel arrays of length n (n capped internally at
 * M1_WIFI_DC_PICKER_MAX_ROWS); NULL-safe (no-op on a NULL picker). */
void m1_wifi_dc_picker_apply_scan(m1_wifi_dc_picker_t *picker,
                                   const char macs[][18], const int *rssis,
                                   uint16_t n);

/* Locates a MAC's current row index, or -1 if it is not (yet) tracked.
 * Used to re-derive a highlighted row after a refresh, never an index. */
int m1_wifi_dc_picker_find(const m1_wifi_dc_picker_t *picker, const char *mac);

/* ---------------------------------------------------------------------
 * CENTER-on-device validate-then-start policy (new linear flow, Part 5).
 *
 * Selecting a device is now the final action: it must re-locate the picked
 * client by MAC (never by a stale index) in the CURRENT scan snapshot,
 * then -- only if the AP is still valid and the client is still resolvable
 * -- issue "select -s <idx>" and check the reply. This module never does
 * the UART I/O itself; it only encodes the pure decision policy so it can
 * be unit tested without HAL/RTOS/u8g2.
 * --------------------------------------------------------------------- */
typedef enum {
    M1_WIFI_DC_START_OK = 0,               /* AP valid, client found, select accepted -- safe to transmit */
    M1_WIFI_DC_START_ERR_AP_INVALID,       /* AP has no SSID (never selected, or was cleared) */
    M1_WIFI_DC_START_ERR_CLIENT_NOT_FOUND, /* picked_mac not present in the current scan snapshot */
    M1_WIFI_DC_START_ERR_SELECT_REJECTED   /* client re-located, but the ESP32 rejected "select -s" */
} m1_wifi_dc_start_result_t;

/* Re-locates picked_mac by value in the parallel macs[]/count snapshot (the
 * same snapshot the device picker is currently showing). Returns its
 * current index, or -1 if picked_mac is not present in this round. Never
 * assumes a previously-known index is still valid. */
int m1_wifi_dc_locate_client_index(const char macs[][18], uint16_t count,
                                    const char *picked_mac);

/* Full validate-then-start decision for CENTER-on-device:
 *   1. ap_ssid must be non-empty (a valid AP is selected).
 *   2. picked_mac must still resolve to a row in macs[]/count (out_sel_idx
 *      receives that row's current index on success).
 *   3. select_resp -- the ESP32's reply to "select -s <out_sel_idx>",
 *      already sent/read by the caller once it has an index from step 2 --
 *      must classify as success (m1_wifi_select_response_ok()).
 * Checks run in this order and stop at the first failure, so a caller can
 * report exactly which precondition failed. Pure function: does no I/O
 * itself, so a caller that fails at step 2 never had a reason to talk to
 * the ESP32 in the first place. */
m1_wifi_dc_start_result_t m1_wifi_dc_validate_start(const char *ap_ssid,
                                                     const char macs[][18],
                                                     uint16_t count,
                                                     const char *picked_mac,
                                                     const char *select_resp,
                                                     int *out_sel_idx);

#ifdef __cplusplus
}
#endif

#endif /* M1_WIFI_CLIENT_ID_H_ */
