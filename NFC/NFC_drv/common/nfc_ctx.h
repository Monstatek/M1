/* See COPYING.txt for license details. */

/*
 * nfc_ctx.h
 *
 *
 */

#ifndef NFC_DRV_NFC_CTX_H_
#define NFC_DRV_NFC_CTX_H_

#include <stdint.h>
#include <stdbool.h>
#include "rfal_nfc.h"
#include "nfc_fileio.h"
#include "main.h"
#include "mf_desfire_parse.h"
#include "nfc_transit_clipper.h"
#include "mfc_result.h"   /* mfc_outcome_t (nfc_mfc_info_t.outcome) */
#include "mfc_dict_types.h"   /* nfc_mfc_info_t/nfc_mfc_sector_t/nfc_mfc_scan_t + MFC geometry --
                                * relocated there so mfc_dict_phase.c (and its host tests) can use
                                * these exact types without this file's HAL includes above */
#include "mfc_dict_resume.h"  /* mfc_dict_resume_t (Find Missing Keys persisted resume state) */


/* ------- Size/parser line buffer defaults (redefine in project settings if needed) ------- */
#ifndef NFC_PATH_MAX
#define NFC_PATH_MAX        128
#endif

#ifndef NFC_LINE_MAX
#define NFC_LINE_MAX        256
#endif

#ifndef NFC_TITLE_MAX
#define NFC_TITLE_MAX       24
#endif

#ifndef NFC_UID_TEXT_MAX
#define NFC_UID_TEXT_MAX    32  /* "AA BB CC DD ..." */
#endif

// nfc_ctx.file.source_kind Info
#define LIVE_CARD   0   // Read card
#define LOAD_FILE   1   // Load file


#define M1NFC_FAM_CLASSIC        0
#define M1NFC_FAM_ULTRALIGHT     1
#define M1NFC_FAM_DESFIRE        2
#define M1NFC_FAM_15693          3   /* ISO15693 / NFC-V */
#define M1NFC_FAM_ST25TB         4   /* ST25TB / SRI / SRIX (ISO14443-B based) */
/* Never alias an unclassified NFC-A card to Classic (whose value is zero). */
#define M1NFC_FAM_UNKNOWN        0xFFU

/* ST25TB / SRI / SRIX sub-variant, established during the read from the UID.
 * Keyed on (reversed) uid[2]>>2. The M1 keeps an unrecognized code GENERIC
 * (never guessed, never crash). */
#define M1NFC_TBVAR_GENERIC      0   /* generic ST25TB (unrecognized code) */
#define M1NFC_TBVAR_512AT        1   /* ST25TB512-AT / SRI512 */
#define M1NFC_TBVAR_512AC        2   /* ST25TB512-AC / SRT512 */
#define M1NFC_TBVAR_X512         3   /* SRIX512 */
#define M1NFC_TBVAR_02K          4   /* ST25TB02K / SRI2K */
#define M1NFC_TBVAR_04K          5   /* ST25TB04K / SRI4K */
#define M1NFC_TBVAR_X4K          6   /* SRIX4K */

/* ISO15693 / NFC-V sub-variant, established during the read from the UID.
 * Identifies the variant from the NXP manufacturer byte and
 * ICODE IC-type byte + type-indicator bits) — never the manufacturer byte
 * alone. SLIX-S / SLIX-L are out of scope this increment and fall through to
 * GENERIC. Anything unknown or ambiguous stays GENERIC. */
#define M1NFC_VVAR_GENERIC       0   /* generic ISO15693 */
#define M1NFC_VVAR_SLIX          1   /* NXP ICODE SLIX */
#define M1NFC_VVAR_SLIX2         2   /* NXP ICODE SLIX2 */

/* Type 2 Tag (Ultralight/NTAG) sub-variant, established during the read.
 * UNKNOWN = generic Type 2 / Ultralight (ambiguous or unrecognized).
 * The specific variant is set by the poller from a complete GET_VERSION
 * response (EV1 11/21, NTAG213/215/216) or from the conservative legacy
 * probe chain (Ultralight C -> NTAG203 -> original Ultralight). */
#define M1NFC_T2TVAR_UNKNOWN     0   /* generic Type 2 / Ultralight */
#define M1NFC_T2TVAR_UL          1   /* original MIFARE Ultralight (MF0ICU1) */
#define M1NFC_T2TVAR_ULC         2   /* MIFARE Ultralight C */
#define M1NFC_T2TVAR_UL11        3   /* MIFARE Ultralight 11 (EV1 MF0UL11) */
#define M1NFC_T2TVAR_UL21        4   /* MIFARE Ultralight 21 (EV1 MF0UL21) */
#define M1NFC_T2TVAR_NTAG203     5   /* NTAG203 */
#define M1NFC_T2TVAR_NTAG213     6   /* NTAG213 */
#define M1NFC_T2TVAR_NTAG215     7   /* NTAG215 */
#define M1NFC_T2TVAR_NTAG216     8   /* NTAG216 */

#define M1NFC_TECH_A    0
#define M1NFC_TECH_B    1
#define M1NFC_TECH_F    2
#define M1NFC_TECH_V    3

#define NFC_T2T_NDEF_MAX_LEN   240   // Adjust later if needed


/* NFC dump workspace (size can be adjusted as needed) */
#define NFC_DUMP_BUF_SIZE    4096U                             /* Maximum dump data capacity */
#define NFC_VALID_BITS_SIZE  (NFC_DUMP_BUF_SIZE / 4U / 8U)     /* Based on minimum unit 4B (Type2 page) */
#define NFC_DUMP_MAX_UNITS      256U                             /* Maximum unit count (dump buffer / unit size) */

extern uint8_t g_nfc_dump_buf[NFC_DUMP_BUF_SIZE];
extern uint8_t g_nfc_valid_bits[NFC_VALID_BITS_SIZE];
extern uint16_t g_nfc_ntag_page_count;

/* ======================= 1) File/State ======================= */
/* State used in storage_browse() selection, extension check (.nfc), full path generation, etc. */
typedef struct {
    char     path[NFC_PATH_MAX]; /* Selected full path */
    uint8_t  uret;               /* Temporary return value (general purpose) */
    uint8_t  sys_error;          /* 0=OK, !0=error */
    uint8_t  ext_len;            /* Can be used for extension length check */
    uint8_t  ret_code;           /* For storing upper API return codes, etc. */
    bool     source_kind;
} nfc_file_state_t;

/* ======================= 2) Header (Card Identity) ======================= */
/* Core information needed for file header and UI summary: Tech/Family/UID/ATQA/SAK/ATS, etc. */
typedef struct {
    uint8_t       tech;                    /* M1NFC_TECH_A/B/F/V */
    uint8_t       family;                  /* M1NFC_FAM_CLASSIC/UL/… */

    uint8_t       uid[10];
    uint8_t       uid_len;

    /* Tech A additional information */
    struct {
        uint8_t   atqa[2];  bool has_atqa;
        uint8_t   sak;      bool has_sak;
        uint8_t   ats[32];  // default
        uint8_t   ats_len;  /* Can be 0 */
    } a;
} nfc_header_t;

/* ======================= 3) Dump Metadata ======================= */
/* Format definition and workspace pointer for body data (pages/blocks/units) */
typedef struct {
    uint16_t  unit_size;        /* Type2=4, Classic=16, others situation-dependent */
    uint32_t  unit_count;       /* Number of units allocated (maximum) */
    uint32_t  origin;           /* Starting index (usually 0) */
    uint8_t  *data;             /* Global workspace bound by nfc_alloc_dump() */
    uint8_t  *valid_bits;       /* NULL if all valid, otherwise byte-wise valid bitmap */
    uint32_t  max_seen_unit;    /* Maximum index actually observed during parsing */
    bool      has_dump;         /* Whether dump exists (for UI display) */
} nfc_dump_meta_t;

/* ======================= 4) Parser State ======================= */
/* Line-based I/O and current scan pointer — same concept as SubGhz block refill/line scan */
typedef struct {
    nfcfio_t  io;                      /* Line I/O context */
    char      line[NFC_LINE_MAX];      /* Single line buffer */
    char     *token;                   /* Token parsing pointer (temporary) */
    const char *p;                     /* Scan pointer (temporary) */
    uint8_t   parse_error;             /* 0=OK, otherwise=error code */
} nfc_parser_t;

/* ======================= 5) UI/Behavior State ======================= */
/* Runtime flags: immediate display text, emulation mode, preview count, etc. */
typedef struct {
    char    title_text[NFC_TITLE_MAX];   /* "Ultralight/NTAG", "Classic", etc. */
    char    uid_text[NFC_UID_TEXT_MAX];  /* UID HEX string "AA BB ..." */
    bool    emulate_uid_only;            /* true=UID-only emulation, false=RAW if possible */
    uint8_t preview_units;               /* Dump preview unit count (log/UI) */
} nfc_ui_state_t;

/* ======================= 6) NDEF Storage ======================= */
typedef struct {
    uint8_t  ndef[NFC_T2T_NDEF_MAX_LEN];
    uint16_t ndef_len;
    bool     valid;                  // Flag indicating if NDEF is actually filled
} nfc_t2t_info_t;

/* ======================= 6b) ISO15693 / NFC-V info ======================= */
/* Populated by the poller's m1_nfcv_read(); block bytes themselves live in the
 * shared dump workspace (unit_size = block_size), so Preview/Raw Data reuse the
 * validated Type-2 rendering. blocks_read (X) <= block_count (Y). */
typedef struct {
    uint8_t  variant;        /* M1NFC_VVAR_* */
    bool     has_dsfid;      uint8_t dsfid;
    bool     has_afi;        uint8_t afi;
    bool     has_sysinfo;    /* GET SYSTEM INFORMATION returned usable memory info */
    uint8_t  block_size;     /* bytes per block (1..32) */
    uint16_t block_count;    /* Y: total blocks reported by the tag (0 if unknown) */
    uint16_t blocks_read;    /* X: blocks successfully read before any stop */
    /* Optional block lock/security status (GET MULTIPLE BLOCK SECURITY STATUS,
     * 0x2C). Read-only, non-fatal: has_security stays false when the tag does
     * not support/answer the command, and the ordinary read above is unaffected.
     * block_locked bit i (LSB-first within each byte) = block i is locked. */
    bool     has_security;   /* 0x2C returned usable per-block status */
    uint16_t locked_count;   /* number of locked blocks among those covered */
    uint8_t  block_locked[(NFC_DUMP_MAX_UNITS + 7U) / 8U];
} nfc_iso15693_info_t;

/* ======================= 6c) ST25TB / SRI / SRIX info ======================= */
/* Populated by the poller's m1_st25tb_read(). Data blocks live in the shared
 * dump workspace (unit_size = 4) so Preview/Raw Data reuse the validated
 * rendering; the system/OTP block (0xFF) is captured separately here.
 * blocks_read (X) <= block_count (Y). */
typedef struct {
    uint8_t  variant;          /* M1NFC_TBVAR_* */
    uint8_t  chip_id;          /* session Chip ID (from Initiate) */
    uint8_t  block_size;       /* bytes per data block (always 4) */
    uint16_t block_count;      /* Y: total data blocks for the variant (0 if generic/unknown) */
    uint16_t blocks_read;      /* X: data blocks read before any stop */
    bool     has_system;       /* system/OTP block (0xFF) was read */
    uint8_t  system_otp[4];    /* system/OTP block bytes (wire order) */
} nfc_st25tb_info_t;

/* ======================= 6d) MIFARE Classic (full-card 1K) ======================= */
/* Full-card model for MIFARE Classic 1K/4K -- nfc_mfc_sector_t/nfc_mfc_info_t
 * and the MFC geometry constants/helpers live in mfc_dict_types.h (included
 * above), not here. Per-sector key store lives there; the block DATA lives in
 * the shared dump buffer (g_nfc_dump_buf, unit_size=16, bound via
 * nfc_ctx_set_dump) indexed by ABSOLUTE block number, with the dump
 * valid-bits bitmap marking which blocks were actually read. This keeps the ctx
 * model tiny and reuses the already-allocated 4096-byte (256-block) buffer for
 * both 1K (64 blocks) and 4K (256 blocks). Unread blocks stay unread. */

/* MFC block-data accessors -- back the per-block store with the shared dump
 * buffer (bound via nfc_ctx_set_dump, unit_size=16). Absolute block index. */
bool           nfc_ctx_mfc_block_valid(uint16_t blk);   /* was this block read?  */
const uint8_t *nfc_ctx_mfc_block(uint16_t blk);         /* NULL if not read      */
void           nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16]);

/* ======================= 6e) MIFARE DESFire (Type 4A) info =============== */
/* Populated by m1_desfire_read() from a read-only native GetVersion (0x60).
 * present=false when the card is not DESFire or GetVersion did not complete. */
typedef struct {
    bool     present;        /* GetVersion returned a valid NXP version tuple    */
    uint8_t  v[28];          /* raw 28-byte GetVersion tuple:                     */
                             /*  [0..6]  hw vendor/type/sub/major/minor/storage/proto */
                             /*  [7..13] sw vendor/type/sub/major/minor/storage/proto */
                             /*  [14..20] UID, [21..25] batch, [26] week, [27] year   */
} nfc_desfire_info_t;

/* Deeper, still read-only DESFire inspection (GetFreeMemory, key settings,
 * application/file enumeration, and plain-access file content) -- populated
 * by m1_desfire_read() immediately after a successful GetVersion, while the
 * card is still selected. See mf_desfire_parse.h for the full data model
 * and every field's exact meaning/bounds. Cleared together with
 * nfc_desfire_info_t by nfc_ctx_clear_desfire(). */
typedef mf_desfire_deep_t nfc_desfire_deep_info_t;

/* Bounded card-interpretation result (currently: Clipper transit card
 * only). Populated by m1_desfire_read() immediately after the generic
 * desfire_deep capture, from bytes that capture already retrieved (plus,
 * for Clipper's ride history specifically, one additional targeted read of
 * the same already-gated file -- see m1_desfire.c). Cleared together with
 * desfire/desfire_deep by nfc_ctx_clear_desfire(). card_id stays
 * NfcTransitCardUnknown when no supported card is recognized -- callers
 * fall back to the generic DESFire screens entirely in that case. */
typedef nfc_transit_result_t nfc_transit_info_t;

/* ======================= Top-level Runtime Context ======================= */
/* Gathers the above blocks in one place for clean passing between functions  */
typedef struct {
    nfc_file_state_t  file;
    nfc_header_t      head;
    nfc_dump_meta_t   dump;
    nfc_parser_t      parser;
    nfc_ui_state_t    ui;
    nfc_t2t_info_t    t2t;   // Type 2 Tag (NTAG, etc.) related information
    nfc_iso15693_info_t v;   // ISO15693 / NFC-V related information
    nfc_st25tb_info_t tb;    // ST25TB / SRI / SRIX related information
    nfc_mfc_info_t    mfc;   // MIFARE Classic (Phase A: single sector)
    nfc_desfire_info_t desfire; // MIFARE DESFire (Type 4A: GetVersion identity)
    nfc_desfire_deep_info_t desfire_deep; // MIFARE DESFire deeper read (apps/files)
    nfc_transit_info_t transit; // Bounded card interpretation (e.g. Clipper)
} nfc_run_ctx_t;


/**
 * @brief nfc_ctx_module_init - Initialize module: prepare global context/mutex
 * 
 * @retval None
 */
void nfc_ctx_module_init(void);

/**
 * @brief nfc_ctx_get - Return pointer for modules that need write access
 * 
 * @retval Pointer to global NFC context
 */
nfc_run_ctx_t* nfc_ctx_get(void);

/**
 * @brief nfc_run_ctx_init - Reset context to default values
 * 
 * @param[in,out] c Pointer to NFC context structure
 * @retval None
 */
void nfc_run_ctx_init(nfc_run_ctx_t* c);

/**
 * @brief nfc_ctx_begin_live - Source switch (Live): set basic fields
 * 
 * @retval None
 */
void nfc_ctx_begin_live(void);

/**
 * @brief nfc_ctx_begin_file - Source switch (File): set basic fields
 * 
 * @param[in] fullpath Full path to NFC file
 * @retval None
 */
void nfc_ctx_begin_file(const char* fullpath);

/**
 * @brief nfc_ctx_refresh_ui - Update UI string with family/UID
 * 
 * @retval None
 */
void nfc_ctx_refresh_ui(void);

/**
 * @brief nfc_ctx_clear_dump - Clear dump metadata/pointer binding (set)/release (clear)
 * 
 * @retval None
 */
void nfc_ctx_clear_dump(void);

/**
 * @brief nfc_ctx_set_dump - Set dump metadata/pointer binding
 * 
 * @param[in] unit_size Size of each unit
 * @param[in] unit_count Total number of units
 * @param[in] origin Starting index
 * @param[in] data Pointer to dump data buffer
 * @param[in] valid_bits Pointer to valid bits bitmap
 * @param[in] max_seen_unit Maximum unit index seen
 * @param[in] has_dump Whether dump exists
 * @retval None
 */
void nfc_ctx_set_dump(uint16_t unit_size, uint32_t unit_count, uint32_t origin,
                      uint8_t* data, uint8_t* valid_bits, uint32_t max_seen_unit, bool has_dump);

/**
 * @brief FillNfcContextFromDevice - Fill nfc_ctx from device (header/UI)
 * 
 * @param[in] dev Pointer to RFAL NFC device
 * @retval 0 Success
 * @retval 1 Invalid device
 * @retval 2 Unsupported device type
 */
uint8_t FillNfcContextFromDevice(const rfalNfcDevice* dev);

/**
 * @brief nfc_classify_family_from_nfca - Weak default rule: classify Family by SAK/ATQA (redefine in project if desired)
 * 
 * @param[in] sak SAK value
 * @param[in] atqa ATQA array (2 bytes)
 * @retval Family code (M1NFC_FAM_*), 0 if unknown
 */
uint8_t nfc_classify_family_from_nfca(uint8_t sak, const uint8_t atqa[2]);

/**
 * @brief nfc_ctx_sync_emu - Reflect current nfc_ctx.head content to emulator EmuNfcA(g_emuA)
 * 
 * @retval None
 */
void nfc_ctx_sync_emu(void);

/* Type 2 Tag NDEF helpers */
/**
 * @brief nfc_ctx_clear_t2t_ndef - Clear T2T NDEF data
 * 
 * @retval None
 */
void nfc_ctx_clear_t2t_ndef(void);

/**
 * @brief nfc_ctx_set_t2t_ndef - Set T2T NDEF data
 * 
 * @param[in] buf Pointer to NDEF data buffer
 * @param[in] len Length of NDEF data
 * @retval None
 */
void nfc_ctx_set_t2t_ndef(const uint8_t *buf, uint16_t len);

/**
 * @brief nfc_ctx_get_t2t_info - Get T2T info structure pointer
 * 
 * @retval Pointer to T2T info structure
 */
const nfc_t2t_info_t * nfc_ctx_get_t2t_info(void);

/*-------------util function-----------*/
/**
 * @brief nfc_ctx_dump_t2t_ndef - Dump T2T NDEF data in hex/ASCII format
 * 
 * @retval None
 */
void nfc_ctx_dump_t2t_ndef(void);

/**
 * @brief nfc_ctx_dump_t2t_pages - Dump T2T pages in hex/ASCII format
 * 
 * @retval None
 */
void nfc_ctx_dump_t2t_pages(void);

/**
 * @brief nfc_ctx_get_t2t_page_count - Return Type 2 / NTAG page count
 * 
 * @retval Page count, or 0 if not T2T or dump invalid
 */
uint16_t nfc_ctx_get_t2t_page_count(void);

/**
 * @brief nfc_ctx_get_t2t_page - Get T2T page data
 * 
 * @param[in] pageIndex Page index to retrieve
 * @param[out] out Output buffer (4 bytes)
 * @retval true Success
 * @retval false Failed
 */
bool nfc_ctx_get_t2t_page(uint16_t pageIndex, uint8_t out[4]);

/**
 * @brief nfc_ctx_t2t_page_valid - Was this T2T page actually read/loaded?
 *
 * nfc_ctx_get_t2t_page() bounds-checks against the highest page index EVER
 * seen but does not consult the valid-bits bitmap, so it cannot by itself
 * distinguish "genuinely captured" from "gap inside an otherwise-larger
 * dump" (e.g. one page skipped after a mid-read error). Mirrors
 * nfc_ctx_mfc_block_valid()'s semantics exactly.
 *
 * @param[in] pageIndex Page index to check
 * @retval true Page is within range and its valid-bit is set (or no bitmap
 *              is bound, in which case everything in range counts valid)
 */
bool nfc_ctx_t2t_page_valid(uint16_t pageIndex);

/**
 * @brief nfc_ctx_format_t2t_page_line - Format T2T page as single line string
 * 
 * @param[in] pageIndex Page index to format
 * @param[out] buf Output buffer for formatted string
 * @param[in] bufSize Size of output buffer
 * @retval true Success
 * @retval false Failed
 */
bool nfc_ctx_format_t2t_page_line(uint16_t pageIndex, char *buf, size_t bufSize);

/**
 * @brief nfc_ctx_set_t2t_version - Set T2T version information
 * 
 * @param[in] ver Pointer to version data
 * @param[in] len Length of version data
 * @retval None
 */
void nfc_ctx_set_t2t_version(const uint8_t *ver, uint8_t len);

/**
 * @brief nfc_ctx_get_t2t_version - Get T2T version information
 *
 * @param[out] out Output buffer (8 bytes)
 * @retval Length of version data (0 if not available)
 */
uint8_t nfc_ctx_get_t2t_version(uint8_t out[8]);

/* Reset the raw GET_VERSION tuple to "not captured" -- call before a fresh
 * live read or file load so a missing/absent tuple can never inherit a
 * stale one from whatever was previously loaded. */
void nfc_ctx_clear_t2t_version(void);

/**
 * @brief nfc_ctx_set_t2t_variant - Record the resolved T2T sub-variant
 * @param[in] variant One of M1NFC_T2TVAR_*
 */
void nfc_ctx_set_t2t_variant(uint8_t variant);

/**
 * @brief nfc_ctx_get_t2t_variant - Get the resolved T2T sub-variant
 * @retval One of M1NFC_T2TVAR_* (M1NFC_T2TVAR_UNKNOWN if not established)
 */
uint8_t nfc_ctx_get_t2t_variant(void);

/* The DECLARED/EXPECTED total page count for the identified variant --
 * distinct from nfc_ctx_get_t2t_page_count() (which reflects how much data
 * is actually present/captured, "highest seen + 1"). Set once, alongside
 * the variant itself: at live-read time to the full geometry the
 * identified variant SHOULD have (e.g. 231 for a GET_VERSION-identified
 * NTAG216), regardless of how many pages the read loop actually managed to
 * capture; at reload time to the file's validated "Pages:" header. This is
 * what m1_t2t_emu_image_build() compares against a variant's real geometry
 * table, and what lets a genuinely-truncated read stay honestly reported
 * as "NTAG216, 42/231 pages" instead of silently becoming a complete-
 * looking "NTAG203, 42/42". 0 means not established (no source loaded/read
 * yet, or the variant itself is unknown). */
void     nfc_ctx_set_t2t_expected_pages(uint16_t pages);
uint16_t nfc_ctx_get_t2t_expected_pages(void);

/* Set by nfc_storage.c's geometry cross-check (variant name vs raw
 * GET_VERSION tuple vs declared Pages: vs highest actually-present page
 * index) when those disagree or land out of range -- a genuinely corrupt/
 * self-inconsistent saved file, distinct from "unidentifiable" (plain
 * UNKNOWN) or "incomplete" (fewer valid pages than expected). Never set
 * during a live read (only a saved file can be internally contradictory
 * this way). m1_t2t_emu_image_build() checks this before anything else
 * and refuses with a dedicated "Invalid saved tag data" status. */
void nfc_ctx_set_t2t_geometry_corrupt(bool corrupt);
bool nfc_ctx_t2t_geometry_corrupt(void);

#define NFC_T2T_COUNTER_MAX 3U   /* NXP counter/tearing-flag index space is 0-2 */

/* Genuine READ_SIGNATURE (0x3C) originality data, captured from a physical
 * tag or loaded from a saved file's "Signature:" line -- never fabricated.
 * Absent (valid=false) unless one of those two actually supplied 32 bytes. */
void nfc_ctx_clear_t2t_signature(void);
void nfc_ctx_set_t2t_signature(const uint8_t sig[32]);
bool nfc_ctx_get_t2t_signature(uint8_t out[32]);
bool nfc_ctx_t2t_signature_valid(void);

/* Genuine READ_CNT (0x39) per-index counter values (index 0-2) and CHECK_
 * TEARING (0x3E) per-index tearing flags (index 0-2). Each index independently
 * valid/invalid -- an index never captured (tag didn't answer, or a saved
 * file has no line for it) stays invalid, never zero-filled. */
void nfc_ctx_clear_t2t_counters(void);
void nfc_ctx_set_t2t_counter(uint8_t idx, const uint8_t val[3]);
bool nfc_ctx_get_t2t_counter(uint8_t idx, uint8_t out[3]);
bool nfc_ctx_t2t_counter_valid(uint8_t idx);

void nfc_ctx_clear_t2t_tearing(void);
void nfc_ctx_set_t2t_tearing(uint8_t idx, uint8_t val);
bool nfc_ctx_get_t2t_tearing(uint8_t idx, uint8_t *out);
bool nfc_ctx_t2t_tearing_valid(uint8_t idx);

/* Protection state (AUTH0/PROT/AUTHLIM), parsed from the config pages
 * during a live read -- see NXP NTAG21x/UL11 CFG0/CFG1 layout: CFG0 byte3=AUTH0;
 * CFG1 byte0
 * bits[2:0]=AUTHLIM, bit6=CFGLCK, bit7=PROT). Each independently valid --
 * a config page that couldn't be read (e.g. protected-read tag, PROT=1)
 * leaves the corresponding field invalid, never a fabricated/assumed
 * value. AUTHLIM in particular MUST be treated as unsafe-for-dictionary
 * whenever invalid, not defaulted to 0 (unlimited). */
void nfc_ctx_clear_t2t_protection(void);
void nfc_ctx_set_t2t_auth0(uint8_t auth0);
bool nfc_ctx_get_t2t_auth0(uint8_t *out);
void nfc_ctx_set_t2t_prot(bool prot);
bool nfc_ctx_get_t2t_prot(bool *out);
void nfc_ctx_set_t2t_authlim(uint8_t authlim);
bool nfc_ctx_get_t2t_authlim(uint8_t *out);

/* Transient, pre-authentication PROTECTION-SUSPECTED signal -- deliberately
 * SEPARATE storage from the genuine AUTH0/PROT/AUTHLIM fields above. Set
 * only when a read stops short of a variant's known page count on a
 * deterministic protocol-level NACK (RFAL_ERR_PROTO), which happens
 * whenever AUTH0 <= the variant's own config-page address (the config
 * pages are then genuinely unreadable pre-auth, so the real AUTH0/PROT/
 * AUTHLIM values cannot be parsed at all). This is a SUSPICION, not a
 * measurement: the exact page where the read stopped is not guaranteed to
 * equal the tag's real AUTH0 (T2T READ works in 4-page bursts, so the
 * boundary is not always page-exact), so it must never be written into
 * nfc_ctx_set_t2t_auth0()/_prot()/_authlim(), never saved to the V4 file,
 * and never read by m1_t2t_emu_image_build(). Its ONLY sanctioned use is
 * nfc_can_unlock() offering manual (never dictionary) Unlock before the
 * genuine fields exist. Cleared on every fresh read; also cleared once
 * nfc_unlock_reread_all_pages() replaces it with genuine, wire-confirmed
 * values after a successful authenticated re-read. */
void nfc_ctx_clear_t2t_protection_suspected(void);
void nfc_ctx_set_t2t_protection_suspected(uint16_t first_blocked_page);
bool nfc_ctx_get_t2t_protection_suspected(uint16_t *first_blocked_page_out);

/* Genuine PWD_AUTH (0x1B) credential -- ONLY ever set after a real tag
 * accepts the password and returns a PACK, never from user entry alone
 * and never fabricated. Mirrors the signature/counter/tearing pattern:
 * independently valid, cleared before conditionally set on every fresh
 * read/load so a stale credential from a prior card can never leak in. */
void nfc_ctx_clear_t2t_credential(void);
void nfc_ctx_set_t2t_credential(const uint8_t pwd[4], const uint8_t pack[2]);
bool nfc_ctx_get_t2t_pwd(uint8_t out[4]);
bool nfc_ctx_get_t2t_pack(uint8_t out[2]);
bool nfc_ctx_t2t_credential_valid(void);

/**
 * @brief nfc_ctx_clear_iso15693 - Reset the ISO15693/NFC-V info block
 */
void nfc_ctx_clear_iso15693(void);

/**
 * @brief nfc_ctx_set_iso15693_variant - Record the resolved NFC-V sub-variant
 * @param[in] variant One of M1NFC_VVAR_*
 */
void nfc_ctx_set_iso15693_variant(uint8_t variant);

/**
 * @brief nfc_ctx_set_iso15693_sysinfo - Store GET SYSTEM INFORMATION results
 * @param[in] has_dsfid   DSFID present
 * @param[in] dsfid       DSFID byte
 * @param[in] has_afi     AFI present
 * @param[in] afi         AFI byte
 * @param[in] block_count Total blocks (Y)
 * @param[in] block_size  Bytes per block
 * @param[in] has_sysinfo Whether usable memory info was obtained
 */
void nfc_ctx_set_iso15693_sysinfo(bool has_dsfid, uint8_t dsfid,
                                  bool has_afi, uint8_t afi,
                                  uint16_t block_count, uint8_t block_size,
                                  bool has_sysinfo);

/**
 * @brief nfc_ctx_set_iso15693_blocks_read - Record blocks actually read (X)
 * @param[in] blocks_read Successfully read block count
 */
void nfc_ctx_set_iso15693_blocks_read(uint16_t blocks_read);

/**
 * @brief nfc_ctx_set_iso15693_security - Store block lock/security status (0x2C)
 * @param[in] has_security  true if the 0x2C query returned usable per-block status
 * @param[in] locked_bits   packed LSB-first bitmap (block i in bit i), or NULL
 * @param[in] n_blocks      number of blocks covered by locked_bits
 * @param[in] locked_count  number of blocks reported locked
 */
void nfc_ctx_set_iso15693_security(bool has_security, const uint8_t *locked_bits,
                                   uint16_t n_blocks, uint16_t locked_count);

/**
 * @brief nfc_ctx_iso15693_block_locked - Per-block lock query (read-only)
 * @param[in] block_index Block number
 * @retval true only if security status is available AND that block is locked
 */
bool nfc_ctx_iso15693_block_locked(uint16_t block_index);

/**
 * @brief nfc_ctx_get_iso15693_info - Get the ISO15693/NFC-V info block
 * @retval Pointer to the ISO15693 info (never NULL)
 */
const nfc_iso15693_info_t * nfc_ctx_get_iso15693_info(void);

/**
 * @brief nfc_ctx_clear_desfire - Reset the DESFire (Type 4A) info block
 */
void nfc_ctx_clear_desfire(void);

/**
 * @brief nfc_ctx_set_desfire_version - Store the raw 28-byte GetVersion tuple
 * @param[in] v28  pointer to the 28 accumulated version bytes (NULL clears)
 */
void nfc_ctx_set_desfire_version(const uint8_t *v28);

/**
 * @brief nfc_ctx_get_desfire_info - Get the DESFire info block (never NULL)
 */
const nfc_desfire_info_t * nfc_ctx_get_desfire_info(void);

/**
 * @brief nfc_ctx_set_desfire_deep - Store a completed deep-read result
 * @param[in] deep  the populated deep-read result (copied by value; NULL clears)
 */
void nfc_ctx_set_desfire_deep(const nfc_desfire_deep_info_t *deep);

/**
 * @brief nfc_ctx_get_desfire_deep - Get the DESFire deep-read block (never NULL)
 */
const nfc_desfire_deep_info_t * nfc_ctx_get_desfire_deep(void);

/**
 * @brief nfc_ctx_set_transit - Store a completed card-interpretation result
 * @param[in] transit  the result (copied by value; NULL clears)
 */
void nfc_ctx_set_transit(const nfc_transit_info_t *transit);

/**
 * @brief nfc_ctx_get_transit - Get the card-interpretation result (never NULL)
 */
const nfc_transit_info_t * nfc_ctx_get_transit(void);

/**
 * @brief nfc_ctx_clear_st25tb - Reset the ST25TB/SRI/SRIX info block
 */
void nfc_ctx_clear_st25tb(void);

/**
 * @brief nfc_ctx_set_st25tb_ident - Record variant, chip ID and geometry
 * @param[in] variant     One of M1NFC_TBVAR_*
 * @param[in] chip_id     Session chip ID
 * @param[in] block_count Total data blocks (0 if generic/unknown)
 * @param[in] block_size  Bytes per data block (4)
 */
void nfc_ctx_set_st25tb_ident(uint8_t variant, uint8_t chip_id,
                              uint16_t block_count, uint8_t block_size);

/**
 * @brief nfc_ctx_set_st25tb_blocks_read - Record data blocks actually read (X)
 */
void nfc_ctx_set_st25tb_blocks_read(uint16_t blocks_read);

/**
 * @brief nfc_ctx_set_st25tb_system - Record the system/OTP block (0xFF)
 * @param[in] sys4 4 system-block bytes (wire order)
 */
void nfc_ctx_set_st25tb_system(const uint8_t sys4[4]);

/**
 * @brief nfc_ctx_get_st25tb_info - Get the ST25TB/SRI/SRIX info block
 * @retval Pointer to the ST25TB info (never NULL)
 */
const nfc_st25tb_info_t * nfc_ctx_get_st25tb_info(void);

/**
 * @brief nfc_ctx_clear_mfc - Reset the MIFARE Classic Phase-A info block
 */
void nfc_ctx_clear_mfc(void);

/**
 * @brief nfc_ctx_get_mfc_info - Get the MIFARE Classic Phase-A info block
 * @retval Pointer to the Classic info (never NULL)
 */
nfc_mfc_info_t * nfc_ctx_get_mfc_info(void);

/* ---- NTAG21x live-write status (small; no duplicated tag data) ----------- */
typedef enum {
    NFC_WR_IDLE = 0,
    NFC_WR_WAIT_TARGET,
    NFC_WR_WRITING,
    NFC_WR_DONE,
    NFC_WR_FAIL
} nfc_wr_state_t;

typedef enum {
    NFC_WR_ERR_NONE = 0,
    NFC_WR_ERR_WRONG_MODEL,     /* target is an NTAG21x, but a different model */
    NFC_WR_ERR_UNSUPPORTED,     /* not NTAG213/215/216 (generic T2 / UL / other) */
    NFC_WR_ERR_INCOMPLETE_SRC,  /* source dump missing a required user page */
    NFC_WR_ERR_TAG_REMOVED,     /* RF link lost mid-operation */
    NFC_WR_ERR_WRITE,           /* write NAK/failure at fail_page */
    NFC_WR_ERR_VERIFY,          /* read-back mismatch at fail_page */
    NFC_WR_ERR_PROTECTED,       /* page write rejected/protected */
    NFC_WR_ERR_ABORTED          /* user cancelled */
} nfc_wr_err_t;

typedef struct {
    uint8_t  state;        /* nfc_wr_state_t */
    uint8_t  err;          /* nfc_wr_err_t  */
    uint8_t  src_variant;  /* M1NFC_T2TVAR_* source model (== required target)   */
    uint16_t first_page;   /* first writable user page (4)                       */
    uint16_t last_page;    /* last  writable user page (model-specific)          */
    uint16_t cur_page;     /* page currently being written/verified              */
    uint16_t done_count;   /* user pages written AND verified                    */
    uint16_t total_count;  /* total user pages to write                          */
    uint16_t fail_page;    /* page where a failure occurred                      */
    bool     any_written;  /* >=1 page written (partial-write indicator)         */
} nfc_wr_status_t;

/* Shared NTAG21x live-write status block (single instance). */
nfc_wr_status_t * nfc_ctx_get_wr_status(void);

/* ---- MIFARE Classic dictionary-scan status (Stage C; transient UI state).
 * Found keys themselves live in nfc_mfc_info_t/nfc_mfc_sector_t (key_a/key_b);
 * this is only the scan-progress bookkeeping. nfc_mfc_scan_state_t/
 * nfc_mfc_scan_src_t/nfc_mfc_scan_t themselves live in mfc_dict_types.h
 * (included above), alongside nfc_mfc_info_t/nfc_mfc_sector_t. ---- */

nfc_mfc_scan_t * nfc_ctx_get_mfc_scan(void);   /* single instance, never NULL */
void             nfc_ctx_clear_mfc_scan(void);

/* Find Missing Keys' persisted dictionary-acquisition resume state (see
 * mfc_dict_resume.h) -- single instance, reset to fresh by
 * nfc_ctx_clear_mfc() (every point that already runs, at the start of every
 * normal Read/Dictionary Scan), so it can never leak across two different
 * physical cards or two unrelated sessions. */
mfc_dict_resume_t * nfc_ctx_get_mfc_dict_resume(void);   /* single instance, never NULL */

/* ---- Harvester (Increment 3b): transient UI status for the live nested-nonce
 * capture. The captured samples go straight to the .m1h on SD; this is only the
 * progress/result bookkeeping the harvest view polls. ---- */
typedef enum {
    NFC_HARVEST_IDLE = 0,
    NFC_HARVEST_WAIT_CARD,   /* activating a MIFARE Classic card            */
    NFC_HARVEST_RUNNING,     /* capturing nested nonces                     */
    NFC_HARVEST_DONE,        /* wrote the .m1h file                         */
    NFC_HARVEST_STOPPED,     /* user-aborted                                */
    NFC_HARVEST_FAIL         /* not a Classic card / 0 samples / SD error   */
} nfc_harvest_state_t;

typedef struct {
    uint8_t state;        /* nfc_harvest_state_t                            */
    uint8_t tgt_sector;   /* target sector 0..15                           */
    uint8_t want;         /* samples requested                             */
    uint8_t got;          /* samples captured                              */
    char    path[64];     /* .m1h path on NFC_HARVEST_DONE                  */
} nfc_harvest_ui_t;

nfc_harvest_ui_t * nfc_ctx_get_harvest(void);  /* single instance, never NULL */

/* ---- MFC Recovery solve (Increment: nested-dictionary key recovery) UI status. ---- */
typedef enum {
    NFC_SOLVE_UI_IDLE = 0,
    NFC_SOLVE_UI_SOLVING,     /* testing dictionary keys against nonces          */
    NFC_SOLVE_UI_VERIFYING,   /* candidate found; authenticating against card    */
    NFC_SOLVE_UI_FOUND,       /* key verified on the card                        */
    NFC_SOLVE_UI_NO_KEY,      /* no dictionary key matched                       */
    NFC_SOLVE_UI_AMBIGUOUS,   /* >1 candidate (collect more nonces)              */
    NFC_SOLVE_UI_UNSUPPORTED, /* capture unsupported/malformed                   */
    NFC_SOLVE_UI_INSUFFICIENT,/* too few samples                                 */
    NFC_SOLVE_UI_CARD_LOST,   /* card removed before verification                */
    NFC_SOLVE_UI_STOPPED,     /* user cancelled                                  */
    NFC_SOLVE_UI_ERROR        /* internal / read failure                         */
} nfc_solve_ui_state_t;

typedef struct {
    uint8_t state;        /* nfc_solve_ui_state_t          */
    uint8_t tgt_sector;   /* recovered sector 0..15        */
    uint8_t tgt_keytype;  /* 0x60 Key A / 0x61 Key B       */
    uint8_t key[6];       /* recovered+verified key        */
} nfc_solve_ui_t;

nfc_solve_ui_t * nfc_ctx_get_solve(void);  /* single instance, never NULL */

/* ---- MIFARE Classic 1K WRITE (clone/restore) UI status. Writes a loaded MFC
 * image onto a physical card: authenticate with the image's known keys, enforce
 * the target's current access conditions, write permitted data blocks and
 * guarded trailers, read-back verify each. This is only the progress/result
 * bookkeeping the write view polls. ---- */
typedef enum {
    NFC_MFCWR_IDLE = 0,
    NFC_MFCWR_WAIT_CARD,   /* activating a MIFARE Classic 1K              */
    NFC_MFCWR_WRITING,     /* auth + write in progress                    */
    NFC_MFCWR_DONE,        /* finished (see counts for detail)            */
    NFC_MFCWR_STOPPED,     /* user-aborted                                */
    NFC_MFCWR_NO_SOURCE,   /* no valid loaded MFC image to write          */
    NFC_MFCWR_FAIL,        /* not a Classic 1K at start                   */
    NFC_MFCWR_CARD_LOST    /* tag removed mid-write                       */
} nfc_mfcwr_state_t;

typedef struct {
    uint8_t  state;          /* nfc_mfcwr_state_t                              */
    uint8_t  cur_sector;     /* sector currently targeted (0..39)             */
    uint16_t written;        /* blocks written AND read-back verified (<=256)  */
    uint16_t skipped;        /* blocks skipped (no key / access-forbidden / blk0) */
    uint16_t failed;         /* blocks that failed write or read-back verify   */
    uint8_t  sectors_noauth; /* sectors no source key could authenticate       */
} nfc_mfc_write_t;

nfc_mfc_write_t * nfc_ctx_get_mfc_write(void);  /* single instance, never NULL */
void              nfc_ctx_clear_mfc_write(void);

/* ---- NTAG/Ultralight Unlock (genuine PWD_AUTH) progress/result. UI polls
 * this; nfc_unlock_run() (nfc_poller.c) is the sole writer. Mirrors
 * nfc_mfc_write_t's role exactly. ---- */
typedef enum {
    NFC_UNLOCK_IDLE = 0,
    NFC_UNLOCK_WAIT_CARD,        /* re-activating the physical tag              */
    NFC_UNLOCK_AUTHENTICATING,   /* single user-entered password attempt        */
    NFC_UNLOCK_SEARCHING,        /* dictionary scan in progress                 */
    NFC_UNLOCK_UNLOCKED,         /* accepted; genuine PWD/PACK now in nfc_ctx    */
    NFC_UNLOCK_WRONG_PASSWORD,   /* tag rejected every password tried            */
    NFC_UNLOCK_PACK_MISMATCH,    /* accepted, but PACK != a previously-verified one */
    NFC_UNLOCK_AUTHLIM_ACTIVE,   /* AUTHLIM nonzero/unreadable -- dictionary refused */
    NFC_UNLOCK_COMM_ERROR,       /* timeout / malformed / link loss / card lost  */
    NFC_UNLOCK_UNSUPPORTED,      /* not a protected UL11/NTAG213/215/216         */
    NFC_UNLOCK_STOPPED,          /* user-aborted (BACK)                          */
    NFC_UNLOCK_REREAD_INCOMPLETE,/* password genuinely accepted, but the same-  */
                                 /* session authenticated re-read afterwards    */
                                 /* did not capture every expected page -- the  */
                                 /* credential is real, but never reported as   */
                                 /* UNLOCKED, since emulation would still       */
                                 /* refuse until a clean re-read succeeds       */
} nfc_unlock_state_t;

typedef struct {
    uint8_t  state;        /* nfc_unlock_state_t */
    uint16_t dict_tried;   /* dictionary entries attempted so far (progress display) */
} nfc_t2t_unlock_t;

nfc_t2t_unlock_t * nfc_ctx_get_t2t_unlock(void);  /* single instance, never NULL */
void                nfc_ctx_clear_t2t_unlock(void);

/**
 * @brief nfc_ctx_set_t2t_page - Set T2T page data
 * 
 * @param[in] page Page number to write
 * @param[in] data Page data (4 bytes)
 * @retval None
 */
void nfc_ctx_set_t2t_page(uint16_t page, const uint8_t data[4]);


#endif /* NFC_DRV_NFC_CTX_H_ */
