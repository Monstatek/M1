/*
 * mfc_detect_seam_test.c - source-seam regression test for
 * NFC/NFC_drv/legacy/mfc_detect.c. That file has a genuine RFAL/HAL
 * dependency (real rfalStartTransceive/HAL_GetTick calls) and cannot be
 * host-linked, so this proves the required protocol-direction and
 * hardening-wiring properties via bounded string search against the real,
 * committed source text -- same technique this repo already established
 * for m1_nfc.c (nfc_saved_stack_test.c, nfc_saved_back_navigation_test.c).
 *
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra NFC/NFC_drv/common/test/mfc_detect_seam_test.c \
 *      -o /tmp/mfc_detect_seam_test && /tmp/mfc_detect_seam_test
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

/* Brace-matched body bounding, LF-anchored (mfc_detect.c uses plain LF,
 * unlike m1_nfc.c's CRLF -- confirmed by direct inspection). */
static const char *bound_block(const char *src, const char *sig, size_t *out_len)
{
    const char *start = strstr(src, sig);
    if (start == NULL) { *out_len = 0U; return NULL; }
    const char *open_brace = strchr(start, '{');
    if (open_brace == NULL) { *out_len = 0U; return NULL; }
    int depth = 0;
    const char *p = open_brace;
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
    char *src = read_all("NFC/NFC_drv/legacy/mfc_detect.c");
    CHECK(src != NULL, "NFC/NFC_drv/legacy/mfc_detect.c readable");
    if (src == NULL) {
        printf("mfc_detect_seam_test: %d passed, %d failed\n", pass_count, fail_count);
        return fail_count ? 1 : 0;
    }

    size_t fn_len;
    const char *fn = bound_block(src, "bool mfc_detect_service_frame(const uint8_t *rx, uint16_t rxLenBits)\n{", &fn_len);
    CHECK(fn != NULL && fn_len > 0U, "mfc_detect_service_frame() is defined and bounded");

    if (fn != NULL && fn_len > 0U) {
        /* --- 1/2. AUTH direction: Nt is CARD-generated (next_nt(), a local
         * PRNG call), never read from the reader's frame. --- */
        CHECK(bounded_contains(fn, fn_len, "nt = next_nt();"),
              "Nt is generated locally (next_nt()), not received from the reader");
        CHECK(!bounded_contains(fn, fn_len, "nt = be32(&rx["),
              "Nt is never parsed out of the inbound reader frame (it is M1's own choice, not the reader's)");

        /* --- 3. Nr||Ar captured in one transceive, unpacked and stored into
         * `cap` BEFORE this function returns -- and RF re-arm happens in
         * the CALLER (nfc_listener.c), strictly after this function
         * returns, so the frame is provably preserved before any re-arm
         * or shared-buffer reuse. --- */
        CHECK(bounded_contains(fn, fn_len, "unpack_bits(rxbuf, rxbits, bytes, 8U)"),
              "the reader's raw Nr||Ar bytes are unpacked from the transceive's own rxbuf");
        CHECK(bounded_contains(fn, fn_len, "cap.nr       = be32(&bytes[0]);") &&
              bounded_contains(fn, fn_len, "cap.ar       = be32(&bytes[4]);"),
              "Nr/Ar are stored into cap (mfc_capture_add's input) before this function returns");
        CHECK(bounded_contains(fn, fn_len, "mfc_capture_add(&cap)"),
              "cap is handed to the capture store before this function returns (i.e. before the caller can re-arm RX)");

        /* --- 4. At is never expected from the reader: exactly ONE
         * transceive call in this function (the Nt-send / Nr+Ar-receive
         * exchange), no second receive step for anything resembling At. --- */
        {
            const char *p = fn;
            int transceive_calls = 0;
            const char *needle = "rfalStartTransceive(&ctx)";
            size_t nlen = strlen(needle);
            const char *fn_end = fn + fn_len;
            while (p < fn_end) {
                const char *hit = NULL;
                for (const char *q = p; q + nlen <= fn_end; q++) {
                    if (memcmp(q, needle, nlen) == 0) { hit = q; break; }
                }
                if (hit == NULL) break;
                transceive_calls++;
                p = hit + nlen;
            }
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "exactly one RF transceive per AUTH (Nt-send/Nr+Ar-receive only, no second exchange for At) -- found %d",
                     transceive_calls);
            CHECK(transceive_calls == 1, msg);
        }
        CHECK(!bounded_contains(fn, fn_len, "at_enc") && !bounded_contains(fn, fn_len, ".at "),
              "no field named/resembling At is ever referenced in this function");

        /* --- now_ms wiring for timeout tracking --- */
        CHECK(bounded_contains(fn, fn_len, "cap.now_ms   = HAL_GetTick();"),
              "cap.now_ms is stamped from the real monotonic tick before being stored");
        CHECK(bounded_contains(fn, fn_len, "mfc_capture_check_timeout(HAL_GetTick(), MFC_DR_PAIR_TIMEOUT_MS)"),
              "a bounded pair-timeout check runs on every serviced frame");
    }

    /* --- 5. Field-loss invalidates in-flight pairs (Phase 3 hardening). --- */
    size_t fl_len;
    const char *fl = bound_block(src, "void mfc_detect_on_field_lost(void)\n{", &fl_len);
    CHECK(fl != NULL && fl_len > 0U, "mfc_detect_on_field_lost() is defined and bounded");
    if (fl != NULL && fl_len > 0U) {
        CHECK(bounded_contains(fl, fl_len, "mfc_capture_invalidate_incomplete();"),
              "field loss invalidates any in-flight (incomplete) capture pair");
    }

    /* --- STOP/BACK (mfc_detect_end()) invalidates in-flight pairs too. --- */
    size_t end_len;
    const char *end_fn = bound_block(src, "void mfc_detect_end(void)\n{", &end_len);
    CHECK(end_fn != NULL && end_len > 0U, "mfc_detect_end() is defined and bounded");
    if (end_fn != NULL && end_len > 0U) {
        CHECK(bounded_contains(end_fn, end_len, "mfc_capture_invalidate_incomplete();"),
              "STOP/BACK (session end) invalidates any in-flight capture pair");
    }

    /* --- Never disturbs saved-card RAWOWN emulation: this whole file has
     * no reference to the RAWOWN/DMA/{At} transmit machinery at all --
     * it is a completely separate, reader-side-only listener persona. --- */
    CHECK(!bounded_contains(src, strlen(src), "M1_MFC_DMA_AT") &&
          !bounded_contains(src, strlen(src), "m1_mfc_raw_listener") &&
          !bounded_contains(src, strlen(src), "RAWOWN"),
          "mfc_detect.c never references the RAWOWN/DMA {At} emulation machinery");

    free(src);
    printf("mfc_detect_seam_test: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
