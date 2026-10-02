/*
 * mfc_capture_test.c - host tests for the Extract Keys authentication-
 * capture store (mfc_capture.c), covering the Phase 3 hardening: session-
 * generation invalidation on field-loss/STOP/timeout, preserving already-
 * completed pairs while making in-flight ones permanently uncompletable,
 * plus regression coverage for the pre-existing duplicate/overflow/
 * pairing rules.
 *
 * Build & run:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/mfc_capture.c \
 *      NFC/NFC_drv/common/test/mfc_capture_test.c \
 *      -o /tmp/mfc_capture_test && /tmp/mfc_capture_test
 */
#include "mfc_capture.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

static mfc_auth_ctx_t make_ctx(uint32_t cuid, uint8_t sector, mfc_key_type_t kt,
                               uint32_t nt, uint32_t nr, uint32_t ar, uint32_t now_ms)
{
    mfc_auth_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.cuid = cuid;
    c.block = (uint8_t)(sector * 4U);
    c.sector = sector;
    c.key_type = kt;
    c.nt = nt;
    c.nr = nr;
    c.ar = ar;
    c.now_ms = now_ms;
    return c;
}

int main(void)
{
    printf("=== mfc_capture (Extract Keys capture store) host tests ===\n");

    const uint32_t CUID = 0x01020304u;

    /* -------- basic pairing (regression: pre-existing behavior) -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a = make_ctx(CUID, 3, MFC_KEY_A, 0x111u, 0x222u, 0x333u, 1000U);
        mfc_auth_ctx_t b = make_ctx(CUID, 3, MFC_KEY_A, 0x444u, 0x555u, 0x666u, 1050U);

        CHECK(mfc_capture_add(&a) == MFC_CAP_ADDED_PARTIAL, "first attempt opens a partial record");
        CHECK(mfc_capture_add(&b) == MFC_CAP_COMPLETED_PAIR, "second matching attempt completes the pair");
        CHECK(mfc_capture_pair_count() == 1U, "pair_count is 1");
        mfc_pair_t p;
        CHECK(mfc_capture_get_pair(0, &p), "completed pair is readable");
        CHECK(p.is_filled && p.nt0 == 0x111u && p.nt1 == 0x444u, "pair contents match both attempts exactly");
    }

    /* -------- mismatched sector never pairs -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a = make_ctx(CUID, 1, MFC_KEY_A, 0x111u, 0x222u, 0x333u, 1000U);
        mfc_auth_ctx_t b = make_ctx(CUID, 2, MFC_KEY_A, 0x444u, 0x555u, 0x666u, 1050U);
        CHECK(mfc_capture_add(&a) == MFC_CAP_ADDED_PARTIAL, "sector 1 opens a partial");
        CHECK(mfc_capture_add(&b) == MFC_CAP_ADDED_PARTIAL, "sector 2 opens its OWN partial, not a completion");
        CHECK(mfc_capture_pair_count() == 0U, "mismatched sector: zero completed pairs");
    }

    /* -------- mismatched key type never pairs -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a = make_ctx(CUID, 5, MFC_KEY_A, 0x111u, 0x222u, 0x333u, 1000U);
        mfc_auth_ctx_t b = make_ctx(CUID, 5, MFC_KEY_B, 0x444u, 0x555u, 0x666u, 1050U);
        CHECK(mfc_capture_add(&a) == MFC_CAP_ADDED_PARTIAL, "Key A opens a partial");
        CHECK(mfc_capture_add(&b) == MFC_CAP_ADDED_PARTIAL, "Key B opens its OWN partial, not a completion");
        CHECK(mfc_capture_pair_count() == 0U, "mismatched key type: zero completed pairs");
    }

    /* -------- mismatched UID (cuid) rejected outright -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t wrong = make_ctx(CUID ^ 1U, 3, MFC_KEY_A, 0x111u, 0x222u, 0x333u, 1000U);
        CHECK(mfc_capture_add(&wrong) == MFC_CAP_INVALID, "a context for a different cuid is rejected outright");
    }

    /* -------- exact duplicate rejected -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a = make_ctx(CUID, 3, MFC_KEY_A, 0x111u, 0x222u, 0x333u, 1000U);
        CHECK(mfc_capture_add(&a) == MFC_CAP_ADDED_PARTIAL, "first attempt accepted");
        CHECK(mfc_capture_add(&a) == MFC_CAP_DUPLICATE, "byte-identical repeat is rejected as duplicate, not a second partial");
        CHECK(mfc_capture_nonce_count() == 1U, "duplicate does not increment the nonce counter");
    }

    /* -------- overflow rejected cleanly, store stays bounded -------- */
    {
        mfc_capture_reset(CUID);
        mfc_capture_result_t last = MFC_CAP_INVALID;
        for (uint32_t s = 0U; s < MFC_CAPTURE_MAX_RECORDS + 4U; s++) {
            mfc_auth_ctx_t a = make_ctx(CUID, (uint8_t)(s % 40U), MFC_KEY_A,
                                        0x1000u + s, 0x2000u + s, 0x3000u + s, 1000U + s);
            last = mfc_capture_add(&a);
        }
        CHECK(last == MFC_CAP_FULL, "capacity exceeded: store rejects cleanly with MFC_CAP_FULL, not a crash/overrun");
    }

    /* -------- field-loss-equivalent invalidation preserves completed pairs -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t done_a = make_ctx(CUID, 1, MFC_KEY_A, 0x1111u, 0x2222u, 0x3333u, 1000U);
        mfc_auth_ctx_t done_b = make_ctx(CUID, 1, MFC_KEY_A, 0x4444u, 0x5555u, 0x6666u, 1010U);
        CHECK(mfc_capture_add(&done_a) == MFC_CAP_ADDED_PARTIAL, "sector 1 first attempt");
        CHECK(mfc_capture_add(&done_b) == MFC_CAP_COMPLETED_PAIR, "sector 1 pair completes before any field loss");

        mfc_auth_ctx_t partial = make_ctx(CUID, 2, MFC_KEY_A, 0x7777u, 0x8888u, 0x9999u, 1020U);
        CHECK(mfc_capture_add(&partial) == MFC_CAP_ADDED_PARTIAL, "sector 2 opens a partial (in-flight when the reader goes away)");

        mfc_capture_invalidate_incomplete();   /* simulates mfc_detect_on_field_lost() */

        mfc_auth_ctx_t late = make_ctx(CUID, 2, MFC_KEY_A, 0xAAAAu, 0xBBBBu, 0xCCCCu, 5000U);
        mfc_capture_result_t r = mfc_capture_add(&late);
        CHECK(r == MFC_CAP_ADDED_PARTIAL,
              "a later attempt for the SAME sector/keytype opens a FRESH partial, never completes the stale one (returns ADDED_PARTIAL, not COMPLETED_PAIR)");
        CHECK(mfc_capture_pair_count() == 1U,
              "the sector-1 pair completed BEFORE invalidation is still intact -- pair_count did not drop");
        mfc_pair_t p;
        CHECK(mfc_capture_get_pair(0, &p) && p.nt0 == 0x1111u && p.nt1 == 0x4444u,
              "the preserved pair's contents are exactly the pre-invalidation capture, untouched");
    }

    /* -------- timeout invalidation: same guarantee, time-driven instead of an explicit event -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t partial = make_ctx(CUID, 4, MFC_KEY_B, 0x1u, 0x2u, 0x3u, 1000U);
        CHECK(mfc_capture_add(&partial) == MFC_CAP_ADDED_PARTIAL, "partial opened at t=1000");

        CHECK(!mfc_capture_check_timeout(1000U + 29000U, 30000U),
              "just under the timeout: nothing invalidated yet");
        mfc_auth_ctx_t still_ok = make_ctx(CUID, 4, MFC_KEY_B, 0x4u, 0x5u, 0x6u, 30000U);
        CHECK(mfc_capture_add(&still_ok) == MFC_CAP_COMPLETED_PAIR,
              "still within the timeout window: the pair legitimately completes");

        mfc_capture_reset(CUID);
        mfc_auth_ctx_t partial2 = make_ctx(CUID, 4, MFC_KEY_B, 0x7u, 0x8u, 0x9u, 1000U);
        CHECK(mfc_capture_add(&partial2) == MFC_CAP_ADDED_PARTIAL, "partial2 opened at t=1000");
        CHECK(mfc_capture_check_timeout(1000U + 30000U, 30000U),
              "at/past the timeout: check_timeout reports it invalidated something");
        mfc_auth_ctx_t too_late = make_ctx(CUID, 4, MFC_KEY_B, 0xAu, 0xBu, 0xCu, 31000U);
        CHECK(mfc_capture_add(&too_late) == MFC_CAP_ADDED_PARTIAL,
              "past the timeout: the stale partial cannot be completed -- this opens a FRESH one instead");
    }

    /* -------- STOP/BACK-equivalent (mfc_detect_end()'s own call) leaves no completable leftovers -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t partial = make_ctx(CUID, 7, MFC_KEY_A, 0x10u, 0x20u, 0x30u, 1000U);
        CHECK(mfc_capture_add(&partial) == MFC_CAP_ADDED_PARTIAL, "partial open when BACK/STOP fires");
        mfc_capture_invalidate_incomplete();   /* simulates mfc_detect_end() */
        mfc_auth_ctx_t next_session_same_sector = make_ctx(CUID, 7, MFC_KEY_A, 0x40u, 0x50u, 0x60u, 2000U);
        CHECK(mfc_capture_add(&next_session_same_sector) == MFC_CAP_ADDED_PARTIAL,
              "a later re-entry to the SAME sector/keytype never silently completes the pre-STOP partial");
    }

    /* -------- new session (full reset) cannot consume anything from before it -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t partial = make_ctx(CUID, 9, MFC_KEY_A, 0x11u, 0x22u, 0x33u, 1000U);
        CHECK(mfc_capture_add(&partial) == MFC_CAP_ADDED_PARTIAL, "partial from the old session");
        uint32_t gen_before = mfc_capture_session_gen();

        mfc_capture_reset(CUID);   /* brand new session, same fixed persona/cuid */
        CHECK(mfc_capture_session_gen() != gen_before, "a full reset bumps the generation too");
        CHECK(mfc_capture_pair_count() == 0U && mfc_capture_nonce_count() == 0U,
              "a full reset clears all counters -- nothing survives into the new session");
        mfc_auth_ctx_t fresh = make_ctx(CUID, 9, MFC_KEY_A, 0x44u, 0x55u, 0x66u, 3000U);
        CHECK(mfc_capture_add(&fresh) == MFC_CAP_ADDED_PARTIAL,
              "the new session's own first attempt opens a fresh partial, not a completion of the old one");
    }

    printf("=== mfc_capture: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
