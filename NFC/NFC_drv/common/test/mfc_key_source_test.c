/* Host tests for mfc_key_source.c -- executed against the REAL production
 * source file (compiled directly into this binary below), not a transcribed
 * copy and not source-text verification. mfc_key_source.c has no FatFs/HAL
 * dependency (that's the whole point of its injected mfc_line_source_t /
 * mfc_path_prober_fn / mfc_line_source_opener_fn seam), so it links and runs
 * on host unmodified; only the mocks in this file stand in for real SD I/O.
 *
 * No REPO_ROOT, no hard-coded filesystem paths: this test never reads a file
 * from disk. "Paths" below are opaque strings the mock prober/opener match
 * against a table, exactly as the production mfc_key_source_sd.c adapter
 * would match them against real files on SD.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../nfc_dict_line.c ../mfc_key_source.c mfc_key_source_test.c -I.. -o /tmp/mks && /tmp/mks
 */
#include "../mfc_key_source.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/*----------------------------------------------------------------------------*/
/* Mock file-I/O seam                                                          */
/*----------------------------------------------------------------------------*/
typedef struct { const char *path; const char *const *lines; size_t n_lines; } mock_entry_t;

static mock_entry_t s_table[8];
static size_t       s_table_n;

static void mock_register(const char *path, const char *const *lines, size_t n_lines)
{
    s_table[s_table_n].path = path;
    s_table[s_table_n].lines = lines;
    s_table[s_table_n].n_lines = n_lines;
    s_table_n++;
}
static void mock_reset(void) { s_table_n = 0; }

/* byte_pos[i] mirrors what a real byte-offset-based file would report after
 * yielding line i -- one plausible, deterministic value per line (so a
 * resume test can seek to "the offset after line i" and assert streaming
 * continues at line i+1), without needing an actual byte-accurate encoding
 * of the mock's fixed-width lines. */
typedef struct { const char *const *lines; size_t n; size_t idx; bool support_resume; } mock_open_file_t;
static mock_open_file_t s_open;   /* single active source at a time, matching real usage */
/* Whether the NEXT source opened (by mock_opener()) provides tell()/seek() --
 * a per-test setting, not auto-reset after one use, since a single test
 * typically wants every source it opens to behave the same way. Production's
 * tell()/seek() gating is by cfg->kind==SYSTEM regardless of what the source
 * itself provides, so setting this true even for a non-SYSTEM source in a
 * test is harmless and never affects last_system_offset_valid. */
static bool s_next_open_supports_resume;

static mfc_path_probe_t mock_prober(const char *path, void *io_ctx)
{
    (void)io_ctx;
    for (size_t i = 0; i < s_table_n; i++) {
        if (strcmp(s_table[i].path, path) == 0) return MFC_PATH_PRESENT;
    }
    /* A path prefixed "ERR:" simulates a present-but-unreadable file without
     * needing a separate registration table. */
    if (strncmp(path, "ERR:", 4) == 0) return MFC_PATH_READ_ERROR;
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

/* "Byte offset" is simply the line index in this mock -- monotonic, unique
 * per position, and trivially invertible by seek() -- a real adapter (SD
 * FatFs) uses true byte offsets, but the iterator only ever treats this
 * value as an opaque resume token, never interprets it, so a fake but
 * consistent unit is sufficient and keeps the mock simple. */
static long mock_tell(void *ctx)
{
    mock_open_file_t *f = (mock_open_file_t *)ctx;
    return (long)f->idx;
}
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
            s_open.lines = s_table[i].lines;
            s_open.n     = s_table[i].n_lines;
            s_open.idx   = 0;
            s_open.support_resume = s_next_open_supports_resume;
            out->next_line = mock_next_line;
            out->close     = mock_close;
            out->tell      = s_open.support_resume ? mock_tell : NULL;
            out->seek      = s_open.support_resume ? mock_seek : NULL;
            out->ctx       = &s_open;
            return 1;
        }
    }
    return 0;
}

static const uint8_t s_builtin[][MFC_KEY_SIZE] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

/* `accumulate` defaults to true at every existing call site below (via
 * mk_cfg_acc()'s wrapper) -- matching this module's pre-accumulate-field
 * behavior for every test that isn't specifically exercising the
 * bounded/unbounded distinction (those call mk_cfg() directly). */
static mfc_key_source_cfg_t mk_cfg_acc(mfc_key_source_kind_t kind,
                                       const uint8_t (*builtin)[MFC_KEY_SIZE], size_t builtin_n,
                                       const char *const *paths, size_t n_paths, bool accumulate)
{
    mfc_key_source_cfg_t c;
    c.kind       = kind;
    c.builtin    = builtin;
    c.builtin_n  = builtin_n;
    c.paths      = paths;
    c.n_paths    = n_paths;
    c.prober     = mock_prober;
    c.opener     = mock_opener;
    c.io_ctx     = NULL;
    c.accumulate = accumulate;
    return c;
}

static mfc_key_source_cfg_t mk_cfg(mfc_key_source_kind_t kind,
                                   const uint8_t (*builtin)[MFC_KEY_SIZE], size_t builtin_n,
                                   const char *const *paths, size_t n_paths)
{
    return mk_cfg_acc(kind, builtin, builtin_n, paths, n_paths, true);
}

static bool keq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, MFC_KEY_SIZE) == 0; }

/*----------------------------------------------------------------------------*/
static void test_parse_line(void)
{
    uint8_t k[MFC_KEY_SIZE];

    CHECK(mfc_key_parse_line("", k) == MFC_KEY_LINE_SKIP, "blank line skipped");
    CHECK(mfc_key_parse_line("# comment", k) == MFC_KEY_LINE_SKIP, "comment skipped");
    CHECK(mfc_key_parse_line("  # indented comment", k) == MFC_KEY_LINE_SKIP, "indented comment skipped");

    CHECK(mfc_key_parse_line("FFFFFFFFFFFF", k) == MFC_KEY_LINE_KEY, "uppercase key accepted");
    CHECK(k[0] == 0xFF && k[5] == 0xFF, "uppercase key bytes correct");
    CHECK(mfc_key_parse_line("ffffffffffff", k) == MFC_KEY_LINE_KEY, "lowercase key accepted");
    CHECK(k[0] == 0xFF, "lowercase normalized correctly");
    CHECK(mfc_key_parse_line("A0A1a2A3a4A5", k) == MFC_KEY_LINE_KEY, "mixed case accepted");
    CHECK(k[0]==0xA0 && k[1]==0xA1 && k[2]==0xA2 && k[3]==0xA3 && k[4]==0xA4 && k[5]==0xA5,
          "mixed case bytes correct");
    CHECK(mfc_key_parse_line("  FFFFFFFFFFFF  ", k) == MFC_KEY_LINE_KEY, "surrounding whitespace tolerated");
    CHECK(mfc_key_parse_line("FFFFFFFFFFFF # trailing", k) == MFC_KEY_LINE_KEY, "trailing comment tolerated");

    CHECK(mfc_key_parse_line("FFFFFFFFFFF", k) == MFC_KEY_LINE_BAD, "too short rejected");
    CHECK(mfc_key_parse_line("FFFFFFFFFFFFF", k) == MFC_KEY_LINE_BAD, "too long rejected");
    CHECK(mfc_key_parse_line("GGGGGGGGGGGG", k) == MFC_KEY_LINE_BAD, "non-hex rejected");
    CHECK(mfc_key_parse_line("FFFFFFFFFFFF-", k) == MFC_KEY_LINE_BAD, "trailing garbage rejected");
}

/* Source ordering: user before system, each source's builtins before its file. */
static void test_source_order(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1" };
    static const char *sys_lines[]  = { "B2B2B2B2B2B2" };
    mock_register("user.nfc", user_lines, 1);
    mock_register("sys.nfc",  sys_lines, 1);

    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, s_builtin, 2, sys_paths, 1),
    };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 2);

    CHECK(mfc_key_source_iter_next(&it, &c), "1st candidate present");
    CHECK(c.kind == MFC_KEY_SRC_USER, "1st candidate is USER (user dict runs first)");
    CHECK(!c.from_builtin, "1st candidate (USER file) is not from_builtin");
    uint8_t exp1[6] = {0xA1,0xA1,0xA1,0xA1,0xA1,0xA1};
    CHECK(keq(c.key, exp1), "1st candidate is the user file's key");

    CHECK(mfc_key_source_iter_next(&it, &c), "2nd candidate present");
    CHECK(c.kind == MFC_KEY_SRC_SYSTEM, "2nd candidate is SYSTEM (builtins, after user exhausted)");
    CHECK(c.from_builtin, "2nd candidate IS from_builtin -- distinguishes it from a system FILE key");
    CHECK(keq(c.key, s_builtin[0]), "2nd candidate is the system source's 1st builtin");

    CHECK(mfc_key_source_iter_next(&it, &c), "3rd candidate present");
    CHECK(c.from_builtin, "3rd candidate is also from_builtin");
    CHECK(keq(c.key, s_builtin[1]), "3rd candidate is the system source's 2nd builtin");

    CHECK(mfc_key_source_iter_next(&it, &c), "4th candidate present");
    CHECK(c.kind == MFC_KEY_SRC_SYSTEM, "4th candidate is still SYSTEM (its file component)");
    CHECK(!c.from_builtin, "4th candidate (SYSTEM file) is NOT from_builtin -- same kind, different origin");
    uint8_t exp2[6] = {0xB2,0xB2,0xB2,0xB2,0xB2,0xB2};
    CHECK(keq(c.key, exp2), "4th candidate is the system file's key (after its builtins)");

    CHECK(!mfc_key_source_iter_next(&it, &c), "iterator exhausted after both sources");
    mfc_key_source_iter_end(&it);
}

static void test_canonical_then_fallback(void)
{
    /* Canonical present: fallback never consulted (mock would ABSENT it anyway,
     * but this proves the canonical entry wins when both would resolve). */
    mock_reset();
    static const char *lines[] = { "C1C1C1C1C1C1" };
    mock_register("canonical.nfc", lines, 1);
    mock_register("fallback.nfc",  lines, 1);
    static const char *paths[] = { "canonical.nfc", "fallback.nfc" };
    mfc_key_source_cfg_t cfg = mk_cfg(MFC_KEY_SRC_USER, NULL, 0, paths, 2);

    mfc_key_source_file_state_t st = mfc_key_source_probe(&cfg);
    CHECK(st.state == MFC_PATH_PRESENT, "canonical path resolves PRESENT");
    CHECK(strcmp(st.used_path, "canonical.nfc") == 0, "canonical path used when present");

    /* Canonical absent: fallback used. */
    mock_reset();
    mock_register("fallback.nfc", lines, 1);
    st = mfc_key_source_probe(&cfg);
    CHECK(st.state == MFC_PATH_PRESENT, "fallback resolves PRESENT when canonical absent");
    CHECK(strcmp(st.used_path, "fallback.nfc") == 0, "fallback path used when canonical absent");
}

static void test_absent_vs_read_error(void)
{
    mock_reset();
    static const char *paths_absent[] = { "nope.nfc", "also-nope.nfc" };
    mfc_key_source_cfg_t cfg = mk_cfg(MFC_KEY_SRC_USER, NULL, 0, paths_absent, 2);
    mfc_key_source_file_state_t st = mfc_key_source_probe(&cfg);
    CHECK(st.state == MFC_PATH_ABSENT, "both paths absent -> ABSENT, not an error");
    CHECK(st.used_path == NULL, "no used_path when both absent");

    /* A read-error on the FIRST candidate stops the search -- it must not
     * silently fall through to a working fallback. */
    static const char *fallback_lines[] = { "D1D1D1D1D1D1" };
    mock_register("fallback-ok.nfc", fallback_lines, 1);
    static const char *paths_err[] = { "ERR:canonical.nfc", "fallback-ok.nfc" };
    cfg.paths = paths_err;
    st = mfc_key_source_probe(&cfg);
    CHECK(st.state == MFC_PATH_READ_ERROR, "canonical read-error reported, not swapped for fallback");
    CHECK(strcmp(st.used_path, "ERR:canonical.nfc") == 0, "used_path names the failing candidate");

    /* Absent required: iterating a source whose file component errors yields
     * nothing from that source (never a partial/garbled read), and does not
     * abort the whole multi-source iteration -- the next source still runs. */
    static const char *sys_lines[] = { "E1E1E1E1E1E1" };
    mock_register("sys.nfc", sys_lines, 1);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        cfg,
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1),
    };
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 2);
    CHECK(mfc_key_source_iter_next(&it, &c), "iteration continues past a failing source");
    uint8_t exp[6] = {0xE1,0xE1,0xE1,0xE1,0xE1,0xE1};
    CHECK(keq(c.key, exp), "the working second source's key is still yielded");
    CHECK(!mfc_key_source_iter_next(&it, &c), "iterator exhausted after the one working source");
    mfc_key_source_iter_end(&it);
}

static void test_dedup_cross_source_and_within_file(void)
{
    mock_reset();
    /* "A1..." appears in BOTH user and system, plus twice within the system
     * file itself (once lowercase). It must be yielded exactly once, tagged
     * with whichever source reached it first (USER). */
    static const char *user_lines[] = { "A1A1A1A1A1A1", "B2B2B2B2B2B2" };
    static const char *sys_lines[]  = { "a1a1a1a1a1a1", "C3C3C3C3C3C3", "A1A1A1A1A1A1" };
    mock_register("user.nfc", user_lines, 2);
    mock_register("sys.nfc",  sys_lines, 3);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1),
    };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    int a1_count = 0, total = 0;
    mfc_key_source_kind_t a1_kind = MFC_KEY_SRC_SYSTEM;
    mfc_key_source_iter_begin(&it, cfgs, 2);
    uint8_t a1[6] = {0xA1,0xA1,0xA1,0xA1,0xA1,0xA1};
    while (mfc_key_source_iter_next(&it, &c)) {
        total++;
        if (keq(c.key, a1)) { a1_count++; a1_kind = c.kind; }
    }
    mfc_key_source_iter_end(&it);

    CHECK(a1_count == 1, "A1..A1 yielded exactly once despite appearing 3 times across both sources");
    CHECK(a1_kind == MFC_KEY_SRC_USER, "duplicate is tagged with the source that reached it FIRST");
    CHECK(total == 3, "exactly 3 unique keys total (A1.., B2.., C3..)");
}

static void test_count_matches_iterator_output(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1", "B2B2B2B2B2B2", "not-a-key", "# c", "" };
    mock_register("user.nfc", user_lines, 5);
    static const char *user_paths[] = { "user.nfc" };
    mfc_key_source_cfg_t cfgs[1] = {
        mk_cfg(MFC_KEY_SRC_USER, s_builtin, 2, user_paths, 1),
    };

    mfc_key_source_count_t cnt = mfc_key_source_count(cfgs, 1);

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    uint32_t manual = 0;
    mfc_key_source_iter_begin(&it, cfgs, 1);
    while (mfc_key_source_iter_next(&it, &c)) manual++;
    mfc_key_source_iter_end(&it);

    CHECK(cnt.count == manual, "mfc_key_source_count() matches a manual drain of the same iterator");
    CHECK(cnt.count == 4, "2 builtins + 2 valid file keys, malformed/blank/comment lines excluded");
    CHECK(!cnt.overflowed, "no overflow for a small source list");
}

static void test_seed_seen(void)
{
    mock_reset();
    static const char *lines[] = { "FFFFFFFFFFFF", "A1A1A1A1A1A1" };
    mock_register("sys.nfc", lines, 2);
    static const char *paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, paths, 1) };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 1);
    uint8_t ffs[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    mfc_key_source_iter_seed_seen(&it, ffs);   /* e.g. already tried by the fast/default pass */

    CHECK(mfc_key_source_iter_next(&it, &c), "one candidate remains after seeding");
    uint8_t exp[6] = {0xA1,0xA1,0xA1,0xA1,0xA1,0xA1};
    CHECK(keq(c.key, exp), "the seeded key is skipped; only the other key is yielded");
    CHECK(!mfc_key_source_iter_next(&it, &c), "iterator exhausted after the one non-seeded key");
    mfc_key_source_iter_end(&it);
}

/* Builds N distinct 12-hex-digit lines ("000000000000", "000000000001", ...)
 * into `bufs`/`lines`, matching the real system dictionary's shape (a large,
 * pre-deduplicated flat text file) without hard-coding any filesystem path
 * or shipping a multi-thousand-line fixture in the repo. */
static void gen_unique_lines(char bufs[][MFC_KEY_HEXLEN + 1], const char *lines[], size_t n)
{
    for (size_t i = 0; i < n; i++) {
        snprintf(bufs[i], MFC_KEY_HEXLEN + 1, "%012zX", i);
        lines[i] = bufs[i];
    }
}

/* The exact scenario the MFC_KEY_SOURCE_MAX_SEEN cap must never break: a
 * SYSTEM dictionary file far larger than the cap (the real production
 * dictionary holds ~2,042 unique keys) must still yield an EXACT count with
 * no overflow and no truncation, because accumulate=false means its entries
 * are only ever checked against the shared set, never added to it -- the
 * cap governs accumulate-eligible entries only. */
static void test_large_unbounded_system_source_exact_count(void)
{
    mock_reset();
    enum { N = 2200 };   /* > MFC_KEY_SOURCE_MAX_SEEN(1024) and > the real ~2042-key dictionary */
    static char bufs[N][MFC_KEY_HEXLEN + 1];
    static const char *lines[N];
    gen_unique_lines(bufs, lines, N);
    mock_register("big_sys.nfc", lines, N);
    static const char *paths[] = { "big_sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = {
        mk_cfg_acc(MFC_KEY_SRC_SYSTEM, NULL, 0, paths, 1, /*accumulate=*/false),
    };

    mfc_key_source_count_t cnt = mfc_key_source_count(cfgs, 1);
    CHECK(cnt.count == N, "unbounded system source count is exact for 2200 unique keys, no truncation");
    CHECK(!cnt.overflowed, "unbounded (accumulate=false) source never reports overflow, regardless of size");

    /* The iterator must yield the same exact count when actually driven end
     * to end, not just when summarized by mfc_key_source_count(). */
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    uint32_t manual = 0;
    mfc_key_source_iter_begin(&it, cfgs, 1);
    while (mfc_key_source_iter_next(&it, &c)) manual++;
    mfc_key_source_iter_end(&it);
    CHECK(manual == N, "driving the iterator directly also yields all 2200 keys, none dropped");
}

/* An accumulate=true source (built-ins, or a bounded user dictionary) that
 * somehow exceeds MFC_KEY_SOURCE_MAX_SEEN still yields every entry -- the cap
 * only degrades this source's ability to self-dedup/cross-suppress beyond
 * that point (signalled via `overflowed`), it never truncates output. This
 * is the accumulate=true counterpart to the exact-count test above. */
static void test_accumulate_source_overflow_does_not_truncate(void)
{
    mock_reset();
    enum { N = 1100 };   /* > MFC_KEY_SOURCE_MAX_SEEN(1024) */
    static char bufs[N][MFC_KEY_HEXLEN + 1];
    static const char *lines[N];
    gen_unique_lines(bufs, lines, N);
    mock_register("big_user.nfc", lines, N);
    static const char *paths[] = { "big_user.nfc" };
    mfc_key_source_cfg_t cfgs[1] = {
        mk_cfg_acc(MFC_KEY_SRC_USER, NULL, 0, paths, 1, /*accumulate=*/true),
    };

    mfc_key_source_count_t cnt = mfc_key_source_count(cfgs, 1);
    CHECK(cnt.count == N, "an oversized accumulate=true source still yields every unique key");
    CHECK(cnt.overflowed, "the accumulate cap is reported once an accumulate-eligible source exceeds it");
}

/*----------------------------------------------------------------------------*/
/* Resume support: mfc_key_source_iter_begin_resume() (required tests #7/#8   */
/* plus the approved resume-cursor design's own test list).                   */
/*----------------------------------------------------------------------------*/

/* NULL seen_in must behave IDENTICALLY to plain mfc_key_source_iter_begin()
 * -- proves every existing call site (which only ever uses the plain
 * function) is provably unaffected by this one's mere existence. */
static void test_resume_null_matches_plain_begin(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1" };
    static const char *sys_lines[]  = { "B2B2B2B2B2B2" };
    mock_register("user.nfc", user_lines, 1);
    mock_register("sys.nfc",  sys_lines, 1);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1),
    };

    s_next_open_supports_resume = false;
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin_resume(&it, cfgs, 2, NULL, 0, false, 0);
    uint32_t n = 0;
    while (mfc_key_source_iter_next(&it, &c)) n++;
    mfc_key_source_iter_end(&it);
    CHECK(n == 2, "begin_resume(NULL seen, 0 offset) yields the same 2 candidates as a fresh begin()");
}

/* Required test #7: a built-in/User candidate already yielded (and recorded
 * into seen[]) in an earlier generation-scoped pass is never re-yielded when
 * that seen[] snapshot is threaded into a second pass via begin_resume(). */
static void test_resume_seen_snapshot_skips_already_tried_user_key(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1", "B2B2B2B2B2B2" };
    mock_register("user.nfc", user_lines, 2);
    static const char *user_paths[] = { "user.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_USER, NULL, 0, user_paths, 1) };

    /* Pass 1: a full, uninterrupted run -- both keys tried. */
    mfc_key_source_iter_t it1;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it1, cfgs, 1);
    int pass1_count = 0;
    while (mfc_key_source_iter_next(&it1, &c)) pass1_count++;
    uint16_t seen_n = it1.seen_n;
    bool     overflowed = it1.seen_overflowed;
    uint8_t  seen_snapshot[MFC_KEY_SOURCE_MAX_SEEN][MFC_KEY_SIZE];
    memcpy(seen_snapshot, it1.seen, sizeof(seen_snapshot));
    mfc_key_source_iter_end(&it1);
    CHECK(pass1_count == 2, "pass 1 tries both User keys");
    CHECK(seen_n == 2, "pass 1's seen[] recorded exactly the 2 tried keys");

    /* Pass 2 (Find Missing Keys continuation): same source, same seen[]
     * snapshot -- nothing left to try, since neither key is new. */
    mfc_key_source_iter_t it2;
    mfc_key_source_iter_begin_resume(&it2, cfgs, 1, seen_snapshot, seen_n, overflowed, 0);
    int pass2_count = 0;
    while (mfc_key_source_iter_next(&it2, &c)) pass2_count++;
    mfc_key_source_iter_end(&it2);
    CHECK(pass2_count == 0,
          "pass 2 (resumed with pass 1's seen[]) retries NEITHER already-tried User key");
}

/* Required test #8: if the User dictionary genuinely changed (a new line
 * appended) between passes, the new key IS tried on the resumed pass, since
 * seen[] dedups by VALUE, not by position/generation. */
static void test_resume_new_user_key_is_tried(void)
{
    mock_reset();
    static const char *user_lines_v1[] = { "A1A1A1A1A1A1" };
    mock_register("user.nfc", user_lines_v1, 1);
    static const char *user_paths[] = { "user.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_USER, NULL, 0, user_paths, 1) };

    mfc_key_source_iter_t it1;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it1, cfgs, 1);
    while (mfc_key_source_iter_next(&it1, &c)) { /* drain pass 1 */ }
    uint16_t seen_n = it1.seen_n;
    uint8_t  seen_snapshot[MFC_KEY_SOURCE_MAX_SEEN][MFC_KEY_SIZE];
    memcpy(seen_snapshot, it1.seen, sizeof(seen_snapshot));
    mfc_key_source_iter_end(&it1);

    /* The user edited their key file, adding a second key, before pressing
     * Find Missing Keys. */
    mock_reset();
    static const char *user_lines_v2[] = { "A1A1A1A1A1A1", "C3C3C3C3C3C3" };
    mock_register("user.nfc", user_lines_v2, 2);

    mfc_key_source_iter_t it2;
    mfc_key_source_iter_begin_resume(&it2, cfgs, 1, seen_snapshot, seen_n, false, 0);
    int pass2_count = 0;
    uint8_t exp[6] = {0xC3,0xC3,0xC3,0xC3,0xC3,0xC3};
    bool saw_new = false;
    while (mfc_key_source_iter_next(&it2, &c)) {
        pass2_count++;
        if (keq(c.key, exp)) saw_new = true;
    }
    mfc_key_source_iter_end(&it2);
    CHECK(pass2_count == 1, "pass 2 tries exactly the one genuinely new key");
    CHECK(saw_new, "the newly appended User key is the one tried");
}

/* System resume: last_system_offset_valid/last_system_offset are populated
 * only for a SYSTEM file candidate whose source supports tell(), and never
 * for a builtin or USER candidate. */
static void test_last_system_offset_tracking(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1" };
    static const char *sys_lines[]  = { "B2B2B2B2B2B2", "C3C3C3C3C3C3" };
    mock_register("user.nfc", user_lines, 1);
    mock_register("sys.nfc",  sys_lines, 2);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0,      user_paths, 1),   /* USER: no builtins, matching real usage */
        mk_cfg(MFC_KEY_SRC_SYSTEM, s_builtin, 2, sys_paths,  1),
    };

    s_next_open_supports_resume = true;   /* every source opened in this test provides tell()/seek() */
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 2);

    CHECK(mfc_key_source_iter_next(&it, &c) && c.kind == MFC_KEY_SRC_USER && !c.from_builtin,
          "1st: USER file candidate");
    CHECK(!it.last_system_offset_valid, "USER file candidate never sets last_system_offset_valid");

    CHECK(mfc_key_source_iter_next(&it, &c) && c.from_builtin, "2nd: a SYSTEM builtin candidate");
    CHECK(!it.last_system_offset_valid, "builtin candidate never sets last_system_offset_valid");

    CHECK(mfc_key_source_iter_next(&it, &c) && c.from_builtin, "3rd: 2nd SYSTEM builtin candidate");
    CHECK(!it.last_system_offset_valid, "2nd builtin candidate never sets last_system_offset_valid either");

    CHECK(mfc_key_source_iter_next(&it, &c) && c.kind == MFC_KEY_SRC_SYSTEM && !c.from_builtin,
          "4th: SYSTEM file candidate (its source supports tell())");
    CHECK(it.last_system_offset_valid, "SYSTEM file candidate DOES set last_system_offset_valid");
    CHECK(it.last_system_offset == 1U, "offset reflects exactly 1 line consumed from the SYSTEM file");

    CHECK(mfc_key_source_iter_next(&it, &c), "5th: 2nd SYSTEM file candidate");
    CHECK(it.last_system_offset_valid && it.last_system_offset == 2U,
          "offset advances to 2 lines consumed");

    CHECK(!mfc_key_source_iter_next(&it, &c), "exhausted");
    mfc_key_source_iter_end(&it);
}

/* Exact resume: seeking to "the offset after line 1" resumes at exactly line
 * 2, retrying nothing and skipping nothing -- not the interrupted line
 * itself (that one is retried, per the approved cancellation-boundary
 * design, exercised separately below), and not an extra line beyond it. */
static void test_system_resume_exact_offset(void)
{
    mock_reset();
    static const char *sys_lines[] = { "B2B2B2B2B2B2", "C3C3C3C3C3C3", "D4D4D4D4D4D4" };
    mock_register("sys.nfc", sys_lines, 3);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1) };

    /* Simulate: an earlier pass fully resolved line 1 ("B2...") and
     * committed offset=1 (mfc_dict_phase.c's own commit point -- see
     * mfc_dict_resume_test.c for that layer). Resuming from offset=1 must
     * yield "C3..." first, never re-yielding "B2...". */
    s_next_open_supports_resume = true;
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin_resume(&it, cfgs, 1, NULL, 0, false, /*system_resume_offset=*/1U);

    CHECK(mfc_key_source_iter_next(&it, &c), "1st candidate after resume present");
    uint8_t exp_c3[6] = {0xC3,0xC3,0xC3,0xC3,0xC3,0xC3};
    CHECK(keq(c.key, exp_c3), "resume from offset 1 yields C3... first -- B2... is NOT retried");

    CHECK(mfc_key_source_iter_next(&it, &c), "2nd candidate after resume present");
    uint8_t exp_d4[6] = {0xD4,0xD4,0xD4,0xD4,0xD4,0xD4};
    CHECK(keq(c.key, exp_d4), "2nd resumed candidate is D4..., nothing skipped");

    CHECK(!mfc_key_source_iter_next(&it, &c), "exhausted after resuming through the remaining 2 lines");
    mfc_key_source_iter_end(&it);
}

/* EOF: resuming from an offset that lands exactly at end-of-file yields
 * nothing new -- a well-defined, non-crashing terminal state. */
static void test_system_resume_at_eof(void)
{
    mock_reset();
    static const char *sys_lines[] = { "B2B2B2B2B2B2", "C3C3C3C3C3C3" };
    mock_register("sys.nfc", sys_lines, 2);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1) };

    s_next_open_supports_resume = true;
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin_resume(&it, cfgs, 1, NULL, 0, false, /*system_resume_offset=*/2U);
    CHECK(!mfc_key_source_iter_next(&it, &c),
          "resuming exactly at EOF (both lines already consumed) yields nothing -- well-defined, no crash");
    mfc_key_source_iter_end(&it);
}

/* Resume unsupported (tell/seek both NULL, e.g. an adapter with no notion of
 * position): a nonzero system_resume_offset request is safely ignored --
 * streaming falls back to the full source from the top, never a crash and
 * never a silently-wrong skipped position. */
static void test_system_resume_unsupported_falls_back_to_full_scan(void)
{
    mock_reset();
    static const char *sys_lines[] = { "B2B2B2B2B2B2", "C3C3C3C3C3C3" };
    mock_register("sys.nfc", sys_lines, 2);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1) };

    s_next_open_supports_resume = false;   /* no tell()/seek() on this source */
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin_resume(&it, cfgs, 1, NULL, 0, false, /*system_resume_offset=*/1U);
    int n = 0;
    while (mfc_key_source_iter_next(&it, &c)) n++;
    mfc_key_source_iter_end(&it);
    CHECK(n == 2, "with no seek() available, streaming falls back to the full 2-line source, not a crash");
}

/* Zero unnecessarily-retried candidates: a full uninterrupted pass (builtin +
 * User + System) followed by a second pass sharing BOTH the seen[] snapshot
 * AND the System offset retries nothing at all, from any source. */
static void test_resume_full_pass_then_second_pass_retries_nothing(void)
{
    mock_reset();
    static const char *user_lines[] = { "A1A1A1A1A1A1" };
    static const char *sys_lines[]  = { "B2B2B2B2B2B2", "C3C3C3C3C3C3" };
    mock_register("user.nfc", user_lines, 1);
    mock_register("sys.nfc",  sys_lines, 2);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   s_builtin, 2, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL,      0, sys_paths,  1),
    };

    s_next_open_supports_resume = true;   /* USER file -- irrelevant here, but harmless */
    mfc_key_source_iter_t it1;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it1, cfgs, 2);
    int pass1 = 0;
    uint32_t committed_offset = 0;
    while (mfc_key_source_iter_next(&it1, &c)) {
        pass1++;
        if (it1.last_system_offset_valid) committed_offset = it1.last_system_offset;
    }
    uint16_t seen_n = it1.seen_n;
    bool     overflowed = it1.seen_overflowed;
    uint8_t  seen_snapshot[MFC_KEY_SOURCE_MAX_SEEN][MFC_KEY_SIZE];
    memcpy(seen_snapshot, it1.seen, sizeof(seen_snapshot));
    mfc_key_source_iter_end(&it1);
    CHECK(pass1 == 5, "pass 1: 2 builtins + 1 User key + 2 System keys = 5 total candidates");
    CHECK(committed_offset == 2U, "the last committed System offset is 2 (both System lines consumed)");

    s_next_open_supports_resume = true;
    mfc_key_source_iter_t it2;
    mfc_key_source_iter_begin_resume(&it2, cfgs, 2, seen_snapshot, seen_n, overflowed, committed_offset);
    int pass2 = 0;
    while (mfc_key_source_iter_next(&it2, &c)) pass2++;
    mfc_key_source_iter_end(&it2);
    CHECK(pass2 == 0, "pass 2, sharing pass 1's full resume state, retries ZERO candidates from any source");
}

/*----------------------------------------------------------------------------*/
/* mfc_key_source_iter_skip_source() -- CENTER "Skip": "skip                 */
/* the CURRENT visible source only", not the raw sources[] entry (built-ins    */
/* and a source's file share one sources[] entry but are two distinct visible  */
/* stages: User -> Built-in -> System).                                        */
/*----------------------------------------------------------------------------*/

static void test_skip_source_from_builtin_lands_on_same_source_file(void)
{
    mock_reset();
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("sys.nfc", sys_lines, 1);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg_acc(MFC_KEY_SRC_SYSTEM, s_builtin, 2, sys_paths, 1, false) };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 1);

    CHECK(mfc_key_source_iter_next(&it, &c) && c.from_builtin, "1st candidate is a builtin (mid built-ins)");

    /* Skip while still walking this source's built-ins: must land on this
     * SAME source's file next, not skip past it entirely. */
    mfc_key_source_iter_skip_source(&it);
    CHECK(mfc_key_source_iter_next(&it, &c), "a candidate remains after skipping the built-ins");
    uint8_t exp[6] = {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA};
    CHECK(c.kind == MFC_KEY_SRC_SYSTEM && !c.from_builtin, "the remaining candidate is SYSTEM's file, not a builtin");
    CHECK(keq(c.key, exp), "the file candidate's key is exactly the one line in sys.nfc");
    CHECK(!mfc_key_source_iter_next(&it, &c), "exhausted after that one file candidate");
    mfc_key_source_iter_end(&it);
}

static void test_skip_source_from_file_advances_to_next_source(void)
{
    mock_reset();
    static const char *user_lines[] = { "111111111111" };
    static const char *sys_lines[]  = { "AAAAAAAAAAAA" };
    mock_register("user.nfc", user_lines, 1);
    mock_register("sys.nfc",  sys_lines, 1);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths,  1),
    };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 2);

    CHECK(mfc_key_source_iter_next(&it, &c) && c.kind == MFC_KEY_SRC_USER, "1st candidate is USER's file");

    /* Skip while in a source's FILE (no builtins here at all): must advance
     * to the next configured source entirely. */
    mfc_key_source_iter_skip_source(&it);
    CHECK(mfc_key_source_iter_next(&it, &c) && c.kind == MFC_KEY_SRC_SYSTEM,
          "skipping USER's file lands on SYSTEM next, not a repeat of USER");
    mfc_key_source_iter_end(&it);
}

static void test_skip_source_from_final_source_exhausts_iterator(void)
{
    mock_reset();
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("sys.nfc", sys_lines, 1);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1) };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 1);
    CHECK(mfc_key_source_iter_next(&it, &c), "1st (only) candidate present");

    /* Skipping the LAST configured source's file: the iterator must report
     * exhausted -- "Skip on the final stage finishes
     * acquisition", achieved here with no special-casing at all. */
    mfc_key_source_iter_skip_source(&it);
    CHECK(!mfc_key_source_iter_next(&it, &c), "skipping the final source exhausts the iterator");
    mfc_key_source_iter_end(&it);
}

static void test_skip_source_preserves_seen_and_recovered_state(void)
{
    mock_reset();
    static const char *user_lines[] = { "111111111111", "222222222222" };
    static const char *sys_lines[]  = { "AAAAAAAAAAAA" };
    mock_register("user.nfc", user_lines, 2);
    mock_register("sys.nfc",  sys_lines, 1);
    static const char *user_paths[] = { "user.nfc" };
    static const char *sys_paths[]  = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[2] = {
        mk_cfg(MFC_KEY_SRC_USER,   NULL, 0, user_paths, 1),
        mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths,  1),
    };

    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 2);
    CHECK(mfc_key_source_iter_next(&it, &c), "1st USER candidate consumed (added to seen[])");
    uint16_t seen_n_before = it.seen_n;

    /* Skip mid-USER (after only 1 of 2 User candidates) -- seen[] must be
     * completely unaffected by the skip itself (only iteration position
     * changes; nothing about already-yielded candidates is undone). */
    mfc_key_source_iter_skip_source(&it);
    CHECK(it.seen_n == seen_n_before, "seen[]/seen_n untouched by skip_source() -- only position moves");

    CHECK(mfc_key_source_iter_next(&it, &c) && c.kind == MFC_KEY_SRC_SYSTEM,
          "skipping USER (mid-file) lands on SYSTEM next -- User's 2nd candidate is genuinely abandoned");
    mfc_key_source_iter_end(&it);
}

static void test_skip_source_null_and_exhausted_are_safe_no_ops(void)
{
    /* NULL iterator: must not crash. */
    mfc_key_source_iter_skip_source(NULL);
    CHECK(1, "mfc_key_source_iter_skip_source(NULL) does not crash");

    /* Already-exhausted iterator: must not crash or misbehave. */
    mock_reset();
    static const char *sys_lines[] = { "AAAAAAAAAAAA" };
    mock_register("sys.nfc", sys_lines, 1);
    static const char *sys_paths[] = { "sys.nfc" };
    mfc_key_source_cfg_t cfgs[1] = { mk_cfg(MFC_KEY_SRC_SYSTEM, NULL, 0, sys_paths, 1) };
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   c;
    mfc_key_source_iter_begin(&it, cfgs, 1);
    CHECK(mfc_key_source_iter_next(&it, &c), "1st candidate present");
    CHECK(!mfc_key_source_iter_next(&it, &c), "exhausted naturally");
    mfc_key_source_iter_skip_source(&it);   /* must not crash on an already-exhausted iterator */
    CHECK(!mfc_key_source_iter_next(&it, &c), "still exhausted after a no-op skip on an exhausted iterator");
    mfc_key_source_iter_end(&it);
}

int main(void)
{
    test_parse_line();
    test_source_order();
    test_canonical_then_fallback();
    test_absent_vs_read_error();
    test_dedup_cross_source_and_within_file();
    test_count_matches_iterator_output();
    test_seed_seen();
    test_large_unbounded_system_source_exact_count();
    test_accumulate_source_overflow_does_not_truncate();
    test_resume_null_matches_plain_begin();
    test_resume_seen_snapshot_skips_already_tried_user_key();
    test_resume_new_user_key_is_tried();
    test_last_system_offset_tracking();
    test_system_resume_exact_offset();
    test_system_resume_at_eof();
    test_system_resume_unsupported_falls_back_to_full_scan();
    test_resume_full_pass_then_second_pass_retries_nothing();
    test_skip_source_from_builtin_lands_on_same_source_file();
    test_skip_source_from_file_advances_to_next_source();
    test_skip_source_from_final_source_exhausts_iterator();
    test_skip_source_preserves_seen_and_recovered_state();
    test_skip_source_null_and_exhausted_are_safe_no_ops();

    printf("mfc_key_source_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
