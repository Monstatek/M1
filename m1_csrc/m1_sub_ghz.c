/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz.c
*
*  M1 sub-ghz functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_tasks.h"
#include "m1_sub_ghz_api.h"
//#include "m1_sub_ghz.h"
#include "m1_sub_ghz_decenc.h"
#include "subghz_decoder_module.h"
#include "m1_ring_buffer.h"
#include "m1_storage.h"
#include "m1_sdcard_man.h"
#include "m1_sdcard_provision.h"
#include "m1_file_util.h"
#include "uiView.h"
#include "m1_subghz_recordraw_draw.h"
#include "m1_subghz_scan_draw.h"
#include "m1_feedback_orchestration.h"

/*************************** D E F I N E S ************************************/

#define SUBGHZ_RAW_DATA_SAMPLES_MAX			64000 // data type of sample: uint16_t

#define SUBGHZ_TX_RAW_REPLAY_REPEAT_DEFAULT		0 // 2 plus the first transmit before repeating

#define SI4463_nIRQ_EXTI_IRQn         	EXTI12_IRQn

#define NOISE_FLOOR_RSSI_THRESHOLD		-110 //dBm
#define SIGNAL_TO_NOISE_RATIO			20 //dB

/* Record RAW view: shared 100 ms animation/RSSI tick (driven by the existing
 * view message loop - no extra task). Ready monitors RSSI with the radio in
 * plain RX (no RAW capture, no file). Set to 0 to keep Ready fully idle. */
#define SUBGHZ_RECORD_TICK_MS				100
#define SUBGHZ_TX_TICK_MS					20   /* UI refresh + button poll while the Transmitting screen is open */
#define SUBGHZ_TX_CYCLE_MS					450  /* readable left-to-right visual fill per playthrough (UI only) */
#define SUBGHZ_RECORD_READY_MONITOR_RSSI	0	/* READY shows NO graph/RSSI monitor; the graph is a Recording-only view */
/* RSSI threshold line / edge-marker level (same basis as the scanner's SNR). */
#define SUBGHZ_RECORD_RSSI_THRESHOLD_DBM	(NOISE_FLOOR_RSSI_THRESHOLD + SIGNAL_TO_NOISE_RATIO)

#define CHANNEL_STEPS_MAX				256
#define CHANNEL_STEP					(float)0.25 // MHz

#define SUB_GHZ_433_92_NEW_PDTC			0x6C // Attack and decay times of the OOK Peak Detector - Default: 0x28
#define SUB_GHZ_SCAN_OOK_PDTC				0x6C // reverted: PDTC was not the issue (frequency was)

#define M1_LOGDB_TAG				"Sub-GHz"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

#define SUB_GHZ_KEYWORD_DELIMITER	" "
#define SUB_GHZ_KEYWORD_CR			"\r"
#define SUB_GHZ_KEYWORD_LF			"\n"
#define SUB_GHZ_KEYWORD_CRLF		"\r\n"

#define SUB_GHZ_FILEPATH			"/SUBGHZ"
#define SUB_GHZ_FILE_EXTENSION		".sgh"
#define SUB_GHZ_TEMP_FILE_EXTENSION	".tmp"
#define SUB_GHZ_FILE_PREFIX			"sghz_"

#define SUB_GHZ_DATAFILE_KEY_FORMAT_N		9
#define SUB_GHZ_DATAFILE_RAW_FORMAT_N		4
#define SUB_GHZ_DATAFILE_DATA_KEYWORD		"Data:"
#define SUB_GHZ_DATAFILE_FILETYPE_NOISE		"NOISE"
#define SUB_GHZ_DATAFILE_FILETYPE_PACKET	"PACKET"
#define SUB_GHZ_DATAFILE_FILETYPE_KEYWORD	SUB_GHZ_DATAFILE_FILETYPE_NOISE

#define SUB_GHZ_RAW_DATA_PARSER_ERROR_MASK	0x80
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_L1	0x81
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_L2	0x82
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_L3	0x83
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_L4	0x84
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_L5	0x85

#define SUB_GHZ_RAW_DATA_PARSER_STOPPED		0x40
#define SUB_GHZ_RAW_DATA_PARSER_IDLE		0x0A
#define SUB_GHZ_RAW_DATA_PARSER_READY		0x0B
#define SUB_GHZ_RAW_DATA_PARSER_COMPLETE	0x0C


#define SUBGHZ_ISM_BANDS_LIST_NA			3
#define SUBGHZ_ISM_BANDS_LIST_EU			3
#define SUBGHZ_ISM_BANDS_LIST_ASIA			2

#define	SUBGHZ_FCC_BASE_FREQ_300_000			(float)300.00001
#define	SUBGHZ_FCC_BASE_FREQ_310_000			(float)310.00001
#define	SUBGHZ_FCC_BASE_FREQ_315_000			(float)315.00001
#define	SUBGHZ_FCC_BASE_FREQ_345_000			(float)345.00001
#define	SUBGHZ_FCC_BASE_FREQ_372_000			(float)372.00001
#define	SUBGHZ_FCC_BASE_FREQ_390_000			(float)390.00001
#define	SUBGHZ_FCC_BASE_FREQ_433_000			(float)433.00001
#define	SUBGHZ_FCC_BASE_FREQ_433_920			(float)433.92001
#define	SUBGHZ_FCC_BASE_FREQ_434_059			(float)434.05900
#define SUBGHZ_FCC_BASE_FREQ_915_000			(float)915.00001

// Reference: FCC 15.205 Restricted bands of operation
// 322MHz-335.4MHz, 399.9MHz-410MHz
//#define SUBGHZ_FCC_ISM_BAND_304_100				(float)304.10001 // FZ
#define SUBGHZ_FCC_ISM_BAND_300_000				(float)300.00001
#define SUBGHZ_FCC_ISM_BAND_310_000				(float)310.00001
#define SUBGHZ_FCC_ISM_BAND_321_950				(float)321.95001

#define SUBGHZ_FCC_ISM_BAND_344_000				(float)344.00001
#define SUBGHZ_FCC_ISM_BAND_392_000				(float)392.00001

#define SUBGHZ_FCC_ISM_BAND_433_050				(float)433.05001
#define SUBGHZ_FCC_ISM_BAND_434_790				(float)434.79001

#define SUBGHZ_FCC_ISM_BAND_915_000				(float)915.00001
#define SUBGHZ_FCC_ISM_BAND_928_000				(float)928.00001

/* Distinct opmode-setter return: the exact frequency tuned fine but transmit is
 * not permitted in the active ISM region. Kept out of the RADIO_TUNE_* range
 * (0..3, Sub_Ghz/m1_sub_ghz_api.h) so callers can tell a region refusal from a
 * synthesizer tuning failure and show the right message. */
#define RADIO_TX_REGION_BLOCKED					4

//************************** C O N S T A N T **********************************/

const char *subghz_ism_regions_text[SUBGHZ_ISM_BAND_REGIONS_LIST] =
{
	"North America",
	"Europe",
	"Asia"
};

static const char *subghz_modulation_text[SUBGHZ_MODULATION_LIST] =
{
	"OOK",
	"ASK",
	"FSK"
};

static const char *subghz_band_text[] =
{
	"300.000",
	"310.000",
	"315.000",
	"345.000",
	"372.000",
	"390.000",
	"433.920",
	"434.059",
	"915.000"
};

static const char *subghz_datfile_keywords[SUB_GHZ_DATAFILE_KEY_FORMAT_N] =
{
	"Filetype:",
	"Version:",
	"Frequency:",
	"Modulation:",
	"Protocol:",
	"Bits:",
	"Payload:",
	"BT:",
	"IT:"
};

static const float subghz_band_steps[SUB_GHZ_BAND_EOL][2] =
{
	{SUBGHZ_FCC_BASE_FREQ_300_000, 4}, 	// 300.000 - 301.000, step = 250KHz //4
	{SUBGHZ_FCC_BASE_FREQ_310_000, 40}, // 310.000 - 320.000, step = 250KHz //40
	{SUBGHZ_FCC_BASE_FREQ_315_000, 0}, 	// 315.000 - not used // 0
	{SUBGHZ_FCC_BASE_FREQ_345_000, 4},	// 345.000 - 346.000 // 4
	{SUBGHZ_FCC_BASE_FREQ_372_000, 4},	// 372.000 - 373.000 // 4
	{SUBGHZ_FCC_BASE_FREQ_390_000, 4},	// 390.000 - 391.000 // 4
	{SUBGHZ_FCC_BASE_FREQ_433_920, 2},	// index 6 -> 433.920 (reordered)
	{SUBGHZ_FCC_BASE_FREQ_434_059, 3}, 	// index 7 -> 434.059 (reordered)
	{SUBGHZ_FCC_BASE_FREQ_915_000, 4} 	// 915.000 - 916.000 // 4
};

/* ---------------------------------------------------------------------------
 * Record RAW extended frequency list (Phase 14).
 *
 * SOURCE OF TRUTH = exact integer Hz + display label. The legacy band enum
 * (subghz_band_text / subghz_band_steps) is NOT used to derive Record RAW
 * frequencies because its names do not reliably map to real frequencies.
 * Record RAW tunes to ONE fixed frequency from this list via
 * radio_tune_exact_hz() and does not frequency-hop while recording.
 *
 * Every entry is programmable on the RECEIVE/tuning path (validated in
 * software). TX output power / RF matching and regional TX enablement are
 * SEPARATE concerns and remain pending factory (Korea) RF validation; a
 * missing test transmitter does NOT make an entry invalid.
 *
 * 434.059 is the pre-existing hardware-tested entry and is preserved as-is.
 * ------------------------------------------------------------------------- */
typedef struct {
	uint32_t    hz;      /* exact frequency, integer Hz (also written to file) */
	const char *label;   /* display / file-infix label, e.g. "434.059"         */
} SubGhz_RecFreq_t;

static const SubGhz_RecFreq_t subghz_rec_freqs[] =
{
	{ 300000000UL, "300.000" },
	{ 303870000UL, "303.870" },
	{ 304250000UL, "304.250" },
	{ 310000000UL, "310.000" },
	{ 315000000UL, "315.000" },
	{ 318000000UL, "318.000" },
	{ 390000000UL, "390.000" },
	{ 418000000UL, "418.000" },
	{ 433070000UL, "433.070" },
	{ 433420000UL, "433.420" },
	{ 433920000UL, "433.920" },
	{ 434059000UL, "434.059" },   /* preserved hardware-tested entry */
	{ 434420000UL, "434.420" },
	{ 434770000UL, "434.770" },
	{ 438900000UL, "438.900" },
	{ 868350000UL, "868.350" },
	{ 915000000UL, "915.000" },
	{ 925000000UL, "925.000" },
};
#define SUBGHZ_REC_FREQ_COUNT      (sizeof(subghz_rec_freqs)/sizeof(subghz_rec_freqs[0]))
#define SUBGHZ_REC_FREQ_DEFAULT    11   /* default selection = 434.059 (preserved) */

static const float subghz_fcc_ism_bands_NA[SUBGHZ_ISM_BANDS_LIST_NA][2] =
{
	{SUBGHZ_FCC_ISM_BAND_310_000, SUBGHZ_FCC_ISM_BAND_321_950}, 	// 310.00MHz - 321.95MHz
	/*{SUBGHZ_FCC_ISM_BAND_344_000, SUBGHZ_FCC_ISM_BAND_392_000},		// 344.00MHz - 392.00MHz*/
	{SUBGHZ_FCC_ISM_BAND_433_050, SUBGHZ_FCC_ISM_BAND_434_790}, 	// 433.05MHz - 434.79MHz
	{SUBGHZ_FCC_ISM_BAND_915_000, SUBGHZ_FCC_ISM_BAND_928_000}		// 915.00MHz - 928.00MHz
};

static const float subghz_fcc_ism_bands_EU[SUBGHZ_ISM_BANDS_LIST_EU][2] =
{
	{SUBGHZ_FCC_ISM_BAND_310_000, SUBGHZ_FCC_ISM_BAND_321_950}, 	// 310.00MHz - 321.95MHz
	/*{SUBGHZ_FCC_ISM_BAND_344_000, SUBGHZ_FCC_ISM_BAND_392_000},		// 344.00MHz - 392.00MHz*/
	{SUBGHZ_FCC_ISM_BAND_433_050, SUBGHZ_FCC_ISM_BAND_434_790}, 	// 433.05MHz - 434.79MHz
	{SUBGHZ_FCC_ISM_BAND_915_000, SUBGHZ_FCC_ISM_BAND_928_000}		// 915.00MHz - 928.00MHz
};

static const float subghz_fcc_ism_bands_ASIA[SUBGHZ_ISM_BANDS_LIST_ASIA][2] =
{
	{SUBGHZ_FCC_ISM_BAND_310_000, SUBGHZ_FCC_ISM_BAND_321_950}, 	// 310.00MHz - 321.95MHz
	/*{SUBGHZ_FCC_ISM_BAND_344_000, SUBGHZ_FCC_ISM_BAND_392_000},		// 344.00MHz - 392.00MHz*/
	/*{SUBGHZ_FCC_ISM_BAND_433_050, SUBGHZ_FCC_ISM_BAND_434_790}, 	// 433.05MHz - 434.79MHz*/
	{SUBGHZ_FCC_ISM_BAND_915_000, SUBGHZ_FCC_ISM_BAND_928_000}		// 915.00MHz - 928.00MHz
};

//typedef struct S_M1_SUBGHZ_ISM_REGIONS_t;
typedef struct
{
	float (*this_region)[2];
	uint8_t bands_list;
} S_M1_SUBGHZ_ISM_REGIONS_t;

const S_M1_SUBGHZ_ISM_REGIONS_t subghz_regions_list[SUBGHZ_ISM_BAND_REGIONS_LIST] =
{
	{	.this_region = subghz_fcc_ism_bands_NA,
		.bands_list = SUBGHZ_ISM_BANDS_LIST_NA
	},
	{	.this_region = subghz_fcc_ism_bands_EU,
		.bands_list = SUBGHZ_ISM_BANDS_LIST_EU
	},
	{	.this_region = subghz_fcc_ism_bands_ASIA,
		.bands_list = SUBGHZ_ISM_BANDS_LIST_ASIA
	},
};

/*

*/
//************************** S T R U C T U R E S *******************************

typedef enum {
	SUB_GHZ_RECORD_IDLE = 0,
	SUB_GHZ_RECORD_ACTIVE,
	SUB_GHZ_RECORD_STANDBY,
	SUB_GHZ_RECORD_REPLAY,
	SUB_GHZ_RECORD_UNKNOWN
} S_M1_SubGHz_Record_t;

typedef enum {
    VIEW_MODE_SUBGHZ_RECORD_NONE = 0,
    VIEW_MODE_SUBGHZ_RECORD,
    VIEW_MODE_SUBGHZ_RECORD_EOL
} S_M1_SubGHz_View_Mode_Record_t;

typedef enum {
    VIEW_MODE_SUBGHZ_REPLAY_NONE = 0,
    VIEW_MODE_SUBGHZ_REPLAY_BROWSE,
	VIEW_MODE_SUBGHZ_REPLAY_PLAY,
    VIEW_MODE_SUBGHZ_REPLAY_EOL
} S_M1_SubGHz_View_Mode_Replay_t;

typedef enum {
	SUBGHZ_RECORD_DISPLAY_PARAM_READY = 0,
	SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE,
	SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE,
	SUBGHZ_RECORD_DISPLAY_PARAM_PLAY,
	SUBGHZ_RECORD_DISPLAY_PARAM_SAVE,
	SUBGHZ_RECORD_DISPLAY_PARAM_RESET,
	SUBGHZ_RECORD_DISPLAY_PARAM_MEM_ERROR,
	SUBGHZ_RECORD_DISPLAY_PARAM_SDCARD_ERROR,
	SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR
} S_M1_SubGHz_Record_Display_Param_t;

typedef enum {
	SUBGHZ_REPLAY_DISPLAY_PARAM_ACTIVE = 0,
	SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY,
	SUBGHZ_REPLAY_DISPLAY_PARAM_SYS_ERROR
} S_M1_SubGHz_Replay_Display_Param_t;

/***************************** V A R I A B L E S ******************************/

TIM_HandleTypeDef   timerhdl_subghz_tx;
TIM_HandleTypeDef   timerhdl_subghz_rx;
EXTI_HandleTypeDef 	si4463_exti_hdl;
DMA_HandleTypeDef hdma_subghz_tx;

S_M1_RingBuffer subghz_rx_rawdata_rb;
static uint16_t *subghz_front_buffer = NULL;
static uint16_t subghz_front_buffer_size = 0;
static uint8_t *subghz_ring_read_buffer = NULL;
static uint8_t *subghz_sdcard_write_buffer = NULL;
static uint8_t *sdcard_dat_buffer = NULL;
static uint8_t *sdcard_buffer_run_ptr = NULL;
static uint8_t double_buffer_ptr_id = 0;
static uint16_t raw_samples_count;
static uint16_t sdcard_dat_read_size;
static uint16_t raw_samples_buffer_size;
static uint16_t subghz_back_buffer_size;
static uint16_t *subghz_back_buffer = NULL;
static uint16_t *double_buffer_ptr[2];
static uint32_t sdcard_dat_file_size, sdcard_dat_buffer_end_pos;
volatile uint8_t subghz_tx_tc_flag;
uint8_t subghz_record_mode_flag = 0;
volatile uint32_t subghz_capture_overflow = 0;
uint8_t subghz_scan_mode_flag = 0; /* route2: buffered scan capture */
/* Record RAW Ready-state RSSI graph render context + ~100ms tick deadlines.
 * Ready-only GUI (Option C): the v0.8.0.49 capture path (ISR / subghz_rx_rawdata_rb
 * / subghz_record_mode_flag / rx_raw_save) is unchanged, and NO captured/flushed
 * sample counter is introduced here (Recording/Complete keep the base layout). */
static SubGhz_RR_Ctx_t subghz_rr_ctx;
static TickType_t subghz_record_next_tick;
static TickType_t subghz_rssi_disp_next;
static uint8_t subghz_tx_animating; /* 1 while a real RAW replay TX is running (reflects the TX engine state) */
volatile uint32_t g_rr_rd, g_rr_push, g_rr_render; /* 0852 diag: RSSI read attempts / graph pushes / renderer calls */
static uint8_t  subghz_record_frame_hold;      /* GUI: ticks to keep a transient msg on screen */
static uint32_t subghz_record_flushed_samples; /* GUI: running RAW sample count for the record view */
#define SUBGHZ_SCAN_CONFIRM_FRAMES 2 /* require N matching frames before display/beep */
static uint64_t _scan_last_key = ~0ULL;
static uint16_t _scan_last_proto = 0xFFFF, _scan_last_bits = 0xFFFF, _scan_match_cnt = 0;
static uint8_t _scan_disp_pending = 0;
static const char *_scan_disp_name = 0;
static uint64_t _scan_disp_key = 0;
static uint16_t _scan_disp_bits = 0;
static uint8_t _mod_level = SUBGHZ_LEVEL_HIGH; /* drain level tracking for modules */

/* shared confirm+beep+defer for BOTH generic and module results */
static void scan_handle_decode(uint64_t key, uint16_t proto, uint16_t bits, const char *name)
{
    if ( key==_scan_last_key && proto==_scan_last_proto && bits==_scan_last_bits )
    { if ( _scan_match_cnt < 0xFFFF ) _scan_match_cnt++; }
    else
    { _scan_last_key=key; _scan_last_proto=proto; _scan_last_bits=bits; _scan_match_cnt=1; }
    if ( _scan_match_cnt == SUBGHZ_SCAN_CONFIRM_FRAMES )
    {
        m1_buzzer_notification();
        _scan_disp_name=name; _scan_disp_key=key; _scan_disp_bits=bits; _scan_disp_pending=1;
    }
}

/* sub_ghz_display_result defined later (after subghz_scan_config) */
static uint8_t subghz_uiview_gui_latest_param;
static uint8_t subghz_replay_ret_code;
static volatile uint8_t subghz_replay_cancel; /* Stop request checked at TX chunk boundaries */
static uint8_t subghz_replay_mod;
static uint8_t subghz_replay_band, subghz_replay_channel;
/* Record RAW extended-frequency selection + exact-Hz tuning source of truth. */
static uint8_t  subghz_record_freq_idx = SUBGHZ_REC_FREQ_DEFAULT; /* index into subghz_rec_freqs[] */
static uint32_t subghz_replay_hz;   /* exact record/replay frequency in Hz (drives radio_tune_exact_hz) */
/* Transmitting-screen VISUAL cycle (UI only - independent of the exact RF).
 * A playthrough starts one byte-exact RF transmission and one readable ~450 ms
 * left-to-right fill; cycle_active marks the fill in progress, cycle_start is its
 * start tick. The RF may finish (short recording) before the fill completes. */
static uint8_t   subghz_tx_cycle_active;
static TickType_t subghz_tx_cycle_start;
/* 1 during the initial one-time transmission (Play from replay-ready): the HOLD
 * TO REPLAY prompt is hidden and the cycle never repeats. Cleared when that first
 * transmission finishes, entering the replay phase (prompt shown, hold repeats). */
static uint8_t   subghz_tx_first;
/* 1 after the initial transmission until the ORIGINAL Play press is released.
 * While set, HOLD TO REPLAY is shown but a held button does NOT start a replay -
 * this stops the Play press being mistaken for a hold. Cleared on OK release. */
static uint8_t   subghz_tx_wait_release;
/* Menu option: SUBGHz Tx mapped to an external GPIO - TIM8_CH4N */
static uint8_t subghz_tx_on_ext_gpio = 0;
GPIO_TypeDef *subghz_tx_remap_port = SUBGHZ_TX_GPIO_PORT;
uint16_t subghz_tx_remap_pin = SUBGHZ_TX_GPIO_PIN;
static uint8_t subghz_tx_remap_pin_alt = SUBGHZ_GPIO_AF_TX;
static TIM_TypeDef *subghz_tx_remap_timer = SUBGHZ_TX_CARRIER_TIMER;
static uint8_t subghz_tx_remap_dma_req_timer = GPDMA1_REQUEST_TIM1_UP;
static uint8_t subghz_tx_remap_timer_irq = SUBGHZ_TX_TIMER_IRQn;

static S_M1_file_info *f_info = NULL;
static S_M1_SDM_DatFileInfo_t datfile_info;
S_M1_Q_Union_t *subghz_rx_q = NULL;
S_M1_SubGHz_Scan_Config subghz_scan_config =
{
	.band = SUB_GHZ_BAND_300,
	.modulation = MODULATION_OOK
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_sub_ghz_init(void);
void menu_sub_ghz_exit(void);

void sub_ghz_init(void);
void sub_ghz_scan(void);
void sub_ghz_record(void);
void sub_ghz_replay(void);
void sub_ghz_frequency_reader(void);
void sub_ghz_gpio_remap(void);

static void sub_ghz_rx_init(void);
static void sub_ghz_rx_start(void);
static void sub_ghz_rx_pause(void);
static void sub_ghz_rx_deinit(void);
static uint8_t sub_ghz_ring_buffers_init(void);
static void sub_ghz_ring_buffers_deinit(void);
static uint16_t sub_ghz_rx_raw_save(bool header_init, bool last_data);
static void sub_ghz_tx_raw_init(void);
static void sub_ghz_tx_raw_deinit(void);
static void sub_ghz_remap_status_update(uint8_t gpio_remapped);

static void sub_ghz_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power);
static uint8_t sub_ghz_set_opmode_hz(uint8_t opmode, uint32_t hz, uint8_t channel, uint8_t tx_power);
static uint8_t sub_ghz_fcc_ism_band_check_hz(uint32_t hz);
static void subghz_format_freq_mhz(uint32_t hz, char *buf, int buflen);
static void sub_ghz_display_result(const char *name, uint64_t key, uint16_t bits);
static uint8_t sub_ghz_raw_samples_init(void);
static void sub_ghz_raw_samples_deinit(bool discard_samples);
static void sub_ghz_transmit_raw(uint32_t source, uint32_t dest, uint32_t len, uint8_t repeat);
static void sub_ghz_transmit_raw_restart(uint32_t source, uint32_t len);
static uint8_t sub_ghz_raw_replay_init(void);
static void sub_ghz_raw_tx_stop(void);
static uint8_t sub_ghz_replay_start(bool record_mode, S_M1_SubGHz_Band band, uint8_t channel, uint8_t power);
static uint8_t sub_ghz_replay_continue(uint8_t ret_code_in);
static uint8_t sub_ghz_parse_raw_data(uint8_t buffer_ptr_id);
static uint8_t sub_ghz_file_load(void);

static void subghz_record_gui_init(void);
static void subghz_record_gui_create(uint8_t param);
static void subghz_record_gui_destroy(uint8_t param);
static void subghz_record_gui_update(uint8_t param);
static int subghz_record_gui_message(void);
static int subghz_record_kp_handler(void);
static int16_t subghz_record_read_rssi(void);
static void subghz_record_disp_rssi_update(int16_t rssi);
static void subghz_record_enter_ready(void);
static void subghz_record_tick(void);

static void subghz_replay_browse_gui_init(void);
static void subghz_replay_browse_gui_create(uint8_t param);
static void subghz_replay_browse_gui_destroy(uint8_t param);
static void subghz_replay_browse_gui_update(uint8_t param);
static int  subghz_replay_browse_gui_message(void);
static int subghz_replay_browse_kp_handler(void);

static void subghz_replay_play_gui_init(void);
static void subghz_replay_play_gui_create(uint8_t param);
static void subghz_replay_play_gui_destroy(uint8_t param);
static void subghz_replay_play_gui_update(uint8_t param);
static int  subghz_replay_play_gui_message(void);
static int subghz_replay_play_kp_handler(void);

//************************** C O N S T A N T **********************************/

static const view_func_t view_subghz_record_table[] = {
    NULL,               			// Empty
    subghz_record_gui_init,      	// VIEW_MODE_SUBGHZ_RECORD
};

static const view_func_t view_subghz_replay_table[] = {
    NULL,               			// Empty
    subghz_replay_browse_gui_init,  // VIEW_MODE_SUBGHZ_REPLAY_BROWSE
	subghz_replay_play_gui_init,  	// VIEW_MODE_SUBGHZ_REPLAY_PLAY
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/
/*
uint16_t *q = 0;
arrpush(q, 1);
arrpush(q, 2);
for(int i = 0; i < arrlen(q); i++)
    printf("%g\n", q[i]);
arrfree(q);
*/
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_sub_ghz_init(void)
{
	// First radio init with reset (do_reset is true) will load the radio patch SI446X_PATCH_CMDS!
	// Other calls to radio_init_rx_tx() will load new configuration data only!
	radio_init_rx_tx(SUB_GHZ_BAND_300, MODEM_MOD_TYPE_OOK, true);
} // void menu_sub_ghz_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_sub_ghz_exit(void)
{
	// Default IO setting for the SI4463
	HAL_GPIO_WritePin(SI4463_CS_GPIO_Port, SI4463_CS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(SI4463_ENA_GPIO_Port, SI4463_ENA_Pin, GPIO_PIN_RESET);
} // void menu_sub_ghz_exit(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void sub_ghz_init(void)
{
	GPIO_InitTypeDef gpio_init_structure;

	si4463_exti_hdl.Line = EXTI_LINE_12;
	si4463_exti_hdl.RisingCallback = NULL;
	si4463_exti_hdl.FallingCallback = NULL;

	/* Configure Interrupt mode for SI4463_nINT pin */
	gpio_init_structure.Pin = SI4463_nINT_Pin;
	gpio_init_structure.Pull = GPIO_PULLUP;
	gpio_init_structure.Speed = GPIO_SPEED_FREQ_LOW;
	gpio_init_structure.Mode = GPIO_MODE_IT_FALLING;
	HAL_GPIO_Init(SI4463_nINT_GPIO_Port, &gpio_init_structure);

	/* Enable and set SI4463_nINT Interrupt to the low priority */
	HAL_NVIC_SetPriority((IRQn_Type)(SI4463_nIRQ_EXTI_IRQn), configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1, 0);
	HAL_NVIC_EnableIRQ((IRQn_Type)(SI4463_nIRQ_EXTI_IRQn));
} // void sub_ghz_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void sub_ghz_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power)
{
	uint8_t mod_type;
	S_M1_SubGHz_Band freq;
	//struct si446x_reply_PART_INFO_map *pinfo;

	switch(band)
	{
		case SUB_GHZ_BAND_300:
		case SUB_GHZ_BAND_310:
		case SUB_GHZ_BAND_315:
		case SUB_GHZ_BAND_345:
		case SUB_GHZ_BAND_372:
		case SUB_GHZ_BAND_390:
		case SUB_GHZ_BAND_433:
		case SUB_GHZ_BAND_433_92:
			freq = band;
			mod_type = MODEM_MOD_TYPE_OOK;
			break;

		case SUB_GHZ_BAND_915:
			freq = band;
			mod_type = MODEM_MOD_TYPE_FSK;
			break;

		default:
			freq = SUB_GHZ_BAND_300;
			mod_type = MODEM_MOD_TYPE_OOK;
			break;
	} // switch(band)

	radio_init_rx_tx(freq, mod_type, SI446x_Get_Reset_Stat());
	SI446x_Select_Frontend(freq);

	//pinfo = SI446x_PartInfo();
	//M1_LOG_I(M1_LOGDB_TAG, "Init done.\r\nPart %d Rev. %d Rom ID %d\r\n", pinfo->PART, pinfo->CHIPREV, pinfo->ROMID);
	M1_LOG_D(M1_LOGDB_TAG, "Rx_Tx mode %d band %d channel %d\r\n", opmode, freq, channel);

	switch (opmode)
	{
		case SUB_GHZ_OPMODE_RX:
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_RX);
			// Put the radio in Rx mode
			SI446x_Start_Rx(channel);
			break;

		case SUB_GHZ_OPMODE_TX:
			/* Defensive regional TX-permission check for the legacy band-preset
			 * TX path. No shipped path transmits through this variant (replay
			 * uses the exact-Hz setter), but if one ever does it must honor the
			 * same region policy as the common boundary. Map the band preset to
			 * its nominal frequency and refuse disallowed-region TX before the
			 * antenna is switched to TX / Radio_Start_Tx(). */
			{
				uint32_t nominal_hz;
				switch (band)
				{
					case SUB_GHZ_BAND_300:    nominal_hz = 300000000UL; break;
					case SUB_GHZ_BAND_310:    nominal_hz = 310000000UL; break;
					case SUB_GHZ_BAND_315:    nominal_hz = 315000000UL; break;
					case SUB_GHZ_BAND_345:    nominal_hz = 345000000UL; break;
					case SUB_GHZ_BAND_372:    nominal_hz = 372000000UL; break;
					case SUB_GHZ_BAND_390:    nominal_hz = 390000000UL; break;
					case SUB_GHZ_BAND_433:    nominal_hz = 433000000UL; break;
					case SUB_GHZ_BAND_433_92: nominal_hz = 433920000UL; break;
					case SUB_GHZ_BAND_915:    nominal_hz = 915000000UL; break;
					default:                  nominal_hz = 0UL;         break;
				}
				if ( sub_ghz_fcc_ism_band_check_hz(nominal_hz) )
				{
					return;
				}
			}
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
			// Read INTs, clear pending ones
			SI446x_Get_IntStatus(0, 0, 0);
			// Direct mode asynchronous mode, TX direct mode on GPIO2,  modulation is sourced in real-time, OOK
			// Mode: TX_DIRECT_MODE_TYPE[7]	TX_DIRECT_MODE_GPIO[6:5]	MOD_SOURCE[4:3]	MOD_TYPE[2:0]
			//					1					10						01				000
			SI446x_Change_ModType(0xC8 | mod_type);
			// Read INTs, clear pending ones
			SI446x_Get_IntStatus(0, 0, 0);
			/* Start sending packet, channel 0, START immediately */
			Radio_Start_Tx(channel, START_TX_COMPLETE_STATE_NOCHANGE, 0); // Do not change state after completion of the packet transmission
			//SI446x_Start_Tx_CW(uint8_t channel);
			SI446x_Set_Tx_Power(tx_power);
			break;

		default: // SUB_GHZ_OPMODE_ISOLATED
			// Put the radio in sleep mode
			SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SLEEP);
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_ISOLATED);
			break;
	} // switch (opmode)

} // static void sub_ghz_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power)



/*============================================================================*/
/**
  * @brief  Set the radio operating mode at an EXACT frequency (integer Hz).
  *         Parity with sub_ghz_set_opmode() but tunes via radio_tune_exact_hz()
  *         instead of a legacy band preset. Used by the Record RAW extended
  *         frequency list and by replay (record + file). No silent fallback:
  *         a tuning failure is returned so the caller can report it explicitly.
  * @retval RADIO_TUNE_OK, or a RADIO_TUNE_ERR_* code.
  */
/*============================================================================*/
static uint8_t sub_ghz_set_opmode_hz(uint8_t opmode, uint32_t hz, uint8_t channel, uint8_t tx_power)
{
	uint8_t mod_type;
	uint8_t tune;

	/* Modulation follows the base band (parity with sub_ghz_set_opmode:
	 * the 900 MHz band uses FSK, all lower bands use OOK). */
	mod_type = ( hz >= 705000000UL ) ? MODEM_MOD_TYPE_FSK : MODEM_MOD_TYPE_OOK;

	/* Program the synthesizer to the EXACT frequency. A non-zero result is an
	 * explicit tuning failure (out of range / calc reject / apply fault) — the
	 * caller surfaces it rather than substituting a nearby frequency. */
	tune = radio_tune_exact_hz(hz, mod_type, SI446x_Get_Reset_Stat());
	if ( tune != RADIO_TUNE_OK )
	{
		M1_LOG_E(M1_LOGDB_TAG, "TUNE FAIL hz=%lu code=%d\r\n", (unsigned long)hz, tune);
		return tune;
	}

	M1_LOG_D(M1_LOGDB_TAG, "Rx_Tx(hz) mode %d hz %lu channel %d\r\n", opmode, (unsigned long)hz, channel);

	switch (opmode)
	{
		case SUB_GHZ_OPMODE_RX:
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_RX);
			SI446x_Start_Rx(channel);
			break;

		case SUB_GHZ_OPMODE_TX:
			/* Central regional TX-permission boundary. Hardware support for a
			 * frequency is SEPARATE from permission to transmit on it: the synth
			 * tuned above, but if the region policy disallows TX here we refuse
			 * before the antenna is switched to TX and before Radio_Start_Tx(),
			 * so the SI4463 never enters TX. Every OPMODE_TX request funnels
			 * through this single check. */
			if ( sub_ghz_fcc_ism_band_check_hz(hz) )
			{
				return RADIO_TX_REGION_BLOCKED;
			}
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
			SI446x_Get_IntStatus(0, 0, 0);
			SI446x_Change_ModType(0xC8 | mod_type);
			SI446x_Get_IntStatus(0, 0, 0);
			Radio_Start_Tx(channel, START_TX_COMPLETE_STATE_NOCHANGE, 0);
			SI446x_Set_Tx_Power(tx_power);
			break;

		default: // SUB_GHZ_OPMODE_ISOLATED
			SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_SLEEP);
			radio_set_antenna_mode(RADIO_ANTENNA_MODE_ISOLATED);
			break;
	} // switch (opmode)

	return RADIO_TUNE_OK;
} // static uint8_t sub_ghz_set_opmode_hz(uint8_t opmode, uint32_t hz, uint8_t channel, uint8_t tx_power)



/*============================================================================*/
/**
  * @brief  Regional TX-policy check by EXACT frequency (Hz). SEPARATE from RX
  *         tuning capability: a frequency can be a valid receive target yet be
  *         disallowed for transmit in the configured ISM region. Gates TX
  *         (replay) only; the record/RX path never calls this.
  * @retval 0 = allowed for TX in the current region, 1 = not allowed.
  */
/*============================================================================*/
static uint8_t sub_ghz_fcc_ism_band_check_hz(uint32_t hz)
{
	float freq;
	uint8_t i, ret;

	/* Fail conservatively: an unprovisioned or out-of-range region selector
	 * must never permit transmit. Without this guard an invalid region value
	 * would index subghz_regions_list[] out of bounds and could wrongly report
	 * a frequency as permitted, allowing unrestricted TX on corrupt/missing
	 * regional configuration. */
	if ( m1_device_stat.config.ism_band_region >= SUBGHZ_ISM_BAND_REGIONS_LIST )
	{
		return 1; /* not allowed */
	}

	ret = 1;
	freq = (float)hz / 1000000.0f; // MHz
	for (i=0; i<subghz_regions_list[m1_device_stat.config.ism_band_region].bands_list; i++)
	{
		if ( (freq >= subghz_regions_list[m1_device_stat.config.ism_band_region].this_region[i][0]) &&
				(freq <= subghz_regions_list[m1_device_stat.config.ism_band_region].this_region[i][1]) )
		{
			ret = 0;
			break;
		}
	} // for (i=0; ...)

	return ret;
} // static uint8_t sub_ghz_fcc_ism_band_check_hz(uint32_t hz)



/*============================================================================*/
/**
  * @brief  Format an exact integer-Hz frequency as a readable MHz label, e.g.
  *         434059000 -> "434.059", 433920000 -> "433.92", 915000000 -> "915.00".
  *         3 decimals with trailing zeros trimmed down to a minimum of 2, so the
  *         real selected frequency is preserved and never rounded into another.
  *         The internal integer-Hz value is never shown.
  */
/*============================================================================*/
static void subghz_format_freq_mhz(uint32_t hz, char *buf, int buflen)
{
	uint32_t mhz = hz / 1000000UL;
	uint32_t frac = (hz % 1000000UL) / 1000UL; // milli-MHz (0..999), i.e. 3 decimals

	// Trim the 3rd decimal only when it is zero (keep >= 2 decimals).
	if ( (frac % 10UL) == 0UL )
		snprintf(buf, buflen, "%lu.%02lu", (unsigned long)mhz, (unsigned long)(frac / 10UL));
	else
		snprintf(buf, buflen, "%lu.%03lu", (unsigned long)mhz, (unsigned long)frac);
} // static void subghz_format_freq_mhz(uint32_t hz, char *buf, int buflen)



/* True while the center/OK button is physically held (polled - there is no
 * release event on the queue). PRESS/LPRESS = held; anything else = released. */
static uint8_t subghz_ok_held(void)
{
	uint8_t s = buttons_ctl[BUTTON_OK_KP_ID].status;
	return ( s==BUTTON_IS_PRESSED || s==BUTTON_IS_LPRESSED ) ? 1 : 0;
} // static uint8_t subghz_ok_held(void)



/* Reset the visual progress box to EMPTY (0%). UI only; does not touch the TX. */
static void subghz_tx_ui_reset(void)
{
	subghz_rr_ctx.tx_pos    = 0;
	subghz_rr_ctx.tx_total  = SUBGHZ_TX_CYCLE_MS;   /* visual denominator (ms) */
	subghz_rr_ctx.tx_done   = 0;
} // static void subghz_tx_ui_reset(void)



/* Begin one readable left-to-right visual fill cycle (UI only). Called right
 * after an RF transmission is started; the fill lasts SUBGHZ_TX_CYCLE_MS
 * regardless of how quickly the exact RF finishes. */
static void subghz_tx_cycle_begin(void)
{
	subghz_rr_ctx.tx_done    = 0;
	subghz_rr_ctx.tx_pos     = 0;                   /* empty */
	subghz_rr_ctx.tx_total   = SUBGHZ_TX_CYCLE_MS;
	subghz_tx_cycle_active   = 1;
	subghz_tx_cycle_start    = xTaskGetTickCount();
} // static void subghz_tx_cycle_begin(void)



/* Enter the completed/idle Transmitting screen: box EMPTY, "SENDING..." kept,
 * hold-to-repeat hint shown. The screen STAYS until the user holds OK or Backs. */
static void subghz_tx_enter_done(void)
{
	subghz_tx_animating     = 0;
	subghz_tx_cycle_active  = 0;
	subghz_rr_ctx.tx_done   = 1;
	subghz_rr_ctx.tx_pos    = 0;                    /* box reset to empty */
	subghz_rr_ctx.tx_total  = SUBGHZ_TX_CYCLE_MS;
	fb_net_replay_stop(); // migrated: Sub-GHz TX cycle done (subghz_tx_enter_done)
} // static void subghz_tx_enter_done(void)



/* Safe-stop the active playthrough using the existing cancellation path, without
 * leaving the radio transmitting. Leaves ret_code = IDLE. Does not change screen. */
static void subghz_tx_safe_stop(void)
{
	subghz_replay_cancel = 1;
	subghz_decenc_ctl.ntx_raw_repeat = 0;
	sub_ghz_raw_tx_stop();
	sub_ghz_raw_samples_deinit(false);
	sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
	subghz_replay_ret_code = SUB_GHZ_RAW_DATA_PARSER_IDLE;
	subghz_tx_animating = 0;
	subghz_tx_cycle_active = 0;
	xQueueReset(main_q_hdl);
	subghz_replay_cancel = 0;
} // static void subghz_tx_safe_stop(void)



/* Standard regional TX-refusal notice for the 128x64 display, shown when a
 * transmit is attempted on a frequency not permitted in the active ISM region.
 * Receive/record/saved-signal inspection are unaffected. */
static void subghz_tx_region_blocked_msg(void)
{
	/* Custom-drawn regional TX-refusal notice: bold title + divider rule, two
	 * centered message lines, and a filled "BACK to return" button. Draw-only;
	 * the BACK-key dismiss reuses the shared m1_wait_back_to_exit() wait loop,
	 * matching m1_message_box() semantics. No RF/timing/protocol path touched. */
	u8g2_uint_t w, x;

	u8g2_ClearBuffer(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Title (bold), centered */
	u8g2_SetFont(&m1_u8g2, u8g2_font_helvB08_tf);
	w = u8g2_GetUTF8Width(&m1_u8g2, "TX Restricted");
	u8g2_DrawStr(&m1_u8g2, (128 - w) / 2, 11, "TX Restricted");

	/* Divider rule under the title */
	u8g2_DrawHLine(&m1_u8g2, 6, 15, 116);

	/* Two message lines, centered */
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	w = u8g2_GetUTF8Width(&m1_u8g2, "Not available");
	u8g2_DrawStr(&m1_u8g2, (128 - w) / 2, 31, "Not available");
	w = u8g2_GetUTF8Width(&m1_u8g2, "in this region");
	u8g2_DrawStr(&m1_u8g2, (128 - w) / 2, 42, "in this region");

	/* Filled "BACK to return" button, centered */
	w = u8g2_GetUTF8Width(&m1_u8g2, "BACK to return");
	x = (128 - (w + 8)) / 2;
	u8g2_DrawRBox(&m1_u8g2, x, 50, w + 8, 13, 2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawStr(&m1_u8g2, x + 4, 60, "BACK to return");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	m1_u8g2_nextpage();

	m1_wait_back_to_exit();
} // static void subghz_tx_region_blocked_msg(void)

/* Start ONE byte-exact RF transmission of the loaded RAW recording from the
 * beginning (raw_replay_init re-opens the file at offset 0), at the RF engine's
 * own real timing. Does NOT touch the visual bar. Returns 1 on success. */
static uint8_t subghz_tx_start_rf(void)
{
	/* Regional TX-permission is enforced at the common OPMODE_TX boundary; a
	 * refusal here means this RAW-replay frequency is not transmittable in the
	 * active region. Report it and leave the radio out of TX. */
	if ( sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_TX, subghz_replay_hz, 0, 255) == RADIO_TX_REGION_BLOCKED )
	{
		subghz_tx_region_blocked_msg();
		sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
		return 0;
	}
	subghz_replay_ret_code = sub_ghz_raw_replay_init();
	if ( subghz_replay_ret_code != 1 )
	{
		double_buffer_ptr_id = 1;
		subghz_decenc_ctl.ntx_raw_repeat = SUBGHZ_TX_RAW_REPLAY_REPEAT_DEFAULT; /* one emission */
		subghz_tx_animating = 1;
		fb_net_replay_start(); // migrated: Sub-GHz TX cycle start (subghz_tx_start_rf)
		return 1;
	}
	/* init failed -> stop cleanly */
	sub_ghz_raw_tx_stop();
	sub_ghz_raw_samples_deinit(false);
	sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
	subghz_tx_animating = 0;
	return 0;
} // static uint8_t subghz_tx_start_rf(void)



/* Start a replay AND (re)start the visual fill cycle from empty. Used when a new
 * replay run begins (initial Play, or a hold from the idle screen). */
static uint8_t subghz_tx_start_playthrough(void)
{
	if ( !subghz_tx_start_rf() )
	{
		subghz_tx_cycle_active = 0;
		return 0;
	}
	subghz_tx_cycle_begin();                              /* visual: empty -> fill (loops) */
	return 1;
} // static uint8_t subghz_tx_start_playthrough(void)



/* Called from the message loop when a real RF transmission finishes
 * (Q_EVENT_SUBGHZ_TX). The engine cleans up; then, in the replay phase and while
 * OK is still held, the NEXT RF replay starts immediately at its real timing -
 * back-to-back, independent of the ~450 ms visual bar (which keeps looping). */
static void subghz_tx_on_rf_end(void)
{
	subghz_replay_ret_code = sub_ghz_replay_continue(subghz_replay_ret_code);
	subghz_tx_animating = 0;
	if ( subghz_replay_ret_code == SUB_GHZ_RAW_DATA_PARSER_IDLE &&
			subghz_tx_cycle_active && !subghz_tx_first && subghz_ok_held() )
	{
		subghz_tx_start_rf();     /* next replay immediately; visual is untouched */
	}
} // static void subghz_tx_on_rf_end(void)



/* Periodic (~20 ms) control + redraw while the Transmitting screen is open. Drives
 * the readable visual fill (independent of the exact RF, which finishes on its own
 * Q_EVENT_SUBGHZ_TX) and the hold-to-repeat cadence:
 *   empty -> fill L->R over SUBGHZ_TX_CYCLE_MS -> full -> reset ->
 *            (OK held & repeating) next RF + next cycle,  else -> completed/idle.
 * The initial Play is a single emission: it is not "repeating", so at cycle end it
 * settles on the completed screen; a hold there starts the repeat sequence. */
static void subghz_tx_poll(uint8_t play_param)
{
	uint8_t held = subghz_ok_held();
	TickType_t now = xTaskGetTickCount();
	uint32_t elapsed;

	subghz_rr_ctx.tx_held = held;
	subghz_rr_ctx.tx_show_hint = subghz_tx_first ? 0 : 1;   /* prompt only in replay phase */

	if ( subghz_tx_cycle_active )
	{
		/* Replay phase only: releasing OK stops replay and settles to the idle
		 * screen. The INITIAL one-time transmission ignores release and always
		 * completes (one normal transmission) before offering HOLD TO REPLAY.
		 * NOTE: the RF cadence is driven entirely by subghz_tx_on_rf_end(); this
		 * poll only animates the bar and handles release / the initial handoff. */
		if ( !subghz_tx_first && !held )
		{
			if ( subghz_replay_ret_code != SUB_GHZ_RAW_DATA_PARSER_IDLE )
				subghz_tx_safe_stop();                    /* cancel active replay safely */
			subghz_tx_enter_done();
			m1_uiView_display_update(play_param);
			return;
		}

		elapsed = (uint32_t)(now - subghz_tx_cycle_start);
		if ( elapsed >= SUBGHZ_TX_CYCLE_MS )
		{
			if ( subghz_tx_first && subghz_replay_ret_code == SUB_GHZ_RAW_DATA_PARSER_IDLE )
			{
				/* Initial one-time transmission finished (and its readable bar
				 * cycle elapsed) -> offer HOLD TO REPLAY. Arm hold-to-replay ONLY
				 * after the original Play press is released (WAIT_FOR_OK_RELEASE),
				 * so that press can't be mistaken for a repeat hold. */
				subghz_tx_first = 0;
				subghz_tx_wait_release = 1;
				subghz_tx_enter_done();
				m1_uiView_display_update(play_param);
				return;
			}
			/* Free-running loop: bar reached full -> reset and keep animating.
			 * This is decorative only; it never starts/stops an RF transmission. */
			subghz_tx_cycle_start = now;
			elapsed = 0;
		}
		subghz_rr_ctx.tx_pos   = elapsed;
		subghz_rr_ctx.tx_total = SUBGHZ_TX_CYCLE_MS;
		m1_uiView_display_update(play_param);
	}
	else if ( subghz_rr_ctx.tx_done )
	{
		if ( subghz_tx_wait_release )
		{
			/* WAIT_FOR_OK_RELEASE: hold-to-replay stays disarmed until the button
			 * (the original Play press) is fully released. */
			if ( !held )
			{
				subghz_tx_wait_release = 0;                /* now armed */
				m1_uiView_display_update(play_param);
			}
		}
		else if ( held )
		{
			/* Armed HOLD TO REPLAY: a fresh hold starts replay immediately. */
			if ( !subghz_tx_start_playthrough() )
				subghz_tx_enter_done();
			m1_uiView_display_update(play_param);
		}
	}
} // static void subghz_tx_poll(uint8_t play_param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void sub_ghz_scan(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	SubGHz_Dec_Info_t decoded_data = {};
	uint8_t info_updated = 0;

	subghz_decenc_init(); // Init() must be in this order!
	menu_sub_ghz_init();
	subghz_scan_diag_reset(); /* route2: reset diagnostic counters */
	if ( sub_ghz_ring_buffers_init() != 0 ) /* route2: allocate BEFORE arming RX */
	{
		M1_LOG_E(M1_LOGDB_TAG, "SCAN ring buffer alloc FAILED\r\n");
	}
	m1_ringbuffer_reset(&subghz_rx_rawdata_rb);
	subghz_capture_overflow = 0;
	sub_ghz_set_opmode(SUB_GHZ_OPMODE_RX, subghz_scan_config.band, 0, 0);
	SI446x_Change_Modem_OOK_PDTC(SUB_GHZ_SCAN_OOK_PDTC); // OOK tuning in scan (was record-only)
	subghz_decenc_ctl.pulse_det_stat = PULSE_DET_ACTIVE; /* match RAW Record arming (was IDLE) */
	sub_ghz_rx_init();
	subghz_modules_init(); subghz_modules_reset(); _mod_level = SUBGHZ_LEVEL_HIGH; /* module decoders */
	M1_LOG_E(M1_LOGDB_TAG, "SCAN band=%s mod=%d\r\n", subghz_band_text[subghz_scan_config.band], subghz_scan_config.modulation);
	subghz_scan_mode_flag = 1; /* route2: enable ISR store (buffer ready) before RX starts */
	sub_ghz_rx_start();
	fb_net_detect_start(); // migrated: Sub-GHz scan entry (sub_ghz_scan)
	// Active Scan screen: RF lock-on target + SCANNING... + freq/mod + Stop bar.
	subghz_scan_draw(&m1_u8g2, subghz_band_text[subghz_scan_config.band],
			subghz_modulation_text[subghz_scan_config.modulation]);
	m1_u8g2_nextpage();

    xQueueReset(main_q_hdl); // Reset main q before start
    while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(30)); /* route2: drain buffer regularly */

		if (subghz_capture_overflow)
		{
			/* Dropped edges invalidate decoder continuity, not just one sample. */
			sub_ghz_rx_pause();
			m1_ringbuffer_reset(&subghz_rx_rawdata_rb);
			subghz_decenc_ctl.npulsecount = 0;
			subghz_decenc_ctl.subghz_reset_data();
			subghz_modules_reset();
			_mod_level = SUBGHZ_LEVEL_HIGH;
			_scan_match_cnt = 0;
			subghz_capture_overflow = 0;
			sub_ghz_rx_start();
		}

		/* Route 2: decode from the raw ring buffer (robust) instead of the per-edge queue */
		{
			uint32_t _avail = ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
			while ( _avail > 0 )
			{
				uint16_t _n = (_avail > SUBGHZ_RAW_DATA_SAMPLES_TO_RW) ? SUBGHZ_RAW_DATA_SAMPLES_TO_RW : (uint16_t)_avail;
				uint16_t _k; uint16_t *_pd;
				_n = m1_ringbuffer_read(&subghz_rx_rawdata_rb, subghz_ring_read_buffer, _n);
				if (_n == 0) break;
				subghz_diag_drained += _n;
				_pd = (uint16_t *)subghz_ring_read_buffer;
				for ( _k = 0; _k < _n; _k++ )
				{
					subghz_diag_submitted++;
					/* generic path: Princeton, CAME, CAME12 */
					subghz_decenc_ctl.subghz_pulse_handler(_pd[_k]);
					if ( subghz_decenc_read(&decoded_data, false) ) {
						M1_LOG_E(M1_LOGDB_TAG, "READ %s key=0x%lX bits=%d te=%d guard=%u rssi=%d\r\n", subghz_protocol_name(decoded_data.protocol), (unsigned long)decoded_data.key, decoded_data.bit_len, decoded_data.te, subghz_seg_guard, (int)decoded_data.rssi);
						scan_handle_decode(decoded_data.key, decoded_data.protocol, decoded_data.bit_len, subghz_protocol_name(decoded_data.protocol)); }
					/* module path: Linear (own state machine, own guard) */
					subghz_modules_feed(_mod_level, _pd[_k]);
					{ SubGHz_Result_t _mr; uint16_t _mi = subghz_modules_poll(&_mr);
					  if ( _mi ) scan_handle_decode(_mr.key, (uint16_t)(0x80 | (_mi-1)), _mr.bit_len, _mr.name); }
					if ( _pd[_k] >= 4000 ) _mod_level = SUBGHZ_LEVEL_HIGH; else _mod_level = !_mod_level;
				}
				_avail -= _n;
			}
		}

		/* diag log removed (clean build) */

		if ( _scan_disp_pending ) { sub_ghz_display_result(_scan_disp_name, _scan_disp_key, _scan_disp_bits); _scan_disp_pending = 0; }


		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_RX )
			{
				; /* pulses drained from ring buffer above */
			} // if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_RX )

			else if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK      // BACK = exit
					|| this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )    // CENTER = on-screen "Stop"
				{
					fb_net_detect_stop(); // migrated: Sub-GHz scan Back/Stop exit
					; // Do extra tasks here if needed
					subghz_scan_mode_flag = 0; /* route2 */
					sub_ghz_rx_deinit();
					sub_ghz_ring_buffers_deinit(); /* route2 */
					sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_300, 0, 0);
					menu_sub_ghz_exit();

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( subghz_scan_config.band < SUB_GHZ_BAND_915 )
					{
						subghz_scan_config.band++;
						if ( subghz_scan_config.band==SUB_GHZ_BAND_915 )
							subghz_scan_config.modulation = MODULATION_FSK;
						else
							subghz_scan_config.modulation = MODULATION_OOK;
					}
					else
					{
						subghz_scan_config.band = SUB_GHZ_BAND_300;
						subghz_scan_config.modulation = MODULATION_OOK;
					}
					info_updated = 1;
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( subghz_scan_config.band > SUB_GHZ_BAND_300 )
					{
						subghz_scan_config.band--;
						subghz_scan_config.modulation = MODULATION_OOK;
					}
					else
					{
						subghz_scan_config.band = SUB_GHZ_BAND_915;
						subghz_scan_config.modulation = MODULATION_FSK;
					}
					info_updated = 1;
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				if ( info_updated )
				{
					subghz_scan_mode_flag = 0; /* pause capture during reconfig */
					m1_ringbuffer_reset(&subghz_rx_rawdata_rb);
					subghz_decenc_ctl.npulsecount = 0;
					subghz_decenc_ctl.subghz_reset_data();
					_scan_match_cnt=0; _scan_last_key=~0ULL; _scan_last_proto=0xFFFF; _scan_last_bits=0xFFFF;
					sub_ghz_set_opmode(SUB_GHZ_OPMODE_RX, subghz_scan_config.band, 0, 0);
					// Adjust the attack and decay times
					SI446x_Change_Modem_OOK_PDTC(SUB_GHZ_SCAN_OOK_PDTC); // enabled
					subghz_decenc_ctl.pulse_det_stat = PULSE_DET_ACTIVE; /* match RAW Record arming (was IDLE) */
					subghz_modules_reset(); _mod_level = SUBGHZ_LEVEL_HIGH;
					subghz_scan_mode_flag = 1; /* resume capture on new band */
					subghz_scan_draw(&m1_u8g2, subghz_band_text[subghz_scan_config.band],
							subghz_modulation_text[subghz_scan_config.modulation]);
					m1_u8g2_nextpage(); // Update display
					info_updated = 0; // Reset
				} // if ( info_updated )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void sub_ghz_scan(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void sub_ghz_record(void)
{
	bool sys_ready;
	uint8_t param = SUBGHZ_RECORD_DISPLAY_PARAM_READY;

	sys_ready = !sub_ghz_ring_buffers_init();
	if ( !sys_ready )
		param = SUBGHZ_RECORD_DISPLAY_PARAM_MEM_ERROR;
	datfile_info.dir_name = SUB_GHZ_FILEPATH;
	datfile_info.file_ext = SUB_GHZ_FILE_EXTENSION;
	datfile_info.file_prefix = SUB_GHZ_FILE_PREFIX;
	datfile_info.file_infix = NULL;
	datfile_info.file_suffix = NULL;

	menu_sub_ghz_init();
    xQueueReset(main_q_hdl); // Reset main q before start

	// Sync the displayed/saved modulation with the currently selected Record RAW
	// frequency (parity with sub_ghz_set_opmode_hz: 900 MHz band = FSK, else OOK).
	subghz_scan_config.modulation =
			( subghz_rec_freqs[subghz_record_freq_idx].hz >= 705000000UL ) ? MODULATION_FSK : MODULATION_OOK;

	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	subghz_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter

	// Init the Record RAW render context; put the radio in RX so Ready can
	// monitor RSSI (no RAW capture / no file until Start is pressed).
	if ( sys_ready )
		subghz_record_enter_ready();
	else
		subghz_rr_ctx_reset(&subghz_rr_ctx, SUBGHZ_RECORD_RSSI_THRESHOLD_DBM);
	subghz_record_next_tick = xTaskGetTickCount() + pdMS_TO_TICKS(SUBGHZ_RECORD_TICK_MS);

	// GUI init
	m1_uiView_functions_init(VIEW_MODE_SUBGHZ_RECORD_EOL, view_subghz_record_table);
	m1_uiView_display_switch(VIEW_MODE_SUBGHZ_RECORD, param);

	// Run
	while( m1_uiView_q_message_process() )
	{
		;
	}
} // void sub_ghz_record(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_record_gui_init(void)
{
	   m1_uiView_functions_register(VIEW_MODE_SUBGHZ_RECORD, subghz_record_gui_create, subghz_record_gui_update, subghz_record_gui_destroy, subghz_record_gui_message);
} // static void subghz_record_gui_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_record_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
} // static void subghz_record_gui_create(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_record_gui_destroy(uint8_t param)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED); // migrated: defensive safety-net release (subghz_record_gui_destroy)
} // static void subghz_record_gui_destroy(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_record_gui_update(uint8_t param)
{
	switch (param)
	{
		case SUBGHZ_RECORD_DISPLAY_PARAM_READY:
			/* Ready: redesigned Change selector + live RSSI graph (Option C, Ready-only).
			 * subghz_rr_draw_ready() clears + draws the full frame; the trailing
			 * m1_u8g2_nextpage() after the switch flushes it. */
			subghz_rr_draw_ready(&m1_u8g2, &subghz_rr_ctx,
					subghz_rec_freqs[subghz_record_freq_idx].label,
					subghz_modulation_text[subghz_scan_config.modulation]);
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE:
			/* Recording: same view/graph coords, live RSSI (tested GUI). */
			subghz_rr_ctx.sample_total = subghz_record_flushed_samples
					+ ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
			subghz_rr_draw_recording(&m1_u8g2, &subghz_rr_ctx,
					subghz_rec_freqs[subghz_record_freq_idx].label,
					subghz_modulation_text[subghz_scan_config.modulation]);
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE:
			/* Capture complete: baseline M1 layout + final sample count (tested GUI). */
			subghz_rr_ctx.sample_total = subghz_record_flushed_samples;
			subghz_rr_draw_complete(&m1_u8g2, &subghz_rr_ctx,
					subghz_rec_freqs[subghz_record_freq_idx].label,
					subghz_modulation_text[subghz_scan_config.modulation]);
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_PLAY:
			/* Transmitting screen: visual fill cycle or completed/idle (empty box +
			 * hold hint). tx_pos/tx_total are set by the tick; here we only draw. */
			if ( subghz_tx_cycle_active || subghz_rr_ctx.tx_done )
			{
				char freq_lbl[16];
				subghz_format_freq_mhz(subghz_replay_hz, freq_lbl, sizeof(freq_lbl));
				subghz_rr_ctx.tx_held = subghz_ok_held();
				subghz_rr_ctx.tx_show_hint = subghz_tx_first ? 0 : 1;
				subghz_rr_draw_transmitting(&m1_u8g2, &subghz_rr_ctx,
						freq_lbl,
						subghz_modulation_text[subghz_scan_config.modulation]);
			}
			else
				subghz_rr_draw_replay_ready(&m1_u8g2, &subghz_rr_ctx,
						subghz_rec_freqs[subghz_record_freq_idx].label,
						subghz_modulation_text[subghz_scan_config.modulation]);
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_SAVE:
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_RESET:
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_MEM_ERROR:
			/* Graphic work starts here */
		    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
			// Display error message on screen
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
			u8g2_DrawXBMP(&m1_u8g2, 2, 52, 10, 10, error_10x10); // draw ERROR icon
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			u8g2_DrawStr(&m1_u8g2, 14, 61, "Memory error!");
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_SDCARD_ERROR:
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			u8g2_DrawStr(&m1_u8g2, 2, 61, "SD card access error!");
			param = subghz_uiview_gui_latest_param; // Do not update this parameter
			subghz_record_frame_hold = 20; // GUI: keep the message readable (~2 s)
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR:
			// Display error message on screen
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
			u8g2_DrawXBMP(&m1_u8g2, 2, 54, 10, 10, error_10x10); // draw ERROR icon
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			u8g2_DrawStr(&m1_u8g2, 14, 61, "Error!");
			break;

		default:
			break;
	} // switch (param)

	m1_u8g2_nextpage(); // Update display RAM

	subghz_uiview_gui_latest_param = param; // Update new param
} // static void subghz_record_gui_update(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief  Read the radio current RSSI in dBm (same basis as the scanner).
  * @retval Current RSSI in dBm.
  */
/*============================================================================*/
static int16_t subghz_record_read_rssi(void)
{
	struct si446x_reply_GET_MODEM_STATUS_map *pmodemstat;

	g_rr_rd++; // diag: count RSSI read attempts
	pmodemstat = SI446x_Get_ModemStatus(0x00); // ~100us radio SPI read (tested GUI path: bare read)
	if ( pmodemstat==NULL )
		return subghz_rr_ctx.last_rssi_dbm; // Retain last valid RSSI on failure
	// RF_Input_Level_dBm = (RSSI_value / 2) - MODEM_RSSI_COMP - 70
	return (int16_t)(pmodemstat->CURR_RSSI/2 - MODEM_RSSI_COMP - 70);
} // static int16_t subghz_record_read_rssi(void)


/*============================================================================*/
/**
  * @brief  Feed a raw RSSI sample into the displayed-text rolling average and
  *         latch a fresh whole-number value ~every SUBGHZ_RR_RSSI_DISP_MS.
  */
/*============================================================================*/
static void subghz_record_disp_rssi_update(int16_t rssi)
{
	TickType_t tnow = xTaskGetTickCount();

	subghz_rr_rssi_feed(&subghz_rr_ctx, rssi);
	if ( (int32_t)(subghz_rssi_disp_next - tnow) <= 0 )
	{
		subghz_rr_rssi_latch(&subghz_rr_ctx);
		subghz_rssi_disp_next = tnow + pdMS_TO_TICKS(SUBGHZ_RR_RSSI_DISP_MS);
	}
} // static void subghz_record_disp_rssi_update(int16_t rssi)


/*============================================================================*/
/**
  * @brief  Prepare the Ready state: reset the RSSI history/counters and put the
  *         radio into plain RX so Ready can monitor RSSI WITHOUT starting RAW
  *         sample capture or any file writing. Capture path stays untouched.
  */
/*============================================================================*/
static void subghz_record_enter_ready(void)
{
	subghz_rr_ctx_reset(&subghz_rr_ctx, SUBGHZ_RECORD_RSSI_THRESHOLD_DBM);
	subghz_record_flushed_samples = 0;
	subghz_tx_animating = 0;
	subghz_rssi_disp_next = xTaskGetTickCount() + pdMS_TO_TICKS(SUBGHZ_RR_RSSI_DISP_MS);
#if SUBGHZ_RECORD_READY_MONITOR_RSSI
	// Radio listens (no RAW capture ISR/DMA, no SD file) for RSSI monitoring.
	// Tune to the exact selected frequency; log an explicit failure (all listed
	// entries are in-range, so this only fires on a genuine radio fault).
	if ( sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_RX, subghz_rec_freqs[subghz_record_freq_idx].hz, 0, 0) != RADIO_TUNE_OK )
		M1_LOG_E(M1_LOGDB_TAG, "READY tune FAIL %s (%lu Hz)\r\n",
				subghz_rec_freqs[subghz_record_freq_idx].label,
				(unsigned long)subghz_rec_freqs[subghz_record_freq_idx].hz);
#endif
} // static void subghz_record_enter_ready(void)


/*============================================================================*/
/**
  * @brief  ~100ms UI tick: redraw the live Record RAW state DIRECTLY (like the
  *         tested GUI). READY = live RSSI graph; PLAY = TX waveform animation
  *         while a real replay is transmitting. Recording/Complete unaffected.
  */
/*============================================================================*/
static void subghz_record_tick(void)
{
	int16_t rssi;
	uint8_t advance;

	if ( subghz_record_frame_hold ) // GUI: keep a transient message on screen
	{
		subghz_record_frame_hold--;
		return;
	}

	switch (subghz_uiview_gui_latest_param)
	{
#if SUBGHZ_RECORD_READY_MONITOR_RSSI
		case SUBGHZ_RECORD_DISPLAY_PARAM_READY:
			rssi = subghz_record_read_rssi();
			advance = ( rssi >= subghz_rr_ctx.threshold_dbm ) ? 1 : 0;
			subghz_rr_push_rssi(&subghz_rr_ctx, rssi, advance);
			g_rr_push++;
			subghz_record_disp_rssi_update(rssi);
			subghz_rr_draw_ready(&m1_u8g2, &subghz_rr_ctx,
					subghz_rec_freqs[subghz_record_freq_idx].label,
					subghz_modulation_text[subghz_scan_config.modulation]);
			g_rr_render++;
			m1_u8g2_nextpage();
			M1_LOG_I(M1_LOGDB_TAG, "RRDBG rd=%lu rssi=%d push=%lu rnd=%lu disp=%d\r\n",
					(unsigned long)g_rr_rd, rssi, (unsigned long)g_rr_push,
					(unsigned long)g_rr_render, subghz_rr_ctx.rssi_disp_dbm);
			break;
#endif
		case SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE:
			rssi = subghz_record_read_rssi();
			subghz_record_disp_rssi_update(rssi);   /* numeric RSSI = real value */
			/* Timer-driven graph: advance one column EVERY tick so it keeps
			 * scrolling even in silence. Append the scaled level when the signal
			 * is above the existing threshold, otherwise a bottom-baseline sample. */
			advance = 1;
			if ( rssi >= subghz_rr_ctx.threshold_dbm )
				subghz_rr_push_rssi(&subghz_rr_ctx, rssi, advance);                  /* peak   */
			else
				subghz_rr_push_rssi(&subghz_rr_ctx, SUBGHZ_RR_RSSI_MIN_DBM, advance); /* baseline */
			subghz_rr_ctx.sample_total = subghz_record_flushed_samples
					+ ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
			g_rr_push++;
			subghz_rr_draw_recording(&m1_u8g2, &subghz_rr_ctx,
					subghz_rec_freqs[subghz_record_freq_idx].label,
					subghz_modulation_text[subghz_scan_config.modulation]);
			g_rr_render++;
			m1_u8g2_nextpage();
			M1_LOG_I(M1_LOGDB_TAG, "RECDBG live=%d disp=%d push=%lu rnd=%lu N=%lu\r\n",
					rssi, subghz_rr_ctx.rssi_disp_dbm, (unsigned long)g_rr_push,
					(unsigned long)g_rr_render, (unsigned long)subghz_rr_ctx.sample_total);
			break;

		case SUBGHZ_RECORD_DISPLAY_PARAM_PLAY:
			/* Hold-to-repeat control + live progress redraw (shared with browse). */
			subghz_tx_poll(SUBGHZ_RECORD_DISPLAY_PARAM_PLAY);
			break;

		default:
			break;
	}
} // static void subghz_record_tick(void)


static bool subghz_record_abort_overflow(bool capture_stopped)
{
	if (!subghz_capture_overflow ||
	    subghz_uiview_gui_latest_param != SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE)
		return false;
	if (!capture_stopped) sub_ghz_rx_pause();
	m1_sdm_task_stop();
	m1_sdm_task_deinit();
	sub_ghz_raw_samples_deinit(true); /* discard only this incomplete temporary recording */
	sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, subghz_scan_config.band, 0, 0);
	fb_net_read_stop(); // migrated: RAW capture aborted (buffer overflow)
	M1_LOG_E(M1_LOGDB_TAG, "Recording aborted: capture buffer overflow\r\n");
	m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR);
	return true;
}

static int subghz_record_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;
	uint32_t rcv_samples;
	TickType_t now;
	TickType_t wait;

	/* Deadline-based ~100 ms UI tick for the Ready RSSI graph, driven from this
	 * existing loop (no extra task). Only the queue WAIT changes here; the
	 * RX/TX/keypad handling below is byte-for-byte the v0.8.0.49 logic, so the
	 * capture drain (subghz_rx_rawdata_rb / sub_ghz_rx_raw_save) is preserved. */
	now = xTaskGetTickCount();
	if ( (int32_t)(subghz_record_next_tick - now) <= 0 )
		wait = 0;
	else
		wait = subghz_record_next_tick - now;

	ret = xQueueReceive(main_q_hdl, &q_item, wait);
	if (subghz_record_abort_overflow(false)) return 1;
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = subghz_record_kp_handler();
		}
		else if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_RX )
		{
			//arrpush(subghz_rx_q, q_item);
			//m1_buzzer_notification();
			rcv_samples = ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
			if ( rcv_samples >= SUBGHZ_RAW_DATA_SAMPLES_TO_RW )
			{
				M1_LOG_N(M1_LOGDB_TAG, "Raw samples %d\r\n", rcv_samples);
				subghz_record_flushed_samples += sub_ghz_rx_raw_save(false, false); // GUI: accumulate flushed count
				vTaskDelay(10); // Give the system some time in case RF noise is flooding the receiver
			} // if ( rcv_samples >= SUBGHZ_RAW_DATA_SAMPLES_TO_RW )
		} // if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_RX )

		else if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_TX )
		{
			/* Real RF transmission finished: engine cleans up, and in the replay
			 * phase (OK held) the NEXT RF replay starts immediately at real timing
			 * - back-to-back, independent of the decorative visual bar. */
			subghz_tx_on_rf_end();
		} // else if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_TX )
	} // if (ret==pdTRUE)

	/* UI tick: ~100 ms normally; ~20 ms while the Transmitting screen is open
	 * (visual cycle animating or completed/idle) so the fill advances and the
	 * center button is polled for hold-to-repeat. */
	now = xTaskGetTickCount();
	if ( (int32_t)(subghz_record_next_tick - now) <= 0 )
	{
		uint32_t tick_ms = ( subghz_tx_cycle_active || subghz_rr_ctx.tx_done )
				? SUBGHZ_TX_TICK_MS : SUBGHZ_RECORD_TICK_MS;
		subghz_record_tick();
		subghz_record_next_tick = now + pdMS_TO_TICKS(tick_ms);
	}

	return ret_val;
} // static int  subghz_record_gui_message(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static int subghz_record_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;
	char infix[5], new_filename[32], *str;
	static uint8_t last_data_saved;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) 		// Exit or Stop
		{
			if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE )
			{
				// This case is handled the same way
				// with the case the [BUTTON_OK_KP_ID] is pressed
				// This code is copied from that case below. It can be placed in a sub-function if needed.
				sub_ghz_rx_pause(); // Stop receiving
				if (subghz_record_abort_overflow(true)) return 1;
				fb_net_read_stop(); // migrated: Back, RAW capture ACTIVE->COMPLETE
				xQueueReset(main_q_hdl); // Reset old samples in the queue, if any

				if ( !last_data_saved )
				{
					sub_ghz_rx_raw_save(false, true);
					last_data_saved = true;
				} // if ( !last_data_saved )
				m1_sdm_task_stop(); // Stop sampling raw data and flush data to SD card
				m1_sdm_task_deinit();
				sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, subghz_scan_config.band, 0, 0);

				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE);
			} // if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			{
				sub_ghz_raw_samples_deinit(true); // Discard samples
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_READY);
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_PLAY )
			{
				subghz_replay_cancel = 1;
				sub_ghz_raw_tx_stop();
				sub_ghz_raw_samples_deinit(false);
				subghz_decenc_ctl.ntx_raw_repeat = 0;
				subghz_replay_ret_code = SUB_GHZ_RAW_DATA_PARSER_IDLE;
				subghz_tx_animating = 0;
				subghz_tx_cycle_active = 0;
				subghz_rr_ctx.tx_done = 0;
				fb_net_replay_stop(); // migrated: Back, PLAY->COMPLETE
				sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, subghz_scan_config.band, 0, 0);
				xQueueReset(main_q_hdl);
				subghz_replay_cancel = 0;
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE);
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_PLAY )
			else
			{
				m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
				; // Do extra tasks here if needed
				sub_ghz_rx_deinit();
				sub_ghz_ring_buffers_deinit();
				sub_ghz_tx_raw_deinit();
				menu_sub_ghz_exit();
				xQueueReset(main_q_hdl); // Reset main q before return

				return 0;
				//break; // Exit and return to the calling task (subfunc_handler_task)
			} // else
		} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )	// Start/Stop
		{
			if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_READY )
			{
				strncpy(infix, subghz_rec_freqs[subghz_record_freq_idx].label, 3);
				infix[3] = '\0';
				datfile_info.file_infix = infix;
				datfile_info.file_suffix = subghz_modulation_text[subghz_scan_config.modulation];
				datfile_info.file_ext = SUB_GHZ_TEMP_FILE_EXTENSION;
				ret = m1_sdm_file_init(&datfile_info);
				if ( !ret )
				{
					last_data_saved = false;
					m1_sdm_task_init();
					m1_sdm_task_start();
					sub_ghz_rx_raw_save(true, false);
					xQueueReset(main_q_hdl); // Reset old samples in the queue, if any
					m1_ringbuffer_reset(&subghz_rx_rawdata_rb); // Reset sample ring buffer
					subghz_capture_overflow = 0;
					fb_net_read_start(); // migrated: OK, READY->ACTIVE start capture
					sub_ghz_tx_raw_deinit();
					subghz_decenc_ctl.pulse_det_stat = PULSE_DET_ACTIVE;
					// Tune to the EXACT selected frequency. On a genuine tuning/apply
					// failure, abort the recording cleanly and report it explicitly
					// (no silent fallback to a nearby frequency).
					if ( sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_RX, subghz_rec_freqs[subghz_record_freq_idx].hz, 0, 0) != RADIO_TUNE_OK )
					{
						M1_LOG_E(M1_LOGDB_TAG, "RECSTART tune FAIL %s (%lu Hz)\r\n",
								subghz_rec_freqs[subghz_record_freq_idx].label,
								(unsigned long)subghz_rec_freqs[subghz_record_freq_idx].hz);
						m1_sdm_task_stop();
						m1_sdm_task_deinit();
						sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
						m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR);
						return 1;
					}
					// Adjust the attack and decay times
					SI446x_Change_Modem_OOK_PDTC(SUB_GHZ_433_92_NEW_PDTC);
					sub_ghz_rx_init();
					sub_ghz_rx_start();
					subghz_record_mode_flag = true;
					// Fresh RSSI history + sample count + display smoothing for this recording (GUI).
					subghz_rr_ctx_reset(&subghz_rr_ctx, SUBGHZ_RECORD_RSSI_THRESHOLD_DBM);
					subghz_record_flushed_samples = 0;
					subghz_rssi_disp_next = xTaskGetTickCount() + pdMS_TO_TICKS(SUBGHZ_RR_RSSI_DISP_MS);
					M1_LOG_I(M1_LOGDB_TAG, "RECSTART: READY->ACTIVE record_mode=%d ctx reset\r\n", subghz_record_mode_flag);
					m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE);
				} // if ( !ret )
				else
				{
					m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_SDCARD_ERROR);
				} // else
			} // if ( nfc_uiview_gui_latest_param==NFC_READ_DISPLAY_PARAM_READING_COMPLETE )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE )
			{
				sub_ghz_rx_pause(); // Stop receiving
				if (subghz_record_abort_overflow(true)) return 1;
				fb_net_read_stop(); // migrated: OK, ACTIVE->COMPLETE stop capture
				xQueueReset(main_q_hdl); // Reset old samples in the queue, if any

				if ( !last_data_saved )
				{
					sub_ghz_rx_raw_save(false, true);
					last_data_saved = true;
				} // if ( !last_data_saved )
				m1_sdm_task_stop(); // Stop sampling raw data and flush data to SD card
				m1_sdm_task_deinit();
				sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, subghz_scan_config.band, 0, 0);

				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE);
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			{
				/* Initial Play of the just-recorded capture at its EXACT frequency
				 * (single normal transmission; repeats only while OK is held). */
				subghz_replay_hz = subghz_rec_freqs[subghz_record_freq_idx].hz;
				subghz_tx_ui_reset();
				subghz_tx_first = 1;          // initial one-time transmission (no HOLD prompt yet)
				subghz_tx_wait_release = 0;   // fresh Play (re)arms cleanly
				subghz_tx_animating = 1;
				subghz_tx_cycle_active = 1;   // show the Transmitting screen empty (0%) right away
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_PLAY); // SENDING 0%
				subghz_replay_ret_code = sub_ghz_replay_start(true, subghz_scan_config.band, 0, 255);
				if ( subghz_replay_ret_code )
				{
					double_buffer_ptr_id = 1; // Update raw samples buffer
					subghz_tx_cycle_begin();   // begin the readable left-to-right fill (aligned to RF start)
					m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_PLAY);
					fb_net_replay_start(); // migrated: OK, COMPLETE->PLAY start replay
				} // if ( subghz_replay_ret_code )
				else
				{
					subghz_tx_animating = 0;
					subghz_tx_cycle_active = 0;
					m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR);
				} // else
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_PLAY )
			{
				/* On the Transmitting screen OK-CLICK is intentionally ignored:
				 * repeat is driven by HOLDING the center button (polled in the
				 * tick), which also swallows the 1 s auto-repeat CLICK events so
				 * they can't double-trigger. Stop is via Back. */
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_PLAY )
		} // else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )	// Left
		{
			if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_READY )
			{
				/* Previous entry in the extended frequency list (wrap). */
				if ( subghz_record_freq_idx > 0 )
					subghz_record_freq_idx--;
				else
					subghz_record_freq_idx = SUBGHZ_REC_FREQ_COUNT - 1;
				subghz_scan_config.modulation =
						( subghz_rec_freqs[subghz_record_freq_idx].hz >= 705000000UL ) ? MODULATION_FSK : MODULATION_OOK;
#if SUBGHZ_RECORD_READY_MONITOR_RSSI
				/* (Only when the Ready RSSI monitor is enabled) re-arm RX at the new
				 * frequency. With the monitor off, READY stays passive - no RX. */
				if ( sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_RX, subghz_rec_freqs[subghz_record_freq_idx].hz, 0, 0) != RADIO_TUNE_OK )
					M1_LOG_E(M1_LOGDB_TAG, "READY sel tune FAIL %s\r\n", subghz_rec_freqs[subghz_record_freq_idx].label);
#endif
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_READY);
				//m1_ringbuffer_reset(&subghz_rx_rawdata_rb); // Reset rx buffer for new frequency
			} // if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_READY )
			else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			{
				sub_ghz_raw_samples_deinit(true); // Discard samples
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_READY);
			} // else if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
		} // else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )	// Right
		{
			if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_READY )
			{
				/* Next entry in the extended frequency list (wrap). */
				if ( subghz_record_freq_idx < (SUBGHZ_REC_FREQ_COUNT - 1) )
					subghz_record_freq_idx++;
				else
					subghz_record_freq_idx = 0;
				subghz_scan_config.modulation =
						( subghz_rec_freqs[subghz_record_freq_idx].hz >= 705000000UL ) ? MODULATION_FSK : MODULATION_OOK;
#if SUBGHZ_RECORD_READY_MONITOR_RSSI
				/* (Only when the Ready RSSI monitor is enabled) re-arm RX at the new
				 * frequency. With the monitor off, READY stays passive - no RX. */
				if ( sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_RX, subghz_rec_freqs[subghz_record_freq_idx].hz, 0, 0) != RADIO_TUNE_OK )
					M1_LOG_E(M1_LOGDB_TAG, "READY sel tune FAIL %s\r\n", subghz_rec_freqs[subghz_record_freq_idx].label);
#endif
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_READY);
				//m1_ringbuffer_reset(&subghz_rx_rawdata_rb); // Reset rx buffer for new frequency
			} // if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_READY )
		} // else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )	// Down
		{
			if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
			{
				// Save recent unsaved data file from SD card, deinit all sdcard tasks and return to previous screen
				sub_ghz_raw_samples_deinit(false);
				str = strstr(&datfile_info.dat_filename[1], "/"); // Search for the second / sign
				if ( str!=NULL )
					str += 1; // Move to next character after the /
				else
					str = datfile_info.dat_filename;
				strcpy(new_filename, str);
				str = strstr(new_filename, "."); // Search for the dot sign
				// The search should always return the valid result, so no need to do a null check.
				strcpy(str, SUB_GHZ_FILE_EXTENSION); // Replace the tmp extension with the real extension
				M1_LOG_I(M1_LOGDB_TAG, "SAVE: tmp='%s' newdefault='%s'\r\n", datfile_info.dat_filename, new_filename);
				ret = m1_fb_rename_file(datfile_info.dat_filename, new_filename, true);
				M1_LOG_I(M1_LOGDB_TAG, "SAVE: final='%s' rename=%d\r\n", new_filename, ret);
				if ( !ret )
				{
					m1_draw_file_saved_screen();      /* unified save-success splash */
					m1_wait_back_to_exit();           /* preserve existing BACK-to-exit blocking */
				}
				else
				{
					m1_fb_delete_file(datfile_info.dat_filename); // Delete the temporary file
				}
				m1_uiView_display_update(SUBGHZ_RECORD_DISPLAY_PARAM_READY);
			} // if ( subghz_uiview_gui_latest_param==SUBGHZ_RECORD_DISPLAY_PARAM_COMPLETE )
		} // else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	} // if (ret==pdTRUE)

	return 1;
} // static int subghz_record_kp_handler(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_browse_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_SUBGHZ_REPLAY_BROWSE, subghz_replay_browse_gui_create, subghz_replay_browse_gui_update, subghz_replay_browse_gui_destroy, subghz_replay_browse_gui_message);
} // static void subghz_replay_browse_gui_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_browse_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
} // static void subghz_replay_browse_gui_create(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_browse_gui_destroy(uint8_t param)
{

} // static void subghz_replay_browse_gui_destroy(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_browse_gui_update(uint8_t param)
{
	if ( m1_browser_explore_active() ) { m1_browser_explore_end(); m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT); return; }
	S_M1_file_info *ex = m1_browser_explore_take_pending();
	while (true)
	{
		f_info = ex ? ex : storage_browse();
		ex = NULL; /* one-shot: only the first pass uses the Explore-selected file */
		if ( !f_info->file_is_selected ) // User exits?
		{
			m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
			break;
		} // if ( f_info->file_is_selected )

		if ( sub_ghz_file_load() ) // Error?
		{
			M1_LOG_E(M1_LOGDB_TAG, "BROWSE: PATH_A file_load FAILED -> File error box\r\n"); // TEMP DIAG
			m1_message_box(&m1_u8g2, "File error!", "", "", "BACK to return");
			continue;
		}
		M1_LOG_I(M1_LOGDB_TAG, "BROWSE: file_load OK -> replay_start (hz=%lu)\r\n",
				(unsigned long)subghz_replay_hz); // TEMP DIAG

		subghz_tx_ui_reset(); // fresh Transmitting screen at 0% (req 1)
		subghz_tx_first = 1;  // initial one-time transmission (no HOLD prompt yet)
		subghz_tx_wait_release = 0;   // fresh Play (re)arms cleanly
		m1_uiView_display_switch(VIEW_MODE_SUBGHZ_REPLAY_PLAY, SUBGHZ_REPLAY_DISPLAY_PARAM_ACTIVE);
		menu_sub_ghz_init();
		subghz_replay_ret_code = sub_ghz_replay_start(false, subghz_replay_band, subghz_replay_channel, 255);
		if ( subghz_replay_ret_code )
		{
			double_buffer_ptr_id = 1; // Update raw samples buffer
			subghz_tx_cycle_begin();   // start the readable left-to-right fill
			fb_net_replay_start(); // migrated: replay browse, file loaded start replay
			m1_uiView_display_update(SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY);
		} // if ( ret_code )
		else
		{
			M1_LOG_E(M1_LOGDB_TAG, "BROWSE: PATH_B replay_start returned 0 -> SYS_ERROR (File error)\r\n"); // TEMP DIAG
			subghz_tx_cycle_active = 0;
			m1_uiView_display_update(SUBGHZ_REPLAY_DISPLAY_PARAM_SYS_ERROR);
		} // else
		break;
	} // while (true)

} // static void subghz_replay_browse_gui_update(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static int subghz_replay_browse_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = subghz_replay_browse_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if(q_item.q_evt_type==Q_EVENT_MENU_EXIT)
		{
			m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
			xQueueReset(main_q_hdl); // Reset main q before return
			menu_sub_ghz_exit();
			ret_val = 0;
		}
	} // if (ret==pdTRUE)

	return ret_val;
} // static int  subghz_replay_browse_gui_message(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static int subghz_replay_browse_kp_handler(void)
{
	return 1;
} // static int subghz_replay_browse_kp_handler(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_play_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_SUBGHZ_REPLAY_PLAY, subghz_replay_play_gui_create, subghz_replay_play_gui_update, subghz_replay_play_gui_destroy, subghz_replay_play_gui_message);
} // static void subghz_replay_play_gui_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_play_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
} // static void subghz_replay_play_gui_create(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_play_gui_destroy(uint8_t param)
{

} // static void subghz_replay_play_gui_destroy(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void subghz_replay_play_gui_update(uint8_t param)
{
	switch (param)
	{
		case SUBGHZ_REPLAY_DISPLAY_PARAM_ACTIVE:
			/* Entering replay -> the redesigned Transmitting screen, progress box
			 * empty (TX burst not started / just starting). Same renderer as the
			 * Record RAW replay path. */
			{
				char freq_lbl[16];
				subghz_format_freq_mhz(subghz_replay_hz, freq_lbl, sizeof(freq_lbl));
				subghz_rr_ctx.tx_pos = 0;
				subghz_rr_ctx.tx_total = 0;
				subghz_rr_ctx.tx_done = 0;
				subghz_rr_ctx.tx_show_hint = subghz_tx_first ? 0 : 1; // no HOLD prompt during initial TX
				subghz_rr_draw_transmitting(&m1_u8g2, &subghz_rr_ctx, freq_lbl,
						subghz_modulation_text[subghz_replay_mod]);
			}
			break;

		case SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY:
			/* Transmitting screen only: visual fill cycle or completed/idle (empty
			 * box + hold hint). tx_pos/tx_total are set by the tick; only draw
			 * here. The obsolete "Press OK to replay" replay-ready presentation
			 * (antenna + freq + Press OK bar) has been removed -- BACK now always
			 * exits this view directly (subghz_replay_play_exit_to_browser()), so
			 * this case is only ever reached with an active or just-completed
			 * transmission (subghz_tx_cycle_active or subghz_rr_ctx.tx_done). */
			{
				char freq_lbl[16];
				subghz_format_freq_mhz(subghz_replay_hz, freq_lbl, sizeof(freq_lbl));
				subghz_rr_ctx.tx_held = subghz_ok_held();
				subghz_rr_ctx.tx_show_hint = subghz_tx_first ? 0 : 1;
				subghz_rr_draw_transmitting(&m1_u8g2, &subghz_rr_ctx, freq_lbl,
						subghz_modulation_text[subghz_replay_mod]);
			}
			break;

		case SUBGHZ_REPLAY_DISPLAY_PARAM_SYS_ERROR:
			// Display error message on screen (self-contained frame)
			u8g2_ClearBuffer(&m1_u8g2);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
			u8g2_DrawXBMP(&m1_u8g2, 2, 52, 10, 10, error_10x10); // draw ERROR icon
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			u8g2_DrawStr(&m1_u8g2, 14, 61, "File error!");
			break;

		default:
			break;
	} // switch (param)

	m1_u8g2_nextpage(); // Update display RAM
    subghz_uiview_gui_latest_param = param; // Update new param
} // static void subghz_replay_play_gui_update(uint8_t param)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static int subghz_replay_play_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;
	static TickType_t next_tick = 0;
	TickType_t now, wait;
	uint8_t active_ui;   /* Transmitting screen open (xmit or completed/idle) -> ~20 ms tick */

	/* While the Transmitting screen is open (transmitting OR completed/idle), wake
	 * on a ~20 ms deadline so progress fills and the held button can be polled for
	 * hold-to-repeat. Otherwise block on events exactly as before. Only the queue
	 * WAIT changes here - the event handling below is byte-for-byte the original. */
	active_ui = ( subghz_uiview_gui_latest_param==SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY &&
				( subghz_tx_cycle_active || subghz_rr_ctx.tx_done ) );
	if ( active_ui )
	{
		now = xTaskGetTickCount();
		wait = ( (int32_t)(next_tick - now) <= 0 ) ? 0 : (next_tick - now);
	}
	else
	{
		wait = portMAX_DELAY;
	}

	ret = xQueueReceive(main_q_hdl, &q_item, wait);
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = subghz_replay_play_kp_handler();
		}
		else if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_TX )
		{
			/* Real RF transmission finished: engine cleans up, and in the replay
			 * phase (OK held) the NEXT RF replay starts immediately at real timing
			 * - back-to-back, independent of the decorative visual bar. */
			subghz_tx_on_rf_end();
		} // else if ( q_item.q_evt_type==Q_EVENT_SUBGHZ_TX )
	} // if (ret==pdTRUE)

	/* ~20 ms control + redraw while the Transmitting screen is open (visual cycle
	 * animating or completed/idle): drives the fill + hold-to-repeat (shared). */
	if ( subghz_uiview_gui_latest_param==SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY &&
			( subghz_tx_cycle_active || subghz_rr_ctx.tx_done ) )
	{
		now = xTaskGetTickCount();
		if ( (int32_t)(next_tick - now) <= 0 )
		{
			subghz_tx_poll(SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY);
			next_tick = now + pdMS_TO_TICKS(SUBGHZ_TX_TICK_MS);
		}
	}

	return ret_val;
} // static int  subghz_replay_play_gui_message(void)


/*============================================================================*/
/**
  * @brief  Canonical, single exit path from the saved-file replay/play view
  *         back to the saved-recordings browser. Used for BACK regardless of
  *         whether a transmission is actively running, just completed, or
  *         already idle -- there is exactly one way out, never a two-step
  *         "stop, then leave" sequence.
  *
  *         Safely stops an in-flight/just-finished transmission first (reusing
  *         subghz_tx_safe_stop() exactly as the old active-TX BACK branch
  *         did), then performs the SAME full teardown and navigation the
  *         file-browser return path always used: RF/DMA/raw-sample resources
  *         deinitialized, pending TX events dropped, replay state reset so no
  *         stale TX event/held-button/animation/replay-request state survives,
  *         LED stopped, start directory set to 0:/SUBGHZ, and the view
  *         switched directly to the browser. Calling the safe-stop and the
  *         full-teardown deinit calls back-to-back on the same resources is
  *         the same redundant-but-safe pattern the OLD two-BACK-press flow
  *         already exercised (every sub_ghz_raw_tx_stop()/
  *         sub_ghz_raw_samples_deinit() below is already guarded/idempotent
  *         against being called when nothing is left to tear down) -- just
  *         collapsed into one BACK press instead of two.
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void subghz_replay_play_exit_to_browser(void)
{
	if ( subghz_tx_cycle_active || subghz_rr_ctx.tx_done )
	{
		subghz_tx_safe_stop();   // safely stop an active/just-finished TX first (req 1/2)
	}
	fb_net_replay_stop(); // migrated: replay play Back-exit (req 5)
	subghz_replay_cancel = 1;                     // stop any in-flight replay before leaving
	sub_ghz_raw_tx_stop();                        // stop TX safely (req 3)
	subghz_replay_ret_code = SUB_GHZ_RAW_DATA_PARSER_IDLE;
	subghz_decenc_ctl.ntx_raw_repeat = 0;
	sub_ghz_raw_samples_deinit(false);
	sub_ghz_ring_buffers_deinit();
	sub_ghz_tx_raw_deinit();
	xQueueReset(main_q_hdl);                      // drop pending TX events (req 6)
	subghz_replay_cancel = 0;
	subghz_rr_ctx.tx_done = 0;                    // no stale TX-done/animation state survives (req 4)
	m1_fb_set_start_dir(M1_SD_DIR_SUBGHZ);              // req 7
	m1_uiView_display_switch(VIEW_MODE_SUBGHZ_REPLAY_BROWSE, 0); // req 8/9
} // static void subghz_replay_play_exit_to_browser(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static int subghz_replay_play_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) 		// Exit or Stop
		{
			/* One BACK, one canonical path -- always returns directly to the
			 * saved-recordings browser, whether a transmission is actively
			 * running, just completed, or already idle. No intermediate
			 * "replay-ready" screen. */
			subghz_replay_play_exit_to_browser();
		} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )	// Play
		{
			/* OK starts a single playthrough ONLY from replay-ready. On the
			 * Transmitting screen OK-CLICK is ignored (repeat is HOLD-driven,
			 * polled in the tick; this also swallows auto-repeat CLICKs). */
			if ( subghz_uiview_gui_latest_param==SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY &&
					!subghz_tx_cycle_active && !subghz_rr_ctx.tx_done )
			{
				subghz_tx_ui_reset();
				subghz_tx_first = 1;   // initial one-time transmission (no HOLD prompt yet)
				subghz_tx_wait_release = 0;   // fresh Play (re)arms cleanly
				m1_uiView_display_update(SUBGHZ_REPLAY_DISPLAY_PARAM_ACTIVE); // SENDING 0%
				if ( subghz_tx_start_playthrough() )
					m1_uiView_display_update(SUBGHZ_REPLAY_DISPLAY_PARAM_PLAY); // live progress
				else
					m1_uiView_display_update(SUBGHZ_REPLAY_DISPLAY_PARAM_SYS_ERROR);
			}
		} // else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	} // if (ret==pdTRUE)

	return 1;
} // static int subghz_replay_play_kp_handler(void)



/*============================================================================*/
/**
  * @brief  Load Sub-GHz data file from SD card
  * @param
  * @retval 0 if success
  */
/*============================================================================*/
static uint8_t sub_ghz_file_load(void)
{
	uint8_t uret, sys_error, key_len;
	char *token, *str, *end_ptr;

	/* Every attempt, including an invalid extension/header retry, starts from
	 * a completely clean ownership state. */
	sub_ghz_raw_samples_deinit(false);
	sub_ghz_ring_buffers_deinit();
	sys_error = 1;
	do
	{
		uret = strlen(f_info->file_name);
		if ( !uret )
			break;
		key_len = 0;
		while ( uret )
		{
			key_len++;
			if ( f_info->file_name[--uret]=='.' ) // Find the dot starting from the last character
				break;
		} // while ( uret )
		if ( !uret || (key_len!=4) ) // key_len==length of SUB_GHZ_FILE_EXTENSION
			break;
		if ( strcmp(&f_info->file_name[uret], SUB_GHZ_FILE_EXTENSION) )
			break;
		fu_path_combine((char*)datfile_info.dat_filename, sizeof(datfile_info.dat_filename), f_info->dir_name, f_info->file_name);
		M1_LOG_I(M1_LOGDB_TAG, "FL: path='%s' name='%s' extlen=%d\r\n", datfile_info.dat_filename, f_info->file_name, key_len);

		sys_error = sub_ghz_ring_buffers_init();
		M1_LOG_I(M1_LOGDB_TAG, "FL: ring_init=%d\r\n", sys_error);
		if ( sys_error )
			break;
		sys_error = sub_ghz_raw_samples_init();
		M1_LOG_I(M1_LOGDB_TAG, "FL: samples_init=%d\r\n", sys_error);
		if ( sys_error )
			break;

		token = strtok(sdcard_dat_buffer, "\r\n"); // Filetype
		if ( token == NULL ) // Empty/blank file -- no lines at all
			break;
		M1_LOG_I(M1_LOGDB_TAG, "FL: filetype='%s'\r\n", token);
		key_len = SUB_GHZ_DATAFILE_RAW_FORMAT_N;
		if ( strstr(token, SUB_GHZ_DATAFILE_FILETYPE_PACKET) )
		{
			key_len = SUB_GHZ_DATAFILE_RAW_FORMAT_N;
			break; // Not support for now.
		} // if ( strstr(token, SUB_GHZ_DATAFILE_FILETYPE_PACKET) )
		token = strtok(NULL, "\r\n"); // Version
		token = strtok(NULL, "\r\n"); // Frequency
		if ( token == NULL ) // File truncated before the Frequency line
			break;
		str = strstr(token, ":");
		if ( str == NULL ) // Frequency line malformed (no ':' separator)
			break;
		str += 1; // Move to the frequency value
		subghz_replay_hz = (uint32_t)strtol(str, &end_ptr, 10); // EXACT integer Hz from the file header
		M1_LOG_I(M1_LOGDB_TAG, "FL: freqHz_str='%s' freqHz=%lu\r\n", str, (unsigned long)subghz_replay_hz);
		if ( subghz_replay_hz==0 )
			break;
		/* Restore the EXACT stored frequency. No nearest-band mapping: replay
		 * retunes to this exact Hz. Reject ONLY if the frequency is outside the
		 * radio's tunable range (explicit File error, not a silent substitution). */
		if ( !radio_freq_in_range(subghz_replay_hz) )
		{
			M1_LOG_E(M1_LOGDB_TAG, "FL: freq %lu Hz out of radio range\r\n", (unsigned long)subghz_replay_hz);
			break;
		}
		subghz_replay_channel = 0;
		M1_LOG_I(M1_LOGDB_TAG, "FL: exact freq %lu Hz (%d.%03d MHz) in range\r\n",
				(unsigned long)subghz_replay_hz,
				(int)(subghz_replay_hz/1000000UL), (int)((subghz_replay_hz/1000UL)%1000UL));

		token = strtok(NULL, "\r\n"); // Modulation
		if ( token == NULL ) // File truncated before the Modulation line
			break;
		m1_strtoupper(token);
		for (subghz_replay_mod=0; subghz_replay_mod<SUBGHZ_MODULATION_LIST; subghz_replay_mod++)
		{
			if ( strstr(token, subghz_modulation_text[subghz_replay_mod]) )
				break;
		}
		M1_LOG_I(M1_LOGDB_TAG, "FL: mod='%s' idx=%d/%d\r\n", token, subghz_replay_mod, SUBGHZ_MODULATION_LIST);
		if ( subghz_replay_mod >= SUBGHZ_MODULATION_LIST ) // Not found?
			break;
		sys_error = 0; // Reset, no error
	} while (0);
	if (sys_error)
	{
		/* sub_ghz_raw_samples_init() may have allocated one buffer and/or
		 * opened the file before detecting a malformed/unsupported header. */
		sub_ghz_raw_samples_deinit(false);
		sub_ghz_ring_buffers_deinit();
	}

	M1_LOG_I(M1_LOGDB_TAG, "FL: RESULT=%d\r\n", sys_error);
	return sys_error;
} // static uint8_t sub_ghz_file_load(void)


/*============================================================================*/
/**
  * @brief  Load data file from SD card and replay it
  * @param  None
  * @retval None
  */
/*============================================================================*/
void sub_ghz_replay(void)
{
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	subghz_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter

	// GUI init
	m1_uiView_functions_init(VIEW_MODE_SUBGHZ_REPLAY_EOL, view_subghz_replay_table);
	/* Open the browser directly inside the Sub-GHz folder (one-shot start dir;
	 * other file-browser users unaffected) -- mirrors NFC Saved. */
	if (!m1_fb_check_existence(M1_SD_DIR_SUBGHZ)) m1_fb_make_dir(M1_SD_DIR_SUBGHZ);
	m1_fb_set_start_dir(M1_SD_DIR_SUBGHZ);
	m1_uiView_display_switch(VIEW_MODE_SUBGHZ_REPLAY_BROWSE, 0);

	// Run
	while( m1_uiView_q_message_process() )
	{
		;
	}
} // void sub_ghz_replay(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void sub_ghz_frequency_reader(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int16_t rssi, rssi_max, rssi_avg, freq_step;
	int16_t avg_noisefloor[SUB_GHZ_BAND_EOL][3];
	uint8_t active_band_id, i, j, asf_sample_count, detection_count;
	float freq_found;
	uint8_t prn_buffer[30], float_buffer[10];
	struct si446x_reply_GET_MODEM_STATUS_map *pmodemstat;

	m1_u8g2_firstpage();
	 // This call required for page drawing in mode 1
    do
    {
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 1, 10, "Frequency Reader");
		u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_2B);
		u8g2_DrawStr(&m1_u8g2, 10, 30, "000.000");
		u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_1B);
		u8g2_DrawStr(&m1_u8g2, 100, 28, "MHz");
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 1, INFO_BOX_Y_POS_ROW_1, "000.000MHz");
		u8g2_DrawStr(&m1_u8g2, 1, INFO_BOX_Y_POS_ROW_2, "000.000MHz");
		u8g2_DrawStr(&m1_u8g2, 1, INFO_BOX_Y_POS_ROW_3, "000.000MHz");
    } while (m1_u8g2_nextpage());

    for (i=0; i<SUB_GHZ_BAND_EOL; i++)
    {
    	for (j=0; j<3; j++)
    		avg_noisefloor[i][j] = NOISE_FLOOR_RSSI_THRESHOLD;
    }

    active_band_id = SUB_GHZ_BAND_300;
    freq_step = CHANNEL_STEPS_MAX + 1;
    detection_count = 0;
    asf_sample_count = 0;

    menu_sub_ghz_init();

    while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;
	    if ( freq_step <= subghz_band_steps[active_band_id][1] )
	    {
	    	SI446x_Start_Rx(freq_step); // Change channel
	    } // if ( freq_step <= subghz_band_steps[active_band_id] )
	    else
	    {
	    	sub_ghz_set_opmode(SUB_GHZ_OPMODE_RX, active_band_id, 0, 0); // Process time: ~27.57ms (with reset) - ~4.5ms (optimized)
	    	if ( subghz_band_steps[active_band_id][1] > 1 ) // There're other channels to retry
	    		freq_step = 0; // Let start with channel 1 in next round
	    } // else
		// Read INTs, clear pending ones
		SI446x_Get_IntStatus(0, 0, 0);
		rssi_max = -255;
		for ( i=0; i<2; i++)
		{
			//vTaskDelay(3); // Give the radio chip some time to do its task
			pmodemstat = SI446x_Get_ModemStatus(0x00); // Process time: ~99.7us
			// RF_Input_Level_dBm = (RSSI_value / 2) – MODEM_RSSI_COMP – 70
			// MODEM_RSSI_COMP = 0x40 = 64d is appropriate for most applications.
			rssi = pmodemstat->CURR_RSSI/2 - MODEM_RSSI_COMP - 70;
			if ( rssi > rssi_max )
				rssi_max = rssi;
			vTaskDelay(1);
		} // for ( i=0; i<2; i++)

		rssi_avg = 0;
		for (i=0; i<3; i++)
			rssi_avg += avg_noisefloor[active_band_id][i];
		rssi_avg /= 3; // Get average noise floor of the current frequency
		if ( rssi_max >= (rssi_avg + SIGNAL_TO_NOISE_RATIO) ) // SNR matches the condition?
		{
			m1_buzzer_notification();
			freq_found = subghz_band_steps[active_band_id][0];
			if ( freq_step <= CHANNEL_STEPS_MAX )
				freq_found += freq_step*CHANNEL_STEP;
			m1_float_to_string(float_buffer, freq_found, 3);

			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawBox(&m1_u8g2, 10, 14, 90, 17); // Clear old content
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_2B);
			u8g2_DrawStr(&m1_u8g2, 10, 30, float_buffer);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawBox(&m1_u8g2, 1, INFO_BOX_Y_POS_ROW_1 + detection_count*10 - 9, M1_LCD_DISPLAY_WIDTH, 10); // Clear old content
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			sprintf(prn_buffer, "%sMHz RSSI%ddBm", float_buffer, rssi);
			u8g2_DrawStr(&m1_u8g2, 1, INFO_BOX_Y_POS_ROW_1 + detection_count*10, prn_buffer);
			m1_u8g2_nextpage(); // Update display RAM

			M1_LOG_N(M1_LOGDB_TAG, float_buffer);
			M1_LOG_N(M1_LOGDB_TAG, " RSSI: %ddBm\r\n", rssi);
			detection_count++;
			if ( detection_count >= 3 )
				detection_count = 0;
		} // if ( rssi_max >= (rssi_avg + SIGNAL_TO_NOISE_RATIO) )
		else
		{
			avg_noisefloor[active_band_id][asf_sample_count] = rssi_max;
		}

		if ( ++freq_step > subghz_band_steps[active_band_id][1] ) // All channels have been completed?
		{
			freq_step = CHANNEL_STEPS_MAX + 1; // Reset
			while (true)
			{
				active_band_id++;
				if ( active_band_id >= SUB_GHZ_BAND_EOL)
				{
					active_band_id = SUB_GHZ_BAND_300;
					asf_sample_count++;
					if ( asf_sample_count >= 3 )
						asf_sample_count = 0;
					vTaskDelay(20); // Return time to system to do its job
				} // if ( active_band_id >= SUB_GHZ_BAND_EOL)
				if ( subghz_band_steps[active_band_id][1] != 0 ) // Change to this band if it is not disabled
					break;
			} // while (true)
		} // if ( ++freq_step > subghz_band_steps[active_band_id][1] )
		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, 0/*portMAX_DELAY*/);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_300, 0, 0);
					menu_sub_ghz_exit();

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void sub_ghz_frequency_reader(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void sub_ghz_gpio_remap(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t refresh = 0;

	/* Graphic work starts here */
	m1_u8g2_firstpage(); // This call required for page drawing in mode 1
	// Draw box for selected menu item with text color
	u8g2_DrawBox(&m1_u8g2, 0, THIS_LCD_MENU_TEXT_FIRST_ROW_Y - THIS_LCD_MENU_TEXT_ROW_SPACE + 2, M1_LCD_SUB_MENU_TEXT_FRAME_W, THIS_LCD_MENU_TEXT_ROW_SPACE);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 4, THIS_LCD_MENU_TEXT_FIRST_ROW_Y, "Sub-GHz Tx output");
	// Draw arrows left and right
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 36, 0 + 2, 10, 10, arrowleft_10x10);
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 16, 0 + 2, 10, 10, arrowright_10x10);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N); // return to default font
	// Draw info box at the bottom
	m1_info_box_display_init(true);
	sub_ghz_remap_status_update(subghz_tx_on_ext_gpio);
	m1_u8g2_nextpage(); // Update display RAM

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;
		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if (this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK)
				{
					subghz_tx_on_ext_gpio ^= 1; // Toggle
					refresh = 1;
				}
				else if (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK)
				{
					subghz_tx_on_ext_gpio ^= 1; // Toggle
					refresh = 1;
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )

			if ( refresh )
			{
				refresh = 0;
				sub_ghz_remap_status_update(subghz_tx_on_ext_gpio);
			} // if ( refresh )
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task
} // void sub_ghz_gpio_remap(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void sub_ghz_remap_status_update(uint8_t gpio_remapped)
{
	const char *gpio_map;

	if ( !gpio_remapped )
	{
		gpio_map = "Monstatek M1";
	}
	else
	{
		gpio_map = "GPIO 16 (PD0)";
	}
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // set to text color
	m1_info_box_display_draw(INFO_BOX_ROW_1, gpio_map);
	m1_u8g2_nextpage(); // Update display RAM
} // static void sub_ghz_remap_status_update(uint8_t gpio_remapped)



/*============================================================================*/
/*
  * @brief  Initialize the decoder module
  * Input capture mode for both rising and falling edges, given in TIMx_CCR1 for each edge capture
  * @param  None
  * @retval None
 */
/*============================================================================*/
static void sub_ghz_rx_init(void)
{
	GPIO_InitTypeDef gpio_init_struct;
	TIM_IC_InitTypeDef tim_ic_init = {0};
	TIM_MasterConfigTypeDef tim_master_conf = {0};
	uint32_t tim_prescaler_val;

	/* Pin configuration: input floating */
	gpio_init_struct.Pin = SUBGHZ_RX_GPIO_PIN;
	gpio_init_struct.Mode = GPIO_MODE_AF_OD;
	gpio_init_struct.Pull = GPIO_NOPULL; // GPIO_PULLDOWN;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio_init_struct.Alternate = SUBGHZ_GPIO_AF_RX;
	HAL_GPIO_Init(SUBGHZ_RX_GPIO_PORT, &gpio_init_struct);

	/*  Clock Configuration for TIMER */
	SUBGHZ_RX_TIMER_CLK();

	/* Timer Clock */
	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / 1000000) - 1; // 1MHz

	timerhdl_subghz_rx.Instance = SUBGHZ_RX_TIMER;

	timerhdl_subghz_rx.Init.ClockDivision = 0;
	timerhdl_subghz_rx.Init.CounterMode = TIM_COUNTERMODE_UP;
	timerhdl_subghz_rx.Init.Period = SUBGHZ_RX_TIMEOUT_TIME;
	timerhdl_subghz_rx.Init.Prescaler = tim_prescaler_val;
	timerhdl_subghz_rx.Init.RepetitionCounter = 0;
	timerhdl_subghz_rx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_IC_Init(&timerhdl_subghz_rx) != HAL_OK)
	{
		//Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}
/*
  Input capture mode
  In input capture mode, the capture/compare registers (TIMx_CCRx) are used to latch the value of the counter after a transition detected by the corresponding ICx signal. When a
  capture occurs, the corresponding CCXIF flag (TIMx_SR register) is set and an interrupt or a DMA request can be sent if they are enabled. If a capture occurs while the CCxIF flag was
  already high, then the overcapture flag CCxOF (TIMx_SR register) is set. CCxIF can be
  cleared by software by writing it to 0 or by reading the captured data stored in the
  TIMx_CCRx register. CCxOF is cleared when it is written with 0.
  The following example shows how to capture the counter value in TIMx_CCR1 when tim_ti1
  input rises. To do this, use the following procedure:
  1. Select the proper tim_tix_in[15:0] source (internal or external) with the TI1SEL[3:0] bits in the TIMx_TISEL register.
  2. Select the active input: TIMx_CCR1 must be linked to the tim_ti1 input, so write the
  CC1S bits to 01 in the TIMx_CCMR1 register. As soon as CC1S becomes different
  from 00, the channel is configured in input and the TIMx_CCR1 register becomes read-
  only.
  3. Program the needed input filter duration in relation with the signal connected to the
  timer (when the input is one of the tim_tix (ICxF bits in the TIMx_CCMRx register). Let’s
  imagine that, when toggling, the input signal is not stable during at most five internal clock cycles. We must program a filter duration longer than these five clock cycles. We can validate a transition on tim_ti1 when eight consecutive samples with the new level
  have been detected (sampled at fDTS frequency). Then write IC1F bits to 0011 in the
  TIMx_CCMR1 register.
  4. Select the edge of the active transition on the tim_ti1 channel by writing the CC1P and CC1NP bits to 000 in the TIMx_CCER register (rising edge in this case).
  5. Program the input prescaler. In this example, the capture is to be performed at each
  valid transition, so the prescaler is disabled (write IC1PS bits to 00 in the TIMx_CCMR1 register).
  6. Enable capture from the counter into the capture register by setting the CC1E bit in the TIMx_CCER register.
  7. If needed, enable the related interrupt request by setting the CC1IE bit in the
  TIMx_DIER register, and/or the DMA request by setting the CC1DE bit in the TIMx_DIER register.
  When an input capture occurs:
  • The TIMx_CCR1 register gets the value of the counter on the active transition.
  • CC1IF flag is set (interrupt flag). CC1OF is also set if at least two consecutive captures occurred whereas the flag was not cleared.
  • An interrupt is generated depending on the CC1IE bit.
  • A DMA request is generated depending on the CC1DE bit.
  In order to handle the overcapture, it is recommended to read the data before the overcapture flag. This is to avoid missing an overcapture which may happen after reading
  the flag and before reading the data.
  Note: IC interrupt and/or DMA requests can be generated by software by setting the
  corresponding CCxG bit in the TIMx_EGR register.
*/
  /* Enable the Master/Slave Mode */
  /* SMS = 1000:Combined reset + trigger mode - Rising edge of the selected trigger input (tim_trgi)
   * reinitializes the counter, generates an update of the registers and starts the counter.
   * SMS = 0100: Reset mode - Rising edge of the selected trigger input (tim_trgi)
   * reinitializes the counter and generates an update of the registers. -Recommended by Reference Manual
   */
	tim_master_conf.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;//TIM_SMCR_MSM;
	tim_master_conf.MasterOutputTrigger = TIM_TRGO_RESET;
	if (HAL_TIMEx_MasterConfigSynchronization(&timerhdl_subghz_rx, &tim_master_conf) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	tim_ic_init.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
	tim_ic_init.ICSelection = TIM_ICSELECTION_DIRECTTI;
	tim_ic_init.ICPrescaler = TIM_ICPSC_DIV1;
	tim_ic_init.ICFilter = 0;
	if (HAL_TIM_IC_ConfigChannel(&timerhdl_subghz_rx, &tim_ic_init, SUBGHZ_RX_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}

	/* Enable the TIMx global Interrupt */
	HAL_NVIC_SetPriority(SUBGHZ_RX_TIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(SUBGHZ_RX_TIMER_IRQn);

	/* Configures the TIM Update Request Interrupt source: counter overflow */
	__HAL_TIM_URS_ENABLE(&timerhdl_subghz_rx);

	/* Clear update flag */
	__HAL_TIM_CLEAR_FLAG( &timerhdl_subghz_rx, TIM_FLAG_UPDATE);

	/* Update ISR (TIM1_UP_IRQHandler) is TX-only DMA/CCR4 code; it must NOT run during RX.
	   TIM1 is shared RX(CC1)/TX(CC4). Disable Update IT for RX (matches proven RAW Record path). */
	__HAL_TIM_DISABLE_IT( &timerhdl_subghz_rx, TIM_FLAG_UPDATE);

	/* Enable the timer */
	//__HAL_TIM_ENABLE(&timerhdl_subghz_rx);

} // static void sub_ghz_rx_init(void)



/*============================================================================*/
/*
  * @brief  Start receiving radio data
  * @param  None
  * @retval None
 */
/*============================================================================*/
static void sub_ghz_rx_start(void)
{
	if (HAL_TIM_IC_Start_IT(&timerhdl_subghz_rx, SUBGHZ_RX_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}
} // static void sub_ghz_rx_start(void)



/*============================================================================*/
/*
  * @brief  Temporarily stop receiving radio data
  * @param  None
  * @retval None
 */
/*============================================================================*/
static void sub_ghz_rx_pause(void)
{
	if (HAL_TIM_IC_Stop_IT(&timerhdl_subghz_rx, SUBGHZ_RX_TIMER_RX_CHANNEL) != HAL_OK)
	{
		//_Error_Handler(__FILE__, __LINE__);
		Error_Handler();
	}
} // static void sub_ghz_rx_pause(void)




/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_rx_deinit(void)
{
	if ( timerhdl_subghz_rx.State==HAL_TIM_STATE_READY ) // Make sure that the timer has been initialized!
	{
		HAL_TIM_IC_DeInit(&timerhdl_subghz_rx);
		__HAL_TIM_URS_DISABLE(&timerhdl_subghz_rx);
		/* Reset the CC1E Bit */
		timerhdl_subghz_rx.Instance->CCER &= ~TIM_CCER_CC1E;
		timerhdl_subghz_rx.Instance->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);
	} // if ( timerhdl_subghz_rx.State==HAL_TIM_STATE_READY )

	SUBGHZ_RX_TIMER_CLK_DIS();
	HAL_NVIC_DisableIRQ(SUBGHZ_RX_TIMER_IRQn);

	HAL_GPIO_DeInit(SUBGHZ_RX_GPIO_PORT, SUBGHZ_RX_GPIO_PIN);

	if ( main_q_hdl != NULL)
		xQueueReset(main_q_hdl);
	//arrfree(subghz_rx_q);
} // static void sub_ghz_rx_deinit(void)




/*============================================================================*/
/*
  * @brief  Initialize ring buffers to receive RF data
  * @param  None
  * @retval 0 if success
 */
/*============================================================================*/
static uint8_t sub_ghz_ring_buffers_init(void)
{
	if (subghz_front_buffer != NULL && subghz_ring_read_buffer != NULL &&
	    subghz_sdcard_write_buffer != NULL)
		return 0;
	if (subghz_front_buffer != NULL || subghz_ring_read_buffer != NULL ||
	    subghz_sdcard_write_buffer != NULL)
		sub_ghz_ring_buffers_deinit();
	subghz_front_buffer_size = SUBGHZ_RAW_DATA_SAMPLES_MAX;

	while ( true )
	{
		subghz_front_buffer = malloc(subghz_front_buffer_size*sizeof(uint16_t));
		if ( subghz_front_buffer )
			break;
		if ( subghz_front_buffer_size <= 1U ) /* halving further can't help -- stop
		                                       * instead of dividing 0 by 2 forever
		                                       * (unsigned: 0/2 == 0, an infinite
		                                       * loop if malloc() never succeeds,
		                                       * e.g. malloc(0) returning NULL). */
			break;
		subghz_front_buffer_size /= 2;
	} // while ( true )

	while ( subghz_front_buffer )
	{
		subghz_ring_read_buffer = malloc(SUBGHZ_RAW_DATA_SAMPLES_TO_RW*2); // Each sample has a 2-byte value
		if ( !subghz_ring_read_buffer )
			break;
		subghz_sdcard_write_buffer = malloc(SUBGHZ_FORTMATTED_DATA_SAMPLES_TO_RW);
		if ( !subghz_sdcard_write_buffer )
			break;
		m1_ringbuffer_init(&subghz_rx_rawdata_rb, (uint8_t *)subghz_front_buffer, subghz_front_buffer_size, sizeof(uint16_t));

		M1_LOG_I(M1_LOGDB_TAG, "sub_ghz_ring_buffers_init %d\r\n", subghz_front_buffer_size);

		return 0;
	} // while ( subghz_front_buffer )

	sub_ghz_ring_buffers_deinit();
	return 1;
} // static uint8_t sub_ghz_ring_buffers_init(void)


/*============================================================================*/
/*
  * @brief  Initialize the radio to replay the recorded raw data
  * @param  None
  * @retval None
 */
/*============================================================================*/
static void sub_ghz_tx_raw_init(void)
{
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	GPIO_InitTypeDef gpio_init_struct = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_OC_InitTypeDef sConfigOC = {0};
	TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
	uint32_t tim_prescaler_val;

	if ( !subghz_tx_on_ext_gpio ) // SubGHz transmits on M1
	{
		subghz_tx_remap_port = SUBGHZ_TX_GPIO_PORT;
		subghz_tx_remap_pin = SUBGHZ_TX_GPIO_PIN;
		subghz_tx_remap_pin_alt = SUBGHZ_GPIO_AF_TX;
		subghz_tx_remap_timer = SUBGHZ_TX_CARRIER_TIMER;
		subghz_tx_remap_dma_req_timer = GPDMA1_REQUEST_TIM1_UP;
		subghz_tx_remap_timer_irq = SUBGHZ_TX_TIMER_IRQn;
		/*  Clock Configuration for TIMER */
		SUBGHZ_TX_TIMER_CLK();
	}
	else // SubGHz transmits on external GPIO
	{
		subghz_tx_remap_port = SUBGHZ_TX_GPIO_PORT_REMAP;
		subghz_tx_remap_pin = SUBGHZ_TX_GPIO_PIN_REMAP;
		subghz_tx_remap_pin_alt = SUBGHZ_GPIO_AF_TX_REMAP;
		subghz_tx_remap_timer = SUBGHZ_TX_CARRIER_TIMER_REMAP;
		subghz_tx_remap_dma_req_timer = GPDMA1_REQUEST_TIM8_UP;
		subghz_tx_remap_timer_irq = SUBGHZ_TX_TIMER_IRQn_REMAP;
		/*  Clock Configuration for TIMER */
		SUBGHZ_TX_TIMER_CLK_REMAP();
	}

	/* Pin configuration: output push-pull */
	gpio_init_struct.Pin = subghz_tx_remap_pin;
	gpio_init_struct.Mode = GPIO_MODE_AF_PP;
	gpio_init_struct.Pull = GPIO_PULLDOWN;//GPIO_NOPULL;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
	gpio_init_struct.Alternate = subghz_tx_remap_pin_alt;
	HAL_GPIO_Init(subghz_tx_remap_port, &gpio_init_struct);

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(subghz_tx_remap_port, subghz_tx_remap_pin, GPIO_PIN_RESET);

	/* Timer Clock */
	tim_prescaler_val = (uint32_t) (HAL_RCC_GetPCLK2Freq() / 1000000) - 1; // 1MHz

	timerhdl_subghz_tx.Instance = subghz_tx_remap_timer;
	timerhdl_subghz_tx.Init.Prescaler = tim_prescaler_val;
	timerhdl_subghz_tx.Init.CounterMode = TIM_COUNTERMODE_UP;
	timerhdl_subghz_tx.Init.Period = 0; // temporary value
	timerhdl_subghz_tx.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	timerhdl_subghz_tx.Init.RepetitionCounter = 0;
	timerhdl_subghz_tx.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_PWM_Init(&timerhdl_subghz_tx) != HAL_OK)
	{
		Error_Handler();
	}

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&timerhdl_subghz_tx, &sClockSourceConfig) != HAL_OK)
	{
		Error_Handler();
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&timerhdl_subghz_tx, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.OCNPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
	sConfigOC.Pulse = 0; // temporary value
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	if ( HAL_TIM_PWM_ConfigChannel(&timerhdl_subghz_tx, &sConfigOC, SUBGHZ_TX_TIMER_TX_CHANNEL) != HAL_OK)
	{
	    Error_Handler();
	}

	sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
	sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
	sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
	sBreakDeadTimeConfig.DeadTime = 0;
	sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
	sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
	sBreakDeadTimeConfig.BreakFilter = 0;
	sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
	sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
	sBreakDeadTimeConfig.Break2Filter = 0;
	sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
	sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
	if (HAL_TIMEx_ConfigBreakDeadTime(&timerhdl_subghz_tx, &sBreakDeadTimeConfig) != HAL_OK)
	{
		Error_Handler();
	}

	// Disable Output Compare 4 preload enable so that the CCR4 register will be updated immediately when its value changes
	//__HAL_TIM_DISABLE_OCxPRELOAD(&timerhdl_subghz_tx, SUBGHZ_TX_TIMER_TX_CHANNEL);

    /* Peripheral clock enable */
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    /* TIM1 DMA Init */
    /* GPDMA1_REQUEST_TIM1_UP Init */
    hdma_subghz_tx.Instance = GPDMA1_Channel0;
    hdma_subghz_tx.Init.Request = subghz_tx_remap_dma_req_timer;
    hdma_subghz_tx.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    hdma_subghz_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_subghz_tx.Init.SrcInc = DMA_SINC_INCREMENTED;
    hdma_subghz_tx.Init.DestInc = DMA_DINC_FIXED;
    hdma_subghz_tx.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
    hdma_subghz_tx.Init.DestDataWidth = DMA_DEST_DATAWIDTH_HALFWORD;
    hdma_subghz_tx.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    hdma_subghz_tx.Init.SrcBurstLength = 1;
    hdma_subghz_tx.Init.DestBurstLength = 1;
    hdma_subghz_tx.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
    hdma_subghz_tx.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hdma_subghz_tx.Init.Mode = DMA_NORMAL;

    if (HAL_DMA_Init(&hdma_subghz_tx) != HAL_OK)
    {
    	Error_Handler();
    }
    __HAL_LINKDMA(&timerhdl_subghz_tx, hdma[TIM_DMA_ID_UPDATE], hdma_subghz_tx);

    if (HAL_DMA_ConfigChannelAttributes(&hdma_subghz_tx, DMA_CHANNEL_NPRIV) != HAL_OK)
    {
    	Error_Handler();
    }

    /* GPDMA1 interrupt init */
	/* Clear all interrupt flags */
	__HAL_DMA_CLEAR_FLAG(&hdma_subghz_tx, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE
						| DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_SUSP | DMA_FLAG_TO);
	// IRQ priority should be lower than that of the Timer
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 5);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

	__HAL_TIM_ENABLE_DMA(&timerhdl_subghz_tx, TIM_DMA_UPDATE);
	/* Enable TIM Update Event Interrupt Request */
	__HAL_TIM_ENABLE_IT(&timerhdl_subghz_tx, TIM_FLAG_UPDATE);

	//__HAL_TIM_CLEAR_FLAG(&timerhdl_subghz_tx, TIM_FLAG_UPDATE);
	timerhdl_subghz_tx.Instance->SR = 0x00; // Clear all flags
	/* Peripheral interrupt init */
	HAL_NVIC_SetPriority(subghz_tx_remap_timer_irq, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1, 0); //!
	HAL_NVIC_ClearPendingIRQ(subghz_tx_remap_timer_irq);
	HAL_NVIC_EnableIRQ(subghz_tx_remap_timer_irq);
} // static void sub_ghz_tx_raw_init(void)



/*============================================================================*/
/**
  * @brief  De-initialize buffers
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_ring_buffers_deinit(void)
{
	if ( subghz_front_buffer )
	{
		free(subghz_front_buffer);
		subghz_front_buffer = NULL;
		subghz_front_buffer_size = 0;
	} // if ( subghz_front_buffer )

	if ( subghz_ring_read_buffer )
	{
		free(subghz_ring_read_buffer);
		subghz_ring_read_buffer = NULL;
	} // if ( subghz_ring_read_buffer )

	if ( subghz_sdcard_write_buffer )
	{
		free(subghz_sdcard_write_buffer);
		subghz_sdcard_write_buffer = NULL;
	}
	subghz_front_buffer_size = 0;
	subghz_record_mode_flag = false;
	M1_LOG_I(M1_LOGDB_TAG, "sub_ghz_ring_buffers_deinit %d\r\n", subghz_back_buffer_size);
} // static void sub_ghz_ring_buffers_deinit(void)


/*============================================================================*/
/**
  * @brief  De-initializes the peripherals (RCC,GPIO, TIM)
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_tx_raw_deinit(void)
{
	GPIO_InitTypeDef gpio_init_struct = {0};

	/* Pin configuration: output push-pull */
	gpio_init_struct.Pin = subghz_tx_remap_pin;
	gpio_init_struct.Mode = GPIO_MODE_ANALOG;
	gpio_init_struct.Pull = GPIO_PULLUP;
	gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(subghz_tx_remap_port, &gpio_init_struct);

	//HAL_DMA_Abort_IT();
	HAL_NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);
	if ( hdma_subghz_tx.Instance != NULL )
	{
		HAL_DMA_DeInit(&hdma_subghz_tx);
		__HAL_DMA_DISABLE_IT(&hdma_subghz_tx, (DMA_IT_TC | DMA_IT_DTE | DMA_IT_ULE | DMA_IT_USE | DMA_IT_TO));
	} // if ( hdma_subghz_tx.Instance!=NULL )

	if ( timerhdl_subghz_tx.Instance != NULL )
	{
		HAL_TIMEx_PWMN_Stop(&timerhdl_subghz_tx, SUBGHZ_TX_TIMER_TX_CHANNEL);
		__HAL_TIM_DISABLE_DMA(&timerhdl_subghz_tx, TIM_DMA_UPDATE);
		__HAL_TIM_DISABLE_IT(&timerhdl_subghz_tx, TIM_FLAG_UPDATE);
	} // if ( timerhdl_subghz_tx.Instance != NULL )

	if ( !subghz_tx_on_ext_gpio )
	{
		SUBGHZ_TX_TIMER_CLK_DIS();
	}
	else
	{
		SUBGHZ_TX_TIMER_CLK_DIS_REMAP();
	}
	HAL_NVIC_DisableIRQ(subghz_tx_remap_timer_irq);

	//SI446x_Set_Tx_Power(12);
	sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);

	if ( main_q_hdl != NULL )
		xQueueReset(main_q_hdl);
} // static void sub_ghz_tx_raw_deinit(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_raw_tx_stop(void)
{
	// Stop DMA
	__HAL_DMA_DISABLE(&hdma_subghz_tx);

	// Stop timer
	//__HAL_TIM_DISABLE(&timerhdl_subghz_tx);
	timerhdl_subghz_tx.Instance->CR1 &= ~(TIM_CR1_CEN);
	//hdma_subghz_tx.Instance->CCR &= ~DMA_CCR_EN;
	/* Check if the DMA channel is effectively disabled */
	while ( (hdma_subghz_tx.Instance->CCR & DMA_CCR_EN) != 0U )
		;
	__HAL_TIM_DISABLE_DMA(&timerhdl_subghz_tx, TIM_DMA_UPDATE);
	// Clear the update interrupt flag
	__HAL_TIM_CLEAR_FLAG(&timerhdl_subghz_tx, TIM_FLAG_UPDATE);
} // static void sub_ghz_raw_tx_stop(void)


/*============================================================================*/
/**
  * @brief This function allocates memory for data buffers, reads data file header and checks its format
  * @param  None
  * @retval error code
  */
/*============================================================================*/
static uint8_t sub_ghz_raw_samples_init(void)
{
	uint16_t sdcard_read_result;
	uint8_t i, error, key_len, *token;
	uint8_t *psdcard_dat_buffer = NULL;

	do
	{
		error = m1_sdm_get_logging_error();
		if ( error )
			break;
		if ( sdcard_dat_buffer == NULL ) /* avoid leak: a prior failed attempt (no matching
		                                  * deinit call on this view's failure/back-out path)
		                                  * may already hold this buffer -- reuse it instead
		                                  * of orphaning it with a fresh allocation. Mirrors
		                                  * sub_ghz_ring_buffers_init()'s own "route2" guard. */
		{
			sdcard_dat_buffer = m1_malloc(M1_SDM_MIN_BUFFER_SIZE);
			if (sdcard_dat_buffer==NULL)
			{
				error = 1;
				break;
			}
			sdcard_dat_buffer += M1_SDM_MIN_BUFFER_SIZE/2; // Start at the middle of the buffer
		}
		sdcard_dat_read_size = M1_SDM_MIN_BUFFER_SIZE/4; // Limit the reading size from SD card to avoid data error
		if ( subghz_back_buffer == NULL ) /* same reuse-not-reallocate guard as above */
		{
			subghz_back_buffer_size = SUBGHZ_RAW_DATA_SAMPLES_MAX;
			while ( true )
			{
				subghz_back_buffer = malloc(subghz_back_buffer_size*sizeof(uint16_t));
				if ( subghz_back_buffer )
					break;
				if ( subghz_back_buffer_size <= 1U ) /* stop instead of dividing 0 by 2
				                                      * forever -- see the identical
				                                      * guard in sub_ghz_ring_buffers_init() */
					break;
				subghz_back_buffer_size /= 2;
			} // while ( true )
			if ( !subghz_back_buffer )
			{
				error = 1;
				break;
			}
		}
		M1_LOG_D(M1_LOGDB_TAG, "sub_ghz_raw_samples_init %d\r\n", subghz_back_buffer_size);
		raw_samples_buffer_size = (subghz_back_buffer_size < subghz_front_buffer_size)?subghz_back_buffer_size:subghz_front_buffer_size;
		double_buffer_ptr[0] = subghz_front_buffer;
		double_buffer_ptr[1] = subghz_back_buffer;

		error = m1_fb_open_file(&datfile_info.dat_file_hdl, datfile_info.dat_filename);
		M1_LOG_I(M1_LOGDB_TAG, "RS: open=%d path='%s'\r\n", error, datfile_info.dat_filename);
		if (error)
			break;

		sdcard_dat_file_size = f_size(&datfile_info.dat_file_hdl);
		if ( sdcard_dat_read_size > sdcard_dat_file_size )
			sdcard_dat_read_size = sdcard_dat_file_size;
		sdcard_read_result = m1_fb_read_from_file(&datfile_info.dat_file_hdl, sdcard_dat_buffer, sdcard_dat_read_size);
		if ( sdcard_read_result!=sdcard_dat_read_size )
		{
			error = 1;
			break;
		}
		sdcard_dat_buffer[sdcard_dat_read_size] = '\0'; // Add end of string to the buffer
		M1_LOG_I(M1_LOGDB_TAG, "RS: size=%lu read=%u/%u hdr0='%.48s'\r\n", (unsigned long)sdcard_dat_file_size, sdcard_read_result, sdcard_dat_read_size, sdcard_dat_buffer);
		sdcard_buffer_run_ptr = sdcard_dat_buffer;

		psdcard_dat_buffer = malloc(sdcard_dat_read_size + 1);
		if ( psdcard_dat_buffer==NULL )
		{
			error = 1;
			break;
		}
		memcpy(psdcard_dat_buffer, sdcard_dat_buffer, sdcard_dat_read_size + 1); // Duplicate this file header

		key_len = SUB_GHZ_DATAFILE_KEY_FORMAT_N;
		i = 0;
		token = strtok(psdcard_dat_buffer, "\r\n"); // Tokenize this duplicated buffer
		if ( token != NULL && strstr(token, subghz_datfile_keywords[i]) )
		{
			if ( strstr(token, SUB_GHZ_DATAFILE_FILETYPE_NOISE) )
				key_len = SUB_GHZ_DATAFILE_RAW_FORMAT_N;
			else if ( strstr(token, SUB_GHZ_DATAFILE_FILETYPE_PACKET) )
				key_len = SUB_GHZ_DATAFILE_KEY_FORMAT_N;
			else
				token = NULL; // Unknown format
		} // if ( strstr(token, subghz_datfile_keywords[i]) )
		else
		{
			token = NULL; // Terminate
		}

		while ( token!=NULL )
		{
			if ( strstr(token, subghz_datfile_keywords[i])==NULL )
				break;
			sdcard_buffer_run_ptr += strlen(token) + 2; // 2 for "\r\n"
			if ( ++i >= key_len )
				break;
			token = strtok(NULL, "\r\n");
		} // while ( token!=NULL )
		if ( i < key_len )
		{
			error = 1;
			break;
		} // if ( i < SUB_GHZ_DATAFILE_KEY_FORMAT_N )

		sdcard_dat_buffer_end_pos = (uint32_t)sdcard_dat_buffer + sdcard_dat_read_size;

	} while(0); // while (0)

	if ( psdcard_dat_buffer!=NULL )
		free(psdcard_dat_buffer);

	return error;

} // static uint8_t sub_ghz_raw_samples_init(void)


/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
static void sub_ghz_raw_samples_deinit(bool discard_samples)
{
	if ( subghz_back_buffer )
	{
		free(subghz_back_buffer);
		subghz_back_buffer = NULL;
	}
	subghz_back_buffer_size = 0;
	if ( sdcard_dat_buffer )
	{
		sdcard_dat_buffer -= M1_SDM_MIN_BUFFER_SIZE/2; // Restore the original allocated address of the buffer
		free(sdcard_dat_buffer);
		sdcard_dat_buffer = NULL;
	}
	m1_fb_close_file(&datfile_info.dat_file_hdl);
	memset(&datfile_info.dat_file_hdl, 0, sizeof(datfile_info.dat_file_hdl));
	sdcard_buffer_run_ptr = NULL;
	sdcard_dat_read_size = 0;
	sdcard_dat_file_size = 0;
	sdcard_dat_buffer_end_pos = 0;
	raw_samples_buffer_size = 0;
	double_buffer_ptr[0] = NULL;
	double_buffer_ptr[1] = NULL;
	if (discard_samples)
		m1_fb_delete_file(datfile_info.dat_filename);

	M1_LOG_D(M1_LOGDB_TAG, "sub_ghz_raw_samples_deinit %d\r\n", subghz_back_buffer_size);
} // static void sub_ghz_raw_samples_deinit(bool discard_samples)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static uint8_t sub_ghz_parse_raw_data(uint8_t buffer_ptr_id)
{
	char *token, *endptr;
	uint8_t error_code, crlf;
	uint16_t rd_samples_count;
	uint16_t sdcard_read_result, number;

	rd_samples_count = 0;
	raw_samples_count = 0;
	error_code = 0;
	crlf = 0;

	while ( true )
	{
		token = strstr(sdcard_buffer_run_ptr, SUB_GHZ_KEYWORD_DELIMITER); // Find next numbers
		if ( token==NULL )
		{
			token = strstr(sdcard_buffer_run_ptr, SUB_GHZ_KEYWORD_CRLF); // Find end of current data block
			do
			{
				if (token==NULL) // Possibly last few characters of this buffer
				{
					if ( (sdcard_buffer_run_ptr + 10) < sdcard_dat_buffer_end_pos ) // Remaining data is too long. Not valid!
					{
						error_code = SUB_GHZ_RAW_DATA_PARSER_ERROR_L1;
						M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L1 no-delim/no-crlf, remain=%lu head='%.16s'\r\n",
								(unsigned long)(sdcard_dat_buffer_end_pos - (uint32_t)sdcard_buffer_run_ptr),
								(char *)sdcard_buffer_run_ptr); // TEMP DIAG
						break;
					} // if ( (sdcard_buffer_run_ptr + 10) < sdcard_dat_buffer_end_pos )
					sdcard_dat_buffer_end_pos -= (uint32_t)sdcard_buffer_run_ptr; // Remaining characters
					if ( sdcard_dat_buffer_end_pos ) // Are there remaining characters?
					{
						// If the write buffer address is not aligned, the data read from SDcard may be corrupted randomly when writing.
						// So if there're remaining characters, let copy the last few characters to before the beginning of the aligned buffer
						sdcard_dat_buffer -= sdcard_dat_buffer_end_pos; // Move backward a few positions
						memcpy(sdcard_dat_buffer, sdcard_buffer_run_ptr, sdcard_dat_buffer_end_pos);
						sdcard_dat_buffer += sdcard_dat_buffer_end_pos; // Restore
					}
					sdcard_dat_file_size -= sdcard_dat_read_size; // Update the remainder
					if ( sdcard_dat_file_size==0 ) // End of file?
					{
						error_code = SUB_GHZ_RAW_DATA_PARSER_COMPLETE;
						if ( sdcard_dat_buffer_end_pos ) // Last number?
						{
							number = strtol((char *)sdcard_buffer_run_ptr, &endptr, 10);
							if ( number!=0 ) // Valid number?
								double_buffer_ptr[buffer_ptr_id][raw_samples_count++] = number;
						} // if ( sdcard_dat_buffer_end_pos )
					} // if ( sdcard_dat_file_size==0 )
					else
					{
						if ( sdcard_dat_read_size > sdcard_dat_file_size ) // Last block to read from file?
							sdcard_dat_read_size = sdcard_dat_file_size; // Adjust the read size
						sdcard_read_result = m1_fb_read_from_file(&datfile_info.dat_file_hdl, sdcard_dat_buffer, sdcard_dat_read_size);
						if ( sdcard_read_result!=sdcard_dat_read_size )
						{
							error_code = SUB_GHZ_RAW_DATA_PARSER_ERROR_L2;
							M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L2 read=%u/%u\r\n",
									sdcard_read_result, sdcard_dat_read_size); // TEMP DIAG
							break;
						}
						sdcard_buffer_run_ptr = sdcard_dat_buffer; // Update moving buffer pointer
						sdcard_buffer_run_ptr -= sdcard_dat_buffer_end_pos; // Update address for remaining characters, if any
						sdcard_dat_buffer_end_pos = (uint32_t)sdcard_dat_buffer; // Update moving buffer pointer
						sdcard_dat_buffer_end_pos += sdcard_dat_read_size; // Update buffer end index for this data block
						sdcard_dat_buffer[sdcard_dat_read_size] = '\0'; // Add end of string to the buffer
						token = strstr(sdcard_buffer_run_ptr, SUB_GHZ_KEYWORD_DELIMITER); // Find next numbers
						if ( token==NULL ) // Possibly error.
						{
							token = strstr(sdcard_buffer_run_ptr, SUB_GHZ_KEYWORD_CRLF); // Find end of current data block
							if ( token==NULL ) // Real error
							{
								error_code = SUB_GHZ_RAW_DATA_PARSER_ERROR_L3;
								M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L3 next chunk has no delim/crlf head='%.16s'\r\n",
										(char *)sdcard_buffer_run_ptr); // TEMP DIAG
							}
							else
								crlf = 1; // CRLF found, likely reaching end of file
							break;
						} // if ( token==NULL )
					} // else
				} // if (token==NULL)
				else
				{
					crlf = 1; // Carriage return and Line feed is found
				} // else
			} while (0);

			if ( error_code )
				break;
		} // if ( token==NULL )

		*token = '\0'; // Add end of string to replace the delimiter at the end
		token = sdcard_buffer_run_ptr;
		sdcard_buffer_run_ptr += strlen(token) + 1 + crlf; // Move read pointer to the next data item
		crlf = 0; // Reset
		if (*token=='\0') // Extra delimiter found?
			continue; // Skip it
		if (*token=='+' || *token=='-')
			*token = '0'; // Replace the +/- sign before a number with 0, if any
		number = strtol(token, &endptr, 10);
		if ( *endptr != '\0' ) // Possibly an invalid number?
		{
			error_code = SUB_GHZ_RAW_DATA_PARSER_ERROR_L4;
			if ( endptr > token ) // Data of this token: number_xxx_delimiter
			{
				if ( m1_datfile_keywords_check(endptr, SUB_GHZ_KEYWORD_CRLF SUB_GHZ_DATAFILE_DATA_KEYWORD, '\0') ) // Not delimiter_keyword?
				{
					M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L4a bad token='%s' tail='%.16s'\r\n", token, endptr); // TEMP DIAG
					break; // Error
				}
				if ( endptr[strlen(SUB_GHZ_KEYWORD_CRLF SUB_GHZ_DATAFILE_DATA_KEYWORD)] != '\0' )
				{
					M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L4b trailing after Data-kw tail='%.16s'\r\n", endptr); // TEMP DIAG
					break;
				}
				error_code = 0; // Get the number
			} // if ( endptr > token )
			else
			{
				if ( m1_datfile_keywords_check(endptr, SUB_GHZ_DATAFILE_DATA_KEYWORD, '\0') ) // Not keyword?
				{
					M1_LOG_E(M1_LOGDB_TAG, "PR: ERROR_L4c non-keyword token='%s' tail='%.16s'\r\n", token, endptr); // TEMP DIAG
					break; // Error
				} // if ( m1_datfile_keywords_check(endptr, SUB_GHZ_DATAFILE_DATA_KEYWORD, '\0') )
				error_code = 0; // Nothing to save
				continue; // Process next data
			} // else
		} // if ( *endptr != '\0' )

		rd_samples_count++;
		if ( rd_samples_count >= SUBGHZ_RAW_DATA_SAMPLES_TO_RW )
		{
			rd_samples_count = 0; // reset
		}
		double_buffer_ptr[buffer_ptr_id][raw_samples_count++] = number;
		if ( raw_samples_count >= raw_samples_buffer_size )
		{
			error_code = SUB_GHZ_RAW_DATA_PARSER_READY;
			break;
			// Start transmitting here,
			// and continue to fill the other tx buffer
		} // if ( raw_samples_count >= raw_samples_buffer_size )
	} // while (true)

	M1_LOG_I(M1_LOGDB_TAG, "PR: RETURN code=0x%02X raw_samples=%d rd=%d\r\n",
			error_code, raw_samples_count, rd_samples_count); // TEMP DIAG
	return error_code;
} // static uint8_t sub_ghz_parse_raw_data(uint8_t buffer_ptr_id)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_transmit_raw(uint32_t source, uint32_t dest, uint32_t len, uint8_t repeat)
{
	if ( source==0 )
		return;

	if ( dest==0 )
		return;

	if ( len==0 )
		return;

	//len &= ~(uint16_t)1; // Make length an even number. DMA doesn't work with odd length for unknown reason!
	// Save these data for repeat
	subghz_decenc_ctl.ntx_raw_len = (len<<1); // Convert length (16-bit blocks) to byte
	subghz_decenc_ctl.ntx_raw_src = source;
	subghz_decenc_ctl.ntx_raw_dest = dest;
	subghz_decenc_ctl.ntx_raw_repeat = repeat;
	subghz_tx_tc_flag = 0;

	/* Enable the DMA channel */
	/**
	  * @brief  Start the DMA data transfer.
	  * @param  hdma DMA handle
	  * @param  src      : The source memory Buffer address.
	  * @param  dst      : The destination memory Buffer address.
	  * @param  length   : The size of a source block transfer in byte.
	  * @retval HAL status
	  */
	HAL_StatusTypeDef ret = TIM_DMA_Start_IT(timerhdl_subghz_tx.hdma[TIM_DMA_ID_UPDATE], source, dest, subghz_decenc_ctl.ntx_raw_len);
	if ( ret != HAL_OK)
		return;

	//__HAL_TIM_URS_ENABLE(&timerhdl_subghz_tx); // Enable URS to temporarily disable the UIF when the UG bit is set
	//	timerhdl_subghz_tx.Instance->ARR = test_protocol[0]; // First pulse for Update Auto Reload Register ARR value
	// Generate Update Event (set UG bit) to reload the DMA source data[0] to the ARR register
	HAL_TIM_GenerateEvent(&timerhdl_subghz_tx, TIM_EVENTSOURCE_UPDATE);
	// Do it again to reload the DMA source data[1] to the ARR register, and reload the DMA source data[0] to the ARR shadow register
	HAL_TIM_GenerateEvent(&timerhdl_subghz_tx, TIM_EVENTSOURCE_UPDATE);
	//__HAL_TIM_URS_DISABLE(&timerhdl_subghz_tx); // Disable URS to enable the UIF again
	//	timerhdl_subghz_tx.Instance->ARR = test_protocol[1]; // Save the ARR for the next bit

	timerhdl_subghz_tx.Instance->CCR4 = 0; // initial value
	subghz_decenc_ctl.ntx_raw_len = hdma_subghz_tx.Instance->CBR1; // Update the remainder

    // Start the timer
	//HAL_TIM_Base_Start_DMA(&timerhdl_subghz_tx, &test_protocol, sizeof(test_protocol));
	HAL_TIMEx_PWMN_Start(&timerhdl_subghz_tx, SUBGHZ_TX_TIMER_TX_CHANNEL);
} // static void sub_ghz_transmit_raw(uint32_t source, uint32_t dest, uint32_t len, uint8_t repeat)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_transmit_raw_restart(uint32_t source, uint32_t len)
{
	sub_ghz_raw_tx_stop();

	if ( (source!=0) && (len!=0) ) // New data source and length?
	{
		//len &= ~(uint16_t)1; // Make length an even number. DMA doesn't work with odd length for unknown reason!
		subghz_decenc_ctl.ntx_raw_len = (len<<1); // Convert length (16-bit blocks) to byte
			subghz_decenc_ctl.ntx_raw_src = source;
	} // if ( (source!=0) && (len!=0) )
	/* Enable the DMA channel */
	/**
	  * @brief  Start the DMA data transfer.
	  * @param  hdma DMA handle
	  * @param  src      : The source memory Buffer address.
	  * @param  dst      : The destination memory Buffer address.
	  * @param  length   : The size of a source block transfer in byte.
	  * @retval HAL status
	  */

	__HAL_TIM_ENABLE_DMA(&timerhdl_subghz_tx, TIM_DMA_UPDATE);

	MODIFY_REG(hdma_subghz_tx.Instance->CBR1, DMA_CBR1_BNDT, (subghz_decenc_ctl.ntx_raw_len & DMA_CBR1_BNDT));
	/* Clear all interrupt flags */
	__HAL_DMA_CLEAR_FLAG(&hdma_subghz_tx, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE
						| DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_SUSP | DMA_FLAG_TO);
	// Configure DMA channel source address
	hdma_subghz_tx.Instance->CSAR = subghz_decenc_ctl.ntx_raw_src;
	// Configure DMA channel destination address
	hdma_subghz_tx.Instance->CDAR = subghz_decenc_ctl.ntx_raw_dest;

    /* Enable common interrupts: Transfer Complete and Transfer Errors ITs */
	__HAL_DMA_ENABLE(&hdma_subghz_tx);

	/* Temporarily disable the complementary PWM output  */
	//__HAL_TIM_MOE_DISABLE(&timerhdl_subghz_tx);

	// Generate Update Event (set UG bit) to reload the DMA source data[0] to the ARR register
	HAL_TIM_GenerateEvent(&timerhdl_subghz_tx, TIM_EVENTSOURCE_UPDATE);
	// Do it again to reload the DMA source data[1] to the ARR register, and reload the DMA source data[0] to the ARR shadow register
	HAL_TIM_GenerateEvent(&timerhdl_subghz_tx, TIM_EVENTSOURCE_UPDATE);

	timerhdl_subghz_tx.Instance->CCR4 = 0; // initial value
	subghz_decenc_ctl.ntx_raw_len = hdma_subghz_tx.Instance->CBR1; // Update the remainder

	/* Enable the complementary PWM output  */
	//__HAL_TIM_MOE_ENABLE(&timerhdl_subghz_tx);

	// Start the timer
	__HAL_TIM_ENABLE(&timerhdl_subghz_tx);
} // static void sub_ghz_transmit_raw_restart(uint32_t source, uint32_t len)




/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static uint16_t sub_ghz_rx_raw_save(bool header_init, bool last_data)
{
	char *prn_buffer;
	uint32_t freq32;
	uint16_t count, n_samples_to_rw, *pdata;
	char *sign_text[2] = {"+", ""};
	uint8_t *pfillbuffer;
	uint8_t sign;

	prn_buffer = malloc(64);
	/* assert() compiles out under NDEBUG (this project's real ARM release
	 * build passes -DNDEBUG -- confirmed via compile_commands.json), so it
	 * was never a real safety net here; a failed allocation would fall
	 * straight into sprintf(prn_buffer, ...) on a NULL pointer below. This
	 * runs repeatedly during an active recording (not boot-time) -- report
	 * "0 samples flushed this call" rather than crash, matching the
	 * meaning this function's own header_init branch already gives that
	 * same return value. */
	if ( prn_buffer == NULL )
		return 0;
	pfillbuffer = subghz_sdcard_write_buffer;
	if ( header_init )
	{
		sprintf(pfillbuffer, "%s M1 SubGHz %s\r\n", subghz_datfile_keywords[0], SUB_GHZ_DATAFILE_FILETYPE_KEYWORD);
		sprintf(prn_buffer, "%s %d.%d\r\n", subghz_datfile_keywords[1], m1_device_stat.config.fw_version_major, m1_device_stat.config.fw_version_minor);
		strcat(pfillbuffer, prn_buffer);
		freq32 = subghz_rec_freqs[subghz_record_freq_idx].hz; // EXACT selected frequency (integer Hz)
		sprintf(prn_buffer, "%s %lu\r\n", subghz_datfile_keywords[2], freq32);
		strcat(pfillbuffer, prn_buffer);
		sprintf(prn_buffer, "%s %s\r\n", subghz_datfile_keywords[3], subghz_modulation_text[subghz_scan_config.modulation]);
		strcat(pfillbuffer, prn_buffer);
		m1_sdm_fill_buffer(pfillbuffer, strlen(pfillbuffer));
		free(prn_buffer); /* this early return skipped the free() at the bottom
		                   * of the function entirely -- leaked 64 bytes once
		                   * per RAW recording session (this header_init branch
		                   * runs exactly once per recording). */
		return 0;
	} // if ( header_init )

	sprintf(pfillbuffer, "%s", SUB_GHZ_DATAFILE_DATA_KEYWORD);
	//m1_test_gpio_pull_high();
	//sub_ghz_rx_pause();
	n_samples_to_rw = SUBGHZ_RAW_DATA_SAMPLES_TO_RW;
	if ( last_data )
	{
		n_samples_to_rw = ringbuffer_get_data_slots(&subghz_rx_rawdata_rb);
		if ( n_samples_to_rw > SUBGHZ_RAW_DATA_SAMPLES_TO_RW ) // This should never happen!
			n_samples_to_rw = SUBGHZ_RAW_DATA_SAMPLES_TO_RW;
	} // if ( last_data )
	n_samples_to_rw = m1_ringbuffer_read(&subghz_rx_rawdata_rb, subghz_ring_read_buffer, n_samples_to_rw);
	//sub_ghz_rx_start();
	//m1_test_gpio_pull_low();
	pdata = (uint16_t *)subghz_ring_read_buffer;
	if ( n_samples_to_rw & 0x01 ) // Odd number of samples?
	{
		pdata[n_samples_to_rw] = INTERPACKET_GAP_MIN; // Extra dummy data
		n_samples_to_rw++; // Make even number
	}
	sign = 0;
	for (count=0; count<n_samples_to_rw; count++)
	{
		sprintf(prn_buffer, " %s%u", sign_text[sign], *pdata);
		strcat(pfillbuffer, prn_buffer);
		pdata++;
		sign ^= 1;
	}
	strcat(pfillbuffer, "\r\n");

	m1_sdm_fill_buffer(pfillbuffer, strlen(pfillbuffer));

	if ( prn_buffer!=NULL )
		free(prn_buffer);
	/* Was `return (uint8_t)n_samples_to_rw;` -- SUBGHZ_RAW_DATA_SAMPLES_TO_RW
	 * is 512, so every non-final flush (always exactly that many samples)
	 * silently truncated to 0 (512 & 0xFF), meaning
	 * subghz_record_flushed_samples (the GUI progress counter, ~line 357)
	 * never advanced during a long recording's intermediate flushes.
	 * n_samples_to_rw is already uint16_t; returning it directly needs no
	 * cast now that this function's own return type matches. */
	return n_samples_to_rw; // ADAPTER: return flushed sample count (base left this undefined; GUI needs it)
} // static uint16_t sub_ghz_rx_raw_save(bool header_init, bool last_data)



/*============================================================================*/
/**
  * @brief Init the raw samples for replay
  * @param  None
  * @retval Error code
  */
/*============================================================================*/
static uint8_t sub_ghz_raw_replay_init(void)
{
	uint8_t ret_code;

	sub_ghz_raw_samples_deinit(false);
	ret_code = sub_ghz_raw_samples_init();
	while ( !ret_code )
	{
		ret_code = sub_ghz_parse_raw_data(0);
		if ( ret_code & SUB_GHZ_RAW_DATA_PARSER_ERROR_MASK )
		{
			ret_code = 1; // Change to common error code
			break;
		}
		sub_ghz_transmit_raw_restart((uint32_t)double_buffer_ptr[0], raw_samples_count);
		if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_READY ) // There're more samples to read?
			ret_code = sub_ghz_parse_raw_data(1);
		else // COMPLETE
			ret_code = SUB_GHZ_RAW_DATA_PARSER_STOPPED; // No more sample
		break;
	} // while ( !ret_code )

	return ret_code;
} // static uint8_t sub_ghz_raw_replay_init(void)



/*============================================================================*/
/**
  * @brief Play recorded samples or samples from SD card
  * @param record_mode true if the record mode is active
  * @retval Error code
  */
/*============================================================================*/
static uint8_t sub_ghz_replay_start(bool record_mode, S_M1_SubGHz_Band band, uint8_t channel, uint8_t power)
{
	uint8_t ret_code = 0;

	(void)band; // frequency now comes from subghz_replay_hz (exact Hz), not the legacy band enum

	/* Regional TX permission is enforced centrally at the OPMODE_TX boundary
	 * (sub_ghz_set_opmode_hz); the refusal is handled where TX is requested
	 * below. No region check here, so this path never affects the record/RX
	 * setup for a frequency that is receivable but not transmittable. */

	if ( record_mode )
	{
		sub_ghz_rx_deinit();
		m1_sdm_task_stop(); // Stop sampling raw data and flush data to SD card and then close file
		ret_code = sub_ghz_raw_samples_init();
	} // if ( record_mode )
	while ( !ret_code )
	{
		ret_code = sub_ghz_parse_raw_data(0);
		M1_LOG_I(M1_LOGDB_TAG, "RP: parse0 ret=0x%02X samples=%d record=%d\r\n",
				ret_code, raw_samples_count, record_mode); // TEMP DIAG
		if ( ret_code & SUB_GHZ_RAW_DATA_PARSER_ERROR_MASK )
		{
			M1_LOG_E(M1_LOGDB_TAG, "RP: BRANCH=PARSE_ERROR code=0x%02X\r\n", ret_code); // TEMP DIAG
			ret_code = 1; // Change to common error code
			break;
		}
		if ( raw_samples_count==0 ) // No samples found in Record mode or Replay mode?
		{
			if ( record_mode ) // Try to replay in Record mode?
			{
				raw_samples_count = 10; // Just play dummy data
			}
			else
			{
				M1_LOG_E(M1_LOGDB_TAG, "RP: BRANCH=ZERO_SAMPLES (file replay)\r\n"); // TEMP DIAG
				ret_code = 1; // Change to common error code
				break;
			}
		} // if ( raw_samples_count==0 )
		M1_LOG_I(M1_LOGDB_TAG, "sub_ghz_replay_start: %d samples\r\n", raw_samples_count);
		sub_ghz_tx_raw_init();
		/* Tune TX to the EXACT frequency (record: recorded Hz; file: loaded Hz).
		 * A region refusal (TX not permitted here) or a tuning failure aborts the
		 * replay rather than faking it; the SI4463 is never placed in TX. */
		uint8_t tx_rc = sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_TX, subghz_replay_hz, channel, power);
		if ( tx_rc != RADIO_TUNE_OK )
		{
			if ( tx_rc == RADIO_TX_REGION_BLOCKED )
				subghz_tx_region_blocked_msg();
			M1_LOG_E(M1_LOGDB_TAG, "RP: BRANCH=TX_REFUSED hz=%lu code=%u\r\n",
					(unsigned long)subghz_replay_hz, (unsigned)tx_rc);
			ret_code = 1;
			break;
		}
		sub_ghz_transmit_raw((uint32_t)double_buffer_ptr[0], (uint32_t)&timerhdl_subghz_tx.Instance->ARR, raw_samples_count, SUBGHZ_TX_RAW_REPLAY_REPEAT_DEFAULT);
		if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_READY ) // There're more samples to read?
			ret_code = sub_ghz_parse_raw_data(1);
		else // COMPLETE
			ret_code = SUB_GHZ_RAW_DATA_PARSER_STOPPED; // No more sample
		break;
	} // while ( !ret_code )
	if ( ret_code==1 )
	{
		sub_ghz_raw_samples_deinit(record_mode);
		ret_code = 0; // Reset
	} // if ( ret_code==1 )

	M1_LOG_I(M1_LOGDB_TAG, "RP: RESULT ret=0x%02X (0=SYS_ERROR/File err, nonzero=OK)\r\n", ret_code); // TEMP DIAG
	return ret_code;
} // static uint8_t sub_ghz_replay_start(bool record_mode, S_M1_SubGHz_Band band, uint8_t channel, uint8_t power)



/*============================================================================*/
/**
  * @brief Continue to replay the samples, and update its status
  * @param
  * @retval Error code
  */
/*============================================================================*/
static uint8_t sub_ghz_replay_continue(uint8_t ret_code_in)
{
	uint8_t ret_code;

	ret_code = ret_code_in;
	if ( subghz_replay_cancel ) // cancellation honored before any TX buffer refill
	{
		sub_ghz_raw_tx_stop();
		sub_ghz_raw_samples_deinit(false);
		sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
		subghz_decenc_ctl.ntx_raw_repeat = 0;
		return SUB_GHZ_RAW_DATA_PARSER_IDLE;
	}
	M1_LOG_I(M1_LOGDB_TAG, "TX: chunk buf=%d ret=%d\r\n", double_buffer_ptr_id, ret_code);
	switch (ret_code)
	{
		case SUB_GHZ_RAW_DATA_PARSER_READY:
		case SUB_GHZ_RAW_DATA_PARSER_COMPLETE:
			sub_ghz_transmit_raw_restart((uint32_t)double_buffer_ptr[double_buffer_ptr_id], raw_samples_count);
			double_buffer_ptr_id ^= 1; // Update raw samples buffer
			if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_READY )
				ret_code = sub_ghz_parse_raw_data(double_buffer_ptr_id);
			else if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_COMPLETE )
				ret_code = SUB_GHZ_RAW_DATA_PARSER_STOPPED;
			break;

		case SUB_GHZ_RAW_DATA_PARSER_ERROR_MASK:
			ret_code = SUB_GHZ_RAW_DATA_PARSER_IDLE;
			subghz_decenc_ctl.ntx_raw_repeat = 0; // Force to stop
			break;

		case SUB_GHZ_RAW_DATA_PARSER_STOPPED:
			if ( subghz_decenc_ctl.ntx_raw_repeat-- )
			{
				ret_code = sub_ghz_raw_replay_init();
				double_buffer_ptr_id = 1;
			} // if ( subghz_decenc_ctl.ntx_raw_repeat-- )
			else
			{
				ret_code = SUB_GHZ_RAW_DATA_PARSER_IDLE;
			}

			if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_IDLE )
			{
				sub_ghz_raw_tx_stop();
				sub_ghz_raw_samples_deinit(false);
				sub_ghz_set_opmode(SUB_GHZ_OPMODE_ISOLATED, SUB_GHZ_BAND_EOL, 0, 0);
				subghz_decenc_ctl.ntx_raw_repeat = 0;
			} // if ( ret_code==SUB_GHZ_RAW_DATA_PARSER_IDLE )
			break;

		default:
			break;
	} // switch (ret_code)

	return ret_code;

} // static uint8_t sub_ghz_replay_continue(uint8_t ret_code_in)









/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
static void sub_ghz_display_result(const char *name, uint64_t key, uint16_t bits)
{
    char hexString[64];
    if ( !bits ) return;
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, u8g2_font_resoledbold_tr);
    u8g2_DrawStr(&m1_u8g2, 2, 12, subghz_band_text[subghz_scan_config.band]);
    u8g2_DrawStr(&m1_u8g2, 66, 12, subghz_modulation_text[subghz_scan_config.modulation]);
    u8g2_DrawStr(&m1_u8g2, 2, 34, name);
    sprintf(hexString, "0x%lX  %dbit", (unsigned long)key, bits);
    u8g2_DrawStr(&m1_u8g2, 2, 52, hexString);
    m1_u8g2_nextpage();
}
