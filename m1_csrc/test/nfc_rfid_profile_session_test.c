/*
 * nfc_rfid_profile_session_test.c - source-seam test proving the NFC/RFID
 * saved-file header-parsing conversion to a single open+scan session is
 * complete, correctly wired, and does not disturb any other caller of the
 * original get_private_profile()/GetPrivateProfileXxx() primitives.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DISCLOSED LIMITATION: privateprofilestring.c, nfc_storage.c, and
 * lfrfid_file.c all pull in stm32h5xx_hal.h/FatFs/FreeRTOS transitively, so
 * they cannot be host-compiled or linked directly. This suite verifies the
 * exact source text instead (reads the actual files; not a mock, not a
 * paraphrase) -- same technique used elsewhere in this project.
 *
 * Context: a hardware-observed defect (NFC Saved button responses up to
 * ~1s) traced to nfc_storage_parse_header_ini() doing up to ~19-22
 * independent f_open+scan+f_close cycles per selected file (one per
 * optional header key via GetPrivateProfileString/Hex/Uint), and
 * lfrfid_profile_load() doing 4. Both are converted here to ONE
 * profile_session_open() + N in-place scans + ONE profile_session_close(),
 * via a new session API in privateprofilestring.c that shares its scan
 * logic (get_private_profile_from_fp()) with the untouched single-shot
 * get_private_profile() -- so every OTHER existing caller of
 * GetPrivateProfileXxx() elsewhere in the tree keeps its original
 * open-per-call behavior, unaffected.
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/nfc_rfid_profile_session_test.c -o /tmp/profile_session && /tmp/profile_session
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}
static int contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }
static int count_occurrences(const char *hay, const char *needle)
{
    int n = 0; const char *p = hay; size_t len = strlen(needle);
    if (!hay || len == 0) return 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

int main(void)
{
    char *pps_h = slurp("m1_csrc/privateprofilestring.h");
    char *pps_c = slurp("m1_csrc/privateprofilestring.c");
    char *nfc_storage = slurp("NFC/NFC_drv/common/nfc_storage.c");
    char *lfrfid_file = slurp("lfrfid/lfrfid_file.c");

    CHECK(pps_h != NULL, "privateprofilestring.h readable");
    CHECK(pps_c != NULL, "privateprofilestring.c readable");
    CHECK(nfc_storage != NULL, "nfc_storage.c readable");
    CHECK(lfrfid_file != NULL, "lfrfid_file.c readable");
    if (!pps_h || !pps_c || !nfc_storage || !lfrfid_file)
    {
        printf("nfc_rfid_profile_session_test: %d passed, %d failed (source unreadable, aborting)\n", g_pass, g_fail);
        return 1;
    }

    /* --- 1. Session API exists and shares scan logic with the unchanged
     * single-shot path (both call the same static core) --- */
    CHECK(contains(pps_c, "static int get_private_profile_from_fp(FIL *fp, ParsedValue *val, const char *entry)"),
          "shared scan core exists, parameterized on an already-open FIL*");
    {
        const char *legacy = strstr(pps_c, "int get_private_profile(ParsedValue *val, const char *entry, const char *file_name)");
        const char *legacy_open = legacy ? strstr(legacy, "f_open(&fp, file_name,FA_READ)") : NULL;
        const char *legacy_delegates = legacy ? strstr(legacy, "get_private_profile_from_fp(&fp, val, entry)") : NULL;
        const char *legacy_close = legacy_delegates ? strstr(legacy_delegates, "f_close(&fp);") : NULL;
        CHECK(legacy && legacy_open && legacy_delegates && legacy_close,
              "single-shot get_private_profile() is unchanged in contract: still opens, delegates to the shared scan, then closes -- every other existing caller is unaffected");
    }
    CHECK(contains(pps_c, "bool profile_session_open(ProfileSession *sess, const char *file_name)"),
          "profile_session_open() exists");
    CHECK(contains(pps_c, "void profile_session_close(ProfileSession *sess)"),
          "profile_session_close() exists");
    {
        const char *core = strstr(pps_c, "static int get_private_profile_session_core(ParsedValue *val, const char *entry, ProfileSession *sess)");
        const char *rewind = core ? strstr(core, "f_lseek(&sess->fp, 0);") : NULL;
        const char *delegate = rewind ? strstr(rewind, "get_private_profile_from_fp(&sess->fp, val, entry);") : NULL;
        CHECK(core && rewind && delegate && (delegate - rewind) < 60,
              "session scan rewinds then reuses the EXACT SAME scan core as the single-shot path -- identical match semantics, just no repeated open/close");
    }
    CHECK(contains(pps_c, "int get_private_profile_session_string(ParsedValue *val, const char *entry, ProfileSession *sess)") &&
          contains(pps_c, "val->type = VALUE_TYPE_STRING;"),
          "session string wrapper sets the same value type the non-session string wrapper would");
    CHECK(contains(pps_c, "int get_private_profile_session_hex(ParsedValue *val, const char *entry, ProfileSession *sess)"),
          "session hex wrapper exists");
    CHECK(contains(pps_c, "int get_private_profile_session_uint(ParsedValue *val, const char *entry, ProfileSession *sess)"),
          "session uint wrapper exists");
    CHECK(contains(pps_c, "bool isValidHeaderFieldSession(ParsedValue *data, const char* filetype, const char* version, ProfileSession *sess)"),
          "session variant of isValidHeaderField exists for the Filetype/Version check");

    /* --- 2. Open counter is purely additive and covers both paths --- */
    CHECK(contains(pps_c, "static uint32_t s_profile_open_count = 0;"), "open counter exists");
    CHECK(count_occurrences(pps_c, "s_profile_open_count++;") == 2,
          "counter increments exactly twice in source: once in the single-shot open, once in profile_session_open -- never per-key");
    CHECK(contains(pps_h, "uint32_t get_private_profile_open_count(void);"), "open-count getter declared");
    CHECK(contains(pps_h, "void reset_private_profile_open_count(void);"), "open-count reset declared");

    /* --- 3. nfc_storage_parse_header_ini(): exactly one open, one close,
     * reached from every early-exit path via goto done --- */
    {
        const char *fn = strstr(nfc_storage, "static nfc_storage_result_t nfc_storage_parse_header_ini(");
        CHECK(fn != NULL, "nfc_storage_parse_header_ini definition found");
        /* Bound the search to this function only, using the next function's
         * doc-comment banner text as the boundary. */
        const char *fn_end = fn ? strstr(fn, "Store unit data from \"Page N:\" / \"Block N:\" line") : NULL;
        CHECK(fn_end != NULL, "found the next function's banner comment to bound the search");

        if (fn && fn_end)
        {
            size_t span = (size_t)(fn_end - fn);
            char *body = malloc(span + 1);
            memcpy(body, fn, span);
            body[span] = 0;

            CHECK(count_occurrences(body, "profile_session_open(&sess, file_path)") == 1,
                  "exactly one profile_session_open() call in the whole function");
            CHECK(count_occurrences(body, "profile_session_close(&sess);") == 1,
                  "exactly one profile_session_close() call (at the shared done: label)");
            CHECK(contains(body, "done:") && contains(body, "return result;"),
                  "single exit point via a done: label returning a captured result");
            int goto_count = count_occurrences(body, "goto done;");
            CHECK(goto_count == 6,
                  "all 6 early-exit paths (isValidHeaderFieldSession fail, Device type not found, unsupported device type, UID not found, invalid UID length, and the final saw_filetype/saw_devtype check) route through goto done");

            /* No leftover direct (non-session, per-key-reopening) calls
             * remain inside this function. */
            CHECK(count_occurrences(body, "GetPrivateProfileString(&data,") == 0,
                  "no leftover non-session GetPrivateProfileString() calls inside the header parser");
            CHECK(count_occurrences(body, "GetPrivateProfileHex(&data,") == 0,
                  "no leftover non-session GetPrivateProfileHex() calls inside the header parser");
            CHECK(count_occurrences(body, "GetPrivateProfileUint(&data,") == 0,
                  "no leftover non-session GetPrivateProfileUint() calls inside the header parser");
            CHECK(contains(body, "isValidHeaderFieldSession(&data, \"M1 NFC device\", \"4\", &sess)"),
                  "Filetype/Version validation uses the session variant with the same literal filetype/version strings");

            /* Every field the original function read is still read, just via
             * the S-suffixed session macro -- spot-check the full set. */
            const char *expected_keys[] = {
                "GetPrivateProfileStringS(&data, \"Device type\", &sess)",
                "GetPrivateProfileHexS(&data, \"UID\", &sess)",
                "GetPrivateProfileHexS(&data, \"ATQA\", &sess)",
                "GetPrivateProfileHexS(&data, \"SAK\", &sess)",
                "GetPrivateProfileHexS(&data, \"ATS\", &sess)",
                "GetPrivateProfileHexS(&data, \"Signature\", &sess)",
                "GetPrivateProfileHexS(&data, kCounterKeys[i], &sess)",
                "GetPrivateProfileHexS(&data, kTearingKeys[i], &sess)",
                "GetPrivateProfileStringS(&data, \"T2T Variant\", &sess)",
                "GetPrivateProfileHexS(&data, \"T2T Version\", &sess)",
                "GetPrivateProfileUintS(&data, \"Pages\", &sess)",
                "GetPrivateProfileHexS(&data, \"PWD\", &sess)",
                "GetPrivateProfileHexS(&data, \"PACK\", &sess)",
                "GetPrivateProfileHexS(&data, \"DESFire Version\", &sess)",
                "GetPrivateProfileUintS(&data, \"DESFire Free Memory\", &sess)",
                "GetPrivateProfileHexS(&data, \"DESFire Master Key Settings\", &sess)",
                "GetPrivateProfileUintS(&data, \"DESFire Apps Truncated\", &sess)",
                "GetPrivateProfileUintS(&data, \"DESFire Apps Protected\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Card\", &sess)",
                "GetPrivateProfileStringS(&data, \"Clipper Type\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Serial\", &sess)",
                "GetPrivateProfileStringS(&data, \"Clipper Balance\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Last Update\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Terminal\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Txn\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Counter\", &sess)",
                "GetPrivateProfileUintS(&data, \"Clipper Rides Truncated\", &sess)",
            };
            size_t missing = 0;
            for (size_t i = 0; i < sizeof(expected_keys)/sizeof(expected_keys[0]); i++)
            {
                if (!contains(body, expected_keys[i])) { printf("  missing: %s\n", expected_keys[i]); missing++; }
            }
            CHECK(missing == 0, "every field the original header parser read is still read, via the session-suffixed call");

            free(body);
        }
    }

    /* --- 4. lfrfid_profile_load(): exactly one open, one close, all 4
     * fields converted --- */
    {
        const char *fn = strstr(lfrfid_file, "bool lfrfid_profile_load(const S_M1_file_info *f, const char* ext)");
        CHECK(fn != NULL, "lfrfid_profile_load definition found");
        const char *fn_end = fn ? strstr(fn, "\n}\n") : NULL; /* first closing brace at column 0 after the opening */
        CHECK(fn_end != NULL, "found the function's closing brace");

        if (fn && fn_end)
        {
            size_t span = (size_t)(fn_end - fn);
            char *body = malloc(span + 1);
            memcpy(body, fn, span);
            body[span] = 0;

            CHECK(count_occurrences(body, "profile_session_open(&sess, file_path)") == 1,
                  "exactly one profile_session_open() call");
            CHECK(count_occurrences(body, "profile_session_close(&sess);") == 1,
                  "exactly one profile_session_close() call");
            CHECK(contains(body, "done:") && contains(body, "return ok;"),
                  "single exit point via a done: label returning a captured result");
            CHECK(count_occurrences(body, "GetPrivateProfileString(&data,") == 0,
                  "no leftover non-session GetPrivateProfileString() calls");
            CHECK(count_occurrences(body, "GetPrivateProfileStringS(&data,") == 4,
                  "all 4 header fields (Filetype, Version, proto_key, data_key) converted to the session call");

            free(body);
        }
    }

    printf("nfc_rfid_profile_session_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
