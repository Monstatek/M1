/*
 * m1_mfc_raw_session_hw.c - ST25R3916 hardware glue for the raw-MFC session.
 * See m1_mfc_raw_session_hw.h. Guarded by M1_MFC_RAW_EMULATION.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "m1_mfc_raw_session_hw.h"

#if defined(M1_MFC_RAW_EMULATION)

#include "m1_mfc_session.h"
#include "m1_nfc_raw_hal.h"
#include "m1_mfc_dma.h"
#include "st25r3916.h"
#include "st25r3916_com.h"
#include "st25r3916_irq.h"
#include "stm32h5xx.h"        /* DWT */
#include "rfal_platform.h"    /* platformLog (task context only) */
#include "legacy/nfc_driver.h" /* nfc_worker_task_hdl */
#include "m1_tasks.h"          /* NFC_WORKER_TASK_PRIORITY, M1_MFC_RAWOWN_PRIORITY */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

/*===========================================================================*/
/* Generation-tagged, per-role fixed buffers. Preserved unchanged from the    */
/* prior ISR/worker-boundary design even though both sides now run in the    */
/* same task/call stack -- the gate, latch, and generation-check semantics    */
/* are exactly what m1_mfc_raw_hw_logic_test.c mirrors and this file must     */
/* keep matching it byte-for-byte.                                           */
/*===========================================================================*/
typedef struct {
    uint8_t           cmd[2];
    uint32_t          gen;
    uint32_t          rxe_cyc;
    uint32_t          nt_tx_cyc;
    volatile uint8_t  ready;
} m1_raw_auth_rec_t;

typedef struct {
    uint8_t           data[8];
    uint8_t           len;
    uint32_t          gen;
    uint32_t          rxe_cyc;
    volatile uint8_t  ready;
} m1_raw_nrar_rec_t;

typedef struct {
    uint8_t           data[4];
    uint8_t           len;
    uint32_t          gen;
    uint32_t          rxe_cyc;
    volatile uint8_t  ready;
} m1_raw_read_rec_t;

static m1_mfc_session_t   s_sess;
static uint8_t            s_nonce_tx[4];    /* prepared plaintext Nt, standard parity added by the chip */

static m1_raw_auth_rec_t  s_auth_rec;
static m1_raw_nrar_rec_t  s_nrar_rec;
static m1_raw_read_rec_t  s_read_rec;

static volatile uint8_t   s_nonce_fired_isr;  /* one nonce per session, enforced before any consumer runs */
static volatile uint8_t   s_field_lost;
static volatile uint32_t  s_field_lost_cyc;

/* Receive-path diagnostics -- never logged from inside the notify-wait loop,
 * only from m1_mfc_raw_hw_report() (task context, outside any timed window). */
static volatile uint32_t  s_wa_rxe_count;      /* every RXE seen in WAIT_AUTH, before the gate */
static volatile uint16_t  s_wa_last_bits;      /* last frame's bit length as read from the FIFO */
static volatile uint8_t   s_wa_last_b0;        /* last frame's byte 0 */
static volatile uint8_t   s_wa_last_b1;        /* last frame's byte 1 */
static volatile uint32_t  s_gate_bad_state;    /* diagnostic mirror of the gate's own state check */
static volatile uint32_t  s_gate_bad_bits;     /* diagnostic mirror of the gate's own bit-count check */
static volatile uint32_t  s_gate_bad_cmd;      /* diagnostic mirror of the gate's own cmd-byte check */
static volatile uint32_t  s_gate_bad_block;    /* diagnostic mirror of the gate's own block-range check */
static volatile uint32_t  s_gate_ok;           /* diagnostic mirror: would have passed all four checks */

/* Auto-response-disable diagnostics -- ported semantics from e68710e's
 * pta_active_seen/pta_disable_done, now driven by the WU_A/WU_A_X bits read
 * directly off the chip (m1_nfc_raw_read_irq_status()) instead of polling
 * PASSIVE_TARGET_STATUS. */
static volatile uint32_t  s_wu_a_seen;      /* WU_A|WU_A_X observed at least once this session */
static volatile uint32_t  s_autoresp_off;   /* d_106_ac_a disable write issued (one-shot per session) */
static volatile uint32_t  s_nt_tx_count;    /* Nt TX submitted (TRANSMIT_WITHOUT_CRC issued) */

/* Nt-TX completion diagnostics ONLY -- read-only trace (this candidate) found
 * that nt_tx above proves TRANSMIT_WITHOUT_CRC was ISSUED, not that the chip
 * ever raised TXE (end-of-transmission) for it, and that m1_nfc_raw_own_
 * arm_rx()'s CLEAR_FIFO runs immediately after issuing the command with no
 * wait -- a real question about whether that aborts an in-flight ~300us
 * on-air transmission. s_nt_tx_pending is a ONE-SHOT window (cleared the
 * moment TXE or an error bit is seen while it's set) so a LATER {At}/READ
 * TXE is never misattributed to the Nt TX. Zero effect on control flow --
 * every field here is written-then-only-read-by-the-report. */
static volatile uint8_t   s_nt_tx_pending;   /* TRANSMIT_WITHOUT_CRC issued for Nt, TXE not yet seen */
static volatile uint32_t  s_nt_tx_req_cyc;   /* DWT->CYCCNT right after issuing the command */
static volatile uint32_t  s_nt_txe_seen;     /* TXE observed while s_nt_tx_pending was set   */
static volatile uint32_t  s_nt_txe_cyc;      /* DWT->CYCCNT at that TXE                      */
static volatile uint32_t  s_nt_err_irq;      /* ERR1|ERR2|PAR|CRC|NRE|COL seen while pending  */
static volatile uint8_t   s_nt_num_tx1, s_nt_num_tx2;      /* NUM_TX_BYTES1/2 readback        */
static volatile uint8_t   s_nt_iso14443a_nfc;              /* ISO14443A_NFC (parity) readback */

/* Pure diagnostic (no control-flow effect): bitwise-OR of every raw value
 * m1_nfc_raw_read_irq_status() has returned this session. wu_a_seen/
 * autoresp_off/auth_rxe/nt_tx only report the FEW named bits this backend
 * acts on -- irq_union reveals every bit that actually fired (EON, RXE_PTA,
 * NRE, COL, etc.), needed to distinguish "reader field never registered at
 * all" from "field registered, something else prevented SELECT completing"
 * when a session ends via field-loss with wu_a_seen still 0. */
static volatile uint32_t  s_irq_union;

/*===========================================================================*/
/* Per-activation / capacity-probe diagnostics ONLY. Capacity detection      */
/* probes block                                                              */
/* 254 (4K), then block 62 (1K vs Mini), each via a plain AUTH+CRC_A frame,  */
/* and unconditionally returns NfcCommandReset between them (nfc.c:186-199:  */
/* field off, 100ms, field on, fresh activation) -- so M1 sees a GENUINE     */
/* field-loss/re-activation cycle between these probes. The session-end-only */
/* WA1-WA6 snapshot above can only ever                                      */
/* show the LAST activation; these counters give full per-activation and     */
/* per-probed-block visibility across the whole session. Zero effect on      */
/* control flow -- every field here is written-then-only-read-by-the-report. */
/*===========================================================================*/
static volatile uint32_t  s_activation_num;   /* bumped at session start + every re-arm (field-loss/timeout) */
static volatile uint32_t  s_blk254_auth_count, s_blk62_auth_count, s_blk14_auth_count; /* 4K/1K/Mini probes seen */
static volatile uint8_t   s_act_state_before_auth;  /* m1_sess_state_t at AUTH-frame dispatch, this activation */
static volatile uint8_t   s_act_auth_block;         /* raw[1] of the AUTH frame this activation, if any        */
static volatile uint16_t  s_act_auth_bits;          /* frame bit length (16/32/other) this activation          */
static volatile uint8_t   s_act_nrar_seen;          /* {Nr,Ar} record captured this activation                 */
static volatile uint8_t   s_act_ar_ok;              /* m1_mfc_session_on_nrar() result this activation         */
static volatile uint8_t   s_act_at_req;              /* {At} TX attempted this activation                      */
static volatile uint8_t   s_act_at_ok;               /* m1_mfc_dma_tx_frame() result for {At} this activation  */

/* Fine-grained per-activation fields, populated for EVERY activation but only
 * printed (m1_mfc_raw_hw_log_activation()) when s_act_auth_block is 254 or 62
 * -- the two blocks the capacity-detection logic actually probes
 * (see the block comment above; block 14 is NOT one of them, corrected this
 * round). Answers directly whether block 62's Nt reached the reader cleanly:
 * exact RXE/TX-request/TXE timing, register readback, every IRQ bit seen
 * during the activation (not just error bits), and how many times RX/FIFO
 * was re-armed -- proving whether a leftover interrupt or a missed re-arm
 * from the immediately-preceding (intentionally-rejected) block-254 probe
 * could have delayed or corrupted block 62's response. */
static volatile uint32_t  s_act_rxe_cyc;      /* DWT->CYCCNT at this activation's AUTH RXE */
static volatile uint32_t  s_act_tx_req_cyc;   /* DWT->CYCCNT right after TRANSMIT_WITHOUT_CRC issued */
static volatile uint32_t  s_act_txe_cyc;      /* DWT->CYCCNT at TXE, 0 if never observed */
static volatile uint32_t  s_act_irq_union;    /* every raw IRQ bit seen during this activation (not just errors) */
static volatile uint8_t   s_act_arm_rx_calls; /* m1_nfc_raw_own_arm_rx() (CLEAR_FIFO+UNMASK_RECEIVE_DATA) calls */

/*===========================================================================*/
/* Ownership state -- see m1_mfc_raw_session_hw.h and the corrected STARTING/ */
/* STOPPING lifecycle. Written only from nfc_worker_task, inside              */
/* m1_mfc_raw_hw_run(); m1_mfc_raw_hw_active() is the sole read-side gate     */
/* every other caller (nfc_driver.c, ListenerCycle, ListenIni) uses.          */
/*===========================================================================*/
typedef enum {
    M1_RAWOWN_INACTIVE = 0,
    M1_RAWOWN_STARTING,
    M1_RAWOWN_ACTIVE,
    M1_RAWOWN_STOPPING
} m1_rawown_state_t;

static volatile m1_rawown_state_t s_state = M1_RAWOWN_INACTIVE;

/* Direct-to-task notification bits (xTaskNotifyWait, NOT ulTaskNotifyTake --
 * two independent, distinguishable event sources share nfc_worker_task's one
 * 32-bit notification value: the ISR gives IRQ, m1_mfc_raw_hw_request_stop_
 * and_wait() gives STOP from a different task). */
#define M1_RAWOWN_NOTIFY_BIT_IRQ   (1UL << 0)
#define M1_RAWOWN_NOTIFY_BIT_STOP  (1UL << 1)

/* STARTED/STOPPED rendezvous with the UI (or any external caller) -- created
 * once, persistent, like every other static wake object in this file. */
static StaticSemaphore_t  s_started_buf;
static SemaphoreHandle_t  s_started_sem = NULL;
static StaticSemaphore_t  s_stopped_buf;
static SemaphoreHandle_t  s_stopped_sem = NULL;

enum { START_REQUEST_IDLE, START_REQUEST_PENDING, START_REQUEST_CANCELLED };
static volatile uint8_t s_start_request = START_REQUEST_IDLE;

bool m1_mfc_raw_hw_prepare_start(void)
{
    bool prepared = false;
    taskENTER_CRITICAL();
    if ((s_start_request == START_REQUEST_IDLE) &&
        (s_state == M1_RAWOWN_INACTIVE)) {
        if (s_started_sem == NULL)
            s_started_sem = xSemaphoreCreateBinaryStatic(&s_started_buf);
        if (s_stopped_sem == NULL)
            s_stopped_sem = xSemaphoreCreateBinaryStatic(&s_stopped_buf);
        if ((s_started_sem != NULL) && (s_stopped_sem != NULL)) {
            (void)xSemaphoreTake(s_started_sem, 0);
            (void)xSemaphoreTake(s_stopped_sem, 0);
            s_start_request = START_REQUEST_PENDING;
            prepared = true;
        }
    }
    taskEXIT_CRITICAL();
    return prepared;
}

bool m1_mfc_raw_hw_start_requested(void)
{
    return s_start_request == START_REQUEST_PENDING;
}

void m1_mfc_raw_hw_cancel_start(void)
{
    bool transitioned = false;
    taskENTER_CRITICAL();
    if (s_start_request == START_REQUEST_PENDING) {
        s_start_request = START_REQUEST_CANCELLED;
        transitioned = true;
    }
    taskEXIT_CRITICAL();
    /* Notify the worker only when this call actually moved PENDING ->
     * CANCELLED. Calling cancel on an already-IDLE/CANCELLED coordinator
     * (e.g. a second timeout path, or a defensive call after the request
     * was already retired) must never post a STOP the worker did not earn
     * -- a stale notification here would sit in nfc_worker_task_hdl's
     * notification word and be consumed by whatever the NEXT, unrelated
     * session happens to be waiting on. */
    if (transitioned && (nfc_worker_task_hdl != NULL))
        (void)xTaskNotify(nfc_worker_task_hdl, M1_RAWOWN_NOTIFY_BIT_STOP, eSetBits);
}

void m1_mfc_raw_hw_finish_request(void)
{
    /* Worker only, after complete cleanup; UI only if queue submission failed. */
    taskENTER_CRITICAL();
    s_start_request = START_REQUEST_IDLE;
    taskEXIT_CRITICAL();
}

/* Attempt bound: matches the existing AUTHRX cadence (M1_MFC_AUTHRX_ATTEMPT_US
 * = 50ms in m1_mfc_raw_listener.c) -- once ACTIVE, a session that gets no
 * capturable frame for this long is a stalled attempt, not a live one. */
#define M1_RAW_HW_ATTEMPT_US  (50000U)

/* Nt TXE bound: 10ms bounded wait for
 * TXE before giving up on a single transmission. Distinct from, and much
 * shorter than, M1_RAW_HW_ATTEMPT_US above (that one bounds a whole stalled
 * attempt; this one bounds a single, already-issued TX). */
#define M1_NT_TXE_TIMEOUT_US  (10000U)

/* Deferred (task-context-only) diagnostics snapshot, printed from
 * m1_mfc_raw_hw_report() outside any timed window. */
static uint8_t   s_last_stage_reached;
static uint32_t  s_last_auth_rxe, s_last_nt_tx;
static uint32_t  s_last_nrar_rxe, s_last_at_tx;
static uint32_t  s_last_read_rxe, s_last_blk_tx;
static uint32_t  s_last_wa_rxe;
static uint16_t  s_last_wa_bits;
static uint8_t   s_last_wa_b0, s_last_wa_b1;
static uint32_t  s_last_gate_bad_state, s_last_gate_bad_bits;
static uint32_t  s_last_gate_bad_cmd, s_last_gate_bad_block, s_last_gate_ok;
static uint32_t  s_last_wu_a_seen, s_last_autoresp_off, s_last_nt_tx_count;
static uint32_t  s_last_irq_union;
static uint32_t  s_last_nt_txe_seen, s_last_nt_err_irq;
static uint32_t  s_last_rxe_to_txreq_cyc, s_last_txreq_to_txe_cyc;
static uint8_t   s_last_nt_num_tx1, s_last_nt_num_tx2, s_last_nt_iso14443a_nfc;
static uint8_t   s_last_nt_bytes[4];
static bool      s_report_pending;

/*===========================================================================*/
/* Init                                                                      */
/*===========================================================================*/
static uint8_t s_id_uid[4];
static uint8_t s_id_atqa[2];
static uint8_t s_id_sak;

void m1_mfc_raw_hw_init(uint32_t cuid, uint64_t key, uint32_t nt,
                        const uint8_t atqa[2], uint8_t sak,
                        m1_mfc_key_resolve_fn key_resolve_fn, void *key_resolve_ctx,
                        m1_mfc_block_resolve_fn block_resolve_fn, void *block_resolve_ctx)
{
    platformLog("[RAW-TRACE] m1_mfc_raw_hw_init entry state=%d\r\n", (int)s_state);

    m1_mfc_session_init(&s_sess, cuid, key, nt);
    m1_mfc_session_set_key_resolver(&s_sess, key_resolve_fn, key_resolve_ctx);
    m1_mfc_session_set_block_resolver(&s_sess, block_resolve_fn, block_resolve_ctx);
    s_nonce_tx[0] = (uint8_t)(nt >> 24); s_nonce_tx[1] = (uint8_t)(nt >> 16);
    s_nonce_tx[2] = (uint8_t)(nt >> 8);  s_nonce_tx[3] = (uint8_t)(nt);
    /* Identity presented to the reader (rfalListenStart PT-memory config).
     * UID = cuid big-endian, matching the rest of this module; ATQA/SAK come
     * from the caller (real saved-card values in production, fixed MFC-1K
     * test values 0x0004/0x08 for the dev/diagnostic persona). */
    s_id_uid[0] = (uint8_t)(cuid >> 24); s_id_uid[1] = (uint8_t)(cuid >> 16);
    s_id_uid[2] = (uint8_t)(cuid >> 8);  s_id_uid[3] = (uint8_t)(cuid);
    s_id_atqa[0] = (atqa != NULL) ? atqa[0] : 0x04U;
    s_id_atqa[1] = (atqa != NULL) ? atqa[1] : 0x00U;
    s_id_sak     = sak;
    memset(&s_auth_rec, 0, sizeof(s_auth_rec));
    memset(&s_nrar_rec, 0, sizeof(s_nrar_rec));
    memset(&s_read_rec, 0, sizeof(s_read_rec));
    s_nonce_fired_isr = 0U;
    s_field_lost      = 0U;
    s_report_pending  = false;
    /* UI prepared and drained rendezvous objects before dispatch. Never
     * recreate/drain an object on which the UI may already be waiting. */
}

bool m1_mfc_raw_hw_active(void)
{
    return s_state != M1_RAWOWN_INACTIVE;
}

bool m1_mfc_raw_hw_wait_started(uint32_t max_ticks)
{
    if (s_started_sem == NULL) { return false; }
    return (xSemaphoreTake(s_started_sem, (TickType_t)max_ticks) == pdTRUE);
}

bool m1_mfc_raw_hw_request_stop_and_wait(uint32_t max_ticks)
{
    if (nfc_worker_task_hdl != NULL) {
        (void)xTaskNotify(nfc_worker_task_hdl, M1_RAWOWN_NOTIFY_BIT_STOP, eSetBits);
    }
    if (s_stopped_sem == NULL) { return false; }
    return (xSemaphoreTake(s_stopped_sem, (TickType_t)max_ticks) == pdTRUE);
}

/*===========================================================================*/
/* ISR: the entire ISR-side contract is this one call.                       */
/*===========================================================================*/
void m1_mfc_raw_hw_isr(void)
{
    BaseType_t woken = pdFALSE;
    if (nfc_worker_task_hdl != NULL) {
        (void)xTaskNotifyFromISR(nfc_worker_task_hdl, M1_RAWOWN_NOTIFY_BIT_IRQ, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* ISO/IEC 14443-3A minimum PICC response Frame Delay Time, in fc (13.56MHz
 * carrier) cycles -- 1172/fc =~ 86.4us. The delay is applied before any
 * FIFO/TX command is issued. M1 has no
 * equivalent dedicated timer wired into this backend; DWT->CYCCNT (already
 * used throughout this file for cycle-accurate timing) gives the same
 * microsecond-class precision a FreeRTOS tick-based wait cannot (1ms
 * granularity), so this ports the SAME logical behavior -- block until at
 * least 1172fc has elapsed since RX-end -- via a tight local spin instead of
 * a second hardware timer peripheral. This is not a busy-poll of the
 * ST25R3916 over SPI (that rule was about the separate TXE-completion wait,
 * m1_mfc_raw_hw_run()'s notify loop, and remains unchanged) -- it is a pure
 * CPU-local cycle count, run at M1_MFC_RAWOWN_PRIORITY specifically to
 * support exactly this kind of latency-critical span. No compensation
 * compensation constant is applied; this waits for the full, uncompensated 1172fc,
 * which can only make the response arrive AT OR AFTER the ISO14443-A
 * minimum, never before (the direction that matters: too early is a
 * protocol violation, too late merely narrows the margin to FWT). */
#define M1_ISO14443_3A_FDT_LISTEN_FC  (1172U)

static void m1_raw_hw_fdt_listen_wait(uint32_t rxe_ref)
{
    const uint32_t fc = 13560000U;
    uint32_t fdt_cyc = (uint32_t)(((uint64_t)M1_ISO14443_3A_FDT_LISTEN_FC * SystemCoreClock) / fc);
    while ((uint32_t)(DWT->CYCCNT - rxe_ref) < fdt_cyc) { /* spin */ }
}

/*===========================================================================*/
/* WAIT_AUTH RXE handler -- unchanged gate/latch/TX logic (see the header     */
/* comment on m1_raw_auth_rec_t). Runs inline in nfc_worker_task now, but the  */
/* exact accept/reject decision and bounded native Nt TX are byte-for-byte    */
/* what the prior ISR-side version did.                                      */
/*===========================================================================*/
static bool m1_mfc_raw_wait_auth_rxe(uint32_t rxe_cyc)
{
    s_wa_rxe_count++;   /* every RXE observed while in WAIT_AUTH, before any gate check */

    /* Per-activation diagnostics only: state BEFORE this AUTH frame is even
     * inspected -- the capacity-probe trace needs to distinguish "still WAIT_AUTH,
     * genuinely a fresh probe" from a stale/mid-auth frame. */
    s_act_state_before_auth = (uint8_t)s_sess.state;

    if (s_nonce_fired_isr != 0U) { return false; }

    uint16_t n = st25r3916GetNumFIFOBytes();
    uint8_t  raw[4] = {0};
    uint16_t rn = (n > (uint16_t)sizeof(raw)) ? (uint16_t)sizeof(raw) : n;
    if (rn > 0U) { (void)st25r3916ReadFifo(raw, rn); }

    /* Diagnostic only: retain the last frame's bit length + first two bytes,
     * and mirror the gate's own conditions purely for counting which one a
     * rejected frame hit. Read-only mirror -- m1_mfc_session_nonce_gate()
     * immediately below is the sole, unmodified accept/reject authority; this
     * block must track that function's actual acceptance rule (16 or 32 bits,
     * CRC_A-validated for 32) or the counters lie about why a frame failed. */
    uint16_t diag_bits = (uint16_t)(rn * 8U);
    s_wa_last_bits = diag_bits;
    s_wa_last_b0   = raw[0];
    s_wa_last_b1   = raw[1];

    /* Per-activation + cumulative diagnostics only: any plausible AUTH-shaped
     * frame (16 or 32 bits) records its target block for this activation's
     * [ACT] line, and TWO of the three tracked blocks -- 254 (4K check) and
     * 62 (1K vs Mini check) -- are the actual capacity probes; block 14 is
     * NOT part of that sequence and is kept as a harmless catch-all, not
     * evidence of anything on its own. Counted cumulatively regardless of
     * whether the gate below ultimately accepts or rejects the frame -- the
     * question is whether the reader tried each block, not only whether M1
     * answered it. */
    if ((diag_bits == 16U) || (diag_bits == 32U)) {
        s_act_auth_block = raw[1];
        s_act_auth_bits  = diag_bits;
        s_act_rxe_cyc    = rxe_cyc;
        if (raw[1] == 254U)      { s_blk254_auth_count++; }
        else if (raw[1] == 62U)  { s_blk62_auth_count++;  }
        else if (raw[1] == 14U)  { s_blk14_auth_count++;  }
    }

    if (s_sess.state != M1_SESS_WAIT_AUTH)                            { s_gate_bad_state++; }
    else if ((diag_bits != 16U) && (diag_bits != 32U))                { s_gate_bad_bits++;  }
    else if ((diag_bits == 32U) &&
             (((uint8_t)(ce_crc_a(raw, 2U) & 0xFFU) != raw[2]) ||
              ((uint8_t)(ce_crc_a(raw, 2U) >> 8) != raw[3])))         { s_gate_bad_bits++;  }
    else if ((raw[0] != 0x60U) && (raw[0] != 0x61U))                  { s_gate_bad_cmd++;   }
    else if (raw[1] >= 64U)                                           { s_gate_bad_block++; }
    else                                                               { s_gate_ok++;        }

    /* Exact gate: fresh 16-bit logical or 32-bit raw+CRC_A-valid 60/61,
     * block<64, WAIT_AUTH, no auth running. Rejects 30 00 / stale 60 FE
     * (block>=64) / bad CRC_A / short / mid-auth silently -- no state change,
     * no TX. */
    if (!m1_mfc_session_nonce_gate(&s_sess, raw, (uint16_t)(rn * 8U))) { return false; }

    s_auth_rec.cmd[0] = raw[0];
    s_auth_rec.cmd[1] = raw[1];
    s_auth_rec.gen    = s_sess.gen;
    s_auth_rec.rxe_cyc = rxe_cyc;

    /* Wait for the ISO14443-A minimum listener FDT (1172fc since RX-end)
     * BEFORE touching the FIFO/TX command, exactly mirroring the order of
     * the listener TX sequence (wait first, then
     * common_fifo_tx()'s CLEAR_FIFO+write+TRANSMIT) -- see the function
     * comment above for the full trace. This is the one and only change in
     * this candidate; everything below is unchanged. */
    m1_raw_hw_fdt_listen_wait(rxe_cyc);

    /* Bounded native nonce TX: prepared Nt, chip adds standard parity.
     * One-per-session, latched here before any other consumer runs. */
    s_nonce_fired_isr = 1U;
    st25r3916SetNumTxBits(32U);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
    (void)st25r3916WriteFifo(s_nonce_tx, 4U);
    (void)st25r3916ExecuteCommand(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
    s_auth_rec.nt_tx_cyc = DWT->CYCCNT;
    s_nt_tx_count++;

    /* WAIT_NT_TXE: mark the Nt TX pending TXE confirmation, and read back the
     * exact registers this TX just used -- see the field declarations above.
     * The listener waits for
     * TXE (bounded, 10ms) BEFORE returning, and never re-arms RX before that.
     * m1_nfc_raw_own_arm_rx() and publishing s_auth_rec.ready are therefore
     * deliberately NOT done here anymore -- both are deferred to the TXE (or
     * timeout) handling in m1_mfc_raw_hw_run()'s notify-wait loop below,
     * mirroring that exact ordering. s_sess.state stays WAIT_AUTH until then
     * (m1_mfc_session_take_auth() -- which advances it to NONCE_SENT -- only
     * runs once s_auth_rec.ready is set), so a stray RXE during this window
     * dispatches back into this same function, which no-ops immediately on
     * s_nonce_fired_isr already being set -- no double transmission risk. */
    s_nt_tx_pending = 1U;
    s_nt_tx_req_cyc = s_auth_rec.nt_tx_cyc;
    s_act_tx_req_cyc = s_auth_rec.nt_tx_cyc;   /* per-activation diagnostics only */
    {
        uint8_t v1 = 0U, v2 = 0U, v3 = 0U;
        (void)st25r3916ReadRegister(ST25R3916_REG_NUM_TX_BYTES1, &v1);
        (void)st25r3916ReadRegister(ST25R3916_REG_NUM_TX_BYTES2, &v2);
        (void)st25r3916ReadRegister(ST25R3916_REG_ISO14443A_NFC, &v3);
        s_nt_num_tx1 = v1; s_nt_num_tx2 = v2; s_nt_iso14443a_nfc = v3;
    }

    return true;
}

/*===========================================================================*/
/* FDT-grid target helper -- unchanged.                                      */
/*===========================================================================*/
static uint32_t m1_raw_hw_fdt_target(uint32_t rxe_ref, uint32_t prep_cyc)
{
    const uint32_t FC = 13560000u;
    uint32_t base_cyc = (uint32_t)(((uint64_t)1236u * SystemCoreClock) / FC);
    uint32_t step_cyc = (uint32_t)(((uint64_t)128u  * SystemCoreClock) / FC);
    if (step_cyc == 0U) { step_cyc = 1U; }
    uint32_t ready = (uint32_t)(DWT->CYCCNT - rxe_ref) + prep_cyc;
    uint32_t target = base_cyc;
    if (ready > base_cyc) {
        target = base_cyc + (((ready - base_cyc) + step_cyc - 1U) / step_cyc) * step_cyc;
    }
    return target;
}

/*===========================================================================*/
/* Frame dispatch -- unchanged worker-side AUTH/NRAR/READ processing, now     */
/* called inline from the same task/loop iteration that captured the frame.  */
/* Returns true if the session ended (session_end() called) during this      */
/* pass, so the run-loop can stop iterating.                                 */
/*===========================================================================*/
static bool m1_mfc_raw_hw_process_pending(void);   /* fwd decl, defined after session_end() below */

/* Per-activation diagnostics only -- prints ONE compact line (well under the
 * 160-byte logger cap) each time an activation ends (field-loss, WAIT_NT_TXE
 * timeout, or session stop), so the full per-activation history is visible
 * across a session with multiple capacity-probe re-activations -- not just
 * the last one (see the field block comment above). reset_reason: 0=field-
 * loss, 1=nt-txe-timeout, 2=stop/session-end (explicit STOP/BACK or
 * unrecoverable hardware failure only), 3=auth-consistency-fail,
 * 4=bad-ar, 5=nrar-stale-or-short, 6=read-reject, 7=read-stale-or-short,
 * 8=attempt-timeout -- 3-8 are all soft re-arms (m1_mfc_raw_hw_soft_rearm()):
 * the current activation ends, the session does not. */
static void m1_mfc_raw_hw_log_activation(uint8_t reset_reason)
{
    /* Kept short deliberately (measured worst case ~58 bytes, well under
     * M1_LOGDB_MESSAGE_SIZE=80): a line landing AT or ABOVE 80 forces
     * m1_logdb_dyn_vsprintf() (m1_log_debug.c) to free+realloc+retry on
     * EVERY call before it can succeed, and that retry allocation silently
     * drops the whole line if it fails -- confirmed to be exactly what
     * happened to an earlier, longer version of this same line (all 4
     * expected prints missing from a real capture, m1_logdb_printf's
     * malloc-NULL path (line ~923/985) returns/breaks with nothing
     * written, no error). Field key: n=activation, b=AUTH block,
     * bt=frame bits, s=state-before-AUTH, tx=Nt TXE seen, nr={Nr,Ar}
     * seen, ar=Ar valid, aq={At} requested, ao={At} TX result,
     * r=reset_reason (see the function-level comment above for 0-8). */
    platformLog("[ACT] n=%u b=%u bt=%u s=%u tx=%u nr=%u ar=%u aq=%u ao=%u r=%u\r\n",
                (unsigned)s_activation_num,
                (unsigned)s_act_auth_block, (unsigned)s_act_auth_bits,
                (unsigned)s_act_state_before_auth, (unsigned)s_nt_txe_seen,
                (unsigned)s_act_nrar_seen, (unsigned)s_act_ar_ok,
                (unsigned)s_act_at_req, (unsigned)s_act_at_ok,
                (unsigned)reset_reason);

    /* Fine-grained detail, ONLY for the two capacity-
     * detection actually probes (254=4K check, 62=1K-vs-Mini check;
     * confirmed against mf_classic_poller.c:127-159 this round) -- kept
     * separate from [ACT] above so ordinary/noise activations (most of a
     * session) don't pay for two log lines. Both lines independently
     * verified under 80 bytes worst-case (63 and 43). rxq=RXE-to-TX-request
     * delay, qte=TX-request-to-TXE delay (0 if TXE never seen), irq=every
     * raw IRQ bit seen this activation (not just errors -- proves whether a
     * leftover NRE/RXE/TXE from the PRECEDING activation's rejected AUTH
     * was still pending), arm=how many times RX/FIFO was re-armed this
     * activation (0 is normal/expected if TXE never fired -- arm only
     * happens on confirmed TXE, see the run-loop). ntb/iso are the same
     * NUM_TX_BYTES1/2 + ISO14443A_NFC register readback already taken at
     * TX-issue time (m1_mfc_raw_wait_auth_rxe()) -- fixed by design (32-bit
     * TX, standard parity) but reported per-activation for direct proof. */
    if ((s_act_auth_block == 254U) || (s_act_auth_block == 62U)) {
        uint32_t cpu = SystemCoreClock / 1000000U; if (cpu == 0U) { cpu = 1U; }
        uint32_t rxq = (s_act_tx_req_cyc && s_act_rxe_cyc) ? ((s_act_tx_req_cyc - s_act_rxe_cyc) / cpu) : 0U;
        uint32_t qte = (s_act_txe_cyc && s_act_tx_req_cyc) ? ((s_act_txe_cyc - s_act_tx_req_cyc) / cpu) : 0U;
        platformLog("[BD] n=%u b=%u rxq=%lu qte=%lu irq=%08lX arm=%u\r\n",
                    (unsigned)s_activation_num, (unsigned)s_act_auth_block,
                    (unsigned long)rxq, (unsigned long)qte,
                    (unsigned long)s_act_irq_union, (unsigned)s_act_arm_rx_calls);
        platformLog("[BD2] n=%u nt=%02X%02X%02X%02X ntb=%02X%02X iso=%02X\r\n",
                    (unsigned)s_activation_num,
                    (unsigned)s_nonce_tx[0], (unsigned)s_nonce_tx[1],
                    (unsigned)s_nonce_tx[2], (unsigned)s_nonce_tx[3],
                    (unsigned)s_nt_num_tx1, (unsigned)s_nt_num_tx2,
                    (unsigned)s_nt_iso14443a_nfc);
    }
}

/* Resets ONLY the per-activation diagnostic fields above (not the cumulative
 * block-254/62/14 counters, which span the whole session) -- called at the
 * start of every fresh activation (session start, field-loss re-arm,
 * NT-TXE-timeout re-arm), mirroring how s_wu_a_seen/s_autoresp_off are
 * already reset per-activation at each of those exact points. */
static void m1_mfc_raw_hw_reset_activation_diag(void)
{
    s_activation_num++;
    s_act_state_before_auth = 0U; s_act_auth_block = 0U; s_act_auth_bits = 0U;
    s_act_nrar_seen = 0U; s_act_ar_ok = 0U; s_act_at_req = 0U; s_act_at_ok = 0U;
    s_act_rxe_cyc = 0U; s_act_tx_req_cyc = 0U; s_act_txe_cyc = 0U;
    s_act_irq_union = 0U; s_act_arm_rx_calls = 0U;
}

/* Shared soft re-arm: end ONLY the current activation and keep the session
 * (and thus MFC Emulate on screen) running, exactly mirroring the existing
 * field-loss (reset_reason=0) and NT-TXE-timeout (reset_reason=1) re-arm
 * logic already proven on hardware -- this is that same sequence, factored
 * out so every per-activation-recoverable exit uses one proven path instead
 * of six near-duplicates. Proven necessary by dual-device capture: a
 * single rejected dictionary-attack key (bad Ar) was calling the FULL
 * m1_mfc_raw_hw_stopping() (session end, radio handed back to RFAL) after
 * exactly one wrong key guess -- indistinguishable in the [ACT] log from an
 * explicit STOP (both used reset_reason=2) until the BACK-press timestamp
 * proved a 57.8s gap with no button press. A dictionary attack routinely
 * tries many wrong keys before (if ever) finding the right one; rejecting
 * one candidate key must not end emulation any more than an ordinary field
 * cycle does. Distinct reset_reason codes per caller (3-8) so this
 * ambiguity cannot recur. Does NOT touch ownership/EXTI/IRQ mask/task
 * state -- those remain exactly as m1_nfc_raw_own_idle_direct() (via
 * acquire()) already configured them for the session's whole lifetime. */
static void m1_mfc_raw_hw_soft_rearm(uint8_t reset_reason)
{
    m1_mfc_raw_hw_log_activation(reset_reason);
    m1_nfc_raw_own_idle_direct();
    s_auth_rec.ready = 0U; s_nrar_rec.ready = 0U; s_read_rec.ready = 0U;
    s_nonce_fired_isr = 0U;
    m1_mfc_session_begin(&s_sess);
    s_wu_a_seen    = 0U;
    s_autoresp_off = 0U;
    s_nt_tx_pending = 0U; s_nt_txe_seen = 0U; s_nt_err_irq = 0U;
    m1_mfc_raw_hw_reset_activation_diag();
}

/*===========================================================================*/
/* Corrected STARTING sequence.                                              */
/*===========================================================================*/
static m1_raw_own_snapshot_t s_own_snap;

static bool m1_mfc_raw_hw_starting(void)
{
    s_state = M1_RAWOWN_STARTING;

    /* 1. Mask EXTI at the NVIC level -- neither st25r3916Isr() nor
     *    m1_mfc_raw_hw_isr() can run from here until the final unmask below. */
    m1_nfc_raw_own_exti_mask();

    /* 2. "Quiesce the normal NFC worker": free -- this function runs inside
     *    nfc_worker_task itself, so by construction nothing else is
     *    concurrently running on it (single program counter). */

    /* 3. Clear stale hardware + RFAL-sticky + MCU-pending state BEFORE
     *    GOTO_SENSE -- so a genuine WU_A latched right after rfalListenStart()
     *    is never erased by a later clear (see m1_nfc_raw_hal.h). */
    m1_nfc_raw_own_clear_stale_irq_state();

    /* 4. Establish ownership + reset per-session diagnostics/state now, still
     *    fully masked. */
    s_auth_rec.ready = 0U; s_nrar_rec.ready = 0U; s_read_rec.ready = 0U;
    s_nonce_fired_isr = 0U;
    s_field_lost      = 0U;
    s_wa_rxe_count    = 0U;
    s_wa_last_bits = 0U; s_wa_last_b0 = 0U; s_wa_last_b1 = 0U;
    s_gate_bad_state = 0U; s_gate_bad_bits = 0U;
    s_gate_bad_cmd   = 0U; s_gate_bad_block = 0U; s_gate_ok = 0U;
    s_wu_a_seen = 0U; s_autoresp_off = 0U; s_nt_tx_count = 0U; s_irq_union = 0U;
    s_nt_tx_pending = 0U; s_nt_txe_seen = 0U; s_nt_err_irq = 0U;
    s_nt_num_tx1 = 0U; s_nt_num_tx2 = 0U; s_nt_iso14443a_nfc = 0U;
    s_activation_num = 0U;   /* reset_activation_diag() below bumps it to 1 */
    s_blk254_auth_count = 0U; s_blk62_auth_count = 0U; s_blk14_auth_count = 0U;
    m1_mfc_raw_hw_reset_activation_diag();
    m1_mfc_session_begin(&s_sess);
    /* Drop any stale notify bits left over from a prior session (e.g. a late
     * IRQ notify that arrived after the last session's own STOPPING sequence
     * already masked EXTI) -- clear-on-entry mask of all bits, non-blocking. */
    {
        uint32_t stale = 0U;
        (void)xTaskNotifyWait(0xFFFFFFFFUL, 0xFFFFFFFFUL, &stale, 0U);
    }

    /* 5. Program the listener and enter sense mode. m1_nfc_raw_own_acquire()
     *    enables the full listener IRQ mask, then rfalListenStart()
     *    (whose internal rfalListenSetState(POWER_OFF) issues GOTO_SENSE). Any
     *    WU_A/REQA/SELECT event from here on simply latches in hardware,
     *    undisturbed -- nothing clears it again until this session ends. */
    if (!m1_nfc_raw_own_acquire(&s_own_snap, s_id_uid, s_id_atqa, s_id_sak)) {
        /* Complete acquire-failure rollback, still fully masked: nothing to
         * release (acquire() itself already rolled back its own partial IRQ
         * enable on failure -- m1_nfc_raw_hal.c), just restore ownership
         * state and EXTI before reporting. */
        s_state = M1_RAWOWN_INACTIVE;
        m1_nfc_raw_own_exti_unmask();
        /* reason: 1=oscOn, 2=rfalListenStart, 3=register-readback verify --
         * see M1_RAW_ACQUIRE_FAIL_* (m1_nfc_raw_hal.h). Previously this line
         * fired identically regardless of cause. */
        platformLog("[B1-RAWHW-START-FAIL] acquire() failed reason=%u\r\n",
                    (unsigned)m1_nfc_raw_own_fail_reason());
        return false;   /* STARTED is never given; caller's wait times out */
    }

    /* 6. Ownership fully established. */
    s_state = M1_RAWOWN_ACTIVE;

    /* 7. Unmask EXTI last. Any WU_A/REQA/SELECT event already latched during
     *    steps 5-6 is delivered immediately (NVIC sees the already-pending
     *    line the instant it's re-enabled -- no fresh edge required). */
    m1_nfc_raw_own_exti_unmask();

    return true;
}

/*===========================================================================*/
/* Corrected STOPPING sequence.                                              */
/*===========================================================================*/
static void m1_mfc_raw_hw_stopping(const char *reason)
{
    (void)reason;

    /* 1. state ACTIVE -> STOPPING: m1_mfc_raw_hw_active() stays true (still
     *    "not INACTIVE") for the whole sequence, so every external gate
     *    (nfc_driver.c, ListenerCycle, ListenIni) stays closed. */
    s_state = M1_RAWOWN_STOPPING;

    /* 2. Mask EXTI immediately, before touching ownership/registers. No new
     *    event reaches either consumer from here. */
    m1_nfc_raw_own_exti_mask();

    /* 3. No task rendezvous is needed: this function runs inside
     *    nfc_worker_task, the same task that was (or wasn't) mid-dispatch --
     *    single program counter, nothing concurrent to wait out. */

    /* Deferred diagnostics snapshot (task context only). */
    s_last_stage_reached = (uint8_t)s_sess.state;
    s_last_auth_rxe = s_auth_rec.rxe_cyc; s_last_nt_tx = s_auth_rec.nt_tx_cyc;
    s_last_nrar_rxe = s_nrar_rec.rxe_cyc;
    s_last_read_rxe = s_read_rec.rxe_cyc;
    s_last_wa_rxe   = s_wa_rxe_count;
    s_last_wa_bits  = s_wa_last_bits;
    s_last_wa_b0    = s_wa_last_b0;
    s_last_wa_b1    = s_wa_last_b1;
    s_last_gate_bad_state = s_gate_bad_state;
    s_last_gate_bad_bits  = s_gate_bad_bits;
    s_last_gate_bad_cmd   = s_gate_bad_cmd;
    s_last_gate_bad_block = s_gate_bad_block;
    s_last_gate_ok        = s_gate_ok;
    s_last_wu_a_seen      = s_wu_a_seen;
    s_last_autoresp_off   = s_autoresp_off;
    s_last_nt_txe_seen    = s_nt_txe_seen;
    s_last_nt_err_irq     = s_nt_err_irq;
    s_last_rxe_to_txreq_cyc = (s_nt_tx_req_cyc && s_auth_rec.rxe_cyc)
                              ? (s_nt_tx_req_cyc - s_auth_rec.rxe_cyc) : 0U;
    s_last_txreq_to_txe_cyc = (s_nt_txe_seen && s_nt_tx_req_cyc)
                              ? (s_nt_txe_cyc - s_nt_tx_req_cyc) : 0U;
    s_last_nt_num_tx1       = s_nt_num_tx1;
    s_last_nt_num_tx2       = s_nt_num_tx2;
    s_last_nt_iso14443a_nfc = s_nt_iso14443a_nfc;
    s_last_nt_bytes[0] = s_nonce_tx[0]; s_last_nt_bytes[1] = s_nonce_tx[1];
    s_last_nt_bytes[2] = s_nonce_tx[2]; s_last_nt_bytes[3] = s_nonce_tx[3];
    s_last_nt_tx_count    = s_nt_tx_count;
    s_last_irq_union      = s_irq_union;
    s_report_pending = true;

    /* Per-activation diagnostics only: log the CURRENT (final) activation's
     * outcome -- every EARLIER activation this session already logged its
     * own line at its own field-loss/timeout re-arm point above. */
    m1_mfc_raw_hw_log_activation(2U);   /* reset_reason=2: stop/session-end */
    /* Short deliberately, same reason as m1_mfc_raw_hw_log_activation() above. */
    platformLog("[BLK] b254=%u b62=%u b14=%u a=%u\r\n",
                (unsigned)s_blk254_auth_count, (unsigned)s_blk62_auth_count,
                (unsigned)s_blk14_auth_count, (unsigned)s_activation_num);

    /* 4. Release ownership (disable the RAWOWN mask, rfalListenStop()). */
    m1_nfc_raw_own_release(&s_own_snap);

    /* 5. Clear teardown state -- hardware + RFAL-sticky + MCU-pending -- so
     *    RFAL resumes into a genuinely clean state (unlike STARTING, clearing
     *    AFTER release here is correct: there is no RAWOWN-relevant event
     *    left to preserve). */
    m1_nfc_raw_own_clear_stale_irq_state();

    /* 6. Ownership formally transferred back to RFAL. */
    s_state = M1_RAWOWN_INACTIVE;

    /* 7. Unmask EXTI last -- fresh events now correctly route to
     *    st25r3916Isr(). */
    m1_nfc_raw_own_exti_unmask();

    m1_mfc_session_end(&s_sess);

    /* Deliver the report synchronously -- outside any timed window, task
     * context, after ownership is fully released. */
    m1_mfc_raw_hw_report();
}

/* Invariant-violation canary only -- see the declaration in
 * m1_mfc_raw_session_hw.h. Not called anywhere in this backend's own control
 * flow; exists solely so rfal_nfc.c's PRE-EXISTING rfalNfcListenActivation()
 * patch (vendor file, not touched here) keeps resolving. */
void m1_mfc_raw_hw_session_end(const char *reason)
{
    if (m1_mfc_raw_hw_active()) {
        m1_mfc_raw_hw_stopping(reason);
        if (s_stopped_sem != NULL) { (void)xSemaphoreGive(s_stopped_sem); }
    }
}

static bool m1_mfc_raw_hw_process_pending(void)
{
    /* Field loss takes priority over any pending record. Ordinary field-loss
     * does NOT end the session: field-off returns the listener to idle and
     * continues its listener loop; only
     * an explicit abort request ends it. Mirror that exactly: re-arm the
     * silicon auto-responder (m1_nfc_raw_own_idle_direct(), the same
     * function acquire()/release() already use), discard any in-flight
     * AUTH/NrAr/read record, and start a fresh activation via
     * m1_mfc_session_begin() -- ownership, EXTI, the IRQ mask, and the task
     * itself are untouched. */
    if (s_field_lost != 0U) {
        s_field_lost = 0U;
        m1_mfc_raw_hw_soft_rearm(0U);   /* reset_reason=0: field-loss */
        return false;
    }

    /* Attempt-timeout bound: no capturable progress for too long since ACTIVE. */
    /* (checked by the caller via a bounded notify-wait timeout; see m1_mfc_raw_hw_run()) */

    /* --- AUTH captured: this task owns Crypto1 init from here. ------------ */
    if (s_auth_rec.ready != 0U) {
        s_auth_rec.ready = 0U;   /* consume */
        if (s_auth_rec.gen == s_sess.gen) {
            uint8_t nt_check[4];
            if (m1_mfc_session_take_auth(&s_sess, s_auth_rec.cmd, 16U, nt_check) &&
                (memcmp(nt_check, s_nonce_tx, 4U) == 0)) {
                s_last_auth_rxe = s_auth_rec.rxe_cyc;
                s_last_nt_tx    = s_auth_rec.nt_tx_cyc;
            } else {
                m1_mfc_raw_hw_soft_rearm(3U);   /* reset_reason=3: auth-consistency-fail */
                return false;
            }
        }
        return false;
    }

    /* --- {Nr,Ar} captured: verify Ar, build+transmit {At}, re-arm READ RX. -- */
    if (s_nrar_rec.ready != 0U) {
        s_nrar_rec.ready = 0U;
        s_act_nrar_seen = 1U;   /* diagnostics only */
        if ((s_nrar_rec.gen == s_sess.gen) && (s_nrar_rec.len == 8U)) {
            uint8_t at_b[4], at_par[4];
            bool ar_ok = m1_mfc_session_on_nrar(&s_sess, s_nrar_rec.data, at_b, at_par);
            s_act_ar_ok = ar_ok ? 1U : 0U;   /* diagnostics only */
            if (ar_ok) {
                (void)m1_mfc_dma_prime_frame(at_b, at_par, 4U);
                uint32_t rxe_ref = s_nrar_rec.rxe_cyc;
                uint32_t target  = m1_raw_hw_fdt_target(rxe_ref, 60U * (SystemCoreClock / 1000000U));
                m1_dma_result_t r; (void)memset(&r, 0, sizeof(r));
                s_act_at_req = 1U;   /* diagnostics only */
                bool at_ok = m1_mfc_dma_tx_frame(at_b, at_par, 4U, rxe_ref, target, &r);
                s_act_at_ok = at_ok ? 1U : 0U;   /* diagnostics only -- closest existing
                                                  * completion signal on this transparent-
                                                  * mode path; no new TXE wait added here */
                s_last_at_tx = DWT->CYCCNT;
                m1_nfc_raw_own_arm_rx();   /* defense in depth -- m1_mfc_dma_tx_frame already re-arms */
            } else {
                m1_mfc_raw_hw_soft_rearm(4U);   /* reset_reason=4: bad-ar (e.g. a
                                                  * wrong dictionary-attack key --
                                                  * routine, must not end emulation) */
                return false;
            }
        } else {
            m1_mfc_raw_hw_soft_rearm(5U);   /* reset_reason=5: nrar-stale-or-short */
            return false;
        }
        return false;
    }

    /* --- READ captured: decrypt, validate, transmit block, re-arm. --------- */
    if (s_read_rec.ready != 0U) {
        s_read_rec.ready = 0U;
        if ((s_read_rec.gen == s_sess.gen) && (s_read_rec.len == 4U)) {
            uint8_t blk18[18], par18[18], blk = 0U;
            int rc = m1_mfc_session_on_read(&s_sess, s_read_rec.data, blk18, par18, &blk);
            if (rc == 1) {
                (void)m1_mfc_dma_prime_frame(blk18, par18, 18U);
                uint32_t rxe_ref = s_read_rec.rxe_cyc;
                uint32_t target  = m1_raw_hw_fdt_target(rxe_ref, 60U * (SystemCoreClock / 1000000U));
                m1_dma_result_t r; (void)memset(&r, 0, sizeof(r));
                (void)m1_mfc_dma_tx_frame(blk18, par18, 18U, rxe_ref, target, &r);
                s_last_blk_tx = DWT->CYCCNT;
                m1_nfc_raw_own_arm_rx();
                /* Sector-0 success: block answered. Session stays until the
                 * reader's own HALT/field-loss triggers cleanup, or STOP. */
            } else {
                m1_mfc_raw_hw_soft_rearm(6U);   /* reset_reason=6: read-reject */
                return false;
            }
        } else {
            m1_mfc_raw_hw_soft_rearm(7U);   /* reset_reason=7: read-stale-or-short */
            return false;
        }
        return false;
    }

    return false;
}

/*===========================================================================*/
/* The one public entry point.                                               */
/*===========================================================================*/
void m1_mfc_raw_hw_run(void)
{
    if (!m1_mfc_raw_hw_start_requested()) return;
    UBaseType_t saved_priority = uxTaskPriorityGet(NULL);

    /* Raise this task's own priority BEFORE listener configuration/GOTO_SENSE
     * and before EXTI is enabled, restored only after complete teardown --
     * covers the whole latency-critical span without a second task. */
    vTaskPrioritySet(NULL, M1_MFC_RAWOWN_PRIORITY);

    if (!m1_mfc_raw_hw_starting()) {
        /* STARTED is correctly never given here (m1_mfc_raw_hw_wait_started()
         * times out, as intended -- the caller must not proceed as if
         * armed). But STOPPED must still be given: this function is about to
         * return without ever entering its notify-wait loop, so if the UI
         * later calls m1_mfc_raw_hw_request_stop_and_wait() (e.g. on BACK),
         * nothing would ever be listening for that STOP notification and
         * its own bounded wait would time out for no reason -- the session
         * is already fully INACTIVE at this point (m1_mfc_raw_hw_starting()
         * guarantees that on its own failure path), so signal it as such. */
        if (s_stopped_sem != NULL) { (void)xSemaphoreGive(s_stopped_sem); }
        vTaskPrioritySet(NULL, saved_priority);
        return;
    }

    /* Cancellation during initialization must not turn into a late start.
     * Later cancellation is delivered by the existing STOP notification. */
    if (!m1_mfc_raw_hw_start_requested()) {
        m1_mfc_raw_hw_stopping("start-cancelled");
        if (s_stopped_sem != NULL) (void)xSemaphoreGive(s_stopped_sem);
        vTaskPrioritySet(NULL, saved_priority);
        return;
    }

    /* Give STARTED only now: acquire succeeded, state is ACTIVE, EXTI is
     * enabled. */
    if (s_started_sem != NULL) { (void)xSemaphoreGive(s_started_sem); }

    uint32_t cpu = SystemCoreClock / 1000000U; if (cpu == 0U) { cpu = 1U; }
    const TickType_t poll_ticks = pdMS_TO_TICKS(M1_RAW_HW_ATTEMPT_US / 1000U);
    uint32_t attempt_start_cyc = 0U;   /* set once, the instant WU_A is first seen below */

    for (;;) {
        uint32_t notified = 0U;
        /* Bounded wait: the reader-approach wait is legitimately unbounded
         * (no reader yet -> no WU_A -> the timeout branch below is a no-op
         * and simply loops again), but a bounded period is still needed so a
         * post-SELECT attempt that goes capture-silent can be detected --
         * see the WU_A-gated elapsed-time check below, not this timeout
         * value itself. STOP is delivered immediately regardless of this
         * period (the notification wakes the wait directly).
         *
         * While an Nt TX is pending TXE confirmation, shorten this wait to
         * the M1_NT_TXE_TIMEOUT_US-class bound instead of the normal (much
         * longer) poll_ticks -- this is how the 10ms deadline is integrated
         * into the existing notification wait without a separate busy-poll
         * loop: the wait itself simply returns sooner so the timeout check
         * below can run promptly. */
        TickType_t wait_ticks = (s_nt_tx_pending != 0U)
                                ? pdMS_TO_TICKS(M1_NT_TXE_TIMEOUT_US / 1000U) : poll_ticks;
        (void)xTaskNotifyWait(0U, (M1_RAWOWN_NOTIFY_BIT_IRQ | M1_RAWOWN_NOTIFY_BIT_STOP),
                              &notified, wait_ticks);

        if ((notified & M1_RAWOWN_NOTIFY_BIT_STOP) != 0U) {
            m1_mfc_raw_hw_stopping("stop-requested");
            break;
        }

        /* WAIT_NT_TXE deadline check -- applies whether or not this pass
         * delivered an IRQ (the IRQ that arrived, if any, might be unrelated
         * to TXE; the deadline is measured from the TX request regardless).
         * On timeout here, M1 safely
         * cancels the current activation using the same proven direct-
         * listener re-arm already used for ordinary field-loss
         * (m1_nfc_raw_own_idle_direct() + m1_mfc_session_begin()), stays
         * ACTIVE, and does NOT claim Nt was transmitted (s_auth_rec.ready is
         * never set on this path). */
        if ((s_nt_tx_pending != 0U) &&
            ((uint32_t)(DWT->CYCCNT - s_nt_tx_req_cyc) > (M1_NT_TXE_TIMEOUT_US * cpu))) {
            s_nt_tx_pending = 0U;
            m1_mfc_raw_hw_soft_rearm(1U);   /* reset_reason=1: nt-txe-timeout */
            if ((notified & M1_RAWOWN_NOTIFY_BIT_IRQ) == 0U) { continue; }
            /* An IRQ also arrived this same pass -- fall through to process
             * it normally (e.g. EOF) rather than discarding it. */
        }

        if ((notified & M1_RAWOWN_NOTIFY_BIT_IRQ) == 0U) {
            /* Timed out with no IRQ. Only a stalled POST-SELECT attempt is a
             * failure -- s_sess.state is M1_SESS_WAIT_AUTH from the moment
             * the session begins (m1_mfc_session_begin()), not only once
             * SELECT completes, so the state alone cannot distinguish "no
             * reader yet" from "reader went silent after SELECT". Gate on
             * s_wu_a_seen (set only once WU_A/WU_A_X has actually been
             * observed) and the elapsed time since then instead. */
            if ((s_wu_a_seen != 0U) &&
                ((uint32_t)(DWT->CYCCNT - attempt_start_cyc) > (M1_RAW_HW_ATTEMPT_US * cpu))) {
                /* Soft re-arm, not session end: a stalled post-SELECT attempt
                 * is recoverable exactly like ordinary field-loss -- the
                 * reader may simply be mid-reactivation (this bound is not
                 * proven to actually fire during that window; dual capture
                 * found "bad-ar" as the confirmed cause of the observed
                 * hang, not this timeout -- but per the product requirement
                 * this exit must not end emulation either, on the same
                 * principle). Resetting s_wu_a_seen here also re-arms this
                 * same watchdog for the next activation (see its use just
                 * above this loop). */
                m1_mfc_raw_hw_soft_rearm(8U);   /* reset_reason=8: attempt-timeout */
                continue;
            }
            continue;
        }

        /* IRQ notified: this task is the sole reader of the chip's own IRQ
         * status registers for the whole session (m1_nfc_raw_read_irq_status(),
         * bypassing RFAL's software sticky word entirely). */
        uint32_t irq = m1_nfc_raw_read_irq_status();
        if (irq == 0U) { continue; }
        s_irq_union |= irq;   /* pure diagnostic, no control-flow effect */
        s_act_irq_union |= irq;   /* per-activation diagnostics only: EVERY bit, not just errors */
        uint32_t rxe_cyc = DWT->CYCCNT;

        /* WAIT_NT_TXE: on TXE, and ONLY on TXE, arm RX and publish the AUTH
         * record only after TXE. One-shot
         * window so a LATER {At}/READ TXE is never misattributed here. Any
         * other IRQ observed while pending (including error bits) is
         * accumulated for diagnostics but does NOT complete the TX -- only
         * TXE does; only that specific IRQ completes transmission. */
        if (s_nt_tx_pending != 0U) {
            uint32_t err_bits = irq & (ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 |
                                       ST25R3916_IRQ_MASK_PAR  | ST25R3916_IRQ_MASK_CRC  |
                                       ST25R3916_IRQ_MASK_NRE  | ST25R3916_IRQ_MASK_COL);
            if (err_bits != 0U) { s_nt_err_irq |= err_bits; }
            if ((irq & ST25R3916_IRQ_MASK_TXE) != 0U) {
                s_nt_txe_seen   = 1U;
                s_nt_txe_cyc    = rxe_cyc;
                s_act_txe_cyc   = rxe_cyc;   /* per-activation diagnostics only */
                s_nt_tx_pending = 0U;
                m1_nfc_raw_own_arm_rx();   /* deferred from wait_auth_rxe() -- see there */
                s_act_arm_rx_calls++;      /* per-activation diagnostics only */
                s_auth_rec.ready = 1U;     /* published only now: TX is confirmed complete */
            }
        }

        if ((irq & ST25R3916_IRQ_MASK_EOF) != 0U) {
            s_field_lost     = 1U;
            s_field_lost_cyc = rxe_cyc;
        }

        /* WU_A/WU_A_X: the chip's own "target now active" signal.
         * One-shot per session. */
        if ((irq & (ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X)) != 0U) {
            if (s_wu_a_seen == 0U) { attempt_start_cyc = rxe_cyc; }
            s_wu_a_seen = 1U;
            if (s_autoresp_off == 0U) {
                (void)st25r3916SetRegisterBits(ST25R3916_REG_PASSIVE_TARGET,
                                               ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
                s_autoresp_off = 1U;
            }
        }

        if ((irq & ST25R3916_IRQ_MASK_RXE) != 0U) {
            switch (s_sess.state) {
            case M1_SESS_WAIT_AUTH: {
                (void)m1_mfc_raw_wait_auth_rxe(rxe_cyc);
                break;
            }
            case M1_SESS_NONCE_SENT: {
                uint16_t n = st25r3916GetNumFIFOBytes();
                uint16_t rn = (n > (uint16_t)sizeof(s_nrar_rec.data)) ? (uint16_t)sizeof(s_nrar_rec.data) : n;
                if (rn > 0U) { (void)st25r3916ReadFifo(s_nrar_rec.data, rn); }
                s_nrar_rec.len     = (uint8_t)rn;
                s_nrar_rec.gen     = s_sess.gen;
                s_nrar_rec.rxe_cyc = rxe_cyc;
                s_nrar_rec.ready   = 1U;
                break;
            }
            case M1_SESS_AUTHED: {
                uint16_t n = st25r3916GetNumFIFOBytes();
                uint16_t rn = (n > (uint16_t)sizeof(s_read_rec.data)) ? (uint16_t)sizeof(s_read_rec.data) : n;
                if (rn > 0U) { (void)st25r3916ReadFifo(s_read_rec.data, rn); }
                s_read_rec.len     = (uint8_t)rn;
                s_read_rec.gen     = s_sess.gen;
                s_read_rec.rxe_cyc = rxe_cyc;
                s_read_rec.ready   = 1U;
                break;
            }
            default:
                break;   /* IDLE / DONE: nothing to capture */
            }
        }

        if (m1_mfc_raw_hw_process_pending()) {
            break;   /* session_end() (STOPPING sequence) ran */
        }
    }

    /* Give STOPPED only now: release, interrupt cleanup, the INACTIVE
     * transition, and EXTI restoration are all complete (m1_mfc_raw_hw_stopping()
     * already ran to completion via every break path above). */
    if (s_stopped_sem != NULL) { (void)xSemaphoreGive(s_stopped_sem); }

    /* Restore normal priority only after complete teardown. */
    vTaskPrioritySet(NULL, saved_priority);
}

void m1_mfc_raw_hw_report(void)
{
    if (!s_report_pending) { return; }
    s_report_pending = false;

    uint32_t cpu = SystemCoreClock / 1000000U; if (cpu == 0U) { cpu = 1U; }
    uint32_t auth_to_nt = (s_last_nt_tx && s_last_auth_rxe) ? (s_last_nt_tx - s_last_auth_rxe) : 0U;
    uint32_t nrar_to_at = (s_last_at_tx && s_last_nrar_rxe) ? (s_last_at_tx - s_last_nrar_rxe) : 0U;
    uint32_t read_to_blk = (s_last_blk_tx && s_last_read_rxe) ? (s_last_blk_tx - s_last_read_rxe) : 0U;

    /* Split across multiple lines: m1_logdb_printf() (m1_log_debug.c) caps a
     * single formatted message at 2*M1_LOGDB_MESSAGE_SIZE = 160 bytes and
     * silently drops the entire line if exceeded (see 52dc41b). */
    platformLog("[RAWHW-END] stage=%u auth_rxe_to_nt_tx_us=%lu nrar_rxe_to_at_tx_us=%lu read_rxe_to_blk_tx_us=%lu\r\n",
                (unsigned)s_last_stage_reached,
                (unsigned long)(auth_to_nt / cpu),
                (unsigned long)(nrar_to_at / cpu),
                (unsigned long)(read_to_blk / cpu));
    platformLog("[RAWHW-WA1] wait_auth_rxe=%lu last_bits=%u last_b0=%02X last_b1=%02X\r\n",
                (unsigned long)s_last_wa_rxe,
                (unsigned)s_last_wa_bits,
                (unsigned)s_last_wa_b0,
                (unsigned)s_last_wa_b1);
    platformLog("[RAWHW-WA2] gate_bad_state=%lu gate_bad_bits=%lu gate_bad_cmd=%lu gate_bad_block=%lu gate_ok=%lu\r\n",
                (unsigned long)s_last_gate_bad_state,
                (unsigned long)s_last_gate_bad_bits,
                (unsigned long)s_last_gate_bad_cmd,
                (unsigned long)s_last_gate_bad_block,
                (unsigned long)s_last_gate_ok);
    platformLog("[RAWHW-WA3] wu_a_seen=%lu autoresp_off=%lu auth_rxe=%lu nt_tx=%lu\r\n",
                (unsigned long)s_last_wu_a_seen,
                (unsigned long)s_last_autoresp_off,
                (unsigned long)s_last_wa_rxe,
                (unsigned long)s_last_nt_tx_count);
    /* Pure diagnostic (see s_irq_union's declaration): every IRQ bit that
     * fired anywhere in the session, not just the named ones above --
     * ST25R3916_IRQ_MASK_* bit positions (st25r3916_irq.h) apply directly. */
    platformLog("[RAWHW-WA4] irq_union=%08lX\r\n", (unsigned long)s_last_irq_union);

    /* Nt-TX completion diagnostics only (read-only trace, this candidate) --
     * distinguishes "TRANSMIT_WITHOUT_CRC issued" (nt_tx, WA3 above) from
     * "the chip actually raised TXE for it" (nt_txe_seen here). Zero effect
     * on control flow or transmission behavior. */
    platformLog("[RAWHW-WA5] nt_tx_req=%lu nt_txe_seen=%lu rxe_to_txreq_us=%lu txreq_to_txe_us=%lu err_irq=%08lX\r\n",
                (unsigned long)s_last_nt_tx_count,
                (unsigned long)s_last_nt_txe_seen,
                (unsigned long)(s_last_rxe_to_txreq_cyc / cpu),
                (unsigned long)(s_last_txreq_to_txe_cyc / cpu),
                (unsigned long)s_last_nt_err_irq);
    platformLog("[RAWHW-WA6] nt=%02X%02X%02X%02X numtx1=%02X numtx2=%02X iso14443a_nfc=%02X\r\n",
                (unsigned)s_last_nt_bytes[0], (unsigned)s_last_nt_bytes[1],
                (unsigned)s_last_nt_bytes[2], (unsigned)s_last_nt_bytes[3],
                (unsigned)s_last_nt_num_tx1, (unsigned)s_last_nt_num_tx2,
                (unsigned)s_last_nt_iso14443a_nfc);
}

#endif /* M1_MFC_RAW_EMULATION */
