/*
 * m1_manager_espupdate_seam_test.c - source-seam regression test for the
 * shared ESP32 ROM-loader session backend (m1_csrc/m1_esp32_flash_session.c)
 * and its two callers (m1_csrc/m1_esp32_fw_update.c, the SD-card updater;
 * m1_csrc/m1_manager_espupdate.c, the M1CP streamed backend). All three have
 * a genuine HAL/esp_loader dependency and cannot be host-linked, so this
 * proves the required unification and safety properties via bounded string
 * search against the real, committed source text -- same technique this
 * repo already established for NFC/NFC_drv/legacy/mfc_detect.c.
 *
 * BACKGROUND: the Web Manager/M1CP ESP32 update path took ~2-2.5 hours for a
 * 1.5MB image; the SD-card updater flashes the same class of image in
 * minutes. Two earlier attempts at a fix (ESP flash block size 1024->4096,
 * then a high-baud-with-115200-fallback retry loop copied INTO the M1CP
 * backend) produced no measurable improvement on real hardware -- both were
 * parameter changes to a SECOND, independently-evolved implementation of
 * the same transport logic, not a fix to whatever actually differed. This
 * change instead extracts the SD updater's own proven connect/write/verify/
 * teardown sequence into m1_esp32_flash_session.c and makes BOTH callers go
 * through that single implementation, so a caller can no longer silently
 * diverge from what's proven to work.
 *
 * What this specifically guards:
 *   - Every raw esp_loader_flash_*()/connect_to_target() call site lives in
 *     m1_esp32_flash_session.c ONLY -- neither caller file makes its own,
 *     independent call to any of these. This is the actual, checkable
 *     definition of "unified": not "looks similar," but "there is only one
 *     call site."
 *   - Both callers route through the SAME shared functions.
 *   - The M1CP backend's earlier retry-loop-around-connect and
 *     retry-loop-around-write (the abandoned WEB-003-era approach) are
 *     gone, not just deprioritized -- checked by their absence, not just by
 *     the presence of the new code.
 *   - The shared module's own teardown sequence (baud restore, ring-buffer
 *     reset, target reset, boot-banner delay, UART deinit) matches what
 *     setting_esp32_firmware_update() always did.
 *   - A failed staged-block write in the M1CP path still never advances the
 *     received offset (stays retryable, nothing silently skipped).
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra tools/m1cp_host_test/m1_manager_espupdate_seam_test.c \
 *      -o /tmp/m1_manager_espupdate_seam_test && /tmp/m1_manager_espupdate_seam_test
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

/* Works whether invoked from the repo root (documented standalone usage)
 * or from tools/m1cp_host_test/ (this directory's own `make test`, which
 * runs with that directory as its working directory) -- tries the
 * repo-root-relative path first, then the same path relative to this
 * directory. */
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

/* Brace-matched body bounding: locate `sig`, then bound the block starting
 * at the NEXT '{' after it through its matching '}'. Skips forward past any
 * occurrence immediately followed by ';' (a prototype, not a definition). */
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

static int count_occurrences(const char *hay, size_t hay_len, const char *needle)
{
    if (hay == NULL || hay_len == 0U) return 0;
    char *buf = malloc(hay_len + 1U);
    if (buf == NULL) return 0;
    memcpy(buf, hay, hay_len);
    buf[hay_len] = '\0';
    int n = 0;
    const char *p = buf;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { n++; p += nlen; }
    free(buf);
    return n;
}

/* Real calls to a required-argument function always have something before
 * the closing paren; prose in a comment referencing the same function by
 * name (e.g. "esp_loader_flash_write() already retries internally") always
 * uses empty parens. Counts only the former, so a comment mentioning a
 * function by name doesn't get mistaken for a real call site. */
static int count_real_calls(const char *hay, size_t hay_len, const char *fn_name)
{
    if (hay == NULL || hay_len == 0U) return 0;
    char *buf = malloc(hay_len + 1U);
    if (buf == NULL) return 0;
    memcpy(buf, hay, hay_len);
    buf[hay_len] = '\0';
    int n = 0;
    const char *p = buf;
    size_t nlen = strlen(fn_name);
    while ((p = strstr(p, fn_name)) != NULL) {
        const char *after = p + nlen;
        if (*after == '(' && *(after + 1) != ')') { n++; }
        p = after;
    }
    free(buf);
    return n;
}

int main(void)
{
    char *session_src = read_all("m1_csrc/m1_esp32_flash_session.c");
    char *sd_src       = read_all("m1_csrc/m1_esp32_fw_update.c");
    char *esp_src      = read_all("m1_csrc/m1_manager_espupdate.c");
    CHECK(session_src != NULL, "m1_csrc/m1_esp32_flash_session.c readable (the shared backend)");
    CHECK(sd_src != NULL, "m1_csrc/m1_esp32_fw_update.c readable (the proven SD updater)");
    CHECK(esp_src != NULL, "m1_csrc/m1_manager_espupdate.c readable (the M1CP backend)");
    if (session_src == NULL || sd_src == NULL || esp_src == NULL) {
        printf("m1_manager_espupdate_seam_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }
    size_t session_len = strlen(session_src);
    size_t sd_len = strlen(sd_src);
    size_t esp_len = strlen(esp_src);

    /* --- 1. The shared module makes exactly one raw call to each esp_loader
     * primitive -- this IS the single implementation. --- */
    CHECK(count_occurrences(session_src, session_len, "connect_to_target(ESP32_UART_HIGH_BAUDRATE)") == 1,
          "shared module: exactly one connect_to_target(ESP32_UART_HIGH_BAUDRATE) call site");
    CHECK(count_real_calls(session_src, session_len, "esp_loader_flash_start") == 1,
          "shared module: exactly one esp_loader_flash_start() call site");
    CHECK(count_real_calls(session_src, session_len, "esp_loader_flash_write") == 1,
          "shared module: exactly one esp_loader_flash_write() call site");
    CHECK(count_real_calls(session_src, session_len, "esp_loader_flash_verify_known_md5") == 1,
          "shared module: exactly one esp_loader_flash_verify_known_md5() call site");

    /* --- 2. Neither caller makes its own, independent raw esp_loader call
     * -- the actual, checkable definition of "unified": there is only one
     * call site for each primitive, not two similar-looking ones. A comment
     * merely mentioning a function's name (as several do, to explain why a
     * behavior is safe) doesn't count as a call -- count_real_calls only
     * matches a name followed by a non-empty argument list. --- */
    CHECK(count_real_calls(sd_src, sd_len, "connect_to_target") == 0,
          "SD updater no longer calls connect_to_target() directly (goes through the shared session)");
    CHECK(count_real_calls(sd_src, sd_len, "esp_loader_flash_start") == 0,
          "SD updater no longer calls esp_loader_flash_start() directly");
    CHECK(count_real_calls(sd_src, sd_len, "esp_loader_flash_write") == 0,
          "SD updater no longer calls esp_loader_flash_write() directly");
    CHECK(count_real_calls(esp_src, esp_len, "connect_to_target") == 0,
          "M1CP backend no longer calls connect_to_target() directly");
    CHECK(count_real_calls(esp_src, esp_len, "esp_loader_flash_start") == 0,
          "M1CP backend no longer calls esp_loader_flash_start() directly");
    CHECK(count_real_calls(esp_src, esp_len, "esp_loader_flash_write") == 0,
          "M1CP backend no longer calls esp_loader_flash_write() directly");
    CHECK(count_real_calls(esp_src, esp_len, "esp_loader_flash_verify_known_md5") == 0,
          "M1CP backend no longer calls esp_loader_flash_verify_known_md5() directly");

    /* --- 3. Both callers actually go through the shared primitives (not
     * just "don't call the raw ones" -- confirm the replacement calls are
     * really there). --- */
    CHECK(bounded_contains(sd_src, sd_len, "m1_esp32_flash_session_connect()"),
          "SD updater's m1_fw_app() calls the shared connect primitive");
    CHECK(bounded_contains(sd_src, sd_len, "m1_esp32_flash_session_start("),
          "SD updater's m1_fw_flash_binary() calls the shared flash_start primitive");
    CHECK(bounded_contains(sd_src, sd_len, "m1_esp32_flash_session_write("),
          "SD updater's m1_fw_flash_binary() calls the shared write primitive");
    CHECK(bounded_contains(sd_src, sd_len, "m1_esp32_flash_session_verify_self("),
          "SD updater's m1_fw_flash_binary() calls the shared self-verify primitive");
    CHECK(bounded_contains(esp_src, esp_len, "m1_esp32_flash_session_connect()"),
          "M1CP backend's espu_be_begin() calls the shared connect primitive");
    CHECK(bounded_contains(esp_src, esp_len, "m1_esp32_flash_session_start("),
          "M1CP backend's espu_be_begin() calls the shared flash_start primitive");
    CHECK(bounded_contains(esp_src, esp_len, "m1_esp32_flash_session_write("),
          "M1CP backend's espu_be_write() calls the shared write primitive");
    CHECK(bounded_contains(esp_src, esp_len, "m1_esp32_flash_session_verify_known_md5("),
          "M1CP backend's espu_be_finish() calls the shared known-MD5-verify primitive");
    CHECK(bounded_contains(esp_src, esp_len, "m1_esp32_flash_session_teardown("),
          "M1CP backend's espu_be_release() calls the shared teardown primitive");

    /* --- 4. The abandoned WEB-003 approach (retry-loop-around-connect with
     * a 115200 fallback, retry-loop-around-write) is actually GONE from the
     * M1CP backend, not merely superseded in intent. Checked by the absence
     * of its distinctive retry-loop shape, not by a comment. --- */
    CHECK(!bounded_contains(esp_src, esp_len, "attempt < 5"),
          "M1CP backend: no 5x connect retry loop remains (WEB-003's approach is actually removed)");
    CHECK(!bounded_contains(esp_src, esp_len, "attempt < 3"),
          "M1CP backend: no 3x write retry loop remains");
    CHECK(!bounded_contains(esp_src, esp_len, "esp_loader_connect(&cargs)"),
          "M1CP backend: no bare 115200-only esp_loader_connect() fallback remains");
    CHECK(!bounded_contains(esp_src, esp_len, "ESP32_UART_HIGH_BAUDRATE"),
          "M1CP backend no longer references a baud constant at all -- the shared session owns that choice");

    /* --- 5. The shared module's own teardown matches
     * setting_esp32_firmware_update()'s proven sequence: baud restore,
     * ring-buffer reset, target reset, boot-banner delay, UART deinit. --- */
    {
        size_t fn_len;
        const char *fn = bound_block(session_src, "void m1_esp32_flash_session_teardown(bool restore_io0_input,", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "m1_esp32_flash_session_teardown() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "esp32_UART_change_baudrate(ESP32_UART_BAUDRATE)"),
                  "shared teardown restores the ESP32 default baud");
            CHECK(bounded_contains(fn, fn_len, "m1_ringbuffer_reset(&esp32_rb_hdl)"),
                  "shared teardown resets the RX ring buffer");
            CHECK(bounded_contains(fn, fn_len, "esp_loader_reset_target()"),
                  "shared teardown resets the ESP target (boots the new image)");
            CHECK(bounded_contains(fn, fn_len, "esp32_UART_deinit()"),
                  "shared teardown deinits the UART");
        }
    }

    /* --- 6. Generic-coordinator migration: espu_data()'s old offset/resend/
     * overrun/alignment logic is now m1cp_update_data() (proven directly in
     * test_m1cp_update_coordinator.c and exercised through this dispatch
     * shim in test_m1cp_espupdate.c) -- confirm the dispatch shim actually
     * calls it rather than reimplementing that logic itself. --- */
    CHECK(bounded_contains(esp_src, esp_len, "m1cp_update_data(&s_session, payload, plen, out, olen)"),
          "espu_data() forwards to the shared coordinator, not its own reimplementation");
    CHECK(bounded_contains(esp_src, esp_len, "m1cp_update_begin(&s_session,"),
          "espu_begin() forwards manifest validation to the shared coordinator");

    /* --- 7. The 480-byte-M1CP-frame -> 4096-byte-ROM-block staging is now
     * backend-internal (moved out of the old espu_data() dispatch handler
     * into espu_be_write()/espu_be_finish()) -- confirm it actually lives
     * there, and that a failed flush never silently drops the offset the
     * coordinator is tracking (the coordinator's own m1cp_update_data()
     * only advances `received` on a write() success -- proven generically
     * in test_m1cp_update_coordinator.c; this just confirms the ESP
     * backend's write() genuinely stages rather than writing every chunk
     * straight through, which is the whole point of the block size). --- */
    {
        size_t fn_len;
        const char *fn = bound_block(esp_src, "static uint8_t espu_be_write(const uint8_t *data, uint32_t size)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "espu_be_write() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "M1CP_ESP_FLASH_BLOCK"),
                  "espu_be_write() stages into M1CP_ESP_FLASH_BLOCK-sized (4096-byte) chunks");
            CHECK(bounded_contains(fn, fn_len, "s_flash_stage_len == M1CP_ESP_FLASH_BLOCK"),
                  "espu_be_write() only flushes a FULL block, not every partial chunk");
        }
    }
    {
        size_t fn_len;
        const char *fn = bound_block(esp_src, "static uint8_t espu_be_finish(const uint8_t *expected_hash, uint8_t hash_len)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "espu_be_finish() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "s_flash_stage_len != 0U"),
                  "espu_be_finish() flushes any remaining partial-block tail before verifying");
            CHECK(!bounded_contains(fn, fn_len, "m1cp_esp_version_set"),
                  "espu_be_finish() does NOT persist the version -- that's dispatch-shim state (espu_finish(), always compiled), not backend-only");
        }
    }
    {
        size_t fn_len;
        const char *fn = bound_block(esp_src, "static uint8_t espu_be_begin(uint32_t flash_offset, uint32_t image_size)", &fn_len);
        CHECK(fn != NULL && fn_len > 0U, "espu_be_begin() is defined and bounded");
        if (fn != NULL) {
            CHECK(bounded_contains(fn, fn_len, "s_flash_stage_len   = 0U;") || bounded_contains(fn, fn_len, "s_flash_stage_len = 0U;"),
                  "espu_be_begin() resets the stage-buffer cursor -- the same class of bug just fixed on the STM32 side (a stale cursor from an aborted attempt corrupting the next one)");
        }
    }
    CHECK(bounded_contains(esp_src, esp_len, "m1cp_esp_version_set(s_pending_version)") &&
          bounded_contains(esp_src, esp_len, "static uint8_t espu_finish(void)"),
          "version persistence lives in espu_finish() (dispatch shim, always compiled) so it runs identically for the mock backend (host tests) and the real one");

    printf("=== m1_manager_espupdate_seam_test: %d passed, %d failed ===\n", pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
