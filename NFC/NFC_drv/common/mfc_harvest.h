/*
 * mfc_harvest.h - MIFARE Classic recovery-capture (.m1h) serializer + bounded
 *                 staging buffer (Harvester Increment 1).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hardware-agnostic: NO RFAL/HAL/FreeRTOS/FatFs dependency, no dynamic
 * allocation, no hidden mutable globals. Serialized bytes are handed to a
 * caller-supplied flush callback (SD/FatFs or WebSerial is wired later); this
 * layer only produces the exact little-endian .m1h byte stream.
 *
 * Streaming model: samples are appended one at a time and accumulate in a fixed
 * 512-byte staging buffer owned by the caller-allocated context; the buffer is
 * flushed (via the callback) whenever it fills and once more at finalize.
 * RAM cost = sizeof(mfc_harvest_t) (~0.5 KiB); nothing scales with sample count.
 *
 * .m1h WIRE FORMAT (all integers LITTLE-ENDIAN):
 *
 *   File header (38 bytes):
 *     0  u8[4]  magic          "M1HC"
 *     4  u16    format_version  = 1
 *     6  u16    header_size     = 38 (readers skip beyond for fwd-compat)
 *     8  u32    capability_flags
 *     12 u8     uid_len         (4 / 7 / 10)
 *     13 u8[10] uid             (uid_len valid bytes, remainder 0)
 *     23 u16    atqa
 *     25 u8     sak
 *     26 u32    capture_ticks   (device monotonic tick; not an RTC time)
 *     30 u32    record_count    (or MFC_HARVEST_COUNT_UNKNOWN)
 *     34 u32    header_crc32    (CRC-32/IEEE of bytes 0..33)
 *
 *   Record (self-framed; unknown types are skipped via record_len):
 *     0  u8     record_type     (1 MFKey64 / 2 Nested / 3 Static / 4 Hardnested)
 *     1  u8     record_version  = 1
 *     2  u32    record_len      (TOTAL record bytes incl. framing + trailer)
 *     6  ...    payload (per type, below)
 *     end-4 u32 record_crc32    (CRC-32/IEEE of bytes 0 .. record_len-5)
 *
 *   NOTE (refinement flagged for review): the record CRC is a TRAILER, not a
 *   pre-payload field. This is required by the 512-byte streaming buffer — a
 *   large (hardnested) record spans multiple flushes, so its CRC cannot be
 *   emitted before the payload. record_len (computable from the declared
 *   sample count) stays in the header so records remain skippable.
 *
 *   Payloads:
 *     Type 1 MFKey64 (reader-auth SNIFF transcript; solved by the retained
 *                     mfkey64 engine — NOT a card-only method):
 *       u8 block, u8 keytype, u32 nt, u32 nr_enc, u32 ar_enc, u32 at_enc
 *     Type 2 Ordinary Nested (card-only):
 *       u8 src_block, u8 src_keytype, u8 tgt_block, u8 tgt_keytype,
 *       u8 known_key_ref (0 host-supplied / 1 embedded), [u8 key[6] if ref==1],
 *       u32 anchor_nt, u16 sample_count,
 *       sample_count x { u32 nt_enc, u8 par, u16 dist }
 *     Type 3 Static Nested (card-only):
 *       u8 src_block, u8 src_keytype, u8 tgt_block, u8 tgt_keytype,
 *       u8 known_key_ref, [u8 key[6] if ref==1], u32 static_nt, u16 sample_count,
 *       sample_count x { u32 nt_enc, u8 par }
 *     Type 4 Hardnested (card-only):
 *       u8 src_block, u8 src_keytype, u8 tgt_block, u8 tgt_keytype,
 *       u8 known_key_ref, [u8 key[6] if ref==1], u32 sample_count,
 *       sample_count x { u32 nt_enc, u8 par }
 *
 *   par: encrypted nonce-byte parity, 4 bits, LSB = nonce byte 0.
 */
#ifndef MFC_HARVEST_H
#define MFC_HARVEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFC_HARVEST_MAGIC0          'M'
#define MFC_HARVEST_MAGIC1          '1'
#define MFC_HARVEST_MAGIC2          'H'
#define MFC_HARVEST_MAGIC3          'C'
#define MFC_HARVEST_FORMAT_VERSION  ((uint16_t)1)
#define MFC_HARVEST_RECORD_VERSION  ((uint8_t)1)
#define MFC_HARVEST_HEADER_SIZE     ((uint16_t)38)
#define MFC_HARVEST_STAGING_BYTES   ((uint16_t)512)
#define MFC_HARVEST_COUNT_UNKNOWN   ((uint32_t)0xFFFFFFFFu)

/* Upper bound on samples per record (defensive; a full hardnested set is well
 * under this). begin_* rejects larger counts rather than overflow record_len. */
#define MFC_HARVEST_MAX_SAMPLES     ((uint32_t)65536u)

typedef enum {
    MFC_HARVEST_TYPE_MFKEY64    = 1,
    MFC_HARVEST_TYPE_NESTED     = 2,
    MFC_HARVEST_TYPE_STATIC     = 3,
    MFC_HARVEST_TYPE_HARDNESTED = 4,
} mfc_harvest_type_t;

typedef enum {
    MFC_HARVEST_OK = 0,
    MFC_HARVEST_INVALID_INPUT,   /* NULL / malformed argument                  */
    MFC_HARVEST_STATE,           /* call made in the wrong order               */
    MFC_HARVEST_BOUNDS,          /* too many samples / count mismatch          */
    MFC_HARVEST_FLUSH_FAILED,    /* the flush callback returned false (sticky)  */
} mfc_harvest_status_t;

/*
 * Flush callback: receive `len` serialized bytes. Return true on success; false
 * marks the harvester failed (all further calls no-op and return FLUSH_FAILED).
 * The bytes are only valid for the duration of the call (copy if retained).
 */
typedef bool (*mfc_harvest_flush_fn)(void *ctx, const uint8_t *bytes, size_t len);

/* Caller-allocated context (stack or static). ~0.5 KiB; no pointers into it are
 * retained by the callback. Treat fields as private. */
typedef struct {
    uint8_t  buf[MFC_HARVEST_STAGING_BYTES];
    uint16_t used;
    mfc_harvest_flush_fn flush;
    void    *flush_ctx;
    uint32_t live_crc;              /* running CRC (record or header)          */
    bool     crc_on;               /* feed staged bytes into live_crc          */
    uint8_t  state;                /* mfc_harvest internal state               */
    uint8_t  cur_type;             /* record type currently open               */
    uint32_t cur_expected_samples;
    uint32_t cur_written_samples;
    uint32_t records_written;
    mfc_harvest_status_t status;   /* sticky error                             */
} mfc_harvest_t;

/* ---- caller-facing record / sample structs ---- */
typedef struct {
    uint8_t  uid_len;              /* 4 / 7 / 10 */
    uint8_t  uid[10];
    uint16_t atqa;
    uint8_t  sak;
    uint32_t capture_ticks;
    uint32_t capability_flags;
    uint32_t record_count;         /* declared count, or MFC_HARVEST_COUNT_UNKNOWN */
} mfc_harvest_file_info_t;

typedef struct {
    uint8_t  block, keytype;
    uint32_t nt, nr_enc, ar_enc, at_enc;
} mfc_mfkey64_record_t;

/* Shared source/target + known-key identification for card-only records. */
typedef struct {
    uint8_t  src_block, src_keytype;   /* known-key source sector             */
    uint8_t  tgt_block, tgt_keytype;   /* key being recovered                 */
    uint8_t  known_key_ref;            /* 0 = host supplies; 1 = key[6] present */
    uint8_t  known_key[6];
} mfc_card_only_hdr_t;

typedef struct { uint32_t nt_enc; uint8_t par; uint16_t dist; } mfc_nested_sample_t;
typedef struct { uint32_t nt_enc; uint8_t par; }               mfc_nonce_sample_t;

/* ---- API ---- */
void mfc_harvest_init(mfc_harvest_t *h, mfc_harvest_flush_fn flush, void *flush_ctx);

mfc_harvest_status_t mfc_harvest_begin_file(mfc_harvest_t *h,
                                            const mfc_harvest_file_info_t *info);

/* Type 1: single fixed record (no add/end). */
mfc_harvest_status_t mfc_harvest_write_mfkey64(mfc_harvest_t *h,
                                               const mfc_mfkey64_record_t *r);

/* Type 2: begin -> add_nested_sample * count -> end_record. */
mfc_harvest_status_t mfc_harvest_begin_nested(mfc_harvest_t *h,
                                              const mfc_card_only_hdr_t *hdr,
                                              uint32_t anchor_nt,
                                              uint16_t sample_count);
mfc_harvest_status_t mfc_harvest_add_nested_sample(mfc_harvest_t *h,
                                                   const mfc_nested_sample_t *s);

/* Type 3: begin -> add_nonce_sample * count -> end_record. */
mfc_harvest_status_t mfc_harvest_begin_static(mfc_harvest_t *h,
                                              const mfc_card_only_hdr_t *hdr,
                                              uint32_t static_nt,
                                              uint16_t sample_count);

/* Type 4: begin -> add_nonce_sample * count -> end_record. */
mfc_harvest_status_t mfc_harvest_begin_hardnested(mfc_harvest_t *h,
                                                  const mfc_card_only_hdr_t *hdr,
                                                  uint32_t sample_count);

/* Types 3 & 4 share this sample writer. */
mfc_harvest_status_t mfc_harvest_add_nonce_sample(mfc_harvest_t *h,
                                                  const mfc_nonce_sample_t *s);

mfc_harvest_status_t mfc_harvest_end_record(mfc_harvest_t *h);

/* Flush the staging remainder and close the file. */
mfc_harvest_status_t mfc_harvest_finalize(mfc_harvest_t *h);

/* Current sticky status (does not mutate state). */
mfc_harvest_status_t mfc_harvest_status(const mfc_harvest_t *h);

#ifdef __cplusplus
}
#endif

#endif /* MFC_HARVEST_H */
