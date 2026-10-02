/* See COPYING.txt for license details. */

/*
*
* m1_wifi_session_cache.c
*
* See m1_wifi_session_cache.h.
*
* M1 Project
*
*/

#include "m1_wifi_session_cache.h"
#include <string.h>

void wifi_session_cache_init(wifi_session_cache_t *c)
{
    memset(c, 0, sizeof(*c));
}

void wifi_session_cache_publish(wifi_session_cache_t *c,
                                const wifi_session_ap_t *aps,
                                uint16_t count,
                                uint32_t now_ms)
{
    uint16_t n = count;

    if (n > WIFI_SESSION_CACHE_MAX_APS)
    {
        n = WIFI_SESSION_CACHE_MAX_APS; /* clamp: never overflow storage */
    }

    if (n > 0u)
    {
        memcpy(c->aps, aps, (size_t)n * sizeof(c->aps[0]));
    }
    c->count = n;

    /* Publish order matters for atomicity: entries are fully copied in above
     * BEFORE valid/last_scan_ts are updated, so any reader that only looks
     * at valid/last_scan_ts to decide "do I trust this" never observes a
     * true flag paired with a stale or partial aps[] array. */
    c->valid = true;
    c->last_scan_ts = now_ms;
}

void wifi_session_cache_invalidate(wifi_session_cache_t *c)
{
    c->valid = false;
}

bool wifi_session_cache_is_fresh(const wifi_session_cache_t *c,
                                 uint32_t now_ms,
                                 uint32_t ttl_ms)
{
    uint32_t age;

    if (!c->valid)
    {
        return false;
    }
    age = now_ms - c->last_scan_ts; /* unsigned: wraparound-safe */
    return age < ttl_ms;
}

uint32_t wifi_session_cache_age_ms(const wifi_session_cache_t *c, uint32_t now_ms)
{
    if (!c->valid)
    {
        return 0u;
    }
    return now_ms - c->last_scan_ts; /* unsigned: wraparound-safe */
}

uint16_t wifi_session_cache_snapshot(const wifi_session_cache_t *c,
                                     wifi_session_ap_t *out,
                                     uint16_t out_cap)
{
    uint16_t n;

    if (!c->valid || out_cap == 0u)
    {
        return 0u;
    }

    n = c->count;
    if (n > out_cap)
    {
        n = out_cap;
    }
    if (n > 0u)
    {
        memcpy(out, c->aps, (size_t)n * sizeof(out[0]));
    }
    return n;
}

void wifi_session_selected_set(wifi_session_selected_t *sel, const wifi_session_ap_t *ap)
{
    sel->ap = *ap;
    sel->valid = true;
}

void wifi_session_selected_clear(wifi_session_selected_t *sel)
{
    memset(sel, 0, sizeof(*sel));
}

bool wifi_session_selected_get(const wifi_session_selected_t *sel, wifi_session_ap_t *out)
{
    if (!sel->valid)
    {
        return false;
    }
    *out = sel->ap;
    return true;
}

/* Zero-initialised by static storage: empty/invalid, same as an explicit
 * wifi_session_cache_init() call -- g_inited exists only so the first
 * wifi_session_cache_get() call still runs init() once, for callers that
 * rely on init()'s documented behavior (e.g. future fields it might zero
 * beyond what static zero-init already covers) rather than on this
 * translation unit's specific storage duration. */
static wifi_session_cache_t g_wifi_session_cache;
static bool g_wifi_session_cache_inited = false;

wifi_session_cache_t *wifi_session_cache_get(void)
{
    if (!g_wifi_session_cache_inited)
    {
        wifi_session_cache_init(&g_wifi_session_cache);
        g_wifi_session_cache_inited = true;
    }
    return &g_wifi_session_cache;
}

void wifi_session_cache_get_reset(void)
{
    wifi_session_cache_init(&g_wifi_session_cache);
    g_wifi_session_cache_inited = true;
}
