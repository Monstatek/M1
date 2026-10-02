/*
 * mfc_dict_solver.c - MIFARE Classic nested-DICTIONARY key solver.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See mfc_dict_solver.h. Pure logic (parser + dictionary filter); no RF, no
 * allocation. Uses the Proxmark-derived crypto1_recover.* primitives.
 */
#include "mfc_dict_solver.h"
#include "crypto1_recover.h"
#include "mfc_harvest.h"      /* .m1h magic / version / sizes / record types */

/* CRC-32/IEEE (reflected poly 0xEDB88320) -- identical to mfc_harvest.c. */
static uint32_t crc32_buf(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define REC_FRAMING   6U    /* type + version + record_len */
#define REC_TRAILER   4U    /* record_crc32 */
#define CARDONLY_BASE 5U    /* src2 + tgt2 + known_key_ref */
#define KEY_BYTES     6U
#define NESTED_SAMPLE 7U    /* nt_enc + par + dist */
#define NONCE_SAMPLE  5U    /* nt_enc + par */

mfc_solve_status_t mfc_solver_parse_m1h(const uint8_t *buf, size_t len,
                                        mfc_solver_capture_t *out) {
    if (buf == NULL || out == NULL) return MFC_SOLVE_INVALID_INPUT;

    /* ---- file header (38 bytes) ---- */
    if (len < (size_t)MFC_HARVEST_HEADER_SIZE) return MFC_SOLVE_BAD_RECORD;
    if (buf[0] != MFC_HARVEST_MAGIC0 || buf[1] != MFC_HARVEST_MAGIC1 ||
        buf[2] != MFC_HARVEST_MAGIC2 || buf[3] != MFC_HARVEST_MAGIC3)
        return MFC_SOLVE_BAD_RECORD;
    if (rd16(buf + 4) != MFC_HARVEST_FORMAT_VERSION) return MFC_SOLVE_UNSUPPORTED;
    if (rd16(buf + 6) != MFC_HARVEST_HEADER_SIZE)    return MFC_SOLVE_BAD_RECORD;
    if (rd32(buf + 34) != crc32_buf(buf, 34))        return MFC_SOLVE_BAD_RECORD;

    uint8_t uid_len = buf[12];
    if (uid_len != 4 && uid_len != 7 && uid_len != 10) return MFC_SOLVE_BAD_RECORD;
    const uint8_t *uid = &buf[13];
    const uint8_t *u = &uid[uid_len - 4U];              /* cuid = last 4 UID bytes, BE */
    out->cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) |
                ((uint32_t)u[2] << 8) | u[3];

    /* ---- record ---- */
    size_t o = MFC_HARVEST_HEADER_SIZE;
    if (len < o + REC_FRAMING + REC_TRAILER) return MFC_SOLVE_BAD_RECORD;
    uint8_t  rtype = buf[o];
    uint8_t  rver  = buf[o + 1];
    uint32_t rlen  = rd32(buf + o + 2);
    if (rver != MFC_HARVEST_RECORD_VERSION) return MFC_SOLVE_UNSUPPORTED;
    if (rlen < REC_FRAMING + CARDONLY_BASE + 4U + 2U + REC_TRAILER) return MFC_SOLVE_BAD_RECORD;
    if ((uint64_t)o + rlen > (uint64_t)len) return MFC_SOLVE_BAD_RECORD;
    if (rd32(buf + o + rlen - REC_TRAILER) != crc32_buf(buf + o, rlen - REC_TRAILER))
        return MFC_SOLVE_BAD_RECORD;
    if (rtype != MFC_HARVEST_TYPE_NESTED && rtype != MFC_HARVEST_TYPE_STATIC)
        return MFC_SOLVE_UNSUPPORTED;

    size_t p = o + REC_FRAMING;
    out->src_block   = buf[p];
    out->src_keytype = buf[p + 1];
    out->tgt_block   = buf[p + 2];
    out->tgt_keytype = buf[p + 3];
    out->known_key_ref = buf[p + 4];
    p += CARDONLY_BASE;
    if (out->known_key_ref > 1U) return MFC_SOLVE_BAD_RECORD;
    if (out->known_key_ref) {
        for (uint8_t i = 0; i < KEY_BYTES; i++) out->known_key[i] = buf[p + i];
        p += KEY_BYTES;
    } else {
        for (uint8_t i = 0; i < KEY_BYTES; i++) out->known_key[i] = 0;
    }
    p += 4U;                                   /* anchor_nt (Nested) / static_nt (Static) */
    uint16_t count = rd16(buf + p);
    p += 2U;

    uint32_t sample_size = (rtype == MFC_HARVEST_TYPE_NESTED) ? NESTED_SAMPLE : NONCE_SAMPLE;
    uint32_t hdr_bytes = CARDONLY_BASE + (out->known_key_ref ? KEY_BYTES : 0U) + 4U + 2U;
    uint32_t expect = REC_FRAMING + hdr_bytes + (uint32_t)count * sample_size + REC_TRAILER;
    if (expect != rlen) return MFC_SOLVE_BAD_RECORD;   /* count vs length must agree */
    if (count > MFC_SOLVER_MAX_NONCES) return MFC_SOLVE_BAD_RECORD;

    for (uint16_t i = 0; i < count; i++) {
        out->nonces[i].cuid   = out->cuid;
        out->nonces[i].nt_enc = rd32(buf + p);
        out->nonces[i].par    = buf[p + 4];
        p += sample_size;
    }
    out->rec_type = rtype;
    out->count    = count;
    return MFC_SOLVE_OK;
}

mfc_solve_status_t mfc_solver_run(const mfc_solver_capture_t *cap,
                                  mfc_solver_key_iter_fn iter, void *iter_ctx,
                                  uint64_t known_src_key, bool is_weak,
                                  uint64_t *out_key,
                                  mfc_solver_progress_fn progress, void *progress_ctx) {
    if (cap == NULL || iter == NULL || out_key == NULL) return MFC_SOLVE_INVALID_INPUT;
    if (cap->count == 0) return MFC_SOLVE_INSUFFICIENT;

    uint64_t distinct_keys[4];
    uint8_t  distinct = 0;
    bool     too_many_distinct = false;
    uint32_t tried = 0;
    uint8_t  k6[6];

    while (iter(iter_ctx, k6)) {
        tried++;
        if (progress != NULL && (tried & 0x3FFu) == 0) {
            if (!progress(progress_ctx, tried)) return MFC_SOLVE_ABORTED;
        }
        uint64_t key = ((uint64_t)k6[0] << 40) | ((uint64_t)k6[1] << 32) |
                       ((uint64_t)k6[2] << 24) | ((uint64_t)k6[3] << 16) |
                       ((uint64_t)k6[4] << 8)  |  (uint64_t)k6[5];
        if (key == known_src_key) continue;         /* never return the source key */

        bool full = true;
        for (uint16_t j = 0; j < cap->count; j++) {
            uint32_t nt = crypto1_recover_decrypt_nt_enc(cap->nonces[j].cuid,
                                                         cap->nonces[j].nt_enc, key);
            if (is_weak && !crypto1_recover_is_weak_prng_nonce(nt)) { full = false; break; }
            if (!crypto1_recover_nonce_matches_parity(nt, nt ^ cap->nonces[j].nt_enc,
                                                      cap->nonces[j].par)) { full = false; break; }
        }
        if (!full) continue;

        bool seen = false;
        for (uint8_t d = 0; d < distinct; d++) if (distinct_keys[d] == key) { seen = true; break; }
        if (!seen) {
            if (distinct < 4) distinct_keys[distinct++] = key;
            else too_many_distinct = true;
        }
    }

    if (distinct == 0) return MFC_SOLVE_NO_KEY;
    *out_key = distinct_keys[0];
    if (distinct > 1 || too_many_distinct) return MFC_SOLVE_AMBIGUOUS;
    return MFC_SOLVE_OK;
}

mfc_solve_status_t mfc_solver_solve_m1h(const uint8_t *buf, size_t len,
                                        mfc_solver_key_iter_fn iter, void *iter_ctx,
                                        uint64_t known_src_key, bool is_weak,
                                        uint64_t *out_key,
                                        uint8_t *out_tgt_block, uint8_t *out_tgt_keytype,
                                        mfc_solver_progress_fn progress, void *progress_ctx) {
    mfc_solver_capture_t cap;
    mfc_solve_status_t st = mfc_solver_parse_m1h(buf, len, &cap);
    if (st != MFC_SOLVE_OK) return st;
    if (out_tgt_block)   *out_tgt_block   = cap.tgt_block;
    if (out_tgt_keytype) *out_tgt_keytype = cap.tgt_keytype;
    return mfc_solver_run(&cap, iter, iter_ctx, known_src_key, is_weak, out_key,
                          progress, progress_ctx);
}
