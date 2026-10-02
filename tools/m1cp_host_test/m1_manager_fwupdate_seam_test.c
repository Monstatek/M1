/*
 * m1_manager_fwupdate_seam_test.c - source-seam regression test for the
 * device-only STM32 self-update backend in m1_csrc/m1_manager_fwupdate.c
 * (the `#ifndef M1CP_HOST_TEST` section: fwu_be_ready/begin/write/finish/
 * activate/abort) and the underlying engine in m1_csrc/m1_fw_update_bl.c
 * that it wraps. Both have a genuine HAL/flash dependency and cannot be
 * host-linked, so this proves the required properties via bounded string
 * search against the real, committed source text -- same technique this
 * repo already established (see m1_manager_espupdate_seam_test.c, the
 * ESP32 backend's own version of this same test).
 *
 * What this specifically guards:
 *   - bl_flash_stream_begin() actually calls the new bl_flash_stream_reset()
 *     -- the fix for a real, confirmed bug: bl_flash_binary()'s write
 *     cursor (init_done/write_acc/flash_add) was function-local `static`
 *     and only ever cleared itself on a SUCCESSFUL finish(); a BEGIN issued
 *     after an aborted or failed-before-FINISH transfer resumed writing at
 *     the STALE mid-image cursor from the abandoned attempt, into a
 *     freshly-erased bank.
 *   - fwu_be_abort() actually calls bl_flash_stream_abort() -- before this
 *     hook existed, nothing released/re-locked flash or reset the cursor on
 *     a cancel/genuine-BAD_ARG/begin-failure, leaving flash unlocked
 *     indefinitely and the cursor stale for the next BEGIN.
 *   - fwu_be_finish() always runs the device's own hardware-CRC32 self-check
 *     (bl_flash_stream_finish(), UNCHANGED, proven, hardware-validated
 *     engine) unconditionally -- never skipped or made conditional on
 *     whether a client MD5 was supplied -- and only ADDITIONALLY checks an
 *     MD5 when one was.
 *   - The dispatch shim (fwu_begin/data/finish/activate/abort) forwards to
 *     the shared m1_manager_update_coordinator rather than reimplementing
 *     any of that state machine itself.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra tools/m1cp_host_test/m1_manager_fwupdate_seam_test.c \
 *      -o /tmp/m1_manager_fwupdate_seam_test && /tmp/m1_manager_fwupdate_seam_test
 * (also wired into `make test` in this directory, invoked with this
 * directory as its own working directory -- see the relative paths below).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass_count;
static int fail_count;
#define CHECK(c, m) do { \
    if (c) { pass_count++; } \
    else { fail_count++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } \
} while (0)

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        char alt[512];
        snprintf(alt, sizeof(alt), "../../%s", path);
        f = fopen(alt, "rb");
    }
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = malloc((size_t)n + 1U);
    if (s == NULL) { fclose(f); return NULL; }
    if (fread(s, 1U, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = '\0';
    fclose(f);
    return s;
}

static const char *bound_block(const char *src, const char *sig, size_t *out_len)
{
    const char *search_from = src;
    for (;;) {
        const char *start = strstr(search_from, sig);
        if (start == NULL) { *out_len = 0U; return NULL; }
        const char *after = start + strlen(sig);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') after++;
        if (*after == ';') { search_from = start + strlen(sig); continue; }
        const char *open_brace = strchr(start, '{');
        if (open_brace == NULL) { *out_len = 0U; return NULL; }
        int depth = 0;
        const char *p = open_brace;
        const char *end = NULL;
        for (; *p != '\0'; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { end = p; break; } }
        }
        *out_len = (end != NULL) ? (size_t)(end - start + 1) : 0U;
        return start;
    }
}

static int bounded_contains(const char *hay, size_t hay_len, const char *needle)
{
    if (hay == NULL || hay_len == 0U) return 0;
    char *buf = malloc(hay_len + 1U);
    if (buf == NULL) return 0;
    memcpy(buf, hay, hay_len);
    buf[hay_len] = '\0';
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

int main(void)
{
    char *bl_src  = read_all("m1_csrc/m1_fw_update_bl.c");
    char *fwu_src = read_all("m1_csrc/m1_manager_fwupdate.c");
    CHECK(bl_src != NULL, "m1_csrc/m1_fw_update_bl.c readable (the hardware-validated flash engine)");
    CHECK(fwu_src != NULL, "m1_csrc/m1_manager_fwupdate.c readable (the STM32 M1CP dispatch shim)");
    if (bl_src == NULL || fwu_src == NULL) {
        printf("m1_manager_fwupdate_seam_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }
    size_t bl_len = strlen(bl_src);
    size_t fwu_len = strlen(fwu_src);

    /* --- 1. The cursor-reset bug fix: bl_flash_stream_begin() actually
     * calls bl_flash_stream_reset() -- not just that bl_flash_stream_reset()
     * exists (that alone proves nothing was wired up). --- */
    {
        size_t fn_len;
        const char *fn = bound_block(bl_src, "uint8_t bl_flash_stream_begin(uint32_t image_size)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "bl_flash_stream_begin() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "bl_flash_stream_reset();"),
                  "bl_flash_stream_begin() resets the write cursor -- fixes: a BEGIN after an aborted transfer no longer resumes at a stale mid-image offset");
        }
    }
    /* The reset function itself must actually clear all three cursor
     * variables (init_done/write_acc/flash_add), not just one. */
    {
        size_t fn_len;
        const char *fn = bound_block(bl_src, "void bl_flash_stream_reset(void)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "bl_flash_stream_reset() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "s_bl_init_done = false;"), "bl_flash_stream_reset() clears the init-done cursor flag");
            CHECK(bounded_contains(fn, fn_len, "s_bl_write_acc = 0;"), "bl_flash_stream_reset() clears the write-offset accumulator");
            CHECK(bounded_contains(fn, fn_len, "s_bl_flash_add = NULL;"), "bl_flash_stream_reset() clears the destination pointer");
        }
    }
    /* The three cursor variables must be FILE-scope (so bl_flash_stream_reset
     * can reach them from outside bl_flash_binary()), not function-local
     * `static` inside bl_flash_binary() the way the original bug had them. */
    CHECK(bounded_contains(bl_src, bl_len, "static uint32_t s_bl_write_acc;") &&
          bounded_contains(bl_src, bl_len, "static uint8_t *s_bl_flash_add;") &&
          bounded_contains(bl_src, bl_len, "static bool     s_bl_init_done = false;"),
          "the write-cursor variables are file-scope statics, not function-local ones bl_flash_stream_reset() couldn't reach");

    /* --- 2. The abort/cleanup bug fix: fwu_be_abort() actually calls the
     * new bl_flash_stream_abort(), which itself resets the cursor AND
     * re-locks flash. Before this, ABORT/a genuine BAD_ARG/a begin()
     * failure released nothing at all on the STM32 path (unlike the ESP
     * path, which always had this). --- */
    {
        size_t fn_len;
        const char *fn = bound_block(fwu_src, "static void fwu_be_abort(void)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "fwu_be_abort() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "bl_flash_stream_abort();"),
                  "fwu_be_abort() calls the new bl_flash_stream_abort() (reset cursor + re-lock flash)");
        }
    }
    {
        size_t fn_len;
        const char *fn = bound_block(bl_src, "void bl_flash_stream_abort(void)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "bl_flash_stream_abort() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "bl_flash_stream_reset();"), "bl_flash_stream_abort() resets the write cursor");
            CHECK(bounded_contains(fn, fn_len, "bl_flash_if_deinit();"), "bl_flash_stream_abort() re-locks flash (HAL_FLASH_Lock, via the file-private bl_flash_if_deinit)");
        }
    }
    /* The backend struct actually wires abort in (a NULL here would mean
     * the coordinator's abort()/reset()/begin-failure paths call nothing). */
    CHECK(bounded_contains(fwu_src, fwu_len, "fwu_be_ready, fwu_be_begin, fwu_be_write, fwu_be_finish, fwu_be_activate, fwu_be_abort"),
          "the STM32 backend struct wires fwu_be_abort in (not NULL)");

    /* --- 3. Integrity: the device's own hardware-CRC32 self-check
     * (bl_flash_stream_finish(), the proven, hardware-validated engine)
     * runs UNCONDITIONALLY -- never skipped, never gated on whether a
     * client hash was supplied. An MD5 check, when a client did supply
     * one, is ADDITIVE on top, not a replacement. --- */
    {
        size_t fn_len;
        const char *fn = bound_block(fwu_src, "static uint8_t fwu_be_finish(const uint8_t *expected_hash, uint8_t hash_len)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "fwu_be_finish() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "bl_flash_stream_finish();"),
                  "fwu_be_finish() always calls the unmodified, hardware-validated bl_flash_stream_finish() (CRC32 self-check)");
            const char *crc_call = strstr(fn, "bl_flash_stream_finish();");
            const char *hash_len_check = strstr(fn, "hash_len == 16U");
            CHECK(crc_call != NULL && hash_len_check != NULL,
                  "the CRC32 call and the additive MD5 check both exist in finish()");
            /* The CRC call must not be inside the hash_len-gated branch --
             * i.e. it must appear BEFORE the hash_len check textually,
             * since this function runs the CRC unconditionally first, then
             * only conditionally checks the MD5 on top. */
            CHECK(crc_call != NULL && hash_len_check != NULL && crc_call < hash_len_check,
                  "the CRC32 self-check runs unconditionally, BEFORE the optional client-MD5 check -- never gated behind it");
        }
    }
    CHECK(bounded_contains(bl_src, bl_len, "bl_crc_check("),
          "bl_flash_stream_finish()'s underlying engine (bl_flash_binary -> bl_crc_check) is unmodified and still present");

    /* --- 4. The dispatch shim forwards to the shared coordinator rather
     * than reimplementing the state machine (mirrors the ESP seam test's
     * own equivalent check). --- */
    CHECK(bounded_contains(fwu_src, fwu_len, "m1cp_update_data(&s_session, payload, plen, out, olen)"),
          "fwu_data() forwards to the shared coordinator, not its own reimplementation");
    CHECK(bounded_contains(fwu_src, fwu_len, "m1cp_update_begin(&s_session,"),
          "fwu_begin() forwards manifest validation to the shared coordinator");
    CHECK(bounded_contains(fwu_src, fwu_len, "m1cp_update_finish(&s_session)"),
          "fwu_finish() forwards to the shared coordinator");
    CHECK(bounded_contains(fwu_src, fwu_len, "m1cp_update_activate(&s_session,"),
          "fwu_activate() forwards to the shared coordinator");

    /* --- 5. last_md5 is populated by the backend's finish(), matching the
     * ESP path's own equivalent diagnostic exactly, and only when a client
     * hash was actually supplied (hash_len == 16). --- */
    {
        size_t fn_len;
        const char *fn = bound_block(fwu_src, "static uint8_t fwu_be_finish(const uint8_t *expected_hash, uint8_t hash_len)", &fn_len);
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "s_last_md5_valid = true;"),
                  "fwu_be_finish() populates the last_md5 diagnostic when a client hash was supplied");
        }
    }

    printf("=== m1_manager_fwupdate_seam_test: %d passed, %d failed ===\n", pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
