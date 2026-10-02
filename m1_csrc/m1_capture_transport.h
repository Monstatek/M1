/* See COPYING.txt for license details. */

/*
*
* m1_capture_transport.h
*
* Host-testable transport foundation for the MonstaShark capture link:
* envelope framing (COBS + CRC-32C + sequence), a resynchronizing streaming
* receiver, sequence-gap/duplicate tracking, byte-credit flow control, and the
* rate-negotiation state machine. All pure logic — no UART/DMA/HAL here; the
* DMA/high-speed hardware layer is built and validated separately (Gate 0
* hardware phase).
*
* M1 Project
*
*/

#ifndef M1_CAPTURE_TRANSPORT_H_
#define M1_CAPTURE_TRANSPORT_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "m1_capture_protocol.h"
#include "m1_cobs.h"

/*--------------------------------- framing ---------------------------------*/

typedef struct {
    uint8_t        version;
    uint8_t        msg_type;
    uint16_t       session_id;
    uint32_t       sequence;
    const uint8_t *payload;     /* into caller-provided decode buffer */
    uint16_t       payload_len;
} m1_cap_msg_t;

typedef enum {
    M1_CAP_OK = 0,
    M1_CAP_ERR_CRC,      /* CRC-32C mismatch                        */
    M1_CAP_ERR_FORMAT,   /* bad length/COBS/bounds                  */
    M1_CAP_ERR_VERSION   /* unsupported protocol version            */
} m1_cap_status_t;

/* Worst-case wire size for one envelope, including the 0x00 delimiter. */
#define M1_CAP_MAX_WIRE (M1_COBS_MAX_ENCODED(M1_CAP_MAX_ENVELOPE) + 1u)

/* Encode a message to a delimited wire frame in dst. Returns the wire length
 * (including the trailing 0x00), or 0 on overflow/oversized payload. */
size_t m1_cap_frame_encode(uint8_t msg_type, uint16_t session_id, uint32_t sequence,
                           const uint8_t *payload, uint16_t payload_len,
                           uint8_t *dst, size_t dstcap);

/* Decode one COBS block (delimiter already removed) into *out. decode_buf is
 * scratch that must outlive *out (payload points into it). */
m1_cap_status_t m1_cap_frame_decode(const uint8_t *cobs_block, size_t block_len,
                                    uint8_t *decode_buf, size_t decode_cap,
                                    m1_cap_msg_t *out);

/*----------------------------- streaming RX --------------------------------*/

typedef struct {
    uint8_t  block[M1_CAP_MAX_WIRE]; /* accumulates one COBS block            */
    size_t   len;                    /* bytes buffered                        */
    bool     overrun;                /* current block exceeded the buffer     */
    uint32_t dropped_frames;         /* frames lost to overrun                */
} m1_cap_rx_stream_t;

void m1_cap_rx_init(m1_cap_rx_stream_t *s);

/* Push one received byte. When a 0x00 delimiter completes a non-empty block,
 * copies that block (delimiter excluded) into out_block and returns its length
 * (>0). Otherwise returns 0. Oversized blocks are dropped and counted; the
 * next delimiter resynchronizes the stream. */
size_t m1_cap_rx_push(m1_cap_rx_stream_t *s, uint8_t byte,
                      uint8_t *out_block, size_t out_cap);

/*--------------------------- sequence tracking -----------------------------*/

typedef struct {
    uint32_t next_expected;
    bool     started;
    uint32_t gaps;   /* count of missing sequence numbers */
    uint32_t dups;   /* count of duplicate/old sequence numbers */
} m1_cap_seq_t;

typedef enum { M1_SEQ_OK = 0, M1_SEQ_GAP, M1_SEQ_DUP } m1_seq_result_t;

void m1_cap_seq_init(m1_cap_seq_t *s);
m1_seq_result_t m1_cap_seq_observe(m1_cap_seq_t *s, uint32_t seq);

/*---------------------------- byte-credit flow -----------------------------*/

typedef struct {
    uint32_t granted;          /* total bytes granted by the STM32 */
    uint32_t consumed;         /* total bytes the ESP has sent      */
    uint32_t reserved_control; /* bytes reserved so control always sends */
} m1_cap_credit_t;

void     m1_cap_credit_init(m1_cap_credit_t *c, uint32_t reserved_control);
void     m1_cap_credit_grant(m1_cap_credit_t *c, uint32_t bytes);
uint32_t m1_cap_credit_available(const m1_cap_credit_t *c);
/* Frame batches must leave the reserved control pool intact. */
bool     m1_cap_credit_can_send_frame(const m1_cap_credit_t *c, uint32_t bytes);
/* Control messages may draw on the whole available pool. */
bool     m1_cap_credit_can_send_control(const m1_cap_credit_t *c, uint32_t bytes);
void     m1_cap_credit_consume(m1_cap_credit_t *c, uint32_t bytes);

/*------------------------- negotiation state machine -----------------------*/

typedef enum {
    M1_CAP_STATE_CONSOLE = 0, /* ordinary ASCII console rate            */
    M1_CAP_STATE_OPENING,     /* CAP_OPEN sent, awaiting CAP_READY      */
    M1_CAP_STATE_READY,       /* link up, buffers not yet armed         */
    M1_CAP_STATE_RUNNING,     /* capturing                              */
    M1_CAP_STATE_STOPPING     /* CAP_STOP sent, draining to CAP_STOPPED */
} m1_cap_state_t;

typedef enum {
    M1_CAP_EV_SEND_OPEN = 0,
    M1_CAP_EV_RX_READY,
    M1_CAP_EV_SEND_START,
    M1_CAP_EV_SEND_STOP,
    M1_CAP_EV_RX_STOPPED,
    M1_CAP_EV_TIMEOUT,
    M1_CAP_EV_ERROR
} m1_cap_event_t;

/* STM32-side negotiation transition. TIMEOUT or ERROR from any active state
 * returns cleanly to CONSOLE (restore the ordinary rate, no capture file).
 * Invalid event/state pairs leave the state unchanged. */
m1_cap_state_t m1_cap_fsm_next(m1_cap_state_t state, m1_cap_event_t ev);

#endif /* M1_CAPTURE_TRANSPORT_H_ */
