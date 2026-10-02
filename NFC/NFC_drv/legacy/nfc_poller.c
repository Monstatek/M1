/* See COPYING.txt for license details. */

/*
 ******************************************************************************
 * nfc_poller.c - NFC Poller Mode (Reader/Card Reading)
 ******************************************************************************
 * 
 * [Purpose]
 * - Implements NFC-A reader functionality
 * - Detects and reads various NFC card types (T2T, MIFARE Classic, T4T)
 * - Performs card type identification and data reading
 * 
 * [State Machine]
 * NOTINIT → START_DISCOVERY → DISCOVERY
 * 
 * [Key Flow]
 * 1. ReadIni(): Initialize RFAL in poller mode
 *    - Configure discovery parameters (POLL_TECH_A)
 *    - Start rfalNfcDiscover()
 * 
 * 2. ReadCycle(): Main processing loop
 *    - START_DISCOVERY: Start discovery process
 *    - DISCOVERY: Poll for cards, handle activation
 * 
 * 3. Card Detection and Type Identification:
 *    - ISO14443A/NFC-A card detected
 *    - Analyze ATQA + SAK to determine card type:
 *      * MIFARE Classic (SAK=0x08/0x18): Use key dictionary attack
 *      * Type 2 Tag (T2T): Read NTAG/Ultralight
 *      * Type 4 Tag (T4T): ISO-DEP APDU exchange
 * 
 * 4. T2T Reading (m1_t2t_read_ntag()):
 *    - GET_VERSION (0x60): Get NTAG version info
 *    - READ (0x30): Read pages sequentially (0~135)
 *    - FAST_READ (0x3A): Read multiple pages at once
 *    - Save data to nfc_ctx and SD card (.nfc file)
 * 
 * [Card Type Detection]
 * - MIFARE Classic: ATQA=0x0400/0x4400, SAK=0x08 (1K) or 0x18 (4K)
 * - Type 2 Tag: ATQA=0x0044, SAK=0x00
 * - Type 4 Tag: ISO-DEP protocol
 * 
 ******************************************************************************
 */
#include "nfc_poller.h"
#include "utils.h"
#include "rfal_nfc.h"
#include "rfal_t2t.h"

#include "st25r3916.h"
#include "st25r3916_com.h"
#include "rfal_utils.h"

#include "rfal_AnalogConfig.h"
#include "rfal_rf.h"
#include "rfal_crc.h"        /* rfalCrcCalculateCcitt (CRC_A) for MIFARE Classic */
#include "crypto1.h"         /* MIFARE Classic Crypto1 cipher (Phase A) */
#include "uiView.h"
#include "nfc_driver.h"
#include "nfc_poller.h"
#include "common/nfc_ctx.h"
#include "common/nfc_storage.h"
#include "common/mfc_keys.h"    /* MFC dictionary streaming iterator (Stage C scan) */
#include "common/mfc_key_source_sd.h"  /* production SD adapter for the canonical acquisition core */
#include "common/mfc_result.h"  /* mfc_classify_outcome (nfc_mfc_info_t.outcome) */
#include "common/mfc_dict_phase.h"  /* canonical key-major dictionary-phase orchestration (RF/crypto seam) */
#include "common/mfc_identity.h"  /* mfc_identity_matches() -- Find Missing Keys same-card guard */
#include "common/m1_t2t_emu_image.h"  /* m1_t2t_emu_image_cfg0_page() -- shared CFG0 lookup */
#include "common/ntag_pwd_keys.h"     /* PWD_AUTH password dictionary streaming iterator */
#include "m1_desfire.h"         /* read-only DESFire GetVersion identification */
#include "m1_sdcard.h"
#include "m1_storage.h"
#include "common/nfc_fileio.h"
#include "logger.h"
#include "stm32h5xx_hal.h"           /* HAL_GetTick + DWT/CoreDebug (nested timing) */
#include "mfc_harvest.h"             /* .m1h serializer (Harvester Increment 1) */
#include "mfc_harvest_storage.h"     /* .m1h FatFs/SD binding (Increment 2)     */
#include "mfc_dict_solver.h"         /* nested-dictionary key solver */
#include "mfc_access.h"              /* sector-trailer access-condition decoder (1K write) */
#include "ff.h"                      /* FatFs f_open/f_read for the .m1h read-back  */
#include <stdio.h>
#include <string.h>

#define NOTINIT              0     /*!< Demo State:  Not initialized        */
#define START_DISCOVERY      1     /*!< Demo State:  Start Discovery        */
#define DISCOVERY            2     /*!< Demo State:  Discovery              */

/*
 ******************************************************************************
 * MIFARE CLASSIC Helper Functions
 ******************************************************************************
 */

#define MFC_DICT_PATH   "nfc/system/mf_classic_dict.nfc"   // Adjust to actual path
#define MFC_MAX_DICT_KEYS   2042
#define MFC_KEY_LEN         6
#define MFC_BLOCK_SIZE      16

/* ISO15693 GET MULTIPLE BLOCK SECURITY STATUS (read-only lock query). RFAL's
 * NFC-V command enum stops at 0x2B; 0x2C is issued via rfalNfcvPollerTransceiveReq.
 * Chunked to no more than 32 blocks per query. */
#define NFCV_CMD_GET_MULTI_BLOCK_SEC   0x2CU
#define NFCV_SEC_BLOCKS_PER_QUERY      32U

typedef struct {
    uint8_t keys[MFC_MAX_DICT_KEYS][MFC_KEY_LEN];
    uint16_t count;
} mfc_key_dict_t;

typedef enum {
    MFC_KEYTYPE_A = 0x60,
    MFC_KEYTYPE_B = 0x61,
} mfc_key_type_t;

static bool mfc_load_key_dict(mfc_key_dict_t *dict);

#ifdef MIFARE_CLASSIC_AUTH_TEST
/* Low-level Mifare Classic authentication/read functions should be implemented in a separate file (only prototypes declared here) */
static ReturnCode mfc_authenticate_block(const rfalNfcDevice *dev, uint8_t blockNo, mfc_key_type_t keyType, const uint8_t key[6]);
static ReturnCode mfc_read_block(const rfalNfcDevice *dev, uint8_t blockNo, uint8_t out[MFC_BLOCK_SIZE]);
/* Key dictionary loading, sector/block layout helper functions */
static bool mfc_load_key_dict(mfc_key_dict_t *dict);
static void mfc_get_layout_from_sak(uint8_t sak, uint16_t *outSectors, uint16_t *outBlocks);
static uint16_t mfc_sector_to_first_block(uint16_t sector);
#endif

extern uint8_t g_nfc_dump_buf[NFC_DUMP_BUF_SIZE];
extern uint8_t g_nfc_valid_bits[NFC_VALID_BITS_SIZE];

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */

static rfalNfcDiscoverParam discParam;
static uint8_t              state = NOTINIT;
static bool                 multiSel;


/* NFC-A CE config */
/* 4-byte UIDs with first byte 0x08 would need random number for the subsequent 3 bytes.
 * 4-byte UIDs with first byte 0x*F are Fixed number, not unique, use for this demo
 * 7-byte UIDs need a manufacturer ID and need to assure uniqueness of the rest.*/

/*------------------------------------------------------------------------------------------*/
static void PollerNotif( rfalNfcState st );
static void m1_t2t_read_ntag(const rfalNfcDevice *dev);
static void m1_nfcv_read(const rfalNfcDevice *dev);
static void m1_st25tb_read(const rfalNfcDevice *dev);
static void m1_mfc_read_card(const rfalNfcDevice *dev);
static bool m1_mfc_acquire_dict_phase(const rfalNfcDevice *dev, uint32_t cuid,
                                      const mfc_key_source_cfg_t *sources, size_t n_sources,
                                      const uint8_t already_tried[MFC_KEY_SIZE],
                                      nfc_mfc_info_t *mfc, nfc_mfc_scan_t *sc,
                                      const volatile bool *abort_flag,
                                      mfc_dict_resume_t *resume,
                                      volatile bool *skip_source_flag);
/* m1_mfc_build_key_sources() itself is declared in nfc_poller.h (public: the
 * MIFARE Classic Keys UI needs the exact same [USER, SYSTEM] source
 * definitions and path precedence the acquisition session uses, to report
 * an honest system-dictionary count without a second implementation). */

/* --- MIFARE Classic nested-nonce harvester (CLI-armed, NFC-task-serviced) --- */
#define HARVEST_MAX_SAMPLES  8u
typedef struct {
    volatile uint8_t pending;      /* 1 = armed (set by CLI, cleared by NFC task) */
    uint8_t  src_block, src_keytype;
    uint8_t  tgt_block, tgt_keytype;
    uint8_t  samples;
    uint64_t known_key;
} harvest_req_t;
static harvest_req_t         g_harvest_req = {0};
/* static (not on the NFC task stack): FIL+path ~0.6 KiB, serializer ctx ~0.55 KiB */
static mfc_harvest_storage_t g_harvest_st;
static mfc_harvest_t         g_harvest_h;
static void m1_mfc_harvest_run(const rfalNfcDevice *dev);
static ReturnCode GetVersion_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen);
static uint16_t LogParsedNtagVersion(const uint8_t *version, uint16_t len);
static uint8_t  t2t_variant_from_version(const uint8_t *version, uint16_t len, uint16_t *outPages);
static uint8_t  t2t_probe_legacy_variant(uint16_t *outPages);

/*------------------------------------------------------------------------------------------*/
#define SET_FAMILY(fmt, ...)  do { snprintf(NFC_Family, sizeof(NFC_Family), fmt, ##__VA_ARGS__); } while(0)

// (Optional) Advanced detection is disabled. Change to 1 if needed for implementation.
#define ENABLE_DESFIRE_PROBE   0
#define ENABLE_MAGIC_PROBE     0

// (Optional) DESFire detection (ISO-DEP APDU GET_VERSION)
// Currently false as implementation is not ready. Connect actual exchange function here if available.
static bool desfire_probe_iso_dep(const rfalNfcDevice* dev) {
#if ENABLE_DESFIRE_PROBE
    // Connect APDU exchange routine like demoAPDU_Exchange(...)
    return false;
#else
    return false;
#endif
}

// (Optional) Magic(Gen1A) detection (backdoor 0x40 0x43)
// Currently false as implementation is not ready.
static bool mfclassic_is_magic_backdoor_supported(void) {
#if ENABLE_MAGIC_PROBE
    return false;
#else
    return false;
#endif
}
/*============================================================================*/
/**
 * @brief nfc_a_fill_uid_and_family - Fill NFC-A UID and Family (SAK/ATQA priority → type as auxiliary)
 * 
 * Fills UID and Family information for NFC-A devices based on SAK/ATQA values,
 * with device type as auxiliary information.
 * 
 * @param[in] dev Pointer to rfalNfcDevice (type=RFAL_NFC_LISTEN_TYPE_NFCA)
 * @retval None
 */
/*============================================================================*/
static void nfc_a_fill_uid_and_family(const rfalNfcDevice *dev)
{
    const rfalNfcaListenDevice* a = &dev->dev.nfca;

    /* Fill UID */
    NFC_ID_LEN = dev->nfcidLen;
    memset(NFC_ID, 0, sizeof(NFC_ID));
    ST_MEMCPY(NFC_ID, dev->nfcid, NFC_ID_LEN);
    strcpy(NFC_UID, hex2Str(dev->nfcid, dev->nfcidLen));

    /* Extract ATQA/SAK */
    const uint8_t sak   = a->selRes.sak;
    const uint8_t atqa0 = a->sensRes.anticollisionInfo;  /* ATQA LSB */
    const uint8_t atqa1 = a->sensRes.platformInfo;       /* ATQA MSB */
    const rfalNfcaListenDeviceType t = a->type;

    NFC_ATQA[0] = (char)atqa0;
    NFC_ATQA[1] = (char)atqa1;
    NFC_SAK = (char)sak;

    platformLog("[NFCA] type=%d ATQA=%02X%02X SAK=%02X\r\n", t, atqa0, atqa1, sak);

    /* 1) Classic has highest priority: Force classification by SAK regardless of RFAL type */
    if (sak == 0x08) { /* 1K */
        if (mfclassic_is_magic_backdoor_supported())
            SET_FAMILY("Magic MIFARE Classic 1K");
        else
            SET_FAMILY("MIFARE Classic 1K");
        return;
    }
    if (sak == 0x18) { /* 4K */
        if (mfclassic_is_magic_backdoor_supported())
            SET_FAMILY("Magic MIFARE Classic 4K");
        else
            SET_FAMILY("MIFARE Classic 4K");
        return;
    }
    if (sak == 0x09) { /* Mini */
        SET_FAMILY("MIFARE Mini 0.3K");
        return;
    }

    /* 2) Type 4A / DESFire */
    if (sak == 0x20 || t == RFAL_NFCA_T4T || t == RFAL_NFCA_T4T_NFCDEP) {
        if (desfire_probe_iso_dep(dev)) SET_FAMILY("MIFARE DESFire (Type 4A)");
        else                            SET_FAMILY("Type 4A (ISO-DEP)");
        return;
    }

    /* 3) Ultralight/NTAG (typical ATQA 0x44 0x00 or T2T) */
    if ( (sak == 0x00 && atqa0 == 0x44) || t == RFAL_NFCA_T2T ) {
        SET_FAMILY("Ultralight/NTAG");
        return;
    }

    /* 4) Topaz (rare but if type is certain) */
    if (t == RFAL_NFCA_T1T) {
        SET_FAMILY("Topaz (Type 1)");
        return;
    }

    /* 5) Others */
    SET_FAMILY("NFC-A (unspecified)");
}


/*============================================================================*/
/**
 * @brief ReadCycle - Main NFC poller processing loop
 * 
 * Main processing loop for NFC poller mode. Handles state machine transitions
 * and card detection/reading operations. Processes discovered cards and
 * identifies their types (MIFARE Classic, T2T, T4T, etc.).
 * 
 * State machine:
 * - START_DISCOVERY: Starts discovery process
 * - DISCOVERY: Polls for cards and handles activation
 * 
 * @retval None
 */
/*============================================================================*/
/*------------------------------------------------------------------------------------------*/
/*============================================================================*/
/* NTAG21x live-tag Write (dump-based clone/restore of user pages only)       */
/*                                                                            */
/* Runs in the NFC worker task (poller mode). The source is the immutable     */
/* page dump already in nfc_ctx from a live read; only model-specific user    */
/* pages are written, then each is read back and verified. No lock/OTP/CC/    */
/* config/PWD/PACK/UID page is ever touched. Status is reported via the       */
/* shared nfc_wr_status_t. Only a 16-byte read-back buffer is used.           */
/*============================================================================*/
static volatile bool s_ntag_write_mode  = false;
static volatile bool s_ntag_write_abort = false;

bool nfc_poller_write_active(void) { return s_ntag_write_mode; }
void nfc_poller_write_begin(void)  { s_ntag_write_mode = true;  s_ntag_write_abort = false; }
void nfc_poller_write_end(void)    { s_ntag_write_mode = false; }
void nfc_poller_write_abort(void)  { s_ntag_write_abort = true; }

/* MIFARE Classic dictionary scan (Stage C): one-shot, worker-run, abortable. */
static volatile bool s_mfc_scan_mode  = false;
static volatile bool s_mfc_scan_abort = false;
/* Skip-current-source-only control (CENTER "Skip"), shared
 * the same way s_mfc_scan_abort already is -- one flag, one worker task,
 * only one of normal Read's dictionary phase / Dictionary Scan / Find
 * Missing Keys is ever actually running at a time. Distinct from
 * s_mfc_scan_abort: this only abandons the REMAINING candidates of
 * whichever source is currently active (see mfc_key_source_iter_skip_source()),
 * never the whole operation. */
static volatile bool s_mfc_scan_skip_source = false;
bool nfc_poller_mfc_scan_active(void) { return s_mfc_scan_mode; }
void nfc_poller_mfc_scan_begin(void)  { s_mfc_scan_mode = true;  s_mfc_scan_abort = false; s_mfc_scan_skip_source = false; }
void nfc_poller_mfc_scan_end(void)    { s_mfc_scan_mode = false; }
void nfc_poller_mfc_scan_abort(void)  { s_mfc_scan_abort = true; }
void nfc_poller_mfc_scan_skip_source(void) { s_mfc_scan_skip_source = true; }

/* Find Missing Keys: one-shot, worker-run, abortable dictionary-phase
 * CONTINUATION of a partial MFC read -- shares s_mfc_scan_abort/
 * nfc_poller_mfc_scan_abort() (and now s_mfc_scan_skip_source/
 * nfc_poller_mfc_scan_skip_source()) with the two ops above (the worker is
 * single-threaded, so only one of the three is ever actually running). */
static volatile bool s_mfc_find_keys_mode = false;
bool nfc_poller_mfc_find_keys_active(void) { return s_mfc_find_keys_mode; }
void nfc_poller_mfc_find_keys_begin(void)  { s_mfc_find_keys_mode = true;  s_mfc_scan_abort = false; s_mfc_scan_skip_source = false; }
void nfc_poller_mfc_find_keys_end(void)    { s_mfc_find_keys_mode = false; }

/* MIFARE Classic 1K write (clone/restore): one-shot, worker-run, abortable. */
static volatile bool s_mfc_write_mode  = false;
static volatile bool s_mfc_write_abort = false;
bool nfc_poller_mfc_write_active(void) { return s_mfc_write_mode; }
void nfc_poller_mfc_write_begin(void)  { s_mfc_write_mode = true;  s_mfc_write_abort = false; }
void nfc_poller_mfc_write_end(void)    { s_mfc_write_mode = false; }
void nfc_poller_mfc_write_abort(void)  { s_mfc_write_abort = true; }

/* NTAG/Ultralight Unlock (genuine PWD_AUTH): one-shot, worker-run,
 * abortable. Mode + single-password payload are set by the UI BEFORE
 * posting the begin event, mirroring nfc_harvest_set_target()'s pattern.
 * s_unlock_pwd is cleared immediately after nfc_unlock_run() consumes it
 * (single-attempt mode) so a stale candidate password never lingers in
 * RAM once no longer needed. */
static volatile bool s_unlock_mode  = false;
static volatile bool s_unlock_abort = false;
static bool          s_unlock_use_dictionary = false;
static uint8_t        s_unlock_pwd[4] = {0};

bool nfc_poller_unlock_active(void) { return s_unlock_mode; }
void nfc_poller_unlock_begin(void)  { s_unlock_mode = true;  s_unlock_abort = false; }
void nfc_poller_unlock_end(void)    { s_unlock_mode = false; }
void nfc_poller_unlock_abort(void)  { s_unlock_abort = true; }

void nfc_unlock_set_single_password(const uint8_t pwd[4])
{
    s_unlock_use_dictionary = false;
    if (pwd != NULL) { memcpy(s_unlock_pwd, pwd, 4U); }
}

void nfc_unlock_set_dictionary_mode(void)
{
    s_unlock_use_dictionary = true;
    memset(s_unlock_pwd, 0, sizeof(s_unlock_pwd));
}

/* Live nested-nonce harvest (UI-driven, worker-run, abortable). */
static volatile bool s_harvest_scan_mode  = false;
static volatile bool s_harvest_scan_abort = false;
bool nfc_poller_harvest_active(void) { return s_harvest_scan_mode; }
void nfc_poller_harvest_begin(void)  { s_harvest_scan_mode = true;  s_harvest_scan_abort = false; }
void nfc_poller_harvest_end(void)    { s_harvest_scan_mode = false; }
void nfc_poller_harvest_abort(void)  { s_harvest_scan_abort = true; }

void nfc_harvest_set_target(uint8_t tgt_sector, uint8_t samples)
{
    g_harvest_req.src_block   = 0U;      /* source sector 0, Key A                */
    g_harvest_req.src_keytype = 0x60U;
    g_harvest_req.tgt_block   = (uint8_t)(tgt_sector * 4U);
    g_harvest_req.tgt_keytype = 0x60U;
    g_harvest_req.samples     = (samples == 0U) ? 1U :
                               ((samples > HARVEST_MAX_SAMPLES) ? HARVEST_MAX_SAMPLES : samples);
    g_harvest_req.known_key   = 0xFFFFFFFFFFFFULL;
}

/* MFC Recovery solve (nested-dictionary): worker-run, abortable. */
static volatile bool s_solve_mode  = false;
static volatile bool s_solve_abort = false;
bool nfc_poller_solve_active(void) { return s_solve_mode; }
void nfc_poller_solve_begin(void)  { s_solve_mode = true;  s_solve_abort = false; }
void nfc_poller_solve_end(void)    { s_solve_mode = false; }
void nfc_poller_solve_abort(void)  { s_solve_abort = true; }

/* Model-specific writable user-page bounds (inclusive). false for unsupported
 * models. Conservative M1 policy: user pages only -- never UID/manufacturer,
 * static/dynamic lock, OTP/CC, config, counter, PWD/PACK, or key pages. The
 * NTAG213/215/216 rows are the production-validated baseline and are unchanged. */
static bool ntag21x_user_bounds(uint8_t variant, uint16_t *first, uint16_t *last)
{
    switch (variant) {
        case M1NFC_T2TVAR_UL:      *first = 4U; *last = 15U;  return true;  /* Original UL: excl 0-3 */
        case M1NFC_T2TVAR_NTAG203: *first = 4U; *last = 39U;  return true;  /* excl 0-3, 40 dynlock, 41 counter */
        case M1NFC_T2TVAR_ULC:     *first = 4U; *last = 39U;  return true;  /* excl 0-3, 40-47 lock/cnt/auth/key */
        case M1NFC_T2TVAR_UL11:    *first = 4U; *last = 15U;  return true;  /* excl 0-3, 16-19 CFG0/1/PWD/PACK */
        case M1NFC_T2TVAR_UL21:    *first = 4U; *last = 35U;  return true;  /* excl 0-3, 36 dynlock, 37-40 cfg */
        case M1NFC_T2TVAR_NTAG213: *first = 4U; *last = 39U;  return true;
        case M1NFC_T2TVAR_NTAG215: *first = 4U; *last = 129U; return true;
        case M1NFC_T2TVAR_NTAG216: *first = 4U; *last = 225U; return true;
        default: return false;
    }
}

void m1_ntag21x_write_run(void)
{
    nfc_wr_status_t *st  = nfc_ctx_get_wr_status();
    rfalNfcDevice   *dev = NULL;
    uint16_t         first = st->first_page;
    uint16_t         last  = st->last_page;
    ReturnCode       err;

    st->state = NFC_WR_WAIT_TARGET;
    st->err   = NFC_WR_ERR_NONE;
    st->cur_page = 0; st->done_count = 0; st->any_written = false; st->fail_page = 0;

    platformLog("[WR] start src_var=%u pages=%u..%u\r\n",
                (unsigned)st->src_variant, (unsigned)first, (unsigned)last);

    /* 1) Poll and activate the target. Start discovery ONCE, then let the RFAL
     *    worker run to completion (mirrors ReadCycle START_DISCOVERY->DISCOVERY).
     *    Re-issuing Deactivate/Discover every tick would abort activation before
     *    it can finish, so the tag would never activate. */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) {
            rfalNfcGetActiveDevice(&dev);
            break;
        }
        if (s_ntag_write_abort) {                 /* cancel before any write -> silent */
            platformLog("[WR] abort while waiting for target\r\n");
            st->state = NFC_WR_IDLE;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }
    platformLog("[WR] target activated dev.type=%d nfca.type=%d\r\n",
                (int)(dev ? (int)dev->type : -1),
                (int)((dev && dev->type==RFAL_NFC_LISTEN_TYPE_NFCA) ? (int)dev->dev.nfca.type : -1));

    /* 2) Require NFC-A Type 2. */
    if (dev == NULL || dev->type != RFAL_NFC_LISTEN_TYPE_NFCA ||
        dev->dev.nfca.type != RFAL_NFCA_T2T) {
        platformLog("[WR] FAIL not NFC-A T2T\r\n");
        st->err = NFC_WR_ERR_UNSUPPORTED; st->state = NFC_WR_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return;
    }

    /* 3) Resolve the target's EXACT model, then require an exact source match.
     *    This MIRRORS the live read path's detection sequence (m1_t2t_read_ntag):
     *      - GET_VERSION, up to 2 attempts 10ms apart, rejecting an all-zero reply;
     *      - if a usable tuple is returned, map it via t2t_variant_from_version
     *        (EV1 MF0UL11/21 and NTAG213/215/216); an unrecognized-but-complete
     *        tuple falls through to the same conservative probe as read;
     *      - otherwise (original UL / Ultralight C / NTAG203 answer no version)
     *        fall back to the SAME legacy chain t2t_probe_legacy_variant()
     *        (Ultralight C 0x1A -> NTAG203 page-41 read -> original UL).
     *    Like the read path, NO reselect/reset is issued between GET_VERSION and
     *    the probe, so the target-detection transaction is identical to read. */
    uint8_t  version[8] = {0};
    uint16_t rcv        = 0U;
    bool     ver_ok     = false;
    for (uint8_t attempt = 0U; (attempt < 2U) && !ver_ok; attempt++) {
        if (attempt > 0U) osDelay(10);
        memset(version, 0x00, sizeof(version));
        rcv = 0U;
        err = GetVersion_Ntag(version, sizeof(version), &rcv);
        bool all_zero = true;
        for (uint8_t i = 0U; i < sizeof(version); i++) {
            if (version[i] != 0x00U) { all_zero = false; break; }
        }
        if ((err == RFAL_ERR_NONE) && (rcv == sizeof(version)) && !all_zero) ver_ok = true;
    }

    uint16_t tpages = 0U;
    uint8_t  tvar   = M1NFC_T2TVAR_UNKNOWN;
    if (ver_ok) {
        tvar = t2t_variant_from_version(version, rcv, &tpages);
        if ((tvar == M1NFC_T2TVAR_UNKNOWN) || (tpages == 0U)) {
            tvar = t2t_probe_legacy_variant(&tpages);   /* complete but unknown tuple */
        }
    } else {
        tvar = t2t_probe_legacy_variant(&tpages);       /* legacy: no GET_VERSION   */
    }
    platformLog("[WR] target var=%u ver_ok=%d (src var=%u)\r\n",
                (unsigned)tvar, (int)ver_ok, (unsigned)st->src_variant);

    uint16_t tf, tl;
    if (!ntag21x_user_bounds(tvar, &tf, &tl)) {   /* not one of the supported T2T models */
        st->err = NFC_WR_ERR_UNSUPPORTED; st->state = NFC_WR_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return;
    }
    if (tvar != st->src_variant) {   /* exact match only: no cross-model shortcuts */
        st->err = NFC_WR_ERR_WRONG_MODEL; st->state = NFC_WR_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return;
    }

    /* 4) Write + verify each allowed user page from the immutable source dump. */
    st->state = NFC_WR_WRITING;
    for (uint16_t pg = first; pg <= last; pg++) {
        st->cur_page = pg;
        if (s_ntag_write_abort) {                 /* cancel: partial if pages changed, else clean */
            platformLog("[WR] abort at page %u any_written=%d\r\n", (unsigned)pg, (int)st->any_written);
            if (st->any_written) { st->err = NFC_WR_ERR_ABORTED; st->fail_page = pg; st->state = NFC_WR_FAIL; }
            else                 { st->state = NFC_WR_IDLE; }
            break;
        }

        uint8_t src[4];
        if (!nfc_ctx_get_t2t_page(pg, src)) {
            platformLog("[WR] FAIL incomplete src p%u\r\n", (unsigned)pg);
            st->err = NFC_WR_ERR_INCOMPLETE_SRC; st->fail_page = pg; st->state = NFC_WR_FAIL; break;
        }

        ReturnCode we = rfalT2TPollerWrite((uint8_t)pg, src);
        if ((pg == first) || (we != RFAL_ERR_NONE))
            platformLog("[WR] W p%u err=%d d=%02X%02X%02X%02X\r\n",
                        (unsigned)pg, (int)we, src[0],src[1],src[2],src[3]);
        if (we != RFAL_ERR_NONE) {
            st->fail_page = pg; st->state = NFC_WR_FAIL;
            st->err = (we == RFAL_ERR_LINK_LOSS) ? NFC_WR_ERR_TAG_REMOVED : NFC_WR_ERR_WRITE;
            break;
        }
        st->any_written = true;

        /* read the page back (T2T READ returns 16 bytes = 4 pages) and compare 4 */
        uint8_t  rb[16] = {0};
        uint16_t rlen   = 0U;
        ReturnCode re = rfalT2TPollerRead((uint8_t)pg, rb, sizeof(rb), &rlen);
        if ((pg == first) || (re != RFAL_ERR_NONE))
            platformLog("[WR] R p%u err=%d rlen=%u rb=%02X%02X%02X%02X\r\n",
                        (unsigned)pg, (int)re, (unsigned)rlen, rb[0],rb[1],rb[2],rb[3]);
        if ((re != RFAL_ERR_NONE) || (rlen < 4U)) {
            st->fail_page = pg; st->state = NFC_WR_FAIL;
            st->err = (re == RFAL_ERR_LINK_LOSS) ? NFC_WR_ERR_TAG_REMOVED : NFC_WR_ERR_VERIFY;
            break;
        }
        if (memcmp(rb, src, 4) != 0) {
            platformLog("[WR] VERIFY mismatch p%u\r\n", (unsigned)pg);
            st->fail_page = pg; st->state = NFC_WR_FAIL; st->err = NFC_WR_ERR_VERIFY; break;
        }
        st->done_count++;
    }

    if (st->state == NFC_WR_WRITING) st->state = NFC_WR_DONE;
    platformLog("[WR] end state=%u err=%u done=%u/%u fail_page=%u\r\n",
                (unsigned)st->state, (unsigned)st->err, (unsigned)st->done_count,
                (unsigned)st->total_count, (unsigned)st->fail_page);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
}

void ReadCycle(void)
{
    static rfalNfcDevice *nfcDevice;

    rfalNfcWorker();                                    /* Run RFAL worker periodically */

    switch (state)
    {
/********************************** State : DISCOVERY *************************************/
    case START_DISCOVERY:
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        rfalNfcDiscover(&discParam);

        multiSel = false;
        state    = DISCOVERY;
        //platformLog("Poller START_DISCOVERY\r\n");
        break;  //↓ 
/************************************** State : DISCOVERY *****************************************/
    case DISCOVERY:
        //rfalNfcDevice *list = NULL; uint8_t cnt = 0;
        //rfalNfcGetDevicesFound(&list, &cnt);
        //if (cnt > 0) platformLog("Found cnt=%u\r\n", cnt);
        if (rfalNfcIsDevActivated(rfalNfcGetState()))
        {
            //platformLog("Poller rfalNfcGetState\r\n");
        
            m1_wdt_reset();

            bool notifyRead = false;

            rfalNfcGetActiveDevice(&nfcDevice); /* Get active device */

            uint8_t ctx_err = FillNfcContextFromDevice(nfcDevice);
            if (ctx_err != 0) {
                platformLog("FillNfcContextFromDevice err=%d (type=%d)\r\n",
                            (int)ctx_err, nfcDevice ? (int)nfcDevice->type : -1);
                /* If needed, can retry by setting state=START_DISCOVERY here */
            }else platformLog("Fill NFC Context Successed\r\n");

            switch (nfcDevice->type)
            {
                case RFAL_NFC_LISTEN_TYPE_NFCA:   /* ISO14443A / NFC-A card (MFC, UL/NTAG, DESFire, Magic) */
                {
                    isNFCCardFound = true;
                    nfc_tx_type    = NFC_TX_A;

                    // For logging: use dev->nfcid
                    platformLog("ISO14443A/NFC-A TAG found. type=0x%02X, UID=%s\r\n", nfcDevice->dev.nfca.type, hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ISO14443A/NFC-A");

                    // ATQA/SAK (access via RFAL structure fields)
                    const uint8_t sak   = nfcDevice->dev.nfca.selRes.sak;
                    const uint8_t atqa0 = nfcDevice->dev.nfca.sensRes.anticollisionInfo;
                    const uint8_t atqa1 = nfcDevice->dev.nfca.sensRes.platformInfo;

                    /* --- MIFARE Classic identification by SAK (the definitive
                     * discriminator: 0x08=1K, 0x18=4K). ATQA varies by product
                     * (e.g. 4K is often 0x0002, not 0x0004) and must NOT gate --
                     * a strict ATQA check made 4K fall through to the Type-2 read
                     * and show up as Ultralight. Matches the SAK-priority rule in
                     * nfc_classify_family_from_nfca / nfc_a_fill_uid_and_family. --- */
                    bool isMfcClassic = false;
                    bool isMfc1K      = (sak == 0x08U);   /* SAK = 0x08 → Classic 1K */
                    bool isMfc4K      = (sak == 0x18U);   /* SAK = 0x18 → Classic 4K */

                    if (isMfc1K || isMfc4K) {
                        isMfcClassic = true;

                        const char *memSizeStr = isMfc1K ? "1K" : "4K";
                        const char *uidTypeStr = (nfcDevice->nfcidLen == 7U) ? "7-byte UID" : "4-byte UID";

                        platformLog("MIFARE Classic %s detected (%s). ATQA=%02X%02X, SAK=%02X, UIDLen=%u\r\n",
                                    memSizeStr,
                                    uidTypeStr,
                                    atqa0, atqa1, sak,
                                    nfcDevice->nfcidLen);
                        // If needed, Family string can be overwritten here
                        SET_FAMILY("MIFARE Classic %s (%s)", memSizeStr, uidTypeStr);

                        /* Full-card read (1K + 4K): configured key FFFFFFFFFFFF
                         * tried as Key A and Key B on every sector. No key
                         * scanning / dictionary here (that is the Tools scan). */
                        nfc_ctx_clear_mfc();
                        if (g_harvest_req.pending) {
                            /* CLI-armed nested-nonce harvest takes priority over
                             * the normal read for this activation (one-shot). */
                            m1_mfc_harvest_run(nfcDevice);
                        } else {
                            m1_mfc_read_card(nfcDevice);
                        }
                    }

                    /* Store in emulation context (UID/ATQA/SAK) */
                    Emu_SetNfcA(nfcDevice->nfcid, nfcDevice->nfcidLen, atqa0, atqa1, sak);
                    //platformLog("Emu_SetNfcA nfcDevice->nfcidLen=%u\r\n", nfcDevice->nfcidLen);

                    // === Auto-set Family/UID here ===
                    nfc_a_fill_uid_and_family(nfcDevice);

                    /* Additional reading for T1/T2/T4/DEP only if not MIFARE Classic */
                    if (!isMfcClassic) {
                        if (nfcDevice->dev.nfca.type == RFAL_NFCA_T1T)
                            platformLog("NFC Type 1 Tag Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T2T) {
                            platformLog("NFC Type 2 Tag Read More\r\n");
                            m1_t2t_read_ntag(nfcDevice); // Improve to t2t
                        }
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T4T ||
                                 nfcDevice->dev.nfca.type == RFAL_NFCA_T4T_NFCDEP) {
                            platformLog("NFC Type 4 Tag Read More\r\n");
                            /* Read-only DESFire identification (native GetVersion
                             * over ISO-DEP). Non-fatal: keeps the generic Type 4A
                             * label if the card is not an ISO-DEP DESFire. */
                            m1_desfire_read(nfcDevice);
                        }
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_NFCDEP)
                            platformLog("NFC DEP Read More\r\n");
                    }


                    notifyRead = true;
                }

                break;
                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCB:   /* ISO14443B / NFC-B */
                    platformLog("ISO14443B/NFC-B TAG found. UID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ISO14443B/NFC-B");
                    nfc_tx_type = NFC_TX_B;
                    notifyRead  = true;
                    break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCF:   /* FeliCa / NFC-F */
                    isNFCCardFound = true;
                    platformLog("FeliCa/NFC-F TAG found. NFCID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "Felica/NFC-F");
                    nfc_tx_type = NFC_TX_F;
                    notifyRead  = true;
                    break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCV:   /* ISO15693 / NFC-V */
                {
                    isNFCCardFound = true;
                    uint8_t devUID[RFAL_NFCV_UID_LEN];
                    ST_MEMCPY(devUID, nfcDevice->nfcid, nfcDevice->nfcidLen);
                    REVERSE_BYTES(devUID, RFAL_NFCV_UID_LEN);  /* Reverse for display */
                    platformLog("ISO15693/NFC-V TAG found. UID=%s\r\n",
                                hex2Str(devUID, RFAL_NFCV_UID_LEN));
                    strcpy(NFC_Type, "ISO15693/NFC-V");
                    nfc_tx_type = NFC_TX_V;

                    /* Inventory + GET SYSTEM INFORMATION + single-block reads. */
                    m1_nfcv_read(nfcDevice);

                    notifyRead  = true;
                }
                break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_ST25TB:
                    isNFCCardFound = true;
                    platformLog("ST25TB TAG found. UID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ST25TB");

                    /* Variant detect + data-block dump + system block read. */
                    m1_st25tb_read(nfcDevice);

                    notifyRead = true;
                    break;

                /*******************************************************************************/
                /* CE/P2P types are ignored in READ-ONLY mode */
                case RFAL_NFC_LISTEN_TYPE_AP2P:
                case RFAL_NFC_POLL_TYPE_AP2P:
                case RFAL_NFC_POLL_TYPE_NFCA:
                case RFAL_NFC_POLL_TYPE_NFCF:
                    platformLog("Non-reader mode type detected (ignored in READ-ONLY): type=%d\r\n",
                                nfcDevice->type);
                    notifyRead = false;
                    break;

                /*******************************************************************************/
                default:
                    platformLog("Unknown type=%d\r\n", nfcDevice->type);
                    notifyRead = false;
                    break;
            } /* switch(nfcDevice->type) */

            /* Common: Update UID buffer and length */
            strcpy(NFC_UID, hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
            NFC_ID_LEN = nfcDevice->nfcidLen;

            for (int i = 0; i < 20; ++i) {
                NFC_ID[i] = 0;
            }
            for (int i = 0; i < nfcDevice->nfcidLen; ++i) {
                NFC_ID[i] = nfcDevice->nfcid[i];
            }

            /* Notify read completion */
            if (notifyRead) {
                m1_wdt_reset();

                m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_READ_COMPLETE);
            }

            /* Always deactivate to idle and return to re-discovery loop */
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);

            state = START_DISCOVERY;
        }
        else
        {
            /* Nothing found. Continue polling in next cycle */
            //platformLog("rfalNfcIsDevActivated false\r\n\r\n");
        }

        m1_wdt_reset();

        break; /* DEMO_ST_DISCOVERY */

/************************************** State : NOTINIT *****************************************/
    case NOTINIT:
        //platformLog("NOTINIT\r\n");
        /* fallthrough */
    default:
        //platformLog("default\r\n");
        break;
    } /* switch(state) */
}


/*============================================================================*/
/**
 * @brief ReadIni - Initialize NFC poller (READ-ONLY mode)
 *
 * Poller-only initialization. CE/P2P disabled.
 * Validates RFAL/DiscParam settings and enters START_DISCOVERY state.
 *
 * @retval true  Initialization successful
 * @retval false Initialization failed
 */
/*============================================================================*/
bool ReadIni(void)
{
    ReturnCode err = RFAL_ERR_NONE;

    /* 1) RFAL Initialize (retry 2 times) */
    for (int i = 0; i < 2; i++) {
        err = rfalNfcInitialize();
        //platformLog("rfalNfcInitialize() = %d\r\n", err);
        if (err == RFAL_ERR_NONE) break;
        vTaskDelay(5);
    }
    if (err != RFAL_ERR_NONE) return false;

    /* 2) Discovery parameters: Explicitly set for Poller-only mode */
    rfalNfcDefaultDiscParams(&discParam);

    discParam.devLimit       = 1U;
    discParam.totalDuration  = 20U;                        /* Phase 0: ~20 ms re-poll cadence (Watlogic parity, 1000->20) */
    discParam.notifyCb       = PollerNotif;                  /* Keep if in use */
#if defined(RFAL_COMPLIANCE_MODE_NFC)
    discParam.compMode       = RFAL_COMPLIANCE_MODE_NFC;   /* Recommended for application */
#endif

    /* CE/P2P disabled: Enable only Poller in techs2Find */
    discParam.techs2Find     = RFAL_NFC_TECH_NONE;

#if RFAL_FEATURE_NFCA
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_A;
#endif
#if RFAL_FEATURE_NFCB
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_B;
#endif
#if RFAL_FEATURE_NFCF
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_F;
#endif
#if RFAL_FEATURE_NFCV
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_V;
#endif
#if RFAL_FEATURE_ST25TB
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_ST25TB;
#endif

    /* Never enabled (READ ONLY) */
    /* discParam.techs2Find |= RFAL_NFC_POLL_TECH_AP2P;        */
    /* discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_AP2P;      */
    /* discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_A/F;       */

#if ST25R95
    discParam.isoDepFS       = RFAL_ISODEP_FSXI_128; /* ST25R95 Limit */
#endif

    /* 3) Verify configuration is valid by calling Discover once, then deactivate to Idle */
    err = rfalNfcDiscover(&discParam);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    if (err != RFAL_ERR_NONE) {
        platformLog("rfalNfcDiscover() check failed: %d\r\n", err);
        return false;
    }

    /* 4) Enter state machine starting point */
    state = START_DISCOVERY;

    //platformLog("ReadIni() OK, state=%d\r\n", state);
    return true;
}


/*============================================================================*/
/**
 * @brief PollerNotif - Poller notification callback
 * 
 * Handles RFAL NFC state change notifications for poller mode.
 * Manages wake-up mode, device detection, multiple device selection,
 * and discovery state transitions.
 * 
 * @param[in] st RFAL NFC state
 * @retval None
 */
/*============================================================================*/
static void PollerNotif( rfalNfcState st )
{
    uint8_t       devCnt;
    rfalNfcDevice *dev;

    if( st == RFAL_NFC_STATE_WAKEUP_MODE )
    {
        platformLog("Wake Up mode started \r\n");
    }
    else if( st == RFAL_NFC_STATE_POLL_TECHDETECT )
    {
        if( discParam.wakeupEnabled )
        {
            platformLog("Wake Up mode terminated. Polling for devices \r\n");
        }
    }
    else if( st == RFAL_NFC_STATE_POLL_SELECT )
    {
        /* Check if in case of multiple devices, selection is already attempted */
        if( (!multiSel) )
        {
            multiSel = true;
            /* Multiple devices were found, activate first of them */
            rfalNfcGetDevicesFound( &dev, &devCnt );
            rfalNfcSelect( 0 );

            platformLog("Multiple Tags detected: %d \r\n", devCnt);
        }
        else
        {
            rfalNfcDeactivate( RFAL_NFC_DEACTIVATE_DISCOVERY );
        }
    }
    else if( st == RFAL_NFC_STATE_START_DISCOVERY )
    {
        /* Clear mutiple device selection flag */
        multiSel = false;
    }
}


/*============================================================================*/
/**
 * @brief GetVersion_Ntag - Send GET_VERSION command to NTAG
 * 
 * Sends NTAG GET_VERSION command (0x60) and receives 8-byte version response.
 * This command retrieves version information including vendor, product,
 * size, and protocol information from NTAG/Ultralight-C tags.
 * 
 * @param[out] rxBuf Buffer to store received version data
 * @param[in] rxBufLen Size of receive buffer (must be at least 8)
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE Success
 * @retval RFAL_ERR_PARAM Invalid parameters
 * @retval Other RFAL error codes
 */
/*============================================================================*/
static ReturnCode GetVersion_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd = 0x60; /* NTAG GET_VERSION command*/

    if ((rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 8U)) {
        return RFAL_ERR_PARAM;
    }

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(&cmd, 1U, rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}


/*============================================================================*/
/**
 * @brief ReadSignature_Ntag - Send READ_SIGNATURE (0x3C) to NTAG/UL11
 *
 * Captures the tag's genuine 32-byte ECC originality signature. This is
 * tag-unique authenticity data that must never be fabricated -- absent a
 * real captured value, the saved-card image is later refused for emulation
 * rather than answering READ_SIGNATURE with placeholder bytes.
 *
 * @param[out] rxBuf Buffer to store received signature (>= 32 bytes)
 * @param[in]  rxBufLen Size of receive buffer
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE on success (rcvLen == 32)
 */
/*============================================================================*/
static ReturnCode ReadSignature_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd[2] = { 0x3C, 0x00 }; /* READ_SIGNATURE, RFU subcommand byte */

    if ((rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 32U)) {
        return RFAL_ERR_PARAM;
    }

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(cmd, sizeof(cmd), rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}


/*============================================================================*/
/**
 * @brief ReadCounter_Ntag - Send READ_CNT (0x39) for one counter index
 *
 * @param[in]  idx Counter index (0-2, per NXP datasheet)
 * @param[out] rxBuf Buffer to store the 3-byte counter value
 * @param[in]  rxBufLen Size of receive buffer
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE on success (rcvLen == 3); a real tag NAKs/times out
 *         when the index is unused, unconfigured, or unsupported -- that
 *         negative result is itself the genuine answer and must be
 *         preserved (index left invalid), not retried into a fabricated one.
 */
/*============================================================================*/
static ReturnCode ReadCounter_Ntag(uint8_t idx, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd[2] = { 0x39, idx };

    if ((rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 3U)) {
        return RFAL_ERR_PARAM;
    }

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(cmd, sizeof(cmd), rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}


/*============================================================================*/
/**
 * @brief CheckTearing_Ntag - Send CHECK_TEARING_EVENT (0x3E) for one flag index
 *
 * @param[in]  idx Tearing-flag index (0-2, per NXP datasheet)
 * @param[out] rxBuf Buffer to store the 1-byte tearing flag
 * @param[in]  rxBufLen Size of receive buffer
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE on success (rcvLen == 1); NTAG213/215/216 genuinely
 *         do not support this command and are expected to NAK it; that NAK is preserved as
 *         "index invalid", never papered over with a fabricated flag byte.
 */
/*============================================================================*/
static ReturnCode CheckTearing_Ntag(uint8_t idx, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd[2] = { 0x3E, idx };

    if ((rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 1U)) {
        return RFAL_ERR_PARAM;
    }

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(cmd, sizeof(cmd), rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}


/*============================================================================*/
/**
 * @brief PwdAuth_Ntag - Send genuine PWD_AUTH (0x1B) to a physical tag
 *
 * @param[in]  pwd 4-byte password to try
 * @param[out] rxBuf Buffer to store the 2-byte PACK response (>= 2 bytes)
 * @param[in]  rxBufLen Size of receive buffer
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE if a frame was received at all (caller must still
 *         check *rcvLen == 2 for a genuine PACK -- any other length is a
 *         malformed/unexpected response, never treated as success).
 * @retval Other RFAL error codes (timeout, CRC, link loss, ...) -- the
 *         genuine transport-level outcome, never papered over.
 */
/*============================================================================*/
static ReturnCode PwdAuth_Ntag(const uint8_t pwd[4], uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd[5] = { 0x1B, 0, 0, 0, 0 };

    if ((pwd == NULL) || (rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 2U)) {
        return RFAL_ERR_PARAM;
    }
    cmd[1] = pwd[0]; cmd[2] = pwd[1]; cmd[3] = pwd[2]; cmd[4] = pwd[3];

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(cmd, sizeof(cmd), rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}

/*============================================================================*/
/**
 * @brief nfc_poller_pwd_auth - One genuine PWD_AUTH attempt against the
 * currently-activated physical tag, with distinct, honestly-derived
 * outcomes. Never fabricates a PACK and never treats an ambiguous result
 * as success.
 *
 * Outcome derivation (see the Phase 1 audit): RFAL_ERR_NONE + exactly 2
 * received bytes is the ONLY success shape -- those 2 bytes are the
 * genuine PACK, copied out verbatim, never validated against anything
 * unless the caller supplies expected_pack. RFAL_ERR_TIMEOUT is treated
 * as an explicit rejection (the documented real-hardware behavior for
 * these chips: a wrong password yields no response, not a distinct NAK
 * frame). Any other transport-level error (CRC/parity/framing/collision/
 * link loss) or a wrong-length-but-received frame is a communication
 * error -- genuinely inconclusive, not a password verdict either way.
 *
 * @param[in]  pwd 4-byte password to try
 * @param[in]  expected_pack NULL, or a previously-verified 2-byte PACK to
 *             re-validate against (re-auth of an already-trusted
 *             credential) -- a differing PACK is reported distinctly
 *             rather than silently accepted.
 * @param[out] pack_out 2-byte buffer receiving the genuine PACK on
 *             NFC_PWDAUTH_OK (undefined otherwise -- caller must check
 *             the return value first).
 */
/*============================================================================*/
typedef enum {
    NFC_PWDAUTH_OK = 0,          /* accepted; pack_out holds the genuine PACK      */
    NFC_PWDAUTH_REJECTED,        /* tag rejected the password (timeout)            */
    NFC_PWDAUTH_PACK_MISMATCH,   /* accepted, but PACK != caller's expected_pack   */
    NFC_PWDAUTH_COMM_ERROR,      /* malformed response / CRC / collision / link loss */
} nfc_pwdauth_result_t;

static nfc_pwdauth_result_t nfc_poller_pwd_auth(const uint8_t pwd[4],
                                                const uint8_t expected_pack[2],
                                                uint8_t pack_out[2])
{
    uint8_t  rx[8];
    uint16_t rcvLen = 0;
    ReturnCode err = PwdAuth_Ntag(pwd, rx, sizeof(rx), &rcvLen);

    if (err == RFAL_ERR_TIMEOUT) {
        return NFC_PWDAUTH_REJECTED;
    }
    if (err != RFAL_ERR_NONE) {
        return NFC_PWDAUTH_COMM_ERROR;
    }
    if (rcvLen != 2U) {
        /* A frame came back but not a 2-byte PACK -- inconclusive, never a
         * password verdict either way. */
        return NFC_PWDAUTH_COMM_ERROR;
    }

    if ((expected_pack != NULL) && (memcmp(rx, expected_pack, 2U) != 0)) {
        return NFC_PWDAUTH_PACK_MISMATCH;
    }

    if (pack_out != NULL) { memcpy(pack_out, rx, 2U); }
    return NFC_PWDAUTH_OK;
}


/*============================================================================*/
/**
 * @brief LogParsedNtagVersion - Parse and log NTAG version information
 * 
 * Parses NTAG GET_VERSION response (8 bytes) and logs detailed version information
 * including vendor, product, sub-type, version number, size, and protocol.
 * Determines total page count based on size code:
 * - 0x0F: NTAG213 (45 pages, 144B user)
 * - 0x11: NTAG215 (135 pages, 504B user)
 * - 0x13: NTAG216 (231 pages, 888B user)
 * 
 * @param[in] version Pointer to 8-byte version data from GET_VERSION response
 * @param[in] len Length of version data (should be 8)
 * @retval Total page count (0 if unknown or invalid)
 */
/*============================================================================*/
static uint16_t LogParsedNtagVersion(const uint8_t *version, uint16_t len)
{
	//Use attributes for the compressor warning issue
    uint16_t totalPages = 0;

    if ((version == NULL) || (len < 8U)) {
        platformLog("NTAG GET_VERSION parse skipped (len=%u)\r\n", len);
        return totalPages;
    }

    const uint8_t header  __attribute__((unused)) = version[0];
    const uint8_t vendor  = version[1];
    const uint8_t prod    = version[2];
    const uint8_t sub     = version[3];
    const uint8_t major   __attribute__((unused)) = version[4];
    const uint8_t minor   __attribute__((unused)) = version[5];
    const uint8_t size    = version[6];
    const uint8_t proto   = version[7];

    const char *vendorStr __attribute__((unused)) = (vendor == 0x04) ? "NXP" : "Unknown";
    const char *prodStr   __attribute__((unused)) = (prod == 0x04) ? "NTAG" : "Unknown";
    const char *subStr    __attribute__((unused)) = (sub == 0x02) ? "Standard" : "Unknown";

    const char *sizeStr __attribute__((unused)) = "Unknown";
    if (size == 0x0F) {
        sizeStr    = "NTAG213 [144B user]";
        totalPages = 45U;
    }
    else if (size == 0x11) {
        sizeStr    = "NTAG215 [504B user]";
        totalPages = 135U;
    }
    else if (size == 0x13) {
        sizeStr    = "NTAG216 [888B user]";
        totalPages = 231U;
    }

    const char *protoStr __attribute__((unused)) = (proto == 0x03) ? "ISO14443-3" : "Unknown";

    platformLog("NTAG VERSION parsed: hdr=0x%02X vendor=%s(0x%02X) prod=%s(0x%02X)\r\n", header, vendorStr, vendor, prodStr, prod);
    platformLog("sub=%s(0x%02X) ver=%u.%u size=%s(0x%02X) proto=%s(0x%02X)\r\n", subStr, sub, (unsigned)major, (unsigned)minor, sizeStr, size, protoStr, proto);

    if (totalPages == 0U) {
        platformLog("NTAG VERSION: unknown size code 0x%02X, using default page count\r\n", size);
    } else {
        platformLog("NTAG VERSION: detected %u total pages\r\n", totalPages);
    }

    return totalPages;
}

/*============================================================================*/
/**
 * @brief t2t_variant_from_version - Map a complete GET_VERSION tuple to a
 *        Type 2 sub-variant + its total page count.
 *
 * Only a recognized, complete (>=8 byte) NXP-vendor response is trusted.
 * Unknown or ambiguous tuples return M1NFC_T2TVAR_UNKNOWN (never forced into
 * a model). The NTAG I2C family (prod subtype 5, major 2) is out of scope and
 * is deliberately NOT classified as NTAG216 on the shared 0x13 size code.
 * Covers the in-scope Type 2 variants.
 *
 * @param[in]  version GET_VERSION response bytes
 * @param[in]  len     Response length
 * @param[out] outPages Total page count for the variant (0 if unknown)
 * @retval One of M1NFC_T2TVAR_*
 */
/*============================================================================*/
static uint8_t t2t_variant_from_version(const uint8_t *version, uint16_t len, uint16_t *outPages)
{
    if (outPages) *outPages = 0U;
    if ((version == NULL) || (len < 8U)) return M1NFC_T2TVAR_UNKNOWN;
    if (version[1] != 0x04U) return M1NFC_T2TVAR_UNKNOWN; /* trust NXP-vendor tuples only */

    const uint8_t subtype = version[3];
    const uint8_t major   = version[4];
    const uint8_t size    = version[6];

    /* NTAG I2C family is out of scope this increment: do not force it into
     * NTAG216 on the shared 0x13 storage-size code. */
    if ((subtype == 0x05U) && (major == 0x02U)) return M1NFC_T2TVAR_UNKNOWN;

    uint8_t  var = M1NFC_T2TVAR_UNKNOWN;
    uint16_t pg  = 0U;
    switch (size) {
        case 0x00U: /* fallthrough - treat 0x00 as EV1 MF0UL11 */
        case 0x0BU: var = M1NFC_T2TVAR_UL11;    pg = 20U;  break;
        case 0x0EU: var = M1NFC_T2TVAR_UL21;    pg = 41U;  break;
        case 0x0FU: var = M1NFC_T2TVAR_NTAG213; pg = 45U;  break;
        case 0x11U: var = M1NFC_T2TVAR_NTAG215; pg = 135U; break;
        case 0x13U: var = M1NFC_T2TVAR_NTAG216; pg = 231U; break;
        default:    var = M1NFC_T2TVAR_UNKNOWN; pg = 0U;   break;
    }
    if (outPages) *outPages = pg;
    return var;
}

/*============================================================================*/
/**
 * @brief t2t_probe_legacy_variant - Identify a Type 2 tag that does NOT answer
 *        GET_VERSION (original Ultralight, Ultralight C, NTAG203).
 *
 * Conservative order, stopping at the
 * first positive result:
 *   1. Ultralight C  - responds to the 3DES AUTHENTICATE first frame (0x1A);
 *                      detection only, no key material is sent and the
 *                      handshake is never completed (non-destructive).
 *   2. NTAG203       - a successful READ of page 41 (its last page). Reached
 *                      ONLY after Ultralight C is ruled out, so a card that
 *                      answers 0x1A can never be mislabeled NTAG203.
 *   3. original Ultralight - conservative final fallback (16 pages). Any
 *                      ambiguous result lands here (safe: under-read only).
 *
 * @param[out] outPages Total page count for the identified variant
 * @retval One of M1NFC_T2TVAR_ULC / _NTAG203 / _UL
 */
/*============================================================================*/
static uint8_t t2t_probe_legacy_variant(uint16_t *outPages)
{
    uint8_t    rx[32];
    uint16_t   rcv = 0U;
    ReturnCode e;

    /* (1) Ultralight C: 3DES AUTHENTICATE first frame. A compliant card replies
     * with 0xAF followed by 8 bytes of encrypted RndB (9 bytes total). */
    {
        uint8_t auth_cmd[2] = { 0x1AU, 0x00U };
        rcv = 0U;
        e = rfalTransceiveBlockingTxRx(auth_cmd, (uint16_t)sizeof(auth_cmd),
                                       rx, (uint16_t)sizeof(rx), &rcv,
                                       RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(20U));
        if ((e == RFAL_ERR_NONE) && (rcv == 9U) && (rx[0] == 0xAFU)) {
            platformLog("T2T probe: Ultralight C (AUTH 0x1A -> 0xAF)\r\n");
            if (outPages) *outPages = 48U;
            return M1NFC_T2TVAR_ULC;
        }
    }

    /* (2) NTAG203: 42 pages (0..41). READ of the last page succeeds only on a
     * tag that actually has it; original Ultralight rejects the out-of-range
     * address. Runs only after the Ultralight C probe above has failed. */
    {
        rcv = 0U;
        e = rfalT2TPollerRead(41U, rx, (uint16_t)sizeof(rx), &rcv);
        if ((e == RFAL_ERR_NONE) && (rcv >= 16U)) {
            platformLog("T2T probe: NTAG203 (page 41 readable)\r\n");
            if (outPages) *outPages = 42U;
            return M1NFC_T2TVAR_NTAG203;
        }
    }

    /* (3) Conservative fallback: original MIFARE Ultralight (16 pages). */
    platformLog("T2T probe: original Ultralight (fallback)\r\n");
    if (outPages) *outPages = 16U;
    return M1NFC_T2TVAR_UL;
}

/*============================================================================*/
/**
 * @brief m1_t2t_read_ntag - Read Type 2 Tag (NTAG/Ultralight) memory
 * 
 * Reads Type 2 Tag memory pages using T2T READ commands.
 * Performs GET_VERSION to determine page count, then reads all pages
 * sequentially. Parses NDEF TLV if present.
 * 
 * @param[in] dev Pointer to NFC device (unused, kept for compatibility)
 * @retval None
 */
/*============================================================================*/
static void m1_t2t_read_ntag(const rfalNfcDevice *dev)
{
    (void)dev; // Temporarily unused

    uint8_t  buf[16];               // T2T READ response buffer
    uint8_t* dump     = g_nfc_dump_buf;
    uint16_t dumpSize = NFC_DUMP_BUF_SIZE;
    uint16_t offset   = 0;
    uint16_t rcvLen   = 0;
    uint16_t max_page = 45U;        // Default to NTAG213 span when size is unknown
    ReturnCode err;

    // Clear previous NDEF / dump (optional)
    nfc_ctx_clear_t2t_ndef();
    nfc_ctx_clear_dump();
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);
    nfc_ctx_set_t2t_expected_pages(0);
    nfc_ctx_set_t2t_geometry_corrupt(false);
    nfc_ctx_clear_t2t_version();
    nfc_ctx_clear_t2t_signature();
    nfc_ctx_clear_t2t_counters();
    nfc_ctx_clear_t2t_tearing();
    nfc_ctx_clear_t2t_protection();
    nfc_ctx_clear_t2t_protection_suspected();
    nfc_ctx_clear_t2t_credential();

    uint8_t version[8] = {0};
    bool    ver_ok     = false;

    for (uint8_t attempt = 0; attempt < 2 && !ver_ok; attempt++) {
        if (attempt > 0) {
            osDelay(10);  // Wait before retry
        }
        
        memset(version, 0x00, sizeof(version));
        rcvLen = 0;
        err = GetVersion_Ntag(version, sizeof(version), &rcvLen);

        /* If all response bytes are 0x00, consider it a failure */
        bool all_zero = true;
        for (uint8_t i = 0; i < sizeof(version); i++) {
            if (version[i] != 0x00) { all_zero = false; break; }
        }

        if ((err == RFAL_ERR_NONE) && (rcvLen == sizeof(version)) && !all_zero) {
            ver_ok = true;
        }
    }

    if (ver_ok) {
        nfc_ctx_set_t2t_version(version, (uint8_t)rcvLen);
        (void)LogParsedNtagVersion(version, rcvLen);   /* keep detailed serial log */

        uint16_t vpages = 0U;
        uint8_t  var    = t2t_variant_from_version(version, rcvLen, &vpages);
        if ((var != M1NFC_T2TVAR_UNKNOWN) && (vpages > 0U)) {
            nfc_ctx_set_t2t_variant(var);
            max_page = vpages;
            /* Record the FULL geometry this identification implies, before
             * the capture loop below runs -- so a read that later stops
             * early (NAK/timeout) still leaves the true expected total on
             * record, distinct from however many pages actually got
             * captured. This is what lets a genuinely-truncated NTAG216
             * round-trip honestly as "NTAG216, 42/231" instead of a
             * complete-looking "NTAG203, 42/42" once saved and reloaded. */
            nfc_ctx_set_t2t_expected_pages(vpages);
            platformLog("GET_VER OK: variant=%u, %u pages\r\n", var, max_page);
        } else {
            /* Complete but unrecognized version tuple: do NOT force a model.
             * Resolve conservatively via the legacy probe chain. */
            uint16_t ppages = 0U;
            uint8_t  pvar   = t2t_probe_legacy_variant(&ppages);
            nfc_ctx_set_t2t_variant(pvar);
            if (ppages > 0U) { max_page = ppages; nfc_ctx_set_t2t_expected_pages(ppages); }
            platformLog("GET_VER OK but unknown tuple -> probe variant=%u, %u pages\r\n", pvar, max_page);
        }
    } else {
        /* No usable GET_VERSION: identify via the conservative probe chain
         * (Ultralight C -> NTAG203 -> original Ultralight). */
        uint16_t ppages = 0U;
        uint8_t  pvar   = t2t_probe_legacy_variant(&ppages);
        nfc_ctx_set_t2t_variant(pvar);
        if (ppages > 0U) { max_page = ppages; nfc_ctx_set_t2t_expected_pages(ppages); }
        platformLog("GET_VER fail -> probe variant=%u, %u pages\r\n", pvar, max_page);
    }



    // Read blocks 0 ~ N (T2T READ reads 4 blocks at a time, so increment by 4)
    ReturnCode last_block_err = RFAL_ERR_NONE;   /* the error that actually ended the read, if any */
    for (uint8_t blk = 0; blk < max_page; blk += 4) {  // Read 4 blocks at a time
        rcvLen = 0;
        bool read_ok = false;
        
        /* Retry each READ command once on a NAK/timeout (2 attempts total) to
         * ride out a transient glitch, then stop. Never loop indefinitely. */
        for (uint8_t retry = 0; retry < 2 && !read_ok; retry++) {
            /* Wait before retry (allow time for CE to respond) */
            if (retry > 0) {
                osDelay(10);  // Increase retry wait time (10ms)
            }
            
            /* Initialize buffer (prevent previous data residue) */
            memset(buf, 0x00, sizeof(buf));
            rcvLen = 0;
            
            err = rfalT2TPollerRead(blk, buf, sizeof(buf), &rcvLen);
            
            if (err == RFAL_ERR_NONE && rcvLen >= 16) {  // T2T READ returns 16 bytes (4 blocks)
                read_ok = true;
                /* Log only critical blocks */
                if (blk == 0 || blk == 4) {
                    platformLog("READ blk%u: %s\r\n", blk, hex2Str(buf, 8));
                }
            } else {
                /* Log only critical blocks on failure */
                if ((blk == 0 || blk == 4) && retry == 0) {
                    platformLog("READ blk%u fail: err=%d rcv=%u\r\n", blk, err, rcvLen);
                }
                /* If field loss, don't retry anymore. Same for a genuine
                 * protocol-level NAK (RFAL_ERR_PROTO, rfal_t2t.c's own T2T
                 * 1.0 5.2.1.7 mapping of a real NACK response): it is a
                 * DEFINITIVE, conclusive answer from the tag, not a
                 * transient glitch this retry loop exists to ride out.
                 * Field evidence (live serial capture) showed retrying it
                 * anyway does active harm: the first attempt correctly
                 * returned RFAL_ERR_PROTO/rcvLen=1 (a clean NAK), but the
                 * very next attempt against the SAME still-present,
                 * still-emulating tag came back RFAL_ERR_TIMEOUT instead --
                 * silently overwriting the clean, correct signal with a
                 * misleading one and leaving downstream protection-
                 * suspicion detection with the wrong error code to reason
                 * about. Stopping immediately preserves the first, genuine
                 * result. */
                if ((err == RFAL_ERR_LINK_LOSS) || (err == RFAL_ERR_PROTO)) {
                    break;
                }
            }
        }

        /* Calculate number of pages to save */
        uint8_t pages_to_save = 4;
        if (blk + pages_to_save > max_page) {
            pages_to_save = max_page - blk;
        }
        
        if (!read_ok) {
            /* NAK, timeout, or link loss that persisted after one retry.
             * End the read here rather than fabricating zero pages past a real
             * memory boundary (an over-read on a small tag would otherwise wrap
             * and duplicate data). "Pages Read" then reflects the pages that
             * were actually captured. last_block_err records exactly which
             * outcome ended it -- RFAL_ERR_PROTO (a genuine T2T NACK,
             * rfal_t2t.c's own T2T-1.0-5.2.1.7 protocol-error mapping) is a
             * DETERMINISTIC signal distinct from a timeout/link-loss (card
             * removed, RF glitch) -- only the former may ever suggest
             * password protection; the latter never should. */
            last_block_err = err;
            platformLog("READ blk%u ended read (err=%d); %u pages captured\r\n",
                        blk, err, (unsigned)blk);
            break;
        }

        /* On success: store received data at the corresponding block position */
        for (uint8_t i = 0; i < pages_to_save; i++) {
            uint16_t page_idx = blk + i;
            if (page_idx >= max_page) break;

            uint16_t save_offset = page_idx * 4;
            if (save_offset + 4 <= dumpSize) {
                /* Copy page data from buf */
                uint8_t *src = &buf[i * 4];
                uint8_t *dst = &dump[save_offset];
                memcpy(dst, src, 4);
            }
        }

        /* Update offset to next block position (successful blocks only).
         * MUST use pages_to_save, not a blind +4: on the final iteration of
         * a page count that isn't a multiple of 4 (e.g. NTAG213=45,
         * NTAG215=135, NTAG216=231), pages_to_save is already clamped to
         * the true remainder while a raw "+4" would silently round the
         * derived page count up to the next multiple of 4 -- inflating
         * num_pages below past the real tag boundary (45->48, 135->136,
         * 231->232) and marking those extra, never-actually-read pages
         * "valid" via the blanket memset further down. A 48-page NTAG213 is
         * indistinguishable from a genuine 48-page Ultralight C to any
         * page-count-keyed consumer (e.g. the saved-file variant restore in
         * nfc_storage.c), which is exactly the false "Tag type not
         * supported" this fixes. */
        offset = (blk + pages_to_save) * 4;
        
        /* Log progress (every 16 blocks) */
        if ((blk + 4) % 16 == 0 || (blk + 4) >= max_page) {
            platformLog("T2T read progress: %u/%u pages\r\n", (unsigned)((blk + 4 > max_page) ? max_page : blk + 4), (unsigned)max_page);
        }
    }

    osDelay(5);

    // Actual dump length and page count
    uint16_t dump_len  = offset;
    uint16_t num_pages = dump_len / 4;

    if (num_pages == 0) {
        platformLog("T2T: no pages dumped\r\n");
        return;
    }

    // Mark all pages as valid in valid_bits for now
    memset(g_nfc_valid_bits, 0xFF, (num_pages + 7) / 8);

    // Update context dump metadata
    nfc_ctx_set_dump(4,                // unit_size: 4 bytes per page
                     num_pages,        // unit_count
                     0,                // origin
                     g_nfc_dump_buf,   // data pointer
                     g_nfc_valid_bits, // valid bits
                     num_pages - 1,    // max_seen_unit
                     true);            // has_dump = true

    /* ---- Genuine authenticity/session data capture (signature, counters,
     * tearing flags) ----
     * Only attempted for variants that support GET_VERSION/READ_SIGNATURE
     * (UL11, NTAG213/215/216 -- plain Ultralight predates these commands
     * entirely, matching m1_t2t_emu_image_get_version()'s existing gate).
     * A failed/NAK'd attempt leaves the corresponding nfc_ctx entry invalid
     * -- never fabricated -- which m1_t2t_emu_image_build() and
     * CeHandleT2TCmdRx() both honor. */
    uint8_t cap_variant = nfc_ctx_get_t2t_variant();
    if (cap_variant == M1NFC_T2TVAR_UL11 || cap_variant == M1NFC_T2TVAR_NTAG213 ||
        cap_variant == M1NFC_T2TVAR_NTAG215 || cap_variant == M1NFC_T2TVAR_NTAG216) {

        /* If the page-dump loop above ended on a genuine protocol NAK (a
         * protected tag's AUTH0 boundary), the emulated tag may be
         * hardware-deselected
         * for ANY NAK'd command outcome -- unlike real NXP silicon, which
         * stays selected after a permission-denied NAK. Every subsequent
         * command in this same RF session then gets a genuine, silent
         * timeout regardless of its own correctness: confirmed by
         * hardware evidence that READ_SIGNATURE's wire format and dispatch
         * are correct, yet it timed out with zero response immediately
         * following exactly this kind of NAK. Re-selecting the SAME tag
         * (matching UID) recovers the session. Only attempted when the
         * dump loop genuinely NAK'd -- an unprotected tag's
         * fully-successful dump never triggers this deselect path, so
         * this never runs for the already-hardware-validated common case. */
        bool capture_ok = true;
        if (last_block_err == RFAL_ERR_PROTO) {
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            rfalNfcDiscover(&discParam);
            rfalNfcDevice *reDev = NULL;
            for (uint16_t tries = 0; tries < 100U; tries++) {
                rfalNfcWorker();
                if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&reDev); break; }
                osDelay(5);
                m1_wdt_reset();
            }
            nfc_run_ctx_t *rc = nfc_ctx_get();
            if ((reDev == NULL) || (reDev->type != RFAL_NFC_LISTEN_TYPE_NFCA) ||
                (reDev->nfcidLen != rc->head.uid_len) ||
                (memcmp(reDev->nfcid, rc->head.uid, rc->head.uid_len) != 0)) {
                platformLog("T2T re-select after NAK failed or UID mismatch -- skipping signature/counter/tearing capture\r\n");
                capture_ok = false;
            } else {
                platformLog("T2T re-selected after NAK for signature/counter/tearing capture\r\n");
            }
        }

        if (capture_ok) {
        uint8_t    sigBuf[32];
        uint16_t   sigLen = 0;
        ReturnCode sigErr = RFAL_ERR_NONE;
        bool       sig_ok = false;
        /* Retry once, matching the page-read loop's own retry pattern, as
         * a defensive fallback for any other transient glitch beyond the
         * deselect case already handled above by re-selecting. */
        for (uint8_t retry = 0; retry < 2U && !sig_ok; retry++) {
            if (retry > 0U) { osDelay(10); }
            sigLen = 0;
            sigErr = ReadSignature_Ntag(sigBuf, sizeof(sigBuf), &sigLen);
            if ((sigErr == RFAL_ERR_NONE) && (sigLen == 32U)) { sig_ok = true; }
        }
        if (sig_ok) {
            nfc_ctx_set_t2t_signature(sigBuf);
            platformLog("T2T signature captured\r\n");
        } else {
            platformLog("T2T signature not captured (err=%d rcv=%u)\r\n", sigErr, sigLen);
        }

        /* UL11 exposes 3 independent counters; NTAG213/215/216 use index 2.
         * These indices avoid probing
         * indices no reader will ever ask for. */
        uint8_t counterIdx[3];
        uint8_t counterIdxCount;
        if (cap_variant == M1NFC_T2TVAR_UL11) {
            counterIdx[0] = 0; counterIdx[1] = 1; counterIdx[2] = 2; counterIdxCount = 3;
        } else {
            counterIdx[0] = 2; counterIdxCount = 1;
        }
        for (uint8_t i = 0; i < counterIdxCount; i++) {
            uint8_t  cBuf[3];
            uint16_t cLen = 0;
            if ((ReadCounter_Ntag(counterIdx[i], cBuf, sizeof(cBuf), &cLen) == RFAL_ERR_NONE) && (cLen == 3U)) {
                nfc_ctx_set_t2t_counter(counterIdx[i], cBuf);
            }
        }

        /* Same index convention for CHECK_TEARING; NTAG213/215/216 are
         * expected to NAK this because they do not support it, leaving the
         * index invalid. */
        for (uint8_t i = 0; i < counterIdxCount; i++) {
            uint8_t  tBuf[1];
            uint16_t tLen = 0;
            if ((CheckTearing_Ntag(counterIdx[i], tBuf, sizeof(tBuf), &tLen) == RFAL_ERR_NONE) && (tLen == 1U)) {
                nfc_ctx_set_t2t_tearing(counterIdx[i], tBuf[0]);
            }
        }
        }   /* if (capture_ok) */

        /* ---- Protection state (AUTH0/PROT/AUTHLIM) ----
         * NXP CFG0/CFG1 layout: CFG0 byte3=
         * AUTH0; CFG1 byte0 bits[2:0]=AUTHLIM, bit6=CFGLCK, bit7=PROT.
         * Read from whatever the dump loop above already captured. CFG0/CFG1
         * are NOT exempt from the AUTH0 gate on real silicon: if AUTH0 <= this variant's own
         * config-page address (e.g. AUTH0=4, protecting the whole tag), the
         * dump loop above NAKs before ever reaching CFG0/CFG1, and they are
         * genuinely unreadable pre-auth -- not "still ordinary". */
        uint16_t cfg0_page = 0;
        uint16_t expected_pages = nfc_ctx_get_t2t_expected_pages();
        if (m1_t2t_emu_image_cfg0_page(cap_variant, &cfg0_page) &&
            ((uint32_t)cfg0_page + 1U) < num_pages) {
            const uint8_t *cfg0 = &dump[cfg0_page * 4U];
            const uint8_t *cfg1 = &dump[(cfg0_page + 1U) * 4U];
            nfc_ctx_set_t2t_auth0(cfg0[3]);
            nfc_ctx_set_t2t_prot((cfg1[0] & 0x80U) != 0U);
            nfc_ctx_set_t2t_authlim((uint8_t)(cfg1[0] & 0x07U));
            if (cfg0[3] != 0xFFU) {
                platformLog("T2T protected: AUTH0=%u PROT=%u AUTHLIM=%u\r\n",
                            (unsigned)cfg0[3], (unsigned)((cfg1[0] & 0x80U) != 0U),
                            (unsigned)(cfg1[0] & 0x07U));
            }
        } else if ((expected_pages != 0U) && (num_pages < expected_pages) &&
                   (last_block_err == RFAL_ERR_PROTO)) {
            /* Config pages themselves are at/after AUTH0 -- genuinely
             * unreadable pre-auth, and CANNOT be inferred from nothing: the
             * first blocked page proves protection is SUSPECTED, never the
             * genuine AUTH0/PROT/AUTHLIM value (a stopped-short read that
             * isn't page-aligned to this variant's real AUTH0 would make a
             * direct "auth0 = num_pages" guess simply wrong, and an
             * unverified guess must never reach the same field a genuine
             * wire-read populates -- it could otherwise leak into a saved
             * file or the emulation image if something later went wrong).
             *
             * Gated on RFAL_ERR_PROTO -- a genuine, deterministic T2T
             * protocol NACK (rfal_t2t.c's own T2T 1.0 5.2.1.7 mapping),
             * distinct from RFAL_ERR_TIMEOUT/RFAL_ERR_LINK_LOSS, which an
             * ordinary RF dropout or card removal produces instead and
             * which therefore never satisfies this condition.
             *
             * T2T-UNLOCK-T2 tried this exact check and it never fired on
             * real hardware; T2T-UNLOCK-T3 replaced it with an outcome-based
             * liveness probe (re-read page 0, require a byte-identical
             * response) to sidestep needing to trust a specific ReturnCode
             * -- but field testing proved the probe itself unreliable even
             * against the externally-validated-as-locked fixture, making it
             * an unsound gate. Live serial capture (T2T-UNLOCK-T4) then
             * found the REAL bug: the retry loop above used to retry a
             * genuine RFAL_ERR_PROTO NACK (attempt 1: err=11/RFAL_ERR_PROTO,
             * rcv=1, a clean textbook NACK; attempt 2, needlessly retried:
             * err=4/RFAL_ERR_TIMEOUT instead) and last_block_err kept only
             * the LAST attempt -- silently overwriting the correct signal
             * before this check ever ran. With that retry now suppressed
             * (see the "stop immediately" fix above, mirroring the existing
             * RFAL_ERR_LINK_LOSS case), last_block_err reliably holds the
             * genuine RFAL_ERR_PROTO value, and this direct check is sound
             * again -- no probe needed.
             *
             * This transient signal is NEVER written to the V4 file and
             * NEVER read by m1_t2t_emu_image_build(); it only ever permits
             * offering manual Unlock (nfc_can_unlock()) before the genuine
             * AUTH0/PROT/AUTHLIM fields exist. Dictionary mode stays
             * refused throughout, since genuine AUTHLIM is untouched here
             * and remains invalid. The genuine fields are populated only
             * once nfc_unlock_reread_all_pages() actually reads CFG0/CFG1
             * post-authentication. */
            nfc_ctx_set_t2t_protection_suspected(num_pages);
            platformLog("T2T protection suspected (genuine NACK, config pages unreachable "
                        "pre-auth): read stopped at %u/%u pages\r\n",
                        (unsigned)num_pages, (unsigned)expected_pages);
        }
    }

    // ---- TLV parsing (find NDEF) ----
    const uint8_t firstDataPage = 4;
    uint8_t *p   = dump + (firstDataPage * 4);
    uint8_t *end = dump + dump_len;

    if (p >= end) {
        platformLog("T2T: dump too short (offset=%u)\r\n", dump_len);
        return;
    }

    while (p < end) {
        uint8_t t = *p++;

        if (t == 0x00) {
            // NULL TLV
            continue;
        }
        if (t == 0xFE) {
            // Terminator TLV
            platformLog("T2T: Terminator TLV reached, no more TLVs\r\n");
            break;
        }

        if (p >= end) break;

        uint16_t len = *p++;  // Simple 1-byte length handling

        if (t == 0x03) {      // NDEF TLV
            if (p + len > end) {
                len = (uint16_t)(end - p);  // Defensive
            }

            platformLog("T2T NDEF TLV found, len=%u\r\n", len);
            nfc_ctx_set_t2t_ndef(p, len);
#if 0
            //nfc_ctx_dump_t2t_ndef();   // Debug hexdump + ASCII
            //nfc_ctx_dump_t2t_pages();
#endif
            return;
        } else {
            // Skip uninterested TLV by len
            if (p + len > end) break;
            p += len;
        }
    }

    platformLog("T2T: NDEF TLV not found (scan from page=%u)\r\n", firstDataPage);
}

/*============================================================================*/
/**
 * @brief nfcv_variant_from_uid - Identify SLIX / SLIX2 from the UID.
 *
 * Gates on the NXP manufacturer byte, then decodes the ICODE IC-type byte and
 * (for icode_type 0x01) the 2-bit
 * type-indicator field. NEVER the manufacturer byte alone. SLIX-S (0x02) and
 * SLIX-L (0x03) are out of scope this increment -> GENERIC. Anything unknown
 * or ambiguous -> GENERIC.
 *
 * @param[in] uidWire 8-byte UID in RFAL wire order (nfcid; LSB-first, [7]=0xE0)
 * @retval M1NFC_VVAR_SLIX / _SLIX2 / _GENERIC
 */
/*============================================================================*/
static uint8_t nfcv_variant_from_uid(const uint8_t *uidWire)
{
    if (uidWire == NULL) return M1NFC_VVAR_GENERIC;

    /* Reverse to MSB-first order (uid[0]=0xE0, uid[1]=mfr,
     * uid[2]=ICODE type, uid[3]=type-indicator byte). */
    uint8_t u[RFAL_NFCV_UID_LEN];
    for (uint8_t i = 0; i < RFAL_NFCV_UID_LEN; i++) {
        u[i] = uidWire[RFAL_NFCV_UID_LEN - 1U - i];
    }

    if (u[1] != 0x04U) return M1NFC_VVAR_GENERIC;   /* NXP manufacturer gate */

    const uint8_t icode_type = u[2];
    if (icode_type == 0x01U) {
        const uint8_t type_indicator = (uint8_t)((u[3] >> 3) & 0x03U);
        if (type_indicator == 0x02U) return M1NFC_VVAR_SLIX;
        if (type_indicator == 0x01U) return M1NFC_VVAR_SLIX2;
        return M1NFC_VVAR_GENERIC;
    }
    /* 0x02 = SLIX-S, 0x03 = SLIX-L: out of scope -> generic. */
    return M1NFC_VVAR_GENERIC;
}

/*============================================================================*/
/**
 * @brief m1_nfcv_read - Read an ISO15693 / NFC-V tag (generic, SLIX, SLIX2).
 *
 * GET SYSTEM INFORMATION for DSFID/AFI/block-size/block-count, then single-
 * block reads (READ SINGLE BLOCK, 0x20) with NO retry, stopping at the first
 * failing block. Block bytes go into the shared
 * dump workspace so Preview/Raw Data reuse the Type-2 rendering. A failed block
 * is never labeled locked; a partial read simply reports fewer blocks.
 *
 * @param[in] dev Active NFC-V device (UID in dev->nfcid, wire order)
 */
/*============================================================================*/
static void m1_nfcv_read(const rfalNfcDevice *dev)
{
    nfc_ctx_clear_iso15693();
    nfc_ctx_clear_dump();
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);  /* avoid stale T2T name leaking */

    if (dev == NULL) return;

    const uint8_t *uid   = dev->nfcid;                     /* wire order, addressed mode */
    const uint8_t  flags = (uint8_t)RFAL_NFCV_REQ_FLAG_DEFAULT;

    /* --- Variant from UID --- */
    uint8_t variant = nfcv_variant_from_uid(uid);
    nfc_ctx_set_iso15693_variant(variant);
    platformLog("NFC-V variant=%u\r\n", variant);

    /* --- GET SYSTEM INFORMATION --- */
    uint8_t  si[32];
    uint16_t siLen       = 0;
    bool     has_sysinfo = false;
    bool     has_dsfid   = false, has_afi = false;
    uint8_t  dsfid = 0, afi = 0;
    uint16_t block_count = 0;
    uint8_t  block_size  = 0;

    ReturnCode err = rfalNfcvPollerGetSystemInformation(flags, uid, si, (uint16_t)sizeof(si), &siLen);
    if ((err == RFAL_ERR_NONE) && (siLen >= 10U) && ((si[0] & 0x01U) == 0U)) {
        /* si[0]=RES_FLAG, si[1]=INFO_FLAGS, si[2..9]=UID, then conditional fields. */
        const uint8_t info = si[1];
        uint16_t idx = 10U;   /* first byte after the always-present 8-byte UID */
        if (((info & 0x01U) != 0U) && (idx < siLen))       { dsfid = si[idx++]; has_dsfid = true; }  /* DSFID   */
        if (((info & 0x02U) != 0U) && (idx < siLen))       { afi   = si[idx++]; has_afi   = true; }  /* AFI     */
        if (((info & 0x04U) != 0U) && ((idx + 1U) < siLen)) {                                         /* MEMSIZE */
            uint8_t nbBlocks  = si[idx++];
            uint8_t blkSzCode = si[idx++];
            block_count = (uint16_t)nbBlocks + 1U;              /* stored value + 1 */
            block_size  = (uint8_t)((blkSzCode & 0x1FU) + 1U); /* low 5 bits + 1   */
            has_sysinfo = (block_count > 0U) && (block_size > 0U);
        }
        platformLog("NFC-V SYSINFO info=0x%02X dsfid=%u afi=%u blocks=%u size=%u\r\n",
                    info, dsfid, afi, block_count, block_size);
    } else {
        platformLog("NFC-V SYSINFO unavailable (err=%d len=%u)\r\n", (int)err, siLen);
    }

    nfc_ctx_set_iso15693_sysinfo(has_dsfid, dsfid, has_afi, afi, block_count, block_size, has_sysinfo);

    /* --- Single-block reads (no retry, stop at first failing block) --- */
    if (!has_sysinfo) {
        /* No usable memory layout: keep UID + variant only; never guess Y. */
        nfc_ctx_set_iso15693_blocks_read(0U);
        return;
    }

    /* Clamp to the shared dump workspace capacity (defensive; scope tags fit). */
    uint16_t max_blocks = block_count;
    if (((uint32_t)max_blocks * block_size) > NFC_DUMP_BUF_SIZE) {
        max_blocks = (uint16_t)(NFC_DUMP_BUF_SIZE / block_size);
    }
    if (max_blocks > NFC_DUMP_MAX_UNITS) max_blocks = (uint16_t)NFC_DUMP_MAX_UNITS;
    if (max_blocks < block_count) {
        platformLog("NFC-V: block_count %u exceeds dump capacity, capping to %u\r\n",
                    block_count, max_blocks);
    }

    uint8_t *dump        = g_nfc_dump_buf;
    uint16_t blocks_read = 0;
    uint8_t  rx[1U + RFAL_NFCV_MAX_BLOCK_LEN];

    for (uint16_t blk = 0; blk < max_blocks; blk++) {
        uint16_t rcv = 0;
        memset(rx, 0x00, sizeof(rx));
        err = rfalNfcvPollerReadSingleBlock(flags, uid, (uint8_t)blk, rx, (uint16_t)sizeof(rx), &rcv);

        /* rx[0]=RES_FLAG; block data follows. Require the whole block. */
        if ((err != RFAL_ERR_NONE) || (rcv < (uint16_t)(1U + block_size)) || ((rx[0] & 0x01U) != 0U)) {
            platformLog("NFC-V READ blk%u stop (err=%d rcv=%u); %u/%u read\r\n",
                        blk, (int)err, rcv, blocks_read, block_count);
            break;   /* no retry, stop at first failing block */
        }

        uint16_t off = (uint16_t)(blk * block_size);
        if (((uint32_t)off + block_size) <= NFC_DUMP_BUF_SIZE) {
            memcpy(&dump[off], &rx[1], block_size);
        }
        blocks_read++;
    }

    nfc_ctx_set_iso15693_blocks_read(blocks_read);

    /* --- Block lock/security status: GET MULTIPLE BLOCK SECURITY STATUS (0x2C) ---
     * Read-only, OPTIONAL command. NON-FATAL: a tag that does
     * not implement it simply leaves the lock state "unavailable" -- the data
     * read above already succeeded and is left completely unchanged (no
     * Option-on-read). Sent addressed with the SAME flags+uid as the reads.
     * Response: rx[0]=RES_FLAG, then one security byte per block; bit0 = locked.
     * Chunked <=32 blocks/query. Covers the same blocks the reader enumerated. */
    {
        uint8_t  lockbits[(NFC_DUMP_MAX_UNITS + 7U) / 8U];
        uint16_t locked_count = 0U;
        bool     sec_ok       = (max_blocks > 0U);

        memset(lockbits, 0, sizeof(lockbits));

        for (uint16_t first = 0U; (first < max_blocks) && sec_ok; first += NFCV_SEC_BLOCKS_PER_QUERY) {
            uint16_t n = (uint16_t)(max_blocks - first);
            if (n > NFCV_SEC_BLOCKS_PER_QUERY) n = NFCV_SEC_BLOCKS_PER_QUERY;

            uint8_t  req[2] = { (uint8_t)first, (uint8_t)(n - 1U) };   /* first block, (count-1) */
            uint8_t  sr[1U + NFCV_SEC_BLOCKS_PER_QUERY];
            uint16_t srLen = 0U;
            memset(sr, 0x00, sizeof(sr));

            ReturnCode se = rfalNfcvPollerTransceiveReq(NFCV_CMD_GET_MULTI_BLOCK_SEC, flags,
                                                        RFAL_NFCV_PARAM_SKIP, uid,
                                                        req, (uint16_t)sizeof(req),
                                                        sr, (uint16_t)sizeof(sr), &srLen);

            /* rx[0]=RES_FLAG (error bit 0 must be clear); then exactly n status bytes. */
            if ((se != RFAL_ERR_NONE) || (srLen < (uint16_t)(1U + n)) || ((sr[0] & 0x01U) != 0U)) {
                platformLog("NFC-V SEC 0x2C unavailable at blk%u (err=%d len=%u)\r\n",
                            (unsigned)first, (int)se, (unsigned)srLen);
                sec_ok = false;   /* non-fatal: read result stands, lock state = unavailable */
                break;
            }
            for (uint16_t j = 0U; j < n; j++) {
                if ((sr[1U + j] & 0x01U) != 0U) {                 /* bit0 = block locked */
                    uint16_t idx = (uint16_t)(first + j);
                    lockbits[idx >> 3] |= (uint8_t)(1U << (idx & 7U));
                    locked_count++;
                }
            }
        }

        nfc_ctx_set_iso15693_security(sec_ok, sec_ok ? lockbits : NULL, max_blocks, locked_count);
        platformLog("NFC-V SEC status=%d locked=%u/%u\r\n",
                    (int)sec_ok, (unsigned)locked_count, (unsigned)max_blocks);
    }

    if (blocks_read > 0U) {
        memset(g_nfc_valid_bits, 0xFF, (blocks_read + 7U) / 8U);
        nfc_ctx_set_dump(block_size,       /* unit_size = block size */
                         blocks_read,      /* unit_count */
                         0,                /* origin */
                         g_nfc_dump_buf,   /* data */
                         g_nfc_valid_bits, /* valid bits */
                         (uint16_t)(blocks_read - 1U), /* max_seen_unit */
                         true);            /* has_dump */
    }
    platformLog("NFC-V read done: %u/%u blocks x %u bytes\r\n",
                blocks_read, block_count, block_size);
}

/*============================================================================*/
/**
 * @brief st25tb_variant_from_uid - Identify the ST25TB variant from the UID.
 *
 * Keyed on (reversed, stored-order) uid[2] >> 2. The M1
 * keeps it GENERIC instead (never guessed, never crash).
 *
 * @param[in]  uidStored 8-byte UID in stored order (reversed from wire)
 * @param[out] outBlocks Total data blocks for the variant (0 if generic)
 * @retval One of M1NFC_TBVAR_*
 */
/*============================================================================*/
static uint8_t st25tb_variant_from_uid(const uint8_t *uidStored, uint16_t *outBlocks)
{
    if (outBlocks) *outBlocks = 0U;
    if (uidStored == NULL) return M1NFC_TBVAR_GENERIC;

    switch (uidStored[2] >> 2) {
        case 0x00: case 0x03: if (outBlocks) *outBlocks = 128U; return M1NFC_TBVAR_X4K;   /* SRIX4K       */
        case 0x04:            if (outBlocks) *outBlocks = 16U;  return M1NFC_TBVAR_X512;  /* SRIX512      */
        case 0x06:            if (outBlocks) *outBlocks = 16U;  return M1NFC_TBVAR_512AC; /* ST25TB512-AC */
        case 0x07:            if (outBlocks) *outBlocks = 128U; return M1NFC_TBVAR_04K;   /* ST25TB04K    */
        case 0x0C:            if (outBlocks) *outBlocks = 16U;  return M1NFC_TBVAR_512AT; /* ST25TB512-AT */
        case 0x0F:            if (outBlocks) *outBlocks = 64U;  return M1NFC_TBVAR_02K;   /* ST25TB02K    */
        default:              if (outBlocks) *outBlocks = 0U;   return M1NFC_TBVAR_GENERIC;
    }
}

/*============================================================================*/
/**
 * @brief m1_st25tb_read - Read an ST25TB / SRI / SRIX tag (read-only).
 *
 * Detects the variant from the (reversed) UID, then reads data blocks
 * 0..count-1 via READ_BLOCK (0x08) with NO retry, stopping at the first failing
 * block, followed by the system/OTP block (0xFF)
 * captured separately. Data blocks (raw wire bytes) go into the shared dump
 * workspace (unit_size = 4) so Preview/Raw Data reuse the Type-2 rendering. A
 * generic / unrecognized variant is NOT given a guessed block count: only the
 * UID, chip ID and system block are captured.
 *
 * @param[in] dev Active ST25TB device (chip ID + UID in dev->dev.st25tb)
 */
/*============================================================================*/
static void m1_st25tb_read(const rfalNfcDevice *dev)
{
    nfc_ctx_clear_st25tb();
    nfc_ctx_clear_dump();

    if (dev == NULL) return;

    /* Reverse the wire UID to stored order for detection/display. */
    uint8_t uidStored[RFAL_ST25TB_UID_LEN];
    for (uint8_t i = 0; i < RFAL_ST25TB_UID_LEN; i++) {
        uidStored[i] = dev->dev.st25tb.UID[RFAL_ST25TB_UID_LEN - 1U - i];
    }

    uint16_t block_count = 0;
    uint8_t  variant = st25tb_variant_from_uid(uidStored, &block_count);
    uint8_t  chip_id = dev->dev.st25tb.chipID;
    nfc_ctx_set_st25tb_ident(variant, chip_id, block_count, (uint8_t)RFAL_ST25TB_BLOCK_LEN);
    platformLog("ST25TB variant=%u chip=0x%02X blocks=%u\r\n", variant, chip_id, block_count);

    /* --- Data blocks 0..count-1 (no retry, stop at first failure) --- */
    uint8_t  *dump        = g_nfc_dump_buf;
    uint16_t  blocks_read = 0;

    /* Cap to dump capacity (all in-scope variants fit: max 128*4 = 512 B). */
    uint16_t max_blocks = block_count;
    if (((uint32_t)max_blocks * RFAL_ST25TB_BLOCK_LEN) > NFC_DUMP_BUF_SIZE) {
        max_blocks = (uint16_t)(NFC_DUMP_BUF_SIZE / RFAL_ST25TB_BLOCK_LEN);
    }
    if (max_blocks > NFC_DUMP_MAX_UNITS) max_blocks = (uint16_t)NFC_DUMP_MAX_UNITS;

    for (uint16_t blk = 0; blk < max_blocks; blk++) {
        rfalSt25tbBlock block;
        ReturnCode e = rfalSt25tbPollerReadBlock((uint8_t)blk, &block);
        if (e != RFAL_ERR_NONE) {
            platformLog("ST25TB READ blk%u stop (err=%d); %u/%u read\r\n",
                        blk, (int)e, blocks_read, block_count);
            break;   /* no retry, stop at first failing block */
        }
        uint16_t off = (uint16_t)(blk * RFAL_ST25TB_BLOCK_LEN);
        if (((uint32_t)off + RFAL_ST25TB_BLOCK_LEN) <= NFC_DUMP_BUF_SIZE) {
            memcpy(&dump[off], block, RFAL_ST25TB_BLOCK_LEN);   /* raw wire bytes */
        }
        blocks_read++;
    }
    nfc_ctx_set_st25tb_blocks_read(blocks_read);

    if (blocks_read > 0U) {
        memset(g_nfc_valid_bits, 0xFF, (blocks_read + 7U) / 8U);
        nfc_ctx_set_dump((uint16_t)RFAL_ST25TB_BLOCK_LEN,  /* unit_size = 4 */
                         blocks_read,                      /* unit_count */
                         0,                                /* origin */
                         g_nfc_dump_buf,                   /* data */
                         g_nfc_valid_bits,                 /* valid bits */
                         (uint16_t)(blocks_read - 1U),     /* max_seen_unit */
                         true);                            /* has_dump */
    }

    /* --- System / OTP block (0xFF), read separately, stored in ctx --- */
    {
        rfalSt25tbBlock sys;
        ReturnCode e = rfalSt25tbPollerReadBlock(0xFFU, &sys);
        if (e == RFAL_ERR_NONE) {
            nfc_ctx_set_st25tb_system(sys);
        } else {
            platformLog("ST25TB system block (0xFF) read failed (err=%d)\r\n", (int)e);
        }
    }

    platformLog("ST25TB read done: %u/%u data blocks + sys\r\n", blocks_read, block_count);
}

/*============================================================================*/
/*  MIFARE Classic - PHASE A (single sector, one explicit key)                */
/*                                                                            */
/*  Authentication and block reads. Crypto1 cipher lives in crypto1.c.       */
/*  IMPLEMENTED - PENDING HARDWARE VALIDATION.                                */
/*                                                                            */
/*  The encrypted MIFARE framing needs bit-level, custom-parity, manual-CRC   */
/*  transceive. The ST25R3916/RFAL raw path used here is:                     */
/*    rfalStartTransceive() + rfalRunBlocking(rfalGetTransceiveStatus())      */
/*  with a bit-length rfalTransceiveContext and flags PAR_TX_NONE |           */
/*  PAR_RX_KEEP | CRC_TX_MANUAL | CRC_RX_KEEP. Each byte is bit-packed as      */
/*  8 data bits (LSB-first) + 1 parity bit. This packing convention is the    */
/*  primary item to confirm against real hardware.                            */
/*============================================================================*/

/* MIFARE Classic frame waiting time (generous for Phase A). */
#define MFC_FWT              rfalConvMsTo1fc(10U)
#define MFC_CRCA_PRELOAD     (0x6363U)
/* Raw bit-level flags for the encrypted frames. CRC_RX_MANUAL is REQUIRED
 * here: rfalStartTransceive() rejects PAR_RX_KEEP without it (rfal_rfst25r3916.c
 * "if parity check is disabled CRC check must be disabled as well" -> ERR_NOTSUPP). */
#define MFC_RAW_FLAGS       ( (uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE   | \
                              (uint32_t)RFAL_TXRX_FLAGS_PAR_RX_KEEP   | \
                              (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL | \
                              (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP   | \
                              (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL | \
                              (uint32_t)RFAL_TXRX_FLAGS_AGC_ON )

/* Map an RFAL ReturnCode to a short name for diagnostics (kept for reuse). */
static const char *mfc_err_name(ReturnCode e) __attribute__((unused));
static const char *mfc_err_name(ReturnCode e)
{
    switch (e) {
        case RFAL_ERR_NONE:            return "NONE";
        case RFAL_ERR_IO:              return "IO";
        case RFAL_ERR_TIMEOUT:         return "TIMEOUT";
        case RFAL_ERR_PARAM:           return "PARAM";
        case RFAL_ERR_FRAMING:         return "FRAMING";
        case RFAL_ERR_CRC:             return "CRC";
        case RFAL_ERR_NOTSUPP:         return "NOTSUPP";
        case RFAL_ERR_PAR:             return "PAR";
        case RFAL_ERR_RF_COLLISION:    return "RF_COLLISION";
        case RFAL_ERR_WRONG_STATE:     return "WRONG_STATE";
        case RFAL_ERR_INCOMPLETE_BYTE: return "INCOMPLETE_BYTE";
        default:                       return "OTHER";
    }
}

/* Pack n bytes as (8 data bits LSB-first + 1 parity bit) each; returns bits. */
static uint16_t mfc_pack_bits(uint8_t *out, const uint8_t *data, const uint8_t *par, uint8_t n)
{
    uint16_t bit = 0;
    (void)memset(out, 0x00, (size_t)((n * 9 + 7) / 8));
    for (uint8_t i = 0; i < n; i++) {
        for (uint8_t j = 0; j < 8; j++) {
            if (((data[i] >> j) & 1U) != 0U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
            bit++;
        }
        if ((par[i] & 1U) != 0U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
        bit++;
    }
    return bit;
}

/* Unpack a bit-packed rx buffer into up to maxn (byte,parity) groups. */
static uint8_t mfc_unpack_bits(const uint8_t *in, uint16_t rxbits, uint8_t *data, uint8_t *par, uint8_t maxn)
{
    uint8_t  n   = 0;
    uint16_t bit = 0;
    while (((bit + 9U) <= rxbits) && (n < maxn)) {
        uint8_t b = 0;
        for (uint8_t j = 0; j < 8; j++) {
            if (((in[bit >> 3] >> (bit & 7U)) & 1U) != 0U) b |= (uint8_t)(1U << j);
            bit++;
        }
        data[n] = b;
        par[n]  = (uint8_t)((in[bit >> 3] >> (bit & 7U)) & 1U);
        bit++;
        n++;
    }
    return n;
}

/* Bit-level blocking transceive (encrypted MIFARE frames). */
static ReturnCode mfc_txrx_bits(uint8_t *tx, uint16_t txbits,
                                uint8_t *rx, uint16_t rxcap_bits, uint16_t *rxbits,
                                uint32_t flags)
{
    rfalTransceiveContext ctx;
    ReturnCode err;

    ctx.txBuf     = tx;
    ctx.txBufLen  = txbits;          /* bits */
    ctx.rxBuf     = rx;
    ctx.rxBufLen  = rxcap_bits;      /* bits */
    ctx.rxRcvdLen = rxbits;          /* bits (out) */
    ctx.flags     = flags;
    ctx.fwt       = MFC_FWT;

    err = rfalStartTransceive(&ctx);
    if (err != RFAL_ERR_NONE) return err;
    rfalRunBlocking(err, rfalGetTransceiveStatus());
    return err;
}

/*============================================================================*/
/**
 * @brief m1_mfc_read_sector - Phase A: authenticate ONE sector with ONE key
 *        and read its four blocks (MIFARE Classic 1K).
 *
 * Uses the single explicit Phase-A test key FFFFFFFFFFFF (Key A) on sector 1.
 * No key scanning, no default-key list, no dictionary, no fallback. On any
 * mismatch/timeout it reports AUTH FAILED cleanly and stops. Read-only.
 */
/*============================================================================*/
/*============================================================================*/
/* MIFARE Classic 1K FULL-CARD read (Crypto1, nested auth).                    */
/* Single configured key FFFFFFFFFFFF tried as Key A and Key B on each of the  */
/* 16 sectors. First auth = plaintext; sector 2+ = nested (encrypted). Honest  */
/* partial results; unread blocks stay unread (never zero-filled).             */
/*============================================================================*/
#define MFC_KEY_A_CMD  0x60U
#define MFC_KEY_B_CMD  0x61U

/* HALT + WUPA + SELECT to reset the card to a fresh (unauthenticated) state
 * after a failed auth/read, so the next attempt uses a clean first-auth. Only
 * exercised on failures; a factory card never needs it. */
static bool mfc_reselect(const rfalNfcDevice *dev)
{
    rfalNfcaSensRes sens;
    rfalNfcaSelRes  sel;
    (void)rfalNfcaPollerSleep();
    if (rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sens) != RFAL_ERR_NONE) return false;
    if (rfalNfcaPollerSelect(dev->nfcid, (uint8_t)dev->nfcidLen, &sel) != RFAL_ERR_NONE) return false;
    return true;
}

#if defined(M1_MFC_AT_RX_DIAG)
/* {At}-window RF-event latches, set by the ST25R3916 IRQ path (st25r3916_irq.c)
 * and reset here immediately before the {At} receive. */
volatile uint8_t g_m1_rdr_rxs = 0U;
volatile uint8_t g_m1_rdr_rxe = 0U;
volatile uint8_t g_m1_rdr_nre = 0U;
#endif

/* One MIFARE authentication (first or nested). On success the cipher `c` is
 * left ready for encrypted block reads. keytype = 0x60 (A) / 0x61 (B). */
static bool mfc_auth(const rfalNfcDevice *dev, uint8_t block, uint8_t keytype,
                     uint64_t key, uint32_t cuid, bool is_nested, Crypto1 *c)
{
    (void)dev;
    uint8_t nt[4];

    if (!is_nested) {
        /* First auth: plaintext AUTH cmd; 4-byte plaintext nonce (no CRC). */
        uint8_t  auth_cmd[2] = { keytype, block };
        uint8_t  rx[8];
        uint16_t rlen  = 0;
        uint32_t flags = (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_AUTO   |
                         (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL |
                         (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP   |
                         (uint32_t)RFAL_TXRX_FLAGS_PAR_TX_AUTO   |
                         (uint32_t)RFAL_TXRX_FLAGS_PAR_RX_REMV   |
                         (uint32_t)RFAL_TXRX_FLAGS_AGC_ON;
        ReturnCode e = rfalTransceiveBlockingTxRx(auth_cmd, 2U, rx, (uint16_t)sizeof(rx), &rlen, flags, MFC_FWT);
        if (!(((e == RFAL_ERR_NONE) || (e == RFAL_ERR_CRC)) && (rlen >= 4U))) return false;
        (void)memcpy(nt, rx, 4);
    } else {
        /* Nested auth: AUTH cmd encrypted with the CURRENT cipher; nonce
         * returns encrypted. */
        uint8_t  cmd[4] = { keytype, block, 0U, 0U };
        uint16_t ccrc   = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, cmd, 2);
        cmd[2] = (uint8_t)(ccrc & 0xFFU);
        cmd[3] = (uint8_t)((ccrc >> 8) & 0xFFU);
        uint8_t enc[4], epar[4];
        for (uint8_t i = 0; i < 4U; i++) enc[i] = crypto1_encrypt_byte(c, cmd[i], &epar[i]);
        uint8_t  cbits[8];
        uint16_t nb = mfc_pack_bits(cbits, enc, epar, 4);
        uint8_t  rb[16];
        uint16_t rbits = 0;
        ReturnCode e = mfc_txrx_bits(cbits, nb, rb, (uint16_t)(sizeof(rb) * 8U), &rbits, MFC_RAW_FLAGS);
        if (!(((e == RFAL_ERR_NONE) || (e == RFAL_ERR_INCOMPLETE_BYTE)) && (rbits >= 36U))) return false;
        uint8_t ntp[4], npar[4];
        if (mfc_unpack_bits(rb, rbits, ntp, npar, 4) < 4U) return false;
        (void)memcpy(nt, ntp, 4);   /* encrypted nonce; recovered inside crypto1_reader_answer */
    }

    /* {Nr}{ar} answer (recovers plaintext nonce; re-keys the cipher). */
    uint8_t  nr[4] = { 0U, 0U, 0U, 0U };
    uint8_t  out[8], opar[8];
    uint32_t nt_plain = crypto1_reader_answer(c, key, cuid, nt, nr, out, opar, is_nested);

    uint8_t  txbits[16];
    uint16_t ntx = mfc_pack_bits(txbits, out, opar, 8);
    uint8_t  rx2[16];
    uint16_t rx2b = 0;
#if defined(M1_MFC_AT_RX_DIAG)
    /* Arm the RF-event latches immediately before the {At} receive window. */
    g_m1_rdr_rxs = 0U; g_m1_rdr_rxe = 0U; g_m1_rdr_nre = 0U;
#endif
    ReturnCode e2 = mfc_txrx_bits(txbits, ntx, rx2, (uint16_t)(sizeof(rx2) * 8U), &rx2b, MFC_RAW_FLAGS);
#if defined(M1_MFC_AT_RX_DIAG)
    /* Reader-side {At} reception truth. RXS (start-of-receive) == subcarrier
     * detected: distinguishes "no modulation reached the reader" (RXS=0, NRE=1,
     * bits=0) from "subcarrier seen but frame not decoded" (RXS=1). MFC_FWT is
     * 10 ms, so a late-but-present response is NOT timed out. Success = RXS or
     * bits>0. */
    {
        static uint8_t  s_atrx_banner = 0U;
        static uint32_t s_atrx_attempt = 0U;
        if (s_atrx_banner == 0U) {
            s_atrx_banner = 1U;   /* build id printed once at NFC-Read start (RDR-POSTAUTH-4) */
        }
        unsigned rb = (unsigned)((rx2b + 7U) / 8U); if (rb > 6U) { rb = 6U; }
        /* rxs is the LATCHED hardware start-of-receive (set in st25r3916_irq.c
         * from the same IRQ read that clears it), NOT inferred from e2. */
        platformLog("[AT-RX] attempt=%lu blk=%u kt=%02X e2=%d bits=%u raw=%02X%02X%02X%02X%02X%02X rxs=%d rxe=%d nre=%d\r\n",
                    (unsigned long)s_atrx_attempt, (unsigned)block, keytype,
                    (int)e2, (unsigned)rx2b,
                    (rb>0)?rx2[0]:0, (rb>1)?rx2[1]:0, (rb>2)?rx2[2]:0,
                    (rb>3)?rx2[3]:0, (rb>4)?rx2[4]:0, (rb>5)?rx2[5]:0,
                    (int)g_m1_rdr_rxs, (int)g_m1_rdr_rxe, (int)g_m1_rdr_nre);
        s_atrx_attempt++;
    }
#endif
    if (!(((e2 == RFAL_ERR_NONE) || (e2 == RFAL_ERR_INCOMPLETE_BYTE)) && (rx2b >= 36U))) return false;

    uint8_t at_enc[4], at_par[4];
    if (mfc_unpack_bits(rx2, rx2b, at_enc, at_par, 4) < 4U) return false;
    uint32_t at = 0;
    for (uint8_t i = 0; i < 4U; i++) {
        uint8_t ksp;
        at = (at << 8) | crypto1_decrypt_byte(c, at_enc[i], &ksp);
    }
    return (at == crypto1_prng_successor(nt_plain, 96U));
}

/* Encrypted READ of one 16-byte block (CRC-verified). c must be post-auth. */
static bool mfc_read_block_enc(Crypto1 *c, uint8_t block, uint8_t out[16])
{
    uint8_t  rd[4] = { 0x30U, block, 0U, 0U };
    uint16_t ccrc  = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, rd, 2);
    rd[2] = (uint8_t)(ccrc & 0xFFU);
    rd[3] = (uint8_t)((ccrc >> 8) & 0xFFU);
    uint8_t enc[4], epar[4];
    for (uint8_t i = 0; i < 4U; i++) enc[i] = crypto1_encrypt_byte(c, rd[i], &epar[i]);
    uint8_t  cbits[8];
    uint16_t nb = mfc_pack_bits(cbits, enc, epar, 4);
    uint8_t  rb[32];
    uint16_t rbits = 0;
#if defined(M1_MFC_AT_RX_DIAG)
    uint32_t rtx_cpu = SystemCoreClock / 1000000U; if (rtx_cpu == 0U) { rtx_cpu = 1U; }
    uint32_t rtx_t0  = DWT->CYCCNT;
#endif
    ReturnCode e = mfc_txrx_bits(cbits, nb, rb, (uint16_t)(sizeof(rb) * 8U), &rbits, MFC_RAW_FLAGS);
#if defined(M1_MFC_AT_RX_DIAG)
    uint32_t rtx_t1 = DWT->CYCCNT;
    /* mfc_txrx_bits is a combined transceive; a RX-phase result (TIMEOUT/CRC/PAR/
     * INCOMPLETE/FRAMING/NONE) means the hardware ACCEPTED and completed TX, so the
     * failure is receive-side -- NOT a transmit failure. Pre-TX errors (WRONG_STATE/
     * PARAM/IO) mean TX never started. txe is inferred on that basis. */
    int rtx_txe = ((e == RFAL_ERR_NONE) || (e == RFAL_ERR_TIMEOUT) || (e == RFAL_ERR_CRC) ||
                   (e == RFAL_ERR_PAR) || (e == RFAL_ERR_INCOMPLETE_BYTE) || (e == RFAL_ERR_FRAMING)) ? 1 : 0;
    /* Honest monolithic-helper telemetry: mfc_txrx_bits does submit+TX+RX and
     * returns ONE code -- so log the real wire buffer (5 packed bytes = 4 ciphertext
     * + 4 custom parity bits = 36 bits), the final_rc, and tx_complete_inferred. No
     * invented split return codes. flags = CRC_TX_MANUAL|PAR_TX_NONE (MFC_RAW_FLAGS). */
    platformLog("[RDR-TX-ENC] block=%u wire=%02X%02X%02X%02X%02X bits=%u flags=%08lX final_rc=%d tx_complete_inferred=%d start_cyc=%lu done_cyc=%lu\r\n",
                (unsigned)block, cbits[0],cbits[1],cbits[2],cbits[3],cbits[4], (unsigned)nb,
                (unsigned long)(uint32_t)MFC_RAW_FLAGS, (int)e, rtx_txe,
                (unsigned long)rtx_t0, (unsigned long)rtx_t1);
    platformLog("[MFC-POSTAUTH] at_ok=1 cmd=30 blk=%u tx_ok=%d\r\n", (unsigned)block,
                (((e == RFAL_ERR_NONE) || (e == RFAL_ERR_INCOMPLETE_BYTE)) && (rbits >= (18U * 9U))) ? 1 : 0);
#endif
    if (!(((e == RFAL_ERR_NONE) || (e == RFAL_ERR_INCOMPLETE_BYTE)) && (rbits >= (18U * 9U)))) return false;
    uint8_t denc[18], dpar[18];
    if (mfc_unpack_bits(rb, rbits, denc, dpar, 18) < 18U) return false;
    uint8_t plain[18];
    for (uint8_t i = 0; i < 18U; i++) {
        uint8_t ksp;
        plain[i] = crypto1_decrypt_byte(c, denc[i], &ksp);
    }
    uint16_t rcrc = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, plain, 16);
    bool crc_ok = (((uint8_t)(rcrc & 0xFFU) == plain[16]) && ((uint8_t)((rcrc >> 8) & 0xFFU) == plain[17]));
#if defined(M1_MFC_AT_RX_DIAG)
    /* POSTAUTH-1 evidence: the decrypted block + CRC result. Parity is implicit in
     * a successful decrypt (reader-mode RFAL verified per-byte parity in hardware). */
    platformLog("[MFC-BLOCK] blk=%u auth=1 read=1 par=1 crc=%d data=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                (unsigned)block, crc_ok ? 1 : 0,
                plain[0],plain[1],plain[2],plain[3],plain[4],plain[5],plain[6],plain[7],
                plain[8],plain[9],plain[10],plain[11],plain[12],plain[13],plain[14],plain[15]);
#endif
    if (!crc_ok) return false;
    (void)memcpy(out, plain, 16);
    return true;
}

/* Decrypt and check the 4-bit MIFARE response nibble that follows each phase of
 * a write. The card sends 4 encrypted bits (no parity); ACK == 0xA, anything
 * else (or a short/absent reply) is a NAK. Clocks the cipher 4 bits so the
 * keystream stays aligned with the card for the next phase. */
static bool mfc_ack_ok(Crypto1 *c, ReturnCode e, const uint8_t *rb, uint16_t rbits)
{
    if (!(((e == RFAL_ERR_NONE) || (e == RFAL_ERR_INCOMPLETE_BYTE)) && (rbits >= 4U)))
        return false;
    uint8_t nib = 0;
    for (uint8_t i = 0; i < 4U; i++) {
        uint8_t enc_bit = (uint8_t)((rb[0] >> i) & 1U);
        uint8_t ks_bit  = (uint8_t)(crypto1_bit(c, 0, 0) & 1U);
        nib |= (uint8_t)((enc_bit ^ ks_bit) << i);
    }
    return (nib == 0x0AU);
}

/* Encrypted WRITE of one 16-byte block. c must be post-auth for this block's
 * sector. Two phases: (1) 0xA0<block>+CRC -> 4-bit ACK, (2) 16 data + CRC ->
 * 4-bit ACK. Returns true only when BOTH phases are ACKed. Read-back
 * verification is the caller's responsibility. */
static bool mfc_write_block_enc(Crypto1 *c, uint8_t block, const uint8_t data[16])
{
    /* ---- Phase 1: write command ---- */
    uint8_t  wc[4] = { 0xA0U, block, 0U, 0U };
    uint16_t wcrc = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, wc, 2);
    wc[2] = (uint8_t)(wcrc & 0xFFU);
    wc[3] = (uint8_t)((wcrc >> 8) & 0xFFU);
    uint8_t enc[4], epar[4];
    for (uint8_t i = 0; i < 4U; i++) enc[i] = crypto1_encrypt_byte(c, wc[i], &epar[i]);
    uint8_t  cbits[8];
    uint16_t nb = mfc_pack_bits(cbits, enc, epar, 4);
    uint8_t  rb1[4];
    uint16_t rb1n = 0;
    ReturnCode e1 = mfc_txrx_bits(cbits, nb, rb1, (uint16_t)(sizeof(rb1) * 8U), &rb1n, MFC_RAW_FLAGS);
    if (!mfc_ack_ok(c, e1, rb1, rb1n)) return false;

    /* ---- Phase 2: 16 data bytes + CRC ---- */
    uint8_t payload[18];
    (void)memcpy(payload, data, 16);
    uint16_t dcrc = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, payload, 16);
    payload[16] = (uint8_t)(dcrc & 0xFFU);
    payload[17] = (uint8_t)((dcrc >> 8) & 0xFFU);
    uint8_t denc[18], dpar[18];
    for (uint8_t i = 0; i < 18U; i++) denc[i] = crypto1_encrypt_byte(c, payload[i], &dpar[i]);
    uint8_t  dbits[24];
    uint16_t ndb = mfc_pack_bits(dbits, denc, dpar, 18);
    uint8_t  rb2[4];
    uint16_t rb2n = 0;
    ReturnCode e2 = mfc_txrx_bits(dbits, ndb, rb2, (uint16_t)(sizeof(rb2) * 8U), &rb2n, MFC_RAW_FLAGS);
    if (!mfc_ack_ok(c, e2, rb2, rb2n)) return false;

    return true;
}

/* Auth wrapper: nested if already authed, else first-auth; re-selects the card
 * after a failure so the next attempt is a clean first-auth. */
static bool mfc_do_auth(const rfalNfcDevice *dev, uint8_t block, uint8_t keytype,
                        uint64_t key, uint32_t cuid, Crypto1 *c, bool *authed)
{
    if (mfc_auth(dev, block, keytype, key, cuid, *authed, c)) {
        *authed = true;
        return true;
    }
    (void)mfc_reselect(dev);
    *authed = false;
    return false;
}

/* Read the blocks of a sector into the shared dump buffer with the just-authed
 * cipher. `first` = absolute first block, `nblocks` = 4 (small) or 16 (big). */
static void mfc_read_sector_blocks(const rfalNfcDevice *dev, uint16_t first,
                                   uint8_t nblocks, Crypto1 *c, bool *authed)
{
    for (uint8_t b = 0; b < nblocks; b++) {
        uint16_t blk = (uint16_t)(first + b);
        if (nfc_ctx_mfc_block_valid(blk)) continue;
        uint8_t data[16];
        if (mfc_read_block_enc(c, (uint8_t)blk, data)) {
            nfc_ctx_mfc_store_block(blk, data);
        } else {
            (void)mfc_reselect(dev);   /* a failed read halts the card */
            *authed = false;
            break;
        }
    }
}

/*============================================================================*/
/* MIFARE Classic nested-nonce HARVESTER (Increment 3a, CLI-armed).            */
/*                                                                            */
/* This CLI-armed function only COLLECTS the encrypted target nonces +        */
/* parity -- it reuses the existing mfc_auth / mfc_reselect / raw-frame       */
/* helpers above, no RFAL changes. Recovery is a SEPARATE, later step:        */
/* on-device dictionary solve now exists (mfc_dict_solver.c, driven by        */
/* nfc_solve_run() via Q_EVENT_NFC_SOLVE -- see below) alongside the still-    */
/* real off-device option of feeding the .m1h capture to an external tool.    */
/* (Note: as of this writing the on-device solve/harvest UI's own menu entry  */
/* has been removed pending a UI redesign -- see m1_menu.c's "MFC Recovery"   */
/* removal commit -- but the harvest/solve implementation itself, and this    */
/* comment's correction, are independent of that.)                           */
/*============================================================================*/

void nfc_harvest_arm(uint8_t src_block, uint8_t src_keytype,
                     uint8_t tgt_block, uint8_t tgt_keytype,
                     uint8_t samples, uint64_t known_key)
{
    g_harvest_req.src_block   = src_block;
    g_harvest_req.src_keytype = src_keytype;
    g_harvest_req.tgt_block   = tgt_block;
    g_harvest_req.tgt_keytype = tgt_keytype;
    g_harvest_req.samples     = (samples == 0U) ? 1U :
                               ((samples > HARVEST_MAX_SAMPLES) ? HARVEST_MAX_SAMPLES : samples);
    g_harvest_req.known_key   = known_key;
    __DMB();                          /* publish fields before the pending flag */
    g_harvest_req.pending     = 1U;   /* NFC task services on next MFC activation */
}

/* Enable the DWT cycle counter (coarse timing for the nested exchange). */
static void harvest_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/*
 * Capture ONE nested target nonce after a fresh first-auth to the source sector
 * with the known key. Sends the target AUTH encrypted with the source cipher and
 * reads back the encrypted nonce + parity; it does NOT complete the target auth
 * (its key is unknown). Returns false on any RF/auth failure.
 */
static bool mfc_harvest_capture_one(const rfalNfcDevice *dev, uint32_t cuid,
                                    mfc_nested_sample_t *out)
{
    Crypto1 c;
    (void)mfc_reselect(dev);
    if (!mfc_auth(dev, g_harvest_req.src_block, g_harvest_req.src_keytype,
                  g_harvest_req.known_key, cuid, false, &c)) {
        return false;                 /* wrong known key / bad source sector */
    }

    uint8_t  cmd[4] = { g_harvest_req.tgt_keytype, g_harvest_req.tgt_block, 0U, 0U };
    uint16_t ccrc   = rfalCrcCalculateCcitt(MFC_CRCA_PRELOAD, cmd, 2);
    cmd[2] = (uint8_t)(ccrc & 0xFFU);
    cmd[3] = (uint8_t)((ccrc >> 8) & 0xFFU);

    uint8_t enc[4], epar[4];
    for (uint8_t i = 0; i < 4U; i++) enc[i] = crypto1_encrypt_byte(&c, cmd[i], &epar[i]);

    uint8_t  cbits[8];
    uint16_t nb = mfc_pack_bits(cbits, enc, epar, 4);
    uint8_t  rb[16];
    uint16_t rbits = 0;

    uint32_t t0 = DWT->CYCCNT;
    ReturnCode e = mfc_txrx_bits(cbits, nb, rb, (uint16_t)(sizeof(rb) * 8U), &rbits, MFC_RAW_FLAGS);
    uint32_t t1 = DWT->CYCCNT;
    if (!(((e == RFAL_ERR_NONE) || (e == RFAL_ERR_INCOMPLETE_BYTE)) && (rbits >= 36U))) return false;

    uint8_t ntp[4], npar[4];
    if (mfc_unpack_bits(rb, rbits, ntp, npar, 4) < 4U) return false;

    out->nt_enc = ((uint32_t)ntp[0] << 24) | ((uint32_t)ntp[1] << 16) |
                  ((uint32_t)ntp[2] << 8)  |  (uint32_t)ntp[3];
    out->par    = (uint8_t)((npar[0] & 1U) | ((npar[1] & 1U) << 1) |
                            ((npar[2] & 1U) << 2) | ((npar[3] & 1U) << 3));
    /* Coarse DWT delta around the nested exchange. NOTE: this is NOT yet a
     * calibrated PRNG nonce-distance (that needs HW timing tuning); stored as a
     * diagnostic for the host to correlate. */
    out->dist   = (uint16_t)(t1 - t0);
    return true;
}

/*
 * Serviced by ReadCycle when a harvest is armed and a MIFARE Classic card is
 * active: collect samples and write one .m1h Nested record to SD. One-shot.
 */
static void m1_mfc_harvest_run(const rfalNfcDevice *dev)
{
    g_harvest_req.pending = 0U;        /* consume the request */
    if (dev == NULL) { platformLog("[HARVEST] no device\r\n"); return; }

    harvest_dwt_init();

    uint8_t        uidlen = (uint8_t)dev->nfcidLen;
    const uint8_t *uid    = dev->nfcid;
    uint32_t       cuid   = 0;
    if (uidlen >= 4U) {
        const uint8_t *u = &uid[uidlen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }
    const uint8_t sak   = dev->dev.nfca.selRes.sak;
    const uint8_t atqa0 = dev->dev.nfca.sensRes.anticollisionInfo;
    const uint8_t atqa1 = dev->dev.nfca.sensRes.platformInfo;

    /* collect what the card gives us (retry each sample once) */
    mfc_nested_sample_t samples[HARVEST_MAX_SAMPLES];
    uint8_t got = 0;
    for (uint8_t i = 0; i < g_harvest_req.samples; i++) {
        mfc_nested_sample_t s;
        bool ok = false;
        for (uint8_t r = 0; r < 2U && !ok; r++) ok = mfc_harvest_capture_one(dev, cuid, &s);
        if (ok) samples[got++] = s;
    }
    if (got == 0U) {
        platformLog("[HARVEST] 0 samples - check known key (Key A FFFFFFFFFFFF) / src sector\r\n");
        return;
    }

    /* write one Nested record to SD (path 0:/NFC/recover/<uid>_<ticks>.m1h) */
    uint32_t ticks = HAL_GetTick();          /* one timestamp for filename + header */
    int r = mfc_harvest_storage_start(&g_harvest_st, uid, uidlen, ticks);
    if (r != MFC_HS_OK) { platformLog("[HARVEST] SD open failed (%d)\r\n", r); return; }

    mfc_harvest_init(&g_harvest_h, mfc_harvest_storage_flush, &g_harvest_st);

    mfc_harvest_file_info_t info;
    memset(&info, 0, sizeof info);
    info.uid_len = uidlen;
    for (uint8_t i = 0; i < uidlen && i < 10U; i++) info.uid[i] = uid[i];
    info.atqa            = (uint16_t)(atqa0 | ((uint16_t)atqa1 << 8));
    info.sak             = sak;
    info.capture_ticks   = ticks;
    info.capability_flags = 0;
    info.record_count    = 1;
    mfc_harvest_begin_file(&g_harvest_h, &info);

    mfc_card_only_hdr_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.src_block   = g_harvest_req.src_block;   hdr.src_keytype = g_harvest_req.src_keytype;
    hdr.tgt_block   = g_harvest_req.tgt_block;   hdr.tgt_keytype = g_harvest_req.tgt_keytype;
    hdr.known_key_ref = 0;                       /* host supplies the known key */
    /* anchor_nt = 0: the source plaintext nonce anchor is deferred to the
     * HW-timing-calibration step (see mfc_harvest_capture_one note on dist). */
    mfc_harvest_begin_nested(&g_harvest_h, &hdr, 0, got);
    for (uint8_t i = 0; i < got; i++) mfc_harvest_add_nested_sample(&g_harvest_h, &samples[i]);
    mfc_harvest_end_record(&g_harvest_h);

    mfc_harvest_status_t hs = mfc_harvest_finalize(&g_harvest_h);
    (void)mfc_harvest_storage_close(&g_harvest_st);

    if (hs != MFC_HARVEST_OK) { platformLog("[HARVEST] serialize failed (%d)\r\n", (int)hs); return; }
    platformLog("[HARVEST] wrote %s (%u nested samples)\r\n", g_harvest_st.path, (unsigned)got);
}

/*
 * UI-driven harvest (Increment 3b): worker-run one-shot. Owns its own card
 * discovery/activation (like mfc_dict_scan), updates nfc_harvest_ui_t for the
 * harvest view to poll, and is abortable via nfc_poller_harvest_abort().
 * Target/samples come from g_harvest_req (set by nfc_harvest_set_target()).
 */
void nfc_harvest_scan_run(void)
{
    nfc_harvest_ui_t *hv = nfc_ctx_get_harvest();
    hv->state      = NFC_HARVEST_WAIT_CARD;
    hv->tgt_sector = (uint8_t)(g_harvest_req.tgt_block / 4U);
    hv->want       = g_harvest_req.samples;
    hv->got        = 0;
    hv->path[0]    = '\0';

    harvest_dwt_init();

    /* 1) discover + activate a card (abortable) */
    rfalNfcDevice *dev = NULL;
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_harvest_scan_abort) {
            hv->state = NFC_HARVEST_STOPPED;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }

    /* 2) require NFC-A MIFARE Classic (SAK 0x08 = 1K) */
    if ((dev == NULL) || (dev->type != RFAL_NFC_LISTEN_TYPE_NFCA) ||
        (dev->dev.nfca.selRes.sak != 0x08U)) {
        hv->state = NFC_HARVEST_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    uint8_t        uidlen = (uint8_t)dev->nfcidLen;
    const uint8_t *uid    = dev->nfcid;
    uint32_t       cuid   = 0;
    if (uidlen >= 4U) {
        const uint8_t *u = &uid[uidlen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }
    const uint8_t sak   = dev->dev.nfca.selRes.sak;
    const uint8_t atqa0 = dev->dev.nfca.sensRes.anticollisionInfo;
    const uint8_t atqa1 = dev->dev.nfca.sensRes.platformInfo;

    hv->state = NFC_HARVEST_RUNNING;

    /* 3) capture samples (abortable, retry each once) */
    mfc_nested_sample_t samples[HARVEST_MAX_SAMPLES];
    uint8_t got = 0;
    for (uint8_t i = 0; i < g_harvest_req.samples; i++) {
        if (s_harvest_scan_abort) break;
        mfc_nested_sample_t s;
        bool ok = false;
        for (uint8_t r = 0; r < 2U && !ok; r++) ok = mfc_harvest_capture_one(dev, cuid, &s);
        if (ok) { samples[got++] = s; hv->got = got; }
    }
    if (got == 0U) {
        hv->state = s_harvest_scan_abort ? NFC_HARVEST_STOPPED : NFC_HARVEST_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    /* 4) write one Nested record to SD */
    uint32_t ticks = HAL_GetTick();
    int r = mfc_harvest_storage_start(&g_harvest_st, uid, uidlen, ticks);
    if (r != MFC_HS_OK) {
        hv->state = NFC_HARVEST_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }
    mfc_harvest_init(&g_harvest_h, mfc_harvest_storage_flush, &g_harvest_st);
    mfc_harvest_file_info_t info;
    memset(&info, 0, sizeof info);
    info.uid_len = uidlen;
    for (uint8_t i = 0; i < uidlen && i < 10U; i++) info.uid[i] = uid[i];
    info.atqa            = (uint16_t)(atqa0 | ((uint16_t)atqa1 << 8));
    info.sak             = sak;
    info.capture_ticks   = ticks;
    info.capability_flags = 0;
    info.record_count    = 1;
    mfc_harvest_begin_file(&g_harvest_h, &info);
    mfc_card_only_hdr_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.src_block   = g_harvest_req.src_block; hdr.src_keytype = g_harvest_req.src_keytype;
    hdr.tgt_block   = g_harvest_req.tgt_block; hdr.tgt_keytype = g_harvest_req.tgt_keytype;
    hdr.known_key_ref = 0;
    mfc_harvest_begin_nested(&g_harvest_h, &hdr, 0, got);
    for (uint8_t i = 0; i < got; i++) mfc_harvest_add_nested_sample(&g_harvest_h, &samples[i]);
    mfc_harvest_end_record(&g_harvest_h);
    mfc_harvest_status_t hs = mfc_harvest_finalize(&g_harvest_h);
    (void)mfc_harvest_storage_close(&g_harvest_st);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);

    if (hs != MFC_HARVEST_OK) { hv->state = NFC_HARVEST_FAIL; return; }

    /* success: expose the path to the UI */
    uint8_t pi = 0;
    for (; pi < (uint8_t)(sizeof(hv->path) - 1U) && g_harvest_st.path[pi] != '\0'; pi++)
        hv->path[pi] = g_harvest_st.path[pi];
    hv->path[pi] = '\0';
    platformLog("[HARVEST] wrote %s (%u nested samples)\r\n", hv->path, (unsigned)got);
    hv->state = NFC_HARVEST_DONE;
}

/* --- MFC Recovery solve: nested-DICTIONARY key recovery + RF verification ----
 * Reads the last capture's .m1h from SD, runs the dictionary
 * filter (crypto1_recover primitives via mfc_dict_solver) over the mfc_keys
 * dictionary, then VERIFIES a candidate by authenticating the target sector
 * against the physical card. Worker-run; watchdog serviced + abortable. */
extern void m1_wdt_reset(void);
static uint8_t s_solve_filebuf[1024];   /* .m1h read buffer (nested records are small) */

static bool solve_key_iter(void *ctx, uint8_t key[6]) {
    return mfc_keys_iter_next((mfc_keys_iter_t *)ctx, key);
}
static bool solve_progress(void *ctx, uint32_t tried) {
    (void)ctx; (void)tried;
    m1_wdt_reset();
    osDelay(1);                          /* yield so the UI can poll status */
    return !s_solve_abort;
}
static mfc_solve_status_t solve_with_dict(const mfc_solver_capture_t *cap,
                                          uint64_t src_key, uint64_t *out_key) {
    for (int weak = 1; weak >= 0; weak--) {   /* weak filter first, then parity-only */
        mfc_keys_iter_t it;
        mfc_keys_iter_begin(&it, NULL);
        mfc_solve_status_t st = mfc_solver_run(cap, solve_key_iter, &it, src_key,
                                               (bool)weak, out_key, solve_progress, NULL);
        mfc_keys_iter_end(&it);
        if (st != MFC_SOLVE_NO_KEY) return st;
    }
    return MFC_SOLVE_NO_KEY;
}

void nfc_solve_run(void)
{
    nfc_solve_ui_t   *sv = nfc_ctx_get_solve();
    nfc_harvest_ui_t *hv = nfc_ctx_get_harvest();
    sv->state = NFC_SOLVE_UI_SOLVING;
    sv->tgt_sector = 0; sv->tgt_keytype = 0x60U; (void)memset(sv->key, 0, 6);

    /* 1) read the last capture's .m1h from SD */
    if (hv->path[0] == '\0') { sv->state = NFC_SOLVE_UI_ERROR; return; }
    FIL f; UINT br = 0;
    if (f_open(&f, hv->path, FA_READ) != FR_OK) { sv->state = NFC_SOLVE_UI_ERROR; return; }
    FRESULT fr = f_read(&f, s_solve_filebuf, sizeof(s_solve_filebuf), &br);
    (void)f_close(&f);
    if (fr != FR_OK || br == 0U) { sv->state = NFC_SOLVE_UI_ERROR; return; }

    /* 2) parse (authoritative versioned parser) */
    mfc_solver_capture_t cap;
    mfc_solve_status_t pst = mfc_solver_parse_m1h(s_solve_filebuf, (size_t)br, &cap);
    if (pst != MFC_SOLVE_OK) {
        sv->state = (pst == MFC_SOLVE_INSUFFICIENT) ? NFC_SOLVE_UI_INSUFFICIENT
                                                    : NFC_SOLVE_UI_UNSUPPORTED;
        return;
    }
    if (cap.count == 0) { sv->state = NFC_SOLVE_UI_INSUFFICIENT; return; }

    /* 3) dictionary solve (skip the source key FFFFFFFFFFFF -> never returned) */
    uint64_t found = 0;
    mfc_solve_status_t rst = solve_with_dict(&cap, 0xFFFFFFFFFFFFULL, &found);
    if (rst == MFC_SOLVE_ABORTED)   { sv->state = NFC_SOLVE_UI_STOPPED;   return; }
    if (rst == MFC_SOLVE_AMBIGUOUS) { sv->state = NFC_SOLVE_UI_AMBIGUOUS; return; }
    if (rst != MFC_SOLVE_OK)        { sv->state = NFC_SOLVE_UI_NO_KEY;    return; }

    /* 4) RF verify: activate a card and authenticate the target sector */
    sv->state = NFC_SOLVE_UI_VERIFYING;
    rfalNfcDevice *dev = NULL;
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        rfalNfcWorker();
        m1_wdt_reset();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_solve_abort) { sv->state = NFC_SOLVE_UI_STOPPED; rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return; }
        if ((HAL_GetTick() - t0) > 8000U) { sv->state = NFC_SOLVE_UI_CARD_LOST; rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return; }
        osDelay(5);
    }
    if (dev == NULL || dev->type != RFAL_NFC_LISTEN_TYPE_NFCA) {
        sv->state = NFC_SOLVE_UI_CARD_LOST; rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE); return;
    }
    uint32_t cuid = 0;
    { uint8_t ul = (uint8_t)dev->nfcidLen;
      if (ul >= 4U) { const uint8_t *u = &dev->nfcid[ul - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3]; } }
    Crypto1 c;
    (void)mfc_reselect(dev);
    bool ok = mfc_auth(dev, cap.tgt_block, cap.tgt_keytype, found, cuid, false, &c);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    if (!ok) { sv->state = NFC_SOLVE_UI_CARD_LOST; return; }   /* did not verify on this card */

    /* 5) verified */
    sv->tgt_sector  = (uint8_t)(cap.tgt_block / 4U);
    sv->tgt_keytype = cap.tgt_keytype;
    sv->key[0]=(uint8_t)(found>>40); sv->key[1]=(uint8_t)(found>>32); sv->key[2]=(uint8_t)(found>>24);
    sv->key[3]=(uint8_t)(found>>16); sv->key[4]=(uint8_t)(found>>8);  sv->key[5]=(uint8_t)found;
    platformLog("[SOLVE] verified key sector %u %c: %02X%02X%02X%02X%02X%02X\r\n",
                (unsigned)sv->tgt_sector, (cap.tgt_keytype==0x61U)?'B':'A',
                sv->key[0],sv->key[1],sv->key[2],sv->key[3],sv->key[4],sv->key[5]);
    sv->state = NFC_SOLVE_UI_FOUND;
}

/*============================================================================*/
/**
 * @brief m1_mfc_read_card - Full-card MIFARE Classic 1K read.
 *
 * Tries the configured key FFFFFFFFFFFF as Key A and Key B on each of the 16
 * sectors (first auth on sector 0, nested thereafter), reads every accessible
 * block, and tallies honest "Keys found: X/32" and "Sectors read: X/16"
 * (block-coverage). A key slot counts only after a successful authentication.
 * Unread blocks stay unread.
 */
/*============================================================================*/
static void m1_mfc_read_card(const rfalNfcDevice *dev)
{
    nfc_ctx_clear_mfc();
    nfc_ctx_clear_t2t_ndef();
    nfc_ctx_clear_dump();
    nfc_ctx_set_t2t_variant(M1NFC_T2TVAR_UNKNOWN);

    if (dev == NULL) return;

    uint8_t  sak     = dev->dev.nfca.selRes.sak;
    uint8_t  mfctype = m1nfc_mfc_type_from_sak(sak);
    uint16_t sectors_total = m1nfc_mfc_sectors_for_type(mfctype);
    uint16_t total_blocks  = m1nfc_mfc_blocks_for_type(mfctype);

    nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    mfc->valid         = true;
    mfc->type          = mfctype;
    mfc->sectors_total = (uint8_t)sectors_total;
    mfc->keys_total    = (uint8_t)(sectors_total * 2U);
    mfc->keys_found    = 0;
    mfc->sectors_read  = 0;

    /* Block storage lives in the shared dump buffer (unit_size=16, abs block). */
    (void)memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
    (void)memset(g_nfc_valid_bits, 0x00, NFC_DUMP_MAX_UNITS / 8U);
    nfc_ctx_set_dump(M1NFC_MFC_BLOCK_SZ, total_blocks, 0,
                     g_nfc_dump_buf, g_nfc_valid_bits, total_blocks, true);

    uint8_t        uidlen = (uint8_t)dev->nfcidLen;
    const uint8_t *uid    = dev->nfcid;
    uint32_t       cuid   = 0;
    if (uidlen >= 4U) {
        const uint8_t *u = &uid[uidlen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }

    const uint64_t key    = 0xFFFFFFFFFFFFULL;   /* configured key, tried as A and B */
    Crypto1        c;
    bool           authed = false;

#if defined(M1_MFC_AT_RX_DIAG)
    /* RDR-POSTAUTH-2: focused one-shot. Use the FIRST cryptographically successful
     * first-auth, then IMMEDIATELY issue one encrypted READ on that same block and
     * continuing Crypto1 state (no HALT/WUPA/reselect/next-sector), then stop. Does
     * NOT hardcode an unproven block/key -- it reads whatever authenticated. */
    platformLog("[BUILD] rdr id=RDR-POSTAUTH-4 git=2664bc6 dirty=1 utc=%s %s\r\n", __DATE__, __TIME__);
    {
        Crypto1 pc;
        for (uint8_t s = 0; s < mfc->sectors_total; s++) {
            uint16_t fb = m1nfc_mfc_sector_first_block(s);
            for (uint8_t kt = 0; kt < 2U; kt++) {
                uint8_t keytype = (kt == 0U) ? MFC_KEY_A_CMD : MFC_KEY_B_CMD;
                if (mfc_auth(dev, (uint8_t)fb, keytype, key, cuid, false, &pc)) {
                    platformLog("[RDR-POSTAUTH-4] auth_ok=1 blk=%u kt=%02X key_idx=0 read_call=1\r\n",
                                (unsigned)fb, keytype);
                    uint8_t data[16];
                    bool rd = mfc_read_block_enc(&pc, (uint8_t)fb, data);   /* logs [RDR-TX-ENC]/[MFC-POSTAUTH]/[MFC-BLOCK] */
                    platformLog("[RDR-POSTAUTH-4] done blk=%u read=%d\r\n", (unsigned)fb, (int)rd);
                    return;
                }
                (void)mfc_reselect(dev);   /* failed auth -> clean re-select before next try */
            }
        }
        platformLog("[RDR-POSTAUTH-4] auth_ok=0 (no sector authenticated)\r\n");
        return;
    }
#endif

    for (uint8_t s = 0; s < mfc->sectors_total; s++) {
        nfc_mfc_sector_t *S = &mfc->sec[s];
        uint16_t first   = m1nfc_mfc_sector_first_block(s);
        uint8_t  nblocks = m1nfc_mfc_sector_blocks(s);

        /* Key A: count the slot on successful auth, then read the sector. */
        if (mfc_do_auth(dev, (uint8_t)first, MFC_KEY_A_CMD, key, cuid, &c, &authed)) {
            S->key_a_found = true;
            (void)memset(S->key_a, 0xFF, 6);
            mfc->keys_found++;
            mfc_read_sector_blocks(dev, first, nblocks, &c, &authed);
        }

        /* Key B: count the slot on successful auth, read any block Key A missed. */
        if (mfc_do_auth(dev, (uint8_t)first, MFC_KEY_B_CMD, key, cuid, &c, &authed)) {
            S->key_b_found = true;
            (void)memset(S->key_b, 0xFF, 6);
            mfc->keys_found++;
            mfc_read_sector_blocks(dev, first, nblocks, &c, &authed);
        }

        /* Sector read (block-coverage): all blocks obtained. */
        bool all = true;
        for (uint8_t b = 0; b < nblocks; b++) {
            if (!nfc_ctx_mfc_block_valid((uint16_t)(first + b))) { all = false; break; }
        }
        if (all) mfc->sectors_read++;

        platformLog("[MFC] sec %u: A=%d B=%d\r\n",
                    s, (int)S->key_a_found, (int)S->key_b_found);
    }

    /* Dictionary phase (Step 3 of the MFC key-source migration): only when
     * the fast/default-key pass above left slots unresolved. Preserves
     * everything the fast pass already proved (the acquisition core skips
     * any slot with key_a_found/key_b_found already set) and seeds
     * FFFFFFFFFFFF as already-tried so it is never proposed again against a
     * slot it has already, provably, failed on. Uses the SAME canonical
     * [USER, SYSTEM] source order and abort flag as Tools > Dictionary Scan
     * (mfc_dict_scan) -- one source chain, one cancellation mechanism,
     * shared by both entry points; the NFC worker task is single-threaded,
     * so only one of them is ever actually running at a time. */
    if (mfc->keys_found < mfc->keys_total) {
        nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
        sc->state       = NFC_SCAN_RUNNING;
        sc->cur_sector  = 0;
        sc->cur_keytype = 0;
        sc->cur_source  = NFC_SCAN_SRC_USER;
        sc->sectors_acc = 0;
        sc->keys_tried  = 0;
        sc->found       = 0;
        for (uint8_t s = 0; s < mfc->sectors_total; s++) {   /* seed progress from the fast pass */
            if (mfc->sec[s].key_a_found) sc->found++;
            if (mfc->sec[s].key_b_found) sc->found++;
            if (mfc->sec[s].key_a_found || mfc->sec[s].key_b_found) sc->sectors_acc++;
        }
        s_mfc_scan_abort = false;        /* fresh cancellation window for this read's dictionary phase */
        s_mfc_scan_skip_source = false;  /* fresh Skip window, likewise */

        const char           *user_paths[2], *sys_paths[2];
        mfc_key_source_cfg_t  cfgs[2];
        m1_mfc_build_key_sources(cfgs, user_paths, sys_paths);
        static const uint8_t s_fast_default_key[MFC_KEY_SIZE] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

        if (!m1_mfc_acquire_dict_phase(dev, cuid, cfgs, 2, s_fast_default_key, mfc, sc, &s_mfc_scan_abort, NULL,
                                       &s_mfc_scan_skip_source)) {
            /* Defensive only: mfc_dict_phase_run() now retries a lost card
             * indefinitely (see mfc_dict_phase.h) rather than giving up, so
             * this branch should not be reachable in ordinary operation --
             * kept as a belt-and-suspenders fallback, not removed. */
            platformLog("[MFC] dictionary phase: unexpected false return\r\n");
            mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                                mfc->keys_total, mfc->keys_found, false);
            return;
        }
        sc->state = s_mfc_scan_abort ? NFC_SCAN_STOPPED : NFC_SCAN_DONE;

        /* Recompute sectors_read (block coverage): the dictionary phase may
         * have completed sectors the fast pass left partially resolved. */
        mfc->sectors_read = 0;
        for (uint8_t s = 0; s < mfc->sectors_total; s++) {
            uint16_t first   = m1nfc_mfc_sector_first_block(s);
            uint8_t  nblocks = m1nfc_mfc_sector_blocks(s);
            bool all = true;
            for (uint8_t b = 0; b < nblocks; b++) {
                if (!nfc_ctx_mfc_block_valid((uint16_t)(first + b))) { all = false; break; }
            }
            if (all) mfc->sectors_read++;
        }
        platformLog("[MFC] dictionary phase %s: keys_tried=%lu found=%u/%u sectors=%u/%u\r\n",
                    s_mfc_scan_abort ? "stopped" : "done",
                    (unsigned long)sc->keys_tried, mfc->keys_found, mfc->keys_total,
                    mfc->sectors_read, mfc->sectors_total);
        mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                            mfc->keys_total, mfc->keys_found, s_mfc_scan_abort);
    } else {
        /* Fast pass alone already resolved every slot -- no dictionary phase
         * needed, so no cancellation is possible here. */
        mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                            mfc->keys_total, mfc->keys_found, false);
    }

    platformLog("[MFC] full-card done: keys=%u/%u sectors=%u/%u outcome=%d\r\n",
                mfc->keys_found, mfc->keys_total, mfc->sectors_read, mfc->sectors_total,
                (int)mfc->outcome);
}

/*============================================================================*/
/* Canonical MFC acquisition core (Step 2 of the MFC key-source migration):    */
/* key-major scan of the given dictionary sources against an ALREADY-         */
/* ACTIVATED card, recording RF-verified keys into the canonical              */
/* nfc_mfc_info_t. The caller has already prepared `mfc` (valid/type/         */
/* sectors_total/keys_total/dump buffer, and any slots it wants preserved     */
/* already marked found) and `sc` (state/counters -- typically freshly reset  */
/* or seeded to reflect mfc's current found slots). Every auth is a           */
/* first-auth from a freshly re-selected card (mfc_do_auth re-selects on      */
/* failure; a re-select follows each success too) so no nested-auth state     */
/* leaks between independent candidates. Checks *abort_flag between RF        */
/* operations -- already-proven slots are preserved on abort. Returns false   */
/* only if the card was lost mid-scan (sc->state is set to NFC_SCAN_CARD_LOST */
/* before returning); true otherwise (ran to exhaustion, to full resolution,  */
/* or was aborted -- the caller distinguishes those via sc->found / *abort_flag */
/* after the call, exactly as before this refactor). Never writes.            */
/*                                                                             */
/* `already_tried`, if non-NULL, marks that one 6-byte key as already yielded */
/* before the scan starts -- for a caller (normal Read's fast/default pass)   */
/* that tried it outside this function, so the dictionary sources that follow */
/* do not propose it again against slots it has already, provably, failed on. */
/*                                                                             */
/* The key-major orchestration itself lives in mfc_dict_phase.c (RF/crypto    */
/* injected via mfc_dict_phase_ops_t, no RFAL/Crypto1 dependency of its own,  */
/* so a host test can drive it directly against mock ops). This function is  */
/* now a thin production wrapper: it supplies ops backed by the real,        */
/* completely unchanged mfc_reselect()/mfc_do_auth()/mfc_read_sector_blocks()  */
/* below, and owns the one Crypto1 cipher session for the whole call exactly  */
/* as before this seam existed.                                               */
/*============================================================================*/
static bool dict_phase_prod_reselect(void *dev)
{
    return mfc_reselect((const rfalNfcDevice *)dev);
}
static bool dict_phase_prod_auth(void *dev, void *crypto_ctx, uint8_t block, uint8_t keytype,
                                 uint64_t key, uint32_t cuid, bool *authed)
{
    return mfc_do_auth((const rfalNfcDevice *)dev, block, keytype, key, cuid,
                       (Crypto1 *)crypto_ctx, authed);
}
static void dict_phase_prod_read_sector(void *dev, void *crypto_ctx, uint16_t first,
                                        uint8_t nblocks, bool *authed)
{
    mfc_read_sector_blocks((const rfalNfcDevice *)dev, first, nblocks, (Crypto1 *)crypto_ctx, authed);
}

static bool m1_mfc_acquire_dict_phase(const rfalNfcDevice *dev, uint32_t cuid,
                                      const mfc_key_source_cfg_t *sources, size_t n_sources,
                                      const uint8_t already_tried[MFC_KEY_SIZE],
                                      nfc_mfc_info_t *mfc, nfc_mfc_scan_t *sc,
                                      const volatile bool *abort_flag,
                                      mfc_dict_resume_t *resume,
                                      volatile bool *skip_source_flag)
{
    Crypto1 c;
    mfc_dict_phase_ops_t ops = {
        .dev = (void *)dev,
        .crypto_ctx = &c,
        .reselect = dict_phase_prod_reselect,
        .auth = dict_phase_prod_auth,
        .read_sector = dict_phase_prod_read_sector,
    };
    /* dict_phase_prod_reselect() (above) wraps mfc_reselect(), which was
     * ALREADY, before this increment, RFAL-level UID-targeted -- it does
     * WUPA (wake any card) then SELECT against this SPECIFIC dev->nfcid
     * (rfalNfcaPollerSelect(), an ISO14443-3A anticollision cascade that
     * only completes for a card whose own UID matches the one being
     * selected). A different card presented during a card-lost retry
     * window therefore already cannot complete this reselect and is
     * refused for free, with zero new identity-comparison code and zero
     * touch to RF/card-presentation semantics -- exactly the guarantee
     * mfc_dict_phase_run()'s card-loss retry loop (mfc_dict_phase.c)
     * relies on its caller to provide. */
    return mfc_dict_phase_run(sources, n_sources, already_tried, cuid, &ops, mfc, sc, abort_flag, resume,
                              skip_source_flag);
}

/* Builds the canonical [USER, SYSTEM] source list every MFC dictionary
 * consumer shares -- normal Read, Tools > Dictionary Scan, and the MIFARE
 * Classic Keys UI's system/user counts -- so path precedence and source
 * order can never drift between entry points. User keys are tried before
 * system keys: they are what an operator explicitly told M1 about this
 * card, a stronger prior than a generic public/community default.
 * `user_paths`/`sys_paths` are caller-owned 2-entry storage that must
 * outlive however long `cfgs` is used. Public (see nfc_poller.h) so the UI
 * layer can build the exact same source list without a second
 * implementation. */
void m1_mfc_build_key_sources(mfc_key_source_cfg_t cfgs[2],
                              const char *user_paths[2], const char *sys_paths[2])
{
    user_paths[0] = MFC_KEYS_USER_PATH;
    user_paths[1] = MFC_KEYS_USER_PATH_COMPAT;
    cfgs[0].kind      = MFC_KEY_SRC_USER;
    cfgs[0].builtin   = NULL;
    cfgs[0].builtin_n = 0;
    cfgs[0].paths     = user_paths;
    cfgs[0].n_paths   = 2;
    cfgs[0].prober    = mfc_key_source_sd_probe;
    cfgs[0].opener    = mfc_key_source_sd_open;
    cfgs[0].io_ctx    = NULL;
    cfgs[0].accumulate = true;   /* USER: bounded by MFC_KEYS_MAX */

    sys_paths[0] = MFC_KEYS_SYSTEM_PATH;
    sys_paths[1] = MFC_KEYS_SYSTEM_PATH_COMPAT;
    cfgs[1].kind      = MFC_KEY_SRC_SYSTEM;
    cfgs[1].builtin   = mfc_keys_builtin_array();
    cfgs[1].builtin_n = mfc_keys_builtin_count();
    cfgs[1].paths     = sys_paths;
    cfgs[1].n_paths   = 2;
    cfgs[1].prober    = mfc_key_source_sd_probe;
    cfgs[1].opener    = mfc_key_source_sd_open;
    cfgs[1].io_ctx    = NULL;
    /* SYSTEM's file component is NOT accumulate: the on-disk dictionary can
     * hold several thousand entries (a real system.txt exceeds 2,000 unique
     * keys) and is trusted to already be internally deduplicated, so
     * streaming it must cost O(1) additional memory and yield an exact
     * count regardless of file size -- no MFC_KEY_SOURCE_MAX_SEEN cap, no
     * truncation. The compiled built-ins on this same source still
     * accumulate unconditionally (see mfc_key_source_iter_next). */
    cfgs[1].accumulate = false;
}

/*============================================================================*/
/* MIFARE Classic 1K/4K dictionary scan (Stage C): explicit, read-only
 * candidate-key TESTING via the canonical acquisition core above. Found keys
 * go into the existing per-sector store (augmenting whatever a prior read
 * found). Honors the abort flag; partial results are preserved.
 *
 * Runs one-shot on the NFC worker task (like the NTAG21x write). This is a
 * SEPARATE entry point from normal Read (m1_mfc_read_card) and Find Missing
 * Keys (nfc_mfc_find_keys_run) -- all three call the SAME canonical
 * m1_mfc_acquire_dict_phase()/mfc_dict_phase_run() core (no duplicate
 * dictionary parser), but this one is explicit, user-initiated candidate
 * testing under NFC Tools, independent of any particular read's outcome. */
/*============================================================================*/
void mfc_dict_scan(void)
{
    nfc_mfc_scan_t *sc  = nfc_ctx_get_mfc_scan();
    rfalNfcDevice  *dev = NULL;

    sc->state = NFC_SCAN_WAIT_CARD;
    sc->cur_sector = 0;
    sc->cur_keytype = 0;
    sc->cur_source = NFC_SCAN_SRC_BUILTIN;   /* placeholder before the first real candidate updates it */
    sc->sectors_acc = 0;
    sc->keys_tried = 0;
    sc->found = 0;

    /* 1) Activate a card (discover once, run to activation -- as the write path). */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_mfc_scan_abort) {
            sc->state = NFC_SCAN_STOPPED;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }

    /* 2) Require NFC-A MIFARE Classic 1K (SAK 0x08) or 4K (SAK 0x18). */
    uint8_t sak = (dev != NULL) ? dev->dev.nfca.selRes.sak : 0U;
    if ((dev == NULL) || (dev->type != RFAL_NFC_LISTEN_TYPE_NFCA) ||
        (((sak & 0x1FU) != 0x08U) && ((sak & 0x1FU) != 0x18U))) {
        platformLog("[MFC-SCAN] not a Classic 1K/4K\r\n");
        sc->state = NFC_SCAN_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }
    uint8_t  mfctype = m1nfc_mfc_type_from_sak(sak);
    uint16_t sectors_total = m1nfc_mfc_sectors_for_type(mfctype);
    uint16_t total_blocks  = m1nfc_mfc_blocks_for_type(mfctype);

    uint32_t       cuid = 0;
    const uint8_t *uid  = dev->nfcid;
    uint8_t        ulen = (uint8_t)dev->nfcidLen;
    if (ulen >= 4U) {
        const uint8_t *u = &uid[ulen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }

    /* Found-key store: augment existing MFC info (a prior read may already hold
     * proven slots -> skip them). Re-initialise if it is not the same card type.
     * Block DATA lives in the shared dump buffer (unit_size=16); bind it here. */
    nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
    if (!mfc->valid || (mfc->type != mfctype)) {
        nfc_ctx_clear_mfc();
        mfc->valid = true; mfc->type = mfctype;
        mfc->sectors_total = (uint8_t)sectors_total;
        mfc->keys_total = (uint8_t)(sectors_total * 2U);
        (void)memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
        (void)memset(g_nfc_valid_bits, 0x00, NFC_DUMP_MAX_UNITS / 8U);
    }
    nfc_ctx_set_dump(M1NFC_MFC_BLOCK_SZ, total_blocks, 0,
                     g_nfc_dump_buf, g_nfc_valid_bits, total_blocks, true);

    for (uint8_t s = 0; s < mfc->sectors_total; s++) {   /* seed progress from found slots */
        if (mfc->sec[s].key_a_found) sc->found++;
        if (mfc->sec[s].key_b_found) sc->found++;
        if (mfc->sec[s].key_a_found || mfc->sec[s].key_b_found) sc->sectors_acc++;
    }

    sc->state = NFC_SCAN_RUNNING;

    /* 3) Key-major streaming scan over the canonical [USER, SYSTEM] sources. */
    const char           *user_paths[2], *sys_paths[2];
    mfc_key_source_cfg_t  cfgs[2];
    m1_mfc_build_key_sources(cfgs, user_paths, sys_paths);

    if (!m1_mfc_acquire_dict_phase(dev, cuid, cfgs, 2, NULL, mfc, sc, &s_mfc_scan_abort, NULL, NULL)) {
        /* Card loss is not a user cancellation -- classified from counts alone. */
        mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                            mfc->keys_total, mfc->keys_found, false);
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;   /* card lost mid-scan; sc->state already set, proven keys kept */
    }

    /* Recompute sectors_read (block coverage) from what the scan captured. */
    mfc->sectors_read = 0;
    for (uint8_t s = 0; s < mfc->sectors_total; s++) {
        uint16_t first   = m1nfc_mfc_sector_first_block(s);
        uint8_t  nblocks = m1nfc_mfc_sector_blocks(s);
        bool all = true;
        for (uint8_t b = 0; b < nblocks; b++) {
            if (!nfc_ctx_mfc_block_valid((uint16_t)(first + b))) { all = false; break; }
        }
        if (all) mfc->sectors_read++;
    }

    sc->state = s_mfc_scan_abort ? NFC_SCAN_STOPPED : NFC_SCAN_DONE;
    mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                        mfc->keys_total, mfc->keys_found, s_mfc_scan_abort);
    platformLog("[MFC-SCAN] %s: keys_tried=%lu found=%u/%u sectors=%u/%u outcome=%d\r\n",
                s_mfc_scan_abort ? "stopped" : "done",
                (unsigned long)sc->keys_tried, sc->found, mfc->keys_total,
                mfc->sectors_read, mfc->sectors_total, (int)mfc->outcome);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
}

/*============================================================================*/
/* Find Missing Keys: dictionary-phase CONTINUATION of a partial MFC read.
 *
 * Unlike mfc_dict_scan() (above) and m1_mfc_read_card(), this function NEVER
 * calls nfc_ctx_clear_mfc() -- it extends the SAME nfc_mfc_info_t/
 * nfc_mfc_scan_t a prior read/continuation already populated, never starts a
 * second acquisition context. Refuses outright (leaving mfc/sc/resume
 * completely untouched) if there is no valid partial result to continue, or
 * if the card now presented is not provably the same physical card the
 * partial result came from -- see mfc_identity_matches(). Uses the SAME
 * canonical [USER, SYSTEM] source list and abort flag as normal Read and
 * Tools > Dictionary Scan, threading the persisted mfc_dict_resume_t through
 * so already-tried built-in/User candidates are never retried and an
 * uninterrupted System-dictionary sweep resumes near where it left off
 * (see mfc_dict_resume.h / mfc_dict_phase.h for the exact commit rules).
 */
/*============================================================================*/
void nfc_mfc_find_keys_run(void)
{
    nfc_mfc_scan_t  *sc  = nfc_ctx_get_mfc_scan();
    nfc_mfc_info_t  *mfc = nfc_ctx_get_mfc_info();
    rfalNfcDevice   *dev = NULL;

    if (!mfc->valid) {
        /* Nothing to continue -- refuse cleanly, touching nothing. This
         * should be unreachable in practice (the UI only offers Find
         * Missing Keys when mfc_action_eligibility() says PARTIAL), but the
         * worker must never assume its caller's gating is the only guard. */
        sc->state = NFC_SCAN_FAIL;
        return;
    }

    sc->state = NFC_SCAN_WAIT_CARD;
    /* Deliberately NOT reset here (unlike mfc_dict_scan()'s own create-time
     * reset): cur_sector/cur_keytype/cur_source/sectors_acc/keys_tried/found
     * already reflect every prior pass's progress and must keep doing so
     * through this one too. */

    /* 1) Activate a card. */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_mfc_scan_abort) {
            sc->state = NFC_SCAN_STOPPED;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }

    /* 2) Require NFC-A MIFARE Classic 1K/4K, exactly like normal Read/Scan. */
    uint8_t sak = (dev != NULL) ? dev->dev.nfca.selRes.sak : 0U;
    if ((dev == NULL) || (dev->type != RFAL_NFC_LISTEN_TYPE_NFCA) ||
        (((sak & 0x1FU) != 0x08U) && ((sak & 0x1FU) != 0x18U))) {
        platformLog("[MFC-FIND-KEYS] not a Classic 1K/4K\r\n");
        sc->state = NFC_SCAN_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    /* 3) Same physical card, provably -- refuse without corrupting anything
     * already proven if not (required: "Card identity mismatch refuses
     * continuation without corrupting the original context"). */
    const nfc_run_ctx_t *c = nfc_ctx_get();
    if (!mfc_identity_matches(dev->nfcid, (uint8_t)dev->nfcidLen, c->head.uid, c->head.uid_len)) {
        platformLog("[MFC-FIND-KEYS] card identity mismatch -- refusing continuation, "
                    "existing partial result left untouched\r\n");
        sc->state = NFC_SCAN_IDENTITY_MISMATCH;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    uint32_t       cuid = 0;
    const uint8_t *uid  = dev->nfcid;
    uint8_t        ulen = (uint8_t)dev->nfcidLen;
    if (ulen >= 4U) {
        const uint8_t *u = &uid[ulen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }

    uint16_t total_blocks = m1nfc_mfc_blocks_for_type(mfc->type);
    nfc_ctx_set_dump(M1NFC_MFC_BLOCK_SZ, total_blocks, 0,
                     g_nfc_dump_buf, g_nfc_valid_bits, total_blocks, true);
    /* g_nfc_dump_buf/g_nfc_valid_bits themselves are NOT cleared -- every
     * block a prior pass already read must remain readable through Info/
     * Data for the rest of this continuation and beyond. */

    sc->state = NFC_SCAN_RUNNING;

    /* 4) Same canonical [USER, SYSTEM] source list every MFC dictionary
     * consumer shares -- no new/duplicate source configuration. */
    const char           *user_paths[2], *sys_paths[2];
    mfc_key_source_cfg_t  cfgs[2];
    m1_mfc_build_key_sources(cfgs, user_paths, sys_paths);

    /* 5) Validate the persisted System resume cursor against the file it
     * would actually apply to -- a changed/replaced dictionary restarts
     * ONLY the System sweep (never discarding seen[] or any recovered
     * key/block). Re-stamped unconditionally afterward so a fresh commit
     * during THIS run is always compared against the right baseline next
     * time, matching or not. */
    mfc_dict_resume_t *resume = nfc_ctx_get_mfc_dict_resume();
    mfc_key_source_file_state_t sys_file = mfc_key_source_probe(&cfgs[1]);
    if (sys_file.state == MFC_PATH_PRESENT) {
        uint32_t sys_size; uint16_t sys_date, sys_time;
        if (mfc_key_source_sd_stat(sys_file.used_path, &sys_size, &sys_date, &sys_time)) {
            if (!mfc_dict_resume_system_identity_matches(resume, sys_file.used_path,
                                                          sys_size, sys_date, sys_time)) {
                mfc_dict_resume_reset_system_only(resume);
            }
            (void)strncpy(resume->sys_path, sys_file.used_path, sizeof(resume->sys_path) - 1U);
            resume->sys_path[sizeof(resume->sys_path) - 1U] = '\0';
            resume->sys_file_size = sys_size;
            resume->sys_file_date = sys_date;
            resume->sys_file_time = sys_time;
        }
        /* Could not stat (SD removed, transient I/O error): leave resume's
         * committed cursor exactly as it was for a later retry -- do not
         * discard a good cursor over one failed stat. */
    }

    if (!m1_mfc_acquire_dict_phase(dev, cuid, cfgs, 2, NULL, mfc, sc, &s_mfc_scan_abort, resume,
                                   &s_mfc_scan_skip_source)) {
        mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                            mfc->keys_total, mfc->keys_found, false);
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;   /* card lost mid-continuation; proven keys/blocks + resume state kept */
    }

    /* Recompute sectors_read (block coverage) -- may now cover sectors the
     * prior pass left partially resolved. */
    mfc->sectors_read = 0;
    for (uint8_t s = 0; s < mfc->sectors_total; s++) {
        uint16_t first   = m1nfc_mfc_sector_first_block(s);
        uint8_t  nblocks = m1nfc_mfc_sector_blocks(s);
        bool all = true;
        for (uint8_t b = 0; b < nblocks; b++) {
            if (!nfc_ctx_mfc_block_valid((uint16_t)(first + b))) { all = false; break; }
        }
        if (all) mfc->sectors_read++;
    }

    sc->state = s_mfc_scan_abort ? NFC_SCAN_STOPPED : NFC_SCAN_DONE;
    /* Same outcome recompute as every other acquisition entry point -- if
     * every slot is now resolved, this naturally becomes MFC_OUTCOME_COMPLETE
     * and the existing completed-read result screen takes over automatically
     * (no second success workflow). */
    mfc->outcome = mfc_classify_outcome(mfc->sectors_total, mfc->sectors_read,
                                        mfc->keys_total, mfc->keys_found, s_mfc_scan_abort);
    platformLog("[MFC-FIND-KEYS] %s: keys_tried=%lu found=%u/%u sectors=%u/%u outcome=%d\r\n",
                s_mfc_scan_abort ? "stopped" : "done",
                (unsigned long)sc->keys_tried, sc->found, mfc->keys_total,
                mfc->sectors_read, mfc->sectors_total, (int)mfc->outcome);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
}

/*============================================================================*/
/* MIFARE Classic 1K WRITE (clone/restore).                                    */
/*                                                                              */
/* Writes the loaded MFC 1K image (nfc_mfc_info_t: per-sector block data +      */
/* keys, populated by a prior read/scan) onto the presented physical card.      */
/* For each sector: authenticate the TARGET with a known key (the image's Key   */
/* A/B, falling back to the transport default FFFFFFFFFFFF for a blank target), */
/* read the target's CURRENT trailer to decode its access conditions, then      */
/* write only the blocks that key is permitted to write -- data blocks first,   */
/* the trailer last and only when the card allows a full trailer rewrite and    */
/* the new access bits are integrity-valid (brick prevention). Every write is   */
/* confirmed by reading the block back. Read-only on all frozen RFAL files.     */
/*============================================================================*/
static uint64_t mfc_key6_to_u64(const uint8_t k[6])
{
    return ((uint64_t)k[0] << 40) | ((uint64_t)k[1] << 32) | ((uint64_t)k[2] << 24) |
           ((uint64_t)k[3] << 16) | ((uint64_t)k[4] << 8)  |  (uint64_t)k[5];
}

/* Try to first-auth the TARGET sector (block `first`) with the image's keys and
 * the transport default. On success leaves the cipher `c` live and reports the
 * key type used (0x60/0x61). Returns false if none authenticate. */
static bool mfc_write_auth_target(const rfalNfcDevice *dev, uint8_t first, uint32_t cuid,
                                  const nfc_mfc_sector_t *S, Crypto1 *c, uint8_t *used_keytype)
{
    static const uint8_t FF[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
    struct { const uint8_t *key; uint8_t cmd; bool use; } cand[4] = {
        { S->key_a, MFC_KEY_A_CMD, S->key_a_found },
        { S->key_b, MFC_KEY_B_CMD, S->key_b_found },
        { FF,       MFC_KEY_A_CMD, true },
        { FF,       MFC_KEY_B_CMD, true },
    };
    for (uint8_t i = 0; i < 4U; i++) {
        if (!cand[i].use) continue;
        bool authed = false;
        if (mfc_do_auth(dev, first, cand[i].cmd, mfc_key6_to_u64(cand[i].key), cuid, c, &authed)) {
            *used_keytype = cand[i].cmd;
            return true;
        }
    }
    return false;
}

void nfc_mfc_write_run(void)
{
    nfc_mfc_write_t *wr = nfc_ctx_get_mfc_write();
    rfalNfcDevice   *dev = NULL;

    wr->state = NFC_MFCWR_WAIT_CARD;
    wr->cur_sector = 0; wr->written = 0; wr->skipped = 0; wr->failed = 0; wr->sectors_noauth = 0;

    /* 0) A valid 1K/4K image must already be loaded (from a read/scan). */
    const nfc_mfc_info_t *src = nfc_ctx_get_mfc_info();
    if (!src->valid || ((src->type != M1NFC_MFCTYPE_1K) && (src->type != M1NFC_MFCTYPE_4K))) {
        wr->state = NFC_MFCWR_NO_SOURCE;
        return;
    }

    /* 1) Activate a card (discover once, run to activation). */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_mfc_write_abort) {
            wr->state = NFC_MFCWR_STOPPED;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }

    /* 2) Require an NFC-A MIFARE Classic whose capacity matches the loaded image
     *    (1K SAK 0x08 / 4K SAK 0x18). Writing a 4K image to a 1K card (or vice
     *    versa) is refused. */
    uint8_t sak = (dev != NULL) ? dev->dev.nfca.selRes.sak : 0U;
    uint8_t tgt_type = ((sak & 0x1FU) == 0x18U) ? M1NFC_MFCTYPE_4K :
                       ((sak & 0x1FU) == 0x08U) ? M1NFC_MFCTYPE_1K : M1NFC_MFCTYPE_UNKNOWN;
    if ((dev == NULL) || (dev->type != RFAL_NFC_LISTEN_TYPE_NFCA) ||
        (tgt_type != src->type)) {
        platformLog("[MFC-WR] target mismatch (tgt_type=%u src=%u)\r\n",
                    (unsigned)tgt_type, (unsigned)src->type);
        wr->state = NFC_MFCWR_FAIL;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    uint32_t       cuid = 0;
    const uint8_t *uid  = dev->nfcid;
    uint8_t        ulen = (uint8_t)dev->nfcidLen;
    if (ulen >= 4U) {
        const uint8_t *u = &uid[ulen - 4U];
        cuid = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    }

    wr->state = NFC_MFCWR_WRITING;

    for (uint8_t s = 0; s < src->sectors_total; s++) {
        wr->cur_sector = s;
        m1_wdt_reset();
        if (s_mfc_write_abort) { wr->state = NFC_MFCWR_STOPPED; break; }

        uint16_t first    = m1nfc_mfc_sector_first_block(s);
        uint8_t  nblocks  = m1nfc_mfc_sector_blocks(s);
        uint16_t trailer  = (uint16_t)(first + nblocks - 1U);   /* last block of sector */
        const nfc_mfc_sector_t *S = &src->sec[s];

        /* Fresh state before this sector's first-auth (tolerate one RF glitch). */
        if (!mfc_reselect(dev) && !mfc_reselect(dev)) {
            wr->state = NFC_MFCWR_CARD_LOST;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }

        Crypto1 c;
        uint8_t used_kt = MFC_KEY_A_CMD;
        if (!mfc_write_auth_target(dev, (uint8_t)first, cuid, S, &c, &used_kt)) {
            wr->sectors_noauth++;
            wr->skipped = (uint16_t)(wr->skipped + nblocks);   /* whole sector unreachable */
            continue;
        }
        mfc_acc_key_t akt = (used_kt == MFC_KEY_B_CMD) ? MFC_ACC_KEY_B : MFC_ACC_KEY_A;

        /* Read the target's current trailer to learn its access conditions. If we
         * cannot read/validate them we cannot safely enforce -> skip the sector. */
        uint8_t tgt_trailer[16];
        mfc_access_t acc;
        if (!mfc_read_block_enc(&c, (uint8_t)trailer, tgt_trailer) ||
            !mfc_access_decode(tgt_trailer, &acc)) {
            wr->skipped = (uint16_t)(wr->skipped + nblocks);
            continue;
        }

        /* --- data blocks (every block except the trailer) --- */
        for (uint8_t b = 0; b < (uint8_t)(nblocks - 1U); b++) {
            uint16_t blk = (uint16_t)(first + b);
            if ((s == 0U) && (b == 0U)) { wr->skipped++; continue; }   /* manufacturer block */
            const uint8_t *sd = nfc_ctx_mfc_block(blk);
            if (sd == NULL)                                        { wr->skipped++; continue; }   /* no source data */
            if (!mfc_access_can_write_data(&acc, b, nblocks, akt)) { wr->skipped++; continue; }

            if (mfc_write_block_enc(&c, (uint8_t)blk, sd)) {
                uint8_t rb[16];
                if (mfc_read_block_enc(&c, (uint8_t)blk, rb) && (memcmp(rb, sd, 16) == 0))
                    wr->written++;
                else
                    wr->failed++;
            } else {
                wr->failed++;
            }
        }

        /* --- trailer (last block of the sector): guarded full rewrite --- */
        const uint8_t *src_tr = nfc_ctx_mfc_block(trailer);
        if ((src_tr == NULL) ||
            !mfc_access_can_write_trailer(&acc, akt) ||
            !mfc_access_valid(src_tr)) {
            wr->skipped++;                                   /* not permitted / no source / bad bits */
        } else if (mfc_write_block_enc(&c, (uint8_t)trailer, src_tr)) {
            /* Verify with the NEW key: the written trailer's Key A (bytes 0..5),
             * then its Key B (bytes 10..15). Compare the access bytes (6..9);
             * key bytes always read back as 0. */
            bool ok = false;
            uint8_t rb[16];
            bool authed = false; Crypto1 vc;
            if (mfc_reselect(dev) &&
                mfc_do_auth(dev, (uint8_t)first, MFC_KEY_A_CMD, mfc_key6_to_u64(&src_tr[0]), cuid, &vc, &authed) &&
                mfc_read_block_enc(&vc, (uint8_t)trailer, rb) &&
                (memcmp(&rb[6], &src_tr[6], 4) == 0)) {
                ok = true;
            } else {
                authed = false;
                if (mfc_reselect(dev) &&
                    mfc_do_auth(dev, (uint8_t)first, MFC_KEY_B_CMD, mfc_key6_to_u64(&src_tr[10]), cuid, &vc, &authed) &&
                    mfc_read_block_enc(&vc, (uint8_t)trailer, rb) &&
                    (memcmp(&rb[6], &src_tr[6], 4) == 0)) {
                    ok = true;
                }
            }
            if (ok) wr->written++; else wr->failed++;
        } else {
            wr->failed++;
        }

        osDelay(1);   /* yield */
    }

    if (wr->state == NFC_MFCWR_WRITING) wr->state = NFC_MFCWR_DONE;
    platformLog("[MFC-WR] %s: written=%u skipped=%u failed=%u noauth=%u\r\n",
                (wr->state == NFC_MFCWR_STOPPED) ? "stopped" : "done",
                wr->written, wr->skipped, wr->failed, wr->sectors_noauth);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
}

/*============================================================================*/
/**
 * @brief nfc_unlock_run - Genuine PWD_AUTH authentication against a
 * physically re-presented tag: either one user-entered password
 * (s_unlock_use_dictionary == false, payload in s_unlock_pwd) or a
 * dictionary scan (streamed from ntag_pwd_keys.h, never held wholesale in
 * RAM). On success, stores the genuinely-accepted password + returned
 * PACK into nfc_ctx (nfc_ctx_set_t2t_credential()) -- the ONLY place that
 * ever happens in this codebase, and only from a real tag's own response,
 * never from user entry alone.
 *
 * AUTHLIM safety (dictionary mode only -- a single explicit user-entered
 * attempt is always permitted, matching "explicit auth with a warning" in
 * the product spec): refuses to scan unless AUTHLIM was read as exactly
 * 0 (unlimited) during the read that led here. AUTHLIM is never reset or
 * modified by this function or by scanning itself.
 */
/*============================================================================*/
/**
 * @brief nfc_unlock_reread_all_pages - Re-capture every page of the tag now
 * that PWD_AUTH has genuinely succeeded, using the SAME still-active,
 * now-authenticated RF session (never deactivates/reactivates -- a real
 * NTAG21x/UL11 stays authenticated for every subsequent command until the
 * field drops or a fresh SELECT happens, so no new session is needed here).
 *
 * Mirrors m1_t2t_read_ntag()'s own 4-page-block READ loop exactly (same
 * retry-once-on-NAK, same LINK_LOSS short-circuit), but writes each
 * captured page straight into nfc_ctx via nfc_ctx_set_t2t_page() -- which
 * also updates the per-page valid bitmap -- so any page left invalid by the
 * original, necessarily partial, pre-auth read becomes genuinely valid once
 * actually read here. Pages already valid from the pre-auth read are simply
 * re-confirmed with identical content (harmless).
 *
 * Returns true ONLY if every expected page was actually captured this pass
 * (the loop reached the end without a NAK/comm failure) -- the caller must
 * never report NFC_UNLOCK_UNLOCKED on anything less, since
 * m1_t2t_emu_image_build() would still correctly refuse. On a false return,
 * no genuine AUTH0/PROT/AUTHLIM field is touched here at all: whatever was
 * known before (confirmed or still just suspected) is left exactly as it
 * was, so a caller can retry Unlock again without corrupting state.
 *
 * Only on a true return are CFG0/CFG1 -- now genuinely, fully captured --
 * parsed into the REAL AUTH0/PROT/AUTHLIM fields, superseding whatever
 * pre-auth suspicion (see nfc_ctx_set_t2t_protection_suspected(), set by
 * m1_t2t_read_ntag() when config pages were themselves unreadable) let the
 * tag into Unlock in the first place. The transient suspicion is explicitly
 * cleared here too -- it is fully resolved into genuine data, one way or
 * the other, and must never linger alongside the real fields.
 */
/*============================================================================*/
static bool nfc_unlock_reread_all_pages(void)
{
    uint16_t expected = nfc_ctx_get_t2t_expected_pages();
    if (expected == 0U) { return false; }

    /* The pre-auth partial read bound nfc_ctx's dump CAPACITY (unit_count)
     * to however many pages it actually captured (e.g. 4, for an AUTH0=4
     * tag) via its own nfc_ctx_set_dump() call. nfc_ctx_set_t2t_page()
     * silently refuses to write any page index >= that unit_count -- so
     * without first widening it to the full expected page count, every
     * page this function captures beyond the original partial-read
     * boundary is silently dropped even though the RF reads themselves
     * genuinely succeed, and this function would report "complete"
     * regardless (it only checks whether each RF read succeeded, never
     * whether the resulting nfc_ctx write did). Confirmed by hardware
     * evidence: PWD_AUTH succeeded and this function returned true, yet
     * the post-auth CFG0/CFG1 re-parse below always failed to find them,
     * and m1_t2t_emu_image_build() kept refusing with INCOMPLETE_PAGES --
     * because pages 4+ were never actually stored. Re-binds the SAME
     * underlying buffers (g_nfc_dump_buf/g_nfc_valid_bits) -- this only
     * widens the capacity, it never clears or resets the already-valid
     * pages 0..3 the pre-auth read captured. */
    nfc_ctx_set_dump(4U, expected, 0U, g_nfc_dump_buf, g_nfc_valid_bits, 0U, true);

    uint8_t    buf[16];
    uint16_t   rcvLen;
    ReturnCode err;
    bool       complete = true;

    for (uint16_t blk = 0; blk < expected; blk += 4U) {
        bool read_ok = false;
        for (uint8_t retry = 0; retry < 2U && !read_ok; retry++) {
            if (retry > 0U) { osDelay(10); }
            rcvLen = 0;
            memset(buf, 0x00, sizeof(buf));
            err = rfalT2TPollerRead((uint8_t)blk, buf, sizeof(buf), &rcvLen);
            if ((err == RFAL_ERR_NONE) && (rcvLen >= 16U)) {
                read_ok = true;
            } else if ((err == RFAL_ERR_LINK_LOSS) || (err == RFAL_ERR_PROTO)) {
                /* Same reasoning as the pre-auth read loop's identical fix:
                 * a genuine protocol NAK is conclusive, never worth
                 * retrying into a misleading TIMEOUT. */
                break;
            }
        }
        if (!read_ok) {
            platformLog("[UNLOCK] re-read stopped at page %u (still restricted or comm error)\r\n",
                        (unsigned)blk);
            complete = false;
            break;
        }

        uint16_t pages_in_block = 4U;
        if ((uint32_t)blk + pages_in_block > expected) {
            pages_in_block = (uint16_t)(expected - blk);
        }
        for (uint16_t i = 0; i < pages_in_block; i++) {
            nfc_ctx_set_t2t_page((uint16_t)(blk + i), &buf[i * 4U]);
        }

        m1_wdt_reset();
    }

    if (!complete) { return false; }

    /* Re-derive protection state from the now-genuinely-captured config
     * pages -- never left as a pre-auth suspicion once real bytes exist. */
    uint8_t  variant = nfc_ctx_get_t2t_variant();
    uint16_t cfg0_page = 0;
    if (m1_t2t_emu_image_cfg0_page(variant, &cfg0_page)) {
        uint8_t cfg0[4], cfg1[4];
        if (nfc_ctx_get_t2t_page(cfg0_page, cfg0) &&
            nfc_ctx_get_t2t_page((uint16_t)(cfg0_page + 1U), cfg1)) {
            nfc_ctx_set_t2t_auth0(cfg0[3]);
            nfc_ctx_set_t2t_prot((cfg1[0] & 0x80U) != 0U);
            nfc_ctx_set_t2t_authlim((uint8_t)(cfg1[0] & 0x07U));
            nfc_ctx_clear_t2t_protection_suspected();
            platformLog("[UNLOCK] post-auth protection confirmed: AUTH0=%u PROT=%u AUTHLIM=%u\r\n",
                        (unsigned)cfg0[3], (unsigned)((cfg1[0] & 0x80U) != 0U),
                        (unsigned)(cfg1[0] & 0x07U));
        }
    }

    return true;
}

/*============================================================================*/
void nfc_unlock_run(void)
{
    nfc_t2t_unlock_t *uk = nfc_ctx_get_t2t_unlock();
    rfalNfcDevice     *dev = NULL;

    uk->state      = NFC_UNLOCK_WAIT_CARD;
    uk->dict_tried = 0;

    /* Must be a supported, currently-protected (or protection-suspected)
     * variant -- established by the read that led here (nfc_ctx's own
     * variant/auth0/suspicion, never re-derived or assumed here). A tag
     * whose config pages were themselves unreadable pre-auth (AUTH0 <=
     * config_page) only ever has the transient suspicion set, never a
     * genuine AUTH0 -- manual Unlock is still permitted in that case
     * (explicit user action, matches product spec), dictionary mode is
     * not (handled by the AUTHLIM gate below, since genuine AUTHLIM is
     * necessarily also unknown whenever only suspicion, not confirmation,
     * is available). */
    uint8_t variant = nfc_ctx_get_t2t_variant();
    bool supported = (variant == M1NFC_T2TVAR_UL11)    || (variant == M1NFC_T2TVAR_NTAG213) ||
                     (variant == M1NFC_T2TVAR_NTAG215) || (variant == M1NFC_T2TVAR_NTAG216);
    uint8_t auth0 = 0xFFU;
    bool    have_auth0  = nfc_ctx_get_t2t_auth0(&auth0);
    bool    known_auth0 = have_auth0 && (auth0 != 0xFFU);
    bool    suspected   = nfc_ctx_get_t2t_protection_suspected(NULL);
    if (!supported || (!known_auth0 && !suspected)) {
        uk->state = NFC_UNLOCK_UNSUPPORTED;
        return;
    }

    /* AUTHLIM safety gate -- dictionary mode only. A nonzero, or genuinely
     * unreadable, AUTHLIM means a wrong guess can permanently brick the
     * tag's authentication -- never risk that on an unattended scan. This
     * also correctly refuses dictionary mode whenever entry was only via
     * the transient suspicion above, since genuine AUTHLIM is necessarily
     * still unread in that case. */
    if (s_unlock_use_dictionary) {
        uint8_t authlim = 0;
        bool    have_authlim = nfc_ctx_get_t2t_authlim(&authlim);
        if (!have_authlim || (authlim != 0U)) {
            uk->state = NFC_UNLOCK_AUTHLIM_ACTIVE;
            return;
        }
    }

    /* Re-activate the physical tag (the read session that detected
     * protection already deactivated RF) -- mirrors nfc_mfc_write_run()'s
     * own activation loop exactly. */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    rfalNfcDiscover(&discParam);
    for (;;) {
        rfalNfcWorker();
        if (rfalNfcIsDevActivated(rfalNfcGetState())) { rfalNfcGetActiveDevice(&dev); break; }
        if (s_unlock_abort) {
            uk->state = NFC_UNLOCK_STOPPED;
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
            return;
        }
        osDelay(5);
    }
    if ((dev == NULL) || (dev->type != RFAL_NFC_LISTEN_TYPE_NFCA)) {
        uk->state = NFC_UNLOCK_COMM_ERROR;
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        return;
    }

    uint8_t pack[2] = {0};

    if (!s_unlock_use_dictionary) {
        uk->state = NFC_UNLOCK_AUTHENTICATING;
        uint8_t pwd[4];
        memcpy(pwd, s_unlock_pwd, 4U);
        memset(s_unlock_pwd, 0, sizeof(s_unlock_pwd));   /* clear candidate from RAM once consumed */

        nfc_pwdauth_result_t r = nfc_poller_pwd_auth(pwd, NULL, pack);
        switch (r) {
            case NFC_PWDAUTH_OK:
                /* Credential is genuine and independently true regardless of
                 * what the re-read below finds -- stored either way. But
                 * UNLOCKED is reported only if the authenticated re-read
                 * actually completed; a real accept with an incomplete
                 * re-read is its own distinct, honestly-reported state. */
                nfc_ctx_set_t2t_credential(pwd, pack);
                uk->state = nfc_unlock_reread_all_pages() ? NFC_UNLOCK_UNLOCKED
                                                           : NFC_UNLOCK_REREAD_INCOMPLETE;
                break;
            case NFC_PWDAUTH_REJECTED:      uk->state = NFC_UNLOCK_WRONG_PASSWORD; break;
            case NFC_PWDAUTH_PACK_MISMATCH: uk->state = NFC_UNLOCK_PACK_MISMATCH;  break;
            default:                        uk->state = NFC_UNLOCK_COMM_ERROR;     break;
        }
        memset(pwd, 0, sizeof(pwd));
    } else {
        uk->state = NFC_UNLOCK_SEARCHING;
        ntag_pwd_keys_iter_t it;
        ntag_pwd_keys_iter_begin(&it, NULL);

        uint8_t cand[4];
        bool found = false;
        while (ntag_pwd_keys_iter_next(&it, cand)) {
            if (s_unlock_abort) { uk->state = NFC_UNLOCK_STOPPED; break; }
            m1_wdt_reset();
            uk->dict_tried++;

            nfc_pwdauth_result_t r = nfc_poller_pwd_auth(cand, NULL, pack);
            if (r == NFC_PWDAUTH_OK) {
                nfc_ctx_set_t2t_credential(cand, pack);
                uk->state = nfc_unlock_reread_all_pages() ? NFC_UNLOCK_UNLOCKED
                                                           : NFC_UNLOCK_REREAD_INCOMPLETE;
                found = true;
                break;
            }
            if (r == NFC_PWDAUTH_COMM_ERROR) {
                /* Tag likely removed or field lost mid-scan -- stop rather
                 * than keep burning candidates against a dead link. */
                uk->state = NFC_UNLOCK_COMM_ERROR;
                break;
            }
            /* NFC_PWDAUTH_REJECTED: genuine wrong-password, try the next
             * candidate. Never looped unboundedly -- bounded by the
             * dictionary's own finite size (built-ins + user file), and
             * every iteration re-checks abort. */
        }
        ntag_pwd_keys_iter_end(&it);
        memset(cand, 0, sizeof(cand));   /* clear last candidate from RAM */

        if (!found && (uk->state == NFC_UNLOCK_SEARCHING)) {
            uk->state = NFC_UNLOCK_WRONG_PASSWORD;   /* exhausted the dictionary */
        }
    }

    memset(pack, 0, sizeof(pack));
    platformLog("[UNLOCK] result state=%u tried=%u\r\n", (unsigned)uk->state, (unsigned)uk->dict_tried);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
}



/*
 ******************************************************************************
 * MIFARE CLASSIC helper functions
 ******************************************************************************
*/

/*============================================================================*/
/**
 * @brief mfc_load_key_dict - Load key list from mf_classic_dict.nfc file
 * 
 * Loads MIFARE Classic key dictionary from SD card file.
 * Currently only validates file format and counts keys.
 * 
 * @param[out] dict Pointer to key dictionary structure
 * @retval true If dictionary loaded successfully
 * @retval false If file not found or invalid
 */
/*============================================================================*/
/* Phase A does not use the key dictionary; kept (compiled) for the
 * MIFARE_CLASSIC_AUTH_TEST path only, so mark it unused to avoid a warning. */
static bool mfc_load_key_dict(mfc_key_dict_t *dict) __attribute__((unused));
static bool mfc_load_key_dict(mfc_key_dict_t *dict)
{
    nfcfio_t io;
    char line[64];
    int  n;

    if (!dict) return false;
    dict->count = 0;

    /* 1) Open key dictionary file from SD card */
    if (!nfcfio_open_read(&io, MFC_DICT_PATH)) {
        platformLog("MFC dict open failed: %s\r\n", MFC_DICT_PATH);
        return false;
    }

    platformLog("MFC dict opened: %s\r\n", MFC_DICT_PATH);

    /* 2) Read and parse line by line (currently only validates format, key application later) */
    while ((n = nfcfio_getline(&io, line, sizeof(line))) >= 0) {
        char *p = line;

        /* Skip whitespace/newlines */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '\0') continue;      // Empty line
        if (*p == '#')  continue;      // Comment

        /* From here, parse hex keys according to mf_classic_dict.nfc actual format
           and put them in dict->keys[dict->count].
           In this step, we only check "if file reads correctly",
           so simply increment line count. */

        if (dict->count < MFC_MAX_DICT_KEYS) {
            dict->count++;
        } else {
            platformLog("MFC dict key overflow (>%d)\r\n", MFC_MAX_DICT_KEYS);
            break;
        }
    }

    nfcfio_close(&io);

    platformLog("MFC dict loaded, lines(keys)=%u\r\n", dict->count);
    return (dict->count > 0);
}



#ifdef MIFARE_CLASSIC_AUTH_TEST
/*============================================================================*/
/**
 * @brief m1_read_mifareclassic - Read MIFARE Classic using key dictionary
 * 
 * Reads MIFARE Classic card using key dictionary attack.
 * Authenticates sectors with dictionary keys and reads all blocks.
 * 
 * @param[in] dev Pointer to NFC device
 * @retval None
 */
/*============================================================================*/
static void m1_read_mifareclassic(const rfalNfcDevice *dev)
{
    const rfalNfcaListenDevice *nfca = &dev->dev.nfca;
    uint16_t totalSectors = 0;
    uint16_t totalBlocks  = 0;
    uint16_t maxBlocks    = NFC_DUMP_MAX_UNITS;

    mfc_key_dict_t dict;
    uint16_t lastSeenBlock = 0;
    uint16_t successSectors = 0;

    /* Determine card capacity (sector/block count) */
    mfc_get_layout_from_sak(nfca->selRes.sak, &totalSectors, &totalBlocks);

    if (totalBlocks > maxBlocks) {
        platformLog("[MFC] totalBlocks(%u) > NFC_DUMP_MAX_UNITS(%u), clamp\r\n",
                    totalBlocks, maxBlocks);
        totalBlocks = maxBlocks;
    }

    /* Load key dictionary */
    if (!mfc_load_key_dict(&dict)) {
        platformLog("[MFC] no key dict, skip MFC dump\r\n");
        return;
    }

    /* Initialize dump buffer / valid bits */
    memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
    memset(g_nfc_valid_bits, 0x00, NFC_DUMP_MAX_UNITS / 8);

    platformLog("[MFC] start dump: sectors=%u blocks=%u\r\n",
                totalSectors, totalBlocks);

    for (uint16_t sector = 0; sector < totalSectors; sector++) {

        uint16_t firstBlock = mfc_sector_to_first_block(sector);
        uint16_t blocksInSector =
            (sector < 32 || totalSectors <= 16) ? 4 : 16;

        if (firstBlock >= totalBlocks) {
            break;
        }

        bool sectorAuthed = false;
        mfc_key_type_t usedType = MFC_KEYTYPE_A;
        uint8_t usedKey[MFC_KEY_LEN];

        /* ---------- Sector authentication: Try all dictionary keys for both A/B ---------- */
        for (uint16_t ki = 0; ki < dict.count && !sectorAuthed; ki++) {

            const uint8_t *key = dict.keys[ki];

            /* Key A */
            if (mfc_authenticate_block(dev, (uint8_t)firstBlock,
                                       MFC_KEYTYPE_A, key) == RFAL_ERR_NONE) {

                sectorAuthed = true;
                usedType = MFC_KEYTYPE_A;
                memcpy(usedKey, key, MFC_KEY_LEN);
                platformLog("[MFC] sector %u auth OK with dict[%u] as KeyA\r\n",
                            sector, ki);
                break;
            }

            /* Key B */
            if (mfc_authenticate_block(dev, (uint8_t)firstBlock,
                                       MFC_KEYTYPE_B, key) == RFAL_ERR_NONE) {

                sectorAuthed = true;
                usedType = MFC_KEYTYPE_B;
                memcpy(usedKey, key, MFC_KEY_LEN);
                platformLog("[MFC] sector %u auth OK with dict[%u] as KeyB\r\n",
                            sector, ki);
                break;
            }
        }

        if (!sectorAuthed) {
            platformLog("[MFC] sector %u auth FAILED\r\n", sector);
            continue;
        }

        successSectors++;

        /* ---------- Read all blocks of successfully authenticated sector ---------- */
        for (uint16_t bi = 0; bi < blocksInSector; bi++) {

            uint16_t blockNo = firstBlock + bi;
            if (blockNo >= totalBlocks) {
                break;
            }

            uint8_t *dst = &g_nfc_dump_buf[blockNo * MFC_BLOCK_SIZE];

            ReturnCode rc = mfc_read_block(dev, (uint8_t)blockNo, dst);
            if (rc == RFAL_ERR_NONE) {
                /* Set flag that this block is valid */
                g_nfc_valid_bits[blockNo >> 3] |= (uint8_t)(1u << (blockNo & 0x7));

                if (blockNo + 1 > lastSeenBlock) {
                    lastSeenBlock = blockNo + 1;
                }
            } else {
                platformLog("[MFC] read block %u failed: %d\r\n", blockNo, rc);
            }
        }
    }

    /* Register dump metadata in NFC context */
    nfc_ctx_set_dump(
        MFC_BLOCK_SIZE,          /* unit_size (block size 16B) */
        totalBlocks,             /* max_units */
        lastSeenBlock,           /* max_seen_unit */
        g_nfc_dump_buf,
        g_nfc_valid_bits,
        0,                       /* begin_unit */
        (lastSeenBlock > 0)      /* has_dump */
    );

    platformLog("[MFC] dump done: successSectors=%u lastBlock=%u\r\n",
                successSectors, lastSeenBlock);
}

/*============================================================================*/
/**
 * @brief mfc_hex_nibble - Convert one character to 0~15 nibble, return -1 on failure
 * 
 * @param[in] c Character to convert
 * @retval 0-15 Nibble value
 * @retval -1 Conversion failed
 */
/*============================================================================*/
static int mfc_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}
/*============================================================================*/
/**
 * @brief mfc_get_layout_from_sak - Estimate Mifare Classic capacity from SAK value
 * 
 * @param[in] sak SAK value
 * @param[out] outSectors Pointer to store sector count
 * @param[out] outBlocks Pointer to store block count
 * @retval None
 */
/*============================================================================*/
static void mfc_get_layout_from_sak(uint8_t sak, uint16_t *outSectors, uint16_t *outBlocks)
{
    uint8_t base = sak & 0x1F;

    if (base == 0x08) {
        /* Mifare Classic 1K */
        *outSectors = 16;
        *outBlocks  = 64;
    } else if (base == 0x18) {
        /* Mifare Classic 4K */
        *outSectors = 40;
        *outBlocks  = 256;
    } else {
        /* If ambiguous, treat as 1K for now */
        platformLog("[MFC] unknown SAK 0x%02X, fallback to 1K layout\r\n", sak);
        *outSectors = 16;
        *outBlocks  = 64;
    }
}

/*============================================================================*/
/**
 * @brief mfc_sector_to_first_block - Convert sector number to first block number
 * 
 * @param[in] sector Sector number
 * @retval First block number of the sector
 */
/*============================================================================*/
static uint16_t mfc_sector_to_first_block(uint16_t sector)
{
    if (sector < 32) {
        /* Sectors 0~31 have 4 blocks per sector */
        return (uint16_t)(sector * 4);
    } else {
        /* Sectors 32~39 have 16 blocks per sector */
        return (uint16_t)(128 + (sector - 32) * 16);
    }
}

/* --- MIFARE Classic low-level stubs (Step 1: Not actually used) --- */
/*============================================================================*/
/**
 * @brief mfc_authenticate_block - Authenticate MIFARE Classic block (stub)
 * 
 * @param[in] dev Pointer to NFC device
 * @param[in] blockNo Block number
 * @param[in] keyType Key type (A or B)
 * @param[in] key Key data (6 bytes)
 * @retval RFAL_ERR_NOTSUPP Not supported (stub)
 */
/*============================================================================*/
static ReturnCode mfc_authenticate_block(const rfalNfcDevice *dev,
                                         uint8_t blockNo,
                                         mfc_key_type_t keyType,
                                         const uint8_t key[6])
{
    (void)dev; (void)blockNo; (void)keyType; (void)key;
    return RFAL_ERR_NOTSUPP;
}

/*============================================================================*/
/**
 * @brief mfc_read_block - Read MIFARE Classic block (stub)
 * 
 * @param[in] dev Pointer to NFC device
 * @param[in] blockNo Block number
 * @param[out] out Output buffer (16 bytes)
 * @retval RFAL_ERR_NOTSUPP Not supported (stub)
 */
/*============================================================================*/
static ReturnCode mfc_read_block(const rfalNfcDevice *dev,
                                 uint8_t blockNo,
                                 uint8_t out[MFC_BLOCK_SIZE])
{
    (void)dev; (void)blockNo;
    memset(out, 0x00, MFC_BLOCK_SIZE);
    return RFAL_ERR_NOTSUPP;
}
#endif
