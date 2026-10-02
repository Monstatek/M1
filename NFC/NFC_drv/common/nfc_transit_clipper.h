/* See COPYING.txt for license details. */

/*
 * nfc_transit_clipper.h
 *
 * Bounded card-interpretation layer for Clipper (San Francisco Bay Area
 * transit card, MIFARE DESFire-based). Runs strictly AFTER the generic,
 * proven DESFire deeper-read model (mf_desfire_parse.h / m1_desfire.c) has
 * already captured an application/file's data -- this module only
 * INTERPRETS bytes that generic capture already retrieved (or, for the ride
 * history file specifically, bytes retrieved by one additional read of the
 * SAME already-gated command, see m1_desfire.c). It never authenticates,
 * writes, or weakens the generic transport/access-control gate in any way.
 *
 * The implementation contains bounded detection identifiers, byte layouts,
 * agency/zone tables and decoding logic for already-captured card data.
 * See COPYING.txt for the project license.
 * Copyright (C) 2026 Monstatek.
 *
 * SCOPE: read-only interpretation of already-captured, unauthenticated
 * plaintext bytes. No authentication, no writes, no key recovery. Every
 * displayed field is either genuinely decoded from captured bytes or
 * explicitly marked unavailable -- never fabricated, never guessed.
 */
#ifndef NFC_TRANSIT_CLIPPER_H_
#define NFC_TRANSIT_CLIPPER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mf_desfire_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================= Detection ======================================*/
/* Clipper's two known application IDs. */
#define NFC_TRANSIT_CLIPPER_AID_CARD   { 0x90, 0x11, 0xF2 }  /* physical plastic card */
#define NFC_TRANSIT_CLIPPER_AID_MOBILE { 0x91, 0x11, 0xF2 }  /* mobile device */

/* File IDs of interest within the Clipper application. */
#define NFC_TRANSIT_CLIPPER_FILE_ECASH    2U   /* Backup, balance/last-transaction */
#define NFC_TRANSIT_CLIPPER_FILE_HISTIDX  6U   /* Backup, 16-byte ride-record index */
#define NFC_TRANSIT_CLIPPER_FILE_IDENTITY 8U   /* Standard, card serial number */
#define NFC_TRANSIT_CLIPPER_FILE_HISTORY  14U  /* Standard, up to 512-byte ride log */

/* Bounded capture cap for the ride-history file, used as a hard upper bound
 * for the one
 * additional targeted read m1_desfire.c performs for this specific,
 * already-free-plaintext-gated file. Never grown further at runtime. */
#define NFC_TRANSIT_CLIPPER_HISTORY_CAP 512U
#define NFC_TRANSIT_CLIPPER_RIDE_RECORD_SIZE 32U
#define NFC_TRANSIT_CLIPPER_HISTIDX_SIZE 16U

/* Bounded number of ride-history entries kept for display. The index file
 * holds at most 16 slots. */
#define NFC_TRANSIT_MAX_RIDES 16U

/* Searches an already-populated deep-read result for a Clipper application
 * (AID-only match, independent of whether any of its files were
 * actually readable). On match, writes the application's index into
 * *out_app_idx and a human-readable device-type label ("Card" / "Mobile
 * Device") into *out_type_label (points to a static string literal, safe to
 * retain). Returns false (leaving both outputs untouched) if no Clipper
 * application is present. */
bool nfc_transit_detect_clipper(const mf_desfire_deep_t *deep,
                                 uint8_t *out_app_idx,
                                 const char **out_type_label);

/*============================= Field decoders ==================================*/
/* Identity file (id 8, Standard, >=5 bytes needed): byte[1..4] is the card
 * serial number, big-endian. Returns false (leaves *out_serial untouched)
 * if len < 5 -- never guesses from a short/partial capture. */
bool nfc_transit_clipper_parse_identity(const uint8_t *buf, uint16_t len, uint32_t *out_serial);

/* ECash file (id 2, Backup, >=0x14=20 bytes needed to reach every field this
 * decodes): counter@2 (2B BE), last_update_1900@4 (4B BE seconds since
 * 1900-01-01 UTC), last_terminal@8 (2B BE), last_txn_id@0x10 (2B BE),
 * balance_cents@0x12 (2B BE signed). Returns false if len < 0x14. */
bool nfc_transit_clipper_parse_cash(const uint8_t *buf, uint16_t len,
                                     uint16_t *out_counter, uint32_t *out_last_update_1900,
                                     uint16_t *out_terminal, uint16_t *out_txn_id,
                                     int16_t *out_balance_cents);

typedef struct {
    uint16_t agency_id;
    int16_t  fare_cents;
    uint16_t vehicle_id;         /* 0 = not provided */
    bool     has_time_on;
    uint32_t time_on_1900;
    bool     has_time_off;
    uint32_t time_off_1900;
    uint16_t zone_on_id;
    uint16_t zone_off_id;
} nfc_transit_ride_t;

/* Decodes ride-history records referenced by the 16-byte index file
 * (histidx, exactly NFC_TRANSIT_CLIPPER_HISTIDX_SIZE bytes -- returns false
 * if histidx_len != 16) against however many bytes of the history file were
 * actually captured (history_len, which may be less than the file's real
 * declared size). Index entries equal to 0xFF end the index. On an out-of-range record offset, a bad record
 * marker (record[0] != 0x10), or an agency_id of 0, the walk STOPS entirely
 * (break, not skip-and-continue). An earlier version of this
 * function used continue instead of break for all three conditions, which
 * violated this contract and has been corrected. Writes up to max_rides
 * entries into out[], reports the count actually stored in *out_count, and
 * sets *out_truncated true if the walk stopped early because an in-range
 * record was found but max_rides or history_len was exhausted, so the UI
 * can show a truthful "more exist but were not available" indicator rather
 * than silently under-reporting. */
bool nfc_transit_clipper_parse_rides(const uint8_t *histidx, uint16_t histidx_len,
                                      const uint8_t *history, uint32_t history_len,
                                      nfc_transit_ride_t *out, uint8_t max_rides,
                                      uint8_t *out_count, bool *out_truncated);

/*============================= Lookups / utilities ==============================*/
/* Agency/zone name lookups.
 * Return NULL if the ID is not in the known table -- callers must show an
 * explicit "Unknown (0xXXXX)" style fallback, never omit or guess a name. */
const char *nfc_transit_clipper_agency_name(uint16_t agency_id);
const char *nfc_transit_clipper_zone_name(uint16_t agency_id, uint16_t zone_id);

/* Splits a signed cents amount for display:
 * out_dollars is the raw signed truncated division (cents/100 -- already
 * carries the sign, e.g. -327 for -32768 cents) and out_cents_abs is the
 * always-non-negative remainder. Format as "Balance: $%d.%02u" using
 * out_dollars/out_cents_abs directly. For a negative balance the result is
 * "$-D.CC" (sign after the currency symbol, not before); out_negative is
 * computed for completeness but is not used by the display. */
void nfc_transit_usd_split(int16_t cents, bool *out_negative, int16_t *out_dollars, uint16_t *out_cents_abs);

/* Converts a Clipper-style "seconds since 1900-01-01 00:00:00 UTC" 32-bit
 * timestamp into a proleptic-Gregorian calendar date/time (UTC, no
 * timezone or DST handling). Independently implemented and golden-vector-tested. */
void nfc_transit_epoch1900_to_ymdhms(uint32_t seconds,
                                      uint16_t *out_year, uint8_t *out_month, uint8_t *out_day,
                                      uint8_t *out_hour, uint8_t *out_minute, uint8_t *out_second);

/*============================= Top-level result ==================================*/
typedef enum {
    NfcTransitCardUnknown = 0,
    NfcTransitCardClipper,
} nfc_transit_card_id_t;

typedef struct {
    nfc_transit_card_id_t card_id;
    const char *card_type_label;    /* "Card" / "Mobile Device"; NULL if card_id == Unknown */

    bool     serial_valid;
    uint32_t serial_number;

    bool     balance_valid;
    int16_t  balance_cents;

    bool     last_update_valid;      /* ecash file was captured/decoded */
    uint32_t last_update_1900;       /* 0 = "Never", per decode_cash_file */

    bool     last_terminal_valid;
    uint16_t last_terminal_id;
    bool     last_txn_valid;
    uint16_t last_txn_id;
    bool     counter_valid;
    uint16_t counter;

    uint8_t  ride_count;              /* how many of rides[] are valid, most-recent-first per histidx order */
    bool     rides_truncated;
    nfc_transit_ride_t rides[NFC_TRANSIT_MAX_RIDES];
} nfc_transit_result_t;

/* Resets a result to its documented empty/not-attempted state. */
void nfc_transit_result_reset(nfc_transit_result_t *r);

#ifdef __cplusplus
}
#endif

#endif /* NFC_TRANSIT_CLIPPER_H_ */
