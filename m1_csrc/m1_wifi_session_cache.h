/* See COPYING.txt for license details. */

/*
*
* m1_wifi_session_cache.h
*
* Passive, session-scoped Wi-Fi AP scan cache shared by Networks, Handshake,
* Deauth Client, and PCAP/Wireshark capture. Pure data + logic, NO RTOS, HAL,
* or ESP dependencies -- host-testable (see test/m1_wifi_session_cache_test.c).
*
* wifi_session_cache_get() below is the ONE canonical instance every real
* consumer (m1_wifi.c's Networks/Handshake/Deauth Client, m1_capture_link.c's
* PCAP path) shares -- a scan taken in one tool is what "fresh" means for all
* of them. Everything else in this header (init/publish/invalidate/is_fresh/
* snapshot) is instance-agnostic and stays fully host-testable against a
* caller-owned local instance, same as before; only the singleton accessor
* itself is a "the" instance rather than "a" instance.
*
* This is deliberately NOT the reverted 5084a3b/590cec1 shared-discovery
* design and does not reintroduce it:
*   - Nothing in this module ever triggers a scan. It is pure storage.
*   - There is no background task, no scheduler, no periodic anything here.
*     The only way data enters this cache is wifi_session_cache_publish(),
*     called once by whichever code just finished a user-initiated scan.
*   - Single-writer discipline: the STM32 menu system runs one screen at a
*     time on one task, so there is never more than one writer active, and
*     publish() is called only at the one well-defined "a scan just
*     completed" moment -- never from ISR context, never concurrently with
*     itself. No mutex is needed for that reason; the valid flag + timestamp
*     are the entire "guard."
*   - Atomicity: publish() takes the caller's already-complete, bounds-checked
*     result and copies it into the cache's storage, then flips `valid` and
*     stamps `last_scan_ts` last. A reader taking a snapshot (which itself
*     is just a bounded memcpy) can only ever see either the prior complete
*     state or the new complete state -- never a partial write, because nothing
*     else runs between "copy the new APs in" and "mark it valid" on a single
*     task with no preemption of this module's own calls by another writer.
*
* M1 Project
*
*/

#ifndef M1_WIFI_SESSION_CACHE_H_
#define M1_WIFI_SESSION_CACHE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounded set of nearby APs. 2.4 GHz has few legal channels; 48 is generous
 * (matches the ESP side's own MAX_AP_RECORDS). */
#define WIFI_SESSION_CACHE_MAX_APS  48u

/* Default freshness window. Named/tunable per the spec, not hardcoded inline
 * at every call site. Widened from the spec's original ~15 s to ~120 s so one
 * scan is reused across normal cross-tool navigation (Networks -> select AP ->
 * Capture, etc.) instead of forcing a redundant `scan -a` per tool. The cache
 * is display-only (see wifi_ap_scan_list_cached): a reused list is byte-for-
 * byte what a fresh scan would yield, and targeted station scans are always
 * fresh regardless, so a longer window trades only mild AP-list staleness. */
#define WIFI_SESSION_CACHE_TTL_MS   120000u

/* Field-for-field the same shape as wifi_scanlist_t (ctrl_api.h), but this
 * module deliberately does not include ctrl_api.h -- it has zero dependency
 * on the ESP control-protocol headers so it stays trivially host-testable.
 * Callers convert to/from wifi_scanlist_t at the integration boundary. */
typedef struct {
    uint8_t ssid[33];
    uint8_t bssid[18];
    int     rssi;
    int     channel;
    int     encryption_mode;
} wifi_session_ap_t;

typedef struct {
    wifi_session_ap_t aps[WIFI_SESSION_CACHE_MAX_APS];
    uint16_t count;
    bool     valid;
    uint32_t last_scan_ts; /* ms timestamp of the last successful publish() */
} wifi_session_cache_t;

/* Selected-AP context for the capture-only consumers (PCAP/Wireshark). Not
 * used by Networks/Handshake/Deauth, which already have their own existing
 * "wifitarget" selection storage -- this is specifically the capture path's
 * equivalent, per the spec (SSID/BSSID/channel/security/RSSI + capture
 * channel derived from it). */
typedef struct {
    wifi_session_ap_t ap;
    bool valid;
} wifi_session_selected_t;

/* Zero the cache: empty, invalid, last_scan_ts = 0. Call once at boot/module
 * init (or any time you want to hard-clear it, though invalidate() is the
 * normal path for that during normal operation). */
void wifi_session_cache_init(wifi_session_cache_t *c);

/* Atomic publish: bounds-checks and copies `count` entries from `aps` (clamps
 * to WIFI_SESSION_CACHE_MAX_APS -- excess entries are dropped, never
 * overflowed), then marks the cache valid and stamps last_scan_ts = now_ms.
 * Call this exactly once, after a scan has fully completed and its result is
 * already a complete, validated array -- never incrementally per-AP. `aps`
 * may be NULL only if count == 0. */
void wifi_session_cache_publish(wifi_session_cache_t *c,
                                const wifi_session_ap_t *aps,
                                uint16_t count,
                                uint32_t now_ms);

/* Manual refresh / failure handling: marks the cache invalid immediately
 * (valid=false) without touching the stored entries. Call this BEFORE
 * starting a forced rescan (manual refresh button) so that if the rescan
 * then fails or is cancelled, the cache is left invalid rather than stamped
 * fresh over data that was never actually refreshed. */
void wifi_session_cache_invalidate(wifi_session_cache_t *c);

/* True iff the cache is valid AND (now_ms - last_scan_ts) < ttl_ms. Safe
 * across a single 32-bit millisecond wraparound (unsigned subtraction).
 * Pass WIFI_SESSION_CACHE_TTL_MS for the spec's default ~15s window, or a
 * different ttl_ms if a caller ever needs one. */
bool wifi_session_cache_is_fresh(const wifi_session_cache_t *c,
                                 uint32_t now_ms,
                                 uint32_t ttl_ms);

/* Age of the cache in ms (now_ms - last_scan_ts), for the "scan-age cue" the
 * spec requires so stale data is never silently presented as live. Returns 0
 * if the cache is not valid. Wraparound-safe like is_fresh(). */
uint32_t wifi_session_cache_age_ms(const wifi_session_cache_t *c, uint32_t now_ms);

/* Copies up to out_cap entries into out (a true copy, not a pointer into the
 * cache's own storage, so the reader holds a stable snapshot regardless of
 * what happens to the cache afterward). Returns the number of entries
 * actually copied (<= out_cap, <= c->count). Returns 0 without touching out
 * if the cache is not valid or out_cap == 0. */
uint16_t wifi_session_cache_snapshot(const wifi_session_cache_t *c,
                                     wifi_session_ap_t *out,
                                     uint16_t out_cap);

/* The one canonical, process-wide AP cache instance. Lazily initialised
 * (empty/invalid) on first call; every later call returns the same pointer.
 * Not reentrant/thread-safe by design -- same single-writer-task discipline
 * as the rest of this module (see the file banner above); the STM32 menu
 * system runs one screen at a time on one task. */
wifi_session_cache_t *wifi_session_cache_get(void);

/* Resets the canonical instance to empty/invalid (same effect as
 * wifi_session_cache_init() on it) without needing the caller to hold a
 * pointer to it first. For "leaving the Wi-Fi area" cache-clear points. */
void wifi_session_cache_get_reset(void);

/* --- selected-AP context (capture consumers only) --- */

/* Stores a copy of *ap as the session's selected network for capture. */
void wifi_session_selected_set(wifi_session_selected_t *sel, const wifi_session_ap_t *ap);

/* Clears the selection (valid=false). */
void wifi_session_selected_clear(wifi_session_selected_t *sel);

/* Copies the current selection into *out. Returns true iff a selection is
 * currently set (sel->valid); *out is left untouched if it returns false. */
bool wifi_session_selected_get(const wifi_session_selected_t *sel, wifi_session_ap_t *out);

#ifdef __cplusplus
}
#endif

#endif /* M1_WIFI_SESSION_CACHE_H_ */
