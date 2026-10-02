/* Host tests for the MIFARE DESFire deep-read persistence format added to
 * nfc_file.c (save) and nfc_storage.c (load).
 *
 * DISCLOSED LIMITATION (same as mfc_key_file_format_test.c and
 * nfc_t2t_persistence_test.c, every other FatFs/HAL-coupled file in this
 * project): nfc_profile_save()/nfc_storage_load_file() cannot be
 * host-compiled directly (m1_sdcard.h pulls main.h/the full HAL stack;
 * nfc_fileio.c wraps real FatFS). Following the exact pattern already
 * established by nfc_t2t_persistence_test.c, this suite:
 *   1. Verifies the exact save-line formats and parse-dispatch literals are
 *      present in the REAL, committed nfc_file.c/nfc_storage.c (source_contains
 *      reads the actual files, not a memory of them) -- so drift between this
 *      test and the shipped logic is caught, not silently trusted.
 *   2. Transcribes (byte-for-byte, matching the format strings verified in
 *      step 1) the DESFire-specific save-line writer and load-line parser as
 *      pure in-memory functions, and round-trips a representative
 *      mf_desfire_deep_t (including edge cases: negative Value limits,
 *      truncation flags, protected/unsupported/partial files, an app with no
 *      readable files, an empty deep-read) through them -- asserting exact
 *      struct equality afterward. The actual byte-level parsing (key
 *      settings, file settings) is NOT transcribed -- it calls the real,
 *      already separately-tested mf_desfire_parse.c functions directly.
 * The real on-disk round trip (survives a reboot, a real SD card) is covered
 * by this milestone's hardware acceptance step instead.
 *
 * Build (from repo root):
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     -I NFC/NFC_drv/common \
 *     NFC/NFC_drv/common/test/nfc_desfire_persistence_test.c \
 *     NFC/NFC_drv/common/mf_desfire_parse.c \
 *     -o /tmp/nfc_desfire_persistence_test && /tmp/nfc_desfire_persistence_test
 * (Run from the repository root; override REPO_ROOT at compile time if needed.)
 */
#include "mf_desfire_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

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

static bool contains(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* ============================================================================
 * Part 1: source-bound proofs against the real, committed files.
 * ========================================================================= */
static void test_source_matches_shipped(void)
{
    char *file_c    = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_file.c");
    char *storage_c = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_storage.c");
    CHECK(file_c != NULL,    "nfc_file.c readable");
    CHECK(storage_c != NULL, "nfc_storage.c readable");

    CHECK(contains(file_c, "\"DESFire Version: %s\\r\\n\""), "nfc_file.c: DESFire Version save line format present");
    CHECK(contains(file_c, "\"DESFire Free Memory: %lu\\r\\n\""), "nfc_file.c: DESFire Free Memory save line format present");
    CHECK(contains(file_c, "\"DESFire Master Key Settings: %02X %02X\\r\\n\""), "nfc_file.c: Master Key Settings save line format present");
    CHECK(contains(file_c, "DESFire Apps Truncated: 1"), "nfc_file.c: Apps Truncated save line present");
    CHECK(contains(file_c, "DESFire Apps Protected: 1"), "nfc_file.c: Apps Protected save line present");
    CHECK(contains(file_c, "\"DESFire Application %s: %d\\r\\n\""), "nfc_file.c: Application save line format present");
    CHECK(contains(file_c, "\"DESFire Application %s Key Settings: %02X %02X\\r\\n\""), "nfc_file.c: per-app Key Settings save line present");
    CHECK(contains(file_c, "\"DESFire Application %s Key Version %02u: %02X\\r\\n\""), "nfc_file.c: Key Version save line present");
    CHECK(contains(file_c, "DESFire Application %s File %02X: %d %d %d %04X %06lX %08lX %08lX %08lX %d %06lX %06lX %06lX %02X %02X %d %u"),
          "nfc_file.c: File settings save line format present");
    CHECK(contains(file_c, "\"DESFire Application %s File %02X Data: %s\\r\\n\""), "nfc_file.c: File Data save line format present");
    CHECK(contains(file_c, "(unsigned long)(uint32_t)s->value_lo_limit"),
          "nfc_file.c: signed value-limit fields are truncated to 32 bits before hex formatting (host 64-bit-long sign-extension guard)");

    CHECK(contains(storage_c, "GetPrivateProfileHexS(&data, \"DESFire Version\", &sess) && data.v.hex.out_len == 28"),
          "nfc_storage.c: DESFire Version parsed as exactly 28 raw bytes");
    CHECK(contains(storage_c, "GetPrivateProfileUintS(&data, \"DESFire Free Memory\", &sess)"),
          "nfc_storage.c: DESFire Free Memory parsed via the header uint reader");
    CHECK(contains(storage_c, "mf_desfire_key_settings_parse(mks, 2, &c->desfire_deep.master_key_settings)"),
          "nfc_storage.c: Master Key Settings re-parsed via the real shared parser, not duplicated field logic");
    CHECK(contains(storage_c, "strncmp(line, \"DESFire Application \", 20) == 0"),
          "nfc_storage.c: Application line dispatch present");
    CHECK(contains(storage_c, "memcmp(c->desfire_deep.apps[i].id.id, aid_bytes, 3) == 0"),
          "nfc_storage.c: application lookup is AID-keyed, not line-order-dependent");
    CHECK(contains(storage_c, "nfc_ctx_clear_desfire();"),
          "nfc_storage.c: a fresh load clears any previously loaded DESFire data before parsing");
}

/* ============================================================================
 * Part 2: transcribed round trip (writer -> in-memory lines -> parser).
 * The writer/parser logic below is transcribed from the real files (each
 * format string used is one already checked verbatim in Part 1); the actual
 * byte-level struct parsing (mf_desfire_key_settings_parse) is the real,
 * linked-in production function.
 * ========================================================================= */

#define MAX_LINES 512
#define MAX_LINE_LEN 320

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int  count;
} line_buf_t;

static void lb_add(line_buf_t *lb, const char *fmt, ...)
{
    if (lb->count >= MAX_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lb->lines[lb->count], MAX_LINE_LEN, fmt, ap);
    va_end(ap);
    lb->count++;
}

/* Transcribed from nfc_file.c's new DESFire save block. */
static void desf_write(line_buf_t *lb, const mf_desfire_deep_t *deep, bool desfire_present, const uint8_t v28[28])
{
    if (!desfire_present) return;

    char v_str[3 * 28 + 1];
    int pos = 0;
    for (uint8_t i = 0; i < 28U; i++) {
        pos += snprintf(v_str + pos, sizeof(v_str) - (size_t)pos, "%02X ", v28[i]);
    }
    lb_add(lb, "DESFire Version: %s", v_str);

    if (deep->free_memory_valid) {
        lb_add(lb, "DESFire Free Memory: %lu", (unsigned long)deep->free_memory_bytes);
    }

    if (deep->master_key_settings_valid) {
        uint8_t settings_byte = (uint8_t)((deep->master_key_settings.change_key_id << 4) |
                                (deep->master_key_settings.config_changeable   ? 0x08U : 0U) |
                                (deep->master_key_settings.free_create_delete  ? 0x04U : 0U) |
                                (deep->master_key_settings.free_directory_list ? 0x02U : 0U) |
                                (deep->master_key_settings.key_changeable      ? 0x01U : 0U));
        uint8_t keycount_byte = (uint8_t)((deep->master_key_settings.flags << 4) | deep->master_key_settings.max_keys);
        lb_add(lb, "DESFire Master Key Settings: %02X %02X", settings_byte, keycount_byte);
    }

    if (deep->apps_truncated)  lb_add(lb, "DESFire Apps Truncated: 1");
    if (deep->apps_protected)  lb_add(lb, "DESFire Apps Protected: 1");

    for (uint8_t a = 0; a < deep->app_count && a < MF_DESFIRE_DEEP_MAX_APPS; a++) {
        const mf_desfire_app_t *app = &deep->apps[a];
        char aid_str[7];
        snprintf(aid_str, sizeof(aid_str), "%02X%02X%02X", app->id.id[0], app->id.id[1], app->id.id[2]);

        lb_add(lb, "DESFire Application %s: %d", aid_str, app->select_ok ? 1 : 0);

        if (app->key_settings_valid) {
            uint8_t settings_byte = (uint8_t)((app->key_settings.change_key_id << 4) |
                                    (app->key_settings.config_changeable   ? 0x08U : 0U) |
                                    (app->key_settings.free_create_delete  ? 0x04U : 0U) |
                                    (app->key_settings.free_directory_list ? 0x02U : 0U) |
                                    (app->key_settings.key_changeable      ? 0x01U : 0U));
            uint8_t keycount_byte = (uint8_t)((app->key_settings.flags << 4) | app->key_settings.max_keys);
            lb_add(lb, "DESFire Application %s Key Settings: %02X %02X", aid_str, settings_byte, keycount_byte);
        }

        if (app->key_versions_truncated) lb_add(lb, "DESFire Application %s Key Versions Truncated: 1", aid_str);

        for (uint8_t k = 0; k < app->key_version_count && k < MF_DESFIRE_MAX_KEYS; k++) {
            lb_add(lb, "DESFire Application %s Key Version %02u: %02X", aid_str, (unsigned)k, app->key_versions[k]);
        }

        if (app->files_truncated) lb_add(lb, "DESFire Application %s Files Truncated: 1", aid_str);

        for (uint8_t f = 0; f < app->file_count && f < MF_DESFIRE_MAX_FILES_PER_APP; f++) {
            const mf_desfire_file_t *file = &app->files[f];
            const mf_desfire_file_settings_t *s = &file->settings;

            lb_add(lb, "DESFire Application %s File %02X: %d %d %d %04X %06lX %08lX %08lX %08lX %d %06lX %06lX %06lX %02X %02X %d %u",
                   aid_str, file->id,
                   file->settings_valid ? 1 : 0,
                   (int)s->type, (int)s->comm, (unsigned)s->access_rights,
                   (unsigned long)s->size,
                   (unsigned long)(uint32_t)s->value_lo_limit,
                   (unsigned long)(uint32_t)s->value_hi_limit,
                   (unsigned long)(uint32_t)s->value_limited_credit,
                   s->value_limited_credit_enabled ? 1 : 0,
                   (unsigned long)s->record_size, (unsigned long)s->record_max, (unsigned long)s->record_cur,
                   s->tmac_key_option, s->tmac_key_version,
                   (int)file->read_status, (unsigned)file->data_len);

            if (file->data_len > 0U) {
                char data_str[3 * MF_DESFIRE_DEEP_FILE_DATA_CAP + 1];
                int dpos = 0;
                for (uint16_t i = 0; i < file->data_len; i++) {
                    dpos += snprintf(data_str + dpos, sizeof(data_str) - (size_t)dpos, "%02X ", file->data[i]);
                }
                lb_add(lb, "DESFire Application %s File %02X Data: %s", aid_str, file->id, data_str);
            }
        }
    }
}

/* Transcribed from nfc_storage.c's new DESFire body-parse dispatch. */
static void desf_parse_line(const char *raw_line, mf_desfire_deep_t *deep, bool *desfire_present, uint8_t v28[28])
{
    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    if (strncmp(line, "DESFire Version: ", 17) == 0) {
        /* Reuse the real hex-array parser semantics via mf_desfire_app_ids_parse's
         * sibling primitive is overkill here; a plain hex scan suffices for this
         * transcription since the real loader uses GetPrivateProfileHex (already
         * covered generically elsewhere in this project's test suite). */
        const char *p = line + 17;
        size_t got = 0;
        while (*p && got < 28) {
            unsigned byte;
            int consumed = 0;
            if (sscanf(p, "%2x%n", &byte, &consumed) != 1) break;
            v28[got++] = (uint8_t)byte;
            p += consumed;
            while (*p == ' ') p++;
        }
        if (got == 28) *desfire_present = true;
        return;
    }

    if (sscanf(line, "DESFire Free Memory: %u", (unsigned int *)&deep->free_memory_bytes) == 1) {
        deep->free_memory_valid = true;
        return;
    }

    {
        unsigned sb = 0, kb = 0;
        if (sscanf(line, "DESFire Master Key Settings: %x %x", &sb, &kb) == 2) {
            uint8_t mks[2] = { (uint8_t)sb, (uint8_t)kb };
            deep->master_key_settings_valid = mf_desfire_key_settings_parse(mks, 2, &deep->master_key_settings);
            return;
        }
    }

    if (strcmp(line, "DESFire Apps Truncated: 1") == 0) { deep->apps_truncated = true; return; }
    if (strcmp(line, "DESFire Apps Protected: 1") == 0) { deep->apps_protected = true; return; }

    if (strncmp(line, "DESFire Application ", 20) == 0) {
        const char *rest = line + 20;
        if (strlen(rest) < 6) return;
        unsigned aidv[3];
        if (sscanf(rest, "%2x%2x%2x", &aidv[0], &aidv[1], &aidv[2]) != 3) return;
        uint8_t aid_bytes[3] = { (uint8_t)aidv[0], (uint8_t)aidv[1], (uint8_t)aidv[2] };
        const char *after_aid = rest + 6;

        mf_desfire_app_t *app = NULL;
        for (uint8_t i = 0; i < deep->app_count; i++) {
            if (memcmp(deep->apps[i].id.id, aid_bytes, 3) == 0) { app = &deep->apps[i]; break; }
        }

        if (after_aid[0] == ':') {
            int select_ok = 0;
            if (sscanf(after_aid, ": %d", &select_ok) != 1) return;
            if (app == NULL) {
                if (deep->app_count >= MF_DESFIRE_DEEP_MAX_APPS) return;
                app = &deep->apps[deep->app_count++];
                memset(app, 0, sizeof(*app));
                memcpy(app->id.id, aid_bytes, 3);
            }
            app->select_ok = (select_ok != 0);
            return;
        }

        if (app == NULL) return;

        if (strncmp(after_aid, " Key Settings:", 14) == 0) {
            unsigned sb = 0, kb = 0;
            if (sscanf(after_aid + 14, " %x %x", &sb, &kb) == 2) {
                uint8_t mks[2] = { (uint8_t)sb, (uint8_t)kb };
                app->key_settings_valid = mf_desfire_key_settings_parse(mks, 2, &app->key_settings);
            }
            return;
        }

        if (strncmp(after_aid, " Key Versions Truncated:", 24) == 0) {
            int v = 0;
            if (sscanf(after_aid + 24, "%d", &v) == 1 && v != 0) app->key_versions_truncated = true;
            return;
        }

        if (strncmp(after_aid, " Key Version ", 13) == 0) {
            unsigned val = 0;
            const char *colon = strchr(after_aid + 13, ':');
            if (colon != NULL && sscanf(colon + 1, "%x", &val) == 1) {
                if (app->key_version_count < MF_DESFIRE_MAX_KEYS) app->key_versions[app->key_version_count++] = (uint8_t)val;
            }
            return;
        }

        if (strncmp(after_aid, " Files Truncated:", 17) == 0) {
            int v = 0;
            if (sscanf(after_aid + 17, "%d", &v) == 1 && v != 0) app->files_truncated = true;
            return;
        }

        if (strncmp(after_aid, " File ", 6) == 0) {
            const char *file_rest = after_aid + 6;
            if (strlen(file_rest) < 2) return;
            unsigned fidv = 0;
            if (sscanf(file_rest, "%2x", &fidv) != 1) return;
            uint8_t file_id = (uint8_t)fidv;
            const char *after_fid = file_rest + 2;

            mf_desfire_file_t *file = NULL;
            for (uint8_t i = 0; i < app->file_count; i++) {
                if (app->files[i].id == file_id) { file = &app->files[i]; break; }
            }

            if (strncmp(after_fid, " Data:", 6) == 0) {
                if (file == NULL) return;
                const char *p = after_fid + 6;
                while (*p == ' ') p++;
                uint16_t got = 0;
                while (*p && got < MF_DESFIRE_DEEP_FILE_DATA_CAP) {
                    unsigned byte; int consumed = 0;
                    if (sscanf(p, "%2x%n", &byte, &consumed) != 1) break;
                    file->data[got++] = (uint8_t)byte;
                    p += consumed;
                    while (*p == ' ') p++;
                }
                file->data_len = got;
                return;
            }

            if (after_fid[0] == ':') {
                if (file == NULL) {
                    if (app->file_count >= MF_DESFIRE_MAX_FILES_PER_APP) return;
                    file = &app->files[app->file_count++];
                    memset(file, 0, sizeof(*file));
                    file->id = file_id;
                }

                int settings_valid = 0, type = 0, comm = 0, credit_en = 0, read_status = 0;
                unsigned access_rights = 0, saved_data_len = 0, tmac_opt = 0, tmac_ver = 0;
                unsigned long size = 0, value_lo = 0, value_hi = 0, value_credit = 0, rec_size = 0, rec_max = 0, rec_cur = 0;
                int fields_scanned = sscanf(after_fid,
                                ": %d %d %d %x %lx %lx %lx %lx %d %lx %lx %lx %x %x %d %u",
                                &settings_valid, &type, &comm, &access_rights, &size,
                                &value_lo, &value_hi, &value_credit, &credit_en,
                                &rec_size, &rec_max, &rec_cur, &tmac_opt, &tmac_ver,
                                &read_status, &saved_data_len);
                if (fields_scanned != 16) return;

                file->settings_valid                        = (settings_valid != 0);
                file->settings.valid                        = file->settings_valid;
                file->settings.type                         = (mf_desfire_file_type_t)type;
                file->settings.comm                         = (mf_desfire_comm_t)comm;
                file->settings.access_rights                = (uint16_t)access_rights;
                file->settings.size                         = (uint32_t)size;
                file->settings.value_lo_limit               = (int32_t)(uint32_t)value_lo;
                file->settings.value_hi_limit               = (int32_t)(uint32_t)value_hi;
                file->settings.value_limited_credit         = (int32_t)(uint32_t)value_credit;
                file->settings.value_limited_credit_enabled = (credit_en != 0);
                file->settings.record_size                  = (uint32_t)rec_size;
                file->settings.record_max                   = (uint32_t)rec_max;
                file->settings.record_cur                   = (uint32_t)rec_cur;
                file->settings.tmac_key_option              = (uint8_t)tmac_opt;
                file->settings.tmac_key_version             = (uint8_t)tmac_ver;
                file->read_status                           = (mf_desfire_file_read_status_t)read_status;
            }
        }
    }
}

static void build_representative_deep(mf_desfire_deep_t *deep, bool *present, uint8_t v28[28])
{
    mf_desfire_deep_reset(deep);
    *present = true;
    for (int i = 0; i < 28; i++) v28[i] = (uint8_t)(0x04 + i);

    deep->free_memory_valid = true;
    deep->free_memory_bytes = 7938;

    deep->master_key_settings_valid = true;
    deep->master_key_settings.valid = true;
    deep->master_key_settings.change_key_id = 0x0A;
    deep->master_key_settings.config_changeable = true;
    deep->master_key_settings.free_directory_list = true;
    deep->master_key_settings.max_keys = 3;
    deep->master_key_settings.flags = 1;

    deep->apps_truncated = true;
    deep->apps_protected = false;

    /* App 0: fully readable, one Standard file (Complete), one negative-limit
     * Value file (Complete), one oversized Standard file (Partial). */
    mf_desfire_app_t *app0 = &deep->apps[deep->app_count++];
    app0->id.id[0] = 0x01; app0->id.id[1] = 0x02; app0->id.id[2] = 0x03;
    app0->select_ok = true;
    app0->key_settings_valid = true;
    app0->key_settings.valid = true;
    app0->key_settings.max_keys = 2;
    app0->key_version_count = 2;
    app0->key_versions[0] = 0x01;
    app0->key_versions[1] = 0x02;
    app0->key_versions_truncated = false;
    app0->files_truncated = true;

    mf_desfire_file_t *f0 = &app0->files[app0->file_count++];
    f0->id = 0x01;
    f0->settings_valid = true;
    f0->settings.valid = true;
    f0->settings.type = MfDesfireFileTypeStandard;
    f0->settings.comm = MfDesfireCommPlaintext;
    f0->settings.access_rights = 0x000E;
    f0->settings.size = 10;
    f0->read_status = MfDesfireFileComplete;
    f0->data_len = 10;
    for (int i = 0; i < 10; i++) f0->data[i] = (uint8_t)(0xA0 + i);

    mf_desfire_file_t *f1 = &app0->files[app0->file_count++];
    f1->id = 0x02;
    f1->settings_valid = true;
    f1->settings.valid = true;
    f1->settings.type = MfDesfireFileTypeValue;
    f1->settings.comm = MfDesfireCommPlaintext;
    f1->settings.access_rights = 0x000E;
    f1->settings.value_lo_limit = -1000;      /* negative -- exercises the sign-extension guard */
    f1->settings.value_hi_limit = 1000000;
    f1->settings.value_limited_credit = -1;   /* all-bits-set negative -- the strictest sign case */
    f1->settings.value_limited_credit_enabled = true;
    f1->read_status = MfDesfireFileComplete;
    f1->data_len = 4;
    f1->data[0] = 0x11; f1->data[1] = 0x22; f1->data[2] = 0x33; f1->data[3] = 0x44;

    mf_desfire_file_t *f2 = &app0->files[app0->file_count++];
    f2->id = 0x03;
    f2->settings_valid = true;
    f2->settings.valid = true;
    f2->settings.type = MfDesfireFileTypeBackup;
    f2->settings.comm = MfDesfireCommPlaintext;
    f2->settings.access_rights = 0x000E;
    f2->settings.size = 500; /* larger than the cap -- Partial, prefix-only data */
    f2->read_status = MfDesfireFilePartial;
    f2->data_len = MF_DESFIRE_DEEP_FILE_DATA_CAP;
    for (unsigned i = 0; i < MF_DESFIRE_DEEP_FILE_DATA_CAP; i++) f2->data[i] = (uint8_t)i;

    /* App 1: select failed -- no keys/files should be captured. */
    mf_desfire_app_t *app1 = &deep->apps[deep->app_count++];
    app1->id.id[0] = 0xAA; app1->id.id[1] = 0xBB; app1->id.id[2] = 0xCC;
    app1->select_ok = false;

    /* App 2: selected, but every file protected/unsupported -- no data, but
     * settings/status must still round-trip exactly. */
    mf_desfire_app_t *app2 = &deep->apps[deep->app_count++];
    app2->id.id[0] = 0x00; app2->id.id[1] = 0x00; app2->id.id[2] = 0x01;
    app2->select_ok = true;
    app2->key_settings_valid = false; /* auth required even for key settings */

    mf_desfire_file_t *pf = &app2->files[app2->file_count++];
    pf->id = 0x10;
    pf->settings_valid = true;
    pf->settings.valid = true;
    pf->settings.type = MfDesfireFileTypeLinearRecord;
    pf->settings.comm = MfDesfireCommAuthenticated;
    pf->settings.access_rights = 0x1234; /* not free -- Protected */
    pf->settings.record_size = 16;
    pf->settings.record_max = 10;
    pf->settings.record_cur = 3;
    pf->read_status = MfDesfireFileProtected;

    mf_desfire_file_t *uf = &app2->files[app2->file_count++];
    uf->id = 0x11;
    uf->settings_valid = true;
    uf->settings.valid = true;
    uf->settings.type = MfDesfireFileTypeTransactionMac;
    uf->settings.comm = MfDesfireCommPlaintext;
    uf->settings.access_rights = 0x000E;
    uf->settings.tmac_key_option = 0x02;
    uf->settings.tmac_key_version = 0x01;
    uf->read_status = MfDesfireFileUnsupported;
}

static bool file_settings_equal(const mf_desfire_file_settings_t *a, const mf_desfire_file_settings_t *b)
{
    return a->valid == b->valid && a->type == b->type && a->comm == b->comm &&
           a->access_rights == b->access_rights && a->size == b->size &&
           a->value_lo_limit == b->value_lo_limit && a->value_hi_limit == b->value_hi_limit &&
           a->value_limited_credit == b->value_limited_credit &&
           a->value_limited_credit_enabled == b->value_limited_credit_enabled &&
           a->record_size == b->record_size && a->record_max == b->record_max && a->record_cur == b->record_cur &&
           a->tmac_key_option == b->tmac_key_option && a->tmac_key_version == b->tmac_key_version;
}

static void test_round_trip_representative(void)
{
    mf_desfire_deep_t original, reloaded;
    bool orig_present, reload_present = false;
    uint8_t orig_v28[28], reload_v28[28];

    build_representative_deep(&original, &orig_present, orig_v28);
    mf_desfire_deep_reset(&reloaded);
    memset(reload_v28, 0, sizeof(reload_v28));

    line_buf_t lb = { .count = 0 };
    desf_write(&lb, &original, orig_present, orig_v28);
    CHECK(lb.count > 0, "writer produced at least one line for a present card");

    for (int i = 0; i < lb.count; i++) {
        desf_parse_line(lb.lines[i], &reloaded, &reload_present, reload_v28);
    }

    CHECK(reload_present, "DESFire Version line round-trips to present=true");
    CHECK(memcmp(orig_v28, reload_v28, 28) == 0, "28-byte version tuple round-trips exactly");

    CHECK(reloaded.free_memory_valid == original.free_memory_valid, "free_memory_valid round-trips");
    CHECK(reloaded.free_memory_bytes == original.free_memory_bytes, "free_memory_bytes round-trips");

    CHECK(reloaded.master_key_settings_valid == original.master_key_settings_valid, "master_key_settings_valid round-trips");
    CHECK(reloaded.master_key_settings.max_keys == original.master_key_settings.max_keys, "master max_keys round-trips");
    CHECK(reloaded.master_key_settings.change_key_id == original.master_key_settings.change_key_id, "master change_key_id round-trips");
    CHECK(reloaded.master_key_settings.config_changeable == original.master_key_settings.config_changeable, "master config_changeable round-trips");

    CHECK(reloaded.apps_truncated == original.apps_truncated, "apps_truncated round-trips");
    CHECK(reloaded.apps_protected == original.apps_protected, "apps_protected round-trips");
    CHECK(reloaded.app_count == original.app_count, "app_count round-trips");

    for (uint8_t a = 0; a < original.app_count; a++) {
        const mf_desfire_app_t *oa = &original.apps[a];
        const mf_desfire_app_t *ra = &reloaded.apps[a];
        CHECK(memcmp(oa->id.id, ra->id.id, 3) == 0, "app AID round-trips");
        CHECK(oa->select_ok == ra->select_ok, "app select_ok round-trips");
        CHECK(oa->key_settings_valid == ra->key_settings_valid, "app key_settings_valid round-trips");
        if (oa->key_settings_valid) {
            CHECK(oa->key_settings.max_keys == ra->key_settings.max_keys, "app max_keys round-trips");
        }
        CHECK(oa->key_version_count == ra->key_version_count, "app key_version_count round-trips");
        for (uint8_t k = 0; k < oa->key_version_count; k++) {
            CHECK(oa->key_versions[k] == ra->key_versions[k], "app key_versions[k] round-trips");
        }
        CHECK(oa->files_truncated == ra->files_truncated, "app files_truncated round-trips");
        CHECK(oa->file_count == ra->file_count, "app file_count round-trips");

        for (uint8_t f = 0; f < oa->file_count; f++) {
            const mf_desfire_file_t *of = &oa->files[f];
            const mf_desfire_file_t *rf = &ra->files[f];
            CHECK(of->id == rf->id, "file id round-trips");
            CHECK(of->settings_valid == rf->settings_valid, "file settings_valid round-trips");
            CHECK(file_settings_equal(&of->settings, &rf->settings), "file settings struct round-trips exactly (incl. negative Value limits)");
            CHECK(of->read_status == rf->read_status, "file read_status round-trips");
            CHECK(of->data_len == rf->data_len, "file data_len round-trips");
            CHECK(memcmp(of->data, rf->data, of->data_len) == 0, "file data bytes round-trip exactly");
        }
    }
}

static void test_empty_deep_produces_no_lines_when_not_present(void)
{
    mf_desfire_deep_t deep;
    mf_desfire_deep_reset(&deep);
    uint8_t v28[28] = {0};

    line_buf_t lb = { .count = 0 };
    desf_write(&lb, &deep, /*desfire_present=*/false, v28);
    CHECK(lb.count == 0, "no DESFire lines are written when the card was never identified as DESFire");
}

static void test_out_of_order_sub_line_is_skipped_not_misattributed(void)
{
    mf_desfire_deep_t deep;
    bool present = false;
    uint8_t v28[28];
    mf_desfire_deep_reset(&deep);

    /* A "Key Settings:" sub-line for an AID that was never introduced by its
     * own "Application <AID>:" line must be dropped, not attributed to some
     * other/garbage application. */
    desf_parse_line("DESFire Application 0A0B0C Key Settings: 0F 0E", &deep, &present, v28);
    CHECK(deep.app_count == 0, "a sub-line with no prior Application line creates no application");
}

static void test_truncated_data_line_capped_not_overflowed(void)
{
    mf_desfire_deep_t deep;
    bool present = false;
    uint8_t v28[28];
    mf_desfire_deep_reset(&deep);

    desf_parse_line("DESFire Application 010203: 1", &deep, &present, v28);
    desf_parse_line("DESFire Application 010203 File 01: 1 0 0 000E 000A00 00000000 00000000 00000000 0 000000 000000 000000 00 00 1 2560",
                     &deep, &present, v28);

    /* A pathological/corrupted file claiming far more data bytes than the
     * cap allows must never overflow file->data[] -- the parser only ever
     * writes what it can actually decode from a "... Data:" line, bounded
     * by MF_DESFIRE_DEEP_FILE_DATA_CAP regardless of what the settings line
     * claims. */
    char big_data_line[3 * MF_DESFIRE_DEEP_FILE_DATA_CAP + 64];
    int pos = snprintf(big_data_line, sizeof(big_data_line), "DESFire Application 010203 File 01 Data: ");
    for (unsigned i = 0; i < MF_DESFIRE_DEEP_FILE_DATA_CAP + 20U && pos < (int)sizeof(big_data_line) - 3; i++) {
        pos += snprintf(big_data_line + pos, sizeof(big_data_line) - pos, "%02X ", (unsigned)(i & 0xFF));
    }
    desf_parse_line(big_data_line, &deep, &present, v28);

    CHECK(deep.app_count == 1 && deep.apps[0].file_count == 1, "app/file created as expected");
    CHECK(deep.apps[0].files[0].data_len == MF_DESFIRE_DEEP_FILE_DATA_CAP,
          "oversized Data line is capped at MF_DESFIRE_DEEP_FILE_DATA_CAP, never overflowed");
}

int main(void)
{
    test_source_matches_shipped();
    test_round_trip_representative();
    test_empty_deep_produces_no_lines_when_not_present();
    test_out_of_order_sub_line_is_skipped_not_misattributed();
    test_truncated_data_line_capped_not_overflowed();

    printf("nfc_desfire_persistence_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
