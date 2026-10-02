/* See COPYING.txt for license details. */

/*
*
* m1_capture_protocol.h
*
* Shared, versioned wire definitions for the MonstaShark (Wi-Fi Capture)
* ESP32-C6 <-> STM32 binary capture transport. This header is intended to be
* byte-identical on both processors.
*
* Envelope (little-endian), in order, before framing:
*     u8   version        (M1_CAP_PROTO_VERSION)
*     u8   msg_type        (m1_cap_msg_type_t)
*     u16  session_id
*     u32  sequence        (monotonic per session)
*     u16  payload_len
*     u8   payload[payload_len]
*     u32  crc32c          (CRC-32C over version..payload inclusive)
*
* The whole envelope is then COBS-encoded and terminated with a single 0x00
* delimiter (see m1_cobs.h) so the link can resynchronize after corruption.
*
* No C bitfields or compiler-packed structs cross the wire; fields are packed
* and parsed explicitly with defined byte order and bounds.
*
* M1 Project
*
*/

#ifndef M1_CAPTURE_PROTOCOL_H_
#define M1_CAPTURE_PROTOCOL_H_

#include <stdint.h>

#define M1_CAP_PROTO_VERSION   1
#define M1_CAP_FRAME_DELIMITER  0x00u

/* Envelope field sizes. */
#define M1_CAP_HDR_LEN   10u   /* version+type+session+sequence+payload_len */
#define M1_CAP_CRC_LEN    4u
#define M1_CAP_MAX_PAYLOAD 1024u
#define M1_CAP_MAX_ENVELOPE (M1_CAP_HDR_LEN + M1_CAP_MAX_PAYLOAD + M1_CAP_CRC_LEN)

/* Message types (spec section 8.2). */
typedef enum {
    M1_CAP_OPEN          = 1,  /* STM->ESP: request capture link + rate      */
    M1_CAP_READY         = 2,  /* ESP->STM: link up, session id/config       */
    M1_CAP_START         = 3,  /* STM->ESP: begin promiscuous capture        */
    M1_CAP_FRAME_BATCH   = 4,  /* ESP->STM: one or more capture records      */
    M1_CAP_STATS         = 5,  /* ESP->STM: counter snapshot                 */
    M1_CAP_MARK          = 6,  /* STM->ESP: user marker for next frame       */
    M1_CAP_CHANNEL_EVENT = 7,  /* ESP->STM: channel switch/reacquire         */
    M1_CAP_CREDIT        = 8,  /* STM->ESP: grant N bytes of queue capacity  */
    M1_CAP_STOP          = 9,  /* STM->ESP: stop capture, drain              */
    M1_CAP_STOPPED       = 10, /* ESP->STM: last sequence + final counters   */
    M1_CAP_ERROR         = 11  /* either: fatal transport/capture error      */
} m1_cap_msg_type_t;

#endif /* M1_CAPTURE_PROTOCOL_H_ */
