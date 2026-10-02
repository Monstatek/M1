/*
 * mfc_dict_solver.h - MIFARE Classic nested-dictionary key solver.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Recovers a sector key by testing a KEY DICTIONARY against nested nonces
 * collected from a card (parsed from a .m1h Nested/Static record). For each
 * candidate key it decrypts every nonce and keeps the key only if ALL nonces
 * pass the weak-PRNG (optional) and encrypted-parity checks. It uses no
 * lfsr_recovery32 and no large buffers: bounded, allocation-free,
 * host-testable pure logic. It finds
 * keys that are IN THE DICTIONARY; true arbitrary-key recovery stays host-side.
 *
 * PROVENANCE
 *   Crypto1 primitives: Proxmark3 (RfidResearchGroup), through the M1
 *   crypto1_recover.* implementation.
 *   The .m1h format is the M1's own (mfc_harvest.*).
 *
 * The caller (device integration) MUST verify the returned key by
 * authenticating the target block against the physical card before reporting
 * success -- this module only proposes a dictionary candidate.
 */
#ifndef MFC_DICT_SOLVER_H
#define MFC_DICT_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Upper bound on nonces parsed from one record (bounded stack; larger records
 * are rejected rather than truncated). */
#define MFC_SOLVER_MAX_NONCES  32U

typedef enum {
    MFC_SOLVE_OK = 0,           /* exactly one distinct candidate key found     */
    MFC_SOLVE_INVALID_INPUT,    /* NULL / bad argument                          */
    MFC_SOLVE_BAD_RECORD,       /* header/record magic/version/len/CRC/bounds   */
    MFC_SOLVE_UNSUPPORTED,      /* record type not solvable here                */
    MFC_SOLVE_INSUFFICIENT,     /* too few nonces to solve safely               */
    MFC_SOLVE_NO_KEY,           /* no dictionary key matched all nonces         */
    MFC_SOLVE_AMBIGUOUS,        /* >1 distinct dictionary key matched           */
    MFC_SOLVE_ABORTED,          /* caller cancelled                             */
} mfc_solve_status_t;

/* One nested-nonce sample the solver needs (cuid is per-capture, copied in). */
typedef struct {
    uint32_t cuid;
    uint32_t nt_enc;
    uint8_t  par;      /* 4 encrypted nonce-byte parity bits, LSB = byte 0 */
} mfc_solver_nonce_t;

/* Parsed, validated capture ready to solve. */
typedef struct {
    uint32_t cuid;
    uint8_t  src_block, src_keytype;
    uint8_t  tgt_block, tgt_keytype;
    uint8_t  known_key_ref;         /* 1 = known_key valid (embedded)           */
    uint8_t  known_key[6];
    uint8_t  rec_type;              /* 2 = Nested, 3 = Static                    */
    uint16_t count;
    mfc_solver_nonce_t nonces[MFC_SOLVER_MAX_NONCES];
} mfc_solver_capture_t;

/*
 * Dictionary iterator: write the next 6-byte key into key[6] and return true;
 * return false when the dictionary is exhausted. The solver may call this many
 * times; it never retains the pointer.
 */
typedef bool (*mfc_solver_key_iter_fn)(void *ctx, uint8_t key[6]);

/* Optional cooperative cancel: return false to abort the solve. May be NULL. */
typedef bool (*mfc_solver_progress_fn)(void *ctx, uint32_t keys_tried);

/*
 * Parse + validate a single-record .m1h buffer into `out`. Validates the 38-byte
 * header (magic, version, header_size, header CRC), the record framing (type,
 * version, record_len bounds, trailer CRC), the payload fields and the
 * sample_count vs record_len. Only Nested (2) and Static (3) records are
 * accepted. Returns MFC_SOLVE_OK or a BAD_RECORD/UNSUPPORTED/INVALID code.
 */
mfc_solve_status_t mfc_solver_parse_m1h(const uint8_t *buf, size_t len,
                                        mfc_solver_capture_t *out);

/*
 * Run the nested-dictionary attack over a parsed capture.
 *   iter/iter_ctx   : the candidate-key source
 *   known_src_key    : the source-sector key (48-bit); SKIPPED so it is never
 *                      returned as the "recovered" target key
 *   is_weak          : apply the weak-PRNG check (true for weak-PRNG cards)
 *   out_key          : receives the recovered 48-bit key on OK
 *   progress/ctx     : optional cancel (may be NULL)
 * Scans the WHOLE dictionary and counts DISTINCT matching keys: OK (exactly 1),
 * NO_KEY (0), AMBIGUOUS (>1 -> collect more nonces). Deterministic; no malloc;
 * bounded stack.
 */
mfc_solve_status_t mfc_solver_run(const mfc_solver_capture_t *cap,
                                  mfc_solver_key_iter_fn iter, void *iter_ctx,
                                  uint64_t known_src_key, bool is_weak,
                                  uint64_t *out_key,
                                  mfc_solver_progress_fn progress, void *progress_ctx);

/*
 * Convenience: parse `buf` then solve. Also returns the target block/keytype
 * (for the caller's RF verification). See the two functions above.
 */
mfc_solve_status_t mfc_solver_solve_m1h(const uint8_t *buf, size_t len,
                                        mfc_solver_key_iter_fn iter, void *iter_ctx,
                                        uint64_t known_src_key, bool is_weak,
                                        uint64_t *out_key,
                                        uint8_t *out_tgt_block, uint8_t *out_tgt_keytype,
                                        mfc_solver_progress_fn progress, void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* MFC_DICT_SOLVER_H */
