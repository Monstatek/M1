/* See COPYING.txt for license details. */

/*
*
* m1_radiotap.c
*
* Minimal Radiotap header builder (see m1_radiotap.h). Pure, host-tested.
*
* M1 Project
*
*/

#include "m1_radiotap.h"

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

uint16_t m1_radiotap_chan_to_freq(uint8_t channel)
{
    if (channel >= 1u && channel <= 13u)
    {
        return (uint16_t)(2407u + (5u * (uint16_t)channel));
    }
    if (channel == 14u)
    {
        return 2484u;
    }
    return 0u;
}

size_t m1_radiotap_build(const m1_radiotap_info_t *info, uint8_t *out, size_t out_cap)
{
    uint32_t present = 0;
    size_t off;

    if (info == NULL || out == NULL)
    {
        return 0;
    }
    if (!info->has_channel && !info->has_dbm_signal)
    {
        return 0; /* nothing to describe */
    }

    if (info->has_channel)
    {
        present |= (1u << M1_RADIOTAP_BIT_CHANNEL);
    }
    if (info->has_dbm_signal)
    {
        present |= (1u << M1_RADIOTAP_BIT_DBM_ANTSIGNAL);
    }

    /* Fixed 8-byte radiotap preamble: version(1) pad(1) len(2) present(4). The
     * Channel field (offset 8) is naturally 2-byte aligned; the 1-byte signal
     * field follows with no padding needed. */
    if (out_cap < 8u)
    {
        return 0;
    }
    off = 8u;

    if (info->has_channel)
    {
        if (out_cap < off + 4u)
        {
            return 0;
        }
        put_u16le(&out[off], info->channel_freq_mhz);
        put_u16le(&out[off + 2u], info->channel_flags);
        off += 4u;
    }

    if (info->has_dbm_signal)
    {
        if (out_cap < off + 1u)
        {
            return 0;
        }
        out[off] = (uint8_t)info->dbm_signal;
        off += 1u;
    }

    /* Now fill the preamble with the final length. */
    out[0] = 0u;                       /* it_version */
    out[1] = 0u;                       /* it_pad     */
    put_u16le(&out[2], (uint16_t)off); /* it_len     */
    put_u32le(&out[4], present);       /* it_present */

    return off;
}
