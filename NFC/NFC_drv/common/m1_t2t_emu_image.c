/*
 * m1_t2t_emu_image.c - see m1_t2t_emu_image.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "m1_t2t_emu_image.h"
#include "nfc_ctx.h"
#include "m1_file_browser.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

/* Launch-scope variant geometry, matching nfc_poller.c's ntag21x_user_
 * bounds() exactly. Everything else is M1_T2T_EMU_UNSUPPORTED_VARIANT.
 *
 * has_dynamic_lock and has_config_pages are INDEPENDENT: NTAG213/215/216
 * have both (5-page tail: dynlock, CFG0, CFG1, PWD, PACK). UL11 has config
 * pages (CFG0/CFG1/PWD/PACK, so it CAN be password-protected and must still
 * be checked/refused) but NO separate dynamic-lock page -- its user range
 * (pages 4-15) is small enough that the static lock bits (page 2, 1:1
 * bit=page for pages 3-15) already cover it entirely, so NXP simply didn't
 * add one. Plain UL has neither. Conflating the two flags (as an earlier
 * version of this table did, when only NTAG21x variants existed here) would
 * either wrongly demand a dynamic-lock page UL11 doesn't have, or wrongly
 * skip UL11's password-protection check. */
typedef struct {
    uint16_t page_count;
    uint16_t user_first, user_last;
    bool     has_dynamic_lock;
    uint16_t dyn_lock_page;             /* valid only if has_dynamic_lock */
    uint8_t  dyn_lock_granularity;      /* valid only if has_dynamic_lock */
    bool     has_config_pages;
    uint16_t cfg0_page;                 /* valid only if has_config_pages; CFG1/PWD/PACK follow at +1/+2/+3 */
    bool     requires_signature;        /* GET_VERSION-capable variants only (see m1_t2t_emu_image.h) */
} t2t_geom_t;

static bool t2t_emu_geometry(uint8_t variant, t2t_geom_t *g)
{
    memset(g, 0, sizeof(*g));
    switch (variant) {
        case M1NFC_T2TVAR_UL:
            g->page_count = 16U; g->user_first = 4U; g->user_last = 15U;
            return true;
        case M1NFC_T2TVAR_UL11:
            g->page_count = 20U; g->user_first = 4U; g->user_last = 15U;
            g->has_config_pages = true; g->cfg0_page = 16U;   /* CFG0=16, CFG1=17, PWD=18, PACK=19 */
            g->requires_signature = true;
            return true;
        case M1NFC_T2TVAR_NTAG213:
            g->page_count = 45U; g->user_first = 4U; g->user_last = 39U;
            g->has_dynamic_lock = true; g->dyn_lock_page = 40U; g->dyn_lock_granularity = 2U;
            g->has_config_pages = true; g->cfg0_page = 41U;
            g->requires_signature = true;
            return true;
        case M1NFC_T2TVAR_NTAG215:
            g->page_count = 135U; g->user_first = 4U; g->user_last = 129U;
            g->has_dynamic_lock = true; g->dyn_lock_page = 130U; g->dyn_lock_granularity = 16U;
            g->has_config_pages = true; g->cfg0_page = 131U;
            g->requires_signature = true;
            return true;
        case M1NFC_T2TVAR_NTAG216:
            g->page_count = 231U; g->user_first = 4U; g->user_last = 225U;
            g->has_dynamic_lock = true; g->dyn_lock_page = 226U; g->dyn_lock_granularity = 16U;
            g->has_config_pages = true; g->cfg0_page = 227U;
            g->requires_signature = true;
            return true;
        default:
            return false;
    }
}

bool m1_t2t_emu_image_cfg0_page(uint8_t variant, uint16_t *out)
{
    t2t_geom_t geom;
    if (!out || !t2t_emu_geometry(variant, &geom) || !geom.has_config_pages) {
        return false;
    }
    *out = geom.cfg0_page;
    return true;
}

m1_t2t_emu_status_t m1_t2t_emu_image_build(m1_t2t_emu_image_t *out)
{
    if (out == NULL) { return M1_T2T_EMU_NO_SOURCE; }
    memset(out, 0, sizeof(*out));

    nfc_run_ctx_t *c = nfc_ctx_get();
    if (c == NULL) { return M1_T2T_EMU_NO_SOURCE; }
    if ((c->head.tech != M1NFC_TECH_A) || (c->head.family != M1NFC_FAM_ULTRALIGHT)) {
        return M1_T2T_EMU_NO_SOURCE;
    }
    if (!c->head.a.has_atqa || !c->head.a.has_sak) {
        return M1_T2T_EMU_NO_SOURCE;
    }

    /* Checked before anything else: a saved file whose declared variant
     * name, raw GET_VERSION tuple, declared Pages:, and highest actually-
     * present page contradict each other (or land out of range) was
     * flagged corrupt by nfc_storage.c's cross-check at load time -- a
     * different, more serious condition than "incomplete" or
     * "unsupported", and never silently coerced into either of those. */
    if (nfc_ctx_t2t_geometry_corrupt()) {
        return M1_T2T_EMU_GEOMETRY_INCONSISTENT;
    }

    uint8_t variant = nfc_ctx_get_t2t_variant();
    t2t_geom_t geom;
    if (!t2t_emu_geometry(variant, &geom)) {
        return M1_T2T_EMU_UNSUPPORTED_VARIANT;
    }

    if (c->head.uid_len != 7U) {
        return M1_T2T_EMU_IDENTITY_INCONSISTENT;
    }

    /* Expected/declared geometry must match the identified variant's real
     * table entry exactly. In practice this is already guaranteed by the
     * cross-check above (nfc_ctx_get_t2t_expected_pages() is only ever set
     * to a variant's own canonical count), but is kept as a direct,
     * defensive check here too rather than relying solely on that
     * invariant holding across every call path (e.g. a live, not-yet-saved
     * read, which never goes through nfc_storage.c's cross-check at all). */
    if (nfc_ctx_get_t2t_expected_pages() != geom.page_count) {
        return M1_T2T_EMU_INCOMPLETE_PAGES;
    }

    /* Every page must have actually been read -- never fabricate. */
    for (uint16_t p = 0; p < geom.page_count; p++) {
        if (!nfc_ctx_t2t_page_valid(p)) {
            return M1_T2T_EMU_INCOMPLETE_PAGES;
        }
    }

    uint8_t page0[4], page1[4], page2[4];
    if (!nfc_ctx_get_t2t_page(0, page0) ||
        !nfc_ctx_get_t2t_page(1, page1) ||
        !nfc_ctx_get_t2t_page(2, page2)) {
        return M1_T2T_EMU_INCOMPLETE_PAGES;
    }

    /* UID/BCC consistency: 7-byte UID uses cascade tag CT=0x88.
     * page0 = UID0 UID1 UID2 BCC0, page1 = UID3 UID4 UID5 UID6,
     * page2 = BCC1 internal lock0 lock1. */
    if (memcmp(page0, c->head.uid, 3U) != 0 || memcmp(page1, &c->head.uid[3], 4U) != 0) {
        return M1_T2T_EMU_IDENTITY_INCONSISTENT;
    }
    uint8_t bcc0 = (uint8_t)(0x88U ^ c->head.uid[0] ^ c->head.uid[1] ^ c->head.uid[2]);
    uint8_t bcc1 = (uint8_t)(c->head.uid[3] ^ c->head.uid[4] ^ c->head.uid[5] ^ c->head.uid[6]);
    if ((page0[3] != bcc0) || (page2[0] != bcc1)) {
        return M1_T2T_EMU_IDENTITY_INCONSISTENT;
    }

    uint16_t static_lock = (uint16_t)(page2[2] | ((uint16_t)page2[3] << 8));

    uint16_t dynamic_lock = 0;
    if (geom.has_dynamic_lock) {
        uint8_t dlp[4];
        if (!nfc_ctx_get_t2t_page(geom.dyn_lock_page, dlp)) {
            return M1_T2T_EMU_INCOMPLETE_PAGES;
        }
        dynamic_lock = (uint16_t)(dlp[0] | ((uint16_t)dlp[1] << 8));
    }

    uint8_t auth0 = 0xFFU;
    bool    prot  = false;
    bool    protected_tag = false;
    if (geom.has_config_pages) {
        uint8_t cfg0[4];
        if (!nfc_ctx_get_t2t_page(geom.cfg0_page, cfg0)) {
            return M1_T2T_EMU_INCOMPLETE_PAGES;
        }
        auth0 = cfg0[3];
        protected_tag = (auth0 != 0xFFU) && (auth0 < geom.page_count);

        if (protected_tag) {
            uint8_t cfg1[4];
            if (!nfc_ctx_get_t2t_page((uint16_t)(geom.cfg0_page + 1U), cfg1)) {
                return M1_T2T_EMU_INCOMPLETE_PAGES;
            }
            prot = ((cfg1[0] & 0x80U) != 0U);
        }
    }

    /* A protected tag is NOT refused outright -- Unlock (this task) exists
     * precisely so a genuinely-authenticated card can still be emulated.
     * If PROT=1 and the tag was never unlocked during the read, the pages
     * at/after auth0 genuinely couldn't be read (real hardware refuses
     * READ past AUTH0 when PROT=1) and the ordinary per-page completeness
     * check below already refuses as INCOMPLETE_PAGES -- no special-casing
     * needed. If PROT=0 (write-only protection), every page reads
     * normally regardless of auth state, and the card is genuinely
     * complete; PWD_AUTH/WRITE access enforcement for pages >= auth0
     * happens in the RF listener (nfc_listener.c), gated by the PER-
     * SESSION authentication state, never here (this struct has no
     * concept of "this particular reader's session"). */

    /* Genuine originality signature is required for every variant that
     * supports READ_SIGNATURE -- without it, a reader cannot
     * complete a read (ReadSignature failure is fatal, per the T7 protocol
     * audit), and this task never answers READ_SIGNATURE with fabricated
     * bytes. A dedicated status/message, distinct from generic page
     * incompleteness: every page can be genuinely present and valid while
     * the signature alone is missing (e.g. a file saved before
     * nfc_profile_save() persisted it), and conflating the two masks the
     * real, specific fix ("read the tag again") behind a misleading
     * generic one. */
    if (geom.requires_signature && !nfc_ctx_t2t_signature_valid()) {
        return M1_T2T_EMU_MISSING_SIGNATURE;
    }

    /* All checks passed -- copy everything into the self-contained image. */
    out->variant = variant;
    memcpy(out->uid, c->head.uid, 7U);
    out->uid_len = 7U;
    out->atqa[0] = c->head.a.atqa[0];
    out->atqa[1] = c->head.a.atqa[1];
    out->sak     = c->head.a.sak;

    out->page_count = geom.page_count;
    for (uint16_t p = 0; p < geom.page_count; p++) {
        uint8_t buf[4];
        nfc_ctx_get_t2t_page(p, buf);   /* already validated above */
        memcpy(out->page[p], buf, 4U);
        out->page_valid[p] = true;
    }

    out->static_lock              = static_lock;
    out->has_dynamic_lock         = geom.has_dynamic_lock;
    out->dynamic_lock             = dynamic_lock;
    out->dynamic_lock_granularity = geom.dyn_lock_granularity;
    out->has_config_pages         = geom.has_config_pages;
    out->auth0                    = auth0;
    out->prot                     = prot;
    out->protected_tag            = protected_tag;
    out->user_first                = geom.user_first;
    out->user_last                 = geom.user_last;

    /* Genuine PWD_AUTH credential, if one was actually accepted by a real
     * tag during this read/load -- never fabricated, never derived from
     * the masked PWD/PACK page bytes. See nfc_ctx.h. */
    out->credential_valid = nfc_ctx_t2t_credential_valid();
    if (out->credential_valid) {
        (void)nfc_ctx_get_t2t_pwd(out->pwd);
        (void)nfc_ctx_get_t2t_pack(out->pack);
    }

    out->requires_signature = geom.requires_signature;
    out->signature_valid    = nfc_ctx_t2t_signature_valid();
    if (out->signature_valid) {
        (void)nfc_ctx_get_t2t_signature(out->signature);
    }
    for (uint8_t i = 0; i < 3U; i++) {
        out->counter_valid[i] = nfc_ctx_t2t_counter_valid(i);
        if (out->counter_valid[i]) { (void)nfc_ctx_get_t2t_counter(i, out->counter[i]); }
        out->tearing_valid[i] = nfc_ctx_t2t_tearing_valid(i);
        if (out->tearing_valid[i]) { (void)nfc_ctx_get_t2t_tearing(i, &out->tearing[i]); }
    }
    out->cfg0_page = geom.cfg0_page;   /* meaningful iff has_config_pages */

    out->dirty = false;
    strncpy(out->source_path, c->file.path, sizeof(out->source_path) - 1U);
    out->source_path[sizeof(out->source_path) - 1U] = '\0';

    return M1_T2T_EMU_OK;
}

bool m1_t2t_emu_image_page_writable(const m1_t2t_emu_image_t *img, uint16_t page)
{
    if (img == NULL) { return false; }
    if ((page < img->user_first) || (page > img->user_last)) { return false; }

    /* Static lock: 1:1 bit=page mapping for pages 3..15 (page 2's own
     * block-lock meta-bits are out of scope -- page 2 is never user-range). */
    if (page <= 15U) {
        if ((img->static_lock & (1U << page)) != 0U) { return false; }
    }

    /* Dynamic lock (NTAG21x only): pages from 16 up to (config_page-2). */
    if (img->has_dynamic_lock && (page >= 16U) && (img->dynamic_lock_granularity != 0U)) {
        uint16_t idx = (uint16_t)((page - 16U) / img->dynamic_lock_granularity);
        if (idx < 16U && (img->dynamic_lock & (1U << idx)) != 0U) { return false; }
    }

    return true;
}

bool m1_t2t_emu_image_write_page(m1_t2t_emu_image_t *img, uint16_t page, const uint8_t data[4])
{
    if ((img == NULL) || (data == NULL)) { return false; }
    if (!m1_t2t_emu_image_page_writable(img, page)) { return false; }

    memcpy(img->page[page], data, 4U);
    img->page_valid[page] = true;
    img->dirty = true;
    return true;
}

bool m1_t2t_emu_image_save(const m1_t2t_emu_image_t *img)
{
    if ((img == NULL) || (img->source_path[0] == '\0')) { return false; }

    char tmp_path[M1_T2T_EMU_PATH_MAX + 4U];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", img->source_path);

    FIL f;
    if (m1_fb_open_new_file(&f, tmp_path) != 0U) { return false; }

    bool ok = true;
    char line[128];

    #define T2T_SAVE_LINE(...) do { \
        snprintf(line, sizeof(line), __VA_ARGS__); \
        if (m1_fb_write_to_file(&f, line, (uint16_t)strlen(line)) != strlen(line)) { ok = false; } \
    } while (0)

    T2T_SAVE_LINE("Filetype: M1 NFC device\r\n");
    T2T_SAVE_LINE("Version: 4\r\n");
    T2T_SAVE_LINE("Device type: Ultralight/NTAG\r\n");

    char uid_str[32];
    int pos = 0;
    for (uint8_t i = 0; i < img->uid_len && pos < (int)sizeof(uid_str) - 3; i++) {
        pos += snprintf(uid_str + pos, sizeof(uid_str) - (size_t)pos,
                         (i + 1U < img->uid_len) ? "%02X " : "%02X", img->uid[i]);
    }
    T2T_SAVE_LINE("UID: %s\r\n", uid_str);
    T2T_SAVE_LINE("ATQA: %02X %02X\r\n", img->atqa[0], img->atqa[1]);
    T2T_SAVE_LINE("SAK: %02X\r\n", img->sak);
    T2T_SAVE_LINE("Pages: %u\r\n", (unsigned)img->page_count);

    for (uint16_t p = 0; ok && (p < img->page_count); p++) {
        if (!img->page_valid[p]) { continue; }
        T2T_SAVE_LINE("Page %03u: %02X %02X %02X %02X\r\n", (unsigned)p,
                      img->page[p][0], img->page[p][1], img->page[p][2], img->page[p][3]);
    }

    /* Genuine authenticity/session data -- omitted (not zero-filled) when
     * never captured, so a reload correctly sees it as still absent. */
    if (ok && img->signature_valid) {
        char sigHex[97];
        int  pos = 0;
        for (uint8_t i = 0; i < 32U && pos < (int)sizeof(sigHex) - 3; i++) {
            pos += snprintf(sigHex + pos, sizeof(sigHex) - (size_t)pos,
                             (i + 1U < 32U) ? "%02X " : "%02X", img->signature[i]);
        }
        T2T_SAVE_LINE("Signature: %s\r\n", sigHex);
    }
    for (uint8_t i = 0; ok && i < 3U; i++) {
        if (img->counter_valid[i]) {
            T2T_SAVE_LINE("Counter%u: %02X %02X %02X\r\n", i,
                          img->counter[i][0], img->counter[i][1], img->counter[i][2]);
        }
        if (img->tearing_valid[i]) {
            T2T_SAVE_LINE("Tearing%u: %02X\r\n", i, img->tearing[i]);
        }
    }
    #undef T2T_SAVE_LINE

    m1_fb_close_file(&f);

    if (!ok) {
        m1_fb_delete_file(tmp_path);
        return false;
    }

    /* Replace the original: delete-then-rename is the best atomicity FatFS
     * offers (no in-place atomic replace primitive). The write above is now
     * fully complete and closed, so the only remaining failure window is the
     * brief gap between the two calls below -- a known, disclosed limitation. */
    if (m1_fb_delete_file(img->source_path) != 0U) {
        m1_fb_delete_file(tmp_path);
        return false;
    }
    if (f_rename(tmp_path, img->source_path) != FR_OK) {
        return false;   /* original already gone -- data is in tmp_path, not silently lost */
    }

    return true;
}

bool m1_t2t_emu_image_get_version(uint8_t variant, uint8_t out[8])
{
    if (out == NULL) { return false; }

    /* NXP datasheet-documented constants: 00 (fixed header) | 04 (NXP vendor
     * ID) | product type (04=NTAG, 03=MIFARE Ultralight EV1) | product
     * subtype | 01 00 (major/minor version) | storage-size byte | 03
     * (protocol type ISO/IEC 14443-3 compliant). Storage-size byte =
     * (n<<1)|1 where 2^n < user_bytes < 2^(n+1): UL11=48B (n=5 -> 0x0B),
     * NTAG213=144B (n=7 -> 0x0F), NTAG215=504B (n=8 -> 0x11), NTAG216=888B
     * (n=9 -> 0x13) -- matching t2t_variant_from_version()'s reverse mapping
     * in nfc_poller.c exactly. Plain original Ultralight predates
     * GET_VERSION entirely and has no tuple to return. */
    static const uint8_t VER_UL11[8]    = { 0x00, 0x04, 0x03, 0x01, 0x01, 0x00, 0x0BU, 0x03 };
    static const uint8_t VER_NTAG213[8] = { 0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0FU, 0x03 };
    static const uint8_t VER_NTAG215[8] = { 0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11U, 0x03 };
    static const uint8_t VER_NTAG216[8] = { 0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x13U, 0x03 };

    const uint8_t *v;
    switch (variant) {
        case M1NFC_T2TVAR_UL11:    v = VER_UL11;    break;
        case M1NFC_T2TVAR_NTAG213: v = VER_NTAG213; break;
        case M1NFC_T2TVAR_NTAG215: v = VER_NTAG215; break;
        case M1NFC_T2TVAR_NTAG216: v = VER_NTAG216; break;
        default: return false;   /* original UL: no GET_VERSION support */
    }
    memcpy(out, v, 8U);
    return true;
}

const char *m1_t2t_emu_status_message(m1_t2t_emu_status_t status)
{
    switch (status) {
        case M1_T2T_EMU_OK:                     return "OK";
        case M1_T2T_EMU_NO_SOURCE:               return "No Ultralight/NTAG card loaded";
        case M1_T2T_EMU_UNSUPPORTED_VARIANT:     return "Tag type not supported";
        case M1_T2T_EMU_INCOMPLETE_PAGES:        return "Complete tag read required";
        case M1_T2T_EMU_MISSING_SIGNATURE:       return "Signature missing / Read tag again";
        case M1_T2T_EMU_GEOMETRY_INCONSISTENT:   return "Invalid saved tag data";
        case M1_T2T_EMU_IDENTITY_INCONSISTENT:   return "Card identity inconsistent";
        default:                                 return "Emulation unavailable";
    }
}
