/*
 * m1_mfc_session.c - dedicated raw MIFARE Classic emulation session logic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pure protocol state machine (no hardware). See m1_mfc_session.h.
 */
#include "m1_mfc_session.h"
#include <string.h>

/* Big-endian last-4 UID bytes -> cuid, matching ce_mfc's convention. */
void m1_mfc_session_init(m1_mfc_session_t *s, uint32_t cuid, uint64_t key, uint32_t nt)
{
    if (s == NULL) { return; }
    memset(s, 0, sizeof(*s));
    s->cuid  = cuid;
    s->key   = key;
    s->nt    = nt;
    s->state = M1_SESS_IDLE;
    s->gen   = 0U;
    ce_mfc_init(&s->card, cuid);
}

void m1_mfc_session_set_key_resolver(m1_mfc_session_t *s, m1_mfc_key_resolve_fn fn, void *ctx)
{
    if (s == NULL) { return; }
    s->key_resolve_fn  = fn;
    s->key_resolve_ctx = ctx;
}

void m1_mfc_session_set_block_resolver(m1_mfc_session_t *s, m1_mfc_block_resolve_fn fn, void *ctx)
{
    if (s == NULL) { return; }
    s->block_resolve_fn  = fn;
    s->block_resolve_ctx = ctx;
}

void m1_mfc_session_begin(m1_mfc_session_t *s)
{
    if (s == NULL) { return; }
    s->gen++;
    s->state       = M1_SESS_WAIT_AUTH;
    s->key_type    = 0U;
    s->block       = 0U;
    s->nonce_fired = 0U;
    s->last_blk    = 0U;
    ce_mfc_init(&s->card, s->cuid);   /* fresh cipher for the new session */
}

void m1_mfc_session_end(m1_mfc_session_t *s)
{
    if (s == NULL) { return; }
    /* Idempotent: only touch the session-scoped fields; leave persona config. */
    s->state       = M1_SESS_IDLE;
    s->nonce_fired = 0U;
    ce_mfc_init(&s->card, s->cuid);
}

bool m1_mfc_session_nonce_gate(const m1_mfc_session_t *s,
                               const uint8_t *frame, uint16_t bits)
{
    if ((s == NULL) || (frame == NULL))      { return false; }
    if (s->state != M1_SESS_WAIT_AUTH)       { return false; }   /* auth already running */
    if (bits == 32U) {
        /* Raw on-air AUTH frame: cmd, block, CRC_A_lo, CRC_A_hi. Validate
         * CRC_A over frame[0..1] (ce_crc_a(), ce_mfc.h) before trusting cmd/
         * block -- readers append CRC_A to AUTH; the card's Nt reply does not
         * (m1_mfc_session_take_auth() below). */
        uint16_t cc = ce_crc_a(frame, 2U);
        if (((uint8_t)(cc & 0xFFU) != frame[2]) ||
            ((uint8_t)(cc >> 8) != frame[3]))  { return false; }
    } else if (bits != 16U) {
        return false;   /* not a raw 32-bit AUTH frame or a 16-bit logical command */
    }
    /* bits==16 here: an already-CRC-validated/stripped 2-byte logical command
     * (see the header comment) -- no CRC bytes present to check. */
    if ((frame[0] != 0x60U) && (frame[0] != 0x61U)) { return false; } /* not AUTH A/B   */
    if (frame[1] >= 64U)                       { return false; }  /* block out of 1K range */
    return true;
}

bool m1_mfc_session_take_auth(m1_mfc_session_t *s,
                              const uint8_t *frame, uint16_t bits,
                              uint8_t out_nt[4])
{
    if ((out_nt == NULL) || !m1_mfc_session_nonce_gate(s, frame, bits)) { return false; }
    if (s->nonce_fired != 0U)                { return false; }   /* one nonce per session */

    s->key_type = frame[0];
    s->block    = frame[1];

    uint64_t key = s->key;
    if (s->key_resolve_fn != NULL) {
        if (!s->key_resolve_fn(s->key_resolve_ctx, s->key_type, s->block, &key)) {
            return false;   /* no key for this sector/type -- fail safely, no fallback */
        }
    }

    uint8_t nt_par[4];
    bool    plaintext = false;
    if (!ce_mfc_auth1(&s->card, s->key_type, s->block, key, s->nt,
                      out_nt, nt_par, &plaintext)) {
        return false;
    }
    /* First auth: the nonce goes out in clear with standard parity (the chip adds
     * parity on the native TRANSMIT_WITHOUT_CRC), so plaintext must hold. */
    if (!plaintext) { return false; }

    s->nonce_fired = 1U;
    s->state       = M1_SESS_NONCE_SENT;
    return true;
}

bool m1_mfc_session_on_nrar(m1_mfc_session_t *s, const uint8_t nr_ar[8],
                            uint8_t out_at[4], uint8_t out_par[4])
{
    if ((s == NULL) || (nr_ar == NULL) || (out_at == NULL) || (out_par == NULL)) {
        return false;
    }
    if (s->state != M1_SESS_NONCE_SENT) { return false; }

    if (!ce_mfc_auth2(&s->card, nr_ar, out_at, out_par)) {
        /* Bad Ar: a real card stays silent and the reader re-selects. Leave the
         * state as NONCE_SENT so the single idempotent cleanup path handles it. */
        return false;
    }
    s->state = M1_SESS_AUTHED;
    return true;
}

int m1_mfc_session_on_read(m1_mfc_session_t *s, const uint8_t enc[4],
                           uint8_t out_block[18], uint8_t out_par[18], uint8_t *blk)
{
    if ((s == NULL) || (enc == NULL) || (out_block == NULL) || (out_par == NULL)) {
        return -1;
    }
    if (s->state != M1_SESS_AUTHED) { return -1; }

    uint8_t rblk = 0U;
    int rc = ce_mfc_read_cmd_nopar(&s->card, enc, &rblk);   /* decrypt + validate 0x30 */
    if (rc != 1) {
        /* Reject (bad opcode / wrong sector / bad CRC): cipher was not advanced. */
        return 0;
    }
    bool resp_ok;
    if (s->block_resolve_fn != NULL) {
        uint8_t data16[16];
        if (!s->block_resolve_fn(s->block_resolve_ctx, rblk, data16)) {
            return 0;   /* no real data for this block -- fail safely, no test-image fallback */
        }
        resp_ok = ce_mfc_build_read_resp_real(&s->card, data16, out_block, out_par);
    } else {
        resp_ok = ce_mfc_build_read_resp(&s->card, rblk, out_block, out_par);
    }
    if (!resp_ok) {
        return 0;
    }
    s->last_blk = rblk;
    if (blk != NULL) { *blk = rblk; }
    /* Stay AUTHED (not DONE): a real MIFARE Classic authenticated session
     * answers every READ for the authenticated sector, not just one -- a
     * real reader
     * requests the sector's remaining blocks under the SAME authentication,
     * with no re-AUTH between them. Ending the session here made every
     * subsequent same-sector READ (and any immediate re-AUTH attempt, since
     * DONE is silently ignored by the RXE dispatcher) go unanswered until
     * the reader moved to a new sector, which is exactly the deterministic
     * 0/16-sectors-despite-32/32-keys failure this fixes. The cipher (s->card),
     * authenticated block/sector, and key context are untouched above, so
     * Crypto1 continues its stream naturally into the next READ exactly as
     * ce_mfc_read_cmd_nopar()/ce_mfc_build_read_resp() already expect. */
    return 1;
}
