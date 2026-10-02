/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    ntag_pwd_keys.c
 * @brief   MIFARE Ultralight EV1 (UL11) / NTAG213/215/216 PWD_AUTH password
 *          store + user dictionary.
 *
 * See ntag_pwd_keys.h. Candidate passwords + user management only -- no
 * card authentication here (that lives in nfc_poller.c's nfc_unlock_run()).
 * Structurally identical to ulc_keys.c; kept as a separate module because
 * the credential type/size and file format are genuinely different (see
 * ntag_pwd_keys.h's header comment).
 */
/*============================================================================*/
#include "ntag_pwd_keys.h"
#include <string.h>
#include "nfc_dict_line.h"
#include "ff.h"      /* f_open / FRESULT: distinguish missing file vs read error */

/*----------------------------------------------------------------------------*/
/* Built-in default password: the well-known NXP factory default PWD for      */
/* UL11/NTAG21x (0xFFFFFFFF -- documented default, not a secret). Kept        */
/* intentionally minimal; every other candidate belongs in the SD user        */
/* dictionary.                                                                 */
/*----------------------------------------------------------------------------*/
static const uint8_t s_builtin[][NTAG_PWD_KEY_SIZE] = {
    { 0xFF, 0xFF, 0xFF, 0xFF },   /* NXP factory default */
};
#define BUILTIN_N ((uint16_t)(sizeof(s_builtin) / sizeof(s_builtin[0])))

uint16_t ntag_pwd_keys_builtin_count(void)
{
    return BUILTIN_N;
}

bool ntag_pwd_keys_builtin_get(uint16_t idx, uint8_t out[NTAG_PWD_KEY_SIZE])
{
    if ((idx >= BUILTIN_N) || (out == NULL)) return false;
    memcpy(out, s_builtin[idx], NTAG_PWD_KEY_SIZE);
    return true;
}

/*----------------------------------------------------------------------------*/
/* Helpers                                                                     */
/*----------------------------------------------------------------------------*/


static bool key_eq(const uint8_t* a, const uint8_t* b)
{
    return memcmp(a, b, NTAG_PWD_KEY_SIZE) == 0;
}

/* Probe one candidate path: openable, absent, or present-but-read-error. */
static ntag_pwd_keys_src_state_t probe_path(const char* path)
{
    FIL     f;
    FRESULT r = f_open(&f, path, FA_READ);
    if (r == FR_OK) { f_close(&f); return NTAG_PWD_KEYS_SRC_LOADED; }
    if ((r == FR_NO_FILE) || (r == FR_NO_PATH)) return NTAG_PWD_KEYS_SRC_NONE;
    return NTAG_PWD_KEYS_SRC_READ_ERROR;
}

/* Open one dictionary for reading. If path is NULL, default_path is used. */
static int open_dict(nfcfio_t* io, const char* path, const char* default_path,
                     const char** used, ntag_pwd_keys_src_state_t* st)
{
    const char* cand = (path != NULL) ? path : default_path;

    if (used != NULL) *used = NULL;
    if (st   != NULL) *st   = NTAG_PWD_KEYS_SRC_NONE;

    ntag_pwd_keys_src_state_t s = probe_path(cand);
    if (s == NTAG_PWD_KEYS_SRC_NONE) return 0;              /* absent */
    if (used != NULL) *used = cand;
    if (s == NTAG_PWD_KEYS_SRC_READ_ERROR) {
        if (st != NULL) *st = NTAG_PWD_KEYS_SRC_READ_ERROR;
        return 0;
    }
    if (nfcfio_open_read(io, cand) == 1) {
        if (st != NULL) *st = NTAG_PWD_KEYS_SRC_LOADED;
        return 1;
    }
    if (st != NULL) *st = NTAG_PWD_KEYS_SRC_READ_ERROR;
    return 0;
}

/*----------------------------------------------------------------------------*/
ntag_pwd_line_kind_t ntag_pwd_keys_parse_line(const char *line, uint8_t out[NTAG_PWD_KEY_SIZE])
{
    switch (nfc_dict_parse_line(line, out, NTAG_PWD_KEY_SIZE)) {
    case NFC_DICT_LINE_SKIP: return NTAG_PWD_LINE_SKIP;
    case NFC_DICT_LINE_KEY:  return NTAG_PWD_LINE_KEY;
    default:                 return NTAG_PWD_LINE_BAD;
    }
}

/*----------------------------------------------------------------------------*/
/* Streaming iterator                                                          */
/*----------------------------------------------------------------------------*/
void ntag_pwd_keys_iter_begin(ntag_pwd_keys_iter_t* it, const char* user_path)
{
    ntag_pwd_keys_iter_begin_paths(it, user_path, NULL);
}

void ntag_pwd_keys_iter_begin_paths(ntag_pwd_keys_iter_t* it, const char* user_path,
                                    const char* system_path)
{
    if (it == NULL) return;
    memset(it, 0, sizeof(*it));
    it->builtin_idx = 0;
    it->phase       = NTAG_PWD_KEYS_PHASE_USER;
    it->user_path   = (user_path != NULL) ? user_path : NTAG_PWD_KEYS_USER_PATH;
    it->system_path = (system_path != NULL) ? system_path : NTAG_PWD_KEYS_SYSTEM_PATH;
    it->file_open   = (open_dict(&it->io, it->user_path, it->user_path, NULL, NULL) == 1);
}

static bool iter_seen(const ntag_pwd_keys_iter_t* it, const uint8_t key[NTAG_PWD_KEY_SIZE])
{
    for (uint16_t i = 0; i < it->seen_count; i++) {
        if (key_eq(it->seen[i], key)) return true;
    }
    return false;
}

static void iter_remember(ntag_pwd_keys_iter_t* it, const uint8_t key[NTAG_PWD_KEY_SIZE])
{
    if (it->seen_count >= NTAG_PWD_KEYS_MAX) return;
    memcpy(it->seen[it->seen_count], key, NTAG_PWD_KEY_SIZE);
    it->seen_count++;
}

static void iter_close_file(ntag_pwd_keys_iter_t* it)
{
    if (!it->file_open) return;
    nfcfio_close(&it->io);
    it->file_open = false;
}

static bool iter_open_phase(ntag_pwd_keys_iter_t* it)
{
    const char* path = (it->phase == NTAG_PWD_KEYS_PHASE_USER) ? it->user_path :
                       (it->phase == NTAG_PWD_KEYS_PHASE_SYSTEM ? it->system_path : NULL);
    if (path == NULL) return false;
    it->file_open = (open_dict(&it->io, path, path, NULL, NULL) == 1);
    return it->file_open;
}

bool ntag_pwd_keys_iter_next(ntag_pwd_keys_iter_t* it, uint8_t out[NTAG_PWD_KEY_SIZE])
{
    if ((it == NULL) || (out == NULL)) return false;

    if (it->phase == NTAG_PWD_KEYS_PHASE_FALLBACK) {
        if (it->builtin_idx < BUILTIN_N) {
            memcpy(out, s_builtin[it->builtin_idx], NTAG_PWD_KEY_SIZE);
            it->builtin_idx++;
            return true;
        }
        it->phase = NTAG_PWD_KEYS_PHASE_DONE;
        return false;
    }

    while (it->phase < NTAG_PWD_KEYS_PHASE_DONE) {
        if (!it->file_open) {
            if (it->phase == NTAG_PWD_KEYS_PHASE_USER) {
                it->phase = NTAG_PWD_KEYS_PHASE_SYSTEM;
                if (!iter_open_phase(it)) it->phase = NTAG_PWD_KEYS_PHASE_FALLBACK;
            } else if (it->phase == NTAG_PWD_KEYS_PHASE_SYSTEM) {
                it->phase = NTAG_PWD_KEYS_PHASE_DONE;
            }
            if (it->phase == NTAG_PWD_KEYS_PHASE_FALLBACK) {
                return ntag_pwd_keys_iter_next(it, out);
            }
            continue;
        }

        int r = nfcfio_getline(&it->io, it->line, sizeof(it->line));
        if (r < 0) {
            iter_close_file(it);
            if (it->phase == NTAG_PWD_KEYS_PHASE_SYSTEM) it->phase = NTAG_PWD_KEYS_PHASE_DONE;
            continue;
        }

        uint8_t k[NTAG_PWD_KEY_SIZE];
        if (ntag_pwd_keys_parse_line(it->line, k) != NTAG_PWD_LINE_KEY) continue;
        if (iter_seen(it, k)) continue;

        memcpy(out, k, NTAG_PWD_KEY_SIZE);
        if (it->phase == NTAG_PWD_KEYS_PHASE_USER) iter_remember(it, k);
        return true;
    }
    return false;
}

void ntag_pwd_keys_iter_end(ntag_pwd_keys_iter_t* it)
{
    if (it != NULL) iter_close_file(it);
}

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot                                                        */
/*----------------------------------------------------------------------------*/
void ntag_pwd_keys_reset(ntag_pwd_keys_t* s)
{
    if (s == NULL) return;
    s->count = 0;
    s->user_count = 0;
    s->system_count = 0;
    for (uint16_t i = 0; (i < BUILTIN_N) && (s->count < NTAG_PWD_KEYS_MAX); i++) {
        memcpy(s->keys[s->count], s_builtin[i], NTAG_PWD_KEY_SIZE);
        s->count++;
    }
    s->builtin_count = s->count;
}

bool ntag_pwd_keys_add(ntag_pwd_keys_t* s, const uint8_t key[NTAG_PWD_KEY_SIZE])
{
    if ((s == NULL) || (key == NULL)) return false;
    for (uint16_t i = 0; i < s->count; i++) {
        if (key_eq(s->keys[i], key)) return true;   /* duplicate: no-op success */
    }
    if (s->count >= NTAG_PWD_KEYS_MAX) return false;  /* bounded */
    uint16_t insert = (uint16_t)(s->builtin_count + s->user_count);
    for (uint16_t i = s->count; i > insert; i--) {
        memcpy(s->keys[i], s->keys[i - 1], NTAG_PWD_KEY_SIZE);
    }
    memcpy(s->keys[insert], key, NTAG_PWD_KEY_SIZE);
    s->count++;
    s->user_count++;
    return true;
}

bool ntag_pwd_keys_remove_at(ntag_pwd_keys_t* s, uint16_t idx)
{
    if (s == NULL) return false;
    uint16_t user_end = (uint16_t)(s->builtin_count + s->user_count);
    if (idx < s->builtin_count || idx >= user_end) return false;
    for (uint16_t i = idx; i + 1 < s->count; i++) {
        memcpy(s->keys[i], s->keys[i + 1], NTAG_PWD_KEY_SIZE);
    }
    s->count--;
    s->user_count--;
    return true;
}

static ntag_pwd_keys_load_status_t load_file(ntag_pwd_keys_t* s, const char* path,
                                             const char* default_path, bool system)
{
    ntag_pwd_keys_load_status_t st;
    st.ok = false;
    st.state = NTAG_PWD_KEYS_SRC_NONE;
    st.loaded = 0;
    st.valid = 0;
    st.skipped_malformed = 0;
    st.truncated = false;
    st.source_path = NULL;

    if (s == NULL) return st;
    if (s->count == 0) ntag_pwd_keys_reset(s);

    nfcfio_t io;
    if (open_dict(&io, path, default_path, &st.source_path, &st.state) != 1) {
        return st;   /* missing / read error: built-ins remain usable */
    }
    st.ok = true;

    char line[32];
    for (;;) {
        int r = nfcfio_getline(&io, line, sizeof(line));
        if (r < 0) break;

        uint8_t k[NTAG_PWD_KEY_SIZE];
        ntag_pwd_line_kind_t kind = ntag_pwd_keys_parse_line(line, k);
        if (kind == NTAG_PWD_LINE_SKIP) continue;
        if (kind == NTAG_PWD_LINE_BAD)  { st.skipped_malformed++; continue; }
        st.valid++;

        bool dup = false;
        for (uint16_t i = 0; i < s->count; i++) {
            if (key_eq(s->keys[i], k)) { dup = true; break; }
        }
        if (dup) continue;

        if (s->count >= NTAG_PWD_KEYS_MAX) { st.truncated = true; break; }
        if (system) {
            memcpy(s->keys[s->count], k, NTAG_PWD_KEY_SIZE);
            s->system_count++;
        } else {
            uint16_t insert = (uint16_t)(s->builtin_count + s->user_count);
            for (uint16_t i = s->count; i > insert; i--) {
                memcpy(s->keys[i], s->keys[i - 1], NTAG_PWD_KEY_SIZE);
            }
            memcpy(s->keys[insert], k, NTAG_PWD_KEY_SIZE);
            s->user_count++;
        }
        s->count++;
        st.loaded++;
    }

    nfcfio_close(&io);
    return st;
}

ntag_pwd_keys_load_status_t ntag_pwd_keys_load_user(ntag_pwd_keys_t* s, const char* user_path)
{
    return load_file(s, user_path, NTAG_PWD_KEYS_USER_PATH, false);
}

ntag_pwd_keys_load_status_t ntag_pwd_keys_load_system(ntag_pwd_keys_t* s, const char* system_path)
{
    return load_file(s, system_path, NTAG_PWD_KEYS_SYSTEM_PATH, true);
}

uint16_t ntag_pwd_keys_count(const ntag_pwd_keys_t* s)
{
    return (s != NULL) ? s->count : 0U;
}

bool ntag_pwd_keys_get(const ntag_pwd_keys_t* s, uint16_t idx, uint8_t out[NTAG_PWD_KEY_SIZE])
{
    if ((s == NULL) || (out == NULL) || (idx >= s->count)) return false;
    memcpy(out, s->keys[idx], NTAG_PWD_KEY_SIZE);
    return true;
}

bool ntag_pwd_keys_is_builtin(const ntag_pwd_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx < s->builtin_count);
}

bool ntag_pwd_keys_is_user(const ntag_pwd_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx >= s->builtin_count) &&
           (idx < (uint16_t)(s->builtin_count + s->user_count));
}

bool ntag_pwd_keys_is_system(const ntag_pwd_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx >= (uint16_t)(s->builtin_count + s->user_count)) &&
           (idx < s->count);
}

int ntag_pwd_keys_save_user(const ntag_pwd_keys_t* s)
{
    if (s == NULL) return 0;

    nfcfio_t io;
    if (nfcfio_open_write(&io, NTAG_PWD_KEYS_USER_PATH) != 1) return 0;
    nfcfio_set_crlf(&io, 0);   /* LF-delimited interoperable text */

    static const char hexd[] = "0123456789ABCDEF";
    int ok = 1;
    uint16_t user_end = (uint16_t)(s->builtin_count + s->user_count);
    for (uint16_t i = s->builtin_count; (i < user_end) && ok; i++) {
        char row[NTAG_PWD_KEY_HEXLEN + 1];
        for (uint16_t b = 0; b < NTAG_PWD_KEY_SIZE; b++) {
            row[2U * b]      = hexd[(s->keys[i][b] >> 4) & 0x0FU];
            row[2U * b + 1U] = hexd[s->keys[i][b] & 0x0FU];
        }
        row[NTAG_PWD_KEY_HEXLEN] = '\0';
        if (nfcfio_putline(&io, row) != 1) ok = 0;
    }

    nfcfio_close(&io);
    return ok;
}
