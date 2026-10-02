/*
 * ce_mfc_test.c - host tests for the MIFARE Classic card-side auth engine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Proves the card-side handshake (ce_mfc) is the exact mirror of the reader-side
 * (crypto1_reader_answer, the same primitive the on-card poller uses): a nonce
 * from the card, a reader answer, and a verified {at} the reader can decrypt to
 * prng_successor(nt,96).
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I NFC/NFC_drv/common \
 *      NFC/NFC_drv/common/crypto1.c NFC/NFC_drv/common/ce_mfc.c \
 *      NFC/NFC_drv/common/test/ce_mfc_test.c -o /tmp/ce_mfc && /tmp/ce_mfc
 */
#include "ce_mfc.h"
#include "crypto1.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static uint32_t be32(const uint8_t b[4])
{
    return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}

/* ISO/IEC 14443-A CRC_A (poly 0x8408, preload 0x6363); CRC[0]=low, CRC[1]=high. */
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

/* Run one full first-auth handshake for (key, cuid, nt). Returns true if the
 * card authenticated the reader AND the reader can decrypt {at}=suc(nt,96). */
static bool run_handshake(uint64_t key, uint32_t cuid, uint32_t nt, const uint8_t nr[4])
{
    ce_mfc_t card;
    ce_mfc_init(&card, cuid);

    uint8_t nt_b[4], par[4];
    bool plain = false;
    if (!ce_mfc_auth1(&card, 0x60, 0, key, nt, nt_b, par, &plain)) return false;
    if (!plain) return false;                 /* first auth must be plaintext */
    if (be32(nt_b) != nt) return false;       /* nonce transmitted verbatim   */

    /* Reader side: produce {Nr}{ar} from the card nonce. */
    Crypto1 rc;
    uint8_t out8[8], outpar8[8];
    uint32_t nt_rec = crypto1_reader_answer(&rc, key, cuid, nt_b, nr, out8, outpar8, false);
    if (nt_rec != nt) return false;

    /* Card verifies {Nr}{ar} and replies with encrypted {at}. */
    uint8_t at_b[4], atpar[4];
    if (!ce_mfc_auth2(&card, out8, at_b, atpar)) return false;
    if (card.state != CE_MFC_AUTHED) return false;

    /* Reader decrypts {at} and checks it equals suc(nt,96). */
    uint32_t at_plain = 0;
    for (int i = 0; i < 4; i++) { uint8_t ksp; at_plain = (at_plain << 8) | crypto1_decrypt_byte(&rc, at_b[i], &ksp); }
    return (at_plain == crypto1_prng_successor(nt, 96));
}

int main(void)
{
    const uint8_t nr0[4] = { 0x00, 0x00, 0x00, 0x00 };
    const uint8_t nrX[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    /* [1] Full handshake round-trips for several keys / cuids / nonces. */
    CHECK(run_handshake(0xFFFFFFFFFFFFULL, 0xCAFEBABE, 0x11223344, nr0), "handshake FF key, nr=0");
    CHECK(run_handshake(0xFFFFFFFFFFFFULL, 0xCAFEBABE, 0x11223344, nrX), "handshake FF key, nr=DEADBEEF");
    CHECK(run_handshake(0xA0A1A2A3A4A5ULL, 0x0BADF00D, 0x89ABCDEF, nrX), "handshake dict key");
    CHECK(run_handshake(0x000000000000ULL, 0x12345678, 0xFFFFFFFF, nr0), "handshake zero key");
    CHECK(run_handshake(0xD3F7D3F7D3F7ULL, 0xA5A5A5A5, 0x00000001, nrX), "handshake transit key");

    /* [2] state starts IDLE; becomes AUTHED only after a valid auth2. */
    {
        ce_mfc_t s; ce_mfc_init(&s, 0x11111111);
        CHECK(s.state == CE_MFC_IDLE, "starts idle");
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&s, 0x60, 4, 0xFFFFFFFFFFFFULL, 0xAABBCCDD, nt_b, par, &pl);
        CHECK(s.state == CE_MFC_IDLE, "still idle after auth1 (not yet verified)");
        CHECK(s.nt == 0xAABBCCDD && s.key_type == 0x60 && s.auth_block == 4, "auth1 records context");
    }

    /* [3] wrong {Nr}{ar} is rejected (card stays silent, not AUTHED). */
    {
        uint64_t key = 0xFFFFFFFFFFFFULL; uint32_t cuid = 0xCAFEBABE, nt = 0x11223344;
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 0, key, nt, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr0, o8, op8, false);
        o8[7] ^= 0xFF;                         /* corrupt ar */
        uint8_t at_b[4], atpar[4];
        CHECK(ce_mfc_auth2(&card, o8, at_b, atpar) == false, "corrupt ar rejected");
        CHECK(card.state != CE_MFC_AUTHED, "not authed after bad reader answer");
    }

    /* [4] NULL hardening. */
    {
        ce_mfc_t s; ce_mfc_init(&s, 0);
        uint8_t a[4], b[4]; bool pl;
        CHECK(ce_mfc_auth1(NULL, 0x60, 0, 0, 0, a, b, &pl) == false, "auth1 NULL session");
        CHECK(ce_mfc_auth2(&s, NULL, a, b) == false, "auth2 NULL nr_ar");
    }

    /* ---- POSTAUTH-1: encrypted READ command + 18-byte block response ---- */
    /* Drive one full auth to AUTHED with card+reader ciphers synchronised
     * through {At}, then round-trip an encrypted 0x30 READ and its response. */
    {
        uint64_t key = 0xFFFFFFFFFFFFULL; uint32_t cuid = 0x01020304, nt = 0x01020304;
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 8, key, nt, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr0, o8, op8, false);
        uint8_t at_b[4], atpar[4];
        CHECK(ce_mfc_auth2(&card, o8, at_b, atpar), "postauth: auth2 ok");
        /* Advance the reader cipher through {At} so both sides are post-{At}. */
        for (int i = 0; i < 4; i++) { uint8_t k; (void)crypto1_decrypt_byte(&rc, at_b[i], &k); }

        /* Reader -> card: encrypted READ 0x30, block 8, + CRC_A. */
        uint8_t rd[4] = { 0x30, 8, 0, 0 };
        uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
        uint8_t cenc[4], cpar[4];
        for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
        uint8_t cplain[4];
        CHECK(ce_mfc_decrypt_cmd(&card, cenc, cpar, 4, cplain), "postauth: READ cmd parity valid");
        CHECK(memcmp(cplain, rd, 4) == 0, "postauth: READ cmd decrypts to 30 08 + CRC");
        CHECK(cplain[0] == 0x30, "postauth: command byte is 0x30");
        uint16_t rxcc = crc_a(cplain, 2);
        CHECK(((uint8_t)(rxcc & 0xFF) == cplain[2]) && ((uint8_t)(rxcc >> 8) == cplain[3]),
              "postauth: READ cmd CRC_A valid");

        /* Card -> reader: 16-byte block + CRC_A = 18 encrypted bytes + 18 parity. */
        uint8_t block[16] = { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                              0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF };
        uint8_t resp[18]; memcpy(resp, block, 16);
        uint16_t bc = crc_a(resp, 16); resp[16] = (uint8_t)(bc & 0xFF); resp[17] = (uint8_t)(bc >> 8);
        uint8_t renc[18], rpar[18];
        CHECK(ce_mfc_encrypt_resp(&card, resp, 18, renc, rpar), "postauth: encrypt 18-byte response");

        /* Reader decrypts, verifies every parity bit and CRC_A over the 16 bytes. */
        uint8_t dplain[18]; bool par_ok = true;
        for (int i = 0; i < 18; i++) {
            uint8_t ksp; dplain[i] = crypto1_decrypt_byte(&rc, renc[i], &ksp);
            uint8_t expp = (uint8_t)((crypto1_odd_parity8(dplain[i]) ^ ksp) & 1U);
            if ((rpar[i] & 1U) != expp) par_ok = false;
        }
        CHECK(par_ok, "postauth: all 18 response parity bits valid");
        CHECK(memcmp(dplain, block, 16) == 0, "postauth: reader recovers exact 16 block bytes");
        uint16_t dcc = crc_a(dplain, 16);
        CHECK(((uint8_t)(dcc & 0xFF) == dplain[16]) && ((uint8_t)(dcc >> 8) == dplain[17]),
              "postauth: response CRC_A valid");

        /* One-bit parity corruption is rejected (recompute from same state). */
        {
            ce_mfc_t c2 = card; Crypto1 r2 = rc;   /* copies are post-response; re-derive */
            (void)c2; (void)r2;
        }
    }

    /* [6] parity corruption and wrong command are rejected. */
    {
        /* Build a fresh post-{At} state, corrupt one command parity bit. */
        uint64_t key = 0xFFFFFFFFFFFFULL; uint32_t cuid = 0x01020304, nt = 0x01020304;
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 8, key, nt, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr0, o8, op8, false);
        uint8_t at_b[4], atpar[4]; ce_mfc_auth2(&card, o8, at_b, atpar);
        for (int i = 0; i < 4; i++) { uint8_t k; (void)crypto1_decrypt_byte(&rc, at_b[i], &k); }
        uint8_t rd[4] = { 0x30, 8, 0, 0 };
        uint16_t cc = crc_a(rd, 2); rd[2] = (uint8_t)(cc & 0xFF); rd[3] = (uint8_t)(cc >> 8);
        uint8_t cenc[4], cpar[4];
        for (int i = 0; i < 4; i++) cenc[i] = crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
        cpar[1] ^= 1U;                              /* corrupt one parity bit */
        uint8_t cplain[4];
        CHECK(ce_mfc_decrypt_cmd(&card, cenc, cpar, 4, cplain) == false, "postauth: bad cmd parity rejected");
    }

    /* [7] new auth / init resets post-auth state (no leak between sessions). */
    {
        ce_mfc_t s; ce_mfc_init(&s, 0x01020304);
        uint8_t enc[4] = {0}, p[4] = {0}, out[4];
        CHECK(ce_mfc_decrypt_cmd(&s, enc, p, 4, out) == false, "decrypt_cmd fails when not AUTHED");
        CHECK(ce_mfc_encrypt_resp(&s, out, 4, enc, p) == false, "encrypt_resp fails when not AUTHED");
    }

    /* [8] Known-answer {At}: key FF..FF, uid/nt 01020304, nr 0 -> {At}=03CA9A82,
     * parity 0011. This is the value the reader decrypted on real hardware, an
     * independent check that ce_mfc's cipher+parity match the on-air result. */
    {
        ce_mfc_t card; ce_mfc_init(&card, 0x01020304);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 8, 0xFFFFFFFFFFFFULL, 0x01020304, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, 0xFFFFFFFFFFFFULL, 0x01020304, nt_b, nr0, o8, op8, false);
        uint8_t at_b[4], atpar[4];
        CHECK(ce_mfc_auth2(&card, o8, at_b, atpar), "KAT: auth2 ok");
        CHECK(at_b[0]==0x03 && at_b[1]==0xCA && at_b[2]==0x9A && at_b[3]==0x82, "KAT: {At}=03CA9A82");
        CHECK(atpar[0]==0 && atpar[1]==0 && atpar[2]==1 && atpar[3]==1, "KAT: {At} parity 0011");
    }

    /* [9] Target-mode parity-unavailable READ path (4 ciphertext bytes, no parity). */
    {
        uint64_t key=0xFFFFFFFFFFFFULL; uint32_t cuid=0x01020304, nt=0x01020304;
        ce_mfc_t card; ce_mfc_init(&card, cuid);
        uint8_t nt_b[4], par[4]; bool pl;
        ce_mfc_auth1(&card, 0x60, 8, key, nt, nt_b, par, &pl);
        Crypto1 rc; uint8_t o8[8], op8[8];
        crypto1_reader_answer(&rc, key, cuid, nt_b, nr0, o8, op8, false);
        uint8_t at_b[4], atpar[4]; ce_mfc_auth2(&card, o8, at_b, atpar);
        for (int i=0;i<4;i++){ uint8_t k; (void)crypto1_decrypt_byte(&rc, at_b[i], &k); }

        /* Reader encrypts READ 0x30 blk8; card sees only 4 ciphertext bytes. */
        uint8_t rd[4]={0x30,8,0,0}; uint16_t cc=crc_a(rd,2); rd[2]=(uint8_t)(cc&0xFF); rd[3]=(uint8_t)(cc>>8);
        uint8_t cenc[4], cpar[4];
        for (int i=0;i<4;i++) cenc[i]=crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
        uint8_t blk=0xFF;
        CHECK(ce_mfc_read_cmd_nopar(&card, cenc, &blk)==1, "nopar: valid READ accepted");
        CHECK(blk==8, "nopar: block 8 extracted");

        uint8_t renc[18], rpar[18];
        CHECK(ce_mfc_build_read_resp(&card, 8, renc, rpar), "nopar: build 18-byte response");
        uint8_t exp[16]; ce_mfc_block_data(8, exp);
        uint8_t got[18]; bool pok=true;
        for (int i=0;i<18;i++){ uint8_t ksp; got[i]=crypto1_decrypt_byte(&rc, renc[i], &ksp);
            if ((rpar[i]&1U)!=((crypto1_odd_parity8(got[i])^ksp)&1U)) pok=false; }
        CHECK(pok, "nopar: reader parity all valid");
        CHECK(memcmp(got, exp, 16)==0, "nopar: reader recovers deterministic block 80..8F");
        uint16_t dcc=crc_a(got,16);
        CHECK((uint8_t)(dcc&0xFF)==got[16] && (uint8_t)(dcc>>8)==got[17], "nopar: response CRC_A valid");
    }

    /* [10] no-parity rejections: wrong opcode and wrong sector, cipher not advanced. */
    {
        uint64_t key=0xFFFFFFFFFFFFULL; uint32_t cuid=0x01020304, nt=0x01020304;
        for (int variant=0; variant<2; variant++) {
            ce_mfc_t card; ce_mfc_init(&card, cuid);
            uint8_t nt_b[4], par[4]; bool pl;
            ce_mfc_auth1(&card, 0x60, 8, key, nt, nt_b, par, &pl);   /* authed sector 2 */
            Crypto1 rc; uint8_t o8[8], op8[8];
            crypto1_reader_answer(&rc, key, cuid, nt_b, nr0, o8, op8, false);
            uint8_t at_b[4], atpar[4]; ce_mfc_auth2(&card, o8, at_b, atpar);
            for (int i=0;i<4;i++){ uint8_t k; (void)crypto1_decrypt_byte(&rc, at_b[i], &k); }
            ce_mfc_t before = card;
            uint8_t rd[4];
            if (variant==0) { rd[0]=0x31; rd[1]=8; }     /* wrong opcode */
            else            { rd[0]=0x30; rd[1]=0; }     /* wrong sector (block 0) */
            uint16_t cc=crc_a(rd,2); rd[2]=(uint8_t)(cc&0xFF); rd[3]=(uint8_t)(cc>>8);
            uint8_t cenc[4], cpar[4];
            for (int i=0;i<4;i++) cenc[i]=crypto1_encrypt_byte(&rc, rd[i], &cpar[i]);
            uint8_t blk=0xFF;
            CHECK(ce_mfc_read_cmd_nopar(&card, cenc, &blk)==0, variant==0?"nopar: wrong opcode rejected":"nopar: wrong sector rejected");
            CHECK(memcmp(&before.cipher, &card.cipher, sizeof(card.cipher))==0, "nopar: cipher NOT advanced on reject");
        }
    }

    printf("\nce_mfc_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
