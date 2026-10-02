/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_key_source.h
 * @brief   Single source-aware MIFARE Classic candidate-key iterator.
 *
 * OWNERSHIP: the one place dictionary ORDERING and DEDUPLICATION live for
 * MIFARE Classic key acquisition. Anything that needs "the keys to try, in
 * order, each tagged with where it came from" -- a dictionary count shown in
 * the UI, the RF-attempt scheduler, a future card-specific cache or recovery
 * source -- goes through this module, so those three can never disagree:
 * mfc_key_source_count() and mfc_key_source_iter_* run the exact same walk.
 *
 * LIFETIME: mfc_key_source_iter_t is a plain value type; the caller owns its
 * storage (stack or static, per the codebase's "never allocate a SCALE
 * structure on the stack of a small task" convention -- this struct itself
 * is small, but its embedded mfc_line_source_t may wrap a much larger
 * production I/O context; see mfc_key_source_sd.h). mfc_key_source_iter_end()
 * MUST be called on every exit path (including abort/cancel) to release the
 * open file, if any -- there is no destructor.
 *
 * ERROR SEMANTICS: a source's file component is ABSENT (not an error -- an
 * optional source simply contributes nothing), PRESENT (opened and
 * streamed), or READ_ERROR (the file exists but could not be opened/read --
 * reported truthfully via mfc_key_source_probe(), never silently swapped for
 * a fallback path). A source with compiled built-ins keeps those usable
 * regardless of its file component's state.
 *
 * TESTABILITY SEAM: this file has NO FatFs/HAL dependency. All file access
 * is through the injected mfc_line_source_t/mfc_path_prober_fn/
 * mfc_line_source_opener_fn function pointers, so mfc_key_source_test.c
 * exercises this exact production code, unmodified, against an in-memory
 * mock -- not a transcribed copy of the algorithm. The one production
 * adapter that touches real SD-card I/O is mfc_key_source_sd.h/.c, compiled
 * only into the firmware, never into the host test binary.
 *
 * SCOPE: yields candidate KEYS only. No RF, no Crypto1, no authentication --
 * those are the acquisition session's job (see nfc_poller.c).
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_KEY_SOURCE_H_
#define NFC_DRV_MFC_KEY_SOURCE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFC_KEY_SIZE    6U
#define MFC_KEY_HEXLEN  12U

/*----------------------------------------------------------------------------*/
/* Line parsing (permissive whitespace/case, strict 12-hex key). Pure, no I/O */
/* -- the canonical parser for every MFC key dictionary file M1 reads.        */
/*----------------------------------------------------------------------------*/
typedef enum {
    MFC_KEY_LINE_SKIP = 0,   /* blank line or '#' comment (not an error) */
    MFC_KEY_LINE_KEY,        /* valid 6-byte key parsed into out[]       */
    MFC_KEY_LINE_BAD,        /* malformed first token (rejected)         */
} mfc_key_line_kind_t;

/* Classify/parse one text line. Leading/trailing whitespace is ignored and
 * blank/'#'-comment lines are SKIP. A KEY is exactly 12 hex chars (upper or
 * lower); after those 12 chars the rest of the line may contain ONLY
 * whitespace, or whitespace followed by a '#' inline comment. Any other
 * trailing text, or a token shorter/longer than 12, or non-hex, is BAD
 * (never truncated). */
mfc_key_line_kind_t mfc_key_parse_line(const char *line, uint8_t out[MFC_KEY_SIZE]);

/* A MIFARE Classic dictionary role. Not a UI label -- the acquisition
 * session and the progress screen both key off this to decide phase order
 * and phase names ("User Dictionary" / "System Dictionary"). */
typedef enum {
    MFC_KEY_SRC_USER = 0,
    MFC_KEY_SRC_SYSTEM,
} mfc_key_source_kind_t;

typedef struct {
    mfc_key_source_kind_t kind;
    bool                   from_builtin;   /* true: cfg->builtin[]; false: the file component.
                                             * `kind` alone cannot tell a caller "Built-in" from
                                             * "Sys Dict" when a SYSTEM source carries both -- this
                                             * is the origin signal for that distinction, kept
                                             * separate from `kind` (the dictionary ROLE) so
                                             * neither meaning is overloaded onto the other. */
    uint8_t                key[MFC_KEY_SIZE];
} mfc_key_candidate_t;

/*----------------------------------------------------------------------------*/
/* Injected file-I/O seam.                                                    */
/*----------------------------------------------------------------------------*/

/* One open line-reader. next_line() fills `buf` (NUL-terminated, EOL kept or
 * stripped is the adapter's choice -- mfc_key_source treats trailing CR/LF as
 * ordinary parseable whitespace either way) and returns 1, or returns 0 at
 * EOF/on error (the two are not distinguished here; the path-level PRESENT
 * vs READ_ERROR probe below is where that distinction is made, before a
 * source is ever opened for streaming). close() releases any resource held
 * by ctx; safe to call on an already-closed/never-opened source.
 *
 * tell/seek are OPTIONAL (nullable) resume support: tell() returns the byte
 * offset of the next unconsumed line (or a negative value if unavailable),
 * seek() moves to a previously-tell()'d offset before the first next_line()
 * call and returns whether it succeeded. An adapter that cannot support
 * resume (e.g. an in-memory test source with no notion of "position") simply
 * leaves both NULL -- mfc_key_source_iter_begin_resume() then falls back to
 * streaming that source from the beginning, exactly like a fresh iterator,
 * never a crash or a silently-wrong offset. */
typedef struct {
    int  (*next_line)(void *ctx, char *buf, size_t bufsz);
    void (*close)(void *ctx);
    long (*tell)(void *ctx);
    bool (*seek)(void *ctx, uint32_t offset);
    void *ctx;
} mfc_line_source_t;

typedef enum {
    MFC_PATH_ABSENT = 0,     /* no file at this path -- try the next candidate, or none configured */
    MFC_PATH_PRESENT,        /* file exists and is (believed) readable                              */
    MFC_PATH_READ_ERROR,     /* file exists but could not be opened/read -- stop, do not fall through */
} mfc_path_probe_t;

/* Reports which of the three states `path` is in. Must not have side
 * effects beyond a read-only existence/openability check. */
typedef mfc_path_probe_t (*mfc_path_prober_fn)(const char *path, void *io_ctx);

/* Opens `path` for line-at-a-time reading into `*out`. Called only after
 * `prober` has reported PRESENT for this exact path. Returns 1 on success,
 * 0 on failure (treated as MFC_PATH_READ_ERROR by the caller). */
typedef int (*mfc_line_source_opener_fn)(mfc_line_source_t *out, const char *path, void *io_ctx);

/*----------------------------------------------------------------------------*/
/* One configured dictionary source.                                          */
/*----------------------------------------------------------------------------*/

/* `paths` are tried in order; the first PRESENT path is opened and streamed,
 * a READ_ERROR path stops the search immediately (never silently skipped to
 * the next candidate), and all-ABSENT means this source contributes no file
 * entries. `builtin`/`builtin_n` are optional compiled-in keys emitted
 * before the file component, always usable regardless of the file's state
 * (may be NULL/0 for a source with no compiled fallback, e.g. USER).
 *
 * `accumulate` decides how this source participates in cross-source
 * deduplication (see the iterator comment below): true for a source whose
 * FULL content is expected to fit in the bounded in-RAM suppression set
 * (compiled built-ins, and a user-authored dictionary no larger than
 * MFC_KEY_SOURCE_MAX_SEEN entries -- matching mfc_keys.h's own
 * MFC_KEYS_MAX cap for the same file) -- its entries are both checked
 * against and ADDED to that set, so later sources never re-propose them,
 * and it is self-deduplicated against its own repeats too. false for a
 * source whose size is NOT assumed bounded (a community/proxmark-style
 * system dictionary can hold several thousand entries) -- its entries are
 * checked against the accumulated set but never added to it, so streaming
 * it costs O(1) additional memory regardless of how large the file is.
 * This requires the source's own file to already be free of internal
 * duplicate lines; mfc_key_source does not (and, for an unbounded source,
 * structurally cannot) verify that -- a duplicate within a non-
 * accumulating source's own file is not caught and would be yielded more
 * than once. Built-ins are always added to the set regardless of this
 * flag (they are few and fixed-size either way). */
typedef struct {
    mfc_key_source_kind_t      kind;
    const uint8_t             (*builtin)[MFC_KEY_SIZE];
    size_t                      builtin_n;
    const char *const         *paths;
    size_t                      n_paths;
    mfc_path_prober_fn          prober;
    mfc_line_source_opener_fn   opener;
    void                       *io_ctx;
    bool                        accumulate;
} mfc_key_source_cfg_t;

typedef struct {
    mfc_path_probe_t  state;       /* ABSENT if no paths configured for this source */
    const char       *used_path;   /* which candidate resolved, or NULL              */
} mfc_key_source_file_state_t;

/* Resolves (without opening for streaming) which candidate path -- if any --
 * this source's file component would use, and its state. Read-only; safe to
 * call at any time, e.g. to show "Dictionary read error" truthfully in the
 * UI without starting a scan. */
mfc_key_source_file_state_t mfc_key_source_probe(const mfc_key_source_cfg_t *cfg);

/*----------------------------------------------------------------------------*/
/* Iterator: walks every configured source in order (builtins, then file).   */
/* Every candidate is checked against the accumulated cross-source           */
/* suppression set before being yielded, so a key already produced by an     */
/* earlier "accumulate" source (built-ins, or a bounded user dictionary) is  */
/* never proposed again by a later source. Only "accumulate" sources ADD     */
/* their own entries to that set -- an unbounded source (a multi-thousand-   */
/* entry system dictionary) is checked but never accumulated, so its own    */
/* size never affects memory use; it is trusted to already be free of        */
/* internal duplicates (see mfc_key_source_cfg_t.accumulate). The count      */
/* this iterator produces is always exactly the count mfc_key_source_count() */
/* reports -- same walk, same rules -- for any source list, of any size.     */
/*----------------------------------------------------------------------------*/
#define MFC_KEY_SOURCE_MAX_SEEN  1024U   /* cap on ACCUMULATE-eligible entries only (built-ins +
                                          * bounded/user-authored sources) -- never a cap on an
                                          * unbounded (accumulate=false) source's own size or count */

typedef struct {
    const mfc_key_source_cfg_t *sources;
    size_t                       n_sources;

    size_t              src_idx;
    bool                in_builtin;
    size_t              builtin_idx;
    mfc_line_source_t   file;
    bool                file_open;
    char                line[96];

    uint8_t   seen[MFC_KEY_SOURCE_MAX_SEEN][MFC_KEY_SIZE];
    uint16_t  seen_n;
    bool      seen_overflowed;   /* dedup set exhausted -- see mfc_key_source_count_t.overflowed */

    /* Resume support (see mfc_key_source_iter_begin_resume() below). When the
     * most recent mfc_key_source_iter_next() yielded a candidate read from a
     * SYSTEM source's file component (never a built-in, never USER --
     * matching mfc_dict_resume_t's own System-only resume scope),
     * last_system_offset_valid is true and last_system_offset holds the byte
     * offset immediately AFTER that candidate's line (i.e. "resume here to
     * skip exactly the candidates already yielded"), read via the source's
     * optional tell() callback. false whenever tell() is unavailable or the
     * candidate came from anywhere else -- a caller must never persist an
     * offset that was not actually reported. */
    bool      last_system_offset_valid;
    uint32_t  last_system_offset;

    /* Internal to mfc_key_source_iter_begin_resume()/_next() -- a caller
     * never reads these directly. Set once by _begin_resume(); consumed
     * (seek() called, then cleared) exactly once, the first time the
     * SYSTEM source's file is opened. */
    bool      pending_sys_resume_active;
    uint32_t  pending_sys_resume_offset;
} mfc_key_source_iter_t;

void mfc_key_source_iter_begin(mfc_key_source_iter_t *it,
                               const mfc_key_source_cfg_t *sources, size_t n_sources);

/* Resume-capable variant: identical to mfc_key_source_iter_begin() when
 * seen_in is NULL (every existing call site's behavior is therefore
 * unaffected by this function's mere existence) -- so a fresh start and a
 * resumed continuation share exactly one iterator implementation, never a
 * second parser.
 *
 * When seen_in is non-NULL, its seen_in_n entries pre-populate the dedup
 * set (so a built-in/User candidate already yielded in an earlier
 * generation-scoped run is never re-yielded here -- matching by VALUE, so a
 * genuinely new candidate added to a source since then is still yielded
 * normally) and seen_in_overflowed seeds the overflow flag. Independently,
 * when system_resume_offset is nonzero, the FIRST source in `sources` whose
 * kind is MFC_KEY_SRC_SYSTEM has its file component's seek() called (if the
 * opened file provides one) to that offset immediately after opening, before
 * any line is read from it -- this request stays armed across any other
 * source (builtins, USER, a SYSTEM source with no file component) that
 * opens first in iteration order, and is consumed exactly once, the moment
 * SYSTEM's own file actually opens -- every other source is completely
 * unaffected regardless of how many of them are streamed before it.
 * The caller is responsible for having already verified this offset still
 * applies to the file that will actually be opened (mfc_dict_resume.h) --
 * this function trusts the value it is given and never inspects file
 * identity itself. If seek() is unavailable (NULL) or fails, streaming
 * silently starts from the beginning of that source instead of skipping --
 * never a crash, and never SILENTLY treating unread lines as already tried
 * (a fallback to full-restart is always safe; a wrong skip would not be). */
void mfc_key_source_iter_begin_resume(mfc_key_source_iter_t *it,
                                      const mfc_key_source_cfg_t *sources, size_t n_sources,
                                      const uint8_t (*seen_in)[MFC_KEY_SIZE], uint16_t seen_in_n,
                                      bool seen_in_overflowed, uint32_t system_resume_offset);

bool mfc_key_source_iter_next (mfc_key_source_iter_t *it, mfc_key_candidate_t *out);
void mfc_key_source_iter_end  (mfc_key_source_iter_t *it);

/* Abandons whatever remains of the CURRENT visible source category only --
 * the compiled built-ins of a source, or a source's file, whichever is
 * currently being walked -- and repositions the iterator so the NEXT
 * mfc_key_source_iter_next() call resumes at the next source (skipping
 * built-ins lands on that SAME source's file, not the next configured
 * source entry -- built-ins and a file share one sources[] entry but are
 * two distinct visible stages). If the current source was the last one
 * configured, the iterator becomes exhausted (the next iter_next() call
 * returns false), exactly as if it had run out naturally. Calling this on
 * an already-exhausted or NULL iterator is a safe no-op. Does not touch
 * seen[]/seen_n/seen_overflowed or any already-yielded candidate's effect
 * -- only which candidates are yielded NEXT changes. */
void mfc_key_source_iter_skip_source(mfc_key_source_iter_t *it);

/* Marks `key` as already yielded, before iterating, without it having come
 * from any configured source -- for the acquisition session to seed in a
 * key it already tried outside this module (e.g. the fast/default-key pass),
 * so the dictionary phases that follow do not propose it again. */
void mfc_key_source_iter_seed_seen(mfc_key_source_iter_t *it, const uint8_t key[MFC_KEY_SIZE]);

/*----------------------------------------------------------------------------*/
typedef struct {
    uint32_t count;
    bool     overflowed;   /* an ACCUMULATE-eligible source (built-ins + bounded/user)
                             * exceeded MFC_KEY_SOURCE_MAX_SEEN unique entries -- never
                             * set by an unbounded (accumulate=false) source regardless
                             * of its size; `count` itself is always exact either way. */
} mfc_key_source_count_t;

/* Runs the given source list through the SAME iterator used for scanning and
 * counts what it yields -- never a second, independently-maintained count.
 * Exact for any source list, including a multi-thousand-entry unbounded
 * system dictionary: no truncation, no probabilistic filter. */
mfc_key_source_count_t mfc_key_source_count(const mfc_key_source_cfg_t *sources, size_t n_sources);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_KEY_SOURCE_H_ */
