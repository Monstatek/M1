/*============================================================================*/
/**
 * @file    mfc_detect.c
 * @brief   MIFARE Classic Detect Reader authentication-capture RF handler.
 *
 * See mfc_detect.h. Composes the existing raw manual-parity RFAL transceive
 * (as used by nfc_poller.c on the initiator side) on the LISTENER side to send
 * Nt and capture the reader's encrypted {Nr,Ar}. RAM-only capture via
 * mfc_capture.*. No Crypto1 verification is needed to record a capture.
 */
/*============================================================================*/
#include "mfc_detect.h"
#include "mfc_capture.h"

#include "rfal_rf.h"
#include "rfal_utils.h"
#include "rfal_platform.h"
#include "stm32h5xx_hal.h"   /* HAL_GetTick() -- same convention nfc_poller.c uses */

/* ---- Emulated MIFARE Classic 1K identity for reader detection -------------
 * A fixed 4-byte UID is sufficient for Phase-1A capture; the reader issues
 * normal MFC AUTH regardless of UID value. CUID = big-endian u32 of the UID,
 * the standard Crypto1 CUID convention for a 4-byte MIFARE Classic UID (same
 * derivation nfc_poller.c uses from the last 4 UID bytes). */
static const uint8_t MFC_DR_UID[4] = { 0x01U, 0x02U, 0x03U, 0x04U };

/* MIFARE Classic frame timing / raw flags (mirrors nfc_poller.c so the encrypted
 * frames are neither parity- nor CRC-transformed by RFAL). CRC_RX_MANUAL is
 * required alongside PAR_RX_KEEP (RFAL rejects PAR_RX_KEEP without it). */
#define MFC_DR_FWT          rfalConvMsTo1fc(10U)
#define MFC_DR_RAW_FLAGS   ( (uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE   | \
                             (uint32_t)RFAL_TXRX_FLAGS_PAR_RX_KEEP   | \
                             (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL | \
                             (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP   | \
                             (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL | \
                             (uint32_t)RFAL_TXRX_FLAGS_AGC_ON )

#define MFC_DR_AUTH_KEY_A   0x60U
#define MFC_DR_AUTH_KEY_B   0x61U

/* How long an incomplete {sector,key_type} pair may wait for its second
 * attempt before it is invalidated (mfc_capture_check_timeout()). Generous
 * relative to a normal reader's own poll interval (typically well under a
 * second) while still bounding how long a stale partial can linger across
 * an idle Extract Keys session. */
#define MFC_DR_PAIR_TIMEOUT_MS  30000U

static mfc_dr_state_t s_state = MFC_DR_IDLE;
static uint32_t       s_cuid;
static uint32_t       s_prng = 0x1D872B41UL;   /* xorshift32 state for Nt      */

/* ---- small local helpers (duplicated intentionally so the validated
 *      nfc_poller.c is not touched; ~identical to its mfc_pack/unpack) ------- */

/* ISO14443A per-byte ODD parity bit. */
static uint8_t odd_parity(uint8_t b)
{
    uint8_t p = 1U;
    for (uint8_t i = 0U; i < 8U; i++) { p ^= (uint8_t)((b >> i) & 1U); }
    return p;   /* 1 => data has even #1s (parity makes total odd) */
}

/* xorshift32: Nt only needs to vary between the two auths of a pair. */
static uint32_t next_nt(void)
{
    uint32_t x = s_prng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_prng = x;
    return x;
}

/* Pack n bytes as (8 data bits LSB-first + 1 parity bit) each; returns #bits. */
static uint16_t pack_bits(uint8_t *out, const uint8_t *data, const uint8_t *par, uint8_t n)
{
    uint16_t bit = 0U;
    for (uint16_t i = 0U; i < (uint16_t)((n * 9 + 7) / 8); i++) { out[i] = 0U; }
    for (uint8_t i = 0U; i < n; i++) {
        for (uint8_t j = 0U; j < 8U; j++) {
            if (((data[i] >> j) & 1U) != 0U) { out[bit >> 3] |= (uint8_t)(1U << (bit & 7U)); }
            bit++;
        }
        if ((par[i] & 1U) != 0U) { out[bit >> 3] |= (uint8_t)(1U << (bit & 7U)); }
        bit++;
    }
    return bit;
}

/* Unpack a bit-packed rx buffer into up to maxn (byte,parity) groups. */
static uint8_t unpack_bits(const uint8_t *in, uint16_t rxbits, uint8_t *data, uint8_t maxn)
{
    uint8_t  n   = 0U;
    uint16_t bit = 0U;
    while (((bit + 9U) <= rxbits) && (n < maxn)) {
        uint8_t b = 0U;
        for (uint8_t j = 0U; j < 8U; j++) {
            if (((in[bit >> 3] >> (bit & 7U)) & 1U) != 0U) { b |= (uint8_t)(1U << j); }
            bit++;
        }
        data[n] = b;
        bit++;          /* skip the parity bit */
        n++;
    }
    return n;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ---- session lifecycle ---------------------------------------------------- */
void mfc_detect_begin(void)
{
    s_cuid  = be32(MFC_DR_UID);
    s_state = MFC_DR_WAIT_READER;
    mfc_capture_reset(s_cuid);
    platformLog("[MFC-DR] begin cuid=%08lX\r\n", (unsigned long)s_cuid);
}

void mfc_detect_end(void)
{
    /* STOP/BACK: any still-incomplete pair must never be completed by a
     * later, causally-unrelated session -- invalidate it now rather than
     * waiting for the next mfc_detect_begin()'s full reset (which would
     * also be too late if a stray frame arrived in between). Already-
     * completed pairs are unaffected and remain in the store for the
     * caller (nfc_detect_reader_gui_destroy(), m1_csrc/m1_nfc.c) to read
     * before the next begin() wipes everything. */
    mfc_capture_invalidate_incomplete();
    s_state = MFC_DR_IDLE;
    platformLog("[MFC-DR] end pairs=%lu nonces=%lu\r\n",
                (unsigned long)mfc_capture_pair_count(),
                (unsigned long)mfc_capture_nonce_count());
}

void mfc_detect_on_activated(void)
{
    /* test04: activation milestone log ONLY. "Reader traffic" (READER_ACTIVE) is
     * now driven by a REAL inbound frame in service_frame, not by RFAL activation,
     * so we can tell whether reader frames actually reach the software path. */
    platformLog("[MFC-DR] activated\r\n");
}

void mfc_detect_on_field_lost(void)
{
    if (s_state == MFC_DR_READER_ACTIVE) {
        s_state = MFC_DR_WAIT_READER;
        /* Invalidate any in-flight (still-incomplete) pair -- a reader
         * that returns after field loss must start that {sector,key_type}
         * pair over, never silently complete a half-captured attempt from
         * before the loss. Already-completed pairs are kept (unaffected
         * by generation invalidation), matching this function's original
         * "keep completed captures" contract exactly. */
        mfc_capture_invalidate_incomplete();
        platformLog("[MFC-DR] field lost\r\n");
    }
}

mfc_dr_state_t mfc_detect_state(void) { return s_state; }
uint32_t       mfc_detect_cuid(void)  { return s_cuid; }

/* ---- the capture exchange -------------------------------------------------- */
bool mfc_detect_service_frame(const uint8_t *rx, uint16_t rxLenBits)
{
    uint8_t  cmd;
    uint8_t  block;
    uint8_t  key_is_b;
    uint8_t  nt_bytes[4];
    uint8_t  nt_par[4];
    uint8_t  tx[8];
    uint16_t txbits;
    uint8_t  rxbuf[16];
    uint16_t rxbits = 0U;
    uint8_t  bytes[8];
    ReturnCode err;
    rfalTransceiveContext ctx;
    mfc_auth_ctx_t cap;
    uint32_t nt;

    if ((rx == NULL) || (rxLenBits == 0U)) {
        return false;
    }
    /* test04 diagnostic: ANY genuine inbound frame == confirmed reader traffic.
     * Runs in the listener worker (task) context, one log per frame -- never
     * from an IRQ; state flag only, no draw/LED here. */
    platformLog("[MFC-DR] RX frame bits=%u cmd=%02X\r\n",
                (unsigned)rxLenBits, (unsigned)rx[0]);
    if ((s_state == MFC_DR_WAIT_READER) || (s_state == MFC_DR_IDLE)) {
        s_state = MFC_DR_READER_ACTIVE;     /* -> "Reader traffic" */
    }
    /* Bounded wait for a partial pair's second attempt -- checked on every
     * genuine inbound frame (this persona has no periodic RTOS timer of
     * its own). A partial record that has waited longer than this can
     * never be legitimately completed by a later, unrelated reader
     * approach; invalidating it here (generation bump only, no data
     * touched -- see mfc_capture_check_timeout()) is a no-op when nothing
     * is actually stale. */
    (void)mfc_capture_check_timeout(HAL_GetTick(), MFC_DR_PAIR_TIMEOUT_MS);
    if (rxLenBits < 16U) {
        return true;                        /* traffic noted; too short for AUTH */
    }
    cmd = rx[0];
    if ((cmd != MFC_DR_AUTH_KEY_A) && (cmd != MFC_DR_AUTH_KEY_B)) {
        return true;                        /* traffic noted; not an AUTH frame */
    }
    key_is_b = (cmd == MFC_DR_AUTH_KEY_B) ? 1U : 0U;
    block    = rx[1];
    if (block >= 128U) {                    /* 1K/4K block range guard */
        platformLog("[MFC-DR] bad AUTH block=%u\r\n", (unsigned)block);
        return true;                        /* consumed but rejected */
    }

    /* Diagnostic: a genuine MIFARE AUTH was received. State flag ONLY here --
     * no draw/LED/blocking in the RF path; the UI task renders it. */
    if (s_state != MFC_DR_DONE) { s_state = MFC_DR_AUTH_ACTIVE; }

    platformLog("[MFC-DR] AUTH %c block=%u sector=%u\r\n",
                key_is_b ? 'B' : 'A', (unsigned)block, (unsigned)(block >> 2));

    /* Generate Nt (big-endian byte order on the wire) and its ISO14443A parity. */
    nt = next_nt();
    nt_bytes[0] = (uint8_t)(nt >> 24); nt_bytes[1] = (uint8_t)(nt >> 16);
    nt_bytes[2] = (uint8_t)(nt >> 8);  nt_bytes[3] = (uint8_t)(nt);
    for (uint8_t i = 0U; i < 4U; i++) { nt_par[i] = odd_parity(nt_bytes[i]); }
    txbits = pack_bits(tx, nt_bytes, nt_par, 4U);

    /* Single raw transceive: send Nt (manual parity, no CRC), then receive the
     * reader's encrypted {Nr}{Ar} keeping raw parity/no-CRC. This is the exact
     * primitive nfc_poller.c uses as initiator, driven here on the target. */
    ctx.txBuf     = tx;
    ctx.txBufLen  = txbits;          /* bits */
    ctx.rxBuf     = rxbuf;
    ctx.rxBufLen  = (uint16_t)(sizeof(rxbuf) * 8U);
    ctx.rxRcvdLen = &rxbits;
    ctx.flags     = MFC_DR_RAW_FLAGS;
    ctx.fwt       = MFC_DR_FWT;

    err = rfalStartTransceive(&ctx);
    if (err == RFAL_ERR_NONE) {
        rfalRunBlocking(err, rfalGetTransceiveStatus());
    }
    if (err != RFAL_ERR_NONE) {
        platformLog("[MFC-DR] Nr/Ar rx err=%d\r\n", (int)err);
        return true;                        /* consumed; incomplete -> not stored */
    }

    /* Expect 8 data bytes (Nr[4] + Ar[4]); each carries a parity bit we drop. */
    if (unpack_bits(rxbuf, rxbits, bytes, 8U) < 8U) {
        platformLog("[MFC-DR] short Nr/Ar (%u bits)\r\n", (unsigned)rxbits);
        return true;
    }

    cap.cuid     = s_cuid;
    cap.block    = block;
    cap.sector   = (uint8_t)(block >> 2);   /* 4 blocks/sector (1K) */
    cap.key_type = key_is_b ? MFC_KEY_B : MFC_KEY_A;
    cap.nt       = nt;
    cap.nr       = be32(&bytes[0]);
    cap.ar       = be32(&bytes[4]);
    cap.now_ms   = HAL_GetTick();

    platformLog("[MFC-DR] CUID=%08lX\r\n", (unsigned long)cap.cuid);
    platformLog("[MFC-DR] NT=%08lX NR=%08lX AR=%08lX\r\n",
                (unsigned long)cap.nt, (unsigned long)cap.nr, (unsigned long)cap.ar);

    switch (mfc_capture_add(&cap)) {
        case MFC_CAP_COMPLETED_PAIR:
            platformLog("[MFC-DR] pair complete sector=%u key=%c pairs=%lu\r\n",
                        (unsigned)cap.sector, key_is_b ? 'B' : 'A',
                        (unsigned long)mfc_capture_pair_count());
            /* fall through to the shared count log */
        case MFC_CAP_ADDED_PARTIAL:
            platformLog("[MFC-DR] capture %lu/%u\r\n",
                        (unsigned long)mfc_capture_nonce_count(),
                        (unsigned)MFC_CAPTURE_TARGET_NONCES);
            if (mfc_capture_target_reached()) {
                s_state = MFC_DR_DONE;
                platformLog("[MFC-DR] target reached\r\n");
            }
            break;
        case MFC_CAP_DUPLICATE:
            platformLog("[MFC-DR] duplicate ignored\r\n");
            break;
        case MFC_CAP_FULL:
            platformLog("[MFC-DR] store full\r\n");
            break;
        case MFC_CAP_INVALID:
        default:
            platformLog("[MFC-DR] malformed capture rejected\r\n");
            break;
    }
    return true;
}
