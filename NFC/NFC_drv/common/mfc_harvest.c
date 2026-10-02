/*
 * mfc_harvest.c - MIFARE Classic recovery-capture (.m1h) serializer + bounded
 *                 staging buffer (Harvester Increment 1).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See mfc_harvest.h for the wire format. No RFAL/HAL/FatFs, no dynamic
 * allocation, no VLA, no hidden mutable globals. All multi-byte integers are
 * serialized little-endian by hand (no struct punning), so the output is
 * identical on any host or target endianness.
 */
#include "mfc_harvest.h"

/* internal states */
enum {
    ST_INIT = 0,
    ST_FILE_OPEN,
    ST_RECORD_OPEN,
    ST_FINALIZED,
};

/* payload / sample sizes (bytes) */
#define REC_FRAMING       (1u + 1u + 4u)     /* type + version + record_len     */
#define REC_TRAILER       (4u)               /* record_crc32                    */
#define MFKEY64_PAYLOAD   (2u + 16u)         /* block,keytype + 4x u32          */
#define CARDONLY_HDR_BASE (5u)               /* src2 + tgt2 + known_key_ref     */
#define KEY_BYTES         (6u)
#define NESTED_SAMPLE     (7u)               /* nt_enc u32 + par u8 + dist u16  */
#define NONCE_SAMPLE      (5u)               /* nt_enc u32 + par u8             */

/* CRC-32/IEEE (reflected, poly 0xEDB88320); init 0xFFFFFFFF, final XOR ~. */
static uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++) {
        crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

/* Append bytes into the staging buffer, flushing full 512-byte blocks. When
 * crc_on, each byte also updates the running CRC. No-op once failed. */
static void stage(mfc_harvest_t *h, const uint8_t *p, size_t n) {
    if (h->status != MFC_HARVEST_OK) return;
    for (size_t i = 0; i < n; i++) {
        if (h->crc_on) h->live_crc = crc32_byte(h->live_crc, p[i]);
        h->buf[h->used++] = p[i];
        if (h->used == MFC_HARVEST_STAGING_BYTES) {
            if (!h->flush(h->flush_ctx, h->buf, h->used)) {
                h->status = MFC_HARVEST_FLUSH_FAILED;
                return;
            }
            h->used = 0;
        }
    }
}
static void put_u8(mfc_harvest_t *h, uint8_t v)  { stage(h, &v, 1); }
static void put_u16(mfc_harvest_t *h, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    stage(h, b, 2);
}
static void put_u32(mfc_harvest_t *h, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8),
                     (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    stage(h, b, 4);
}

void mfc_harvest_init(mfc_harvest_t *h, mfc_harvest_flush_fn flush, void *flush_ctx) {
    if (h == NULL) return;
    for (uint16_t i = 0; i < MFC_HARVEST_STAGING_BYTES; i++) h->buf[i] = 0;
    h->used = 0;
    h->flush = flush;
    h->flush_ctx = flush_ctx;
    h->live_crc = 0;
    h->crc_on = false;
    h->state = ST_INIT;
    h->cur_type = 0;
    h->cur_expected_samples = 0;
    h->cur_written_samples = 0;
    h->records_written = 0;
    h->status = (flush == NULL) ? MFC_HARVEST_INVALID_INPUT : MFC_HARVEST_OK;
}

mfc_harvest_status_t mfc_harvest_status(const mfc_harvest_t *h) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    return h->status;
}

mfc_harvest_status_t mfc_harvest_begin_file(mfc_harvest_t *h,
                                            const mfc_harvest_file_info_t *info) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (info == NULL) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (h->state != ST_INIT) { h->status = MFC_HARVEST_STATE; return h->status; }
    if (info->uid_len != 4 && info->uid_len != 7 && info->uid_len != 10) {
        h->status = MFC_HARVEST_INVALID_INPUT; return h->status;
    }

    h->crc_on = true;
    h->live_crc = 0xFFFFFFFFu;
    put_u8(h, MFC_HARVEST_MAGIC0);
    put_u8(h, MFC_HARVEST_MAGIC1);
    put_u8(h, MFC_HARVEST_MAGIC2);
    put_u8(h, MFC_HARVEST_MAGIC3);
    put_u16(h, MFC_HARVEST_FORMAT_VERSION);
    put_u16(h, MFC_HARVEST_HEADER_SIZE);
    put_u32(h, info->capability_flags);
    put_u8(h, info->uid_len);
    for (uint8_t i = 0; i < 10; i++) put_u8(h, info->uid[i]);
    put_u16(h, info->atqa);
    put_u8(h, info->sak);
    put_u32(h, info->capture_ticks);
    put_u32(h, info->record_count);          /* bytes 0..33 hashed */
    h->crc_on = false;
    put_u32(h, h->live_crc ^ 0xFFFFFFFFu);    /* header_crc32 (bytes 34..37) */

    if (h->status == MFC_HARVEST_OK) h->state = ST_FILE_OPEN;
    return h->status;
}

/* Open a record: writes framing (type, version, record_len) under a fresh CRC. */
static void record_begin(mfc_harvest_t *h, uint8_t type, uint32_t record_len,
                         uint32_t expected_samples) {
    h->crc_on = true;
    h->live_crc = 0xFFFFFFFFu;
    put_u8(h, type);
    put_u8(h, MFC_HARVEST_RECORD_VERSION);
    put_u32(h, record_len);
    h->cur_type = type;
    h->cur_expected_samples = expected_samples;
    h->cur_written_samples = 0;
    h->state = ST_RECORD_OPEN;
}

static void put_cardonly_hdr(mfc_harvest_t *h, const mfc_card_only_hdr_t *hdr) {
    put_u8(h, hdr->src_block);
    put_u8(h, hdr->src_keytype);
    put_u8(h, hdr->tgt_block);
    put_u8(h, hdr->tgt_keytype);
    put_u8(h, hdr->known_key_ref ? 1u : 0u);
    if (hdr->known_key_ref) {
        for (uint8_t i = 0; i < KEY_BYTES; i++) put_u8(h, hdr->known_key[i]);
    }
}

mfc_harvest_status_t mfc_harvest_write_mfkey64(mfc_harvest_t *h,
                                               const mfc_mfkey64_record_t *r) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (r == NULL) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (h->state != ST_FILE_OPEN) { h->status = MFC_HARVEST_STATE; return h->status; }

    uint32_t rlen = REC_FRAMING + MFKEY64_PAYLOAD + REC_TRAILER;   /* 28 */
    record_begin(h, MFC_HARVEST_TYPE_MFKEY64, rlen, 0);
    put_u8(h, r->block);
    put_u8(h, r->keytype);
    put_u32(h, r->nt);
    put_u32(h, r->nr_enc);
    put_u32(h, r->ar_enc);
    put_u32(h, r->at_enc);
    return mfc_harvest_end_record(h);
}

static mfc_harvest_status_t begin_cardonly(mfc_harvest_t *h, uint8_t type,
                                           const mfc_card_only_hdr_t *hdr,
                                           uint32_t sample_count,
                                           uint32_t sample_size,
                                           uint32_t extra_hdr,   /* anchor/static/count field */
                                           bool count_is_u32) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (hdr == NULL) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (h->state != ST_FILE_OPEN) { h->status = MFC_HARVEST_STATE; return h->status; }
    if (hdr->known_key_ref > 1u) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (sample_count > MFC_HARVEST_MAX_SAMPLES) { h->status = MFC_HARVEST_BOUNDS; return h->status; }

    uint32_t hdr_bytes = CARDONLY_HDR_BASE + (hdr->known_key_ref ? KEY_BYTES : 0u)
                         + extra_hdr + (count_is_u32 ? 4u : 2u);
    uint32_t rlen = REC_FRAMING + hdr_bytes + sample_count * sample_size + REC_TRAILER;
    record_begin(h, type, rlen, sample_count);
    put_cardonly_hdr(h, hdr);
    return h->status;
}

mfc_harvest_status_t mfc_harvest_begin_nested(mfc_harvest_t *h,
                                              const mfc_card_only_hdr_t *hdr,
                                              uint32_t anchor_nt,
                                              uint16_t sample_count) {
    mfc_harvest_status_t s = begin_cardonly(h, MFC_HARVEST_TYPE_NESTED, hdr,
                                            sample_count, NESTED_SAMPLE, 4u, false);
    if (s != MFC_HARVEST_OK) return s;
    put_u32(h, anchor_nt);
    put_u16(h, sample_count);
    return h->status;
}

mfc_harvest_status_t mfc_harvest_begin_static(mfc_harvest_t *h,
                                              const mfc_card_only_hdr_t *hdr,
                                              uint32_t static_nt,
                                              uint16_t sample_count) {
    mfc_harvest_status_t s = begin_cardonly(h, MFC_HARVEST_TYPE_STATIC, hdr,
                                            sample_count, NONCE_SAMPLE, 4u, false);
    if (s != MFC_HARVEST_OK) return s;
    put_u32(h, static_nt);
    put_u16(h, sample_count);
    return h->status;
}

mfc_harvest_status_t mfc_harvest_begin_hardnested(mfc_harvest_t *h,
                                                  const mfc_card_only_hdr_t *hdr,
                                                  uint32_t sample_count) {
    mfc_harvest_status_t s = begin_cardonly(h, MFC_HARVEST_TYPE_HARDNESTED, hdr,
                                            sample_count, NONCE_SAMPLE, 0u, true);
    if (s != MFC_HARVEST_OK) return s;
    put_u32(h, sample_count);
    return h->status;
}

mfc_harvest_status_t mfc_harvest_add_nested_sample(mfc_harvest_t *h,
                                                   const mfc_nested_sample_t *s) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (s == NULL) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (h->state != ST_RECORD_OPEN || h->cur_type != MFC_HARVEST_TYPE_NESTED) {
        h->status = MFC_HARVEST_STATE; return h->status;
    }
    if (h->cur_written_samples >= h->cur_expected_samples) {
        h->status = MFC_HARVEST_BOUNDS; return h->status;
    }
    put_u32(h, s->nt_enc);
    put_u8(h, s->par);
    put_u16(h, s->dist);
    if (h->status == MFC_HARVEST_OK) h->cur_written_samples++;
    return h->status;
}

mfc_harvest_status_t mfc_harvest_add_nonce_sample(mfc_harvest_t *h,
                                                  const mfc_nonce_sample_t *s) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (s == NULL) { h->status = MFC_HARVEST_INVALID_INPUT; return h->status; }
    if (h->state != ST_RECORD_OPEN ||
        (h->cur_type != MFC_HARVEST_TYPE_STATIC &&
         h->cur_type != MFC_HARVEST_TYPE_HARDNESTED)) {
        h->status = MFC_HARVEST_STATE; return h->status;
    }
    if (h->cur_written_samples >= h->cur_expected_samples) {
        h->status = MFC_HARVEST_BOUNDS; return h->status;
    }
    put_u32(h, s->nt_enc);
    put_u8(h, s->par);
    if (h->status == MFC_HARVEST_OK) h->cur_written_samples++;
    return h->status;
}

mfc_harvest_status_t mfc_harvest_end_record(mfc_harvest_t *h) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (h->state != ST_RECORD_OPEN) { h->status = MFC_HARVEST_STATE; return h->status; }
    if (h->cur_written_samples != h->cur_expected_samples) {
        h->status = MFC_HARVEST_BOUNDS; return h->status;   /* under/over-filled */
    }
    uint32_t rcrc = h->live_crc ^ 0xFFFFFFFFu;
    h->crc_on = false;
    put_u32(h, rcrc);
    if (h->status == MFC_HARVEST_OK) {
        h->records_written++;
        h->state = ST_FILE_OPEN;
    }
    return h->status;
}

mfc_harvest_status_t mfc_harvest_finalize(mfc_harvest_t *h) {
    if (h == NULL) return MFC_HARVEST_INVALID_INPUT;
    if (h->status != MFC_HARVEST_OK) return h->status;
    if (h->state != ST_FILE_OPEN) { h->status = MFC_HARVEST_STATE; return h->status; }
    if (h->used > 0) {
        if (!h->flush(h->flush_ctx, h->buf, h->used)) {
            h->status = MFC_HARVEST_FLUSH_FAILED; return h->status;
        }
        h->used = 0;
    }
    h->state = ST_FINALIZED;
    return h->status;
}
