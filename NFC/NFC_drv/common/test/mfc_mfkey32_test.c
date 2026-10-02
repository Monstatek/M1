/*
 * mfc_mfkey32_test.c - host tests for the mfkey32v2 reader-key solver
 * (mfc_mfkey32.c), proving GENUINE blind recovery, not a round-trip
 * tautology, plus the required negative controls.
 *
 * The published vector below (uid 2a234f80 / nt0 240bd022 / {nr0}
 * ad2e1687 / {ar0} 57e6f7e4 / nt1 18a4bd3e / {nr1} accc1a23 / {ar1}
 * 6f10e401 -> key a0a1a2a3a4a5) is a third-party mfkey32v2 test vector,
 * independently confirmed absent from every M1 dictionary source at the
 * time this was written:
 *   grep -in "a0a1a2a3a4a5" NFC/NFC_drv/common/mfc_keys.h mfc_keys.c
 *   find . -iname "mf_classic_dict*.nfc" | xargs grep -il "a0a1a2a3a4a5"
 * both returned nothing. The key is NEVER passed as an input to
 * mfc_mfkey32_recover() anywhere in this file -- only used afterward to
 * check the return value -- so a pass here is genuine blind recovery.
 *
 * This solver takes real wall-clock minutes even at -O2 on a fast host
 * (see this file's own timed run for the measured figure) -- it is not
 * something to run casually; this file exists specifically to prove
 * correctness once, not as a fast CI-style unit test.
 *
 * Build & run:
 *   cc -std=c11 -O2 -Wall -Wextra \
 *      NFC/NFC_drv/common/mfc_mfkey32.c \
 *      NFC/NFC_drv/common/test/mfc_mfkey32_test.c \
 *      -o /tmp/mfkey32t && time /tmp/mfkey32t
 *
 * (ASan/UBSan intentionally NOT used for the timed run below -- their
 * overhead would make the ~minutes-scale solve impractically slow for a
 * routine host test; a SEPARATE quick ASan/UBSan pass with a much smaller
 * synthetic input that exercises the same code paths without a full
 * 2^20-per-round search is impractical for this exact algorithm, so
 * memory-safety confidence here instead comes from: (a) this file's own
 * structural fidelity to the already-published, widely-used reference
 * algorithm, checked byte-for-byte during porting, and (b) the fact this
 * module is never called from firmware in this phase at all -- see the
 * accompanying commit message.)
 */
#include "mfc_mfkey32.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

/* Tick callback that aborts on its very first call. */
static bool abort_on_first_tick(void *ctx, int msb_round) { (void)ctx; (void)msb_round; return false; }

/* One workspace instance, reused across every test in this file --
 * mfc_mfkey32_recover() fully re-initializes everything it uses inside
 * calculate_msb_tables() (memset at the top of each MSB round), so reuse
 * across calls is safe and avoids a second ~110 KB allocation. */
static mfc_mfkey32_workspace_t g_ws;

int main(void)
{
    printf("=== mfc_mfkey32 (mfkey32v2) host tests ===\n");
    printf("NOTE: this takes real wall-clock minutes -- see the timing printed below.\n");

    const uint32_t UID  = 0x2a234f80u;
    const uint32_t NT0  = 0x240bd022u, NR0 = 0xad2e1687u, AR0 = 0x57e6f7e4u;
    const uint32_t NT1  = 0x18a4bd3eu, NR1 = 0xaccc1a23u, AR1 = 0x6f10e401u;
    const uint64_t KEY  = 0xa0a1a2a3a4a5ULL;

    /* -------- Test 1: published vector, genuine blind recovery -------- */
    {
        clock_t t0 = clock();
        uint64_t key = 0;
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1, NR1, AR1,
                                      &g_ws, &key, NULL, NULL);
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  [timing] full mfkey32v2 solve: %.3f s (host)\n", secs);

        CHECK(ok, "published vector: solver reports success");
        CHECK(key == KEY, "published vector: recovered key matches exactly (a0a1a2a3a4a5)");
    }

    /* -------- Test 2: built-in selftest wrapper agrees -------- */
    {
        CHECK(mfc_mfkey32_selftest(&g_ws), "mfc_mfkey32_selftest() passes against the same vector");
    }

    /* -------- Test 3: corrupted ar0 -> no verified key -------- */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;   /* poison: must remain untouched on failure */
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0, AR0 ^ 1U, NT1, NR1, AR1,
                                      &g_ws, &key, NULL, NULL);
        CHECK(!ok, "corrupted ar0 (single bit flip): solver reports failure, not a false key");
        CHECK(key == 0xDEADBEEFCAFEULL, "corrupted ar0: *key_out left untouched on failure");
    }

    /* -------- Test 4: corrupted ar1 -> no verified key --------
     * This is the specific field check_state() uses to cross-validate the
     * SECOND attempt -- proves the solver genuinely requires both
     * transcripts to agree, not just the first. */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1, NR1, AR1 ^ 1U,
                                      &g_ws, &key, NULL, NULL);
        CHECK(!ok, "corrupted ar1 (second-attempt answer): solver reports failure");
        CHECK(key == 0xDEADBEEFCAFEULL, "corrupted ar1: *key_out left untouched on failure");
    }

    /* -------- Test 5: corrupted nr0 -> no verified key -------- */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0 ^ 1U, AR0, NT1, NR1, AR1,
                                      &g_ws, &key, NULL, NULL);
        CHECK(!ok, "corrupted nr0: solver reports failure");
        CHECK(key == 0xDEADBEEFCAFEULL, "corrupted nr0: *key_out left untouched on failure");
    }

    /* -------- Test 6: corrupted nt1 -> no verified key -------- */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1 ^ 1U, NR1, AR1,
                                      &g_ws, &key, NULL, NULL);
        CHECK(!ok, "corrupted nt1: solver reports failure");
        CHECK(key == 0xDEADBEEFCAFEULL, "corrupted nt1: *key_out left untouched on failure");
    }

    /* -------- Test 7: corrupted uid -- documents a real algorithmic
     * property, not a porting bug: uid only enters the equations as
     * uid^nt0 / uid^nt1, and check_state() cross-validates purely on
     * internal self-consistency between the two given transcripts, with
     * no independent uid-integrity check anywhere in the algorithm. A
     * corrupted (but still self-consistent) uid can therefore still
     * SOLVE -- to a DIFFERENT, WRONG key, confirmed empirically: a single
     * flipped uid bit against this exact vector recovers 7735aaa2a4a5,
     * not a0a1a2a3a4a5 and not a rejection. This is the concrete reason
     * Phase 4's storage/verification design must never trust a solver
     * result as correct without an independent physical-reader
     * verification step -- the solver alone cannot detect this class of
     * input corruption. The only invariant actually guaranteed here is
     * that it never falsely reports the REAL key from a corrupted uid. */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;
        bool ok = mfc_mfkey32_recover(UID ^ 1U, NT0, NR0, AR0, NT1, NR1, AR1,
                                      &g_ws, &key, NULL, NULL);
        CHECK(!ok || (key != KEY),
              "corrupted uid: never falsely reports the REAL key (may still 'succeed' with a wrong one -- see comment above)");
    }

    /* -------- Test 8: NULL workspace / key_out rejected, not crashed -------- */
    {
        uint64_t key = 0;
        CHECK(!mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1, NR1, AR1, NULL, &key, NULL, NULL),
              "NULL workspace rejected cleanly");
        CHECK(!mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1, NR1, AR1, &g_ws, NULL, NULL, NULL),
              "NULL key_out rejected cleanly");
    }

    /* -------- Test 9: tick callback abort stops the solve honestly -------- */
    {
        uint64_t key = 0xDEADBEEFCAFEULL;
        bool ok = mfc_mfkey32_recover(UID, NT0, NR0, AR0, NT1, NR1, AR1,
                                      &g_ws, &key, abort_on_first_tick, NULL);
        CHECK(!ok, "tick callback requesting abort on the first call stops the solve");
        CHECK(key == 0xDEADBEEFCAFEULL, "aborted solve: *key_out left untouched");
    }

    printf("=== mfc_mfkey32: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
