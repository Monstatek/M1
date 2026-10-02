/*
 * m1_capture_scan_parse.c - see m1_capture_scan_parse.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "m1_capture_scan_parse.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* Bounded-length view over [start,end) -- never dereferences at or past end. */
typedef struct {
    const char *start;
    const char *end;
} m1_cap_span_t;

static bool is_digit_c(char c) { return c >= '0' && c <= '9'; }

/* True if buf[pos..pos+17) within [0,end) matches "XX:XX:XX:XX:XX:XX"
 * (hex, case-insensitive; ':' at the five fixed offsets). */
static bool mac_matches_at(const char *p, const char *end)
{
    static const int colon_at[5] = { 2, 5, 8, 11, 14 };
    int i;

    if ((p + 17) > end) { return false; }

    for (i = 0; i < 17; i++)
    {
        bool want_colon = false;
        int k;
        for (k = 0; k < 5; k++) { if (colon_at[k] == i) { want_colon = true; break; } }
        if (want_colon)
        {
            if (p[i] != ':') { return false; }
        }
        else
        {
            if (!isxdigit((unsigned char)p[i])) { return false; }
        }
    }
    return true;
}

/* First MAC-pattern match at or after `from`, strictly before `end`. NULL if none. */
static const char *find_mac(const char *from, const char *end)
{
    const char *p;
    for (p = from; p + 17 <= end; p++)
    {
        if (mac_matches_at(p, end)) { return p; }
    }
    return NULL;
}

/* Scan backward from `from` (exclusive) over whitespace then decimal digits
 * (with an optional leading '-'), staying within [line_start,from). Returns
 * the digit-span [ds,de) or {NULL,NULL} if no digits were found. */
static m1_cap_span_t scan_back_number(const char *line_start, const char *from)
{
    const char *p = from;
    m1_cap_span_t none = { NULL, NULL };
    const char *de;
    const char *ds;

    while (p > line_start && isspace((unsigned char)*(p - 1))) { p--; }
    de = p;
    while (p > line_start && is_digit_c(*(p - 1))) { p--; }
    ds = p;
    if (ds == de) { return none; }
    if (ds > line_start && *(ds - 1) == '-') { ds--; }

    {
        m1_cap_span_t s;
        s.start = ds;
        s.end = de;
        return s;
    }
}

static int span_to_int(m1_cap_span_t s)
{
    char tmp[16];
    size_t n = (size_t)(s.end - s.start);
    if (n >= sizeof(tmp)) { n = sizeof(tmp) - 1u; }
    memcpy(tmp, s.start, n);
    tmp[n] = '\0';
    return (int)strtol(tmp, NULL, 10);
}

static void copy_bounded(char *dst, size_t dst_cap, const char *src, size_t src_len)
{
    size_t n = src_len;
    if (n >= dst_cap) { n = dst_cap - 1u; }
    if (n > 0u) { memcpy(dst, src, n); }
    dst[n] = '\0';
}

/* Locates the "N AP(s) found" sentinel anywhere in [buf,buf+len). On success,
 * *reported is the parsed N and the return value points just past the
 * sentinel line's newline (or buf+len if the buffer ends there). NULL if the
 * sentinel text is not present at all. */
static const char *find_sentinel(const char *buf, uint32_t len, uint32_t *reported)
{
    static const char needle[] = "AP(s) found";
    const char *end = buf + len;
    const char *hit = NULL;
    const char *p;
    m1_cap_span_t num;
    const char *line_end;

    for (p = buf; p + (sizeof(needle) - 1u) <= end; p++)
    {
        if (memcmp(p, needle, sizeof(needle) - 1u) == 0) { hit = p; break; }
    }
    if (hit == NULL) { return NULL; }

    /* Digits sit immediately before "AP(s) found"; scan_back_number already
     * skips the separating whitespace. */
    num = scan_back_number(buf, hit);
    if (num.start == NULL) { return NULL; }
    *reported = (uint32_t)span_to_int(num);

    line_end = hit;
    while (line_end < end && *line_end != '\n') { line_end++; }
    return (line_end < end) ? (line_end + 1) : end;
}

/* Parses one already-bounded line [line,line_end) into `row`. Returns true if
 * the row is well-formed and should be stored. */
static bool parse_row(const char *line, const char *line_end, m1_cap_scan_ap_t *row)
{
    const char *id_end;
    const char *mac_p;
    const char *auth_start, *auth_end;
    m1_cap_span_t rssi_sp, ch_sp;
    const char *ssid_start, *ssid_end;

    if (line >= line_end) { return false; }
    if (line[0] != '[' || (line + 1) >= line_end || !is_digit_c(line[1])) { return false; }

    id_end = memchr(line, ']', (size_t)(line_end - line));
    if (id_end == NULL) { return false; }

    mac_p = find_mac(id_end + 1, line_end);
    if (mac_p == NULL) { return false; }

    copy_bounded(row->bssid, sizeof(row->bssid), mac_p, 17u);

    /* AUTH: whitespace-delimited token right after the BSSID. */
    auth_start = mac_p + 17;
    while (auth_start < line_end && isspace((unsigned char)*auth_start)) { auth_start++; }
    auth_end = auth_start;
    while (auth_end < line_end && !isspace((unsigned char)*auth_end)) { auth_end++; }
    copy_bounded(row->auth, sizeof(row->auth), auth_start, (size_t)(auth_end - auth_start));

    /* Working backward from the BSSID: RSSI, then channel. */
    rssi_sp = scan_back_number(id_end + 1, mac_p);
    if (rssi_sp.start == NULL) { return false; }
    row->rssi = span_to_int(rssi_sp);

    ch_sp = scan_back_number(id_end + 1, rssi_sp.start);
    if (ch_sp.start == NULL) { return false; }
    row->channel = span_to_int(ch_sp);
    if (row->channel < 1 || row->channel > 14) { return false; }

    /* SSID: between ']' and the channel digits, quotes trimmed if present. */
    ssid_start = id_end + 1;
    while (ssid_start < ch_sp.start && isspace((unsigned char)*ssid_start)) { ssid_start++; }
    ssid_end = ch_sp.start;
    while (ssid_end > ssid_start && isspace((unsigned char)*(ssid_end - 1))) { ssid_end--; }
    if (ssid_end > ssid_start && *ssid_start == '"') { ssid_start++; }
    if (ssid_end > ssid_start && *(ssid_end - 1) == '"') { ssid_end--; }
    if (ssid_end < ssid_start) { ssid_end = ssid_start; } /* defensive; cannot happen */
    copy_bounded(row->ssid, sizeof(row->ssid), ssid_start, (size_t)(ssid_end - ssid_start));

    return true;
}

int m1_cap_scan_parse(const char *buf, uint32_t len, m1_cap_scan_result_t *out)
{
    const char *end = buf + len;
    const char *cursor;
    uint32_t reported = 0;

    memset(out, 0, sizeof(*out));
    if (buf == NULL || len == 0u)
    {
        return -1;
    }

    cursor = find_sentinel(buf, len, &reported);
    if (cursor == NULL)
    {
        return -1;
    }
    out->valid = true;
    out->reported = reported;

    while (cursor < end && out->count < M1_CAP_SCAN_MAX_APS)
    {
        const char *line = cursor;
        const char *line_end = line;
        m1_cap_scan_ap_t row;

        while (line_end < end && *line_end != '\n') { line_end++; }
        cursor = (line_end < end) ? (line_end + 1) : end;

        memset(&row, 0, sizeof(row));
        if (parse_row(line, line_end, &row))
        {
            out->aps[out->count] = row;
            out->count++;
        }
    }

    return 0;
}
