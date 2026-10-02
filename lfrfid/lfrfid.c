/* See COPYING.txt for license details. */

/*
 * lfrfid.c
 *
 * 
 */

/*************************** I N C L U D E S **********************************/
#include <stdbool.h>

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "m1_lcd.h"
#include "m1_lp5814.h"
#include "m1_bq27421.h"
#include "m1_power_ctl.h"
#include "m1_bq25896.h"
#include "m1_log_debug.h"
#include "m1_i2c.h"
#include "m1_rf_spi.h"
#include "m1_sdcard.h"
#include "m1_esp32_hal.h"
#include "uiView.h"
#include "privateprofilestring.h"
#include "lfrfid.h"
#include "lfrfid_dma_tx.h"

#define M1_LOGDB_TAG	"RFID"

/*************************** D E F I N E S ************************************/
#define LFRFID_QUEUE_ITEMS_MAX_N		10

#define LFRFID_READ_TIMEOUT_MS   (2500)
//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/
TaskHandle_t	lfrfid_task_hdl;
TaskHandle_t	lfrfid_rx_task_hdl;
QueueHandle_t	lfrfid_q_hdl;
TimerHandle_t 	lfrfid_read_timeout_handle;

LFRFID_TAG_INFO lfrfid_tag_info;
LFRFID_TAG_INFO *lfrfid_tag_info_back;
LFRFIDProgram *lfrfid_program;
volatile lfrfid_state_t lfrfid_state;
uint32_t 	   lfrfid_write_count;

StaticStreamBuffer_t sb_ctrl;
uint8_t sb_storage[LFR_SBUF_BYTES];
StreamBufferHandle_t lfrfid_sb_hdl;
static int lfrfid_lock = 0;

/* Pet Tag scope: when set, lfrfid_read_hw_init() drives the 134.2 kHz carrier
 * (instead of 125 kHz) and the RX decode fan-out runs FDX-B only. Set by the
 * Pet Tag UI before Read start; cleared on exit so normal Read stays 125 kHz. */
uint8_t lfrfid_pettag_mode = 0;

/* Protocol of the most recent T5577 WRITE, used only to bias the immediately
 * following write-verification read-back onto the matching acquisition pass
 * (see the START_READ handler). 0xFF = none pending; consumed (reset to 0xFF)
 * by the read-back that uses it, so an ordinary Read is never affected. */
static uint8_t lfrfid_writeverify_proto = 0xFFu;

/* ------------------------------------------------------------------------
 * Phase 2: consecutive-frame confirmation.
 *
 * A live read is accepted only after N consecutive identical valid decodes
 * (protocol + exact data size + every data byte). The candidate snapshot is
 * owned exclusively by lfrfid_rxThread (the sole writer), so no critical
 * section is needed. Other tasks request a reset via lfrfid_cand_reset_req
 * or, when the RX task is guaranteed idle/suspended, reset it directly.
 * ------------------------------------------------------------------------ */
typedef struct {
	uint8_t  protocol;    /* candidate protocol, PROTOCOL_NO (0xFF) = none */
	uint8_t  data_size;   /* bytes compared and committed */
	uint8_t  count;       /* consecutive identical valid decodes */
	uint16_t bitrate;     /* snapshot for Details/Save/Emulate/Write */
	uint8_t  uid[sizeof(((LFRFID_TAG_INFO*)0)->uid)];
} lfrfid_candidate_t;

static lfrfid_candidate_t lfrfid_cand;
static volatile uint8_t   lfrfid_cand_reset_req = 1;   /* reset before first use */

/* Per-protocol confirmation thresholds. */
static const uint8_t lfrfid_validate_count[LFRFIDProtocolMax] = {
	3,   /* LFRFIDProtocolEM4100    */
	3,   /* LFRFIDProtocolEM4100_32 */
	3,   /* LFRFIDProtocolEM4100_16 */
	3,   /* LFRFIDProtocolH10301    */
	3,   /* LFRFIDProtocolPyramid   */
	3,   /* LFRFIDProtocolIoProxXSF */
	3,   /* LFRFIDProtocolAWID      */
	3,   /* LFRFIDProtocolRadioKey  */
	3,   /* LFRFIDProtocolJablotron */
	3,   /* LFRFIDProtocolFDXB      */
	6,   /* LFRFIDProtocolHIDProx   */
	3,   /* LFRFIDProtocolHIDExt    */
	6,   /* LFRFIDProtocolKeri      */
	6,   /* LFRFIDProtocolNexwatch  */
};

static void lfrfid_candidate_reset(void)
{
	lfrfid_cand.protocol = (uint8_t)PROTOCOL_NO;
	lfrfid_cand.count    = 0;
}

#if defined(LFRFID_PSK_PASS_ENABLED)
/* ------------------------------------------------------------------------
 * PSK acquisition mode.
 *
 * A normal (non-Pet-Tag) Read session alternates between an ASK pass
 * (125kHz/50% duty exciter, ASK/FSK/Manchester protocols only) and a PSK
 * pass (62.5kHz/25% duty exciter, PSK protocols only), using these timing
 * constants:
 *   LFRFID_WORKER_READ_SWITCH_TIME_MS    = 2000  (run a pass this long
 *                                                  before switching, if no
 *                                                  candidate confirms)
 *   LFRFID_WORKER_READ_STABILIZE_TIME_MS =  450  (settle time after
 *                                                  reconfiguring the
 *                                                  exciter, before trusting
 *                                                  decoder state)
 *   LFRFID_WORKER_READ_DROP_TIME_MS      =   50  (brief pause between
 *                                                  stopping the outgoing
 *                                                  pass's exciter and
 *                                                  starting the next)
 *   LFRFID_WORKER_DELAY_QUANT            =   50  (delay is chunked into
 *                                                  this quantum, checking
 *                                                  for a pending Stop
 *                                                  between chunks, so BACK
 *                                                  stays responsive)
 *
 * Pet Tag mode is unaffected: it stays single-pass, ASK-equivalent
 * (134.2kHz/50%, FDX-B only), exactly as before -- the switch timer is
 * simply never armed in that mode.
 * ------------------------------------------------------------------------ */
#define LFRFID_PASS_ASK   (0U)
#define LFRFID_PASS_PSK   (1U)

#define LFRFID_PASS_SWITCH_TIME_MS      (2000U)
#define LFRFID_PASS_STABILIZE_TIME_MS   (450U)
#define LFRFID_PASS_DROP_TIME_MS        (50U)
#define LFRFID_PASS_DELAY_QUANT_MS      (50U)

static uint8_t       lfrfid_active_pass = LFRFID_PASS_ASK;
static TimerHandle_t lfrfid_pass_switch_handle;

/* Non-destructively checks whether a Stop is already queued, without
 * consuming it -- lets a chunked delay bail out early and responsively
 * while leaving the Stop event for the normal xQueueReceive loop to
 * process next using this codebase's queue signaling. */
static bool lfrfid_pass_delay_chunk(uint32_t ms)
{
	S_M1_Main_Q_t peek;
	for(uint32_t waited = 0; waited < ms; waited += LFRFID_PASS_DELAY_QUANT_MS)
	{
		if(xQueuePeek(lfrfid_q_hdl, &peek, pdMS_TO_TICKS(LFRFID_PASS_DELAY_QUANT_MS)) == pdTRUE)
		{
			if(peek.q_evt_type == Q_EVENT_UI_LFRFID_STOP)
				return true; /* caller should abandon the switch and let Stop run */
		}
	}
	return false;
}

/* Starts `pass`'s exciter after tearing down whatever was running, waits the
 * stabilize delay, then resets decoder/candidate state. Order: exciter
 * start -> stabilize delay -> decoders start. Applied identically on the
 * very first pass of a session, not just at switches. Returns true if a Stop
 * was observed during the stabilize
 * wait (caller should abandon the switch and let Stop run). Does NOT
 * touch lfrfid_lock/lfrfid_state -- caller's responsibility, same as the
 * existing hw_init/hw_deinit split. */
static bool lfrfid_pass_start(uint8_t pass)
{
	if(rfid_rxtx_is_taking_this_irq)
	{
		lfrfid_read_hw_deinit();
	}

	if(pass == LFRFID_PASS_PSK)
	{
		lfrfid_hal_set_edge_max_us(LFRFID_HAL_PSK_EDGE_MAX_US);
		lfrfid_read_hw_init(62500, 25);
	}
	else
	{
		lfrfid_hal_set_edge_max_us(LFRFID_HAL_ASK_EDGE_MAX_US);
		lfrfid_read_hw_init(125000, 50);
	}

	lfrfid_active_pass = pass;

	if(lfrfid_pass_delay_chunk(LFRFID_PASS_STABILIZE_TIME_MS))
		return true;

	lfrfid_isr_init();
	lfrfid_decoder_begin();
	lfrfid_tag_info_init();
	lfrfid_cand_reset_req = 1; /* rxThread applies this before touching the candidate */
	return false;
}

static void lfrfid_pass_switch_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_LFRFID_PASS_SWITCH);
}

void lfrfid_pass_switch_timer_create(void)
{
    if(lfrfid_pass_switch_handle != NULL)
        return;

    lfrfid_pass_switch_handle = xTimerCreate(
        "LFRFID_PASS_SW",
        pdMS_TO_TICKS(LFRFID_PASS_SWITCH_TIME_MS),
        pdFALSE,                    /* one-shot; rearmed explicitly each pass */
        (void *)0,
        lfrfid_pass_switch_cb
    );

    configASSERT(lfrfid_pass_switch_handle != NULL);
}

void lfrfid_pass_switch_timer_delete(void)
{
    if(lfrfid_pass_switch_handle == NULL)
        return;

    (void)xTimerStop(lfrfid_pass_switch_handle, 0);
    (void)xTimerDelete(lfrfid_pass_switch_handle, portMAX_DELAY);
    lfrfid_pass_switch_handle = NULL;
}

static void lfrfid_pass_switch_arm(void)
{
    (void)xTimerChangePeriod(lfrfid_pass_switch_handle,
                              pdMS_TO_TICKS(LFRFID_PASS_SWITCH_TIME_MS), 0);
}

static void lfrfid_pass_switch_stop(void)
{
    (void)xTimerStop(lfrfid_pass_switch_handle, 0);
}
#endif /* LFRFID_PSK_PASS_ENABLED */

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void lfrfidThread(void *param);
static void lfrfid_rxThread(void *param);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void lfrfid_stream_init(void)
{
	lfrfid_sb_hdl = xStreamBufferCreateStatic(
        LFR_SBUF_BYTES,
        LFR_TRIGGER_BYTES,
        sb_storage,
        &sb_ctrl
    );
    configASSERT(lfrfid_sb_hdl);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void lfrfid_stream_deinit(void)
{

	if(lfrfid_sb_hdl)
	{
		vStreamBufferDelete(lfrfid_sb_hdl);
		lfrfid_sb_hdl = NULL;
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void lfrfid_read_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;


    m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_READ_TIMEOUT);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_read_timeout_timer_create(void)
{
    if (lfrfid_read_timeout_handle != NULL)
        return;

    lfrfid_read_timeout_handle = xTimerCreate(
        "LFRFID_RD_TO",                    // pcTimerName
        pdMS_TO_TICKS(LFRFID_READ_TIMEOUT_MS), // xTimerPeriodInTicks
        pdFALSE,                           // uxAutoReload: pdFALSE=oneshot
        (void *)0,                         // pvTimerID
        lfrfid_read_timeout_cb             // pxCallbackFunction
    );

    configASSERT(lfrfid_read_timeout_handle != NULL);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_read_timeout_timer_delete(void)
{
    if (lfrfid_read_timeout_handle == NULL)
        return;

    (void)xTimerStop(lfrfid_read_timeout_handle, 0);

    (void)xTimerDelete(lfrfid_read_timeout_handle, portMAX_DELAY);

    lfrfid_read_timeout_handle = NULL;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_read_timeout_start(void)
{
    //(void)xTimerStop(lfrfid_read_timeout_handle, 0);
    (void)xTimerChangePeriod(lfrfid_read_timeout_handle,
                             pdMS_TO_TICKS(LFRFID_READ_TIMEOUT_MS), 0);
    //(void)xTimerStart(lfrfid_read_timeout_handle, 0);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_read_timeout_stop(void)
{
    (void)xTimerStop(lfrfid_read_timeout_handle, 0);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_Init(void)
{
	BaseType_t ret;
	size_t free_heap;

	lfrfid_q_hdl = xQueueCreate(LFRFID_QUEUE_ITEMS_MAX_N, sizeof(S_M1_Main_Q_t));
	assert(lfrfid_q_hdl != NULL);

	ret = xTaskCreate(lfrfidThread, "lfrfid_task_n", M1_TASK_STACK_SIZE_1024, NULL, TASK_PRIORITY_SYS_INIT, &lfrfid_task_hdl);
	assert(ret==pdPASS);
	assert(lfrfid_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	lfrfid_stream_init();

	ret = xTaskCreate(lfrfid_rxThread, "lfrfid_rx_task_n", M1_TASK_STACK_SIZE_4096, NULL, TASK_PRIORITY_SYS_INIT, &lfrfid_rx_task_hdl);
	assert(ret==pdPASS);
	assert(lfrfid_rx_task_hdl!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	lfrfid_read_timeout_timer_create();
#if defined(LFRFID_PSK_PASS_ENABLED)
	lfrfid_pass_switch_timer_create();
#endif

	set_line_buffer_size(512);

	lfrfid_encoded_data.data = malloc(sizeof(Encoded_Data_t)*ENCODED_DATA_MAX);
	assert(lfrfid_encoded_data.data!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	lfrfid_program = malloc(sizeof(LFRFIDProgram));
	assert(lfrfid_program!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	lfrfid_tag_info_back = malloc(sizeof(LFRFID_TAG_INFO));
	assert(lfrfid_tag_info_back!=NULL);
	free_heap = xPortGetFreeHeapSize();
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_DeInit(void)
{
	if(lfrfid_task_hdl)
	{
		vTaskDelete(lfrfid_task_hdl);
		lfrfid_task_hdl = NULL;
	}

	if(lfrfid_q_hdl)
	{
		vQueueDelete(lfrfid_q_hdl);
		lfrfid_q_hdl = NULL;
	}

	if(lfrfid_rx_task_hdl)
	{
		vTaskDelete(lfrfid_rx_task_hdl);
		lfrfid_rx_task_hdl = NULL;
	}

	lfrfid_read_timeout_timer_delete();
#if defined(LFRFID_PSK_PASS_ENABLED)
	lfrfid_pass_switch_timer_delete();
#endif

	lfrfid_stream_deinit();

	safe_free((void**)&lfrfid_encoded_data.data);
	safe_free((void**)&lfrfid_program);
	safe_free((void**)&lfrfid_tag_info_back);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void task_suspend_safe(TaskHandle_t xHandle)
{
#if 1
	TaskStatus_t xTaskDetails;

	vTaskGetInfo(xHandle, &xTaskDetails, pdTRUE, eInvalid);
	if (xTaskDetails.eCurrentState != eSuspended) {
		vTaskSuspend(xHandle);
	}
#endif
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void task_resume_safe(TaskHandle_t xHandle)
{
#if 1
	TaskStatus_t xTaskDetails;

	vTaskGetInfo(xHandle, &xTaskDetails, pdTRUE, eInvalid);
	if (xTaskDetails.eCurrentState == eSuspended) {
		vTaskResume(xHandle);
	}
#endif
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
inline void safe_free(void **pp)
{
    if (pp && *pp) {
        free(*pp);
        *pp = NULL;
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void bytes_to_u32_be(const uint8_t in_data[], uint32_t out_data[], int size)
{
    for (int i = 0; i < size; i++) {
    	out_data[i] =
            ((uint32_t)in_data[i * 4 + 0] << 24) |
            ((uint32_t)in_data[i * 4 + 1] << 16) |
            ((uint32_t)in_data[i * 4 + 2] <<  8) |
            ((uint32_t)in_data[i * 4 + 3] <<  0);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void bytes_to_u32_le(const uint8_t in_data[], uint32_t out_data[], int size)
{
    for (int i = 0; i < size; i++) {
    	out_data[i] =
            ((uint32_t)in_data[i * 4 + 0] <<  0) |
            ((uint32_t)in_data[i * 4 + 1] <<  8) |
            ((uint32_t)in_data[i * 4 + 2] << 16) |
            ((uint32_t)in_data[i * 4 + 3] << 24);
    }
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void bytes_to_u32_array(BitOrder order, const uint8_t in_data[], uint32_t out_data[], int size)
{
	if(order == BIT_ORDER_LSB_FIRST)
		bytes_to_u32_le(in_data,out_data,size);
	else
		bytes_to_u32_be(in_data,out_data,size);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
//lfrfid_evt_t sample_data[120];
void lfrfid_rxThread(void *param)
{
	uint8_t  batch_buf[LFR_TRIGGER_BYTES]; // 10개 분량
	memset(batch_buf,0,sizeof(batch_buf));

	lfrfid_decoder_begin();
	lfrfid_candidate_reset();
	while(1)
	{
        size_t n = xStreamBufferReceive(lfrfid_sb_hdl, batch_buf, sizeof(batch_buf), LFR_RX_TIMEOUT);

        if(lfrfid_lock == 0){
            continue;
        }

        /* Service deferred candidate resets here so this task (the sole
         * writer of the candidate) is the only one to touch it. */
        if(lfrfid_cand_reset_req){
            lfrfid_cand_reset_req = 0;
            lfrfid_candidate_reset();
        }

        if(n == 0){
            continue;   /* gap: no data this cycle; candidate persists */
        }

        uint16_t total_events = n / LFR_ITEM_SIZE;
        uint8_t CHUNK_SIZE = FRAME_CHUNK_SIZE>>1;
        lfrfid_evt_t* p = (lfrfid_evt_t*)batch_buf;
        bool confirmed = false;

        for (int i = 0; i < total_events && !confirmed; i += CHUNK_SIZE)
        {
            uint16_t events_to_process = (total_events - i < CHUNK_SIZE) ? (total_events - i) : CHUNK_SIZE;

            for(int protoIdx=LFRFIDProtocolEM4100; protoIdx<LFRFIDProtocolMax; protoIdx++)
            {
                /* Pet Tag (134.2 kHz) mode decodes FDX-B ONLY. Normal Read
                 * dispatches only the protocols matching the currently
                 * active acquisition pass -- ASK pass feeds ASK/FSK/
                 * Manchester protocols, PSK pass feeds PSK protocols only
                 * according to each protocol's feature flags.
                 * This is what guarantees a PSK candidate can never
                 * accumulate from ASK-pass data or vice versa: a protocol
                 * outside the active pass's feature is never fed a single
                 * event while that pass is running. */
                if(lfrfid_pettag_mode)
                {
                    if(protoIdx != LFRFIDProtocolFDXB)
                        continue;
                }
#if defined(LFRFID_PSK_PASS_ENABLED)
                else
                {
                    /* NULL when a protocol is compile-time-disabled (e.g.
                     * NexWatch in the Candidate-A build) -- skip, same as
                     * every other generic accessor in this file treats an
                     * unregistered slot. */
                    if(lfrfid_protocols[protoIdx] == NULL)
                        continue;

                    bool proto_is_psk = (lfrfid_protocols[protoIdx]->features & LFRFIDFeaturePSK) != 0;
                    bool pass_is_psk  = (lfrfid_active_pass == LFRFID_PASS_PSK);
                    if(proto_is_psk != pass_is_psk)
                        continue;
                }
#else
                else if(lfrfid_protocols[protoIdx] == NULL)
                {
                    /* No PSK pass architecture in this build: dispatch every
                     * registered protocol unconditionally, exactly like the
                     * pre-PSK baseline. Still skip a compile-time-disabled
                     * slot (e.g. NexWatch) -- the registry can be sparse
                     * regardless of whether pass-based filtering exists. */
                    continue;
                }
#endif

             	if(lfrfid_decoder_execute(protoIdx, &p[i], events_to_process))
               	{
                    /* A valid frame was decoded; the decoder wrote its bytes
                     * into the shared lfrfid_tag_info. Confirm across N
                     * consecutive identical valid frames before accepting. */
                    uint16_t ds = protocol_get_data_size(protoIdx);
                    if(ds == 0 || ds > sizeof(lfrfid_tag_info.uid))
                        continue;

                    if(lfrfid_cand.protocol  == (uint8_t)protoIdx &&
                       lfrfid_cand.count      != 0                &&
                       lfrfid_cand.data_size == (uint8_t)ds       &&
                       memcmp(lfrfid_cand.uid, lfrfid_tag_info.uid, ds) == 0)
                    {
                        if(lfrfid_cand.count < 0xFF)
                            lfrfid_cand.count++;                 /* same credential */
                    }
                    else
                    {
                        lfrfid_cand.protocol  = (uint8_t)protoIdx; /* new candidate */
                        lfrfid_cand.data_size = (uint8_t)ds;
                        lfrfid_cand.bitrate   = lfrfid_tag_info.bitrate;
                        memcpy(lfrfid_cand.uid, lfrfid_tag_info.uid, ds);
                        lfrfid_cand.count     = 1;
                    }

                    /* Force the next successful decode to consume a fresh
                     * complete frame (decoders do not self-reset). */
                    lfrfid_decoder_reset(protoIdx);

                    if(lfrfid_cand.count >= lfrfid_validate_count[protoIdx])
                    {
                        /* Commit the confirmed credential FROM THE SNAPSHOT
                         * (other decoders in this loop may overwrite the
                         * shared buffer), then stop all further processing. */
                        lfrfid_tag_info.protocol = lfrfid_cand.protocol;
                        lfrfid_tag_info.bitrate  = lfrfid_cand.bitrate;
                        memcpy(lfrfid_tag_info.uid, lfrfid_cand.uid, lfrfid_cand.data_size);

                        lfrfid_candidate_reset();
                        lfrfid_lock = 0;   /* nothing may overwrite the confirmed state */

                        m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_LFRFID_TAG_DETECTED);

                        confirmed = true;
                        break;             /* stop scanning further decoders */
                    }
               	}
            }
        }

	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfidThread(void *param)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	while(1)
	{
		ret = xQueueReceive(lfrfid_q_hdl, &q_item,
		        lfrfid_state == LFRFID_STATE_EMULATE ? pdMS_TO_TICKS(100) : portMAX_DELAY);
		/* Task-side supervision only; the steady-state DMA waveform is unchanged. */
		if (lfrfid_state == LFRFID_STATE_EMULATE && !lfrfid_dma_tx_check_health())
		{
			rfid_rxtx_is_taking_this_irq = 0;
			lfrfid_state = LFRFID_STATE_ERROR;
			M1_LOG_E(M1_LOGDB_TAG, "LF emulation DMA failed\r\n");
		}
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
			{
				lfrfid_lock = 0;

				lfrfid_read_timeout_stop();

				lfrfid_read_hw_deinit();
				osDelay(10);

				task_suspend_safe(lfrfid_rx_task_hdl);

				m1_app_send_q_message(main_q_hdl, Q_EVENT_LFRFID_TAG_DETECTED);
			} // else if ( q_item.q_evt_type==Q_EVENT_LFRFID_TAG_DETECTED )
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_START_READ)
			{
				if(lfrfid_lock)
					continue;

				task_resume_safe(lfrfid_rx_task_hdl);

				if(lfrfid_pettag_mode)
				{
					/* Pet Tag: single pass, unchanged from before -- FDX-B
					 * (ASK-featured) only, 134.2kHz/50% duty, no ASK<->PSK
					 * switching. */
					lfrfid_isr_init();
					lfrfid_hal_set_edge_max_us(LFRFID_HAL_ASK_EDGE_MAX_US);
					lfrfid_read_hw_init(134200, 50);
#if defined(LFRFID_PSK_PASS_ENABLED)
					lfrfid_active_pass = LFRFID_PASS_ASK;
#endif
					lfrfid_decoder_begin();
					lfrfid_tag_info_init();

					/* Clear confirmation state before a new/retried scan. Safe
					 * without a lock: lfrfid_lock is still 0 here, so lfrfid_rxThread
					 * cannot touch the candidate yet. */
					lfrfid_candidate_reset();
				}
#if defined(LFRFID_PSK_PASS_ENABLED)
				else
				{
					/* Normal Read: start on the ASK pass; the pass-switch
					 * timer alternates to PSK (and back) if nothing
					 * confirms within LFRFID_PASS_SWITCH_TIME_MS. A Stop
					 * arriving during the stabilize wait is left queued;
					 * lfrfid_lock is already about to be set below
					 * regardless, so it is simply processed next.
					 *
					 * Exception -- write-verification read-back of a PSK
					 * protocol (Keri/NexWatch): start directly on the PSK pass.
					 * The ASK-first alternation only reaches PSK at ~2450 ms,
					 * but the read-back times out at 2500 ms and then re-writes,
					 * so a PSK write could never be confirmed (verify was never
					 * reached; the write UI stayed on "WRITING" until the retry
					 * cap). Biasing the starting pass to the just-written
					 * protocol lets the read-back decode/confirm within the
					 * first window. Consumes the flag so an ordinary Read (no
					 * pending write) is unaffected and still starts on ASK. */
					uint8_t start_pass = LFRFID_PASS_ASK;
					if(lfrfid_writeverify_proto < LFRFIDProtocolMax &&
					   lfrfid_protocols[lfrfid_writeverify_proto] &&
					   (lfrfid_protocols[lfrfid_writeverify_proto]->features & LFRFIDFeaturePSK))
						start_pass = LFRFID_PASS_PSK;
					lfrfid_writeverify_proto = 0xFFu;

					(void)lfrfid_pass_start(start_pass);
					lfrfid_pass_switch_arm();
				}
#else
				else
				{
					/* No PSK pass architecture in this build: single ASK
					 * pass only, byte-for-byte the pre-PSK baseline
					 * behavior (same carrier/duty, same call sequence). */
					lfrfid_isr_init();
					lfrfid_hal_set_edge_max_us(LFRFID_HAL_ASK_EDGE_MAX_US);
					lfrfid_read_hw_init(125000, 50);
					lfrfid_decoder_begin();
					lfrfid_tag_info_init();
					lfrfid_candidate_reset();
				}
#endif

				lfrfid_read_timeout_start();

				lfrfid_state = LFRFID_STATE_READ;
				lfrfid_lock = 1;
				// RFID_PULL output LOW (MUTE OFF - DET)
				HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_RESET);
				//READ_STEP = RFIN_PREAMBLE;
				osDelay(50);
			}
#if defined(LFRFID_PSK_PASS_ENABLED)
			else if(q_item.q_evt_type==Q_EVENT_LFRFID_PASS_SWITCH)
			{
				/* Only meaningful mid-Read; a stale/racing timer fire after
				 * Stop or TAG_DETECTED already tore things down is a no-op. */
				if(lfrfid_lock == 0 || lfrfid_state != LFRFID_STATE_READ || lfrfid_pettag_mode)
					continue;

				if(lfrfid_pass_delay_chunk(LFRFID_PASS_DROP_TIME_MS))
					continue; /* Stop is queued; let it run instead of switching */

				/* Re-check: lfrfid_rxThread may have confirmed a candidate
				 * (clearing lfrfid_lock, queuing TAG_DETECTED) during the
				 * drop delay. Don't start reconfiguring for a read that's
				 * already ending -- TAG_DETECTED is next in the queue and
				 * will tear down cleanly regardless, but there's no reason
				 * to touch the exciter first. */
				if(lfrfid_lock == 0)
					continue;

				uint8_t next_pass = (lfrfid_active_pass == LFRFID_PASS_ASK)
				                     ? LFRFID_PASS_PSK : LFRFID_PASS_ASK;

				/* lfrfid_pass_start() tears down the outgoing pass, starts
				 * the incoming pass's exciter, waits the stabilize delay
				 * (bailing early and responsively if Stop arrives), then
                 * resets decoder/candidate state. */
				if(lfrfid_pass_start(next_pass))
					continue;

				/* Same race, checked again after the (much longer)
				 * stabilize wait: only arm the next switch if this read is
				 * still the one running. A candidate confirmed during the
				 * stabilize wait already has TAG_DETECTED queued behind
				 * this event and will tear the new pass's HW down cleanly
				 * via the normal path -- it just never gets fed to a
				 * decoder, so it can never overwrite the confirmed
				 * candidate from the other pass. */
				if(lfrfid_lock != 0)
					lfrfid_pass_switch_arm();
			}
#endif /* LFRFID_PSK_PASS_ENABLED */
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_STOP)
			{
				if(rfid_rxtx_is_taking_this_irq)
				{

					lfrfid_lock = 0;
					lfrfid_state = LFRFID_STATE_IDLE;

					lfrfid_read_timeout_stop();
#if defined(LFRFID_PSK_PASS_ENABLED)
					lfrfid_pass_switch_stop();
#endif
					lfrfid_read_hw_deinit();
					osDelay(10);

					task_suspend_safe(lfrfid_rx_task_hdl);

					/* RX task suspended -> safe to clear the candidate directly. */
					lfrfid_candidate_reset();
				}

			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_EMULATE)
			{
				lfrfid_state = LFRFID_STATE_EMULATE;
				lfrfid_encoder_begin(lfrfid_tag_info.protocol,&lfrfid_tag_info);
				lfrfid_encoder_send(lfrfid_tag_info.protocol,NULL);

				if (!lfrfid_dma_tx_active())
				{
					rfid_rxtx_is_taking_this_irq = 0;
					lfrfid_state = LFRFID_STATE_ERROR;
					M1_LOG_E(M1_LOGDB_TAG, "LF emulation start failed\r\n");
				}

			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_EMULATE_STOP)
			{
				lfrfid_state = LFRFID_STATE_IDLE;
				lfrfid_emul_hw_deinit();

			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_WRITE)
			{
				if(lfrfid_write_count)
				{
					lfrfid_lock = 0;
					lfrfid_state = LFRFID_STATE_IDLE;

					rfid_rxtx_is_taking_this_irq = 1; // dummy
					lfrfid_read_timeout_stop();
					lfrfid_read_hw_deinit();
					osDelay(10);

					task_suspend_safe(lfrfid_rx_task_hdl);
					HAL_GPIO_WritePin(RFID_PULL_GPIO_Port, RFID_PULL_Pin, GPIO_PIN_SET);
					osDelay(52);
				}

				lfrfid_write_count++;
				lfrfid_writeverify_proto = lfrfid_tag_info.protocol; /* bias the write-verify read-back onto this protocol's pass */

				lfrfid_state = LFRFID_STATE_WRITE;
				lfrfid_program->type = LFRFIDProgramTypeT5577;
				lfrfid_write_begin(lfrfid_tag_info.protocol,&lfrfid_tag_info, lfrfid_program);
				lfrfid_write_send(lfrfid_tag_info.protocol, NULL);

				osDelay(1000);
				/* This #if 0 straddles the enclosing if/else-if braces -- with
				 * the block preprocessed out (as it currently is), the code
				 * that actually runs is exactly:
				 *   osDelay(1000); lfrfid_lock = 0;
				 *   m1_app_send_q_message(main_q_hdl, Q_EVENT_UI_LFRFID_WRITE_DONE);
				 * i.e. a synchronous fixed wait for the T5577 write to
				 * physically complete, then a direct notification to the UI
				 * task (main_q_hdl). The disabled code inside was an
				 * abandoned two-phase design: self-repost
				 * Q_EVENT_UI_LFRFID_WRITE_DONE to THIS task's own queue
				 * (lfrfid_q_hdl) and handle it in a separate branch here.
				 * Confirmed dead on both ends -- nothing else in the
				 * codebase ever posts Q_EVENT_UI_LFRFID_WRITE_DONE to
				 * lfrfid_q_hdl, so that separate branch could never run
				 * even if re-enabled. Left exactly as-is (not restructured
				 * into plain if/else-if, not deleted) since this sits in
				 * the LF RFID write completion/event-routing path this
				 * hardening pass is forbidden from altering; this comment
				 * exists only so the brace-straddling #if 0 isn't mistaken
				 * for a mid-edit accident by a future reader. */
#if 0
				m1_app_send_q_message(lfrfid_q_hdl, Q_EVENT_UI_LFRFID_WRITE_DONE);
			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_WRITE_DONE)
			{
#endif
				lfrfid_lock = 0;
				m1_app_send_q_message(main_q_hdl, Q_EVENT_UI_LFRFID_WRITE_DONE);
			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_READ_TIMEOUT)
			{
				//lfrfid_lock = 0;
				/* Read may still be active (RX not suspended here); defer the
				 * candidate reset to lfrfid_rxThread to avoid a cross-task race. */
				lfrfid_cand_reset_req = 1;
				m1_app_send_q_message(main_q_hdl, Q_EVENT_UI_LFRFID_READ_TIMEOUT);
			}
			else if(q_item.q_evt_type==Q_EVENT_UI_LFRFID_WRITE_STOP)
			{
				lfrfid_lock = 0;
				lfrfid_state = LFRFID_STATE_IDLE;

				rfid_rxtx_is_taking_this_irq = 1; // dummy
				lfrfid_read_timeout_stop();
				lfrfid_read_hw_deinit();
				osDelay(10);
				task_suspend_safe(lfrfid_rx_task_hdl);
			}

		} // if (ret==pdTRUE)
	}
}
