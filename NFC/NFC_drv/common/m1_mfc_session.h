/*
 * m1_mfc_session.h - dedicated raw MIFARE Classic emulation session (Scope C).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The production state machine for one card-emulation authentication session:
 *   AUTH -> Nt -> {Nr}{Ar} -> Ar verify -> {At} -> READ -> block.
 *
 * This module owns the PROTOCOL LOGIC only: the nonce-trigger gate, the session
 * generation + per-stage buffers, and the Crypto1 orchestration (via ce_mfc.c).
 * It performs NO hardware access, so the entire chain is exercised on host with
 * an independent reader oracle (see test/mfc_session_test.c); the ST25R3916
 * boundary (native nonce TX, transparent-mode custom-parity {At}/block TX, FIFO
 * RX, IRQ ownership) is the caller's job in the target glue (m1_mfc_raw_listener
 * ISR + raw worker), which invokes these functions in order.
 *
 * Hardware division of labour (see also m1_nfc_raw_hal.h, m1_mfc_fast.c,
 * m1_mfc_dma.c): the plaintext nonce is emitted with standard parity by the chip
 * (native TRANSMIT_WITHOUT_CRC) from the AUTH-RXE ISR; {At}/block carry ENCRYPTED
 * parity, which the ST25R3916 can only produce in card-emulation mode via
 * transparent-mode load modulation (no_tx_par is reader-mode only, confirmed
 * against captured ISO/IEC 14443-A traffic). {Nr}{Ar} and READ arrive as
 * ciphertext DATA bytes; Ar verification is mathematically independent of the
 * reader's parity, so par_valid is false in this mode and validation uses `data`.
 */
#ifndef NFC_DRV_M1_MFC_SESSION_H_
#define NFC_DRV_M1_MFC_SESSION_H_

#include <stdint.h>
#include <stdbool.h>
#include "crypto1.h"
#include "ce_mfc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M1_SESS_IDLE = 0,     /* no session; radio owned by RFAL                 */
    M1_SESS_WAIT_AUTH,    /* owns radio post-SELECT; awaiting a gated AUTH    */
    M1_SESS_NONCE_SENT,   /* nonce emitted; awaiting {Nr}{Ar}                 */
    M1_SESS_AUTHED,       /* Ar verified, {At} emitted; answers every READ   */
                          /* for the authenticated sector -- stays AUTHED    */
                          /* after each one, exactly as a real card does not */
                          /* end the session after a single block            */
    M1_SESS_DONE          /* reserved for genuinely terminal/teardown paths;  */
                          /* a successful READ no longer transitions here     */
} m1_sess_state_t;

/*
 * Canonical received-frame contract. `data` holds ciphertext bytes read from the
 * ST25R3916 FIFO. `par` is the 1-bit-per-byte parity SLOT; `par_valid` is false
 * in card-emulation mode (the chip cannot source RX parity there), and Ar/READ
 * validation uses `data` only. Kept explicit so parity is
 * never silently discarded: a future transparent-mode RX decoder can fill `par`
 * and set par_valid without changing any caller.
 */
typedef struct {
    uint8_t data[18];
    uint8_t par[18];
    uint8_t len;         /* number of valid data bytes                        */
    bool    par_valid;   /* false in ST25R card-emulation mode                */
} m1_rx_frame_t;

/* Optional per-(key_type,block) key lookup, set via m1_mfc_session_set_key_
 * resolver(). Returns false (auth then fails safely) if no key is available
 * for that key_type/block -- never a fallback/default key. Left NULL by
 * m1_mfc_session_init(), in which case the session's single fixed `key`
 * field is used unconditionally, exactly as before -- every existing caller
 * (tests, the fixed-persona raw-emulation init) is unaffected unless it
 * explicitly opts in via the setter below. */
typedef bool (*m1_mfc_key_resolve_fn)(void *ctx, uint8_t key_type, uint8_t block,
                                      uint64_t *out_key);

/* Optional per-block real-data lookup, set via m1_mfc_session_set_block_
 * resolver(). Returns false if no real data is available for that block --
 * m1_mfc_session_on_read() then rejects the READ rather than falling back to
 * ce_mfc_block_data()'s deterministic test image. Left NULL by
 * m1_mfc_session_init(), in which case the deterministic image is used
 * unconditionally, exactly as before. */
typedef bool (*m1_mfc_block_resolve_fn)(void *ctx, uint8_t block, uint8_t out_data[16]);

typedef struct {
    m1_sess_state_t state;
    uint32_t        gen;         /* session generation (stale-frame guard)     */
    uint32_t        cuid;        /* card UID as big-endian 32-bit              */
    uint64_t        key;         /* 48-bit sector key (fixed-persona fallback) */
    uint32_t        nt;          /* fixed plaintext tag nonce                  */
    ce_mfc_t        card;        /* continuing Crypto1 card context            */
    uint8_t         key_type;    /* captured 0x60 (Key A) / 0x61 (Key B)       */
    uint8_t         block;       /* captured AUTH block                        */
    uint8_t         nonce_fired; /* one-nonce-per-session guard                */
    uint8_t         last_blk;    /* last READ block answered                   */
    m1_mfc_key_resolve_fn   key_resolve_fn;     /* optional, NULL = use `key`  */
    void                   *key_resolve_ctx;
    m1_mfc_block_resolve_fn block_resolve_fn;   /* optional, NULL = test image */
    void                   *block_resolve_ctx;
} m1_mfc_session_t;

/* Configure the fixed persona (identity + key + nonce). Leaves state IDLE.
 * `key` is used unless a resolver is later installed via the setter below. */
void m1_mfc_session_init(m1_mfc_session_t *s, uint32_t cuid, uint64_t key, uint32_t nt);

/* Install an optional per-(key_type,block) key resolver, overriding the
 * fixed `key` set at init for every subsequent AUTH in this session. Pass
 * fn=NULL to revert to the fixed key. Does not affect state/generation. */
void m1_mfc_session_set_key_resolver(m1_mfc_session_t *s, m1_mfc_key_resolve_fn fn, void *ctx);

/* Install an optional per-block real-data resolver, overriding
 * ce_mfc_block_data()'s deterministic test image for every subsequent READ.
 * Pass fn=NULL to revert to the test image. */
void m1_mfc_session_set_block_resolver(m1_mfc_session_t *s, m1_mfc_block_resolve_fn fn, void *ctx);

/* Open a new session: bumps the generation and enters WAIT_AUTH. Called at the
 * ownership handoff (post-SELECT / ACTIVE_A). */
void m1_mfc_session_begin(m1_mfc_session_t *s);

/* Idempotent terminal cleanup: returns to IDLE. Safe to call from any state and
 * repeatedly (HALT / WUPA / field-loss / STOP / BACK / timeout / error). */
void m1_mfc_session_end(m1_mfc_session_t *s);

/* Nonce-trigger gate (pure, ISR-safe): true IFF state==WAIT_AUTH AND either
 *   - bits==32: frame is { cmd, block, CRC_A_lo, CRC_A_hi } as received raw
 *     off the air (a reader's AUTH command legitimately carries CRC_A, unlike
 *     the card's own Nt reply -- see m1_mfc_session_take_auth()); CRC_A over
 *     frame[0..1] is validated (ce_crc_a(), ce_mfc.h) before frame[0]/frame[1]
 *     are even inspected, or
 *   - bits==16: frame is already a CRC-validated/stripped 2-byte logical
 *     command supplied by a caller that gated the raw frame itself already
 *     (e.g. m1_mfc_raw_hw_process_pending()'s take_auth() re-check of a
 *     previously-gated record) -- no CRC bytes are present to check here.
 * Either way: frame[0] must be 0x60/0x61 and frame[1] < 64. Rejects 30 00,
 * stale 60 FE, bad CRC_A, short/other-length frames, and any frame once an
 * authentication is already running. */
bool m1_mfc_session_nonce_gate(const m1_mfc_session_t *s,
                               const uint8_t *frame, uint16_t bits);

/* Record a gated AUTH and initialise Crypto1; emits the 4 plaintext nonce bytes
 * to transmit (standard parity added by the chip). WAIT_AUTH -> NONCE_SENT.
 * Returns false (no state change) if the frame is not gated, the state is wrong,
 * or a nonce already fired this session. */
bool m1_mfc_session_take_auth(m1_mfc_session_t *s,
                              const uint8_t *frame, uint16_t bits,
                              uint8_t out_nt[4]);

/* Verify the reader's {Nr}{Ar} (8 ciphertext data bytes) and, on success, produce
 * the encrypted {At}[4] + its 4 custom-parity bits. NONCE_SENT -> AUTHED.
 * Returns false (stay silent, no state change) on bad Ar or wrong state. */
bool m1_mfc_session_on_nrar(m1_mfc_session_t *s, const uint8_t nr_ar[8],
                            uint8_t out_at[4], uint8_t out_par[4]);

/* Validate the encrypted READ (4 ciphertext bytes: 0x30, block, CRC_A) and, on
 * success, produce the encrypted 18-byte block response + its 18 custom-parity
 * bits, and report the block. Stays AUTHED on success -- the same
 * authentication answers every subsequent READ for the authenticated sector,
 * matching real MIFARE Classic behavior. Only the per-call
 * READ record is consumed by the caller between commands; the cipher,
 * authenticated block/sector, and key context are untouched here, so Crypto1
 * continues its stream naturally into the next call. Returns 1 on success,
 * 0 on reject (bad opcode / wrong sector / bad CRC; cipher NOT advanced,
 * state stays AUTHED), -1 on wrong state (not currently AUTHED). */
int m1_mfc_session_on_read(m1_mfc_session_t *s, const uint8_t enc[4],
                           uint8_t out_block[18], uint8_t out_par[18], uint8_t *blk);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_M1_MFC_SESSION_H_ */
