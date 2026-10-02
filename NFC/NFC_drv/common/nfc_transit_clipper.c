/* See COPYING.txt for license details. */

/*
 * nfc_transit_clipper.c
 *
 * See nfc_transit_clipper.h for scope and byte-layout details.
 *
 * Pure, host-testable C -- no HAL/RFAL/RTOS dependency, matching the
 * mf_desfire_parse.c decoupling pattern.
 */

#include "nfc_transit_clipper.h"
#include <string.h>

/*============================= Name tables ======================================*/

typedef struct {
    uint16_t id;
    const char *name;
} nfc_transit_id_name_t;

static const nfc_transit_id_name_t s_agency_names[] = {
    {0x0001, "AC Transit"},
    {0x0004, "BART"},
    {0x0006, "Caltrain"},
    {0x0008, "CCTA"},
    {0x000B, "GGT"},
    {0x000F, "SamTrans"},
    {0x0011, "VTA"},
    {0x0012, "Muni"},
    {0x0019, "GG Ferry"},
    {0x001B, "SF Bay Ferry"},
};

static const nfc_transit_id_name_t s_bart_zones[] = {
    {0x0001, "Colma"}, {0x0002, "Daly City"}, {0x0003, "Balboa Park"},
    {0x0004, "Glen Park"}, {0x0005, "24th St Mission"}, {0x0006, "16th St Mission"},
    {0x0007, "Civic Center/UN Plaza"}, {0x0008, "Powell St"}, {0x0009, "Montgomery St"},
    {0x000A, "Embarcadero"}, {0x000B, "West Oakland"}, {0x000C, "12th St/Oakland City Center"},
    {0x000D, "19th St/Oakland"}, {0x000E, "MacArthur"}, {0x000F, "Rockridge"},
    {0x0010, "Orinda"}, {0x0011, "Lafayette"}, {0x0012, "Walnut Creek"},
    {0x0013, "Pleasant Hill/Contra Costa Centre"}, {0x0014, "Concord"},
    {0x0015, "North Concord/Martinez"}, {0x0016, "Pittsburg/Bay Point"}, {0x0017, "Ashby"},
    {0x0018, "Downtown Berkeley"}, {0x0019, "North Berkeley"}, {0x001A, "El Cerrito Plaza"},
    {0x001B, "El Cerrito Del Norte"}, {0x001C, "Richmond"}, {0x001D, "Lake Merrit"},
    {0x001E, "Fruitvale"}, {0x001F, "Coliseum"}, {0x0020, "San Leandro"},
    {0x0021, "Bay Fair"}, {0x0022, "Hayward"}, {0x0023, "South Hayward"},
    {0x0024, "Union City"}, {0x0025, "Fremont"}, {0x0026, "Castro Valley"},
    {0x0027, "Dublin/Pleasanton"}, {0x0028, "South San Francisco"}, {0x0029, "San Bruno"},
    {0x002A, "SFO Airport"}, {0x002B, "Millbrae"}, {0x002C, "West Dublin/Pleasanton"},
    {0x002D, "OAK Airport"}, {0x002E, "Warm Springs/South Fremont"}, {0x002F, "Milpitas"},
    {0x0030, "Berryessa/North San Jose"},
};

static const nfc_transit_id_name_t s_muni_zones[] = {
    {0x0000, "City Street"}, {0x0005, "Embarcadero"}, {0x0006, "Montgomery"},
    {0x0007, "Powell"}, {0x0008, "Civic Center"}, {0x0009, "Van Ness"},
    {0x000A, "Church"}, {0x000B, "Castro"}, {0x000C, "Forest Hill"},
    {0x000D, "West Portal"}, {0x0019, "Union Square/Market Street"},
    {0x001A, "Chinatown - Rose Pak"}, {0x001B, "Yerba Buena/Moscone"},
};

static const nfc_transit_id_name_t s_actransit_zones[] = {
    {0x0000, "City Street"},
};

static const nfc_transit_id_name_t s_caltrain_zones[] = {
    {0x0001, "Zone 1"}, {0x0002, "Zone 2"}, {0x0003, "Zone 3"},
    {0x0004, "Zone 4"}, {0x0005, "Zone 5"}, {0x0006, "Zone 6"},
};

typedef struct {
    uint16_t agency_id;
    const nfc_transit_id_name_t *zones;
    size_t zone_count;
} nfc_transit_agency_zone_map_t;

static const nfc_transit_agency_zone_map_t s_agency_zone_map[] = {
    {0x0001, s_actransit_zones, sizeof(s_actransit_zones) / sizeof(s_actransit_zones[0])},
    {0x0004, s_bart_zones,      sizeof(s_bart_zones) / sizeof(s_bart_zones[0])},
    {0x0006, s_caltrain_zones,  sizeof(s_caltrain_zones) / sizeof(s_caltrain_zones[0])},
    {0x0012, s_muni_zones,      sizeof(s_muni_zones) / sizeof(s_muni_zones[0])},
};

static const char *id_name_lookup(uint16_t id, const nfc_transit_id_name_t *table, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (table[i].id == id) return table[i].name;
    }
    return NULL;
}

const char *nfc_transit_clipper_agency_name(uint16_t agency_id)
{
    return id_name_lookup(agency_id, s_agency_names, sizeof(s_agency_names) / sizeof(s_agency_names[0]));
}

const char *nfc_transit_clipper_zone_name(uint16_t agency_id, uint16_t zone_id)
{
    for (size_t i = 0; i < sizeof(s_agency_zone_map) / sizeof(s_agency_zone_map[0]); i++) {
        if (s_agency_zone_map[i].agency_id == agency_id) {
            return id_name_lookup(zone_id, s_agency_zone_map[i].zones, s_agency_zone_map[i].zone_count);
        }
    }
    return NULL;
}

/*============================= Byte helpers ====================================*/

static uint16_t get_u16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int16_t get_i16be(const uint8_t *p)
{
    uint16_t raw = get_u16be(p);
    return (int16_t)raw; /* two's-complement reinterpretation */
}

/*============================= Detection =======================================*/

bool nfc_transit_detect_clipper(const mf_desfire_deep_t *deep, uint8_t *out_app_idx, const char **out_type_label)
{
    if ((deep == NULL) || (out_app_idx == NULL) || (out_type_label == NULL)) return false;

    static const uint8_t card_aid[3]   = NFC_TRANSIT_CLIPPER_AID_CARD;
    static const uint8_t mobile_aid[3] = NFC_TRANSIT_CLIPPER_AID_MOBILE;

    for (uint8_t i = 0; i < deep->app_count; i++) {
        if (memcmp(deep->apps[i].id.id, card_aid, 3) == 0) {
            *out_app_idx = i;
            *out_type_label = "Card";
            return true;
        }
        if (memcmp(deep->apps[i].id.id, mobile_aid, 3) == 0) {
            *out_app_idx = i;
            *out_type_label = "Mobile Device";
            return true;
        }
    }
    return false;
}

/*============================= Field decoders ==================================*/

bool nfc_transit_clipper_parse_identity(const uint8_t *buf, uint16_t len, uint32_t *out_serial)
{
    if ((buf == NULL) || (out_serial == NULL) || (len < 5U)) return false;
    *out_serial = get_u32be(&buf[1]);
    return true;
}

bool nfc_transit_clipper_parse_cash(const uint8_t *buf, uint16_t len,
                                     uint16_t *out_counter, uint32_t *out_last_update_1900,
                                     uint16_t *out_terminal, uint16_t *out_txn_id,
                                     int16_t *out_balance_cents)
{
    if ((buf == NULL) || (len < 0x14U)) return false;
    if (out_counter) *out_counter = get_u16be(&buf[2]);
    if (out_last_update_1900) *out_last_update_1900 = get_u32be(&buf[4]);
    if (out_terminal) *out_terminal = get_u16be(&buf[8]);
    if (out_txn_id) *out_txn_id = get_u16be(&buf[0x10]);
    if (out_balance_cents) *out_balance_cents = get_i16be(&buf[0x12]);
    return true;
}

bool nfc_transit_clipper_parse_rides(const uint8_t *histidx, uint16_t histidx_len,
                                      const uint8_t *history, uint32_t history_len,
                                      nfc_transit_ride_t *out, uint8_t max_rides,
                                      uint8_t *out_count, bool *out_truncated)
{
    if (out_count) *out_count = 0;
    if (out_truncated) *out_truncated = false;
    /* "At least" NFC_TRANSIT_CLIPPER_HISTIDX_SIZE, not exactly. Acquisition
     * checks `simple_array_get_count(...) < min_size` and only ever reads
     * the first 16 bytes regardless of the file's true declared size. A
     * strict != here rejected real cards whose histidx file legitimately
     * declares more than 16 bytes -- desf_interpret_clipper()'s own gate
     * (m1_desfire.c) already only requires >=16, so this was a stricter,
     * contradictory re-check of the same value, silently zeroing
     * ride_count while the rest of the interpretation still succeeded. */
    if ((histidx == NULL) || (histidx_len < NFC_TRANSIT_CLIPPER_HISTIDX_SIZE) || (history == NULL)) return false;

    uint8_t stored = 0;
    bool truncated = false;

    for (size_t i = 0; i < NFC_TRANSIT_CLIPPER_HISTIDX_SIZE; i++) {
        uint8_t record_num = histidx[i];
        if (record_num == 0xFFU) break; /* end-of-index sentinel */

        uint32_t offset = (uint32_t)record_num * NFC_TRANSIT_CLIPPER_RIDE_RECORD_SIZE;
        if (offset + NFC_TRANSIT_CLIPPER_RIDE_RECORD_SIZE > history_len) {
            /* This record's bytes were not fully captured. Never guess from
             * partial data; stop the index walk rather than skipping it. */
            truncated = true;
            break;
        }

        const uint8_t *record = &history[offset];
        if (record[0] != 0x10U) {
            /* A bad ride-record marker ends the history walk. */
            break;
        }

        uint16_t agency_id = get_u16be(&record[2]);
        if (agency_id == 0U) break; /* agency_id==0 ends the history walk */

        if (stored >= max_rides) {
            truncated = true;
            continue;
        }

        nfc_transit_ride_t *r = &out[stored];
        memset(r, 0, sizeof(*r));
        r->agency_id    = agency_id;
        r->fare_cents   = get_i16be(&record[6]);
        r->vehicle_id   = get_u16be(&record[0x0A]);
        uint32_t time_on  = get_u32be(&record[0x0C]);
        uint32_t time_off = get_u32be(&record[0x10]);
        r->has_time_on  = (time_on != 0U);
        r->time_on_1900 = time_on;
        r->has_time_off = (time_off != 0U);
        r->time_off_1900 = time_off;
        r->zone_on_id   = get_u16be(&record[0x14]);
        r->zone_off_id  = get_u16be(&record[0x16]);
        stored++;
    }

    if (out_count) *out_count = stored;
    if (out_truncated) *out_truncated = truncated;
    return true;
}

/*============================= Utilities =======================================*/

void nfc_transit_usd_split(int16_t cents, bool *out_negative, int16_t *out_dollars, uint16_t *out_cents_abs)
{
    /* Split signed cents into display dollars and cents. */
    if (out_dollars) *out_dollars = (int16_t)(cents / 100);

    if (cents >= 0) {
        if (out_negative)  *out_negative  = false;
        if (out_cents_abs) *out_cents_abs = (uint16_t)(cents % 100);
    } else {
        if (out_negative)  *out_negative  = true;
        if (out_cents_abs) *out_cents_abs = (uint16_t)((-(int32_t)cents) % 100);
    }
}

static bool is_leap_year(uint16_t y)
{
    return ((y % 4U == 0U) && (y % 100U != 0U)) || (y % 400U == 0U);
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if ((m == 2U) && is_leap_year(y)) return 29U;
    return days[m - 1U];
}

void nfc_transit_epoch1900_to_ymdhms(uint32_t seconds,
                                      uint16_t *out_year, uint8_t *out_month, uint8_t *out_day,
                                      uint8_t *out_hour, uint8_t *out_minute, uint8_t *out_second)
{
    uint32_t days_left = seconds / 86400U;
    uint32_t secs_in_day = seconds % 86400U;

    uint16_t year = 1900U;
    for (;;) {
        uint16_t year_days = is_leap_year(year) ? 366U : 365U;
        if (days_left >= year_days) {
            days_left -= year_days;
            year++;
        } else {
            break;
        }
    }

    uint8_t month = 1U;
    for (;;) {
        uint8_t md = days_in_month(year, month);
        if (days_left >= md) {
            days_left -= md;
            month++;
        } else {
            break;
        }
    }

    if (out_year)  *out_year  = year;
    if (out_month) *out_month = month;
    if (out_day)   *out_day   = (uint8_t)(days_left + 1U);
    if (out_hour)  *out_hour  = (uint8_t)(secs_in_day / 3600U);
    if (out_minute) *out_minute = (uint8_t)((secs_in_day % 3600U) / 60U);
    if (out_second) *out_second = (uint8_t)(secs_in_day % 60U);
}

void nfc_transit_result_reset(nfc_transit_result_t *r)
{
    if (r == NULL) return;
    memset(r, 0, sizeof(*r));
}
