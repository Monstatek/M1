/*
 * mfc_mfkey32.h - MIFARE Classic Crypto1 reader-key recovery (mfkey32v2)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PROVENANCE
 * ----------
 * This is a memory-bounded, workspace-based M1 port of the mfkey32v2
 * algorithm, adapted from a local, license-compatible reference:
 *   NFC/NFC_drv/legacy/mfkey32.c/.h in the sibling M1-firmware forks
 *   dagnazty/M1_T-1000 and hapaxx11/M1 (both GPL-3.0, COPYING.txt present
 *   at their repo roots), which themselves credit "noproto/FlipperMfkey
 *   (GPLv3)" and the underlying Crapto-1 algorithm lineage (bla / Karsten
 *   Nohl / Courtois et al.). This M1 project is already GPL-3.0-or-later
 *   (see crypto1_recover.c/.h, ported from the same Crapto-1 lineage via
 *   Proxmark3), so this port is license-compatible with the rest of this
 *   codebase -- same pattern already established there.
 *
 * Adaptation from the reference (algorithm and tables are otherwise kept
 * intentionally faithful -- this is dense, easy-to-subtly-break bit-
 * twiddling cryptographic code, so the only deliberate change is memory
 * ownership, not the math):
 *   - The reference's five heap-allocated (malloc/free) working buffers
 *     (~110 KB total: two 16-entry `struct Msb` tables, two 1280-word temp
 *     state buffers, one 1024-word state buffer) are replaced by fields of
 *     ONE caller-supplied mfc_mfkey32_workspace_t, matching the same
 *     no-malloc, caller-owned-workspace convention crypto1_recover.h
 *     already established for crypto1_recover_lfsr64()'s 256 KiB
 *     workspace. No dynamic allocation, no hidden mutable globals.
 *   - The reference's two module-level statics (s_tick_cb/s_tick_ctx) and
 *     one abort flag are passed as explicit parameters instead, matching
 *     this module's own progress-callback convention
 *     (mfc_mfkey32_tick_fn) and making the function re-entrant/thread-
 *     naive-safe the way every other pure module in this directory is.
 *   - Everything else (filter(), the two lookup tables, crypt_word(),
 *     rollback primitives, prng_successor(), crypto1_get_lfsr(),
 *     check_state(), state_loop(), extend_table(), old_recover(),
 *     calculate_msb_tables(), the top-level MSB round loop) is a faithful
 *     line-for-line port, verified byte-identical in behavior against the
 *     reference by running BOTH the reference (unmodified, as its own
 *     MFKEY32_HOST_TEST) and this port against the SAME published vector
 *     (see mfc_mfkey32_selftest() below) and confirming identical output
 *     (see the accompanying test file's own commit message / report for
 *     the exact command and result).
 *
 * SCOPE: this module is pure logic -- no RFAL/HAL/FreeRTOS dependency,
 * host-buildable and host-testable exactly like crypto1_recover.c. It
 * recovers the READER's key from two captured authentication attempts
 * where M1 is emulating the CARD (see NFC/NFC_drv/legacy/mfc_detect.c/.h
 * and NFC/NFC_drv/common/mfc_capture.c/.h for the capture side, which
 * already produces exactly this function's input shape via mfc_pair_t).
 * This is the "Extract Keys" feature's solver -- explicitly NOT wired
 * into "Find Missing Keys" (a completely different scenario: M1 reading
 * a physical card, not emulating one to a reader).
 */
#ifndef MFC_MFKEY32_H_
#define MFC_MFKEY32_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal table sizing (mirrors the reference's MSB_LIMIT=16 out of 256,
 * struct Msb { int tail; uint32_t states[768]; }, and its three scratch
 * buffers). Exposed only so mfc_mfkey32_workspace_t's array sizes are
 * self-documenting; callers should not need these directly. */
#define MFC_MFKEY32_MSB_LIMIT        16U
#define MFC_MFKEY32_MSB_STATES_MAX   768U
#define MFC_MFKEY32_TEMP_STATES_MAX  1280U
#define MFC_MFKEY32_STATES_BUF_MAX   1024U

typedef struct {
    int          tail;
    unsigned int states[MFC_MFKEY32_MSB_STATES_MAX];   /* unsigned int, not
        uint32_t: matches the ported algorithm's own native type exactly.
        Both are 32 bits on every platform this module builds for (host and
        this project's STM32H5 target), but uint32_t is a DIFFERENT C type
        from unsigned int on this ARM EABI (unsigned long, not unsigned
        int) -- using unsigned int here avoids an incompatible-pointer-type
        build error at the workspace/algorithm boundary without any cast. */
} mfc_mfkey32_msb_t;

/* Caller-owned working arena -- exactly the reference's ~110 KB heap
 * arena, laid out as static fields instead of five malloc'd blocks. No
 * result is carried here; contents are scratch and undefined once
 * mfc_mfkey32_recover() returns. A caller typically places one instance in
 * a location that is only "hot" while a solve is actually running (e.g. a
 * stack allocation in a worker task with a large enough stack, or a
 * dedicated static buffer reused across solves -- never on the NFC RF
 * task's own small stack; see the accompanying capture-coordinator commit
 * for exactly where this is placed and why). */
typedef struct {
    mfc_mfkey32_msb_t odd_msbs[MFC_MFKEY32_MSB_LIMIT];
    mfc_mfkey32_msb_t even_msbs[MFC_MFKEY32_MSB_LIMIT];
    unsigned int       temp_states_odd[MFC_MFKEY32_TEMP_STATES_MAX];
    unsigned int       temp_states_even[MFC_MFKEY32_TEMP_STATES_MAX];
    unsigned int       states_buffer[MFC_MFKEY32_STATES_BUF_MAX];
} mfc_mfkey32_workspace_t;

/* Periodic tick during recovery (~every 32768 inner iterations of one MSB
 * round, and once per MSB round boundary). Use it to feed the watchdog,
 * refresh a progress UI, and poll for cancel -- this recovery genuinely
 * takes minutes on-device (see this module's accompanying cost-measurement
 * report), so a caller MUST supply this to stay responsive; NULL disables
 * it entirely (host test use only -- never pass NULL from a worker task).
 * msb_round is the current chunk (0..MFC_MFKEY32_MSB_LIMIT-1... actually
 * 0..(256/MFC_MFKEY32_MSB_LIMIT)-1, i.e. 0..15). Return false to abort. */
typedef bool (*mfc_mfkey32_tick_fn)(void *ctx, int msb_round);

/*
 * Recover the 48-bit reader key from two captured authentication attempts
 * against the SAME emulated {cuid, sector, key_type} -- exactly the shape
 * mfc_pair_t (mfc_capture.h) already stores. All inputs are as observed on
 * the wire:
 *   uid       : the emulated card's UID, big-endian as one u32 (same CUID
 *               convention as crypto1_recover.h/mfc_capture.h)
 *   nt0, nt1  : the two nonces M1 itself generated and sent (plaintext --
 *               M1 chose them, so no decryption is needed or possible)
 *   nr0, nr1  : the two reader challenges, EXACTLY as received (still
 *               encrypted -- never decrypt these before calling)
 *   ar0, ar1  : the two reader answers, EXACTLY as received (still
 *               encrypted)
 * workspace   : caller-owned scratch (~110 KB); contents undefined on
 *               return; never NULL
 * key_out     : receives the 48-bit key (MSB-first in the low 48 bits) on
 *               success; untouched on failure
 * tick/tick_ctx : progress/cancel callback (see mfc_mfkey32_tick_fn); pass
 *               NULL only for host tests
 *
 * Returns true and writes *key_out only when a key was found and (via
 * check_state()'s own internal cross-check against BOTH captured answers)
 * self-consistently verified against both attempts -- this function never
 * reports a candidate that satisfies only one of the two transcripts.
 * Returns false if no key is found, if workspace/key_out is NULL, or if
 * the tick callback requested an abort.
 *
 * Deterministic for a given (uid, nt0, nr0, ar0, nt1, nr1, ar1); no
 * dynamic allocation, no recursion depth beyond the reference's own
 * bounded old_recover() (rem starts at 3, decrements each call -- at most
 * 4 levels deep, matching the reference exactly).
 */
bool mfc_mfkey32_recover(uint32_t uid,
                         uint32_t nt0, uint32_t nr0, uint32_t ar0,
                         uint32_t nt1, uint32_t nr1, uint32_t ar1,
                         mfc_mfkey32_workspace_t *workspace,
                         uint64_t *key_out,
                         mfc_mfkey32_tick_fn tick, void *tick_ctx);

/*
 * Built-in correctness gate against a PUBLISHED third-party mfkey32v2
 * vector (uid=2a234f80, nt0=240bd022, {nr0}=ad2e1687, {ar0}=57e6f7e4,
 * nt1=18a4bd3e, {nr1}=accc1a23, {ar1}=6f10e401 -> key=a0a1a2a3a4a5). The
 * key is NEVER passed to mfc_mfkey32_recover() -- only compared against
 * its return value here, proving genuine blind recovery, not a round-trip
 * tautology. Confirmed absent from every M1 dictionary (Built-in, User,
 * System) at the time this was written -- see the accompanying commit
 * message for the exact verification. Safe to call before any nonce
 * capture; takes the same ~minutes as any other solve, so this is a host-
 * test/manufacturing-diagnostic gate, not something to call from the UI
 * path on every boot.
 */
bool mfc_mfkey32_selftest(mfc_mfkey32_workspace_t *workspace);

#ifdef __cplusplus
}
#endif

#endif /* MFC_MFKEY32_H_ */
