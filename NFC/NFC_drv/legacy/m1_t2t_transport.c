/* See COPYING.txt for license details. */
/*
 * m1_t2t_transport.c - see m1_t2t_transport.h for the full design rationale.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>
#include "m1_t2t_transport.h"
#include "rfal_rf.h"
#include "rfal_nfca.h"
#include "logger.h"

typedef enum {
    M1_T2T_TP_IDLE = 0,          /* not started */
    M1_T2T_TP_WAIT_ACTIVATION,   /* rfalListenStart() armed; waiting for the
                                   * reader to activate AND send its first
                                   * command -- checked together, one call */
    M1_T2T_TP_WAIT_NEXT_FRAME,   /* a real, directly-owned rfalStartTransceive()
                                   * RX-only operation is in flight */
} m1_t2t_tp_state_t;

#define M1_T2T_TP_RXBUF_LEN 32U   /* largest T2T command (COMPATIBILITY_WRITE,
                                    * cmd+page+16 data = 18B) with margin;
                                    * CRC is stripped before reaching this
                                    * buffer (see rfalListenStart() and the
                                    * HLTA-length proof below) */

static m1_t2t_dispatch_fn  s_dispatch_cb = NULL;
static bool                s_active      = false;
static m1_t2t_tp_state_t   s_state       = M1_T2T_TP_IDLE;
static rfalLmConfPA        s_lm_conf;
static uint8_t             s_rx_buf[M1_T2T_TP_RXBUF_LEN];
static uint16_t            s_rx_len_bits;

/* Reactivation watchdog -- bounds how long WAIT_ACTIVATION may wait for a
 * post-HLTA WUPA reactivation before the underlying listen-mode hardware
 * state machine is treated as stalled and a full restart (the same
 * already-proven m1_t2t_transport_restart_listen() RFAL_ERR_LINK_LOSS
 * already uses) is forced.
 *
 * Both the mechanism and the value are taken directly from RFAL's own
 * equivalent safety net, not invented. rfal_nfc.c's high-level worker
 * (used in production here for T3T/T4T card emulation) creates exactly one
 * discovery-wide timer at session start -- gNfcDev.discTmr =
 * platformTimerCreate(gNfcDev.disc.totalDuration) (rfal_nfc.c:417-418) --
 * and, while waiting for a Type-A tag to (re)activate out of listen mode
 * (RFAL_NFC_STATE_LISTEN_COLAVOIDANCE/LISTEN_ACTIVATION/LISTEN_SLEEP),
 * treats that timer's expiry as a stalled listen requiring
 * rfalListenStop() and a full restart of discovery (rfal_nfc.c:608-613,
 * 639-648 -- confirmed by direct source read: any LISTEN_SLEEP status
 * other than NONE/BUSY falls straight to the restart branch, since the
 * "tolerate and keep waiting" carve-out at line 639 only applies to
 * RFAL_NFC_STATE_LISTEN_ACTIVATION, never to LISTEN_SLEEP).
 *
 * The totalDuration this exact codebase already configures for every
 * card-emulation (listen) discovery session is 60000 ms
 * (nfc_listener.c: discParam.totalDuration = 60000U; /- CE waits longer -/).
 * This reuses that already-configured value verbatim, and the same
 * platformTimerCreate()/platformTimerIsExpired() primitives RFAL itself
 * uses for discTmr (transitively available here via rfal_nfca.h's own
 * #include "rfal_platform.h") -- not a new number, not a new mechanism. */
#define M1_T2T_REACTIVATION_TIMEOUT_MS 60000U

static uint32_t s_reactivation_tmr       = 0U;
static bool     s_reactivation_tmr_armed = false;

/* Arm/rearm the reactivation watchdog. Called from every site that
 * transitions into M1_T2T_TP_WAIT_ACTIVATION (start(), the HLTA sleep
 * path, and the LINK_LOSS/timeout full-restart path) so a fresh session
 * always gets a fresh bound, and a just-completed restart cannot itself
 * be immediately re-flagged as stalled -- the same restart storm guard
 * RFAL's own discTmr gets for free by only being recreated at discovery
 * start (rfal_nfc.c:417-418), never mid-session. */
static void m1_t2t_transport_arm_reactivation_timer(void)
{
    s_reactivation_tmr       = platformTimerCreate(M1_T2T_REACTIVATION_TIMEOUT_MS);
    s_reactivation_tmr_armed = true;
}

static void m1_t2t_transport_go_idle_and_await_reactivation(void)
{
    /* HLTA ONLY (0x50 0x00): sleep this tag (WUPA-only reactivation from
     * here, matching real ISO14443-3A HLTA semantics), then return to
     * WAIT_ACTIVATION so the next tap is detected the exact same unified
     * way as the very first one.
     *
     * This is NOT a general field-loss recovery path -- confirmed against
     * RFAL's own source (Drivers/BSP/Components/ST25R3916/rfal_rfst25r3916.c):
     * every production call site of rfalListenSleepStart() (rfal_nfc.c:871,
     * 2090, 2127) fires only on RFAL_ERR_SLEEP_REQ, i.e. a genuine HLTA. On
     * a real EOF (external-field-off) event, RFAL's own listen-mode IRQ
     * handler (rfal_rfst25r3916.c:~3793) does NOT call this function at
     * all -- it calls rfalListenSetState(RFAL_LM_STATE_POWER_OFF) instead,
     * a distinct state rfal_nfc.c's own status helper maps back to
     * RFAL_ERR_LINK_LOSS, never to RFAL_ERR_BUSY the way SLEEP_A/SLEEP_AF
     * are (rfal_nfc.c:2280-2286) -- i.e. genuinely not a "still
     * recoverable, waiting for WUPA" condition the way sleep is.
     * rfalListenSleepStart() itself even guards against being called when
     * the field already reads off: if so, it calls rfalListenStop()
     * internally and returns RFAL_ERR_LINK_LOSS without ever reaching
     * SLEEP_A (rfal_rfst25r3916.c:4045-4053) -- a return value a caller
     * MUST check (every RFAL-internal call site wraps it in
     * RFAL_EXIT_ON_ERR). See m1_t2t_transport_restart_listen() for the
     * LINK_LOSS-specific recovery this function must never be used for. */
    s_rx_len_bits = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    (void)rfalListenSleepStart(RFAL_LM_STATE_SLEEP_A, s_rx_buf,
                               rfalConvBytesToBits((uint16_t)sizeof(s_rx_buf)),
                               &s_rx_len_bits);
    s_state = M1_T2T_TP_WAIT_ACTIVATION;
    m1_t2t_transport_arm_reactivation_timer();
}

static void m1_t2t_transport_restart_listen(void)
{
    /* RFAL_ERR_LINK_LOSS recovery: a real "the field reads off" condition,
     * not a graceful protocol sleep -- see the header comment on
     * m1_t2t_transport_go_idle_and_await_reactivation() for the full
     * source-audit trail. rfal_nfc.c's own equivalent recovery is a
     * fresh rfalListenStart() (it never calls rfalListenSleepStart() for
     * this case). s_lm_conf already holds the exact identity
     * (UID/ATQA/SAK) passed to m1_t2t_transport_start() and is never
     * mutated afterward, so reusing it here re-arms with the same
     * emulated identity and the same armed image (the saved-card image
     * itself is owned by the dispatch callback, entirely untouched by
     * this transport-level restart). rfalListenStop() is the same,
     * already-hardware-proven primitive m1_t2t_transport_stop() uses;
     * rfalListenStart() clears gRFAL.Lm.dataFlag/rxLen itself
     * (rfal_rfst25r3916.c, confirmed by source read), so no stale
     * dataFlag/status from the terminated session can leak into the new
     * one -- s_rx_buf/s_rx_len_bits are zeroed here as well, matching the
     * exact pattern m1_t2t_transport_start() already uses. */
    (void)rfalListenStop();

    s_rx_len_bits = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));

    ReturnCode err = rfalListenStart(RFAL_LM_MASK_NFCA, &s_lm_conf, NULL, NULL,
                                     s_rx_buf, rfalConvBytesToBits((uint16_t)sizeof(s_rx_buf)),
                                     &s_rx_len_bits);
    if (err != RFAL_ERR_NONE) {
        platformLog("[T2T-TP] restart rfalListenStart err=%d\r\n", err);
    }
    s_state = M1_T2T_TP_WAIT_ACTIVATION;
    m1_t2t_transport_arm_reactivation_timer();
}

bool m1_t2t_transport_rearm_rx(void)
{
    if (!s_active) { return false; }

    rfalTransceiveContext ctx;
    s_rx_len_bits = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));

    ctx.txBuf     = NULL;
    ctx.txBufLen  = 0U;
    ctx.rxBuf     = s_rx_buf;
    ctx.rxBufLen  = rfalConvBytesToBits((uint16_t)sizeof(s_rx_buf));
    ctx.rxRcvdLen = &s_rx_len_bits;
    ctx.flags     = RFAL_TXRX_FLAGS_DEFAULT;
    ctx.fwt       = RFAL_FWT_NONE;

    ReturnCode err = rfalStartTransceive(&ctx);
    if (err != RFAL_ERR_NONE) {
        platformLog("[T2T-TP] rearm rfalStartTransceive err=%d\r\n", err);
        /* Don't strand the session: fall back to re-detecting activation
         * the same way a fresh tap would be detected. */
        s_state = M1_T2T_TP_WAIT_ACTIVATION;
        return false;
    }
    s_state = M1_T2T_TP_WAIT_NEXT_FRAME;
    return true;
}

/* Validate, HALT-check, and dispatch one already-received frame; on a
 * not-handled result, rearm ourselves (a handled command has already
 * rearmed internally via CeRearmRxAfterTx() -> m1_t2t_transport_rearm_rx()). */
static void m1_t2t_transport_service_frame(void)
{
    if (s_rx_len_bits < 8U) {
        /* Malformed/too-short: drop, never fabricated into a command. */
        (void)m1_t2t_transport_rearm_rx();
        return;
    }

    uint16_t rxBytes = rfalConvBitsToBytes(s_rx_len_bits);

    /* HLTA, using RFAL's own real primitive against the actual proven CRC
     * contract: rfalNfcaListenerIsSleepReq() requires bufLen ==
     * sizeof(rfalNfcaSlpReq) == 2 (see rfal_nfca.c) -- CRC is already
     * stripped by the time a frame reaches an rfalListenStart()-owned
     * buffer (confirmed: rfal_nfc.c itself calls this exact function
     * against gNfcDev.rxBuf.rfBuf/gNfcDev.rxLen, the same kind of buffer
     * this module owns, with the same 2-byte expectation) -- never a
     * hardcoded 4-byte comparison. */
    if (rfalNfcaListenerIsSleepReq(s_rx_buf, rxBytes)) {
        m1_t2t_transport_go_idle_and_await_reactivation();
        return;
    }

    bool handled = (s_dispatch_cb != NULL) && s_dispatch_cb(s_rx_buf, s_rx_len_bits);
    if (!handled) {
        (void)m1_t2t_transport_rearm_rx();
    }
}

void m1_t2t_transport_set_dispatch(m1_t2t_dispatch_fn cb)
{
    s_dispatch_cb = cb;
}

bool m1_t2t_transport_start(const uint8_t *uid, uint8_t uidLen,
                            const uint8_t atqa[2], uint8_t sak)
{
    if (s_active) { return false; }   /* caller must stop() first */
    if (s_dispatch_cb == NULL) { return false; }   /* never arm with nowhere to send frames */
    if ((uid == NULL) || (atqa == NULL) || ((uidLen != 4U) && (uidLen != 7U))) {
        return false;
    }

    memset(&s_lm_conf, 0, sizeof(s_lm_conf));
    s_lm_conf.nfcidLen = (uidLen == 7U) ? RFAL_LM_NFCID_LEN_07 : RFAL_LM_NFCID_LEN_04;
    memcpy(s_lm_conf.nfcid, uid, uidLen);
    s_lm_conf.SENS_RES[0] = atqa[0];
    s_lm_conf.SENS_RES[1] = atqa[1];
    s_lm_conf.SEL_RES     = sak;

    s_rx_len_bits = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));

    ReturnCode err = rfalListenStart(RFAL_LM_MASK_NFCA, &s_lm_conf, NULL, NULL,
                                     s_rx_buf, rfalConvBytesToBits((uint16_t)sizeof(s_rx_buf)),
                                     &s_rx_len_bits);
    if (err != RFAL_ERR_NONE) {
        platformLog("[T2T-TP] rfalListenStart err=%d\r\n", err);
        return false;
    }

    s_state  = M1_T2T_TP_WAIT_ACTIVATION;
    s_active = true;
    m1_t2t_transport_arm_reactivation_timer();
    return true;
}

void m1_t2t_transport_stop(void)
{
    if (!s_active) { return; }   /* idempotent */
    (void)rfalListenStop();
    s_active      = false;
    s_state       = M1_T2T_TP_IDLE;
    s_rx_len_bits = 0;
    /* Disarm so no stale expiry can carry into the next start(): every
     * WAIT_ACTIVATION entry rearms fresh anyway, but this keeps stop() a
     * complete, self-contained reset with no watchdog state left live. */
    s_reactivation_tmr_armed = false;
}

bool m1_t2t_transport_is_active(void)
{
    return s_active;
}

void m1_t2t_transport_tick(void)
{
    if (!s_active) { return; }

    rfalWorker();

    switch (s_state) {
        case M1_T2T_TP_WAIT_ACTIVATION: {
            /* Reactivation watchdog: see the header comment on
             * m1_t2t_transport_arm_reactivation_timer() for the full
             * rfal_nfc.c discTmr audit trail. This covers both a stalled
             * listen-mode IRQ chain with the field still on (SLEEP_A/
             * READY_Ax never reaching ACTIVE_A/ACTIVE_Ax) and the field
             * genuinely gone with no EOF ever observed (stuck at
             * POWER_OFF) -- either way, checked before the state read
             * below so it fires regardless of what lmSt currently is. */
            if (s_reactivation_tmr_armed && platformTimerIsExpired(s_reactivation_tmr)) {
                m1_t2t_transport_restart_listen();
                return;
            }

            bool dataFlag = false;
            rfalLmState lmSt = rfalListenGetState(&dataFlag, NULL);

            if ((lmSt != RFAL_LM_STATE_ACTIVE_A) && (lmSt != RFAL_LM_STATE_ACTIVE_Ax)) {
                return;   /* reader still approaching -- nothing to do this tick */
            }
            if (!dataFlag) {
                return;   /* activated, first command not sent yet */
            }

            /* Valid activation completed within the bound: cancel the
             * watchdog before servicing the frame (which may itself HLTA
             * right back into WAIT_ACTIVATION and rearm a fresh one). */
            s_reactivation_tmr_armed = false;

            /* Unified activation+first-frame detection (see header comment):
             * the frame is already fully present in s_rx_buf/s_rx_len_bits,
             * populated directly by rfalListenStart()'s own hardware
             * receive during activation -- no separate status call whose
             * verdict can disagree with what's actually in the buffer. */
            m1_t2t_transport_service_frame();
            return;
        }

        case M1_T2T_TP_WAIT_NEXT_FRAME: {
            ReturnCode err = rfalGetTransceiveStatus();
            if (err == RFAL_ERR_BUSY) { return; }   /* still waiting, normal */

            if (err == RFAL_ERR_LINK_LOSS) {
                /* The field itself is reported gone -- a full restart, not
                 * a sleep. See m1_t2t_transport_restart_listen()'s header
                 * comment for the source-audit trail. */
                m1_t2t_transport_restart_listen();
                return;
            }

            if (err != RFAL_ERR_NONE) {
                /* Any other status (CRC/parity/framing error, RF
                 * collision, incomplete byte, timeout): a malformed or
                 * absent reception, not a field-loss condition -- the
                 * field itself was never reported gone. RFAL's own
                 * listen-mode IRQ handler treats a malformed received
                 * frame this way too (rfal_rfst25r3916.c's RXE handler:
                 * on a CRC/parity/ERR1 error while ACTIVE_A, it goes back
                 * to IDLE and keeps listening in the SAME session -- it
                 * never sleeps or restarts for this). The equivalent
                 * here is to keep this session's activation and simply
                 * listen again for the next frame -- neither HLTA-style
                 * sleep (that's for a genuine 0x50 0x00) nor a full
                 * restart (that's LINK_LOSS-specific). */
                (void)m1_t2t_transport_rearm_rx();
                return;
            }

            m1_t2t_transport_service_frame();
            return;
        }

        case M1_T2T_TP_IDLE:
        default:
            return;
    }
}
