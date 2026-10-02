/* See COPYING.txt for license details. */

/*
*
* m1_capture_frame.h
*
* Normalized per-frame capture record for MonstaShark (Wi-Fi Capture). This is
* the payload the ESP32-C6 packs for each promiscuous frame and batches into a
* CAP_FRAME_BATCH transport envelope; the STM32 decodes it and turns it into a
* Radiotap header + Enhanced Packet Block.
*
* Only stable, explicitly mapped metadata crosses the wire (spec section 7.3).
* The compiler's packed wifi_pkt_rx_ctrl_t is NEVER sent — its layout is target-
* and IDF-dependent. Fields are packed and parsed explicitly, little-endian,
* with bounds checks. Pure logic, host-tested, shared byte-identical by both
* processors.
*
* Wire layout (little-endian), fixed 24-byte header then cap_len raw bytes:
*     u8   version        (M1_CAPFRAME_VERSION)
*     u8   pkt_class       (m1_capframe_class_t)
*     u8   primary_channel (1..14)
*     u8   sec_channel     (m1_capframe_sec_t)
*     s8   rssi_dbm
*     u8   phy_mode        (m1_capframe_phy_t)
*     u8   rate_500kbps    (legacy rate in 500 kbps units; 0 if not valid)
*     u8   flags           (M1_CAPFRAME_FLAG_*)
*     u16  orig_len        (on-air 802.11 length)
*     u16  cap_len         (captured bytes that follow this header)
*     u32  frame_seq       (monotonic per-session frame index)
*     u64  ts_usec         (unwrapped 64-bit receive timestamp, microseconds)
*     u8   frame[cap_len]  (raw 802.11 MAC frame)
*
* M1 Project
*
*/

#ifndef M1_CAPTURE_FRAME_H_
#define M1_CAPTURE_FRAME_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define M1_CAPFRAME_VERSION   1
#define M1_CAPFRAME_HDR_LEN   24u
#define M1_CAPFRAME_MAX_RAW   2304u  /* max 802.11 MAC frame (incl. QoS/HT) */
#define M1_CAPFRAME_MAX_LEN   (M1_CAPFRAME_HDR_LEN + M1_CAPFRAME_MAX_RAW)

/* ESP promiscuous packet class (maps from wifi_promiscuous_pkt_type_t). */
typedef enum {
    M1_CAPFRAME_CLASS_MGMT = 0,
    M1_CAPFRAME_CLASS_CTRL = 1,
    M1_CAPFRAME_CLASS_DATA = 2,
    M1_CAPFRAME_CLASS_MISC = 3
} m1_capframe_class_t;

/* Secondary channel offset (maps from wifi_second_chan_t). */
typedef enum {
    M1_CAPFRAME_SEC_NONE  = 0,
    M1_CAPFRAME_SEC_ABOVE = 1,
    M1_CAPFRAME_SEC_BELOW = 2
} m1_capframe_sec_t;

/* PHY mode. Only set to a specific value when the C6 reports it reliably;
 * otherwise UNKNOWN (the STM omits the corresponding Radiotap fields). */
typedef enum {
    M1_CAPFRAME_PHY_UNKNOWN = 0,
    M1_CAPFRAME_PHY_11B = 1,
    M1_CAPFRAME_PHY_11G = 2,
    M1_CAPFRAME_PHY_11N = 3,
    M1_CAPFRAME_PHY_11AX = 4
} m1_capframe_phy_t;

/* flags */
#define M1_CAPFRAME_FLAG_FCS_PRESENT 0x01u
#define M1_CAPFRAME_FLAG_FCS_ERROR   0x02u
#define M1_CAPFRAME_FLAG_TRUNCATED   0x04u

typedef struct {
    uint8_t  version;
    uint8_t  pkt_class;
    uint8_t  primary_channel;
    uint8_t  sec_channel;
    int8_t   rssi_dbm;
    uint8_t  phy_mode;
    uint8_t  rate_500kbps;
    uint8_t  flags;
    uint16_t orig_len;
    uint16_t cap_len;
    uint32_t frame_seq;
    uint64_t ts_usec;
} m1_capframe_meta_t;

/* Encode meta + raw[cap_len] into out. meta->cap_len is authoritative and must
 * be <= M1_CAPFRAME_MAX_RAW. Returns total bytes written (HDR + cap_len), or 0
 * on overflow / invalid length. version is set for the caller. */
size_t m1_capframe_encode(const m1_capframe_meta_t *meta, const uint8_t *raw,
                          uint8_t *out, size_t out_cap);

/* Decode a record from buf[0..len). Fills *meta and points *raw_out at the raw
 * frame inside buf (no copy). Returns true on success; false on short buffer,
 * bad version, or cap_len exceeding the available bytes. */
bool m1_capframe_decode(const uint8_t *buf, size_t len,
                        m1_capframe_meta_t *meta, const uint8_t **raw_out);

#endif /* M1_CAPTURE_FRAME_H_ */
