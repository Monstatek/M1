/*============================================================================*/
/**
 * @file    mfc_capture.c
 * @brief   MIFARE Classic Detect Reader authentication-capture store (Phase 1A).
 *
 * Fixed-size, RAM-only, no heap. Pairs two authentication contexts for the
 * same {cuid, sector, key_type} into one mfkey32 parameter set. NO key
 * recovery here. See mfc_capture.h for the model and the read-only API that a
 * future recovery stage consumes.
 */
/*============================================================================*/
#include "mfc_capture.h"
#include <string.h>

/* ---- Session state (bounded, static) -------------------------------------- */
static mfc_pair_t     s_rec[MFC_CAPTURE_MAX_RECORDS];
static uint32_t       s_rec_count;      /* records ever created this session   */
static uint32_t       s_nonce_count;    /* accepted attempts (partials+pairs)  */
static uint32_t       s_pair_count;     /* completed (is_filled) records       */
static uint32_t       s_session_cuid;
static uint32_t       s_gen;            /* current session generation          */
static bool           s_has_last;
static uint8_t        s_last_sector;
static mfc_key_type_t s_last_key_type;

/* ---- Lifecycle ------------------------------------------------------------ */
void mfc_capture_reset(uint32_t cuid)
{
    (void)memset(s_rec, 0, sizeof(s_rec));
    s_rec_count     = 0U;
    s_nonce_count   = 0U;
    s_pair_count    = 0U;
    s_session_cuid  = cuid;
    s_gen++;                    /* never resolves to a stale record's gen */
    s_has_last      = false;
    s_last_sector   = 0U;
    s_last_key_type = MFC_KEY_A;
}

void mfc_capture_invalidate_incomplete(void)
{
    /* Bumping the generation alone is sufficient: mfc_capture_add()'s
     * pairing search only considers a record eligible to complete when
     * (!is_filled && r->gen == s_gen) -- see below. Every currently-open
     * partial record's stored gen is now stale by construction, so none of
     * them can ever be completed again. Already-completed pairs (is_filled
     * == true) are never gen-checked when READ back
     * (mfc_capture_get_pair() only checks is_filled), so they remain fully
     * intact and readable. No array contents are touched here at all. */
    s_gen++;
}

bool mfc_capture_check_timeout(uint32_t now_ms, uint32_t timeout_ms)
{
    bool any_stale = false;
    for (uint32_t i = 0U; i < s_rec_count; i++) {
        mfc_pair_t *r = &s_rec[i];
        if (r->is_filled || (r->gen != s_gen)) {
            continue;   /* completed, or already invalidated by an earlier event */
        }
        if ((now_ms - r->opened_ms) >= timeout_ms) {   /* unsigned wrap-safe */
            any_stale = true;
            break;
        }
    }
    if (any_stale) {
        mfc_capture_invalidate_incomplete();
    }
    return any_stale;
}

/* True if (nt,nr,ar) is a plausible captured exchange. We generate a non-zero
 * Nt for every auth, and a real reader answer never comes back with both Nr
 * and Ar zero, so those signal an incomplete/failed capture and are rejected
 * (never counted as a successful capture). */
static bool ctx_plausible(const mfc_auth_ctx_t *c)
{
    if (c->nt == 0U) {
        return false;
    }
    if ((c->nr == 0U) && (c->ar == 0U)) {
        return false;
    }
    /* 1K = 16 sectors (0..15); allow up to 4K layout defensively (0..39). */
    if (c->sector >= 40U) {
        return false;
    }
    return true;
}

/* Exact-duplicate guard: same {sector,key_type} AND identical nt/nr/ar as an
 * already-stored attempt (either slot). Nt is fresh-random per real auth, so a
 * byte-identical triple means the same frame was observed twice. */
static bool is_duplicate(const mfc_auth_ctx_t *c)
{
    for (uint32_t i = 0U; i < s_rec_count; i++) {
        const mfc_pair_t *r = &s_rec[i];
        if ((r->sector != c->sector) || (r->key_type != c->key_type)) {
            continue;
        }
        if ((r->nt0 == c->nt) && (r->nr0 == c->nr) && (r->ar0 == c->ar)) {
            return true;
        }
        if (r->is_filled &&
            (r->nt1 == c->nt) && (r->nr1 == c->nr) && (r->ar1 == c->ar)) {
            return true;
        }
    }
    return false;
}

mfc_capture_result_t mfc_capture_add(const mfc_auth_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->cuid != s_session_cuid) || !ctx_plausible(ctx)) {
        return MFC_CAP_INVALID;
    }
    if (is_duplicate(ctx)) {
        return MFC_CAP_DUPLICATE;
    }

    /* Pairing: complete the FIRST unfilled record with matching sector+keytype
     * (cuid is constant per session). Key A never pairs with Key B, and blocks
     * in different sectors never pair. Also requires r->gen == s_gen: a
     * record opened before the most recent field-loss/STOP/BACK/timeout
     * invalidation is permanently ineligible to complete, even though it is
     * still physically sitting in the array (see
     * mfc_capture_invalidate_incomplete()). */
    for (uint32_t i = 0U; i < s_rec_count; i++) {
        mfc_pair_t *r = &s_rec[i];
        if (r->is_filled || (r->gen != s_gen)) {
            continue;
        }
        if ((r->sector == ctx->sector) && (r->key_type == ctx->key_type)) {
            r->nt1 = ctx->nt;
            r->nr1 = ctx->nr;
            r->ar1 = ctx->ar;
            r->is_filled = true;
            s_pair_count++;
            s_nonce_count++;
            s_has_last = true;
            s_last_sector = ctx->sector;
            s_last_key_type = ctx->key_type;
            return MFC_CAP_COMPLETED_PAIR;
        }
    }

    /* Otherwise open a new incomplete record (nt0/nr0/ar0) if there is room. */
    if (s_rec_count >= MFC_CAPTURE_MAX_RECORDS) {
        return MFC_CAP_FULL;
    }
    {
        mfc_pair_t *r = &s_rec[s_rec_count];
        r->is_filled  = false;
        r->cuid       = ctx->cuid;
        r->sector     = ctx->sector;
        r->key_type   = ctx->key_type;
        r->nt0 = ctx->nt;
        r->nr0 = ctx->nr;
        r->ar0 = ctx->ar;
        r->gen        = s_gen;
        r->opened_ms  = ctx->now_ms;
        s_rec_count++;
        s_nonce_count++;
        s_has_last = true;
        s_last_sector = ctx->sector;
        s_last_key_type = ctx->key_type;
    }
    return MFC_CAP_ADDED_PARTIAL;
}

/* ---- Read-only status ----------------------------------------------------- */
uint32_t mfc_capture_nonce_count(void)  { return s_nonce_count; }
uint32_t mfc_capture_pair_count(void)   { return s_pair_count; }
uint32_t mfc_capture_session_cuid(void) { return s_session_cuid; }
uint32_t mfc_capture_session_gen(void)  { return s_gen; }
bool     mfc_capture_target_reached(void) { return s_nonce_count >= MFC_CAPTURE_TARGET_NONCES; }

uint32_t mfc_capture_get_completed_pairs(void) { return s_pair_count; }

bool mfc_capture_get_pair(uint32_t index, mfc_pair_t *out)
{
    uint32_t seen = 0U;
    if (out == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < s_rec_count; i++) {
        if (!s_rec[i].is_filled) {
            continue;
        }
        if (seen == index) {
            *out = s_rec[i];
            return true;
        }
        seen++;
    }
    return false;
}

bool mfc_capture_last(uint8_t *sector, mfc_key_type_t *key_type)
{
    if (!s_has_last) {
        return false;
    }
    if (sector != NULL)   { *sector = s_last_sector; }
    if (key_type != NULL) { *key_type = s_last_key_type; }
    return true;
}
