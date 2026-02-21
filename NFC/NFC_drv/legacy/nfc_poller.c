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

#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "uiView.h"
#include "nfc_driver.h"
#include "nfc_poller.h"
#include "common/nfc_ctx.h"
#include "common/nfc_storage.h"
#include "m1_sdcard.h"
#include "m1_storage.h"
#include "common/nfc_fileio.h"  
#include "logger.h"
#include <stdio.h>  

#define NOTINIT              0     /*!< Demo State:  Not initialized        */
#define START_DISCOVERY      1     /*!< Demo State:  Start Discovery        */
#define DISCOVERY            2     /*!< Demo State:  Discovery              */

/*
 ******************************************************************************
 * MIFARE CLASSIC Helper Functions
 ******************************************************************************
 */

#define MFC_DICT_PATH   "NFC/system/mf_classic_dict.nfc"   // Adjust to actual path
#define MFC_MAX_DICT_KEYS   2042
#define MFC_KEY_LEN         6
#define MFC_BLOCK_SIZE      16

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
static ReturnCode GetVersion_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen);
static uint16_t LogParsedNtagVersion(const uint8_t *version, uint16_t len);

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

                    /* --- MIFARE Classic identification: ATQA + SAK combination --- */
                    bool isMfcClassic = false;
                    bool isUid4Byte   = ((atqa0 == 0x04U) && (atqa1 == 0x00U));  /* ATQA = 0x0400 → 4B UID */
                    bool isUid7Byte   = ((atqa0 == 0x44U) && (atqa1 == 0x00U));  /* ATQA = 0x4400 → 7B UID */
                    bool isMfc1K      = (sak == 0x08U);                          /* SAK = 0x08 → Classic 1K */
                    bool isMfc4K      = (sak == 0x18U);                          /* SAK = 0x18 → Classic 4K */

                    if ((isMfc1K || isMfc4K) && (isUid4Byte || isUid7Byte)) {
                        isMfcClassic = true;

                        const char *memSizeStr = isMfc1K ? "1K" : "4K";
                        const char *uidTypeStr = isUid4Byte ? "4-byte UID" : "7-byte UID";

                        platformLog("MIFARE Classic %s detected (%s). ATQA=%02X%02X, SAK=%02X, UIDLen=%u\r\n",
                                    memSizeStr,
                                    uidTypeStr,
                                    atqa0, atqa1, sak,
                                    nfcDevice->nfcidLen);
                        // If needed, Family string can be overwritten here
                        SET_FAMILY("MIFARE Classic %s (%s)", memSizeStr, uidTypeStr);
                        //m1_read_mifareclassic(nfcDevice);
                            /* Step 1: Check only if system key dictionary exists */
                        mfc_key_dict_t dict;
                        if (mfc_load_key_dict(&dict)) {
                            platformLog("[MFC] system key dict OK (%u keys)\r\n", dict.count);
                        } else {
                            platformLog("[MFC] system key dict missing or invalid\r\n");
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
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T4T)
                            platformLog("NFC Type 4 Tag Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_NFCDEP)
                            platformLog("NFC DEP Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T4T_NFCDEP)
                            platformLog("NFC Type 4 Tag DEP Read More\r\n");
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
                    uint8_t devUID[RFAL_NFCV_UID_LEN];
                    ST_MEMCPY(devUID, nfcDevice->nfcid, nfcDevice->nfcidLen);
                    REVERSE_BYTES(devUID, RFAL_NFCV_UID_LEN);  /* Reverse for display */
                    platformLog("ISO15693/NFC-V TAG found. UID=%s\r\n",
                                hex2Str(devUID, RFAL_NFCV_UID_LEN));
                    strcpy(NFC_Type, "ISO15693/NFC-V");
                    nfc_tx_type = NFC_TX_V;
                    notifyRead  = true;
                }
                break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_ST25TB:
                    platformLog("ST25TB TAG found. UID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ST25TB");
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
    discParam.totalDuration  = 1000U;                      /* Discovery window (adjust if needed) */
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
        max_page = LogParsedNtagVersion(version, rcvLen);
        nfc_ctx_set_t2t_version(version, (uint8_t)rcvLen);
        platformLog("GET_VER OK: %u pages\r\n", max_page);
    } else {
        /* If GET_VERSION fails, get page count from context */
        uint16_t ctx_pages = nfc_ctx_get_t2t_page_count();
        if (ctx_pages > 0) {
            max_page = ctx_pages;
            platformLog("GET_VER fail, using ctx: %u pages\r\n", max_page);
        } else {
            platformLog("GET_VER fail, using default: %u pages\r\n", max_page);
        }
    }



    // Read blocks 0 ~ N (T2T READ reads 4 blocks at a time, so increment by 4)
    for (uint8_t blk = 0; blk < max_page; blk += 4) {  // Read 4 blocks at a time
        rcvLen = 0;
        bool read_ok = false;
        
        /* Retry each READ command (max 3 times, to handle timing issues) */
        for (uint8_t retry = 0; retry < 3 && !read_ok; retry++) {
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
                /* If field loss, don't retry anymore */
                if (err == RFAL_ERR_LINK_LOSS) {
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
            /* Stop if field loss */
            if (err == RFAL_ERR_LINK_LOSS) {
                break;
            }
            /* Fill failed blocks with 0 at corresponding position (prevent data shift) */
            for (uint8_t i = 0; i < pages_to_save; i++) {
                uint16_t page_idx = blk + i;
                if (page_idx >= max_page) break;
                
                uint16_t save_offset = page_idx * 4;
                if (save_offset + 4 <= dumpSize) {
                    memset(&dump[save_offset], 0x00, 4);
                }
            }
        } else {
            /* On success: Store received data at corresponding block position */
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
        }
        
        /* Update offset to next block position (regardless of success/failure) */
        offset = (blk + 4) * 4;
        
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
