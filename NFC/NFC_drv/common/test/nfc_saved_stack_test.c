/* Regression guard for the NFC2-005 hardware finding: NFC > Saved
 * immediately reset the M1.
 *
 * Root cause (confirmed via the linked ELF's real disassembly, not
 * inferred): nfc_saved() (m1_csrc/m1_nfc.c) declared and initialized an
 * unused stack-local nfc_run_ctx_t:
 *
 *     nfc_run_ctx_t ctx;
 *     nfc_run_ctx_init(&ctx);
 *
 * -- never referenced again; saved-file loading uses the global NFC
 * context, not a local one. The compiler reserved that struct's full frame
 * on entry (0x48D4 = 18,644 bytes, per `sub.w sp, sp, #0x4880` + `sub sp,
 * #0x54` in the pre-fix disassembly), which alone overflows the calling
 * subfunc_handler_task's M1_TASK_STACK_SIZE_4096 stack (4096 32-bit words =
 * 16,384 bytes) before the function body even runs.
 *
 * The fix removes the unused declaration/init entirely -- no replacement
 * initialization of the global context (that would be a behavior change at
 * browser entry the fix wasn't asked to make), and no task-stack increase
 * (that would conceal the dead allocation rather than remove it). Post-fix,
 * the real linked ELF's nfc_saved() prologue is `push {r3, lr}` with no
 * `sub sp, sp, #...` at all -- confirmed by direct arm-none-eabi-objdump
 * inspection of /tmp/arm_off/MonstaTek_M1_v0800.elf during this fix, not
 * assumed.
 *
 * This test is a narrow, host-only, portable regression guard: it cannot
 * itself rebuild and disassemble the ARM ELF (that needs the ARM toolchain,
 * not available in every host-test environment), so it instead proves the
 * one fact an ARM rebuild would otherwise have to catch by hand every time
 * -- that nfc_saved()'s real, current source no longer declares ANY local
 * variable of type nfc_run_ctx_t (the exact struct whose size caused the
 * overflow) anywhere in its body. A future change that reintroduces
 * `nfc_run_ctx_t <anything>;` inside this function -- with or without
 * nfc_run_ctx_init() -- fails this test immediately, without needing a
 * hardware reset to discover it.
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/nfc_saved_stack_test.c \
 *      -o /tmp/nfc_saved_stack_test && /tmp/nfc_saved_stack_test
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

/* Bounds the authoritative nfc_saved_launch() body via brace matching --
 * same technique nfc_mfc_dict_ui_test.c already uses for m1_nfc.c. */
static const char *bound_function(const char *src, const char *sig, size_t *out_len)
{
    const char *start = strstr(src, sig);
    if (start == NULL) { *out_len = 0U; return NULL; }
    int depth = 0;
    const char *p = start;
    const char *end = NULL;
    for (; *p != '\0'; p++) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) { end = p; break; }
        }
    }
    *out_len = (end != NULL) ? (size_t)(end - start + 1) : 0U;
    return start;
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
    char *src = read_all("m1_csrc/m1_nfc.c");
    CHECK(src != NULL, "m1_csrc/m1_nfc.c readable");
    if (src == NULL) {
        printf("nfc_saved_stack_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }

    size_t fn_len;
    const char *fn = bound_function(src, "void nfc_saved_launch(const char *dir_name, const char *file_name)\n{", &fn_len);
    CHECK(fn != NULL, "nfc_saved_launch() is defined");
    CHECK(fn_len > 0U, "nfc_saved_launch() body could be brace-bounded");
    CHECK(strstr(src, "nfc_saved_launch(NULL, NULL);") != NULL,
          "native nfc_saved() delegates to the authoritative launcher");

    if (fn_len > 0U) {
        /* The exact defect: a local nfc_run_ctx_t of any name, anywhere in
         * this function's body. Checks the type token followed by an
         * identifier and a statement terminator -- not just the substring
         * "nfc_run_ctx_t" (which would also match a comment mentioning the
         * type, a pointer parameter, or an extern -- none of which are the
         * defect). Any occurrence of this exact declaration
         * pattern inside the body is necessarily a fresh stack-local. */
        CHECK(!bounded_contains(fn, fn_len, "nfc_run_ctx_t ctx"),
              "no local 'nfc_run_ctx_t ctx' remains (the exact NFC2-005 hardware-crash declaration)");
        CHECK(!bounded_contains(fn, fn_len, "nfc_run_ctx_init(&ctx)"),
              "no 'nfc_run_ctx_init(&ctx)' call remains (its matching init)");

        /* Broader guard: no declaration of the type at all, under any local
         * name, in a fresh nfc_run_ctx_t/nfc_run_ctx_t\s+\w+ shape. A
         * plain substring search for "nfc_run_ctx_t" would also flag a
         * legitimate pointer-returning call, so this specifically looks for the local-declaration shape: the
         * type name immediately followed by whitespace and an identifier
         * character (not '*'), which a parameter/pointer never is inside a
         * function that (per its own signature) takes no parameters at all. */
        {
            const char *p = fn;
            const char *fn_end = fn + fn_len;
            int found_bad_decl = 0;
            const char *needle = "nfc_run_ctx_t";
            size_t needle_len = strlen(needle);
            while (p < fn_end) {
                const char *hit = NULL;
                for (const char *q = p; q + needle_len <= fn_end; q++) {
                    if (memcmp(q, needle, needle_len) == 0) { hit = q; break; }
                }
                if (hit == NULL) break;
                const char *after = hit + needle_len;
                /* Skip whitespace after the type token. */
                while (after < fn_end && (*after == ' ' || *after == '\t')) after++;
                if (after < fn_end && *after != '*' && *after != '(' &&
                    ((*after >= 'a' && *after <= 'z') || (*after >= 'A' && *after <= 'Z') || *after == '_')) {
                    found_bad_decl = 1;
                    break;
                }
                p = hit + needle_len;
            }
            CHECK(!found_bad_decl,
              "no local nfc_run_ctx_t declaration under ANY name remains inside nfc_saved_launch()'s body");
        }

        /* Confirm the fix didn't silently swap the local for a global-
         * context initialization instead -- the task explicitly forbids
         * that as an unasked-for behavior change at browser entry. */
        CHECK(!bounded_contains(fn, fn_len, "nfc_run_ctx_init("),
              "nfc_saved_launch() calls nfc_run_ctx_init() nowhere at all -- not on a local, not on the global context either");

        /* The rest of the function's real behavior is untouched: still
         * initializes the Saved browser in NFC_SAVE_DIR and pumps the same
         * message loop. */
        CHECK(bounded_contains(fn, fn_len, "m1_fb_set_start_dir(NFC_SAVE_DIR);"),
              "nfc_saved_launch() still opens the browser in NFC_SAVE_DIR, unchanged");
        CHECK(bounded_contains(fn, fn_len, "VIEW_MODE_NFC_SAVED_BROWSE"),
              "nfc_saved_launch() still switches to the Saved browser view, unchanged");
        CHECK(bounded_contains(fn, fn_len, "m1_uiView_q_message_process()"),
              "nfc_saved_launch() still pumps its message loop, unchanged");
    }

    free(src);
    printf("nfc_saved_stack_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
