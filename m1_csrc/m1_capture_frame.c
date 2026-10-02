/* See COPYING.txt for license details. */

/*
*
* m1_capture_frame.c
*
* Normalized per-frame capture record codec (see m1_capture_frame.h).
* Pure, host-tested. Shared byte-identical by both processors.
*
* M1 Project
*
*/

#include "m1_capture_frame.h"
#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static void put_u64(uint8_t *p, uint64_t v)
{
    put_u32(&p[0], (uint32_t)(v & 0xFFFFFFFFu));
    put_u32(&p[4], (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(&p[0]) | ((uint64_t)get_u32(&p[4]) << 32);
}

size_t m1_capframe_encode(const m1_capframe_meta_t *meta, const uint8_t *raw,
                          uint8_t *out, size_t out_cap)
{
    size_t total;

    if (meta == NULL || out == NULL)
    {
        return 0;
    }
    if (meta->cap_len > M1_CAPFRAME_MAX_RAW)
    {
        return 0;
    }
    if (meta->cap_len > 0u && raw == NULL)
    {
        return 0;
    }
    total = (size_t)M1_CAPFRAME_HDR_LEN + meta->cap_len;
    if (total > out_cap)
    {
        return 0;
    }

    out[0] = M1_CAPFRAME_VERSION;
    out[1] = meta->pkt_class;
    out[2] = meta->primary_channel;
    out[3] = meta->sec_channel;
    out[4] = (uint8_t)meta->rssi_dbm;
    out[5] = meta->phy_mode;
    out[6] = meta->rate_500kbps;
    out[7] = meta->flags;
    put_u16(&out[8], meta->orig_len);
    put_u16(&out[10], meta->cap_len);
    put_u32(&out[12], meta->frame_seq);
    put_u64(&out[16], meta->ts_usec);

    if (meta->cap_len > 0u)
    {
        memcpy(&out[M1_CAPFRAME_HDR_LEN], raw, meta->cap_len);
    }
    return total;
}

bool m1_capframe_decode(const uint8_t *buf, size_t len,
                        m1_capframe_meta_t *meta, const uint8_t **raw_out)
{
    uint16_t cap_len;

    if (buf == NULL || meta == NULL)
    {
        return false;
    }
    if (len < (size_t)M1_CAPFRAME_HDR_LEN)
    {
        return false;
    }
    if (buf[0] != M1_CAPFRAME_VERSION)
    {
        return false;
    }

    cap_len = get_u16(&buf[10]);
    if (cap_len > M1_CAPFRAME_MAX_RAW)
    {
        return false;
    }
    if ((size_t)M1_CAPFRAME_HDR_LEN + cap_len > len)
    {
        return false;
    }

    meta->version = buf[0];
    meta->pkt_class = buf[1];
    meta->primary_channel = buf[2];
    meta->sec_channel = buf[3];
    meta->rssi_dbm = (int8_t)buf[4];
    meta->phy_mode = buf[5];
    meta->rate_500kbps = buf[6];
    meta->flags = buf[7];
    meta->orig_len = get_u16(&buf[8]);
    meta->cap_len = cap_len;
    meta->frame_seq = get_u32(&buf[12]);
    meta->ts_usec = get_u64(&buf[16]);

    if (raw_out != NULL)
    {
        *raw_out = (cap_len > 0u) ? &buf[M1_CAPFRAME_HDR_LEN] : NULL;
    }
    return true;
}
