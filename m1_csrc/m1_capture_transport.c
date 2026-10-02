/* See COPYING.txt for license details. */

/*
*
* m1_capture_transport.c
*
* Host-testable capture-transport foundation (see m1_capture_transport.h).
* Pure logic: framing, streaming RX, sequence tracking, byte credits, and the
* rate-negotiation state machine. No UART/DMA/HAL.
*
* M1 Project
*
*/

#include "m1_capture_transport.h"
#include "m1_crc32c.h"
#include <string.h>

/*--------------------------------- framing ---------------------------------*/

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t m1_cap_frame_encode(uint8_t msg_type, uint16_t session_id, uint32_t sequence,
                           const uint8_t *payload, uint16_t payload_len,
                           uint8_t *dst, size_t dstcap)
{
    uint8_t env[M1_CAP_MAX_ENVELOPE];
    size_t env_len;
    uint32_t crc;
    size_t cobs_len;

    if (payload_len > M1_CAP_MAX_PAYLOAD)
    {
        return 0;
    }
    if (payload_len > 0u && payload == NULL)
    {
        return 0;
    }

    env[0] = M1_CAP_PROTO_VERSION;
    env[1] = msg_type;
    put_u16le(&env[2], session_id);
    put_u32le(&env[4], sequence);
    put_u16le(&env[8], payload_len);
    if (payload_len > 0u)
    {
        memcpy(&env[M1_CAP_HDR_LEN], payload, payload_len);
    }
    env_len = (size_t)M1_CAP_HDR_LEN + payload_len;

    crc = m1_crc32c(env, env_len);
    put_u32le(&env[env_len], crc);
    env_len += M1_CAP_CRC_LEN;

    cobs_len = m1_cobs_encode(env, env_len, dst, dstcap);
    if (cobs_len == 0u || cobs_len + 1u > dstcap)
    {
        return 0;
    }
    dst[cobs_len] = M1_CAP_FRAME_DELIMITER;
    return cobs_len + 1u;
}

m1_cap_status_t m1_cap_frame_decode(const uint8_t *cobs_block, size_t block_len,
                                    uint8_t *decode_buf, size_t decode_cap,
                                    m1_cap_msg_t *out)
{
    size_t env_len;
    uint16_t payload_len;
    uint32_t crc_calc, crc_wire;

    if (cobs_block == NULL || decode_buf == NULL || out == NULL)
    {
        return M1_CAP_ERR_FORMAT;
    }

    env_len = m1_cobs_decode(cobs_block, block_len, decode_buf, decode_cap);
    if (env_len < (size_t)(M1_CAP_HDR_LEN + M1_CAP_CRC_LEN))
    {
        return M1_CAP_ERR_FORMAT;
    }

    payload_len = rd_u16le(&decode_buf[8]);
    if ((size_t)M1_CAP_HDR_LEN + payload_len + M1_CAP_CRC_LEN != env_len)
    {
        return M1_CAP_ERR_FORMAT;
    }

    crc_wire = rd_u32le(&decode_buf[env_len - M1_CAP_CRC_LEN]);
    crc_calc = m1_crc32c(decode_buf, env_len - M1_CAP_CRC_LEN);
    if (crc_calc != crc_wire)
    {
        return M1_CAP_ERR_CRC;
    }

    if (decode_buf[0] != M1_CAP_PROTO_VERSION)
    {
        return M1_CAP_ERR_VERSION;
    }

    out->version = decode_buf[0];
    out->msg_type = decode_buf[1];
    out->session_id = rd_u16le(&decode_buf[2]);
    out->sequence = rd_u32le(&decode_buf[4]);
    out->payload_len = payload_len;
    out->payload = (payload_len > 0u) ? &decode_buf[M1_CAP_HDR_LEN] : NULL;
    return M1_CAP_OK;
}

/*----------------------------- streaming RX --------------------------------*/

void m1_cap_rx_init(m1_cap_rx_stream_t *s)
{
    s->len = 0;
    s->overrun = false;
    s->dropped_frames = 0;
}

size_t m1_cap_rx_push(m1_cap_rx_stream_t *s, uint8_t byte,
                      uint8_t *out_block, size_t out_cap)
{
    if (byte == M1_CAP_FRAME_DELIMITER)
    {
        size_t n = s->len;
        bool bad = s->overrun;
        s->len = 0;
        s->overrun = false;

        if (bad || n == 0u)
        {
            if (bad) { s->dropped_frames++; }
            return 0; /* empty (idle delimiter) or a dropped oversized block */
        }
        if (n > out_cap)
        {
            s->dropped_frames++;
            return 0;
        }
        memcpy(out_block, s->block, n);
        return n;
    }

    if (s->len < sizeof(s->block))
    {
        s->block[s->len++] = byte;
    }
    else
    {
        s->overrun = true; /* keep consuming until the next delimiter */
    }
    return 0;
}

/*--------------------------- sequence tracking -----------------------------*/

void m1_cap_seq_init(m1_cap_seq_t *s)
{
    s->next_expected = 0;
    s->started = false;
    s->gaps = 0;
    s->dups = 0;
}

m1_seq_result_t m1_cap_seq_observe(m1_cap_seq_t *s, uint32_t seq)
{
    if (!s->started)
    {
        s->started = true;
        s->next_expected = seq + 1u;
        return M1_SEQ_OK;
    }
    if (seq == s->next_expected)
    {
        s->next_expected = seq + 1u;
        return M1_SEQ_OK;
    }
    if (seq > s->next_expected)
    {
        s->gaps += (seq - s->next_expected);
        s->next_expected = seq + 1u;
        return M1_SEQ_GAP;
    }
    /* seq < next_expected: duplicate or out-of-order old frame */
    s->dups++;
    return M1_SEQ_DUP;
}

/*---------------------------- byte-credit flow -----------------------------*/

void m1_cap_credit_init(m1_cap_credit_t *c, uint32_t reserved_control)
{
    c->granted = 0;
    c->consumed = 0;
    c->reserved_control = reserved_control;
}

void m1_cap_credit_grant(m1_cap_credit_t *c, uint32_t bytes)
{
    c->granted += bytes;
}

uint32_t m1_cap_credit_available(const m1_cap_credit_t *c)
{
    return (c->granted > c->consumed) ? (c->granted - c->consumed) : 0u;
}

bool m1_cap_credit_can_send_frame(const m1_cap_credit_t *c, uint32_t bytes)
{
    uint32_t avail = m1_cap_credit_available(c);
    if (avail <= c->reserved_control)
    {
        return (bytes == 0u);
    }
    return (bytes <= (avail - c->reserved_control));
}

bool m1_cap_credit_can_send_control(const m1_cap_credit_t *c, uint32_t bytes)
{
    return (bytes <= m1_cap_credit_available(c));
}

void m1_cap_credit_consume(m1_cap_credit_t *c, uint32_t bytes)
{
    c->consumed += bytes;
}

/*------------------------- negotiation state machine -----------------------*/

m1_cap_state_t m1_cap_fsm_next(m1_cap_state_t state, m1_cap_event_t ev)
{
    /* Any active state falls back cleanly to CONSOLE on timeout or error. */
    if ((ev == M1_CAP_EV_TIMEOUT || ev == M1_CAP_EV_ERROR) &&
        state != M1_CAP_STATE_CONSOLE)
    {
        return M1_CAP_STATE_CONSOLE;
    }

    switch (state)
    {
    case M1_CAP_STATE_CONSOLE:
        if (ev == M1_CAP_EV_SEND_OPEN) { return M1_CAP_STATE_OPENING; }
        break;
    case M1_CAP_STATE_OPENING:
        if (ev == M1_CAP_EV_RX_READY) { return M1_CAP_STATE_READY; }
        break;
    case M1_CAP_STATE_READY:
        if (ev == M1_CAP_EV_SEND_START) { return M1_CAP_STATE_RUNNING; }
        break;
    case M1_CAP_STATE_RUNNING:
        if (ev == M1_CAP_EV_SEND_STOP) { return M1_CAP_STATE_STOPPING; }
        break;
    case M1_CAP_STATE_STOPPING:
        if (ev == M1_CAP_EV_RX_STOPPED) { return M1_CAP_STATE_CONSOLE; }
        break;
    default:
        break;
    }
    return state; /* ignore invalid transitions */
}
