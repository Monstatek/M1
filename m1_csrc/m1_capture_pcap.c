/* See COPYING.txt for license details. */

/*
*
* m1_capture_pcap.c
*
* CAP_FRAME_BATCH -> PCAPNG EPB pipeline (see m1_capture_pcap.h). Pure logic,
* host-tested.
*
* M1 Project
*
*/

#include "m1_capture_pcap.h"
#include "m1_radiotap.h"

#include <string.h>

void m1_capture_pcap_radiotap_from_meta(const m1_capframe_meta_t *meta,
                                        m1_radiotap_info_t *rt)
{
    uint16_t freq;

    memset(rt, 0, sizeof(*rt));

    freq = m1_radiotap_chan_to_freq(meta->primary_channel);
    if (freq != 0u)
    {
        rt->has_channel = true;
        rt->channel_freq_mhz = freq;
        /* 11b is DSSS/CCK; everything else the C6 reports here is OFDM. */
        rt->channel_flags = (uint16_t)(M1_RADIOTAP_CHAN_2GHZ |
            ((meta->phy_mode == M1_CAPFRAME_PHY_11B) ? M1_RADIOTAP_CHAN_CCK
                                                     : M1_RADIOTAP_CHAN_OFDM));
    }

    /* RSSI is always available and useful; emit it as the dBm antenna signal. */
    rt->has_dbm_signal = true;
    rt->dbm_signal = meta->rssi_dbm;
}

int m1_capture_pcap_frame(m1_pcapng_writer_t *w, uint32_t iface_id,
                          const m1_capframe_meta_t *meta, const uint8_t *raw,
                          uint8_t *scratch, size_t scratch_cap)
{
    m1_radiotap_info_t rt;
    size_t rt_len;
    size_t total;

    m1_capture_pcap_radiotap_from_meta(meta, &rt);

    rt_len = m1_radiotap_build(&rt, scratch, scratch_cap);
    if (rt_len == 0u)
    {
        return -1;
    }
    if ((size_t)rt_len + meta->cap_len > scratch_cap)
    {
        return -1;
    }
    if (meta->cap_len > 0u && raw != NULL)
    {
        memcpy(&scratch[rt_len], raw, meta->cap_len);
    }
    total = rt_len + meta->cap_len;

    /* orig_len (on-air length) = radiotap + the original 802.11 length, so a
     * truncated capture still reports the true frame size to the analyzer. */
    return m1_pcapng_write_epb(w, iface_id, meta->ts_usec,
                               (uint32_t)(rt_len + meta->orig_len),
                               scratch, (uint32_t)total, NULL);
}

uint32_t m1_capture_pcap_batch(m1_pcapng_writer_t *w, uint32_t iface_id,
                               const uint8_t *payload, uint16_t payload_len,
                               uint8_t *scratch, size_t scratch_cap,
                               m1_capture_pcap_counts_t *counts)
{
    size_t off = 0;
    uint32_t appended = 0;

    if (payload == NULL)
    {
        return 0;
    }

    while (off < payload_len)
    {
        m1_capframe_meta_t meta;
        const uint8_t *raw = NULL;
        size_t rec_len;
        size_t before;

        if (!m1_capframe_decode(&payload[off], (size_t)payload_len - off, &meta, &raw))
        {
            counts->decode_errors++;
            break; /* desynced: rest of the batch is unreliable */
        }
        rec_len = (size_t)M1_CAPFRAME_HDR_LEN + meta.cap_len;

        before = w->len;
        if (m1_capture_pcap_frame(w, iface_id, &meta, raw, scratch, scratch_cap) == 0)
        {
            counts->frames++;
            counts->epb_bytes += (uint64_t)(w->len - before);
            appended++;
        }
        else
        {
            counts->overflow++;
            /* Leave the batch: the caller must flush the writer and can retry. */
            break;
        }

        off += rec_len;
    }

    return appended;
}
