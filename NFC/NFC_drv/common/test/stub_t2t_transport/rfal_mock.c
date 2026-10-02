/* Programmable mock backing rfal_rf.h/rfal_nfca.h's stub declarations --
 * the "mocked low-level radio interface" the transport state machine test
 * drives. Scripts exact sequences (activation, per-frame RX/TX, HALT,
 * field-loss, errors) without any real RFAL/HAL involvement.
 *
 * rfalNfcaListenerIsSleepReq() is NOT mocked -- it's real, faithful logic
 * (transcribed from and verified against the real rfal_nfca.c while
 * designing m1_t2t_transport.c: HLTA is exactly 2 bytes, 0x50 0x00, CRC
 * already stripped), so this test exercises the real HLTA-detection
 * behavior, not a stand-in for it.
 */
#include "rfal_rf.h"
#include "rfal_nfca.h"
#include <string.h>

/* ---- rfalListenStart() capture -------------------------------------- */
static int     s_listen_start_count = 0;
static int     s_listen_stop_count  = 0;
static int     s_listen_sleep_count = 0;
static uint8_t s_last_uid[10];
static uint8_t s_last_uid_len = 0;
static uint8_t s_last_atqa[2];
static uint8_t s_last_sak = 0;
static ReturnCode s_next_listen_start_err = RFAL_ERR_NONE;

/* ---- currently-armed RX target (rfalListenStart or rfalStartTransceive) */
static uint8_t  *s_cur_rxbuf = NULL;
static uint16_t *s_cur_rxlen = NULL;

/* ---- rfalStartTransceive() capture ----------------------------------- */
static int      s_start_transceive_count = 0;
static int      s_tx_count    = 0;   /* txBuf != NULL: a real response TX  */
static int      s_rearm_count = 0;   /* txBuf == NULL: an RX-only rearm    */
static uint8_t  s_last_tx_buf[64];
static uint16_t s_last_tx_len_bits = 0;
static ReturnCode s_next_start_transceive_err = RFAL_ERR_NONE;

/* ---- rfalListenGetState() / rfalGetTransceiveStatus() FIFO scripts --- */
#define MOCK_QUEUE_MAX 32U
static rfalLmState s_lm_state_q[MOCK_QUEUE_MAX];
static bool         s_lm_flag_q[MOCK_QUEUE_MAX];
static uint8_t       s_lm_q_head = 0, s_lm_q_tail = 0;
static rfalLmState  s_lm_state_sticky = RFAL_LM_STATE_IDLE;
static bool          s_lm_flag_sticky  = false;

static ReturnCode s_txrx_status_q[MOCK_QUEUE_MAX];
static uint8_t    s_txrx_q_head = 0, s_txrx_q_tail = 0;
static ReturnCode s_txrx_status_sticky = RFAL_ERR_BUSY;

static int s_worker_count = 0;

/* ---- mock tick (backs timerCalculateTimer()/timerIsExpired()) -------- */
static uint32_t s_mock_tick = 0U;

void mock_reset(void)
{
    s_mock_tick = 0U;
    s_listen_start_count = 0;
    s_listen_stop_count  = 0;
    s_listen_sleep_count = 0;
    memset(s_last_uid, 0, sizeof(s_last_uid));
    s_last_uid_len = 0;
    memset(s_last_atqa, 0, sizeof(s_last_atqa));
    s_last_sak = 0;
    s_next_listen_start_err = RFAL_ERR_NONE;

    s_cur_rxbuf = NULL;
    s_cur_rxlen = NULL;

    s_start_transceive_count = 0;
    s_tx_count    = 0;
    s_rearm_count = 0;
    memset(s_last_tx_buf, 0, sizeof(s_last_tx_buf));
    s_last_tx_len_bits = 0;
    s_next_start_transceive_err = RFAL_ERR_NONE;

    s_lm_q_head = s_lm_q_tail = 0;
    s_lm_state_sticky = RFAL_LM_STATE_IDLE;
    s_lm_flag_sticky  = false;

    s_txrx_q_head = s_txrx_q_tail = 0;
    s_txrx_status_sticky = RFAL_ERR_BUSY;

    s_worker_count = 0;
}

/* ---- production seam (rfal_rf.h declarations) ------------------------ */

ReturnCode rfalListenStart(uint32_t lmMask, const rfalLmConfPA *confA, const rfalLmConfPB *confB,
                           const rfalLmConfPF *confF, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen)
{
    (void)lmMask; (void)confB; (void)confF; (void)rxBufLen;
    s_listen_start_count++;
    if (confA != NULL) {
        s_last_uid_len = (confA->nfcidLen == RFAL_LM_NFCID_LEN_07) ? 7U : 4U;
        memcpy(s_last_uid, confA->nfcid, s_last_uid_len);
        s_last_atqa[0] = confA->SENS_RES[0];
        s_last_atqa[1] = confA->SENS_RES[1];
        s_last_sak     = confA->SEL_RES;
    }
    if (s_next_listen_start_err != RFAL_ERR_NONE) {
        return s_next_listen_start_err;
    }
    s_cur_rxbuf = rxBuf;
    s_cur_rxlen = rxLen;
    if (rxLen != NULL) { *rxLen = 0U; }
    return RFAL_ERR_NONE;
}

ReturnCode rfalListenSleepStart(rfalLmState sleepSt, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen)
{
    (void)sleepSt; (void)rxBufLen;
    s_listen_sleep_count++;
    s_cur_rxbuf = rxBuf;
    s_cur_rxlen = rxLen;
    if (rxLen != NULL) { *rxLen = 0U; }
    return RFAL_ERR_NONE;
}

ReturnCode rfalListenStop(void)
{
    s_listen_stop_count++;
    s_cur_rxbuf = NULL;
    s_cur_rxlen = NULL;
    return RFAL_ERR_NONE;
}

rfalLmState rfalListenGetState(bool *dataFlag, void *lastBR)
{
    (void)lastBR;
    rfalLmState st;
    bool flag;
    if (s_lm_q_head != s_lm_q_tail) {
        st   = s_lm_state_q[s_lm_q_head];
        flag = s_lm_flag_q[s_lm_q_head];
        s_lm_q_head = (uint8_t)((s_lm_q_head + 1U) % MOCK_QUEUE_MAX);
        s_lm_state_sticky = st;
        s_lm_flag_sticky  = flag;
    } else {
        st   = s_lm_state_sticky;
        flag = s_lm_flag_sticky;
    }
    if (dataFlag != NULL) { *dataFlag = flag; }
    return st;
}

ReturnCode rfalStartTransceive(rfalTransceiveContext *ctx)
{
    s_start_transceive_count++;
    if (ctx->txBuf != NULL) {
        s_tx_count++;
        s_last_tx_len_bits = ctx->txBufLen;
        uint16_t bytes = rfalConvBitsToBytes(ctx->txBufLen);
        if (bytes > sizeof(s_last_tx_buf)) { bytes = (uint16_t)sizeof(s_last_tx_buf); }
        memcpy(s_last_tx_buf, ctx->txBuf, bytes);
    } else {
        s_rearm_count++;
    }
    if (s_next_start_transceive_err != RFAL_ERR_NONE) {
        return s_next_start_transceive_err;
    }
    s_cur_rxbuf = ctx->rxBuf;
    s_cur_rxlen = ctx->rxRcvdLen;
    if (ctx->rxRcvdLen != NULL) { *ctx->rxRcvdLen = 0U; }
    return RFAL_ERR_NONE;
}

ReturnCode rfalGetTransceiveStatus(void)
{
    if (s_txrx_q_head != s_txrx_q_tail) {
        ReturnCode st = s_txrx_status_q[s_txrx_q_head];
        s_txrx_q_head = (uint8_t)((s_txrx_q_head + 1U) % MOCK_QUEUE_MAX);
        s_txrx_status_sticky = st;
        return st;
    }
    return s_txrx_status_sticky;
}

void rfalWorker(void)
{
    s_worker_count++;
}

bool rfalNfcaListenerIsSleepReq(const uint8_t *buf, uint16_t bufLen)
{
    /* Real logic, transcribed and verified against rfal_nfca.c: exactly 2
     * bytes, 0x50 0x00 -- CRC already stripped by the time a frame reaches
     * an rfalListenStart()-owned buffer. */
    if ((bufLen != 2U) || (buf[0] != 0x50U) || (buf[1] != 0x00U)) {
        return false;
    }
    return true;
}

uint32_t timerCalculateTimer(uint16_t time)
{
    /* Real logic (Drivers/BSP/Components/ST25R3916/timer.c), transcribed
     * verbatim against the mock tick instead of platformGetSysTick(). */
    return s_mock_tick + time;
}

bool timerIsExpired(uint32_t timer)
{
    /* Real logic, transcribed verbatim (same signed-diff/rollover-safe
     * comparison as timer.c's timerIsExpired()), against the mock tick. */
    uint32_t uDiff = (timer - s_mock_tick);
    int32_t  sDiff = (int32_t)uDiff;
    return (sDiff < 0);
}

/* ---- test control API ------------------------------------------------ */

void mock_push_lm_state(rfalLmState state, bool dataFlag)
{
    s_lm_state_q[s_lm_q_tail] = state;
    s_lm_flag_q[s_lm_q_tail]  = dataFlag;
    s_lm_q_tail = (uint8_t)((s_lm_q_tail + 1U) % MOCK_QUEUE_MAX);
}

void mock_push_transceive_status(ReturnCode status)
{
    s_txrx_status_q[s_txrx_q_tail] = status;
    s_txrx_q_tail = (uint8_t)((s_txrx_q_tail + 1U) % MOCK_QUEUE_MAX);
}

void mock_set_next_listen_start_err(ReturnCode err) { s_next_listen_start_err = err; }
void mock_set_next_start_transceive_err(ReturnCode err) { s_next_start_transceive_err = err; }

bool mock_inject_rx(const uint8_t *data, uint16_t lenBytes)
{
    if ((s_cur_rxbuf == NULL) || (s_cur_rxlen == NULL)) { return false; }
    memcpy(s_cur_rxbuf, data, lenBytes);
    *s_cur_rxlen = rfalConvBytesToBits(lenBytes);
    return true;
}

void     mock_set_tick(uint32_t ms)     { s_mock_tick = ms; }
void     mock_advance_tick(uint32_t ms) { s_mock_tick += ms; }
uint32_t mock_get_tick(void)            { return s_mock_tick; }

int mock_listen_start_count(void) { return s_listen_start_count; }
int mock_listen_stop_count(void)  { return s_listen_stop_count; }
int mock_listen_sleep_count(void) { return s_listen_sleep_count; }
int mock_start_transceive_count(void) { return s_start_transceive_count; }
int mock_rearm_count(void) { return s_rearm_count; }
int mock_tx_count(void)    { return s_tx_count; }
int mock_worker_count(void) { return s_worker_count; }

uint16_t       mock_last_tx_len_bits(void) { return s_last_tx_len_bits; }
const uint8_t *mock_last_tx_buf(void)      { return s_last_tx_buf; }
const uint8_t *mock_last_listen_start_uid(void) { return s_last_uid; }
uint8_t        mock_last_listen_start_uid_len(void) { return s_last_uid_len; }
const uint8_t *mock_last_listen_start_atqa(void) { return s_last_atqa; }
uint8_t        mock_last_listen_start_sak(void) { return s_last_sak; }
