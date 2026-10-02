/* See COPYING.txt for license details. */

/*
*
* m1_capture_pcap.h
*
* MonstaShark (Wi-Fi Capture) consumer core: turn received CAP_FRAME_BATCH
* payloads into PCAPNG Enhanced Packet Blocks. Each m1_capframe record (see
* m1_capture_frame.h) is decoded, a Radiotap header is built from its metadata
* (channel + dBm signal — see m1_radiotap.h), and an EPB of [radiotap][raw
* 802.11] is appended to a caller-owned PCAPNG writer (see m1_pcapng.h).
*
* Pure logic: NO RTOS / HAL / FatFs. The hardware side (m1_capture_link.c)
* drains the DMA ring, hands each batch payload here, and flushes the writer to
* SD between blocks. This split keeps the decode/encode pipeline host-testable.
*
* M1 Project
*
*/

#ifndef M1_CAPTURE_PCAP_H_
#define M1_CAPTURE_PCAP_H_

#include <stdint.h>
#include <stddef.h>

#include "m1_pcapng.h"
#include "m1_radiotap.h"
#include "m1_capture_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scratch must hold one assembled EPB payload: radiotap header + raw frame. */
#define M1_CAPTURE_PCAP_SCRATCH_MIN  (M1_RADIOTAP_MAX_LEN + M1_CAPFRAME_MAX_RAW)

/* Running counts over a capture session. */
typedef struct {
    uint32_t frames;         /* capframes turned into EPBs                    */
    uint64_t epb_bytes;      /* pcapng bytes appended for those EPBs          */
    uint32_t decode_errors;  /* malformed capframe records                   */
    uint32_t overflow;       /* EPB appends that overflowed the writer buffer */
} m1_capture_pcap_counts_t;

/* Fill a Radiotap info struct from a decoded capframe's metadata. Emits the
 * 2.4 GHz channel (CCK for 11b, otherwise OFDM) and the dBm antenna signal. */
void m1_capture_pcap_radiotap_from_meta(const m1_capframe_meta_t *meta,
                                        m1_radiotap_info_t *rt);

/* Append one decoded capframe as an EPB. `scratch`/`scratch_cap` assemble the
 * [radiotap][raw] payload (scratch_cap must be >= M1_RADIOTAP_MAX_LEN +
 * meta->cap_len). Returns 0 on success, -1 if the writer overflowed or scratch
 * was too small. */
int m1_capture_pcap_frame(m1_pcapng_writer_t *w, uint32_t iface_id,
                          const m1_capframe_meta_t *meta, const uint8_t *raw,
                          uint8_t *scratch, size_t scratch_cap);

/* Decode a whole CAP_FRAME_BATCH payload (one or more concatenated capframe
 * records) and append each as an EPB. Stops at the first malformed record.
 * Counters in *counts are ACCUMULATED (initialize once before the session).
 * Returns the number of frames appended from this batch. */
uint32_t m1_capture_pcap_batch(m1_pcapng_writer_t *w, uint32_t iface_id,
                               const uint8_t *payload, uint16_t payload_len,
                               uint8_t *scratch, size_t scratch_cap,
                               m1_capture_pcap_counts_t *counts);

#ifdef __cplusplus
}
#endif

#endif /* M1_CAPTURE_PCAP_H_ */
