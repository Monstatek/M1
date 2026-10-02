/* See COPYING.txt for license details. */

/*
 * m1_wifi_client_id.c
 *
 * See m1_wifi_client_id.h for scope. Pure data + logic, host-testable.
 *
 * The table below is a small, conservative set of well-known public IEEE
 * OUI (MAC vendor prefix) assignments for common consumer/IoT vendors. It is
 * intentionally short (a handful of dozens of entries, not an exhaustive
 * database) and every entry is a real, publicly documented OUI block for the
 * named manufacturer. A hit means "this vendor manufactured the radio",
 * never a specific product/model.
 *
 * M1 Project
 */

#include "m1_wifi_client_id.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    uint8_t oui[3];
    const char *vendor;
} m1_wifi_oui_entry_t;

/* Conservative, ~30-60 entry table of real, well-known IEEE OUI assignments.
 * Not exhaustive -- an unmatched prefix simply falls back to "Device" at the
 * caller (m1_wifi_format_client_label()), never a guess. */
static const m1_wifi_oui_entry_t s_oui_table[] = {
    /* Apple */
    { {0x3C, 0x15, 0xC2}, "Apple" },
    { {0xA4, 0x5E, 0x60}, "Apple" },
    { {0xF0, 0x18, 0x98}, "Apple" },
    { {0x00, 0x1B, 0x63}, "Apple" },
    { {0x68, 0x96, 0x7B}, "Apple" },
    { {0xDC, 0xA9, 0x04}, "Apple" },
    /* Samsung */
    { {0x00, 0x12, 0xFB}, "Samsung" },
    { {0x5C, 0x0A, 0x5B}, "Samsung" },
    { {0x8C, 0x77, 0x12}, "Samsung" },
    { {0xB4, 0x74, 0x9F}, "Samsung" },
    /* Google */
    { {0x3C, 0x5A, 0xB4}, "Google" },
    { {0xF4, 0xF5, 0xD8}, "Google" },
    { {0x94, 0xEB, 0x2C}, "Google" },
    /* Espressif (ESP32/ESP8266 modules -- other M1-style and hobbyist gear) */
    { {0x24, 0x0A, 0xC4}, "Espressif" },
    { {0x30, 0xAE, 0xA4}, "Espressif" },
    { {0xEC, 0xFA, 0xBC}, "Espressif" },
    { {0x7C, 0x9E, 0xBD}, "Espressif" },
    /* Intel */
    { {0x00, 0x1B, 0x21}, "Intel" },
    { {0x3C, 0x97, 0x0E}, "Intel" },
    /* Amazon (Echo/Ring/Fire devices) */
    { {0x74, 0xC2, 0x46}, "Amazon" },
    { {0x68, 0x37, 0xE9}, "Amazon" },
    { {0xF0, 0x81, 0x73}, "Amazon" },
    { {0x44, 0x65, 0x0D}, "Amazon" },
    /* Sonos */
    { {0x00, 0x0E, 0x58}, "Sonos" },
    { {0x5C, 0xAA, 0xFD}, "Sonos" },
    /* Raspberry Pi Foundation */
    { {0xB8, 0x27, 0xEB}, "Raspberry Pi" },
    { {0xDC, 0xA6, 0x32}, "Raspberry Pi" },
    { {0xE4, 0x5F, 0x01}, "Raspberry Pi" },
    /* Microsoft */
    { {0x00, 0x03, 0xFF}, "Microsoft" },
    { {0x7C, 0x1E, 0x52}, "Microsoft" },
    /* Nest Labs */
    { {0x18, 0xB4, 0x30}, "Nest" },
    { {0x64, 0x16, 0x66}, "Nest" },
    /* TP-Link */
    { {0x50, 0xC7, 0xBF}, "TP-Link" },
    { {0xEC, 0x08, 0x6B}, "TP-Link" },
    /* Xiaomi */
    { {0x34, 0xCE, 0x00}, "Xiaomi" },
    { {0x78, 0x11, 0xDC}, "Xiaomi" },
    /* Huawei */
    { {0x00, 0xE0, 0xFC}, "Huawei" },
    /* Sony */
    { {0x00, 0x04, 0x1F}, "Sony" },
    /* LG Electronics */
    { {0x00, 0x1C, 0x62}, "LG" },
    /* Belkin (Wemo) */
    { {0x94, 0x10, 0x3E}, "Belkin" },
    /* Philips (Hue) */
    { {0x00, 0x17, 0x88}, "Philips" },
    /* Netgear */
    { {0x20, 0xE5, 0x2A}, "Netgear" },
    /* D-Link */
    { {0x00, 0x1B, 0x11}, "D-Link" },
    /* ASUS */
    { {0x00, 0x1F, 0xC6}, "ASUS" },
    /* Ubiquiti Networks */
    { {0x24, 0xA4, 0x3C}, "Ubiquiti" },
    /* Roku */
    { {0xB0, 0xA7, 0x37}, "Roku" },
    /* Logitech */
    { {0x00, 0x07, 0x61}, "Logitech" },
};

#define M1_WIFI_OUI_TABLE_COUNT (sizeof(s_oui_table) / sizeof(s_oui_table[0]))

const char *m1_wifi_oui_vendor(const uint8_t mac[3])
{
    size_t i;

    if (!mac)
    {
        return NULL;
    }

    for (i = 0; i < M1_WIFI_OUI_TABLE_COUNT; i++)
    {
        if ((s_oui_table[i].oui[0] == mac[0]) &&
            (s_oui_table[i].oui[1] == mac[1]) &&
            (s_oui_table[i].oui[2] == mac[2]))
        {
            return s_oui_table[i].vendor;
        }
    }

    return NULL;
}

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return 10 + (c - 'a'); }
    if ((c >= 'A') && (c <= 'F')) { return 10 + (c - 'A'); }
    return -1;
}

int m1_wifi_mac_str_to_bytes(const char *mac_str, uint8_t out_bytes[6])
{
    int i;
    size_t len;

    if (!mac_str || !out_bytes)
    {
        return 0;
    }

    /* A well-formed "AA:BB:CC:DD:EE:FF" is exactly 17 chars; bail out before
     * indexing past a short/empty string instead of relying on hex_nibble()
     * to reject a NUL byte (it does, but mac_str[i*3+1] for the last octet
     * would still read one byte past a too-short string first). */
    len = strlen(mac_str);
    if (len < 17u)
    {
        return 0;
    }

    for (i = 0; i < 6; i++)
    {
        int hi = hex_nibble(mac_str[i * 3]);
        int lo = hex_nibble(mac_str[i * 3 + 1]);

        if ((hi < 0) || (lo < 0))
        {
            return 0;
        }
        if (i < 5)
        {
            if (mac_str[i * 3 + 2] != ':')
            {
                return 0;
            }
        }

        out_bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    return 1;
}

int m1_wifi_parse_sta_rssi(const char *mac_end)
{
    const char *p = mac_end;

    if (!p)
    {
        return 0;
    }

    while (*p && isspace((unsigned char)*p))
    {
        p++;
    }

    if ((*p != '-') && !isdigit((unsigned char)*p))
    {
        return 0;
    }

    return (int)strtol(p, NULL, 10);
}

int m1_wifi_select_response_ok(const char *resp)
{
    if (!resp)
    {
        return 0;
    }

    /* An explicit ESP32 rejection is always a failure, even if a stray
     * "[*]" substring somehow also appeared (defensive; not expected in
     * practice). Checked first so ambiguous text never reads as success. */
    if (strstr(resp, "[!]") != NULL)
    {
        return 0;
    }

    return (strstr(resp, "[*]") != NULL) ? 1 : 0;
}

m1_wifi_rssi_category_t m1_wifi_rssi_category(int rssi)
{
    if (rssi >= -50) { return M1_WIFI_RSSI_EXCELLENT; }
    if (rssi >= -67) { return M1_WIFI_RSSI_GOOD; }
    if (rssi >= -75) { return M1_WIFI_RSSI_FAIR; }
    return M1_WIFI_RSSI_WEAK;
}

const char *m1_wifi_rssi_category_str(m1_wifi_rssi_category_t cat)
{
    switch (cat)
    {
        case M1_WIFI_RSSI_EXCELLENT: return "Excellent";
        case M1_WIFI_RSSI_GOOD:      return "Good";
        case M1_WIFI_RSSI_FAIR:      return "Fair";
        case M1_WIFI_RSSI_WEAK:
        default:                     return "Weak";
    }
}

static int m1_wifi_dc_row_rssi_desc_cmp(const void *a, const void *b)
{
    const m1_wifi_dc_row_t *ea = (const m1_wifi_dc_row_t *)a;
    const m1_wifi_dc_row_t *eb = (const m1_wifi_dc_row_t *)b;
    return eb->rssi - ea->rssi;
}

void m1_wifi_dc_picker_init(m1_wifi_dc_picker_t *picker)
{
    if (!picker)
    {
        return;
    }
    memset(picker, 0, sizeof(*picker));
}

void m1_wifi_dc_picker_freeze(m1_wifi_dc_picker_t *picker)
{
    if (!picker)
    {
        return;
    }
    picker->frozen = 1U;
}

int m1_wifi_dc_picker_find(const m1_wifi_dc_picker_t *picker, const char *mac)
{
    uint16_t i;

    if (!picker || !mac)
    {
        return -1;
    }

    for (i = 0; i < picker->count; i++)
    {
        if (strcmp(picker->rows[i].mac, mac) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

void m1_wifi_dc_picker_apply_scan(m1_wifi_dc_picker_t *picker,
                                   const char macs[][18], const int *rssis,
                                   uint16_t n)
{
    uint16_t i, j;
    uint16_t existing_count;

    if (!picker)
    {
        return;
    }
    if ((n > 0U) && (!macs || !rssis))
    {
        return;
    }
    if (n > M1_WIFI_DC_PICKER_MAX_ROWS)
    {
        n = M1_WIFI_DC_PICKER_MAX_ROWS;
    }

    existing_count = picker->count;

    /* Assume absent until this round proves otherwise -- never deletes a
     * row, just flips its visibility for the "(gone)" display. */
    for (j = 0; j < existing_count; j++)
    {
        picker->rows[j].present = 0U;
    }

    for (i = 0; i < n; i++)
    {
        int found = -1;

        for (j = 0; j < existing_count; j++)
        {
            if (strcmp(picker->rows[j].mac, macs[i]) == 0)
            {
                found = (int)j;
                break;
            }
        }

        if (found >= 0)
        {
            picker->rows[found].rssi = rssis[i];
            picker->rows[found].present = 1U;
        }
        else if (picker->count < M1_WIFI_DC_PICKER_MAX_ROWS)
        {
            /* Newly-seen client: appended at the end -- never inserted "by
             * strength" into an already-frozen (or already-sorted) region,
             * and never auto-highlighted by this module. */
            m1_wifi_dc_row_t *r = &picker->rows[picker->count];
            snprintf(r->mac, sizeof(r->mac), "%s", macs[i]);
            r->rssi = rssis[i];
            r->present = 1U;
            picker->count++;
        }
        /* else: at M1_WIFI_DC_PICKER_MAX_ROWS capacity -- this round's extra
         * entry is dropped rather than growing the backing array further. */
    }

    if (!picker->frozen)
    {
        /* Pre-freeze: covers both the very first population (this is what
         * makes it "sort strongest-first on first population") and any
         * further automatic refresh that lands before the caller's first
         * UP/DOWN/CENTER -- both are safe to fully resort since no row has
         * been highlighted by explicit user choice yet. */
        qsort(picker->rows, picker->count, sizeof(picker->rows[0]),
              m1_wifi_dc_row_rssi_desc_cmp);
    }
}

int m1_wifi_dc_locate_client_index(const char macs[][18], uint16_t count,
                                    const char *picked_mac)
{
    uint16_t i;

    if (!macs || !picked_mac)
    {
        return -1;
    }

    for (i = 0; i < count; i++)
    {
        if (strcmp(macs[i], picked_mac) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

m1_wifi_dc_start_result_t m1_wifi_dc_validate_start(const char *ap_ssid,
                                                     const char macs[][18],
                                                     uint16_t count,
                                                     const char *picked_mac,
                                                     const char *select_resp,
                                                     int *out_sel_idx)
{
    int idx;

    if (!ap_ssid || (ap_ssid[0] == '\0'))
    {
        return M1_WIFI_DC_START_ERR_AP_INVALID;
    }

    idx = m1_wifi_dc_locate_client_index(macs, count, picked_mac);
    if (idx < 0)
    {
        return M1_WIFI_DC_START_ERR_CLIENT_NOT_FOUND;
    }
    if (out_sel_idx)
    {
        *out_sel_idx = idx;
    }

    if (!m1_wifi_select_response_ok(select_resp))
    {
        return M1_WIFI_DC_START_ERR_SELECT_REJECTED;
    }

    return M1_WIFI_DC_START_OK;
}

int m1_wifi_format_client_label(const char *mac_str, char *out, size_t out_cap)
{
    uint8_t mac[6];
    const char *vendor;

    if (!out || (out_cap == 0))
    {
        return 0;
    }

    if (!m1_wifi_mac_str_to_bytes(mac_str, mac))
    {
        /* No usable MAC at all -- neutral fallback with no suffix. */
        return snprintf(out, out_cap, "Device");
    }

    vendor = m1_wifi_oui_vendor(mac);

    if (vendor)
    {
        return snprintf(out, out_cap, "%s - %02X:%02X", vendor, mac[4], mac[5]);
    }

    return snprintf(out, out_cap, "Device - %02X:%02X", mac[4], mac[5]);
}

int m1_wifi_format_client_name(const char *mac_str, char *out, size_t out_cap)
{
    uint8_t mac[6];
    const char *vendor;

    if (!out || (out_cap == 0))
    {
        return 0;
    }

    if (!m1_wifi_mac_str_to_bytes(mac_str, mac))
    {
        return snprintf(out, out_cap, "Unknown Device");
    }

    vendor = m1_wifi_oui_vendor(mac);
    if (vendor)
    {
        return snprintf(out, out_cap, "%s", vendor);
    }

    return snprintf(out, out_cap, "Unknown Device");
}

int m1_wifi_format_mac_suffix(const char *mac_str, char *out, size_t out_cap)
{
    uint8_t mac[6];

    if (!out || (out_cap == 0))
    {
        return 0;
    }

    if (!m1_wifi_mac_str_to_bytes(mac_str, mac))
    {
        out[0] = '\0';
        return 0;
    }

    return snprintf(out, out_cap, "...%02X:%02X", mac[4], mac[5]);
}
