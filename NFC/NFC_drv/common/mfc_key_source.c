/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_key_source.c
 * @brief   See mfc_key_source.h. No FatFs/HAL dependency by design -- every
 *          file access goes through the injected mfc_line_source_t /
 *          mfc_path_prober_fn / mfc_line_source_opener_fn seam, so this file
 *          compiles and runs unmodified in the host test binary.
 */
/*============================================================================*/
#include "mfc_key_source.h"
#include <string.h>
#include "nfc_dict_line.h"

/*----------------------------------------------------------------------------*/
/* Line parsing                                                                */
/*----------------------------------------------------------------------------*/


mfc_key_line_kind_t mfc_key_parse_line(const char *line, uint8_t out[MFC_KEY_SIZE])
{
    switch (nfc_dict_parse_line(line, out, MFC_KEY_SIZE)) {
    case NFC_DICT_LINE_SKIP: return MFC_KEY_LINE_SKIP;
    case NFC_DICT_LINE_KEY:  return MFC_KEY_LINE_KEY;
    default:                 return MFC_KEY_LINE_BAD;
    }
}

static bool key_eq(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, MFC_KEY_SIZE) == 0;
}

/*----------------------------------------------------------------------------*/
/* Path resolution -- shared by mfc_key_source_probe() and the iterator, so   */
/* both agree on which candidate resolves and why.                            */
/*----------------------------------------------------------------------------*/
static mfc_key_source_file_state_t resolve_path(const mfc_key_source_cfg_t *cfg)
{
    mfc_key_source_file_state_t st = { MFC_PATH_ABSENT, NULL };
    if ((cfg == NULL) || (cfg->paths == NULL) || (cfg->n_paths == 0U) || (cfg->prober == NULL)) {
        return st;
    }
    for (size_t i = 0; i < cfg->n_paths; i++) {
        mfc_path_probe_t s = cfg->prober(cfg->paths[i], cfg->io_ctx);
        if (s == MFC_PATH_ABSENT) continue;   /* try the next candidate */
        st.state     = s;                     /* PRESENT or READ_ERROR: stop here */
        st.used_path = cfg->paths[i];
        return st;
    }
    return st;   /* every candidate absent */
}

mfc_key_source_file_state_t mfc_key_source_probe(const mfc_key_source_cfg_t *cfg)
{
    return resolve_path(cfg);
}

/* Opens the resolved candidate for streaming, if PRESENT. Returns true and
 * fills *out if a file is now open; false otherwise (ABSENT, READ_ERROR, or
 * the opener itself failed -- the latter is reported as READ_ERROR too,
 * matching mfc_key_source_probe()'s tri-state for a caller comparing the
 * two). */
static bool open_source_file(const mfc_key_source_cfg_t *cfg, mfc_line_source_t *out)
{
    mfc_key_source_file_state_t st = resolve_path(cfg);
    if (st.state != MFC_PATH_PRESENT) return false;
    if ((cfg->opener == NULL) || (cfg->opener(out, st.used_path, cfg->io_ctx) != 1)) return false;
    return true;
}

/*----------------------------------------------------------------------------*/
/* Dedup set                                                                   */
/*----------------------------------------------------------------------------*/
static bool seen_contains(const mfc_key_source_iter_t *it, const uint8_t key[MFC_KEY_SIZE])
{
    for (uint16_t i = 0; i < it->seen_n; i++) {
        if (key_eq(it->seen[i], key)) return true;
    }
    return false;
}

static void seen_mark(mfc_key_source_iter_t *it, const uint8_t key[MFC_KEY_SIZE])
{
    if (it->seen_n >= MFC_KEY_SOURCE_MAX_SEEN) { it->seen_overflowed = true; return; }
    memcpy(it->seen[it->seen_n], key, MFC_KEY_SIZE);
    it->seen_n++;
}

void mfc_key_source_iter_seed_seen(mfc_key_source_iter_t *it, const uint8_t key[MFC_KEY_SIZE])
{
    if ((it == NULL) || (key == NULL)) return;
    if (!seen_contains(it, key)) seen_mark(it, key);
}

/*----------------------------------------------------------------------------*/
/* Iterator                                                                    */
/*----------------------------------------------------------------------------*/
void mfc_key_source_iter_begin(mfc_key_source_iter_t *it,
                               const mfc_key_source_cfg_t *sources, size_t n_sources)
{
    if (it == NULL) return;
    memset(it, 0, sizeof(*it));
    it->sources   = sources;
    it->n_sources = n_sources;
    it->src_idx   = 0;
    it->in_builtin = true;
}

void mfc_key_source_iter_begin_resume(mfc_key_source_iter_t *it,
                                      const mfc_key_source_cfg_t *sources, size_t n_sources,
                                      const uint8_t (*seen_in)[MFC_KEY_SIZE], uint16_t seen_in_n,
                                      bool seen_in_overflowed, uint32_t system_resume_offset)
{
    mfc_key_source_iter_begin(it, sources, n_sources);
    if (it == NULL) return;

    if ((seen_in != NULL) && (seen_in_n > 0U)) {
        uint16_t n = (seen_in_n <= MFC_KEY_SOURCE_MAX_SEEN) ? seen_in_n : MFC_KEY_SOURCE_MAX_SEEN;
        memcpy(it->seen, seen_in, (size_t)n * MFC_KEY_SIZE);
        it->seen_n = n;
    }
    it->seen_overflowed = seen_in_overflowed;

    /* Seeking to offset 0 is behaviorally identical to not seeking at all
     * (streaming already starts at the top), so treating 0 as "no resume
     * requested" loses nothing -- it just skips a redundant seek() call. */
    it->pending_sys_resume_active = (system_resume_offset != 0U);
    it->pending_sys_resume_offset = system_resume_offset;
}

static bool advance_to_next_source(mfc_key_source_iter_t *it)
{
    if (it->file_open) {
        if (it->file.close != NULL) it->file.close(it->file.ctx);
        it->file_open = false;
        memset(&it->file, 0, sizeof(it->file));
    }
    it->src_idx++;
    it->builtin_idx = 0;
    it->in_builtin  = true;
    return it->src_idx < it->n_sources;
}

void mfc_key_source_iter_skip_source(mfc_key_source_iter_t *it)
{
    if ((it == NULL) || (it->sources == NULL) || (it->src_idx >= it->n_sources)) return;

    if (it->in_builtin) {
        /* Skip the remaining compiled built-ins of the CURRENT source only,
         * landing on that SAME source's file component -- deliberately not
         * advance_to_next_source() here, which would also skip the file and
         * over-skip past the visible "System Dictionary" stage when the
         * caller only meant to skip "Built-in Keys". Mirrors
         * mfc_key_source_iter_next()'s own builtin-exhaustion transition
         * exactly (including resume-seek consumption), just triggered on
         * request instead of only when builtin_idx reaches builtin_n. */
        const mfc_key_source_cfg_t *cfg = &it->sources[it->src_idx];
        it->in_builtin = false;
        it->file_open  = open_source_file(cfg, &it->file);
        if (it->file_open && it->pending_sys_resume_active && (cfg->kind == MFC_KEY_SRC_SYSTEM)) {
            if (it->file.seek != NULL) {
                (void)it->file.seek(it->file.ctx, it->pending_sys_resume_offset);
            }
            it->pending_sys_resume_active = false;
        }
        if (!it->file_open) {
            /* This source has no file component either -- nothing left in
             * it at all, so fall through to the next configured source. */
            (void)advance_to_next_source(it);
        }
    } else {
        /* Already in this source's file (or it never had one) -- there is
         * nothing else belonging to this source, so move on entirely. */
        (void)advance_to_next_source(it);
    }
}

bool mfc_key_source_iter_next(mfc_key_source_iter_t *it, mfc_key_candidate_t *out)
{
    if ((it == NULL) || (out == NULL) || (it->sources == NULL)) return false;

    /* Only a SYSTEM file candidate ever sets this true again below -- a
     * builtin or USER candidate returned from THIS call must never leave a
     * stale offset from some earlier SYSTEM candidate looking current. */
    it->last_system_offset_valid = false;

    for (;;) {
        if (it->src_idx >= it->n_sources) return false;   /* all sources exhausted */
        const mfc_key_source_cfg_t *cfg = &it->sources[it->src_idx];

        if (it->in_builtin) {
            if (it->builtin_idx < cfg->builtin_n) {
                const uint8_t *k = cfg->builtin[it->builtin_idx];
                it->builtin_idx++;
                if (seen_contains(it, k)) continue;   /* already yielded by an earlier source */
                seen_mark(it, k);   /* built-ins always accumulate: few, fixed-size */
                out->kind = cfg->kind;
                out->from_builtin = true;
                memcpy(out->key, k, MFC_KEY_SIZE);
                return true;
            }
            it->in_builtin = false;
            it->file_open  = open_source_file(cfg, &it->file);

            /* Resume: consumed exactly once, the first time the SYSTEM
             * source's file specifically is opened (mfc_dict_resume_t's own
             * scope) -- gated on cfg->kind here, not merely "the first file
             * of any kind", since a USER (or any other) source opened
             * earlier in iteration order must leave this flag untouched for
             * SYSTEM to still consume later. A NULL seek() (source doesn't
             * support resume) or a failed seek() both fall back to reading
             * from wherever open just left the file -- the true start --
             * never a crash and never a silently-wrong position. */
            if (it->file_open && it->pending_sys_resume_active && (cfg->kind == MFC_KEY_SRC_SYSTEM)) {
                if (it->file.seek != NULL) {
                    (void)it->file.seek(it->file.ctx, it->pending_sys_resume_offset);
                }
                it->pending_sys_resume_active = false;
            }
        }

        if (!it->file_open) {
            if (!advance_to_next_source(it)) return false;
            continue;
        }

        int r = it->file.next_line(it->file.ctx, it->line, sizeof(it->line));
        if (r != 1) {   /* EOF or error: this source is exhausted */
            if (!advance_to_next_source(it)) return false;
            continue;
        }

        uint8_t k[MFC_KEY_SIZE];
        if (mfc_key_parse_line(it->line, k) != MFC_KEY_LINE_KEY) continue;   /* skip blank/comment/bad */
        /* Cross-source suppression: always checked, regardless of this
         * source's own size. Only an "accumulate" source (built-ins, a
         * bounded user dictionary) adds its own entries to that set -- an
         * unbounded source (e.g. a multi-thousand-entry system dictionary)
         * is trusted to already be free of internal duplicates and is never
         * added, so streaming it costs O(1) additional memory regardless of
         * file size (see mfc_key_source_cfg_t.accumulate). */
        if (seen_contains(it, k)) continue;
        if (cfg->accumulate) seen_mark(it, k);
        if ((cfg->kind == MFC_KEY_SRC_SYSTEM) && (it->file.tell != NULL)) {
            long pos = it->file.tell(it->file.ctx);
            if (pos >= 0) {
                it->last_system_offset_valid = true;
                it->last_system_offset = (uint32_t)pos;
            }
        }
        out->kind = cfg->kind;
        out->from_builtin = false;
        memcpy(out->key, k, MFC_KEY_SIZE);
        return true;
    }
}

void mfc_key_source_iter_end(mfc_key_source_iter_t *it)
{
    if (it == NULL) return;
    if (it->file_open && (it->file.close != NULL)) it->file.close(it->file.ctx);
    it->file_open = false;
}

/*----------------------------------------------------------------------------*/
mfc_key_source_count_t mfc_key_source_count(const mfc_key_source_cfg_t *sources, size_t n_sources)
{
    mfc_key_source_iter_t it;
    mfc_key_candidate_t   cand;
    mfc_key_source_count_t r = { 0, false };

    mfc_key_source_iter_begin(&it, sources, n_sources);
    while (mfc_key_source_iter_next(&it, &cand)) r.count++;
    r.overflowed = it.seen_overflowed;
    mfc_key_source_iter_end(&it);
    return r;
}
