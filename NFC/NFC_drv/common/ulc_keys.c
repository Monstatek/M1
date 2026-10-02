/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    ulc_keys.c
 * @brief   MIFARE Ultralight C key store + user dictionary.
 *
 * See ulc_keys.h. Candidate keys + user management only -- no card
 * authentication and no 3DES/crypto here.
 */
/*============================================================================*/
#include "ulc_keys.h"
#include <string.h>
#include "nfc_dict_line.h"
#include "ff.h"      /* f_open / FRESULT: distinguish missing file vs read error */

/*----------------------------------------------------------------------------*/
/* Built-in default key: the public, well-known NXP Ultralight C factory 2K3DES */
/* key (49 45 4D 4B 41 45 52 42 21 4E 41 43 55 4F 59 46). Kept intentionally    */
/* minimal; every other public/user key belongs in the SD user dictionary.      */
/*----------------------------------------------------------------------------*/
static const uint8_t s_builtin[][ULC_KEY_SIZE] = {
    { 0x49, 0x45, 0x4D, 0x4B, 0x41, 0x45, 0x52, 0x42,
      0x21, 0x4E, 0x41, 0x43, 0x55, 0x4F, 0x59, 0x46 },   /* NXP factory default */
};
#define BUILTIN_N ((uint16_t)(sizeof(s_builtin) / sizeof(s_builtin[0])))

uint16_t ulc_keys_builtin_count(void)
{
    return BUILTIN_N;
}

bool ulc_keys_builtin_get(uint16_t idx, uint8_t out[ULC_KEY_SIZE])
{
    if ((idx >= BUILTIN_N) || (out == NULL)) return false;
    memcpy(out, s_builtin[idx], ULC_KEY_SIZE);
    return true;
}

/*----------------------------------------------------------------------------*/
/* Helpers                                                                     */
/*----------------------------------------------------------------------------*/


static bool key_eq(const uint8_t* a, const uint8_t* b)
{
    return memcmp(a, b, ULC_KEY_SIZE) == 0;
}

/* Probe one candidate path: openable, absent, or present-but-read-error. */
static ulc_keys_src_state_t probe_path(const char* path)
{
    FIL     f;
    FRESULT r = f_open(&f, path, FA_READ);
    if (r == FR_OK) { f_close(&f); return ULC_KEYS_SRC_LOADED; }
    if ((r == FR_NO_FILE) || (r == FR_NO_PATH)) return ULC_KEYS_SRC_NONE;
    return ULC_KEYS_SRC_READ_ERROR;
}

/* Open one dictionary for reading. If path is NULL, default_path is used.
 * Reports the selected path and source state without ever creating a file. */
static int open_dict(nfcfio_t* io, const char* path, const char* default_path,
                     const char** used, ulc_keys_src_state_t* st)
{
    const char* cand = (path != NULL) ? path : default_path;

    if (used != NULL) *used = NULL;
    if (st   != NULL) *st   = ULC_KEYS_SRC_NONE;

    ulc_keys_src_state_t s = probe_path(cand);
    if (s == ULC_KEYS_SRC_NONE) return 0;              /* absent */
    if (used != NULL) *used = cand;
    if (s == ULC_KEYS_SRC_READ_ERROR) {
        if (st != NULL) *st = ULC_KEYS_SRC_READ_ERROR;
        return 0;
    }
    if (nfcfio_open_read(io, cand) == 1) {
        if (st != NULL) *st = ULC_KEYS_SRC_LOADED;
        return 1;
    }
    if (st != NULL) *st = ULC_KEYS_SRC_READ_ERROR;
    return 0;
}

/*----------------------------------------------------------------------------*/
ulc_line_kind_t ulc_keys_parse_line(const char *line, uint8_t out[ULC_KEY_SIZE])
{
    switch (nfc_dict_parse_line(line, out, ULC_KEY_SIZE)) {
    case NFC_DICT_LINE_SKIP: return ULC_LINE_SKIP;
    case NFC_DICT_LINE_KEY:  return ULC_LINE_KEY;
    default:                 return ULC_LINE_BAD;
    }
}

/*----------------------------------------------------------------------------*/
/* Streaming iterator                                                          */
/*----------------------------------------------------------------------------*/
void ulc_keys_iter_begin(ulc_keys_iter_t* it, const char* user_path)
{
    ulc_keys_iter_begin_paths(it, user_path, NULL);
}

void ulc_keys_iter_begin_paths(ulc_keys_iter_t* it, const char* user_path,
                               const char* system_path)
{
    if (it == NULL) return;
    memset(it, 0, sizeof(*it));
    it->builtin_idx = 0;
    it->phase       = ULC_KEYS_PHASE_USER;
    it->user_path   = (user_path != NULL) ? user_path : ULC_KEYS_USER_PATH;
    it->system_path = (system_path != NULL) ? system_path : ULC_KEYS_SYSTEM_PATH;
    it->file_open   = (open_dict(&it->io, it->user_path, it->user_path,
                                 NULL, NULL) == 1);
}

static bool iter_seen(const ulc_keys_iter_t* it, const uint8_t key[ULC_KEY_SIZE])
{
    for (uint16_t i = 0; i < it->seen_count; i++) {
        if (key_eq(it->seen[i], key)) return true;
    }
    return false;
}

static void iter_remember(ulc_keys_iter_t* it, const uint8_t key[ULC_KEY_SIZE])
{
    if (it->seen_count >= ULC_KEYS_MAX) return;
    memcpy(it->seen[it->seen_count], key, ULC_KEY_SIZE);
    it->seen_count++;
}

static void iter_close_file(ulc_keys_iter_t* it)
{
    if (!it->file_open) return;
    nfcfio_close(&it->io);
    it->file_open = false;
}

static bool iter_open_phase(ulc_keys_iter_t* it)
{
    const char* path = (it->phase == ULC_KEYS_PHASE_USER) ? it->user_path :
                       (it->phase == ULC_KEYS_PHASE_SYSTEM ? it->system_path : NULL);
    if (path == NULL) return false;
    it->file_open = (open_dict(&it->io, path, path, NULL, NULL) == 1);
    return it->file_open;
}

bool ulc_keys_iter_next(ulc_keys_iter_t* it, uint8_t out[ULC_KEY_SIZE])
{
    if ((it == NULL) || (out == NULL)) return false;

    if (it->phase == ULC_KEYS_PHASE_FALLBACK) {
        if (it->builtin_idx < BUILTIN_N) {
            memcpy(out, s_builtin[it->builtin_idx], ULC_KEY_SIZE);
            it->builtin_idx++;
            return true;
        }
        it->phase = ULC_KEYS_PHASE_DONE;
        return false;
    }

    while (it->phase < ULC_KEYS_PHASE_DONE) {
        if (!it->file_open) {
            if (it->phase == ULC_KEYS_PHASE_USER) {
                it->phase = ULC_KEYS_PHASE_SYSTEM;
                if (!iter_open_phase(it)) it->phase = ULC_KEYS_PHASE_FALLBACK;
            } else if (it->phase == ULC_KEYS_PHASE_SYSTEM) {
                it->phase = ULC_KEYS_PHASE_DONE;
            }
            if (it->phase == ULC_KEYS_PHASE_FALLBACK) {
                return ulc_keys_iter_next(it, out);
            }
            continue;
        }

        int r = nfcfio_getline(&it->io, it->line, sizeof(it->line));
        if (r < 0) {
            iter_close_file(it);
            if (it->phase == ULC_KEYS_PHASE_SYSTEM) it->phase = ULC_KEYS_PHASE_DONE;
            continue;
        }

        uint8_t k[ULC_KEY_SIZE];
        if (ulc_keys_parse_line(it->line, k) != ULC_LINE_KEY) continue;
        if (iter_seen(it, k)) continue;

        memcpy(out, k, ULC_KEY_SIZE);
        if (it->phase == ULC_KEYS_PHASE_USER) iter_remember(it, k);
        return true;
    }
    return false;
}

void ulc_keys_iter_end(ulc_keys_iter_t* it)
{
    if (it != NULL) iter_close_file(it);
}

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot                                                        */
/*----------------------------------------------------------------------------*/
void ulc_keys_reset(ulc_keys_t* s)
{
    if (s == NULL) return;
    s->count = 0;
    s->user_count = 0;
    s->system_count = 0;
    for (uint16_t i = 0; (i < BUILTIN_N) && (s->count < ULC_KEYS_MAX); i++) {
        memcpy(s->keys[s->count], s_builtin[i], ULC_KEY_SIZE);
        s->count++;
    }
    s->builtin_count = s->count;
}

bool ulc_keys_add(ulc_keys_t* s, const uint8_t key[ULC_KEY_SIZE])
{
    if ((s == NULL) || (key == NULL)) return false;
    for (uint16_t i = 0; i < s->count; i++) {
        if (key_eq(s->keys[i], key)) return true;   /* duplicate: no-op success */
    }
    if (s->count >= ULC_KEYS_MAX) return false;      /* bounded */
    uint16_t insert = (uint16_t)(s->builtin_count + s->user_count);
    for (uint16_t i = s->count; i > insert; i--) {
        memcpy(s->keys[i], s->keys[i - 1], ULC_KEY_SIZE);
    }
    memcpy(s->keys[insert], key, ULC_KEY_SIZE);
    s->count++;
    s->user_count++;
    return true;
}

bool ulc_keys_remove_at(ulc_keys_t* s, uint16_t idx)
{
    if (s == NULL) return false;
    uint16_t user_end = (uint16_t)(s->builtin_count + s->user_count);
    if ((idx < s->builtin_count) || (idx >= user_end)) return false;
    for (uint16_t i = idx; i + 1 < s->count; i++) {
        memcpy(s->keys[i], s->keys[i + 1], ULC_KEY_SIZE);
    }
    s->count--;
    s->user_count--;
    return true;
}

static ulc_keys_load_status_t load_file(ulc_keys_t* s, const char* path,
                                        const char* default_path, bool system)
{
    ulc_keys_load_status_t st;
    st.ok = false;
    st.state = ULC_KEYS_SRC_NONE;
    st.loaded = 0;
    st.valid = 0;
    st.skipped_malformed = 0;
    st.truncated = false;
    st.source_path = NULL;

    if (s == NULL) return st;
    if (s->count == 0) ulc_keys_reset(s);

    nfcfio_t io;
    if (open_dict(&io, path, default_path, &st.source_path, &st.state) != 1) {
        return st;   /* missing / read error: built-ins remain usable */
    }
    st.ok = true;

    char line[96];
    for (;;) {
        int r = nfcfio_getline(&io, line, sizeof(line));
        if (r < 0) break;

        uint8_t k[ULC_KEY_SIZE];
        ulc_line_kind_t kind = ulc_keys_parse_line(line, k);
        if (kind == ULC_LINE_SKIP) continue;
        if (kind == ULC_LINE_BAD)  { st.skipped_malformed++; continue; }
        st.valid++;

        bool dup = false;
        for (uint16_t i = 0; i < s->count; i++) {
            if (key_eq(s->keys[i], k)) { dup = true; break; }
        }
        if (dup) continue;

        if (s->count >= ULC_KEYS_MAX) { st.truncated = true; break; }
        if (system) {
            memcpy(s->keys[s->count], k, ULC_KEY_SIZE);
            s->system_count++;
        } else {
            uint16_t insert = (uint16_t)(s->builtin_count + s->user_count);
            for (uint16_t i = s->count; i > insert; i--) {
                memcpy(s->keys[i], s->keys[i - 1], ULC_KEY_SIZE);
            }
            memcpy(s->keys[insert], k, ULC_KEY_SIZE);
            s->user_count++;
        }
        s->count++;
        st.loaded++;
    }

    nfcfio_close(&io);
    return st;
}

ulc_keys_load_status_t ulc_keys_load_user(ulc_keys_t* s, const char* user_path)
{
    return load_file(s, user_path, ULC_KEYS_USER_PATH, false);
}

ulc_keys_load_status_t ulc_keys_load_system(ulc_keys_t* s, const char* system_path)
{
    return load_file(s, system_path, ULC_KEYS_SYSTEM_PATH, true);
}

uint16_t ulc_keys_count(const ulc_keys_t* s)
{
    return (s != NULL) ? s->count : 0U;
}

bool ulc_keys_get(const ulc_keys_t* s, uint16_t idx, uint8_t out[ULC_KEY_SIZE])
{
    if ((s == NULL) || (out == NULL) || (idx >= s->count)) return false;
    memcpy(out, s->keys[idx], ULC_KEY_SIZE);
    return true;
}

bool ulc_keys_is_builtin(const ulc_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx < s->builtin_count);
}

bool ulc_keys_is_user(const ulc_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx >= s->builtin_count) &&
           (idx < (uint16_t)(s->builtin_count + s->user_count));
}

bool ulc_keys_is_system(const ulc_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx >= (uint16_t)(s->builtin_count + s->user_count)) &&
           (idx < s->count);
}

int ulc_keys_save_user(const ulc_keys_t* s)
{
    if (s == NULL) return 0;

    nfcfio_t io;
    if (nfcfio_open_write(&io, ULC_KEYS_USER_PATH) != 1) return 0;
    nfcfio_set_crlf(&io, 0);   /* LF-delimited interoperable text */

    static const char hexd[] = "0123456789ABCDEF";
    int ok = 1;
    uint16_t user_end = (uint16_t)(s->builtin_count + s->user_count);
    for (uint16_t i = s->builtin_count; (i < user_end) && ok; i++) {
        char row[ULC_KEY_HEXLEN + 1];
        for (uint16_t b = 0; b < ULC_KEY_SIZE; b++) {
            row[2U * b]      = hexd[(s->keys[i][b] >> 4) & 0x0FU];
            row[2U * b + 1U] = hexd[s->keys[i][b] & 0x0FU];
        }
        row[ULC_KEY_HEXLEN] = '\0';
        if (nfcfio_putline(&io, row) != 1) ok = 0;
    }

    nfcfio_close(&io);
    return ok;
}
