/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_keys.c
 * @brief   MIFARE Classic key store + user dictionary management (add/list/
 *          delete/persist). See mfc_keys.h.
 *
 * Line parsing, canonical-then-compatible path precedence, and
 * candidate-key iteration/dedup live in mfc_key_source.h/.c (the single
 * seam-tested source-aware module) -- this file no longer duplicates any of
 * that. It owns only what is genuinely specific to the key-manager tool: the
 * compiled built-in array, the bounded UI snapshot, and the atomic
 * add/remove/persist operations a card scan never needs.
 */
/*============================================================================*/
#include "mfc_keys.h"
#include "mfc_key_source_sd.h"
#include <string.h>
#include <stdio.h>   /* snprintf: build the ".tmp" sibling path */
#include "ff.h"      /* f_unlink/f_rename/FRESULT: atomic save                */

/*----------------------------------------------------------------------------*/
/* Built-in default keys: public, well-known, generic MIFARE Classic keys.     */
/* Kept intentionally small; the user dictionary carries everything else.      */
/* (These are the canonical public defaults, e.g. the head of any public dict.)*/
/*----------------------------------------------------------------------------*/
static const uint8_t s_builtin[][MFC_KEY_SIZE] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },   /* factory default            */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* blank                      */
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },   /* MAD / NFC Forum key A      */
    { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5 },   /* common                     */
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 },   /* NDEF                       */
    { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },   /* common                     */
};
#define BUILTIN_N ((uint16_t)(sizeof(s_builtin) / sizeof(s_builtin[0])))

uint16_t mfc_keys_builtin_count(void)
{
    return BUILTIN_N;
}

bool mfc_keys_builtin_get(uint16_t idx, uint8_t out[MFC_KEY_SIZE])
{
    if ((idx >= BUILTIN_N) || (out == NULL)) return false;
    memcpy(out, s_builtin[idx], MFC_KEY_SIZE);
    return true;
}

const uint8_t (*mfc_keys_builtin_array(void))[MFC_KEY_SIZE]
{
    return s_builtin;
}

static bool key_eq(const uint8_t* a, const uint8_t* b)
{
    return memcmp(a, b, MFC_KEY_SIZE) == 0;
}

/*----------------------------------------------------------------------------*/
/* User-dictionary path resolution: delegates entirely to mfc_key_source's    */
/* canonical-then-compatible-fallback precedence (no local re-implementation). */
/*----------------------------------------------------------------------------*/
static mfc_key_source_cfg_t user_source_cfg(const char* user_path, const char* paths_buf[2])
{
    mfc_key_source_cfg_t cfg;
    cfg.kind    = MFC_KEY_SRC_USER;
    cfg.builtin = NULL;   /* the snapshot seeds built-ins itself (mfc_keys_reset) -- avoid double emission */
    cfg.builtin_n = 0;
    if (user_path != NULL) {
        paths_buf[0] = user_path;
        cfg.n_paths  = 1;
    } else {
        paths_buf[0] = MFC_KEYS_USER_PATH;
        paths_buf[1] = MFC_KEYS_USER_PATH_COMPAT;
        cfg.n_paths  = 2;
    }
    cfg.paths  = paths_buf;
    cfg.prober = mfc_key_source_sd_probe;
    cfg.opener = mfc_key_source_sd_open;
    cfg.io_ctx = NULL;
    cfg.accumulate = true;   /* USER role: bounded by MFC_KEYS_MAX, same cap as MFC_KEY_SOURCE_MAX_SEEN */
    return cfg;
}

const char* mfc_keys_resolve_user_path(void)
{
    const char* paths_buf[2];
    mfc_key_source_cfg_t cfg = user_source_cfg(NULL, paths_buf);
    mfc_key_source_file_state_t st = mfc_key_source_probe(&cfg);
    return (st.state == MFC_PATH_PRESENT) ? st.used_path : NULL;
}

/*----------------------------------------------------------------------------*/
/* Scalable streaming iterator (built-in keys first, then the user file) --   */
/* the path a card scan must use. A thin wrapper over mfc_key_source_iter_t,  */
/* preserving this exact signature for its one remaining caller (nfc_poller.c */
/* mfc_dict_scan(), migrating to mfc_key_source directly in a later change).  */
/*----------------------------------------------------------------------------*/
void mfc_keys_iter_begin(mfc_keys_iter_t* it, const char* user_path)
{
    if (it == NULL) return;
    it->cfg.kind      = MFC_KEY_SRC_USER;
    it->cfg.builtin   = s_builtin;
    it->cfg.builtin_n = BUILTIN_N;
    if (user_path != NULL) {
        it->paths[0]    = user_path;
        it->cfg.n_paths = 1;
    } else {
        it->paths[0]    = MFC_KEYS_USER_PATH;
        it->paths[1]    = MFC_KEYS_USER_PATH_COMPAT;
        it->cfg.n_paths = 2;
    }
    it->cfg.paths  = it->paths;
    it->cfg.prober = mfc_key_source_sd_probe;
    it->cfg.opener = mfc_key_source_sd_open;
    it->cfg.io_ctx = NULL;
    it->cfg.accumulate = true;   /* USER role: bounded by MFC_KEYS_MAX, same cap as MFC_KEY_SOURCE_MAX_SEEN */
    mfc_key_source_iter_begin(&it->inner, &it->cfg, 1);
}

bool mfc_keys_iter_next(mfc_keys_iter_t* it, uint8_t out[MFC_KEY_SIZE])
{
    if ((it == NULL) || (out == NULL)) return false;
    mfc_key_candidate_t c;
    if (!mfc_key_source_iter_next(&it->inner, &c)) return false;
    memcpy(out, c.key, MFC_KEY_SIZE);
    return true;
}

void mfc_keys_iter_end(mfc_keys_iter_t* it)
{
    if (it == NULL) return;
    mfc_key_source_iter_end(&it->inner);
}

bool mfc_keys_iter_last_was_builtin(const mfc_keys_iter_t* it)
{
    return (it != NULL) && it->inner.in_builtin;
}

uint32_t mfc_keys_total(const char* user_path)
{
    const char* paths_buf[2];
    mfc_key_source_cfg_t cfg = user_source_cfg(user_path, paths_buf);
    cfg.builtin   = s_builtin;   /* the scan-total DOES include built-ins */
    cfg.builtin_n = BUILTIN_N;
    return mfc_key_source_count(&cfg, 1).count;
}

/*----------------------------------------------------------------------------*/
/* Bounded RAM snapshot (convenience for the UI list / add / delete).          */
/*----------------------------------------------------------------------------*/
void mfc_keys_reset(mfc_keys_t* s)
{
    if (s == NULL) return;
    s->count = 0;
    for (uint16_t i = 0; (i < BUILTIN_N) && (s->count < MFC_KEYS_MAX); i++) {
        memcpy(s->keys[s->count], s_builtin[i], MFC_KEY_SIZE);
        s->count++;
    }
    s->builtin_count = s->count;
}

bool mfc_keys_add(mfc_keys_t* s, const uint8_t key[MFC_KEY_SIZE])
{
    if ((s == NULL) || (key == NULL)) return false;
    for (uint16_t i = 0; i < s->count; i++) {
        if (key_eq(s->keys[i], key)) return true;   /* duplicate: no-op success */
    }
    if (s->count >= MFC_KEYS_MAX) return false;      /* bounded */
    memcpy(s->keys[s->count], key, MFC_KEY_SIZE);
    s->count++;
    return true;
}

mfc_keys_load_status_t mfc_keys_load_user(mfc_keys_t* s, const char* user_path)
{
    mfc_keys_load_status_t st;
    st.ok = false;
    st.state = MFC_KEYS_SRC_NONE;
    st.loaded = 0;
    st.skipped_malformed = 0;
    st.truncated = false;
    st.source_path = NULL;

    if (s == NULL) return st;
    if (s->count == 0) mfc_keys_reset(s);            /* ensure built-ins seeded */

    const char* paths_buf[2];
    mfc_key_source_cfg_t cfg = user_source_cfg(user_path, paths_buf);
    mfc_key_source_file_state_t fst = mfc_key_source_probe(&cfg);
    st.source_path = fst.used_path;
    if (fst.state == MFC_PATH_ABSENT)     { st.state = MFC_KEYS_SRC_NONE;       return st; }
    if (fst.state == MFC_PATH_READ_ERROR) { st.state = MFC_KEYS_SRC_READ_ERROR; return st; }
    st.state = MFC_KEYS_SRC_LOADED;
    st.ok    = true;

    mfc_line_source_t file;
    if (cfg.opener(&file, fst.used_path, cfg.io_ctx) != 1) {
        st.state = MFC_KEYS_SRC_READ_ERROR;   /* resolved present, but the open itself failed */
        st.ok = false;
        return st;
    }

    /* Snapshot-specific bookkeeping (loaded/skipped_malformed counts for the
     * UI status line) that mfc_key_source's scan-oriented iterator does not
     * report -- so this loop, unlike mfc_keys_iter_next/mfc_keys_total above,
     * stays a purpose-built consumer of mfc_key_parse_line() rather than the
     * full candidate iterator. Path resolution and file opening above are
     * still fully delegated, not re-implemented. */
    char line[96];
    for (;;) {
        int r = file.next_line(file.ctx, line, sizeof(line));
        if (r != 1) break;

        uint8_t k[MFC_KEY_SIZE];
        mfc_key_line_kind_t kind = mfc_key_parse_line(line, k);
        if (kind == MFC_KEY_LINE_SKIP) continue;
        if (kind == MFC_KEY_LINE_BAD)  { st.skipped_malformed++; continue; }

        bool dup = false;
        for (uint16_t i = 0; i < s->count; i++) {
            if (key_eq(s->keys[i], k)) { dup = true; break; }
        }
        if (dup) continue;

        if (s->count >= MFC_KEYS_MAX) { st.truncated = true; break; }
        memcpy(s->keys[s->count], k, MFC_KEY_SIZE);
        s->count++;
        st.loaded++;
    }

    if (file.close != NULL) file.close(file.ctx);
    return st;
}

uint16_t mfc_keys_count(const mfc_keys_t* s)
{
    return (s != NULL) ? s->count : 0U;
}

bool mfc_keys_get(const mfc_keys_t* s, uint16_t idx, uint8_t out[MFC_KEY_SIZE])
{
    if ((s == NULL) || (out == NULL) || (idx >= s->count)) return false;
    memcpy(out, s->keys[idx], MFC_KEY_SIZE);
    return true;
}

bool mfc_keys_is_builtin(const mfc_keys_t* s, uint16_t idx)
{
    return (s != NULL) && (idx < s->builtin_count);
}

bool mfc_keys_remove_at(mfc_keys_t* s, uint16_t idx)
{
    if (s == NULL) return false;
    if ((idx < s->builtin_count) || (idx >= s->count)) return false;  /* never remove a built-in */
    for (uint16_t i = idx; (uint16_t)(i + 1U) < s->count; i++) {
        memcpy(s->keys[i], s->keys[i + 1U], MFC_KEY_SIZE);
    }
    s->count--;
    return true;
}

/*----------------------------------------------------------------------------*/
/* Persistence: atomic write via a ".tmp" sibling + delete-then-rename, so a  */
/* write failure partway through never corrupts the existing on-disk         */
/* dictionary (matches the m1_t2t_emu_image_save() precedent).                */
/*----------------------------------------------------------------------------*/
static void hex12_upper(const uint8_t key[MFC_KEY_SIZE], char out[MFC_KEY_HEXLEN + 1U])
{
    static const char hexd[] = "0123456789ABCDEF";
    for (uint16_t b = 0; b < MFC_KEY_SIZE; b++) {
        out[2U * b]      = hexd[(key[b] >> 4) & 0x0FU];
        out[2U * b + 1U] = hexd[key[b] & 0x0FU];
    }
    out[MFC_KEY_HEXLEN] = '\0';
}

int mfc_keys_save_user(const mfc_keys_t* s)
{
    if (s == NULL) return 0;

    char tmp_path[sizeof(MFC_KEYS_USER_PATH) + 4U];
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", MFC_KEYS_USER_PATH);

    nfcfio_t io;
    if (nfcfio_open_write(&io, tmp_path) != 1) return 0;
    nfcfio_set_crlf(&io, 0);   /* LF-delimited interoperable text */

    int ok = 1;
    for (uint16_t i = s->builtin_count; (i < s->count) && ok; i++) {   /* user keys only */
        char row[MFC_KEY_HEXLEN + 1U];
        hex12_upper(s->keys[i], row);
        if (nfcfio_putline(&io, row) != 1) ok = 0;
    }
    nfcfio_close(&io);

    if (!ok) { (void)f_unlink(tmp_path); return 0; }

    /* Replace the original: delete-then-rename is the best atomicity FatFS
     * offers (no in-place atomic replace primitive). The tmp write above is
     * fully complete and closed here, so the only remaining failure window is
     * the brief gap between these two calls -- a known, disclosed limitation
     * shared with the existing m1_t2t_emu_image_save() precedent. */
    FRESULT rm = f_unlink(MFC_KEYS_USER_PATH);
    if ((rm != FR_OK) && (rm != FR_NO_FILE) && (rm != FR_NO_PATH)) {
        (void)f_unlink(tmp_path);
        return 0;   /* could not remove the original -- it is unchanged */
    }
    if (f_rename(tmp_path, MFC_KEYS_USER_PATH) != FR_OK) {
        return 0;   /* original already gone -- data is in tmp_path, not silently lost */
    }
    return 1;
}
