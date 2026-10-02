/* See COPYING.txt for license details. */

/*
 ******************************************************************************
 * nfc_listener.c - NFC Listener Mode (Card Emulation)
 ******************************************************************************
 * 
 * [Purpose]
 * - Implements NFC-A Type 2 Tag (T2T) card emulation
 * - Handles T2T commands (READ, GET_VERSION, FAST_READ, WRITE, etc.)
 * - Synchronous transmission for strict timing requirements (FDT: 86~177μs)
 * 
 * [State Machine]
 * NOTINIT → IDLE → START_DISCOVERY → DISCOVERY → DATAEXCHANGE
 * 
 * [Key Flow]
 * 1. ListenIni(): Initialize RFAL in listener mode
 *    - Configure discovery parameters (LISTEN_TECH_A only)
 *    - Set UID, ATQA, SAK for emulation
 *    - Start rfalNfcDiscover()
 * 
 * 2. ListenerCycle(): Main processing loop
 *    - DISCOVERY: Wait for reader field, handle ACTIVATED state
 *    - DATAEXCHANGE: Process T2T commands from reader
 * 
 * 3. ACTIVATED → DATAEXCHANGE Transition:
 *    - rfal_nfc.c receives T2T command in LISTEN_ACTIVATED state
 *    - Data stored in gNfcDev.rxBuf.rfBuf
 *    - rfalNfcDataExchangeStart() retrieves already-received data pointer
 *    - rfalNfcDataExchangeGetStatus() checks if data exists
 *    - Transition to DATAEXCHANGE state
 * 
 * 4. T2T Command Processing (CeHandleT2TCmdRx()):
 *    - 0x30 (READ): Build page response, sync TX (500μs FWT)
 *    - 0x3A (FAST_READ): Build multi-page response, sync TX
 *    - 0x60 (GET_VERSION): Send version info, sync TX
 *    - 0xA2 (WRITE): Save page data, send ACK
 *    - After TX: Re-arm RX for next command
 * 
 * [Timing Critical]
 * - Uses rfalTransceiveBlockingTx() for immediate response
 * - FWT = 500μs (sufficient margin for T2T standard 86~177μs FDT)
 * - RX re-arm immediately after TX completion
 * 
 * [Phase States]
 * - CE_PHASE_WAIT_RX: Waiting for command from reader
 * - CE_PHASE_DATAEX: Data exchange in progress (TX completed)
 * 
 ******************************************************************************
 */
#include "nfc_poller.h"
#include "utils.h"
#include "rfal_nfc.h"
#include "rfal_t2t.h"
#include "rfal_utils.h"
#include "logger.h"

#include "st25r3916.h"
#include "st25r3916_com.h"
#include "nfc_conf.h"        /* ST25R_INT_PORT/PIN, IRQ masks, EnableInterrupts */
#include "rfal_utils.h"

#include "rfal_AnalogConfig.h"
#include "rfal_rf.h"
#include "uiView.h"
#include "nfc_driver.h"
#include "nfc_listener.h"
#include "mfc_detect.h"
#if defined(M1_MFC_RAW_EMULATION)
/* MonstaTek: raw MIFARE Classic emulation (Scope B) -- isolated protocol layer. */
#include "m1_mfc_raw_listener.h"
#include "m1_nfc_raw_hal.h"
#include "m1_mfc_raw_session_hw.h" /* Scope C: m1_mfc_raw_hw_active/session_end */
#include "stm32h5xx.h"       /* DWT cycle counter for {At} timing diagnostics */
#if defined(M1_MFC_DMA_AT) || defined(MFC_ROUTE_PROBE)
#include "m1_mfc_dma.h"      /* TIM7 + dual-GPDMA1 {At} transport             */
extern volatile uint32_t g_m1_rxe_cyc;   /* latched RF end-of-receive (ISR)    */
extern volatile uint8_t  g_m1_rxe_seen;  /* set by ISR on RXE; reset by caller */
#if defined(M1_MFC_DMA_AT)
extern volatile uint8_t  g_m1_dump_regs; /* 1 => emit [B2b-REGS] once (session) */
#endif
#endif
#endif
#include "common/nfc_ctx.h"
#include "m1_t2t_emu_image.h"
#include "m1_t2t_transport.h"
#include "lfrfid.h"
#include "rfal_nfc.h"

#define NOTINIT             0     
#define IDLE                1     
#define START_DISCOVERY     3    
#define DISCOVERY           4    
#define SELECT              5     
#define DATAEXCHANGE        6     

uint8_t g_T2tCmd;

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */
static uint16_t g_ceTxLenBytes = 0U;
static bool     g_ceTxPending  = false;

static rfalNfcDiscoverParam discParam;
static uint8_t              state = NOTINIT;
static bool                 multiSel;

static EmuPersona_t g_persona = EMU_PERSONA_RAW;
void Emu_SetPersona(EmuPersona_t p) { g_persona = p; }
EmuPersona_t Emu_GetPersona(void)   { return g_persona; }
static uint16_t ceT2T_FromDump(const uint8_t *rx, uint16_t rxLenBytes, uint8_t *tx, uint16_t txMaxLen);

/* CE state and buffer */
typedef enum {
    CE_PHASE_IDLE = 0,
    CE_PHASE_WAIT_RX,
    CE_PHASE_WAIT_TX,
    CE_PHASE_DATAEX,   /* Data exchange in progress */
} CePhase_t;

static rfalNfcDevice *s_ceActiveDev = NULL;
static uint8_t       *s_ceRxData    = NULL;
static uint16_t      *s_ceRxRcvLen  = NULL;
static uint8_t        g_ceTxBuf[192];
static bool s_firstRxHandled = false;
static CePhase_t     s_cePhase   = CE_PHASE_IDLE;
#if defined(M1_MFC_RAW_EMULATION)
#if defined(M1_MFC_RAW_EMULATION)
#endif
#endif
static uint8_t       *s_rxData    = NULL;   // Buffer pointer managed by RFAL
static uint16_t      *s_rcvLen    = NULL;
static rfalNfcDevice *s_active_dev = NULL;
static volatile bool s_listener_stop = false;
static bool          s_wasActivated = false;
static bool          s_firstRxPending = false;

static uint16_t s_lastRxBits;
static uint8_t  s_lastRxBuf[32]; /* As needed length */

/* Session-owned T2T (Ultralight/NTAG) saved-card emulation image (see
 * m1_t2t_emu_image.h). Set by the UI before RF starts via
 * nfc_listener_set_t2t_emu_image(); once armed, ALL T2T command handling
 * (CeBuildT2TReadResp/CeHandleT2TCmdRx) reads/writes s_t2t_img exclusively
 * and never touches nfc_ctx -- matching the same "immutable session-owned
 * image, no live pointers into UI/parser/global buffers" architecture used
 * for MFC 1K (m1_mfc_raw_set_emu_image). While unarmed, T2T persona falls
 * back to the legacy nfc_ctx-direct path unchanged (default/no-context
 * emulate entry points outside this task's two required entry points). */
static m1_t2t_emu_image_t s_t2t_img;
static bool                s_t2t_img_armed = false;

/* PWD_AUTH session state: true once a reader has successfully authenticated
 * WITHIN THIS ARMED SESSION. Deliberately session-scoped, not per-
 * activation/HLTA-cycle: real NXP silicon re-locks after every HALT, but
 * this transport (m1_t2t_transport.c, which this task does not touch --
 * see its own explicit scope boundary) has no per-activation hook exposed
 * to nfc_listener.c, since HLTA is intercepted and serviced entirely
 * inside the transport before CeHandleT2TCmdRx() is ever called. Resetting
 * once per armed session (rather than never, or incorrectly attempting a
 * transport-layer hook) is the honest, safe simplification: a reader that
 * proved it knows the password once is trusted for the rest of THIS
 * Emulate session, never carried into a different card or a fresh arm. */
static bool s_t2t_authenticated = false;

void nfc_listener_set_t2t_emu_image(const m1_t2t_emu_image_t *img)
{
    if (img == NULL) { return; }
    s_t2t_img = *img;
    s_t2t_img_armed = true;
    s_t2t_authenticated = false;
}

void nfc_listener_clear_t2t_emu_image(void)
{
    s_t2t_img_armed = false;
    s_t2t_authenticated = false;
    memset(&s_t2t_img, 0, sizeof(s_t2t_img));
}

const m1_t2t_emu_image_t* nfc_listener_get_t2t_emu_image(void)
{
    return s_t2t_img_armed ? &s_t2t_img : NULL;
}

/* T2T short-frame (4-bit, no CRC) ACK/NAK values -- ISO14443-3A / NFC
 * Forum T2T. A single generic NAK code (0x0) is used for every failure
 * case (invalid argument, locked page, out-of-range page): real tags use a
 * handful of distinct NAK subcodes, but this project has no verified
 * source for which exact subcode belongs to which failure short of
 * guessing, and every subcode means "failed, try again differently" to a
 * compliant reader either way -- 0x0 is never reported as success. */
#define T2T_ACK_NIBBLE   0x0AU
#define T2T_NAK_NIBBLE   0x00U

/*============================================================================*/
/**
 * @brief CeSendShortFrame - Send a T2T 4-bit short frame (ACK/NAK), blocking
 *
 * rfalTransceiveBlockingTx() always converts its length from bytes to bits
 * (rfalCreateByteFlagsTxRxContext), so it can never express a non-byte-
 * aligned frame -- the real T2T ACK/NAK is 4 bits with NO CRC and NO parity,
 * not a byte. This builds the rfalTransceiveContext directly (txBufLen in
 * BITS) and reproduces rfalTransceiveBlockingTx()'s own blocking loop using
 * only its public building blocks (rfalStartTransceive/rfalWorker/
 * rfalGetTransceiveStatus/rfalIsTransceiveInTx/rfalIsTransceiveInRx) --
 * rfalTransceiveRunBlockingTx() itself is file-static inside the vendor
 * rfal_rfst25r3916.c and not exported.
 *
 * @param[in] nibble Low 4 bits are sent as the short frame (T2T_ACK_NIBBLE / T2T_NAK_NIBBLE)
 * @retval RFAL_ERR_NONE Success
 * @retval Other RFAL error codes
 */
/*============================================================================*/
static ReturnCode CeSendShortFrame(uint8_t nibble)
{
    uint8_t txByte = (uint8_t)(nibble & 0x0FU);

    rfalTransceiveContext ctx;
    ctx.txBuf     = &txByte;
    ctx.txBufLen  = 4U;   /* BITS -- the whole reason this can't use rfalTransceiveBlockingTx() */
    ctx.rxBuf     = NULL;
    ctx.rxBufLen  = 0U;
    ctx.rxRcvdLen = NULL;
    ctx.flags     = (uint32_t)RFAL_TXRX_FLAGS_DEFAULT
                   | (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL   /* no CRC on a short frame */
                   | (uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE;    /* no parity on a short frame */
    ctx.fwt       = rfalConvUsTo1fc(500U);

    ReturnCode err = rfalStartTransceive(&ctx);
    if (err != RFAL_ERR_NONE) { return err; }

    do {
        rfalWorker();
        err = rfalGetTransceiveStatus();
    } while (rfalIsTransceiveInTx() && (err == RFAL_ERR_BUSY));

    if (rfalIsTransceiveInRx()) {
        return RFAL_ERR_NONE;
    }
    return err;
}

/* Re-arm RX and update phase -- the same three lines repeated after every
 * TX in CeHandleT2TCmdRx(); factored out once for the new image-backed
 * cases so the pattern isn't re-typed a sixth time.
 *
 * CeHandleT2TCmdRx() itself and its response builders are otherwise
 * unchanged by the dedicated Type-2 transport (m1_t2t_transport.c): this is
 * the ONLY seam. When that transport owns the session, rearming through
 * rfal_nfc.c's rfalNfcDataExchangeStart() would be meaningless (that
 * module's state, gNfcDev, was never advanced by the transport, which calls
 * rfalListenStart()/rfalStartTransceive() directly instead) -- so rearm
 * through the transport's own directly-owned rfalStartTransceive() instead.
 * Every other persona (still driven by ListenerCycle()/rfal_nfc.c) is
 * completely unaffected: this branch is only ever true for T2T. */
static bool CeRearmRxAfterTx(void)
{
    if (m1_t2t_transport_is_active()) {
        return m1_t2t_transport_rearm_rx();
    }

    g_ceTxPending = false;
    ReturnCode err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
    if (err == RFAL_ERR_NONE) {
        s_cePhase = CE_PHASE_WAIT_RX;
    }
    return (err == RFAL_ERR_NONE);
}


__attribute__((weak)) void Listener_OnActivated(const rfalNfcDevice* dev) { (void)dev; }
__attribute__((weak)) void Listener_OnDeactivated(void) {}

static void CeApplyDiscParamToActiveDev( rfalNfcDevice *dev );
//static uint16_t CeBuildEmulationResponse( const uint8_t *rx, uint16_t rxLenB, uint8_t *tx, uint16_t txSize );
static ReturnCode CeArmRx(void);
static void ListenerNotif( rfalNfcState st );


/*============================================================================*/
/**
 * @brief ListenerGetLastRx - Get last received data for listener
 * 
 * @param[out] lenBits Pointer to store received length in bits
 * @retval Pointer to last received data buffer, or NULL if no data
 */
/*============================================================================*/
const uint8_t* ListenerGetLastRx(uint16_t *lenBits)
{
    if (lenBits) *lenBits = s_lastRxBits;
    return s_lastRxBits ? s_lastRxBuf : NULL;
}

/*============================================================================*/
/**
 * @brief ListenerGetActiveDev - Get active device for listener
 * 
 * @retval Pointer to active NFC device, or NULL if none
 */
/*============================================================================*/
const rfalNfcDevice* ListenerGetActiveDev(void)
{
    return s_active_dev;
}

/*============================================================================*/
/**
 * @brief ListenerRequestStop - Request CE stop from external
 * 
 * @retval None
 */
/*============================================================================*/
void ListenerRequestStop(void) { s_listener_stop = true; }

/*============================================================================*/
/**
 * @brief NFC_T2TTransportIsActive / NFC_T2TTransportProcess - see
 * nfc_listener.h. The nfc_driver.c ownership gate calls IsActive() before
 * nfc_process_func() (mirroring m1_mfc_raw_hw_active()'s existing role) and,
 * when true, calls Process() each worker-task tick instead. s_listener_stop
 * is serviced here (not inside m1_t2t_transport.c, which stays free of
 * UI/stop-flag concerns) so rfalListenStop() only ever runs on the worker
 * task, matching the SPI-single-owner-task contract every other RFAL/ST25R
 * access in this driver already follows -- ListenerRequestStop() itself
 * (called from the UI/menu task) only ever sets a flag, never touches the
 * radio directly.
 */
/*============================================================================*/
bool NFC_T2TTransportIsActive(void)
{
    return m1_t2t_transport_is_active();
}

void NFC_T2TTransportProcess(void)
{
    if (s_listener_stop) {
        m1_t2t_transport_stop();
        s_listener_stop = false;
        state     = IDLE;
        s_cePhase = CE_PHASE_IDLE;
        platformLog("[T2T-TP] stop requested -> transport stopped\r\n");
        return;
    }
    m1_t2t_transport_tick();
}

/*============================================================================*/
/**
 * @brief CeArmRx - Arm RFAL RX for the next frame (LISTEN/T2T)
 * 
 * Arms the RFAL receiver to wait for the next command from reader.
 * 
 * @retval RFAL_ERR_NONE Success
 * @retval Other RFAL error codes
 */
/*============================================================================*/
static ReturnCode CeArmRx(void)
{
    s_ceRxData   = NULL;
    s_ceRxRcvLen = NULL;
    ReturnCode err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
    platformLog("[CE] Arm RX err=%d\r\n", err);
    return err;
}

static void CeApplyDiscParamToActiveDev( rfalNfcDevice *dev )
{
    if (dev == NULL) {
        return;
    }

    /* dev->type records the REMOTE device's role (from RFAL's own perspective),
     * not ours -- in listen mode the remote is always an NFC-A poller, so this
     * must be RFAL_NFC_POLL_TYPE_NFCA. The previous RFAL_NFC_LISTEN_TYPE_NFCA
     * here made rfalNfcIsRemDevPoller(dev->type) false inside RFAL's own
     * rfalNfcDataExchangeStart() (rfal_nfc.c), which is the ONLY branch that
     * hands back the already-buffered first post-activation frame; failing
     * that check made it fall through to the poller-oriented interface
     * switch and start a fresh (wrong) zero-length transceive instead of
     * returning the reader's first command -- silently losing it every time,
     * for every T2T-family variant identically (root cause of a reader
     * reporting "ISO14443A Unknown" despite a correct UID: activation/UID
     * comes from RFAL's own lower listen state machine, entirely before this
     * point, so it was never affected). */
    dev->type = RFAL_NFC_POLL_TYPE_NFCA;

    /* rfInterface must NOT be forced here for T4T: rfal_nfc.c's own CARDEMU_4A
     * activation (RATS/PPS) already sets it to RFAL_NFC_INTERFACE_ISODEP
     * before gNfcDev.state ever reaches ACTIVATED, and overwriting that back
     * to RF would break ISO-DEP data exchange. T2T/RAW/MFC_DETECT never
     * negotiate ISO-DEP (rfal_nfc.c's T2T-family activation branch returns
     * ACTIVATE immediately on the first raw frame, never setting
     * rfInterface itself), so they need it explicit rather than relying on
     * the zero-initialized default happening to already be RF. MFC_EMU never
     * reaches this function at all (intercepted in nfc_driver.c before
     * nfc_process_func() runs for that persona). */
    if (g_persona != EMU_PERSONA_T4T) {
        dev->rfInterface = RFAL_NFC_INTERFACE_RF;
    }

    uint8_t uidLen = (discParam.lmConfigPA.nfcidLen == RFAL_LM_NFCID_LEN_07) ? 7U : 4U;

    if (dev->nfcid != NULL) {
        dev->nfcidLen = uidLen;
        ST_MEMCPY(dev->nfcid, discParam.lmConfigPA.nfcid, uidLen);
    }

    dev->dev.nfca.nfcId1Len = uidLen;
    ST_MEMCPY(dev->dev.nfca.nfcId1, discParam.lmConfigPA.nfcid, uidLen);

    dev->dev.nfca.sensRes.anticollisionInfo = discParam.lmConfigPA.SENS_RES[0];
    dev->dev.nfca.sensRes.platformInfo      = discParam.lmConfigPA.SENS_RES[1];
    dev->dev.nfca.selRes.sak                = discParam.lmConfigPA.SEL_RES;
#if defined(M1_MFC_RAW_EMULATION)
    /* MFC_EMU is a raw MIFARE Classic card: its post-SELECT frames (AUTH 0x60/0x61,
     * READ 0x30, ...) are raw NFC-A, NOT ISO-DEP. Marking the active device T4T
     * routes rfalNfcDataExchange through the ISO-DEP layer, which drops the first
     * AUTH before it reaches the raw handler. Present it as T2T (raw NFC-A), same
     * as the RAW persona. UID/ATQA/SAK above are unchanged. */
    dev->dev.nfca.type = ((g_persona == EMU_PERSONA_RAW) || (g_persona == EMU_PERSONA_MFC_EMU))
                             ? RFAL_NFCA_T2T : RFAL_NFCA_T4T;
#else
    dev->dev.nfca.type = (g_persona == EMU_PERSONA_RAW) ? RFAL_NFCA_T2T : RFAL_NFCA_T4T;
#endif
    dev->dev.nfca.isSleep = false;
#if defined(M1_MFC_RAW_EMULATION)
    if (!m1_mfc_in_auth_crit())   /* deferred inside the AUTH->nonce window */
#endif
    { platformLog("[CE] Applied Discovery Param to Active Dev\r\n"); }
}

/*============================================================================*/
/**
 * @brief CeBuildEmulationResponse - Build emulation response based on received frame
 * 
 * Receives RX frame and builds appropriate emulation response based on emulation type.
 * 
 * @param[in] rx Received data buffer
 * @param[in] rxLenB Received data length in bytes
 * @param[out] tx Transmission buffer
 * @param[in] txSize Transmission buffer size
 * @retval Response length in bytes, or 0 on error
 */
/*============================================================================*/
static uint16_t __attribute__((unused)) CeBuildEmulationResponse( const uint8_t *rx, uint16_t rxLenB, uint8_t *tx, uint16_t txSize )
{
    if (!rx || rxLenB == 0U || !tx || txSize == 0U) {
        platformLog("[CE][DBG] CeBuildEmulationResponse: invalid args rx=%p rxLenB=%u tx=%p txSize=%u\r\n",
                    rx, (unsigned)rxLenB, tx, (unsigned)txSize);
        return 0;
    }

    nfc_run_ctx_t *c = nfc_ctx_get();
    if (!c) {
        platformLog("[CE][DBG] CeBuildEmulationResponse: no context\r\n");
        return 0;
    }

    /* Currently only NFC-A based emulation is handled */
    if (c->head.tech != M1NFC_TECH_A) {
        platformLog("[CE][DBG] CeBuildEmulationResponse: unsupported tech=%d\r\n", c->head.tech);
        return 0;
    }

    switch (c->head.family) {
    case M1NFC_FAM_CLASSIC:
        /* TODO: Replace with ceMfc_FromDump when implemented */
        platformLog("[CE][DBG] Classic not implemented\r\n");
        return 0;

    case M1NFC_FAM_ULTRALIGHT:
        return ceT2T_FromDump(rx, rxLenB, tx, txSize);
    case M1NFC_FAM_DESFIRE:
        /* TODO: Replace with ceDesfire_FromDump when implemented */
        if (txSize >= 2) { tx[0] = 0x90; tx[1] = 0x00; return 2; }
        platformLog("[CE][DBG] Desfire not implemented\r\n");
        return 0;

    default:
        platformLog("[CE][DBG] Unknown family=%d\r\n", c->head.family);
        return 0;
    }

    return 0;
}





/*============================================================================*/
/**
 * @brief SendGetVersion - Handle GET_VERSION command (send response if cmd==0x60, else return false)
 * 
 * @param[in] rx Received data buffer
 * @param[in] rxLenB Received data length in bytes
 * @retval true If GET_VERSION command was handled
 * @retval false If not GET_VERSION command
 */
/*============================================================================*/
bool SendGetVersion(const uint8_t *rx, uint16_t rxLenB)
{
    if (!rx || rxLenB < 1U || rx[0] != 0x60) {
        return false;
    }

    /* NTAG215 example version */
    uint8_t ver[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
    platformLog("[CE][TX] GET_VERSION rsp: %s\r\n", hex2Str(ver, sizeof(ver)));

    s_firstRxHandled = true;
    rfalNfcDataExchangeStart(ver, rfalConvBytesToBits(sizeof(ver)), &s_rxData, &s_rcvLen, RFAL_FWT_NONE);
    return true;
}

/*============================================================================*/
/**
 * @brief CeBuildT2TReadResp - Build T2T READ response
 * 
 * Builds response for T2T READ command (0x30) by reading 4 pages from context.
 * 
 * @param[in] rx Received data buffer
 * @param[in] rxBits Received data length in bits
 * @retval RFAL_ERR_NONE Success
 * @retval RFAL_ERR_PARAM Invalid parameters
 */
/*============================================================================*/
static ReturnCode CeBuildT2TReadResp(const uint8_t *rx, uint16_t rxBits)
{
    if (!rx || rxBits < 16) return RFAL_ERR_PARAM;

    uint16_t rxBytes = rfalConvBitsToBytes(rxBits);
    if (rxBytes < 2) return RFAL_ERR_PARAM;

    uint8_t startPage = rx[1];
    uint16_t txLen = 0;

    /* Prepare 4 pages of data (g_ceTxBuf is already initialized externally) */
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t page = (uint16_t)startPage + i;
        uint16_t bufOffset = txLen;

        if (!nfc_ctx_get_t2t_page(page, &g_ceTxBuf[bufOffset])) {
            /* Fill with 0 if page doesn't exist */
            g_ceTxBuf[bufOffset]     = 0x00;
            g_ceTxBuf[bufOffset + 1] = 0x00;
            g_ceTxBuf[bufOffset + 2] = 0x00;
            g_ceTxBuf[bufOffset + 3] = 0x00;
        }
        txLen += 4;
    }

    g_ceTxLenBytes = (uint16_t)txLen;
    return RFAL_ERR_NONE;
}

/* Image-backed READ (0x30): 4 pages starting at startPage, WRAPPING around
 * at page_count (matches the real NXP tags' documented behavior -- reading
 * past the last page continues from page 0, it does not zero-fill). Only
 * used while s_t2t_img_armed; the legacy CeBuildT2TReadResp() above is
 * unchanged and still serves the unarmed fallback path. */
static void CeBuildT2TReadRespFromImage(uint8_t startPage)
{
    uint16_t pc = s_t2t_img.page_count;
    for (uint8_t i = 0; i < 4U; i++) {
        uint16_t page = (uint16_t)((startPage + i) % pc);
        memcpy(&g_ceTxBuf[(uint16_t)i * 4U], s_t2t_img.page[page], 4U);
    }
    g_ceTxLenBytes = 16U;
}


/* T2T/Ultralight/NTAG command opcodes not already named in nfc_listener.h
 * (T2T_CMD_READ/FAST_READ/WRITE/COMPAT_WRITE/GET_VERSION/SECTOR_SEL), per
 * NXP datasheet (MF0ICU2/NTAG213-215-216 product specs). Named to replace
 * magic literals below, and to correct an earlier mislabeled comment (an
 * older revision of this switch called 0x39 "READ_SIG" and 0x53 "PWD_AUTH"
 * -- neither matches the real NXP-assigned byte for that command name). */
#define T2T_CMD_PWD_AUTH         0x1BU
#define T2T_CMD_READ_CNT         0x39U
#define T2T_CMD_READ_SIG         0x3CU
#define T2T_CMD_CHECK_TEARING    0x3EU

/*============================================================================*/
/**
 * @brief CeHandleT2TCmdRx - Dispatch T2T commands
 *
 * Handles T2T commands from reader:
 * - 0x30 (READ): Read 4 pages
 * - 0x3A (FAST_READ): Read multiple pages
 * - 0x60 (GET_VERSION): Send version info
 * - 0xA2 (WRITE): Write one page
 * - 0x3C (READ_SIGNATURE), 0x39 (READ_CNT), 0x3E (CHECK_TEARING),
 *   0x1B (PWD_AUTH): answered from genuine captured/loaded data only
 *   (see m1_t2t_emu_image.h) -- never fabricated
 * - Others: Simple ACK responses
 *
 * @param[in] rx Received data buffer
 * @param[in] rxBits Received data length in bits
 * @retval true If command was handled successfully
 * @retval false If command was not handled or error occurred
 */
/*============================================================================*/
static bool CeHandleT2TCmdRx(const uint8_t *rx, uint16_t rxBits)
{
    if (!rx || rxBits < 8) return false;
    
    uint16_t rxBytes = rfalConvBitsToBytes(rxBits);
    if (rxBytes < 1) return false;
    
    uint8_t cmd = rx[0];
    ReturnCode err;
    //uint32_t fwt;
    
    switch (cmd) {
        case T2T_CMD_READ: {
            /* READ command: Prepare response then start TX immediately */
            if (rxBytes < 2) {
                return false;
            }
            uint8_t startPage = rx[1];

            if (s_t2t_img_armed) {
                if (s_t2t_img.page_count == 0U) { return false; }
                /* PROT=1 (read+write protection): pages at/after AUTH0 are
                 * refused, never disclosed, until this session has proven
                 * it knows the real password via a genuine PWD_AUTH match.
                 * PROT=0 (write-only) never gates READ. Matches this file's
                 * own established "refuse rather than silently truncate/
                 * fabricate" idiom (see FAST_READ's out-of-range refusal
                 * below). */
                if (s_t2t_img.protected_tag && s_t2t_img.prot && !s_t2t_authenticated &&
                    (startPage >= s_t2t_img.auth0)) {
                    err = CeSendShortFrame(T2T_NAK_NIBBLE);
                    if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
                    return CeRearmRxAfterTx();
                }
                CeBuildT2TReadRespFromImage(startPage);
            } else {
                err = CeBuildT2TReadResp(rx, rxBits);
                if (err != RFAL_ERR_NONE || g_ceTxLenBytes == 0U) {
                    return false;
                }
            }

            /* Immediate synchronous transmission: Meet T2T FDT 86~177μs requirement */
            err = rfalTransceiveBlockingTx(g_ceTxBuf, g_ceTxLenBytes, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) {
                platformLog("[CE] READ TX err=%d\r\n", err);
                return false;
            }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_FAST_READ: {
            /* FAST_READ command: Read multiple pages sequentially (start ~ end) */
            if (rxBytes < 3) {
                return false;
            }

            uint8_t startPage = rx[1];
            uint8_t endPage   = rx[2];

            if (s_t2t_img_armed) {
                uint16_t pc = s_t2t_img.page_count;
                /* Reject rather than silently clamp/truncate: an out-of-range
                 * or inverted request is an invalid argument, not something
                 * to paper over with partial data. Same principle covers a
                 * PROT=1 range touching any page at/after AUTH0 while
                 * unauthenticated -- refused whole, never silently
                 * truncated to "just the allowed prefix". */
                bool auth_blocked = s_t2t_img.protected_tag && s_t2t_img.prot &&
                                    !s_t2t_authenticated && (endPage >= s_t2t_img.auth0);
                if ((pc == 0U) || (endPage < startPage) || (startPage >= pc) || (endPage >= pc) ||
                    auth_blocked) {
                    err = CeSendShortFrame(T2T_NAK_NIBBLE);
                    if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
                    return CeRearmRxAfterTx();
                }
                uint16_t numPages = (uint16_t)(endPage - startPage + 1);
                uint16_t txLen = 0;
                for (uint16_t i = 0; i < numPages; i++) {
                    if (txLen + 4U > sizeof(g_ceTxBuf)) break;
                    memcpy(&g_ceTxBuf[txLen], s_t2t_img.page[startPage + i], 4U);
                    txLen += 4U;
                }
                err = rfalTransceiveBlockingTx(g_ceTxBuf, txLen, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
                if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
                return CeRearmRxAfterTx();
            }

            uint16_t pageCnt = nfc_ctx_get_t2t_page_count();

            if (endPage < startPage) {
                return false;
            }

            if (endPage >= pageCnt) {
                endPage = (uint8_t)(pageCnt - 1);
            }

            uint16_t numPages = (uint16_t)(endPage - startPage + 1);
            uint16_t txLen = 0;
            uint8_t pageBuf[4];

            for (uint16_t i = 0; i < numPages; i++) {
                uint16_t page = (uint16_t)startPage + i;
                if (!nfc_ctx_get_t2t_page(page, pageBuf)) {
                    pageBuf[0] = pageBuf[1] = pageBuf[2] = pageBuf[3] = 0x00;
                }

                if (txLen + 4U > sizeof(g_ceTxBuf)) break;
                memcpy(&g_ceTxBuf[txLen], pageBuf, 4);
                txLen += 4;
            }

            /* Immediate synchronous transmission */
            err = rfalTransceiveBlockingTx(g_ceTxBuf, txLen, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) {
                return false;
            }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_GET_VERSION: {
            /* GET_VERSION command: NTAG/Ultralight-C series */
            uint8_t ver[8];

            if (s_t2t_img_armed) {
                if (!m1_t2t_emu_image_get_version(s_t2t_img.variant, ver)) {
                    return false;   /* variant (e.g. original UL) has no GET_VERSION -- never fabricate */
                }
            } else if (!nfc_ctx_get_t2t_version(ver)) {
                platformLog("[CE] GET_VER: no data\r\n");
                return false;
            }

            platformLog("[CE] GET_VER TX: %s\r\n", hex2Str(ver, 8));

            /* Immediate synchronous transmission: Meet T2T FDT 86~177μs requirement */
            err = rfalTransceiveBlockingTx(ver, sizeof(ver), NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE) {
                platformLog("[CE] GET_VER TX err=%d\r\n", err);
                return false;
            }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_WRITE:
        case T2T_CMD_COMPAT_WRITE: {
            /* WRITE (0xA2, 4-byte payload) / COMPATIBILITY_WRITE (0xA0,
             * 16-byte payload, only the first 4 bytes are actually stored --
             * both per NXP datasheet). COMPATIBILITY_WRITE is only handled
             * while armed: the legacy path never implemented it (falls to
             * default: false below), and there's no legacy-context reason to
             * start now -- only the saved-card path exercises it. */
            if (cmd == T2T_CMD_COMPAT_WRITE) {
                if (!s_t2t_img_armed || (rxBytes < 18)) { return false; }
            } else if (rxBytes < 6) {
                return false;
            }

            uint8_t page = rx[1];

            if (s_t2t_img_armed) {
                /* WRITE past AUTH0 is ALWAYS auth-gated when protected,
                 * regardless of PROT (PROT only distinguishes read+write
                 * vs write-only protection -- write is never the "allowed"
                 * side of that distinction). */
                bool auth_blocked = s_t2t_img.protected_tag && !s_t2t_authenticated &&
                                    (page >= s_t2t_img.auth0);
                bool ok = !auth_blocked && m1_t2t_emu_image_write_page(&s_t2t_img, page, &rx[2]);
                err = CeSendShortFrame(ok ? T2T_ACK_NIBBLE : T2T_NAK_NIBBLE);
                if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
                return CeRearmRxAfterTx();
            }

            uint8_t data[4];
            memcpy(data, &rx[2], 4);

            /* Save page data */
            nfc_ctx_set_t2t_page(page, data);

            /* WRITE response: ACK (0x0A), immediate synchronous transmission */
            g_ceTxBuf[0] = 0x0A;
            err = rfalTransceiveBlockingTx(g_ceTxBuf, 1, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) {
                return false;
            }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_READ_SIG: {
            /* READ_SIGNATURE: genuine captured 32-byte originality signature
             * only. m1_t2t_emu_image_build()'s eligibility gate already
             * refuses emulation for any signature-capable variant (UL11/
             * NTAG213/215/216) unless a real signature was captured, so
             * signature_valid is guaranteed true here whenever armed with
             * such a variant -- the checks below are a defensive backstop,
             * not the actual gate. Plain UL predates this command entirely
             * (requires_signature=false) and genuinely doesn't answer it. */
            if (s_t2t_img_armed) {
                if (!s_t2t_img.requires_signature || !s_t2t_img.signature_valid) { return false; }
                memcpy(g_ceTxBuf, s_t2t_img.signature, 32U);
            } else {
                if (!nfc_ctx_t2t_signature_valid() || !nfc_ctx_get_t2t_signature(g_ceTxBuf)) { return false; }
            }
            err = rfalTransceiveBlockingTx(g_ceTxBuf, 32U, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_READ_CNT: {
            /* READ_CNT: genuine per-index captured counter, or a protocol-
             * correct NAK for an index never captured -- proven tolerated
             * by readers (ReadCounters routes forward on any
             * error, never to ReadFailed), so a NAK here is a real,
             * spec-compliant answer, not a dropped command. */
            if (rxBytes < 2) { return false; }
            uint8_t idx = rx[1];
            uint8_t val[3];
            bool haveVal = false;
            if (idx < 3U) {
                if (s_t2t_img_armed) {
                    if (s_t2t_img.counter_valid[idx]) {
                        memcpy(val, s_t2t_img.counter[idx], 3U);
                        haveVal = true;
                    }
                } else {
                    haveVal = nfc_ctx_get_t2t_counter(idx, val);
                }
            }
            if (haveVal) {
                err = rfalTransceiveBlockingTx(val, 3U, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            } else {
                err = CeSendShortFrame(T2T_NAK_NIBBLE);
            }
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_CHECK_TEARING: {
            /* CHECK_TEARING_EVENT: genuine per-index captured flag, or a
             * protocol-correct NAK. NTAG213/215/216 genuinely do not support
             * this command; that NAK is expected for those variants. */
            if (rxBytes < 2) { return false; }
            uint8_t idx = rx[1];
            uint8_t val = 0;
            bool haveVal = false;
            if (idx < 3U) {
                if (s_t2t_img_armed) {
                    if (s_t2t_img.tearing_valid[idx]) {
                        val = s_t2t_img.tearing[idx];
                        haveVal = true;
                    }
                } else {
                    haveVal = nfc_ctx_get_t2t_tearing(idx, &val);
                }
            }
            if (haveVal) {
                err = rfalTransceiveBlockingTx(&val, 1U, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            } else {
                err = CeSendShortFrame(T2T_NAK_NIBBLE);
            }
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_PWD_AUTH: {
            /* PWD_AUTH: answers from a genuinely-verified credential only
             * (see nfc_ctx_t2t_credential_valid() / the Unlock feature,
             * NFC-T2T-Unlock task) -- never the masked PWD/PACK page bytes.
             *
             * An earlier revision of this case compared the incoming
             * password against s_t2t_img.page[cfg0_page+2] (the PWD page),
             * reasoning that it was ordinary data within the "every page
             * read" eligibility gate. That reasoning was wrong: genuine NXP
             * silicon masks the PWD/PACK pages to zero on an ordinary READ,
             * regardless of AUTH0/protection state (the password is write-
             * only, never disclosed via READ). So a "captured" PWD page
             * was never the true password -- just the same masked zero
             * bytes. The ONLY genuine credential source is a real PWD_AUTH
             * exchange that a physical tag itself accepted (Unlock), which
             * is what s_t2t_img.credential_valid/pwd/pack hold. Without
             * one, this still answers with a genuine, protocol-correct NAK
             * exactly as before. */
            if (rxBytes < 5) { return false; }

            if (s_t2t_img_armed && s_t2t_img.credential_valid &&
                (memcmp(&rx[1], s_t2t_img.pwd, 4U) == 0)) {
                s_t2t_authenticated = true;
                /* Genuine PACK response is a normal 2-byte CRC'd frame, NOT
                 * the 4-bit short-frame ACK/NAK WRITE uses -- matches
                 * GET_VERSION's own rfalTransceiveBlockingTx() pattern. */
                err = rfalTransceiveBlockingTx(s_t2t_img.pack, 2U, NULL, 0, NULL,
                                               RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
                if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
                return CeRearmRxAfterTx();
            }

            err = CeSendShortFrame(T2T_NAK_NIBBLE);
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) { return false; }
            return CeRearmRxAfterTx();
        }

        case T2T_CMD_SECTOR_SEL:
        case 0x57:
        case 0xE0: {
            /* SECTOR_SELECT and two non-standard legacy opcodes: no multi-sector
             * addressing needed within our supported page-count range), and
             * never extended to the armed, saved-card path -- unchanged from
             * before this task. */
            if (s_t2t_img_armed) {
                return false;
            }

            uint8_t respLen = 1;
            g_ceTxBuf[0] = 0x0A;  /* ACK */

            err = rfalTransceiveBlockingTx(g_ceTxBuf, respLen, NULL, 0, NULL, RFAL_TXRX_FLAGS_DEFAULT, rfalConvUsTo1fc(500U));
            if (err != RFAL_ERR_NONE && err != RFAL_ERR_LINK_LOSS) {
                return false;
            }
            return CeRearmRxAfterTx();
        }

        default:
            return false;
    }
}


/*============================================================================*/
/**
 * @brief ListenIni - Initialize NFC listener (CARD EMULATION / LISTENER) Set Profile
 * 
 * Initializes RFAL in listener mode for card emulation.
 * Configures discovery parameters, sets UID/ATQA/SAK for emulation,
 * and starts discovery process.
 * 
 * @retval true Initialization successful
 * @retval false Initialization failed
 */
/*============================================================================*/
bool ListenIni(void)
{
    ReturnCode err = RFAL_ERR_NONE;

    /* --- Global CE state reset --- */
    s_cePhase       = CE_PHASE_IDLE;
    s_rxData        = NULL;
    s_rcvLen        = NULL;
    s_active_dev    = NULL;
    s_listener_stop = false;
    s_wasActivated  = false;
    s_firstRxPending = false;
    state           = IDLE;

    for (int i = 0; i < 2; i++) {
        err = rfalNfcInitialize();
        //platformLog("rfalNfcInitialize() = %d\r\n", err);
        if (err == RFAL_ERR_NONE) break;
        vTaskDelay(5);
    }
    if (err != RFAL_ERR_NONE) return false;

    /* 2) Discovery parameters: Listener(CE) only */
    rfalNfcDefaultDiscParams(&discParam);

    // --- CE-only safety guard (remove POLL traces/garbage values) ---
    discParam.maxBR  = RFAL_BR_KEEP;

    discParam.GBLen  = 0;
    discParam.ap2pBR = RFAL_BR_106;     // Safe default value even if not used
    discParam.nfcfBR = RFAL_BR_212;     // Safe even if only LISTEN_F is enabled, compared to POLL_F

    // Remove all POLL bits and set only LISTEN
    discParam.techs2Find &= ~( RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B |
                            RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V |
                            RFAL_NFC_POLL_TECH_AP2P | RFAL_NFC_POLL_TECH_ST25TB );

    discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_A;

    discParam.devLimit       = 1U;
    discParam.totalDuration  = 60000U;                 /* CE waits longer (e.g., 60s) */
    discParam.notifyCb       = ListenerNotif;
#if defined(RFAL_COMPLIANCE_MODE_NFC)
    discParam.compMode       = RFAL_COMPLIANCE_MODE_NFC;
#endif

    /* Turn off all Poller, enable only Listen */
    discParam.techs2Find     = RFAL_NFC_TECH_NONE;
    discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_A;   /* CE-A active */

    /* --- Detect Reader (gated): forced MIFARE Classic 1K identity ----------
     * When the caller selected EMU_PERSONA_MFC_DETECT, present a fixed 4-byte
     * UID / ATQA 0x0400 / SAK 0x08 (Classic 1K) and hand the session to the
     * isolated capture core. Entirely separate from the normal personas below;
     * nothing here runs unless MFC_DETECT is explicitly selected. */
    if (g_persona == EMU_PERSONA_MFC_DETECT) {
        static const uint8_t mfc_dr_uid[4] = { 0x01, 0x02, 0x03, 0x04 };
        discParam.lmConfigPA.nfcidLen    = RFAL_LM_NFCID_LEN_04;
        ST_MEMCPY(discParam.lmConfigPA.nfcid, mfc_dr_uid, 4);
        discParam.lmConfigPA.SENS_RES[0] = 0x04;   /* ATQA 0x0400 (4-byte UID) */
        discParam.lmConfigPA.SENS_RES[1] = 0x00;
        discParam.lmConfigPA.SEL_RES     = 0x08;   /* SAK 0x08 = MIFARE Classic 1K */
        mfc_detect_begin();
        state = IDLE;
        platformLog("[CE] Persona=MFC_DETECT ATQA=0400 SAK=08 (Detect Reader)\r\n");
        return true;
    }

#if defined(M1_MFC_RAW_EMULATION)
    /* --- Raw MIFARE Classic emulation (gated): present a saved card identity --
     * MonstaTek Scope B. Mirrors the MFC_DETECT block but sources UID/ATQA/SAK
     * from a read/loaded MIFARE Classic card (fallback: fixed 1K). The isolated
     * protocol layer (m1_mfc_raw_listener) owns the session. Nothing here runs
     * unless EMU_PERSONA_MFC_EMU is explicitly selected. */
    if (g_persona == EMU_PERSONA_MFC_EMU) {
        platformLog("[RAW-TRACE] ListenIni MFC_EMU branch caller=%p\r\n", __builtin_return_address(0));

        /* Single-session invariant: this branch is the ONE raw listener
         * initialization per MFC Emulate entry. Since m1_mfc_raw_hw_run()
         * (the sole hardware entry point) runs synchronously, blocking, in
         * THIS SAME nfc_worker_task, from nfc_driver.c's NFC_STATE_PROCESS --
         * called only after ListenIni() has already returned -- ListenIni()
         * being re-entered while m1_mfc_raw_hw_active() is true would require
         * this task to call itself reentrantly, which is not possible on a
         * single program counter. This check is retained purely as a canary:
         * if it ever fires, something has broken the single-task-ownership
         * invariant, not merely raced a timing window. */
        if (m1_mfc_raw_hw_active()) {
            platformLog("[RAW-INVARIANT-VIOLATION] ListenIni() MFC_EMU re-entered while raw session active\r\n");
        }

        /* Identity: m1_mfc_raw_begin() resolves it independently (fixed
         * persona UID 01020304 / ATQA 0400 / SAK 08 -- see m1_mfc_raw_hw_init
         * in m1_mfc_raw_listener.c) and calls m1_mfc_raw_hw_session_start(),
         * which programs the ST25R3916 PT-memory itself via rfalListenStart()
         * -- NOT via discParam/
         * rfalNfcDiscover(), which is never invoked for this persona. */
        platformLog("[CE] Persona=MFC_EMU (raw passive-target emulation)\r\n");
        m1_mfc_raw_begin();
        state = IDLE;
        return true;
    }
#endif

    EmuNfcA_t emuA;
    bool has_ctx = Emu_GetNfcA(&emuA);
    const uint8_t default_uid[7] = { 0x08, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

    /* UID fill (fallback to default when no emu context) */
    if (has_ctx) {
        discParam.lmConfigPA.nfcidLen = (emuA.uid_len == 7)
                                      ? RFAL_LM_NFCID_LEN_07
                                      : RFAL_LM_NFCID_LEN_04;
        ST_MEMCPY(discParam.lmConfigPA.nfcid, emuA.uid,
                  (discParam.lmConfigPA.nfcidLen == RFAL_LM_NFCID_LEN_07) ? 7 : 4);
    } else {
        discParam.lmConfigPA.nfcidLen = RFAL_LM_NFCID_LEN_04;
        ST_MEMCPY(discParam.lmConfigPA.nfcid, default_uid, 4);
        platformLog("[CE] No emu context; using default UID\r\n");
    }

    /* Fill ATQA/SAK + auto-infer persona */
    if (has_ctx) {
        /* Check NFC context family and force T2T if applicable */
        nfc_run_ctx_t* ctx = nfc_ctx_get();
        bool is_t2t_family = (ctx && ctx->head.family == M1NFC_FAM_ULTRALIGHT);
        
        if (is_t2t_family) {
            /* If T2T family: Force SAK=0x00, ATQA=0x44 0x00 (prevent ISO-DEP) */
            discParam.lmConfigPA.SENS_RES[0] = 0x44;
            discParam.lmConfigPA.SENS_RES[1] = 0x00;
            discParam.lmConfigPA.SEL_RES     = 0x00;
            g_persona = EMU_PERSONA_T2T;
            platformLog("[CE] T2T family: forcing ATQA=0x4400 SAK=0x00 (ISO-DEP disabled)\r\n");
        } else {
            /* Apply context values as-is */
            discParam.lmConfigPA.SENS_RES[0] = emuA.atqa[0];
            discParam.lmConfigPA.SENS_RES[1] = emuA.atqa[1];
            discParam.lmConfigPA.SEL_RES     = emuA.sak;

            /* Infer persona based on ATQA/SAK */
            if (emuA.sak == 0x20) {
                /* Type 4A (ISO-DEP / DESFire series) */
                g_persona = EMU_PERSONA_T4T;
                platformLog("[CE] Persona=T4T ATQA=%02X%02X SAK=%02X\r\n", emuA.atqa[0], emuA.atqa[1], emuA.sak);
                discParam.lmConfigPA.SEL_RES     = 0x00; // Force SAK 0x00 for T4T compatibility with common NFC readers (temporary)
            } else if (emuA.atqa[0] == 0x44 && emuA.atqa[1] == 0x00 && emuA.sak == 0x00) {
                /* NTAG/Ultralight (Type 2) */
                g_persona = EMU_PERSONA_T2T;
                platformLog("[CE] Persona=T2T ATQA=%02X%02X SAK=%02X\r\n", emuA.atqa[0], emuA.atqa[1], emuA.sak);
            } else {
                /* Others: Use ATQA/SAK from read card as-is */
                g_persona = EMU_PERSONA_RAW;
                platformLog("[CE] Persona=RAW ATQA=%02X%02X SAK=%02X\r\n", emuA.atqa[0], emuA.atqa[1], emuA.sak);
            }
        }

    } else {
        /* If no context, force default T2T values */
        discParam.lmConfigPA.SENS_RES[0] = 0x44;
        discParam.lmConfigPA.SENS_RES[1] = 0x00;
        discParam.lmConfigPA.SEL_RES     = 0x00;
        g_persona = EMU_PERSONA_T2T;
        platformLog("[CE] Persona=T2T (default) ATQA=4400 SAK=00\r\n");
    }


    /* Check if T2T dump is ready */
    {
        uint16_t t2_pages = nfc_ctx_get_t2t_page_count();
        if (t2_pages == 0) {
            platformLog("[CE] WARNING: T2T dump is empty. Only UID will be emulated.\r\n");
        } else {
            platformLog("[CE] T2T dump ready: %u pages\r\n", t2_pages);
        }
    }

    /* T2T persona: hand the ENTIRE session to the dedicated transport
     * (m1_t2t_transport.c), bypassing rfalNfcDiscover()/rfalNfcWorker()/
     * rfalNfcDataExchange*() for Type-2 emulation entirely -- see that
     * module's header for the full rationale. discParam.lmConfigPA above is
     * already fully populated (nfcid/nfcidLen/SENS_RES/SEL_RES) regardless
     * of which branch set it, so it's reused verbatim as the identity
     * source; nothing below this point (rfalNfcDiscover() et al, reached
     * only via ListenerCycle()) will ever run for this persona once
     * nfc_driver.c's ownership gate sees the transport active -- there is
     * no path where both the legacy T2T handling and this transport can
     * run at once. A failed start() must not fall back to the legacy path
     * (that would be an unarmed, silent regression to the twice-disproven
     * mechanism this transport replaces) -- ListenIni() fails outright. */
    if (g_persona == EMU_PERSONA_T2T) {
        m1_t2t_transport_set_dispatch(CeHandleT2TCmdRx);
        uint8_t idUid[7];
        uint8_t idLen = (discParam.lmConfigPA.nfcidLen == RFAL_LM_NFCID_LEN_07) ? 7U : 4U;
        ST_MEMCPY(idUid, discParam.lmConfigPA.nfcid, idLen);
        if (!m1_t2t_transport_start(idUid, idLen, discParam.lmConfigPA.SENS_RES, discParam.lmConfigPA.SEL_RES)) {
            platformLog("[T2T-TP] ListenIni: transport start failed\r\n");
            return false;
        }
        platformLog("[T2T-TP] ListenIni() OK (dedicated transport), uid=%s\r\n", hex2Str(idUid, idLen));
        return true;
    }

    state = IDLE;
    platformLog("ListenIni() OK, state=%d, uid=%s\r\n", state, hex2Str(discParam.lmConfigPA.nfcid, (discParam.lmConfigPA.nfcidLen == RFAL_LM_NFCID_LEN_07) ? 7 : 4));
    return true;
}


/****************************************************************************************/
/*Emunlate Cycle*/
/****************************************************************************************/
/*============================================================================*/
/**
 * @brief ListenerCycle - Card Emulation main cycle
 *
 * Non-blocking function called periodically from FreeRTOS task.
 * Uses discParam initialized by ListenerIni() to enter Listen mode.
 * After REQA / ANTICOLL / SELECT, when RFAL_NFC_STATE_ACTIVATED is reached,
 * card type is visible on the reader (UID / ATQA / SAK).
 * At this stage, memory emulation is not performed,
 * and T2T / MFC commands are minimally responded to or ignored.
 * 
 * @retval None
 */
/*============================================================================*/
/* test07 (diagnostic): map RFAL NFC state -> short name for the [MFC-DR] trace. */
static const char *dr_nfc_state_name(rfalNfcState s)
{
    switch (s) {
        case RFAL_NFC_STATE_NOTINIT:             return "NOTINIT";
        case RFAL_NFC_STATE_IDLE:                return "IDLE";
        case RFAL_NFC_STATE_START_DISCOVERY:     return "START_DISCOVERY";
        case RFAL_NFC_STATE_WAKEUP_MODE:         return "WAKEUP";
        case RFAL_NFC_STATE_LISTEN_TECHDETECT:   return "LISTEN_TECHDETECT";
        case RFAL_NFC_STATE_LISTEN_COLAVOIDANCE: return "LISTEN_COLAVOID";
        case RFAL_NFC_STATE_LISTEN_ACTIVATION:   return "LISTEN_ACTIVATION";
        case RFAL_NFC_STATE_LISTEN_SLEEP:        return "LISTEN_SLEEP";
        case RFAL_NFC_STATE_ACTIVATED:           return "ACTIVATED";
        case RFAL_NFC_STATE_DATAEXCHANGE:        return "DATAEXCHANGE";
        case RFAL_NFC_STATE_DATAEXCHANGE_DONE:   return "DATAEXCHANGE_DONE";
        case RFAL_NFC_STATE_DEACTIVATION:        return "DEACTIVATION";
        default:                                 return "other";
    }
}

void ListenerCycle(void)
{
    ReturnCode   err;
    rfalNfcState nfcState;

#if defined(M1_MFC_RAW_EMULATION)
    /* Defensive invariant (should be structurally unreachable): nfc_driver.c's
     * PROCESS state checks m1_mfc_raw_hw_active() BEFORE ever calling
     * nfc_process_func() (-> here), and ownership is acquired synchronously at
     * ListenIni()'s MFC_EMU branch -- before the FIRST such check can occur --
     * so ListenerCycle() should never run at all while a raw session owns the
     * radio. If it somehow does (this is exactly the class of bug the locked
     * architecture eliminates: RFAL and the raw backend racing as concurrent
     * owners), stop HERE, before reading nfcState or running the switch below,
     * rather than acting on state the raw session may be concurrently using. */
    if (m1_mfc_raw_hw_active()) {
        platformLog("[RAW-INVARIANT-VIOLATION] ListenerCycle ran while raw session active\r\n");
        return;
    }
#endif

    /* Defensive invariant, same philosophy as the MFC check above -- should
     * be structurally unreachable now that nfc_driver.c's PROCESS state
     * transitions NfcState to NFC_STATE_DONE the SAME tick the dedicated T2T
     * transport stops (see that fix's comment), never leaving a window where
     * this generic path could run with a stale g_persona==EMU_PERSONA_T2T
     * after the transport already released the radio. T2T never legitimately
     * reaches ListenerCycle()/rfalNfcWorker() at all -- m1_t2t_transport.c is
     * the sole owner for the entire lifetime of that persona, starting,
     * running, or just-stopped. If this somehow fires anyway, stop here
     * rather than let the legacy high-level RFAL worker touch a radio the
     * dedicated transport owns (or just released). */
    if (g_persona == EMU_PERSONA_T2T) {
        platformLog("[T2T-INVARIANT-VIOLATION] ListenerCycle ran during T2T persona\r\n");
        return;
    }

    /* Periodic RFAL processing */
    rfalNfcWorker();

    nfcState = rfalNfcGetState();

    /* test07 (diagnostic): for the Detect Reader persona, log each RFAL NFC
     * state transition ONCE (log-on-change, no per-loop spam), to see how far
     * activation progresses after DISCOVERY. Uses the value already read above
     * (pure getter) -- no extra RF access, no listener dataFlag consumption. */
    if (g_persona == EMU_PERSONA_MFC_DETECT) {
        static rfalNfcState s_prevNfcState = (rfalNfcState)0xFF;
        if (nfcState != s_prevNfcState) {
            platformLog("[MFC-DR] RFAL %s(%d)\r\n", dr_nfc_state_name(nfcState), (int)nfcState);
            s_prevNfcState = nfcState;
        }
    }

    /* If CE stop request received from external (optional) */
    if (s_listener_stop) {
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        state     = IDLE;
        s_cePhase = CE_PHASE_IDLE;
        s_listener_stop = false;
        platformLog("[CE] listener stop -> IDLE\r\n");
        return;
    }

    /* AUTHTX: collapse the ACTIVATED -> DATAEXCHANGE round-trip for raw MFC so the
     * canonical AUTH dispatch + nonce submit run in the SAME ListenerCycle turn as
     * activation (no worker round-trip before nonce TX). Bounded to one extra pass;
     * every other persona/state runs the switch exactly once, unchanged. Does NOT
     * call rfalNfcWorker again. There are no `continue` statements inside the switch,
     * so wrapping it in a do/while is behaviour-preserving for the break/return cases. */
    uint8_t _mfc_prev_state; (void)_mfc_prev_state;
    do {
    _mfc_prev_state = state;
    switch (state)
    {
    /* ListenerIni() not yet called */
    case NOTINIT:
        return;

    /* First entry after ListenIni()
       → Set to START_DISCOVERY once to start Discover */
    case IDLE:
        state = START_DISCOVERY;
        break;

    /* Start Discover: Enter Listen mode and wait for REQA/ANTICOLL/SELECT */
    case START_DISCOVERY:
        platformLog("[CE] IDLE -> DISCOVERY persona=%d\r\n", g_persona);

        /* Clean up if previous session remains */
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);

        err = rfalNfcDiscover(&discParam);
        if (err != RFAL_ERR_NONE) {
            platformLog("[CE] rfalNfcDiscover() error=%d\r\n", err);
            state     = IDLE;
            s_cePhase = CE_PHASE_IDLE;
            break;
        }

        state     = DISCOVERY;
        s_cePhase = CE_PHASE_IDLE;
        break;

    /* Waiting for field detection / ACTIVATE */
    case DISCOVERY:
        switch (nfcState) {
        case RFAL_NFC_STATE_IDLE:
        case RFAL_NFC_STATE_LISTEN_TECHDETECT:
        case RFAL_NFC_STATE_LISTEN_ACTIVATION:
            /* Reader is still approaching, wait here without doing anything */
            break;

        case RFAL_NFC_STATE_ACTIVATED:
        {
            rfalNfcDevice *dev = NULL;

            /* Get active device (CE target) */
            if (rfalNfcGetActiveDevice(&dev) == RFAL_ERR_NONE && dev != NULL) {
                s_ceActiveDev = dev;

                /* Correct activeDev fields based on discParam
                   (UID, ATQA, SAK, ATS, etc.) */
                CeApplyDiscParamToActiveDev(dev);
            }

            /* Detect Reader (gated): mark the reader as active. */
            if (g_persona == EMU_PERSONA_MFC_DETECT) { mfc_detect_on_activated(); }

            /* For T4T: Check status with rfalNfcDataExchangeGetStatus() then arm RX */
            if( Emu_GetPersona() == EMU_PERSONA_T4T )
            {
                err = rfalNfcDataExchangeGetStatus();
                platformLog("RFAL_NFC_STATE_ACTIVATED (T4T) err = %d\r\n", err);
                
                if (err == RFAL_ERR_BUSY) break;
                if (err != RFAL_ERR_NONE) { break; }

                /* Arm RX so first frame can be received in ISO-DEP CE as well */
                s_ceRxData   = NULL;
                s_ceRxRcvLen = 0;

                err = CeArmRx();
                if (err != RFAL_ERR_NONE) {
                    platformLog("[CE] DataExchangeStart(RX) error=%d\r\n", err);
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                    break;
                }

                state     = DATAEXCHANGE;
                s_cePhase = CE_PHASE_WAIT_RX;

                platformLog("[CE] DISCOVERY -> DATAEXCHANGE <T4T>\r\n");
                break;
            }

            /* For T2T: Call rfalNfcDataExchangeStart() first to check already received data */
            /* T2T command received in LISTEN_ACTIVATED of rfal_nfc.c is stored in gNfcDev.rxBuf.rfBuf */
            s_ceRxData   = NULL;
            s_ceRxRcvLen = NULL;
            
            /* Get pointer to already received data (returns gNfcDev.rxBuf.rfBuf when called in ACTIVATED state) */
            err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
            if (err != RFAL_ERR_NONE) {
                platformLog("[CE] DataExchangeStart error=%d\r\n", err);
                state     = START_DISCOVERY;
                s_cePhase = CE_PHASE_IDLE;
                break;
            }
            
            /* Check if data was already received */
            err = rfalNfcDataExchangeGetStatus();
            if (err == RFAL_ERR_BUSY) {
                /* Still in data exchange, process in next loop */
                state     = DATAEXCHANGE;
                s_cePhase = CE_PHASE_WAIT_RX;
                break;
            }

            if (err == RFAL_ERR_NONE && s_ceRxData != NULL && s_ceRxRcvLen != NULL && (*s_ceRxRcvLen > 0U)) {
                /* First frame already received (T2T command received in LISTEN_ACTIVATED of rfal_nfc.c) */
                state     = DATAEXCHANGE;
                s_cePhase = CE_PHASE_WAIT_RX;
                platformLog("[CE] DISCOVERY -> DATAEXCHANGE (first frame already received, len=%u bits)\r\n", *s_ceRxRcvLen);
                break; /* Process with CeHandleT2TCmdRx() in next loop */
            }
            
            /* If no data received, arm RX (normal case) */
            err = CeArmRx();
            if (err != RFAL_ERR_NONE) {
                platformLog("[CE] DataExchangeStart(RX) error=%d\r\n", err);
                state     = START_DISCOVERY;
                s_cePhase = CE_PHASE_IDLE;
                break;
            }
            
            state     = DATAEXCHANGE;
            s_cePhase = CE_PHASE_WAIT_RX;
            platformLog("[CE] DISCOVERY -> DATAEXCHANGE\r\n");
            break;
        }

        case RFAL_NFC_STATE_LISTEN_SLEEP:
            /* Reader sent DESELECT / HLTA → restart discovery */
            platformLog("[CE] LISTEN_SLEEP -> restart discovery\r\n");
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
            state     = START_DISCOVERY;
            s_cePhase = CE_PHASE_IDLE;
            break;

        default:
            break;
        }
        break;

    /* Data exchange stage with reader
       - At this stage, only UID-based type identification needs to be guaranteed,
         so T2T/MFC commands can be minimally responded to.
       - DESFire(T4T) is mostly handled by RFAL T4T CE */
    case DATAEXCHANGE:

        /* If field disappeared or session ended, return to DISCOVERY */
        if ((nfcState == RFAL_NFC_STATE_IDLE) || (nfcState == RFAL_NFC_STATE_LISTEN_SLEEP))
        {
            platformLog("[CE] field off -> restart discovery\r\n");
            if (g_persona == EMU_PERSONA_MFC_DETECT) { mfc_detect_on_field_lost(); }
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_DISCOVERY);
            state     = START_DISCOVERY;
            s_cePhase = CE_PHASE_IDLE;
            break;
        }

        switch (s_cePhase)
        {
            case CE_PHASE_IDLE:
            {
                /* Idle state: no action needed, wait for next discovery */
                break;
            }
            case CE_PHASE_WAIT_RX:
            {
                /* 1) Check RX completion/error status */
                err = rfalNfcDataExchangeGetStatus();
                if (err == RFAL_ERR_BUSY) {
                    break;
                }
                if (err != RFAL_ERR_NONE) {
                    platformLog("[CE] RX error=%d\r\n", err);
                    /* LINK_LOSS (37) means Poller turned off field, treat as normal termination */
                    if (err == RFAL_ERR_LINK_LOSS) {
                        platformLog("[CE] RX: link loss (poller field off) - normal termination\r\n");
                        state     = START_DISCOVERY;
                        s_cePhase = CE_PHASE_IDLE;
                        break;
                    }
                    /* For other errors, try to re-arm RX */
                    err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                    if (err != RFAL_ERR_NONE) {
                        platformLog("[CE] Re-arm RX after error failed: %d\r\n", err);
                        state     = START_DISCOVERY;
                        s_cePhase = CE_PHASE_IDLE;
                    }
                    break;
                }

                /* 2) Validate RX data pointer/length */
                if (!s_ceRxData || !s_ceRxRcvLen || (*s_ceRxRcvLen == 0U)) {
                    /* If RX is empty, arm RX */
                    err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                    if (err != RFAL_ERR_NONE) {
                        platformLog("[CE] Arm RX error=%d\r\n", err);
                        state     = START_DISCOVERY;
                        s_cePhase = CE_PHASE_IDLE;
                    }
                    break;
                }

                /* 3) Parse RX */
                uint16_t rxBits  = *s_ceRxRcvLen;
                uint16_t rxBytes = rfalConvBitsToBytes(rxBits);

                /* RX logging (enable only when needed) */
#if 0
                if (rxBytes >= 1) {
                    uint8_t cmd = s_ceRxData[0];
                    if (cmd == 0x30 && rxBytes >= 2) {
                        platformLog("[CE][RX] T2T READ: page=%u (cmd=0x%02X)\r\n", 
                                   (unsigned)s_ceRxData[1], cmd);
                    } else if (cmd == 0x60) {
                        platformLog("[CE][RX] T2T GET_VERSION (cmd=0x%02X)\r\n", cmd);
                    } else {
                        platformLog("[CE][RX] cmd=0x%02X len=%uB\r\n", cmd, (unsigned)rxBytes);
                    }
                }
#endif

                /* 4) If REQA/WUPA comes in DATAEXCHANGE, return to discovery */
                if ((s_ceRxData[0] == NFCA_CMD_REQA) || (s_ceRxData[0] == NFCA_CMD_WUPA)) {
                    platformLog("[CE] REQA/WUPA in DATAEXCHANGE -> restart discovery\r\n");
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                    break;
                }

                /* Detect Reader (gated): route AUTH frames to the isolated capture
                 * core, then re-arm RX. Other personas are unaffected and fall
                 * through to the normal T2T handling below. */
                if (g_persona == EMU_PERSONA_MFC_DETECT) {
                    (void)mfc_detect_service_frame(s_ceRxData, rxBits);
                    err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                    if (err != RFAL_ERR_NONE) {
                        state     = START_DISCOVERY;
                        s_cePhase = CE_PHASE_IDLE;
                    }
                    break;
                }


                /* 5) Handle T2T command: CeHandleT2TCmdRx() prepares response and starts TX internally */
                g_ceTxLenBytes = 0U;
                g_ceTxPending  = false;

                if (CeHandleT2TCmdRx(s_ceRxData, rxBits) == true) {
                    /* TX started inside CeHandleT2TCmdRx() and s_cePhase set to CE_PHASE_DATAEX */
                    break;
                }

                /* 6) If non-T2T data received: (if T2T-only, just re-arm RX or return to discovery) */
                platformLog("[CE] Non-T2T cmd=0x%02X (%uB) -> re-arm RX\r\n",
                            s_ceRxData[0], (unsigned)rxBytes);

                err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                if (err != RFAL_ERR_NONE) {
                    platformLog("[CE] re-arm RX error=%d\r\n", err);
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                }
                break;
            }
            case CE_PHASE_WAIT_TX:
            {
                /* WAIT_TX state: not currently used in this implementation */
                /* If needed in future, handle TX completion here */
                break;
            }
            case CE_PHASE_DATAEX:
            {
                /* Wait for TX completion */
                err = rfalNfcDataExchangeGetStatus();
                if (err == RFAL_ERR_BUSY) {
                    break;
                }

                /* RFAL_ERR_LINK_LOSS means Poller turned off field, treat as normal termination */
                if (err == RFAL_ERR_LINK_LOSS) {
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                    break;
                }

                if (err != RFAL_ERR_NONE) {
                    platformLog("[CE] TX err=%d\r\n", err);
                    /* Some errors are retryable, so try to re-arm RX */
                    if (err == RFAL_ERR_TIMEOUT || err == RFAL_ERR_FRAMING || err == RFAL_ERR_CRC || err == RFAL_ERR_PAR) {
                        g_ceTxPending  = false;
                        g_ceTxLenBytes = 0U;
                        *s_ceRxRcvLen = 0;
                        
                        err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                        if (err == RFAL_ERR_NONE) {
                            s_cePhase = CE_PHASE_WAIT_RX;
                            break;
                        }
                    }
                    /* For non-retryable errors or re-arm failure, return to discovery */
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                    break;
                }

                /* TX completed normally → Arm RX to receive next command */
                g_ceTxPending  = false;
                g_ceTxLenBytes = 0U;
                
                /* Reset only RX buffer pointer (fast processing) */
                *s_ceRxRcvLen = 0;

                /* Immediately arm RX after TX completion (no delay) */
                err = rfalNfcDataExchangeStart(NULL, 0, &s_ceRxData, &s_ceRxRcvLen, RFAL_FWT_NONE);
                if (err != RFAL_ERR_NONE) {
                    platformLog("[CE] Arm RX err=%d\r\n", err);
                    state     = START_DISCOVERY;
                    s_cePhase = CE_PHASE_IDLE;
                    break;
                }

                s_cePhase = CE_PHASE_WAIT_RX;
                break;
            }
        }
        break;//end of DATAEXCHANGE

    default:
        break;
    }
    } while (0);
}


/*============================================================================*/
/**
 * @brief ListenerNotif - Listener(CE) dedicated notification callback
 * 
 * Handles RFAL NFC state change notifications for listener mode.
 * 
 * @param[in] st RFAL NFC state
 * @retval None
 */
/*============================================================================*/
static void ListenerNotif(rfalNfcState st)
{
    rfalNfcDevice *dev = NULL;

    switch (st)
    {
    case RFAL_NFC_STATE_START_DISCOVERY:
        /* If was previously active, handle session termination here */
        if (s_wasActivated) {
            platformLog("[ListenerNotif] Deactivated -> restart discovery\r\n");
            s_wasActivated = false;
            s_active_dev   = NULL;
            Listener_OnDeactivated();    /* Upper hook */

        } else {
            //platformLog("[CE] Start discovery\r\n");
        }
        /* Multi-select flag has no meaning in listener but initialize existing variable */
        multiSel = false;
        break;

    case RFAL_NFC_STATE_ACTIVATED:
#if defined(M1_MFC_RAW_EMULATION)
        if (!m1_mfc_in_auth_crit())   /* deferred inside the AUTH->nonce window */
#endif
        { platformLog("[ListenerNotif] ListenerNotif: ACTIVATED\r\n"); }
        if (rfalNfcGetActiveDevice(&dev) == RFAL_ERR_NONE && dev) {
            s_active_dev   = dev;
            s_wasActivated = true;

        } else {
            platformLog("[ListenerNotif] Activated, but no active device\r\n");
        }
        break;

    case RFAL_NFC_STATE_DATAEXCHANGE:
        platformLog("[ListenerNotif] Data exchange\r\n");
        break;

    case RFAL_NFC_STATE_DATAEXCHANGE_DONE:
        //platformLog("[CE] Data exchange done\r\n");
        break;

    case RFAL_NFC_STATE_LISTEN_SLEEP:
        platformLog("[ListenerNotif] Listen sleep\r\n");
        break;

    /* Poller-only states are ignored in listener */
    case RFAL_NFC_STATE_POLL_TECHDETECT:
    case RFAL_NFC_STATE_POLL_SELECT:
        break;

    case RFAL_NFC_STATE_WAKEUP_MODE:
        platformLog("[ListenerNotif] Wake-up mode\r\n");
        break;

    default:
        platformLog("[ListenerNotif] State=%d\r\n", st);
        break;
    }
}


/*============================================================================*/
/**
 * @brief ceT2T_FromDump - Type 2 / NTAG dump-based CE response
 * 
 * Builds CE response based on T2T dump data for various T2T commands.
 * 
 * @param[in] rx Received data buffer
 * @param[in] rxLenB Received data length in bytes
 * @param[out] tx Transmission buffer
 * @param[in] txSize Transmission buffer size
 * @retval Response length in bytes, or 0 on error
 */
/*============================================================================*/
static uint16_t ceT2T_FromDump(const uint8_t *rx, uint16_t rxLenB, uint8_t *tx, uint16_t txSize)
{
    uint8_t cmd  = rx[0];
    uint8_t addr = rx[1];

    platformLog("[CE] T2T cmd=0x%02X len=%uB\r\n", cmd, (unsigned)rxLenB);

    if (!rx || rxLenB < 2 || !tx || txSize < 4) {
        return 0;
    }

    const uint16_t page_cnt = nfc_ctx_get_t2t_page_count();
    if (page_cnt == 0) {
        return 0;
    }

    /* Clamp address to actual page range */
    uint16_t page = addr;
    if (page >= page_cnt) {
        page = page_cnt - 1;
    }

    switch (cmd)
    {
        case 0x30:
        {
            platformLog("[CE] 0x30 T2T READ startPage=%u\r\n", rx[1]);
            if (rxLenB < 2U) return 0;

            uint8_t  startPage   = rx[1];
            uint8_t  pagesToRead = 4U;  
            uint8_t  page_buf[4];
            uint16_t totalBytes  = 0;

            for (uint8_t i = 0; i < pagesToRead; i++) {
                uint16_t pageIndex = (uint16_t)startPage + i;

                if (pageIndex >= page_cnt || !nfc_ctx_get_t2t_page(pageIndex, page_buf)) {
                    /* Pad with 0 if out of range */
                    memset(page_buf, 0x00, sizeof(page_buf));
                }

                if (totalBytes + 4U > txSize) break;
                memcpy(&tx[totalBytes], page_buf, 4U);
                totalBytes += 4U;
            }

            return totalBytes;
        }

        /* 0x3A: FAST_READ multiple pages (N * 4 bytes) */
        case 0x3A:
        {
            platformLog("[CE] T2T FAST_READ start=%u end=%u\r\n", rx[1], rx[2]);
            if (rxLenB < 3) {
                return 0;
            }

            uint8_t end_addr = rx[2];
            uint16_t start   = page;
            uint16_t end     = end_addr;

            if (end >= page_cnt) end = page_cnt - 1;
            if (end < start)     end = start;

            uint16_t num_pages = (end - start + 1);
            uint16_t total_len = num_pages * 4U;

            if (txSize < total_len) {
                total_len = (txSize / 4U) * 4U;
                num_pages = total_len / 4U;
            }

            uint8_t page_buf[4];
            uint16_t out_off = 0;

            for (uint16_t p = 0; p < num_pages; p++) {
                uint16_t cur_page = start + p;
                if (!nfc_ctx_get_t2t_page(cur_page, page_buf)) {
                    memset(page_buf, 0x00, sizeof(page_buf));
                }
                memcpy(&tx[out_off], page_buf, 4);
                out_off += 4;
            }

            return out_off;
        }

        /* 0x60: GET_VERSION (NTAG / Ultralight-C series) */
        case 0x60:
        {
            uint8_t ver[8];
            if (!nfc_ctx_get_t2t_version(ver)) {
                /* NAK if no version info */
                return 0;
            }

            if (txSize < sizeof(ver)) {
                return 0;
            }

            memcpy(tx, ver, sizeof(ver));
            return (uint16_t)sizeof(ver);
        }

        /* 0xA2: WRITE one page (4 bytes) → Update dump so it's reflected when read again later */
        case 0xA2:
        {
            if (rxLenB < 6) {
                return 0;
            }

            uint8_t data[4];
            memcpy(data, &rx[2], 4);

            nfc_ctx_set_t2t_page(page, data);

            /* NTAG usually returns ACK one byte (0x0A / 0x0F / 0x00, etc.) */
            if (txSize < 1) return 0;
            tx[0] = 0x0A;   // generic ACK
            return 1;
        }

        default:
            break;
    }

    /* Unsupported command */
    return 0;
}
