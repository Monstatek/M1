/* See COPYING.txt for license details. */

/*
*
* m1_pcapng.h
*
* PCAPNG writer for MonstaShark (Wi-Fi Capture): Section Header Block,
* Interface Description Block, Enhanced Packet Blocks, and Interface
* Statistics Block, using LINKTYPE_IEEE802_11_RADIOTAP (127).
*
* Little-endian, standards-compliant, buffer-based (no malloc, no RTOS/HAL) so
* it is exercised by host golden tests independent of hardware. The caller owns
* the output buffer; every writer appends and reports overflow rather than
* writing past the end.
*
* Reference: IETF draft-ietf-opsawg-pcapng (SHB/IDB/EPB/ISB, options,
* timestamp resolution). Each EPB packet payload is: [radiotap header][raw
* 802.11 frame]. See m1_radiotap.h.
*
* M1 Project
*
*/

#ifndef M1_PCAPNG_H_
#define M1_PCAPNG_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* IETF/tcpdump link type for radiotap-prefixed 802.11. */
#define M1_PCAPNG_LINKTYPE_IEEE802_11_RADIOTAP 127

typedef struct {
    uint8_t *buf;      /* caller-owned output buffer            */
    size_t   cap;      /* buffer capacity                       */
    size_t   len;      /* bytes written so far                  */
    bool     overflow; /* set once a write would exceed cap     */
} m1_pcapng_writer_t;

/* Section Header Block options (all optional; NULL/empty => omitted). */
typedef struct {
    const char *shb_hardware;  /* e.g. "MonstaTek M1 / ESP32-C6"          */
    const char *shb_os;        /* firmware identity                       */
    const char *shb_userappl;  /* e.g. "MonstaTek M1 Wi-Fi Capture"       */
    const char *comment;       /* opt_comment: profile, clock note, etc.  */
} m1_pcapng_section_info_t;

/* Interface Description Block options. */
typedef struct {
    uint32_t    snaplen;        /* 0 = no limit                           */
    const char *if_name;        /* e.g. "esp32c6-monitor"                 */
    const char *if_description; /* may be NULL                            */
    uint8_t     tsresol;        /* if_tsresol; 6 = microseconds           */
} m1_pcapng_iface_info_t;

/* Interface Statistics Block counters (PCAPNG isb_* options). */
typedef struct {
    bool     has_starttime;
    uint64_t isb_starttime_usec;
    bool     has_endtime;
    uint64_t isb_endtime_usec;
    bool     has_ifrecv;      uint64_t isb_ifrecv;      /* frames received      */
    bool     has_ifdrop;      uint64_t isb_ifdrop;      /* frames dropped       */
    bool     has_filteraccept;uint64_t isb_filteraccept;/* accepted by filter   */
    bool     has_osdrop;      uint64_t isb_osdrop;      /* dropped by OS/link    */
    bool     has_usrdeliv;    uint64_t isb_usrdeliv;    /* delivered to user     */
} m1_pcapng_stats_t;

/* Initialize a writer over a caller-owned buffer. */
void m1_pcapng_writer_init(m1_pcapng_writer_t *w, uint8_t *buf, size_t cap);

/* True if any append has overflowed the buffer (file is incomplete). */
bool m1_pcapng_writer_ok(const m1_pcapng_writer_t *w);

/* Append the four block types. Return 0 on success, -1 on overflow. */
int m1_pcapng_write_shb(m1_pcapng_writer_t *w, const m1_pcapng_section_info_t *s);
int m1_pcapng_write_idb(m1_pcapng_writer_t *w, const m1_pcapng_iface_info_t *i);

/* Enhanced Packet Block. packet_data is [radiotap][802.11] of captured_len
 * bytes; orig_len is the on-air length (>= captured_len for truncated frames).
 * ts_usec is in the units declared by if_tsresol (microseconds when tsresol=6).
 * comment (may be NULL) is attached as an opt_comment (used for markers). */
int m1_pcapng_write_epb(m1_pcapng_writer_t *w,
                        uint32_t iface_id,
                        uint64_t ts_usec,
                        uint32_t orig_len,
                        const uint8_t *packet_data,
                        uint32_t captured_len,
                        const char *comment);

/* Interface Statistics Block (written at graceful stop / recovery). */
int m1_pcapng_write_isb(m1_pcapng_writer_t *w,
                        uint32_t iface_id,
                        uint64_t ts_usec,
                        const m1_pcapng_stats_t *st);

#endif /* M1_PCAPNG_H_ */
