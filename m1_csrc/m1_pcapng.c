/* See COPYING.txt for license details. */

/*
*
* m1_pcapng.c
*
* PCAPNG writer (see m1_pcapng.h). Pure, host-tested. Little-endian.
*
* Block lengths are back-patched: each block writes a placeholder total length,
* appends its body, then fills in the leading length and the mirrored trailing
* length. Appends past the buffer set the overflow flag but still advance the
* logical length, so total-length fields stay self-consistent whenever the
* buffer was large enough.
*
* M1 Project
*
*/

#include "m1_pcapng.h"
#include <string.h>

/* Block types. */
#define BT_SHB 0x0A0D0D0Au
#define BT_IDB 0x00000001u
#define BT_EPB 0x00000006u
#define BT_ISB 0x00000005u

#define SHB_BYTE_ORDER_MAGIC 0x1A2B3C4Du
#define SECTION_LENGTH_UNSPECIFIED 0xFFFFFFFFFFFFFFFFULL

/* Option codes. */
#define OPT_ENDOFOPT      0
#define OPT_COMMENT       1
#define OPT_SHB_HARDWARE  2
#define OPT_SHB_OS        3
#define OPT_SHB_USERAPPL  4
#define OPT_IF_NAME       2
#define OPT_IF_DESCRIPTION 3
#define OPT_IF_TSRESOL    9
#define OPT_ISB_STARTTIME 2
#define OPT_ISB_ENDTIME   3
#define OPT_ISB_IFRECV    4
#define OPT_ISB_IFDROP    5
#define OPT_ISB_FILTERACCEPT 6
#define OPT_ISB_OSDROP    7
#define OPT_ISB_USRDELIV  8

/*************************** low-level append *********************************/

static void wr_bytes(m1_pcapng_writer_t *w, const void *src, size_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; i++)
    {
        size_t pos = w->len + i;
        if (pos < w->cap)
        {
            w->buf[pos] = (s != NULL) ? s[i] : 0u;
        }
        else
        {
            w->overflow = true;
        }
    }
    w->len += n;
}

static void wr_u8(m1_pcapng_writer_t *w, uint8_t v)  { wr_bytes(w, &v, 1); }

static void wr_u16(m1_pcapng_writer_t *w, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    wr_bytes(w, b, 2);
}

static void wr_u32(m1_pcapng_writer_t *w, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
    wr_bytes(w, b, 4);
}

static void wr_u64(m1_pcapng_writer_t *w, uint64_t v)
{
    wr_u32(w, (uint32_t)(v & 0xFFFFFFFFu));
    wr_u32(w, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static void wr_pad32(m1_pcapng_writer_t *w)
{
    while ((w->len & 3u) != 0u)
    {
        wr_u8(w, 0u);
    }
}

static void wr_u32_at(m1_pcapng_writer_t *w, size_t pos, uint32_t v)
{
    if (pos + 4u <= w->cap)
    {
        w->buf[pos]     = (uint8_t)(v & 0xFFu);
        w->buf[pos + 1] = (uint8_t)((v >> 8) & 0xFFu);
        w->buf[pos + 2] = (uint8_t)((v >> 16) & 0xFFu);
        w->buf[pos + 3] = (uint8_t)((v >> 24) & 0xFFu);
    }
}

/*************************** options *****************************************/

static void wr_opt(m1_pcapng_writer_t *w, uint16_t code, const void *val, uint16_t vlen)
{
    wr_u16(w, code);
    wr_u16(w, vlen);
    wr_bytes(w, val, vlen);
    wr_pad32(w);
}

static bool wr_opt_str(m1_pcapng_writer_t *w, uint16_t code, const char *s)
{
    size_t n;
    if (s == NULL || s[0] == '\0')
    {
        return false;
    }
    n = strlen(s);
    if (n > 0xFFFFu)
    {
        n = 0xFFFFu;
    }
    wr_opt(w, code, s, (uint16_t)n);
    return true;
}

static void wr_opt_u64(m1_pcapng_writer_t *w, uint16_t code, uint64_t v)
{
    uint8_t b[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        b[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
    wr_opt(w, code, b, 8);
}

/* PCAPNG timestamp-format option: high 32 bits then low 32 bits (each LE). */
static void wr_opt_ts(m1_pcapng_writer_t *w, uint16_t code, uint64_t usec)
{
    uint8_t b[8];
    uint32_t hi = (uint32_t)((usec >> 32) & 0xFFFFFFFFu);
    uint32_t lo = (uint32_t)(usec & 0xFFFFFFFFu);
    b[0] = (uint8_t)(hi & 0xFFu); b[1] = (uint8_t)((hi >> 8) & 0xFFu);
    b[2] = (uint8_t)((hi >> 16) & 0xFFu); b[3] = (uint8_t)((hi >> 24) & 0xFFu);
    b[4] = (uint8_t)(lo & 0xFFu); b[5] = (uint8_t)((lo >> 8) & 0xFFu);
    b[6] = (uint8_t)((lo >> 16) & 0xFFu); b[7] = (uint8_t)((lo >> 24) & 0xFFu);
    wr_opt(w, code, b, 8);
}

static void wr_opt_end(m1_pcapng_writer_t *w)
{
    wr_u16(w, OPT_ENDOFOPT);
    wr_u16(w, 0);
}

/*************************** block framing ***********************************/

static size_t block_begin(m1_pcapng_writer_t *w, uint32_t type)
{
    size_t start = w->len;
    wr_u32(w, type);
    wr_u32(w, 0u); /* placeholder for total length */
    return start;
}

static int block_end(m1_pcapng_writer_t *w, size_t start)
{
    uint32_t total;
    wr_pad32(w);
    total = (uint32_t)(w->len - start) + 4u; /* + trailing length field */
    wr_u32(w, total);                        /* trailing total length   */
    wr_u32_at(w, start + 4u, total);         /* patch leading length    */
    return w->overflow ? -1 : 0;
}

/*************************** public API **************************************/

void m1_pcapng_writer_init(m1_pcapng_writer_t *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = false;
}

bool m1_pcapng_writer_ok(const m1_pcapng_writer_t *w)
{
    return (w != NULL) && !w->overflow;
}

int m1_pcapng_write_shb(m1_pcapng_writer_t *w, const m1_pcapng_section_info_t *s)
{
    size_t st = block_begin(w, BT_SHB);
    bool any = false;

    wr_u32(w, SHB_BYTE_ORDER_MAGIC);
    wr_u16(w, 1); /* major */
    wr_u16(w, 0); /* minor */
    wr_u64(w, SECTION_LENGTH_UNSPECIFIED);

    if (s != NULL)
    {
        any |= wr_opt_str(w, OPT_COMMENT, s->comment);
        any |= wr_opt_str(w, OPT_SHB_HARDWARE, s->shb_hardware);
        any |= wr_opt_str(w, OPT_SHB_OS, s->shb_os);
        any |= wr_opt_str(w, OPT_SHB_USERAPPL, s->shb_userappl);
    }
    if (any)
    {
        wr_opt_end(w);
    }
    return block_end(w, st);
}

int m1_pcapng_write_idb(m1_pcapng_writer_t *w, const m1_pcapng_iface_info_t *i)
{
    size_t st = block_begin(w, BT_IDB);
    uint8_t tsresol;

    wr_u16(w, M1_PCAPNG_LINKTYPE_IEEE802_11_RADIOTAP);
    wr_u16(w, 0); /* reserved */
    wr_u32(w, (i != NULL) ? i->snaplen : 0u);

    if (i != NULL)
    {
        (void)wr_opt_str(w, OPT_IF_NAME, i->if_name);
        (void)wr_opt_str(w, OPT_IF_DESCRIPTION, i->if_description);
        tsresol = (i->tsresol != 0u) ? i->tsresol : 6u;
    }
    else
    {
        tsresol = 6u;
    }
    wr_opt(w, OPT_IF_TSRESOL, &tsresol, 1); /* microseconds by default */
    wr_opt_end(w);
    return block_end(w, st);
}

int m1_pcapng_write_epb(m1_pcapng_writer_t *w,
                        uint32_t iface_id,
                        uint64_t ts_usec,
                        uint32_t orig_len,
                        const uint8_t *packet_data,
                        uint32_t captured_len,
                        const char *comment)
{
    size_t st = block_begin(w, BT_EPB);

    wr_u32(w, iface_id);
    wr_u32(w, (uint32_t)((ts_usec >> 32) & 0xFFFFFFFFu)); /* timestamp high */
    wr_u32(w, (uint32_t)(ts_usec & 0xFFFFFFFFu));         /* timestamp low  */
    wr_u32(w, captured_len);
    wr_u32(w, orig_len);
    wr_bytes(w, packet_data, captured_len);
    wr_pad32(w); /* pad packet data to a 32-bit boundary */

    if (comment != NULL && comment[0] != '\0')
    {
        (void)wr_opt_str(w, OPT_COMMENT, comment);
        wr_opt_end(w);
    }
    return block_end(w, st);
}

int m1_pcapng_write_isb(m1_pcapng_writer_t *w,
                        uint32_t iface_id,
                        uint64_t ts_usec,
                        const m1_pcapng_stats_t *st_stats)
{
    size_t st = block_begin(w, BT_ISB);
    bool any = false;

    wr_u32(w, iface_id);
    wr_u32(w, (uint32_t)((ts_usec >> 32) & 0xFFFFFFFFu));
    wr_u32(w, (uint32_t)(ts_usec & 0xFFFFFFFFu));

    if (st_stats != NULL)
    {
        if (st_stats->has_starttime)    { wr_opt_ts(w, OPT_ISB_STARTTIME, st_stats->isb_starttime_usec); any = true; }
        if (st_stats->has_endtime)      { wr_opt_ts(w, OPT_ISB_ENDTIME, st_stats->isb_endtime_usec); any = true; }
        if (st_stats->has_ifrecv)       { wr_opt_u64(w, OPT_ISB_IFRECV, st_stats->isb_ifrecv); any = true; }
        if (st_stats->has_ifdrop)       { wr_opt_u64(w, OPT_ISB_IFDROP, st_stats->isb_ifdrop); any = true; }
        if (st_stats->has_filteraccept) { wr_opt_u64(w, OPT_ISB_FILTERACCEPT, st_stats->isb_filteraccept); any = true; }
        if (st_stats->has_osdrop)       { wr_opt_u64(w, OPT_ISB_OSDROP, st_stats->isb_osdrop); any = true; }
        if (st_stats->has_usrdeliv)     { wr_opt_u64(w, OPT_ISB_USRDELIV, st_stats->isb_usrdeliv); any = true; }
    }
    if (any)
    {
        wr_opt_end(w);
    }
    return block_end(w, st);
}
