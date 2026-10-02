/*
 * crypto1_recover.h - MIFARE Classic Crypto1 card-only recovery FOUNDATION
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reusable, bounded mathematical primitives shared by the (future) Nested and
 * Static-Nested card-only key-recovery attacks. This file deliberately does NOT
 * touch the live polling path: nothing here is called from firmware yet.
 *
 * PROVENANCE
 * ----------
 * The nonce/keystream algorithms use Crypto1 material from Proxmark3
 * (RfidResearchGroup):
 *   https://github.com/RfidResearchGroup/proxmark3.git   (crapto1, GPL-2.0+)
 * Rewritten for the M1 (no third-party code copied — plain C over the existing
 * crypto1 API, bounded, allocation-free):
 *   crypto1_recover_nonce_distance        (bounded suc^n search, 16-bit period)
 *   crypto1_recover_classify_static       (constant-nonce detection)
 *   crypto1_recover_select_weak_nonces    (capacity-checked filter)
 *   crypto1_recover_key_consistent        (per-candidate nested acceptance test)
 *
 * GENUINE STATE RECOVERY (added Phase 1-fix) — direct Proxmark3 port:
 *   crypto1_recover_lfsr64        (Proxmark3 lfsr_recovery64: 64 keystream bits
 *                                  -> candidate Crypto1 states)
 *   crypto1_recover_state_to_key  (Proxmark3 crypto1_get_lfsr: state -> 48-bit key)
 *   Source:  RfidResearchGroup/proxmark3
 *            commit ee8b9ca74b10536db246286179183e0f1d89770e (2024-08-03)
 *            common/crapto1/crapto1.c  (lfsr_recovery64, extend_table_simple,
 *                                       S1/S2/T1/T2/C1/C2 tables)
 *            common/crapto1/crypto1.c  (crypto1_get_lfsr)
 *   Copyright (C) 2008-2014 bla <blapost@gmail.com>;
 *            (C) Proxmark3 contributors (AUTHORS.md).  License: GPL-3.0-or-later.
 *   Adapted for the M1: the upstream 256 KiB on-stack `table[1<<16]` and the
 *   calloc'd state list are replaced by a CALLER-SUPPLIED workspace and a
 *   CALLER-OWNED output buffer with explicit capacity/workspace status codes;
 *   the small filter() is replicated locally (identical to crypto1.c's private
 *   copy) so no third-party malloc/free is used. The core algorithm, tables,
 *   bit ordering (BEBIT) and state layout are unchanged from upstream.
 *
 * M1 adaptation notes:
 *   - Keys use a plain 48-bit uint64_t representation (big-endian, key[0] in
 *     bits 47..40), matching crypto1_init().
 *   - No dynamic allocation, no hidden mutable globals, no large stack arrays.
 *   - All buffer output is caller-owned and capacity-checked.
 *
 * SCOPE / DEFERRED
 *   The heavyweight Crapto1 keyspace solver (lfsr_recovery32/64) is NOT included
 *   here: it needs multi-MB working memory and does not fit the STM32H573 RAM
 *   budget (see the accompanying phase report). Card-only recovery on-device is
 *   built from these bounded primitives (decrypt + weak/static classification +
 *   parity + per-candidate acceptance) plus a constrained candidate source
 *   supplied by a later orchestration phase; unbounded search stays host-side.
 *
 * Bit/byte conventions (shared with crypto1.c):
 *   - Nonces (nt, nt_enc) and cuid are 32-bit big-endian numbers: byte 0 of the
 *     wire value occupies bits 31..24.
 *   - nt_par_enc holds the 3 relevant encrypted parity bits of a nested nonce in
 *     bits 3..1 (bit 3 = parity of nt byte 0, bit 1 = parity of nt byte 2),
 *     matching the wire convention used by this module.
 */
#ifndef CRYPTO1_RECOVER_H
#define CRYPTO1_RECOVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "crypto1.h"   /* Crypto1 state type used by the recovery API */

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit, exhaustive result codes: callers must distinguish these cases. */
typedef enum {
    CRYPTO1_RECOVER_OK = 0,             /* success                              */
    CRYPTO1_RECOVER_INVALID_INPUT,      /* NULL / malformed / out-of-domain arg */
    CRYPTO1_RECOVER_NO_CANDIDATE,       /* well-formed input, no result exists  */
    CRYPTO1_RECOVER_INSUFFICIENT_CAPACITY, /* output buffer too small           */
    CRYPTO1_RECOVER_INSUFFICIENT_WORKSPACE, /* scratch workspace too small       */
    CRYPTO1_RECOVER_ABORTED,               /* caller yield callback requested stop */
} crypto1_recover_status_t;

/*
 * Cooperative yield/abort callback for the long (2^20-iteration) recovery loop.
 * Called periodically with the current outer index (counts DOWN from 0xFFFFF).
 * Return true to continue, false to abort (recovery returns _ABORTED). Pass NULL
 * to disable. Intended for the on-device task to refresh the IWDG watchdog and
 * honour a cancel request WITHOUT this module depending on FreeRTOS/HAL. The
 * callback must not block for long and must not re-enter the recovery API.
 */
typedef bool (*crypto1_recover_progress_fn)(void *ctx, uint32_t outer_index);

/*
 * Minimum caller workspace for crypto1_recover_lfsr64(), in 32-bit words.
 * This is the upstream Proxmark `table[1<<16]` moved off the stack into a
 * caller-owned buffer: 65536 words = 262,144 bytes (256 KiB). The workspace is
 * scratch (contents undefined on return); it holds no result.
 */
#define CRYPTO1_RECOVER64_WORKSPACE_WORDS ((size_t)1u << 16)

/*
 * Decrypt a nested (encrypted) tag nonce with a KNOWN sector key.
 * Returns the plaintext nonce nt such that nt_enc = nt ^ ks, where ks is the
 * keystream after clocking (nt_enc ^ cuid) into the cipher.
 * Deterministic, allocation-free, ~O(64) LFSR clocks.
 */
uint32_t crypto1_recover_decrypt_nt_enc(uint32_t cuid, uint32_t nt_enc,
                                        uint64_t known_key);

/*
 * True if the (plaintext) nonce nt, keystream ks and the 3 encrypted parity
 * bits nt_par_enc are mutually consistent (the nested-nonce parity check).
 */
bool crypto1_recover_nonce_matches_parity(uint32_t nt, uint32_t ks,
                                          uint8_t nt_par_enc);

/*
 * True if `nonce` is a weak (16-bit LFSR) PRNG nonce, i.e. its low 16 bits are
 * the LFSR continuation of its high 16 bits. Nonce 0 is treated as non-weak.
 */
bool crypto1_recover_is_weak_prng_nonce(uint32_t nonce);

/*
 * Distance d in [0, 65535] such that suc^d(nt_from) == nt_to, for weak nonces.
 *   out_distance : receives d on success (must be non-NULL)
 * Returns INVALID_INPUT if out_distance is NULL or either nonce is not a weak
 * PRNG nonce; NO_CANDIDATE if the nonces are unrelated (no d within the period).
 * Bounded: at most 65536 suc() steps, no allocation.
 */
crypto1_recover_status_t crypto1_recover_nonce_distance(uint32_t nt_from,
                                                        uint32_t nt_to,
                                                        uint16_t *out_distance);

/*
 * Classify a sequence of collected first-auth nonces as static/constant.
 *   nonces, count : caller-owned sample array (count >= 2 required)
 *   out_is_static : receives true iff every sample is identical
 * Returns INVALID_INPUT for NULL args or count < 2.
 */
crypto1_recover_status_t crypto1_recover_classify_static(const uint32_t *nonces,
                                                         size_t count,
                                                         bool *out_is_static);

/*
 * Copy the weak-PRNG nonces from `nonces` into caller-owned `out`.
 *   out, out_capacity : destination buffer and its element capacity
 *   out_count         : receives the number of weak nonces found (always set on
 *                       a non-NULL pointer, even when capacity is exceeded)
 * Returns INSUFFICIENT_CAPACITY (and fills `out` up to out_capacity) if more
 * weak nonces exist than fit; INVALID_INPUT for NULL where count/capacity > 0.
 * count == 0 is valid and yields OK with *out_count == 0.
 */
crypto1_recover_status_t crypto1_recover_select_weak_nonces(const uint32_t *nonces,
                                                            size_t count,
                                                            uint32_t *out,
                                                            size_t out_capacity,
                                                            size_t *out_count);

/*
 * Per-candidate nested acceptance test: is `candidate_key` consistent with a
 * single collected nested sample (cuid, nt_enc, nt_par_enc)? True iff decrypting
 * nt_enc under the candidate yields a weak-PRNG nonce whose encrypted parity
 * matches. This is the bounded building block a candidate-source orchestrator
 * uses to accept/reject keys; it never searches the keyspace itself.
 */
bool crypto1_recover_key_consistent(uint32_t cuid, uint32_t nt_enc,
                                    uint8_t nt_par_enc, uint64_t candidate_key);

/*
 * GENUINE STATE RECOVERY — Proxmark3 lfsr_recovery64.
 *
 * Recover the Crypto1 LFSR state candidate(s) consistent with 64 observed
 * keystream bits (ks2 = first 32, ks3 = next 32; each a big-endian keystream
 * word using the BEBIT convention, exactly as produced by crypto1_word()).
 * This inverts the cipher from keystream alone — it does NOT take a key.
 *
 *   ks2, ks3        : the 64 observed keystream bits
 *   workspace       : caller-owned scratch, >= CRYPTO1_RECOVER64_WORKSPACE_WORDS
 *                     uint32 words (256 KiB); contents undefined on return
 *   workspace_words : number of uint32 words in `workspace`
 *   out_states      : caller-owned buffer receiving recovered Crypto1 states
 *   out_capacity    : number of Crypto1 entries `out_states` can hold
 *   out_count       : receives the TOTAL number of candidates found (set on
 *                     every non-NULL pointer, even on overflow); the correct
 *                     key is among them — the caller must reconstruct and test
 *                     each with crypto1_recover_state_to_key() + the auth
 *                     rollback (see the test for the exact rollback sequence)
 *
 * Returns:
 *   OK                       >=1 candidate, all fit in out_capacity
 *   NO_CANDIDATE             keystream yielded no state (defensive)
 *   INSUFFICIENT_WORKSPACE   workspace_words < CRYPTO1_RECOVER64_WORKSPACE_WORDS
 *   INSUFFICIENT_CAPACITY    more candidates than out_capacity (out_states filled
 *                            to capacity; *out_count = true total)
 *   INVALID_INPUT            NULL workspace/out_count, or NULL out_states w/ cap>0
 *
 * Determinism: output and count are deterministic for given (ks2, ks3).
 * Candidate ORDER follows the internal search and is deterministic but carries
 * no semantic meaning (do not assume index 0 is "the" key — test all).
 * Bounds: outer loop is exactly 2^20 iterations; no recursion; no dynamic
 * allocation; no VLA. Writes never exceed workspace_words or out_capacity.
 *
 * progress/progress_ctx: optional cooperative yield/abort callback (may be NULL)
 * — see crypto1_recover_progress_fn. Returns _ABORTED if it requests a stop.
 */
crypto1_recover_status_t crypto1_recover_lfsr64(uint32_t ks2, uint32_t ks3,
                                                uint32_t *workspace,
                                                size_t workspace_words,
                                                Crypto1 *out_states,
                                                size_t out_capacity,
                                                size_t *out_count,
                                                crypto1_recover_progress_fn progress,
                                                void *progress_ctx);

/*
 * Reconstruct the 48-bit MIFARE key from a recovered LFSR state
 * (Proxmark3 crypto1_get_lfsr). Returns the key in bits 47..0, matching
 * crypto1_init()'s input convention. Reads only the public odd/even fields.
 */
uint64_t crypto1_recover_state_to_key(const Crypto1 *state);

/* Max candidate states the mfkey64 pipeline keeps on-stack (matches upstream's
 * 16-slot statelist; overflow is reported, never overrun). */
#define CRYPTO1_RECOVER_MFKEY64_MAX_CANDIDATES 16

/*
 * END-TO-END mfkey64 pipeline (Phase 2, item 4) — pure logic, no RF/hardware.
 *
 * Given ONE complete MIFARE Classic authentication transcript, recover and
 * VERIFY the 48-bit sector key. This mirrors the Proxmark3 `mfkey64` tool:
 *   ks2 = ar_enc ^ suc^64(nt);  ks3 = at_enc ^ suc^96(nt)
 *   states = lfsr_recovery64(ks2, ks3)
 *   for each state: roll back {0,0}{0,0}{nr_enc,1}{uid^nt,0} -> candidate key
 *   VERIFY the key by re-running the forward cipher and confirming it
 *   reproduces ks2 and ks3 (prunes any state that does not); dedupe.
 *
 *   uid, nt, nr_enc, ar_enc, at_enc : the observed transcript (32-bit BE)
 *   workspace, workspace_words      : scratch for lfsr_recovery64 (see above)
 *   out_key             : receives the verified key when exactly one is found
 *   out_candidate_count : receives the number of DISTINCT verified keys
 *
 * Returns:
 *   OK                       exactly one distinct verified key (*out_key set)
 *   NO_CANDIDATE             no state verified (bad/incomplete transcript)
 *   INSUFFICIENT_WORKSPACE   workspace too small (see crypto1_recover_lfsr64)
 *   INSUFFICIENT_CAPACITY    lfsr64 produced > MAX_CANDIDATES states, OR more
 *                            than one DISTINCT key verified (ambiguous: 64 bits
 *                            under-determine the key — collect more keystream);
 *                            *out_candidate_count reports how many
 *   INVALID_INPUT            NULL workspace/out_key/out_candidate_count
 *
 * progress/progress_ctx: optional yield/abort callback (may be NULL), forwarded
 * to crypto1_recover_lfsr64; _ABORTED is propagated if it requests a stop.
 *
 * Deterministic; no dynamic allocation; no recursion; bounded stack.
 */
crypto1_recover_status_t crypto1_recover_mfkey64(uint32_t uid, uint32_t nt,
                                                 uint32_t nr_enc, uint32_t ar_enc,
                                                 uint32_t at_enc,
                                                 uint32_t *workspace,
                                                 size_t workspace_words,
                                                 uint64_t *out_key,
                                                 size_t *out_candidate_count,
                                                 crypto1_recover_progress_fn progress,
                                                 void *progress_ctx);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO1_RECOVER_H */
