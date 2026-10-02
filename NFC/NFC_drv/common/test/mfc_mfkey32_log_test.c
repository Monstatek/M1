/*
 * mfc_mfkey32_log_test.c - host tests for the Extract Keys nonce-pair
 * export (mfc_mfkey32_log.c, Phase 4): exact Flipper mfkey32.log line-format
 * compatibility, append/reopen behavior, deduplication, interrupted/failed
 * writes, wrong-card and stale-session isolation, and never persisting an
 * incomplete pair.
 *
 * Build & run:
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/mfc_capture.c \
 *      NFC/NFC_drv/common/mfc_mfkey32_log.c \
 *      NFC/NFC_drv/common/test/mfc_mfkey32_log_test.c \
 *      -o /tmp/mfc_mfkey32_log_test && /tmp/mfc_mfkey32_log_test
 */
#include "mfc_capture.h"
#include "mfc_mfkey32_log.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

#define CUID 0xDEADBEEFu

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

/* ---- Mock write sink: appends to an in-memory buffer, with optional
 * scripted failure (fail_after_calls == 0 means never fail; N means the
 * Nth call onward returns false, simulating a full card / SD ejection /
 * a short write partway through a session). ---- */
typedef struct {
    char     buf[4096];
    size_t   len;
    int      call_count;
    int      fail_after_calls;   /* 0 = never fail */
} mock_sink_t;

static void sink_reset(mock_sink_t *s, int fail_after_calls)
{
    memset(s, 0, sizeof(*s));
    s->fail_after_calls = fail_after_calls;
}

static bool sink_write(void *ctx, const uint8_t *bytes, size_t len)
{
    mock_sink_t *s = (mock_sink_t *)ctx;
    s->call_count++;
    if ((s->fail_after_calls != 0) && (s->call_count >= s->fail_after_calls)) {
        return false;   /* simulated SD-full / write failure -- nothing appended */
    }
    if ((s->len + len) > sizeof(s->buf)) {
        return false;
    }
    memcpy(s->buf + s->len, bytes, len);
    s->len += len;
    return true;
}

int main(void)
{
    /* -------- exact Flipper mfkey32.log format compatibility -------- */
    {
        mfc_pair_t p;
        memset(&p, 0, sizeof(p));
        p.is_filled = true;
        p.cuid = 0xDEADBEEFu;
        p.sector = 3;
        p.key_type = MFC_KEY_A;
        p.nt0 = 0x11111111u; p.nr0 = 0x22222222u; p.ar0 = 0x33333333u;
        p.nt1 = 0x44444444u; p.nr1 = 0x55555555u; p.ar1 = 0x66666666u;

        char line[MFC_MFKEY32_LOG_LINE_MAX];
        size_t n = mfc_mfkey32_log_format_line(line, sizeof(line), &p);
        const char *expect =
            "Sec 3 key A cuid deadbeef nt0 11111111 nr0 22222222 ar0 33333333 "
            "nt1 44444444 nr1 55555555 ar1 66666666\n";
        CHECK(n == strlen(expect), "formatted line length matches the Flipper-format vector");
        CHECK(strcmp(line, expect) == 0,
              "formatted line is byte-for-byte identical to Flipper's own "
              "mfkey32_logger.c printf format (Sec %d key %c cuid ... nt0..ar1 %08lx, \\n)");

        p.key_type = MFC_KEY_B;
        n = mfc_mfkey32_log_format_line(line, sizeof(line), &p);
        CHECK((n > 0U) && (strstr(line, "key B ") != NULL), "Key B renders as 'B', not 'A' or '1'");

        /* worst-case field widths (sector max 255, all-Fs nonces) must still fit */
        p.sector = 255;
        p.nt0 = p.nr0 = p.ar0 = p.nt1 = p.nr1 = p.ar1 = 0xFFFFFFFFu;
        n = mfc_mfkey32_log_format_line(line, sizeof(line), &p);
        CHECK(n > 0U && n < MFC_MFKEY32_LOG_LINE_MAX, "worst-case line fits the sized buffer");
        CHECK(line[n - 1U] == '\n', "line is newline-terminated like Flipper's log");

        /* an incomplete pair must never be formatted -- nothing valid to emit */
        mfc_pair_t incomplete;
        memset(&incomplete, 0, sizeof(incomplete));
        incomplete.is_filled = false;
        CHECK(mfc_mfkey32_log_format_line(line, sizeof(line), &incomplete) == 0U,
              "an incomplete (is_filled=false) pair is never formatted");
        CHECK(mfc_mfkey32_log_format_line(NULL, sizeof(line), &p) == 0U, "NULL buf rejected");
        CHECK(mfc_mfkey32_log_format_line(line, 0U, &p) == 0U, "zero buflen rejected");
        CHECK(mfc_mfkey32_log_format_line(line, sizeof(line), NULL) == 0U, "NULL pair rejected");
        {
            char tiny[8];
            CHECK(mfc_mfkey32_log_format_line(tiny, sizeof(tiny), &p) == 0U,
                  "a buffer too small to hold the whole line yields 0, never a truncated line");
        }
    }

    /* -------- never persists an incomplete pair (via the real capture store) -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t partial = make_ctx(CUID, 2, MFC_KEY_A, 0xAAu, 0xBBu, 0xCCu, 1000U);
        CHECK(mfc_capture_add(&partial) == MFC_CAP_ADDED_PARTIAL, "one partial, no pair yet");

        mock_sink_t sink; sink_reset(&sink, 0);
        mfc_mfkey32_log_t log;
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID, mfc_capture_session_gen());
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n == 0, "flush_new writes nothing while zero pairs are complete");
        CHECK(sink.len == 0U, "no bytes reach the sink for an incomplete-only session");
    }

    /* -------- basic write + dedup across repeated flush_new calls -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 5, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 5, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair 1");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair 1");

        mock_sink_t sink; sink_reset(&sink, 0);
        mfc_mfkey32_log_t log;
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID, mfc_capture_session_gen());

        int n1 = mfc_mfkey32_log_flush_new(&log);
        CHECK(n1 == 1, "first flush_new writes exactly the one completed pair");
        size_t len_after_first = sink.len;
        CHECK(len_after_first > 0U, "bytes actually reached the sink");

        int n2 = mfc_mfkey32_log_flush_new(&log);
        CHECK(n2 == 0, "second flush_new with no new pairs writes nothing new (dedup)");
        CHECK(sink.len == len_after_first, "no duplicate bytes appended on a no-op flush");
        CHECK(mfc_mfkey32_log_written_count(&log) == 1U, "written_count reflects exactly one export");

        /* a second, independently completed pair (different sector) is exported once new */
        mfc_auth_ctx_t b0 = make_ctx(CUID, 6, MFC_KEY_B, 0x10u, 0x20u, 0x30u, 1000U);
        mfc_auth_ctx_t b1 = make_ctx(CUID, 6, MFC_KEY_B, 0x40u, 0x50u, 0x60u, 1000U);
        CHECK(mfc_capture_add(&b0) == MFC_CAP_ADDED_PARTIAL, "open pair 2");
        CHECK(mfc_capture_add(&b1) == MFC_CAP_COMPLETED_PAIR, "complete pair 2");

        int n3 = mfc_mfkey32_log_flush_new(&log);
        CHECK(n3 == 1, "flush_new exports exactly the newly-completed second pair, not a re-export of the first");
        CHECK(mfc_mfkey32_log_written_count(&log) == 2U, "written_count now reflects both exports");
    }

    /* -------- append/reopen: a fresh log session over the SAME underlying
     * sink (simulating the file being reopened in append mode across a
     * save-then-later-resave within one Detect Reader session) re-exports
     * nothing that a PRIOR session already wrote, as long as the caller
     * still tracks what was already flushed -- and correctly starts fresh
     * (re-exports everything complete so far) when it does not, matching
     * Flipper's own always-append/no-cross-session-dedup behavior. -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 1, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 1, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair");
        uint32_t gen = mfc_capture_session_gen();

        mock_sink_t sink; sink_reset(&sink, 0);   /* one persistent "file" across two log opens */

        mfc_mfkey32_log_t log1;
        mfc_mfkey32_log_begin(&log1, sink_write, &sink, CUID, gen);
        CHECK(mfc_mfkey32_log_flush_new(&log1) == 1, "session 1 exports the one completed pair");
        size_t len_after_session1 = sink.len;

        /* "reopen" -- a brand new export session bound to the SAME still-current
         * capture generation (e.g. the app restarted mid-Detect-Reader and
         * re-created its export context, but the capture store itself, and
         * therefore the completed pair, is unchanged). */
        mfc_mfkey32_log_t log2;
        mfc_mfkey32_log_begin(&log2, sink_write, &sink, CUID, gen);
        int n = mfc_mfkey32_log_flush_new(&log2);
        CHECK(n == 1, "a fresh export session with its own dedup state re-appends the still-complete "
                      "pair once -- matches Flipper's own always-append, no cross-session dedup "
                      "(mfkey32_logger.c: FSOM_OPEN_APPEND, no persisted dedup)");
        CHECK(sink.len == 2U * len_after_session1, "the reopened session appended a second identical "
                                                    "line after the first, not in place of it");
    }

    /* -------- wrong-card isolation -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 4, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 4, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair");
        uint32_t gen = mfc_capture_session_gen();

        mock_sink_t sink; sink_reset(&sink, 0);
        mfc_mfkey32_log_t log;
        /* opened bound to a DIFFERENT cuid than the capture store's actual session */
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID + 1U, gen);
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n < 0, "flush_new refuses when bound cuid != mfc_capture's current session cuid");
        CHECK(mfc_mfkey32_log_status(&log) == MFC_MFLOG_WRONG_CARD, "status reports WRONG_CARD");
        CHECK(sink.len == 0U, "no bytes written for a wrong-card export attempt");
    }

    /* -------- stale-session isolation (field-loss/STOP/reset moved the generation on) -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 8, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 8, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair");
        uint32_t stale_gen = mfc_capture_session_gen();

        mfc_capture_invalidate_incomplete();   /* e.g. field loss right after -- bumps gen */

        mock_sink_t sink; sink_reset(&sink, 0);
        mfc_mfkey32_log_t log;
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID, stale_gen);
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n < 0, "flush_new refuses once the capture store's generation has moved on");
        CHECK(mfc_mfkey32_log_status(&log) == MFC_MFLOG_STALE_SESSION, "status reports STALE_SESSION");
        CHECK(sink.len == 0U, "no bytes written for a stale-generation export attempt "
                              "-- an already-completed pair from BEFORE the invalidating event "
                              "cannot be silently combined with whatever comes after it");
    }

    /* -------- interrupted / failed writes (SD full or ejected mid-export) -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 1, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 1, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        mfc_auth_ctx_t b0 = make_ctx(CUID, 2, MFC_KEY_A, 0x11u, 0x12u, 0x13u, 1000U);
        mfc_auth_ctx_t b1 = make_ctx(CUID, 2, MFC_KEY_A, 0x14u, 0x15u, 0x16u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair 1");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair 1");
        CHECK(mfc_capture_add(&b0) == MFC_CAP_ADDED_PARTIAL, "open pair 2");
        CHECK(mfc_capture_add(&b1) == MFC_CAP_COMPLETED_PAIR, "complete pair 2");

        /* sink fails starting on its FIRST call: simulates a full card / SD
         * removed / write error on the very first write attempt (covers both
         * "interrupted writes" and "SD failure" requirements). */
        mock_sink_t sink; sink_reset(&sink, 1);
        mfc_mfkey32_log_t log;
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID, mfc_capture_session_gen());
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n < 0, "flush_new reports failure when the write callback fails");
        CHECK(mfc_mfkey32_log_status(&log) == MFC_MFLOG_WRITE_FAILED, "status reports WRITE_FAILED");
        CHECK(mfc_mfkey32_log_written_count(&log) == 0U, "the failing pair is NOT marked written");
        CHECK(sink.len == 0U, "no partial line content was accepted");

        /* the failure is sticky: even if the sink would now succeed, this
         * export session refuses further attempts until a fresh begin(). */
        sink.fail_after_calls = 0;   /* "card reinserted" -- would succeed now */
        int n2 = mfc_mfkey32_log_flush_new(&log);
        CHECK(n2 < 0 && mfc_mfkey32_log_status(&log) == MFC_MFLOG_WRITE_FAILED,
              "a latched write failure blocks further writes until mfc_mfkey32_log_begin() again");
        CHECK(sink.len == 0U, "still nothing written while the failure is latched");

        /* a fresh begin() (matching a real reopen/retry of the export) can
         * now export both completed pairs, since neither was ever marked
         * written by the failed session. */
        mfc_mfkey32_log_t retry;
        mfc_mfkey32_log_begin(&retry, sink_write, &sink, CUID, mfc_capture_session_gen());
        int n3 = mfc_mfkey32_log_flush_new(&retry);
        CHECK(n3 == 2, "a fresh export session after re-begin() exports BOTH completed pairs "
                       "-- nothing was lost by the earlier failed attempt");
    }

    /* -------- a write failure partway through (pair 1 OK, pair 2 fails)
     * leaves pair 1 durably written and only pair 2 retryable. -------- */
    {
        mfc_capture_reset(CUID);
        mfc_auth_ctx_t a0 = make_ctx(CUID, 1, MFC_KEY_A, 0x01u, 0x02u, 0x03u, 1000U);
        mfc_auth_ctx_t a1 = make_ctx(CUID, 1, MFC_KEY_A, 0x04u, 0x05u, 0x06u, 1000U);
        mfc_auth_ctx_t b0 = make_ctx(CUID, 2, MFC_KEY_A, 0x11u, 0x12u, 0x13u, 1000U);
        mfc_auth_ctx_t b1 = make_ctx(CUID, 2, MFC_KEY_A, 0x14u, 0x15u, 0x16u, 1000U);
        CHECK(mfc_capture_add(&a0) == MFC_CAP_ADDED_PARTIAL, "open pair 1");
        CHECK(mfc_capture_add(&a1) == MFC_CAP_COMPLETED_PAIR, "complete pair 1");
        CHECK(mfc_capture_add(&b0) == MFC_CAP_ADDED_PARTIAL, "open pair 2");
        CHECK(mfc_capture_add(&b1) == MFC_CAP_COMPLETED_PAIR, "complete pair 2");

        mock_sink_t sink; sink_reset(&sink, 2);   /* 1st call (pair 1's line) OK, 2nd call fails */
        mfc_mfkey32_log_t log;
        mfc_mfkey32_log_begin(&log, sink_write, &sink, CUID, mfc_capture_session_gen());
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n < 0, "flush_new reports failure once the second pair's write fails");
        CHECK(mfc_mfkey32_log_written_count(&log) == 1U, "the first pair, already durably written, stays marked written");
        CHECK(sink.len > 0U, "the first pair's line is preserved in the sink, not rolled back");
    }

    /* -------- flush_new before begin() -------- */
    {
        mfc_mfkey32_log_t log;
        memset(&log, 0, sizeof(log));   /* never begin()'d */
        int n = mfc_mfkey32_log_flush_new(&log);
        CHECK(n < 0, "flush_new on a never-opened log context fails cleanly");
        CHECK(mfc_mfkey32_log_status(&log) == MFC_MFLOG_NOT_OPEN, "status reports NOT_OPEN");
    }

    printf("=== mfc_mfkey32_log: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
