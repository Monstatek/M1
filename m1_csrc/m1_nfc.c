/* See COPYING.txt for license details. */

/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>   /* srand/rand for Add > Generate UID */
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_nfc.h"
#include "m1_storage.h"
#include "m1_sdcard.h"
#include "m1_sdcard_provision.h"
#include "m1_virtual_kb.h"
#include "app_x-cube-nfcx.h"
#include "uiView.h"
#include "m1_tasks.h"
#include "logger.h"
#include "legacy/nfc_driver.h"
#include "legacy/nfc_listener.h"
#include "legacy/nfc_poller.h"
#include "legacy/m1_desfire.h"   /* DESFire generation/size display decode */
#include "common/mfc_keys.h"     /* NFC Tools > MIFARE Classic Keys */
#include "common/mfc_keys_ui_model.h"   /* pure Keys-UI decision rules, host-tested */
#include "common/nfc_hex_utils.h"       /* nfc_hex_nibbles_to_bytes() -- contiguous-hex Add/UID entry */
#include "common/mfc_block_render.h"    /* mfc_block_format_rows() -- Data screen 2-byte-group formatting */
#include "common/ulc_keys.h"     /* NFC Tools > MIFARE Ultralight Keys (Ultralight C key mgmt) */
#include "common/ntag_pwd_keys.h" /* NFC Tools > NTAG/UL Password Dictionary (4-byte PWD mgmt) */
#include "nfc_poller.h"           /* nfc_poller_unlock_*, nfc_unlock_set_* (T2T Unlock) */
#include "common/nfc_add_image.h" /* Add: manual blank-template card builders */
#include "common/nfc_storage.h"
#include "common/nfc_ctx.h"
#include "common/m1_mfc_raw_session_hw.h" /* [RAW-TRACE] m1_mfc_raw_hw_active() in menu_nfc_deinit */
#include "m1_file_browser.h"
#include "m1_file_util.h"
#include "privateprofilestring.h"
#include "common/nfc_file.h"
#include "res_string.h"
#include "m1_read_icon.h"
#include "mfc_capture.h"
#include "mfc_detect.h"
#if defined(M1_MFC_RAW_EMULATION)
#include "m1_mfc_raw_listener.h"   /* raw MIFARE Classic emulation (Scope B) */
#include "m1_feedback_orchestration.h"
#include "m1_system.h"   /* m1_lcd_wake_restart_timer() for the successful-read info screen */
#endif

/*************************** D E F I N E S ************************************/
#define M1_LOGDB_TAG					"NFC"

/* ---------------------------------------------------------------------------
 * NFC action-menu emulation label
 *
 * This selects only the LABEL shown in the action menu. It is an intentional
 * UI decision and is deliberately DECOUPLED from the M1
 * backend, which today performs UID-only anticollision emulation for EVERY
 * family (g_emuA = UID/ATQA/SAK only; see nfc_ctx_sync_emu). Full protocol +
 * stored-data emulation is a later, separately hardware-tested parity stage;
 * until then "Emulate" is a parity label, not a backend capability claim.
 *   NFC_EMU_HIDE -> no emulation item
 *   NFC_EMU_UID  -> "Emulate UID" (UID-only fallback label)
 *   NFC_EMU_FULL -> "Emulate"     (full-emulation label) */
typedef enum {
    NFC_EMU_HIDE = 0,
    NFC_EMU_UID,
    NFC_EMU_FULL
} nfc_emu_cap_t;

/* Ordered action-menu item identities (label text is chosen at build time). */
typedef enum {
    NFC_ACT_SAVE = 0,
    NFC_ACT_EMULATE,
    NFC_ACT_WRITE,
    NFC_ACT_INFO,
    NFC_ACT_EDIT_UID,
    NFC_ACT_RENAME,
    NFC_ACT_DELETE,
    NFC_ACT_FIND_KEYS,   /* Find Missing Keys: dictionary-phase continuation of a partial MFC read */
    NFC_ACT_UNLOCK
} nfc_menu_action_t;

#define NFC_MENU_MAX_ITEMS   7

#define NFC_FILEPATH					"/nfc"
#define NFC_SAVE_DIR					M1_SD_DIR_NFC   /* drive-qualified save dir (== nfc_file.c DRIVE0_NFC) */
#define NFC_FILE_EXTENSION				".nfc"
#define NFC_FILE_EXTENSION_TMP			"nfc"  // For IsValidFileSpec (without dot)

#define CONCAT_FILEPATH_FILENAME(fpath, fname) fpath fname

#define NFC_WORKER_TASK_PRIORITY   		(tskIDLE_PRIORITY + 1)

#define NFC_INFO_LINES_PER_SCREEN   	5
/* One row narrower than a plain info screen: the DESFire Applications/Files
 * lists reserve the bottom 12px for the standard CENTER+"Select" footer bar
 * (target_10x10 icon, matching every other selectable-list screen in this
 * file, e.g. the MFC Sector list's "Details" footer). */
#define NFC_DESF_LIST_VISIBLE_ROWS   	(NFC_INFO_LINES_PER_SCREEN - 1)
#define RAW_PAGES_PER_ROW               2      /* Raw Data view: 2 T2T pages (8 bytes) per row */
#define RAW_PAGES_PER_SCREEN            (NFC_INFO_LINES_PER_SCREEN * RAW_PAGES_PER_ROW)  /* 10 pages/screen */

/* NFC typography standard = the M1 main-menu fonts, for device-wide visual
 * consistency. STD (resoledmedium) for all body/labels/UID/hex/bar text;
 * EMPH (helvB08) for titles and selected rows - exactly as the main menu. */
#define NFC_FONT_STD    M1_DISP_MAIN_MENU_FONT_N   /* u8g2_font_resoledmedium_tr */
#define NFC_FONT_EMPH   M1_DISP_MAIN_MENU_FONT_B   /* u8g2_font_helvB08_tf       */
#define NFC_HEXCELL_W   6      /* fixed hex cell width (px) - alignment w/o mono font */
#define NFC_HEXGRP_GAP  3      /* extra gap between 4-digit (2-byte) groups */

//#define SEE_DUMP_MEMORY //READ or Load file dump memory view
/************************** C O N S T A N T **********************************/
/* The card action-menu is assembled at runtime from explicit capabilities
 * (see nfc_build_action_menu); there are no fixed per-family label tables. */

//************************** S T R U C T U R E S *******************************

typedef enum
{
	NFC_READ_DISPLAY_PARAM_READING_READY = 0,
	NFC_READ_DISPLAY_PARAM_READING_COMPLETE,
	NFC_READ_DISPLAY_PARAM_NDEF,          /* dedicated NDEF-content screen (OK from summary) */
	NFC_READ_DISPLAY_PARAM_MFC_DATA,      /* MIFARE Classic sector/block Data view (OK from MFC result) */
	NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY,    /* Human-readable device name/tech/UID/app+file counts/storage (Card Info > More) */
	NFC_READ_DISPLAY_PARAM_DESFIRE_DATA,  /* DESFire GetVersion raw detail (DESFire Summary > More) */
	NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT,    /* Interpreted transit-card summary (Card Info > More, recognized cards only) */
	NFC_READ_DISPLAY_PARAM_DESFIRE_APPS,       /* DESFire deep read: Applications list (DESFire Data > More) */
	NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL, /* DESFire deep read: selected Application detail */
	NFC_READ_DISPLAY_PARAM_DESFIRE_FILES,      /* DESFire deep read: Files list within an Application */
	NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL,/* DESFire deep read: selected File settings + captured content */
	NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM,   /* "Exit to NFC Menu?" / Exit-Stay, shown from
	                                             * BACK while the MFC dictionary phase is live -- stays within
	                                             * VIEW_MODE_NFC_READ (never destroys/recreates the view, which
	                                             * would re-post Q_EVENT_NFC_START_READ and restart the read) */
	NFC_READ_DISPLAY_PARAM_READING_EOL
} S_M1_nfc_read_display_mode_t;

enum {
    VIEW_MODE_NFC_NONE = 0,
    VIEW_MODE_NFC_READ,
    VIEW_MODE_NFC_READ_MORE,
    VIEW_MODE_NFC_SAVE,     //sub menu option
    VIEW_MODE_NFC_EMULATE,
    VIEW_MODE_NFC_WRITE,
	VIEW_MODE_NFC_INFO,
	VIEW_MODE_NFC_EDIT_UID, // Edit UID for loaded file
	VIEW_MODE_NFC_RENAME,   // Rename file for loaded file
	VIEW_MODE_NFC_SAVED_BROWSE, // Browse and load saved NFC file
    VIEW_MODE_NFC_DETECT_READER, // MIFARE Classic Detect Reader / auth capture
    VIEW_MODE_NFC_MFC_KEYS,      // NFC Tools > MFC Keys (dashboard + Add/List/Delete)
    VIEW_MODE_NFC_MFC_SCAN,      // MIFARE Classic dictionary scan (candidate-key testing)
    VIEW_MODE_NFC_MFC_FIND_KEYS, // Find Missing Keys: dictionary-phase continuation of a partial MFC read
    VIEW_MODE_NFC_ULC_KEYS,      // NFC Tools > MIFARE Ultralight Keys (Ultralight C key management)
    VIEW_MODE_NFC_HARVEST,       // NFC Tools > Harvest (live nested-nonce capture -> .m1h)
    VIEW_MODE_NFC_MFC_WRITE,     // MIFARE Classic 1K write (clone/restore of a loaded image)
    VIEW_MODE_NFC_ADD,           // Add: manual card creation (blank template + UID)
    VIEW_MODE_NFC_UNLOCK,        // T2T Unlock: genuine PWD_AUTH (Enter Password / Use Dictionary)
    VIEW_MODE_NFC_PWD_KEYS,      // NFC Tools > NTAG/UL Password Dictionary (4-byte PWD management)
#if defined(M1_MFC_RAW_EMULATION)
    VIEW_MODE_NFC_MFC_EMU,       // Raw MIFARE Classic emulation (Scope B) -- fits under VIEW_MODE_END (18)
#endif
    VIEW_MODE_NFC_END
};

typedef enum {
    NFC_MODE_NONE=0,
    NFC_MODE_READ,
    NFC_MODE_EMULATE,
    NFC_MODE_WRITE //utils mode
} nfc_mode_t;

typedef enum {
	NFC_RECORD_IDLE = 0,
	NFC_RECORD_ACTIVE,
	NFC_RECORD_STANDBY,
	NFC_RECORD_REPLAY,
	NFC_RECORD_UNKNOWN
} S_M1_NFC_Record_t;

/***************************** V A R I A B L E S ******************************/

static uint16_t s_page_scroll = 0;
static uint16_t s_info_mode   = 0;

/* --- NDEF (Type-2) parsed-content cache for the Info view's NDEF screen --- */
#define NDEF_MAX_LINES   24
#define NDEF_LINE_CHARS  32
#define NDEF_VISIBLE     5      /* content lines visible under the title */
/* NDEF state: 0=Not formatted, 1=Empty, 2=URL, 3=Text, 4=Unsupported, 0xFF=not a Type-2 tag */
static uint8_t s_ndef_state = 0xFF;
static char    s_ndef_text[192];   /* decoded URL/Text (or "Unsupported record") */
static char    s_ndef_lines[NDEF_MAX_LINES][NDEF_LINE_CHARS];
static uint8_t s_ndef_line_count = 0;
static uint8_t s_ndef_scroll = 0;
static uint16_t s_mfc_scroll = 0;        /* MIFARE Classic Data view scroll (in rows) */
static uint16_t s_mfc_total_rows = 0;    /* total rows in the Classic Data view */
static uint8_t s_desf_scroll = 0;        /* DESFire Data view scroll (in lines) */
static uint8_t s_desf_line_count = 0;    /* total lines in the DESFire Data view */
static uint8_t s_desf_visible_rows = NFC_INFO_LINES_PER_SCREEN; /* shrinks by 1 when a "More" footer is shown */
/* DESFire deep-read Applications/App-Detail/Files/File-Detail navigation
 * state. Selection indices are absolute (into nfc_ctx_get_desfire_deep()'s
 * bounded arrays); *_scroll is the viewport's top row, kept in sync with
 * the selection by nfc_desfire_apps_kp_move()/nfc_desfire_files_kp_move()
 * so the highlighted row is always visible. */
static uint8_t s_desf_app_sel     = 0;
static uint8_t s_desf_apps_scroll = 0;
static uint8_t s_desf_file_sel     = 0;
static uint8_t s_desf_files_scroll = 0;
static uint8_t s_desf_sel_app_idx  = 0;  /* which application the Files/File-Detail screens are drilled into */
static uint8_t s_desf_filedetail_scroll = 0;
static uint8_t s_desf_filedetail_line_count = 0;
static uint8_t s_desf_transit_scroll = 0;
static uint8_t s_desf_transit_line_count = 0;
static uint8_t s_desf_summary_scroll = 0;
static uint8_t s_desf_summary_line_count = 0;
static void    nfc_mfc_data_draw(void);  /* fwd */
static bool s_edit_uid_started = false;  // Edit UID 시작 플래그
static uint8_t nfc_uiview_gui_latest_param;
static S_M1_NFC_Record_t record_stat;
/* Set only when the user confirms "Exit" from NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM
 * (mid MFC dictionary phase). nfc_read_gui_message()'s Q_EVENT_NFC_READ_COMPLETE
 * handler checks this: while true, that event -- which only ever arrives once the
 * worker has genuinely finished winding down -- triggers the actual exit-to-idle
 * instead of showing a read result, so the queue is never reset/torn down before
 * that real acknowledgment, and a Partial (or any other) result can never be shown
 * for an explicitly-exited session. Cleared the instant it's consumed. */
static bool s_read_exiting = false;
/* Which situation NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM was opened from --
 * an ACTIVE live dictionary phase (the original use, worker still running)
 * or a FINISHED, unsaved MFC Partial result (BACK from the Partial Read
 * result screen: there is real progress worth protecting, but no active
 * worker to wait for). The two need different Stay/Exit routing: active
 * Stay returns to READING_READY and active Exit defers teardown via
 * s_read_exiting until the worker's real completion lands (see above);
 * finished-result Stay returns to READING_COMPLETE (the same result screen,
 * unchanged) and finished-result Exit has no worker to wait for, so it goes
 * straight to the NFC menu via the same VIEW_MODE_IDLE switch every other
 * "leave this read session" exit in this file already uses. false always
 * means "active" (the original, only case before this flag existed), so
 * every existing active-phase call site is correct without having to set it.
 * Reset on a fresh session (nfc_read_gui_create), on Exit being chosen, and
 * on view destruction (nfc_read_gui_destroy) -- never left stale across
 * sessions. */
static bool s_exit_confirm_from_finished_result = false;
//static FIL nfc_file;
//static DIR nfc_dir;
static S_M1_file_info *f_info = NULL;

/* Saved-file launch context.  Native NFC > Saved leaves external=false and
 * browses normally.  Home -> LEFT copies the exact selection here so the
 * NFC module, rather than the generic browser, owns its lifecycle and return
 * routing for the duration of the synchronous uiView loop. */
static struct {
	bool external;
	bool pending;
	char dir[ESP_FILE_PATH_LEN_MAX + 1U];
	char name[ESP_FILE_NAME_LEN_MAX + 1U];
	S_M1_file_info file;
} s_nfc_saved_launch;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void nfc_read(void);
void nfc_saved(void);
static uint8_t nfc_read_more_options_save(void);
static uint8_t nfc_read_more_options_delete(void);
void m1_nfc_info_more_draw(void);

/* For each mode init/create/update/destroy/message prototype */
static void nfc_read_gui_init(void);
static void nfc_read_gui_create(uint8_t param);
static void nfc_read_gui_destroy(uint8_t param);
static void nfc_read_gui_update(uint8_t param);
static int  nfc_read_gui_message(void);
static int nfc_read_kp_handler(void);
/* Defined near nfc_build_action_menu() (well after nfc_read_kp_handler /
 * nfc_read_gui_update in file order); forward-declared here so both can
 * ask it whether the action menu they gate the "More" control on would be
 * empty -- the same builder VIEW_MODE_NFC_READ_MORE and the OK dispatcher
 * use, so the drawn control, the RIGHT-button gate, and the actual menu can
 * never disagree. */
static uint8_t nfc_build_action_menu(const char *labels[NFC_MENU_MAX_ITEMS],
                                     uint8_t      actions[NFC_MENU_MAX_ITEMS]);

static void nfc_read_more_gui_init(void);
static void nfc_read_more_gui_create(uint8_t param);
static void nfc_read_more_gui_destroy(uint8_t param);
static void nfc_read_more_gui_update(uint8_t param);
static int  nfc_read_more_gui_message(void);
static int nfc_read_more_kp_handler(void);

static void nfc_save_gui_init(void);
static void nfc_save_gui_create(uint8_t param);
static void nfc_save_gui_destroy(uint8_t param);
static void nfc_save_gui_update(uint8_t param);
static int  nfc_save_gui_message(void);
static int nfc_save_kp_handler(void);

static void nfc_emulate_gui_init(void);
static void nfc_emulate_gui_create(uint8_t param);
static void nfc_emulate_gui_destroy(uint8_t param);
static void nfc_emulate_gui_update(uint8_t param);
static int  nfc_emulate_gui_message(void);
static int nfc_emulate_kp_handler(void);
static void nfc_ulc_toast(const char *msg);

static void nfc_write_gui_init(void);
static void nfc_write_gui_create(uint8_t param);
static void nfc_write_gui_destroy(uint8_t param);
static void nfc_write_gui_update(uint8_t param);
static int  nfc_write_gui_message(void);
static int nfc_write_kp_handler(void);

static void nfc_info_gui_init(void);
static void nfc_info_gui_create(uint8_t param);
static void nfc_info_gui_destroy(uint8_t param);
static void nfc_info_gui_update(uint8_t param);
static int  nfc_info_gui_message(void);
static int nfc_info_kp_handler(void);
static void nfc_info_drawing(void);
static void nfc_info_preview_draw(void);
static void nfc_desfire_summary_draw(void);
static void nfc_desfire_data_draw(void);
static void nfc_desfire_transit_draw(void);
static void nfc_desfire_apps_draw(void);
static void nfc_desfire_app_detail_draw(void);
static void nfc_desfire_files_draw(void);
static void nfc_desfire_file_detail_draw(void);
static void nfc_info_ndef_draw(void);
static uint8_t nfc_ndef_parse(const uint8_t *msg, uint16_t msg_len, char *out, size_t out_sz);
static void    nfc_ndef_rewrap(const char *text);
static uint8_t nfc_ndef_state(void);
static void    nfc_ndef_open(void);

static void nfc_edit_uid_gui_init(void);
static void nfc_edit_uid_gui_create(uint8_t param);
static void nfc_edit_uid_gui_destroy(uint8_t param);
static void nfc_edit_uid_gui_update(uint8_t param);
static int  nfc_edit_uid_gui_message(void);
static int nfc_edit_uid_kp_handler(void);

static void nfc_rename_gui_init(void);
static void nfc_rename_gui_create(uint8_t param);
static void nfc_rename_gui_destroy(uint8_t param);
static void nfc_rename_gui_update(uint8_t param);
static int  nfc_rename_gui_message(void);
static int nfc_rename_kp_handler(void);

static void nfc_saved_browse_gui_init(void);
static void nfc_saved_browse_gui_create(uint8_t param);
static void nfc_saved_browse_gui_destroy(uint8_t param);
static void nfc_saved_browse_gui_update(uint8_t param);
static int  nfc_saved_browse_gui_message(void);
static int nfc_saved_browse_kp_handler(void);
static void nfc_detect_reader_gui_init(void);
static void nfc_detect_reader_gui_create(uint8_t param);
static void nfc_detect_reader_gui_destroy(uint8_t param);
static void nfc_detect_reader_gui_update(uint8_t param);
static int  nfc_detect_reader_gui_message(void);
static int  nfc_detect_reader_kp_handler(void);
#if defined(M1_MFC_RAW_EMULATION)
static void nfc_mfc_emu_gui_init(void);
static void nfc_mfc_emu_gui_create(uint8_t param);
static void nfc_mfc_emu_gui_destroy(uint8_t param);
static void nfc_mfc_emu_gui_update(uint8_t param);
static int  nfc_mfc_emu_gui_message(void);
static int  nfc_mfc_emu_kp_handler(void);
#endif
/* MFC dictionary progress renderer -- the ONE implementation
 * shared by normal Read's live dictionary phase (nfc_read_gui_update(),
 * VIEW_MODE_NFC_READ) and Find Missing Keys (VIEW_MODE_NFC_MFC_FIND_KEYS),
 * never a second/duplicated draw path. See mfc_dict_progress_draw.c-shaped
 * comment above its definition for the full layout. */
static void nfc_mfc_dict_progress_draw(void);
static void nfc_mfc_lost_tag_draw(void);
static void nfc_mfc_exit_confirm_draw(void);
static void nfc_mfc_dict_totals_ensure(void);
static void nfc_mfc_dict_totals_invalidate(void);   /* forces the next _ensure() to recompute -- fresh per session */
static void nfc_mfc_keys_gui_init(void);
static void nfc_mfc_keys_gui_create(uint8_t param);
static void nfc_mfc_keys_gui_destroy(uint8_t param);
static void nfc_mfc_keys_gui_update(uint8_t param);
static int  nfc_mfc_keys_gui_message(void);
static int  nfc_mfc_keys_kp_handler(void);
static void nfc_mfc_scan_gui_init(void);
static void nfc_mfc_scan_gui_create(uint8_t param);
static void nfc_mfc_scan_gui_destroy(uint8_t param);
static void nfc_mfc_scan_gui_update(uint8_t param);
static int  nfc_mfc_scan_gui_message(void);
static int  nfc_mfc_scan_kp_handler(void);
static void nfc_mfc_find_keys_gui_init(void);
static void nfc_mfc_find_keys_gui_create(uint8_t param);
static void nfc_mfc_find_keys_gui_destroy(uint8_t param);
static void nfc_mfc_find_keys_gui_update(uint8_t param);
static int  nfc_mfc_find_keys_gui_message(void);
static int  nfc_mfc_find_keys_kp_handler(void);
static void nfc_ulc_keys_gui_init(void);
static void nfc_ulc_keys_gui_create(uint8_t param);
static void nfc_ulc_keys_gui_destroy(uint8_t param);
static void nfc_ulc_keys_gui_update(uint8_t param);
static int  nfc_ulc_keys_gui_message(void);
static int  nfc_ulc_keys_kp_handler(void);
static void nfc_unlock_gui_init(void);
static void nfc_pwd_keys_gui_init(void);
static void nfc_pwd_keys_gui_create(uint8_t param);
static void nfc_pwd_keys_gui_destroy(uint8_t param);
static void nfc_pwd_keys_gui_update(uint8_t param);
static int  nfc_pwd_keys_gui_message(void);
static int  nfc_pwd_keys_kp_handler(void);
static void nfc_harvest_gui_init(void);
static void nfc_harvest_gui_create(uint8_t param);
static void nfc_harvest_gui_destroy(uint8_t param);
static void nfc_harvest_gui_update(uint8_t param);
static int  nfc_harvest_gui_message(void);
static int  nfc_harvest_kp_handler(void);
static void nfc_mfc_write_gui_init(void);
static void nfc_mfc_write_gui_create(uint8_t param);
static void nfc_mfc_write_gui_destroy(uint8_t param);
static void nfc_mfc_write_gui_update(uint8_t param);
static int  nfc_mfc_write_gui_message(void);
static int  nfc_mfc_write_kp_handler(void);
static void nfc_add_gui_init(void);
static void nfc_add_gui_create(uint8_t param);
static void nfc_add_gui_destroy(uint8_t param);
static void nfc_add_gui_update(uint8_t param);
static int  nfc_add_gui_message(void);
static int  nfc_add_kp_handler(void);
static void nfc_add_launch(uint8_t add_type);

/*============================================================================*/
/*                            table of ui view                                */
/*============================================================================*/
static const view_func_t view_nfc_read_table[] = {
    NULL,               // Empty
    nfc_read_gui_init,      // VIEW_MODE_NFC_READ
    nfc_read_more_gui_init,   // VIEW_MODE_NFC_READ_MORE
    nfc_save_gui_init,      // VIEW_MODE_NFC_SAVE
    nfc_emulate_gui_init,   // VIEW_MODE_NFC_EMULATE
    nfc_write_gui_init,     // VIEW_MODE_NFC_WRITE
    nfc_info_gui_init,      // VIEW_MODE_NFC_INFO
    nfc_edit_uid_gui_init,  // VIEW_MODE_NFC_EDIT_UID
    nfc_rename_gui_init,    // VIEW_MODE_NFC_RENAME
    nfc_saved_browse_gui_init, // VIEW_MODE_NFC_SAVED_BROWSE
    nfc_detect_reader_gui_init, // VIEW_MODE_NFC_DETECT_READER
    nfc_mfc_keys_gui_init,      // VIEW_MODE_NFC_MFC_KEYS
    nfc_mfc_scan_gui_init,      // VIEW_MODE_NFC_MFC_SCAN
    nfc_mfc_find_keys_gui_init, // VIEW_MODE_NFC_MFC_FIND_KEYS
    nfc_ulc_keys_gui_init,      // VIEW_MODE_NFC_ULC_KEYS
    nfc_harvest_gui_init,       // VIEW_MODE_NFC_HARVEST
    nfc_mfc_write_gui_init,     // VIEW_MODE_NFC_MFC_WRITE
    nfc_add_gui_init,           // VIEW_MODE_NFC_ADD
    nfc_unlock_gui_init,        // VIEW_MODE_NFC_UNLOCK
    nfc_pwd_keys_gui_init,      // VIEW_MODE_NFC_PWD_KEYS
#if defined(M1_MFC_RAW_EMULATION)
    nfc_mfc_emu_gui_init,       // VIEW_MODE_NFC_MFC_EMU
#endif
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief menu_nfc_init - Initialize NFC menu and create worker task
 * 
 * This function initializes the NFC sub-menu by creating the NFC worker task
 * and message queue. It prevents duplicate initialization by checking if
 * the task and queue handles are already created.
 * 
 * @note This function checks free heap before and after task creation
 *       and logs a warning if heap is low.
 * 
 * @retval None
 */
/*============================================================================*/
void menu_nfc_init(void)
{
    platformLog("menu_nfc_init and Task Create\r\n");

    /* Do not recreate if already created (prevent duplication) */
    if (nfc_worker_task_hdl != NULL || nfc_worker_q_hdl != NULL)
    {
        platformLog("[NFC] menu_nfc_init: already initialized. task=%p, q=%p\r\n",
                    nfc_worker_task_hdl, nfc_worker_q_hdl);
        return;
    }

    BaseType_t ret;
    size_t free_heap_before = xPortGetFreeHeapSize();
    platformLog("[NFC] free heap before create: %u bytes\r\n", (unsigned)free_heap_before);
	osDelay(100);

    ret = xTaskCreate(nfc_worker_task,
                      "nfc_worker",
                      M1_TASK_STACK_SIZE_4096,
                      NULL,
                      NFC_WORKER_TASK_PRIORITY, //TASK_PRIORITY_SUBFUNC_HANDLER + 1,
                      &nfc_worker_task_hdl);

    if (ret != pdPASS || nfc_worker_task_hdl==NULL)
    {
        platformLog("[NFC] nfc_worker_task create failed! ret=%d, hdl=%p\r\n",
                    (int)ret, nfc_worker_task_hdl);
        nfc_worker_task_hdl = NULL;
        return;    
    }
	platformLog("[NFC] xTaskCreate ret=%ld, handle=%p\r\n",
            (long)ret, nfc_worker_task_hdl);

    
    nfc_worker_q_hdl = xQueueCreate(10, sizeof(S_M1_Main_Q_t));
    if (nfc_worker_q_hdl==NULL)
    {
        platformLog("[NFC] nfc_worker_q create failed! delete task and return\r\n");
        vTaskDelete(nfc_worker_task_hdl);
        nfc_worker_task_hdl = NULL;
        return;
    }

    size_t free_heap_after = xPortGetFreeHeapSize();
    platformLog("[NFC] free heap after create: %u bytes\r\n", (unsigned)free_heap_after);

    if (free_heap_after < M1_LOW_FREE_HEAP_WARNING_SIZE)
    {
        platformLog("[NFC][WARN] free heap is low! %u < %u\r\n",
                    (unsigned)free_heap_after,
                    (unsigned)M1_LOW_FREE_HEAP_WARNING_SIZE);
    }
}

/*============================================================================*/
/**
 * @brief menu_nfc_deinit - Deinitialize NFC menu and destroy worker task
 * 
 * This function cleans up the NFC sub-menu by deleting the NFC worker task
 * and message queue. It safely handles NULL handles.
 * 
 * @retval None
 */
 /*============================================================================*/
void menu_nfc_deinit(void)
{
    platformLog("menu_nfc_deinit and Task Destroy\r\n");

#if defined(M1_MFC_RAW_EMULATION)
    platformLog("[RAW-TRACE] menu_nfc_deinit entry m1_mfc_raw_hw_active()=%d\r\n",
                (int)m1_mfc_raw_hw_active());
    /* Phase A (persona/lifecycle fix): never hard-delete nfc_worker_task while
     * RAWOWN is STARTING/ACTIVE/STOPPING -- that leaves m1_mfc_raw_session_hw.c's
     * s_state stuck non-INACTIVE (a plain static, survives vTaskDelete) and
     * EXTI masked until reboot. Request the same graceful stop-and-wait the UI
     * BACK path already uses, bounded, before proceeding either way.
     *
     * Fail-closed (lifecycle correction): the wait's own return value only
     * says whether the bounded semaphore was signaled in time -- it is NOT
     * the authoritative answer to "is RAWOWN actually done with the radio".
     * That is m1_mfc_raw_hw_active() (s_state != INACTIVE), checked again
     * here regardless of the wait's result. If RAWOWN is STILL active after
     * the bounded wait, the worker task must NOT be deleted -- there would
     * be no task left to ever bring s_state back to INACTIVE (a plain
     * static, survives vTaskDelete) or to service the radio, permanently
     * stranding RAWOWN mid-session. The worker task, its queue, and the
     * start coordinator are all left completely intact in that case, and
     * this function returns without tearing anything down -- the existing
     * idempotent guard at the top of menu_nfc_init() (task/queue handles
     * still non-NULL) already supports re-entering NFC on top of the
     * preserved task/queue rather than creating a second one. */
    if (m1_mfc_raw_hw_active()) {
        platformLog("[RAW-TRACE] menu_nfc_deinit: RAWOWN active, requesting stop before task delete\r\n");
        (void)m1_mfc_raw_hw_request_stop_and_wait(pdMS_TO_TICKS(2000));
        if (m1_mfc_raw_hw_active()) {
            platformLog("[RAWOWN-STOP-TIMEOUT] menu_nfc_deinit: RAWOWN still active after stop-and-wait -- "
                        "worker task/queue and start coordinator left intact, teardown skipped\r\n");
            return;
        }
        /* The wait may have reported failure (bounded semaphore not
         * signaled in time) while RAWOWN nonetheless finished and became
         * inactive just after -- that race is safe to proceed past. */
        platformLog("[RAW-TRACE] menu_nfc_deinit: RAWOWN confirmed inactive after stop-and-wait, "
                    "proceeding with teardown\r\n");
    }
#endif

    if (nfc_worker_task_hdl != NULL)
    {
        platformLog("[NFC] delete worker task\r\n");
        vTaskDelete(nfc_worker_task_hdl);
        nfc_worker_task_hdl = NULL;
    }

    if (nfc_worker_q_hdl != NULL)
    {
        platformLog("[NFC] delete worker queue\r\n");
        vQueueDelete(nfc_worker_q_hdl);
        nfc_worker_q_hdl = NULL;
    }

#if defined(M1_MFC_RAW_EMULATION)
    /* Safe coordinator-reset point: only reached once RAWOWN is confirmed
     * inactive (the fail-closed return above is the only way to skip this)
     * and the worker task has been deleted -- or was already NULL, which
     * can only mean it was deleted by an earlier call or never existed,
     * either way still satisfying "the worker can no longer consume the
     * request". A PENDING or CANCELLED request can no longer survive NFC
     * menu teardown; re-entering NFC always finds the coordinator IDLE. */
    m1_mfc_raw_hw_finish_request();
#endif
}



/*============================================================================*/
/**
 * @brief nfc_read - Main NFC read function
 * 
 * This function handles the NFC card reading workflow. It registers
 * the NFC read view table, switches to the read view mode, and enters
 * a message loop until the view exits.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_read(void)
{
	platformLog("nfc_read()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// init
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_READY);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}
	platformLog("nfc_read()-exit\r\n");
}

/*============================================================================*/
/**
 * @brief nfc_read_kp_handler - Handle keypad input for NFC read view
 * 
 * Processes button events in the NFC read view:
 * - BACK: Exit to idle view
 * - LEFT: Retry reading (restart read process)
 * - RIGHT: Switch to submenu view
 * 
 * @retval 0 Exit requested (BACK button)
 * @retval 1 Continue processing
 */
/*============================================================================*/
extern void nfc_poller_mfc_scan_abort(void);  /* nfc_poller.c -- shared by Tools > Dictionary
                                                * Scan and normal Read's own dictionary phase */
extern void nfc_poller_mfc_scan_skip_source(void);  /* nfc_poller.c -- CENTER
                                                      * "Skip": abandons only the current
                                                      * dictionary source, not the whole phase */

/* Whether the "Retry" action (LEFT on the read-result screen) is meaningful
 * for the just-finished read, per mfc_result.h. Non-MFC families have no
 * outcome concept at all -- Retry stays universally available for them,
 * unchanged. For MFC, mirrors nfc_build_action_menu()'s own use of
 * mfc_action_eligibility() so the submenu's implied action set and this
 * screen's LEFT-button binding never disagree about what's meaningful for
 * the same outcome. */
static bool nfc_mfc_try_again_allowed(void)
{
    nfc_run_ctx_t *c = nfc_ctx_get();
    if (!c || c->head.family != M1NFC_FAM_CLASSIC) return true;
    const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
    return mfc_action_eligibility(mc ? mc->outcome : MFC_OUTCOME_NONE).try_again;
}

/* Whether the "More" control (RIGHT on the read-result screen) has an
 * action menu to open at all. Every non-MFC live family always has at
 * least "Info" (nfc_build_action_menu() adds it unconditionally when
 * !is_mfc), so this is a no-op for them -- always true, unchanged. For
 * MFC, a FAILED read or a CANCELLED read with no data offers nothing
 * (no Save/Save Partial, no Emulate/Write -- both require a complete
 * image -- and no Info), so the menu this would open is genuinely empty. */
static bool nfc_read_more_menu_has_items(void)
{
    const char *labels[NFC_MENU_MAX_ITEMS];
    uint8_t     actions[NFC_MENU_MAX_ITEMS];
    return nfc_build_action_menu(labels, actions) > 0;
}

static int nfc_read_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0); 
	if (ret==pdTRUE)
	{
		uint8_t p = nfc_uiview_gui_latest_param;
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if ( p==NFC_READ_DISPLAY_PARAM_MFC_DATA )
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_INFO, 0); // Classic Data -> Card Info
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY )
			{
				/* Level 2 ("Info") -> Level 1: which Level 1 depends on how
				 * this session got here. A recognized Clipper card reaches
				 * Summary via Transit's "Info" action, so Back returns to
				 * Transit. A generic (non-recognized) DESFire card now
				 * reaches Summary directly from the action menu's own
				 * "Info" item (NFC_ACT_INFO skips the generic ISO Card Info
				 * screen entirely for this family -- that screen was a
				 * strict subset of Summary's content, a genuinely redundant
				 * extra hop, not a distinct level), so Back returns to that
				 * menu, not to a Card Info screen this session never
				 * visited. */
				if (nfc_ctx_get_transit()->card_id != NfcTransitCardUnknown)
					m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT); // DESFire Summary -> Transit
				else
					m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_RESET); // DESFire Summary -> action menu
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA )
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY); // DESFire Data -> DESFire Summary
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APPS )
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_DATA); // Applications -> DESFire Data
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL )
			{
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_APPS); // App Detail -> Applications
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILES )
			{
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL); // Files -> App Detail
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL )
			{
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILES); // File Detail -> Files
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_NDEF )
			{
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_READING_COMPLETE); // NDEF -> result
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM )
			{
				/* BACK on the confirmation itself behaves like "Stay" (the
				 * least-surprising default for a confirm dialog's own BACK --
				 * dismiss it, change nothing) rather than a second, different
				 * way to exit. Origin-aware: an active-phase confirm returns
				 * to the live progress screen; a finished-result confirm
				 * returns to that same finished result, not READING_READY --
				 * see s_exit_confirm_from_finished_result's own comment. */
				m1_uiView_display_update(s_exit_confirm_from_finished_result ?
				    NFC_READ_DISPLAY_PARAM_READING_COMPLETE : NFC_READ_DISPLAY_PARAM_READING_READY);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE
			          && nfc_ctx_get()->head.family == M1NFC_FAM_CLASSIC
			          && nfc_ctx_get_mfc_info()->valid
			          && nfc_ctx_get_mfc_info()->outcome == MFC_OUTCOME_PARTIAL )
			{
				/* BACK from a finished, unsaved MFC Partial result: real
				 * progress worth protecting (found keys, read sectors), same
				 * as the live dictionary phase, so it gets the same Exit/Stay
				 * confirmation rather than falling into the catchall below,
				 * which is written for the still-waiting/still-scanning case
				 * and would otherwise exit straight to the idle menu with no
				 * confirmation at all. Scoped to PARTIAL only -- Complete/
				 * Failed/Cancelled and every non-MFC family keep their
				 * existing, already-correct catchall behavior below,
				 * unchanged. */
				s_exit_confirm_from_finished_result = true;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM);
			}
			else
			{
				/* p==NFC_READ_DISPLAY_PARAM_READING_READY: waiting for a card,
				 * or a read (fast pass, then possibly the dictionary phase)
				 * actively running in the worker task. BACK
				 * while the MFC dictionary phase is genuinely live (running,
				 * or waiting through a lost-tag retry) opens an Exit/Stay
				 * confirmation instead of exiting directly -- there is real,
				 * truthfully-recoverable progress to protect. Before the
				 * dictionary phase has even started (or for every non-MFC
				 * family), there is nothing yet worth confirming, so BACK
				 * keeps exiting immediately, unchanged. Also reached for
				 * every READING_COMPLETE result NOT covered by the explicit
				 * MFC-Partial branch above (Complete/Failed/Cancelled, every
				 * non-MFC family) -- unchanged from before that branch
				 * existed, since scan_state is never RUNNING/CARD_LOST once
				 * a session has actually finished. */
				nfc_mfc_scan_state_t scan_state = (nfc_mfc_scan_state_t)nfc_ctx_get_mfc_scan()->state;
				if ((scan_state == NFC_SCAN_RUNNING) || (scan_state == NFC_SCAN_CARD_LOST)) {
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM);
				} else {
					m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
					xQueueReset(main_q_hdl);
					return 0;
				}
			}
		}
		else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if ( p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE )
			{
				/* COMPLETE/CANCELLED-with-no-data MFC results have nothing to
				 * retry for -- see nfc_mfc_try_again_allowed(). Every other
				 * family, and MFC PARTIAL/FAILED, keep this unchanged. */
				if (nfc_mfc_try_again_allowed())
					m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_READY); // Retry
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_MFC_DATA )
				m1_uiView_display_switch(VIEW_MODE_NFC_INFO, 0); // Classic Data -> Card Info
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY )
			{
				/* Same session-dependent routing as the BACK handler above. */
				if (nfc_ctx_get_transit()->card_id != NfcTransitCardUnknown)
					m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT); // DESFire Summary -> Transit
				else
					m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_RESET); // DESFire Summary -> action menu
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA )
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY); // DESFire Data -> DESFire Summary
			else if ( p==NFC_READ_DISPLAY_PARAM_NDEF )
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_READING_COMPLETE); // NDEF -> result
			else if ( p==NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM )
			{
				if (s_exit_confirm_from_finished_result)
				{
					/* "Exit" from a FINISHED result's confirmation: there is
					 * no active worker to wait for (the session already
					 * finished before BACK was even pressed), so this exits
					 * directly and safely -- the same VIEW_MODE_IDLE switch
					 * every other "leave this read session" exit in this
					 * file already uses. Does NOT set s_read_exiting: that
					 * flag exists specifically to defer teardown until a
					 * real pending Q_EVENT_NFC_READ_COMPLETE arrives for an
					 * ACTIVE abort, and no such event is pending here. */
					s_exit_confirm_from_finished_result = false;
					m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
					xQueueReset(main_q_hdl);
					return 0;
				}
				/* "Exit" (left button on the confirm dialog), active
				 * phase. Stops the worker safely (the SAME abort flag as
				 * CENTER Skip/BACK elsewhere -- this just requests the whole
				 * phase stop, not one source) and marks this session as
				 * exiting; the actual teardown happens only once the
				 * worker's real completion is observed in
				 * nfc_read_gui_message(), never before -- so the shared
				 * queue is never reset ahead of that acknowledgment, and no
				 * result (Partial or otherwise) can ever be shown for a
				 * session the user explicitly chose to discard. */
				nfc_poller_mfc_scan_abort();
				s_read_exiting = true;
			}
		}
		else if(this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )	// More: action menu / next data screen
		{
			if ( p==NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM )
			{
				/* "Stay" (right button) -- dismiss the confirmation.
				 * Origin-aware, same as the BACK-on-confirm handler above:
				 * active phase returns to the live progress screen (the
				 * background acquisition was never paused or touched, so
				 * this is a pure display-param switch, nothing to resume);
				 * a finished result returns to that exact same finished
				 * result screen, not READING_READY (there is no acquisition
				 * to resume -- it already finished). */
				m1_uiView_display_update(s_exit_confirm_from_finished_result ?
				    NFC_READ_DISPLAY_PARAM_READING_COMPLETE : NFC_READ_DISPLAY_PARAM_READING_READY);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE )
			{
				/* Never open an action menu with nothing in it (e.g. a FAILED
				 * or no-data-CANCELLED MFC read) -- see nfc_read_more_menu_has_items(). */
				if (nfc_read_more_menu_has_items())
				{
					m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_RESET);
					nfc_uiview_gui_latest_param = X_MENU_UPDATE_RESET;
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY )
			{
				/* Human-readable Summary -> the raw GetVersion hex dump,
				 * matching the "Info -> More -> Card Info" depth
				 * (nfc_scene_more_info_on_enter_mf_desfire's immediate
				 * redirect into the raw-hex Submenu). Always offered --
				 * unlike the generic-vs-protected app-count gate below,
				 * the raw dump doesn't depend on whether apps are readable. */
				s_desf_scroll = 0;
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_DATA);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				if ( (deep->app_count > 0) || deep->apps_protected )
				{
					s_desf_app_sel = 0; s_desf_apps_scroll = 0;
					m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_APPS);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
				if ( app->select_ok && (app->file_count > 0) )
				{
					s_desf_file_sel = 0; s_desf_files_scroll = 0;
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILES);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT )
			{
				/* Interpreted summary -> the human-readable generic DESFire
				 * Summary screen, matching the parsed
				 * summary -> More -> ReadMenu -> Info" chain (the Clipper
				 * path passes through the SAME generic Info screen non-
				 * Clipper cards land on -- verified against
				 * nfc_scene_read_success_on_enter_mf_desfire's routing).
				 * The raw hex dump stays reachable one further "More" away,
				 * never the first (or second) thing shown. */
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY);
			}
		}
		else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )	// OK: family-routed / list select
		{
			if ( p==NFC_READ_DISPLAY_PARAM_READING_COMPLETE )
			{
				/* OK opens the NDEF View only for readable Ultralight/NTAG URL/Text
				 * (guarded in nfc_ndef_open). Classic Data is reached via
				 * More > Info > Data, so Classic OK is inactive here. */
				nfc_ndef_open();
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APPS )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				if ( deep->app_count > 0 )
				{
					s_desf_sel_app_idx = s_desf_app_sel;
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILES )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
				if ( app->file_count > 0 )
				{
					s_desf_filedetail_scroll = 0;
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_READING_READY )
			{
				/* CENTER "Skip": only meaningful while the
				 * MFC dictionary phase is genuinely live -- abandons the
				 * REMAINING candidates of the currently active source only
				 * (never the whole phase), preserving every already-proven
				 * key/block. Skipping the LAST configured source finishes
				 * acquisition naturally (the iterator reports exhausted),
				 * which flows through the existing outcome classifier and
				 * Q_EVENT_NFC_READ_COMPLETE exactly like a fully-exhausted
				 * dictionary -- no separate "finish" call needed here. A
				 * harmless no-op before the dictionary phase has started, or
				 * for every non-MFC family (nothing currently binds CENTER
				 * here otherwise). */
				nfc_mfc_scan_state_t scan_state = (nfc_mfc_scan_state_t)nfc_ctx_get_mfc_scan()->state;
				if ((scan_state == NFC_SCAN_RUNNING) || (scan_state == NFC_SCAN_CARD_LOST)) {
					nfc_poller_mfc_scan_skip_source();
				}
			}
		}
		else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if ( p==NFC_READ_DISPLAY_PARAM_NDEF && s_ndef_scroll > 0 )
			{
				s_ndef_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_NDEF);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_MFC_DATA && s_mfc_scroll > 0 )
			{
				s_mfc_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_MFC_DATA);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA && s_desf_scroll > 0 )
			{
				s_desf_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_DATA);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APPS && s_desf_app_sel > 0 )
			{
				s_desf_app_sel--;
				if ( s_desf_app_sel < s_desf_apps_scroll ) s_desf_apps_scroll = s_desf_app_sel;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_APPS);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILES && s_desf_file_sel > 0 )
			{
				s_desf_file_sel--;
				if ( s_desf_file_sel < s_desf_files_scroll ) s_desf_files_scroll = s_desf_file_sel;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILES);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL && s_desf_filedetail_scroll > 0 )
			{
				s_desf_filedetail_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT && s_desf_transit_scroll > 0 )
			{
				s_desf_transit_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY && s_desf_summary_scroll > 0 )
			{
				s_desf_summary_scroll--;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY);
			}
		}
		else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if ( p==NFC_READ_DISPLAY_PARAM_NDEF && (s_ndef_line_count > NDEF_VISIBLE) &&
			     (s_ndef_scroll < (uint8_t)(s_ndef_line_count - NDEF_VISIBLE)) )
			{
				s_ndef_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_NDEF);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_MFC_DATA && (s_mfc_total_rows > 7) &&
			     (s_mfc_scroll < (uint16_t)(s_mfc_total_rows - 7)) )
			{
				s_mfc_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_MFC_DATA);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA &&
			     (s_desf_line_count > s_desf_visible_rows) &&
			     (s_desf_scroll < (uint8_t)(s_desf_line_count - s_desf_visible_rows)) )
			{
				s_desf_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_DATA);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_APPS )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				if ( s_desf_app_sel + 1 < deep->app_count )
				{
					s_desf_app_sel++;
					if ( s_desf_app_sel >= (uint8_t)(s_desf_apps_scroll + NFC_DESF_LIST_VISIBLE_ROWS) )
						s_desf_apps_scroll = (uint8_t)(s_desf_app_sel - NFC_DESF_LIST_VISIBLE_ROWS + 1);
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_APPS);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILES )
			{
				const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
				const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
				if ( s_desf_file_sel + 1 < app->file_count )
				{
					s_desf_file_sel++;
					if ( s_desf_file_sel >= (uint8_t)(s_desf_files_scroll + NFC_DESF_LIST_VISIBLE_ROWS) )
						s_desf_files_scroll = (uint8_t)(s_desf_file_sel - NFC_DESF_LIST_VISIBLE_ROWS + 1);
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILES);
				}
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL &&
			     (s_desf_filedetail_line_count > NFC_INFO_LINES_PER_SCREEN) &&
			     (s_desf_filedetail_scroll < (uint8_t)(s_desf_filedetail_line_count - NFC_INFO_LINES_PER_SCREEN)) )
			{
				s_desf_filedetail_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT &&
			     (s_desf_transit_line_count > NFC_DESF_LIST_VISIBLE_ROWS) &&
			     (s_desf_transit_scroll < (uint8_t)(s_desf_transit_line_count - NFC_DESF_LIST_VISIBLE_ROWS)) )
			{
				s_desf_transit_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT);
			}
			else if ( p==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY &&
			     (s_desf_summary_line_count > NFC_DESF_LIST_VISIBLE_ROWS) &&
			     (s_desf_summary_scroll < (uint8_t)(s_desf_summary_line_count - NFC_DESF_LIST_VISIBLE_ROWS)) )
			{
				s_desf_summary_scroll++;
				m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY);
			}
		}
	}

	return 1;
}


/*============================================================================*/
/**
 * @brief nfc_read_gui_create - Create and initialize NFC read view
 * 
 * Initializes the NFC read view. If param is 0, starts the NFC reading
 * process by sending a start read event to the worker queue and enabling
 * LED blink indication.
 * 
 * @param[in] param View parameter (0 = start reading, other = update only)
 * @retval None
 */
/*============================================================================*/
static void nfc_read_gui_create(uint8_t param)
{
	platformLog("nfc_read_gui_create param[%d]\r\n", param);
	if( param==NFC_READ_DISPLAY_PARAM_READING_READY )
	{
		/* Discard any leftover main_q_hdl message before this fresh session
		 * starts. Exiting the MFC dictionary phase via BACK now waits for
		 * the worker's real Q_EVENT_NFC_READ_COMPLETE before ever resetting
		 * this queue (see s_read_exiting in nfc_read_gui_message()), so
		 * that specific stale-event race is closed at the source -- this
		 * reset is kept as a defensive backstop for any other path that
		 * might still leave something queued (e.g. a view switch away from
		 * Read that does not go through the Exit-confirm flow at all). */
		xQueueReset(main_q_hdl);

		record_stat = NFC_RECORD_IDLE;
		s_read_exiting = false;               /* fresh session: no exit-in-progress carried over */
		s_exit_confirm_from_finished_result = false;   /* fresh session: no confirm origin carried over */
		nfc_mfc_dict_totals_invalidate();     /* fresh per-source-total computation for this session */
		fb_net_read_start(); /* migrated: NFC Read lifecycle entry, M1_FEEDBACK_RECONCILIATION.md sec 10.3 */
		m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_START_READ);
		vTaskDelay(50);
	}

	m1_uiView_display_update(param);
}

/*============================================================================*/
/**
 * @brief nfc_read_gui_destroy - Destroy NFC read view and cleanup resources
 * 
 * Cleans up the NFC read view by turning off LED blink indication
 * and sending a read completion event to the worker queue.
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_read_gui_destroy(uint8_t param)
{
	platformLog("nfc_read_gui_destroy param[%d]\r\n", param);
	fb_net_read_stop(); // migrated: NFC Read lifecycle exit
	s_exit_confirm_from_finished_result = false;   /* view is going away: never carry this into the next session */
	m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_READ_COMPLETE);
}


/*============================================================================*/
 /* @brief nfc_read_gui_update - Update NFC read view display
 * 
 * Updates the display based on the read state:
 * - param 0: Shows "Reading" screen with instructions
 * - param 1: Shows read complete screen with card information (Type, Family, UID)
 * 
 * @param[in] param View update parameter (0 = reading, 1 = read done)
 * @retval None
 */
/*============================================================================*/
/*============================================================================*/
/**
 * @brief nfc_t2t_model_pages - Derive T2T model name + "Pages Read: X/Y"
 *
 * Read-only helper shared by the read-complete summary and the Info screen.
 * Maps the T2T sub-variant resolved by the poller (nfc_ctx_get_t2t_variant)
 * to a display name and total page count, then formats
 * "Pages Read: <read>/<total>" from the dump page count and that total.
 * Outputs are set to "" when unavailable (e.g. non-Type-2 cards or an
 * unresolved variant), so callers can test buf[0].
 *
 * @param[out] model     Model-name buffer (may be NULL)
 * @param[in]  model_sz  Size of model buffer
 * @param[out] pages     "Pages Read: X/Y" buffer (may be NULL)
 * @param[in]  pages_sz  Size of pages buffer
 * @retval None
 */
/*============================================================================*/
static void nfc_t2t_model_pages(char *model, size_t model_sz, char *pages, size_t pages_sz)
{
    if (model && model_sz) model[0] = '\0';
    if (pages && pages_sz) pages[0] = '\0';

    /* Y (total pages) and the display name come from the variant resolved by
     * the poller (GET_VERSION for EV1 11/21 and NTAG213/215/216; the legacy
     * probe chain for original Ultralight, Ultralight C, NTAG203). An
     * unresolved variant leaves the name empty so the caller can fall back to
     * the generic Type 2 / family title. */
    uint16_t    total_pages = 0U;
    const char *name        = NULL;

    switch (nfc_ctx_get_t2t_variant())
    {
        case M1NFC_T2TVAR_UL:      name = "MIFARE Ultralight";    total_pages = 16U;  break;
        case M1NFC_T2TVAR_ULC:     name = "MIFARE Ultralight C";  total_pages = 48U;  break;
        case M1NFC_T2TVAR_UL11:    name = "MIFARE Ultralight 11"; total_pages = 20U;  break;
        case M1NFC_T2TVAR_UL21:    name = "MIFARE Ultralight 21"; total_pages = 41U;  break;
        case M1NFC_T2TVAR_NTAG203: name = "NTAG203";              total_pages = 42U;  break;
        case M1NFC_T2TVAR_NTAG213: name = "NTAG213";              total_pages = 45U;  break;
        case M1NFC_T2TVAR_NTAG215: name = "NTAG215";              total_pages = 135U; break;
        case M1NFC_T2TVAR_NTAG216: name = "NTAG216";              total_pages = 231U; break;
        default:                   name = NULL;                   total_pages = 0U;   break;
    }

    if (name && model && model_sz)
    {
        strncpy(model, name, model_sz - 1);
        model[model_sz - 1] = '\0';
    }

    uint16_t x_pages = nfc_ctx_get_t2t_page_count();
    if ((x_pages > 0U) && pages && pages_sz)
    {
        uint16_t y_pages = (total_pages > 0U) ? total_pages : x_pages;
        snprintf(pages, pages_sz, "Pages Read: %u/%u", (unsigned)x_pages, (unsigned)y_pages);
    }
}

/*============================================================================*/
/**
 * @brief nfc_draw_hex_cells - Draw hex bytes in fixed-width cells (aligned)
 *
 * Renders each hex digit centered in an NFC_HEXCELL_W cell, grouped in
 * 4-digit (2-byte) groups separated by NFC_HEXGRP_GAP. Column alignment is
 * therefore independent of the (proportional) font's string width, so hex
 * data lines up across rows even with the main-menu font. Caller sets the
 * font first. Returns the end X coordinate.
 *
 * @param[in] x0     Left X of the first cell
 * @param[in] y      Text baseline
 * @param[in] bytes  Byte array
 * @param[in] n      Number of bytes
 * @retval End X coordinate after the last cell
 */
/*============================================================================*/
static uint8_t nfc_draw_hex_cells(uint8_t x0, uint8_t y, const uint8_t *bytes, uint8_t n)
{
    static const char HEXD[] = "0123456789ABCDEF";
    uint8_t x = x0;
    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t grp = (uint8_t)(i / 2);   /* 2 bytes = one 4-digit group */
        uint8_t cx  = (uint8_t)(x0 + i * 2 * NFC_HEXCELL_W + grp * NFC_HEXGRP_GAP);
        char hi[2] = { HEXD[(bytes[i] >> 4) & 0x0F], 0 };
        char lo[2] = { HEXD[bytes[i] & 0x0F], 0 };
        uint8_t wh = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, hi);
        uint8_t wl = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, lo);
        u8g2_DrawStr(&m1_u8g2, cx + (NFC_HEXCELL_W - wh) / 2, y, hi);
        u8g2_DrawStr(&m1_u8g2, cx + NFC_HEXCELL_W + (NFC_HEXCELL_W - wl) / 2, y, lo);
        x = (uint8_t)(cx + 2 * NFC_HEXCELL_W);
    }
    return x;
}

/*============================================================================*/
/**
 * @brief st25tb_type_full - Full display name for an ST25TB variant.
 */
/*============================================================================*/
static const char *st25tb_type_full(uint8_t variant)
{
    switch (variant)
    {
        case M1NFC_TBVAR_512AT: return "ST25TB512-AT/SRI512";
        case M1NFC_TBVAR_512AC: return "ST25TB512-AC/SRT512";
        case M1NFC_TBVAR_X512:  return "SRIX512";
        case M1NFC_TBVAR_02K:   return "ST25TB02K/SRI2K";
        case M1NFC_TBVAR_04K:   return "ST25TB04K/SRI4K";
        case M1NFC_TBVAR_X4K:   return "SRIX4K";
        default:                return "ST25TB";
    }
}

/*============================================================================*/
/**
 * @brief st25tb_fit_name - Copy the ST25TB type name, dropping the /SRIxxx
 * alias when the full name would exceed max_w px in the current font.
 * The caller must set the font first.
 */
/*============================================================================*/
static void st25tb_fit_name(uint8_t variant, char *out, size_t out_sz, uint8_t max_w)
{
    const char *full = st25tb_type_full(variant);
    strncpy(out, full, out_sz - 1);
    out[out_sz - 1] = '\0';
    if (u8g2_GetStrWidth(&m1_u8g2, out) > max_w)
    {
        char *slash = strchr(out, '/');   /* drop the alias suffix to fit */
        if (slash) *slash = '\0';
    }
}

static void nfc_read_gui_update(uint8_t param)
{
    if ( nfc_uiview_gui_latest_param==X_MENU_UPDATE_RESET )
    {
    	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_RESTORE);
    }
    nfc_uiview_gui_latest_param = param; // Update new param

    /* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1

    if( param==NFC_READ_DISPLAY_PARAM_READING_READY )	// reading
    {
		/* Before the MFC dictionary phase starts (still identifying the
		 * card / running the fast pass), and for every non-MFC family: the
		 * existing generic waiting screen, completely unchanged -- never
		 * force a non-MFC card through an MFC-specific screen. Once the
		 * dictionary phase is genuinely live (running, or waiting through a
		 * lost-tag retry), show the SAME progress renderer
		 * Find Missing Keys already uses -- one shared implementation, not
		 * a second one, per family/state gated purely on live nfc_ctx
		 * state, never on which entry point got here. */
		nfc_run_ctx_t *rc = nfc_ctx_get();
		nfc_mfc_scan_state_t scan_state = (nfc_mfc_scan_state_t)nfc_ctx_get_mfc_scan()->state;
		if (rc && (rc->head.family == M1NFC_FAM_CLASSIC) && (scan_state == NFC_SCAN_CARD_LOST))
		{
			nfc_mfc_lost_tag_draw();
		}
		else if (rc && (rc->head.family == M1NFC_FAM_CLASSIC) && (scan_state == NFC_SCAN_RUNNING))
		{
			nfc_mfc_dict_progress_draw();
		}
		else
		{
			// Centered NFC read/waiting screen: tag + "N" + signal waves + caption.
			m1_read_icon_draw(&m1_u8g2, 'N', 3);
		}
    }
    else if( param==NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM )
    {
		nfc_mfc_exit_confirm_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_READING_COMPLETE )	// read done
    {
		platformLog("NFC Read Done UI Display\r\n");

		char model_str[24];
		char pages_str[24];
		nfc_t2t_model_pages(model_str, sizeof(model_str), pages_str, sizeof(pages_str));
		s_ndef_state = nfc_ndef_state();   // classify NDEF + cache decoded content

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

		if (nfc_ctx_get()->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)   // MIFARE Classic (Phase A)
		{
			const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
			nfc_run_ctx_t *cc = nfc_ctx_get();
			char ml[28];

			if (mc->outcome != MFC_OUTCOME_COMPLETE)
			{
				/* Dedicated Partial/Failed/Cancelled result presentation --
				 * COMPLETE (below, unchanged) keeps its exact prior layout;
				 * this increment only adds truthful, distinct wording for
				 * every other outcome, per spec. Actions themselves (Save
				 * Partial / Find Missing Keys / Try Again / Info) are
				 * reached via the existing Retry/More bar below and the
				 * existing action menu -- no new navigation surface. */
				u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
				u8g2_DrawStr(&m1_u8g2, 2, 9,
				    (mc->outcome == MFC_OUTCOME_PARTIAL)   ? "Partial Read" :
				    (mc->outcome == MFC_OUTCOME_FAILED)    ? "Read Failed"  :
				                                             "Cancelled");
				u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

				if (mc->outcome == MFC_OUTCOME_PARTIAL)
				{
					snprintf(ml, sizeof(ml), "Sectors: %u/%u",
					         (unsigned)mc->sectors_read, (unsigned)mc->sectors_total);
					u8g2_DrawStr(&m1_u8g2, 2, 22, ml);
					snprintf(ml, sizeof(ml), "Keys: %u/%u",
					         (unsigned)mc->keys_found, (unsigned)mc->keys_total);
					u8g2_DrawStr(&m1_u8g2, 2, 33, ml);
				}
				else   // FAILED, or CANCELLED with no data -- mfc_classify_outcome()
				       // already routes a cancelled-with-data result to PARTIAL above
				{
					u8g2_DrawStr(&m1_u8g2, 2, 22, "No sectors recovered");
				}
			}
			else
			{
				u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
				u8g2_DrawStr(&m1_u8g2, 2, 9,
				    (mc->type == M1NFC_MFCTYPE_4K) ? "MIFARE Classic 4K" : "MIFARE Classic 1K");

				u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
				u8g2_DrawStr(&m1_u8g2, 2, 20, "UID:");
				nfc_draw_hex_cells(26, 20, cc->head.uid, cc->head.uid_len);

				snprintf(ml, sizeof(ml), "Keys found: %u/%u",
				         (unsigned)mc->keys_found, (unsigned)mc->keys_total);
				u8g2_DrawStr(&m1_u8g2, 2, 31, ml);

				snprintf(ml, sizeof(ml), "Sectors read: %u/%u",
				         (unsigned)mc->sectors_read, (unsigned)mc->sectors_total);
				u8g2_DrawStr(&m1_u8g2, 2, 42, ml);
			}
		}
		else if (nfc_ctx_get()->head.family == M1NFC_FAM_ST25TB)   // ST25TB summary: Type / NFC-B / Blocks Read
		{
			nfc_run_ctx_t *tc = nfc_ctx_get();
			const nfc_st25tb_info_t *tb = nfc_ctx_get_st25tb_info();
			char tname[24];

			u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
			st25tb_fit_name(tb->variant, tname, sizeof(tname), 126);
			u8g2_DrawStr(&m1_u8g2, 2, 9, tname);

			u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
			u8g2_DrawStr(&m1_u8g2, 2, 20, "ISO14443B / ST25TB");

			if (tb->block_count > 0U)
			{
				char bl[32];
				snprintf(bl, sizeof(bl), "Blocks Read: %u/%u",
				         (unsigned)tb->blocks_read, (unsigned)tb->block_count);
				u8g2_DrawStr(&m1_u8g2, 2, 31, bl);
			}
			else   // generic/unknown: show UID rather than a guessed block count
			{
				u8g2_DrawStr(&m1_u8g2, 2, 31, "UID");
				nfc_draw_hex_cells(23, 31, tc->head.uid, tc->head.uid_len);
			}
		}
		else if (nfc_ctx_get()->head.tech == M1NFC_TECH_V)   // ISO15693 / NFC-V summary: Type / NFC-V / Blocks Read
		{
			const nfc_iso15693_info_t *v = nfc_ctx_get_iso15693_info();
			const char *vname = (v->variant == M1NFC_VVAR_SLIX)  ? "SLIX"  :
			                    (v->variant == M1NFC_VVAR_SLIX2) ? "SLIX2" : "ISO15693";

			u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
			u8g2_DrawStr(&m1_u8g2, 2, 9, vname);

			u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
			u8g2_DrawStr(&m1_u8g2, 2, 20, "NFC-V / ISO15693");

			if (v->has_sysinfo && v->block_count > 0U)
			{
				char bl[32];
				snprintf(bl, sizeof(bl), "Blocks Read: %u/%u",
				         (unsigned)v->blocks_read, (unsigned)v->block_count);
				u8g2_DrawStr(&m1_u8g2, 2, 31, bl);

				/* Block lock status (0x2C). Truthful: "n/a" when the tag did not
				 * answer the read-only security-status query (never implies 0). */
				char lk[24];
				if (v->has_security)
					snprintf(lk, sizeof(lk), "Locked: %u/%u",
					         (unsigned)v->locked_count, (unsigned)v->block_count);
				else
					snprintf(lk, sizeof(lk), "Locked: n/a");
				u8g2_DrawStr(&m1_u8g2, 2, 42, lk);
			}
			else   // no usable memory layout: show UID rather than a fabricated count
			{
				nfc_run_ctx_t *vc = nfc_ctx_get();
				u8g2_DrawStr(&m1_u8g2, 2, 31, "UID");
				nfc_draw_hex_cells(23, 31, vc->head.uid, vc->head.uid_len);
			}
		}
		else if (s_ndef_state != 0xFF)   // Type-2 (NTAG/Ultralight): NDEF-first summary
		{
			static const char *const NDEF_STATE_STR[] =
				{ "Not formatted", "Empty", "URL", "Text", "Unsupported" };

			u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
			u8g2_DrawStr(&m1_u8g2, 2, 9, (model_str[0] != '\0') ? model_str : "Type 2 Tag");

			u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
			char ndef_line[28];
			/* Password-protected indicator: honest, from what this read
			 * itself established (nfc_poller.c) -- never inferred beyond
			 * that. Three distinct cases, in priority order: genuinely
			 * confirmed AUTH0, merely SUSPECTED (a genuine protocol NAK at
			 * a deterministic boundary, but config pages themselves still
			 * unreadable -- see nfc_ctx_get_t2t_protection_suspected()),
			 * or neither (ordinary NDEF summary). Shares the NDEF line's
			 * row (a partial/protected read rarely has meaningful NDEF
			 * content to show anyway) rather than disturbing the screen's
			 * established layout for every other Type-2 tag. */
			uint8_t auth0_disp = 0xFFU;
			if (nfc_ctx_get_t2t_auth0(&auth0_disp) && (auth0_disp != 0xFFU)) {
				snprintf(ndef_line, sizeof(ndef_line), "Password protected");
			} else if (nfc_ctx_get_t2t_protection_suspected(NULL)) {
				snprintf(ndef_line, sizeof(ndef_line), "Locked - Partial Read");
			} else {
				snprintf(ndef_line, sizeof(ndef_line), "NDEF: %s", NDEF_STATE_STR[s_ndef_state]);
			}
			u8g2_DrawStr(&m1_u8g2, 2, 22, ndef_line);

			/* Readable URL/Text -> up to two char-clipped preview lines. */
			if ((s_ndef_state == 2 || s_ndef_state == 3) && s_ndef_text[0] != '\0')
			{
				const char *pv = s_ndef_text;
				for (uint8_t row = 0; row < 2 && *pv != '\0'; row++)
				{
					char seg[48]; uint8_t li = 0;
					while (*pv != '\0' && li < (uint8_t)(sizeof(seg) - 1))
					{
						seg[li] = *pv; seg[li + 1] = '\0';
						if (u8g2_GetStrWidth(&m1_u8g2, seg) > 122) { seg[li] = '\0'; break; }
						li++; pv++;
					}
					u8g2_DrawStr(&m1_u8g2, 2, (uint8_t)(33 + row * 10), seg);
				}
			}
		}
		else if (nfc_ctx_get()->head.family == M1NFC_FAM_DESFIRE)   // DESFire: model/size/UID
		{
			const nfc_desfire_info_t *df = nfc_ctx_get_desfire_info();
			u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
			if (df->present)
			{
				char nm[24];
				snprintf(nm, sizeof(nm), "DESFire %s", m1_desfire_gen_name(df->v[3]));
				u8g2_DrawStr(&m1_u8g2, 2, 9, nm);
				u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
				char sz[20];
				snprintf(sz, sizeof(sz), "Size: %s", m1_desfire_size_name(df->v[5]));
				u8g2_DrawStr(&m1_u8g2, 2, 20, sz);
			}
			else   // GetVersion unavailable: keep the correct generic Type 4A label
			{
				u8g2_DrawStr(&m1_u8g2, 2, 9, (NFC_Family[0] != '\0') ? NFC_Family : "Type 4A (ISO-DEP)");
				u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
				u8g2_DrawStr(&m1_u8g2, 2, 20, NFC_Type);
			}
			u8g2_DrawStr(&m1_u8g2, 2, 31, "UID:");
			nfc_draw_hex_cells(26, 31, NFC_ID, NFC_ID_LEN);
		}
		else   // Non-Type-2 tag: keep the generic model/type/UID/pages summary
		{
			u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
			u8g2_DrawStr(&m1_u8g2, 2, 9, (model_str[0] != '\0') ? model_str : NFC_Family);
			u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
			u8g2_DrawStr(&m1_u8g2, 2, 20, NFC_Type);
			u8g2_DrawStr(&m1_u8g2, 2, 31, "UID:");
			nfc_draw_hex_cells(26, 31, NFC_ID, NFC_ID_LEN);
			if (pages_str[0] != '\0')
				u8g2_DrawStr(&m1_u8g2, 2, 42, pages_str);
		}

		/* Only draw a control whose handler actually does something: LEFT is a
		 * silent no-op for a COMPLETE/no-data-CANCELLED MFC read
		 * (nfc_mfc_try_again_allowed()), and RIGHT would otherwise open an
		 * empty action menu for a FAILED/no-data-CANCELLED MFC read
		 * (nfc_read_more_menu_has_items()) -- both already gate the real
		 * button handlers above, so the drawn controls and the handlers can
		 * never disagree. Every non-MFC family: both helpers are always
		 * true, so this is visually unchanged. */
		bool show_retry = nfc_mfc_try_again_allowed();
		bool show_more  = nfc_read_more_menu_has_items();

		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);   // bottom-bar labels: standard font
		if (show_retry) {
			u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8); // draw arrowleft icon
			u8g2_DrawStr(&m1_u8g2, 11, 61, "Retry");
		}
		if (show_more) {
			u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8); // draw arrowright icon
			u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
		}
		if (s_ndef_state == 2 || s_ndef_state == 3)   // readable URL/Text -> centered "View" cue
			u8g2_DrawStr(&m1_u8g2, (uint8_t)((128 - u8g2_GetStrWidth(&m1_u8g2, "View")) / 2), 61, "View");
#ifdef SEE_DUMP_MEMORY
        m1_wdt_reset();
		nfc_run_ctx_t * c = nfc_ctx_get(); //DBG NFC Context(UID) Data 
		platformLog("nfc_ctx[source_kind]:%d\r\n",c->file.source_kind); //LIVE_CARD   0   LOAD_FILE   1
		platformLog("nfc_ctx[tech]:%d\r\n",c->head.tech); //enum NFC_TX_A 0, NFC_TX_B 1, NFC_TX_F 2, NFC_TX_V 3
		platformLog("nfc_ctx[uid_len]:%d\r\n",c->head.uid_len);
		platformLog("nfc_ctx[uid]: %s\r\n", hex2Str(c->head.uid, c->head.uid_len));
		platformLog("nfc_ctx[atqa0]:%02X\r\n",c->head.a.atqa[0]);
		platformLog("nfc_ctx[atqa1]:%02X\r\n",c->head.a.atqa[1]);
		platformLog("nfc_ctx[sak]:%02X\r\n",c->head.a.sak);
		platformLog("nfc_ctx[family]:%d\r\n",c->head.family);
		platformLog("nfc_ctx[unit_size]:%d\r\n",c->dump.unit_size);
		platformLog("nfc_ctx[unit_count]:%d\r\n",c->dump.unit_count);
		platformLog("nfc_ctx[has_dump]:%d\r\n",c->dump.has_dump);
		platformLog("nfc_ctx[max_seen_unit]:%lu\r\n",c->dump.max_seen_unit);
		nfc_ctx_dump_t2t_pages();
#endif
    }
    else if( param==NFC_READ_DISPLAY_PARAM_NDEF )   // dedicated NDEF-content screen (OK from summary)
    {
        nfc_info_ndef_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_MFC_DATA )   // MIFARE Classic sector/block Data view
    {
        nfc_mfc_data_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY )   // Human-readable DESFire summary
    {
        nfc_desfire_summary_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_DATA )   // DESFire GetVersion detail
    {
        nfc_desfire_data_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT )   // Interpreted transit-card summary
    {
        nfc_desfire_transit_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_APPS )   // DESFire deep read: Applications list
    {
        nfc_desfire_apps_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_APP_DETAIL )   // DESFire deep read: Application detail
    {
        nfc_desfire_app_detail_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_FILES )   // DESFire deep read: Files list
    {
        nfc_desfire_files_draw();
    }
    else if( param==NFC_READ_DISPLAY_PARAM_DESFIRE_FILE_DETAIL )   // DESFire deep read: File detail
    {
        nfc_desfire_file_detail_draw();
    }
	m1_u8g2_nextpage(); // Update display RAM
}


/*============================================================================*/
 /* @brief nfc_read_gui_message - Process messages for NFC read view
 * 
 * Handles messages from the main queue:
 * - Q_EVENT_KEYPAD: Processes button events
 * - Q_EVENT_NFC_READ_COMPLETE: Updates view to show read complete state
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_read_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	/* 150 ms poll, matching every other MFC view (Dictionary Scan, Find
	 * Missing Keys) -- needed so the MFC dictionary-phase progress screen
	 * (drawn from nfc_read_gui_update(), below) actually animates instead
	 * of sitting frozen for the entire duration of a blocking
	 * xQueueReceive(..., portMAX_DELAY). A timeout redraw is a harmless
	 * no-op for every other family/state, exactly like the existing MFC
	 * views' own 150 ms redraw-on-timeout already is. */
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(150));
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = nfc_read_kp_handler();
		}
		else if ( q_item.q_evt_type==Q_EVENT_NFC_READ_COMPLETE )
		{
			if (s_read_exiting)
			{
				/* This is the REAL worker completion the user's "Exit"
				 * confirmation has been waiting for (see
				 * NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM's OK/LEFT/RIGHT
				 * handling in nfc_read_kp_handler()) -- the worker has now
				 * genuinely wound down, so it is finally safe to tear the
				 * queue down and leave. Never shows a read result: an
				 * explicit Exit must never present a Partial/Complete
				 * result the user just asked to discard. */
				s_read_exiting = false;
				m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
				xQueueReset(main_q_hdl);
				ret_val = 0;
			}
			else
			{
				// Do other things for this task
				record_stat = NFC_RECORD_ACTIVE;
				fb_net_read_stop(); // migrated: NFC read complete
				fb_alert_success(FB_OWNER_RADIO);
				/* Info screen is now shown: wake the backlight and restart the
				 * normal 45 s inactivity timer from this moment (scanning itself
				 * is left to time out normally). */
				m1_lcd_wake_restart_timer();
				/* A card recognized by the bounded card-interpretation layer
				 * (e.g. Clipper) shows its human-readable transit result as the
				 * IMMEDIATE first view -- never the generic ISO Card Info
				 * screen first, and never behind a "More" press. This is
				 * Level 1 of the required hierarchy; the generic screen and
				 * the technical DESFire Summary ("Info", Level 2) remain
				 * reachable from here via the visible "Info" action. */
				if (nfc_ctx_get_transit()->card_id != NfcTransitCardUnknown)
				{
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT);
				}
				else
				{
					m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
				}
			}
		}
	} // if (ret==pdTRUE)
	else
	{
		/* Timeout: redraw so live MFC dictionary progress (sector, source,
		 * keys/sectors found) actually animates. A no-op redraw of the
		 * current param for every other state/family. */
		m1_uiView_display_update(nfc_uiview_gui_latest_param);
	}

	return ret_val;
} // static int nfc_read_gui_message(void)



/*============================================================================*/
 /* @brief nfc_read_gui_init - Initialize and register NFC read view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC read view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_read_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_READ, nfc_read_gui_create, nfc_read_gui_update, nfc_read_gui_destroy, nfc_read_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_emu_menu - Emulation label for the detected card.
 *
 * Label only (see the enum note): the M1 backend emulates UID/ATQA/SAK for
 * every family. "Emulate" is shown for Type 2 / NTAG and MIFARE Classic;
 * "Emulate UID" is shown where only UID emulation
 * (ISO14443-4A / DESFire); nothing for families whose emulation is not
 * offered this stage (NFC-V, ST25TB). Unknown NFC-A is a distinct family and
 * is never promoted to Classic or offered Classic emulation.
 */
/*============================================================================*/
static bool nfc_context_is_mfc(const nfc_run_ctx_t *c);

static nfc_emu_cap_t nfc_emu_menu(nfc_run_ctx_t *c)
{
    if (!c || c->head.uid_len == 0) return NFC_EMU_HIDE;

    /* Classic evidence always takes the dedicated eligibility gate. Never
     * let a damaged family byte redirect a valid MFC image or Classic SAK to
     * the generic NFC-A emitter. */
    if (nfc_context_is_mfc(c)) {
#if defined(M1_MFC_RAW_EMULATION)
        m1_mfc_emu_image_t img;
        if (m1_mfc_emu_image_build(&img) != M1_MFC_EMU_OK) return NFC_EMU_HIDE;
        return NFC_EMU_FULL;
#else
#error "M1_MFC_RAW_EMULATION is mandatory for every MIFARE Classic card"
#endif
    }

    switch (c->head.family) {
        case M1NFC_FAM_ULTRALIGHT:   /* Type 2 / NTAG        ("Emulate")              */
            return NFC_EMU_FULL;

        case M1NFC_FAM_DESFIRE:      /* ISO14443-4A          ("Emulate UID")          */
            return NFC_EMU_UID;

        case M1NFC_FAM_15693:        /* NFC-V:  emulation not offered this stage */
        case M1NFC_FAM_ST25TB:       /* NFC-B:  emulation not offered this stage */
        default:
            return NFC_EMU_HIDE;
    }
}

/*============================================================================*/
/**
 * @brief nfc_can_write_ntag21x - True for a positively-identified, writable
 *        Type 2 model: original Ultralight, NTAG203, Ultralight C, EV1
 *        MF0UL11/MF0UL21, and NTAG213/215/216. Drives the "Write" action
 *        (dump-based clone/restore of approved user pages only). Any other
 *        variant (unresolved / out of scope) returns false and hides Write.
 */
/*============================================================================*/
static bool nfc_can_write_ntag21x(nfc_run_ctx_t *c)
{
    if (!c || c->head.family != M1NFC_FAM_ULTRALIGHT) return false;
    switch (nfc_ctx_get_t2t_variant()) {
        case M1NFC_T2TVAR_UL:        /* Original Ultralight        */
        case M1NFC_T2TVAR_NTAG203:   /* NTAG203                    */
        case M1NFC_T2TVAR_ULC:       /* Ultralight C               */
        case M1NFC_T2TVAR_UL11:      /* EV1 MF0UL11                */
        case M1NFC_T2TVAR_UL21:      /* EV1 MF0UL21                */
        case M1NFC_T2TVAR_NTAG213:
        case M1NFC_T2TVAR_NTAG215:
        case M1NFC_T2TVAR_NTAG216:
            return true;
        default:
            return false;
    }
}

/*============================================================================*/
/**
 * @brief nfc_can_write_mfc - True for a LIVE MIFARE Classic 1K/4K whose
 *        full-card image (every sector's blocks and both keys) has been
 *        captured by a prior read/scan. Drives the "Write" action (clone/
 *        restore of the loaded image onto a presented card, with access-
 *        condition enforcement + read-back verify). Requires MFC_OUTCOME_
 *        COMPLETE, not merely mc->valid: writing from a partial image would
 *        silently omit or leave stale whatever wasn't captured, which the
 *        write backend is not designed to reconcile -- see mfc_result.h.
 */
/*============================================================================*/
static bool nfc_can_write_mfc(nfc_run_ctx_t *c)
{
    if (!c || c->head.family != M1NFC_FAM_CLASSIC) return false;
    const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
    return (mc && mc->valid &&
            ((mc->type == M1NFC_MFCTYPE_1K) || (mc->type == M1NFC_MFCTYPE_4K)) &&
            mfc_action_eligibility(mc->outcome).emulate_write_allowed);
}

/* Permanent routing invariant: any positive Classic evidence owns the card.
 * This deliberately does not rely on head.family alone; a stale or damaged
 * family byte must fail closed instead of exposing the generic NFC-A emitter. */
static bool nfc_context_is_mfc(const nfc_run_ctx_t *c)
{
    if (c == NULL) return false;
    if (c->head.family == M1NFC_FAM_CLASSIC) return true;
    if (c->mfc.valid) return true;
    if ((c->head.tech == M1NFC_TECH_A) && c->head.a.has_sak) {
        const uint8_t sak = (uint8_t)(c->head.a.sak & 0x1FU);
        return (sak == 0x08U) || (sak == 0x18U) || (sak == 0x09U);
    }
    return false;
}

/*============================================================================*/
/**
 * @brief nfc_can_unlock - True for a positively-identified, password-
 *        protected UL11/NTAG213/215/216 (this feature's exact supported
 *        set -- plain Ultralight, NTAG203, and Ultralight C never offer
 *        Unlock at all, matching the product spec's explicit exclusions).
 *        Drives the "Unlock" action. Shown regardless of whether this
 *        session has already authenticated -- re-entering Unlock (e.g. to
 *        try a different password) is always a valid, safe action.
 *
 *        Also true when protection is only SUSPECTED (nfc_ctx's transient,
 *        pre-auth signal -- set when a low AUTH0 makes the tag's own config
 *        pages unreadable pre-auth, so the genuine AUTH0 value can't be
 *        parsed at all yet). Manual Unlock must still be reachable in that
 *        case; nfc_unlock_run()'s own AUTHLIM gate independently keeps
 *        dictionary mode refused until AUTHLIM is genuinely confirmed.
 */
/*============================================================================*/
static bool nfc_can_unlock(nfc_run_ctx_t *c)
{
    if (!c || c->head.family != M1NFC_FAM_ULTRALIGHT) return false;
    switch (nfc_ctx_get_t2t_variant()) {
        case M1NFC_T2TVAR_UL11:
        case M1NFC_T2TVAR_NTAG213:
        case M1NFC_T2TVAR_NTAG215:
        case M1NFC_T2TVAR_NTAG216:
            break;
        default:
            return false;
    }
    uint8_t auth0 = 0xFFU;
    if (nfc_ctx_get_t2t_auth0(&auth0) && (auth0 != 0xFFU)) { return true; }
    return nfc_ctx_get_t2t_protection_suspected(NULL);
}

/*============================================================================*/
/**
 * @brief nfc_build_action_menu - Assemble the ordered action-menu for the
 *        current card context from explicit capabilities.
 *
 * Fills labels[]/actions[] and returns the item count (<= NFC_MENU_MAX_ITEMS).
 * Save is shown for every persistable LIVE family (ST25TB block data is not
 * persisted this increment, so Save stays hidden for it). The emulation item
 * follows nfc_emu_menu; Write follows nfc_can_write_ntag21x; Info is
 * always present. Both the drawer and the OK dispatcher call this single
 * builder, so labels and action indices can never drift apart.
 */
/*============================================================================*/
static uint8_t nfc_build_action_menu(const char *labels[NFC_MENU_MAX_ITEMS],
                                     uint8_t      actions[NFC_MENU_MAX_ITEMS])
{
    nfc_run_ctx_t *c = nfc_ctx_get();
    uint8_t n = 0;
    bool is_load_file = (c && c->file.source_kind == LOAD_FILE);
    bool is_st25tb    = (c && c->head.family == M1NFC_FAM_ST25TB);
    nfc_emu_cap_t emu = nfc_emu_menu(c);

    if (is_load_file) {
        /* Loaded file: already persisted, so no Save. */
        if (emu == NFC_EMU_UID)           { labels[n] = "Emulate UID"; actions[n++] = NFC_ACT_EMULATE; }
        else if (emu == NFC_EMU_FULL) { labels[n] = "Emulate";     actions[n++] = NFC_ACT_EMULATE; }
        if (nfc_can_unlock(c))                 { labels[n] = "Unlock";      actions[n++] = NFC_ACT_UNLOCK; }
        labels[n] = "Edit UID"; actions[n++] = NFC_ACT_EDIT_UID;
        labels[n] = "Info";     actions[n++] = NFC_ACT_INFO;
        labels[n] = "Rename";   actions[n++] = NFC_ACT_RENAME;
        labels[n] = "Delete";   actions[n++] = NFC_ACT_DELETE;
        return n;
    }

    /* LIVE card. For MIFARE Classic specifically, Save/Info follow the
     * outcome classifier (mfc_result.h) rather than the unconditional rule
     * every other live family still uses -- a FAILED/CANCELLED MFC read has
     * nothing worth saving or showing, and a PARTIAL one is truthfully
     * labeled "Save Partial", never plain "Save". Other families are
     * unaffected: they have no nfc_mfc_info_t.outcome concept at all. */
    bool is_mfc = (c && c->head.family == M1NFC_FAM_CLASSIC);
    mfc_action_eligibility_t mfc_elig;
    mfc_elig.save = false; mfc_elig.save_partial = false; mfc_elig.info = false;
    mfc_elig.try_again = false; mfc_elig.find_missing_keys = false; mfc_elig.emulate_write_allowed = false;
    if (is_mfc) {
        const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
        mfc_elig = mfc_action_eligibility(mc ? mc->outcome : MFC_OUTCOME_NONE);
    }

    if (is_mfc) {
        if (mfc_elig.save)              { labels[n] = "Save";         actions[n++] = NFC_ACT_SAVE; }
        else if (mfc_elig.save_partial) { labels[n] = "Save Partial"; actions[n++] = NFC_ACT_SAVE; }
        if (mfc_elig.find_missing_keys) { labels[n] = "Find Missing Keys"; actions[n++] = NFC_ACT_FIND_KEYS; }
    } else if (!is_st25tb) {
        labels[n] = "Save"; actions[n++] = NFC_ACT_SAVE;
    }
    if (emu == NFC_EMU_UID)           { labels[n] = "Emulate UID"; actions[n++] = NFC_ACT_EMULATE; }
    else if (emu == NFC_EMU_FULL) { labels[n] = "Emulate";     actions[n++] = NFC_ACT_EMULATE; }
    if (nfc_can_unlock(c))                 { labels[n] = "Unlock";      actions[n++] = NFC_ACT_UNLOCK; }
    if (nfc_can_write_ntag21x(c))          { labels[n] = "Write";       actions[n++] = NFC_ACT_WRITE; }
    if (nfc_can_write_mfc(c))              { labels[n] = "Write";       actions[n++] = NFC_ACT_WRITE; }
    /* Dictionary Scan is NOT offered here: the MIFARE Classic key tool is
     * self-contained under NFC > Tools > MIFARE Classic Keys (Dictionary Scan /
     * Key Dictionary). Keeping the read result free of it avoids an ambiguous,
     * duplicated entry point. */
    if (!is_mfc || mfc_elig.info) { labels[n] = "Info"; actions[n++] = NFC_ACT_INFO; }
    return n;
}

/*============================================================================*/
/**
 * @brief nfc_read_more_kp_handler - Handle keypad input for NFC submenu view
 * 
 * Processes button events in the NFC submenu view. The menu options
 * differ based on whether the card was read live (LIVE_CARD) or loaded
 * from file (LOAD_FILE). For LOAD_FILE, the "Save" option is excluded.
 * 
 * Button actions:
 * - BACK: Move up one level -- read view for LIVE_CARD, the Saved Files
 *   list this card was opened from for LOAD_FILE (never a full exit to
 *   the NFC parent menu; see the LOAD_FILE branch below for why).
 * - LEFT: Return to read view (LIVE_CARD only)
 * - OK: Select menu item and switch to corresponding view
 * - UP/DOWN: Navigate menu items
 *
 * @retval 1 Continue processing (both BACK branches now switch views
 *   in place rather than requesting a message-loop exit)
 */
/*============================================================================*/
static int nfc_read_more_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;
	nfc_run_ctx_t* c = nfc_ctx_get();
	bool is_load_file = (c && c->file.source_kind==LOAD_FILE);
	uint8_t menu_index;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
		{
			if (is_load_file)
			{
				/* Card from SD -> BACK moves up one logical level: the Saved
				 * Files list this card was opened from, not a full exit to
				 * the NFC parent menu (hardware finding: returning 0 here
				 * terminated nfc_saved()'s own message loop outright). Same
				 * directory-recovery pattern as NFC_ACT_DELETE just above --
				 * reopen rooted at this file's own folder (one-shot start
				 * dir; m1_fb_init() always resets to the SD root otherwise),
				 * refreshed and at the top of the list. The file browser has
				 * no persistent selection/scroll state to restore (its
				 * listing/row index is unconditionally zeroed by every
				 * m1_fb_init() call), so the previously highlighted entry
				 * and scroll offset are NOT preserved -- only the directory
				 * is. */
				char backDir[NFC_PATH_MAX];
				if (c && c->file.path[0]) {
					strncpy(backDir, c->file.path, sizeof(backDir) - 1);
					backDir[sizeof(backDir) - 1] = '\0';
					char *slash = strrchr(backDir, '/');
					if (slash && slash != backDir) *slash = '\0';
					else strcpy(backDir, NFC_SAVE_DIR);
					m1_fb_set_start_dir(backDir);
				} else {
					m1_fb_set_start_dir(NFC_SAVE_DIR);
				}
				platformLog("LOAD_FILE exit (submenu) -> VIEW_MODE_NFC_SAVED_BROWSE\r\n");
				m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);
			}
			else
			{
				// Card read live → Return to the Read Complete Screen
				platformLog("LIVE_CARD exit (submenu)\r\n");
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
			}
		} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if (!is_load_file)
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
			}
		} // else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			menu_index = m1_gui_submenu_update(NULL, 0, 0, MENU_UPDATE_NONE); // Get current index
				{
					const char  *labels[NFC_MENU_MAX_ITEMS];
					uint8_t      actions[NFC_MENU_MAX_ITEMS];
					uint8_t      count   = nfc_build_action_menu(labels, actions);
					uint8_t      view_id = 0xFF;
					(void)labels;
					if (menu_index < count)
					{
						switch (actions[menu_index])
						{
							case NFC_ACT_SAVE:     view_id = VIEW_MODE_NFC_SAVE;     break;
							case NFC_ACT_EMULATE:
								/* MIFARE Classic has exactly one emulation route: the
								 * dedicated MFC Emulate view, which validates/builds
								 * the real saved-card image itself before starting RF
								 * (nfc_mfc_emu_gui_create()) -- never the generic
								 * VIEW_MODE_NFC_EMULATE path (Ultralight/NTAG and
								 * UID-only families keep that unchanged). */
#if defined(M1_MFC_RAW_EMULATION)
								view_id = nfc_context_is_mfc(c)
								          ? VIEW_MODE_NFC_MFC_EMU : VIEW_MODE_NFC_EMULATE;
#else
#error "M1_MFC_RAW_EMULATION is mandatory: the generic Type-A MFC emulation fallback has been physically removed (see cmake/m1_01/mfc_authoritative_manifest.txt)"
#endif
								break;
							case NFC_ACT_INFO:
								/* DESFire: "Info" jumps straight to the richer
								 * DESFire Summary screen (device name/tech/UID/
								 * app count/memory) instead of the generic ISO
								 * Card Info screen (Type/ISO/UID/ATQA/SAK) --
								 * that generic screen is a strict subset of
								 * Summary's own content for this family and was
								 * a genuinely redundant extra hop, not a
								 * distinct piece of information. Every other
								 * protocol (MFC, T2T, etc.) keeps the original
								 * generic Card Info screen unchanged. */
								if (c && c->head.family == M1NFC_FAM_DESFIRE)
								{
									m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY);
									view_id = 0xFF;
								}
								else
								{
									view_id = VIEW_MODE_NFC_INFO;
								}
								break;
							case NFC_ACT_EDIT_UID: view_id = VIEW_MODE_NFC_EDIT_UID; break;
							case NFC_ACT_RENAME:   view_id = VIEW_MODE_NFC_RENAME;   break;
							case NFC_ACT_DELETE:
								if (nfc_read_more_options_delete()==0)
								{
									/* Deleted: reopen the Saved Files list rooted at the just-deleted
									 * file's folder (the /NFC folder), refreshed and at the top -- not
									 * the submenu or the SD root. One-shot start dir affects only this
									 * next browse; an empty folder shows the empty list; Back normal. */
									nfc_run_ctx_t *dc = nfc_ctx_get();
									char delDir[NFC_PATH_MAX];
									if (dc && dc->file.path[0]) {
										strncpy(delDir, dc->file.path, sizeof(delDir) - 1);
										delDir[sizeof(delDir) - 1] = '\0';
										char *slash = strrchr(delDir, '/');
										if (slash && slash != delDir) *slash = '\0';
										else strcpy(delDir, NFC_SAVE_DIR);
										m1_fb_set_start_dir(delDir);
									} else {
										m1_fb_set_start_dir(NFC_SAVE_DIR);
									}
									m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);
									break;
								}
								m1_uiView_display_update(X_MENU_UPDATE_REFRESH);
								break;
							case NFC_ACT_WRITE:
								view_id = (nfc_ctx_get()->head.family == M1NFC_FAM_CLASSIC)
								          ? VIEW_MODE_NFC_MFC_WRITE : VIEW_MODE_NFC_WRITE;
								break;
							case NFC_ACT_UNLOCK:   view_id = VIEW_MODE_NFC_UNLOCK;   break;
							case NFC_ACT_FIND_KEYS: view_id = VIEW_MODE_NFC_MFC_FIND_KEYS; break;
							default: break;
						}
					}
					if (view_id != 0xFF)
					{
						m1_uiView_display_switch(view_id, 0);
					}
				}
		} // else if(this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
		else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
		{
			m1_uiView_display_update(X_MENU_UPDATE_MOVE_UP);

		}
		else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
		{
			m1_uiView_display_update(X_MENU_UPDATE_MOVE_DOWN);
		}
	} // if (ret==pdTRUE)

	return 1;
} // static int nfc_read_more_kp_handler(void)



/*============================================================================*/
 /* @brief nfc_read_more_gui_create - Create and initialize NFC submenu view
 * 
 * Initializes the submenu view and sets the cursor index based on param.
 * If param is out of range, resets cursor to 0.
 * 
 * @param[in] param Initial cursor index (or 0xFF for refresh)
 * @retval None
 */
/*============================================================================*/
static void nfc_read_more_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}

/*============================================================================*/
 /* @brief nfc_read_more_gui_destroy - Destroy NFC submenu view
 * 
 * Cleanup function for submenu view (currently empty).
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_read_more_gui_destroy(uint8_t param)
{
	;
}

/*============================================================================*/
 /* @brief nfc_read_more_gui_update - Update NFC submenu view display
 * 
 * Updates the submenu display based on the card source (LIVE_CARD or LOAD_FILE).
 * Uses different menu arrays depending on the source kind and updates
 * the display accordingly.
 * 
 * @param[in] param Update type (0 = reset, 1 = move up, 2 = move down, 0xFF = refresh)
 * @retval None
 */
/*============================================================================*/
static void nfc_read_more_gui_update(uint8_t param)
{
	/* nfc_build_action_menu labels/count assembled from explicit capabilities. */
	static const char *labels[NFC_MENU_MAX_ITEMS];
	static uint8_t      actions[NFC_MENU_MAX_ITEMS];
	uint8_t count = nfc_build_action_menu(labels, actions);
	(void)actions;
	m1_gui_submenu_update(labels, count, 0, param);
}


/*============================================================================*/
 /* @brief nfc_read_more_gui_message - Process messages for NFC submenu view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */	
/*============================================================================*/
static int nfc_read_more_gui_message(void)
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
			ret_val = nfc_read_more_kp_handler();
		} 
		else 
		{

		}
	} 

	return ret_val;
}

/*============================================================================*/
 /* @brief nfc_read_more_gui_init - Initialize and register NFC submenu view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC submenu view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_read_more_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_READ_MORE, nfc_read_more_gui_create, nfc_read_more_gui_update, nfc_read_more_gui_destroy, nfc_read_more_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_save_kp_handler - Handle keypad input for NFC save view
 * 
 * Processes button events in the NFC save view:
 * - BACK: Return to submenu
 * - LEFT: Return to read view
 * 
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_save_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
		{
			// Do extra tasks here if needed
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		} 
		else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		{
			//m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
		}
	}

	return 1;
}

/*============================================================================*/
/**
 * @brief nfc_save_gui_create - Create and initialize NFC save view
 * 
 * Initializes the save view and triggers an update.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_save_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}

/*============================================================================*/
/**
 * @brief nfc_save_gui_destroy - Destroy NFC save view
 * 
 * Cleanup function for save view (currently empty).
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_save_gui_destroy(uint8_t param)
{
	;
}


/*============================================================================*/
/**
 * @brief nfc_save_gui_update - Update NFC save view display
 * 
 * Calls the save function and switches back to submenu if save
 * was cancelled by user (return value 3).
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_save_gui_update(uint8_t param)
{
	BaseType_t ret;

	ret = nfc_read_more_options_save();
	if ( ret==3 )
	{
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
	}
}

/*============================================================================*/
/**
 * @brief nfc_save_gui_message - Process messages for NFC save view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_save_gui_message(void)
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
			ret_val = nfc_save_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			// SAVED screen timed out -> return to the NFC action menu
			// (Save / Emulate UID / Utils / Info). Tag data/context preserved.
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
		else
		{
			; // Do other things for this task
		}
	}

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_save_gui_init - Initialize and register NFC save view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC save view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_save_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_SAVE, nfc_save_gui_create, nfc_save_gui_update, nfc_save_gui_destroy, nfc_save_gui_message);
}

/* Saved-card Ultralight/NTAG213/215/216 emulation: refusal-message pattern
 * (nfc_t2t_draw_refusal(), same style the dedicated MFC Emulate view's own
 * nfc_mfc_draw_refusal() uses), plus the Save Changes/Discard prompt shown
 * on BACK when the reader actually wrote to the session shadow.
 * s_t2t_stop_confirm: 0 = normal emulating/refused
 * display, 1 = showing the confirm prompt (BACK was already pressed once;
 * emulation already stopped, waiting for the user's Save/Discard choice
 * before leaving the view). */
static m1_t2t_emu_status_t s_t2t_emu_refused  = M1_T2T_EMU_OK;
static uint8_t             s_t2t_stop_confirm = 0;
static bool                s_nfc_emu_feedback_acquired = false;

/*============================================================================*/
/**
 * @brief nfc_emulate_kp_handler - Handle keypad input for NFC emulate view
 *
 * Processes button events in the NFC emulate view:
 * - BACK: Stop emulation and return to submenu with saved cursor index
 *
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_emulate_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if (s_t2t_stop_confirm != 0)
		{
			/* Awaiting the user's Save/Discard choice; emulation is already
			 * stopped. CENTER saves the (already-mutated) session shadow
			 * back to its source file; BACK discards -- neither path ever
			 * happens silently, matching the "BACK must not silently
			 * overwrite" / "Discard preserves the original" requirements. */
			if (this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK)
			{
				const m1_t2t_emu_image_t *img = nfc_listener_get_t2t_emu_image();
				bool saved = (img != NULL) && m1_t2t_emu_image_save(img);
				nfc_ulc_toast(saved ? "Saved" : "Save Failed");
				m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			}
			else if (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK)
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			}
			return 1;
		}

		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			ListenerRequestStop();
			/* ListenerCycle() (worker task) only observes the stop flag at
			 * the top of its next iteration -- give it a moment to actually
			 * reach IDLE before this task reads s_t2t_img, so a WRITE that
			 * was in flight the instant BACK was pressed can never race the
			 * dirty-check/save below (same handoff margin already used
			 * after Q_EVENT_NFC_START_EMULATE above). */
			osDelay(50);

			/* If the reader actually wrote to the T2T session shadow, stop
			 * short of leaving the view and ask Save/Discard first -- a
			 * silent overwrite (or a silent loss) of the saved card is
			 * exactly what this gate exists to prevent. Every other
			 * emulation kind (MFC, RAW/UID-only, refused/no image armed)
			 * is unaffected and exits immediately as before. */
			const m1_t2t_emu_image_t *img = nfc_listener_get_t2t_emu_image();
			if ((img != NULL) && img->dirty)
			{
				s_t2t_stop_confirm = 1;
				m1_uiView_display_update(0);
				return 1;
			}

			/* Return submenu to stored index */
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	}

	return 1;
}

/*============================================================================*/
 /* @brief nfc_emulate_gui_create - Create and initialize NFC emulate view
 * 
 * Initializes the emulate view. If param is 0, synchronizes the NFC context
 * to the emulator, enables LED blink, and starts the emulation process.
 * 
 * @param[in] param View parameter (0 = start emulation, other = update only)
 * @retval None
 */
/*============================================================================*/
static uint8_t nfc_emu_frame; // Emulate-screen RF-wave animation frame
/* s_t2t_emu_refused / s_t2t_stop_confirm are declared above, next to
 * nfc_emulate_kp_handler() (their first use precedes this point in the
 * file). This view is generic/T2T/T4T/UID-only: MIFARE Classic never
 * reaches it (see nfc_read_more_kp_handler()'s NFC_ACT_EMULATE dispatch --
 * Classic routes to the dedicated VIEW_MODE_NFC_MFC_EMU/
 * nfc_mfc_emu_gui_create() instead, which owns its own image build/
 * validate/arm gate). */

static void nfc_emulate_gui_create(uint8_t param)
{
	if(param==0)
	{
		s_nfc_emu_feedback_acquired = false;
        /* MIFARE Classic must never use generic Type-A emulation; it has
         * exactly one route -- the dedicated RAWOWN backend
         * (VIEW_MODE_NFC_MFC_EMU). Defensive rejection: if an identified
         * MFC card ever reaches this generic emulation handler, refuse and
         * emit no card (no generic anticollision response). */
        {
            nfc_run_ctx_t *cc = nfc_ctx_get();
            if (nfc_context_is_mfc(cc)) {
                platformLog("[EMU-REFUSED] Classic evidence reached generic NFC-A; no RF started\r\n");
                s_t2t_emu_refused = M1_T2T_EMU_NO_SOURCE; /* -> "Cannot Emulate" */
                m1_uiView_display_update(param);
                return;
            }
        }
        nfc_ctx_sync_emu();//Current nfc_ctx.head content reflected in emulator context (g_emuA)

        s_t2t_stop_confirm = 0;
        nfc_listener_clear_t2t_emu_image();   /* never inherit a stale image from a prior card */

        /* Saved-card Ultralight/NTAG213/215/216: eligibility-gate pattern
         * (see m1_t2t_emu_image.h). Non-Ultralight families are unaffected
         * -- s_t2t_emu_refused stays M1_T2T_EMU_OK. */
        s_t2t_emu_refused = M1_T2T_EMU_OK;
        {
            nfc_run_ctx_t *c = nfc_ctx_get();
            if ((c != NULL) && (c->head.family == M1NFC_FAM_ULTRALIGHT)) {
                m1_t2t_emu_image_t img;
                s_t2t_emu_refused = m1_t2t_emu_image_build(&img);
                if (s_t2t_emu_refused == M1_T2T_EMU_OK) {
                    nfc_listener_set_t2t_emu_image(&img);
                }
            }
        }

        if (s_t2t_emu_refused == M1_T2T_EMU_OK)
        {
			if (m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_START_EMULATE) == pdTRUE)
			{
				fb_net_emulating_start(); /* NFC Emulate lifecycle entry */
				s_nfc_emu_feedback_acquired = true;
				osDelay(50);
				nfc_emu_frame = 0; // Start the wave animation from the first frame
			}
			else
			{
				platformLog("[EMU-REFUSED] NFC worker queue unavailable; no RF started\r\n");
				s_t2t_emu_refused = M1_T2T_EMU_NO_SOURCE;
			}
        }
	}

    m1_uiView_display_update(param);
}

/*============================================================================*/
 /* @brief nfc_emulate_gui_destroy - Destroy NFC emulate view and cleanup resources
 * 
 * Cleans up the emulate view by turning off LED blink and sending
 * a stop emulation event to the worker queue.
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_emulate_gui_destroy(uint8_t param)
{
	if (s_nfc_emu_feedback_acquired)
	{
		(void)m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_EMULATE_STOP);
		fb_net_emulating_stop();
		s_nfc_emu_feedback_acquired = false;
	}
	/* Single authoritative cleanup point: any Save decision (kp_handler,
	 * confirm branch) has already run by the time destroy() fires, so this
	 * never discards an unsaved decision -- it only guarantees a stale
	 * image can never survive into the next Emulate entry. */
	nfc_listener_clear_t2t_emu_image();
}


/*============================================================================*/
 /* @brief nfc_emulate_gui_update - Update NFC emulate view display
 * 
 * Updates the display to show the emulation status screen with
 * instructions for the user.
 * 
 * Displays "Emulate UID" if only UID is available for emulation,
 * or "Emulate" if Page/Block dump data is available for full emulation.
 * 
 * @param[in] param View update parameter (0 = emulating)
 * @retval None
 */
/*============================================================================*/
/* Save Changes / Discard prompt, shown once BACK is pressed on an Emulate
 * session where the reader actually wrote to the T2T session shadow.
 * Mirrors nfc_add_draw_confirm()'s exact visual convention: CENTER+"Save"
 * footer, physical BACK cancels (here: discards). */
static void nfc_t2t_draw_stop_confirm(void)
{
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Card Modified");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawStr(&m1_u8g2, 2, 24, "Reader wrote to this");
    u8g2_DrawStr(&m1_u8g2, 2, 34, "tag during emulation.");

    /* Footer: CENTER + Save (physical BACK discards). */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Save");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

#if defined(M1_MFC_RAW_EMULATION)
/* Full-screen refusal state for saved-card MIFARE Classic emulation. The old
 * branch drew only one status string without starting a fresh u8g2 page, so
 * the previous action menu (including its "Emulate" row) remained underneath
 * and made the refused action look overprinted and unselectable. Keep the
 * production eligibility gate unchanged; only render its result atomically. */
static void nfc_mfc_draw_refusal(m1_mfc_emu_status_t status)
{
    const char *line1;
    const char *line2;

    switch (status) {
        case M1_MFC_EMU_UNSUPPORTED_4K:
            line1 = "MFC 4K"; line2 = "not supported"; break;
        case M1_MFC_EMU_UNSUPPORTED_UID_LEN:
            line1 = "7-byte UID"; line2 = "not supported"; break;
        case M1_MFC_EMU_INCOMPLETE_BLOCKS:
        case M1_MFC_EMU_INCOMPLETE_KEYS:
            line1 = "Complete MFC 1K"; line2 = "read required"; break;
        case M1_MFC_EMU_IDENTITY_INCONSISTENT:
            line1 = "Invalid or corrupt"; line2 = "saved card data"; break;
        case M1_MFC_EMU_START_FAILED:
            line1 = "Emulator failed"; line2 = "to start"; break;
        case M1_MFC_EMU_NO_SOURCE:
        default:
            line1 = "No MFC 1K"; line2 = "card loaded"; break;
    }

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Cannot Emulate");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawStr(&m1_u8g2, 2, 30, line1);
    u8g2_DrawStr(&m1_u8g2, 2, 42, line2);
}
#endif

/* Dedicated full-screen "Cannot Emulate" refusal state for the T2T
 * eligibility gate -- replaces the previous single DrawStr call that had no
 * u8g2_FirstPage(), so it silently drew over whatever the emulate screen
 * (or, on the very first frame, the prior menu) already had in the display
 * buffer. Physical BACK returns to the card's More menu (nfc_emulate_kp_
 * handler, unchanged); no on-screen Back control is drawn here. */
static void nfc_t2t_draw_refusal(m1_t2t_emu_status_t status)
{
    const char *line1;
    const char *line2;
    switch (status) {
        case M1_T2T_EMU_UNSUPPORTED_VARIANT:
            line1 = "Tag type"; line2 = "not supported"; break;
        case M1_T2T_EMU_INCOMPLETE_PAGES:
            line1 = "Complete tag read"; line2 = "required"; break;
        case M1_T2T_EMU_MISSING_SIGNATURE:
            line1 = "Signature missing"; line2 = "Read tag again"; break;
        case M1_T2T_EMU_GEOMETRY_INCONSISTENT:
            line1 = "Invalid saved"; line2 = "tag data"; break;
        case M1_T2T_EMU_IDENTITY_INCONSISTENT:
            line1 = "Invalid or corrupt"; line2 = "saved data"; break;
        case M1_T2T_EMU_NO_SOURCE:
        default:
            line1 = "Emulation"; line2 = "unavailable"; break;
    }

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Cannot Emulate");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawStr(&m1_u8g2, 2, 30, line1);
    u8g2_DrawStr(&m1_u8g2, 2, 42, line2);
}

static void nfc_emulate_gui_update(uint8_t param)
{
    if ( param==0 )	// emulating
    {
        if (s_t2t_stop_confirm != 0) {
            u8g2_FirstPage(&m1_u8g2);
            nfc_t2t_draw_stop_confirm();
            m1_u8g2_nextpage();
            return;
        }
        if (s_t2t_emu_refused != M1_T2T_EMU_OK) {
            u8g2_FirstPage(&m1_u8g2);
            nfc_t2t_draw_refusal(s_t2t_emu_refused);
            m1_u8g2_nextpage();
            return;
        }
		// Shared centered emulate screen: tag + animated RF waves + EMULATING...
		m1_read_icon_draw_emulate(&m1_u8g2, nfc_emu_frame);
		m1_u8g2_nextpage(); // Update display RAM
    } // if ( param==0 )
}

/*============================================================================*/
 /* @brief nfc_emulate_gui_message - Process messages for NFC emulate view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_emulate_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;

	// Short timeout so the RF-wave animation advances (~M1_READ_ICON_EMU_MS)
	// without a dedicated task; emulation itself runs in the worker task.
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(M1_READ_ICON_EMU_MS));
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		{
			// Notification is only sent to this task when there's any button activity,
			// so it doesn't need to wait when reading the event from the queue
			ret_val = nfc_emulate_kp_handler();
		}
		else
		{
			; // Do other things for this task
		}
	}
	else
	{
		nfc_emu_frame++;              // advance the RF-wave animation
		m1_uiView_display_update(0);  // redraw the current frame
	}

	return ret_val;
}

/*============================================================================*/
 /* @brief nfc_emulate_gui_init - Initialize and register NFC emulate view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC emulate view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_emulate_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_EMULATE, nfc_emulate_gui_create, nfc_emulate_gui_update, nfc_emulate_gui_destroy, nfc_emulate_gui_message);
}

/*============================================================================*/
/* MIFARE Classic Detect Reader view (Phase 1A capture). Starts the listener in
 * the MFC-detect persona via the worker, polls the isolated capture store for a
 * truthful nonce count, and stops cleanly on BACK. No key recovery here. */
/*============================================================================*/
static int nfc_detect_reader_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			m1_uiView_display_switch(VIEW_MODE_IDLE, 0);  /* destroy() stops listener + capture */
			xQueueReset(main_q_hdl);
			return 0;   /* exit the message loop -> back to the NFC menu */
		}
	}
	return 1;
}

static void nfc_detect_reader_gui_create(uint8_t param)
{
	if (param==0)
	{
		m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_START_DETECT_READER);
		osDelay(50);
		m1_led_fast_blink(LED_BLINK_ON_BLUE, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M); /* diag: listening */
	}
	m1_uiView_display_update(param);
}

static void nfc_detect_reader_gui_destroy(uint8_t param)
{
	(void)param;
	ListenerRequestStop();
	mfc_detect_end();
	m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_EMULATE_STOP);
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF); /* diag: LED off */
}

static void nfc_detect_reader_gui_update(uint8_t param)
{
	mfc_dr_state_t st = mfc_detect_state();
	const char *stage;
	uint8_t cx = 64, cy = 46;

	(void)param;
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Extract Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	/* Truthful stage text from the real backend state -- no counters. This
	 * view only captures authentication data (nonces); it never derives or
	 * validates keys itself (that is a separate, later MFC Recovery solve),
	 * so "Keys recovered" is never truthful here and is intentionally not
	 * one of the strings below. */
	if (st == MFC_DR_AUTH_ACTIVE || st == MFC_DR_DONE)
	{
		const char *captured = "Authentication data captured";
		if (u8g2_GetStrWidth(&m1_u8g2, captured) > 124)
		{
			u8g2_DrawStr(&m1_u8g2, 2, 26, "Authentication data");
			u8g2_DrawStr(&m1_u8g2, 2, 35, "captured");
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 2, 26, captured);
		}
	}
	else
	{
		if (st == MFC_DR_READER_ACTIVE) stage = "Reader traffic";
		else                            stage = "Waiting for reader...";
		u8g2_DrawStr(&m1_u8g2, 2, 26, stage);
	}

	/* Activity graphic: centred card symbol; RF arcs appear on reader activity;
	 * a filled card marks an AUTH event. Cosmetic diagnostic only. */
	if (st == MFC_DR_AUTH_ACTIVE || st == MFC_DR_DONE)
		u8g2_DrawBox(&m1_u8g2, (uint8_t)(cx - 12), (uint8_t)(cy - 8), 24, 16);
	else
		u8g2_DrawFrame(&m1_u8g2, (uint8_t)(cx - 12), (uint8_t)(cy - 8), 24, 16);
	if (st != MFC_DR_WAIT_READER && st != MFC_DR_IDLE)
	{
		u8g2_DrawCircle(&m1_u8g2, (uint8_t)(cx - 12), cy, 5,  U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_LOWER_LEFT);
		u8g2_DrawCircle(&m1_u8g2, (uint8_t)(cx - 12), cy, 9,  U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_LOWER_LEFT);
		u8g2_DrawCircle(&m1_u8g2, (uint8_t)(cx + 12), cy, 5,  U8G2_DRAW_UPPER_RIGHT|U8G2_DRAW_LOWER_RIGHT);
		u8g2_DrawCircle(&m1_u8g2, (uint8_t)(cx + 12), cy, 9,  U8G2_DRAW_UPPER_RIGHT|U8G2_DRAW_LOWER_RIGHT);
	}
	m1_u8g2_nextpage();
}

static int nfc_detect_reader_gui_message(void)
{
	static mfc_dr_state_t last_st = MFC_DR_IDLE;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;
	mfc_dr_state_t st;

	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(200));
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			ret_val = nfc_detect_reader_kp_handler();
	}
	else
	{
		m1_uiView_display_update(0);   /* refresh diagnostic stage */
	}

	/* Diagnostic LED -- UI task context only, never from the NFC callback.
	 * A brief colour pulse on each stage change: blue=listening, green=reader,
	 * red=auth. No counters. */
	st = mfc_detect_state();
	if (st != last_st)
	{
		if (st == MFC_DR_READER_ACTIVE)
			m1_led_fast_blink(LED_BLINK_ON_GREEN, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		else if (st == MFC_DR_AUTH_ACTIVE || st == MFC_DR_DONE)
			m1_led_fast_blink(LED_BLINK_ON_RED,   LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		else
			m1_led_fast_blink(LED_BLINK_ON_BLUE,  LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		last_st = st;
	}
	return ret_val;
}

static void nfc_detect_reader_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_DETECT_READER, nfc_detect_reader_gui_create, nfc_detect_reader_gui_update, nfc_detect_reader_gui_destroy, nfc_detect_reader_gui_message);
}

/* Top-level NFC menu entry (NFC -> Detect Reader). Registers the NFC views and
 * runs the Detect Reader status view loop; BACK exits back to the NFC menu. */
void nfc_detect_reader(void)
{
	platformLog("nfc_detect_reader()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_DETECT_READER, 0);
	while( m1_uiView_q_message_process() )
	{
		;
	}
	platformLog("nfc_detect_reader()-exit\r\n");
}

#if defined(M1_MFC_RAW_EMULATION)
/*--------------------------- Raw MFC Emulation (Scope B) --------------------*/
/* B1 status view: presents a saved MIFARE Classic identity and shows the live
 * activation stage from the isolated raw listener. Serial [B1-*] markers carry
 * the diagnostics; this screen is a minimal reader-activity indicator. */
static int nfc_mfc_emu_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			/* Return to this same card's action menu (Save/Emulate/Info),
			 * not the NFC root menu -- this is a normal post-read Emulate
			 * session, still within the same read flow. nfc_ctx and the
			 * completed card image are untouched here (nothing in this
			 * handler clears them), so the returned menu still reflects
			 * the real outcome. m1_uiView_display_switch()'s synchronous
			 * destroy() call (nfc_mfc_emu_gui_destroy()) is what actually
			 * requests RAWOWN STOP and waits for the STOPPED
			 * acknowledgment -- exactly once -- before this switch
			 * completes; nothing further to do here. Stays within the
			 * SAME already-running view-table message loop (no second UI
			 * loop, no nfc_mfc_emulate() launcher call), so this returns
			 * 1 (continue), not 0 (which would exit the whole read flow). */
			platformLog("[RAW-TRACE] nfc_mfc_emu_kp_handler BACK -> VIEW_MODE_NFC_READ_MORE\r\n");
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			return 1;
		}
	}
	return 1;
}

/* M1_MFC_EMU_OK = a real saved-card image was built, validated, and armed
 * this entry; anything else means the production eligibility gate refused
 * (see m1_mfc_emu_image.h) and nfc_mfc_emu_gui_update() shows that instead
 * of the activation screen. Reset fresh on every entry, never inherited. */
static m1_mfc_emu_status_t s_mfc_dedicated_refused = M1_MFC_EMU_OK;
static bool s_mfc_emu_feedback_acquired = false;
/* The UI never owns a second, inferred "started" state. RAWOWN's state is the
 * sole lifecycle authority; duplicating it here previously allowed a timeout
 * to be displayed as a successful emulation session. */

static m1_mfc_emu_status_t mfc_emu_start(void)
{
	m1_mfc_emu_image_t img;
	m1_mfc_emu_status_t status = m1_mfc_emu_image_build(&img);
	if (status != M1_MFC_EMU_OK) return status;

    if (!m1_mfc_raw_hw_prepare_start()) return M1_MFC_EMU_START_FAILED;

	(void)m1_mfc_raw_set_emu_image(&img);
    S_M1_Main_Q_t request = {0};
    request.q_evt_type = Q_EVENT_NFC_MFC_EMULATE;
    if ((nfc_worker_q_hdl == NULL) ||
        (xQueueSend(nfc_worker_q_hdl, &request, 0) != pdTRUE)) {
        (void)m1_mfc_raw_set_emu_image(NULL);
        m1_mfc_raw_hw_finish_request();
        return M1_MFC_EMU_START_FAILED;
    }
	if (!m1_mfc_raw_hw_wait_started(pdMS_TO_TICKS(2000))) {
        m1_mfc_raw_hw_cancel_start();
		platformLog("[RAWOWN-START-TIMEOUT] start not confirmed; refusing UI success\r\n");
		return M1_MFC_EMU_START_FAILED;
	}
	return M1_MFC_EMU_OK;
}

static void nfc_mfc_emu_gui_create(uint8_t param)
{
	if (param==0)
	{
		s_mfc_emu_feedback_acquired = false;
		/* Live reads and saved files converge on one coordinator and one
		 * backend. No source-kind branch and no generic fallback exist here. */
		s_mfc_dedicated_refused = mfc_emu_start();
		if (s_mfc_dedicated_refused == M1_MFC_EMU_OK) {
			fb_net_emulating_start();
			s_mfc_emu_feedback_acquired = true;
			nfc_emu_frame = 0;   // start the shared RF-wave animation from the first frame
		}
	}
	m1_uiView_display_update(param);
}

static void nfc_mfc_emu_gui_destroy(uint8_t param)
{
	(void)param;
	platformLog("[RAW-TRACE] nfc_mfc_emu_gui_destroy entry\r\n");
	if (m1_mfc_raw_hw_active()) {
		ListenerRequestStop();
		/* Requests STOP and blocks for the STOPPED acknowledgment -- never
		 * touches ST25R3916 SPI/registers directly (see m1_mfc_raw_end()). On
		 * failure the ownership gate (m1_mfc_raw_hw_active()) stays closed; this
		 * is already logged inside m1_mfc_raw_end() itself. */
		(void)m1_mfc_raw_end();
	}
	/* Always clear the armed image on exit (STOP/BACK/field loss and a
	 * refused entry alike) -- never let it leak into the next session,
	 * whether this one ran or was refused before RF started. */
	(void)m1_mfc_raw_set_emu_image(NULL);
	if (s_mfc_emu_feedback_acquired) {
		fb_net_emulating_stop();
		s_mfc_emu_feedback_acquired = false;
	}
}

static void nfc_mfc_emu_gui_update(uint8_t param)
{
	(void)param;
	u8g2_FirstPage(&m1_u8g2);

	if (s_mfc_dedicated_refused != M1_MFC_EMU_OK) {
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		nfc_mfc_draw_refusal(s_mfc_dedicated_refused);
		m1_u8g2_nextpage();
		return;
	}

	/* Same user-facing presentation every other card emulation uses (shared
	 * centered tag + animated RF waves + "EMULATING..." caption) -- not a
	 * Detect-Reader-style diagnostic (Waiting for reader / Reader active /
	 * Auth requested). Raw listener state (m1_mfc_raw_state()) still drives
	 * the LED color in nfc_mfc_emu_gui_message() below and is logged over
	 * serial; it does not belong on this screen. */
	m1_read_icon_draw_emulate(&m1_u8g2, nfc_emu_frame);
	m1_u8g2_nextpage();
}

static int nfc_mfc_emu_gui_message(void)
{
	static m1_mfc_raw_state_t last_st = M1_MFC_RAW_IDLE;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;
	m1_mfc_raw_state_t st;

	/* Same poll period as the generic Emulate view's animation timing
	 * (M1_READ_ICON_EMU_MS) -- frame advances on timeout, matching
	 * m1_read_icon_draw_emulate()'s expected cadence exactly. */
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(M1_READ_ICON_EMU_MS));
	if (ret==pdTRUE)
	{
		if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			ret_val = nfc_mfc_emu_kp_handler();
	}
	else
	{
		nfc_emu_frame++;              // advance the RF-wave animation
		m1_uiView_display_update(0);  // redraw the current frame
	}

	st = m1_mfc_raw_state();
	if (st != last_st)
	{
		if (st == M1_MFC_RAW_ACTIVE)
			m1_led_fast_blink(LED_BLINK_ON_GREEN, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		else if (st == M1_MFC_RAW_AUTH_SEEN)
			m1_led_fast_blink(LED_BLINK_ON_RED,   LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		else
			m1_led_fast_blink(LED_BLINK_ON_BLUE,  LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
		last_st = st;
	}
	return ret_val;
}

static void nfc_mfc_emu_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_MFC_EMU, nfc_mfc_emu_gui_create, nfc_mfc_emu_gui_update, nfc_mfc_emu_gui_destroy, nfc_mfc_emu_gui_message);
}

/* Top-level NFC menu entry (NFC -> Tools -> MFC Emulate). */
void nfc_mfc_emulate(void)
{
	platformLog("nfc_mfc_emulate()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_MFC_EMU, 0);
	while( m1_uiView_q_message_process() )
	{
		;
	}
	platformLog("nfc_mfc_emulate()-exit\r\n");
}
#endif /* M1_MFC_RAW_EMULATION */


/*============================================================================*/
/**
 * @brief nfc_write_kp_handler - Handle keypad input for NFC utils view
 * 
 * Processes button events in the NFC utils view:
 * - BACK: Return to submenu with saved cursor index
 * 
 * @retval 1 Continue processing
 */
/*============================================================================*/
/* ---- NTAG21x live Write UI (dump-based clone/restore) ------------------- */
extern void nfc_poller_write_abort(void);   /* nfc_poller.c */
extern void nfc_poller_mfc_scan_abort(void);/* nfc_poller.c (Stage C dictionary scan) */

/* UI phase: 0 = running (poll+write), 1 = result. Success auto-returns via
 * the shared M1 uiScreen_timeout -> Q_EVENT_MENU_TIMEOUT (same as RFID). */
static uint8_t s_wr_phase = 0;

/* Model-specific writable user-page bounds (inclusive; mirror of the poller's
 * ntag21x_user_bounds). Kept as a deliberate duplicate of the validated table -
 * both must stay consistent. NTAG213/215/216 are the production baseline. */
static bool nfc_ntag21x_bounds(uint8_t var, uint16_t *first, uint16_t *last)
{
	switch (var) {
		case M1NFC_T2TVAR_UL:      *first = 4U; *last = 15U;  return true;
		case M1NFC_T2TVAR_NTAG203: *first = 4U; *last = 39U;  return true;
		case M1NFC_T2TVAR_ULC:     *first = 4U; *last = 39U;  return true;
		case M1NFC_T2TVAR_UL11:    *first = 4U; *last = 15U;  return true;
		case M1NFC_T2TVAR_UL21:    *first = 4U; *last = 35U;  return true;
		case M1NFC_T2TVAR_NTAG213: *first = 4U; *last = 39U;  return true;
		case M1NFC_T2TVAR_NTAG215: *first = 4U; *last = 129U; return true;
		case M1NFC_T2TVAR_NTAG216: *first = 4U; *last = 225U; return true;
		default: return false;
	}
}

static int nfc_write_kp_handler(void)
{
	S_M1_Buttons_Status b;
	nfc_wr_status_t *st = nfc_ctx_get_wr_status();

	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE)
		return 1;

	if (s_wr_phase == 0)   /* RUNNING (poll/write) -> Back aborts (silent if nothing written) */
	{
		if (b.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ||
		    b.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK)
			nfc_poller_write_abort();
	}
	else                   /* RESULT */
	{
		bool wrong = (st->state==NFC_WR_FAIL &&
		              (st->err==NFC_WR_ERR_WRONG_MODEL || st->err==NFC_WR_ERR_UNSUPPORTED));
		if (wrong && b.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK)   /* Retry -> resume polling */
		{
			st->state = NFC_WR_WAIT_TARGET; st->err = NFC_WR_ERR_NONE;
			st->cur_page = 0; st->done_count = 0; st->any_written = false; st->fail_page = 0;
			s_wr_phase = 0;
			fb_net_write_start();
			m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_WRITE);
			m1_uiView_display_update(0);
		}
		else if (b.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ||
		         b.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK)
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
	}
	return 1;
}


/*============================================================================*/
 /* @brief nfc_write_gui_create - Create and initialize NFC utils view
 * 
 * Initializes the utils view and triggers an update.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_write_gui_create(uint8_t param)
{
	(void)param;
	nfc_wr_status_t *st = nfc_ctx_get_wr_status();
	uint8_t  var = nfc_ctx_get_t2t_variant();
	uint16_t first = 0U, last = 0U;

	st->err = NFC_WR_ERR_NONE; st->state = NFC_WR_IDLE;
	st->cur_page = 0; st->done_count = 0; st->any_written = false; st->fail_page = 0;
	st->src_variant = var;

	if (!nfc_ntag21x_bounds(var, &first, &last))   /* not NTAG213/215/216 */
	{
		st->state = NFC_WR_FAIL; st->err = NFC_WR_ERR_UNSUPPORTED; s_wr_phase = 1;
	}
	else
	{
		st->first_page = first; st->last_page = last;
		st->total_count = (uint16_t)(last - first + 1U);
		bool complete = true; uint8_t tmp[4];
		for (uint16_t pg = first; pg <= last; pg++)
			if (!nfc_ctx_get_t2t_page(pg, tmp)) { complete = false; break; }
		if (!complete)
		{
			st->state = NFC_WR_FAIL; st->err = NFC_WR_ERR_INCOMPLETE_SRC; s_wr_phase = 1;
			m1_buzzer_notification();   /* terminal error notification */
		}
		else
		{
			/* Locked flow: selecting Write immediately begins the write. */
			st->state = NFC_WR_WAIT_TARGET; s_wr_phase = 0;
			fb_net_write_start();
			m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_WRITE);
		}
	}
	m1_uiView_display_update(0);
}

/*============================================================================*/
 /* @brief nfc_write_gui_destroy - Destroy NFC utils view and cleanup resources
 * 
 * Cleans up the utils view by turning off LED blink indication.
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_write_gui_destroy(uint8_t param)
{
	(void)param;
	uiScreen_timeout_cancel();   /* drop any pending success auto-return timer */
	fb_net_write_stop();
}

/*============================================================================*/
 /* @brief nfc_write_gui_update - Update NFC utils view display
 * 
 * Updates the utils menu display.
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_write_gui_update(uint8_t param)
{
	(void)param;
	nfc_wr_status_t *st = nfc_ctx_get_wr_status();

	/* Standard NFC Write graphic for the active-write and success states.
	 * Failure states keep their existing dedicated screens (unchanged). */
	if (s_wr_phase == 0)                 /* RUNNING (poll + write) */
	{
		m1_draw_write_screen("WRITING...");
		return;
	}
	if (st->state == NFC_WR_DONE)        /* SUCCESS (auto-returns via timeout) */
	{
		m1_draw_write_screen("SUCCESS");
		return;
	}

	/* ---- Failure screens (unchanged): Wrong Card / generic Error ---- */
	/* Source model name for "Present <model>" -- reuse the single T2T name
	 * table (nfc_t2t_model_pages reads the source variant from nfc_ctx, which
	 * equals st->src_variant here). Covers all supported models with no
	 * duplicate mapping; empty only for an unresolved variant. */
	char modelbuf[24];
	nfc_t2t_model_pages(modelbuf, sizeof(modelbuf), NULL, 0);
	const char *model = (modelbuf[0] != '\0') ? modelbuf : "Type 2";
	bool wrong = (st->err==NFC_WR_ERR_WRONG_MODEL || st->err==NFC_WR_ERR_UNSUPPORTED);

	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 2, 8, 48, 48, nfc_emit_48x48);

	if (wrong)   /* WRONG CARD: positively-identified model/type mismatch + Retry */
	{
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		u8g2_DrawStr(&m1_u8g2, 58, 20, "Wrong card");
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 58, 32, "Present");
		u8g2_DrawStr(&m1_u8g2, 58, 42, model);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); u8g2_DrawXBMP(&m1_u8g2, 1, 54, 8, 8, arrowleft_8x8);
		u8g2_DrawStr(&m1_u8g2, 11, 61, "Retry");
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		m1_u8g2_nextpage(); return;
	}

	/* generic ERROR (cause not positively known) */
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 58, 20, res_string(IDS_WRITING));
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 58, 32, res_string(IDS_ERROR));
	if (st->any_written)   /* never claim the target is unchanged */
		u8g2_DrawStr(&m1_u8g2, 58, 44, "Partial write");
	m1_u8g2_nextpage();
}

/*============================================================================*/
 /* @brief nfc_write_gui_message - Process messages for NFC utils view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_write_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t ret_val = 1;
	nfc_wr_status_t *st = nfc_ctx_get_wr_status();

	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(120));
	if (ret==pdTRUE)
	{
		if (q_item.q_evt_type==Q_EVENT_KEYPAD)
			ret_val = nfc_write_kp_handler();
		else if (q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT)   /* M1 success auto-return */
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			return ret_val;
		}
	}

	if (s_wr_phase == 0)   /* RUNNING */
	{
		if (st->state == NFC_WR_IDLE)   /* clean cancel before any write -> silent return */
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			return ret_val;
		}
		if (st->state == NFC_WR_DONE || st->state == NFC_WR_FAIL)
		{
			s_wr_phase = 1;
			/* Stop the active-write LED on any terminal state (RFID convention). */
			fb_net_write_stop();
			if (st->state == NFC_WR_DONE)
			{
				fb_alert_success(FB_OWNER_RADIO);           /* success alert (RFID) */
				uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
			}
			else
			{
				/* Beep once on a terminal write error; the retry-able Wrong Card state does not. */
				bool wrong = (st->err==NFC_WR_ERR_WRONG_MODEL || st->err==NFC_WR_ERR_UNSUPPORTED);
				if (!wrong)
					m1_buzzer_notification();
			}
		}
		m1_uiView_display_update(0);
	}
	/* RESULT is static: success returns via MENU_TIMEOUT; wrong-card/error via Back. */

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_write_gui_init - Initialize and register NFC utils view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC utils view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_write_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_WRITE, nfc_write_gui_create, nfc_write_gui_update, nfc_write_gui_destroy, nfc_write_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_info_kp_handler - Handle keypad input for NFC info view
 * 
 * Processes button events in the NFC info view:
 * - BACK: Return to submenu with saved cursor index
 * - RIGHT: Switch to page dump view (param = 1)
 * - UP: Scroll up in page view (if in page view mode)
 * - DOWN: Scroll down in page view (if in page view mode)
 * 
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_info_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			/* Back unwinds one level: Raw(2) -> Preview(1) -> Parsed(0) -> submenu */
			if (s_info_mode==2)
			{
				m1_uiView_display_update(1);   // Raw Data -> Preview
			}
			else if (s_info_mode==1)
			{
				m1_uiView_display_update(0);   // Preview -> Parsed Info
			}
			else
			{
				m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH); // Parsed Info -> submenu
			}
		}
		else if (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK)
		{
			if (s_info_mode==0)
			{
				nfc_run_ctx_t *ic = nfc_ctx_get();
				if (ic && ic->head.family == M1NFC_FAM_CLASSIC &&
				    nfc_ctx_get_mfc_info()->valid && nfc_ctx_get_mfc_info()->keys_found > 0)
				{
					s_mfc_scroll = 0;
					m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_MFC_DATA); // Card Info -> Classic Data
				}
				else if (ic && ic->head.family == M1NFC_FAM_DESFIRE)
				{
					/* DESFire: "More" opens the human-readable DESFire Summary
					 * screen (device name/tech/UID/app+file counts/storage --
					 * matching the generic Info screen);
					 * the raw GetVersion hex dump is one further "More" away
					 * from there, never the first thing shown -- unless this
					 * card was recognized by the bounded card-interpretation
					 * layer (e.g. Clipper), in which case the user-facing
					 * interpreted summary is shown FIRST instead, matching
					 * the read-result flow (parsed summary
					 * before the generic technical view). */
					if (nfc_ctx_get_transit()->card_id != NfcTransitCardUnknown)
					{
						m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT);
					}
					else
					{
						m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_SUMMARY);
					}
				}
				else
				{
					m1_uiView_display_update(1);   // NTAG Info -> Preview ("More >")
				}
			}
			else if (s_info_mode==1)
			{
				m1_uiView_display_update(2);   // Preview -> Raw Data ("Raw Data >")
			}
		}
		else if(this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if (s_info_mode==1)
			{
				m1_uiView_display_update(0);   // Preview -> Parsed Info ("< Info")
			}
		}
		else if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if (s_info_mode==2)   // Scroll only in full Raw Data page view (one row = 2 pages)
			{
				if (s_page_scroll >= RAW_PAGES_PER_ROW)
				{
					s_page_scroll -= RAW_PAGES_PER_ROW;
					m1_uiView_display_update(2);
				}
				else if (s_page_scroll > 0)
				{
					s_page_scroll = 0;
					m1_uiView_display_update(2);
				}
			}
		}
		else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
		{
			if (s_info_mode==2)   // Scroll only in full Raw Data page view (one row = 2 pages)
			{
				uint16_t total = nfc_ctx_get_t2t_page_count();
				if (total > 0)
				{
					uint16_t max_scroll = // Max start index = total_pages - pages per screen
						(total > RAW_PAGES_PER_SCREEN)
						? (uint16_t)(total - RAW_PAGES_PER_SCREEN)
						: 0;

					if (s_page_scroll < max_scroll)
					{
						s_page_scroll = (uint16_t)(s_page_scroll + RAW_PAGES_PER_ROW);
						if (s_page_scroll > max_scroll)
							s_page_scroll = max_scroll;
						m1_uiView_display_update(2);
					}
				}
			}
		} // else if(this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	} // if (ret==pdTRUE)

	return 1;
}


/*============================================================================*/
/**
 * @brief nfc_info_gui_create - Create and initialize NFC info view
 * 
 * Initializes the info view by resetting info mode to 0 (summary screen)
 * and page scroll index to 0.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
/* NFC Forum URI Record type identifier code -> prefix (indices 0x00..0x23) */
static const char *const NDEF_URI_PREFIX[] = {
	"", "http://www.", "https://www.", "http://", "https://",
	"tel:", "mailto:", "ftp://anonymous:anonymous@", "ftp://ftp.",
	"ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://",
	"news:", "telnet://", "imap:", "rtsp://", "urn:", "pop:",
	"sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
	"tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:",
	"urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:"
};

/*============================================================================*/
/**
 * @brief nfc_ndef_parse - Decode the first NDEF record (URI or Text)
 *
 * Fully bounds-checked against msg_len. Supports Well-Known URI ("U") and
 * Text ("T") records; any other TNF/type (or malformed data) returns 0 so
 * no NDEF screen is shown. Never reads out of bounds.
 *
 * @param[in]  msg     NDEF message bytes (TLV value from nfc_ctx t2t.ndef)
 * @param[in]  msg_len Message length
 * @param[out] out     Decoded printable-ASCII string (NUL-terminated)
 * @param[in]  out_sz  Size of out
 * @retval 0 none/unsupported/malformed, 1 URL, 2 Text
 */
/*============================================================================*/
static uint8_t nfc_ndef_parse(const uint8_t *msg, uint16_t msg_len, char *out, size_t out_sz)
{
	if (!out || out_sz == 0) return 0;
	out[0] = '\0';
	if (!msg || msg_len < 3) return 0;

	uint16_t i   = 0;
	uint8_t  hdr = msg[i++];
	uint8_t  tnf = (uint8_t)(hdr & 0x07);
	bool     sr  = (hdr & 0x10) != 0;   /* short record */
	bool     il  = (hdr & 0x08) != 0;   /* ID length present */

	if (i >= msg_len) return 0;
	uint8_t  type_len = msg[i++];

	uint32_t payload_len;
	if (sr) { if (i >= msg_len) return 0; payload_len = msg[i++]; }
	else    { if ((uint32_t)i + 4U > msg_len) return 0;
	          payload_len = ((uint32_t)msg[i] << 24) | ((uint32_t)msg[i+1] << 16) |
	                        ((uint32_t)msg[i+2] << 8) | msg[i+3]; i = (uint16_t)(i + 4); }

	uint8_t id_len = 0;
	if (il) { if (i >= msg_len) return 0; id_len = msg[i++]; }

	if ((uint32_t)i + type_len > msg_len) return 0;
	const uint8_t *type = &msg[i];
	i = (uint16_t)(i + type_len + id_len);
	if (i > msg_len) return 0;

	if ((uint32_t)i + payload_len > msg_len)   /* clamp defensively */
		payload_len = (uint32_t)(msg_len - i);
	const uint8_t *payload = &msg[i];

	if (tnf == 0x01 && type_len == 1 && type[0] == 0x55 && payload_len >= 1)  /* 'U' URI */
	{
		uint8_t code = payload[0];
		const char *pfx = (code < (sizeof(NDEF_URI_PREFIX)/sizeof(NDEF_URI_PREFIX[0]))) ? NDEF_URI_PREFIX[code] : "";
		size_t o = 0;
		for (; *pfx && o < out_sz - 1; pfx++) out[o++] = *pfx;
		for (uint32_t k = 1; k < payload_len && o < out_sz - 1; k++)
		{
			char ch = (char)payload[k];
			out[o++] = (ch >= 0x20 && ch <= 0x7E) ? ch : '.';
		}
		out[o] = '\0';
		return 1;
	}
	if (tnf == 0x01 && type_len == 1 && type[0] == 0x54 && payload_len >= 1)  /* 'T' Text */
	{
		uint8_t  status   = payload[0];
		uint8_t  lang_len = (uint8_t)(status & 0x3F);
		uint32_t start    = 1U + lang_len;
		if (start > payload_len) return 0;
		size_t o = 0;
		for (uint32_t k = start; k < payload_len && o < out_sz - 1; k++)
		{
			char ch = (char)payload[k];
			out[o++] = (ch >= 0x20 && ch <= 0x7E) ? ch : '.';
		}
		out[o] = '\0';
		return 2;
	}
	return 0;  /* unsupported record */
}

/*============================================================================*/
/**
 * @brief nfc_ndef_rewrap - Word-wrap decoded text into s_ndef_lines[]
 *
 * Pixel-based wrap (<=122px) using the currently-selected font, preferring
 * space breaks and hard-wrapping long tokens (e.g. URLs). Fills the static
 * line cache used by nfc_info_ndef_draw().
 *
 * @param[in] text NUL-terminated decoded NDEF text
 * @retval None
 */
/*============================================================================*/
static void nfc_ndef_rewrap(const char *text)
{
	s_ndef_line_count = 0;
	const char *p = text;

	while (*p && s_ndef_line_count < NDEF_MAX_LINES)
	{
		char    line[NDEF_LINE_CHARS];
		uint8_t li = 0;
		int16_t last_space = -1;            /* index in line[] of last space */
		const char *last_space_src = NULL;  /* src position just after it    */

		while (*p && li < NDEF_LINE_CHARS - 1)
		{
			line[li]   = *p;
			line[li+1] = '\0';
			if (u8g2_GetStrWidth(&m1_u8g2, line) > 122) { line[li] = '\0'; break; }
			if (*p == ' ') { last_space = li; last_space_src = p + 1; }
			li++; p++;
		}

		if (*p && *p != ' ' && last_space > 0)  /* break at the last space */
		{
			line[last_space] = '\0';
			p = last_space_src;
		}
		else
		{
			line[li] = '\0';
			while (*p == ' ') p++;              /* drop leading spaces next line */
		}

		/* line is built with li < NDEF_LINE_CHARS-1, so it always fits */
		strcpy(s_ndef_lines[s_ndef_line_count], line);
		s_ndef_line_count++;
	}
}

/*============================================================================*/
/**
 * @brief nfc_info_ndef_draw - Dedicated NDEF content screen (Info mode 3)
 *
 * Title ("NDEF: URL"/"NDEF: Text") + word-wrapped content, scrollable with
 * a right-edge indicator. Main-menu fonts. Only reached when a supported
 * record was decoded (s_ndef_state >= 2).
 *
 * @retval None
 */
/*============================================================================*/
/*============================================================================*/
/**
 * @brief nfc_ndef_state - Classify the read tag's NDEF into a display state
 *
 * "Not formatted" is driven by NDEF TLV detection, NOT merely valid=false:
 * t2t.valid is set true ONLY when the T2T read scan finds a 0x03 NDEF TLV
 * (nfc_poller.c), so valid==false means no NDEF TLV was present. A TLV that
 * IS present but whose record fails to parse returns Unsupported (4), never
 * Not formatted. For URL/Text it also fills s_ndef_text with the content.
 *
 * @retval 0xFF not a Type-2 tag, 0 Not formatted, 1 Empty, 2 URL, 3 Text, 4 Unsupported
 */
/*============================================================================*/
static uint8_t nfc_ndef_state(void)
{
	nfc_run_ctx_t *c = nfc_ctx_get();
	s_ndef_text[0] = '\0';
	if (!c || c->head.family != M1NFC_FAM_ULTRALIGHT) return 0xFF;  /* not a T2T */
	if (!c->t2t.valid)         return 0;   /* no NDEF TLV -> Not formatted */
	if (c->t2t.ndef_len == 0)  return 1;   /* NDEF TLV present, empty       */
	uint8_t k = nfc_ndef_parse(c->t2t.ndef, c->t2t.ndef_len, s_ndef_text, sizeof(s_ndef_text));
	if (k == 1) return 2;   /* URL  */
	if (k == 2) return 3;   /* Text */
	return 4;               /* Unsupported */
}

/* Draws the NDEF content only (caller provides the FirstPage/nextpage wrapper). */
static void nfc_info_ndef_draw(void)
{
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 10, (s_ndef_state == 2) ? "URL" : (s_ndef_state == 3) ? "Text" : "NDEF");

	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	for (uint8_t r = 0; r < NDEF_VISIBLE; r++)
	{
		uint8_t idx = (uint8_t)(s_ndef_scroll + r);
		if (idx >= s_ndef_line_count) break;
		u8g2_DrawStr(&m1_u8g2, 2, (uint8_t)(22 + r * 9), s_ndef_lines[idx]);
	}

	if (s_ndef_line_count > NDEF_VISIBLE)   /* scroll indicator */
	{
		const uint8_t top = 18, bot = 61, th = bot - top;
		for (uint8_t yy = top; yy <= bot; yy = (uint8_t)(yy + 3)) u8g2_DrawPixel(&m1_u8g2, 125, yy);
		uint8_t max_s   = (uint8_t)(s_ndef_line_count - NDEF_VISIBLE);
		uint8_t thumb_h = (uint8_t)(((uint32_t)th * NDEF_VISIBLE) / s_ndef_line_count);
		if (thumb_h < 4) thumb_h = 4;
		uint8_t thumb_y = (uint8_t)(top + ((uint32_t)(th - thumb_h) * s_ndef_scroll) / (max_s ? max_s : 1));
		u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
	}
}

/* Prepare + open the dedicated NDEF-content screen from the summary (OK). */
static void nfc_ndef_open(void)
{
	if (s_ndef_state != 2 && s_ndef_state != 3) return;   /* only readable URL/Text open the View */
	s_ndef_scroll = 0;
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);          /* wrap uses these metrics */
	nfc_ndef_rewrap(s_ndef_text);
	m1_uiView_display_update(NFC_READ_DISPLAY_PARAM_NDEF);
}

/* Build the display rows for one MIFARE Classic block: full raw
 * dump. Every block, including the sector trailer, is rendered as its plain
 * 16 stored bytes -- two rows of four space-separated 2-byte hex groups each
 * (mfc_block_format_rows(), the real production formatter, also linked
 * directly by the host test) -- unlabelled; unread blocks use the same
 * two-row geometry with "????" groups. No Sec/Blk/A:/Ac:/B: labels and no
 * trailer interpretation -- the trailer bytes are shown exactly as read (the
 * card masks Key A to 00). Always returns 2. */
static uint8_t mfc_block_rows(const nfc_mfc_info_t *mc, uint16_t b, char rows[4][26])
{
	(void)mc;
	const uint8_t *d = nfc_ctx_mfc_block(b);   /* absolute block; NULL if unread */
	char formatted[2][24];

	mfc_block_format_rows(d, formatted);
	memcpy(rows[0], formatted[0], sizeof(formatted[0]));
	memcpy(rows[1], formatted[1], sizeof(formatted[1]));
	return 2;
}

/* MIFARE Classic Data view (draw-only; caller wraps FirstPage/nextpage).
 * Scrollable full raw hex dump in card block order; every block (trailer
 * included) shown as plain 16-byte hex, unread = ????. Back -> Card Info. */
static void nfc_mfc_data_draw(void)
{
	const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
	const uint8_t VISIBLE = 7;
	uint16_t total = (mc->type == M1NFC_MFCTYPE_4K) ? M1NFC_MFC_BLOCKS_4K : M1NFC_MFC_BLOCKS_1K;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	uint16_t row_global = 0, drawn = 0;
	for (uint16_t b = 0; b < total; b++) {
		char rows[4][26];
		uint8_t n = mfc_block_rows(mc, b, rows);
		for (uint8_t r = 0; r < n; r++) {
			if (row_global >= s_mfc_scroll && drawn < VISIBLE) {
				u8g2_DrawStr(&m1_u8g2, 2, (uint8_t)(8 + drawn * 8), rows[r]);
				drawn++;
			}
			row_global++;
		}
	}
	s_mfc_total_rows = row_global;
	if (s_mfc_total_rows > VISIBLE)
		for (uint8_t yy = 2; yy <= 60; yy = (uint8_t)(yy + 3)) u8g2_DrawPixel(&m1_u8g2, 125, yy);
}

static void nfc_info_gui_create(uint8_t param)
{
	s_info_mode   = 0;  // Initially, the Summary Information Screen
    s_page_scroll = 0;  // Page Viewer Start Index 0
	/* (NDEF now lives on the read summary, not the Info view) */

	m1_uiView_display_update(param);
}

/*============================================================================*/
/**
 * @brief nfc_info_gui_destroy - Destroy NFC info view and cleanup resources
 * 
 * Cleans up the info view by turning off LED blink indication.
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_info_gui_destroy(uint8_t param)
{
	fb_release(FB_OWNER_RADIO, FB_RES_LED); // migrated: defensive safety-net release, see reconciliation doc sec 10.3
}

/*============================================================================*/
/**
 * @brief nfc_info_gui_update - Update NFC info view display
 * 
 * Updates the display based on the info mode:
 * - param 0: Shows default card information screen (summary)
 * - param 1: Shows card sector/page dump view
 * 
 * @param[in] param View update parameter (0 = summary, 1 = page dump)
 * @retval None
 */
/*============================================================================*/
static void nfc_info_gui_update(uint8_t param)
{
    if (param==0)
    {
		//Mode 0: Parsed/summary card information screen
        s_info_mode = 0;
		nfc_info_drawing();
    }
    else if (param==1)
    {
		//Mode 1: hex+ASCII data preview (pages 0..4, no page numbers)
        s_info_mode = 1;
        nfc_info_preview_draw();
    }
    else
    {	//Mode 2: Full Raw Data page view (page numbers, scrollable)
        s_info_mode = 2;
        m1_nfc_info_more_draw();
    }
}

/*============================================================================*/
/**
 * @brief nfc_info_gui_message - Process messages for NFC info view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_info_gui_message(void)
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
			ret_val = nfc_info_kp_handler();
		}
		else
		{
			; // Do other things for this task
		}
	}

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_info_gui_init - Initialize and register NFC info view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC info view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_info_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_INFO, nfc_info_gui_create, nfc_info_gui_update, nfc_info_gui_destroy, nfc_info_gui_message);
}

/**
 * @brief nfc_info_drawing - Draw NFC card information summary screen
 * 
 * Displays the default card information screen showing:
 * - Technology type (ISO14443A/B/F/V)
 * - Card family/type name
 * - UID (Unique Identifier)
 * - ATQA and SAK values (for ISO14443A cards)
 * 
 * @retval None
 */
static void nfc_info_drawing(void)
{
    nfc_run_ctx_t* c = nfc_ctx_get();

    /* ISO15693 / NFC-V: dedicated Info layout (Type, tech, UID, DSFID/AFI,
     * Blocks Read X/Y + block size). Returns before the tech-A ATQA/SAK path. */
    /* MIFARE Classic (Phase A): minimal confirm layout (Type, tech, UID,
     * sector, auth result, blocks read). Returns before the ATQA/SAK path. */
    if (c && c->head.family == M1NFC_FAM_CLASSIC && nfc_ctx_get_mfc_info()->valid)
    {
        const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
        char cline[40];

        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_FirstPage(&m1_u8g2);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

        /* Complete read only (mc->outcome == MFC_OUTCOME_COMPLETE): a compact
         * layout that also shows the shared ISO14443A ATQA/SAK fields (the
         * old branch returned before ever reaching those). Partial/failed/
         * cancelled outcomes keep the exact prior layout below, unchanged --
         * this increment does not define a partial-result UI. */
        if (mc->outcome == MFC_OUTCOME_COMPLETE)
        {
            snprintf(cline, sizeof(cline), "MIFARE Classic %s",
                     (mc->type == M1NFC_MFCTYPE_4K) ? "4K" : "1K");
            u8g2_DrawStr(&m1_u8g2, 2, 10, cline);

            u8g2_DrawStr(&m1_u8g2, 2, 21, "UID:");
            if (c->head.uid_len > 0)
                nfc_draw_hex_cells(26, 21, c->head.uid, c->head.uid_len);

            /* Same shared ATQA/SAK convention (label + nfc_draw_hex_cells(),
             * gated on has_atqa/has_sak) as the generic tech-A Info screen
             * further below -- never fabricated when the flags are false. */
            u8g2_DrawStr(&m1_u8g2, 2, 32, "ATQA:");
            if (c->head.a.has_atqa)
                nfc_draw_hex_cells(32, 32, c->head.a.atqa, 2);
            u8g2_DrawStr(&m1_u8g2, 74, 32, "SAK:");
            if (c->head.a.has_sak)
                nfc_draw_hex_cells(98, 32, &c->head.a.sak, 1);

            snprintf(cline, sizeof(cline), "Keys: %u/%u  Sec: %u/%u",
                     (unsigned)mc->keys_found, (unsigned)mc->keys_total,
                     (unsigned)mc->sectors_read, (unsigned)mc->sectors_total);
            u8g2_DrawStr(&m1_u8g2, 2, 43, cline);

            u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Data"), 61, "Data");
            u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);

            m1_u8g2_nextpage();
            return;
        }

        /* Partial/Failed/Cancelled-with-data: same MIFARE Classic 1K/4K
         * title logic as the COMPLETE branch above (was previously
         * hardcoded to "1K" regardless of the actual detected type -- the
         * card's real type is already known the instant a card activates,
         * long before the outcome is classified, so this was always a
         * presentation bug, not a missing-data one). */
        snprintf(cline, sizeof(cline), "MIFARE Classic %s",
                 (mc->type == M1NFC_MFCTYPE_4K) ? "4K" : "1K");
        u8g2_DrawStr(&m1_u8g2, 2, 10, cline);

        u8g2_DrawStr(&m1_u8g2, 2, 21, "UID");
        if (c->head.uid_len > 0)
            nfc_draw_hex_cells(23, 21, c->head.uid, c->head.uid_len);

        /* H. Info consistency: every valid MFC result -- Complete or
         * Partial -- shows the same shared identity block (the
         * model: MFC Info always renders ISO14443-3A identity first, then
         * the MFC-specific recovery counts, unconditional on completeness).
         * Same helper, same has_atqa/has_sak gating, same field values as
         * the COMPLETE branch above -- never fabricated when the card
         * genuinely didn't report ATQA/SAK. */
        u8g2_DrawStr(&m1_u8g2, 2, 32, "ATQA:");
        if (c->head.a.has_atqa)
            nfc_draw_hex_cells(32, 32, c->head.a.atqa, 2);
        u8g2_DrawStr(&m1_u8g2, 74, 32, "SAK:");
        if (c->head.a.has_sak)
            nfc_draw_hex_cells(98, 32, &c->head.a.sak, 1);

        /* Two lines, tightened to y=42/y=51 (visual-render gate finding:
         * the original y=43/y=54 placement left "Sectors read: 15/16"
         * overlapping the unchanged "Data" label at y=61; COMPLETE's own
         * single-line "Keys: X/Y  Sec: X/Y" format was tried here too but
         * itself clips past x=128 for realistic two-digit values -- e.g.
         * "Keys: 30/32  Sec: 15/16" measures 137px at NFC_FONT_STD -- so
         * it is not reused here even though COMPLETE keeps it, since
         * COMPLETE's own branch is left untouched per this task's scope).
         * y=42/y=51 matches the 9px inter-row pitch nfc_mfc_dict_progress_draw()
         * already uses safely for this exact pair of lines. */
        snprintf(cline, sizeof(cline), "Keys found: %u/%u",
                 (unsigned)mc->keys_found, (unsigned)mc->keys_total);
        u8g2_DrawStr(&m1_u8g2, 2, 42, cline);

        snprintf(cline, sizeof(cline), "Sectors read: %u/%u",
                 (unsigned)mc->sectors_read, (unsigned)mc->sectors_total);
        u8g2_DrawStr(&m1_u8g2, 2, 51, cline);

        /* "Data >" opens the hex dump (only when something was actually read). */
        if (mc->keys_found > 0U)
        {
            u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Data"), 61, "Data");
            u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
        }

        m1_u8g2_nextpage();
        return;
    }

    /* ST25TB / SRI / SRIX: dedicated Info layout (Type, tech, UID, Chip,
     * Blocks Read X/Y, system/OTP block). Returns before the tech-A path. */
    if (c && c->head.family == M1NFC_FAM_ST25TB)
    {
        const nfc_st25tb_info_t *tb = nfc_ctx_get_st25tb_info();
        char tline[40];
        char tname[24];

        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_FirstPage(&m1_u8g2);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

        st25tb_fit_name(tb->variant, tname, sizeof(tname), 118);
        snprintf(tline, sizeof(tline), "Type: %s", tname);
        if (u8g2_GetStrWidth(&m1_u8g2, tline) > 124)
            snprintf(tline, sizeof(tline), "%s", tname);
        u8g2_DrawStr(&m1_u8g2, 2, 10, tline);

        u8g2_DrawStr(&m1_u8g2, 2, 21, "ISO14443B / ST25TB");

        u8g2_DrawStr(&m1_u8g2, 2, 32, "UID");
        if (c->head.uid_len > 0)
            nfc_draw_hex_cells(23, 32, c->head.uid, c->head.uid_len);

        if (tb->block_count > 0U)
            snprintf(tline, sizeof(tline), "Chip:%02X  Blk:%u/%u",
                     tb->chip_id, (unsigned)tb->blocks_read, (unsigned)tb->block_count);
        else
            snprintf(tline, sizeof(tline), "Chip:%02X  Blk: --", tb->chip_id);
        u8g2_DrawStr(&m1_u8g2, 2, 43, tline);

        if (tb->has_system)
            snprintf(tline, sizeof(tline), "Sys:%02X%02X%02X%02X Sz:4",
                     tb->system_otp[0], tb->system_otp[1], tb->system_otp[2], tb->system_otp[3]);
        else
            snprintf(tline, sizeof(tline), "Sys: -- Sz:4");
        u8g2_DrawStr(&m1_u8g2, 2, 54, tline);

        m1_u8g2_nextpage();
        return;
    }

    if (c && c->head.tech == M1NFC_TECH_V)
    {
        const nfc_iso15693_info_t *v = nfc_ctx_get_iso15693_info();
        const char *vname = (v->variant == M1NFC_VVAR_SLIX)  ? "SLIX"  :
                            (v->variant == M1NFC_VVAR_SLIX2) ? "SLIX2" : "ISO15693";
        char vline[40];

        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_FirstPage(&m1_u8g2);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

        snprintf(vline, sizeof(vline), "Type: %s", vname);
        u8g2_DrawStr(&m1_u8g2, 2, 10, vline);

        u8g2_DrawStr(&m1_u8g2, 2, 21, "ISO15693/NFC-V");

        /* 8-byte NFC-V UID: shorter "UID" label + cells at x=21 so all 8 bytes
         * fit within 128px (a colon + x=26 would clip the last byte). */
        u8g2_DrawStr(&m1_u8g2, 2, 32, "UID");
        if (c->head.uid_len > 0)
            nfc_draw_hex_cells(23, 32, c->head.uid, c->head.uid_len);

        if (v->has_dsfid) snprintf(vline, sizeof(vline), "DSFID:%02X", v->dsfid);
        else              snprintf(vline, sizeof(vline), "DSFID:--");
        u8g2_DrawStr(&m1_u8g2, 2, 43, vline);
        if (v->has_afi) snprintf(vline, sizeof(vline), "AFI:%02X", v->afi);
        else            snprintf(vline, sizeof(vline), "AFI:--");
        u8g2_DrawStr(&m1_u8g2, 70, 43, vline);

        if (v->has_sysinfo && v->block_count > 0U)
            snprintf(vline, sizeof(vline), "Blocks:%u/%u Sz:%u",
                     (unsigned)v->blocks_read, (unsigned)v->block_count, (unsigned)v->block_size);
        else
            snprintf(vline, sizeof(vline), "Blocks: --");
        u8g2_DrawStr(&m1_u8g2, 2, 54, vline);

        m1_u8g2_nextpage();
        return;
    }

    char type_str[32];          // ISO tech string
    char model_str[24];         // "NTAG215" (from GET_VERSION), else family title
    char pages_str[24];         // "Pages Read: 135/135"
    char line[40];

    strcpy(type_str, "Unknown");
    model_str[0] = '\0';
    pages_str[0] = '\0';

    if (c && c->head.uid_len > 0)
    {
        switch (c->head.tech)
        {
            case M1NFC_TECH_A: strcpy(type_str, "ISO14443A/NFC-A"); break;
            case M1NFC_TECH_B: strcpy(type_str, "ISO14443B/NFC-B"); break;
            case M1NFC_TECH_F: strcpy(type_str, "Felica/NFC-F");    break;
            case M1NFC_TECH_V: strcpy(type_str, "ISO15693/NFC-V");  break;
            default:           strcpy(type_str, "Unknown");           break;
        }
        nfc_t2t_model_pages(model_str, sizeof(model_str), pages_str, sizeof(pages_str));
        if (model_str[0] == '\0' && c->ui.title_text[0] != '\0')
        {
            strncpy(model_str, c->ui.title_text, sizeof(model_str) - 1);
            model_str[sizeof(model_str) - 1] = '\0';
        }
    }

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_FirstPage(&m1_u8g2);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);   /* labeled technical fields, main-menu font */

    /* First line: prefer "Type: <name>"; if the labeled form would overflow
     * 128px (long MIFARE Ultralight names), show the exact variant name
     * alone (which fits) instead of clipping it. */
    if (model_str[0] != '\0')
    {
        snprintf(line, sizeof(line), "Type: %s", model_str);
        if (u8g2_GetStrWidth(&m1_u8g2, line) > 124)
        {
            snprintf(line, sizeof(line), "%s", model_str);
        }
    }
    else
    {
        strcpy(line, "Type: Unknown");
    }
    u8g2_DrawStr(&m1_u8g2, 2, 10, line);

    snprintf(line, sizeof(line), "ISO: %s", type_str);
    u8g2_DrawStr(&m1_u8g2, 2, 21, line);

    u8g2_DrawStr(&m1_u8g2, 2, 32, "UID:");
    if (c && c->head.uid_len > 0)
        nfc_draw_hex_cells(26, 32, c->head.uid, c->head.uid_len);

    u8g2_DrawStr(&m1_u8g2, 2, 43, "ATQA:");
    if (c && c->head.a.has_atqa)
        nfc_draw_hex_cells(32, 43, c->head.a.atqa, 2);
    u8g2_DrawStr(&m1_u8g2, 74, 43, "SAK:");
    if (c && c->head.a.has_sak)
        nfc_draw_hex_cells(98, 43, &c->head.a.sak, 1);

    /* T2T Data lives behind "More >" (Right -> Preview -> Raw Data). No
     * "Pages Read" or memory-size line on this Info screen. DESFire routes the
     * same "More" to its own GetVersion Data view (Card Info > More). */
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
    u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);

    m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief nfc_info_preview_draw - Hex+ASCII data preview (mode 1)
 *
 * Renders the first up-to-NFC_INFO_LINES_PER_SCREEN Type-2 pages as raw hex
 * (left) and printable ASCII (right), separated by a vertical divider, with
 * no top header. A bottom bar shows "<Info" (Left) and "Raw Data >" (Right).
 * Read-only: page bytes are fetched via nfc_ctx_get_t2t_page(); no scrolling
 * (this is a fixed preview - full memory access is the Raw Data view).
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_info_preview_draw(void)
{
    uint16_t total = nfc_ctx_get_t2t_page_count();
    uint16_t rows  = (total < NFC_INFO_LINES_PER_SCREEN) ? total : (uint16_t)NFC_INFO_LINES_PER_SCREEN;

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_FirstPage(&m1_u8g2);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);   /* main-menu standard font */

    /* Fixed columns: hex cells (4 bytes = 2 groups), divider, then ASCII */
    const uint8_t hex_x = 2;
    const uint8_t div_x = (uint8_t)(hex_x + 4 * 2 * NFC_HEXCELL_W + NFC_HEXGRP_GAP + 3);
    const uint8_t asc_x = (uint8_t)(div_x + 4);

    for (uint16_t r = 0; r < rows; r++)
    {
        uint8_t page[4];
        if (!nfc_ctx_get_t2t_page(r, page))
        {
            continue;
        }

        char ascbuf[6];
        for (uint8_t b = 0; b < 4; b++)
        {
            ascbuf[b] = (page[b] >= 0x20 && page[b] <= 0x7E) ? (char)page[b] : '.';
        }
        ascbuf[4] = '\0';

        uint8_t y = (uint8_t)(8 + r * 10);   /* baselines 8,18,28,38,48 */
        nfc_draw_hex_cells(hex_x, y, page, 4);
        u8g2_DrawStr(&m1_u8g2, asc_x, y, ascbuf);
    }

    /* Vertical divider between hex and ASCII areas (above the bottom bar) */
    u8g2_DrawVLine(&m1_u8g2, div_x, 2, 48);

    /* Bottom navigation bar: <Info                 Raw Data> */
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8);
    u8g2_DrawStr(&m1_u8g2, 11, 61, "Info");
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Raw Data"), 61, "Raw Data");
    u8g2_DrawXBMP(&m1_u8g2, 118, 54, 8, 8, arrowright_8x8);

    m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief m1_nfc_info_more_draw - Draw NFC info more view
 *
 * Draws the NFC info more view with the page information.
 *
 * @retval None
 */
/*============================================================================*/
void m1_nfc_info_more_draw(void)
{
    /************************************************/
    // Hex-only Raw Data view:
    //   no header, no page numbers, no ASCII column.
    //   8 bytes (2 T2T pages) per row as four 4-digit
    //   groups; 5 rows; vertical scroll bar at right.
    //     044D 9E5F 263F 6180
    //     F848 0000 E110 3E00
    /************************************************/

    const uint16_t total_pages = nfc_ctx_get_t2t_page_count();

    if (total_pages == 0)
    {
        /* No dump: preserve existing "no data" behaviour */
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_FirstPage(&m1_u8g2);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
        m1_gui_let_update_fw();
        m1_u8g2_nextpage();
        return;
    }

    /* Clamp scroll start into a valid, row-aligned range */
    uint16_t max_start = (total_pages > RAW_PAGES_PER_SCREEN)
                         ? (uint16_t)(total_pages - RAW_PAGES_PER_SCREEN) : 0;
    if (s_page_scroll > max_start)
        s_page_scroll = max_start;
    const uint16_t start = s_page_scroll;

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_FirstPage(&m1_u8g2);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);   /* main-menu standard font; hex via fixed cells */

    for (uint8_t row = 0; row < NFC_INFO_LINES_PER_SCREEN; row++)
    {
        uint16_t page_a = (uint16_t)(start + (uint16_t)row * RAW_PAGES_PER_ROW);
        if (page_a >= total_pages)
            break;

        uint8_t d[8];
        uint8_t pg[4];
        memset(d, 0x00, sizeof(d));
        if (nfc_ctx_get_t2t_page(page_a, pg))
            memcpy(&d[0], pg, 4);
        if (((uint16_t)(page_a + 1) < total_pages) && nfc_ctx_get_t2t_page((uint16_t)(page_a + 1), pg))
            memcpy(&d[4], pg, 4);

        uint8_t y = (uint8_t)(10 + row * 11);   /* baselines 10,21,32,43,54 */
        nfc_draw_hex_cells(2, y, d, 8);         /* 8 bytes = four 4-digit groups */
    }

    /* Vertical scroll indicator (right edge): dotted track + solid thumb */
    {
        const uint8_t track_top = 2;
        const uint8_t track_bot = 61;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);   /* 59 */

        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);

        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * RAW_PAGES_PER_SCREEN) / total_pages);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;

        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * start) / max_start);

        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief nfc_desfire_data_draw - DESFire GetVersion detail (Card Info > More)
 *
 * Renders the full stored 28-byte GetVersion tuple (UID, HW fields, SW fields,
 * batch, production week/year) as a scrollable list
 * layout. DESFire-only terminal detail screen; Back returns to Card Info.
 *
 * @retval None
 */
/*============================================================================*/
/**
 * @brief nfc_desf_draw_str_fit - Draw a string, truncating with ".." if it
 * would exceed max_width pixels in the CURRENT font.
 *
 * Uses u8g2_GetStrWidth() -- the real, currently-loaded font's measured
 * width -- never an estimated character count. Any string this firmware
 * could ever produce (a card's own name/AID text, a worst-case numeric
 * field) is guaranteed to stay within max_width, so no DESFire screen can
 * draw text past its own row's intended right edge, regardless of content
 * this code's author did not anticipate.
 *
 * @param x         Left x position
 * @param y         Baseline y position
 * @param max_width Maximum width in pixels the string may occupy
 * @param s         String to draw (must be NUL-terminated, reasonable length)
 * @retval None
 */
/*============================================================================*/
static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)
{
    if (u8g2_GetStrWidth(&m1_u8g2, s) <= max_width) {
        u8g2_DrawStr(&m1_u8g2, x, y, s);
        return;
    }

    char buf[40];
    size_t len = strlen(s);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';

    while (len > 0) {
        len--;
        buf[len] = '\0';
        char tmp[42];
        snprintf(tmp, sizeof(tmp), "%s..", buf);
        if (u8g2_GetStrWidth(&m1_u8g2, tmp) <= max_width) {
            u8g2_DrawStr(&m1_u8g2, x, y, tmp);
            return;
        }
    }
    /* Even ".." alone doesn't fit in max_width -- draw nothing rather than
     * overflow; this only happens for a pathologically tiny max_width. */
}

/*============================================================================*/
/**
 * @brief nfc_desfire_summary_draw - Human-readable DESFire card summary
 *
 * Renders the generic MIFARE DESFire "Info" screen from captured device data:
 * device name ("MIFARE DESFire <EV1/
 * EV2/EV2 XL/EV3/UNK> <2K/4K/8K/16K/32K>", derived from the GetVersion
 * hw_major/hw_storage bytes), Tech line, UID,
 * "<N> Application(s), <M> File(s)" (or "Auth required to read apps!"),
 * and "<bytes_total>[+] bytes, <bytes_free> bytes free" (bytes_total from
 * the sw_storage byte via "1 << (sw_storage >> 1)").
 *
 * This is the screen that was MISSING before this fix -- M1 previously
 * jumped straight from the generic ISO-level Card Info screen into the
 * raw GetVersion hex dump (now nfc_desfire_data_draw(), one level deeper),
 * skipping the human-readable tier. That raw dump remains the deep "Card Info"
 * hex
 * screen; it was simply being shown too early, with nothing readable
 * before it.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_summary_draw(void)
{
    const nfc_desfire_info_t *df = nfc_ctx_get_desfire_info();
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

    if (!df->present)
    {
        u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
        u8g2_DrawStr(&m1_u8g2, 2, 9, "MIFARE DESFire");
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
        u8g2_DrawStr(&m1_u8g2, 2, 24, "No version data");
        return;
    }

    const uint8_t *v = df->v;
    const uint8_t hw_major = v[3], hw_storage = v[5], sw_storage = v[12];

    const char *type_str;
    switch (hw_major) {
        case 0x01: type_str = "EV1";    break;
        case 0x12: type_str = "EV2";    break;
        case 0x22: type_str = "EV2 XL"; break;
        case 0x33: type_str = "EV3";    break;
        default:   type_str = "UNK";    break;
    }
    const char *size_str;
    switch (hw_storage) {
        case 0x16: size_str = "2K";  break;
        case 0x18: size_str = "4K";  break;
        case 0x1A: size_str = "8K";  break;
        case 0x1C: size_str = "16K"; break;
        case 0x1E: size_str = "32K"; break;
        default:   size_str = "";    break;
    }

    /* Scrollable multi-line list, same architecture as
     * nfc_desfire_transit_draw() (TLINE-style accumulation + UP/DOWN scroll
     * + right-edge scrollbar) -- adopted here specifically because a real
     * production-font render (this file's own render test, not a photo)
     * proved every content line at the old fixed-baseline layout truncated
     * with "..": "Tech: ISO 14443-4 (..", "UID: 04 1F 66 D2 DA..",
     * "1 Application, 8 Fi..", "4096 bytes, 2016 by..". Real measured
     * widths (u8g2_GetStrWidth against the production font) showed the
     * combined strings exceed even a generous budget ("Tech: ISO 14443-4
     * (NFC-A)"=148px, "UID: <7 hex bytes>"=119px, "N Application(s), M
     * File(s)"=132px, "<total> bytes, <free> bytes free"=~184px, all over
     * the 118px safe width) while each HALF, split onto its own line,
     * measures well under budget (101/41, 21/~98, 95/52, 101/89
     * respectively) -- so long values are split across two lines below
     * instead of being shortened with an ellipsis, guaranteeing no
     * information is ever dropped regardless of scroll position. */
    #define NFC_DESF_SUMMARY_MAX_LINES 12
    static char s_summary_lines[NFC_DESF_SUMMARY_MAX_LINES][40];
    static bool s_summary_is_header[NFC_DESF_SUMMARY_MAX_LINES];
    uint8_t n = 0;

    #define SLINE_HDR(fmt, ...) do { if (n < NFC_DESF_SUMMARY_MAX_LINES) { \
        snprintf(s_summary_lines[n], sizeof(s_summary_lines[0]), fmt, ##__VA_ARGS__); \
        s_summary_is_header[n] = true; n++; } } while (0)
    #define SLINE(fmt, ...) do { if (n < NFC_DESF_SUMMARY_MAX_LINES) { \
        snprintf(s_summary_lines[n], sizeof(s_summary_lines[0]), fmt, ##__VA_ARGS__); \
        s_summary_is_header[n] = false; n++; } } while (0)

    SLINE_HDR("MIFARE DESFire");
    SLINE("%s %s", type_str, size_str);
    SLINE("Tech: ISO 14443-4");
    SLINE("(NFC-A)");
    SLINE("UID:");
    /* Split 4+3 bytes across two lines, not one 7-byte line: a real
     * production-font measurement (u8g2_GetStrWidth) showed the full 7-byte
     * hex string alone is 119px -- one pixel over the 118px safe budget,
     * still enough to trigger truncation even after moving the "UID:"
     * label to its own line. Two shorter lines both measure well under
     * budget. */
    SLINE("%02X %02X %02X %02X", v[14], v[15], v[16], v[17]);
    SLINE("%02X %02X %02X", v[18], v[19], v[20]);

    if (deep->apps_protected)
    {
        SLINE("Auth required to");
        SLINE("read apps!");
    }
    else
    {
        uint32_t file_total = 0;
        for (uint8_t i = 0; i < deep->app_count; i++)
        {
            const mf_desfire_app_t *app = &deep->apps[i];
            if (app->key_settings_valid && app->key_settings.free_directory_list)
                file_total += app->file_count;
        }
        SLINE("%u Application%s,", (unsigned)deep->app_count, (deep->app_count != 1) ? "s" : "");
        SLINE("%lu File%s", (unsigned long)file_total, (file_total != 1) ? "s" : "");

        uint32_t bytes_total = 1UL << (sw_storage >> 1);
        uint32_t bytes_free = deep->free_memory_valid ? deep->free_memory_bytes : 0;
        SLINE("%lu%s bytes total", (unsigned long)bytes_total, (sw_storage & 1U) ? "+" : "");
        SLINE("%lu bytes free", (unsigned long)bytes_free);
    }

    #undef SLINE_HDR
    #undef SLINE

    s_desf_summary_line_count = n;
    uint8_t max_start = (n > NFC_DESF_LIST_VISIBLE_ROWS) ? (uint8_t)(n - NFC_DESF_LIST_VISIBLE_ROWS) : 0;
    if (s_desf_summary_scroll > max_start) s_desf_summary_scroll = max_start;

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++) {
        uint8_t idx = (uint8_t)(s_desf_summary_scroll + r);
        if (idx >= n) break;
        u8g2_SetFont(&m1_u8g2, s_summary_is_header[idx] ? NFC_FONT_EMPH : NFC_FONT_STD);
        /* 118, not 124: leaves room for the scrollbar thumb (columns
         * 123-126) drawn below whenever n>NFC_DESF_LIST_VISIBLE_ROWS --
         * same budget and same reasoning as nfc_desfire_transit_draw(). */
        nfc_desf_draw_str_fit(2, (uint8_t)(9 + r * 11), 118, s_summary_lines[idx]);
    }
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (n > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 2, track_bot = 49;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_summary_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Full-width footer bar (matching nfc_desfire_apps_draw()/files_draw()'s
     * own "Select" footer convention) -- unconditionally overwrites rows
     * 52-63, so no body line above can ever visually collide with the
     * "More" icon or text regardless of its rendered width. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    #undef NFC_DESF_SUMMARY_MAX_LINES
}

/*============================================================================*/
static void nfc_desfire_data_draw(void)
{
    const nfc_desfire_info_t *df = nfc_ctx_get_desfire_info();
    const uint8_t *v = df->v;
    char lines[9][24];
    uint8_t n = 0;

    if (df->present)
    {
        snprintf(lines[n++], sizeof(lines[0]), "%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 v[14], v[15], v[16], v[17], v[18], v[19], v[20]);            /* UID */
        snprintf(lines[n++], sizeof(lines[0]), "hw %02X type %02X sub %02X", v[0], v[1], v[2]);
        snprintf(lines[n++], sizeof(lines[0]), " maj %02X min %02X",         v[3], v[4]);
        snprintf(lines[n++], sizeof(lines[0]), " size %02X proto %02X",      v[5], v[6]);
        snprintf(lines[n++], sizeof(lines[0]), "sw %02X type %02X sub %02X", v[7], v[8], v[9]);
        snprintf(lines[n++], sizeof(lines[0]), " maj %02X min %02X",         v[10], v[11]);
        snprintf(lines[n++], sizeof(lines[0]), " size %02X proto %02X",      v[12], v[13]);
        snprintf(lines[n++], sizeof(lines[0]), "batch %02X%02X%02X%02X%02X",
                 v[21], v[22], v[23], v[24], v[25]);
        snprintf(lines[n++], sizeof(lines[0]), "prod wk %02X yr %02X", v[26], v[27]);
    }
    else
    {
        snprintf(lines[n++], sizeof(lines[0]), "No version data");
    }

    s_desf_line_count = n;   /* used by the keypad handler for scroll bounds */

    /* "More" (RIGHT) drills into the deep-read Applications list -- only
     * offered when there is something to show there, matching every other
     * conditional "More" footer in this file (e.g. MFC Sector Keys). Shown
     * as a small corner hint (no reserved black bar), same convention as
     * nfc_info_preview_draw()'s own "More" cue; visible rows/track shrink
     * by exactly one line to make room, so the existing no-deep-data layout
     * (still the common case for older/basic DESFire cards) is completely
     * unchanged from before this milestone. */
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    bool has_more = (deep->app_count > 0) || deep->apps_protected;
    s_desf_visible_rows = has_more ? (uint8_t)(NFC_INFO_LINES_PER_SCREEN - 1) : NFC_INFO_LINES_PER_SCREEN;

    uint8_t max_start = (n > s_desf_visible_rows)
                        ? (uint8_t)(n - s_desf_visible_rows) : 0;
    if (s_desf_scroll > max_start) s_desf_scroll = max_start;

    /* The shared nfc_read_gui_update owns FirstPage/NextPage; this only draws. */
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    for (uint8_t r = 0; r < s_desf_visible_rows; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_scroll + r);
        if (idx >= n) break;
        u8g2_DrawStr(&m1_u8g2, 2, (uint8_t)(10 + r * 11), lines[idx]);
    }

    /* Vertical scroll indicator (only when the list overflows one screen). */
    if (n > s_desf_visible_rows)
    {
        const uint8_t track_top = 2;
        const uint8_t track_bot = has_more ? 49 : 61;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);

        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * s_desf_visible_rows) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;

        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_scroll) / max_start);

        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    if (has_more)
    {
        u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
        u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    }
}

/*============================================================================*/
/**
 * @brief nfc_desfire_transit_draw - Interpreted transit-card summary
 *
 * Renders the parsed Clipper output field-for-field: exact
 * order, exact labels, exact value formatting (including its "$%d.%02u"
 * balance/fare format, which prints the sign inside the currency string
 * for negative amounts, e.g. "$-1.23" -- a faithful reproduction of
 * the currency formatter, not smoothed over). The `\e#`
 * bold section headers ("Clipper", "Last Update", "Ride Record") become
 * their own EMPH-font line here, since M1 has no inline rich-text
 * rendering -- the one adaptation this screen makes, calling M1's graphics
 * primitives without changing field
 * order, wording, or visibility rules.
 *
 * Shown ONLY when the bounded card-interpretation layer's all-or-nothing
 * gate succeeded (nfc_ctx_get_transit()->card_id == NfcTransitCardClipper
 * is set by desf_interpret_card() only in that case): parsing either returns fully populated
 * text or false (falling back to the generic DESFire screens); there is no
 * partial Clipper-branded screen with some fields
 * blank, so this screen never does either.
 *
 * RIGHT/"More" leads to the existing, unchanged technical DESFire Data
 * screen, which remains reachable for every card regardless of
 * recognition, matching the "parsed summary -> More -> Info"
 * chain (see nfc_desfire_data_draw()).
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_transit_draw(void)
{
    const nfc_transit_info_t *t = nfc_ctx_get_transit();

    if (t->card_id != NfcTransitCardClipper)
    {
        /* Should be unreachable (the caller only switches to this screen
         * when card_id==Clipper) -- defensive fallback that draws nothing
         * rather than ever showing a half-populated screen. */
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
        u8g2_DrawStr(&m1_u8g2, 2, 9, "Transit Card");
        return;
    }

    #define NFC_DESF_TRANSIT_MAX_LINES 180
    static char s_transit_lines[NFC_DESF_TRANSIT_MAX_LINES][48];
    static bool s_transit_is_header[NFC_DESF_TRANSIT_MAX_LINES];
    uint8_t n = 0;

    #define TLINE_HDR(fmt, ...) do { if (n < NFC_DESF_TRANSIT_MAX_LINES) { \
        snprintf(s_transit_lines[n], sizeof(s_transit_lines[0]), fmt, ##__VA_ARGS__); \
        s_transit_is_header[n] = true; n++; } } while (0)
    #define TLINE(fmt, ...) do { if (n < NFC_DESF_TRANSIT_MAX_LINES) { \
        snprintf(s_transit_lines[n], sizeof(s_transit_lines[0]), fmt, ##__VA_ARGS__); \
        s_transit_is_header[n] = false; n++; } } while (0)

    /* \e#Clipper / Serial / Balance / Type / \e#Last Update / date+time-or-
     * "Never" / Terminal / Transaction Id / Counter -- clipper.c:272-295. */
    TLINE_HDR("Clipper");
    TLINE("Serial: %lu", (unsigned long)t->serial_number);
    {
        bool neg; int16_t dollars; uint16_t cents;
        nfc_transit_usd_split(t->balance_cents, &neg, &dollars, &cents);
        (void)neg; /* Sign state is not used directly for display. */
        TLINE("Balance: $%d.%02u", (int)dollars, (unsigned)cents);
    }
    TLINE("Type: %s", (t->card_type_label != NULL) ? t->card_type_label : "Card");

    TLINE_HDR("Last Update");
    if (t->last_update_1900 == 0U) {
        TLINE("Never"); /* clipper.c:287 -- literal, no Date:/Time: lines in this case */
    } else {
        uint16_t y; uint8_t mo, d, h, mi, s;
        nfc_transit_epoch1900_to_ymdhms(t->last_update_1900, &y, &mo, &d, &h, &mi, &s);
        TLINE("Date: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
        TLINE("Time: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
    }
    TLINE("Terminal: 0x%04x", (unsigned)t->last_terminal_id);
    TLINE("Transaction Id: %u", (unsigned)t->last_txn_id);
    TLINE("Counter: %u", (unsigned)t->counter);

    /* \e#Ride Record per entry, in histidx order -- clipper.c:479-500. */
    for (uint8_t ri = 0; ri < t->ride_count; ri++) {
        const nfc_transit_ride_t *ride = &t->rides[ri];
        TLINE_HDR("Ride Record");

        if (ride->has_time_on) {
            uint16_t y; uint8_t mo, d, h, mi, s;
            nfc_transit_epoch1900_to_ymdhms(ride->time_on_1900, &y, &mo, &d, &h, &mi, &s);
            TLINE("Date: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
            TLINE("Time: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
        }
        {
            bool neg; int16_t dollars; uint16_t cents;
            nfc_transit_usd_split(ride->fare_cents, &neg, &dollars, &cents);
            (void)neg;
            TLINE("Fare: $%d.%02u", (int)dollars, (unsigned)cents);
        }
        {
            const char *agency = nfc_transit_clipper_agency_name(ride->agency_id);
            TLINE("Agency: %s (%04x)", (agency != NULL) ? agency : "Unknown", (unsigned)ride->agency_id);
        }
        {
            const char *zone_on = nfc_transit_clipper_zone_name(ride->agency_id, ride->zone_on_id);
            TLINE("On: %s (%04x)", (zone_on != NULL) ? zone_on : "Unknown", (unsigned)ride->zone_on_id);
        }
        if (ride->vehicle_id != 0U) {
            TLINE("Vehicle id: %u", (unsigned)ride->vehicle_id);
        }
        if (ride->has_time_off) {
            const char *zone_off = nfc_transit_clipper_zone_name(ride->agency_id, ride->zone_off_id);
            TLINE("Off: %s (%04x)", (zone_off != NULL) ? zone_off : "Unknown", (unsigned)ride->zone_off_id);
            uint16_t y; uint8_t mo, d, h, mi, s;
            nfc_transit_epoch1900_to_ymdhms(ride->time_off_1900, &y, &mo, &d, &h, &mi, &s);
            TLINE("Date Off: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
            TLINE("Time Off: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
        }
    }

    #undef TLINE_HDR
    #undef TLINE

    s_desf_transit_line_count = n;
    uint8_t max_start = (n > NFC_DESF_LIST_VISIBLE_ROWS) ? (uint8_t)(n - NFC_DESF_LIST_VISIBLE_ROWS) : 0;
    if (s_desf_transit_scroll > max_start) s_desf_transit_scroll = max_start;

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++) {
        uint8_t idx = (uint8_t)(s_desf_transit_scroll + r);
        if (idx >= n) break;
        u8g2_SetFont(&m1_u8g2, s_transit_is_header[idx] ? NFC_FONT_EMPH : NFC_FONT_STD);
        /* 118, not 124: the scrollbar thumb drawn below occupies columns
         * 123-126 whenever n>NFC_DESF_LIST_VISIBLE_ROWS, which is true for
         * nearly every real Clipper card (the unconditional base field set
         * alone is 8-9 lines) -- the old 124 budget let truncated text reach
         * column 123, directly under the thumb (hardware-proven overlap). */
        nfc_desf_draw_str_fit(2, (uint8_t)(9 + r * 11), 118, s_transit_lines[idx]);
    }
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (n > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 2, track_bot = 49;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_transit_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* "Info", not "More": Level 1 (this screen) -> Level 2 must be visibly
     * labelled "Info" per the required hierarchy -- Level 2 is the
     * technical DESFire Summary screen (UID/protocol/version/app count/
     * memory), reached via the identical RIGHT-button action as before,
     * just relabelled. */
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Info"), 61, "Info");
    u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    #undef NFC_DESF_TRANSIT_MAX_LINES
}

/*============================================================================*/
/**
 * @brief nfc_desfire_apps_draw - DESFire deep read: Applications list
 *
 * Scrollable, selectable list of the applications captured during the deep
 * read (bounded by MF_DESFIRE_DEEP_MAX_APPS). CENTER opens the highlighted
 * application's detail screen. If GetApplicationIDs itself required
 * authentication, this screen shows that explicitly rather than an empty
 * list. This screen's row/box/scrollbar geometry is the template
 * nfc_mfc_keys_draw_list() (MIFARE Classic Keys user-key list) reuses.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_apps_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Applications");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (deep->app_count == 0)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24,
                     deep->apps_protected ? "Auth required to" : "No applications");
        if (deep->apps_protected)
            u8g2_DrawStr(&m1_u8g2, 2, 35, "list applications");
        return;
    }

    if (s_desf_app_sel >= deep->app_count) s_desf_app_sel = (uint8_t)(deep->app_count - 1);

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_apps_scroll + r);
        if (idx >= deep->app_count) break;
        const mf_desfire_app_t *app = &deep->apps[idx];
        char row[28];
        /* "App %02x%02x%02x", lowercase, AID bytes reversed (data[2],[1],[0])
         * -- the submenu label. The
         * list never annotates a non-selectable app inline -- that failure is
         * only revealed on the App Detail screen after selecting it. */
        snprintf(row, sizeof(row), "App %02x%02x%02x", app->id.id[2], app->id.id[1], app->id.id[0]);
        /* Baselines 20,30,40,50 -- box top (y-8) gives rows 12,22,32,42, a
         * full row of real clearance below the header's measured ink-bottom
         * row 11 (verified by real u8g2 rendering + pixel scan, not font
         * metrics alone -- a baseline of 19 left zero margin, touching the
         * header at row 11 exactly). Box bottom for the last row (42+9=51)
         * stays a full row clear of the y=52 footer bar. Replaces the old
         * 12+r*11/y-9,h11 formula, whose row 0 box (y=3..14) directly
         * overlapped the header (hardware-proven). */
        uint8_t y = (uint8_t)(20 + r * 10);
        if (idx == s_desf_app_sel)
        {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(y - 8), 121, 10);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            nfc_desf_draw_str_fit(2, y, 116, row);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        }
        else
        {
            nfc_desf_draw_str_fit(2, y, 116, row);
        }
    }

    if (deep->app_count > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 12, track_bot = 50;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        uint8_t max_start = (uint8_t)(deep->app_count - NFC_DESF_LIST_VISIBLE_ROWS);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / deep->app_count);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_apps_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Footer: CENTER + Select -> the highlighted application's detail. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/*============================================================================*/
/**
 * @brief nfc_desfire_app_detail_draw - DESFire deep read: selected Application detail
 *
 * Key settings summary and file count for the application selected on the
 * Applications list. RIGHT/"More" opens the Files list only when the
 * application was actually selectable and has at least one file.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_app_detail_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
    char line[28];

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    snprintf(line, sizeof(line), "App %02X%02X%02X", app->id.id[0], app->id.id[1], app->id.id[2]);
    nfc_desf_draw_str_fit(2, 9, 124, line);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (!app->select_ok)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "Could not select");
        u8g2_DrawStr(&m1_u8g2, 2, 35, "this application");
        return;
    }

    uint8_t y = 22;
    if (app->key_settings_valid)
    {
        snprintf(line, sizeof(line), "Max keys: %u", (unsigned)app->key_settings.max_keys);
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
        snprintf(line, sizeof(line), "Free dir list: %s", app->key_settings.free_directory_list ? "Yes" : "No");
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, y, "Key settings: n/a"); y = (uint8_t)(y + 11);
    }

    if (app->key_version_count > 0)
    {
        snprintf(line, sizeof(line), "Key versions: %u%s", (unsigned)app->key_version_count,
                 app->key_versions_truncated ? "+" : "");
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
    }

    snprintf(line, sizeof(line), "Files: %u%s", (unsigned)app->file_count, app->files_truncated ? "+" : "");
    u8g2_DrawStr(&m1_u8g2, 2, y, line);

    if (app->file_count > 0)
    {
        u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
        u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    }
}

/*============================================================================*/
/**
 * @brief nfc_desfire_files_draw - DESFire deep read: Files list within an Application
 *
 * Same scrollable/selectable list style as the Applications screen. Each
 * row shows the file ID, type, and read outcome so a protected/unsupported
 * file is distinguishable from a fully or partially captured one before
 * drilling into its detail.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_files_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Files");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (app->file_count == 0)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "No files");
        return;
    }

    if (s_desf_file_sel >= app->file_count) s_desf_file_sel = (uint8_t)(app->file_count - 1);

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_files_scroll + r);
        if (idx >= app->file_count) break;
        const mf_desfire_file_t *file = &app->files[idx];
        char row[28];
        /* "File %d", decimal, nothing else. Type/comm/access-rights/read-status
         * are File Detail (raw hex TextBox) content, never
         * shown in its list -- they remain available on M1's own File
         * Detail screen after selecting, just no longer crammed into this
         * row abbreviated to fit. */
        snprintf(row, sizeof(row), "File %u", (unsigned)file->id);
        /* Same corrected geometry as nfc_desfire_apps_draw() -- see the
         * comment there for the header/footer clearance derivation. */
        uint8_t y = (uint8_t)(20 + r * 10);
        if (idx == s_desf_file_sel)
        {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(y - 8), 121, 10);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            nfc_desf_draw_str_fit(2, y, 116, row);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        }
        else
        {
            nfc_desf_draw_str_fit(2, y, 116, row);
        }
    }

    if (app->file_count > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 12, track_bot = 50;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        uint8_t max_start = (uint8_t)(app->file_count - NFC_DESF_LIST_VISIBLE_ROWS);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / app->file_count);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_files_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Footer: CENTER + Select -> the highlighted file's detail. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/*============================================================================*/
/**
 * @brief nfc_desfire_file_detail_draw - DESFire deep read: selected File settings + content
 *
 * Terminal screen: file type/comm/access rights/size-or-record-geometry,
 * the explicit read outcome (Complete/Partial/Protected/Unsupported/
 * Failed/Not read), and a scrollable hex dump of whatever content was
 * actually captured (Complete or Partial only -- every other outcome shows
 * its status wording instead of a dump, never a fabricated/empty-looking
 * one). No further "More": this is the bottom of the hierarchy.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_desfire_file_detail_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
    const mf_desfire_file_t *file = &app->files[s_desf_file_sel];
    const mf_desfire_file_settings_t *s = &file->settings;

    /* Worst case: 6 header lines (Comm, AR, Status, plus up to 3 more for a
     * Linear/Cyclic Record file's size/cur/max, each on its own line so no
     * single line can ever combine multiple large 24-bit DESFire fields --
     * see the hardware finding this replaced: "Rec size, cur/max" as one
     * combined line measured up to 197px wide against a 128px display)
     * plus a hex dump at 6 bytes/line (measured 108px, safely under 128;
     * 8 bytes/line measured 144px -- the confirmed hardware overflow).
     * Every write below is still bounds-checked against this capacity
     * regardless -- the arithmetic here only sizes the buffer. */
    #define NFC_DESF_HEXBYTES_PER_LINE 6U
    #define NFC_DESF_FILEDETAIL_MAX_LINES (6 + (MF_DESFIRE_DEEP_FILE_DATA_CAP + NFC_DESF_HEXBYTES_PER_LINE - 1) / NFC_DESF_HEXBYTES_PER_LINE)
    char lines[NFC_DESF_FILEDETAIL_MAX_LINES][24];
    uint8_t n = 0;

    char hdr[28];
    snprintf(hdr, sizeof(hdr), "File %02X %s", file->id, mf_desfire_file_type_name(s->type));
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    nfc_desf_draw_str_fit(2, 9, 124, hdr);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (file->settings_valid && (n < NFC_DESF_FILEDETAIL_MAX_LINES))
    {
        snprintf(lines[n++], sizeof(lines[0]), "Comm: %s", mf_desfire_comm_name(s->comm));
        if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
        {
            snprintf(lines[n++], sizeof(lines[0]), "AR: %04X", s->access_rights);
        }
        switch (s->type)
        {
        case MfDesfireFileTypeStandard:
        case MfDesfireFileTypeBackup:
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Size: %lu bytes", (unsigned long)s->size);
            break;
        case MfDesfireFileTypeValue:
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Lo:%ld Hi:%ld", (long)s->value_lo_limit, (long)s->value_hi_limit);
            break;
        case MfDesfireFileTypeLinearRecord:
        case MfDesfireFileTypeCyclicRecord:
            /* Each field on its own line -- a 24-bit DESFire field can be
             * up to 16,777,215; combining even two of these on one line
             * was the exact combination that overflowed on hardware. */
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Rec size: %lu B", (unsigned long)s->record_size);
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Cur: %lu", (unsigned long)s->record_cur);
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Max: %lu", (unsigned long)s->record_max);
            break;
        default:
            break;
        }
    }

    if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
    {
        snprintf(lines[n++], sizeof(lines[0]), "Status: %s", mf_desfire_file_read_status_name(file->read_status));
    }

    if ((file->read_status == MfDesfireFileComplete || file->read_status == MfDesfireFilePartial) &&
        (file->data_len > 0U))
    {
        for (uint16_t i = 0; i < file->data_len; i += NFC_DESF_HEXBYTES_PER_LINE)
        {
            if (n >= NFC_DESF_FILEDETAIL_MAX_LINES) break; /* checked BEFORE writing, not after */
            uint16_t chunk = (uint16_t)((file->data_len - i < NFC_DESF_HEXBYTES_PER_LINE) ? (file->data_len - i) : NFC_DESF_HEXBYTES_PER_LINE);
            char *dst = lines[n];
            size_t pos = 0;
            for (uint16_t k = 0; k < chunk && pos < sizeof(lines[0]) - 3U; k++)
            {
                pos += (size_t)snprintf(dst + pos, sizeof(lines[0]) - pos, "%02X ", file->data[i + k]);
            }
            n++;
        }
    }

    s_desf_filedetail_line_count = n;
    uint8_t max_start = (n > NFC_INFO_LINES_PER_SCREEN) ? (uint8_t)(n - NFC_INFO_LINES_PER_SCREEN) : 0;
    if (s_desf_filedetail_scroll > max_start) s_desf_filedetail_scroll = max_start;

    for (uint8_t r = 0; r < NFC_INFO_LINES_PER_SCREEN; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_filedetail_scroll + r);
        if (idx >= n) break;
        /* 118, not 124: same scrollbar-gutter reservation as the Transit
         * screen -- latent here today (no reachable line gets within 14px of
         * the thumb) but the same root defect, fixed proactively. */
        nfc_desf_draw_str_fit(2, (uint8_t)(21 + r * 10), 118, lines[idx]);
    }

    if (n > NFC_INFO_LINES_PER_SCREEN)
    {
        const uint8_t track_top = 12, track_bot = 61;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_INFO_LINES_PER_SCREEN) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_filedetail_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }
    #undef NFC_DESF_FILEDETAIL_MAX_LINES
    #undef NFC_DESF_HEXBYTES_PER_LINE
}

/* ---- NFC Tools > MIFARE Classic Keys (Step 5: dashboard + Add/List/Delete) --
 * Provides MIFARE Classic key management (system count, user count,
 * CENTER Add, RIGHT List only when user keys exist, manual-entry Add, user-
 * only List, select-then-confirm Delete, read-only system keys) with M1's own
 * dashboard-first landing and explanatory copy.
 *
 * Snapshot (mfc_keys_t) is ~6 KB -> static (never on the stack), and holds
 * ONLY the compiled built-ins + the bounded user dictionary -- exactly what
 * it held before this increment. The system dictionary is NEVER loaded into
 * it: the "System: N" count streams/counts through mfc_key_source (the same
 * canonical [USER, SYSTEM] source list m1_mfc_build_key_sources() gives the
 * acquisition session), so a multi-thousand-key SD file costs no extra RAM
 * here and can never be "truncated" by this UI's snapshot cap. */
static mfc_keys_t             s_mfck;
static mfc_keys_load_status_t s_mfck_st;
static uint32_t               s_mfck_sys_count = 0;      /* SYSTEM role total (built-ins + file), exact */
static mfc_path_probe_t       s_mfck_sys_state = MFC_PATH_ABSENT;
static uint8_t                s_mfck_mode   = 0;   /* 0=dashboard, 1=user-key list, 2=delete confirm */
static uint16_t               s_mfck_scroll = 0;   /* list scroll offset (rows, user-key index space) */
static uint16_t               s_mfck_sel    = 0;   /* list/delete selection (user-key index space)    */

#define NFC_MFCK_LIST_VISIBLE_ROWS   NFC_DESF_LIST_VISIBLE_ROWS   /* same 4-row/scrollbar layout */

static inline uint16_t nfc_mfc_keys_user_count(void)
{
	return (uint16_t)(s_mfck.count - s_mfck.builtin_count);
}

/* Reload BOTH counts from the authoritative on-disk state: the bounded user
 * snapshot (mfc_keys_load_user, unchanged mechanism) and the exact SYSTEM
 * total via the canonical source list -- probed first so an absent/unreadable
 * file is reported honestly rather than silently shown as "0" or "6". Called
 * on entry and after every Add/Delete, so the dashboard and list always
 * reflect what is actually on disk, never a stale in-RAM guess. */
static void nfc_mfc_keys_reload(void)
{
	mfc_keys_reset(&s_mfck);
	s_mfck_st = mfc_keys_load_user(&s_mfck, NULL);   /* NULL: canonical path, then compatible fallback */

	mfc_key_source_cfg_t cfgs[2];
	const char *user_paths[2], *sys_paths[2];
	m1_mfc_build_key_sources(cfgs, user_paths, sys_paths);   /* SAME cfg the acquisition session uses */
	s_mfck_sys_state = mfc_key_source_probe(&cfgs[1]).state;
	s_mfck_sys_count = (s_mfck_sys_state == MFC_PATH_PRESENT)
	                   ? mfc_key_source_count(&cfgs[1], 1).count
	                   : 0;

	platformLog("[MFCK] sys_state=%d sys=%u builtin=%u user=%u bad=%u trunc=%d src=%s\r\n",
	            (int)s_mfck_sys_state, (unsigned)s_mfck_sys_count,
	            (unsigned)s_mfck.builtin_count, (unsigned)nfc_mfc_keys_user_count(),
	            (unsigned)s_mfck_st.skipped_malformed, (int)s_mfck_st.truncated,
	            s_mfck_st.source_path ? s_mfck_st.source_path : "(none)");
}

static void nfc_mfc_keys_gui_create(uint8_t param)
{
	(void)param;
	nfc_mfc_keys_reload();
	s_mfck_mode = 0; s_mfck_scroll = 0; s_mfck_sel = 0;
	m1_uiView_display_update(0);
}

static void nfc_mfc_keys_gui_destroy(uint8_t param) { (void)param; }

/* Add Key: hex entry via the shared virtual keyboard, validated through the
 * SAME mfc_key_parse_line() every other MFC key consumer uses (mfc_key_source.h)
 * -- not a second/ad-hoc hex parser -- so "exactly 12 hex, case-insensitive"
 * can never drift from what a scan or the SD file itself accepts. */
static void nfc_mfc_keys_do_add(void)
{
	char buf[MFC_KEY_HEXLEN + 1];
	/* Growing hex-key editor: appends up to 12 nibbles and validates IN-EDITOR --
	 * an incomplete Save stays on the Add screen (input preserved), so we only
	 * return here with exactly 12 hex, or 0 on cancel (RAM/SD untouched). */
	if (!m1_vkbs_get_hexkey("Enter MFC key", buf, MFC_KEY_HEXLEN)) return;

	uint8_t key[MFC_KEY_SIZE];
	if (mfc_key_parse_line(buf, key) != MFC_KEY_LINE_KEY) {
		nfc_ulc_toast("Invalid key");   /* defensive: the editor guarantees 12 hex chars */
		return;
	}

	mfc_keys_ui_dup_kind_t dup = mfc_keys_ui_dup_kind(s_mfck.keys, s_mfck.count, s_mfck.builtin_count, key);
	if (dup == MFC_KEYS_UI_DUP_BUILTIN) { nfc_ulc_toast("Already included"); return; }
	if (dup == MFC_KEYS_UI_DUP_USER)    { nfc_ulc_toast("Already saved");    return; }

	if (!mfc_keys_add(&s_mfck, key)) { nfc_ulc_toast("Key store full"); return; }   /* MFC_KEYS_MAX cap */
	if (!mfc_keys_save_user(&s_mfck)) {
		nfc_ulc_toast("Save failed");
		nfc_mfc_keys_reload();   /* authoritative on-disk state -- never claim a save that failed */
		return;
	}
	nfc_ulc_toast("Key added");
	nfc_mfc_keys_reload();
}

/* Delete the selected user key (structurally impossible to target a built-in:
 * the index is always builtin_count + a user-list-space selection). */
static void nfc_mfc_keys_do_delete(void)
{
	uint16_t idx = mfc_keys_ui_target_index(s_mfck.builtin_count, s_mfck_sel);
	if (!mfc_keys_remove_at(&s_mfck, idx)) return;
	if (!mfc_keys_save_user(&s_mfck)) {
		nfc_ulc_toast("Delete failed");
		nfc_mfc_keys_reload();   /* authoritative on-disk state -- never claim a delete that failed */
		return;
	}
	nfc_ulc_toast("Key deleted");
	nfc_mfc_keys_reload();
	s_mfck_sel = mfc_keys_ui_clamp_selection(s_mfck_sel, nfc_mfc_keys_user_count());
}

static void nfc_mfc_keys_draw_dashboard(void)
{
	char line[28];
	uint16_t user_n = nfc_mfc_keys_user_count();

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 11, "Classic Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	switch (mfc_keys_ui_system_label(s_mfck_sys_state)) {
		case MFC_KEYS_UI_SYS_NOT_INSTALLED: snprintf(line, sizeof(line), "System: Not installed"); break;
		case MFC_KEYS_UI_SYS_READ_ERROR:    snprintf(line, sizeof(line), "System: Read error");     break;
		default: snprintf(line, sizeof(line), "System: %lu", (unsigned long)s_mfck_sys_count);       break;
	}
	u8g2_DrawStr(&m1_u8g2, 2, 24, line);

	if (s_mfck_st.state == MFC_KEYS_SRC_READ_ERROR)
		snprintf(line, sizeof(line), "Your keys: Read error");
	else
		snprintf(line, sizeof(line), "Your keys: %u%s", (unsigned)user_n, s_mfck_st.truncated ? "+" : "");
	u8g2_DrawStr(&m1_u8g2, 2, 35, line);

	u8g2_DrawStr(&m1_u8g2, 2, 46, "Auto-used in Read");

	/* Black bottom action bar. CENTER + Add is always offered (a save
	 * failure is reported at save time -- see nfc_mfc_keys_do_add()); RIGHT
	 * + List only when there is at least one user key to show. */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Add");
	if (mfc_keys_ui_list_visible(user_n)) {
		u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
		u8g2_DrawStr(&m1_u8g2, 117 - (int)u8g2_GetStrWidth(&m1_u8g2, "List"), 61, "List");
	}
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/* User-key list: SAME 4-row + scrollbar layout as the DESFire Applications
 * list (nfc_desfire_apps_draw, hardware-verified row/box/scrollbar geometry),
 * reused here rather than re-derived. Never shows a built-in or system key. */
static void nfc_mfc_keys_draw_list(void)
{
	uint16_t user_n = nfc_mfc_keys_user_count();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 11, "Your Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	if (user_n == 0) { u8g2_DrawStr(&m1_u8g2, 2, 34, "No user keys"); return; }   /* defensive only */

	for (uint8_t r = 0; r < NFC_MFCK_LIST_VISIBLE_ROWS; r++) {
		uint16_t idx = (uint16_t)(s_mfck_scroll + r);
		if (idx >= user_n) break;
		uint8_t k[MFC_KEY_SIZE];
		if (!mfc_keys_get(&s_mfck, mfc_keys_ui_target_index(s_mfck.builtin_count, idx), k)) break;
		char row[16];
		snprintf(row, sizeof(row), "%02X%02X%02X%02X%02X%02X", k[0], k[1], k[2], k[3], k[4], k[5]);
		uint8_t y = (uint8_t)(20 + r * 10);
		if (idx == s_mfck_sel) {
			u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(y - 8), 121, 10);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 2, y, row);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		} else {
			u8g2_DrawStr(&m1_u8g2, 2, y, row);
		}
	}

	if (user_n > NFC_MFCK_LIST_VISIBLE_ROWS) {
		const uint8_t track_top = 12, track_bot = 50;
		const uint8_t track_h   = (uint8_t)(track_bot - track_top);
		uint16_t max_start = (uint16_t)(user_n - NFC_MFCK_LIST_VISIBLE_ROWS);
		for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
			u8g2_DrawPixel(&m1_u8g2, 125, yy);
		uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_MFCK_LIST_VISIBLE_ROWS) / user_n);
		if (thumb_h < 4)       thumb_h = 4;
		if (thumb_h > track_h) thumb_h = track_h;
		uint8_t thumb_y = track_top;
		if (max_start > 0)
			thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_mfck_scroll) / max_start);
		u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
	}

	/* Footer: CENTER + Select -> delete confirmation (never an immediate delete). */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_mfc_keys_draw_delete_confirm(void)
{
	uint8_t k[MFC_KEY_SIZE];
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 11, "Delete this key?");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	if (mfc_keys_get(&s_mfck, mfc_keys_ui_target_index(s_mfck.builtin_count, s_mfck_sel), k)) {
		char row[16];
		snprintf(row, sizeof(row), "%02X%02X%02X%02X%02X%02X", k[0], k[1], k[2], k[3], k[4], k[5]);
		u8g2_DrawStr(&m1_u8g2, 2, 32, row);
	}

	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8);
	u8g2_DrawStr(&m1_u8g2, 11, 61, "Cancel");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
	u8g2_DrawStr(&m1_u8g2, 117 - (int)u8g2_GetStrWidth(&m1_u8g2, "Delete"), 61, "Delete");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_mfc_keys_gui_update(uint8_t param)
{
	(void)param;
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	if      (s_mfck_mode == 1) nfc_mfc_keys_draw_list();
	else if (s_mfck_mode == 2) nfc_mfc_keys_draw_delete_confirm();
	else                       nfc_mfc_keys_draw_dashboard();
	m1_u8g2_nextpage();
}

static int nfc_mfc_keys_kp_handler(void)
{
	S_M1_Buttons_Status b;
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (s_mfck_mode == 0)   /* dashboard */
	{
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl);
			return 0;   /* exit directly to NFC > Tools */
		}
		if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			nfc_mfc_keys_do_add();
			m1_uiView_display_update(0);
		} else if (b.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
			uint16_t user_n = nfc_mfc_keys_user_count();
			if (mfc_keys_ui_list_visible(user_n)) {
				s_mfck_mode = 1; s_mfck_scroll = 0;
				s_mfck_sel = mfc_keys_ui_clamp_selection(s_mfck_sel, user_n);
				m1_uiView_display_update(0);
			}
		}
		return 1;
	}

	if (s_mfck_mode == 1)   /* user-key list */
	{
		uint16_t user_n = nfc_mfc_keys_user_count();
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_mfck_mode = 0;   /* one level up: back to the dashboard */
			m1_uiView_display_update(0);
		}
		else if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			mfc_keys_ui_nav_up(&s_mfck_sel, &s_mfck_scroll);
			m1_uiView_display_update(0);
		}
		else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			mfc_keys_ui_nav_down(&s_mfck_sel, &s_mfck_scroll, user_n, NFC_MFCK_LIST_VISIBLE_ROWS);
			m1_uiView_display_update(0);
		}
		else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (mfc_keys_ui_list_visible(user_n)) { s_mfck_mode = 2; m1_uiView_display_update(0); }   /* -> delete confirm */
		}
		return 1;
	}

	/* s_mfck_mode == 2: delete confirmation */
	if ((b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) ||
	    (b.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)) {
		s_mfck_mode = 1;   /* Cancel: back to the list, no write */
		m1_uiView_display_update(0);
	}
	else if (b.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
		nfc_mfc_keys_do_delete();
		s_mfck_mode = mfc_keys_ui_mode_after_delete(nfc_mfc_keys_user_count(), /*mode_list=*/1, /*mode_dashboard=*/0);
		m1_uiView_display_update(0);
	}
	return 1;
}

static int nfc_mfc_keys_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_mfc_keys_kp_handler();
	return rv;
}

static void nfc_mfc_keys_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_MFC_KEYS,
	    nfc_mfc_keys_gui_create, nfc_mfc_keys_gui_update,
	    nfc_mfc_keys_gui_destroy, nfc_mfc_keys_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_mfc_keys - NFC Tools > MFC Keys launcher (dashboard + Add/List/Delete).
 */
/*============================================================================*/
void nfc_mfc_keys(void)
{
	platformLog("nfc_mfc_keys()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_MFC_KEYS, 0);
	while (m1_uiView_q_message_process()) { ; }
	platformLog("nfc_mfc_keys()-exit\r\n");
}

/* ==========================================================================
 * NFC Tools > MIFARE Ultralight Keys  (MIFARE Ultralight C key management)
 * --------------------------------------------------------------------------
 * Self-contained: View / Add / Delete of Ultralight C 2K3DES keys (16 bytes =
 * 32 hex), backed by ulc_keys (built-in NXP default + owner-managed and
 * read-only system SD dictionaries, strict parser, deterministic order,
 * bounded UI snapshot; add/delete rewrites the owner file only). KEY
 * MANAGEMENT ONLY -- actual Ultralight C dictionary
 * AUTHENTICATION is NOT performed: the firmware has no 3DES primitive, so that
 * stage is blocked and reported, never faked. Not NTAG PWD_AUTH, not amiibo,
 * not EV1 unlock. */
static ulc_keys_t             s_ulck;
static ulc_keys_load_status_t s_ulck_st;
static ulc_keys_load_status_t s_ulck_sys_st;
static uint8_t                s_ulck_mode     = 0;   /* 0=dashboard, 1=owner-key pager */
static uint8_t                s_ulck_land_sel = 0;   /* 0=Add, 1=View/Delete owner keys */
static uint16_t               s_ulck_idx      = 0;   /* owner-key pager index */

static void nfc_ulc_keys_reload(void)
{
	ulc_keys_reset(&s_ulck);
	s_ulck_st = ulc_keys_load_user(&s_ulck, NULL);
	s_ulck_sys_st = ulc_keys_load_system(&s_ulck, NULL);
	if (s_ulck_idx >= s_ulck.user_count) s_ulck_idx = s_ulck.user_count ? (uint16_t)(s_ulck.user_count - 1) : 0;
}

static void nfc_ulc_keys_gui_create(uint8_t param)
{
	(void)param;
	nfc_ulc_keys_reload();
	s_ulck_mode = 0; s_ulck_land_sel = 0; s_ulck_idx = 0;
	platformLog("[ULCK] builtin=%u user=%u system=%u total=%u bad_user=%u bad_system=%u\r\n",
	            (unsigned)s_ulck.builtin_count,
	            (unsigned)s_ulck.user_count, (unsigned)s_ulck.system_count,
	            (unsigned)s_ulck.count, (unsigned)s_ulck_st.skipped_malformed,
	            (unsigned)s_ulck_sys_st.skipped_malformed);
	m1_uiView_display_update(0);
}

static void nfc_ulc_keys_gui_destroy(uint8_t param) { (void)param; }

/* Draw a 16-byte key as two 16-hex lines. */
static void nfc_ulc_draw_key16(const uint8_t *k, uint8_t y1, uint8_t y2)
{
	char line[20];
	for (uint8_t i = 0; i < 8; i++) snprintf(&line[i * 2], 3, "%02X", k[i]);
	u8g2_DrawStr(&m1_u8g2, 2, y1, line);
	for (uint8_t i = 0; i < 8; i++) snprintf(&line[i * 2], 3, "%02X", k[8 + i]);
	u8g2_DrawStr(&m1_u8g2, 2, y2, line);
}

static void nfc_ulc_keys_draw_landing(void)
{
	static const char *items[2] = { "Add Key", "Your Keys" };
	char line[24];
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 11, "Ultralight Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(line, sizeof(line), "System: %u", (unsigned)s_ulck_sys_st.valid);
	u8g2_DrawStr(&m1_u8g2, 2, 23, line);
	snprintf(line, sizeof(line), "Your keys: %u", (unsigned)s_ulck.user_count);
	u8g2_DrawStr(&m1_u8g2, 2, 34, line);
	for (uint8_t i = 0; i < 2; i++) {
		uint8_t base = (uint8_t)(47 + i * 12);
		if (i == s_ulck_land_sel) {
			u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 9), 128, 12);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		} else {
			u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
		}
	}
}

static void nfc_ulc_keys_draw_view(void)
{
	char line[24];
	uint8_t k[ULC_KEY_SIZE];
	uint16_t user_n = s_ulck.user_count;
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 11, "Your Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	if (user_n == 0) { u8g2_DrawStr(&m1_u8g2, 2, 34, "No user keys"); return; }

	snprintf(line, sizeof(line), "Key %u/%u", (unsigned)(s_ulck_idx + 1), (unsigned)user_n);
	u8g2_DrawStr(&m1_u8g2, 2, 24, line);
	uint16_t idx = (uint16_t)(s_ulck.builtin_count + s_ulck_idx);
	if (ulc_keys_get(&s_ulck, idx, k)) nfc_ulc_draw_key16(k, 36, 49);

	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);   /* footer: CENTER + Delete */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Delete");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_ulc_keys_gui_update(uint8_t param)
{
	(void)param;
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	if (s_ulck_mode == 1) nfc_ulc_keys_draw_view();
	else                   nfc_ulc_keys_draw_landing();
	m1_u8g2_nextpage();
}

/* Brief acknowledgment toast for a manual add/delete action (NOT a scan path --
 * this deliberate pause is a user-action confirmation, not an RF delay). */
static void nfc_ulc_toast(const char *msg)
{
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	u8g2_DrawStr(&m1_u8g2, 4, 34, msg);
	m1_u8g2_nextpage();
	vTaskDelay(pdMS_TO_TICKS(1100));
}

/*============================================================================*/
/* NFC T2T Unlock (genuine PWD_AUTH) -- Enter Password / Use Dictionary,      */
/* reachable from the Read/Saved > More workflow (nfc_can_unlock()) via      */
/* NFC_ACT_UNLOCK. Uses the SAME ntag_pwd_keys.h dictionary Tools > NTAG/UL  */
/* Password Dictionary manages -- never a second store. See product spec.   */
/*============================================================================*/
static uint8_t s_unlock_ui_mode  = 0;   /* 0=landing, 1=running, 2=result */
static uint8_t s_unlock_land_sel = 0;   /* 0=Enter Password, 1=Use Dictionary */

static const char *nfc_unlock_state_text(uint8_t state)
{
    switch ((nfc_unlock_state_t)state) {
        case NFC_UNLOCK_WAIT_CARD:       return "Present tag...";
        case NFC_UNLOCK_AUTHENTICATING:  return "Authenticating";
        case NFC_UNLOCK_SEARCHING:       return "Searching dict...";
        case NFC_UNLOCK_UNLOCKED:        return "Unlocked!";
        case NFC_UNLOCK_WRONG_PASSWORD:  return "Wrong password";
        case NFC_UNLOCK_PACK_MISMATCH:   return "PACK mismatch";
        case NFC_UNLOCK_AUTHLIM_ACTIVE:  return "AUTHLIM active";
        case NFC_UNLOCK_COMM_ERROR:      return "Communication error";
        case NFC_UNLOCK_UNSUPPORTED:     return "Unsupported tag";
        case NFC_UNLOCK_STOPPED:         return "Cancelled";
        case NFC_UNLOCK_REREAD_INCOMPLETE: return "Auth OK, re-read failed";
        default:                         return "";
    }
}

static bool nfc_unlock_state_terminal(uint8_t state)
{
    switch ((nfc_unlock_state_t)state) {
        case NFC_UNLOCK_WAIT_CARD:
        case NFC_UNLOCK_AUTHENTICATING:
        case NFC_UNLOCK_SEARCHING:
        case NFC_UNLOCK_IDLE:
            return false;
        default:
            return true;
    }
}

/*============================================================================*/
/**
 * @brief nfc_unlock_dictionary_allowed - True only when AUTHLIM has been
 *        genuinely read as exactly 0 (unlimited). Whenever entry into
 *        Unlock came from the transient protection-SUSPECTED signal alone
 *        (config pages themselves unreadable pre-auth), genuine AUTHLIM is
 *        necessarily also unread -- "Use Dictionary" must not even be
 *        offered as a landing choice in that case, never merely refused
 *        after the fact. Mirrors nfc_unlock_run()'s own runtime gate
 *        exactly, so the UI and the worker can never disagree.
 */
/*============================================================================*/
static bool nfc_unlock_dictionary_allowed(void)
{
    uint8_t authlim = 0;
    return nfc_ctx_get_t2t_authlim(&authlim) && (authlim == 0U);
}

static void nfc_unlock_draw_landing(void)
{
    static const char *items[2] = { "Enter Password", "Use Dictionary" };
    uint8_t auth0 = 0xFFU;
    bool    known_auth0 = nfc_ctx_get_t2t_auth0(&auth0) && (auth0 != 0xFFU);
    uint8_t item_count  = nfc_unlock_dictionary_allowed() ? 2U : 1U;

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Unlock");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    char sub[24];
    /* AUTH0 may still be genuinely unknown here -- entry was via the
     * transient protection-SUSPECTED signal (config pages themselves
     * unreadable pre-auth), never a fabricated/displayed guess. Reaching
     * this screen at all requires one of these two (nfc_can_unlock()'s own
     * gate), so there is no third case. */
    if (known_auth0) {
        snprintf(sub, sizeof(sub), "Protected: AUTH0=%u", (unsigned)auth0);
    } else {
        snprintf(sub, sizeof(sub), "Locked - Partial Read");
    }
    u8g2_DrawStr(&m1_u8g2, 2, 22, sub);

    for (uint8_t i = 0; i < item_count; i++) {
        uint8_t base = (uint8_t)(38 + i * 12);
        if (i == s_unlock_land_sel) {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 9), 128, 12);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        } else {
            u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
        }
    }
}

static void nfc_unlock_draw_running(void)
{
    nfc_t2t_unlock_t *uk = nfc_ctx_get_t2t_unlock();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Unlock");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawStr(&m1_u8g2, 2, 30, nfc_unlock_state_text(uk->state));
    if (uk->state == NFC_UNLOCK_SEARCHING) {
        char line[24];
        snprintf(line, sizeof(line), "Tried: %u", (unsigned)uk->dict_tried);
        u8g2_DrawStr(&m1_u8g2, 2, 42, line);
    }
}

static void nfc_unlock_draw_result(void)
{
    nfc_t2t_unlock_t *uk = nfc_ctx_get_t2t_unlock();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Unlock");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    u8g2_DrawStr(&m1_u8g2, 2, 30, nfc_unlock_state_text(uk->state));

    if (uk->state == NFC_UNLOCK_UNLOCKED) {
        /* Footer: CENTER + View (physical BACK returns to the Unlock
         * landing menu -- dedicated HW button, never drawn on-screen).
         * Matches the established icon+word footer convention exactly
         * (see nfc_t2t_draw_stop_confirm()'s identical CENTER+Save
         * footer): never a literal "OK"/"CENTER" word, no arrows, no
         * extra frame or menu. */
        u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        u8g2_DrawStr(&m1_u8g2, 15, 61, "View");
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    } else {
        u8g2_DrawStr(&m1_u8g2, 2, 42, "BACK to return");
    }
}

/* Enter Password: hex entry via the shared virtual keyboard, strict 4-byte/
 * 8-hex validation. The prompt itself carries the AUTHLIM-consumption
 * warning inline (no separate confirm screen) -- a single explicit,
 * user-entered attempt is always permitted regardless of AUTHLIM (only
 * dictionary scanning is AUTHLIM-gated), matching the product spec. */
static void nfc_unlock_do_enter_password(void)
{
    char buf[NTAG_PWD_KEY_HEXLEN + 1];
    if (!m1_vkbs_get_hexkey("PWD (may lock tag!)", buf, NTAG_PWD_KEY_HEXLEN)) return;   /* cancelled */

    uint8_t pwd[4];
    if (!nfc_hex_nibbles_to_bytes(buf, pwd, (int)sizeof(pwd))) { nfc_ulc_toast("Invalid password"); return; }

    nfc_ctx_clear_t2t_unlock();
    nfc_unlock_set_single_password(pwd);
    memset(pwd, 0, sizeof(pwd));
    memset(buf, 0, sizeof(buf));
    s_unlock_ui_mode = 1;
    m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_T2T_UNLOCK);
    m1_uiView_display_update(0);
}

static void nfc_unlock_do_use_dictionary(void)
{
    nfc_ctx_clear_t2t_unlock();
    nfc_unlock_set_dictionary_mode();
    s_unlock_ui_mode = 1;
    m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_T2T_UNLOCK);
    m1_uiView_display_update(0);
}

static void nfc_unlock_gui_create(uint8_t param)
{
    (void)param;
    s_unlock_ui_mode = 0;
    s_unlock_land_sel = 0;
    nfc_ctx_clear_t2t_unlock();
}

static void nfc_unlock_gui_destroy(uint8_t param)
{
    (void)param;
    /* Safe on repeated/idempotent call, and safe even if never armed --
     * mirrors every other abort-on-exit worker in this file. Ensures BACK
     * out of a running scan/attempt always stops the RF operation. */
    nfc_poller_unlock_abort();
}

static void nfc_unlock_gui_update(uint8_t param)
{
    (void)param;
    u8g2_FirstPage(&m1_u8g2);
    if (s_unlock_ui_mode == 0)      { nfc_unlock_draw_landing(); }
    else if (s_unlock_ui_mode == 1) {
        nfc_t2t_unlock_t *uk = nfc_ctx_get_t2t_unlock();
        if (nfc_unlock_state_terminal(uk->state) && (uk->state != NFC_UNLOCK_IDLE)) {
            s_unlock_ui_mode = 2;
        }
        nfc_unlock_draw_running();
    } else {
        nfc_unlock_draw_result();
    }
    m1_u8g2_nextpage();
}

static int nfc_unlock_kp_handler(void)
{
    S_M1_Buttons_Status b;
    if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) {
        /* Poll worker progress even with no keypress, while running. */
        if (s_unlock_ui_mode == 1) { m1_uiView_display_update(0); }
        return 1;
    }

    if (s_unlock_ui_mode == 0) {   /* landing */
        uint8_t max_sel = nfc_unlock_dictionary_allowed() ? 1U : 0U;   /* "Use Dictionary" only when genuinely safe */
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return 0; }
        if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)   { if (s_unlock_land_sel > 0) { s_unlock_land_sel--; m1_uiView_display_update(0); } }
        else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) { if (s_unlock_land_sel < max_sel) { s_unlock_land_sel++; m1_uiView_display_update(0); } }
        else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
            if (s_unlock_land_sel == 0) { nfc_unlock_do_enter_password(); }
            else                        { nfc_unlock_do_use_dictionary(); }
        }
        return 1;
    }

    if (s_unlock_ui_mode == 1) {   /* running -- BACK cancels safely */
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
            nfc_poller_unlock_abort();
        }
        return 1;
    }

    /* result (terminal) */
    if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
        s_unlock_ui_mode = 0;
        m1_uiView_display_update(0);
        return 1;
    }
    if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
        nfc_t2t_unlock_t *uk = nfc_ctx_get_t2t_unlock();
        if (uk->state == NFC_UNLOCK_UNLOCKED) {
            /* Show the read result that nfc_unlock_reread_all_pages()
             * already fully populated in nfc_ctx -- READING_READY is the
             * WRONG parameter here: it triggers Q_EVENT_NFC_START_READ, a
             * brand-new physical read cycle. Since the RF session was
             * deactivated when nfc_unlock_run() finished, that fresh read
             * would reselect the tag unauthenticated, hit the AUTH0
             * boundary again, and -- via m1_t2t_read_ntag()'s own top-of-
             * function nfc_ctx_clear_t2t_credential()/clear_t2t_protection()
             * -- silently wipe the credential and genuine protection state
             * the unlock just established, undoing the whole result.
             * READING_COMPLETE is the existing, established parameter for
             * "jump straight to the already-captured result" (see its
             * other call sites in this file) -- no new RF action, no
             * physical re-tap, nothing cleared. */
            m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
            return 1;
        }
        s_unlock_ui_mode = 0;
        m1_uiView_display_update(0);
    }
    return 1;
}

static int nfc_unlock_gui_message(void)
{
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    int rv = 1;
    ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(200));
    if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD)) {
        rv = nfc_unlock_kp_handler();
    } else if (s_unlock_ui_mode == 1) {
        m1_uiView_display_update(0);   /* keep polling worker progress */
    }
    return rv;
}

static void nfc_unlock_gui_init(void)
{
    m1_uiView_functions_register(VIEW_MODE_NFC_UNLOCK,
        nfc_unlock_gui_create, nfc_unlock_gui_update,
        nfc_unlock_gui_destroy, nfc_unlock_gui_message);
}

/*============================================================================*/
/* NFC Tools > NTAG/UL Password Dictionary -- 4-byte PWD_AUTH password       */
/* management (view/add/delete). Structurally identical to the MIFARE       */
/* Ultralight C key tool just above; this is the SAME dictionary the        */
/* contextual Unlock workflow's "Use Dictionary" scans -- never a second    */
/* store (see ntag_pwd_keys.h's own header comment).                        */
/*============================================================================*/
static ntag_pwd_keys_t             s_pwdk;
static ntag_pwd_keys_load_status_t s_pwdk_st;
static ntag_pwd_keys_load_status_t s_pwdk_sys_st;
static uint8_t                     s_pwdk_mode = 0;      /* 0=dashboard, 1=owner-password pager */
static uint8_t                     s_pwdk_land_sel = 0;  /* 0=Add, 1=Your Passwords */
static uint16_t                    s_pwdk_idx = 0;

static void nfc_pwd_keys_reload(void)
{
    ntag_pwd_keys_reset(&s_pwdk);
    s_pwdk_st = ntag_pwd_keys_load_user(&s_pwdk, NULL);
    s_pwdk_sys_st = ntag_pwd_keys_load_system(&s_pwdk, NULL);
    if (s_pwdk_idx >= s_pwdk.user_count) {
        s_pwdk_idx = s_pwdk.user_count ? (uint16_t)(s_pwdk.user_count - 1) : 0;
    }
}

static void nfc_pwd_keys_draw_key4(const uint8_t *k, uint8_t y)
{
    char line[10];
    snprintf(line, sizeof(line), "%02X%02X%02X%02X", k[0], k[1], k[2], k[3]);
    u8g2_DrawStr(&m1_u8g2, 2, y, line);
}

static void nfc_pwd_keys_draw_landing(void)
{
    static const char *items[2] = { "Add Password", "Your Passwords" };
    char line[24];
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "NTAG/UL Passwords");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    snprintf(line, sizeof(line), "System: %u", (unsigned)s_pwdk_sys_st.valid);
    u8g2_DrawStr(&m1_u8g2, 2, 23, line);
    snprintf(line, sizeof(line), "Your pwds: %u", (unsigned)s_pwdk.user_count);
    u8g2_DrawStr(&m1_u8g2, 2, 34, line);
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t base = (uint8_t)(47 + i * 12);
        if (i == s_pwdk_land_sel) {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 9), 128, 12);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        } else {
            u8g2_DrawStr(&m1_u8g2, 4, base, items[i]);
        }
    }
}

static void nfc_pwd_keys_draw_view(void)
{
    char line[24];
    uint8_t k[NTAG_PWD_KEY_SIZE];
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "NTAG/UL Passwords");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
    if (s_pwdk.user_count == 0) { u8g2_DrawStr(&m1_u8g2, 2, 34, "No user passwords"); return; }

    snprintf(line, sizeof(line), "Pwd %u/%u", (unsigned)(s_pwdk_idx + 1), (unsigned)s_pwdk.user_count);
    u8g2_DrawStr(&m1_u8g2, 2, 24, line);
    uint16_t idx = (uint16_t)(s_pwdk.builtin_count + s_pwdk_idx);
    if (ntag_pwd_keys_get(&s_pwdk, idx, k)) nfc_pwd_keys_draw_key4(k, 36);

    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);   /* footer: CENTER + Delete */
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Delete");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_pwd_keys_do_add(void)
{
    char buf[NTAG_PWD_KEY_HEXLEN + 1];
    if (!m1_vkbs_get_hexkey("NTAG/UL Password", buf, NTAG_PWD_KEY_HEXLEN)) return;   /* cancelled */

    uint8_t key[NTAG_PWD_KEY_SIZE];
    if (!nfc_hex_nibbles_to_bytes(buf, key, (int)sizeof(key))) {
        nfc_ulc_toast("Invalid password");   /* defensive: editor guarantees 8 hex chars */
        return;
    }

    uint16_t before = s_pwdk.count;
    if (!ntag_pwd_keys_add(&s_pwdk, key)) { nfc_ulc_toast("Key store full"); return; }
    if (s_pwdk.count == before)           { nfc_ulc_toast("Already present"); return; }
    nfc_ulc_toast(ntag_pwd_keys_save_user(&s_pwdk) ? "Password added" : "Save failed");
    nfc_pwd_keys_reload();
}

static void nfc_pwd_keys_do_delete(void)
{
    uint16_t idx = (uint16_t)(s_pwdk.builtin_count + s_pwdk_idx);
    if (!ntag_pwd_keys_remove_at(&s_pwdk, idx)) return;
    nfc_ulc_toast(ntag_pwd_keys_save_user(&s_pwdk) ? "Password deleted" : "Save failed");
    nfc_pwd_keys_reload();
}

static int nfc_pwd_keys_kp_handler(void)
{
    S_M1_Buttons_Status b;
    if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

    if (s_pwdk_mode == 1) {   /* view pager */
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { s_pwdk_mode = 0; m1_uiView_display_update(0); }
        else if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) { if (s_pwdk_idx > 0) { s_pwdk_idx--; m1_uiView_display_update(0); } }
        else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) { if (s_pwdk_idx + 1 < s_pwdk.user_count) { s_pwdk_idx++; m1_uiView_display_update(0); } }
        else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { if (s_pwdk.user_count > 0) { nfc_pwd_keys_do_delete(); m1_uiView_display_update(0); } }
        return 1;
    }

    /* landing */
    if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { xQueueReset(main_q_hdl); return 0; }
    if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) { if (s_pwdk_land_sel > 0) { s_pwdk_land_sel--; m1_uiView_display_update(0); } }
    else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) { if (s_pwdk_land_sel < 1) { s_pwdk_land_sel++; m1_uiView_display_update(0); } }
    else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
        if (s_pwdk_land_sel == 0) { nfc_pwd_keys_do_add(); m1_uiView_display_update(0); }
        else                      { s_pwdk_mode = 1; s_pwdk_idx = 0; m1_uiView_display_update(0); }
    }
    return 1;
}

static void nfc_pwd_keys_gui_create(uint8_t param)
{
    (void)param;
    nfc_pwd_keys_reload();
    s_pwdk_mode = 0; s_pwdk_land_sel = 0; s_pwdk_idx = 0;
    platformLog("[PWDK] builtin=%u user=%u system=%u total=%u bad_user=%u bad_system=%u\r\n",
                (unsigned)s_pwdk.builtin_count,
                (unsigned)s_pwdk.user_count, (unsigned)s_pwdk.system_count,
                (unsigned)s_pwdk.count, (unsigned)s_pwdk_st.skipped_malformed,
                (unsigned)s_pwdk_sys_st.skipped_malformed);
}

static void nfc_pwd_keys_gui_destroy(uint8_t param) { (void)param; }

static void nfc_pwd_keys_gui_update(uint8_t param)
{
    (void)param;
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_FirstPage(&m1_u8g2);
    if (s_pwdk_mode == 1) nfc_pwd_keys_draw_view();
    else                  nfc_pwd_keys_draw_landing();
    m1_u8g2_nextpage();
}

static int nfc_pwd_keys_gui_message(void)
{
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    int rv = 1;
    ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
    if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
        rv = nfc_pwd_keys_kp_handler();
    return rv;
}

static void nfc_pwd_keys_gui_init(void)
{
    m1_uiView_functions_register(VIEW_MODE_NFC_PWD_KEYS,
        nfc_pwd_keys_gui_create, nfc_pwd_keys_gui_update,
        nfc_pwd_keys_gui_destroy, nfc_pwd_keys_gui_message);
}

/* Add Key: hex entry via the shared virtual keyboard, strict 16-byte validation
 * (reusing the Edit-UID hex path), dedup + persist to the user dictionary. */
static void nfc_ulc_keys_do_add(void)
{
	char buf[ULC_KEY_HEXLEN + 1];
	/* Growing hex-key editor: appends up to 32 nibbles and validates IN-EDITOR --
	 * an incomplete Save stays on the Add Key screen (input preserved), so we only
	 * return here with exactly 32 hex, or 0 on cancel. */
	if (!m1_vkbs_get_hexkey("Ultralight C key", buf, ULC_KEY_HEXLEN)) return;   /* cancelled */

	uint8_t key[ULC_KEY_SIZE];
	if (!nfc_hex_nibbles_to_bytes(buf, key, (int)sizeof(key))) {
		nfc_ulc_toast("Invalid key");   /* defensive: editor guarantees 32 hex chars */
		return;
	}

	uint16_t before = s_ulck.count;
	if (!ulc_keys_add(&s_ulck, key)) { nfc_ulc_toast("Key store full"); return; }
	if (s_ulck.count == before)      { nfc_ulc_toast("Already present"); return; }
	nfc_ulc_toast(ulc_keys_save_user(&s_ulck) ? "Key added" : "Save failed");
	nfc_ulc_keys_reload();
}

static void nfc_ulc_keys_do_delete(void)
{
	uint16_t idx = (uint16_t)(s_ulck.builtin_count + s_ulck_idx);
	if (!ulc_keys_remove_at(&s_ulck, idx)) return;
	nfc_ulc_toast(ulc_keys_save_user(&s_ulck) ? "Key deleted" : "Save failed");
	nfc_ulc_keys_reload();
	uint16_t user_n = s_ulck.user_count;
	if (s_ulck_idx >= user_n) s_ulck_idx = user_n ? (uint16_t)(user_n - 1) : 0;
}

static int nfc_ulc_keys_kp_handler(void)
{
	S_M1_Buttons_Status b;
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (s_ulck_mode == 1)   /* owner-key pager */
	{
		uint16_t user_n = s_ulck.user_count;
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { s_ulck_mode = 0; m1_uiView_display_update(0); }
		else if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) { if (s_ulck_idx > 0) { s_ulck_idx--; m1_uiView_display_update(0); } }
		else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) { if (s_ulck_idx + 1 < user_n) { s_ulck_idx++; m1_uiView_display_update(0); } }
		else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { if (user_n > 0) { nfc_ulc_keys_do_delete(); m1_uiView_display_update(0); } }
		return 1;
	}

	/* landing */
	if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { xQueueReset(main_q_hdl); return 0; }
	if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) { if (s_ulck_land_sel > 0) { s_ulck_land_sel--; m1_uiView_display_update(0); } }
	else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) { if (s_ulck_land_sel < 1) { s_ulck_land_sel++; m1_uiView_display_update(0); } }
	else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
		if (s_ulck_land_sel == 0) { nfc_ulc_keys_do_add(); m1_uiView_display_update(0); }
		else                       { s_ulck_mode = 1; s_ulck_idx = 0; m1_uiView_display_update(0); }
	}
	return 1;
}

static int nfc_ulc_keys_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_ulc_keys_kp_handler();
	return rv;
}

static void nfc_ulc_keys_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_ULC_KEYS,
	    nfc_ulc_keys_gui_create, nfc_ulc_keys_gui_update,
	    nfc_ulc_keys_gui_destroy, nfc_ulc_keys_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_ulc_keys - NFC Tools > MIFARE Ultralight Keys launcher.
 */
/*============================================================================*/
void nfc_ulc_keys(void)
{
	platformLog("nfc_ulc_keys()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_ULC_KEYS, 0);
	while (m1_uiView_q_message_process()) { ; }
	platformLog("nfc_ulc_keys()-exit\r\n");
}

/*============================================================================*/
/**
 * @brief nfc_pwd_keys - NFC Tools > NTAG/UL Password Dictionary launcher.
 */
/*============================================================================*/
void nfc_pwd_keys(void)
{
	platformLog("nfc_pwd_keys()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_PWD_KEYS, 0);
	while (m1_uiView_q_message_process()) { ; }
	platformLog("nfc_pwd_keys()-exit\r\n");
}

/* ---- MIFARE Classic Dictionary Scan view (Stage C) ------------------------
 * Reached from the Classic read result: Actions -> Dictionary Scan. The scan
 * itself runs one-shot on the NFC worker task (mfc_dict_scan, key-major
 * streaming over the mfc_keys iterator). This view posts the worker event,
 * polls nfc_mfc_scan_t for progress, and lets physical BACK abort. Found keys
 * land in the existing MFC store, viewable via the normal Card Info/Data path.
 * No on-screen Back (dedicated hardware button). */

/* RUNNING is operational state, not information the user must read; the terminal
 * summary is the readable screen. So the RUNNING view is PRESENTED only if the
 * scan is still running after this threshold -- a scan that finishes sooner goes
 * straight to the persistent COMPLETE summary and no transient RUNNING frame ever
 * flashes. This gates only the UI presentation: it never delays the scan (RF runs
 * at full speed), never blocks the UI thread, and is re-evaluated on the normal
 * 150 ms message-loop poll. */
#define NFC_MFC_SCAN_RUN_PRESENT_MS   250U
#define NFC_MFC_SCAN_SEC_ROWS         4       /* sectors shown per Sector Keys screen */
static uint32_t s_mfc_scan_view_t0 = 0;       /* tick at view entry (presentation clock) */
static uint8_t  s_mfc_scan_ui      = 0;       /* 0=scan/summary  1=Sector Keys  2=Sector detail */
static uint8_t  s_mfc_scan_sel     = 0;       /* selected sector 0..15 (list/detail)          */

static void nfc_mfc_scan_gui_create(uint8_t param)
{
	(void)param;
	nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
	sc->state = NFC_SCAN_WAIT_CARD; sc->cur_sector = 0; sc->cur_keytype = 0;
	sc->cur_source = NFC_SCAN_SRC_BUILTIN; sc->sectors_acc = 0;   /* placeholder before the first real candidate updates it */
	sc->keys_tried = 0; sc->found = 0;
	s_mfc_scan_ui = 0; s_mfc_scan_sel = 0;
	s_mfc_scan_view_t0 = xTaskGetTickCount();   /* start of the RUNNING-presentation window */
	fb_net_read_start();
	m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_DICT_SCAN);   /* worker runs the scan */
	/* No render here: the prior screen (the Actions menu) stays on-glass until the
	 * outcome is known. A fast scan is already terminal by the first poll, so the
	 * persistent COMPLETE summary is drawn directly -- no intervening RUNNING frame,
	 * no blank, no old-result redraw. */
}

static void nfc_mfc_scan_gui_destroy(uint8_t param)
{
	(void)param;
	fb_net_read_stop();
}

/* --- sub-mode 0: persistent scan / summary screen ---------------------------
 * Counters are truthful: Tried = candidate keys consumed from the iterator;
 * Keys/Found = sector-key slots proven by authentication (0..32); Sectors =
 * sectors with >=1 proven (accessible) key (0..16). No percentage or total-key
 * denominator (the dictionary streams uncapped -- no honest denominator). The
 * terminal summary IS the result screen; there is no auto-forward. */
static void nfc_mfc_scan_draw_summary(nfc_mfc_scan_t *sc)
{
	char line[28];
	const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
	unsigned kt = (mc->keys_total    > 0U) ? mc->keys_total    : 32U;   /* key-slot denominator  */
	unsigned st = (mc->sectors_total > 0U) ? mc->sectors_total : 16U;   /* sector denominator    */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "Dictionary Scan");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	if (sc->state == NFC_SCAN_WAIT_CARD) {            /* operational: waiting to activate */
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Present MFC 1K/4K...");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Waiting for tag");
		return;
	}
	if (sc->state == NFC_SCAN_FAIL) {                 /* wrong/absent card -- scan never ran */
		u8g2_DrawStr(&m1_u8g2, 2, 34, "No MFC 1K/4K card");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Present a Classic");
		return;
	}
	if (sc->state == NFC_SCAN_RUNNING) {             /* operational: live progress */
		u8g2_DrawStr(&m1_u8g2, 2, 20,
		    (sc->cur_source == NFC_SCAN_SRC_USER)    ? "User Dict" :
		    (sc->cur_source == NFC_SCAN_SRC_BUILTIN) ? "Built-in"  : "Sys Dict");
		snprintf(line, sizeof(line), "S%02u", (unsigned)sc->cur_sector);
		u8g2_DrawStr(&m1_u8g2, 2, 31, line);
		snprintf(line, sizeof(line), "Key %c", sc->cur_keytype ? 'B' : 'A');
		u8g2_DrawStr(&m1_u8g2, (int)(126 - u8g2_GetStrWidth(&m1_u8g2, line)), 31, line);
		snprintf(line, sizeof(line), "Tried %lu", (unsigned long)sc->keys_tried);
		u8g2_DrawStr(&m1_u8g2, 2, 42, line);
		snprintf(line, sizeof(line), "Found %u/%u", (unsigned)sc->found, kt);
		u8g2_DrawStr(&m1_u8g2, (int)(126 - u8g2_GetStrWidth(&m1_u8g2, line)), 42, line);
		snprintf(line, sizeof(line), "Sectors %u/%u", (unsigned)sc->sectors_acc, st);
		u8g2_DrawStr(&m1_u8g2, 2, 53, line);
		return;
	}

	/* Terminal persistent summary: DONE / STOPPED / CARD_LOST. */
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 22,
	    (sc->state == NFC_SCAN_DONE)    ? "Complete" :
	    (sc->state == NFC_SCAN_STOPPED) ? "Stopped"  : "Card removed");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(line, sizeof(line), "Keys     %u/%u", (unsigned)sc->found, kt);
	u8g2_DrawStr(&m1_u8g2, 2, 35, line);
	snprintf(line, sizeof(line), "Sectors  %u/%u", (unsigned)sc->sectors_acc, st);
	u8g2_DrawStr(&m1_u8g2, 2, 46, line);
	snprintf(line, sizeof(line), "Tried    %lu", (unsigned long)sc->keys_tried);
	u8g2_DrawStr(&m1_u8g2, 2, 58, line);

	if (sc->found > 0) {   /* RIGHT + More -> Sector Keys (only when proven keys exist) */
		u8g2_DrawXBMP(&m1_u8g2, 119, 55, 8, 8, arrowright_8x8);
		u8g2_DrawStr(&m1_u8g2, 117 - (int)u8g2_GetStrWidth(&m1_u8g2, "More"), 63, "More");
	}
}

/* --- sub-mode 1: Sector Keys list (scrollable, per-sector A/B status) -------- */
static void nfc_mfc_scan_draw_sectors(void)
{
	const nfc_mfc_info_t *mfc = nfc_ctx_get_mfc_info();
	uint8_t sectors = (mfc->sectors_total > 0U) ? mfc->sectors_total : M1NFC_MFC_SECTORS_1K;
	char row[28];
	uint8_t top = (s_mfc_scan_sel < NFC_MFC_SCAN_SEC_ROWS) ? 0
	              : (uint8_t)(s_mfc_scan_sel - (NFC_MFC_SCAN_SEC_ROWS - 1));

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "Sector Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	for (uint8_t r = 0; r < NFC_MFC_SCAN_SEC_ROWS; r++) {
		uint8_t s = (uint8_t)(top + r);
		if (s >= sectors) break;
		uint8_t base = (uint8_t)(19 + r * 10);   /* baselines 19,29,39,49 */
		snprintf(row, sizeof(row), "S%02u    A:%c    B:%c", (unsigned)s,
		         mfc->sec[s].key_a_found ? 'Y' : '-',
		         mfc->sec[s].key_b_found ? 'Y' : '-');
		if (s == s_mfc_scan_sel) {   /* selection highlight (inverse row) */
			u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 8), 121, 10);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 2, base, row);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		} else {
			u8g2_DrawStr(&m1_u8g2, 2, base, row);
		}
	}

	/* Scrollbar (sectors total, NFC_MFC_SCAN_SEC_ROWS visible). */
	{
		const uint8_t track_top = 12, track_bot = 50;
		uint8_t track_h = (uint8_t)(track_bot - track_top);
		uint8_t max_top = (sectors > NFC_MFC_SCAN_SEC_ROWS)
		                  ? (uint8_t)(sectors - NFC_MFC_SCAN_SEC_ROWS) : 1U;
		uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_MFC_SCAN_SEC_ROWS) / sectors);
		if (thumb_h < 4) thumb_h = 4;
		for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
			u8g2_DrawPixel(&m1_u8g2, 125, yy);
		u8g2_DrawBox(&m1_u8g2, 123,
		    (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * top) / (max_top ? max_top : 1)),
		    4, thumb_h);
	}

	/* Footer: CENTER + Details -> the selected sector's key bytes. */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Details");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	m1_u8g2_nextpage();
}

/* --- sub-mode 2: selected-sector detail (proven Key A / Key B bytes) --------- */
static void nfc_mfc_scan_draw_detail(void)
{
	const nfc_mfc_sector_t *S = &nfc_ctx_get_mfc_info()->sec[s_mfc_scan_sel];
	char line[28];

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	snprintf(line, sizeof(line), "Sector %02u", (unsigned)s_mfc_scan_sel);
	u8g2_DrawStr(&m1_u8g2, 2, 9, line);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	u8g2_DrawStr(&m1_u8g2, 2, 24, "Key A");
	if (S->key_a_found) {
		snprintf(line, sizeof(line), "%02X%02X%02X%02X%02X%02X",
		         S->key_a[0], S->key_a[1], S->key_a[2], S->key_a[3], S->key_a[4], S->key_a[5]);
		u8g2_DrawStr(&m1_u8g2, 2, 35, line);
	} else {
		u8g2_DrawStr(&m1_u8g2, 2, 35, "not found");
	}
	u8g2_DrawStr(&m1_u8g2, 2, 50, "Key B");
	if (S->key_b_found) {
		snprintf(line, sizeof(line), "%02X%02X%02X%02X%02X%02X",
		         S->key_b[0], S->key_b[1], S->key_b[2], S->key_b[3], S->key_b[4], S->key_b[5]);
		u8g2_DrawStr(&m1_u8g2, 2, 61, line);
	} else {
		u8g2_DrawStr(&m1_u8g2, 2, 61, "not found");
	}
	m1_u8g2_nextpage();
}

static void nfc_mfc_scan_gui_update(uint8_t param)
{
	(void)param;
	if (s_mfc_scan_ui == 1) { nfc_mfc_scan_draw_sectors(); return; }   /* self-contained */
	if (s_mfc_scan_ui == 2) { nfc_mfc_scan_draw_detail();  return; }

	nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
	bool operational = (sc->state == NFC_SCAN_WAIT_CARD) || (sc->state == NFC_SCAN_RUNNING);
	uint32_t elapsed = (uint32_t)(xTaskGetTickCount() - s_mfc_scan_view_t0);

	/* Suppress transient operational UI: while still operational but under the
	 * presentation threshold, draw NOTHING -- the full-frame buffer keeps the prior
	 * screen on-glass, so a fast scan shows no RUNNING frame and no blank flash. */
	if (operational && (elapsed < pdMS_TO_TICKS(NFC_MFC_SCAN_RUN_PRESENT_MS)))
		return;

	u8g2_FirstPage(&m1_u8g2);
	nfc_mfc_scan_draw_summary(sc);
	m1_u8g2_nextpage();
}

static int nfc_mfc_scan_kp_handler(void)
{
	S_M1_Buttons_Status b;
	nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (s_mfc_scan_ui == 1)   /* Sector Keys list */
	{
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_mfc_scan_ui = 0; m1_uiView_display_update(0);   /* back to summary */
		} else if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (s_mfc_scan_sel > 0) { s_mfc_scan_sel--; m1_uiView_display_update(0); }
		} else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			{ uint8_t st = nfc_ctx_get_mfc_info()->sectors_total; if (st == 0U) st = M1NFC_MFC_SECTORS_1K;
			  if (s_mfc_scan_sel < (st - 1)) { s_mfc_scan_sel++; m1_uiView_display_update(0); } }
		} else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_mfc_scan_ui = 2; m1_uiView_display_update(0);   /* CENTER + Details */
		}
		return 1;
	}
	if (s_mfc_scan_ui == 2)   /* Sector detail */
	{
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_mfc_scan_ui = 1; m1_uiView_display_update(0);   /* back to list */
		}
		return 1;
	}

	/* sub-mode 0: scan / summary */
	if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
	{
		if (sc->state == NFC_SCAN_WAIT_CARD || sc->state == NFC_SCAN_RUNNING)
		{
			/* BACK aborts only while a scan is genuinely active; the worker finishes
			 * the current op and reports STOPPED, then the persistent summary shows. */
			nfc_poller_mfc_scan_abort();
		}
		else
		{
			/* Terminal summary -> return to the MIFARE Classic Keys landing (the
			 * scanner's sole home under NFC Tools). */
			xQueueReset(main_q_hdl);
			m1_uiView_display_switch(VIEW_MODE_NFC_MFC_KEYS, 0);
		}
	}
	else if (b.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
	{
		/* RIGHT + More -> Sector Keys, only from a terminal state with proven keys. */
		bool terminal = (sc->state == NFC_SCAN_DONE) || (sc->state == NFC_SCAN_STOPPED) ||
		                (sc->state == NFC_SCAN_CARD_LOST);
		if (terminal && sc->found > 0) {
			s_mfc_scan_sel = 0; s_mfc_scan_ui = 1; m1_uiView_display_update(0);
		}
	}
	return 1;
}

static int nfc_mfc_scan_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(150));
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_mfc_scan_kp_handler();
	m1_uiView_display_update(0);   /* poll worker progress + redraw */
	return rv;
}

static void nfc_mfc_scan_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_MFC_SCAN,
	    nfc_mfc_scan_gui_create, nfc_mfc_scan_gui_update,
	    nfc_mfc_scan_gui_destroy, nfc_mfc_scan_gui_message);
}

/*============================================================================*/
/* MFC dictionary progress renderer -- shared by normal
 * Read's live dictionary phase (VIEW_MODE_NFC_READ) and Find Missing Keys
 * (VIEW_MODE_NFC_MFC_FIND_KEYS, below): ONE implementation, never two.
 * Layout includes header, sector, progress, key and sector counts, and Skip.
 * Pure drawing only (no FirstPage/NextPage of its own -- the caller,
 * already inside its own FirstPage/NextPage bracket, owns that).
 */
/*============================================================================*/

/* Per-source candidate totals (Built-in / Your Keys / System), computed ONCE
 * per entry into a live dictionary phase -- never re-streamed on every
 * ~150 ms UI poll, which would mean re-reading a multi-thousand-line System
 * dictionary file from SD dozens of times over one session. Shared by both
 * call sites via nfc_mfc_dict_totals_ensure(); reset (via
 * s_mfc_dict_totals_computed = false) at the start of each fresh entry --
 * normal Read's nfc_read_gui_create() and Find Missing Keys' own create(). */
static uint32_t s_mfc_dict_total_builtin;
static uint32_t s_mfc_dict_total_user;
static uint32_t s_mfc_dict_total_system;
static bool     s_mfc_dict_totals_computed = false;

static void nfc_mfc_dict_totals_ensure(void)
{
	if (s_mfc_dict_totals_computed) return;

	const char           *user_paths[2], *sys_paths[2];
	mfc_key_source_cfg_t  cfgs[2];
	m1_mfc_build_key_sources(cfgs, user_paths, sys_paths);
	s_mfc_dict_total_builtin = (uint32_t)cfgs[1].builtin_n;
	s_mfc_dict_total_user    = mfc_key_source_count(&cfgs[0], 1).count;
	mfc_key_source_cfg_t sys_file_only = cfgs[1];
	sys_file_only.builtin = NULL; sys_file_only.builtin_n = 0;
	s_mfc_dict_total_system = mfc_key_source_count(&sys_file_only, 1).count;

	s_mfc_dict_totals_computed = true;
}

static void nfc_mfc_dict_totals_invalidate(void)
{
	s_mfc_dict_totals_computed = false;
}

/* Full-width progress bar at (x,y,w,h) for current/total,
 * clamped and zero-total-safe, with "current/total" drawn INSIDE the bar --
 * inverted color over the filled portion, normal color over the rest --
 * using u8g2_DrawFrame/DrawBox + two clip-windowed text draws. */
static void nfc_mfc_progress_bar_draw(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                                      uint32_t current, uint32_t total)
{
	char     line[24];
	uint8_t  fill_w;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawFrame(&m1_u8g2, x, y, w, h);

	if (total == 0U) {
		fill_w = 0U;   /* zero-total-safe: an empty, un-filled bar, never a divide-by-zero */
	} else {
		uint32_t c = (current > total) ? total : current;   /* clamp at 100% */
		fill_w = (uint8_t)(((uint32_t)(w - 2U) * c) / total);
	}
	if (fill_w > 0U) u8g2_DrawBox(&m1_u8g2, (uint8_t)(x + 1), (uint8_t)(y + 1), fill_w, (uint8_t)(h - 2U));

	/* A genuinely-zero current is shown as 1 so the
	 * bar never reads as stuck/inert on the very first candidate. */
	snprintf(line, sizeof(line), "%lu/%lu",
	         (unsigned long)((current == 0U) ? 1U : current), (unsigned long)total);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	uint8_t text_w = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, line);
	uint8_t text_x = (uint8_t)(x + ((w > text_w) ? ((w - text_w) / 2U) : 0U));
	uint8_t text_y = (uint8_t)(y + h - 2U);
	uint8_t fill_x1 = (uint8_t)(x + 1U + fill_w);

	if (fill_w > 0U) {
		u8g2_SetClipWindow(&m1_u8g2, x, y, fill_x1, (uint8_t)(y + h));
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_DrawStr(&m1_u8g2, text_x, text_y, line);
	}
	u8g2_SetClipWindow(&m1_u8g2, fill_x1, y, (uint8_t)(x + w), (uint8_t)(y + h));
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawStr(&m1_u8g2, text_x, text_y, line);
	u8g2_SetMaxClipWindow(&m1_u8g2);   /* restore -- every other draw call in this file assumes full-screen clipping */
}

static void nfc_mfc_dict_progress_draw(void)
{
	nfc_mfc_scan_t       *sc = nfc_ctx_get_mfc_scan();
	const nfc_mfc_info_t *mc = nfc_ctx_get_mfc_info();
	char line[28];
	unsigned kt = (mc->keys_total > 0U) ? mc->keys_total : 32U;
	unsigned st = (mc->sectors_total > 0U) ? mc->sectors_total : 16U;

	nfc_mfc_dict_totals_ensure();

	/* Header: the truthful active source -- exactly the three real M1
	 * origins, never collapsed or invented. Matches the header-
	 * is-the-active-dictionary convention. "MF Classic " is dropped from
	 * the on-screen label (kept in code comments/docs) because at
	 * NFC_FONT_EMPH the full "MF Classic System Dictionary" measures
	 * 149px -- wider than the 126px usable width -- and clips; this screen
	 * is only ever reached for M1NFC_FAM_CLASSIC, so the prefix carries no
	 * information the context doesn't already give. */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9,
	    (sc->cur_source == NFC_SCAN_SRC_USER)    ? "User Dictionary" :
	    (sc->cur_source == NFC_SCAN_SRC_BUILTIN) ? "Built-in Keys"   :
	                                               "System Dictionary");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	snprintf(line, sizeof(line), "Unlocking sector: %u", (unsigned)sc->cur_sector);
	u8g2_DrawStr(&m1_u8g2, 2, 19, line);

	uint32_t src_total =
	    (sc->cur_source == NFC_SCAN_SRC_USER)    ? s_mfc_dict_total_user :
	    (sc->cur_source == NFC_SCAN_SRC_BUILTIN) ? s_mfc_dict_total_builtin :
	                                               s_mfc_dict_total_system;
	nfc_mfc_progress_bar_draw(2, 22, 124, 10, sc->src_tried, src_total);

	snprintf(line, sizeof(line), "Keys found: %u/%u", (unsigned)sc->found, kt);
	u8g2_DrawStr(&m1_u8g2, 2, 42, line);
	snprintf(line, sizeof(line), "Sectors Read: %u/%u", (unsigned)sc->sectors_acc, st);
	u8g2_DrawStr(&m1_u8g2, 2, 51, line);

	/* Footer: CENTER + Skip, full-width -- matches the established M1
	 * single-control footer convention (target_10x10 icon + label). */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Skip");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_mfc_lost_tag_draw(void)
{
	/* Lost-tag text: the
	 * progress UI is hidden, but Skip stays available -- the user is never
	 * stuck with no way out just because the card wandered off. */
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, (uint8_t)((128 - u8g2_GetStrWidth(&m1_u8g2, "Lost the tag!")) / 2U), 13, "Lost the tag!");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	u8g2_DrawStr(&m1_u8g2, 2, 30, "Make sure the tag is");
	u8g2_DrawStr(&m1_u8g2, 2, 40, "positioned correctly.");

	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Skip");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/* "Exit to NFC Menu?" confirmation, shown from BACK while the
 * MFC dictionary phase is genuinely live (NFC_READ_DISPLAY_PARAM_MFC_EXIT_CONFIRM,
 * VIEW_MODE_NFC_READ only -- Find Missing Keys keeps its own existing,
 * simpler BACK-abort pattern per the task's explicit shared-elements list,
 * which does not include this confirmation). */
static void nfc_mfc_exit_confirm_draw(void)
{
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, (uint8_t)((128 - u8g2_GetStrWidth(&m1_u8g2, "Exit to NFC Menu?")) / 2U), 13,
	            "Exit to NFC Menu?");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	/* "All unsaved data will be lost" does not fit NFC_FONT_STD's width in
	 * one line -- wrapped across two lines, left-aligned, matching the same
	 * convention nfc_mfc_lost_tag_draw()'s own two-line body already uses. */
	u8g2_DrawStr(&m1_u8g2, 2, 30, "All unsaved data");
	u8g2_DrawStr(&m1_u8g2, 2, 40, "will be lost");

	/* LEFT = Exit, RIGHT = Stay -- matching the left/right
	 * placement exactly, and this file's own established split-footer
	 * convention (arrowleft/arrowright, e.g. the read-result Retry/More bar). */
	u8g2_DrawXBMP(&m1_u8g2, 1, 53, 8, 8, arrowleft_8x8);
	u8g2_DrawStr(&m1_u8g2, 11, 61, "Exit");
	u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Stay"), 61, "Stay");
	u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
}

/*============================================================================*/
/* VIEW_MODE_NFC_MFC_FIND_KEYS - Find Missing Keys: dictionary-phase
 * CONTINUATION of a partial MFC read. Posts Q_EVENT_NFC_MFC_FIND_KEYS;
 * nfc_mfc_find_keys_run() (nfc_poller.c) resumes the SAME acquisition a
 * prior read/continuation already made progress on -- unlike Dictionary
 * Scan's own create(), this view's create() deliberately does NOT reset
 * nfc_mfc_scan_t's counters, and the worker function it triggers never
 * calls nfc_ctx_clear_mfc(). RUNNING/CARD_LOST are drawn via the SAME
 * shared nfc_mfc_dict_progress_draw()/nfc_mfc_lost_tag_draw() normal Read's
 * own dictionary phase uses -- not a second implementation. Only the
 * entry-specific states below (waiting for the SAME card, wrong card type,
 * identity mismatch, and the terminal summary) are drawn locally.
 */
/*============================================================================*/

static void nfc_mfc_find_keys_gui_create(uint8_t param)
{
	(void)param;
	nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
	sc->state = NFC_SCAN_WAIT_CARD;
	/* Deliberately NOT resetting cur_sector/cur_keytype/cur_source/
	 * sectors_acc/keys_tried/found/src_tried here (unlike
	 * nfc_mfc_scan_gui_create()) -- they already reflect every prior
	 * pass's progress and must keep doing so through this one too. */
	s_mfc_dict_totals_computed = false;   /* fresh per-entry computation, shared helper */

	fb_net_read_start();
	m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_MFC_FIND_KEYS);   /* worker runs the continuation */
}

static void nfc_mfc_find_keys_gui_destroy(uint8_t param)
{
	(void)param;
	fb_net_read_stop();
}

static void nfc_mfc_find_keys_draw(void)
{
	nfc_mfc_scan_t        *sc = nfc_ctx_get_mfc_scan();
	const nfc_mfc_info_t  *mc = nfc_ctx_get_mfc_info();
	char line[28];
	unsigned kt = (mc->keys_total > 0U) ? mc->keys_total : 32U;
	unsigned st = (mc->sectors_total > 0U) ? mc->sectors_total : 16U;

	if (sc->state == NFC_SCAN_RUNNING) {
		nfc_mfc_dict_progress_draw();   /* shared renderer -- see above */
		return;
	}
	if (sc->state == NFC_SCAN_CARD_LOST) {
		nfc_mfc_lost_tag_draw();        /* shared renderer -- live/resumable, not terminal */
		return;
	}

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "Finding Keys");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	if (sc->state == NFC_SCAN_WAIT_CARD) {
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Present the same card...");
		return;
	}
	if (sc->state == NFC_SCAN_FAIL) {
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Not a MFC 1K/4K card");
		return;
	}
	if (sc->state == NFC_SCAN_IDENTITY_MISMATCH) {
		/* Truthful: this refusal never touches the original partial
		 * result (nfc_mfc_find_keys_run()), so say so plainly. */
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Different card presented");
		u8g2_DrawStr(&m1_u8g2, 2, 45, "Progress preserved");
		return;
	}

	/* Terminal: DONE / STOPPED. */
	u8g2_DrawStr(&m1_u8g2, 2, 22, (sc->state == NFC_SCAN_DONE) ? "Complete" : "Stopped");
	snprintf(line, sizeof(line), "Found %u/%u", (unsigned)sc->found, kt);
	u8g2_DrawStr(&m1_u8g2, 2, 33, line);
	snprintf(line, sizeof(line), "Sectors %u/%u", (unsigned)sc->sectors_acc, st);
	u8g2_DrawStr(&m1_u8g2, 2, 44, line);
	u8g2_DrawStr(&m1_u8g2, 2, 61, "BACK to continue");
}

static void nfc_mfc_find_keys_gui_update(uint8_t param)
{
	(void)param;
	u8g2_FirstPage(&m1_u8g2);
	nfc_mfc_find_keys_draw();
	m1_u8g2_nextpage();
}

static int nfc_mfc_find_keys_kp_handler(void)
{
	S_M1_Buttons_Status b;
	nfc_mfc_scan_t *sc = nfc_ctx_get_mfc_scan();
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
	{
		if (sc->state == NFC_SCAN_WAIT_CARD || sc->state == NFC_SCAN_RUNNING ||
		    sc->state == NFC_SCAN_CARD_LOST)
		{
			/* Sets the existing abort flag only -- no RF work here. The
			 * worker (single-threaded, same as Dictionary Scan) finishes
			 * whatever safe operation it is mid-way through and reports a
			 * terminal state; this same handler then routes home on the
			 * NEXT press, exactly like Dictionary Scan's own BACK. */
			nfc_poller_mfc_scan_abort();
		}
		else
		{
			/* Terminal (DONE/STOPPED), or refused before RF ever started
			 * (FAIL/IDENTITY_MISMATCH) -- return to the read result
			 * screen. mfc->outcome already reflects whatever this run
			 * achieved (recomputed by nfc_mfc_find_keys_run() before
			 * NFC_STATE_DONE): if it is now COMPLETE, the SAME screen
			 * shows the existing completed-read layout automatically --
			 * no second success workflow, no new destination to invent. */
			m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);
		}
	}
	else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
	{
		/* CENTER "Skip" -- same control and semantics,
		 * same shared abort/skip flag as normal Read's own dictionary
		 * phase. See nfc_mfc_dict_progress_draw()'s footer above. */
		if ((sc->state == NFC_SCAN_RUNNING) || (sc->state == NFC_SCAN_CARD_LOST)) {
			nfc_poller_mfc_scan_skip_source();
		}
	}
	return 1;
}

static int nfc_mfc_find_keys_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(150));
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_mfc_find_keys_kp_handler();
	m1_uiView_display_update(0);   /* poll worker progress + redraw */
	return rv;
}

static void nfc_mfc_find_keys_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_MFC_FIND_KEYS,
	    nfc_mfc_find_keys_gui_create, nfc_mfc_find_keys_gui_update,
	    nfc_mfc_find_keys_gui_destroy, nfc_mfc_find_keys_gui_message);
}

/*============================================================================*/
/* VIEW_MODE_NFC_MFC_WRITE - MIFARE Classic 1K write (clone/restore).           */
/* Posts Q_EVENT_NFC_MFC_WRITE; nfc_mfc_write_run() authenticates the presented */
/* card with the loaded image's known keys, enforces the target's access        */
/* conditions, writes permitted data blocks + guarded trailers, and read-back   */
/* verifies each. Progress/result are polled from nfc_mfc_write_t. There is no   */
/* auto-forward: the terminal summary IS the result screen.                     */
/*============================================================================*/
#define NFC_MFC_WRITE_RUN_PRESENT_MS   250U
static uint32_t s_mfc_write_view_t0 = 0;   /* tick at view entry (presentation clock) */
static bool     s_mfc_write_led_on  = false;  /* active-write LED currently blinking */

static void nfc_mfc_write_led_off(void)
{
	if (s_mfc_write_led_on) {
		fb_net_write_stop();
		s_mfc_write_led_on = false;
	}
}

static void nfc_mfc_write_gui_create(uint8_t param)
{
	(void)param;
	nfc_ctx_clear_mfc_write();
	nfc_ctx_get_mfc_write()->state = NFC_MFCWR_WAIT_CARD;
	s_mfc_write_view_t0 = xTaskGetTickCount();
	fb_net_write_start();
	s_mfc_write_led_on = true;
	m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_MFC_WRITE);   /* worker runs the write */
}

static void nfc_mfc_write_gui_destroy(uint8_t param)
{
	(void)param;
	nfc_mfc_write_led_off();
}

static void nfc_mfc_write_draw(nfc_mfc_write_t *wr)
{
	char line[28];
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "MFC Write");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	switch (wr->state) {
	case NFC_MFCWR_NO_SOURCE:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "No card loaded");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Read a card first");
		return;
	case NFC_MFCWR_WAIT_CARD:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Present target card...");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Waiting for tag");
		return;
	case NFC_MFCWR_FAIL:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Card type mismatch");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Present matching MFC");
		return;
	case NFC_MFCWR_WRITING:
		snprintf(line, sizeof(line), "Writing  S%02u/%u", (unsigned)wr->cur_sector,
		         (unsigned)nfc_ctx_get_mfc_info()->sectors_total);
		u8g2_DrawStr(&m1_u8g2, 2, 26, line);
		snprintf(line, sizeof(line), "Written  %u", (unsigned)wr->written);
		u8g2_DrawStr(&m1_u8g2, 2, 40, line);
		snprintf(line, sizeof(line), "Skip %u   Fail %u",
		         (unsigned)wr->skipped, (unsigned)wr->failed);
		u8g2_DrawStr(&m1_u8g2, 2, 52, line);
		return;
	default:
		break;   /* terminal states drawn below */
	}

	/* Terminal persistent summary: DONE / STOPPED / CARD_LOST. */
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 22,
	    (wr->state == NFC_MFCWR_DONE)    ? "Write Complete" :
	    (wr->state == NFC_MFCWR_STOPPED) ? "Stopped" : "Card removed");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(line, sizeof(line), "Written  %u", (unsigned)wr->written);
	u8g2_DrawStr(&m1_u8g2, 2, 35, line);
	snprintf(line, sizeof(line), "Skipped  %u", (unsigned)wr->skipped);
	u8g2_DrawStr(&m1_u8g2, 2, 46, line);
	snprintf(line, sizeof(line), "Failed   %u", (unsigned)wr->failed);
	u8g2_DrawStr(&m1_u8g2, 2, 57, line);
	if (wr->sectors_noauth > 0) {
		snprintf(line, sizeof(line), "NoKey %u", (unsigned)wr->sectors_noauth);
		u8g2_DrawStr(&m1_u8g2, (int)(126 - u8g2_GetStrWidth(&m1_u8g2, line)), 57, line);
	}
}

static void nfc_mfc_write_gui_update(uint8_t param)
{
	(void)param;
	nfc_mfc_write_t *wr = nfc_ctx_get_mfc_write();
	bool operational = (wr->state == NFC_MFCWR_WAIT_CARD) || (wr->state == NFC_MFCWR_WRITING);
	uint32_t elapsed = (uint32_t)(xTaskGetTickCount() - s_mfc_write_view_t0);

	/* Terminal state reached: stop the active-write LED now (the view stays on the
	 * persistent summary, so destroy may not run for a while). */
	if (!operational) nfc_mfc_write_led_off();

	/* Suppress transient operational UI under the presentation threshold: keep the
	 * prior screen on-glass so a fast write shows no flicker. */
	if (operational && (elapsed < pdMS_TO_TICKS(NFC_MFC_WRITE_RUN_PRESENT_MS)))
		return;

	u8g2_FirstPage(&m1_u8g2);
	nfc_mfc_write_draw(wr);
	m1_u8g2_nextpage();
}

static int nfc_mfc_write_kp_handler(void)
{
	S_M1_Buttons_Status b;
	nfc_mfc_write_t *wr = nfc_ctx_get_mfc_write();
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
	{
		if (wr->state == NFC_MFCWR_WAIT_CARD || wr->state == NFC_MFCWR_WRITING) {
			/* BACK aborts only while genuinely active; the worker finishes the
			 * current block and reports STOPPED, then the summary shows. */
			nfc_poller_mfc_write_abort();
		} else {
			/* Terminal summary -> back to the card action submenu (as NTAG write). */
			nfc_mfc_write_led_off();
			xQueueReset(main_q_hdl);
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	}
	return 1;
}

static int nfc_mfc_write_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(150));
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_mfc_write_kp_handler();
	m1_uiView_display_update(0);   /* poll worker progress + redraw */
	return rv;
}

static void nfc_mfc_write_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_MFC_WRITE,
	    nfc_mfc_write_gui_create, nfc_mfc_write_gui_update,
	    nfc_mfc_write_gui_destroy, nfc_mfc_write_gui_message);
}

/*============================================================================*/
/* NFC > Add: manual card creation.                                            */
/*                                                                              */
/* One launcher per approved type (MFC 1K/4K, Ultralight, NTAG213/215/216)      */
/* switches to this view with the type as a parameter. The view enters a UID    */
/* via the shared hex keyboard, builds a structurally-valid BLANK image from    */
/* the approved template (nfc_add_image), shows a confirmation summary, and      */
/* saves through the existing NFC save workflow with a manual provenance mark.   */
/* No manufacturer data, keys, passwords, or PACK values are invented: template  */
/* defaults only (see nfc_add_image.c).                                          */
/*============================================================================*/
static uint8_t s_add_type    = 0;     /* nfc_add_type_t selected by the launcher    */
static uint8_t s_add_ui      = 0;     /* 0=UID method  1=confirm  2=saving          */
static uint8_t s_add_method  = 0;     /* 0=Enter UID   1=Generate UID               */
static bool    s_add_step    = false; /* per-stage one-shot guard                   */
static bool    s_add_exit    = false; /* request loop exit -> back to menu          */
static bool    s_add_generated = false;/* provenance: UID was generated (not typed) */
static uint8_t s_add_uid[7]  = {0};
static uint8_t s_add_uidlen  = 0;

/* Populate the run context with the freshly built blank image so the existing
 * save workflow serialises it. Returns false if the template could not be built. */
static bool nfc_add_populate_ctx(const nfc_add_geom_t *g, const uint8_t *uid)
{
	nfc_ctx_begin_live();                       /* fresh live-style context (not a loaded file) */
	nfc_ctx_clear_mfc();
	nfc_ctx_clear_t2t_ndef();

	uint32_t sz = nfc_add_build_image((nfc_add_type_t)s_add_type, uid, g->uid_len,
	                                  g_nfc_dump_buf, NFC_DUMP_BUF_SIZE);
	if (sz == 0) return false;

	nfc_run_ctx_t *c = nfc_ctx_get();
	c->head.tech   = M1NFC_TECH_A;
	c->head.family = (g->family == NFC_ADD_FAM_CLASSIC) ? M1NFC_FAM_CLASSIC : M1NFC_FAM_ULTRALIGHT;
	memcpy(c->head.uid, uid, g->uid_len);
	c->head.uid_len       = g->uid_len;
	c->head.a.atqa[0]     = g->atqa[0];
	c->head.a.atqa[1]     = g->atqa[1];
	c->head.a.has_atqa    = true;
	c->head.a.sak         = g->sak;
	c->head.a.has_sak     = true;
	c->head.a.ats_len     = 0;

	(void)memset(g_nfc_valid_bits, 0xFF, NFC_DUMP_MAX_UNITS / 8U);   /* all units present */
	nfc_ctx_set_dump(g->unit_size, g->unit_count, 0,
	                 g_nfc_dump_buf, g_nfc_valid_bits, g->unit_count, true);
	if (g->family == NFC_ADD_FAM_T2T) nfc_ctx_set_t2t_variant(g->t2t_variant);
	nfc_ctx_refresh_ui();
	return true;
}

/* Build the image from a validated UID + stash it for the summary. Returns a
 * result-state label on failure, or NULL on success. */
static const char *nfc_add_finalize(const nfc_add_geom_t *g, const uint8_t *uid)
{
	if (!nfc_add_populate_ctx(g, uid)) return "Invalid Template";
	memcpy(s_add_uid, uid, g->uid_len);
	s_add_uidlen = g->uid_len;
	return NULL;
}

/* Enter a UID on the shared hex keyboard (hex-only, exact length enforced in the
 * editor). Returns a result-state label on failure, or NULL on success. */
static const char *nfc_add_enter_uid(const nfc_add_geom_t *g)
{
	char desc[24];
	snprintf(desc, sizeof(desc), "%s UID (%u B)", g->label, (unsigned)g->uid_len);

	char hexbuf[16];                 /* max 14 nibbles + NUL */
	(void)memset(hexbuf, 0, sizeof(hexbuf));
	if (!m1_vkbs_get_hexkey(desc, hexbuf, (uint8_t)(g->uid_len * 2U)))
		return "Cancelled";          /* user escaped the keyboard */

	uint8_t uid[7];
	(void)memset(uid, 0, sizeof(uid));
	bool ok = nfc_hex_nibbles_to_bytes(hexbuf, uid, (int)g->uid_len);
	(void)memset(hexbuf, 0, sizeof(hexbuf));   /* clear sensitive input */

	/* No count-based "Invalid Length" branch: the editor above was asked for
	 * exactly g->uid_len * 2 nibbles and enforces that in-editor before it
	 * ever returns non-cancelled, so nfc_hex_nibbles_to_bytes() -- given
	 * exactly that many hex chars -- can only fail on a non-hex character. */
	const char *res;
	if (!ok)                         res = "Invalid Hex";       /* non-hex (defensive) */
	else                              res = nfc_add_finalize(g, uid);
	if (res == NULL) s_add_generated = false;
	(void)memset(uid, 0, sizeof(uid));
	return res;
}

/* Generate a random UID (established rand() source) and build. Fully random --
 * NOT forced to any manufacturer prefix, so it is never presented as a genuine
 * manufacturer-issued card. BCC values are computed by the template builder. */
static const char *nfc_add_gen_uid(const nfc_add_geom_t *g)
{
	uint8_t uid[7];
	(void)memset(uid, 0, sizeof(uid));
	srand((unsigned)HAL_GetTick());
	for (uint8_t i = 0; i < g->uid_len; i++) uid[i] = (uint8_t)(rand() & 0xFFU);
	const char *res = nfc_add_finalize(g, uid);
	if (res == NULL) s_add_generated = true;
	(void)memset(uid, 0, sizeof(uid));
	return res;
}

/* Stage 0: choose the UID method (Enter / Generate). */
static void nfc_add_draw_method(void)
{
	const nfc_add_geom_t *g = nfc_add_geometry((nfc_add_type_t)s_add_type);
	static const char *items[2] = { "Enter UID", "Generate UID" };

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, g ? g->label : "Add Card");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	for (uint8_t i = 0; i < 2; i++) {
		uint8_t base = (uint8_t)(26 + i * 12);
		if (i == s_add_method) {
			u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 9), 128, 12);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 6, base, items[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		} else {
			u8g2_DrawStr(&m1_u8g2, 6, base, items[i]);
		}
	}

	/* Footer: CENTER + Select (physical BACK cancels). */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/* Stage 1: confirmation summary -- type, UID, size, Template provenance. */
static void nfc_add_draw_confirm(void)
{
	const nfc_add_geom_t *g = nfc_add_geometry((nfc_add_type_t)s_add_type);
	char line[28];

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "Add Card");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	if (g != NULL) {
		snprintf(line, sizeof(line), "Type: %s", g->label);
		u8g2_DrawStr(&m1_u8g2, 2, 20, line);
	}
	u8g2_DrawStr(&m1_u8g2, 2, 30, "UID:");
	{
		char uidhex[24]; int p = 0;
		for (uint8_t i = 0; i < s_add_uidlen && p < (int)sizeof(uidhex) - 3; i++)
			p += snprintf(uidhex + p, sizeof(uidhex) - p, "%02X", s_add_uid[i]);
		u8g2_DrawStr(&m1_u8g2, 26, 30, uidhex);
	}
	if (g != NULL) {
		snprintf(line, sizeof(line), "Size: %lu B", (unsigned long)g->image_size);
		u8g2_DrawStr(&m1_u8g2, 2, 40, line);
	}
	u8g2_DrawStr(&m1_u8g2, 2, 50,
	    s_add_generated ? "Template (gen UID)" : "Template (typed UID)");

	/* Footer: CENTER + Save (physical BACK cancels). */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Save");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_add_gui_create(uint8_t param)
{
	s_add_type      = param;
	s_add_ui        = 0;
	s_add_method    = 0;
	s_add_step      = false;
	s_add_exit      = false;
	s_add_generated = false;
	s_add_uidlen    = 0;
	(void)memset(s_add_uid, 0, sizeof(s_add_uid));   /* no stale data from a prior Add */
}

static void nfc_add_gui_destroy(uint8_t param)
{
	(void)param;
	(void)memset(s_add_uid, 0, sizeof(s_add_uid));   /* clear sensitive temp data */
	s_add_uidlen = 0;
}

static void nfc_add_gui_update(uint8_t param)
{
	(void)param;
	if (s_add_exit) return;

	if (s_add_ui == 2) {                 /* saving stage: run once */
		if (s_add_step) return;
		s_add_step = true;
		char filepath[128];
		nfc_file_mark_next_save_manual();
		uint8_t err = nfc_save_file_keyboard(filepath, sizeof(filepath));
		if (err == 3)                                        nfc_ulc_toast("Cancelled");
		else if (err != 0)                                   nfc_ulc_toast("Save Failed");
		else if (nfc_profile_save(filepath, nfc_ctx_get()))  nfc_ulc_toast("Added");
		else                                                 nfc_ulc_toast("Save Failed");
		s_add_exit = true;
		return;
	}

	u8g2_FirstPage(&m1_u8g2);
	if (s_add_ui == 0) nfc_add_draw_method();
	else               nfc_add_draw_confirm();   /* s_add_ui == 1 */
	m1_u8g2_nextpage();
}

static int nfc_add_kp_handler(void)
{
	S_M1_Buttons_Status b;
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (s_add_ui == 0)   /* UID method chooser */
	{
		if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK ||
		    b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			s_add_method ^= 1u;
			m1_uiView_display_update(0);
		} else if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_add_exit = true;                               /* cancel, no file */
		} else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			const nfc_add_geom_t *g = nfc_add_geometry((nfc_add_type_t)s_add_type);
			const char *res;
			if (g == NULL)               res = "Unsupported Variant";
			else if (s_add_method == 1)  res = nfc_add_gen_uid(g);   /* Generate */
			else                         res = nfc_add_enter_uid(g); /* Enter (blocking kb) */
			if (res == NULL) {
				s_add_ui = 1;                                /* -> confirmation */
				m1_uiView_display_update(0);
			} else {
				nfc_ulc_toast(res);                          /* result state */
				s_add_exit = true;
			}
		}
	}
	else if (s_add_ui == 1)   /* confirmation */
	{
		if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_add_ui = 2; s_add_step = false;                /* -> saving (runs in update) */
			m1_uiView_display_update(0);
		} else if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_add_exit = true;                               /* cancel without saving */
		}
	}
	return 1;
}

static int nfc_add_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(120));
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		(void)nfc_add_kp_handler();

	if (s_add_exit) {
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);   /* runs destroy() */
		return 0;                                      /* end loop -> back to menu */
	}
	m1_uiView_display_update(0);
	return 1;
}

static void nfc_add_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_ADD,
	    nfc_add_gui_create, nfc_add_gui_update,
	    nfc_add_gui_destroy, nfc_add_gui_message);
}

/* Shared launcher: init the NFC view table and run the Add view for `add_type`
 * (mirrors nfc_read()). One thin wrapper per type is exposed to the menu. */
static void nfc_add_launch(uint8_t add_type)
{
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_ADD, add_type);
	while (m1_uiView_q_message_process()) { ; }
}

void nfc_add_mfc1k(void)     { nfc_add_launch(NFC_ADD_MFC_1K); }
void nfc_add_mfc4k(void)     { nfc_add_launch(NFC_ADD_MFC_4K); }
void nfc_add_ultralight(void){ nfc_add_launch(NFC_ADD_ULTRALIGHT); }
void nfc_add_ntag213(void)   { nfc_add_launch(NFC_ADD_NTAG213); }
void nfc_add_ntag215(void)   { nfc_add_launch(NFC_ADD_NTAG215); }
void nfc_add_ntag216(void)   { nfc_add_launch(NFC_ADD_NTAG216); }

/*============================================================================*/
/* NFC Tools > Harvest (Increment 3b): pick a target sector, present a MIFARE   */
/* Classic card, capture nested nonces to 0:/NFC/recover/<uid>_<ticks>.m1h.     */
/* This view only captures -- it never derives or validates keys itself.       */
/* On-device dictionary solve of a captured .m1h now exists separately         */
/* (mfc_dict_solver.c / nfc_solve_run(), Q_EVENT_NFC_SOLVE); the capture can    */
/* also still be handed to an external tool, off-device, if preferred. This    */
/* view's own menu entry is currently unreachable (see m1_menu.c's "MFC        */
/* Recovery" removal) pending a UI redesign -- independent of the above.       */
/* Footers show only the forward action + control icon (physical BACK handles  */
/* back); body font NFC_FONT_STD, footer font M1_DISP_FUNC_MENU_FONT_N.        */
/*============================================================================*/
#define NFC_HARVEST_SAMPLES   4U   /* nested nonces captured per run */
#define NFC_HARVEST_SEC_ROWS  4    /* sectors shown per Select screen */
static uint8_t s_harvest_ui  = 0;  /* 0=Start  1=Select sector  2=Harvesting/result */
static uint8_t s_harvest_sel = 2;  /* target sector 0..15 */
static bool    s_harvest_led_on = false;

static void nfc_harvest_led_off(void)
{
	if (s_harvest_led_on) {
		m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
		s_harvest_led_on = false;
	}
}

static void nfc_harvest_gui_create(uint8_t param)
{
	(void)param;
	s_harvest_ui  = 0;
	s_harvest_sel = 2;
	s_harvest_led_on = false;
	nfc_ctx_get_harvest()->state = NFC_HARVEST_IDLE;
	m1_uiView_display_update(0);
}

static void nfc_harvest_gui_destroy(uint8_t param)
{
	(void)param;
	nfc_harvest_ui_t *hv = nfc_ctx_get_harvest();
	if (hv->state == NFC_HARVEST_WAIT_CARD || hv->state == NFC_HARVEST_RUNNING)
		nfc_poller_harvest_abort();     /* worker finishes current op, reports STOPPED */
	nfc_harvest_led_off();
}

/* --- sub-mode 0: Start ------------------------------------------------------- */
static void nfc_harvest_draw_start(void)
{
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "MFC Recovery");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	u8g2_DrawStr(&m1_u8g2, 2, 24, "Capture nonces from a");
	u8g2_DrawStr(&m1_u8g2, 2, 35, "MIFARE Classic card");
	u8g2_DrawStr(&m1_u8g2, 2, 46, "to SD for recovery.");
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Start");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	m1_u8g2_nextpage();
}

/* --- sub-mode 1: Select target sector (0..15) -------------------------------- */
static void nfc_harvest_draw_select(void)
{
	char row[20];
	uint8_t top = (s_harvest_sel < NFC_HARVEST_SEC_ROWS) ? 0
	              : (uint8_t)(s_harvest_sel - (NFC_HARVEST_SEC_ROWS - 1));

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "Target Sector");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

	for (uint8_t r = 0; r < NFC_HARVEST_SEC_ROWS; r++) {
		uint8_t s = (uint8_t)(top + r);
		if (s >= M1NFC_MFC_SECTORS_1K) break;
		uint8_t base = (uint8_t)(19 + r * 10);
		snprintf(row, sizeof(row), "Sector %02u", (unsigned)s);
		if (s == s_harvest_sel) {
			u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(base - 8), 121, 10);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 2, base, row);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		} else {
			u8g2_DrawStr(&m1_u8g2, 2, base, row);
		}
	}
	/* scrollbar (16 sectors, NFC_HARVEST_SEC_ROWS visible) */
	{
		const uint8_t track_top = 12, track_bot = 50;
		uint8_t track_h = (uint8_t)(track_bot - track_top);
		uint8_t max_top = (uint8_t)(M1NFC_MFC_SECTORS_1K - NFC_HARVEST_SEC_ROWS);
		uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_HARVEST_SEC_ROWS) / M1NFC_MFC_SECTORS_1K);
		if (thumb_h < 4) thumb_h = 4;
		for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
			u8g2_DrawPixel(&m1_u8g2, 125, yy);
		u8g2_DrawBox(&m1_u8g2, 123,
		    (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * top) / (max_top ? max_top : 1)),
		    4, thumb_h);
	}
	/* footer: CENTER + Select */
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	m1_u8g2_nextpage();
}

/* --- sub-mode 2: Harvesting / result (polls nfc_harvest_ui_t) ---------------- */
static void nfc_harvest_draw_run(nfc_harvest_ui_t *hv)
{
	char line[28];
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "MFC Recovery");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(line, sizeof(line), "Target sector %u", (unsigned)hv->tgt_sector);
	u8g2_DrawStr(&m1_u8g2, 2, 20, line);

	if (hv->state == NFC_HARVEST_WAIT_CARD) {
		u8g2_DrawStr(&m1_u8g2, 2, 36, "Present MFC card...");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Waiting for tag");
		return;
	}
	if (hv->state == NFC_HARVEST_RUNNING) {
		snprintf(line, sizeof(line), "Harvesting %u/%u", (unsigned)hv->got, (unsigned)hv->want);
		u8g2_DrawStr(&m1_u8g2, 2, 40, line);
		return;
	}
	/* terminal */
	if (hv->state == NFC_HARVEST_DONE) {
		u8g2_DrawXBMP(&m1_u8g2, 2, 24, 10, 10, check_10x10);
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
		u8g2_DrawStr(&m1_u8g2, 16, 33, "Success");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
		snprintf(line, sizeof(line), "%u samples saved", (unsigned)hv->got);
		u8g2_DrawStr(&m1_u8g2, 2, 45, line);
		/* footer: CENTER + Solve (recover the key from the capture) */
		u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
		u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 15, 61, "Solve");
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	} else if (hv->state == NFC_HARVEST_STOPPED) {
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
		u8g2_DrawStr(&m1_u8g2, 2, 40, "Stopped");
	} else {   /* NFC_HARVEST_FAIL */
		u8g2_DrawXBMP(&m1_u8g2, 2, 27, 10, 10, error_10x10);
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
		u8g2_DrawStr(&m1_u8g2, 16, 36, "Error");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
		u8g2_DrawStr(&m1_u8g2, 2, 50, "No card / capture fail");
	}
}

/* --- sub-mode 3: solve / verify / result (polls nfc_solve_ui_t) ------------- */
static void nfc_harvest_draw_solve(nfc_solve_ui_t *sv)
{
	char line[28];
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 9, "MFC Recovery");
	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	switch (sv->state) {
	case NFC_SOLVE_UI_SOLVING:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Solving...");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Testing dictionary");
		break;
	case NFC_SOLVE_UI_VERIFYING:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Verifying...");
		u8g2_DrawStr(&m1_u8g2, 2, 48, "Keep card present");
		break;
	case NFC_SOLVE_UI_FOUND:
		u8g2_DrawXBMP(&m1_u8g2, 2, 20, 10, 10, check_10x10);
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
		u8g2_DrawStr(&m1_u8g2, 16, 29, "Key Found");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
		snprintf(line, sizeof(line), "Sector %u  Key %c",
		         (unsigned)sv->tgt_sector, (sv->tgt_keytype == 0x61U) ? 'B' : 'A');
		u8g2_DrawStr(&m1_u8g2, 2, 43, line);
		snprintf(line, sizeof(line), "%02X%02X%02X%02X%02X%02X",
		         sv->key[0], sv->key[1], sv->key[2], sv->key[3], sv->key[4], sv->key[5]);
		u8g2_DrawStr(&m1_u8g2, 2, 55, line);
		break;
	case NFC_SOLVE_UI_NO_KEY:
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH); u8g2_DrawStr(&m1_u8g2, 2, 30, "Not Found");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);  u8g2_DrawStr(&m1_u8g2, 2, 46, "Key not in dictionary");
		break;
	case NFC_SOLVE_UI_AMBIGUOUS:
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH); u8g2_DrawStr(&m1_u8g2, 2, 30, "Ambiguous");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);  u8g2_DrawStr(&m1_u8g2, 2, 46, "Collect more samples");
		break;
	case NFC_SOLVE_UI_UNSUPPORTED:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Unsupported capture");
		break;
	case NFC_SOLVE_UI_INSUFFICIENT:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Too few samples");
		break;
	case NFC_SOLVE_UI_CARD_LOST:
		u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH); u8g2_DrawStr(&m1_u8g2, 2, 30, "Card removed");
		u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);  u8g2_DrawStr(&m1_u8g2, 2, 46, "before verify");
		break;
	case NFC_SOLVE_UI_STOPPED:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Stopped");
		break;
	default:
		u8g2_DrawStr(&m1_u8g2, 2, 34, "Error");
		break;
	}
}

static void nfc_harvest_gui_update(uint8_t param)
{
	(void)param;
	if (s_harvest_ui == 0) { nfc_harvest_draw_start();  return; }
	if (s_harvest_ui == 1) { nfc_harvest_draw_select(); return; }
	if (s_harvest_ui == 3) {
		u8g2_FirstPage(&m1_u8g2);
		nfc_harvest_draw_solve(nfc_ctx_get_solve());
		m1_u8g2_nextpage();
		return;
	}
	u8g2_FirstPage(&m1_u8g2);
	nfc_harvest_draw_run(nfc_ctx_get_harvest());
	m1_u8g2_nextpage();
}

static int nfc_harvest_kp_handler(void)
{
	S_M1_Buttons_Status b;
	nfc_harvest_ui_t *hv = nfc_ctx_get_harvest();
	if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) return 1;

	if (s_harvest_ui == 0) {                 /* Start */
		if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_harvest_ui = 1; m1_uiView_display_update(0);
		} else if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			m1_uiView_display_switch(VIEW_MODE_IDLE, 0);   /* runs destroy (LED off) */
			xQueueReset(main_q_hdl); return 0;             /* exit tool -> NFC menu */
		}
		return 1;
	}
	if (s_harvest_ui == 1) {                 /* Select sector */
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			s_harvest_ui = 0; m1_uiView_display_update(0);
		} else if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (s_harvest_sel > 0) { s_harvest_sel--; m1_uiView_display_update(0); }
		} else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (s_harvest_sel < (M1NFC_MFC_SECTORS_1K - 1)) { s_harvest_sel++; m1_uiView_display_update(0); }
		} else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			nfc_harvest_set_target(s_harvest_sel, NFC_HARVEST_SAMPLES);
			hv->state = NFC_HARVEST_WAIT_CARD; hv->tgt_sector = s_harvest_sel;
			hv->want = NFC_HARVEST_SAMPLES;    hv->got = 0; hv->path[0] = '\0';
			m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);
			s_harvest_led_on = true;
			m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_HARVEST);
			s_harvest_ui = 2; m1_uiView_display_update(0);
		}
		return 1;
	}
	if (s_harvest_ui == 3) {   /* solve / verify / result */
		nfc_solve_ui_t *sv = nfc_ctx_get_solve();
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (sv->state == NFC_SOLVE_UI_SOLVING || sv->state == NFC_SOLVE_UI_VERIFYING) {
				nfc_poller_solve_abort();      /* abort; worker reports STOPPED */
			} else {
				s_harvest_ui = 2; m1_uiView_display_update(0);   /* result -> capture screen */
			}
		}
		return 1;
	}

	/* sub-mode 2: Harvesting / result */
	if ((b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) && (hv->state == NFC_HARVEST_DONE)) {
		/* CENTER + Solve: recover the key from the just-written capture */
		nfc_ctx_get_solve()->state = NFC_SOLVE_UI_SOLVING;
		m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_SOLVE);
		s_harvest_ui = 3; m1_uiView_display_update(0);
		return 1;
	}
	if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
		if (hv->state == NFC_HARVEST_WAIT_CARD || hv->state == NFC_HARVEST_RUNNING) {
			nfc_poller_harvest_abort();       /* abort; worker reports STOPPED */
		} else {
			s_harvest_ui = 1; m1_uiView_display_update(0);   /* terminal -> Select */
		}
	}
	return 1;
}

static int nfc_harvest_gui_message(void)
{
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	int rv = 1;
	ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(150));
	if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
		rv = nfc_harvest_kp_handler();
	m1_uiView_display_update(0);   /* poll worker progress + redraw */
	{
		uint8_t st = nfc_ctx_get_harvest()->state;   /* stop the LED once terminal */
		if (st == NFC_HARVEST_DONE || st == NFC_HARVEST_FAIL || st == NFC_HARVEST_STOPPED)
			nfc_harvest_led_off();
	}
	return rv;
}

static void nfc_harvest_gui_init(void)
{
	m1_uiView_functions_register(VIEW_MODE_NFC_HARVEST,
	    nfc_harvest_gui_create, nfc_harvest_gui_update,
	    nfc_harvest_gui_destroy, nfc_harvest_gui_message);
}

/* Top-level NFC menu entry (NFC -> Tools -> Harvest). */
void nfc_harvest(void)
{
	platformLog("nfc_harvest()\r\n");
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF;
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	m1_uiView_display_switch(VIEW_MODE_NFC_HARVEST, 0);
	while( m1_uiView_q_message_process() ) { ; }
	platformLog("nfc_harvest()-exit\r\n");
}

/*============================================================================*/
/**
 * @brief nfc_saved - Load and display NFC card data from saved file
 * 
 * This function allows the user to browse and load a previously saved
 * NFC card file from SD card. It validates the file extension (.nfc),
 * loads the file data into NFC context, and enters the submenu view
 * with the loaded card data.
 * 
 * The function handles various error conditions:
 * - File not selected
 * - Invalid file name
 * - Invalid extension
 * - File load failure
 * 
 * @retval None
 */
/*============================================================================*/
/*============================================================================*/
/**
 * @brief nfc_saved - Browse and load NFC card data from saved file
 * 
 * This function uses the uiView system to browse and load a previously saved
 * NFC card file from SD card. It follows the same pattern as RFID's saved
 * functionality for consistency.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_saved_launch(const char *dir_name, const char *file_name)
{
	bool owns_lifecycle = false;

	memset(&s_nfc_saved_launch, 0, sizeof(s_nfc_saved_launch));
	if ((dir_name == NULL) != (file_name == NULL))
	{
		platformLog("[NFC] saved launch rejected incomplete path\r\n");
		return;
	}
	if (dir_name != NULL)
	{
		if (strlen(dir_name) > ESP_FILE_PATH_LEN_MAX ||
		    strlen(file_name) > ESP_FILE_NAME_LEN_MAX)
		{
			platformLog("[NFC] saved launch rejected overlong path\r\n");
			return;
		}
		memcpy(s_nfc_saved_launch.dir, dir_name, strlen(dir_name) + 1U);
		memcpy(s_nfc_saved_launch.name, file_name, strlen(file_name) + 1U);
		s_nfc_saved_launch.file.dir_name = s_nfc_saved_launch.dir;
		s_nfc_saved_launch.file.file_name = s_nfc_saved_launch.name;
		s_nfc_saved_launch.file.file_is_selected = true;
		s_nfc_saved_launch.file.status = FB_OK;
		s_nfc_saved_launch.external = true;
		s_nfc_saved_launch.pending = true;

		menu_nfc_init();
		owns_lifecycle = true;
		if (nfc_worker_task_hdl == NULL || nfc_worker_q_hdl == NULL)
		{
			platformLog("[NFC] saved launch failed: worker lifecycle unavailable\r\n");
			m1_message_box(&m1_u8g2, "NFC unavailable", "", NULL, res_string(IDS_BACK));
			menu_nfc_deinit();
			memset(&s_nfc_saved_launch, 0, sizeof(s_nfc_saved_launch));
			return;
		}
	}

	/* Hardware finding (NFC2-005): this function used to declare and init an
	 * unused stack-local `nfc_run_ctx_t`. That struct's full frame (0x48D4
	 * = 18,644 bytes per the linked ELF) overflowed the calling
	 * subfunc_handler_task's 4096-word (16,384-byte) stack immediately on
	 * entry, resetting the M1 the instant NFC > Saved was opened. Nothing
	 * below ever referenced it -- saved-file loading uses the global NFC
	 * context, not a local one -- so it is removed rather than fixed in
	 * place; do not reintroduce a local one of that type in this function,
	 * and do not grow the task's stack to make room for one instead. */
	platformLog("nfc_saved_launch(external=%d)\r\n", (int)s_nfc_saved_launch.external);
	m1_gui_submenu_update(NULL, 0, 0, X_MENU_UPDATE_INIT);
	nfc_uiview_gui_latest_param = 0xFF; // Initialize with an invalid parameter
	// initial
	m1_uiView_functions_init(VIEW_MODE_NFC_END, view_nfc_read_table);
	/* Open the Saved Files browser directly inside the NFC save directory
	 * (one-shot start dir; other file-browser users are unaffected). Ensure
	 * the folder exists so a first-time user with no saves gets an empty list
	 * instead of an SD error. Back from this list returns to the NFC menu. */
	if (!m1_fb_check_existence(NFC_SAVE_DIR)) m1_fb_make_dir(NFC_SAVE_DIR);
	m1_fb_set_start_dir(NFC_SAVE_DIR);
	m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);

	// loop
	while( m1_uiView_q_message_process() )
	{
		;
	}

	memset(&s_nfc_saved_launch, 0, sizeof(s_nfc_saved_launch));
	if (owns_lifecycle)
		menu_nfc_deinit();
}

void nfc_saved(void)
{
	nfc_saved_launch(NULL, NULL);
}


/*============================================================================*/
/**
 * @brief nfc_read_more_options_save - Save NFC card data to SD card file
 * 
 * Saves the current NFC card context to a file on the SD card in
 * the NFC directory. The function:
 * 1. Checks SD card free space (minimum 4KB required)
 * 2. Creates /NFC directory if it doesn't exist
 * 3. Prompts user for filename using virtual keyboard
 * 4. Validates filename doesn't already exist
 * 5. Writes card data in M1 NFC device format (Version 4)
 * 6. Includes device type, UID, ATQA, SAK, and page dumps (for Ultralight/NTAG)
 * 
 * @retval 0 Success
 * @retval 1 Insufficient SD card space
 * @retval 2 Directory creation failed
 * @retval 3 User cancelled (escaped)
 * @retval 4 File creation failed
 */
/*============================================================================*/
/*============================================================================*/
/**
 * @brief nfc_draw_saved_screen - Procedural "save complete" splash
 *
 * Centered composition drawn entirely with u8g2 primitives (no bitmap asset):
 * a rounded check-box icon with a bold checkmark, and the word "SAVED" below.
 * Same single FirstPage/nextpage render path as m1_image_message(), so the
 * screen's persistence/dismissal behaviour is unchanged.
 *
 * @retval None
 */
/*============================================================================*/
static void nfc_draw_saved_screen(void)
{
	m1_draw_file_saved_screen();   /* unified save-success splash */
}

static uint8_t nfc_read_more_options_save(void)
{
	char filepath[128];
	uint8_t error;
	nfc_run_ctx_t* c;

	// Get filename from user and create full path
	error = nfc_save_file_keyboard(filepath, sizeof(filepath));
	if (error != 0) {
		// Error or user escaped
		if (error != 3) { // Not user escape - show error
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		}
		return error;
	}

	// Save NFC profile to file
	c = nfc_ctx_get();
	if (!c) {
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		return 4; // Error
	}

	if (nfc_profile_save(filepath, c)) {
		nfc_draw_saved_screen();   // procedural check-box + "SAVED" splash
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);  // same 2s auto-return as RFID
		return 0; // Success
	} else {
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		return 4; // Error
	}
} 

/*============================================================================*/
/**
 * @brief nfc_edit_uid_kp_handler - Handle keypad input for NFC edit UID view
 * 
 * Processes button events in the NFC edit UID view:
 * - BACK: Return to submenu
 * 
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_edit_uid_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	}
	return 1;
}

/*============================================================================*/
/**
 * @brief nfc_edit_uid_gui_create - Create and initialize NFC edit UID view
 * 
 * Initializes the edit UID view and triggers an update.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_edit_uid_gui_create(uint8_t param)
{
	s_edit_uid_started = false;  // 초기화
	m1_uiView_display_update(param);
}

/*============================================================================*/
/**
 * @brief nfc_edit_uid_gui_destroy - Destroy NFC edit UID view
 * 
 * Cleanup function for edit UID view (currently empty).
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_edit_uid_gui_destroy(uint8_t param)
{
	s_edit_uid_started = false;  // 플래그 초기화
}

/*============================================================================*/
/**
 * @brief nfc_edit_uid_gui_update - Update NFC edit UID view display
 * 
 * Handles UID editing using virtual keyboard. This function is called
 * from the view system's update cycle, allowing m1_vkbs_get_data() to
 * work properly within the message loop.
 * 
 * @param[in] param View parameter (0 = start edit)
 * @retval None
 */
/*============================================================================*/
static void nfc_edit_uid_gui_update(uint8_t param)
{
	char data_buffer[64];
	uint8_t data_size;
	uint8_t val, error;

	// param==0일 때만 실행 (한 번만 실행되도록)
	if (param != 0 || s_edit_uid_started) {
		return;
	}
	
	s_edit_uid_started = true;  // 시작 플래그 설정
	
	nfc_run_ctx_t* c = nfc_ctx_get();
	if (!c || c->file.source_kind != LOAD_FILE) {
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	// Get current UID length
	data_size = c->head.uid_len;
	if (data_size==0 || data_size > 10) {
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	memset(data_buffer, 0, sizeof(data_buffer));

	// Set initial text from current UID
	m1_byte_to_hextext(c->head.uid, data_size, data_buffer);

	// Debug: Print data_buffer contents before m1_vkbs_get_data
	platformLog("[NFC Edit UID] data_size=%d, uid_len=%d\r\n", data_size, c->head.uid_len);
	platformLog("[NFC Edit UID] data_buffer='%s' (len=%d)\r\n", data_buffer, (int)strlen(data_buffer));
	platformLog("[NFC Edit UID] UID bytes: %s\r\n", hex2Str(c->head.uid, data_size));

	// Get new UID from user (hex input) - this works within view message loop
	val = m1_vkbs_get_data("Enter hex UID:", data_buffer);
	

	if (val) {
		// Validate and convert hex string to bytes
		uint8_t new_uid[10];
		memset(new_uid, 0, sizeof(new_uid));
		
		int converted = m1_strtob_with_base(data_buffer, new_uid, sizeof(new_uid), 16);
		
		// Validate length matches original
		if (converted != data_size) {
			// Show error
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
			uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
			return;
		}

		// Update context
		memcpy(c->head.uid, new_uid, data_size);
		c->head.uid_len = data_size;
		nfc_ctx_refresh_ui();

		error = 1;
		// Save to existing file
		if (c->file.path[0] != '\0')
		{
			// Use nfc_profile_save to save updated data
			if (nfc_profile_save(c->file.path, c))
			{
				error = 0;
			}
		}

		if ( error )
			m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		else
			nfc_draw_saved_screen();   // procedural check-box + "SAVED" splash (edit-UID save)
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
	} else {
		// User cancelled
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
	}
}

/*============================================================================*/
/**
 * @brief nfc_edit_uid_gui_message - Process messages for NFC edit UID view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_edit_uid_gui_message(void)
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
			ret_val = nfc_edit_uid_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	} 

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_edit_uid_gui_init - Initialize and register NFC edit UID view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC edit UID view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_edit_uid_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_EDIT_UID, nfc_edit_uid_gui_create, nfc_edit_uid_gui_update, nfc_edit_uid_gui_destroy, nfc_edit_uid_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_read_more_options_delete - Delete loaded NFC card file
 * 
 * Shows confirmation dialog and deletes the loaded NFC card file.
 * 
 * @retval 0 File deleted successfully (exit signal)
 * @retval 1 User cancelled or error
 */
/*============================================================================*/
static uint8_t nfc_read_more_options_delete(void)
{
	// Wait for user input
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	// Show confirmation dialog
	char szString[128];
	char filename[64];

	nfc_run_ctx_t* c = nfc_ctx_get();
	if (!c || c->file.source_kind != LOAD_FILE) {
		return 1; // Only available for loaded files
	}

	if (c->file.path[0]=='\0') {
		return 1; // No file path
	}
	
	// Extract filename from path
	const char *pbuff = fu_get_filename(c->file.path);
	if (pbuff) {
		strncpy(filename, pbuff, sizeof(filename) - 1);
		filename[sizeof(filename) - 1] = '\0';
		fu_get_filename_without_ext(filename, filename, sizeof(filename));
	} else {
		strcpy(filename, "file");
	}

	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	u8g2_DrawStr(&m1_u8g2, 2, 12, "Delete file?");

	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(szString, sizeof(szString), "Name: %s", filename);
	u8g2_DrawStr(&m1_u8g2, 2, 22, szString);

	u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
	// Use title_text from NFC context
	if (c->ui.title_text[0] != '\0') {
		strncpy(szString, c->ui.title_text, sizeof(szString) - 1);
		szString[sizeof(szString) - 1] = '\0';
	} else {
		strcpy(szString, "NFC");
	}
	u8g2_DrawStr(&m1_u8g2, 2, 32, szString);

	u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
	snprintf(szString, sizeof(szString), "UID: %s", hex2Str(c->head.uid, c->head.uid_len));
	u8g2_DrawStr(&m1_u8g2, 2, 42, szString);

	// Bottom bar: LEFT=Cancel, RIGHT=Delete
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Cancel", "Delete", arrowright_8x8);
	m1_u8g2_nextpage();

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if (q_item.q_evt_type==Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (ret==pdTRUE)
				{
					if (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK)
					{
						// Confirm delete
						uint8_t delete_ret = m1_fb_delete_file(c->file.path);
						if (delete_ret==0) {
							// Show success message
							u8g2_FirstPage(&m1_u8g2);
							u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
							u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
							u8g2_DrawStr(&m1_u8g2, 2, 12, "Delete");
							u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
							u8g2_DrawStr(&m1_u8g2, 10, 22, "Delete success");
							m1_u8g2_nextpage();
							uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);

							return 0; // Exit signal
						} else {
							// Show error
							m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
							uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);

							return 1;
						}
					} // else if (this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK)
					else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ||
							this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
					{
						// Cancel
						return 1;
					}
				} // if (ret==pdTRUE)
			} // if (q_item.q_evt_type==Q_EVENT_KEYPAD)
		} // if (ret==pdTRUE)
	} // while (1)

	return 1;
} 


/*============================================================================*/
/**
 * @brief nfc_rename_kp_handler - Handle keypad input for NFC rename view
 * 
 * Processes button events in the NFC rename view:
 * - BACK: Return to submenu
 * 
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_rename_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
	if (ret==pdTRUE)
	{
		if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	}
	return 1;
}

/*============================================================================*/
/**
 * @brief nfc_rename_gui_create - Create and initialize NFC rename view
 * 
 * Initializes the rename view and triggers an update.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_rename_gui_create(uint8_t param)
{
	m1_uiView_display_update(param);
}

/*============================================================================*/
/**
 * @brief nfc_rename_gui_destroy - Destroy NFC rename view
 * 
 * Cleanup function for rename view (currently empty).
 * 
 * @param[in] param View parameter (unused)
 * @retval None
 */
/*============================================================================*/
static void nfc_rename_gui_destroy(uint8_t param)
{
	;
}

/*============================================================================*/
/**
 * @brief nfc_rename_gui_update - Update NFC rename view display
 * 
 * Handles file renaming using virtual keyboard. This function is called
 * from the view system's update cycle, allowing m1_vkb_get_filename() to
 * work properly within the message loop.
 * 
 * @param[in] param View parameter (0 = start rename)
 * @retval None
 */
/*============================================================================*/
static void nfc_rename_gui_update(uint8_t param)
{
	char new_file[256];  // Increased buffer size to avoid truncation warning
	char old_file[128];
	char fname[50];
	char dname[50];
	uint8_t ret, error;

	nfc_run_ctx_t* c = nfc_ctx_get();
	if (!c || c->file.source_kind != LOAD_FILE) {
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	if (c->file.path[0]=='\0') {
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	// Extract current filename without extension
	// fu_get_filename_without_ext takes full path and extracts filename without extension
	if (c->file.path[0] != '\0') {
		fu_get_filename_without_ext(c->file.path, dname, sizeof(dname));
	} else {
	    srand(HAL_GetTick());
	    	sprintf((char*)dname, "nfc_%05u", rand() % 0xFFFFF);
	}

	// Get new filename from user
			ret = m1_vkb_get_filename("Enter filename:", (char*)dname, (char*)fname, 0 /* existing loaded-file name: normal delete */);
	if (!ret) {
		// User escaped
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	// Build new file path
	strcpy(old_file, c->file.path);
	
	// Get directory path from old file
	char dir_path[128];
	fu_get_directory_path(old_file, dir_path, sizeof(dir_path));
	
	// Combine directory + new filename + extension
	// Check return value to ensure no truncation
	int snprintf_ret = snprintf(new_file, sizeof(new_file), "%s/%s%s", dir_path, fname, NFC_FILE_EXTENSION);
	if (snprintf_ret < 0 || snprintf_ret >= (int)sizeof(new_file)) {
		// Path truncated or error occurred
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	// Check if new file already exists
	if (m1_fb_check_existence(new_file)) {
		// Show error - file exists
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
		uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
		m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		return;
	}

	// Rename file
	error = 1;
	FRESULT res = f_rename(old_file, new_file);
	if (res==FR_OK) {
		error = 0;
		// Update context with new path
		strncpy(c->file.path, new_file, sizeof(c->file.path) - 1);
		c->file.path[sizeof(c->file.path) - 1] = '\0';
	}

	if (error)
		m1_image_message(sd_card_error_46x36, SDCARD_ERROR_IMAGE_WIDTH, SDCARD_ERROR_IMAGE_HEIGHT, sdcard_access_error_message);
	else
		m1_draw_file_saved_screen();
	uiScreen_timeout_start(UI_SCREEN_TIMEOUT, NULL);
	m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
}

/*============================================================================*/
/**
 * @brief nfc_rename_gui_message - Process messages for NFC rename view
 * 
 * Handles messages from the main queue, primarily keypad events.
 * 
 * @retval 0 Exit requested
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_rename_gui_message(void)
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
			ret_val = nfc_rename_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if ( q_item.q_evt_type==Q_EVENT_MENU_TIMEOUT )
		{
			m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_REFRESH);
		}
	} 

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_rename_gui_init - Initialize and register NFC rename view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC rename view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_rename_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_RENAME, nfc_rename_gui_create, nfc_rename_gui_update, nfc_rename_gui_destroy, nfc_rename_gui_message);
}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_kp_handler - Handle keypad input for NFC saved browse view
 * 
 * Processes button events in the NFC saved browse view:
 * - BACK: Exit to IDLE
 * 
 * @retval 0 Exit
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_saved_browse_kp_handler(void)
{
	S_M1_Buttons_Status this_button_status;

	if(xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE)
		return 1;

	if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
	{
		; // Do extra tasks here if needed
		m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
		xQueueReset(main_q_hdl); // Reset main q before return
		return 0;
	} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )

	return 1;
}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_gui_create - Create and initialize NFC saved browse view
 * 
 * Initializes the saved browse view and triggers an update.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_saved_browse_gui_create(uint8_t param)
{
	m1_uiView_display_update(0);
}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_gui_destroy - Destroy NFC saved browse view
 * 
 * Cleanup function for the saved browse view.
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_saved_browse_gui_destroy(uint8_t param)
{

}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_gui_update - Update NFC saved browse view
 * 
 * Handles file browsing and loading. When a file is selected:
 * - Validates file extension (.nfc)
 * - Loads file using nfc_storage_load_file()
 * - On success: switches to submenu view
 * - On failure: shows error message and returns to browse
 * 
 * @param[in] param View parameter
 * @retval None
 */
/*============================================================================*/
static void nfc_saved_browse_gui_update(uint8_t param)
{
	if (s_nfc_saved_launch.external && !s_nfc_saved_launch.pending)
	{
		(void)m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
		return;
	}
	if (s_nfc_saved_launch.pending)
	{
		s_nfc_saved_launch.pending = false;
		f_info = &s_nfc_saved_launch.file;
	}
	else
	{
		f_info = storage_browse();
	}
	if (f_info == NULL)
	{
		(void)m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
		return;
	}
	if ( f_info->file_is_selected )
	{
		if(nfc_profile_load(f_info, NFC_FILE_EXTENSION_TMP))
		{
			/* Same Level-1 routing as a live read's Q_EVENT_NFC_READ_COMPLETE
			 * handler above: a saved card whose file recorded a recognized
			 * transit result (nfc_storage_load_file() -> nfc_ctx_clear_desfire()
			 * resets .transit, then the "Clipper Card:"/"Clipper Mobile
			 * Device:" body lines repopulate card_id) must reopen straight to
			 * its human-readable Transit screen, not the generic action menu
			 * -- reopening a saved Clipper card is not a lesser case than
			 * scanning it live. */
			if (nfc_ctx_get_transit()->card_id != NfcTransitCardUnknown)
				m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_DESFIRE_TRANSIT);
			else
				m1_uiView_display_switch(VIEW_MODE_NFC_READ_MORE, X_MENU_UPDATE_RESET);
		}
		else
		{
			m1_message_box(&m1_u8g2, res_string(IDS_UNSUPPORTED_FILE_), " ", NULL, res_string(IDS_BACK));
			m1_uiView_display_switch(VIEW_MODE_NFC_SAVED_BROWSE, 0);
		}
	} // if ( f_info->file_is_selected )
	else	// user escaped?
	{
	    m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
	}
}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_gui_message - Handle messages for NFC saved browse view
 * 
 * Processes messages from the main queue:
 * - Q_EVENT_KEYPAD: Handles button events
 * - Q_EVENT_MENU_EXIT: Exits to IDLE
 * 
 * @retval 0 Exit
 * @retval 1 Continue processing
 */
/*============================================================================*/
static int nfc_saved_browse_gui_message(void)
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
			ret_val = nfc_saved_browse_kp_handler();
		} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		else if(q_item.q_evt_type==Q_EVENT_MENU_EXIT)
		{
			m1_uiView_display_switch(VIEW_MODE_IDLE, 0);
			xQueueReset(main_q_hdl); // Reset main q before return
			ret_val = 0;
		}
	} // if (ret==pdTRUE)

	return ret_val;
}

/*============================================================================*/
/**
 * @brief nfc_saved_browse_gui_init - Initialize and register NFC saved browse view functions
 * 
 * Registers the view functions (create, update, destroy, message) for
 * the NFC saved browse view mode.
 * 
 * @retval None
 */
/*============================================================================*/
void nfc_saved_browse_gui_init(void)
{
   m1_uiView_functions_register(VIEW_MODE_NFC_SAVED_BROWSE, nfc_saved_browse_gui_create, nfc_saved_browse_gui_update, nfc_saved_browse_gui_destroy, nfc_saved_browse_gui_message);
}

