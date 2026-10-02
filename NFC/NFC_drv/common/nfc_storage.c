/* See COPYING.txt for license details. */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "nfc_storage.h"
#include "m1_sdcard.h"
#include "nfc_fileio.h"
#include "nfc_ctx.h"
#include "m1_nfc.h"     /* M1NFC_FAM_*, M1NFC_TECH_* */
#include "privateprofilestring.h"  /* INI style parsing for header */
#include "logger.h"     /* platformLog */
#include "m1_t2t_emu_image.h"      /* m1_t2t_emu_image_cfg0_page() -- shared CFG0 lookup */

#define NFC_STORAGE_MIN_DUMP_UNITS  1

typedef struct {
    uint8_t  tech;      /* M1NFC_TECH_* */
    uint8_t  family;    /* M1NFC_FAM_*  */
    uint16_t unit_size; /* Page/block size */

    /* Raw, as-declared T2T identity/geometry fields (Ultralight family
     * only) -- read once during header parsing, cross-checked and turned
     * into the actual nfc_ctx variant/expected-pages/version AFTER the
     * body has been parsed (the cross-check needs the highest actually-
     * present "Page NNN:" index, which isn't known until then). Never
     * used directly as ground truth on their own -- see
     * t2t_resolve_and_validate_geometry(). */
    bool     t2t_variant_present;
    uint8_t  t2t_variant_declared;      /* M1NFC_T2TVAR_* from "T2T Variant:", if present+recognized */
    uint8_t  t2t_version[8];
    uint8_t  t2t_version_len;           /* 0 if "T2T Version:" absent or not exactly 8 bytes */
    bool     t2t_pages_present;
    uint32_t t2t_pages_declared;        /* from "Pages:", if present */

    /* Genuine PWD_AUTH credential (Unlock feature) -- both PWD and PACK
     * must be present with the exact expected length for the credential
     * to be considered genuine; a file with only one of the two (or a
     * wrong-length value) is contradictory/malformed and MUST be rejected,
     * never partially trusted. See the post-body-parse block in
     * nfc_storage_load_file() that validates and applies these. */
    bool     t2t_pwd_present;
    uint8_t  t2t_pwd[4];
    bool     t2t_pack_present;
    uint8_t  t2t_pack[2];
} nfc_family_info_t;


/*============================================================================*/
/**
 * @brief Remove leading whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_ltrim(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/*============================================================================*/
/**
 * @brief Remove trailing whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_rtrim(char* s)
{
    size_t len = strlen(s);
    while (len > 0 &&
           (s[len-1] == ' ' || s[len-1] == '\t' ||
            s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

/*============================================================================*/
/**
 * @brief Remove leading and trailing whitespace from string
 * @param s String to trim (modified in place)
 * @return Pointer to trimmed string
 */
/*============================================================================*/
static char* str_trim(char* s)
{
    return str_ltrim(str_rtrim(s));
}

/*============================================================================*/
/**
 * @brief Convert a single character to 0~15 hex value
 * @param c Character to convert
 * @return Hex value (0-15) on success, -1 on failure
 */
/*============================================================================*/
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/*============================================================================*/
/**
 * @brief Parse HEX byte sequence from string (supports "AA BB CC" or "AABBCC" format)
 * @param s Input string containing hex bytes
 * @param out Output buffer for parsed bytes
 * @param max Maximum number of bytes to parse
 * @param out_len Pointer to store actual parsed length
 * @return 0 on success, 1 if second digit missing, 2 if invalid char/hex, 3 if buffer full
 */
/*============================================================================*/
static int parse_hex_bytes(char *s, uint8_t *out, size_t max, size_t *out_len)
{
    size_t      n        = 0;
    int         have_hi  = 0;
    uint8_t     hi_nib   = 0;
    unsigned char *p     = (unsigned char *)str_trim(s);

    while (*p)
    {
        /* Skip all whitespace */
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
            continue;
        }

        /* Non-hex character is treated as error */
        if (!isxdigit(*p)) {
            return 2;   // invalid char
        }

        int v = hex_nibble(*p);
        if (v < 0) {
            return 2;   // invalid hex
        }

        if (!have_hi) {
            /* Store upper nibble */
            hi_nib  = (uint8_t)v;
            have_hi = 1;
        } else {
            /* Combine with lower nibble to complete 1 byte */
            if (n >= max) {
                return 3;   // out buffer full
            }
            out[n++] = (uint8_t)((hi_nib << 4) | (uint8_t)v);
            have_hi  = 0;
        }

        p++;
    }

    /* Error if hex string has odd number of characters */
    if (have_hi) {
        return 1;   // second digit missing
    }

    if (out_len) {
        *out_len = n;
    }
    return 0;
}


/*============================================================================*/
/**
 * @brief Mark unit as valid in valid_bits bitmap
 * @param valid_bits Pointer to valid bits bitmap
 * @param valid_bytes Size of valid_bits buffer in bytes
 * @param idx Unit index to mark as valid
 */
/*============================================================================*/
static void mark_unit_valid(uint8_t* valid_bits, uint32_t valid_bytes, uint32_t idx)
{
    if (!valid_bits) return;
    uint32_t byte_idx = idx >> 3;
    uint8_t  bit_mask = (uint8_t)(1u << (idx & 7));
    if (byte_idx >= valid_bytes) return;
    valid_bits[byte_idx] |= bit_mask;
}


/*============================================================================*/
/**
 * @brief Parse device type string and fill family info structure
 * @param s Device type string (e.g., "Classic", "Ultralight/NTAG")
 * @param out Pointer to output structure to fill
 * @return 0 on success, -1 on failure
 */
/*============================================================================*/
static int parse_device_type(const char* s, nfc_family_info_t* out)
{
    if (!s || !out) return -1;

    if (strncmp(s, "Classic", 7) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_CLASSIC;
        out->unit_size = 16;
        return 0;
    }

    //if (strncmp(s, "Mifare Classic", 14) == 0) {
    //    out->tech      = M1NFC_TECH_A;
    //    out->family    = M1NFC_FAM_CLASSIC;
    //    out->unit_size = 16;
    //    return 0;
    //}

    if (strncmp(s, "Ultralight/NTAG", 15) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_ULTRALIGHT;
        out->unit_size = 4;   /* Type2 page */
        return 0;
    }

#ifdef M1NFC_FAM_DESFIRE
    if (strncmp(s, "DESFire", 7) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_DESFIRE;
        out->unit_size = 16;  /* Arbitrary, adjust as needed */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_T4T
    if (strncmp(s, "ISO14443-4A", 11) == 0) {
        out->tech      = M1NFC_TECH_A;
        out->family    = M1NFC_FAM_T4T;
        out->unit_size = 4;   /* Logical block unit (arbitrary) */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_FELICA
    if (strncmp(s, "Felica", 6) == 0) {
        out->tech      = M1NFC_TECH_F;
        out->family    = M1NFC_FAM_FELICA;
        out->unit_size = 16;  /* Service block size (arbitrary) */
        return 0;
    }
#endif

#ifdef M1NFC_FAM_15693
    if (strncmp(s, "ISO15693", 8) == 0) {
        out->tech      = M1NFC_TECH_V;
        out->family    = M1NFC_FAM_15693;
        out->unit_size = 4;   /* block_size: can be adjusted per project */
        return 0;
    }
#endif

    return -1;
}

/*============================================================================*/
/**
 * @brief Parse header section using INI style parsing (hybrid approach)
 * 
 * Uses privateprofilestring.c for header fields (Filetype, Version, Device type, UID, ATQA, SAK, ATS)
 * This provides consistency with RFID parsing and better maintainability.
 * 
 * @param c Pointer to NFC context
 * @param faminfo Pointer to output family info structure
 * @param file_path Full path to the NFC file
 * @return NFC_STORAGE_OK on success, error code on failure
 */
/*============================================================================*/
/*============================================================================*/
/* T2T variant <-> name / canonical geometry table.
 *
 * Single source of truth for the file-format's explicit "T2T Variant:"
 * name string and the true, fixed total page count each named variant
 * has. Indexed directly by M1NFC_T2TVAR_* (0=UNKNOWN..8=NTAG216, all
 * contiguous -- see nfc_ctx.h).
 *
 * This REPLACES the previous "guess the variant purely from the highest
 * saved page number" heuristic,
 * which guessed the variant purely from "highest page actually stored +
 * 1". That heuristic is exactly the mechanism that let a genuinely
 * truncated 231-page NTAG216 read (stopped early at page 42 for any RF
 * reason) round-trip through save+reload as a complete-looking 42-page
 * NTAG203: the guess cannot distinguish "a genuine 42-page tag" from "a
 * truncated read of any larger one" -- every count is ambiguous under a
 * truncation model, not just 42. See t2t_resolve_and_validate_geometry():
 * a file with no explicit "T2T Variant:"/"T2T Version:" now resolves to
 * UNKNOWN (requires a fresh read), never a guess.
 */
/*============================================================================*/
#define T2T_VARIANT_TABLE_COUNT 9U   /* M1NFC_T2TVAR_UNKNOWN..M1NFC_T2TVAR_NTAG216 */

static const char *const T2T_VARIANT_NAMES[T2T_VARIANT_TABLE_COUNT] = {
    [M1NFC_T2TVAR_UNKNOWN] = NULL,
    [M1NFC_T2TVAR_UL]      = "UL",
    [M1NFC_T2TVAR_ULC]     = "ULC",
    [M1NFC_T2TVAR_UL11]    = "UL11",
    [M1NFC_T2TVAR_UL21]    = "UL21",
    [M1NFC_T2TVAR_NTAG203] = "NTAG203",
    [M1NFC_T2TVAR_NTAG213] = "NTAG213",
    [M1NFC_T2TVAR_NTAG215] = "NTAG215",
    [M1NFC_T2TVAR_NTAG216] = "NTAG216",
};

static const uint16_t T2T_VARIANT_PAGES[T2T_VARIANT_TABLE_COUNT] = {
    [M1NFC_T2TVAR_UNKNOWN] = 0U,
    [M1NFC_T2TVAR_UL]      = 16U,
    [M1NFC_T2TVAR_ULC]     = 48U,
    [M1NFC_T2TVAR_UL11]    = 20U,
    [M1NFC_T2TVAR_UL21]    = 41U,
    [M1NFC_T2TVAR_NTAG203] = 42U,
    [M1NFC_T2TVAR_NTAG213] = 45U,
    [M1NFC_T2TVAR_NTAG215] = 135U,
    [M1NFC_T2TVAR_NTAG216] = 231U,
};

const char *nfc_t2t_variant_name(uint8_t variant)
{
    if (variant >= T2T_VARIANT_TABLE_COUNT) return NULL;
    return T2T_VARIANT_NAMES[variant];
}

static uint8_t t2t_variant_from_name(const char *name)
{
    if (name == NULL) return M1NFC_T2TVAR_UNKNOWN;
    for (uint8_t v = 0U; v < T2T_VARIANT_TABLE_COUNT; v++) {
        if ((T2T_VARIANT_NAMES[v] != NULL) && (strcmp(T2T_VARIANT_NAMES[v], name) == 0)) {
            return v;
        }
    }
    return M1NFC_T2TVAR_UNKNOWN;
}

static uint16_t t2t_variant_canonical_pages(uint8_t variant)
{
    if (variant >= T2T_VARIANT_TABLE_COUNT) return 0U;
    return T2T_VARIANT_PAGES[variant];
}

/*============================================================================*/
/**
 * @brief Map a raw 8-byte GET_VERSION tuple to a variant, for the saved-file
 * cross-check only.
 *
 * Mirrors nfc_poller.c's t2t_variant_from_version() byte-6 mapping exactly
 * (GET_VERSION-capable variants only: UL11/UL21/NTAG213/NTAG215/NTAG216 --
 * UL/ULC/NTAG203 predate GET_VERSION and never produce a tuple at all).
 * Duplicated here rather than exposed from that legacy RF-protocol file so
 * this module's validation stays free of any RFAL/HAL dependency -- the
 * same reasoning already documented for the page-count table above.
 * @retval One of M1NFC_T2TVAR_UL11/_UL21/_NTAG213/_NTAG215/_NTAG216, or
 *         M1NFC_T2TVAR_UNKNOWN for an absent, malformed, or unrecognized
 *         tuple (never forced into a model, matching the live-read side).
 */
/*============================================================================*/
static uint8_t t2t_variant_from_version_bytes(const uint8_t *ver, uint8_t len)
{
    if ((ver == NULL) || (len != 8U)) return M1NFC_T2TVAR_UNKNOWN;
    if (ver[1] != 0x04U) return M1NFC_T2TVAR_UNKNOWN;                 /* trust NXP-vendor tuples only */
    if ((ver[3] == 0x05U) && (ver[4] == 0x02U)) return M1NFC_T2TVAR_UNKNOWN; /* NTAG I2C, out of scope */

    switch (ver[6]) {
        case 0x00U: case 0x0BU: return M1NFC_T2TVAR_UL11;
        case 0x0EU:              return M1NFC_T2TVAR_UL21;
        case 0x0FU:              return M1NFC_T2TVAR_NTAG213;
        case 0x11U:              return M1NFC_T2TVAR_NTAG215;
        case 0x13U:              return M1NFC_T2TVAR_NTAG216;
        default:                 return M1NFC_T2TVAR_UNKNOWN;
    }
}

/*============================================================================*/
/**
 * @brief Resolve and validate a just-parsed Ultralight-family file's
 * variant/expected-page-count/version-tuple triple, applying every
 * cross-check the persistence correction requires. Called once, after the
 * body has been fully parsed (the cross-check needs the highest actually-
 * present "Page NNN:" index).
 *
 * On success, sets nfc_ctx's variant and expected-page-count to the
 * validated result (variant may legitimately be UNKNOWN for a true legacy
 * file with none of the new fields -- that is unidentifiable, not
 * corrupt). On any contradiction, sets both to UNKNOWN/0 and returns
 * false so the caller can flag the file as corrupt (M1_T2T_EMU_GEOMETRY_
 * INCONSISTENT / "Invalid saved tag data") -- geometry is never guessed or
 * silently coerced into consistency.
 *
 * @param fi                Parsed header fields (t2t_variant_*, t2t_version*,
 *                          t2t_pages_*)
 * @param highest_page_seen Highest "Page NNN:" index actually present in
 *                          the body, or -1 if none
 * @retval true  resolved cleanly (possibly to UNKNOWN, which is not an error)
 * @retval false a genuine contradiction/out-of-range combination was found
 */
/*============================================================================*/
static bool t2t_resolve_and_validate_geometry(const nfc_family_info_t *fi, int32_t highest_page_seen)
{
    uint8_t variant = fi->t2t_variant_present ? fi->t2t_variant_declared : M1NFC_T2TVAR_UNKNOWN;

    /* Version-tuple cross-check: only meaningful when the tuple maps to a
     * recognized model. An unrecognized-but-genuinely-present tuple never
     * forces or contradicts anything (matching the live-read side's own
     * "do not force a model" rule for an unrecognized GET_VERSION size
     * byte) -- but a RECOGNIZED tuple disagreeing with an explicit "T2T
     * Variant:" name is exactly the kind of self-inconsistent save this
     * check exists to catch. */
    if (fi->t2t_version_len == 8U) {
        uint8_t ver_variant = t2t_variant_from_version_bytes(fi->t2t_version, fi->t2t_version_len);
        if (ver_variant != M1NFC_T2TVAR_UNKNOWN) {
            if (fi->t2t_variant_present) {
                if (ver_variant != variant) {
                    platformLog("T2T geometry: variant/version contradiction (declared=%u version-implies=%u)\r\n",
                               (unsigned)variant, (unsigned)ver_variant);
                    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
                    nfc_ctx_set_t2t_expected_pages(0U);
                    return false;
                }
            } else {
                /* No explicit variant name, but a genuine, recognized
                 * version tuple IS "other saved evidence" strong enough to
                 * establish the variant on its own (unlike a bare page
                 * count, this cannot be produced by truncation). */
                variant = ver_variant;
            }
        }
    }

    if (variant == M1NFC_T2TVAR_UNKNOWN) {
        /* Genuinely unidentifiable: no variant name, no usable version
         * tuple. A true pre-this-task legacy file, or a tag that was
         * never identified. Never guess from raw page count -- see the
         * table's header comment. Requires a fresh physical read, exactly
         * like a missing signature does. */
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return true;
    }

    uint16_t canonical = t2t_variant_canonical_pages(variant);
    if (canonical == 0U) {
        /* Defensive: a recognized enum value with no table entry should be
         * impossible (the table covers every named variant), but never
         * silently proceed with a 0-page "expected" geometry. */
        platformLog("T2T geometry: variant %u has no known page-count geometry\r\n", (unsigned)variant);
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }

    if (fi->t2t_pages_present && (fi->t2t_pages_declared != (uint32_t)canonical)) {
        platformLog("T2T geometry: Pages: %lu disagrees with variant %u's real geometry (%u)\r\n",
                   (unsigned long)fi->t2t_pages_declared, (unsigned)variant, (unsigned)canonical);
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }

    if ((highest_page_seen >= 0) && ((uint32_t)highest_page_seen >= (uint32_t)canonical)) {
        platformLog("T2T geometry: Page %ld is out of range for variant %u (expects 0..%u)\r\n",
                   (long)highest_page_seen, (unsigned)variant, (unsigned)(canonical - 1U));
        nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
        nfc_ctx_set_t2t_expected_pages(0U);
        return false;
    }

    nfc_ctx_set_t2t_variant(variant);
    nfc_ctx_set_t2t_expected_pages(canonical);
    return true;
}

static nfc_storage_result_t nfc_storage_parse_header_ini(
        nfc_run_ctx_t* c,
        nfc_family_info_t* faminfo,
        const char* file_path)
{
    char buf[200];
    ParsedValue data;
    bool saw_filetype = false;
    bool saw_devtype  = false;
    nfc_storage_result_t result;
    ProfileSession sess;

    memset(faminfo, 0, sizeof(*faminfo));

    /* Single open for this whole header parse: every field lookup below
     * scans this ONE already-open file from the top (same match semantics
     * as the plain GetPrivateProfileXxx() calls this replaced), instead of
     * each field independently doing its own open+scan+close. A fully
     * populated Ultralight/NTAG or DESFire+Clipper record previously did
     * ~19-22 separate f_open/f_close cycles here; this is now exactly 1. */
    if (!profile_session_open(&sess, file_path)) {
        platformLog("Filetype/Version Parsing Fail\r\n");
        return NFC_STORAGE_ERR_FORMAT;
    }

    /* 1) Validate Filetype and Version */
    data.buf = buf;
    data.max_len = sizeof(buf);
    if (!isValidHeaderFieldSession(&data, "M1 NFC device", "4", &sess)) {
        platformLog("Filetype/Version Parsing Fail\r\n");
        result = NFC_STORAGE_ERR_FORMAT;
        goto done;
    }
    saw_filetype = true;

    /* 2) Parse Device type */
    data.buf = buf;
    data.max_len = sizeof(buf);
    if (!GetPrivateProfileStringS(&data, "Device type", &sess)) {
        platformLog("Device type not found\r\n");
        result = NFC_STORAGE_ERR_FORMAT;
        goto done;
    }
    if (parse_device_type(data.buf, faminfo) != 0) {
        platformLog("Unsupported Device type: %s\r\n", data.buf);
        result = NFC_STORAGE_ERR_UNSUP;
        goto done;
    }
    c->head.tech   = faminfo->tech;
    c->head.family = faminfo->family;
    saw_devtype    = true;

    /* 3) Parse UID */
    // Use temporary buffer like RFID does, then copy to c->head.uid
    uint8_t tmp_uid[10];
    memset(tmp_uid, 0, sizeof(tmp_uid));
    data.buf = tmp_uid;
    data.max_len = sizeof(tmp_uid);

    // Debug: Check what we're trying to parse
    platformLog("[NFC Storage] Parsing UID from file: %s\r\n", file_path);

    if (!GetPrivateProfileHexS(&data, "UID", &sess)) {
        platformLog("UID not found\r\n");
        result = NFC_STORAGE_ERR_FORMAT;
        goto done;
    }

    // Debug: Check parsed result
    //platformLog("[NFC Storage] UID parse result: out_len=%d\r\n", data.v.hex.out_len);
    //platformLog("[NFC Storage] UID tmp buffer: %s\r\n", hex2Str(tmp_uid, data.v.hex.out_len));

    c->head.uid_len = (uint8_t)data.v.hex.out_len;
    if (c->head.uid_len == 0 || c->head.uid_len > 10) {
        platformLog("Invalid UID length: %d\r\n", c->head.uid_len);
        result = NFC_STORAGE_ERR_FORMAT;
        goto done;
    }
    // Copy parsed UID to context (like RFID does)
    memcpy(c->head.uid, tmp_uid, c->head.uid_len);
    platformLog("[NFC Storage] UID final: len=%d, bytes=%s\r\n",
                c->head.uid_len, hex2Str(c->head.uid, c->head.uid_len));

    /* 4) Parse ATQA (optional, Tech A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        data.buf = c->head.a.atqa;
        data.max_len = 2;
        if (GetPrivateProfileHexS(&data, "ATQA", &sess) && data.v.hex.out_len == 2) {
            c->head.a.has_atqa = true;
        }
    }

    /* 5) Parse SAK (optional, Tech A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        uint8_t tmp_sak[1];
        data.buf = tmp_sak;
        data.max_len = 1;
        if (GetPrivateProfileHexS(&data, "SAK", &sess) && data.v.hex.out_len == 1) {
            c->head.a.sak = tmp_sak[0];
            c->head.a.has_sak = true;
        }
    }

    /* 6) Parse ATS (optional, ISO14443-4A only) */
    if (faminfo->tech == M1NFC_TECH_A) {
        data.buf = c->head.a.ats;
        data.max_len = sizeof(c->head.a.ats);
        if (GetPrivateProfileHexS(&data, "ATS", &sess) && data.v.hex.out_len > 0) {
            c->head.a.ats_len = (uint8_t)data.v.hex.out_len;
        }
    }

    /* 7) Genuine authenticity/session data (optional; Ultralight/NTAG only).
     * Each key is independently optional -- a file saved before this task
     * existed, or one where the original read never captured a given item,
     * simply has no line for it. GetPrivateProfileHexS() returning false
     * leaves the corresponding nfc_ctx entry invalid (see the clears just
     * above/below in nfc_storage_load_file()), never zero-filled: a missing
     * "Signature:" line must read back as "not captured", not as 32 zero
     * bytes standing in for real originality data. */
    if (faminfo->family == M1NFC_FAM_ULTRALIGHT) {
        uint8_t sig[32];
        data.buf = sig;
        data.max_len = sizeof(sig);
        if (GetPrivateProfileHexS(&data, "Signature", &sess) && data.v.hex.out_len == 32) {
            nfc_ctx_set_t2t_signature(sig);
        }

        static const char *const kCounterKeys[3] = { "Counter0", "Counter1", "Counter2" };
        static const char *const kTearingKeys[3] = { "Tearing0", "Tearing1", "Tearing2" };
        for (uint8_t i = 0; i < 3U; i++) {
            uint8_t cnt[3];
            data.buf = cnt;
            data.max_len = sizeof(cnt);
            if (GetPrivateProfileHexS(&data, kCounterKeys[i], &sess) && data.v.hex.out_len == 3) {
                nfc_ctx_set_t2t_counter(i, cnt);
            }

            uint8_t tear[1];
            data.buf = tear;
            data.max_len = sizeof(tear);
            if (GetPrivateProfileHexS(&data, kTearingKeys[i], &sess) && data.v.hex.out_len == 1) {
                nfc_ctx_set_t2t_tearing(i, tear[0]);
            }
        }

        /* 8) Explicit T2T identity/geometry -- "T2T Variant:", "T2T
         * Version:" (the raw GET_VERSION tuple, NEVER to be confused with
         * this file format's own top-level "Version: 4" field parsed in
         * step 1) and "Pages:" (now the DECLARED/EXPECTED total, not a
         * count to be re-derived from how many "Page NNN:" lines happen to
         * follow). Stored raw here; t2t_resolve_and_validate_geometry()
         * (called from nfc_storage_load_file() once the body has been
         * parsed, since the cross-check also needs the highest actually-
         * present page index) turns these into the validated nfc_ctx
         * variant/expected-pages/version, or rejects the combination as
         * corrupt. A file with none of these three fields is a true
         * pre-this-task legacy save -- left fully unresolved (UNKNOWN)
         * rather than guessed from page count; see that function. */
        char variant_name[16];
        data.buf = variant_name;
        data.max_len = sizeof(variant_name);
        if (GetPrivateProfileStringS(&data, "T2T Variant", &sess)) {
            uint8_t v = t2t_variant_from_name(variant_name);
            if (v != M1NFC_T2TVAR_UNKNOWN) {
                faminfo->t2t_variant_present   = true;
                faminfo->t2t_variant_declared  = v;
            }
        }

        data.buf = faminfo->t2t_version;
        data.max_len = sizeof(faminfo->t2t_version);
        if (GetPrivateProfileHexS(&data, "T2T Version", &sess) && data.v.hex.out_len == 8) {
            faminfo->t2t_version_len = 8U;
        }

        if (GetPrivateProfileUintS(&data, "Pages", &sess)) {
            faminfo->t2t_pages_present  = true;
            faminfo->t2t_pages_declared = data.v.u32;
        }

        /* Genuine PWD_AUTH credential -- see the struct field comment for
         * why both must be present with exact lengths before either is
         * trusted (validated together in nfc_storage_load_file(), not
         * here, since that is where the rest of this file's contradiction-
         * rejection logic already lives). */
        data.buf = faminfo->t2t_pwd;
        data.max_len = sizeof(faminfo->t2t_pwd);
        if (GetPrivateProfileHexS(&data, "PWD", &sess) && data.v.hex.out_len == 4) {
            faminfo->t2t_pwd_present = true;
        }

        data.buf = faminfo->t2t_pack;
        data.max_len = sizeof(faminfo->t2t_pack);
        if (GetPrivateProfileHexS(&data, "PACK", &sess) && data.v.hex.out_len == 2) {
            faminfo->t2t_pack_present = true;
        }
    }

    /* 9) MIFARE DESFire identity (Tier 1) and deeper-read scalar fields
     * (DESFire family only). Written directly into the live context --
     * unlike the T2T fields above, none of these need a post-body
     * cross-check, so there is no reason to stage them in faminfo first.
     * Absence of "DESFire Version" simply leaves c->desfire.present false,
     * matching every other "never fabricate a value that wasn't captured"
     * field in this loader. */
    if (faminfo->family == M1NFC_FAM_DESFIRE) {
        uint8_t v28[28];
        data.buf = v28;
        data.max_len = sizeof(v28);
        if (GetPrivateProfileHexS(&data, "DESFire Version", &sess) && data.v.hex.out_len == 28) {
            c->desfire.present = true;
            memcpy(c->desfire.v, v28, 28);
        }

        if (GetPrivateProfileUintS(&data, "DESFire Free Memory", &sess)) {
            c->desfire_deep.free_memory_valid = true;
            c->desfire_deep.free_memory_bytes = data.v.u32;
        }

        uint8_t mks[2];
        data.buf = mks;
        data.max_len = sizeof(mks);
        if (GetPrivateProfileHexS(&data, "DESFire Master Key Settings", &sess) && data.v.hex.out_len == 2) {
            c->desfire_deep.master_key_settings_valid = mf_desfire_key_settings_parse(mks, 2, &c->desfire_deep.master_key_settings);
        }

        if (GetPrivateProfileUintS(&data, "DESFire Apps Truncated", &sess) && data.v.u32 != 0U) {
            c->desfire_deep.apps_truncated = true;
        }
        if (GetPrivateProfileUintS(&data, "DESFire Apps Protected", &sess) && data.v.u32 != 0U) {
            c->desfire_deep.apps_protected = true;
        }

        /* Bounded card-interpretation result (Clipper) -- persisted
         * directly (see nfc_file.c), loaded directly, never recomputed
         * from the generic DESFire data (the ride-history file's full
         * byte capture is intentionally never persisted). Absence of
         * "Clipper Card:" simply leaves c->transit.card_id at
         * NfcTransitCardUnknown, matching every other "never fabricate a
         * value that wasn't captured" field in this loader. */
        if (GetPrivateProfileUintS(&data, "Clipper Card", &sess) && data.v.u32 != 0U) {
            c->transit.card_id = NfcTransitCardClipper;

            char type_buf[24];
            data.buf = type_buf;
            data.max_len = sizeof(type_buf);
            if (GetPrivateProfileStringS(&data, "Clipper Type", &sess)) {
                if (strcmp(type_buf, "Mobile Device") == 0) c->transit.card_type_label = "Mobile Device";
                else if (strcmp(type_buf, "Card") == 0) c->transit.card_type_label = "Card";
            }

            if (GetPrivateProfileUintS(&data, "Clipper Serial", &sess)) {
                c->transit.serial_valid = true;
                c->transit.serial_number = data.v.u32;
            }
            /* Balance is signed -- read as a plain uint32 bit pattern via
             * the generic reader is not safe for a leading '-', so parse it
             * directly with a small local scan instead of GetPrivateProfileUintS. */
            {
                char bal_buf[16];
                data.buf = bal_buf;
                data.max_len = sizeof(bal_buf);
                if (GetPrivateProfileStringS(&data, "Clipper Balance", &sess)) {
                    long v = strtol(bal_buf, NULL, 10);
                    if (v >= -32768 && v <= 32767) {
                        c->transit.balance_valid = true;
                        c->transit.balance_cents = (int16_t)v;
                    }
                }
            }
            if (GetPrivateProfileUintS(&data, "Clipper Last Update", &sess)) {
                c->transit.last_update_valid = true;
                c->transit.last_update_1900 = data.v.u32;
            }
            if (GetPrivateProfileUintS(&data, "Clipper Terminal", &sess)) {
                c->transit.last_terminal_valid = true;
                c->transit.last_terminal_id = (uint16_t)data.v.u32;
            }
            if (GetPrivateProfileUintS(&data, "Clipper Txn", &sess)) {
                c->transit.last_txn_valid = true;
                c->transit.last_txn_id = (uint16_t)data.v.u32;
            }
            if (GetPrivateProfileUintS(&data, "Clipper Counter", &sess)) {
                c->transit.counter_valid = true;
                c->transit.counter = (uint16_t)data.v.u32;
            }
            if (GetPrivateProfileUintS(&data, "Clipper Rides Truncated", &sess) && data.v.u32 != 0U) {
                c->transit.rides_truncated = true;
            }
        }
    }

    if (!saw_filetype || !saw_devtype) {
        result = NFC_STORAGE_ERR_FORMAT;
        goto done;
    }

    result = NFC_STORAGE_OK;

done:
    profile_session_close(&sess);
    return result;
}

/*============================================================================*/
/**
 * @brief Store unit data from "Page N:" / "Block N:" line into dump buffer
 * @param dump_buf Dump buffer to store data
 * @param unit_size Size of each unit (page/block)
 * @param unit_count Total number of units
 * @param valid_bits Valid bits bitmap (optional)
 * @param valid_bits_bytes Size of valid_bits buffer
 * @param max_seen_unit Pointer to update maximum seen unit index
 * @param idx Unit index to store
 * @param src_bytes Source data bytes
 * @param src_len Length of source data
 */
/*============================================================================*/
static void nfc_storage_store_unit(
        uint8_t*    dump_buf,
        uint16_t    unit_size,
        uint32_t    unit_count,
        uint8_t*    valid_bits,
        uint32_t    valid_bits_bytes,
        uint32_t*   max_seen_unit,
        uint32_t    idx,
        const uint8_t* src_bytes,
        size_t      src_len)
{
    if (!dump_buf || unit_size == 0 || unit_count == 0) return;

    if (src_len < unit_size) {
        /* Pad remaining part with 0 if insufficient */
        uint8_t tmp[32] = {0};
        size_t copy = src_len;
        if (copy > sizeof(tmp)) copy = sizeof(tmp);
        memcpy(tmp, src_bytes, copy);

        if (idx < unit_count) {
            memcpy(dump_buf + idx * unit_size, tmp, unit_size);
            mark_unit_valid(valid_bits, valid_bits_bytes, idx);
            if (max_seen_unit && idx > *max_seen_unit) *max_seen_unit = idx;
        }
        return;
    }

    if (idx >= unit_count) return; /* Buffer overflow → clip */

    memcpy(dump_buf + idx * unit_size, src_bytes, unit_size);
    mark_unit_valid(valid_bits, valid_bits_bytes, idx);
    if (max_seen_unit && idx > *max_seen_unit) *max_seen_unit = idx;
}

/*============================================================================*/
/**
 * @brief Parse body section: "Page N:" for Type2, "Block N:" for Classic
 * @param c Pointer to NFC context
 * @param faminfo Pointer to family info structure
 * @param dump_buf Dump buffer to store parsed data
 * @param dump_buf_bytes Size of dump buffer
 * @param valid_bits Valid bits bitmap (optional)
 * @param valid_bits_bytes Size of valid_bits buffer
 * @return NFC_STORAGE_OK on success, error code on failure
 * @note Felica, 15693, etc. can parse header only for now
 */
/*============================================================================*/
static nfc_storage_result_t nfc_storage_parse_body(
        nfc_run_ctx_t*     c,
        const nfc_family_info_t* faminfo,
        uint8_t*           dump_buf,
        uint32_t           dump_buf_bytes,
        uint8_t*           valid_bits,
        uint32_t           valid_bits_bytes)
{
    if (!dump_buf || dump_buf_bytes == 0)
        return NFC_STORAGE_ERR_NO_BUFFER;

    nfc_parser_t* ps = &c->parser;

    uint16_t unit_size  = faminfo->unit_size;
    if (unit_size == 0) unit_size = 4;
    uint32_t unit_count = dump_buf_bytes / unit_size;
    if (unit_count < NFC_STORAGE_MIN_DUMP_UNITS)
        return NFC_STORAGE_ERR_NO_BUFFER;

    memset(dump_buf, 0, dump_buf_bytes);
    if (valid_bits && valid_bits_bytes > 0)
        memset(valid_bits, 0, valid_bits_bytes);

    uint32_t max_seen = 0;

    while (1) {
        int n = nfcfio_getline(&ps->io, ps->line, sizeof(ps->line));
        if (n == -1) break;          /* EOF */
        if (n < 0)  return NFC_STORAGE_ERR_IO;

        char* line = str_trim(ps->line);
        if (line[0] == '\0') continue;
        if (line[0] == '#')  continue;

        /* Classic: "Block N:" */
        if (strncmp(line, "Block ", 6) == 0) {
            unsigned long idx = 0;
            char* colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (sscanf(line, "Block %lu", &idx) != 1) continue;

            char* data_str = str_trim(colon + 1);
            uint8_t bytes[32];
            size_t  len = 0;
            uint8_t b = parse_hex_bytes(data_str, bytes, sizeof(bytes), &len);
            if (b != 0)
            {
                platformLog("Block Parsing Fail [%d]\r\n",b);
                return NFC_STORAGE_ERR_FORMAT;
            }
            nfc_storage_store_unit(dump_buf, unit_size, unit_count,
                                   valid_bits, valid_bits_bytes,
                                   &max_seen, (uint32_t)idx,
                                   bytes, len);
            continue;
        }

        /* Type2: "Page N:" */
        if (strncmp(line, "Page ", 5) == 0) {
            unsigned long idx = 0;
            char* colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (sscanf(line, "Page %lu", &idx) != 1) continue;

            char* data_str = str_trim(colon + 1);
            uint8_t bytes[32];
            size_t  len = 0;
            uint8_t b = parse_hex_bytes(data_str, bytes, sizeof(bytes), &len);
            if (b != 0)
            {
                platformLog("Page Parsing Fail [%d]\r\n",b);
                return NFC_STORAGE_ERR_FORMAT;
            }
            nfc_storage_store_unit(dump_buf, unit_size, unit_count,
                                   valid_bits, valid_bits_bytes,
                                   &max_seen, (uint32_t)idx,
                                   bytes, len);
            continue;
        }

        /* Classic: "Sector N KeyA:" / "Sector N KeyB:" -- optional, only
         * present for keys the original read actually recovered. Absence of
         * a line (old file, or a key never found) MUST leave that key
         * unknown -- never a zero/default fill. Independent of the "Block "
         * loop above so a key line in any position/order still parses. */
        if (strncmp(line, "Sector ", 7) == 0) {
            unsigned sector = 0;
            char keytag[8] = {0};
            char* colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (sscanf(line, "Sector %u %7s", &sector, keytag) != 2) continue;
            if (sector >= M1NFC_MFC_SECTORS_MAX) continue;

            char* data_str = str_trim(colon + 1);
            uint8_t bytes[8];
            size_t  len = 0;
            if (parse_hex_bytes(data_str, bytes, sizeof(bytes), &len) != 0) continue;
            if (len != 6) continue;   /* a sector key is exactly 6 bytes */

            nfc_mfc_sector_t* sc = &c->mfc.sec[sector];
            if (strcmp(keytag, "KeyA") == 0) {
                memcpy(sc->key_a, bytes, 6);
                sc->key_a_found = true;
            } else if (strcmp(keytag, "KeyB") == 0) {
                memcpy(sc->key_b, bytes, 6);
                sc->key_b_found = true;
            }
            continue;
        }

        /* DESFire: "Application <AID>:" and its sub-lines ("Key Settings:",
         * "Key Versions Truncated:", "Key Version NN:", "Files Truncated:",
         * "File NN:", "File NN Data:"). Every sub-line looks up its
         * application (and, for File lines, its file) by ID among entries
         * already seen this load rather than relying on line order -- a
         * sub-line appearing before its own "Application <AID>:" line is
         * skipped, never misattributed to the wrong application/file. */
        if (strncmp(line, "DESFire Application ", 20) == 0) {
            const char *rest = line + 20;
            if (strlen(rest) < 6) continue;
            char aidbuf[7];
            memcpy(aidbuf, rest, 6);
            aidbuf[6] = '\0';
            uint8_t aid_bytes[3];
            size_t  aid_len = 0;
            if (parse_hex_bytes(aidbuf, aid_bytes, sizeof(aid_bytes), &aid_len) != 0 || aid_len != 3) continue;
            const char *after_aid = rest + 6;

            mf_desfire_app_t *app = NULL;
            for (uint8_t i = 0; i < c->desfire_deep.app_count; i++) {
                if (memcmp(c->desfire_deep.apps[i].id.id, aid_bytes, 3) == 0) {
                    app = &c->desfire_deep.apps[i];
                    break;
                }
            }

            if (after_aid[0] == ':') {
                int select_ok = 0;
                if (sscanf(after_aid, ": %d", &select_ok) != 1) continue;
                if (app == NULL) {
                    if (c->desfire_deep.app_count >= MF_DESFIRE_DEEP_MAX_APPS) continue;
                    app = &c->desfire_deep.apps[c->desfire_deep.app_count++];
                    memset(app, 0, sizeof(*app));
                    memcpy(app->id.id, aid_bytes, 3);
                }
                app->select_ok = (select_ok != 0);
                continue;
            }

            if (app == NULL) continue; /* every sub-line below requires its application to already exist */

            if (strncmp(after_aid, " Key Settings:", 14) == 0) {
                char   *data_str = str_trim((char *)after_aid + 14);
                uint8_t mks[2];
                size_t  mkslen = 0;
                if (parse_hex_bytes(data_str, mks, sizeof(mks), &mkslen) == 0 && mkslen == 2) {
                    app->key_settings_valid = mf_desfire_key_settings_parse(mks, 2, &app->key_settings);
                }
                continue;
            }

            if (strncmp(after_aid, " Key Versions Truncated:", 24) == 0) {
                int v = 0;
                if (sscanf(after_aid + 24, "%d", &v) == 1 && v != 0) app->key_versions_truncated = true;
                continue;
            }

            if (strncmp(after_aid, " Key Version ", 13) == 0) {
                unsigned val = 0;
                const char *colon = strchr(after_aid + 13, ':');
                if (colon != NULL && sscanf(colon + 1, "%x", &val) == 1) {
                    if (app->key_version_count < MF_DESFIRE_MAX_KEYS) {
                        app->key_versions[app->key_version_count++] = (uint8_t)val;
                    }
                }
                continue;
            }

            if (strncmp(after_aid, " Files Truncated:", 17) == 0) {
                int v = 0;
                if (sscanf(after_aid + 17, "%d", &v) == 1 && v != 0) app->files_truncated = true;
                continue;
            }

            if (strncmp(after_aid, " File ", 6) == 0) {
                const char *file_rest = after_aid + 6;
                if (strlen(file_rest) < 2) continue;
                char fidbuf[3];
                memcpy(fidbuf, file_rest, 2);
                fidbuf[2] = '\0';
                uint8_t fid_bytes[1];
                size_t  fid_len = 0;
                if (parse_hex_bytes(fidbuf, fid_bytes, 1, &fid_len) != 0 || fid_len != 1) continue;
                uint8_t     file_id  = fid_bytes[0];
                const char *after_fid = file_rest + 2;

                mf_desfire_file_t *file = NULL;
                for (uint8_t i = 0; i < app->file_count; i++) {
                    if (app->files[i].id == file_id) { file = &app->files[i]; break; }
                }

                if (strncmp(after_fid, " Data:", 6) == 0) {
                    if (file == NULL) continue; /* the main File line must precede its Data line */
                    char *data_str = str_trim((char *)after_fid + 6);
                    uint8_t bytes[MF_DESFIRE_DEEP_FILE_DATA_CAP];
                    size_t  len = 0;
                    if (parse_hex_bytes(data_str, bytes, sizeof(bytes), &len) == 0) {
                        memcpy(file->data, bytes, len);
                        file->data_len = (uint16_t)len; /* ground truth is what was actually parsed here */
                    }
                    continue;
                }

                if (after_fid[0] == ':') {
                    if (file == NULL) {
                        if (app->file_count >= MF_DESFIRE_MAX_FILES_PER_APP) continue;
                        file = &app->files[app->file_count++];
                        memset(file, 0, sizeof(*file));
                        file->id = file_id;
                    }

                    int    settings_valid = 0, type = 0, comm = 0, credit_en = 0, read_status = 0;
                    unsigned access_rights = 0, saved_data_len = 0, tmac_opt = 0, tmac_ver = 0;
                    unsigned long size = 0, value_lo = 0, value_hi = 0, value_credit = 0, rec_size = 0, rec_max = 0, rec_cur = 0;
                    int fields_scanned = sscanf(after_fid,
                                    ": %d %d %d %x %lx %lx %lx %lx %d %lx %lx %lx %x %x %d %u",
                                    &settings_valid, &type, &comm, &access_rights, &size,
                                    &value_lo, &value_hi, &value_credit, &credit_en,
                                    &rec_size, &rec_max, &rec_cur, &tmac_opt, &tmac_ver,
                                    &read_status, &saved_data_len);
                    if (fields_scanned != 16) continue;
                    (void)saved_data_len; /* informational only -- actual data_len comes from the "... Data:" line, if any */

                    file->settings_valid                        = (settings_valid != 0);
                    file->settings.valid                        = file->settings_valid;
                    file->settings.type                         = (mf_desfire_file_type_t)type;
                    file->settings.comm                         = (mf_desfire_comm_t)comm;
                    file->settings.access_rights                = (uint16_t)access_rights;
                    file->settings.size                         = (uint32_t)size;
                    file->settings.value_lo_limit               = (int32_t)(uint32_t)value_lo;
                    file->settings.value_hi_limit               = (int32_t)(uint32_t)value_hi;
                    file->settings.value_limited_credit         = (int32_t)(uint32_t)value_credit;
                    file->settings.value_limited_credit_enabled = (credit_en != 0);
                    file->settings.record_size                  = (uint32_t)rec_size;
                    file->settings.record_max                   = (uint32_t)rec_max;
                    file->settings.record_cur                   = (uint32_t)rec_cur;
                    file->settings.tmac_key_option              = (uint8_t)tmac_opt;
                    file->settings.tmac_key_version              = (uint8_t)tmac_ver;
                    file->read_status                           = (mf_desfire_file_read_status_t)read_status;
                }
                continue;
            }
            continue;
        }

        /* Clipper ride-history entries: "Clipper Ride NN: agency fare
         * vehicle time_on time_off zone_on zone_off". Appended in order to
         * c->transit.rides[] -- has_time_on/has_time_off are re-derived
         * from time_on/time_off being non-zero, exactly matching how the
         * live-read path (nfc_transit_clipper_parse_rides) sets them, so
         * there is nothing extra to persist for those two flags. */
        if (strncmp(line, "Clipper Ride ", 13) == 0) {
            unsigned idx = 0; unsigned agency = 0; int fare = 0; unsigned vehicle = 0;
            unsigned long time_on = 0, time_off = 0; unsigned zone_on = 0, zone_off = 0;
            int fields = sscanf(line + 13, "%u: %x %d %u %lu %lu %u %u",
                                 &idx, &agency, &fare, &vehicle, &time_on, &time_off, &zone_on, &zone_off);
            if ((fields == 8) && (c->transit.ride_count < NFC_TRANSIT_MAX_RIDES)) {
                nfc_transit_ride_t *r = &c->transit.rides[c->transit.ride_count++];
                memset(r, 0, sizeof(*r));
                r->agency_id     = (uint16_t)agency;
                r->fare_cents    = (int16_t)fare;
                r->vehicle_id    = (uint16_t)vehicle;
                r->time_on_1900  = (uint32_t)time_on;
                r->has_time_on   = (time_on != 0UL);
                r->time_off_1900 = (uint32_t)time_off;
                r->has_time_off  = (time_off != 0UL);
                r->zone_on_id    = (uint16_t)zone_on;
                r->zone_off_id   = (uint16_t)zone_off;
            }
            continue;
        }

        /* Felica/ISO15693, etc. can add separate processing here */
    }

    /* Parsing complete → set nfc_ctx.dump metadata */
    nfc_ctx_set_dump(unit_size,
                     unit_count,
                     0,             /* origin */
                     dump_buf,
                     valid_bits,
                     max_seen,
                     true); //dump set

    return NFC_STORAGE_OK;
}



/*============================================================================*/
/**
 * @brief Load and parse .nfc text file to fill nfc_ctx
 * 
 * Parses .nfc file format:
 * - Header: Filetype, Version, Device type, UID, ATQA, SAK, ATS, etc.
 * - Body: "Page N:" / "Block N:" lines → stored in dump buffer
 * 
 * @param path Full path on SD card (e.g., "/nfc/card1.nfc")
 * @param dump_buf Workspace pointer to store dump data
 * @param dump_buf_bytes Total size of dump_buf (in bytes)
 * @param valid_bits Unit validity bitmap (optional, NULL allowed)
 * @param valid_bits_bytes Size of valid_bits buffer (bytes, 1 byte per 8 units)
 * @return NFC_STORAGE_OK on success, error code on failure
 */
/*============================================================================*/
nfc_storage_result_t nfc_storage_load_file(
        const char* path,
        uint8_t*    dump_buf,
        uint32_t    dump_buf_bytes,
        uint8_t*    valid_bits,
        uint32_t    valid_bits_bytes)

{
    if (!path || !dump_buf || dump_buf_bytes == 0)
        return NFC_STORAGE_ERR_NO_BUFFER;

    nfc_run_ctx_t* c = nfc_ctx_get();
    nfc_family_info_t faminfo;
    nfc_storage_result_t ret;

    /* Start file session: set source_kind, path */
    nfc_ctx_begin_file(path);

    /* Initialize header/dump. Also clear .mfc (sector keys) so a previously
     * loaded/read card's keys can never leak into the one being opened now --
     * a file with no "Sector N KeyX:" lines (old format, or a key that was
     * never found) must leave every key genuinely unknown, not inherited. */
    memset(&c->head, 0, sizeof(c->head));
    nfc_ctx_clear_dump();
    nfc_ctx_clear_mfc();
    nfc_ctx_clear_t2t_signature();
    nfc_ctx_clear_t2t_counters();
    nfc_ctx_clear_t2t_tearing();
    nfc_ctx_clear_t2t_protection();
    nfc_ctx_clear_t2t_credential();
    nfc_ctx_clear_desfire();
    c->parser.parse_error = 0;

    /* ---------- Phase 1: Parse header using INI style ---------- */
    ret = nfc_storage_parse_header_ini(c, &faminfo, path);

    if (ret != NFC_STORAGE_OK) {
        c->file.sys_error = 1;
        return ret;
    }

    /* ---------- Phase 2: Parse body (pages/blocks) ---------- */
    if (!nfcfio_open_read(&c->parser.io, path)) {
        c->file.sys_error = 1;
        return NFC_STORAGE_ERR_IO;
    }

    ret = nfc_storage_parse_body(c, &faminfo,
                                 dump_buf, dump_buf_bytes,
                                 valid_bits, valid_bits_bytes);

    nfcfio_close(&c->parser.io);

    if (ret == NFC_STORAGE_OK) {
        /* Restore .mfc (type/sector count/key-validity) for a Classic-family
         * load, mirroring the live-read population in m1_mfc_read_card() so
         * a saved card looks identical to a freshly-read one to every
         * consumer (Card Info, Write, Emulate). Sector keys themselves were
         * already filled in by the "Sector N KeyX:" lines above (or left
         * unknown if absent) -- this only derives the summary counters and
         * type/sector-count metadata from the header + what was parsed. */
        if (faminfo.family == M1NFC_FAM_CLASSIC) {
            uint8_t mfctype = m1nfc_mfc_type_from_sak(c->head.a.sak);
            uint8_t sectors = m1nfc_mfc_sectors_for_type(mfctype);

            c->mfc.valid         = true;
            c->mfc.type          = mfctype;
            c->mfc.sectors_total = sectors;
            c->mfc.keys_total    = (uint8_t)(sectors * 2U);

            uint8_t keys_found   = 0;
            uint8_t sectors_read = 0;
            for (uint8_t s = 0; s < sectors && s < M1NFC_MFC_SECTORS_MAX; s++) {
                if (c->mfc.sec[s].key_a_found) keys_found++;
                if (c->mfc.sec[s].key_b_found) keys_found++;

                uint16_t first = m1nfc_mfc_sector_first_block(s);
                uint8_t  nblk  = m1nfc_mfc_sector_blocks(s);
                bool     all   = true;
                for (uint8_t bidx = 0; bidx < nblk; bidx++) {
                    if (!nfc_ctx_mfc_block_valid((uint16_t)(first + bidx))) { all = false; break; }
                }
                if (all) sectors_read++;
            }
            c->mfc.keys_found   = keys_found;
            c->mfc.sectors_read = sectors_read;
        } else if (faminfo.family == M1NFC_FAM_ULTRALIGHT) {
            /* Restore the raw GET_VERSION tuple first (clear-then-
             * conditionally-set, matching signature/counter/tearing's own
             * pattern) so a file with no "T2T Version:" line can never
             * inherit a stale tuple from whatever was previously loaded. */
            nfc_ctx_clear_t2t_version();
            if (faminfo.t2t_version_len == 8U) {
                nfc_ctx_set_t2t_version(faminfo.t2t_version, faminfo.t2t_version_len);
            }

            uint16_t pc = nfc_ctx_get_t2t_page_count();   /* highest "Page NNN:" seen, +1 */
            int32_t highest_page_seen = (pc > 0U) ? ((int32_t)pc - 1) : -1;
            bool geom_ok = t2t_resolve_and_validate_geometry(&faminfo, highest_page_seen);
            nfc_ctx_set_t2t_geometry_corrupt(!geom_ok);

            /* AUTH0/PROT/AUTHLIM: re-derived from the just-reloaded CFG0/
             * CFG1 "Page N:" bytes -- the exact same values a live read
             * would have parsed (nfc_poller.c), since AUTH0's own config
             * pages are ordinary, unmasked page data and round-trip
             * through the writer/parser like any other page. Only
             * attempted once the variant resolved cleanly (never on a
             * corrupt/unidentified file). */
            uint16_t cfg0_page = 0;
            if (geom_ok && m1_t2t_emu_image_cfg0_page(nfc_ctx_get_t2t_variant(), &cfg0_page)) {
                uint8_t cfg0[4], cfg1[4];
                if (nfc_ctx_get_t2t_page(cfg0_page, cfg0) &&
                    nfc_ctx_get_t2t_page((uint16_t)(cfg0_page + 1U), cfg1)) {
                    nfc_ctx_set_t2t_auth0(cfg0[3]);
                    nfc_ctx_set_t2t_prot((cfg1[0] & 0x80U) != 0U);
                    nfc_ctx_set_t2t_authlim((uint8_t)(cfg1[0] & 0x07U));
                }
            }

            /* Genuine PWD_AUTH credential: both PWD and PACK must be
             * present with the exact expected length, or the credential is
             * left invalid entirely -- a file with only one of the two is
             * contradictory (never partially trusted, never inferred). */
            if (faminfo.t2t_pwd_present && faminfo.t2t_pack_present) {
                nfc_ctx_set_t2t_credential(faminfo.t2t_pwd, faminfo.t2t_pack);
            }
        } else {
            nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);  /* avoid stale T2T name leaking */
            nfc_ctx_set_t2t_geometry_corrupt(false);
        }

        nfc_ctx_refresh_ui();
        c->file.sys_error = 0;
    } else {
        c->file.sys_error = 1;
    }

    return ret;
}        
