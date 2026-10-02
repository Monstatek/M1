/*
 * m1_t2t_emu_image.h - stable, session-owned MIFARE Ultralight / UL11 /
 * NTAG213 / NTAG215 / NTAG216 emulation image.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Mirrors m1_mfc_emu_image.h's role for MFC 1K: bridges the currently-
 * selected/loaded card in nfc_ctx (mutable, singleton, shared with UI/file-
 * parser/live-read) to the RF listener, which must not touch nfc_ctx after
 * emulation starts.
 *
 * m1_t2t_emu_image_build() performs the full production eligibility gate --
 * a supported, positively-identified variant (UL / UL11 / NTAG213 / NTAG215 /
 * NTAG216 only), UID length and page geometry matching that variant exactly,
 * every page actually read, UID/BCC internally consistent, and NOT password-
 * protected (PWD_AUTH is a separate later task; a protected tag is refused,
 * never emulated insecurely) -- and either fills a self-contained image or
 * fails with a specific, user-reportable reason. It never fabricates missing
 * content and never partially fills *out on failure.
 *
 * The same struct also serves as the session WRITE SHADOW: once armed, the
 * RF listener mutates pages here (subject to lock/OTP/manufacturer-page
 * enforcement) and never touches nfc_ctx/the shared dump buffer directly.
 * m1_t2t_emu_image_save() persists an edited image back to its source .nfc
 * file atomically (write-temp-then-rename; a failed/interrupted save leaves
 * the original file untouched).
 */
#ifndef M1_T2T_EMU_IMAGE_H_
#define M1_T2T_EMU_IMAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M1_T2T_EMU_MAX_PAGES   231U   /* NTAG216, the largest supported variant */
#define M1_T2T_EMU_UID_MAX     10U    /* matches nfc_header_t.uid[10]           */
#define M1_T2T_EMU_PATH_MAX    128U

typedef enum {
    M1_T2T_EMU_OK = 0,
    M1_T2T_EMU_NO_SOURCE,               /* no loaded Ultralight/NTAG card             */
    M1_T2T_EMU_UNSUPPORTED_VARIANT,     /* not UL/UL11/NTAG213/NTAG215/NTAG216         */
    M1_T2T_EMU_INCOMPLETE_PAGES,        /* one or more required pages not read        */
    M1_T2T_EMU_MISSING_SIGNATURE,       /* variant requires READ_SIGNATURE support but none was ever captured */
    M1_T2T_EMU_GEOMETRY_INCONSISTENT,   /* saved variant/version/page-count/highest-page contradict each other */
    M1_T2T_EMU_IDENTITY_INCONSISTENT,   /* UID / BCC / internal byte disagree         */
} m1_t2t_emu_status_t;

typedef struct {
    uint8_t  variant;                          /* M1NFC_T2TVAR_UL/NTAG213/215/216   */
    uint8_t  uid[M1_T2T_EMU_UID_MAX];
    uint8_t  uid_len;
    uint8_t  atqa[2];
    uint8_t  sak;

    uint16_t page_count;
    uint8_t  page[M1_T2T_EMU_MAX_PAGES][4];
    bool     page_valid[M1_T2T_EMU_MAX_PAGES];

    /* Lock state, derived once at build time from the captured pages
     * (page 2 bytes 2-3 for static; the page immediately before CFG0 for
     * dynamic, NTAG21x only). See m1_t2t_emu_image_page_writable(). */
    uint16_t static_lock;          /* page2[2:4], LE                        */
    bool     has_dynamic_lock;     /* false for plain Ultralight (16 pages) */
    uint16_t dynamic_lock;         /* lower 16 bits of the dynamic-lock page */
    uint8_t  dynamic_lock_granularity; /* pages per dynamic-lock bit (2 or 16) */

    /* Password protection state, parsed from CFG0/CFG1 (NXP layout,
     * CFG0 byte3=AUTH0;
     * CFG1 byte0 bit7=PROT). `protected_tag` is what the eligibility gate
     * acts on (auth0 < page_count); `prot` distinguishes read+write
     * protection (true) from write-only protection (false) for the
     * PWD_AUTH-gated read/write enforcement below. */
    bool     has_config_pages;     /* NTAG21x only -- plain UL has none      */
    uint8_t  auth0;
    bool     prot;
    bool     protected_tag;

    /* Genuine PWD_AUTH (0x1B) credential -- ONLY populated when nfc_ctx
     * holds a credential a real tag actually accepted (see
     * nfc_ctx_t2t_credential_valid()), never fabricated and never derived
     * from the masked PWD/PACK page bytes. When credential_valid, the RF
     * listener compares an incoming password against pwd[] and returns
     * pack[] on match; when not, it answers with a genuine NAK exactly as
     * it always has (see nfc_listener.c's T2T_CMD_PWD_AUTH case). */
    uint8_t  pwd[4];
    uint8_t  pack[2];
    bool     credential_valid;

    /* Writable user-page range (inclusive), matching nfc_poller.c's
     * ntag21x_user_bounds() exactly -- manufacturer/UID/lock/OTP/config/
     * PWD/PACK pages all fall outside this range and are never writable
     * through the RF listener regardless of lock-bit state. */
    uint16_t user_first;
    uint16_t user_last;

    /* Genuine authenticity/session data (see m1_t2t_emu_image.c's per-command
     * audit). Never fabricated: each item is either a real captured/loaded
     * value (valid=true) or genuinely absent (valid=false), and the RF
     * listener answers accordingly -- a real value, or the same protocol-
     * correct NAK a real tag would give for an unanswered command. */
    bool     requires_signature;       /* true for UL11/NTAG213/215/216 (GET_VERSION-capable);
                                         * false for plain UL, which predates READ_SIGNATURE */
    uint8_t  signature[32];
    bool     signature_valid;

    uint8_t  counter[3][3];
    bool     counter_valid[3];
    uint8_t  tearing[3];
    bool     tearing_valid[3];

    /* Config-page layout (valid iff has_config_pages): CFG1 = cfg0_page+1,
     * PWD = cfg0_page+2, PACK = cfg0_page+3, per NXP's fixed 4-page tail
     * (CFG0/CFG1/PWD/PACK). The PWD/PACK pages themselves are never used as
     * a credential source -- real silicon masks them to zero on ordinary READ
     * regardless of protection state, so any "captured" bytes there are
     * never genuine. See `pwd`/`pack`/`credential_valid` above for the
     * actual credential path. */
    uint16_t cfg0_page;

    /* Session shadow state. */
    bool     dirty;                /* true once any reader WRITE lands      */
    char     source_path[M1_T2T_EMU_PATH_MAX]; /* origin .nfc file, for Save Changes */
} m1_t2t_emu_image_t;

/* Build a self-contained snapshot from the currently-selected card
 * (nfc_ctx). On M1_T2T_EMU_OK, *out is fully populated (dirty=false) and
 * independent of nfc_ctx from that point on. On any other status, *out is
 * left zeroed -- never a partially-filled or fabricated image. */
m1_t2t_emu_status_t m1_t2t_emu_image_build(m1_t2t_emu_image_t *out);

/* True iff `page` may be written through the RF listener right now: within
 * the variant's user range AND not covered by a set static or dynamic lock
 * bit. Manufacturer/UID/internal/lock/OTP/config/PWD/PACK pages always
 * return false (they are outside user_first..user_last by construction). */
bool m1_t2t_emu_image_page_writable(const m1_t2t_emu_image_t *img, uint16_t page);

/* Apply a reader WRITE to the session shadow. Returns false (no mutation)
 * if the page is not writable right now -- caller must NAK, never silently
 * succeed. Sets img->dirty on success. */
bool m1_t2t_emu_image_write_page(m1_t2t_emu_image_t *img, uint16_t page, const uint8_t data[4]);

/* Persist an edited image back to source_path atomically: write to a
 * "<path>.tmp" file first; only replace the original after the temp file is
 * fully written and closed. A failure at any point leaves the original file
 * untouched. Returns false on any failure (temp file is cleaned up). */
bool m1_t2t_emu_image_save(const m1_t2t_emu_image_t *img);

/* Short, user-facing refusal message for a given status. Never NULL. */
const char *m1_t2t_emu_status_message(m1_t2t_emu_status_t status);

/* GET_VERSION (0x60) response for a variant -- the 8-byte NXP tuple is a
 * FIXED, datasheet-documented constant per chip model (vendor/product/
 * subtype/major/minor/storage-size/protocol), not tag-unique data, so
 * returning it is not fabrication in the sense the rest of this module
 * guards against. Returns false for M1NFC_T2TVAR_UL: the original
 * MIFARE Ultralight predates GET_VERSION and does not support it -- the
 * caller must not answer, not invent a tuple. */
bool m1_t2t_emu_image_get_version(uint8_t variant, uint8_t out[8]);

/* CFG0 page index for a variant (single source of truth, shared with the
 * live-read protection-parsing path in nfc_poller.c so the two never
 * drift apart). CFG1/PWD/PACK follow at +1/+2/+3. Returns false (no
 * config pages at all) for M1NFC_T2TVAR_UL. */
bool m1_t2t_emu_image_cfg0_page(uint8_t variant, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* M1_T2T_EMU_IMAGE_H_ */
