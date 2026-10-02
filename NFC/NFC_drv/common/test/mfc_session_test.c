/*
 * mfc_session_test.c - host tests for the raw MFC emulation session state machine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Drives the PRODUCTION orchestrator (m1_mfc_session.c) through the entire
 * sector-0 chain AUTH -> Nt -> {Nr}{Ar} -> Ar verify -> {At} -> READ -> block,
 * with the reader side played by the independent crypto1 primitives. Also covers
 * the nonce gate (rejects 30 00 / stale 60 FE / 7-bit / wrong-state), one-nonce
 * -per-session, bad-Ar silence, READ reject, generation reset, and idempotent end.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/crypto1.c NFC/NFC_drv/common/ce_mfc.c \
 *      NFC/NFC_drv/common/m1_mfc_session.c \
 *      NFC/NFC_drv/common/test/mfc_session_test.c -o /tmp/sess && /tmp/sess
 */
#include "m1_mfc_session.h"
#include "ce_mfc.h"
#include "crypto1.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static uint16_t crc_a(const uint8_t *d, int n)
{
    uint16_t crc = 0x6363;
    for (int i = 0; i < n; i++) {
        uint8_t b = (uint8_t)(d[i] ^ (uint8_t)(crc & 0xFF));
        b = (uint8_t)(b ^ (uint8_t)(b << 4));
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4));
    }
    return crc;
}

#define KEY  0xFFFFFFFFFFFFULL
#define CUID 0x01020304U
#define NT   0x01020304U

/* Drive one full sector-0 session for AUTH (key_type, block) with reader nonce nr[4].
 * Returns 1 iff the reader recovers the exact deterministic block with valid parity+CRC
 * AND the session stayed AUTHED (a successful READ no longer ends the session -- see
 * m1_mfc_session_on_read()). */
static int full_chain(uint8_t key_type, uint8_t block, const uint8_t nr[4])
{
    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, KEY, NT);
    m1_mfc_session_begin(&s);
    if (s.state != M1_SESS_WAIT_AUTH) return 0;

    /* --- AUTH + nonce --- */
    uint8_t frame[2] = { key_type, block };
    uint8_t nt_b[4];
    if (!m1_mfc_session_take_auth(&s, frame, 16, nt_b)) return 0;
    if (s.state != M1_SESS_NONCE_SENT) return 0;
    /* nonce transmitted verbatim (big-endian NT) */
    if (((uint32_t)nt_b[0]<<24 | (uint32_t)nt_b[1]<<16 | (uint32_t)nt_b[2]<<8 | nt_b[3]) != NT) return 0;

    /* --- reader computes {Nr}{Ar} from the nonce, independently --- */
    Crypto1 rc;
    uint8_t nrar[8], nrarpar[8];
    uint32_t ntrec = crypto1_reader_answer(&rc, KEY, CUID, nt_b, nr, nrar, nrarpar, false);
    if (ntrec != NT) return 0;

    /* --- Ar verify + {At} --- */
    uint8_t at_b[4], at_par[4];
    if (!m1_mfc_session_on_nrar(&s, nrar, at_b, at_par)) return 0;
    if (s.state != M1_SESS_AUTHED) return 0;
    /* reader decrypts {At}, must equal suc(nt,96) */
    Crypto1 rc_at = rc; uint32_t at_plain = 0;
    for (int i = 0; i < 4; i++) { uint8_t k; at_plain = (at_plain<<8) | crypto1_decrypt_byte(&rc_at, at_b[i], &k); }
    if (at_plain != crypto1_prng_successor(NT, 96)) return 0;
    rc = rc_at;   /* commit reader cipher past {At} */

    /* --- reader sends encrypted READ 0x30, card answers block --- */
    uint8_t rd[4] = { 0x30, block, 0, 0 };
    uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
    uint8_t cenc[4], cpar[4];
    for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);

    uint8_t renc[18], rpar[18], gotblk = 0xFF;
    if (m1_mfc_session_on_read(&s, cenc, renc, rpar, &gotblk) != 1) return 0;
    if (gotblk != block || s.state != M1_SESS_AUTHED) return 0;

    /* reader recovers the 18-byte response, checks parity + CRC + data */
    uint8_t exp[16]; ce_mfc_block_data(block, exp);
    uint8_t got[18];
    for (int i = 0; i < 18; i++) {
        uint8_t ksp; got[i] = crypto1_decrypt_byte(&rc, renc[i], &ksp);
        uint8_t expp = (uint8_t)((crypto1_odd_parity8(got[i]) ^ ksp) & 1U);
        if ((rpar[i] & 1U) != expp) return 0;
    }
    if (memcmp(got, exp, 16) != 0) return 0;
    uint16_t dcc = crc_a(got, 16);
    if ((uint8_t)(dcc & 0xFF) != got[16] || (uint8_t)(dcc >> 8) != got[17]) return 0;

    m1_mfc_session_end(&s);
    return (s.state == M1_SESS_IDLE) ? 1 : 0;
}

/* Drive one AUTH for sector 0, then READ blocks 0,1,2,3 in sequence under that
 * SAME authentication (no re-AUTH between them, matching real MIFARE Classic
 * behavior). Verifies, for every block: correct
 * plaintext, correct CRC_A, correct encrypted custom parity, session state
 * stays AUTHED after each success, and Crypto1 continuity -- the reader-side
 * mock `rc` is the SAME Crypto1 object carried forward call to call, exactly
 * as a real reader continues clocking one stream across multiple commands
 * under one auth. Then verifies an out-of-sector READ (block 4, sector 1) is
 * rejected without exposing data and without disturbing the session, and
 * that m1_mfc_session_end() still tears down cleanly after several
 * successful reads. Returns 1 iff every check passes. */
static int multi_read_chain(void)
{
    m1_mfc_session_t s;
    m1_mfc_session_init(&s, CUID, KEY, NT);
    m1_mfc_session_begin(&s);
    if (s.state != M1_SESS_WAIT_AUTH) return 0;

    /* --- AUTH for sector 0 (Key A, block 0) --- */
    uint8_t frame[2] = { 0x60, 0 };
    uint8_t nt_b[4];
    if (!m1_mfc_session_take_auth(&s, frame, 16, nt_b)) return 0;

    Crypto1 rc;
    uint8_t nrar[8], nrarpar[8];
    const uint8_t nr0[4] = {0,0,0,0};
    if (crypto1_reader_answer(&rc, KEY, CUID, nt_b, nr0, nrar, nrarpar, false) != NT) return 0;

    uint8_t at_b[4], at_par[4];
    if (!m1_mfc_session_on_nrar(&s, nrar, at_b, at_par)) return 0;
    if (s.state != M1_SESS_AUTHED) return 0;
    Crypto1 rc_at = rc; uint32_t at_plain = 0;
    for (int i = 0; i < 4; i++) { uint8_t k; at_plain = (at_plain<<8) | crypto1_decrypt_byte(&rc_at, at_b[i], &k); }
    if (at_plain != crypto1_prng_successor(NT, 96)) return 0;
    rc = rc_at;   /* commit reader cipher past {At}; this SAME `rc` carries
                   * forward, unreset, through every READ below. */

    /* --- blocks 0,1,2,3 of sector 0, all under the one AUTH above --- */
    for (uint8_t block = 0; block < 4; block++) {
        uint8_t rd[4] = { 0x30, block, 0, 0 };
        uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
        uint8_t cenc[4], cpar[4];
        for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);

        uint8_t renc[18], rpar[18], gotblk = 0xFF;
        if (m1_mfc_session_on_read(&s, cenc, renc, rpar, &gotblk) != 1) return 0;
        if (gotblk != block) return 0;
        /* Session-level readiness: on_read() has no per-call latch of its
         * own beyond `state` -- immediately accepting the NEXT block in
         * this same loop iteration IS the proof nothing needs a separate
         * clear/re-arm at this layer. (The hardware ISR/dispatch layer's
         * own per-activation s_read_rec.ready flag, m1_mfc_raw_session_hw.c,
         * is outside what this host-only harness can exercise -- no
         * ST25R3916/FreeRTOS on host.) */
        if (s.state != M1_SESS_AUTHED) return 0;

        uint8_t exp[16]; ce_mfc_block_data(block, exp);
        uint8_t got[18];
        for (int i = 0; i < 18; i++) {
            uint8_t ksp; got[i] = crypto1_decrypt_byte(&rc, renc[i], &ksp);
            uint8_t expp = (uint8_t)((crypto1_odd_parity8(got[i]) ^ ksp) & 1U);
            if ((rpar[i] & 1U) != expp) return 0;
        }
        if (memcmp(got, exp, 16) != 0) return 0;
        uint16_t dcc = crc_a(got, 16);
        if ((uint8_t)(dcc & 0xFF) != got[16] || (uint8_t)(dcc >> 8) != got[17]) return 0;
    }

    /* --- out-of-sector READ (block 4 = sector 1) must be rejected, without
     * exposing data, and without disturbing the still-AUTHED session. --- */
    {
        uint8_t rd[4] = { 0x30, 4, 0, 0 };
        uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
        uint8_t cenc[4], cpar[4];
        for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
        uint8_t renc[18] = {0}, rpar[18] = {0}, gotblk = 0xFF;
        int rc_read = m1_mfc_session_on_read(&s, cenc, renc, rpar, &gotblk);
        if (rc_read != 0) return 0;                 /* rejected, not accepted    */
        if (gotblk != 0xFF) return 0;                /* *blk left untouched        */
        if (s.state != M1_SESS_AUTHED) return 0;     /* session not disturbed      */
    }

    /* --- explicit stop/field-loss still tears down cleanly after several
     * successful reads (not just after zero, as [7] already covers). --- */
    m1_mfc_session_end(&s);
    if (s.state != M1_SESS_IDLE) return 0;
    m1_mfc_session_end(&s);   /* idempotent */
    if (s.state != M1_SESS_IDLE) return 0;

    return 1;
}

int main(void)
{
    const uint8_t nr0[4] = {0,0,0,0};
    const uint8_t nrX[4] = {0xDE,0xAD,0xBE,0xEF};

    /* [1] full sector-0 chains: Key A, Key B, several blocks/nonces */
    CHECK(full_chain(0x60, 0, nr0),  "full chain KeyA blk0 nr0");
    CHECK(full_chain(0x60, 0, nrX),  "full chain KeyA blk0 nrX");
    CHECK(full_chain(0x61, 0, nrX),  "full chain KeyB blk0");
    CHECK(full_chain(0x60, 4, nr0),  "full chain KeyA blk4");
    CHECK(full_chain(0x60, 63, nrX), "full chain KeyA blk63 (in range)");

    /* [2] nonce gate */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t f30[2]={0x30,0x00}, f60[2]={0x60,0x00}, f60fe[2]={0x60,0xFE}, f61[2]={0x61,0x04};
        CHECK(!m1_mfc_session_nonce_gate(&s, f30, 16),  "gate rejects 30 00");
        CHECK(!m1_mfc_session_nonce_gate(&s, f60fe, 16),"gate rejects stale 60 FE (block>=64)");
        CHECK(!m1_mfc_session_nonce_gate(&s, f60, 7),   "gate rejects 7-bit frame");
        CHECK( m1_mfc_session_nonce_gate(&s, f60, 16),  "gate accepts fresh 60 00");
        CHECK( m1_mfc_session_nonce_gate(&s, f61, 16),  "gate accepts 61 04 (KeyB)");
    }

    /* [2b] nonce gate: raw 32-bit on-air AUTH frame (cmd, block, CRC_A_lo,
     * CRC_A_hi) -- the actual shape a real reader sends and the ST25R3916
     * FIFO delivers (confirmed on hardware: AUTH arrived as 32
     * bits, last_b0=61 last_b1=00, rejected pre-fix only because the gate
     * required exactly 16). Known vectors matching the project's own
     * ce_crc_a(): 60 00 -> F5 7B, 61 00 -> 2D 62. */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t good60[4] = {0x60,0x00,0xF5,0x7B};
        uint8_t good61[4] = {0x61,0x00,0x2D,0x62};
        uint8_t bad_crc[4]    = {0x60,0x00,0x00,0x00};   /* correct cmd, wrong CRC_A     */
        uint8_t bad_cmd[4]    = {0x30,0x00,0x00,0x00};   /* not AUTH; CRC_A computed below so
                                                           * only the command byte is under test */
        uint8_t bad_block[4]  = {0x60,0xFE,0x00,0x00};   /* block>=64; CRC_A computed below   */
        uint8_t truncated[3]  = {0x60,0x00,0xF5};        /* 24 bits: cmd+block+1 CRC byte only */
        uint16_t ccc = crc_a(bad_cmd, 2);
        bad_cmd[2]   = (uint8_t)(ccc & 0xFF); bad_cmd[3]   = (uint8_t)(ccc >> 8);
        uint16_t bcc = crc_a(bad_block, 2);
        bad_block[2] = (uint8_t)(bcc & 0xFF); bad_block[3] = (uint8_t)(bcc >> 8);

        CHECK( m1_mfc_session_nonce_gate(&s, good60, 32), "32-bit gate accepts 60 00 F5 7B");
        CHECK( m1_mfc_session_nonce_gate(&s, good61, 32), "32-bit gate accepts 61 00 2D 62");
        CHECK(!m1_mfc_session_nonce_gate(&s, bad_crc, 32),   "32-bit gate rejects correct cmd, bad CRC_A");
        CHECK(!m1_mfc_session_nonce_gate(&s, bad_cmd, 32),   "32-bit gate rejects correct CRC_A, invalid cmd (30, not 60/61)");
        CHECK(!m1_mfc_session_nonce_gate(&s, bad_block, 32), "32-bit gate rejects correct CRC_A, out-of-range block (>=64)");
        CHECK(!m1_mfc_session_nonce_gate(&s, truncated, 24), "gate rejects a 24-bit truncated frame");
        CHECK(!m1_mfc_session_nonce_gate(&s, good60, 8),     "gate rejects an 8-bit frame");
        CHECK(!m1_mfc_session_nonce_gate(&s, good60, 40),    "gate rejects a 40-bit frame");

        /* Existing 16-bit logical-command acceptance is unchanged (the
         * already-CRC-stripped-record caller, m1_mfc_raw_hw_process_
         * pending()'s take_auth() re-check -- see m1_mfc_session.h). */
        uint8_t logical60[2] = {0x60,0x00};
        CHECK( m1_mfc_session_nonce_gate(&s, logical60, 16), "16-bit logical-command acceptance unchanged");

        /* Accepted 32-bit frame advances to the existing, unchanged Nt-TX
         * path exactly like the 16-bit logical form always has. */
        uint8_t nt_b[4];
        CHECK(m1_mfc_session_take_auth(&s, good60, 32, nt_b),
              "accepted 32-bit AUTH advances to the existing Nt-transmit request");
    }

    /* [3] one nonce per session; gate closed once auth running */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t f60[2]={0x60,0x00}, nt_b[4];
        CHECK(m1_mfc_session_take_auth(&s, f60, 16, nt_b), "first take_auth fires");
        CHECK(!m1_mfc_session_nonce_gate(&s, f60, 16),     "gate closed once NONCE_SENT");
        CHECK(!m1_mfc_session_take_auth(&s, f60, 16, nt_b),"second take_auth rejected");
    }

    /* [4] bad Ar -> silent reject, state unchanged (single cleanup handles it) */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t f60[2]={0x60,0x00}, nt_b[4];
        m1_mfc_session_take_auth(&s, f60, 16, nt_b);
        Crypto1 rc; uint8_t nrar[8], pp[8];
        crypto1_reader_answer(&rc, KEY, CUID, nt_b, nr0, nrar, pp, false);
        nrar[7] ^= 0xFF;   /* corrupt Ar */
        uint8_t at[4], atp[4];
        CHECK(!m1_mfc_session_on_nrar(&s, nrar, at, atp), "bad Ar rejected");
        CHECK(s.state == M1_SESS_NONCE_SENT, "state unchanged after bad Ar");
    }

    /* [5] wrong-state calls are rejected */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t at[4], atp[4], enc[4]={0}, blk18[18], p18[18], b;
        CHECK(!m1_mfc_session_on_nrar(&s, (uint8_t[8]){0}, at, atp), "on_nrar rejected in WAIT_AUTH");
        CHECK(m1_mfc_session_on_read(&s, enc, blk18, p18, &b) == -1, "on_read rejected before AUTHED");
    }

    /* [6] READ reject: wrong opcode after a real auth, cipher not advanced */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT); m1_mfc_session_begin(&s);
        uint8_t f60[2]={0x60,8}, nt_b[4];
        m1_mfc_session_take_auth(&s, f60, 16, nt_b);
        Crypto1 rc; uint8_t nrar[8], pp[8];
        crypto1_reader_answer(&rc, KEY, CUID, nt_b, nr0, nrar, pp, false);
        uint8_t at[4], atp[4];
        m1_mfc_session_on_nrar(&s, nrar, at, atp);
        Crypto1 rc2 = rc; for (int i=0;i<4;i++){uint8_t k;(void)crypto1_decrypt_byte(&rc2, at[i], &k);}
        uint8_t rd[4]={0x31,8,0,0}; uint16_t cc=crc_a(rd,2); rd[2]=(uint8_t)(cc&0xFF); rd[3]=(uint8_t)(cc>>8);
        uint8_t cenc[4], cpar[4]; for(int i=0;i<4;i++)cenc[i]=crypto1_encrypt_byte(&rc2, rd[i], &cpar[i]);
        uint8_t b18[18], p18[18], b;
        CHECK(m1_mfc_session_on_read(&s, cenc, b18, p18, &b) == 0, "wrong-opcode READ rejected");
        CHECK(s.state == M1_SESS_AUTHED, "state stays AUTHED after rejected READ");
    }

    /* [7] generation reset + idempotent end */
    {
        m1_mfc_session_t s; m1_mfc_session_init(&s, CUID, KEY, NT);
        m1_mfc_session_begin(&s); uint32_t g1 = s.gen;
        uint8_t f60[2]={0x60,0}, nt_b[4];
        m1_mfc_session_take_auth(&s, f60, 16, nt_b);
        m1_mfc_session_end(&s);
        CHECK(s.state == M1_SESS_IDLE, "end -> IDLE");
        m1_mfc_session_end(&s);
        CHECK(s.state == M1_SESS_IDLE, "end idempotent");
        m1_mfc_session_begin(&s);
        CHECK(s.gen == g1 + 1U, "begin bumps generation");
        CHECK(s.state == M1_SESS_WAIT_AUTH && s.nonce_fired == 0U, "new session rearmed");
    }

    /* [8] NULL hardening */
    {
        uint8_t nt_b[4], f[2]={0x60,0};
        CHECK(!m1_mfc_session_take_auth(NULL, f, 16, nt_b), "take_auth NULL session");
        CHECK(!m1_mfc_session_nonce_gate(NULL, f, 16), "gate NULL session");
    }

    /* [9a] Consecutive same-sector READs under one AUTH: blocks 0,1,2,3 all
     * answered without re-authenticating, Crypto1 continuity across every
     * READ, out-of-sector rejection, and clean teardown -- the exact
     * multi-block regression for the AUTHED->DONE one-shot fix. */
    CHECK(multi_read_chain(), "consecutive blocks 0-3 under one AUTH, no re-auth between them");

    /* [9] Soak: the PRODUCTION session orchestrator (not just ce_mfc directly)
     * driven through the complete sector-0 chain SOAK_ITERS times with a
     * deterministic PRNG, asserting the reader recovers the exact block with
     * valid parity + CRC every time. This is the exact call sequence the raw
     * ISR/worker (m1_mfc_raw_session_hw.c) drives on hardware. */
    {
        uint64_t rng = 0xC2B2AE3D27D4EB4FULL;
        long ok = 0;
#ifndef SOAK_ITERS
#define SOAK_ITERS 1000
#endif
        for (long it = 0; it < SOAK_ITERS; it++) {
            rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
            uint64_t r = rng * 0x2545F4914F6CDD1DULL;
            uint8_t nr[4] = { (uint8_t)(r>>24), (uint8_t)(r>>16), (uint8_t)(r>>8), (uint8_t)r };
            uint8_t key_type = (r & 0x100) ? 0x61 : 0x60;
            uint8_t block    = (uint8_t)((r >> 9) & 3U);   /* sector-0 blocks 0..3 */
            ok += full_chain(key_type, block, nr);
        }
        CHECK(ok == SOAK_ITERS, "production-orchestrator soak: all iterations succeeded");
        printf("  session soak: %ld/%d full-chain sessions via m1_mfc_session\n", ok, SOAK_ITERS);
    }

    printf("\nmfc_session_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
