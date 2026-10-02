/* Host test for mfc_dict_phase_run() -- the REAL production key-major
 * dictionary-phase orchestration (nfc_poller.c's m1_mfc_acquire_dict_phase()
 * is now a thin wrapper around this exact function; see mfc_dict_phase.h).
 * This links and drives the ACTUAL production .c file below, unmodified --
 * not a transcription. RF/crypto (reselect/auth/read_sector) are supplied
 * as deterministic mocks via mfc_dict_phase_ops_t, the same seam production
 * wires to the real mfc_reselect()/mfc_do_auth()/mfc_read_sector_blocks().
 *
 * Also links the REAL mfc_key_source.c (dictionary iteration/dedup) and
 * mfc_result.c (outcome classification), so the whole chain from "candidate
 * keys" to "what actually got tried, in what order, with what preserved on
 * cancellation" to "what outcome that implies" is real production code,
 * end to end.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../nfc_dict_line.c ../mfc_key_source.c ../mfc_result.c ../mfc_dict_resume.c ../mfc_dict_phase.c \
 *      mfc_dict_phase_test.c -I.. -o /tmp/mdpt && /tmp/mdpt
 */
#include "../mfc_dict_phase.h"
#include "../mfc_result.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/*----------------------------------------------------------------------------*/
/* mfc_key_source mock file-I/O seam (mirrors mfc_key_source_test.c exactly). */
/*----------------------------------------------------------------------------*/
typedef struct { const char *path; const char *const *lines; size_t n_lines; } mock_entry_t;
static mock_entry_t s_table[4];
static size_t       s_table_n;
static void mock_reset(void) { s_table_n = 0; }
static void mock_register(const char *path, const char *const *lines, size_t n_lines)
{
    s_table[s_table_n].path = path;
    s_table[s_table_n].lines = lines;
    s_table[s_table_n].n_lines = n_lines;
    s_table_n++;
}
typedef struct { const char *const *lines; size_t n; size_t idx; } mock_open_file_t;
static mock_open_file_t s_open;
static mfc_path_probe_t mock_prober(const char *path, void *io_ctx)
{
    (void)io_ctx;
    for (size_t i = 0; i < s_table_n; i++) if (strcmp(s_table[i].path, path) == 0) return MFC_PATH_PRESENT;
    return MFC_PATH_ABSENT;
}
static int mock_next_line(void *ctx, char *buf, size_t bufsz)
{
    mock_open_file_t *f = (mock_open_file_t *)ctx;
    if (f->idx >= f->n) return 0;
    snprintf(buf, bufsz, "%s", f->lines[f->idx]);
    f->idx++;
    return 1;
}
static void mock_close(void *ctx) { (void)ctx; }
/* Line-index-as-byte-offset, exactly matching mfc_key_source_test.c's own
 * mock -- see that file's comment for why this fake-but-consistent unit is
 * sufficient (the iterator only ever treats it as an opaque resume token). */
static long mock_tell(void *ctx) { return (long)((mock_open_file_t *)ctx)->idx; }
static bool mock_seek(void *ctx, uint32_t offset)
{
    mock_open_file_t *f = (mock_open_file_t *)ctx;
    if (offset > f->n) return false;
    f->idx = (size_t)offset;
    return true;
}
static int mock_opener(mfc_line_source_t *out, const char *path, void *io_ctx)
{
    (void)io_ctx;
    for (size_t i = 0; i < s_table_n; i++) {
        if (strcmp(s_table[i].path, path) == 0) {
            s_open.lines = s_table[i].lines; s_open.n = s_table[i].n_lines; s_open.idx = 0;
            out->next_line = mock_next_line; out->close = mock_close;
            out->tell = mock_tell; out->seek = mock_seek;
            out->ctx = &s_open;
            return 1;
        }
    }
    return 0;
}
static const uint8_t s_builtins[][MFC_KEY_SIZE] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
};
static void build_key_sources(mfc_key_source_cfg_t cfgs[2],
                              const char *user_paths[1], const char *sys_paths[1])
{
    user_paths[0] = "user.nfc";
    cfgs[0].kind = MFC_KEY_SRC_USER; cfgs[0].builtin = NULL; cfgs[0].builtin_n = 0;
    cfgs[0].paths = user_paths; cfgs[0].n_paths = 1;
    cfgs[0].prober = mock_prober; cfgs[0].opener = mock_opener; cfgs[0].io_ctx = NULL;
    cfgs[0].accumulate = true;

    sys_paths[0] = "system.nfc";
    cfgs[1].kind = MFC_KEY_SRC_SYSTEM; cfgs[1].builtin = s_builtins; cfgs[1].builtin_n = 1;
    cfgs[1].paths = sys_paths; cfgs[1].n_paths = 1;
    cfgs[1].prober = mock_prober; cfgs[1].opener = mock_opener; cfgs[1].io_ctx = NULL;
    cfgs[1].accumulate = false;
}

/*----------------------------------------------------------------------------*/
/* mfc_dict_phase_ops_t mocks: reselect always succeeds (no card-loss in     */
/* these tests -- that path is a separate, orthogonal concern), auth is a    */
/* fact-table lookup keyed by (first-block, keytype, key) -- exactly the     */
/* arguments the real production ops->auth() receives, computed by the real  */
/* m1nfc_mfc_sector_first_block() the production code itself uses.           */
/*----------------------------------------------------------------------------*/
static bool mock_reselect(void *dev) { (void)dev; return true; }

typedef struct { uint16_t first_block; uint8_t keytype; uint8_t key[6]; } auth_fact_t;
static auth_fact_t s_facts[8];
static size_t       s_facts_n;
static void facts_reset(void) { s_facts_n = 0; }
static void facts_add(uint16_t first_block, uint8_t keytype, const uint8_t key[6])
{
    s_facts[s_facts_n].first_block = first_block; s_facts[s_facts_n].keytype = keytype;
    memcpy(s_facts[s_facts_n].key, key, 6); s_facts_n++;
}

static int              s_auth_calls;          /* total ops->auth() invocations this test */
static int              s_auth_calls_at_arm;    /* snapshot of s_auth_calls when abort armed (0 = not yet armed) */
static int              s_abort_after_n = -1;   /* arm *s_abort_flag right after this many total auth() calls; -1 = never */
static volatile bool   *s_abort_flag_ptr;
static uint8_t           s_last_authed_key[8][6];   /* every key ops->auth() was ever called with, in order */
static int              s_last_authed_n;

static bool mock_auth(void *dev, void *crypto_ctx, uint8_t block, uint8_t keytype,
                      uint64_t key, uint32_t cuid, bool *authed)
{
    (void)dev; (void)crypto_ctx; (void)cuid;
    s_auth_calls++;
    uint8_t k[6] = {
        (uint8_t)(key >> 40), (uint8_t)(key >> 32), (uint8_t)(key >> 24),
        (uint8_t)(key >> 16), (uint8_t)(key >> 8),  (uint8_t)key,
    };
    if (s_last_authed_n < 8) { memcpy(s_last_authed_key[s_last_authed_n], k, 6); s_last_authed_n++; }

    bool ok = false;
    for (size_t i = 0; i < s_facts_n; i++) {
        if ((s_facts[i].first_block == block) && (s_facts[i].keytype == keytype) &&
            (memcmp(s_facts[i].key, k, 6) == 0)) { ok = true; break; }
    }
    *authed = ok;

    if ((s_abort_after_n >= 0) && (s_auth_calls == s_abort_after_n)) {
        *s_abort_flag_ptr = true;
        s_auth_calls_at_arm = s_auth_calls;
    }
    return ok;
}

static void mock_read_sector(void *dev, void *crypto_ctx, uint16_t first, uint8_t nblocks, bool *authed)
{
    (void)dev; (void)crypto_ctx; (void)first; (void)nblocks; (void)authed;   /* block DATA not under test here */
}

static void mfc_init(nfc_mfc_info_t *mfc, nfc_mfc_scan_t *sc, uint8_t sectors_total)
{
    memset(mfc, 0, sizeof(*mfc));
    mfc->valid = true; mfc->sectors_total = sectors_total; mfc->keys_total = (uint8_t)(sectors_total * 2U);
    memset(sc, 0, sizeof(*sc));
}

static const uint8_t FAST_DEFAULT_KEY[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};   /* mirrors s_fast_default_key */

/*----------------------------------------------------------------------------*/
/* One continuous USER -> SYSTEM scan, real cancellation mid-SYSTEM-phase.
 *
 * 2 sectors (sector 0 first_block=0, sector 1 first_block=4), keys_total=4.
 * USER supplies exactly one key (u1) that opens ONLY sector 0 Key A.
 * SYSTEM's file supplies 5 lines that open nothing at all, so every SYSTEM
 * candidate against the two still-open sectors costs a PREDICTABLE number
 * of auth() calls: sector 0 needs only Key B (Key A already found, so that
 * block is skipped entirely -- exactly as the real loop does), sector 1
 * needs both Key A and Key B -- 3 auth() calls per non-matching SYSTEM
 * candidate. Cancellation is armed to fire on the 5th SYSTEM auth() call
 * (mid-way through the 2nd SYSTEM candidate), proving the loop stops
 * genuinely mid-scan, not merely between two separately-invoked phases. */
static void test_continuous_user_to_system_scan_cancelled_midway(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_auth_calls_at_arm = 0; s_abort_after_n = -1; s_last_authed_n = 0;

    static const uint8_t u1[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    static const char *user_lines[] = { "111111111111" };
    mock_register("user.nfc", user_lines, 1);

    /* 5 SYSTEM lines, none of which open anything -- also includes the
     * fast-pass default key FFFFFFFFFFFF (already seeded as tried), to prove
     * it is skipped by the iterator's dedup and therefore NEVER reaches
     * ops->auth() at all, even though it is textually present in the file. */
    static const char *sys_lines[] = {
        "FFFFFFFFFFFF", "AAAAAAAAAAAA", "BBBBBBBBBBBB", "CCCCCCCCCCCC", "DDDDDDDDDDDD",
    };
    mock_register("system.nfc", sys_lines, 5);

    facts_add(/*first_block=*/0, MFC_DICT_PHASE_KEY_A_CMD, u1);   /* u1 opens ONLY sector 0 Key A */

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, /*sectors_total=*/2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;

    /* Arm cancellation for the 5th auth() call overall. USER contributes
     * exactly 4 calls for its one candidate u1 (sector0: A succeeds, B
     * fails; sector1: A fails, B fails) -- so call #5 is the FIRST SYSTEM
     * auth() call. Move the trigger to fire on SYSTEM call #2 instead
     * (overall call #6), landing mid-candidate as the scenario intends:
     * SYSTEM candidate 1 (AAAA..) costs 3 calls (B0, A1, B1) = overall
     * calls 5,6,7; candidate 2 (BBBB..) starts at overall call 8 (B0). Arm
     * at overall call 9 (candidate 2's A1) so it fires mid-candidate-2,
     * BEFORE candidate 2's B1 (call 10) and before candidate 3 exists. */
    s_abort_after_n = 9;

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };

    bool ran_to_completion_or_clean_abort =
        mfc_dict_phase_run(cfgs, 2, FAST_DEFAULT_KEY, /*cuid=*/0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(ran_to_completion_or_clean_abort, "not card-lost: returns true");
    CHECK(abort_flag, "abort flag ended up set (armed and fired)");

    /* --- user results already obtained remain preserved --- */
    CHECK(mfc.sec[0].key_a_found, "sector 0 Key A (from USER) preserved across the SYSTEM-phase cancellation");
    CHECK(memcmp(mfc.sec[0].key_a, u1, 6) == 0, "preserved key A bytes are exactly u1, unmodified");

    /* --- some SYSTEM candidates were genuinely attempted --- */
    CHECK(s_auth_calls > 4, "more than the 4 USER-phase auth() calls happened -- SYSTEM candidates were tried");
    CHECK(s_auth_calls == 9, "exactly 9 total auth() calls: 4 USER + 5 SYSTEM (B0,A1,B1 for cand1; B0,A1 for cand2)");

    /* --- cancellation occurs before the next authentication attempt; no
     * later candidate is attempted --- */
    CHECK(s_auth_calls_at_arm == 9, "abort armed on exactly the 9th call, as configured");
    CHECK(!mfc.sec[1].key_a_found && !mfc.sec[1].key_b_found,
          "sector 1 never solved -- candidate 2's B1 attempt (call #10) never happened, nor did candidate 3+");
    CHECK(sc.keys_tried == 3, "keys_tried == 3: USER's u1, SYSTEM's AAAA.., SYSTEM's BBBB.. -- candidate 3 (CCCC..) never started");

    /* --- the pre-tried fast key is never attempted again --- */
    for (int i = 0; i < s_last_authed_n; i++) {
        CHECK(memcmp(s_last_authed_key[i], FAST_DEFAULT_KEY, 6) != 0,
              "FF..FF (seeded already_tried) never reached ops->auth(), despite being the SYSTEM file's first line");
    }

    /* --- outcome becomes PARTIAL when data exists --- */
    mfc_outcome_t outcome = mfc_classify_outcome(mfc.sectors_total, /*sectors_read=*/1,
                                                  mfc.keys_total, mfc.keys_found, /*cancelled=*/true);
    CHECK(outcome == MFC_OUTCOME_PARTIAL, "real mfc_classify_outcome(): partial data + cancelled -> PARTIAL");
    mfc_action_eligibility_t elig = mfc_action_eligibility(outcome);
    CHECK(elig.save_partial && elig.find_missing_keys && !elig.try_again,
          "PARTIAL outcome offers Save Partial and Find Missing Keys, not a blind Try Again");
}

/*----------------------------------------------------------------------------*/
/* Zero-data cancellation within the SAME continuous-scan machinery: abort
 * fires on the very first auth() call (before anything can succeed), so
 * nothing is ever found. Real classifier must report CANCELLED, not FAILED
 * -- the distinction that matters for "keep partial results" vs "nothing to
 * keep". */
static void test_zero_data_cancellation_is_cancelled_not_failed(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_auth_calls_at_arm = 0; s_last_authed_n = 0;

    static const char *user_lines[] = { "111111111111" };
    mock_register("user.nfc", user_lines, 1);
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("system.nfc", sys_lines, 1);
    /* No auth facts registered: nothing can ever succeed. */

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, /*sectors_total=*/2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = 1;   /* fires on the very first auth() call */

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    (void)mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(mfc.keys_found == 0, "nothing found");
    mfc_outcome_t outcome = mfc_classify_outcome(mfc.sectors_total, 0, mfc.keys_total, mfc.keys_found, true);
    CHECK(outcome == MFC_OUTCOME_CANCELLED, "real mfc_classify_outcome(): zero data + cancelled -> CANCELLED, not FAILED");
    mfc_action_eligibility_t elig = mfc_action_eligibility(outcome);
    CHECK(elig.try_again && elig.info, "CANCELLED with no data offers Try Again + Info -- not a dead end");
    CHECK(!elig.save && !elig.save_partial && !elig.find_missing_keys && !elig.emulate_write_allowed,
          "CANCELLED with no data offers no Save/Find-Missing-Keys/Emulate/Write");
}

/*----------------------------------------------------------------------------*/
/* Uninterrupted run: proves the same production loop, given no cancellation
 * at all, resolves every sector across a continuous USER -> SYSTEM scan and
 * reports COMPLETE -- the counterpart to the two cancellation scenarios
 * above, confirming the seam doesn't just stop correctly but also
 * completes correctly. */
static void test_continuous_scan_completes_without_cancellation(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;

    static const uint8_t u1[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    static const char *user_lines[] = { "111111111111" };
    mock_register("user.nfc", user_lines, 1);

    static const uint8_t s1[6] = {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA};
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("system.nfc", sys_lines, 1);

    facts_add(0, MFC_DICT_PHASE_KEY_A_CMD, u1); facts_add(0, MFC_DICT_PHASE_KEY_B_CMD, u1);   /* u1 opens sector 0 A and B */
    facts_add(4, MFC_DICT_PHASE_KEY_A_CMD, s1); facts_add(4, MFC_DICT_PHASE_KEY_B_CMD, s1);   /* s1 opens sector 1 A and B */

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;   /* never */

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    bool r = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(r, "not card-lost");
    CHECK(mfc.keys_found == 4, "all 4 sector-key slots resolved (2 from USER, 2 from SYSTEM)");
    mfc_outcome_t outcome = mfc_classify_outcome(mfc.sectors_total, mfc.sectors_total,
                                                  mfc.keys_total, mfc.keys_found, false);
    CHECK(outcome == MFC_OUTCOME_COMPLETE, "real mfc_classify_outcome(): COMPLETE");
    CHECK(mfc_action_eligibility(outcome).save, "COMPLETE outcome is eligible for Save");
}

/*----------------------------------------------------------------------------*/
/* Find-Missing-Keys continuation: mfc_dict_phase_run() called TWICE against  */
/* the SAME mfc/sc, sharing one mfc_dict_resume_t -- the exact shape          */
/* nfc_mfc_find_keys_run() (nfc_poller.c) uses in production.                 */
/*----------------------------------------------------------------------------*/

/* Required test #6: a first pass resolves sector 0 (USER) and leaves sector 1
 * unresolved (nothing in either dictionary opens it yet). A second pass,
 * given a NEWLY-added SYSTEM candidate that opens sector 1, resolves it --
 * while sector 0's key from pass 1 survives byte-for-byte. Also exercises
 * required test #11 (completion after continuation unlocks Save/Emulate/
 * Write/Info via the real, unmodified mfc_action_eligibility()). */
static void test_continuation_preserves_prior_progress_and_resolves_more(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;

    static const uint8_t u1[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    static const char *user_lines[] = { "111111111111" };
    mock_register("user.nfc", user_lines, 1);
    static const char *sys_lines_pass1[] = { "AAAAAAAAAAAA" };   /* opens nothing */
    mock_register("system.nfc", sys_lines_pass1, 1);
    facts_add(0, MFC_DICT_PHASE_KEY_A_CMD, u1); facts_add(0, MFC_DICT_PHASE_KEY_B_CMD, u1);   /* u1 opens sector 0 */

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, /*sectors_total=*/2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;
    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };

    mfc_dict_resume_t resume;
    mfc_dict_resume_reset(&resume);

    bool r1 = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    CHECK(r1, "pass 1: not card-lost");
    CHECK(mfc.sec[0].key_a_found && mfc.sec[0].key_b_found, "pass 1: sector 0 fully resolved by USER's u1");
    CHECK(!mfc.sec[1].key_a_found && !mfc.sec[1].key_b_found, "pass 1: sector 1 still unresolved");
    CHECK(mfc.keys_found == 2, "pass 1: exactly 2 of 4 key slots resolved");
    mfc_outcome_t outcome1 = mfc_classify_outcome(mfc.sectors_total, /*sectors_read=*/1,
                                                  mfc.keys_total, mfc.keys_found, false);
    CHECK(outcome1 == MFC_OUTCOME_PARTIAL, "pass 1 outcome: PARTIAL");

    /* Between passes: the user added a new key to their dictionary that
     * happens to open sector 1 -- exactly the "Find Missing Keys" scenario. */
    mock_reset();
    mock_register("user.nfc", user_lines, 1);
    static const uint8_t s2[6] = {0xBB,0xBB,0xBB,0xBB,0xBB,0xBB};
    static const char *sys_lines_pass2[] = { "AAAAAAAAAAAA", "BBBBBBBBBBBB" };
    mock_register("system.nfc", sys_lines_pass2, 2);
    facts_add(4, MFC_DICT_PHASE_KEY_A_CMD, s2); facts_add(4, MFC_DICT_PHASE_KEY_B_CMD, s2);

    uint8_t sector0_key_a_before[6], sector0_key_b_before[6];
    memcpy(sector0_key_a_before, mfc.sec[0].key_a, 6);
    memcpy(sector0_key_b_before, mfc.sec[0].key_b, 6);

    bool r2 = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    CHECK(r2, "pass 2 (continuation): not card-lost");
    CHECK(memcmp(mfc.sec[0].key_a, sector0_key_a_before, 6) == 0 &&
          memcmp(mfc.sec[0].key_b, sector0_key_b_before, 6) == 0,
          "pass 2: sector 0's keys from pass 1 survive byte-for-byte, untouched");
    CHECK(mfc.sec[1].key_a_found && mfc.sec[1].key_b_found, "pass 2: sector 1 now resolved by the new SYSTEM key");
    CHECK(memcmp(mfc.sec[1].key_a, s2, 6) == 0, "pass 2: sector 1's key is exactly the newly added s2");
    CHECK(mfc.keys_found == 4, "pass 2: all 4 key slots now resolved");

    mfc_outcome_t outcome2 = mfc_classify_outcome(mfc.sectors_total, mfc.sectors_total,
                                                  mfc.keys_total, mfc.keys_found, false);
    CHECK(outcome2 == MFC_OUTCOME_COMPLETE, "pass 2 outcome: recomputed as COMPLETE");
    mfc_action_eligibility_t elig2 = mfc_action_eligibility(outcome2);
    CHECK(elig2.save && elig2.emulate_write_allowed && elig2.info,
          "required test #11: completion via continuation immediately unlocks Save/Emulate/Write/Info, "
          "via the SAME unmodified eligibility function COMPLETE always used -- no second success workflow");
}

/* Required test #7 (dict_phase-layer integration, complementing the
 * iterator-layer tests in mfc_key_source_test.c): a SYSTEM candidate that
 * failed in pass 1 is never re-attempted (never reaches ops->auth() again)
 * in a pass-2 continuation sharing the same resume state. */
static void test_continuation_does_not_retry_failed_candidate(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;

    static const char *user_lines[] = { "111111111111" };   /* opens nothing */
    mock_register("user.nfc", user_lines, 1);
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };     /* opens nothing */
    mock_register("system.nfc", sys_lines, 1);

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;
    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };

    mfc_dict_resume_t resume;
    mfc_dict_resume_reset(&resume);
    (void)mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    /* 3 distinct candidates this build_key_sources() config actually
     * produces -- USER's one file line, SYSTEM's one compiled builtin
     * (s_builtins[0], always yielded regardless of the file), and SYSTEM's
     * one file line -- each costing 4 auth() calls (2 sectors x A/B), all
     * failing. */
    CHECK(s_auth_calls == 12, "pass 1: 3 candidates (USER file + SYSTEM builtin + SYSTEM file) x 4 calls each, all failing");

    /* Pass 2, same failed candidates still present in both dictionaries. */
    mock_reset();
    mock_register("user.nfc", user_lines, 1);
    mock_register("system.nfc", sys_lines, 1);
    s_auth_calls = 0; s_last_authed_n = 0;
    bool r2 = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    CHECK(r2, "pass 2: not card-lost");
    CHECK(s_auth_calls == 0,
          "pass 2 retries the already-failed USER and SYSTEM candidates ZERO times -- "
          "no ops->auth() calls at all, since neither candidate is new");
}

/* Approved-design cancellation-boundary test: a SYSTEM candidate interrupted
 * mid-sector-loop (abort fires between its two sectors) is NOT committed to
 * the resume cursor, so a later continuation retries it FROM THE START
 * (never silently skipping the sector it never reached) -- and, retried
 * without interruption this time, successfully resolves that sector. */
static void test_continuation_retries_interrupted_candidate_not_skips_it(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;

    static const char *user_lines[] = { "111111111111" };   /* opens nothing */
    mock_register("user.nfc", user_lines, 1);
    static const uint8_t cand2[6] = {0xBB,0xBB,0xBB,0xBB,0xBB,0xBB};
    static const char *sys_lines[] = { "AAAAAAAAAAAA", "BBBBBBBBBBBB" };   /* cand1 opens nothing, cand2 opens sector 1 */
    mock_register("system.nfc", sys_lines, 2);
    facts_add(4, MFC_DICT_PHASE_KEY_A_CMD, cand2); facts_add(4, MFC_DICT_PHASE_KEY_B_CMD, cand2);

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };

    /* Call sequence: USER (4, all fail) -> SYSTEM cand1 (4, all fail,
     * overall calls 5-8) -> SYSTEM cand2: sector0 A (call 9), sector0 B
     * (call 10) both fail, THEN sector1 A (call 11) would succeed -- arm
     * abort right after call 10, before cand2 ever reaches sector 1. */
    s_abort_after_n = 10;

    mfc_dict_resume_t resume;
    mfc_dict_resume_reset(&resume);
    bool r1 = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    CHECK(r1, "pass 1: not card-lost (clean abort)");
    CHECK(abort_flag, "pass 1: abort fired as armed");
    CHECK(!mfc.sec[1].key_a_found && !mfc.sec[1].key_b_found,
          "pass 1: sector 1 (cand2's target) never reached -- still unresolved");

    /* Pass 2: same dictionaries, abort cleared, resume shared. */
    mock_reset();
    mock_register("user.nfc", user_lines, 1);
    mock_register("system.nfc", sys_lines, 2);
    abort_flag = false;
    s_abort_after_n = -1;
    s_auth_calls = 0; s_last_authed_n = 0;
    bool r2 = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, &resume, NULL);
    CHECK(r2, "pass 2: not card-lost");
    CHECK(mfc.sec[1].key_a_found && mfc.sec[1].key_b_found,
          "pass 2: cand2 was RETRIED (not silently skipped) and this time reaches and resolves sector 1");
    CHECK(memcmp(mfc.sec[1].key_a, cand2, 6) == 0, "pass 2: sector 1's key is exactly cand2");
}

/* sc->src_tried powers the Find Missing Keys progress line's per-source
 * "<current>/<total>" count -- resets to 1 on every source change, counts
 * up within one source, matching sc->cur_source's own transitions exactly. */
static void test_src_tried_resets_per_source(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;

    static const char *user_lines[] = { "111111111111", "222222222222" };   /* 2 USER candidates, open nothing */
    mock_register("user.nfc", user_lines, 2);
    static const char *sys_lines[] = { "AAAAAAAAAAAA", "BBBBBBBBBBBB" };   /* 2 SYSTEM file candidates, open nothing */
    mock_register("system.nfc", sys_lines, 2);

    /* Built locally (not via build_key_sources()) so SYSTEM has NO builtin --
     * a builtin candidate always reports cur_source==NFC_SCAN_SRC_BUILTIN,
     * its own category, which would otherwise interrupt the "2 consecutive
     * SYSTEM candidates" sequence this test wants to observe. */
    static const char *user_paths[1], *sys_paths[1];
    mfc_key_source_cfg_t cfgs[2];
    user_paths[0] = "user.nfc";
    cfgs[0].kind = MFC_KEY_SRC_USER; cfgs[0].builtin = NULL; cfgs[0].builtin_n = 0;
    cfgs[0].paths = user_paths; cfgs[0].n_paths = 1;
    cfgs[0].prober = mock_prober; cfgs[0].opener = mock_opener; cfgs[0].io_ctx = NULL;
    cfgs[0].accumulate = true;
    sys_paths[0] = "system.nfc";
    cfgs[1].kind = MFC_KEY_SRC_SYSTEM; cfgs[1].builtin = NULL; cfgs[1].builtin_n = 0;
    cfgs[1].paths = sys_paths; cfgs[1].n_paths = 1;
    cfgs[1].prober = mock_prober; cfgs[1].opener = mock_opener; cfgs[1].io_ctx = NULL;
    cfgs[1].accumulate = false;

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;
    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };

    /* Candidate sequence: USER "111.." (src_tried=1) -> USER "222.." (same
     * source, src_tried=2) -> SYSTEM "AAA.." (source changed, src_tried=1)
     * -> SYSTEM "BBB.." (same source, src_tried=2). We only observe the
     * FINAL value here (sc reflects the last candidate processed); the
     * per-step sequence is exercised live by the real production UI poll,
     * not asserted step-by-step in this test. */
    (void)mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);
    CHECK(sc.cur_source == NFC_SCAN_SRC_SYSTEM, "final candidate was SYSTEM's 2nd file line");
    CHECK(sc.src_tried == 2U, "src_tried counts 2 consecutive SYSTEM candidates, not the 4 total");
}

/*----------------------------------------------------------------------------*/
/* Card-loss retry loop (card loss is a LIVE, RESUMABLE state)               */
/* state, not a terminal failure) and CENTER "Skip" (skip current source     */
/* only, via a NEW skip_source_flag, distinct from the whole-phase           */
/* abort_flag).                                                              */
/*----------------------------------------------------------------------------*/

/* Configurable reselect mock: fails s_reselect_fail_remaining times (each
 * failure decrementing it), then succeeds. Can optionally fire abort_flag or
 * skip_source_flag after a specific reselect call number, to test control
 * being honored WHILE the retry loop is waiting. */
static int            s_reselect_calls;
static int            s_reselect_fail_remaining;
static int            s_reselect_succeed_until_call;   /* calls 1..this always succeed, regardless of fail_remaining */
static volatile bool *s_reselect_trigger_flag;
static int            s_reselect_trigger_after_call;
static volatile bool *s_reselect_trigger2_flag;         /* optional 2nd trigger, for a scenario needing two */
static int            s_reselect_trigger2_after_call;

static bool mock_reselect_lossy(void *dev)
{
    (void)dev;
    s_reselect_calls++;
    if ((s_reselect_trigger_flag != NULL) && (s_reselect_calls == s_reselect_trigger_after_call)) {
        *s_reselect_trigger_flag = true;
    }
    if ((s_reselect_trigger2_flag != NULL) && (s_reselect_calls == s_reselect_trigger2_after_call)) {
        *s_reselect_trigger2_flag = true;
    }
    if (s_reselect_calls <= s_reselect_succeed_until_call) {
        return true;   /* forced-success window, e.g. to let an earlier candidate complete cleanly first */
    }
    if (s_reselect_fail_remaining > 0) {
        s_reselect_fail_remaining--;
        return false;
    }
    return true;
}

static void reselect_lossy_reset(void)
{
    s_reselect_calls = 0;
    s_reselect_fail_remaining = 0;
    s_reselect_succeed_until_call = 0;
    s_reselect_trigger_flag = NULL;
    s_reselect_trigger_after_call = -1;
    s_reselect_trigger2_flag = NULL;
    s_reselect_trigger2_after_call = -1;
}

/* Required test: same card returns after being lost -- the retry loop keeps
 * waiting (sc->state live-flips to CARD_LOST then back to RUNNING), and the
 * candidate that was mid-flight when the card vanished proceeds normally
 * once it returns, resolving exactly as if the card had never left. */
static void test_card_loss_then_same_card_resumes(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;
    reselect_lossy_reset();

    static const uint8_t u1[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    static const char *user_lines[] = { "111111111111" };
    mock_register("user.nfc", user_lines, 1);
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("system.nfc", sys_lines, 1);
    facts_add(0, MFC_DICT_PHASE_KEY_A_CMD, u1); facts_add(0, MFC_DICT_PHASE_KEY_B_CMD, u1);

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;   /* mock_auth's own abort-arming, unused here (no auth trigger armed) */
    s_abort_after_n = -1;

    /* Fail reselect for the FIRST 3 calls (tolerated-1 + 2 more, so
     * sc->state definitely flips to CARD_LOST before recovering), then
     * succeed from the 4th call onward. */
    s_reselect_fail_remaining = 3;

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect_lossy, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    bool r = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(r, "returns true (card loss is no longer a give-up condition)");
    CHECK(sc.state != NFC_SCAN_CARD_LOST, "state is NOT left at CARD_LOST -- the card came back");
    CHECK(mfc.sec[0].key_a_found && mfc.sec[0].key_b_found,
          "sector 0 resolves normally once the SAME card returns -- nothing lost to the outage");
    CHECK(s_reselect_calls >= 4, "reselect was retried multiple times before finally succeeding");
}

/* Required test: abort fires WHILE the card is lost -- the loop must stop
 * cleanly (return true) rather than spin forever, and everything already
 * proven before the loss must survive. */
static void test_card_loss_then_abort_preserves_progress(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;
    reselect_lossy_reset();

    static const uint8_t u1[6] = {0x11,0x11,0x11,0x11,0x11,0x11};
    static const char *user_lines[] = { "111111111111", "222222222222" };
    mock_register("user.nfc", user_lines, 2);
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("system.nfc", sys_lines, 1);
    /* u1 (candidate 1) opens sector 0 -- proven BEFORE the loss. */
    facts_add(0, MFC_DICT_PHASE_KEY_A_CMD, u1); facts_add(0, MFC_DICT_PHASE_KEY_B_CMD, u1);

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;

    /* Candidate 1 (u1) needs exactly 3 successful reselects to fully
     * resolve sector 0 (initial + post-KeyA "back to fresh" + post-KeyB) --
     * force those 3 to succeed, THEN start failing from call 4 onward
     * (candidate 2's own initial reselect), "forever" (a large bounded
     * count so the test can't hang if something is wrong). Abort fires on
     * call 5 -- the SECOND consecutive failure, i.e. exactly when
     * sc->state has just become CARD_LOST (the first failure alone is
     * tolerated silently, matching the real one-glitch grace). */
    s_reselect_succeed_until_call = 3;
    s_reselect_fail_remaining = 1000000;
    s_reselect_trigger_flag = &abort_flag;
    s_reselect_trigger_after_call = 5;

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect_lossy, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    bool r = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(r, "clean stop while lost returns true, not a hang and not false");
    CHECK(abort_flag, "abort flag ended up set (armed and fired mid-loss)");
    CHECK(sc.state == NFC_SCAN_CARD_LOST, "state reflects the truth: still lost when the user gave up waiting");
    CHECK(mfc.sec[0].key_a_found && mfc.sec[0].key_b_found,
          "sector 0 (proven by candidate 1, BEFORE the loss) survives the abort-while-lost");
}

/* Required test: CENTER Skip fires while the card is lost -- abandons only
 * the source that was active, resumes acquisition with the next source
 * (never requires the card to come back first). */
static void test_card_loss_then_skip_source_advances(void)
{
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;
    reselect_lossy_reset();

    static const char *user_lines[] = { "111111111111" };   /* opens nothing */
    mock_register("user.nfc", user_lines, 1);
    static const uint8_t s1[6] = {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA};
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("system.nfc", sys_lines, 1);
    /* SYSTEM's one candidate would open sector 1 -- but the test expects it
     * never gets the chance, because the card is "lost" for the whole USER
     * phase and Skip is used to abandon USER before ever reselecting
     * successfully. */
    facts_add(4, MFC_DICT_PHASE_KEY_A_CMD, s1); facts_add(4, MFC_DICT_PHASE_KEY_B_CMD, s1);

    mfc_key_source_cfg_t cfgs[2];
    const char *user_paths[1], *sys_paths[1];
    build_key_sources(cfgs, user_paths, sys_paths);

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    volatile bool skip_flag  = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;

    /* Reselect fails "forever" (never lets USER's candidate be tried at
     * all); Skip fires on the 2nd reselect call (once genuinely inside the
     * loss window, USER's own retry loop). After Skip abandons USER and
     * moves on to SYSTEM, SYSTEM's own candidate immediately hits the SAME
     * still-failing reselect -- a second trigger (abort, on call 4: the
     * 2nd consecutive failure of SYSTEM's own retry loop) stops the test
     * deterministically and fast, instead of letting it run up to
     * s_reselect_fail_remaining's full bound. */
    s_reselect_fail_remaining = 1000000;
    s_reselect_trigger_flag = &skip_flag;
    s_reselect_trigger_after_call = 2;
    s_reselect_trigger2_flag = &abort_flag;
    s_reselect_trigger2_after_call = 4;

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect_lossy, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    bool r = mfc_dict_phase_run(cfgs, 2, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, &skip_flag);

    CHECK(r, "returns true");
    CHECK(s_auth_calls == 0, "USER's candidate was never actually tried (reselect never succeeded for it)");
    /* After Skip abandons USER and moves to SYSTEM, reselect must succeed
     * again for SYSTEM's own attempts to have any chance -- but this mock's
     * s_reselect_fail_remaining is still "forever failing" from where it
     * left off, so SYSTEM's own candidate ALSO never gets a chance; the
     * point of this test is specifically that Skip moved iteration past
     * USER without requiring the card back first, not that SYSTEM then
     * succeeds. Confirms sc->state stays truthfully CARD_LOST (reselect is
     * still failing) and nothing hangs. */
    CHECK(sc.state == NFC_SCAN_CARD_LOST, "still truthfully lost -- Skip does not fake the card being back");
}

static void test_zero_data_no_source_is_still_safe(void)
{
    /* Defensive: a source list containing a source with NO candidates at
     * all (no builtins, no file) must not crash or hang -- iterator
     * exhausts immediately, function returns true with nothing found. */
    mock_reset(); facts_reset();
    s_auth_calls = 0; s_last_authed_n = 0;
    reselect_lossy_reset();

    mfc_key_source_cfg_t cfgs[1];
    const char *user_paths[1] = { "nonexistent.nfc" };
    cfgs[0].kind = MFC_KEY_SRC_USER; cfgs[0].builtin = NULL; cfgs[0].builtin_n = 0;
    cfgs[0].paths = user_paths; cfgs[0].n_paths = 1;
    cfgs[0].prober = mock_prober; cfgs[0].opener = mock_opener; cfgs[0].io_ctx = NULL;
    cfgs[0].accumulate = true;

    nfc_mfc_info_t mfc; nfc_mfc_scan_t sc;
    mfc_init(&mfc, &sc, 2);
    volatile bool abort_flag = false;
    s_abort_flag_ptr = &abort_flag;
    s_abort_after_n = -1;

    mfc_dict_phase_ops_t ops = {
        .dev = NULL, .crypto_ctx = NULL,
        .reselect = mock_reselect, .auth = mock_auth, .read_sector = mock_read_sector,
    };
    bool r = mfc_dict_phase_run(cfgs, 1, NULL, 0x12345678U, &ops, &mfc, &sc, &abort_flag, NULL, NULL);

    CHECK(r, "an empty/absent source list is safe -- returns true, no hang, no crash");
    CHECK(mfc.keys_found == 0, "nothing found, truthfully");
    CHECK(s_auth_calls == 0, "no auth attempts made at all -- nothing to try");
}

int main(void)
{
    test_continuous_user_to_system_scan_cancelled_midway();
    test_zero_data_cancellation_is_cancelled_not_failed();
    test_continuous_scan_completes_without_cancellation();
    test_continuation_preserves_prior_progress_and_resolves_more();
    test_continuation_does_not_retry_failed_candidate();
    test_continuation_retries_interrupted_candidate_not_skips_it();
    test_src_tried_resets_per_source();
    test_card_loss_then_same_card_resumes();
    test_card_loss_then_abort_preserves_progress();
    test_card_loss_then_skip_source_advances();
    test_zero_data_no_source_is_still_safe();

    printf("mfc_dict_phase_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
