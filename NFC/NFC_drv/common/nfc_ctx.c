/* See COPYING.txt for license details. */

/*
 * nfc_ctx.c
 *
 *
 */

#include <string.h>
#include <stdio.h>  
#include "nfc_ctx.h"
#include "rfal_nfc.h"
#include "legacy/nfc_driver.h"   // Use Emu_SetNfcA, Emu_Clear

/* --------- (Optional) Mutex usage setting ---------
 * If needed, define NFC_CTX_USE_MUTEX in project settings and
 * include FreeRTOS header, then create with xSemaphoreCreateMutex().
 */
//#define NFC_CTX_USE_MUTEX 1

#if defined(NFC_CTX_USE_MUTEX)
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t s_ctx_mtx = NULL;  
static inline void nfc_ctx_lock(void)   { xSemaphoreTake(s_ctx_mtx, portMAX_DELAY); }
static inline void nfc_ctx_unlock(void) { xSemaphoreGive(s_ctx_mtx); }
#else
static inline void nfc_ctx_lock(void)   {}
static inline void nfc_ctx_unlock(void) {}
#endif


/* --------- Internal global variables --------- */
static uint8_t s_t2t_version[8];
static uint8_t s_t2t_version_len = 0;
static uint8_t s_t2t_variant = M1NFC_T2TVAR_UNKNOWN;
static uint16_t s_t2t_expected_pages = 0;
static bool     s_t2t_geometry_corrupt = false;
static uint8_t  s_t2t_auth0;
static bool     s_t2t_auth0_valid = false;
static bool     s_t2t_prot;
static bool     s_t2t_prot_valid = false;
static uint8_t  s_t2t_authlim;
static bool     s_t2t_authlim_valid = false;
static bool     s_t2t_protection_suspected = false;   /* transient -- see nfc_ctx.h */
static uint16_t s_t2t_first_blocked_page = 0;
static uint8_t  s_t2t_pwd[4];
static uint8_t  s_t2t_pack[2];
static bool     s_t2t_credential_valid = false;
static uint8_t s_t2t_signature[32];
static bool    s_t2t_signature_valid = false;
static uint8_t s_t2t_counter[NFC_T2T_COUNTER_MAX][3];
static bool    s_t2t_counter_valid[NFC_T2T_COUNTER_MAX];
static uint8_t s_t2t_tearing[NFC_T2T_COUNTER_MAX];
static bool    s_t2t_tearing_valid[NFC_T2T_COUNTER_MAX];
/* --------- Module global context (single instance) --------- */
static nfc_run_ctx_t g_nfc_ctx;

uint16_t g_nfc_ntag_page_count = 0; // NTAG total page count

uint8_t g_nfc_dump_buf[NFC_DUMP_BUF_SIZE];
uint8_t g_nfc_valid_bits[NFC_VALID_BITS_SIZE];

/* --------- Internal utilities --------- */
/*============================================================================*/
/**
 * @brief family_to_title - Convert family code to title string
 * 
 * @param[in] family Family code (M1NFC_FAM_*)
 * @retval Title string for the family
 */
/*============================================================================*/
static const char* family_to_title(uint8_t family)
{
#ifdef M1NFC_FAM_CLASSIC
    if (family == M1NFC_FAM_CLASSIC)    return "MIFARE Classic";
#endif
#ifdef M1NFC_FAM_ULTRALIGHT
    if (family == M1NFC_FAM_ULTRALIGHT) return "Ultralight/NTAG";
#endif
#ifdef M1NFC_FAM_T4T
    if (family == M1NFC_FAM_T4T)        return "Type 4A";
#endif
#ifdef M1NFC_FAM_DESFIRE
    if (family == M1NFC_FAM_DESFIRE)    return "DESFire";
#endif
#ifdef M1NFC_FAM_FELICA
    if (family == M1NFC_FAM_FELICA)     return "Felica";
#endif
#ifdef M1NFC_FAM_15693
    if (family == M1NFC_FAM_15693)      return "ISO15693";
#endif
    return "NFC";
}

/*============================================================================*/
/**
 * @brief make_uid_text - Convert UID bytes to hex text string
 * 
 * Converts UID byte array to space-separated hex string (e.g., "AA BB CC DD").
 * 
 * @param[out] out Output buffer for hex string
 * @param[in] outsz Size of output buffer
 * @param[in] uid UID byte array
 * @param[in] uid_len UID length in bytes
 * @retval None
 */
/*============================================================================*/
static void make_uid_text(char* out, size_t outsz, const uint8_t* uid, uint8_t uid_len)
{
    static const char H[] = "0123456789ABCDEF";
    size_t p = 0;
    for (uint8_t i = 0; i < uid_len && (p + 3) <= outsz; i++) {
        uint8_t b = uid[i];
        out[p++] = H[(b >> 4) & 0xF];
        out[p++] = H[b & 0xF];
        out[p++] = (i + 1 < uid_len) ? ' ' : '\0';
    }
    if (p < outsz) out[p] = '\0';
}

/* --------- API functions --------- */

/*============================================================================*/
/**
 * @brief nfc_ctx_module_init - Initialize module: prepare global context/mutex
 * 
 * Initializes the NFC context module by creating mutex (if enabled)
 * and initializing the global context structure.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_module_init(void)
{

#if defined(NFC_CTX_USE_MUTEX)
    if (!s_ctx_mtx) s_ctx_mtx = xSemaphoreCreateMutex();
#endif

    nfc_run_ctx_init(&g_nfc_ctx);

    memset(&g_nfc_ctx, 0, sizeof(g_nfc_ctx)); // Initialize NDEF memory
    g_nfc_ctx.head.family = M1NFC_FAM_UNKNOWN;
    g_nfc_ctx.t2t.valid    = false;
    g_nfc_ctx.t2t.ndef_len = 0;

}

/*============================================================================*/
/**
 * @brief nfc_ctx_get - Get context pointer (read-only usage recommended)
 * 
 * Returns pointer to the global NFC context structure.
 * 
 * @retval Pointer to global NFC context
 */
/*============================================================================*/
nfc_run_ctx_t* nfc_ctx_get(void)
{
    return &g_nfc_ctx;
}

/*============================================================================*/
/**
 * @brief nfc_run_ctx_init - Initialize context with default values (prototype addition in header recommended)
 * 
 * Initializes NFC context structure with default values.
 * Sets file state to error state, clears header, dump metadata, parser, and UI state.
 * 
 * @param[in,out] c Pointer to NFC context structure
 * @retval None
 */
/*============================================================================*/
void nfc_run_ctx_init(nfc_run_ctx_t* c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));

    /* File/state: Not yet selected → Start in error state */
    c->file.sys_error   = 1;   /* Change to 0 when selection/load succeeds */
    c->file.uret        = 0;
    c->file.ext_len     = 0;
    c->file.ret_code    = 0;
    c->file.source_kind = LIVE_CARD; /* Default to Live */

    /* Header */
    c->head.tech    = 0;
    c->head.family  = M1NFC_FAM_UNKNOWN;
    c->head.uid_len = 0;
    c->head.a.has_atqa = false;
    c->head.a.has_sak  = false;
    c->head.a.ats_len  = 0;

    /* Dump metadata */
    c->dump.unit_size     = 0;
    c->dump.unit_count    = 0;
    c->dump.origin        = 0;
    c->dump.data          = NULL;
    c->dump.valid_bits    = NULL;
    c->dump.max_seen_unit = 0;
    c->dump.has_dump      = false;

    /* Parser */
    c->parser.token       = NULL;
    c->parser.p           = NULL;
    c->parser.parse_error = 0;
    /* c->parser.io / c->parser.line[] are cleared to 0 */

    /* UI/behavior */
    c->ui.preview_units    = 4;      /* Default 4-unit preview */
    c->ui.emulate_uid_only = true;   /* Default UID-only */
    c->ui.title_text[0]    = '\0';
    c->ui.uid_text[0]      = '\0';
}

/*============================================================================*/
/**
 * @brief nfc_ctx_begin_live - Begin Live card session: explicitly set source_kind for save-disable policy
 * 
 * Starts a live card reading session by setting source kind to LIVE_CARD,
 * clearing file path, and resetting error state.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_begin_live(void)
{
    nfc_ctx_lock();
    g_nfc_ctx.file.source_kind = LIVE_CARD;
    g_nfc_ctx.file.path[0]     = '\0';    /* No file path */
    g_nfc_ctx.file.sys_error   = 0;       /* Valid state */
    nfc_ctx_clear_dump();   // Clear previously loaded dump
    nfc_ctx_unlock();
}

/*============================================================================*/
/**
 * @brief nfc_ctx_begin_file - Begin file load session: set path and source kind
 * 
 * Starts a file loading session by setting source kind to LOAD_FILE
 * and storing the file path.
 * 
 * @param[in] fullpath Full path to the NFC file (NULL to clear)
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_begin_file(const char* fullpath)
{
    nfc_ctx_lock();
    g_nfc_ctx.file.source_kind = LOAD_FILE;
    if (fullpath) {
        strncpy(g_nfc_ctx.file.path, fullpath, sizeof(g_nfc_ctx.file.path)-1);
        g_nfc_ctx.file.path[sizeof(g_nfc_ctx.file.path)-1] = '\0';
    } else {
        g_nfc_ctx.file.path[0] = '\0';
    }
    g_nfc_ctx.file.sys_error = 0;
    nfc_ctx_unlock();
}

/*============================================================================*/
/**
 * @brief nfc_ctx_refresh_ui - Refresh UI text fields (title/UID)
 * 
 * Updates UI text fields based on current context family and UID.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_refresh_ui(void)
{
    nfc_ctx_lock();
    const char* t = family_to_title(g_nfc_ctx.head.family);
    strncpy(g_nfc_ctx.ui.title_text, t, sizeof(g_nfc_ctx.ui.title_text)-1);
    g_nfc_ctx.ui.title_text[sizeof(g_nfc_ctx.ui.title_text)-1] = '\0';

    make_uid_text(g_nfc_ctx.ui.uid_text, sizeof(g_nfc_ctx.ui.uid_text),
                  g_nfc_ctx.head.uid, g_nfc_ctx.head.uid_len);
    nfc_ctx_unlock();
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_dump - Clear entire dump (reset type/pointer/length)
 * 
 * Clears all dump metadata including unit size, count, origin,
 * data pointer, valid bits, and flags.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_clear_dump(void)
{
    nfc_ctx_lock();
    g_nfc_ctx.dump.unit_size     = 0;
    g_nfc_ctx.dump.unit_count    = 0;
    g_nfc_ctx.dump.origin        = 0;
    g_nfc_ctx.dump.data          = NULL;
    g_nfc_ctx.dump.valid_bits    = NULL;
    g_nfc_ctx.dump.max_seen_unit = 0;
    g_nfc_ctx.dump.has_dump      = false;
    nfc_ctx_unlock();
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_dump - Set dump metadata/pointer (bind workspace pointer)
 * 
 * Sets dump metadata and binds workspace pointers for dump data and valid bits.
 * 
 * @param[in] unit_size Size of each unit (e.g., 4 for T2T page, 16 for MFC block)
 * @param[in] unit_count Total number of units allocated
 * @param[in] origin Starting index (usually 0)
 * @param[in] data Pointer to dump data buffer
 * @param[in] valid_bits Pointer to valid bits bitmap (NULL if all valid)
 * @param[in] max_seen_unit Maximum unit index actually seen during parsing
 * @param[in] has_dump Whether dump exists (for UI display)
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_set_dump(uint16_t unit_size, uint32_t unit_count, uint32_t origin,
                      uint8_t* data, uint8_t* valid_bits, uint32_t max_seen_unit, bool has_dump)
{
    nfc_ctx_lock();
    g_nfc_ctx.dump.unit_size     = unit_size;
    g_nfc_ctx.dump.unit_count    = unit_count;
    g_nfc_ctx.dump.origin        = origin;
    g_nfc_ctx.dump.data          = data;
    g_nfc_ctx.dump.valid_bits    = valid_bits;
    g_nfc_ctx.dump.max_seen_unit = max_seen_unit;
    g_nfc_ctx.dump.has_dump      = has_dump;
    nfc_ctx_unlock();
}


/*============================================================================*/
/**
 * @brief nfc_classify_family_from_nfca - Classify family from NFC-A SAK/ATQA
 * 
 * Classifies NFC card family based on SAK and ATQA values.
 * Weak default rule: can be redefined in project if needed.
 * 
 * @param[in] sak SAK (Select Acknowledge) value
 * @param[in] atqa ATQA (Answer To Request) array (2 bytes)
 * @retval Family code (M1NFC_FAM_*), M1NFC_FAM_UNKNOWN if unrecognised
 */
/*============================================================================*/
uint8_t nfc_classify_family_from_nfca(uint8_t sak, const uint8_t atqa[2])
{
    if (sak == 0x08) { /* 1K */
            platformLog("MIFARE CLASSIC 1K\r\n");
        return M1NFC_FAM_CLASSIC;
    } else if (sak == 0x18) { /* 4K */
            platformLog("MIFARE Classic 4K\r\n");
        return M1NFC_FAM_CLASSIC;
    } else if (sak == 0x09) { /* Mini */
            platformLog("MIFARE Classic 0.3K\r\n");
        return M1NFC_FAM_CLASSIC;
    }
    if (sak == 0x00 && atqa[0] == 0x44) {
            platformLog("Ultralight/NTAG\r\n");
        return M1NFC_FAM_ULTRALIGHT;
    }
    if (sak == 0x20) {
            platformLog("MIFARE DESFire\r\n");
        return M1NFC_FAM_DESFIRE;
    }

    platformLog("Unknown ISO14443-A (sak=%02X atqa=%02X%02X)\r\n",
                sak, atqa[0], atqa[1]);
    return M1NFC_FAM_UNKNOWN;
}


/*============================================================================*/
/**
 * @brief FillNfcContextFromDevice - Fill nfc_ctx from device (header/UI)
 * 
 * Fills NFC context structure from RFAL NFC device information.
 * Extracts UID, ATQA, SAK, and classifies family type.
 * 
 * @param[in] dev Pointer to RFAL NFC device
 * @retval 0 Success
 * @retval 1 Invalid device pointer
 * @retval 2 Unsupported device type
 */
/*============================================================================*/
uint8_t FillNfcContextFromDevice(const rfalNfcDevice* dev)
{
    if (!dev) return 1;

    nfc_ctx_begin_live();
    nfc_run_ctx_t* c = nfc_ctx_get();
    memset(&c->head, 0, sizeof(c->head));

    switch (dev->type)
    {
        case RFAL_NFC_LISTEN_TYPE_NFCA:
        {
            c->head.tech = M1NFC_TECH_A; // NFC_TX_A; // Note: M1NFC_TECH_* recommended for tech
            uint8_t len = (uint8_t)dev->nfcidLen;
            if (len > sizeof(c->head.uid)) len = sizeof(c->head.uid);
            c->head.uid_len = len;
            memcpy(c->head.uid, dev->nfcid, len);

            c->head.a.has_atqa = true;
            c->head.a.atqa[0]  = dev->dev.nfca.sensRes.anticollisionInfo;
            c->head.a.atqa[1]  = dev->dev.nfca.sensRes.platformInfo;
            c->head.a.has_sak  = true;
            c->head.a.sak      = dev->dev.nfca.selRes.sak;

            c->head.a.ats_len  = 0; // ISO-DEP(ATS) can be reflected later from proto.isoDep if needed
            c->head.family     = nfc_classify_family_from_nfca(c->head.a.sak, c->head.a.atqa);
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_NFCV:
        {
            /* ISO15693 / NFC-V: capture identity here; DSFID/AFI/blocks are
             * filled afterwards by the poller's m1_nfcv_read(). */
            c->head.tech   = M1NFC_TECH_V;
            c->head.family = M1NFC_FAM_15693;
            uint8_t len = (uint8_t)dev->nfcidLen;
            if (len > sizeof(c->head.uid)) len = sizeof(c->head.uid);
            c->head.uid_len = len;
            /* ISO15693 UID is transmitted LSB-first; store MSB-first (0xE0
             * leading) to match the display convention. */
            for (uint8_t i = 0; i < len; i++) {
                c->head.uid[i] = dev->nfcid[len - 1U - i];
            }
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_ST25TB:
        {
            /* ST25TB / SRI / SRIX (ISO14443-B based). UID comes from the
             * ST25TB device struct; store reversed for detection
             * (uid[2]>>2) and display. Variant, chip ID,
             * blocks and system block are filled afterwards by the poller's
             * m1_st25tb_read(). */
            c->head.tech   = M1NFC_TECH_B;
            c->head.family = M1NFC_FAM_ST25TB;
            uint8_t len = (uint8_t)RFAL_ST25TB_UID_LEN;
            if (len > sizeof(c->head.uid)) len = sizeof(c->head.uid);
            c->head.uid_len = len;
            for (uint8_t i = 0; i < len; i++) {
                c->head.uid[i] = dev->dev.st25tb.UID[len - 1U - i];
            }
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_NFCB:
        case RFAL_NFC_LISTEN_TYPE_NFCF:
        default:
            return 2;
    }

    nfc_ctx_refresh_ui();
    nfc_ctx_sync_emu(); /* Sync ctx→emu when reading live card as well */
    
    return 0;
}


/*============================================================================*/
/**
 * @brief nfc_ctx_sync_emu - Reflect current nfc_ctx.head content to emulator EmuNfcA(g_emuA)
 * 
 * Synchronizes NFC context header information to emulator structure.
 * Handles T2T family special case (forces ATQA/SAK to prevent ISO-DEP).
 * Sets default ATQA/SAK values based on family if not provided.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_sync_emu(void)
{
    nfc_run_ctx_t* c = nfc_ctx_get();
    if (!c) return;

    /* If no UID, emulation impossible → clear */
    if (c->head.uid_len == 0) {
        Emu_Clear();
        return;
    }

    uint8_t atqa0 = 0;
    uint8_t atqa1 = 0;
    uint8_t sak   = 0;

    /* If ATQA/SAK came from file/reader, use as-is */
    if (c->head.a.has_atqa) {
        atqa0 = c->head.a.atqa[0];
        atqa1 = c->head.a.atqa[1];
    }
    if (c->head.a.has_sak) {
        sak = c->head.a.sak;
    }

    /* For T2T (Ultralight/NTAG) family, force SAK/ATQA setting (prevent ISO-DEP) */
    if (c->head.family == M1NFC_FAM_ULTRALIGHT) {
        /* T2T always sets SAK=0x00, ATQA=0x44 0x00 to disable ISO-DEP */
        atqa0 = 0x44;
        atqa1 = 0x00;
        sak   = 0x00;
        platformLog("[CE] T2T family detected: forcing ATQA=0x%02X%02X SAK=0x%02X (ISO-DEP disabled)\r\n", atqa0, atqa1, sak);
    }
    /* If ATQA/SAK is empty, set default values based on family (adjust if needed) */
    else if (!c->head.a.has_atqa || !c->head.a.has_sak) {
#ifdef M1NFC_FAM_CLASSIC
        if (c->head.family == M1NFC_FAM_CLASSIC) {
            /* MIFARE Classic (common default for 1K/4K) */
            if (!c->head.a.has_atqa) { atqa0 = 0x04; atqa1 = 0x00; }
            if (!c->head.a.has_sak)  sak   = 0x08;    // Based on 1K, add 4K distinction if needed
        }
#endif
#ifdef M1NFC_FAM_T4T
        if (c->head.family == M1NFC_FAM_T4T
#ifdef M1NFC_FAM_DESFIRE
            || c->head.family == M1NFC_FAM_DESFIRE
#endif
           ) {
            if (!c->head.a.has_atqa) { atqa0 = 0x04; atqa1 = 0x00; }
            if (!c->head.a.has_sak)  sak   = 0x20;   // ISO-DEP
        }
#endif
    }

    /* Currently only NFCA is supported */
    Emu_SetNfcA(c->head.uid, c->head.uid_len, atqa0, atqa1, sak);
}



/* --------- T2T NDEF helpers ---------- */
/*============================================================================*/
/**
 * @brief nfc_ctx_clear_t2t_ndef - Clear T2T NDEF data
 * 
 * Clears T2T NDEF validity flag and length.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_ndef(void)
{
    g_nfc_ctx.t2t.valid    = false;
    g_nfc_ctx.t2t.ndef_len = 0;
    // If you want to clear memory as well, add below
    // memset(g_nfc_ctx.t2t.ndef, 0, NFC_T2T_NDEF_MAX_LEN);
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_t2t_ndef - Set T2T NDEF data
 * 
 * Sets T2T NDEF data from buffer. If buffer is NULL, clears NDEF.
 * 
 * @param[in] buf Pointer to NDEF data buffer (NULL to clear)
 * @param[in] len Length of NDEF data
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_set_t2t_ndef(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL) {
        nfc_ctx_clear_t2t_ndef();
        return;
    }

    if (len > NFC_T2T_NDEF_MAX_LEN) {
        len = NFC_T2T_NDEF_MAX_LEN;
    }

    memcpy(g_nfc_ctx.t2t.ndef, buf, len);
    g_nfc_ctx.t2t.ndef_len = len;   // Can be 0
    g_nfc_ctx.t2t.valid    = true;  // Even if length is 0, treat as "empty NDEF"

}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_t2t_info - Get T2T info structure pointer
 * 
 * @retval Pointer to T2T info structure
 */
/*============================================================================*/
const nfc_t2t_info_t * nfc_ctx_get_t2t_info(void)
{
    return &g_nfc_ctx.t2t;
}


/*-------------util function-----------*/
/*============================================================================*/
/**
 * @brief nfc_ctx_dump_t2t_ndef - Dump T2T NDEF data in hex/ASCII format
 * 
 * Logs T2T NDEF data in formatted hex and ASCII representation.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_dump_t2t_ndef(void)
{
    if (!g_nfc_ctx.t2t.valid) {
        platformLog("T2T NDEF: not valid\r\n");
        return;
    }

    uint16_t len = g_nfc_ctx.t2t.ndef_len;
    const uint8_t *buf = g_nfc_ctx.t2t.ndef;

    platformLog("T2T NDEF dump (len=%u bytes)\r\n", len);
    platformLog("Page  HEX         | ASCII\r\n");
    platformLog("-------------------------\r\n");


    uint16_t offset = 0;
    uint16_t page   = 0;   // Can change this to 4 to match "Tag's Page4 onwards" if needed

    while (offset < len) {
        char hexLine[3 * 4 + 1];   // "FF " * 4 = 12 chars + null
        char asciiLine[4 + 1];     // 4 chars + null
        uint16_t bytesThisPage = (len - offset >= 4) ? 4 : (len - offset);

        int hpos = 0;
        int apos = 0;

        for (uint16_t i = 0; i < bytesThisPage; i++) {
            uint8_t b = buf[offset + i];

            // Accumulate HEX string
            hpos += sprintf(&hexLine[hpos], "%02X ", b);

            // Accumulate ASCII string
            char c = (char)b;
            if ((c < 0x20) || (c > 0x7E)) {
                c = '.';
            }
            asciiLine[apos++] = c;
        }

        hexLine[hpos]   = '\0';
        asciiLine[apos] = '\0';

        // Pad HEX part for last page with less than 4 bytes (for alignment, optional)
        while (hpos < 3 * 4) {
            hpos += sprintf(&hexLine[hpos], "   ");
        }

        platformLog("P%02u: %s | %s\r\n", page, hexLine, asciiLine);

        offset += bytesThisPage;
        page++;
    }
    platformLog("-------------------------\r\n");
}


/*============================================================================*/
/**
 * @brief nfc_ctx_dump_t2t_pages - Dump T2T pages in hex/ASCII format
 * 
 * Logs all T2T pages from dump in formatted hex and ASCII representation.
 * For LIVE_CARD source, only valid pages (according to valid_bits) are shown.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_dump_t2t_pages(void)
{
    const nfc_dump_meta_t *d = &g_nfc_ctx.dump;

    /* Skip if not Type 2 / NTAG */
    if ((g_nfc_ctx.head.tech   != M1NFC_TECH_A) ||
        (g_nfc_ctx.head.family != M1NFC_FAM_ULTRALIGHT)) {
        platformLog("T2T PAGE: not Ultralight/NTAG (tech=%u, family=%u)\r\n",
                    g_nfc_ctx.head.tech, g_nfc_ctx.head.family);
        return;
    }

    if (!d->has_dump || d->data == NULL || d->unit_count == 0) {
        platformLog("T2T PAGE: dump not valid\r\n");
        return;
    }

    if (d->unit_size != 4) {
        platformLog("T2T PAGE: unexpected unit_size=%u (expected 4)\r\n",
                    d->unit_size);
        return;
    }

    /* Actual used page count is limited by max_seen_unit */
    uint32_t pages = d->max_seen_unit + 1;
    if (pages == 0 || pages > d->unit_count) {
        pages = d->unit_count;
    }

    g_nfc_ntag_page_count = (uint16_t)pages;

    uint32_t origin  = d->origin;
    const uint8_t *dump  = d->data;
    const uint8_t *vbits = d->valid_bits;

    platformLog("T2T PAGE dump (pages=%lu, origin=%lu, unit_size=%u, max_seen=%lu, src=%d)\r\n",
                (unsigned long)pages,
                (unsigned long)origin,
                d->unit_size,
                (unsigned long)d->max_seen_unit,
                g_nfc_ctx.file.source_kind);

    platformLog("vbits=%p\r\n", vbits);
    platformLog("Page  HEX          | ASCII\r\n");
    platformLog("-------------------------\r\n");

    /* For files loaded from LOAD_FILE, ignore valid_bits and print all for now */
    bool use_valid_bits = ( (vbits != NULL) && (g_nfc_ctx.file.source_kind == LIVE_CARD) );

    for (uint32_t i = 0; i < pages; i++) {
        uint8_t is_valid = 1;
        if (use_valid_bits) {
            uint32_t byte_idx = (i >> 3);          // i / 8
            uint8_t  mask     = (uint8_t)(1u << (i & 0x07)); // i % 8
            if ((vbits[byte_idx] & mask) == 0) {
                is_valid = 0;
            }
        }
        if (!is_valid) {
            continue;
        }

        const uint8_t *page_ptr = &dump[i * d->unit_size];
        uint32_t page_no = origin + i;

        char hexLine[3 * 4 + 1];   // "FF " * 4 + null
        char asciiLine[4 + 1];     // 4 chars + null
        int  hpos = 0;
        int  apos = 0;

        for (uint16_t j = 0; j < 4; j++) {
            uint8_t b = page_ptr[j];
            hpos += sprintf(&hexLine[hpos], "%02X ", b);

            char c = (char)b;
            if ((c < 0x20) || (c > 0x7E)) {
                c = '.';
            }
            asciiLine[apos++] = c;
        }

        hexLine[hpos]   = '\0';
        asciiLine[apos] = '\0';

        platformLog("P%03lu: %s | %s\r\n",
                    (unsigned long)page_no, hexLine, asciiLine);
        osDelay(1);
    }

    platformLog("-------------------------\r\n");
}


/*============================================================================*/
/**
 * @brief nfc_ctx_get_t2t_page_count - Return Type 2 / NTAG page count (0 if none)
 * 
 * Returns the number of pages in T2T dump.
 * 
 * @retval Page count, or 0 if not T2T or dump invalid
 */
/*============================================================================*/
uint16_t nfc_ctx_get_t2t_page_count(void)
{
    const nfc_dump_meta_t *d = &g_nfc_ctx.dump;

    /* Serves both the Type-2 page dump (tech A / Ultralight) and a 4-byte-block
     * ISO15693 / NFC-V dump (tech V / 15693) so Preview and Raw Data reuse the
     * validated 4-byte-unit rendering. Non-4-byte-block V tags return 0 here. */
    bool is_t2t  = (g_nfc_ctx.head.tech == M1NFC_TECH_A) && (g_nfc_ctx.head.family == M1NFC_FAM_ULTRALIGHT);
    bool is_nfcv = (g_nfc_ctx.head.tech == M1NFC_TECH_V) && (g_nfc_ctx.head.family == M1NFC_FAM_15693);
    bool is_st25tb = (g_nfc_ctx.head.tech == M1NFC_TECH_B) && (g_nfc_ctx.head.family == M1NFC_FAM_ST25TB);
    if ( (!is_t2t && !is_nfcv && !is_st25tb) ||
         (!d->has_dump) || (d->data == NULL) || (d->unit_size != 4) ) {
        return 0;
    }

    uint32_t pages = d->max_seen_unit + 1U;
    if ((pages == 0U) || (pages > d->unit_count)) {
        pages = d->unit_count;
    }

    return (uint16_t)pages;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_t2t_page - Copy pageIndex-th T2T page to out[4] (returns true on success)
 * 
 * Retrieves a specific T2T page from dump data.
 * 
 * @param[in] pageIndex Page index to retrieve
 * @param[out] out Output buffer (4 bytes)
 * @retval true Success
 * @retval false Invalid parameters or page index out of range
 */
/*============================================================================*/
bool nfc_ctx_get_t2t_page(uint16_t pageIndex, uint8_t out[4])
{
    const nfc_dump_meta_t *d = &g_nfc_ctx.dump;

    if (!out) return false;

    /* Also serves the 4-byte-block ISO15693 / NFC-V dump (see page_count). */
    bool is_t2t  = (g_nfc_ctx.head.tech == M1NFC_TECH_A) && (g_nfc_ctx.head.family == M1NFC_FAM_ULTRALIGHT);
    bool is_nfcv = (g_nfc_ctx.head.tech == M1NFC_TECH_V) && (g_nfc_ctx.head.family == M1NFC_FAM_15693);
    bool is_st25tb = (g_nfc_ctx.head.tech == M1NFC_TECH_B) && (g_nfc_ctx.head.family == M1NFC_FAM_ST25TB);
    if ( (!is_t2t && !is_nfcv && !is_st25tb) ||
         (!d->has_dump) || (d->data == NULL) || (d->unit_size != 4) ) {
        return false;
    }

    uint32_t pages = d->max_seen_unit + 1U;
    if ((pages == 0U) || (pages > d->unit_count)) {
        pages = d->unit_count;
    }

    if (pageIndex >= pages) {
        return false;
    }

    const uint8_t *page_ptr = &d->data[ (uint32_t)pageIndex * d->unit_size ];
    memcpy(out, page_ptr, 4);

    return true;
}

bool nfc_ctx_t2t_page_valid(uint16_t pageIndex)
{
    const nfc_dump_meta_t *d = &g_nfc_ctx.dump;

    bool is_t2t  = (g_nfc_ctx.head.tech == M1NFC_TECH_A) && (g_nfc_ctx.head.family == M1NFC_FAM_ULTRALIGHT);
    if (!is_t2t || !d->has_dump || d->data == NULL || d->unit_size != 4) {
        return false;
    }

    uint32_t pages = d->max_seen_unit + 1U;
    if ((pages == 0U) || (pages > d->unit_count)) {
        pages = d->unit_count;
    }
    if (pageIndex >= pages) {
        return false;
    }

    if (d->valid_bits == NULL) {
        return true;   /* no bitmap -> treat everything in range as valid */
    }
    return ((d->valid_bits[pageIndex >> 3] >> (pageIndex & 7U)) & 1U) != 0U;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_format_t2t_page_line - Format T2T page as single line string
 * 
 * Formats a T2T page as a formatted line string (e.g., "P000:04 B7 28 13|..(.").
 * 
 * @param[in] pageIndex Page index to format
 * @param[out] buf Output buffer for formatted string
 * @param[in] bufSize Size of output buffer (must be at least 32)
 * @retval true Success
 * @retval false Invalid parameters or page retrieval failed
 */
/*============================================================================*/
bool nfc_ctx_format_t2t_page_line(uint16_t pageIndex, char *buf, size_t bufSize)
{
    uint8_t page[4];

    if (!buf || bufSize < 32) {
        return false;
    }

    if (!nfc_ctx_get_t2t_page(pageIndex, page)) {
        return false;
    }

    char a0 = (page[0] >= 0x20 && page[0] <= 0x7E) ? (char)page[0] : '.';
    char a1 = (page[1] >= 0x20 && page[1] <= 0x7E) ? (char)page[1] : '.';
    char a2 = (page[2] >= 0x20 && page[2] <= 0x7E) ? (char)page[2] : '.';
    char a3 = (page[3] >= 0x20 && page[3] <= 0x7E) ? (char)page[3] : '.';

    // Page000: 04 B7 28 13 | ..(.
    snprintf(buf, bufSize,
             "P%03u:%02X %02X %02X %02X|%c%c%c%c",
             (unsigned)pageIndex,
             page[0], page[1], page[2], page[3],
             a0, a1, a2, a3);

    return true;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_t2t_version - Set T2T version information
 * 
 * Stores T2T GET_VERSION response data (8 bytes max).
 * 
 * @param[in] ver Pointer to version data
 * @param[in] len Length of version data (0-8)
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_set_t2t_version(const uint8_t *ver, uint8_t len)
{
    if (!ver || len == 0 || len > 8) {
        platformLog("T2T version: clear\r\n");
        s_t2t_version_len = 0;
        return;
    }
    memcpy(s_t2t_version, ver, len);
    s_t2t_version_len = len;
    platformLog("Set T2T version: %s\r\n", hex2Str(s_t2t_version, s_t2t_version_len));

    // platformLog("Set T2T version: ");
    // for (uint8_t i = 0; i < s_t2t_version_len; i++) {
    //     platformLog("%02X ", s_t2t_version[i]);
    // }
    // platformLog("\r\n");
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_t2t_version - Get T2T version information
 * 
 * Retrieves stored T2T GET_VERSION response data.
 * 
 * @param[out] out Output buffer (8 bytes)
 * @retval Length of version data (0 if not available)
 */
/*============================================================================*/
uint8_t nfc_ctx_get_t2t_version(uint8_t out[8])
{
    if (!out || s_t2t_version_len == 0) 
    {
        return 0;
    }
    memcpy(out, s_t2t_version, s_t2t_version_len);
    return s_t2t_version_len;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_t2t_variant - Record the resolved T2T sub-variant
 */
/*============================================================================*/
void nfc_ctx_set_t2t_variant(uint8_t variant)
{
    s_t2t_variant = variant;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_t2t_variant - Get the resolved T2T sub-variant
 */
/*============================================================================*/
uint8_t nfc_ctx_get_t2t_variant(void)
{
    return s_t2t_variant;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_t2t_version - Reset the raw GET_VERSION tuple to "not
 * captured". Mirrors nfc_ctx_clear_t2t_signature()/_counters()/_tearing()'s
 * clear-before-conditionally-set pattern so a file/tag with no version data
 * can never inherit a stale tuple left over from whatever was loaded/read
 * previously.
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_version(void)
{
    s_t2t_version_len = 0;
    memset(s_t2t_version, 0, sizeof(s_t2t_version));
}

void nfc_ctx_set_t2t_expected_pages(uint16_t pages)
{
    s_t2t_expected_pages = pages;
}

uint16_t nfc_ctx_get_t2t_expected_pages(void)
{
    return s_t2t_expected_pages;
}

void nfc_ctx_set_t2t_geometry_corrupt(bool corrupt)
{
    s_t2t_geometry_corrupt = corrupt;
}

bool nfc_ctx_t2t_geometry_corrupt(void)
{
    return s_t2t_geometry_corrupt;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_*_t2t_auth0/_prot/_authlim - Protection state parsed from
 * CFG0/CFG1. See nfc_ctx.h.
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_protection(void)
{
    s_t2t_auth0_valid   = false;
    s_t2t_prot_valid    = false;
    s_t2t_authlim_valid = false;
    s_t2t_auth0   = 0;
    s_t2t_prot    = false;
    s_t2t_authlim = 0;
}

void nfc_ctx_set_t2t_auth0(uint8_t auth0)
{
    s_t2t_auth0 = auth0;
    s_t2t_auth0_valid = true;
}

bool nfc_ctx_get_t2t_auth0(uint8_t *out)
{
    if (!out || !s_t2t_auth0_valid) { return false; }
    *out = s_t2t_auth0;
    return true;
}

void nfc_ctx_set_t2t_prot(bool prot)
{
    s_t2t_prot = prot;
    s_t2t_prot_valid = true;
}

bool nfc_ctx_get_t2t_prot(bool *out)
{
    if (!out || !s_t2t_prot_valid) { return false; }
    *out = s_t2t_prot;
    return true;
}

void nfc_ctx_set_t2t_authlim(uint8_t authlim)
{
    s_t2t_authlim = authlim;
    s_t2t_authlim_valid = true;
}

bool nfc_ctx_get_t2t_authlim(uint8_t *out)
{
    if (!out || !s_t2t_authlim_valid) { return false; }
    *out = s_t2t_authlim;
    return true;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_*_t2t_protection_suspected - Transient, pre-auth signal
 * only. Deliberately separate storage from s_t2t_auth0/_prot/_authlim
 * above -- see nfc_ctx.h for why this must never feed those fields, the
 * V4 file, or m1_t2t_emu_image_build().
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_protection_suspected(void)
{
    s_t2t_protection_suspected  = false;
    s_t2t_first_blocked_page    = 0;
}

void nfc_ctx_set_t2t_protection_suspected(uint16_t first_blocked_page)
{
    s_t2t_protection_suspected = true;
    s_t2t_first_blocked_page   = first_blocked_page;
}

bool nfc_ctx_get_t2t_protection_suspected(uint16_t *first_blocked_page_out)
{
    if (!s_t2t_protection_suspected) { return false; }
    if (first_blocked_page_out) { *first_blocked_page_out = s_t2t_first_blocked_page; }
    return true;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_*_t2t_credential - Genuine PWD_AUTH (0x1B) password+PACK,
 * accepted by a real tag. See nfc_ctx.h.
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_credential(void)
{
    s_t2t_credential_valid = false;
    memset(s_t2t_pwd, 0, sizeof(s_t2t_pwd));
    memset(s_t2t_pack, 0, sizeof(s_t2t_pack));
}

void nfc_ctx_set_t2t_credential(const uint8_t pwd[4], const uint8_t pack[2])
{
    if (!pwd || !pack) { return; }
    memcpy(s_t2t_pwd, pwd, sizeof(s_t2t_pwd));
    memcpy(s_t2t_pack, pack, sizeof(s_t2t_pack));
    s_t2t_credential_valid = true;
}

bool nfc_ctx_get_t2t_pwd(uint8_t out[4])
{
    if (!out || !s_t2t_credential_valid) { return false; }
    memcpy(out, s_t2t_pwd, sizeof(s_t2t_pwd));
    return true;
}

bool nfc_ctx_get_t2t_pack(uint8_t out[2])
{
    if (!out || !s_t2t_credential_valid) { return false; }
    memcpy(out, s_t2t_pack, sizeof(s_t2t_pack));
    return true;
}

bool nfc_ctx_t2t_credential_valid(void)
{
    return s_t2t_credential_valid;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_*_t2t_signature - Genuine READ_SIGNATURE (0x3C) originality
 * data. See nfc_ctx.h.
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_signature(void)
{
    s_t2t_signature_valid = false;
    memset(s_t2t_signature, 0, sizeof(s_t2t_signature));
}

void nfc_ctx_set_t2t_signature(const uint8_t sig[32])
{
    if (!sig) { return; }
    memcpy(s_t2t_signature, sig, sizeof(s_t2t_signature));
    s_t2t_signature_valid = true;
}

bool nfc_ctx_get_t2t_signature(uint8_t out[32])
{
    if (!out || !s_t2t_signature_valid) { return false; }
    memcpy(out, s_t2t_signature, sizeof(s_t2t_signature));
    return true;
}

bool nfc_ctx_t2t_signature_valid(void)
{
    return s_t2t_signature_valid;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_*_t2t_counter / _tearing - Genuine READ_CNT (0x39) and
 * CHECK_TEARING (0x3E) per-index data. See nfc_ctx.h.
 */
/*============================================================================*/
void nfc_ctx_clear_t2t_counters(void)
{
    for (uint8_t i = 0; i < NFC_T2T_COUNTER_MAX; i++) {
        s_t2t_counter_valid[i] = false;
        memset(s_t2t_counter[i], 0, sizeof(s_t2t_counter[i]));
    }
}

void nfc_ctx_set_t2t_counter(uint8_t idx, const uint8_t val[3])
{
    if (!val || idx >= NFC_T2T_COUNTER_MAX) { return; }
    memcpy(s_t2t_counter[idx], val, 3U);
    s_t2t_counter_valid[idx] = true;
}

bool nfc_ctx_get_t2t_counter(uint8_t idx, uint8_t out[3])
{
    if (!out || idx >= NFC_T2T_COUNTER_MAX || !s_t2t_counter_valid[idx]) { return false; }
    memcpy(out, s_t2t_counter[idx], 3U);
    return true;
}

bool nfc_ctx_t2t_counter_valid(uint8_t idx)
{
    return (idx < NFC_T2T_COUNTER_MAX) && s_t2t_counter_valid[idx];
}

void nfc_ctx_clear_t2t_tearing(void)
{
    for (uint8_t i = 0; i < NFC_T2T_COUNTER_MAX; i++) {
        s_t2t_tearing_valid[i] = false;
        s_t2t_tearing[i] = 0U;
    }
}

void nfc_ctx_set_t2t_tearing(uint8_t idx, uint8_t val)
{
    if (idx >= NFC_T2T_COUNTER_MAX) { return; }
    s_t2t_tearing[idx] = val;
    s_t2t_tearing_valid[idx] = true;
}

bool nfc_ctx_get_t2t_tearing(uint8_t idx, uint8_t *out)
{
    if (!out || idx >= NFC_T2T_COUNTER_MAX || !s_t2t_tearing_valid[idx]) { return false; }
    *out = s_t2t_tearing[idx];
    return true;
}

bool nfc_ctx_t2t_tearing_valid(uint8_t idx)
{
    return (idx < NFC_T2T_COUNTER_MAX) && s_t2t_tearing_valid[idx];
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_iso15693 - Reset the ISO15693/NFC-V info block
 */
/*============================================================================*/
void nfc_ctx_clear_iso15693(void)
{
    nfc_run_ctx_t *c = nfc_ctx_get();
    memset(&c->v, 0, sizeof(c->v));
    c->v.variant = M1NFC_VVAR_GENERIC;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_iso15693_variant - Record the resolved NFC-V sub-variant
 */
/*============================================================================*/
void nfc_ctx_set_iso15693_variant(uint8_t variant)
{
    nfc_ctx_get()->v.variant = variant;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_iso15693_sysinfo - Store GET SYSTEM INFORMATION results
 */
/*============================================================================*/
void nfc_ctx_set_iso15693_sysinfo(bool has_dsfid, uint8_t dsfid,
                                  bool has_afi, uint8_t afi,
                                  uint16_t block_count, uint8_t block_size,
                                  bool has_sysinfo)
{
    nfc_iso15693_info_t *v = &nfc_ctx_get()->v;
    v->has_dsfid   = has_dsfid;
    v->dsfid       = dsfid;
    v->has_afi     = has_afi;
    v->afi         = afi;
    v->block_count = block_count;
    v->block_size  = block_size;
    v->has_sysinfo = has_sysinfo;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_iso15693_blocks_read - Record blocks actually read (X)
 */
/*============================================================================*/
void nfc_ctx_set_iso15693_blocks_read(uint16_t blocks_read)
{
    nfc_ctx_get()->v.blocks_read = blocks_read;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_iso15693_security - Store block lock/security status (0x2C)
 */
/*============================================================================*/
void nfc_ctx_set_iso15693_security(bool has_security, const uint8_t *locked_bits,
                                   uint16_t n_blocks, uint16_t locked_count)
{
    nfc_iso15693_info_t *v = &nfc_ctx_get()->v;
    memset(v->block_locked, 0, sizeof(v->block_locked));
    v->has_security = has_security;
    v->locked_count = has_security ? locked_count : 0U;
    if (has_security && (locked_bits != NULL)) {
        uint16_t maxbits = (uint16_t)(sizeof(v->block_locked) * 8U);
        if (n_blocks > maxbits) n_blocks = maxbits;
        memcpy(v->block_locked, locked_bits, (size_t)((n_blocks + 7U) / 8U));
    }
}

/*============================================================================*/
/**
 * @brief nfc_ctx_iso15693_block_locked - Per-block lock query (read-only)
 */
/*============================================================================*/
bool nfc_ctx_iso15693_block_locked(uint16_t block_index)
{
    const nfc_iso15693_info_t *v = &nfc_ctx_get()->v;
    if (!v->has_security) return false;
    if (block_index >= (uint16_t)(sizeof(v->block_locked) * 8U)) return false;
    return ((v->block_locked[block_index >> 3] >> (block_index & 7U)) & 1U) != 0U;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_iso15693_info - Get the ISO15693/NFC-V info block
 */
/*============================================================================*/
const nfc_iso15693_info_t * nfc_ctx_get_iso15693_info(void)
{
    return &nfc_ctx_get()->v;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_desfire - Reset the DESFire (Type 4A) info block
 */
/*============================================================================*/
void nfc_ctx_clear_desfire(void)
{
    memset(&nfc_ctx_get()->desfire, 0, sizeof(nfc_ctx_get()->desfire));
    mf_desfire_deep_reset(&nfc_ctx_get()->desfire_deep);
    nfc_transit_result_reset(&nfc_ctx_get()->transit);
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_desfire_version - Store the parsed GetVersion identity
 */
/*============================================================================*/
void nfc_ctx_set_desfire_version(const uint8_t *v28)
{
    nfc_desfire_info_t *d = &nfc_ctx_get()->desfire;
    d->present = (v28 != NULL);
    if (v28 != NULL) memcpy(d->v, v28, sizeof(d->v));
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_desfire_info - Get the DESFire info block (never NULL)
 */
/*============================================================================*/
const nfc_desfire_info_t * nfc_ctx_get_desfire_info(void)
{
    return &nfc_ctx_get()->desfire;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_desfire_deep - Store a completed deep-read result
 */
/*============================================================================*/
void nfc_ctx_set_desfire_deep(const nfc_desfire_deep_info_t *deep)
{
    nfc_desfire_deep_info_t *d = &nfc_ctx_get()->desfire_deep;
    if (deep == NULL) {
        mf_desfire_deep_reset(d);
    } else {
        *d = *deep;
    }
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_desfire_deep - Get the DESFire deep-read block (never NULL)
 */
/*============================================================================*/
const nfc_desfire_deep_info_t * nfc_ctx_get_desfire_deep(void)
{
    return &nfc_ctx_get()->desfire_deep;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_transit - Store a completed card-interpretation result
 */
/*============================================================================*/
void nfc_ctx_set_transit(const nfc_transit_info_t *transit)
{
    nfc_transit_info_t *t = &nfc_ctx_get()->transit;
    if (transit == NULL) {
        nfc_transit_result_reset(t);
    } else {
        *t = *transit;
    }
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_transit - Get the card-interpretation result (never NULL)
 */
/*============================================================================*/
const nfc_transit_info_t * nfc_ctx_get_transit(void)
{
    return &nfc_ctx_get()->transit;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_st25tb - Reset the ST25TB/SRI/SRIX info block
 */
/*============================================================================*/
void nfc_ctx_clear_st25tb(void)
{
    nfc_run_ctx_t *c = nfc_ctx_get();
    memset(&c->tb, 0, sizeof(c->tb));
    c->tb.variant = M1NFC_TBVAR_GENERIC;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_st25tb_ident - Record variant, chip ID and geometry
 */
/*============================================================================*/
void nfc_ctx_set_st25tb_ident(uint8_t variant, uint8_t chip_id,
                              uint16_t block_count, uint8_t block_size)
{
    nfc_st25tb_info_t *tb = &nfc_ctx_get()->tb;
    tb->variant     = variant;
    tb->chip_id     = chip_id;
    tb->block_count = block_count;
    tb->block_size  = block_size;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_st25tb_blocks_read - Record data blocks actually read (X)
 */
/*============================================================================*/
void nfc_ctx_set_st25tb_blocks_read(uint16_t blocks_read)
{
    nfc_ctx_get()->tb.blocks_read = blocks_read;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_st25tb_system - Record the system/OTP block (0xFF)
 */
/*============================================================================*/
void nfc_ctx_set_st25tb_system(const uint8_t sys4[4])
{
    nfc_st25tb_info_t *tb = &nfc_ctx_get()->tb;
    if (sys4) {
        memcpy(tb->system_otp, sys4, 4);
        tb->has_system = true;
    }
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_st25tb_info - Get the ST25TB/SRI/SRIX info block
 */
/*============================================================================*/
const nfc_st25tb_info_t * nfc_ctx_get_st25tb_info(void)
{
    return &nfc_ctx_get()->tb;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_clear_mfc - Reset the MIFARE Classic Phase-A info block
 */
/*============================================================================*/
void nfc_ctx_clear_mfc(void)
{
    memset(&nfc_ctx_get()->mfc, 0, sizeof(nfc_ctx_get()->mfc));
    /* A genuinely new acquisition generation begins here (every call site is
     * "about to (re)read a card from scratch") -- the Find Missing Keys
     * dedup/resume state must never survive into it, or a later
     * continuation could silently trust a byte offset or seen[] set that
     * belongs to a completely different card. */
    mfc_dict_resume_reset(nfc_ctx_get_mfc_dict_resume());
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_mfc_info - Get the MIFARE Classic Phase-A info block
 */
/*============================================================================*/
nfc_mfc_info_t * nfc_ctx_get_mfc_info(void)
{
    return &nfc_ctx_get()->mfc;
}

/*============================================================================*/
/**
 * @brief nfc_ctx_get_wr_status - shared NTAG21x live-write status block.
 * Small, standalone (not part of nfc_run_ctx): it survives the source dump and
 * carries no duplicated tag data.
 */
/*============================================================================*/
static nfc_wr_status_t s_nfc_wr_status = {0};
nfc_wr_status_t * nfc_ctx_get_wr_status(void)
{
    return &s_nfc_wr_status;
}

static nfc_mfc_scan_t s_nfc_mfc_scan = {0};
nfc_mfc_scan_t * nfc_ctx_get_mfc_scan(void)
{
    return &s_nfc_mfc_scan;
}
void nfc_ctx_clear_mfc_scan(void)
{
    memset(&s_nfc_mfc_scan, 0, sizeof(s_nfc_mfc_scan));
}

static mfc_dict_resume_t s_nfc_mfc_dict_resume = {0};
mfc_dict_resume_t * nfc_ctx_get_mfc_dict_resume(void)
{
    return &s_nfc_mfc_dict_resume;
}

static nfc_harvest_ui_t s_nfc_harvest = {0};
nfc_harvest_ui_t * nfc_ctx_get_harvest(void)
{
    return &s_nfc_harvest;
}

static nfc_solve_ui_t s_nfc_solve = {0};
nfc_solve_ui_t * nfc_ctx_get_solve(void)
{
    return &s_nfc_solve;
}

static nfc_mfc_write_t s_nfc_mfc_write = {0};
nfc_mfc_write_t * nfc_ctx_get_mfc_write(void)
{
    return &s_nfc_mfc_write;
}
void nfc_ctx_clear_mfc_write(void)
{
    memset(&s_nfc_mfc_write, 0, sizeof(s_nfc_mfc_write));
}

static nfc_t2t_unlock_t s_nfc_t2t_unlock = {0};
nfc_t2t_unlock_t * nfc_ctx_get_t2t_unlock(void)
{
    return &s_nfc_t2t_unlock;
}
void nfc_ctx_clear_t2t_unlock(void)
{
    memset(&s_nfc_t2t_unlock, 0, sizeof(s_nfc_t2t_unlock));
}

/* ---- MFC block-data store: backed by the shared dump buffer bound via
 * nfc_ctx_set_dump (unit_size = 16). Absolute block index (0..255). ---- */
bool nfc_ctx_mfc_block_valid(uint16_t blk)
{
    nfc_dump_meta_t *d = &g_nfc_ctx.dump;
    if (!d->has_dump || d->data == NULL || (uint32_t)blk >= NFC_DUMP_MAX_UNITS) return false;
    if (d->valid_bits == NULL) return true;   /* no bitmap -> treat all as valid */
    return ((d->valid_bits[blk >> 3] >> (blk & 7U)) & 1U) != 0U;
}

const uint8_t * nfc_ctx_mfc_block(uint16_t blk)
{
    if (!nfc_ctx_mfc_block_valid(blk)) return NULL;
    return &g_nfc_ctx.dump.data[(uint32_t)blk * M1NFC_MFC_BLOCK_SZ];
}

void nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16])
{
    nfc_dump_meta_t *d = &g_nfc_ctx.dump;
    if (!d->has_dump || d->data == NULL || (uint32_t)blk >= NFC_DUMP_MAX_UNITS) return;
    memcpy(&d->data[(uint32_t)blk * M1NFC_MFC_BLOCK_SZ], data, 16);
    if (d->valid_bits != NULL) d->valid_bits[blk >> 3] |= (uint8_t)(1U << (blk & 7U));
}

/*============================================================================*/
/**
 * @brief nfc_ctx_set_t2t_page - Set T2T page data
 * 
 * Writes 4-byte page data to dump at specified page index.
 * Updates valid_bits if available and max_seen_unit if needed.
 * 
 * @param[in] page Page number to write
 * @param[in] data Page data (4 bytes)
 * @retval None
 */
/*============================================================================*/
void nfc_ctx_set_t2t_page(uint16_t page, const uint8_t data[4])
{
    nfc_ctx_lock();

    nfc_dump_meta_t *d = &g_nfc_ctx.dump;

    /* Ignore if not Type 2 / NTAG */
    if ((g_nfc_ctx.head.tech   != M1NFC_TECH_A) ||
        (g_nfc_ctx.head.family != M1NFC_FAM_ULTRALIGHT) ||
        !d->has_dump           ||
        d->data      == NULL   ||
        d->unit_size != 4      ||
        d->unit_count == 0)
    {
        nfc_ctx_unlock();
        return;
    }

    uint32_t origin = d->origin;

    /* Ignore if page number is outside dump range */
    if (page < origin) {
        nfc_ctx_unlock();
        return;
    }

    uint32_t idx = (uint32_t)page - origin;
    if (idx >= d->unit_count) {
        nfc_ctx_unlock();
        return;
    }

    /* Actual page data location: data[ idx * unit_size ] */
    uint8_t *dump_buf = d->data;
    uint8_t *page_ptr = &dump_buf[idx * d->unit_size];

    memcpy(page_ptr, data, 4);

    /* If valid_bits exists, mark this page as valid */
    if (d->valid_bits != NULL) {
        uint32_t bitIndex = idx;
        uint32_t byte_idx = (bitIndex >> 3);          // /8
        uint8_t  mask     = (uint8_t)(1u << (bitIndex & 0x07)); // %8

        d->valid_bits[byte_idx] |= mask;
    }

    /* Update max_seen_unit (maximum page actually used in dump) */
    if (idx > d->max_seen_unit) {
        d->max_seen_unit = idx;
    }

    nfc_ctx_unlock();
}


/* --------- Mifare Classic helpers ---------- */
