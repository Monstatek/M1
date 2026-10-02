/* See COPYING.txt for license details. */

/*
 ******************************************************************************
 * nfc_driver.c - NFC Role Management and Worker Task
 ******************************************************************************
 * 
 * [Purpose]
 * - Top-level NFC role management (Poller/Listener switching)
 * - NFC worker task execution and state machine management
 * - Event-based NFC operation control via queue
 * 
 * [State Machine]
 * NFC_STATE_WAIT
 *   ↓ (Q_EVENT_NFC_START_READ / Q_EVENT_NFC_START_EMULATE)
 * NFC_STATE_INITIALIZE
 *   ↓ (NfcRole.nfc_init_func() called)
 * NFC_STATE_PROCESS
 *   ↓ (NfcRole.nfc_process_func() called periodically)
 * NFC_STATE_DONE
 *   ↓ (NfcRole.nfc_deinit_func() called)
 * NFC_STATE_WAIT (loop)
 * 
 * [Role Switching]
 * - NFC_ROLE_POLLER: Reader mode (card reading)
 *   → Uses NFC_Polling_Init/Process/DeInit functions
 * - NFC_ROLE_LISTENER: Emulation mode (card emulation)
 *   → Uses NFC_Listening_Init/Process/DeInit functions
 * 
 * [Worker Task]
 * - nfc_worker_task(): FreeRTOS task running periodically
 * - Receives events via queue (nfc_worker_q_hdl)
 * - Calls appropriate NFC functions based on current state
 * 
 ******************************************************************************
 */
#include <stdint.h>
#include "m1_nfc.h"
#include "main.h"
#include "nfc_driver.h"
#include "nfc_listener.h"
#include "m1_t2t_transport.h"
#if defined(M1_MFC_RAW_EMULATION)
#include "common/m1_mfc_raw_listener.h"   /* POSTAUTH-9 critical-window flag */
#include "common/m1_mfc_raw_session_hw.h" /* Scope C: dedicated raw-MFC session worker/ISR glue */
#endif

#include "common/nfc_ctx.h"

#include "app_x-cube-nfcx.h"
#include "m1_log_debug.h"
#include "rfal_platform.h"
#include "m1_tasks.h"
#include "uiView.h"

/* NTAG21x live-tag Write (implemented in nfc_poller.c). Runs in this worker's
 * poller mode; drives the shared nfc_wr_status_t. */
extern bool nfc_poller_write_active(void);
extern void nfc_poller_write_begin(void);
extern void nfc_poller_write_end(void);
extern void m1_ntag21x_write_run(void);
/* MIFARE Classic dictionary scan (Stage C, implemented in nfc_poller.c). */
extern bool nfc_poller_mfc_scan_active(void);
extern void nfc_poller_mfc_scan_begin(void);
extern void nfc_poller_mfc_scan_end(void);
extern void mfc_dict_scan(void);
/* Find Missing Keys: dictionary-phase continuation of a partial MFC read
 * (implemented in nfc_poller.c). Shares the SAME abort flag as the two
 * above (single-threaded worker, only one op ever active at once). */
extern bool nfc_poller_mfc_find_keys_active(void);
extern void nfc_poller_mfc_find_keys_begin(void);
extern void nfc_poller_mfc_find_keys_end(void);
extern void nfc_mfc_find_keys_run(void);
/* Live nested-nonce harvest (Increment 3b, implemented in nfc_poller.c). */
extern bool nfc_poller_harvest_active(void);
extern void nfc_poller_harvest_begin(void);
extern void nfc_poller_harvest_end(void);
extern void nfc_harvest_scan_run(void);
/* MFC Recovery solve (nested-dictionary key recovery, in nfc_poller.c). */
extern bool nfc_poller_solve_active(void);
extern void nfc_poller_solve_begin(void);
extern void nfc_poller_solve_end(void);
extern void nfc_solve_run(void);
/* MIFARE Classic 1K write (clone/restore, in nfc_poller.c). */
extern bool nfc_poller_mfc_write_active(void);
extern void nfc_poller_mfc_write_begin(void);
extern void nfc_poller_mfc_write_end(void);
extern void nfc_mfc_write_run(void);
/* NTAG/Ultralight Unlock (genuine PWD_AUTH, in nfc_poller.c). */
extern bool nfc_poller_unlock_active(void);
extern void nfc_poller_unlock_begin(void);
extern void nfc_poller_unlock_end(void);
extern void nfc_unlock_run(void);


S_M1_NfcFunc_t NfcRole = {
    //"NFC Poller Role",      //title
    NFC_Polling_Init,       //nfc_init_func
    NFC_Polling_Process,    //nfc_process_func
    NFC_Polling_DeInit,     //nfc_deinit_func
    0,                      //req_Evnet
};

static NfcRole_e s_currentRole = NFC_ROLE_POLLER;
static NfcStateMachine NfcState = NFC_STATE_WAIT;
EmuNfcA_t g_emuA = {0};

TaskHandle_t nfc_worker_task_hdl = NULL;
QueueHandle_t nfc_worker_q_hdl = NULL;


/*============================================================================*/
/**
 * @brief Emu_SetNfcA - Set NFC-A emulation data
 * 
 * Sets the NFC-A emulation data (UID, ATQA, SAK) in a thread-safe manner.
 * The UID length is set to 7 bytes if uid_len is 7, otherwise defaults to 4 bytes.
 * 
 * @param[in] uid Pointer to UID data
 * @param[in] uid_len UID length (4 or 7 bytes)
 * @param[in] atqa0 ATQA byte 0
 * @param[in] atqa1 ATQA byte 1
 * @param[in] sak SAK byte
 * @retval None
 */
/*============================================================================*/
void Emu_SetNfcA(const uint8_t* uid, uint8_t uid_len, uint8_t atqa0, uint8_t atqa1, uint8_t sak)
{
    taskENTER_CRITICAL();
    memset(&g_emuA, 0, sizeof(g_emuA));
    if (uid_len == 7) g_emuA.uid_len = 7;
    else               g_emuA.uid_len = 4;      // Default 4 bytes

    memcpy(g_emuA.uid, uid, g_emuA.uid_len);
    g_emuA.atqa[0] = atqa0;
    g_emuA.atqa[1] = atqa1;
    g_emuA.sak     = sak;
    g_emuA.valid   = true;
    taskEXIT_CRITICAL();
}

/*============================================================================*/
/**
 * @brief Emu_GetNfcA - Get NFC-A emulation data
 * 
 * Retrieves the current NFC-A emulation data in a thread-safe manner.
 * 
 * @param[out] out Pointer to output structure to store emulation data
 * @retval true If emulation data is valid and copied
 * @retval false If output pointer is NULL or data is invalid
 */
/*============================================================================*/
bool Emu_GetNfcA(EmuNfcA_t* out)
{
    if (!out) return false;
    bool ok;
    taskENTER_CRITICAL();
    ok = g_emuA.valid;
    if (ok) memcpy(out, &g_emuA, sizeof(g_emuA));
    taskEXIT_CRITICAL();

#if 0
    //platformLog("g_emuA.atqa[0] = %x g_emuA.atqa[1] = %x g_emuA.sak = %x g_emuA.valid = %x\r\n"
    //                                ,g_emuA.atqa[0], g_emuA.atqa[1],g_emuA.sak,g_emuA.valid);
    //platformLog("out.atqa[0] = %x out.atqa[1] = %x out.sak = %x out.valid = %x\r\n"
    //                                ,out->atqa[0], out->atqa[1],out->sak,out->valid);
#endif

    return ok;
}

/*============================================================================*/
/**
 * @brief Emu_Clear - Clear NFC-A emulation data
 * 
 * Clears and invalidates the NFC-A emulation data in a thread-safe manner.
 * 
 * @retval None
 */
/*============================================================================*/
void Emu_Clear(void)
{
    taskENTER_CRITICAL();
    memset(&g_emuA, 0, sizeof(g_emuA));
    taskEXIT_CRITICAL();
}

/*============================================================================*/
/**
 * @brief apply_poller_role - Apply poller role configuration
 * 
 * Sets the NFC role function pointers to poller (reader) mode functions.
 * 
 * @retval None
 */
/*============================================================================*/
static inline void apply_poller_role(void)
{
    //NfcRole.title            = "NFC Poller Role";
    NfcRole.nfc_init_func    = NFC_Polling_Init;
    NfcRole.nfc_process_func = NFC_Polling_Process;
    NfcRole.nfc_deinit_func  = NFC_Polling_DeInit;
    NfcRole.tick_ms          = 0;
    s_currentRole            = NFC_ROLE_POLLER;
}

/*============================================================================*/
/**
 * @brief apply_listener_role - Apply listener role configuration
 * 
 * Sets the NFC role function pointers to listener (emulation) mode functions.
 * 
 * @retval None
 */
/*============================================================================*/
static inline void apply_listener_role(void)
{
    //NfcRole.title            = "NFC Listener Role";
    NfcRole.nfc_init_func    = NFC_Listening_Init;
    NfcRole.nfc_process_func = NFC_Listening_Process;
    NfcRole.nfc_deinit_func  = NFC_Listening_DeInit;
    NfcRole.tick_ms          = 0;
    s_currentRole            = NFC_ROLE_LISTENER;
}

/*============================================================================*/
/**
 * @brief NFC_SetRole - Set NFC role (simple pointer replacement)
 * 
 * Sets the NFC role by replacing function pointers only.
 * Does not perform initialization or cleanup.
 * 
 * @param[in] role NFC role to set (NFC_ROLE_POLLER or NFC_ROLE_LISTENER)
 * @retval true If role was set successfully
 * @retval false If invalid role specified
 */
/*============================================================================*/
bool NFC_SetRole(NfcRole_e role)
{
    switch (role) {
        case NFC_ROLE_POLLER:   apply_poller_role();   return true;
        case NFC_ROLE_LISTENER: apply_listener_role(); return true;
        default: return false;
    }
}

/*============================================================================*/
/**
 * @brief NFC_SwitchRole - Switch NFC role with cleanup and initialization
 * 
 * Switches the NFC role by deinitializing the current role, setting new role,
 * and optionally initializing the new role. If the role is already set, no action is taken.
 * 
 * @param[in] role NFC role to switch to (NFC_ROLE_POLLER or NFC_ROLE_LISTENER)
 * @retval true If role switch was successful
 * @retval false If role switch failed (rolls back to poller role)
 */
/*============================================================================*/
bool NFC_SwitchRole(NfcRole_e role)
{
    if (s_currentRole == role) {
        // Already in the same role, no need to switch
        //platformLog("No Need Switching Role\r\n");
        return true;
    }

    // 1) Cleanup existing role
    if (NfcRole.nfc_deinit_func) {
        NfcRole.nfc_deinit_func();
    }

    // 2) Reset function pointers
    if (!NFC_SetRole(role)) {
        // If pointer restoration fails, safely rollback to poller role
        apply_poller_role();
        return false;
    }

    // 3) Initialize new role
#if defined(M1_MFC_RAW_EMULATION)
    /* Every NFC_SwitchRole() caller sets NfcState = NFC_STATE_INITIALIZE right
     * before calling, and that state runs nfc_init_func() exactly once. Running it
     * here too made the LISTENER init run TWICE (observed: "NFC Listening Init" /
     * build banner / [B1-PRESENT] printed twice before discovery). The second
     * ListenIni()/rfalNfcInitialize() re-armed the ST25R3916 listener + its RX/IRQ
     * config after the first had already set it up, so the reader's first
     * post-SELECT frame (the MIFARE AUTH sent immediately after SELECT) was dropped
     * -- lm reaches ACTIVE_A but isDataRcvd never fires. Skip the redundant listener
     * init here; NFC_STATE_INITIALIZE performs it once. Poller path unchanged. */
    if ((role != NFC_ROLE_LISTENER) && NfcRole.nfc_init_func) {
        NfcRole.nfc_init_func();
    }
    return true;
#else
    if (NfcRole.nfc_init_func) {
        NfcRole.nfc_init_func();
        return true;
    }
    return true;
#endif
}



/*============================================================================*/
/**
 * @brief nfc_worker_task - NFC worker task main function
 * 
 * FreeRTOS task that manages NFC operations through a state machine.
 * Handles role switching (Poller/Listener), initialization, processing, and cleanup.
 * Receives events via queue (nfc_worker_q_hdl) to control NFC operations.
 * 
 * State machine flow:
 * - NFC_STATE_WAIT: Waits for events (Q_EVENT_NFC_START_READ or Q_EVENT_NFC_START_EMULATE)
 * - NFC_STATE_INITIALIZE: Initializes NFC role
 * - NFC_STATE_PROCESS: Processes NFC operations (reading/emulating)
 * - NFC_STATE_DONE: Deinitializes and returns to WAIT state
 * 
 * @param[in] arg Task argument (unused)
 * @retval None (infinite loop)
 */
/*============================================================================*/
void nfc_worker_task(void *arg)
{
    S_M1_Main_Q_t q_item;
	BaseType_t ret;
#if defined(M1_MFC_RAW_EMULATION)
    bool mfc_start_request_in_worker = false;
#endif

	platformLog("NFC Worker Task Started!\r\n");
    nfc_ctx_module_init(); // NFC context initialization

	for(;;)
	{
        m1_wdt_reset();
        switch (NfcState)
        {
            case NFC_STATE_WAIT:
#if defined(M1_MFC_RAW_EMULATION)
                /* Central ownership gate, canary form: nfc_worker_task cannot
                 * actually BE here while m1_mfc_raw_hw_active() is true --
                 * m1_mfc_raw_hw_run() (called from NFC_STATE_PROCESS) blocks
                 * this exact task for the whole raw session and never returns
                 * control to this xQueueReceive() until fully torn down, so
                 * no other NFC operation can start regardless of what the UI
                 * does meanwhile (any such request just queues, unprocessed,
                 * until the raw session ends). Logged if it somehow fires
                 * anyway, rather than silently starting a second owner. */
                if (m1_mfc_raw_hw_active()) {
                    platformLog("[RAW-INVARIANT-VIOLATION] NFC_STATE_WAIT reached while raw session active\r\n");
                }
#endif
                ret = xQueueReceive(nfc_worker_q_hdl, &q_item, pdMS_TO_TICKS(100));//portMAX_DELAY);
                if (ret==pdTRUE)
                {
                    if ( q_item.q_evt_type==Q_EVENT_NFC_START_READ )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        //platformLog("NFC Worker Task: Received NFC_START_READ event, moving to Init state\r\n");

                        /* Phase A (persona/lifecycle fix): every start event
                         * must explicitly establish its own persona rather
                         * than inherit whatever a prior session left --
                         * Emu_SetPersona() has exactly two other call sites
                         * (MFC_DETECT, MFC_EMU) and nothing previously reset
                         * it, so a stale MFC_EMU here used to survive into an
                         * ordinary Read. RAW is the neutral/default value
                         * (same as g_persona's own static initializer);
                         * ordinary Read has no persona-specific behavior of
                         * its own -- this exists purely to prevent inheriting
                         * one. Role alone (POLLER below) already excludes
                         * this from the RAWOWN dispatch (see NFC_STATE_PROCESS
                         * below); this is belt-and-suspenders per persona. */
                        Emu_SetPersona(EMU_PERSONA_RAW);
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER); // Switch to poller role

                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_START_EMULATE )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        //platformLog("NFC Worker Task: Received NFC_START_READ event, moving to Init state\r\n");

                        /* Phase A (persona/lifecycle fix): this is the generic
                         * "Read > More > Emulate" screen (T2T/T4T/UID-only
                         * families only -- MIFARE Classic has its own dedicated
                         * route, Q_EVENT_NFC_MFC_EMULATE below, and never
                         * reaches this event at all; see m1_nfc.c's
                         * NFC_ACT_EMULATE dispatch). Its OWN persona (T2T/T4T/
                         * RAW) is auto-inferred from the loaded card inside
                         * ListenIni() (nfc_listener.c), but ONLY once it
                         * reaches that inference code. ListenIni() has an
                         * earlier, unconditional check for
                         * g_persona==EMU_PERSONA_MFC_EMU that short-circuits
                         * straight into m1_mfc_raw_begin() -- if a prior MFC
                         * Emulate session left that persona stuck, THIS event
                         * would silently run raw MFC emulation instead of the
                         * intended legacy identity. Reset to the neutral/
                         * default value first so ListenIni() always reaches
                         * its own auto-inference for this event -- never
                         * MFC_EMU's early-exit branch. This event must never
                         * select EMU_PERSONA_MFC_EMU, regardless of any
                         * armed MFC image state left over from elsewhere. */
                        Emu_SetPersona(EMU_PERSONA_RAW);
                        (void)NFC_SwitchRole(NFC_ROLE_LISTENER); // Switch to listener (emulation) role

                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_WRITE )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for NTAG21x write
                        nfc_poller_write_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_DICT_SCAN )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for MFC dictionary scan
                        nfc_poller_mfc_scan_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_MFC_FIND_KEYS )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for Find Missing Keys continuation
                        nfc_poller_mfc_find_keys_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_HARVEST )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for nested-nonce harvest
                        nfc_poller_harvest_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_SOLVE )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for solve + RF verify
                        nfc_poller_solve_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_MFC_WRITE )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for MFC 1K write
                        nfc_poller_mfc_write_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_T2T_UNLOCK )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        (void)NFC_SwitchRole(NFC_ROLE_POLLER);   // Poller mode for T2T PWD_AUTH unlock
                        nfc_poller_unlock_begin();
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_START_DETECT_READER )
                    {
                        NfcState = NFC_STATE_INITIALIZE;
                        Emu_SetPersona(EMU_PERSONA_MFC_DETECT);   // gate ListenIni to Detect Reader
                        (void)NFC_SwitchRole(NFC_ROLE_LISTENER);  // listener mode, MFC-detect persona
                    }
#if defined(M1_MFC_RAW_EMULATION)
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_MFC_EMULATE )
                    {
                        /* Production safety gate: the dedicated MFC Emulate
                         * view (nfc_mfc_emu_gui_create(), m1_nfc.c) -- the
                         * ONE production route for MIFARE Classic emulation
                         * -- validates and arms a real saved-card image via
                         * m1_mfc_raw_set_emu_image() BEFORE sending this
                         * event, so this check should always pass in normal
                         * operation. It is still enforced here, not merely
                         * trusted, because m1_mfc_raw_hw_starting()/
                         * m1_mfc_raw_hw_run() have no "was I initialized"
                         * check of their own -- they always attempt a real
                         * ST25R3916 acquire against whatever identity state
                         * is currently set, so reaching them with no image
                         * armed would acquire against stale/uninitialized
                         * data instead of failing cleanly. Refuse here,
                         * before any state transition or role switch, if
                         * that invariant was somehow violated;
                         * m1_mfc_raw_begin() itself also refuses as a
                         * second-layer canary (m1_mfc_raw_listener.c), but
                         * this is the primary gate. */
                        if (m1_mfc_raw_hw_start_requested() && m1_mfc_raw_has_emu_image()) {
                            mfc_start_request_in_worker = true;
                            NfcState = NFC_STATE_INITIALIZE;
                            Emu_SetPersona(EMU_PERSONA_MFC_EMU);      // gate ListenIni to raw MFC emulation
                            (void)NFC_SwitchRole(NFC_ROLE_LISTENER);  // listener mode, MFC-emu persona
                        } else {
                            /* This is the one rejection path that never reaches
                             * m1_mfc_raw_hw_run()/m1_mfc_raw_teardown_cleanup()
                             * (which unconditionally clears the armed image for
                             * every path that DOES start) -- clear it here too,
                             * before retiring the request, so a cancelled-before-
                             * consumption or no-image event can never leave a
                             * stale armed image for the next session to inherit. */
                            (void)m1_mfc_raw_set_emu_image(NULL);
                            m1_mfc_raw_hw_finish_request();
                            platformLog("[MFC-EMU-REFUSED] Q_EVENT_NFC_MFC_EMULATE cancelled or with no armed "
                                        "saved-card image -- ignored, no RF started\r\n");
                        }
                    }
#endif
                }
                break;

            case NFC_STATE_INITIALIZE:
                //platformLog("NFC Worker Task: rfal_init, moving to Process state\r\n");
                NfcRole.nfc_init_func();
                NfcState = NFC_STATE_PROCESS;
                vTaskDelay(5);
                break;

            case NFC_STATE_PROCESS:
                //platformLog("NFC Worker Task: Processing\r\n");
                m1_wdt_reset();
                if ( nfc_poller_write_active() )   // NTAG21x live write (one-shot)
                {
                    m1_ntag21x_write_run();        // wait+activate, verify model, write+verify
                    nfc_poller_write_end();
                    NfcState = NFC_STATE_DONE;      // UI reads result from nfc_wr_status_t
                    break;
                }
                if ( nfc_poller_mfc_write_active() )  // MFC 1K write (one-shot)
                {
                    nfc_mfc_write_run();           // activate, auth known keys, write+verify
                    nfc_poller_mfc_write_end();
                    NfcState = NFC_STATE_DONE;      // UI reads progress/result from nfc_mfc_write_t
                    break;
                }
                if ( nfc_poller_unlock_active() )  // T2T PWD_AUTH unlock (one-shot)
                {
                    nfc_unlock_run();              // activate, PWD_AUTH (single or dictionary)
                    nfc_poller_unlock_end();
                    NfcState = NFC_STATE_DONE;      // UI reads progress/result from nfc_t2t_unlock_t
                    break;
                }
                if ( nfc_poller_mfc_scan_active() )   // MFC dictionary scan (one-shot)
                {
                    mfc_dict_scan();               // activate, key-major streaming auth test
                    nfc_poller_mfc_scan_end();
                    NfcState = NFC_STATE_DONE;      // UI reads progress/result from nfc_mfc_scan_t
                    break;
                }
                if ( nfc_poller_mfc_find_keys_active() )   // Find Missing Keys continuation (one-shot)
                {
                    nfc_mfc_find_keys_run();       // activate, verify same card, resume the dictionary phase
                    nfc_poller_mfc_find_keys_end();
                    NfcState = NFC_STATE_DONE;      // UI reads progress/result from nfc_mfc_scan_t
                    break;
                }
                if ( nfc_poller_harvest_active() )    // nested-nonce harvest (one-shot)
                {
                    nfc_harvest_scan_run();        // activate, capture nt_enc+parity, write .m1h
                    nfc_poller_harvest_end();
                    NfcState = NFC_STATE_DONE;      // UI reads progress/result from nfc_harvest_ui_t
                    break;
                }
                if ( nfc_poller_solve_active() )      // MFC Recovery solve (one-shot)
                {
                    nfc_solve_run();               // read .m1h, dictionary-solve, RF-verify
                    nfc_poller_solve_end();
                    NfcState = NFC_STATE_DONE;      // UI reads result from nfc_solve_ui_t
                    break;
                }
#if defined(M1_MFC_RAW_EMULATION)
                /* Phase A (persona/lifecycle fix): require BOTH the current
                 * role AND persona, not persona alone. s_currentRole is the
                 * SAME authoritative role state NFC_SwitchRole() already
                 * maintains (file-scope static, this file) -- no new sticky
                 * mechanism introduced. Before this check, a stale
                 * EMU_PERSONA_MFC_EMU left over from an earlier MFC Emulate
                 * session (nothing ever reset it) would hijack an ordinary
                 * Q_EVENT_NFC_START_READ -- which explicitly switches to
                 * NFC_ROLE_POLLER above -- into this RAWOWN branch instead of
                 * running the real poller, so the M1 silently listened
                 * instead of polling and could never read a card. Persona
                 * is now also explicitly reset per start event (see
                 * Q_EVENT_NFC_START_READ/_START_EMULATE above and
                 * m1_mfc_raw_teardown_cleanup() below), so this check is
                 * defense in depth, not the sole fix. */
                if ( (s_currentRole == NFC_ROLE_LISTENER) &&
                     (Emu_GetPersona() == EMU_PERSONA_MFC_EMU) )   // raw MFC emulation (one-shot, blocking)
                {
                    /* m1_mfc_raw_hw_run() is the SOLE public entry point for a raw
                     * session (m1_mfc_raw_session_hw.h) -- it runs entirely inside
                     * THIS task (nfc_worker_task), owns every ST25R3916 SPI/
                     * register access for the whole session (STARTING through
                     * STOPPING), and blocks until the session is fully started,
                     * run to completion (UI-requested STOP via
                     * m1_mfc_raw_hw_request_stop_and_wait(), attempt-timeout, or
                     * field-loss), and fully torn down -- mirroring exactly how
                     * the one-shot branches above (NTAG/MFC write, dict-scan,
                     * harvest, solve) call their own run-to-completion function.
                     * RFAL's own worker (nfc_process_func / ListenerCycle) is
                     * therefore never reached for this persona (this branch
                     * always breaks out below it), consistent with the
                     * m1_nfc_raw_hal.h contract. */
                    m1_mfc_raw_hw_run();
                    m1_mfc_raw_teardown_cleanup();
                    NfcState = NFC_STATE_DONE;
                    m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
                    break;
                }
#endif
                /* Type-2 (Ultralight/NTAG) ownership gate: checked here, in
                 * the SAME sequential if/else-if chain as the MFC raw gate
                 * above (never a separate/parallel check), so MFC and T2T
                 * ownership can never overlap -- exactly one persona owns
                 * the radio at a time. Unlike MFC's raw session (a single
                 * blocking run-to-completion call), the Type-2 transport is
                 * ticked non-blockingly once per iteration here -- it must
                 * never block this task, which also has to keep servicing
                 * the worker queue (STOP/READ_COMPLETE) every pass, exactly
                 * as the ordinary nfc_process_func() branch below already
                 * does. While this branch is taken, NfcRole.nfc_process_func()
                 * (-> ListenerCycle() -> the legacy T2T handling) is never
                 * called -- there is no tick on which both the old and new
                 * T2T exchange paths could run. */
                if ( NFC_T2TTransportIsActive() )
                {
                    NFC_T2TTransportProcess();

                    /* Lifecycle-race fix: NFC_T2TTransportProcess() can itself
                     * stop the transport this same call (it consumes
                     * ListenerRequestStop()'s flag internally, independently
                     * of the queued Q_EVENT_NFC_EMULATE_STOP sent later from
                     * nfc_emulate_gui_destroy()). Previously NfcState only
                     * ever advanced to NFC_STATE_DONE via that SECOND,
                     * UI-task-gated queued event -- so a flag-only stop left
                     * NfcState stuck at NFC_STATE_PROCESS while
                     * NFC_T2TTransportIsActive() was already false, and the
                     * NEXT iteration fell through to the generic
                     * NfcRole.nfc_process_func() (ListenerCycle()) below with
                     * stale g_persona==EMU_PERSONA_T2T still set -- running
                     * the legacy high-level RFAL worker against the radio the
                     * dedicated transport had just released, and able to
                     * autonomously restart legacy discovery/listen-mode
                     * init, fully independent of the user's actual request.
                     * Detecting the stop HERE and transitioning immediately
                     * makes the flag path authoritative on its own, closing
                     * that window entirely rather than guarding against
                     * activity inside it. */
                    if ( !NFC_T2TTransportIsActive() )
                    {
                        NfcState = NFC_STATE_DONE;
                        m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
                        break;
                    }

                    ret = xQueueReceive(nfc_worker_q_hdl, &q_item, 0);
                    if (ret==pdTRUE)
                    {
                        if ( q_item.q_evt_type==Q_EVENT_NFC_READ_COMPLETE )
                        {
                            NfcState = NFC_STATE_DONE;
                            m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
                        }
                        else if ( q_item.q_evt_type==Q_EVENT_NFC_EMULATE_STOP )
                        {
                            m1_t2t_transport_stop();   /* idempotent -- exactly-once guarantee even if
                                                         * NFC_T2TTransportProcess() already stopped it above */
                            NfcState = NFC_STATE_DONE;
                            m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
                        }
                    }
                    break;
                }
                NfcRole.nfc_process_func();
                ret = xQueueReceive(nfc_worker_q_hdl, &q_item, 0);//worker tick delay & Q wait
                if (ret==pdTRUE)
                {
                    if ( q_item.q_evt_type==Q_EVENT_NFC_READ_COMPLETE )
                    {
                        NfcState = NFC_STATE_DONE;
                        m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
                        //platformLog("NFC Worker Task: Process Complete, moving to Done state\r\n");
                    }
                    else if ( q_item.q_evt_type==Q_EVENT_NFC_EMULATE_STOP )
                    {
                        NfcState = NFC_STATE_DONE;
                        m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); // Turn off
                        //platformLog("NFC Worker Task: Process Complete, moving to Done state\r\n");
                    }

                }
#if defined(M1_MFC_RAW_EMULATION)
                /* AUTHRX (POSTAUTH-10): during an ARMED MFC_EMU listener session,
                 * do NOT poll the RX path. Service RFAL once (nfc_process_func
                 * above), run the monotonic attempt-deadline housekeeping, then
                 * BLOCK on the ST25R interrupt via a static COUNTING semaphore.
                 * An ST25R RXE interrupt wakes the worker immediately (IRQ +
                 * context-switch latency, MEASURED and reported in [B1-PA9] /
                 * [B1-AUTHRX-FAIL]); the timeout is only a WDT/STOP housekeeping
                 * ceiling (<= the proven WAIT-state IWDG window) and never sets RX
                 * latency. Counting semaphore => coalescing cannot lose an RX edge;
                 * the worker drains one RFAL pass per count, blocking only when the
                 * count reaches 0 (any later IRQ re-increments and wakes it, so no
                 * edge is lost between drain and wait). All other NFC modes and the
                 * OFF build keep the original vTaskDelay(5). */
                if ( m1_mfc_authrx_armed() ) {
                    m1_mfc_authrx_housekeep();
                    (void)m1_mfc_authrx_wait(pdMS_TO_TICKS(20));  /* housekeeping ceiling only */
                } else
#endif
                {
                    vTaskDelay(5);
                }
                break;

            case NFC_STATE_DONE:
#if defined(M1_MFC_RAW_EMULATION)
                platformLog("[RAW-TRACE] NFC_STATE_DONE entry m1_mfc_raw_hw_active()=%d\r\n",
                            (int)m1_mfc_raw_hw_active());
                /* Defensive idempotent net: whatever path reached DONE (normal
                 * STOP handled above, a BACK/timeout/error path elsewhere, or
                 * any future caller), a still-active raw session must never be
                 * left owning the radio. m1_mfc_raw_hw_session_end() is a no-op
                 * if already released. */
                if ( m1_mfc_raw_hw_active() ) {
                    m1_mfc_raw_hw_session_end("done-state");
                }
#endif
                NfcRole.nfc_deinit_func();
                HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, GPIO_PIN_RESET);
                NfcState = NFC_STATE_WAIT;
#if defined(M1_MFC_RAW_EMULATION)
                if (mfc_start_request_in_worker) {
                    mfc_start_request_in_worker = false;
                    m1_mfc_raw_hw_finish_request();
                }
#endif
                //platformLog("NFC Worker Task: De-init NFC, state initialize\r\n");
                break;

            default:
		        ret = xQueueReceive(nfc_worker_q_hdl, &q_item, portMAX_DELAY);
                if (ret==pdTRUE)
                {
                    platformLog("default\r\n");
                }
                break;
        } // switch (NfcState)
        //xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
	}
}
