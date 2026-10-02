/* See rfal_mock.h. Implements rfal_nfc.h's three data-exchange functions
 * (the exact production seam m1_desfire.c's desf_xchg()/desf_xchg_raw()
 * call) entirely synchronously: every call completes immediately with
 * whatever was queued via mock_queue_response(), so the do-while BUSY-poll
 * loop in desf_xchg_raw() runs exactly once per call. Not used by the
 * firmware build -- host analysis only. */
#include "rfal_nfc.h"
#include "rfal_mock.h"
#include <string.h>

#define MOCK_MAX_RESPONSES     64
#define MOCK_MAX_RESPONSE_LEN  200
#define MOCK_MAX_TX_CALLS      64
#define MOCK_MAX_TX_LEN        16

static uint8_t  s_resp_data[MOCK_MAX_RESPONSES][MOCK_MAX_RESPONSE_LEN];
static uint16_t s_resp_len[MOCK_MAX_RESPONSES];
static int      s_resp_count = 0;
static int      s_resp_next  = 0;

static uint8_t  s_tx_data[MOCK_MAX_TX_CALLS][MOCK_MAX_TX_LEN];
static uint16_t s_tx_len[MOCK_MAX_TX_CALLS];
static int      s_tx_count = 0;

static uint8_t  s_cur_rx[MOCK_MAX_RESPONSE_LEN];
static uint16_t s_rcv_len_slot;

void mock_reset(void)
{
    s_resp_count = 0;
    s_resp_next  = 0;
    s_tx_count   = 0;
    memset(s_resp_data, 0, sizeof(s_resp_data));
    memset(s_resp_len, 0, sizeof(s_resp_len));
    memset(s_tx_data, 0, sizeof(s_tx_data));
    memset(s_tx_len, 0, sizeof(s_tx_len));
    memset(s_cur_rx, 0, sizeof(s_cur_rx));
    s_rcv_len_slot = 0;
}

void mock_queue_response(const uint8_t *bytes, uint16_t len)
{
    if (s_resp_count >= MOCK_MAX_RESPONSES) return;
    if (len > MOCK_MAX_RESPONSE_LEN) len = MOCK_MAX_RESPONSE_LEN;
    memcpy(s_resp_data[s_resp_count], bytes, len);
    s_resp_len[s_resp_count] = len;
    s_resp_count++;
}

int mock_tx_call_count(void)
{
    return s_tx_count;
}

uint16_t mock_get_tx_call(int index, uint8_t *out, uint16_t out_cap)
{
    if ((index < 0) || (index >= s_tx_count)) return 0;
    uint16_t len = s_tx_len[index];
    uint16_t copy_len = (len > out_cap) ? out_cap : len;
    memcpy(out, s_tx_data[index], copy_len);
    return len;
}

void rfalNfcWorker(void)
{
    /* no-op: the mock always completes synchronously */
}

ReturnCode rfalNfcDataExchangeStart(uint8_t *txData, uint16_t txDataLen, uint8_t **rxData, uint16_t **rvdLen, uint32_t fwt)
{
    (void)fwt;

    if (s_tx_count < MOCK_MAX_TX_CALLS) {
        uint16_t len = (txDataLen > MOCK_MAX_TX_LEN) ? MOCK_MAX_TX_LEN : txDataLen;
        memcpy(s_tx_data[s_tx_count], txData, len);
        s_tx_len[s_tx_count] = txDataLen;
        s_tx_count++;
    }

    if (s_resp_next >= s_resp_count) {
        /* Script exhausted -- fail loudly rather than hang or fabricate. */
        *rxData = NULL;
        *rvdLen = NULL;
        return RFAL_ERR_TIMEOUT;
    }

    memcpy(s_cur_rx, s_resp_data[s_resp_next], s_resp_len[s_resp_next]);
    s_rcv_len_slot = s_resp_len[s_resp_next];
    s_resp_next++;

    *rxData = s_cur_rx;
    *rvdLen = &s_rcv_len_slot;
    return RFAL_ERR_NONE;
}

ReturnCode rfalNfcDataExchangeGetStatus(void)
{
    return RFAL_ERR_NONE;
}
