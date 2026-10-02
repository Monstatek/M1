/* Host test: drives the REAL, unmodified m1_t2t_transport.c against a
 * programmable mock of the low-level RFAL listen-mode seam
 * (rfalListenStart/Stop/GetState/SleepStart, rfalStartTransceive/
 * rfalGetTransceiveStatus/rfalWorker) -- NOT a source-text transcription of
 * this module (unlike the prior T4/T5 investigation tests, which
 * necessarily transcribed rfal_nfc.c/rfal_rfst25r3916.c since those files
 * cannot be host-compiled at all). m1_t2t_transport.c itself is compiled
 * as-is here; only the RFAL primitives beneath it are mocked, via a
 * structurally-faithful stub (rfal_rf.h/rfal_nfca.h in stub_t2t_transport/,
 * same rationale as every other stub directory in this project: the real
 * headers pull the full STM32/ST25R3916 HAL stack).
 *
 * The dispatch callback (which stands in for CeHandleT2TCmdRx(), itself
 * unlinkable for the same HAL reason) is a test double that mirrors the
 * REAL function's contract exactly: given a raw frame, "transmit" a
 * response via a direct rfalStartTransceive() call (recorded by the mock)
 * and rearm on success via m1_t2t_transport_rearm_rx() -- exactly what
 * CeHandleT2TCmdRx() really does (build response -> rfalTransceiveBlockingTx()
 * / CeSendShortFrame() -> CeRearmRxAfterTx() -> m1_t2t_transport_rearm_rx()).
 * This test therefore verifies the TRANSPORT's state machine (activation,
 * one-time first-frame consumption, rearm, HALT, field-loss, session
 * restart, ownership) end-to-end; the command engine's own byte-level
 * correctness per variant (image geometry/locking/GET_VERSION content) is
 * separately and thoroughly verified by t2t_emu_image_test.c.
 *
 *   cp NFC/NFC_drv/legacy/m1_t2t_transport.c NFC/NFC_drv/legacy/m1_t2t_transport.h \
 *      NFC/NFC_drv/common/test/stub_t2t_transport/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common/test/stub_t2t_transport \
 *      NFC/NFC_drv/common/test/stub_t2t_transport/m1_t2t_transport.c \
 *      NFC/NFC_drv/common/test/stub_t2t_transport/rfal_mock.c \
 *      NFC/NFC_drv/common/test/m1_t2t_transport_test.c -o /tmp/t2ttransport && /tmp/t2ttransport
 *   rm NFC/NFC_drv/common/test/stub_t2t_transport/m1_t2t_transport.c NFC/NFC_drv/common/test/stub_t2t_transport/m1_t2t_transport.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_t2t_transport.h"
#include "rfal_mock.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* ---- fake dispatch: mirrors CeHandleT2TCmdRx()'s contract exactly ----- */
static int      s_dispatch_calls = 0;
static uint8_t  s_last_dispatch_rx[64];
static uint16_t s_last_dispatch_rxbits = 0;
static bool     s_dispatch_should_handle = true;
static uint16_t s_dispatch_response_len_bytes = 16U;

static void reset_fake_dispatch(void)
{
    s_dispatch_calls = 0;
    memset(s_last_dispatch_rx, 0, sizeof(s_last_dispatch_rx));
    s_last_dispatch_rxbits = 0;
    s_dispatch_should_handle = true;
    s_dispatch_response_len_bytes = 16U;
}

static bool fake_dispatch(const uint8_t *rx, uint16_t rxBits)
{
    s_dispatch_calls++;
    s_last_dispatch_rxbits = rxBits;
    uint16_t rxBytes = rfalConvBitsToBytes(rxBits);
    if (rxBytes > sizeof(s_last_dispatch_rx)) { rxBytes = (uint16_t)sizeof(s_last_dispatch_rx); }
    memcpy(s_last_dispatch_rx, rx, rxBytes);

    if (!s_dispatch_should_handle) {
        return false;   /* "not handled" -- transport must rearm itself */
    }

    static uint8_t resp[32];   /* largest real response is READ_SIGNATURE's 32 bytes */
    for (uint16_t i = 0; i < s_dispatch_response_len_bytes; i++) { resp[i] = (uint8_t)(0xA0U + i); }
    rfalTransceiveContext ctx;
    ctx.txBuf     = resp;
    ctx.txBufLen  = rfalConvBytesToBits(s_dispatch_response_len_bytes);
    ctx.rxBuf     = NULL;
    ctx.rxBufLen  = 0U;
    ctx.rxRcvdLen = NULL;
    ctx.flags     = RFAL_TXRX_FLAGS_DEFAULT;
    ctx.fwt       = RFAL_FWT_NONE;
    (void)rfalStartTransceive(&ctx);        /* the "response TX", recorded by the mock */
    (void)m1_t2t_transport_rearm_rx();      /* the real contract: handled -> caller rearms */
    return true;
}

static void full_reset(void)
{
    mock_reset();
    reset_fake_dispatch();
    m1_t2t_transport_stop();   /* idempotent -- ensures no state leaks between tests */
    m1_t2t_transport_set_dispatch(fake_dispatch);
}

/* Saved-card identities for the four launch variants (7-byte UID + ATQA
 * 0x4400 + SAK 0x00, matching every T2T-family card regardless of variant --
 * the transport itself is variant-agnostic; what differs is only the exact
 * UID bytes each represents). */
typedef struct { const char *name; uint8_t uid[7]; } variant_id_t;
static const variant_id_t VARIANTS[] = {
    { "UL11",    { 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 } },
    { "NTAG213", { 0x04, 0x21, 0x22, 0x33, 0x44, 0x55, 0x66 } },
    { "NTAG215", { 0x04, 0x31, 0x22, 0x33, 0x44, 0x55, 0x66 } },
    { "NTAG216", { 0x04, 0x41, 0x22, 0x33, 0x44, 0x55, 0x66 } },
};
static const uint8_t ATQA[2] = { 0x44U, 0x00U };
static const uint8_t SAK     = 0x00U;

/* ---- full scripted session, run once per variant ---------------------- */
static void test_full_session(const variant_id_t *v)
{
    full_reset();

    /* 1) start */
    CHECK(m1_t2t_transport_start(v->uid, 7U, ATQA, SAK), "start: succeeds");
    CHECK(m1_t2t_transport_is_active(), "start: transport reports active");
    CHECK(mock_listen_start_count() == 1, "start: rfalListenStart() called exactly once");

    /* 2) activation with correct identity */
    CHECK(mock_last_listen_start_uid_len() == 7U, "activation: 7-byte UID passed to rfalListenStart()");
    CHECK(memcmp(mock_last_listen_start_uid(), v->uid, 7U) == 0, "activation: exact UID bytes match the saved card");
    CHECK(memcmp(mock_last_listen_start_atqa(), ATQA, 2U) == 0, "activation: ATQA 0x4400 (Type 2, ISO-DEP disabled)");
    CHECK(mock_last_listen_start_sak() == SAK, "activation: SAK 0x00");

    /* Reader activates and sends READ 30 00 -- delivered as ONE unified
     * activation+first-frame event (dataFlag=true the moment ACTIVE_A is
     * reported), preserving the combined event word. */
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    CHECK(mock_inject_rx(read_p0, sizeof(read_p0)), "first frame: injected into the buffer rfalListenStart() armed");

    /* 3) buffered READ 30 00, dispatched exactly once */
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "READ 30 00: dispatched exactly once");
    CHECK(s_last_dispatch_rxbits == 16U, "READ 30 00: exact 2-byte (16-bit) command delivered, not padded/truncated");
    CHECK(memcmp(s_last_dispatch_rx, read_p0, 2U) == 0, "READ 30 00: exact command bytes delivered");

    /* 4) exact 16-byte response */
    CHECK(mock_tx_count() == 1, "READ 30 00: exactly one response transmitted");
    CHECK(mock_last_tx_len_bits() == 128U, "READ 30 00: response is exactly 16 bytes (128 bits)");

    /* 5) RX rearm */
    CHECK(mock_rearm_count() == 1, "READ 30 00: RX rearmed exactly once after the response");

    /* 6) GET_VERSION */
    mock_push_transceive_status(RFAL_ERR_BUSY);   /* still waiting a tick */
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "GET_VERSION: not yet dispatched while genuinely still busy");

    uint8_t get_version[1] = { 0x60U };
    mock_push_transceive_status(RFAL_ERR_NONE);
    CHECK(mock_inject_rx(get_version, sizeof(get_version)), "GET_VERSION: injected into the rearmed buffer");
    s_dispatch_response_len_bytes = 8U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 2, "GET_VERSION: dispatched exactly once");
    CHECK(s_last_dispatch_rxbits == 8U, "GET_VERSION: exact 1-byte command delivered");

    /* 7) correct eight-byte response */
    CHECK(mock_tx_count() == 2, "GET_VERSION: exactly one more response transmitted");
    CHECK(mock_last_tx_len_bits() == 64U, "GET_VERSION: response is exactly 8 bytes (64 bits)");

    /* 8) RX rearm */
    CHECK(mock_rearm_count() == 2, "GET_VERSION: RX rearmed again");

    /* 9) READ_SIGNATURE (0x3C) -- per the T7 protocol audit, this is the
     * first command the read-scene poller sends after GET_VERSION
     * (mf_ultralight_poller.c handler_get_feature_set -> handler_read_
     * signature), and a fatal one: an unanswered/timed-out READ_SIGNATURE is
     * exactly the bug that produced T6's indefinite "Don't move" hang. This
     * step proves the transport carries the command through to dispatch and
     * back to a real 32-byte TX without ever falling into a BUSY-forever
     * stall -- the same single-tick pattern already proven for GET_VERSION. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t read_sig[2] = { 0x3CU, 0x00U };
    CHECK(mock_inject_rx(read_sig, sizeof(read_sig)), "READ_SIGNATURE: injected");
    s_dispatch_response_len_bytes = 32U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 3, "READ_SIGNATURE: dispatched exactly once, no retry loop");
    CHECK(s_last_dispatch_rxbits == 16U, "READ_SIGNATURE: exact 2-byte (0x3C 0x00) command delivered");
    CHECK(mock_tx_count() == 3, "READ_SIGNATURE: responded");
    CHECK(mock_last_tx_len_bits() == 256U, "READ_SIGNATURE: genuine 32-byte (256-bit) signature response");
    CHECK(mock_rearm_count() == 3, "READ_SIGNATURE: rearmed -- session proceeds, never stalls here");

    /* 10) page reads (ReadPages, per the audit: one READ per page,
     * pages_total commands for a full read) -- one representative page here;
     * exhaustive per-page coverage is unit-tested for the byte content in
     * t2t_emu_image_test.c, not the transport's job. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t read_p4[2] = { 0x30U, 0x04U };
    CHECK(mock_inject_rx(read_p4, sizeof(read_p4)), "page READ: injected");
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 4, "page READ: dispatched");
    CHECK(mock_tx_count() == 4, "page READ: responded");
    CHECK(mock_rearm_count() == 4, "page READ: rearmed");

    /* 11) READ_CNT (0x39) index 2 -- the exact index/parameter byte the
     * NTAG21x SingleCounter path (and UL11's final counter slot) requests.
     * Answered with a genuine 3-byte value here; the NAK-when-uncaptured
     * path is the same TX-then-rearm shape (content, not transport
     * mechanics, differs) and is covered by nfc_listener.c's source-bound
     * checks below, since CeHandleT2TCmdRx() itself cannot be host-linked. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t read_cnt[2] = { 0x39U, 0x02U };
    CHECK(mock_inject_rx(read_cnt, sizeof(read_cnt)), "READ_CNT: injected");
    s_dispatch_response_len_bytes = 3U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 5, "READ_CNT: dispatched exactly once, no retry loop");
    CHECK(mock_last_tx_len_bits() == 24U, "READ_CNT: genuine 3-byte counter response");
    CHECK(mock_rearm_count() == 5, "READ_CNT: rearmed -- session proceeds regardless of counter outcome");

    /* 12) CHECK_TEARING_EVENT (0x3E) index 2 -- NTAG213/215/216 genuinely
     * NAK this (no tearing-flag support); the poller explicitly
     * tolerates that NAK and proceeds. The transport must carry the NAK
     * response through exactly like any other, never dropping into a
     * timeout the way T6's unhandled-default-case bug did. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t check_tearing[2] = { 0x3EU, 0x02U };
    CHECK(mock_inject_rx(check_tearing, sizeof(check_tearing)), "CHECK_TEARING: injected");
    s_dispatch_response_len_bytes = 1U;   /* NAK short frame -- content is CeSendShortFrame()'s concern */
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 6, "CHECK_TEARING: dispatched exactly once, no retry loop");
    CHECK(mock_tx_count() == 6, "CHECK_TEARING: answered (NAK is still a real, timely response, never silence)");
    CHECK(mock_rearm_count() == 6, "CHECK_TEARING: rearmed -- session proceeds after the NAK");

    /* 13) PWD_AUTH (0x1B) -- optimistic default-password guess
     * (0xFFFFFFFF) once ReadPages/counters/tearing complete. Tolerated
     * either way per the audit (handler_auth/handler_try_default_pass
     * proceed regardless); this proves the transport carries it cleanly. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t pwd_auth[5] = { 0x1BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
    CHECK(mock_inject_rx(pwd_auth, sizeof(pwd_auth)), "PWD_AUTH: injected");
    s_dispatch_response_len_bytes = 2U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 7, "PWD_AUTH: dispatched exactly once, no retry loop");
    CHECK(s_last_dispatch_rxbits == 40U, "PWD_AUTH: exact 5-byte (cmd+4-byte password) command delivered");
    CHECK(mock_rearm_count() == 7, "PWD_AUTH: rearmed -- session reaches completion regardless of match/NAK");

    /* 14) WRITE with correct ACK/NAK behavior -- both outcomes go through
     * the SAME transport path; the ACK-vs-NAK byte content itself is
     * CeHandleT2TCmdRx()'s/CeSendShortFrame()'s responsibility (already
     * verified elsewhere), so this proves the transport carries a WRITE
     * command and a short (4-bit) response correctly either way. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t write_cmd[6] = { 0xA2U, 0x04U, 0x11U, 0x22U, 0x33U, 0x44U };
    CHECK(mock_inject_rx(write_cmd, sizeof(write_cmd)), "WRITE: injected");
    s_dispatch_response_len_bytes = 16U;   /* fake dispatch always "responds"; real one sends a 4-bit ACK/NAK, already covered by ce_dev_type_test.c/production */
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 8, "WRITE: dispatched");
    CHECK(s_last_dispatch_rxbits == 48U, "WRITE: exact 6-byte command delivered");
    CHECK(mock_rearm_count() == 8, "WRITE: rearmed after ACK/NAK");

    /* 15) HALT -- the read-success path always sends
     * an explicit HLTA (0x50 0x00) once it reaches ReadSuccess, confirming
     * the whole transcript above completes without ever stalling. */
    mock_push_transceive_status(RFAL_ERR_NONE);
    uint8_t halt[2] = { 0x50U, 0x00U };
    CHECK(mock_inject_rx(halt, sizeof(halt)), "HALT: injected");
    int calls_before_halt = s_dispatch_calls;
    int sleep_before = mock_listen_sleep_count();
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == calls_before_halt, "HALT: never dispatched to the command engine as a normal command");
    CHECK(mock_listen_sleep_count() == sleep_before + 1, "HALT: sleeps the tag (rfalListenSleepStart) -- session terminates successfully, WUPA-only reactivation");

    /* Reactivation after HALT (WUPA): same unified activation+first-frame
     * detection as the very first tap. */
    uint8_t read_again[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_Ax, true);   /* Ax: reactivation-from-sleep variant */
    CHECK(mock_inject_rx(read_again, sizeof(read_again)), "reactivation: injected");
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == calls_before_halt + 1, "reactivation: WUPA-style re-tap is serviced normally");

    /* 12) cleanup */
    m1_t2t_transport_stop();
    CHECK(!m1_t2t_transport_is_active(), "stop: transport reports inactive");
    CHECK(mock_listen_stop_count() == 1, "stop: rfalListenStop() called exactly once");

    /* 13) second fresh session without reboot */
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);   /* reset sticky state for the new session */
    CHECK(m1_t2t_transport_start(v->uid, 7U, ATQA, SAK), "second session: starts cleanly, no reboot needed");
    CHECK(mock_listen_start_count() == 2, "second session: rfalListenStart() called again (fresh)");
    m1_t2t_transport_stop();
}

static void test_all_variants(void)
{
    for (size_t i = 0; i < sizeof(VARIANTS) / sizeof(VARIANTS[0]); i++) {
        test_full_session(&VARIANTS[i]);
    }
}

/* ---- edge cases -------------------------------------------------------- */

static void test_zero_length_activation_frame(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "zero-len: start ok");

    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    /* dataFlag true but nothing actually injected -- rxLen stays 0 (the
     * mock's own rfalListenStart() sets *rxLen=0 and nothing wrote it). */
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 0, "zero-len activation frame: never dispatched (validated before dispatch)");
    CHECK(mock_rearm_count() == 1, "zero-len activation frame: rearmed instead of wedging");
    m1_t2t_transport_stop();
}

static void test_malformed_command(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "malformed: start ok");

    s_dispatch_should_handle = false;   /* simulate CeHandleT2TCmdRx() rejecting it */
    uint8_t garbage[1] = { 0xFFU };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    CHECK(mock_inject_rx(garbage, sizeof(garbage)), "malformed: injected");
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "malformed: still handed to the dispatcher for it to decide");
    CHECK(mock_tx_count() == 0, "malformed: no response transmitted for a rejected command");
    CHECK(mock_rearm_count() == 1, "malformed: transport rearms itself since the callback returned false");
    m1_t2t_transport_stop();
}

static void test_tx_failure(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "tx-fail: start ok");

    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    CHECK(mock_inject_rx(read_p0, sizeof(read_p0)), "tx-fail: injected");

    /* Force the rearm (the transceive that follows the response TX) to fail. */
    mock_set_next_start_transceive_err(RFAL_ERR_TIMEOUT);
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "tx-fail: command still dispatched and responded to");
    /* Rearm failed -> transport must not silently wedge: it falls back to
     * re-detecting activation, never claims to be armed when it isn't. */
    CHECK(m1_t2t_transport_is_active(), "tx-fail: session stays active (recoverable, not torn down)");
    m1_t2t_transport_stop();
}

static void test_receive_timeout(void)
{
    /* TIMEOUT (and CRC/parity/framing/collision/incomplete-byte -- any
     * non-BUSY, non-NONE, non-LINK_LOSS status) is a malformed-or-absent
     * reception, not a field-loss report. Per RFAL's own listen-mode IRQ
     * handling of a malformed frame (stays in the same session, just goes
     * back to listening), the transport must rearm and keep the SAME
     * active session -- never sleep (that's HLTA-only) and never restart
     * (that's LINK_LOSS-only). */
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "timeout: start ok");
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p0, sizeof(read_p0));
    m1_t2t_transport_tick();   /* first command handled, now WAIT_NEXT_FRAME */

    mock_push_transceive_status(RFAL_ERR_TIMEOUT);
    int sleep_before  = mock_listen_sleep_count();
    int stop_before   = mock_listen_stop_count();
    int rearm_before  = mock_rearm_count();
    m1_t2t_transport_tick();
    CHECK(mock_rearm_count() == rearm_before + 1,
          "timeout: rearmed within the SAME session, never converted into valid data");
    CHECK(mock_listen_sleep_count() == sleep_before, "timeout: never collapsed into HLTA-style sleep");
    CHECK(mock_listen_stop_count() == stop_before, "timeout: never triggers a full restart (that's LINK_LOSS-only)");
    CHECK(m1_t2t_transport_is_active(), "timeout: session stays alive");
    m1_t2t_transport_stop();
}

static void test_other_errors_stay_in_session(void)
{
    /* Same claim as test_receive_timeout, exercised for the other
     * malformed-reception statuses the mock exposes, to prove the
     * 3-way split (HLTA / LINK_LOSS / everything else) doesn't
     * accidentally special-case only one of them. */
    const ReturnCode others[] = { RFAL_ERR_CRC, RFAL_ERR_PARAM };
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        full_reset();
        CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "other-error: start ok");
        uint8_t read_p0[2] = { 0x30U, 0x00U };
        mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
        (void)mock_inject_rx(read_p0, sizeof(read_p0));
        m1_t2t_transport_tick();

        mock_push_transceive_status(others[i]);
        int stop_before  = mock_listen_stop_count();
        int start_before = mock_listen_start_count();
        int rearm_before = mock_rearm_count();
        m1_t2t_transport_tick();
        CHECK(mock_rearm_count() == rearm_before + 1, "other-error: rearmed within the same session");
        CHECK(mock_listen_stop_count() == stop_before, "other-error: no stop -- not treated as link loss");
        CHECK(mock_listen_start_count() == start_before, "other-error: no restart -- not treated as link loss");
        m1_t2t_transport_stop();
    }
}

/* ---- REQUIRED PROOF #1: READ -> HLTA uses sleep/reactivation (not restart) */
static void test_read_then_hlta_uses_sleep_not_restart(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "hlta: start ok");
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p0, sizeof(read_p0));
    mock_push_transceive_status(RFAL_ERR_NONE);
    m1_t2t_transport_tick();   /* READ dispatched, TX'd, rearmed -> WAIT_NEXT_FRAME */

    uint8_t halt[2] = { 0x50U, 0x00U };
    mock_push_transceive_status(RFAL_ERR_NONE);
    (void)mock_inject_rx(halt, sizeof(halt));
    int sleep_before = mock_listen_sleep_count();
    int stop_before  = mock_listen_stop_count();
    int start_before = mock_listen_start_count();
    m1_t2t_transport_tick();
    CHECK(mock_listen_sleep_count() == sleep_before + 1, "HLTA: uses rfalListenSleepStart(), the correct primitive for a genuine 0x50 0x00");
    CHECK(mock_listen_stop_count() == stop_before, "HLTA: never stops the listen engine -- that's LINK_LOSS-only");
    CHECK(mock_listen_start_count() == start_before, "HLTA: never restarts -- WUPA reactivates the same sleeping session");
    CHECK(m1_t2t_transport_is_active(), "HLTA: session stays alive, waiting for WUPA");
    m1_t2t_transport_stop();
}

/* ---- REQUIRED PROOF #2-5: LINK_LOSS full-restart recovery, clean state,
 * repeated cycles never leaving the transport inactive. -------------------- */
static void test_link_loss_full_restart(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "link-loss: start ok");
    const uint8_t *origUid = VARIANTS[0].uid;

    uint8_t get_version[1] = { 0x60U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(get_version, sizeof(get_version));
    /* No transceive-status push here: this tick is WAIT_ACTIVATION-driven
     * (dataFlag-triggered), which never consults rfalGetTransceiveStatus()
     * at all -- that queue is WAIT_NEXT_FRAME-only. Pushing one here would
     * sit unconsumed and wrongly satisfy a LATER WAIT_NEXT_FRAME poll. */
    s_dispatch_response_len_bytes = 8U;
    m1_t2t_transport_tick();   /* GET_VERSION dispatched, TX'd, rearmed -> WAIT_NEXT_FRAME */
    CHECK(s_dispatch_calls == 1, "link-loss: GET_VERSION dispatched first, exactly as on hardware");

    /* #2: RFAL_ERR_LINK_LOSS performs a FULL listen restart -- stop then a
     * fresh start -- never a sleep. */
    mock_push_transceive_status(RFAL_ERR_LINK_LOSS);
    int stop_before  = mock_listen_stop_count();
    int start_before = mock_listen_start_count();
    int sleep_before = mock_listen_sleep_count();
    m1_t2t_transport_tick();
    CHECK(mock_listen_stop_count() == stop_before + 1, "LINK_LOSS: rfalListenStop() called -- the terminated session is actually stopped");
    CHECK(mock_listen_start_count() == start_before + 1, "LINK_LOSS: a fresh rfalListenStart() follows -- full restart, not sleep");
    CHECK(mock_listen_sleep_count() == sleep_before, "LINK_LOSS: never collapsed into HLTA-style sleep");
    CHECK(m1_t2t_transport_is_active(), "LINK_LOSS: transport stays active throughout its own internal restart");
    CHECK(memcmp(mock_last_listen_start_uid(), origUid, 7U) == 0,
          "LINK_LOSS: restart re-arms with the SAME emulated UID (s_lm_conf reused, not re-derived)");
    CHECK(mock_last_listen_start_uid_len() == 7U, "LINK_LOSS: restart preserves the 7-byte UID length");
    CHECK(memcmp(mock_last_listen_start_atqa(), ATQA, 2U) == 0, "LINK_LOSS: restart preserves ATQA");
    CHECK(mock_last_listen_start_sak() == SAK, "LINK_LOSS: restart preserves SAK");

    /* #4: no stale frame auto-fires immediately after restart -- the
     * transport is genuinely back in WAIT_ACTIVATION, not silently still
     * holding the old GET_VERSION frame or the LINK_LOSS status. */
    int dispatch_before_reactivation = s_dispatch_calls;
    m1_t2t_transport_tick();
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == dispatch_before_reactivation,
          "LINK_LOSS: no stale frame or status leaks a phantom dispatch before real reactivation");

    /* #3: a new REQA/WUPA-style activation is accepted after the restart,
     * and #4: the frame delivered is exactly the NEW one -- proving
     * s_rx_buf/s_rx_len_bits were genuinely cleared, not left over from
     * the terminated session (which had a 1-byte GET_VERSION sitting in
     * them). */
    uint8_t read_p4[2] = { 0x30U, 0x04U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    CHECK(mock_inject_rx(read_p4, sizeof(read_p4)), "LINK_LOSS: new activation's frame injected");
    /* No transceive-status push here either -- same WAIT_ACTIVATION reason. */
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == dispatch_before_reactivation + 1, "LINK_LOSS: post-restart reactivation is accepted and dispatched");
    CHECK(s_last_dispatch_rxbits == 16U, "LINK_LOSS: exact new-frame length delivered, no stale length leaked");
    CHECK(memcmp(s_last_dispatch_rx, read_p4, 2U) == 0, "LINK_LOSS: exact new-frame bytes delivered, no stale GET_VERSION byte leaked");

    /* #5: repeated field-loss/restart cycles never leave the transport
     * inactive. */
    for (int cycle = 0; cycle < 4; cycle++) {
        mock_push_transceive_status(RFAL_ERR_LINK_LOSS);
        int sb = mock_listen_start_count();
        m1_t2t_transport_tick();
        CHECK(mock_listen_start_count() == sb + 1, "LINK_LOSS: each repeated cycle performs its own fresh restart");
        CHECK(m1_t2t_transport_is_active(), "LINK_LOSS: repeated cycles never leave the transport inactive");

        uint8_t read_again[2] = { 0x30U, 0x00U };
        mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
        (void)mock_inject_rx(read_again, sizeof(read_again));
        /* No transceive-status push here either -- same WAIT_ACTIVATION reason. */
        m1_t2t_transport_tick();
        CHECK(m1_t2t_transport_is_active(), "LINK_LOSS: transport still active after reactivating post-cycle");
    }

    m1_t2t_transport_stop();
}

static void test_user_cancellation(void)
{
    /* "User cancellation" at the transport level IS m1_t2t_transport_stop()
     * -- the UI-task BACK press only ever sets a flag (ListenerRequestStop(),
     * nfc_listener.c) that the worker task later turns into exactly this
     * call, never touching the radio from the UI task directly. */
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "cancel: start ok");
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, false);   /* mid-session, no frame yet */
    m1_t2t_transport_tick();
    m1_t2t_transport_stop();
    CHECK(!m1_t2t_transport_is_active(), "cancel: stops cleanly even with no frame ever exchanged");
    CHECK(mock_listen_stop_count() == 1, "cancel: exactly one rfalListenStop()");
}

static void test_repeated_start_stop_no_double_stop(void)
{
    full_reset();
    for (int i = 0; i < 5; i++) {
        CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "repeated start/stop: start succeeds each cycle");
        m1_t2t_transport_stop();
    }
    CHECK(mock_listen_start_count() == 5, "repeated start/stop: exactly 5 starts");
    CHECK(mock_listen_stop_count() == 5, "repeated start/stop: exactly 5 stops (1:1, no double stop)");

    /* Calling stop() again with nothing active must be a true no-op. */
    m1_t2t_transport_stop();
    CHECK(mock_listen_stop_count() == 5, "no double stop: an extra stop() when already inactive does not call rfalListenStop() again");
}

/* ---- LIFECYCLE-RACE PROOF #2: 20 consecutive rapid start/stop cycles,
 * immediate BACK->Emulate (stop then start with no delay in between), never
 * leaving the transport in a state a fresh start can't cleanly recover
 * from. -------------------------------------------------------------------- */
static void test_20_rapid_start_stop_cycles(void)
{
    full_reset();
    for (int i = 0; i < 20; i++) {
        CHECK(m1_t2t_transport_start(VARIANTS[i % 4].uid, 7U, ATQA, SAK),
              "20 rapid cycles: start succeeds every cycle, immediately after the prior stop");
        CHECK(m1_t2t_transport_is_active(), "20 rapid cycles: active immediately after start");
        m1_t2t_transport_stop();   /* no delay -- mirrors BACK immediately followed by Emulate */
        CHECK(!m1_t2t_transport_is_active(), "20 rapid cycles: inactive immediately after stop");
    }
    CHECK(mock_listen_start_count() == 20, "20 rapid cycles: exactly 20 starts, none skipped or doubled");
    CHECK(mock_listen_stop_count() == 20, "20 rapid cycles: exactly 20 stops, 1:1 with starts");

    /* A real session still works correctly as the 21st cycle -- the rapid
     * churn above left nothing that would prevent a genuine read. */
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "20 rapid cycles: 21st start still succeeds cleanly");
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p0, sizeof(read_p0));
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "20 rapid cycles: a genuine frame after the churn still dispatches correctly");
    CHECK(memcmp(s_last_dispatch_rx, read_p0, 2U) == 0, "20 rapid cycles: exact frame bytes, nothing stale from the 20 prior cycles");
    m1_t2t_transport_stop();
}

/* ---- LIFECYCLE-RACE PROOF #3: stop() called while genuinely mid-WAIT_NEXT_
 * FRAME (a real command was just answered and rearmed, no HALT/LINK_LOSS
 * involved) leaves nothing behind for the next session. --------------------- */
static void test_stop_during_wait_next_frame(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "stop-in-WAIT_NEXT_FRAME: start ok");
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p0, sizeof(read_p0));
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();   /* dispatched, TX'd, rearmed -> genuinely WAIT_NEXT_FRAME now */
    CHECK(s_dispatch_calls == 1, "stop-in-WAIT_NEXT_FRAME: precondition -- one real exchange happened first");

    int stop_before = mock_listen_stop_count();
    m1_t2t_transport_stop();
    CHECK(mock_listen_stop_count() == stop_before + 1, "stop-in-WAIT_NEXT_FRAME: rfalListenStop() called");
    CHECK(!m1_t2t_transport_is_active(), "stop-in-WAIT_NEXT_FRAME: transport reports inactive");

    /* Fresh session afterward: a DIFFERENT command must be delivered exactly
     * -- proves no stale rx buffer/length from the mid-flight stop leaks in. */
    CHECK(m1_t2t_transport_start(VARIANTS[1].uid, 7U, ATQA, SAK), "stop-in-WAIT_NEXT_FRAME: fresh start ok");
    uint8_t get_version[1] = { 0x60U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(get_version, sizeof(get_version));
    s_dispatch_response_len_bytes = 8U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 2, "stop-in-WAIT_NEXT_FRAME: fresh session dispatches its own new frame");
    CHECK(s_last_dispatch_rxbits == 8U, "stop-in-WAIT_NEXT_FRAME: exact new-frame length, not the old 2-byte READ's");
    CHECK(memcmp(s_last_dispatch_rx, get_version, 1U) == 0, "stop-in-WAIT_NEXT_FRAME: exact new-frame bytes, no stale READ leaked");
    m1_t2t_transport_stop();
}

/* ---- LIFECYCLE-RACE PROOF #4: stop() called immediately after a LINK_LOSS
 * restart has run (the most state-heavy recovery path: stop+fresh-start
 * already happened once internally) still tears down cleanly and a further
 * fresh session is unaffected. ---------------------------------------------- */
static void test_stop_during_link_loss_recovery(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "stop-in-link-loss: start ok");
    uint8_t get_version[1] = { 0x60U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(get_version, sizeof(get_version));
    s_dispatch_response_len_bytes = 8U;
    m1_t2t_transport_tick();   /* GET_VERSION dispatched, rearmed -> WAIT_NEXT_FRAME */

    mock_push_transceive_status(RFAL_ERR_LINK_LOSS);
    int start_before = mock_listen_start_count();
    m1_t2t_transport_tick();   /* internal restart: stop() + fresh start() already ran here */
    CHECK(mock_listen_start_count() == start_before + 1, "stop-in-link-loss: precondition -- the internal restart actually happened");
    CHECK(m1_t2t_transport_is_active(), "stop-in-link-loss: transport active again post-restart");

    /* Now stop WHILE sitting in that freshly-restarted WAIT_ACTIVATION state
     * -- the most state-heavy point to interrupt, immediately after the
     * transport's own internal stop+start pair. */
    int stop_before = mock_listen_stop_count();
    m1_t2t_transport_stop();
    CHECK(mock_listen_stop_count() == stop_before + 1, "stop-in-link-loss: rfalListenStop() called on top of the internal restart");
    CHECK(!m1_t2t_transport_is_active(), "stop-in-link-loss: transport reports inactive");

    /* A further fresh session must behave exactly like a first-ever session
     * -- no residue from the LINK_LOSS-restart-then-stop sequence. */
    CHECK(m1_t2t_transport_start(VARIANTS[2].uid, 7U, ATQA, SAK), "stop-in-link-loss: fresh start after the whole sequence ok");
    uint8_t read_p4[2] = { 0x30U, 0x04U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p4, sizeof(read_p4));
    s_dispatch_response_len_bytes = 16U;
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 2, "stop-in-link-loss: fresh session dispatches cleanly");
    CHECK(memcmp(s_last_dispatch_rx, read_p4, 2U) == 0, "stop-in-link-loss: exact new bytes, nothing stale from the recovery sequence");
    m1_t2t_transport_stop();
}

static void test_no_frame_replay(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "no-replay: start ok");
    uint8_t read_p0[2] = { 0x30U, 0x00U };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_p0, sizeof(read_p0));
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "no-replay: first frame dispatched once");

    /* rfalListenGetState() still (hypothetically) reporting the OLD
     * dataFlag/state -- sticky mock behavior -- must never cause the SAME
     * activation frame to be redelivered once the transport has moved on
     * to WAIT_NEXT_FRAME (a different rfalGetTransceiveStatus()-driven
     * state entirely; rfalListenGetState() is consulted only once, for the
     * very first frame of a session). */
    m1_t2t_transport_tick();
    m1_t2t_transport_tick();
    CHECK(s_dispatch_calls == 1, "no-replay: the same first frame is never redelivered on later ticks");
    m1_t2t_transport_stop();
}

static void test_no_ownership_collision(void)
{
    /* A second start() while already active must not silently take over
     * (which would mean two overlapping rfalListenStart() sessions) --
     * caller must stop() first. */
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "collision: first start ok");
    CHECK(!m1_t2t_transport_start(VARIANTS[1].uid, 7U, ATQA, SAK), "collision: second start while active is refused");
    CHECK(mock_listen_start_count() == 1, "collision: rfalListenStart() never called twice for one session");
    m1_t2t_transport_stop();
}

static void test_start_without_dispatch_refused(void)
{
    /* Never arm with nowhere to send frames -- the callback registration
     * order this test's full_reset() normally guarantees is itself load-
     * bearing production behavior, verified here directly. */
    mock_reset();
    m1_t2t_transport_stop();
    m1_t2t_transport_set_dispatch(NULL);
    CHECK(!m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK),
          "no dispatch: start() refuses to arm without a registered callback");
    CHECK(mock_listen_start_count() == 0, "no dispatch: rfalListenStart() never even attempted");
    m1_t2t_transport_set_dispatch(fake_dispatch);   /* restore for subsequent tests */
}

/* ---- REACTIVATION WATCHDOG (repeated-reactivation-degradation fix):
 * bounds how long WAIT_ACTIVATION may wait for a post-HLTA WUPA
 * reactivation before forcing the already-proven full-restart recovery.
 * mock_set_tick()/mock_advance_tick() drive the same timerCalculateTimer()/
 * timerIsExpired() primitives the production code calls (platformTimerCreate()/
 * platformTimerIsExpired()), so these run instantly -- no real 60s wait. ---- */
#define WATCHDOG_MS 60000U

static void do_one_read_cycle(uint8_t page)
{
    uint8_t read_cmd[2] = { 0x30U, page };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_A, true);
    (void)mock_inject_rx(read_cmd, sizeof(read_cmd));
    m1_t2t_transport_tick();   /* dispatched, TX'd, rearmed -> WAIT_NEXT_FRAME */
}

static void do_one_hlta(void)
{
    uint8_t halt[2] = { 0x50U, 0x00U };
    mock_push_transceive_status(RFAL_ERR_NONE);
    (void)mock_inject_rx(halt, sizeof(halt));
    m1_t2t_transport_tick();   /* HLTA serviced -> sleep -> WAIT_ACTIVATION, watchdog (re)armed */
}

static void do_one_wupa_reactivation(uint8_t page)
{
    uint8_t read_cmd[2] = { 0x30U, page };
    mock_push_lm_state(RFAL_LM_STATE_ACTIVE_Ax, true);   /* WUPA-reactivation state, per rfal_rfst25r3916.c */
    (void)mock_inject_rx(read_cmd, sizeof(read_cmd));
    m1_t2t_transport_tick();
}

/* ACCEPTANCE PROOF #1: 100 consecutive HLTA->WUPA cycles complete, all
 * within one continuous session -- never a restart, since every WUPA
 * arrives well inside the watchdog bound. */
static void test_100_hlta_wupa_cycles(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[3].uid, 7U, ATQA, SAK), "100-cycle: start ok");
    do_one_read_cycle(0x00U);
    CHECK(s_dispatch_calls == 1, "100-cycle: first read dispatched");

    for (int i = 0; i < 100; i++) {
        do_one_hlta();
        CHECK(m1_t2t_transport_is_active(), "100-cycle: active after HLTA");
        do_one_wupa_reactivation((uint8_t)(i & 0xFFU));
    }
    CHECK(s_dispatch_calls == 101, "100-cycle: every one of the 100 reactivations dispatched a real frame (plus the first read)");
    CHECK(mock_listen_sleep_count() == 100, "100-cycle: exactly 100 HLTA sleeps");
    CHECK(mock_listen_start_count() == 1, "100-cycle: never restarted -- the ONLY rfalListenStart() is the original start()");
    CHECK(mock_listen_stop_count() == 0, "100-cycle: never stopped -- no restart was ever triggered");
    CHECK(m1_t2t_transport_is_active(), "100-cycle: transport still active after all 100 cycles");
    m1_t2t_transport_stop();
}

/* ACCEPTANCE PROOF #2: a WUPA arriving any time before the bound -- including
 * right up near it -- never trips the watchdog into a restart. */
static void test_wupa_before_timeout_never_restarts(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "wupa-before-timeout: start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();

    mock_advance_tick(WATCHDOG_MS - 1U);   /* 1ms shy of the bound */
    int start_before = mock_listen_start_count();
    int stop_before  = mock_listen_stop_count();
    do_one_wupa_reactivation(0x04U);
    CHECK(mock_listen_start_count() == start_before, "wupa-before-timeout: no restart -- rfalListenStart() count unchanged");
    CHECK(mock_listen_stop_count() == stop_before, "wupa-before-timeout: no restart -- rfalListenStop() count unchanged");
    CHECK(s_dispatch_calls == 2, "wupa-before-timeout: the late-but-in-time WUPA frame was dispatched normally");
    m1_t2t_transport_stop();
}

/* AUDIT ITEM 3/design requirement: REQA during the normal SLEEP_A window
 * must be ignored -- a halted Type-A tag only answers WUPA while the field
 * remains on. Modeled as: rfalListenGetState() keeps reporting a
 * non-ACTIVE_A/ACTIVE_Ax state (the chip's own automatic-response hardware
 * silently drops REQA in SLEEP_A -- see rfal_rfst25r3916.c's SLEEP_A/AF
 * case only transitioning on RXE_PTA/WU_A_X, never a bare REQA), and the
 * transport must neither dispatch anything nor restart while that persists,
 * provided it stays within the watchdog bound. */
static void test_reqa_ignored_during_sleep_window(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "reqa-ignored: start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();

    /* Explicitly override the mock's sticky lm_state (left at ACTIVE_A/true
     * from the read cycle above) back to "not activated" -- exactly
     * "reader sent something (REQA), but the tag never left sleep for it."
     * Tick repeatedly, advancing time a little each time but staying well
     * under the bound. */
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);
    int start_before = mock_listen_start_count();
    for (int i = 0; i < 20; i++) {
        mock_advance_tick(500U);
        m1_t2t_transport_tick();
    }
    CHECK(s_dispatch_calls == 1, "reqa-ignored: no frame ever dispatched while only ignored REQAs arrive");
    CHECK(mock_listen_start_count() == start_before, "reqa-ignored: no restart triggered -- still well within the bound");
    CHECK(m1_t2t_transport_is_active(), "reqa-ignored: session stays alive, still waiting for a real WUPA");

    /* A genuine WUPA afterward is accepted normally. */
    do_one_wupa_reactivation(0x04U);
    CHECK(s_dispatch_calls == 2, "reqa-ignored: the eventual real WUPA is dispatched normally");
    m1_t2t_transport_stop();
}

/* ACCEPTANCE PROOF #4/#5: a stuck activation (listen-mode never reaches
 * ACTIVE_A/ACTIVE_Ax+dataFlag at all, simulating the stalled IRQ chain the
 * source audit identified) triggers EXACTLY ONE bounded restart once the
 * watchdog expires, and activation works normally after that restart. */
static void test_stuck_activation_triggers_one_bounded_restart(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "stuck-activation: start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();
    /* Override the sticky lm_state (left at ACTIVE_A/true by the read
     * cycle) to "never activated" -- activation never completes, exactly
     * the observed hardware symptom. */
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);

    int start_before = mock_listen_start_count();
    int stop_before  = mock_listen_stop_count();

    mock_advance_tick(WATCHDOG_MS - 1U);
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before, "stuck-activation: no restart yet, 1ms shy of the bound");

    mock_advance_tick(2U);   /* now past the bound */
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before + 1, "stuck-activation: exactly one restart once the bound is crossed");
    CHECK(mock_listen_stop_count() == stop_before + 1, "stuck-activation: the restart used the proven stop()+start() pair");
    CHECK(m1_t2t_transport_is_active(), "stuck-activation: transport stays active through its own internal restart");

    /* "Exactly one" -- further ticks with no time advance and lm_state
     * still stuck must NOT restart again immediately (the freshly-armed
     * watchdog has its own full new window). */
    for (int i = 0; i < 10; i++) { m1_t2t_transport_tick(); }
    CHECK(mock_listen_start_count() == start_before + 1, "stuck-activation: no restart storm -- repeated ticks right after don't restart again");

    /* #5: activation works after that restart. */
    do_one_wupa_reactivation(0x00U);
    CHECK(s_dispatch_calls == 2, "stuck-activation: a real activation after the restart dispatches normally");
    m1_t2t_transport_stop();
}

/* Timer state must not leak across stop()/start(): a fresh start() always
 * gets a fresh full-length window measured from ITS OWN arm time, never
 * inheriting whatever remained of a previous session's watchdog. */
static void test_timer_no_leak_across_stop_start(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "no-leak-stop-start: start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();

    mock_advance_tick(30000U);   /* halfway through the first session's window */
    m1_t2t_transport_stop();
    CHECK(!m1_t2t_transport_is_active(), "no-leak-stop-start: stopped cleanly");

    CHECK(m1_t2t_transport_start(VARIANTS[1].uid, 7U, ATQA, SAK), "no-leak-stop-start: fresh start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);   /* override sticky ACTIVE_A/true -- never activates */
    /* If the old timer had leaked, the elapsed-since-first-arm clock would
     * already be at 30000 + 59000 = 89000ms, well past 60000 -- a leak
     * would restart here. A correctly fresh arm measures from THIS start's
     * own HLTA, so 59000ms more must NOT be enough to expire it. */
    mock_advance_tick(59000U);
    int start_before = mock_listen_start_count();
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before, "no-leak-stop-start: 59s after the SECOND session's own HLTA does not restart -- no stale timer leaked in");

    mock_advance_tick(2000U);   /* now 61s since the second session's own HLTA */
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before + 1, "no-leak-stop-start: the second session's OWN fresh window does eventually expire correctly");
    m1_t2t_transport_stop();
}

/* Timer state must not leak across a LINK_LOSS-triggered restart either --
 * each restart (whatever triggered it) rearms its own fresh full window. */
static void test_timer_no_leak_across_link_loss(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "no-leak-link-loss: start ok");
    do_one_read_cycle(0x00U);   /* -> WAIT_NEXT_FRAME */

    mock_advance_tick(40000U);
    mock_push_transceive_status(RFAL_ERR_LINK_LOSS);
    int start_before = mock_listen_start_count();
    m1_t2t_transport_tick();   /* LINK_LOSS restart -> WAIT_ACTIVATION, watchdog armed fresh at tick=40000 */
    CHECK(mock_listen_start_count() == start_before + 1, "no-leak-link-loss: precondition -- the LINK_LOSS restart happened");
    /* Override the sticky lm_state (left at ACTIVE_A/true by the read cycle
     * above) so WAIT_ACTIVATION doesn't see a phantom "already activated"
     * on the very next tick -- it must genuinely keep waiting. */
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);

    /* If the PRE-restart watchdog had somehow leaked (armed at tick=0,
     * 60000ms bound), it would already read as expired right now (tick is
     * 40000, well past nothing -- this only matters once we advance
     * further). Advance close to but under 60000 MORE from the restart's
     * own arm time; a leaked/stale timer measured from tick=0 would long
     * since have expired by tick=40000+59000=99000. */
    mock_advance_tick(59000U);
    int start_before2 = mock_listen_start_count();
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before2, "no-leak-link-loss: 59s after the restart's OWN arm does not re-expire -- no stale pre-restart timer leaked");

    mock_advance_tick(2000U);
    m1_t2t_transport_tick();
    CHECK(mock_listen_start_count() == start_before2 + 1, "no-leak-link-loss: the restart's own fresh window does eventually expire correctly");
    m1_t2t_transport_stop();
}

/* ACCEPTANCE PROOF #7: a persistently stuck listen (field gone, or field
 * back but still stuck) must be retried at the bounded rate only -- never
 * a tight restart loop -- and must still recover once a real activation
 * finally arrives. */
static void test_no_restart_loop_on_persistent_stall(void)
{
    full_reset();
    CHECK(m1_t2t_transport_start(VARIANTS[0].uid, 7U, ATQA, SAK), "no-restart-loop: start ok");
    do_one_read_cycle(0x00U);
    do_one_hlta();
    /* Override the sticky lm_state (left at ACTIVE_A/true by the read
     * cycle) so it stays stuck at "not yet activated" throughout -- models
     * "field off" and "field back on but still stuck" identically from the
     * transport's point of view. */
    mock_push_lm_state(RFAL_LM_STATE_IDLE, false);

    int restarts = 0;
    int start_before = mock_listen_start_count();
    /* Tick at a realistic 1000ms granularity. Each restart rearms exactly
     * WATCHDOG_MS from the tick it fires on (timerIsExpired() requires
     * strictly passing the bound, matching the real timer.c semantics
     * exactly -- see timerIsExpired()'s "sDiff < 0" check), so at this
     * granularity a restart actually recurs every (WATCHDOG_MS + 1000)ms,
     * not exactly every WATCHDOG_MS. Run for just over 3 such periods --
     * long enough to guarantee 3 restarts happen, short enough that a
     * tight loop (which would fire far more than 3 times over this much
     * simulated time) is unambiguously distinguishable from the correct
     * bounded rate. */
    const int ticks = (int)(3 * ((WATCHDOG_MS / 1000U) + 1U));
    for (int i = 0; i < ticks; i++) {
        mock_advance_tick(1000U);
        int before = mock_listen_start_count();
        m1_t2t_transport_tick();
        if (mock_listen_start_count() != before) {
            restarts++;
            CHECK(mock_listen_start_count() == before + 1, "no-restart-loop: never more than one restart in a single tick");
        }
    }
    CHECK(restarts == 3, "no-restart-loop: exactly 3 restarts over exactly 3 bound-periods worth of simulated time -- bounded rate, not a tight loop");
    CHECK(mock_listen_start_count() == start_before + 3, "no-restart-loop: restart count matches the observed restarts exactly");
    CHECK(m1_t2t_transport_is_active(), "no-restart-loop: transport remains active throughout persistent stall retries");

    /* Recovery: a real activation after all that still works. */
    do_one_wupa_reactivation(0x00U);
    CHECK(s_dispatch_calls == 2, "no-restart-loop: activation succeeds normally once the field/reader genuinely comes back");
    m1_t2t_transport_stop();
}

/* ---- integration wiring (source-bound: nfc_driver.c is HAL-heavy/
 * unlinkable, same constraint as every other RFAL-coupled file in this
 * project) -- proves the ownership gate is correctly ordered and that
 * every other persona bypasses this module completely, complementing the
 * pure unit-level behavior already proven above. ---------------------- */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1U);
    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}
static bool contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void test_integration_wiring(void)
{
    char *drv = read_file("NFC/NFC_drv/legacy/nfc_driver.c");

    /* Gate ordering: the T2T check must appear in the SAME sequential
     * if/else-if region as the MFC raw gate, and BEFORE the ordinary
     * NfcRole.nfc_process_func() call -- so a persona can be routed to at
     * most one of {MFC raw, T2T transport, everything else}, never two. */
    const char *mfc_gate  = "Emu_GetPersona() == EMU_PERSONA_MFC_EMU";
    const char *t2t_gate  = "if ( NFC_T2TTransportIsActive() )";
    const char *legacy    = "NfcRole.nfc_process_func();";
    CHECK(contains(drv, mfc_gate), "nfc_driver.c: MFC raw ownership gate present");
    CHECK(contains(drv, t2t_gate), "nfc_driver.c: T2T transport ownership gate present");
    CHECK(contains(drv, legacy),   "nfc_driver.c: the ordinary (all other personas) path is unchanged");
    {
        const char *pm = strstr(drv, mfc_gate);
        const char *pt = strstr(drv, t2t_gate);
        const char *pl = strstr(drv, legacy);
        CHECK((pm != NULL) && (pt != NULL) && (pl != NULL) && (pm < pt) && (pt < pl),
              "nfc_driver.c: gate ordering is MFC -> T2T -> legacy, sequential if/else-if, never parallel checks");
    }
    CHECK(contains(drv, "NFC_T2TTransportProcess();"),
          "nfc_driver.c: T2T branch ticks the transport instead of calling nfc_process_func() -- mutually exclusive, never both");
    CHECK(contains(drv, "m1_t2t_transport_stop();"),
          "nfc_driver.c: Q_EVENT_NFC_EMULATE_STOP path also stops the transport (idempotent safety net)");

    /* LIFECYCLE-RACE FIX proof #1/#6: the flag-based stop (consumed inside
     * NFC_T2TTransportProcess(), independent of the later queued
     * Q_EVENT_NFC_EMULATE_STOP) must itself immediately transition NfcState
     * -- never leaving a tick where NFC_T2TTransportIsActive() is already
     * false but NfcState is still NFC_STATE_PROCESS, which is exactly the
     * window that let the generic NfcRole.nfc_process_func() (legacy
     * ListenerCycle()) run against a radio the dedicated transport had just
     * released. */
    CHECK(contains(drv, "if ( !NFC_T2TTransportIsActive() )"),
          "nfc_driver.c: an immediate re-check for the transport having just stopped is present");
    {
        const char *immediate_check = "if ( !NFC_T2TTransportIsActive() )";
        const char *process_call    = "NFC_T2TTransportProcess();";
        const char *legacy_call     = "NfcRole.nfc_process_func();";
        const char *p_proc  = strstr(drv, process_call);
        const char *p_imm   = (p_proc != NULL) ? strstr(p_proc, immediate_check) : NULL;
        const char *p_leg   = strstr(drv, legacy_call);
        CHECK((p_proc != NULL) && (p_imm != NULL) && (p_leg != NULL) && (p_proc < p_imm) && (p_imm < p_leg),
              "nfc_driver.c: the immediate-transition check appears right after NFC_T2TTransportProcess(), strictly before the legacy fallthrough");
    }
    free(drv);

    char *lis = read_file("NFC/NFC_drv/legacy/nfc_listener.c");
    /* Exactly one production call site for m1_t2t_transport_start(): inside
     * ListenIni()'s T2T branch. A second call site anywhere would risk the
     * "no ownership collision" property this test's unit-level check above
     * already proves start() itself refuses. */
    {
        const char *needle = "m1_t2t_transport_start(";
        const char *p = strstr(lis, needle);
        int count = 0;
        while (p != NULL) { count++; p = strstr(p + 1, needle); }
        CHECK(count == 1, "nfc_listener.c: exactly one call site for m1_t2t_transport_start() (in ListenIni())");
    }
    CHECK(contains(lis, "if (m1_t2t_transport_is_active()) {\n        return m1_t2t_transport_rearm_rx();"),
          "nfc_listener.c: CeRearmRxAfterTx() defers to the transport's own rearm only when it owns the session");

    /* LIFECYCLE-RACE FIX defensive guard: ListenerCycle() must refuse to run
     * at all while the persona is T2T, mirroring the MFC raw-session
     * invariant guard exactly (same file, same function, established
     * pattern) -- belt-and-suspenders on top of the nfc_driver.c fix above,
     * for the case some future change reopens a window where this function
     * gets called with a stale EMU_PERSONA_T2T. */
    CHECK(contains(lis, "if (g_persona == EMU_PERSONA_T2T) {"),
          "nfc_listener.c: ListenerCycle() has an explicit T2T-persona invariant guard");
    {
        const char *mfc_guard = "if (m1_mfc_raw_hw_active()) {";
        const char *t2t_guard = "if (g_persona == EMU_PERSONA_T2T) {";
        const char *worker    = "rfalNfcWorker();";
        const char *p_mfc = strstr(lis, mfc_guard);
        const char *p_t2t = (p_mfc != NULL) ? strstr(p_mfc, t2t_guard) : NULL;
        const char *p_wrk = strstr(lis, worker);
        CHECK((p_mfc != NULL) && (p_t2t != NULL) && (p_wrk != NULL) && (p_mfc < p_t2t) && (p_t2t < p_wrk),
              "nfc_listener.c: T2T guard sits after the MFC guard and before rfalNfcWorker() is ever called");
    }
    free(lis);

    /* REQUIRED PROOF #6: MFC and non-T2T personas remain untouched by the
     * LINK_LOSS fix -- m1_t2t_transport.c is the only file that fix
     * touches, and it must reference nothing MFC-specific at all. Combined
     * with the gate-ordering/single-call-site checks above (already
     * proving MFC and T2T ownership are mutually exclusive and unchanged),
     * this shows the fix cannot have altered MFC or any other persona's
     * behavior even in principle. */
    char *xport = read_file("NFC/NFC_drv/legacy/m1_t2t_transport.c");
    CHECK(!contains(xport, "MFC") && !contains(xport, "Mfc") && !contains(xport, "mfc"),
          "m1_t2t_transport.c: no MFC reference anywhere -- the LINK_LOSS fix cannot touch MFC's raw-emulation path");

    /* Reactivation-watchdog fix: value taken from the codebase's own
     * already-configured CE totalDuration, mechanism from RFAL's own
     * discTmr, not invented -- and it must never fire before a valid
     * activation, only ever route through the already-proven restart. */
    CHECK(contains(xport, "#define M1_T2T_REACTIVATION_TIMEOUT_MS 60000U"),
          "m1_t2t_transport.c: watchdog bound is the codebase's own existing CE totalDuration (60000ms), not an invented value");
    CHECK(contains(xport, "platformTimerCreate(M1_T2T_REACTIVATION_TIMEOUT_MS)"),
          "m1_t2t_transport.c: watchdog uses the same platformTimerCreate() primitive RFAL's own discTmr uses");
    CHECK(contains(xport, "platformTimerIsExpired(s_reactivation_tmr)"),
          "m1_t2t_transport.c: watchdog expiry check uses the same platformTimerIsExpired() primitive RFAL's own discTmr uses");
    {
        const char *arm_fn   = "static void m1_t2t_transport_arm_reactivation_timer(void)";
        const char *go_idle  = "static void m1_t2t_transport_go_idle_and_await_reactivation(void)";
        const char *restart  = "static void m1_t2t_transport_restart_listen(void)";
        const char *start_fn = "bool m1_t2t_transport_start(";
        const char *p_arm   = strstr(xport, arm_fn);
        const char *p_idle  = strstr(xport, go_idle);
        const char *p_rst   = strstr(xport, restart);
        const char *p_start = strstr(xport, start_fn);
        CHECK((p_arm != NULL) && (p_idle != NULL) && (p_rst != NULL) && (p_start != NULL) && (p_arm < p_idle) && (p_idle < p_rst) && (p_rst < p_start),
              "m1_t2t_transport.c: the arm helper is defined before every site that must call it");
        /* Each of the three WAIT_ACTIVATION entry points must call the arm
         * helper -- checked by proximity within each function's own body,
         * not a global count (which couldn't distinguish "called from the
         * right places" from "called from the wrong ones"). */
        const char *arm_call = "m1_t2t_transport_arm_reactivation_timer();";
        const char *p_idle_arm  = strstr(p_idle, arm_call);
        const char *p_rst_arm   = strstr(p_rst, arm_call);
        const char *p_start_arm = strstr(p_start, arm_call);
        CHECK((p_idle_arm != NULL) && (p_idle_arm < p_rst), "m1_t2t_transport.c: HLTA-sleep path arms the watchdog");
        CHECK((p_rst_arm != NULL) && (p_rst_arm < p_start), "m1_t2t_transport.c: LINK_LOSS/timeout restart path re-arms the watchdog");
        CHECK(p_start_arm != NULL, "m1_t2t_transport.c: start() arms the watchdog");
    }
    CHECK(contains(xport, "s_reactivation_tmr_armed = false;"),
          "m1_t2t_transport.c: the watchdog is explicitly cancellable (on success and on stop()), not just overwritten");
    free(xport);
}

/* ---- command-engine authenticity (source-bound: CeHandleT2TCmdRx() lives
 * in nfc_listener.c, which cannot be host-linked -- same HAL constraint as
 * above). Proves the T7 audit's per-command behavior actually landed: real
 * captured data or a genuine NAK, never the old placeholder/fabricated
 * responses this task specifically removed. ------------------------------ */
static void test_command_engine_authenticity(void)
{
    char *lis = read_file("NFC/NFC_drv/legacy/nfc_listener.c");

    CHECK(contains(lis, "#define T2T_CMD_READ_SIG         0x3CU"),
          "nfc_listener.c: READ_SIGNATURE opcode is the real NXP-assigned 0x3C");
    CHECK(contains(lis, "#define T2T_CMD_READ_CNT         0x39U"),
          "nfc_listener.c: READ_CNT opcode is the real NXP-assigned 0x39");
    CHECK(contains(lis, "#define T2T_CMD_CHECK_TEARING    0x3EU"),
          "nfc_listener.c: CHECK_TEARING opcode is the real NXP-assigned 0x3E");
    CHECK(contains(lis, "#define T2T_CMD_PWD_AUTH         0x1BU"),
          "nfc_listener.c: PWD_AUTH opcode is the real NXP-assigned 0x1B");

    CHECK(contains(lis, "case T2T_CMD_READ_SIG:"), "nfc_listener.c: READ_SIGNATURE is a real dispatch case");
    CHECK(contains(lis, "if (!s_t2t_img.requires_signature || !s_t2t_img.signature_valid) { return false; }"),
          "nfc_listener.c: READ_SIGNATURE refuses to answer without a genuinely captured signature");
    CHECK(contains(lis, "memcpy(g_ceTxBuf, s_t2t_img.signature, 32U);"),
          "nfc_listener.c: READ_SIGNATURE responds with the real captured signature bytes");

    CHECK(contains(lis, "case T2T_CMD_READ_CNT:"), "nfc_listener.c: READ_CNT is a real dispatch case");
    CHECK(contains(lis, "case T2T_CMD_CHECK_TEARING:"), "nfc_listener.c: CHECK_TEARING is a real dispatch case");
    CHECK(contains(lis, "case T2T_CMD_PWD_AUTH:"), "nfc_listener.c: PWD_AUTH is a real dispatch case");
    /* NFC-T2T-Unlock task: PWD_AUTH now genuinely answers a real PACK when
     * a verified credential (Unlock) matches, and a genuine NAK otherwise
     * -- superseding the earlier "always NAK" era this file's own header
     * comment used to (correctly, for its time) require. Both sides are
     * checked: the match path must exist and must never compare against
     * the masked PWD/PACK page bytes; the no-match/no-credential path
     * must still fall through to a real NAK, never a fabricated PACK. */
    CHECK(contains(lis, "s_t2t_img_armed && s_t2t_img.credential_valid &&") &&
          contains(lis, "memcmp(&rx[1], s_t2t_img.pwd, 4U) == 0"),
          "nfc_listener.c: PWD_AUTH compares against a genuinely-verified credential only (Unlock)");
    CHECK(contains(lis, "never the masked PWD/PACK page bytes"),
          "nfc_listener.c: PWD_AUTH documented as never using the masked PWD/PACK page bytes as a credential");
    CHECK(contains(lis, "err = CeSendShortFrame(T2T_NAK_NIBBLE);"),
          "nfc_listener.c: PWD_AUTH still falls through to a genuine NAK when there is no verified match");
    CHECK(!contains(lis, "bool matched = false;"),
          "nfc_listener.c: PWD_AUTH never reintroduces the old fabricated-page-comparison branch");

    /* The true password is genuinely unknown -- PWD_AUTH must never compare
     * against or return a "captured" PWD/PACK page again. The
     * listener (mf_ultralight_listener.c) proves those pages read back as
     * masked zero bytes on real hardware regardless of protection state, so
     * anything claiming to compare against or return them is fabrication
     * wearing a captured-data costume. A regression reintroducing this
     * (even as dead code) is itself the defect this check exists to catch. */
    CHECK(!contains(lis, "s_t2t_img.page[pwdPage]"),
          "nfc_listener.c: PWD_AUTH no longer references a captured PWD page -- it's masked zero data, never genuine");
    CHECK(!contains(lis, "s_t2t_img.page[packPage]"),
          "nfc_listener.c: PWD_AUTH no longer references a captured PACK page -- it's masked zero data, never genuine");

    /* The three earlier fabricating lines this task removed must stay gone. */
    CHECK(!contains(lis, "g_ceTxBuf[0] = g_ceTxBuf[1] = g_ceTxBuf[2] = 0x00;"),
          "nfc_listener.c: old fabricated all-zero READ_CNT response is gone");
    CHECK(!contains(lis, "memset(g_ceTxBuf, 0x00, 32);"),
          "nfc_listener.c: old fabricated all-zero READ_SIGNATURE response is gone");
    CHECK(!contains(lis, "g_ceTxBuf[0] = 0x80;"),
          "nfc_listener.c: old fabricated fixed PACK byte (0x80) is gone");

    free(lis);
}

int main(void)
{
    test_integration_wiring();
    test_command_engine_authenticity();
    test_all_variants();
    test_zero_length_activation_frame();
    test_malformed_command();
    test_tx_failure();
    test_receive_timeout();
    test_other_errors_stay_in_session();
    test_read_then_hlta_uses_sleep_not_restart();
    test_link_loss_full_restart();
    test_user_cancellation();
    test_repeated_start_stop_no_double_stop();
    test_20_rapid_start_stop_cycles();
    test_stop_during_wait_next_frame();
    test_stop_during_link_loss_recovery();
    test_no_frame_replay();
    test_no_ownership_collision();
    test_start_without_dispatch_refused();
    test_100_hlta_wupa_cycles();
    test_wupa_before_timeout_never_restarts();
    test_reqa_ignored_during_sleep_window();
    test_stuck_activation_triggers_one_bounded_restart();
    test_timer_no_leak_across_stop_start();
    test_timer_no_leak_across_link_loss();
    test_no_restart_loop_on_persistent_stall();

    printf("\nm1_t2t_transport_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
