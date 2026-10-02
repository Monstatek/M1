/*
*
* m1_bt.c
*
* Source for M1 bluetooth
*
* M1 Project
*
*/
/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/********************************************************************************
*
* This file is for gatt server. It can send adv data, and get connected by client.
*
*********************************************************************************/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_esp_uart_transport.h"
#include "m1_bt.h"
#include "m1_ble_signal.h"
#include "m1_display.h"
#include "m1_esp32_hal.h"
#include "m1_usb_cdc_msc.h"
#include "m1_log_debug.h"
#include "spi_master.h"
#include "esp_app_main.h"
#include "esp_at_list.h"
#include "m1_file_browser.h"
#include "m1_sdcard_provision.h"
#include "m1_virtual_kb.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG				"BLE"
#define SEC_TO_MILLISEC(x) (1000*(x))

#define M1_GUI_ROW_SPACING			1

#define M1_BLE_SCANNING_TIME		10 // seconds
#define M1_BLE_ADVERTISE_TIME       10 // seconds
#define M1_BLE_SCAN_RETRY_MAX      3U
#define BLE_SCAN_MAX_RESULTS       64U
#define BLE_SCAN_NAME_MAX          40U
#define BLE_SCAN_RESP_BUF_SIZE     4096U

#define BLE_SAVE_DIR               M1_SD_DIR_BLE
#define BLE_SAVE_EXT               ".ble"

#define ESP32UART_BUFFER_SIZE       1024*4 // byte

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

typedef struct {
	wifi_scanlist_t ble_advertising_inf;
	wifi_scanlist_t ble_discovery_inf;
} ble_target_t;

/*
 * Canonical BLE scan-result model (current ESP32-C6 firmware contract).
 *
 * The firmware discovery workflow is:  scan  ->  list  ->  list <id>
 *   - "scan"      performs GAP discovery and streams progress lines.
 *   - "list"      prints numbered summary rows  "[NN] MAC RSSI=d NAME=s".
 *   - "list <id>" prints the detailed ADV fields for one device.
 * The array index below IS the firmware scan-list id used by list/connect/signal.
 * This single model is shared by Scan Devices, Device Details, and (later) Signal
 * Meter and GATT Explorer; no other BLE screen keeps its own scan table.
 */
typedef struct {
	char bssid[BSSID_STR_SIZE];
	int rssi;
	char name[BLE_SCAN_NAME_MAX];
    unsigned company;
    uint32_t age_ms;
    uint16_t wire_index;
} ble_scan_item_t;

/* Device currently selected by the user; persists across BLE functions until a new
 * scan, an explicit re-selection, or the BLE session resets (menu exit). */
typedef struct {
	uint8_t  valid;
	uint16_t index;                    /* firmware scan-list id (list/connect/signal) */
	char     mac[BSSID_STR_SIZE];      /* durable identity (with address; type held on ESP32) */
	char     name[BLE_SCAN_NAME_MAX];  /* advertised local name (may be empty)               */
	char     disp[BLE_SCAN_NAME_MAX];  /* friendly presentation label (name or fallback) */
	int      rssi;
    uint32_t age_ms;
} ble_selected_t;

/* Parsed "list <id>" advertisement detail (only present fields are flagged). */
typedef struct {
	uint8_t  has_rssi;      int      rssi;
	uint8_t  has_flags;     unsigned flags;
	uint8_t  has_name;      uint8_t  name_complete;  char name[BLE_SCAN_NAME_MAX];
	uint8_t  has_tx;        int      tx_power;
	uint8_t  uuid16_count;  uint16_t uuid16[8];
	uint8_t  has_uuid128;
	uint8_t  has_mfg;       unsigned mfg_company;    char mfg_hex[48];
	uint8_t  has_svc_data;  unsigned svc_uuid;       char svc_hex[48];
	uint8_t  has_raw;       char     raw_hex[80];
	uint8_t  has_rsp;       char     rsp_hex[80];
	uint8_t  has_addr;      char     addr[BSSID_STR_SIZE];  /* from a saved record's "Address:" line */
} bt_detail_t;

static ble_scan_item_t g_ble_scan_items[BLE_SCAN_MAX_RESULTS];
static uint16_t g_ble_scan_count = 0U;
static uint8_t g_bt_mode_switch_fail = 0U;
static ble_selected_t g_ble_sel;

ble_target_t *bletarget;

extern uint8_t DEBUG_esp32_reset_pin;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_bluetooth_init(void);
void menu_bluetooth_exit(void);
void bluetooth_scan_devices(void);
void bluetooth_signal_meter(void);
void bluetooth_gatt_explorer(void);
void bluetooth_saved(void);
static uint16_t bt_scan_parse_output(const char *resp_text);
static uint8_t bt_cmd_process(const char *cmd_option, char *resp_buf, uint16_t resp_buf_size, uint32_t timeout_sec);
extern void  esp32_main_init(void);

/* Canonical scan / selection / detail helpers (single source of truth for all BLE screens). */
static void    ble_sel_clear(void);
static void    ble_sel_set_from_item(uint16_t index);
static void    bt_detail_view(uint16_t index);
static void    bt_detail_render(const bt_detail_t *d, const char *mac, const char *name, int rssi, const char *raw_for_save);
static void    bt_detail_save(const char *mac, const char *name, const char *raw);
static void    bt_saved_view(const char *dir, const char *filename);
static void    bt_parse_detail(const char *resp, bt_detail_t *d);
static const char *bt_uuid16_name(uint16_t uuid);
static const char *bt_uuid16_char_name(uint16_t uuid);
static void    ble_get_display_name(uint16_t index, char *out, uint16_t sz);
static void    bt_draw_str_clipped(int x, int y, const char *text, int max_px);

/* Small shared UI primitives (BLE screens). */
static void    bt_draw_title(const char *title);
static void    bt_draw_title_paged(const char *title, uint8_t cur, uint8_t total);
static const char *bt_detail_page_title(uint8_t kind);
static void    bt_draw_actionbar(const char *ok_label, const char *hint);
static void    bt_draw_scrollbar(uint16_t index, uint16_t total, uint16_t visible, uint8_t y_top, uint8_t y_bottom);
static uint8_t bt_wait_button(void);
static uint8_t bt_poll_button(void);
static uint16_t bt_read_line(char *out, uint16_t out_size, uint32_t timeout_ms);
static void    bt_stream_reset(void);
static void    bt_message_screen(const char *title, const char *l1, const char *l2);
static void    bt_signal_draw(uint8_t state, int avg, const char *catlabel);
static uint16_t bt_scan_list_cached(void);
static uint8_t bt_signal_pick_target(void);
static int     bt_device_picker(const char *title, const char *center_label);
static void    bt_signal_run(void);
static uint8_t bt_session_ensure(void);
static void    bt_session_end(void);
static void    bt_session_resume_scan(void);

typedef struct {
	enCdcMode prev_usbcdc_mode;
	uint8_t restore_usbcdc_mode;
} bt_cmd_prep_ctx_t;

static uint8_t bt_prepare_esp32_cmd_path(bt_cmd_prep_ctx_t *ctx, uint8_t show_init_screen, uint8_t show_fail_screen);
static void bt_restore_esp32_cmd_path(const bt_cmd_prep_ctx_t *ctx);


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes display for this sub-menu item.
 */
/*============================================================================*/
void menu_bluetooth_init(void)
{
    DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);
    /* Start one background BLE discovery for the whole Bluetooth session; the submenu is
     * usable immediately and the nearby-device table fills in as devices arrive. */
    (void)bt_session_ensure();
} // void menu_bluetooth_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void  menu_bluetooth_exit(void)
{
    /* Leaving Bluetooth: stop background discovery and release the ESP32 path, then reset
     * the chip. The shared table lives only for the session, so clear the selection. */
    bt_session_end();
    ble_sel_clear();
    g_ble_scan_count = 0U;

    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET);    // esp32 Reset
    osDelay(20); DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);
} // void  menu_bluetooth_exit(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t bt_prepare_esp32_cmd_path(bt_cmd_prep_ctx_t *ctx, uint8_t show_init_screen, uint8_t show_fail_screen)
{
	uint8_t need_esp32_reinit = 0U;
	uint8_t esp32_prompt_ready = 0U;
	uint8_t ready_try;
	uint32_t prompt_timeout_ms;
	static const char esp32_prompt_ping[] = "\r\n";
	char prompt_resp[96] = {0};

    char mode_resp[96] = {0};
    static const char mode_ble_cmd[] = "mode -b\r\n";

	if (ctx == NULL)
	{
		return ERROR;
	}

	ctx->prev_usbcdc_mode = m1_usbcdc_mode;
	ctx->restore_usbcdc_mode = 0U;

	if (ctx->prev_usbcdc_mode == CDC_MODE_ESP32)
	{
		ctx->restore_usbcdc_mode = 1U;
		m1_logdb_deinit();
		m1_usbcdc_mode = CDC_MODE_LOG_CLI;
		m1_logdb_init();
	}

	if (!m1_esp32_get_init_status())
	{
		need_esp32_reinit = 1U;
	}

	if (HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin) == GPIO_PIN_RESET)
	{
		need_esp32_reinit = 1U;
	}

	if (need_esp32_reinit)
	{
		if (m1_esp32_get_init_status())
		{
			m1_esp32_deinit();
		}
		m1_esp32_init();

		if (!get_esp32_main_init_status())
		{
			esp32_main_init();
		}

		if (show_init_screen)
		{
			m1_u8g2_firstpage();
			u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
			u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
			m1_u8g2_nextpage();
		}
	}

	for (ready_try = 0U; ready_try < 3U; ready_try++)
	{
		if (need_esp32_reinit)
		{
			prompt_timeout_ms = 1200U + ((uint32_t)ready_try * 700U);
		}
		else
		{
			prompt_timeout_ms = 300U + ((uint32_t)ready_try * 400U);
		}

		memset(prompt_resp, 0, sizeof(prompt_resp));

		(void)esp32_uart_write((const uint8_t *)esp32_prompt_ping,
							   (uint16_t)(sizeof(esp32_prompt_ping) - 1U),
							   100U);

		if (esp32_uart_read_complete_prompt(prompt_resp,
										 (uint16_t)sizeof(prompt_resp),
										 prompt_timeout_ms,
										 ">> ") > 0U)
		{
			if (strstr(prompt_resp, ">> ") != NULL)
			{
				esp32_prompt_ready = 1U;
				break;
			}
		}

		m1_esp32_deinit();
		osDelay(20);
		m1_esp32_init();
		if (!get_esp32_main_init_status())
		{
			esp32_main_init();
		}

		need_esp32_reinit = 1U;
		osDelay(150);
	}

	if (!esp32_prompt_ready)
	{
		if (show_fail_screen)
		{
			m1_u8g2_firstpage();
			u8g2_DrawStr(&m1_u8g2, 2, 15, "ESP32 not ready");
			u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING, "Try bridge reset");
			m1_u8g2_nextpage();
			osDelay(1200);
		}
		return ERROR;
	}

	m1_esp32_reset_buffer();

    /* Always switch to BLE mode first and confirm prompt reception. */
    if (!esp32_uart_write((const uint8_t *)mode_ble_cmd, (uint16_t)(sizeof(mode_ble_cmd) - 1U), 100U))
    {
        g_bt_mode_switch_fail = 1U;
        return ERROR;
    }

    if (esp32_uart_read_complete_prompt(mode_resp,
                                     (uint16_t)sizeof(mode_resp),
                                     1500U,
                                     ">> ") == 0U)
    {
        g_bt_mode_switch_fail = 1U;
        return ERROR;
    }

	return SUCCESS;
} // static uint8_t bt_prepare_esp32_cmd_path(bt_cmd_prep_ctx_t *ctx, uint8_t show_init_screen, uint8_t show_fail_screen)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void bt_restore_esp32_cmd_path(const bt_cmd_prep_ctx_t *ctx)
{
	if ((ctx != NULL) && ctx->restore_usbcdc_mode && (m1_usbcdc_mode != ctx->prev_usbcdc_mode))
	{
		m1_logdb_deinit();
		m1_usbcdc_mode = ctx->prev_usbcdc_mode;
		m1_logdb_init();
	}
} // static void bt_restore_esp32_cmd_path(const bt_cmd_prep_ctx_t *ctx)






/*============================================================================*/
/*
 * This function scans for devices.
 */
/*============================================================================*/
void bluetooth_scan_devices(void)
{
    /* Scan Devices no longer owns scanning: it displays the shared live discovery table and
     * updates while background discovery runs. CENTER opens Device Details. */
    if (bt_session_ensure() != SUCCESS)
    {
        bt_message_screen("Bluetooth", "BLE not ready", "Try again");
        return;
    }

    for (;;)
    {
        int idx = bt_device_picker("BLE Devices", "Details");
        if (idx < 0) { break; }                    /* BACK -> Bluetooth menu */
        ble_sel_set_from_item((uint16_t)idx);
        bt_detail_view((uint16_t)idx);             /* CENTER = Details inspector */
    }
} // void bluetooth_scan_devices(void)


/*============================================================================*/
/* Shared BLE UI primitives + selection + scan acquisition                    */
/*============================================================================*/

/* One 4 KB scratch buffer shared by scan/list fetches (never used concurrently). */
static char g_bt_scratch[BLE_SCAN_RESP_BUF_SIZE];

/* ---- Bluetooth session: ONE background discovery for the whole submenu ---- */
static bt_cmd_prep_ctx_t g_ble_ses_ctx;
static uint8_t           g_ble_ses_active = 0U;

/* Bring up the ESP32 BLE path (mode -b) and start one continuous duty-cycled active scan
 * that keeps the ESP32's bounded, address-keyed device table updated in the background for
 * the whole time the user is inside Bluetooth. Snapshot it with 'list' on demand - no
 * per-screen blocking scan. Idempotent; returns SUCCESS when discovery is running. */
static uint8_t bt_session_ensure(void)
{
    if (g_ble_ses_active) { return SUCCESS; }
    if (bt_prepare_esp32_cmd_path(&g_ble_ses_ctx, 1U, 1U) != SUCCESS) { return ERROR; }
    if (bt_cmd_process("scan live", g_bt_scratch, (uint16_t)sizeof(g_bt_scratch), 5U) != SUCCESS ||
        strstr(g_bt_scratch, "[BLE:SCAN:LIVE]") == NULL) {
        bt_restore_esp32_cmd_path(&g_ble_ses_ctx);
        return ERROR;
    }
    g_ble_ses_active = 1U;
    return SUCCESS;
}

/* Stop background discovery and release the ESP32 path. */
static void bt_session_end(void)
{
    if (!g_ble_ses_active) { return; }
    (void)bt_cmd_process("stop", g_bt_scratch, (uint16_t)sizeof(g_bt_scratch), 3U);
    m1_esp32_deinit();
    bt_restore_esp32_cmd_path(&g_ble_ses_ctx);
    g_ble_ses_active = 0U;
}

/* Resume background discovery after a targeted op (Signal Meter / GATT) paused it. Uses 'resume'
 * (not 'scan') so the ESP32 keeps the accumulated device table and any GATT-learned names, rather
 * than wiping and rediscovering from scratch. */
static void bt_session_resume_scan(void)
{
    if (g_ble_ses_active) { (void)bt_cmd_process("resume", g_bt_scratch, (uint16_t)sizeof(g_bt_scratch), 5U); }
}

/* Header frame (14 px) + title in the main font; leaves the small body font set. */
static void bt_draw_title(const char *title)
{
    /* Clean header: title text + a single hairline divider (no bordered box). */
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, title);
    u8g2_DrawHLine(&m1_u8g2, 0, 13, 128);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
}

/* Header for a horizontal paged view: title (left) + dynamic "X/Y" counter (upper-right),
 * on the same header row, no decorative box. The title is clipped so it can never reach
 * or overlap the counter. Used by Device Details; the counter updates on LEFT/RIGHT. */
static void bt_draw_title_paged(const char *title, uint8_t cur, uint8_t total)
{
    char cnt[12];
    uint16_t cw;
    int avail;
    char tbuf[24];
    uint16_t i;

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    (void)snprintf(cnt, sizeof(cnt), "%u/%u", (unsigned)cur, (unsigned)total);
    cw = u8g2_GetStrWidth(&m1_u8g2, cnt);
    u8g2_DrawStr(&m1_u8g2, (int)(128 - cw - 1), M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, cnt);

    /* Clip the title to the space left of the counter (with a small gap). */
    avail = 128 - (int)cw - 4;
    tbuf[0] = '\0';
    for (i = 0U; (title[i] != '\0') && (i < (uint16_t)(sizeof(tbuf) - 1U)); i++)
    {
        tbuf[i] = title[i];
        tbuf[i + 1U] = '\0';
        if ((int)u8g2_GetStrWidth(&m1_u8g2, tbuf) > avail) { tbuf[i] = '\0'; break; }
    }
    u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, tbuf);

    u8g2_DrawHLine(&m1_u8g2, 0, 13, 128);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
}

/* Page title for a Device Details page 'kind'. */
static const char *bt_detail_page_title(uint8_t kind)
{
    switch (kind)
    {
    case 0U: return "Device";
    case 1U: return "Advertisement";
    case 2U: return "Services";
    case 3U: return "Manufacturer";
    case 4U: return "Service Data";
    case 5U: return "Raw ADV";
    default: return "Scan Resp";
    }
}

/* Inverted bottom action bar: left "OK:<label>" (centre-button action) + right hint. */
static void bt_draw_actionbar(const char *ok_label, const char *left_hint)
{
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    /* Left: optional nav hint (e.g. page flip). Never a "Back" label - BACK is a
     * dedicated physical button, so it is never shown on screen. */
    if (left_hint != NULL)
    {
        u8g2_DrawStr(&m1_u8g2, 3, 61, left_hint);
    }

    /* Right: the centre-button (OK) action - shared target glyph then label,
     * matching the Sub-GHz bottom-bar convention. Right-aligned by label width. */
    if (ok_label != NULL)
    {
        uint16_t lw = u8g2_GetStrWidth(&m1_u8g2, ok_label);
        int gx = 128 - (10 + 4 + (int)lw) - 3;
        u8g2_DrawXBMP(&m1_u8g2, gx, 53, 10, 10, target_10x10);
        u8g2_DrawStr(&m1_u8g2, gx + 14, 61, ok_label);
    }
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/* Minimal scroll indicator: a single 1 px thumb on the right edge (no track), so it
 * shows position without clutter and never overlaps body text. Nothing when total<=1. */
static void bt_draw_scrollbar(uint16_t index, uint16_t total, uint16_t visible, uint8_t y_top, uint8_t y_bottom)
{
    uint8_t track_h;
    uint8_t thumb_h;
    uint8_t thumb_y;

    if ((total <= 1U) || (y_bottom <= y_top)) { return; }
    if (visible == 0U) { visible = 1U; }

    track_h = (uint8_t)(y_bottom - y_top);

    thumb_h = (uint8_t)(((uint16_t)track_h * visible) / total);
    if (thumb_h < 4U)        { thumb_h = 4U; }
    if (thumb_h > track_h)   { thumb_h = track_h; }

    thumb_y = (uint8_t)(y_top + ((uint16_t)(track_h - thumb_h) * index) / (total - 1U));
    u8g2_DrawVLine(&m1_u8g2, 126, thumb_y, thumb_h);
}

/* Blocking single-click reader; returns a BUTTON_*_KP_ID. */
static uint8_t bt_wait_button(void)
{
    S_M1_Buttons_Status bs;
    S_M1_Main_Q_t q;
    for (;;)
    {
        if ((xQueueReceive(main_q_hdl, &q, portMAX_DELAY) == pdTRUE) && (q.q_evt_type == Q_EVENT_KEYPAD))
        {
            memset(&bs, 0, sizeof(bs));
            if (xQueueReceive(button_events_q_hdl, &bs, 0) == pdTRUE)
            {
                if (bs.event[BUTTON_BACK_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_BACK_KP_ID;
                if (bs.event[BUTTON_OK_KP_ID]    == BUTTON_EVENT_CLICK) return BUTTON_OK_KP_ID;
                if (bs.event[BUTTON_UP_KP_ID]    == BUTTON_EVENT_CLICK) return BUTTON_UP_KP_ID;
                if (bs.event[BUTTON_DOWN_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_DOWN_KP_ID;
                if (bs.event[BUTTON_LEFT_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_LEFT_KP_ID;
                if (bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) return BUTTON_RIGHT_KP_ID;
            }
        }
    }
}

/* Non-blocking button read for streaming screens; returns a BUTTON_*_KP_ID or 0xFF. */
static uint8_t bt_poll_button(void)
{
    S_M1_Buttons_Status bs;
    S_M1_Main_Q_t q;
    if ((xQueueReceive(main_q_hdl, &q, 0) == pdTRUE) && (q.q_evt_type == Q_EVENT_KEYPAD))
    {
        memset(&bs, 0, sizeof(bs));
        if (xQueueReceive(button_events_q_hdl, &bs, 0) == pdTRUE)
        {
            if (bs.event[BUTTON_BACK_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_BACK_KP_ID;
            if (bs.event[BUTTON_OK_KP_ID]    == BUTTON_EVENT_CLICK) return BUTTON_OK_KP_ID;
            if (bs.event[BUTTON_UP_KP_ID]    == BUTTON_EVENT_CLICK) return BUTTON_UP_KP_ID;
            if (bs.event[BUTTON_DOWN_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_DOWN_KP_ID;
            if (bs.event[BUTTON_LEFT_KP_ID]  == BUTTON_EVENT_CLICK) return BUTTON_LEFT_KP_ID;
            if (bs.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) return BUTTON_RIGHT_KP_ID;
        }
    }
    return 0xFFU;
}

/* Streaming line reader for the async BLE screens (Signal Meter now; GATT later).
 * Pulls bytes from the ESP32 RX ring buffer and returns one completed line (terminator
 * stripped) or 0 on timeout, so the caller can interleave UART reads with button polling.
 * The accumulator persists across calls so a line split across two calls is not lost. */
static char     g_bt_line[160];
static uint16_t g_bt_line_len = 0U;
static uint8_t g_bt_line_drop = 0U;

static void bt_stream_reset(void) { g_bt_line_len = 0U; g_bt_line_drop = 0U; }

static uint16_t bt_read_line(char *out, uint16_t out_size, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    for (;;)
    {
        uint8_t ch;
        if (m1_ringbuffer_read(&esp32_rb_hdl, &ch, 1) == 1U)
        {
            if ((ch == '\n') || (ch == '\r'))
            {
                if (g_bt_line_drop) { bt_stream_reset(); continue; }
                if (g_bt_line_len > 0U)
                {
                    uint16_t n = g_bt_line_len;
                    if (n >= out_size) { n = (uint16_t)(out_size - 1U); }
                    memcpy(out, g_bt_line, n);
                    out[n] = '\0';
                    g_bt_line_len = 0U;
                    return n;
                }
                /* skip empty line / stray terminator */
            }
            else if (g_bt_line_drop) { continue; }
            else if (g_bt_line_len < (uint16_t)(sizeof(g_bt_line) - 1U))
            {
                g_bt_line[g_bt_line_len++] = (char)ch;
            }
            else
            {
                g_bt_line_len = 0U;
                g_bt_line_drop = 1U;
            }
        }
        else
        {
            if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) { return 0U; }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

/* Two-line message with a "Back" bar; returns when BACK is pressed. */
static void bt_message_screen(const char *title, const char *l1, const char *l2)
{
    uint8_t y;
    xQueueReset(button_events_q_hdl);
    m1_u8g2_firstpage();
    bt_draw_title(title);
    y = 14 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
    if (l1 != NULL) { u8g2_DrawStr(&m1_u8g2, 2, y, l1); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING); }
    if (l2 != NULL) { u8g2_DrawStr(&m1_u8g2, 2, y, l2); }
    /* No action bar: the only action is the dedicated physical BACK button. */
    m1_u8g2_nextpage();
    while (bt_wait_button() != BUTTON_BACK_KP_ID) { ; }
}

/* ---- shared selection state ---- */
static void ble_sel_clear(void)
{
    memset(&g_ble_sel, 0, sizeof(g_ble_sel));
}

static void ble_sel_set_from_item(uint16_t index)
{
    if (index >= g_ble_scan_count) { return; }
    g_ble_sel.valid = 1U;
    g_ble_sel.index = g_ble_scan_items[index].wire_index;
    g_ble_sel.rssi  = g_ble_scan_items[index].rssi;
    g_ble_sel.age_ms = g_ble_scan_items[index].age_ms;
    strncpy(g_ble_sel.mac, g_ble_scan_items[index].bssid, sizeof(g_ble_sel.mac) - 1U);
    g_ble_sel.mac[sizeof(g_ble_sel.mac) - 1U] = '\0';
    strncpy(g_ble_sel.name, g_ble_scan_items[index].name, sizeof(g_ble_sel.name) - 1U);
    g_ble_sel.name[sizeof(g_ble_sel.name) - 1U] = '\0';
    ble_get_display_name(index, g_ble_sel.disp, sizeof(g_ble_sel.disp));
}

/* Common 16-bit GATT/service UUID names (readable-first; unknown shown as raw). */
static const char *bt_uuid16_name(uint16_t uuid)
{
    switch (uuid)
    {
    case 0x1800: return "Generic Access";
    case 0x1801: return "Generic Attribute";
    case 0x1802: return "Immediate Alert";
    case 0x1803: return "Link Loss";
    case 0x1804: return "TX Power";
    case 0x180A: return "Device Information";
    case 0x180D: return "Heart Rate";
    case 0x180F: return "Battery Service";
    case 0x1810: return "Blood Pressure";
    case 0x1812: return "HID";
    case 0x181A: return "Environmental";
    case 0xFE95: return "Xiaomi MiBeacon";
    default:     return NULL;
    }
}

/* Common 16-bit characteristic UUID names (readable-first; unknown shown as raw). */
static const char *bt_uuid16_char_name(uint16_t uuid)
{
    switch (uuid)
    {
    case 0x2A00: return "Device Name";
    case 0x2A01: return "Appearance";
    case 0x2A04: return "Conn Params";
    case 0x2A19: return "Battery Level";
    case 0x2A23: return "System ID";
    case 0x2A24: return "Model Number";
    case 0x2A25: return "Serial Number";
    case 0x2A26: return "FW Revision";
    case 0x2A27: return "HW Revision";
    case 0x2A28: return "SW Revision";
    case 0x2A29: return "Manufacturer";
    case 0x2A37: return "Heart Rate";
    case 0x2A6E: return "Temperature";
    default:     return NULL;
    }
}

/* Common 16-bit descriptor UUID names (readable-first; unknown shown as raw). */
static const char *bt_uuid16_desc_name(uint16_t uuid)
{
    switch (uuid)
    {
    case 0x2900: return "Ext Properties";
    case 0x2901: return "User Desc";
    case 0x2902: return "CCCD";
    case 0x2903: return "Server Config";
    case 0x2904: return "Presentation";
    case 0x2905: return "Aggregate";
    default:     return NULL;
    }
}

/* Advertised name, reliable company ID, then address-distinguished unnamed. */
static void ble_get_display_name(uint16_t index, char *out, uint16_t sz)
{
    if (!out || !sz) return;
    if (index >= g_ble_scan_count) { out[0] = '\0'; return; }
    const ble_scan_item_t *it = &g_ble_scan_items[index];
    m1_ble_label(out, sz, it->name, it->bssid, it->company);
}

/* The UI font has no Latin-1 bullet. Reserve a space and draw the separator
 * explicitly, so unnamed address suffixes render on the actual M1 font. */
static uint16_t bt_label_width(const char *text)
{
    char ascii[BLE_SCAN_NAME_MAX];
    (void)snprintf(ascii, sizeof(ascii), "%s", text);
    for (unsigned i = 0; ascii[i]; ++i) if ((unsigned char)ascii[i] == 0xB7) ascii[i] = ' ';
    return u8g2_GetStrWidth(&m1_u8g2, ascii);
}
static void bt_draw_label(int x, int y, const char *text)
{
    char ascii[BLE_SCAN_NAME_MAX];
    (void)snprintf(ascii, sizeof(ascii), "%s", text);
    for (unsigned i = 0; ascii[i]; ++i) {
        if ((unsigned char)ascii[i] == 0xB7) {
            ascii[i] = '\0';
            int bx = x + u8g2_GetStrWidth(&m1_u8g2, ascii) + 2;
            ascii[i] = ' ';
            u8g2_DrawDisc(&m1_u8g2, bx, y - 3, 1, U8G2_DRAW_ALL);
        }
    }
    u8g2_DrawStr(&m1_u8g2, x, y, ascii);
}

/* Draw text clipped to max_px, appending ".." when truncated (show as much as possible). */
static void bt_draw_str_clipped(int x, int y, const char *text, int max_px)
{
    char buf[40];
    uint16_t n;

    buf[0] = '\0';
    for (n = 0U; (text[n] != '\0') && (n < (uint16_t)(sizeof(buf) - 1U)); n++)
    {
        buf[n] = text[n];
        buf[n + 1U] = '\0';
        if ((int)bt_label_width(buf) > max_px)
        {
            buf[(n >= 1U) ? (n - 1U) : 0U] = '\0';
            strncat(buf, "..", sizeof(buf) - strlen(buf) - 1U);
            break;
        }
    }
    bt_draw_label(x, y, buf);
}

/*
 * Acquire a fresh scan into the shared model. Draws the scanning screen, runs a timed
 * active scan (returns to the prompt immediately; discovery runs for the window), then
 * pulls the numbered "list" rows and parses them. Assumes the ESP32 path is prepared.
 * Returns SUCCESS if a list was obtained (g_ble_scan_count may be 0), ERROR on comms fail.
 */


/* Reuse the ESP32's existing discovery table without a new scan: fetch the numbered
 * 'list' rows and parse them into the shared table. Returns the device count (0 if the
 * table is empty / the ESP32 has no results). Fast (no 6 s scan). */
static uint16_t bt_scan_list_cached(void)
{
    if (bt_cmd_process("list", g_bt_scratch, (uint16_t)sizeof(g_bt_scratch), 5U) != SUCCESS)
    {
        g_ble_scan_count = 0U;
        return 0U;
    }
    g_ble_scan_count = bt_scan_parse_output(g_bt_scratch);
    return g_ble_scan_count;
}

/* ---- live device-picker order model (RSSI ranking + navigation freeze) ----
 *
 * The three live pickers (Scan / Signal / GATT) all render through this one model. While the user
 * is idle the visible order is recomputed strongest-RSSI-first on every 1.5 s refresh, because the
 * device they most likely want is the nearest one. The instant the user navigates (UP/DOWN) or
 * commits (CENTER), the order FREEZES for the life of that picker: rows no longer reorder under the
 * cursor, RSSI/names still update in place, and devices discovered later append to the end. Each
 * row carries the durable Bluetooth address, so a selection always resolves to a physical device by
 * address - never by a row index that a background RSSI update might have moved. */
typedef struct {
    char    mac[BSSID_STR_SIZE];
    char    disp[BLE_SCAN_NAME_MAX];   /* friendly label (real name, or assigned address-suffixed fallback) */
    int     rssi;
    uint8_t named;                     /* disp holds a real advertised/learned name                  */
    uint8_t present;                   /* seen in the most recent snapshot                           */
} bt_pick_row_t;

static bt_pick_row_t g_pick[BLE_SCAN_MAX_RESULTS];
static uint16_t      g_pick_n;

/* Current firmware scan-list id for a durable address, or -1 if it is no longer discoverable. */
static int bt_scan_index_of(const char *mac)
{
    uint16_t i;
    for (i = 0U; i < g_ble_scan_count; i++)
    {
        if (strcmp(g_ble_scan_items[i].bssid, mac) == 0) { return (int)i; }
    }
    return -1;
}

/* Row index in the picker model for a durable address, or -1 if not present. */
static int bt_pick_index_of(const char *mac)
{
    uint16_t i;
    for (i = 0U; i < g_pick_n; i++)
    {
        if (strcmp(g_pick[i].mac, mac) == 0) { return (int)i; }
    }
    return -1;
}

/* Merge the current scan snapshot (g_ble_scan_items) into the picker model. Updates RSSI and
 * upgrades an address-suffixed fallback label in place when a real name arrives. When !frozen, devices
 * that vanished are dropped and the list is re-sorted strongest-first; when frozen, existing rows
 * keep their positions and newly-seen devices are appended to the end. */
static void bt_pick_sync(uint8_t frozen)
{
    uint16_t i, w;

    for (i = 0U; i < g_pick_n; i++) { g_pick[i].present = 0U; }

    for (i = 0U; i < g_ble_scan_count; i++)
    {
        const ble_scan_item_t *it = &g_ble_scan_items[i];
        int pi = bt_pick_index_of(it->bssid);
        if (pi < 0)
        {
            if (g_pick_n >= BLE_SCAN_MAX_RESULTS) { continue; }
            pi = (int)g_pick_n++;
            memset(&g_pick[pi], 0, sizeof(g_pick[pi]));
            strncpy(g_pick[pi].mac, it->bssid, sizeof(g_pick[pi].mac) - 1U);
            if (it->name[0] != '\0')
            {
                (void)snprintf(g_pick[pi].disp, sizeof(g_pick[pi].disp), "%s", it->name);
                g_pick[pi].named = 1U;
            }
            else
            {
                ble_get_display_name(i, g_pick[pi].disp, sizeof(g_pick[pi].disp));
                g_pick[pi].named = 0U;
            }
        }
        else if (it->name[0] != '\0')
        {
            (void)snprintf(g_pick[pi].disp, sizeof(g_pick[pi].disp), "%s", it->name);  /* name improved in place */
            g_pick[pi].named = 1U;
        }
        g_pick[pi].rssi    = it->rssi;
        if (!g_pick[pi].named) ble_get_display_name(i, g_pick[pi].disp, sizeof(g_pick[pi].disp));
        g_pick[pi].present = it->age_ms < 5000U;
    }

    if (!frozen)
    {
        w = 0U;
        for (i = 0U; i < g_pick_n; i++) { if (bt_scan_index_of(g_pick[i].mac) >= 0) { if (w != i) { g_pick[w] = g_pick[i]; } w++; } }
        g_pick_n = w;
        for (i = 1U; i < g_pick_n; i++)   /* insertion sort by RSSI descending (n<=64) */
        {
            bt_pick_row_t key = g_pick[i];
            int j = (int)i - 1;
            while ((j >= 0) && (g_pick[j].rssi < key.rssi)) { g_pick[j + 1] = g_pick[j]; j--; }
            g_pick[j + 1] = key;
        }
    }
}

/* Shared BLE device picker used by Scan Devices, Signal Meter and GATT Explorer. Renders the
 * shared session table (friendly name + RSSI, thin scrollbar) under 'title', with CENTER labelled
 * 'center_label'. UP/DOWN browse; CENTER returns the firmware scan-list id of the highlighted
 * device (resolved by address); BACK returns -1. The list LIVE-UPDATES from background discovery
 * every ~1.5 s: strongest-first while idle, order-frozen once the user navigates (see the order
 * model above). Empty shows "Searching..." and keeps polling - discovery is continuous. */
static int bt_device_picker(const char *title, const char *center_label)
{
    uint16_t sel = 0U;
    uint16_t top = 0U;
    const uint8_t VIS = 3U;
    uint8_t  dirty = 1U;
    uint8_t  frozen = 0U;
    char     sel_mac[BSSID_STR_SIZE];
    TickType_t last_refresh;

    g_pick_n = 0U;
    sel_mac[0] = '\0';
    (void)bt_scan_list_cached();                 /* initial snapshot of the live table */
    bt_pick_sync(0U);                            /* initial strongest-first order      */
    last_refresh = xTaskGetTickCount();

    for (;;)
    {
        uint8_t btn;

        if (dirty)
        {
            m1_u8g2_firstpage();
            bt_draw_title(title);
            if (g_pick_n == 0U)
            {
                u8g2_DrawStr(&m1_u8g2, 2, 32, "Searching...");
            }
            else
            {
                uint16_t r;
                uint8_t  row_y;
                if (sel >= g_pick_n) { sel = (uint16_t)(g_pick_n - 1U); }
                if (sel < top)       { top = sel; }
                if (sel >= top + VIS){ top = (uint16_t)(sel - VIS + 1U); }
                row_y = 14 + M1_GUI_FONT_HEIGHT;
                for (r = 0U; (r < VIS) && ((top + r) < g_pick_n); r++)
                {
                    uint16_t idx = (uint16_t)(top + r);
                    char rb[8];
                    (void)snprintf(rb, sizeof(rb), g_pick[idx].present ? "%d" : "%d?", g_pick[idx].rssi);
                    if (idx == sel)
                    {
                        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                        u8g2_DrawBox(&m1_u8g2, 0, (int)(row_y - M1_GUI_FONT_HEIGHT + 1), 122, M1_GUI_FONT_HEIGHT + 1);
                        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
                    }
                    bt_draw_str_clipped(2, row_y, g_pick[idx].disp, 94);
                    m1_draw_text(&m1_u8g2, 98, row_y, 26, rb, TEXT_ALIGN_RIGHT);
                    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                    row_y = (uint8_t)(row_y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
                }
                bt_draw_scrollbar(sel, g_pick_n, VIS, 15, 51);
                bt_draw_actionbar(center_label, NULL);
            }
            m1_u8g2_nextpage();
            dirty = 0U;
        }

        btn = bt_poll_button();
        if (btn == BUTTON_BACK_KP_ID) { return -1; }
        if (btn == BUTTON_UP_KP_ID || btn == BUTTON_DOWN_KP_ID || btn == BUTTON_OK_KP_ID) frozen = 1U;
        if (g_pick_n > 0U)
        {
            if (btn == BUTTON_UP_KP_ID)        { frozen = 1U; sel = (sel == 0U) ? (uint16_t)(g_pick_n - 1U) : (uint16_t)(sel - 1U); dirty = 1U; }
            else if (btn == BUTTON_DOWN_KP_ID) { frozen = 1U; sel = (uint16_t)((sel + 1U) >= g_pick_n ? 0U : (sel + 1U)); dirty = 1U; }
            else if (btn == BUTTON_OK_KP_ID)
            {
                int si;
                frozen = 1U;                             /* CENTER safety: freeze before resolving */
                si = bt_scan_index_of(g_pick[sel].mac);  /* resolve the durable address to a live id */
                if (si < 0) { bt_message_screen(title, "Device gone", "Pick another"); dirty = 1U; }
                else        { return si; }
            }
        }

        /* Live refresh: re-snapshot, merge into the order model (frozen or strongest-first), and
         * keep the highlight on the same physical device by its address. */
        if ((xTaskGetTickCount() - last_refresh) >= pdMS_TO_TICKS(1500))
        {
            last_refresh = xTaskGetTickCount();
            if ((g_pick_n > 0U) && (sel < g_pick_n))
            {
                strncpy(sel_mac, g_pick[sel].mac, sizeof(sel_mac) - 1U);
                sel_mac[sizeof(sel_mac) - 1U] = '\0';
            }
            (void)bt_scan_list_cached();
            bt_pick_sync(frozen);
            if (frozen && sel_mac[0] != '\0')
            {
                int pi = bt_pick_index_of(sel_mac);
                if (pi >= 0) { sel = (uint16_t)pi; }
            }
            dirty = 1U;
        }

        osDelay(40);
    }
}

/* Open the shared picker for Signal Meter; auto-scans first when the table is empty.
 * Returns 1 and sets the shared selection if a device was chosen, else 0. */
static uint8_t bt_signal_pick_target(void)
{
    int idx = bt_device_picker("Select Device", "Select");
    if (idx < 0) { return 0U; }
    ble_sel_set_from_item((uint16_t)idx);
    return 1U;
}



/*============================================================================*/
/*
 * ble command function
 */
/*============================================================================*/
uint8_t bt_cmd_process(const char *cmd_option, char *resp_buf, uint16_t resp_buf_size, uint32_t timeout_sec)
{
    char cmd_buf[168];   /* must fit the longest command ("writenr <handle> <64 bytes hex>") + CRLF */
    uint16_t rx_len;
    uint32_t timeout_ms;

    if ((cmd_option == NULL) || (cmd_option[0] == '\0') ||
            (resp_buf == NULL) || (resp_buf_size < 2U))
    {
        return ERROR;
    }

    g_bt_mode_switch_fail = 0U;

    (void)snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", cmd_option);

    resp_buf[0] = '\0';
    m1_esp32_reset_buffer();

    if (!esp32_uart_write((const uint8_t *)cmd_buf, (uint16_t)strlen(cmd_buf), 100U))
    {
        return ERROR;
    }

    timeout_ms = timeout_sec * 1000U;
    rx_len = esp32_uart_read_complete_prompt(resp_buf, resp_buf_size, timeout_ms, ">> ");
    if (rx_len == 0U)
    {
        return ERROR;
    }

    return SUCCESS;
} // uint8_t bt_cmd_process(const char *cmd_option, char *resp_buf, uint16_t resp_buf_size, uint32_t timeout_sec)



/*============================================================================*/
/*
 * This function validates the scan list.
 */
/*============================================================================*/
static uint16_t bt_scan_parse_output(const char *resp_text)
{
	char line_buf[128];
	size_t line_len = 0U;
	uint16_t out_count = 0U;
	const char *p;

	if (resp_text == NULL)
	{
		return 0U;
	}

	memset(g_ble_scan_items, 0, sizeof(g_ble_scan_items));

	for (p = resp_text; ; p++)
	{
		char ch = *p;
		if ((ch != '\0') && (ch != '\r') && (ch != '\n'))
		{
			if (line_len < (sizeof(line_buf) - 1U))
			{
				line_buf[line_len++] = ch;
			}
			continue;
		}

		line_buf[line_len] = '\0';
		if (line_len > 0U)
		{
			char *line = line_buf;
			char *start;
			char *mac_start;
			char *mac_end;
			char *rssi_pos;
			char *name_pos;
			char mac[BSSID_STR_SIZE] = {0};
			char name[BLE_SCAN_NAME_MAX] = {0};
			int rssi = 0;

			while ((*line != '\0') && isspace((unsigned char)*line))
			{
				line++;
			}

			start = strstr(line, "[");
			if ((start != NULL) && isdigit((unsigned char)start[1]))
			{
				mac_start = strchr(start, ']');
				if ((mac_start != NULL) && (mac_start[1] == ' '))
				{
					mac_start += 2;
					mac_end = strstr(mac_start, " RSSI=");
					rssi_pos = strstr(mac_start, "RSSI=");
					name_pos = strstr(mac_start, "NAME=");

					if ((mac_end != NULL) && (rssi_pos != NULL) && (name_pos != NULL) && (out_count < BLE_SCAN_MAX_RESULTS))
					{
						size_t mac_len = (size_t)(mac_end - mac_start);
						if (mac_len >= BSSID_STR_SIZE)
						{
							mac_len = BSSID_STR_SIZE - 1U;
						}

						memcpy(mac, mac_start, mac_len);
						mac[mac_len] = '\0';

						rssi = atoi(rssi_pos + 5);

						strncpy(name, name_pos + 5, BLE_SCAN_NAME_MAX - 1U);
						name[BLE_SCAN_NAME_MAX - 1U] = '\0';

						strncpy(g_ble_scan_items[out_count].bssid, mac, BSSID_STR_SIZE - 1U);
                        g_ble_scan_items[out_count].rssi = rssi;
                        g_ble_scan_items[out_count].wire_index = (uint16_t)strtoul(start + 1, NULL, 10);
                        char *mp = strstr(mac_start, " MFG=");
                        char *ap = strstr(mac_start, " AGE=");
                        g_ble_scan_items[out_count].company = mp ? (unsigned)strtoul(mp + 5, NULL, 10) : 65535U;
                        g_ble_scan_items[out_count].age_ms = ap ? (uint32_t)strtoul(ap + 5, NULL, 10) : 0;

						strncpy(g_ble_scan_items[out_count].name, name, BLE_SCAN_NAME_MAX - 1U);
						out_count++;
					}
				}
			}
		}
		line_len = 0U;
		if (ch == '\0')
		{
			break;
		}
	}
	return out_count;
}



/*============================================================================*/
/*
 * Display BLE device detail information from list <id> response
 * UP/DOWN buttons to scroll through different information pages
 */
/*============================================================================*/
static void bt_parse_detail(const char *resp, bt_detail_t *d)
{
    const char *p = resp;
    char line[128];

    while (*p != '\0')
    {
        size_t n = 0U;
        const char *t;
        while ((*p != '\0') && (*p != '\n') && (*p != '\r'))
        {
            if (n < (sizeof(line) - 1U)) { line[n++] = *p; }
            p++;
        }
        while ((*p == '\n') || (*p == '\r')) { p++; }
        line[n] = '\0';

        t = line;
        while ((*t == ' ') || (*t == '\t')) { t++; }

        if (strncmp(t, "Address:", 8) == 0)   /* saved-record header line */
        {
            const char *s = t + 8; while (*s == ' ') { s++; }
            strncpy(d->addr, s, sizeof(d->addr) - 1U); d->addr[sizeof(d->addr) - 1U] = '\0';
            d->has_addr = 1U;
        }
        else if (strncmp(t, "- Flags:", 8) == 0)
        {
            unsigned v;
            if ((sscanf(t + 8, " 0x%x", &v) == 1) || (sscanf(t + 8, " %x", &v) == 1)) { d->has_flags = 1U; d->flags = v; }
        }
        else if (strncmp(t, "- Shortened Local Name:", 23) == 0)
        {
            const char *s = t + 23; while (*s == ' ') { s++; }
            strncpy(d->name, s, sizeof(d->name) - 1U); d->name[sizeof(d->name) - 1U] = '\0';
            d->has_name = 1U; d->name_complete = 0U;
        }
        else if (strncmp(t, "- Complete Local Name:", 22) == 0)
        {
            const char *s = t + 22; while (*s == ' ') { s++; }
            strncpy(d->name, s, sizeof(d->name) - 1U); d->name[sizeof(d->name) - 1U] = '\0';
            d->has_name = 1U; d->name_complete = 1U;
        }
        else if (strncmp(t, "- Service UUID16 (0x", 20) == 0)
        {
            const char *x = strstr(t, "(0x");
            unsigned v;
            if ((x != NULL) && (sscanf(x + 1, "0x%x", &v) == 1) && (d->uuid16_count < 8U))
            {
                d->uuid16[d->uuid16_count++] = (uint16_t)v;
            }
        }
        else if (strncmp(t, "- Service UUID128:", 18) == 0)
        {
            d->has_uuid128 = 1U;
        }
        else if (strncmp(t, "- TX Power:", 11) == 0)
        {
            int v; if (sscanf(t + 11, " %d", &v) == 1) { d->has_tx = 1U; d->tx_power = v; }
        }
        else if (strncmp(t, "- Service Data UUID16:", 22) == 0)
        {
            unsigned u; const char *dp;
            if (sscanf(t + 22, " 0x%x", &u) == 1) { d->has_svc_data = 1U; d->svc_uuid = u; }
            dp = strstr(t, "Data: ");
            if (dp != NULL) { strncpy(d->svc_hex, dp + 6, sizeof(d->svc_hex) - 1U); d->svc_hex[sizeof(d->svc_hex) - 1U] = '\0'; }
        }
        else if (strncmp(t, "- Manufacturer Data:", 20) == 0)
        {
            const char *cp = strstr(t, "company_id=0x");
            const char *dp = strstr(t, "data=");
            unsigned u;
            d->has_mfg = 1U;
            if ((cp != NULL) && (sscanf(cp + 13, "%x", &u) == 1)) { d->mfg_company = u; }
            if (dp != NULL) { strncpy(d->mfg_hex, dp + 5, sizeof(d->mfg_hex) - 1U); d->mfg_hex[sizeof(d->mfg_hex) - 1U] = '\0'; }
        }
        else if (strncmp(t, "RSSI:", 5) == 0)
        {
            int v; if (sscanf(t + 5, " %d", &v) == 1) { d->has_rssi = 1U; d->rssi = v; }
        }
        else if (strncmp(t, "Raw SCAN_RSP data", 17) == 0)
        {
            const char *c = strchr(t, ':');
            if (c != NULL) { c++; while (*c == ' ') { c++; } strncpy(d->rsp_hex, c, sizeof(d->rsp_hex) - 1U); d->rsp_hex[sizeof(d->rsp_hex) - 1U] = '\0'; }
            d->has_rsp = 1U;
        }
        else if (strncmp(t, "Raw ADV data", 12) == 0)
        {
            const char *c = strchr(t, ':');
            if (c != NULL) { c++; while (*c == ' ') { c++; } strncpy(d->raw_hex, c, sizeof(d->raw_hex) - 1U); d->raw_hex[sizeof(d->raw_hex) - 1U] = '\0'; }
            d->has_raw = 1U;
        }
    }
} // static void bt_parse_detail(const char *resp, bt_detail_t *d)



/*============================================================================*/
/*
 * Device Details / Advertisement Inspector.
 * Fetches "list <index>" for the selected device, parses the current-firmware ADV
 * fields, and pages through only the fields that are present. No scan logic here.
 */
/*============================================================================*/
/* Write the current device's advertisement to 0:/BLE/<name>.ble (name via the virtual
 * keyboard). The file is a re-parseable text record: an "Address:" header line followed by
 * the raw "list <id>" detail block, so bt_parse_detail can read it back for viewing. */
static void bt_detail_save(const char *mac, const char *name, const char *raw)
{
    char defname[40];
    char newname[40];
    char path[80];
    char hdr[64];
    FIL  file;
    uint16_t i;
    uint16_t j;

    /* Default file name: sanitised device name, else the MAC without colons. */
    j = 0U;
    if ((name != NULL) && (name[0] != '\0'))
    {
        for (i = 0U; (name[i] != '\0') && (j < (uint16_t)(sizeof(defname) - 1U)); i++)
        {
            char c = name[i];
            uint8_t ok = (uint8_t)(((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')));
            defname[j++] = ok ? c : '_';
        }
    }
    else if (mac != NULL)
    {
        for (i = 0U; (mac[i] != '\0') && (j < (uint16_t)(sizeof(defname) - 1U)); i++)
        {
            if (mac[i] != ':') { defname[j++] = mac[i]; }
        }
    }
    defname[j] = '\0';
    if (defname[0] == '\0') { (void)snprintf(defname, sizeof(defname), "device"); }

    /* Virtual keyboard: returns non-zero on confirm, 0 on cancel. */
    if (!m1_vkb_get_filename("Save BLE device", defname, newname, 1U))
    {
        return;
    }

    if (!m1_fb_check_existence(BLE_SAVE_DIR)) { (void)m1_fb_make_dir(BLE_SAVE_DIR); }

    (void)snprintf(path, sizeof(path), "%s/%s%s", BLE_SAVE_DIR, newname, BLE_SAVE_EXT);
    if (m1_fb_open_new_file(&file, path) != 0)   /* 0 == success */
    {
        bt_message_screen("Save", "Save failed", "Check SD card");
        return;
    }
    (void)snprintf(hdr, sizeof(hdr), "Address: %s\r\n", (mac != NULL) ? mac : "");
    (void)m1_fb_write_to_file(&file, hdr, (uint16_t)strlen(hdr));
    (void)m1_fb_write_to_file(&file, raw, (uint16_t)strlen(raw));
    (void)m1_fb_close_file(&file);

    /* Brief confirmation, then return to the inspector -- same shared
     * "Saved" screen and the same 900ms dismissal every other BLE save
     * action here already used; no new blocking delay introduced. */
    m1_draw_file_saved_screen();
    osDelay(900);
}

/*
 * Device Details / Advertisement Inspector renderer. Pages through the present fields of a
 * parsed record. Identity (address / name / RSSI) is passed in so this serves both the live
 * inspector (from Scan) and the offline Saved viewer. When raw_for_save != NULL the CENTER
 * button saves the record (live inspector only); LEFT/RIGHT page, BACK returns.
 */
static void bt_detail_render(const bt_detail_t *d, const char *mac, const char *name, int rssi, const char *raw_for_save)
{
    uint8_t pages[7];
    uint8_t page_count = 0U;
    uint8_t cur = 0U;
    uint8_t exit_view = 0U;

    pages[page_count++] = 0U;                                       /* Identity - always */
    if (d->has_flags || d->has_tx)          { pages[page_count++] = 1U; }
    if (d->uuid16_count || d->has_uuid128)  { pages[page_count++] = 2U; }
    if (d->has_mfg)                         { pages[page_count++] = 3U; }
    if (d->has_svc_data)                    { pages[page_count++] = 4U; }
    if (d->has_raw)                         { pages[page_count++] = 5U; }
    if (d->has_rsp)                         { pages[page_count++] = 6U; }

    while (!exit_view)
    {
        uint8_t kind = pages[cur];
        uint8_t y;
        char line[28];
        uint8_t btn;

        m1_u8g2_firstpage();
        bt_draw_title_paged(bt_detail_page_title(kind), (uint8_t)(cur + 1U), page_count);
        y = 14 + M1_GUI_FONT_HEIGHT;

        switch (kind)
        {
        case 0U:
            (void)snprintf(line, sizeof(line), "%.17s", ((mac != NULL) && (mac[0] != '\0')) ? mac : "(no addr)");
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            (void)snprintf(line, sizeof(line), "%.20s", ((name != NULL) && (name[0] != '\0')) ? name : (d->has_name ? d->name : "(no name)"));
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            (void)snprintf(line, sizeof(line), "RSSI: %d dBm", d->has_rssi ? d->rssi : rssi);
            u8g2_DrawStr(&m1_u8g2, 2, y, line);
            break;
        case 1U:
            if (d->has_flags) { (void)snprintf(line, sizeof(line), "Flags: 0x%02X", d->flags & 0xFFu); u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING); }
            if (d->has_tx)    { (void)snprintf(line, sizeof(line), "TX Power: %d dBm", d->tx_power); u8g2_DrawStr(&m1_u8g2, 2, y, line); }
            break;
        case 2U:
            {
                uint8_t i;
                for (i = 0U; (i < d->uuid16_count) && (i < 3U); i++)
                {
                    const char *nm = bt_uuid16_name(d->uuid16[i]);
                    if (nm != NULL) { (void)snprintf(line, sizeof(line), "%.20s", nm); }
                    else            { (void)snprintf(line, sizeof(line), "0x%04X", d->uuid16[i]); }
                    u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
                }
                if (d->has_uuid128) { u8g2_DrawStr(&m1_u8g2, 2, y, "128-bit UUID present"); }
            }
            break;
        case 3U:
            (void)snprintf(line, sizeof(line), "ID: 0x%04X", d->mfg_company & 0xFFFFu);
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            (void)snprintf(line, sizeof(line), "%.20s", d->mfg_hex);
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            if (strlen(d->mfg_hex) > 20U) { u8g2_DrawStr(&m1_u8g2, 2, y, d->mfg_hex + 20); }
            break;
        case 4U:
            (void)snprintf(line, sizeof(line), "UUID: 0x%04X", d->svc_uuid & 0xFFFFu);
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            (void)snprintf(line, sizeof(line), "%.20s", d->svc_hex);
            u8g2_DrawStr(&m1_u8g2, 2, y, line);
            break;
        case 5U:
            (void)snprintf(line, sizeof(line), "%.20s", d->raw_hex);
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            if (strlen(d->raw_hex) > 20U) { (void)snprintf(line, sizeof(line), "%.20s", d->raw_hex + 20); u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING); }
            if (strlen(d->raw_hex) > 40U) { u8g2_DrawStr(&m1_u8g2, 2, y, d->raw_hex + 40); }
            break;
        default:
            (void)snprintf(line, sizeof(line), "%.20s", d->rsp_hex);
            u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
            if (strlen(d->rsp_hex) > 20U) { (void)snprintf(line, sizeof(line), "%.20s", d->rsp_hex + 20); u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING); }
            if (strlen(d->rsp_hex) > 40U) { u8g2_DrawStr(&m1_u8g2, 2, y, d->rsp_hex + 40); }
            break;
        }

        /* CENTER = Save (live inspector only). The X/Y counter in the header conveys paging;
         * LEFT/RIGHT flip, BACK returns. */
        if (raw_for_save != NULL) { bt_draw_actionbar("Save", NULL); }
        m1_u8g2_nextpage();

        btn = bt_wait_button();
        switch (btn)
        {
        case BUTTON_BACK_KP_ID:
            exit_view = 1U;
            break;
        case BUTTON_LEFT_KP_ID:
            if (page_count > 1U) { cur = (cur == 0U) ? (uint8_t)(page_count - 1U) : (uint8_t)(cur - 1U); }
            break;
        case BUTTON_RIGHT_KP_ID:
            if (page_count > 1U) { cur = (uint8_t)(((cur + 1U) >= page_count) ? 0U : (cur + 1U)); }
            break;
        case BUTTON_OK_KP_ID:
            if (raw_for_save != NULL) { bt_detail_save(mac, name, raw_for_save); }
            break;
        default:
            break;
        }
    }
}

/* Live Device Details: fetch "list <index>" and render it, with CENTER = Save. */
static void bt_detail_view(uint16_t index)
{
    bt_detail_t d;
    char cmd[24];

    (void)snprintf(cmd, sizeof(cmd), "list %u", (unsigned)index);
    if (bt_cmd_process(cmd, g_bt_scratch, (uint16_t)sizeof(g_bt_scratch), 5U) != SUCCESS)
    {
        bt_message_screen("Device Details", "Fetch failed", "Run Scan again");
        return;
    }
    if (strstr(g_bt_scratch, "Invalid BLE ID") != NULL)
    {
        bt_message_screen("Device Details", "Not in scan list", "Run Scan again");
        return;
    }

    memset(&d, 0, sizeof(d));
    bt_parse_detail(g_bt_scratch, &d);
    bt_detail_render(&d, g_ble_sel.mac, g_ble_sel.name, g_ble_sel.rssi, g_bt_scratch);
}

/* Offline viewer for a saved .ble record: read the file, parse it, and render it in the
 * same inspector (no Save action, no ESP32). Identity comes from the saved record. */
static void bt_saved_view(const char *dir, const char *filename)
{
    bt_detail_t d;
    char path[96];
    const char *sep = "";
    FIL  file;
    UINT n;

    if ((dir == NULL) || (filename == NULL)) { return; }
    if ((dir[0] != '\0') && (dir[strlen(dir) - 1U] != '/')) { sep = "/"; }
    (void)snprintf(path, sizeof(path), "%s%s%s", dir, sep, filename);

    if (m1_fb_open_file(&file, path) != 0)
    {
        bt_message_screen("Saved", "Open failed", filename);
        return;
    }
    FRESULT read_result = m1_fb_read_file(&file, g_bt_scratch, sizeof(g_bt_scratch) - 1U, &n);
    (void)m1_fb_close_file(&file);
    if (read_result != FR_OK)
    {
        bt_message_screen("Saved", "Read failed", filename);
        return;
    }
    g_bt_scratch[n] = '\0';

    memset(&d, 0, sizeof(d));
    bt_parse_detail(g_bt_scratch, &d);
    bt_detail_render(&d, d.has_addr ? d.addr : "", d.has_name ? d.name : filename, d.rssi, NULL);
}

/*
 * Saved: browse saved BLE records under 0:/BLE and open one in the inspector. Offline (no
 * ESP32). Reuses the shared file browser; BACK exits to the Bluetooth menu.
 */
void bluetooth_saved(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    const S_M1_file_info *f_info;
    BaseType_t ret;
    uint8_t exit_saved = 0U;

    if (!m1_fb_check_existence(BLE_SAVE_DIR)) { (void)m1_fb_make_dir(BLE_SAVE_DIR); }
    m1_fb_set_start_dir(BLE_SAVE_DIR);
    m1_fb_init(&m1_u8g2);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    m1_u8g2_nextpage();
    (void)m1_fb_display(NULL);

    while (!exit_saved)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
        {
            memset(&this_button_status, 0, sizeof(this_button_status));
            (void)xQueueReceive(button_events_q_hdl, &this_button_status, 0);

            if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                exit_saved = 1U;
            }
            else
            {
                f_info = m1_fb_display(&this_button_status);
                if ((f_info != NULL) && (f_info->status == FB_OK) && f_info->file_is_selected)
                {
                    bt_saved_view(f_info->dir_name, f_info->file_name);
                    xQueueReset(button_events_q_hdl);
                    (void)m1_fb_display(NULL);   /* restore the listing after viewing */
                }
            }
        }
    }

    m1_fb_deinit();
} // void bluetooth_saved(void)




/* Live Signal Meter screen. state: 0=searching without a reading, 1=live, 2=last seen. Favours a large,
 * immediately-readable value over field clutter (no action bar - BACK is physical). */
static void bt_signal_draw(uint8_t state, int avg, const char *catlabel)
{
    char buf[24];
    uint16_t w;

    m1_u8g2_firstpage();
    bt_draw_title("Signal Meter");

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    (void)snprintf(buf, sizeof(buf), "%.20s", g_ble_sel.disp[0] ? g_ble_sel.disp : (g_ble_sel.name[0] ? g_ble_sel.name : g_ble_sel.mac));
    w = bt_label_width(buf);
    bt_draw_label((int)(64 - (w / 2U)), 24, buf);

    if (state != 0U)
    {
        int pct;
        if (state == 2U) catlabel = "Searching / Last seen";

        u8g2_SetFont(&m1_u8g2, u8g2_font_10x20_mr);
        (void)snprintf(buf, sizeof(buf), "%d dBm", avg);
        w = u8g2_GetStrWidth(&m1_u8g2, buf);
        u8g2_DrawStr(&m1_u8g2, (int)(64 - (w / 2U)), 46, buf);

        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        w = u8g2_GetStrWidth(&m1_u8g2, catlabel);
        u8g2_DrawStr(&m1_u8g2, (int)(64 - (w / 2U)), 57, catlabel);

        /* strength bar: map avg -100..-40 dBm to 0..100 px */
        pct = ((avg + 100) * 100) / 60;
        if (pct < 0)   { pct = 0; }
        if (pct > 100) { pct = 100; }
        u8g2_DrawFrame(&m1_u8g2, 14, 60, 100, 4);
        if (pct > 0) { u8g2_DrawBox(&m1_u8g2, 14, 60, (uint16_t)pct, 4); }
    }
    else
    {
        const char *msg = "Searching...";
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        w = u8g2_GetStrWidth(&m1_u8g2, msg);
        u8g2_DrawStr(&m1_u8g2, (int)(64 - (w / 2U)), 44, msg);
    }

    m1_u8g2_nextpage();
}

/* Live meter for the current selection. Assumes a prepared path + valid selection; rebinds
 * the index to the selection's Bluetooth address, starts "signal <index>" (the ESP32 then
 * tracks that address, not the index), streams updates, and on BACK sends "signal stop"
 * and returns to the caller (the Signal Meter target screen). */
static void bt_signal_run(void)
{
    char cmd[32], line[160];
    int avg = g_ble_sel.rssi;
    uint8_t state = m1_ble_rssi_valid(avg) ? (g_ble_sel.age_ms < 5000U ? 1U : 2U) : 0U;
    TickType_t last_seen = xTaskGetTickCount();
    if (!g_ble_sel.valid) return;
    /* Draw the selected observation before waiting for any ESP32 response. */
    bt_signal_draw(state, avg, m1_ble_category(avg));
    bt_stream_reset();
    (void)snprintf(cmd, sizeof(cmd), "signal %s", g_ble_sel.mac);
    if (bt_cmd_process(cmd, g_bt_scratch, sizeof(g_bt_scratch), 3U) != SUCCESS ||
        strstr(g_bt_scratch, "[BLE:SIG:START]") == NULL) {
        (void)bt_cmd_process("signal stop", g_bt_scratch, sizeof(g_bt_scratch), 2U);
        bt_session_resume_scan();
        bt_message_screen("Signal Meter", "Tracking unavailable", "Check ESP32 test build");
        return;
    }
    /* The synchronous reply may already contain a reading. Never discard it. */
    if (m1_ble_signal_record(g_bt_scratch, &avg)) { state = 1U; last_seen = xTaskGetTickCount(); }
    bt_signal_draw(state, avg, m1_ble_category(avg));
    for (;;) {
        if (bt_poll_button() == BUTTON_BACK_KP_ID) break;
        uint8_t dirty = 0U;
        if (bt_read_line(line, sizeof(line), 120U)) {
            if (m1_ble_signal_record(line, &avg)) {
                state = 1U; last_seen = xTaskGetTickCount(); dirty = 1U;
            } else if (strstr(line, "[BLE:SIG:LOST]")) {
                if (state) state = 2U;
                dirty = 1U;
            }
        }
        if (state == 1U && xTaskGetTickCount() - last_seen >= pdMS_TO_TICKS(5000)) {
            state = 2U; dirty = 1U;
        }
        if (dirty) bt_signal_draw(state, avg, m1_ble_category(avg));
    }
    (void)bt_cmd_process("signal stop", g_bt_scratch, sizeof(g_bt_scratch), 2U);
    bt_session_resume_scan();
    bt_stream_reset();
    xQueueReset(main_q_hdl);
}

/*
 * Signal Meter (self-contained): choose/confirm a target and watch its live RSSI without
 * ever leaving the Signal Meter function. Reuses the shared BLE discovery engine (scan
 * table, parser, picker) and the shared selection - the user never needs to visit Scan
 * Devices. Cached discovery results are reused (fast 'list', no 6 s rescan) when present;
 * an explicit Rescan lives in the picker. BACK from the live meter returns here; BACK
 * here exits to the Bluetooth menu.
 */
void bluetooth_signal_meter(void)
{
    uint8_t exit_sm = 0U;
    uint8_t in_meter;

    if (bt_session_ensure() != SUCCESS)
    {
        bt_message_screen("Signal Meter", "BLE not ready", "Try again");
        return;
    }

    /* Always start on the shared live device list. BACK from the meter returns here; BACK
     * from the list exits to the Bluetooth menu. bt_signal_run pauses/resumes discovery. */
    in_meter = 0U;
    while (!exit_sm)
    {
        if (in_meter)                     { bt_signal_run(); in_meter = 0U; }
        else if (bt_signal_pick_target()) { in_meter = 1U; }
        else                              { exit_sm = 1U; }
    }
    /* No deinit: the Bluetooth session owns discovery. */
} // void bluetooth_signal_meter(void)
/*============================================================================*/
/* GATT Explorer: connect, discover, read/write and subscriptions. */
/*============================================================================*/

#define GATT_MAX_SVC   12U
#define GATT_MAX_CHR   32U
#define GATT_MAX_DSC   32U
#define GATT_PROP_READ    0x02U
#define GATT_PROP_WNR     0x04U
#define GATT_PROP_WRITE   0x08U
#define GATT_PROP_NOTIFY  0x10U
#define GATT_PROP_INDIC   0x20U

typedef struct { uint8_t is128; uint16_t uuid16; uint16_t sh; uint16_t eh; } gatt_svc_t;
typedef struct { uint8_t svc; uint8_t is128; uint16_t uuid16; uint16_t val; uint8_t props; } gatt_chr_t;
typedef struct { uint8_t chr; uint8_t is128; uint16_t uuid16; uint16_t handle; } gatt_dsc_t;

static gatt_svc_t g_gatt_svc[GATT_MAX_SVC];  static uint16_t g_gatt_svc_n;
static gatt_chr_t g_gatt_chr[GATT_MAX_CHR];  static uint16_t g_gatt_chr_n;
static gatt_dsc_t g_gatt_dsc[GATT_MAX_DSC];  static uint16_t g_gatt_dsc_n;
static uint8_t    g_gatt_map[GATT_MAX_CHR];  static uint16_t g_gatt_map_n;   /* filtered view */

enum { ACT_DETAILS = 0, ACT_READ, ACT_WRITE, ACT_WRITENR, ACT_NOTIFY, ACT_INDICATE, ACT_DESC };
static uint8_t g_gatt_act[7]; static uint16_t g_gatt_act_n;

typedef void (*bt_row_fmt_t)(uint16_t idx, char *out, uint16_t out_sz);

/* Core's connect-only reply follows its 30-second HAL connect. Discovery is
 * a separate command so a large GATT tree cannot extend that contract. */
#define GATT_CONNECT_COMMAND_MS 35000U
#define GATT_DISCOVERY_COMMAND_MS 370000U
#define GATT_CCCD_COMMAND_MS 16000U
static uint8_t g_gatt_connected;
static uint8_t g_gatt_remote_disconnected;
/* Set when discovery returned a usable but partial tree (Core reported
 * "discovery incomplete"); drives the "GATT Truncated" list header. */
static uint8_t g_gatt_truncated;

/* A cancelled or incomplete command can still produce a late Core prompt.
 * Reset Core before releasing UART ownership so that prompt cannot complete
 * a later command or leave a live link outside the UI. */
static void bt_gatt_abort_session(void)
{
    esp32_disable();
    m1_esp32_deinit();
    bt_restore_esp32_cmd_path(&g_ble_ses_ctx);
    g_ble_ses_active = 0U;
    g_gatt_connected = 0U;
    ble_sel_clear();
    g_ble_scan_count = 0U;
    g_gatt_svc_n = g_gatt_chr_n = g_gatt_dsc_n = g_gatt_map_n = 0U;
    bt_stream_reset();
}

static uint8_t bt_gatt_disconnected(const char *reply)
{
    const char *event = strstr(reply, "[BLE:CONN] disconnected");
    if (event == NULL && strstr(reply, "Not connected") == NULL) { return 0U; }
    g_gatt_connected = 0U;
    g_gatt_remote_disconnected = 1U;
    if (event != NULL)
    {
        const char *field = strstr(event, "reason=");
        if (field != NULL)
        {
            char *end;
            unsigned long reason = strtoul(field + 7, &end, 10);
            if (end != field + 7 && reason <= 0xFFFFUL)
            {
                char detail[24];
                (void)snprintf(detail, sizeof(detail), "Reason 0x%04lX", reason);
                bt_message_screen("GATT Explorer", "Disconnected", detail);
                return 1U;
            }
        }
    }
    bt_message_screen("GATT Explorer", "Disconnected", NULL);
    return 1U;
}

/* GATT-only cancellable reader. A partial response is never success. */
static uint8_t bt_gatt_command(const char *command, uint32_t timeout_ms)
{
    char wire[168];
    uint16_t n = 0U;
    TickType_t start;
    (void)snprintf(wire, sizeof(wire), "%s\r\n", command);
    g_bt_scratch[0] = '\0';
    bt_stream_reset();
    m1_esp32_reset_buffer();
    if (!esp32_uart_write((const uint8_t *)wire, (uint16_t)strlen(wire), 100U))
    {
        bt_gatt_abort_session();
        bt_message_screen("GATT Explorer", "Transport failed", "Session closed");
        return ERROR;
    }
    start = xTaskGetTickCount();
    for (;;)
    {
        uint8_t ch;
        if (bt_poll_button() == BUTTON_BACK_KP_ID)
        {
            bt_gatt_abort_session();
            return ERROR;
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms))
        {
            bt_gatt_abort_session();
            bt_message_screen("GATT Explorer", "Operation timed out", "Session closed");
            return ERROR;
        }
        if (m1_ringbuffer_read(&esp32_rb_hdl, &ch, 1U) == 1U)
        {
            if (n >= sizeof(g_bt_scratch) - 1U)
            {
                bt_gatt_abort_session();
                bt_message_screen("GATT Explorer", "Reply too large", "Session closed");
                return ERROR;
            }
            g_bt_scratch[n++] = (char)ch;
            g_bt_scratch[n] = '\0';
            if (n >= 3U && memcmp(g_bt_scratch + n - 3U, ">> ", 3U) == 0) { return SUCCESS; }
        }
        else { vTaskDelay(pdMS_TO_TICKS(2)); }
    }
}

static uint8_t bt_gatt_wait_button(void)
{
    char line[160];
    while (g_gatt_connected)
    {
        uint8_t button = bt_poll_button();
        if (button != 0xFFU) { return button; }
        if (bt_read_line(line, sizeof(line), 20U) && bt_gatt_disconnected(line)) { break; }
    }
    return BUTTON_BACK_KP_ID;
}

/* Generic vertical picker: rows drawn by row_fmt(idx). Returns the selected index, or -1
 * on BACK. center_label NULL => view-only (CENTER still returns idx; callers may ignore). */
static int bt_list_pick(const char *title, uint16_t count, const char *center_label, bt_row_fmt_t row_fmt)
{
    uint16_t sel = 0U;
    uint16_t top = 0U;
    const uint8_t VIS = 3U;

    if (count == 0U) { bt_message_screen(title, "(none)", NULL); return -1; }

    for (;;)
    {
        uint8_t btn;
        uint16_t r;
        uint8_t row_y;

        if (sel < top)        { top = sel; }
        if (sel >= top + VIS) { top = (uint16_t)(sel - VIS + 1U); }

        m1_u8g2_firstpage();
        bt_draw_title(title);
        row_y = 14 + M1_GUI_FONT_HEIGHT;
        for (r = 0U; (r < VIS) && ((top + r) < count); r++)
        {
            uint16_t idx = (uint16_t)(top + r);
            char l[24];
            row_fmt(idx, l, sizeof(l));
            if (idx == sel)
            {
                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                u8g2_DrawBox(&m1_u8g2, 0, (int)(row_y - M1_GUI_FONT_HEIGHT + 1), 122, M1_GUI_FONT_HEIGHT + 1);
                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            }
            bt_draw_str_clipped(2, row_y, l, 118);   /* clip long service/char names with ".." */
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            row_y = (uint8_t)(row_y + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING);
        }
        bt_draw_scrollbar(sel, count, VIS, 15, 51);
        bt_draw_actionbar(center_label, NULL);
        m1_u8g2_nextpage();

        btn = bt_gatt_wait_button();
        switch (btn)
        {
        case BUTTON_BACK_KP_ID: return -1;
        case BUTTON_UP_KP_ID:   sel = (sel == 0U) ? (uint16_t)(count - 1U) : (uint16_t)(sel - 1U); break;
        case BUTTON_DOWN_KP_ID: sel = (uint16_t)((sel + 1U) >= count ? 0U : (sel + 1U)); break;
        case BUTTON_OK_KP_ID:   return (int)sel;
        default: break;
        }
    }
}

/* ---- row formatters ---- */
static void gatt_svc_row(uint16_t idx, char *out, uint16_t sz)
{
    const gatt_svc_t *s = &g_gatt_svc[idx];
    if (s->is128) { (void)snprintf(out, sz, "128-bit service"); }
    else
    {
        const char *nm = bt_uuid16_name(s->uuid16);
        if (nm != NULL) { (void)snprintf(out, sz, "%.20s", nm); }
        else            { (void)snprintf(out, sz, "0x%04X", s->uuid16); }
    }
}

static void gatt_chr_row(uint16_t idx, char *out, uint16_t sz)
{
    const gatt_chr_t *c = &g_gatt_chr[g_gatt_map[idx]];
    char pr[6];
    uint8_t k = 0U;
    if (c->props & GATT_PROP_READ)                       { pr[k++] = 'R'; }
    if (c->props & (GATT_PROP_WRITE | GATT_PROP_WNR))    { pr[k++] = 'W'; }
    if (c->props & GATT_PROP_NOTIFY)                     { pr[k++] = 'N'; }
    if (c->props & GATT_PROP_INDIC)                      { pr[k++] = 'I'; }
    pr[k] = '\0';
    if (c->is128) { (void)snprintf(out, sz, "UUID128 %s", pr); }
    else
    {
        const char *nm = bt_uuid16_char_name(c->uuid16);
        if (nm != NULL) { (void)snprintf(out, sz, "%s %s", nm, pr); }
        else            { (void)snprintf(out, sz, "0x%04X %s", c->uuid16, pr); }
    }
}

static void gatt_dsc_row(uint16_t idx, char *out, uint16_t sz)
{
    const gatt_dsc_t *dd = &g_gatt_dsc[g_gatt_map[idx]];
    if (dd->is128) { (void)snprintf(out, sz, "UUID128 h=%u", (unsigned)dd->handle); return; }
    const char *nm = bt_uuid16_desc_name(dd->uuid16);
    if (nm != NULL) { (void)snprintf(out, sz, "%s h=%u", nm, (unsigned)dd->handle); }
    else            { (void)snprintf(out, sz, "0x%04X h=%u", dd->uuid16, (unsigned)dd->handle); }
}

static void gatt_act_row(uint16_t idx, char *out, uint16_t sz)
{
    switch (g_gatt_act[idx])
    {
    case ACT_DETAILS:  (void)snprintf(out, sz, "Details"); break;
    case ACT_READ:     (void)snprintf(out, sz, "Read"); break;
    case ACT_WRITE:    (void)snprintf(out, sz, "Write"); break;
    case ACT_WRITENR:  (void)snprintf(out, sz, "Write No Rsp"); break;
    case ACT_NOTIFY:   (void)snprintf(out, sz, "Notify"); break;
    case ACT_INDICATE: (void)snprintf(out, sz, "Indicate"); break;
    default:           (void)snprintf(out, sz, "Descriptors"); break;
    }
}

/* Parse a "services" reply into the bounded service/characteristic/descriptor tables. */
static void bt_gatt_parse_services(const char *resp)
{
    const char *p = resp;
    char line[160];
    int cur_svc = -1;
    int cur_chr = -1;

    g_gatt_svc_n = 0U; g_gatt_chr_n = 0U; g_gatt_dsc_n = 0U;

    while (*p != '\0')
    {
        size_t n = 0U;
        const char *t;
        while ((*p != '\0') && (*p != '\n') && (*p != '\r')) { if (n < (sizeof(line) - 1U)) { line[n++] = *p; } p++; }
        while ((*p == '\n') || (*p == '\r')) { p++; }
        line[n] = '\0';
        t = line; while ((*t == ' ') || (*t == '\t')) { t++; }

        if (strncmp(t, "[SVC ", 5) == 0)
        {
            if (g_gatt_svc_n < GATT_MAX_SVC)
            {
                gatt_svc_t *sv = &g_gatt_svc[g_gatt_svc_n];
                unsigned u = 0U, sh = 0U, eh = 0U;
                sv->is128 = (uint8_t)((strstr(t, "UUID128") != NULL) ? 1U : 0U);
                if (sv->is128) { (void)sscanf(t, "[SVC %*d] UUID128 handles %u-%u", &sh, &eh); sv->uuid16 = 0U; }
                else           { (void)sscanf(t, "[SVC %*d] UUID16 0x%x handles %u-%u", &u, &sh, &eh); sv->uuid16 = (uint16_t)u; }
                sv->sh = (uint16_t)sh; sv->eh = (uint16_t)eh;
                cur_svc = (int)g_gatt_svc_n; g_gatt_svc_n++;
            }
            else { cur_svc = -1; }
            cur_chr = -1;
        }
        else if ((strncmp(t, "[CHR]", 5) == 0) && (cur_svc >= 0))
        {
            if (g_gatt_chr_n < GATT_MAX_CHR)
            {
                gatt_chr_t *c = &g_gatt_chr[g_gatt_chr_n];
                unsigned u = 0U, val = 0U, props = 0U;
                c->svc = (uint8_t)cur_svc;
                c->is128 = (uint8_t)((strstr(t, "UUID128") != NULL) ? 1U : 0U);
                if (c->is128) { (void)sscanf(t, "[CHR] UUID128 val=%u props=0x%x", &val, &props); c->uuid16 = 0U; }
                else          { (void)sscanf(t, "[CHR] UUID16 0x%x val=%u props=0x%x", &u, &val, &props); c->uuid16 = (uint16_t)u; }
                c->val = (uint16_t)val; c->props = (uint8_t)props;
                cur_chr = (int)g_gatt_chr_n; g_gatt_chr_n++;
            }
            else { cur_chr = -1; }
        }
        else if ((strncmp(t, "[DSC]", 5) == 0) && (cur_chr >= 0))
        {
            if (g_gatt_dsc_n < GATT_MAX_DSC)
            {
                gatt_dsc_t *dd = &g_gatt_dsc[g_gatt_dsc_n];
                unsigned u = 0U, h = 0U;
                int fields;
                uint8_t is128 = (uint8_t)(strncmp(t, "[DSC] UUID128 ", 14) == 0);
                if (is128) { fields = sscanf(t, "[DSC] UUID128 handle=%u", &h); }
                else { fields = sscanf(t, "[DSC] UUID16 0x%x handle=%u", &u, &h); }
                if (fields == (is128 ? 1 : 2) && h > 0U && h <= UINT16_MAX && u <= UINT16_MAX)
                {
                    dd->chr = (uint8_t)cur_chr;
                    dd->is128 = is128;
                    dd->uuid16 = (uint16_t)u;
                    dd->handle = (uint16_t)h;
                    g_gatt_dsc_n++;
                }
            }
        }
    }
}

/* Connect + wait for discovery. Returns 1 connected, 0 on failure/timeout/cancel. */
static uint8_t bt_gatt_connect(uint16_t index)
{
    char cmd[24];
    g_gatt_connected = 0U;
    m1_u8g2_firstpage();
    bt_draw_title("GATT Explorer");
    u8g2_DrawStr(&m1_u8g2, 2, 28, "Connecting / discovery");
    u8g2_DrawStr(&m1_u8g2, 2, 44, "BACK cancels");
    m1_u8g2_nextpage();
    xQueueReset(button_events_q_hdl);
    xQueueReset(main_q_hdl);
    (void)snprintf(cmd, sizeof(cmd), "connect-only %u", (unsigned)index);
    if (bt_gatt_command(cmd, GATT_CONNECT_COMMAND_MS) != SUCCESS) { return 0U; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL || bt_gatt_disconnected(g_bt_scratch))
    {
        if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL) { bt_message_screen("GATT Explorer", "Connect failed", NULL); }
        return 0U;
    }
    if (strstr(g_bt_scratch, "Radio busy") != NULL) { bt_message_screen("Bluetooth", "Wi-Fi is using the", "radio. Stop Wi-Fi."); return 0U; }
    if (strstr(g_bt_scratch, "Invalid BLE ID") != NULL) { bt_message_screen("GATT Explorer", "Not in scan list", "Run Rescan"); return 0U; }
    if (strstr(g_bt_scratch, "[BLE:CONN] connected") == NULL)
    {
        bt_message_screen("GATT Explorer", "Connect not confirmed", NULL);
        return 0U;
    }
    g_gatt_connected = 1U;
    return 1U;
}

/* Read a characteristic value by value handle and show it (hex, plus decimal if 1 byte). */
static void bt_gatt_read(uint16_t val)
{
    char cmd[24];
    char data[80];
    int  len = -1;
    uint8_t got = 0U;
    const char *r;

    data[0] = '\0';
    (void)snprintf(cmd, sizeof(cmd), "read %u", (unsigned)val);
    if (bt_gatt_command(cmd, 6000U) != SUCCESS || bt_gatt_disconnected(g_bt_scratch)) { return; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL) { bt_message_screen("Read", "Read failed", NULL); return; }

    r = strstr(g_bt_scratch, "[BLE:READ]");
    if (r != NULL)
    {
        const char *lp = strstr(r, "len="); const char *dp = strstr(r, "data=");
        if (lp != NULL) { len = atoi(lp + 4); }
        if (dp != NULL) { strncpy(data, dp + 5, sizeof(data) - 1U); data[sizeof(data) - 1U] = '\0'; }
        got = 1U;
    }

    /* strip a trailing prompt / spaces from the hex string */
    { char *pr = strstr(data, ">>"); if (pr != NULL) { *pr = '\0'; } }
    { size_t dl = strlen(data); while (dl > 0U && (data[dl-1U] == ' ' || data[dl-1U] == '\t')) { data[--dl] = '\0'; } }

    m1_u8g2_firstpage();
    bt_draw_title("Value");
    if (got)
    {
        char l[28];
        (void)snprintf(l, sizeof(l), "Len: %d", len);
        u8g2_DrawStr(&m1_u8g2, 2, 24, l);
        (void)snprintf(l, sizeof(l), "%.20s", data);
        u8g2_DrawStr(&m1_u8g2, 2, 38, l);
        if (strlen(data) > 20U) { u8g2_DrawStr(&m1_u8g2, 2, 50, data + 20); }
    }
    else { u8g2_DrawStr(&m1_u8g2, 2, 30, "(no value)"); }
    m1_u8g2_nextpage();
    while (bt_gatt_wait_button() != BUTTON_BACK_KP_ID) { ; }
}

/* Subscribe to notifications (or indications) and show incoming values live until BACK. */
static void bt_gatt_notify(uint16_t val, uint8_t indicate)
{
    char cmd[24];
    char line[160];
    char last[64];
    unsigned long count = 0UL;

    last[0] = '\0';
    (void)snprintf(cmd, sizeof(cmd), "%s %u", indicate ? "indicate" : "subscribe", (unsigned)val);
    if (bt_gatt_command(cmd, GATT_CCCD_COMMAND_MS) != SUCCESS || bt_gatt_disconnected(g_bt_scratch)) { return; }
    if (strstr(g_bt_scratch, "no CCCD") != NULL)      { bt_message_screen("Notify", "Not subscribable", NULL); return; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL || strstr(g_bt_scratch, "[BLE:SUB] ok") == NULL)
    {
        /* An unknown reply may hide a successful subscription. */
        if (strstr(g_bt_scratch, "[BLE:ERR]") == NULL) { bt_gatt_abort_session(); }
        bt_message_screen("Subscribe", "Subscribe failed", NULL);
        return;
    }

    bt_stream_reset();
    xQueueReset(button_events_q_hdl);
    xQueueReset(main_q_hdl);
    while (g_gatt_connected)
    {
        uint16_t nrd;
        char hdr[28];

        if (bt_poll_button() == BUTTON_BACK_KP_ID) { break; }
        nrd = bt_read_line(line, sizeof(line), 150U);
        if (nrd > 0U)
        {
            if (bt_gatt_disconnected(line)) { return; }
            char *d = strstr(line, "[BLE:NTF]");
            if (d == NULL) { d = strstr(line, "[BLE:IND]"); }
            if (d != NULL)
            {
                char *dp = strstr(d, "data=");
                if (dp != NULL) { strncpy(last, dp + 5, sizeof(last) - 1U); last[sizeof(last) - 1U] = '\0'; count++; }
            }
        }

        m1_u8g2_firstpage();
        bt_draw_title(indicate ? "Indications" : "Notifications");
        (void)snprintf(hdr, sizeof(hdr), "h=%u  #%lu", (unsigned)val, count);
        u8g2_DrawStr(&m1_u8g2, 2, 26, hdr);
        u8g2_DrawStr(&m1_u8g2, 2, 42, (last[0] != '\0') ? last : "(waiting)");
        m1_u8g2_nextpage();
    }

    (void)snprintf(cmd, sizeof(cmd), "unsubscribe %u", (unsigned)val);
    if (bt_gatt_command(cmd, GATT_CCCD_COMMAND_MS) != SUCCESS || bt_gatt_disconnected(g_bt_scratch)) { return; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL || strstr(g_bt_scratch, "[BLE:UNSUB] ok") == NULL)
    {
        uint8_t failed = (uint8_t)(strstr(g_bt_scratch, "[BLE:ERR]") != NULL);
        bt_gatt_abort_session();
        bt_message_screen("Unsubscribe", failed ? "Unsubscribe failed" : "Not confirmed", "Session closed");
    }
    xQueueReset(main_q_hdl);
}

/* Friendly one-line label for a characteristic (standard name, else raw UUID / "128-bit char"). */
static void gatt_chr_label(const gatt_chr_t *c, char *out, uint16_t sz)
{
    if (c->is128) { (void)snprintf(out, sz, "128-bit char"); return; }
    {
        const char *nm = bt_uuid16_char_name(c->uuid16);
        if (nm != NULL) { (void)snprintf(out, sz, "%s", nm); }
        else            { (void)snprintf(out, sz, "0x%04X", c->uuid16); }
    }
}

/* Technical characteristic detail: horizontal LEFT/RIGHT paging with an upper-right X/Y counter
 * (no scrollbar). Pages: Characteristic (name/UUID/value handle), Properties, and Descriptors
 * (only when the characteristic has any). BACK exits. */
static void bt_gatt_char_details(uint8_t chr_idx)
{
    const gatt_chr_t *c = &g_gatt_chr[chr_idx];
    uint8_t ndsc = 0U;
    uint16_t i;
    uint8_t total;
    uint8_t page = 0U;
    char nm[28];

    for (i = 0U; i < g_gatt_dsc_n; i++) { if (g_gatt_dsc[i].chr == chr_idx) { ndsc++; } }
    total = (uint8_t)((ndsc > 0U) ? 3U : 2U);
    gatt_chr_label(c, nm, sizeof(nm));

    for (;;)
    {
        uint8_t btn;
        uint8_t y = 26U;
        char l[36];

        m1_u8g2_firstpage();
        bt_draw_title_paged((page == 0U) ? "Characteristic" : ((page == 1U) ? "Properties" : "Descriptors"),
                            (uint8_t)(page + 1U), total);
        if (page == 0U)
        {
            bt_draw_str_clipped(2, y, nm, 124); y = (uint8_t)(y + 12U);
            if (c->is128) { (void)snprintf(l, sizeof(l), "UUID: 128-bit"); }
            else          { (void)snprintf(l, sizeof(l), "UUID: 0x%04X", c->uuid16); }
            u8g2_DrawStr(&m1_u8g2, 2, y, l); y = (uint8_t)(y + 12U);
            (void)snprintf(l, sizeof(l), "Handle: %u", (unsigned)c->val);
            u8g2_DrawStr(&m1_u8g2, 2, y, l);
        }
        else if (page == 1U)
        {
            uint8_t any = 0U;
            if (c->props & GATT_PROP_READ)   { u8g2_DrawStr(&m1_u8g2, 2, y, "Read");         y = (uint8_t)(y + 11U); any = 1U; }
            if (c->props & GATT_PROP_WRITE)  { u8g2_DrawStr(&m1_u8g2, 2, y, "Write");        y = (uint8_t)(y + 11U); any = 1U; }
            if (c->props & GATT_PROP_WNR)    { u8g2_DrawStr(&m1_u8g2, 2, y, "Write No Rsp"); y = (uint8_t)(y + 11U); any = 1U; }
            if (c->props & GATT_PROP_NOTIFY) { u8g2_DrawStr(&m1_u8g2, 2, y, "Notify");       y = (uint8_t)(y + 11U); any = 1U; }
            if (c->props & GATT_PROP_INDIC)  { u8g2_DrawStr(&m1_u8g2, 2, y, "Indicate");     y = (uint8_t)(y + 11U); any = 1U; }
            if (!any) { u8g2_DrawStr(&m1_u8g2, 2, y, "(none)"); }
        }
        else
        {
            uint16_t k;
            uint8_t shown = 0U;
            (void)snprintf(l, sizeof(l), "Count: %u", (unsigned)ndsc);
            u8g2_DrawStr(&m1_u8g2, 2, y, l); y = (uint8_t)(y + 12U);
            for (k = 0U; (k < g_gatt_dsc_n) && (shown < 2U); k++)
            {
                const char *dn;
                if (g_gatt_dsc[k].chr != chr_idx) { continue; }
                dn = g_gatt_dsc[k].is128 ? NULL : bt_uuid16_desc_name(g_gatt_dsc[k].uuid16);
                if (g_gatt_dsc[k].is128) { (void)snprintf(l, sizeof(l), "UUID128 h=%u", (unsigned)g_gatt_dsc[k].handle); }
                else if (dn != NULL) { (void)snprintf(l, sizeof(l), "%s h=%u", dn, (unsigned)g_gatt_dsc[k].handle); }
                else { (void)snprintf(l, sizeof(l), "0x%04X h=%u", g_gatt_dsc[k].uuid16, (unsigned)g_gatt_dsc[k].handle); }
                u8g2_DrawStr(&m1_u8g2, 2, y, l); y = (uint8_t)(y + 11U); shown++;
            }
        }
        m1_u8g2_nextpage();

        btn = bt_gatt_wait_button();
        if (btn == BUTTON_BACK_KP_ID)       { return; }
        else if (btn == BUTTON_LEFT_KP_ID)  { page = (page == 0U) ? (uint8_t)(total - 1U) : (uint8_t)(page - 1U); }
        else if (btn == BUTTON_RIGHT_KP_ID) { page = (uint8_t)((page + 1U) % total); }
    }
}

/* Stage + confirm a GATT write. Gets a variable-length hex value from the shared data editor,
 * validates it as whole bytes, shows a confirmation screen (target / value / mode), and only on
 * an explicit CENTER=Send stages ('write'/'writenr') then transmits ('confirm') on the ESP32.
 * with_response selects Write (acknowledged) vs Write Without Response. BACK cancels harmlessly. */
static void bt_gatt_write(const gatt_chr_t *c, uint8_t with_response)
{
    char hexin[132];
    char clean[132];
    char cmd[160];
    char nm[28];
    uint16_t i;
    uint16_t n = 0U;

    hexin[0] = '\0';
    if (!m1_vkbs_get_hex_bytes(with_response ? "Write hex" : "Write hex (no rsp)", hexin, 64U)) { return; }  /* cancelled */

    /* Sanitise to hex nibbles only; require non-empty, even (whole bytes), <= 64 bytes. */
    for (i = 0U; (hexin[i] != '\0') && (n < (uint16_t)(sizeof(clean) - 1U)); i++)
    {
        char ch = hexin[i];
        if (((ch >= '0') && (ch <= '9')) || ((ch >= 'a') && (ch <= 'f')) || ((ch >= 'A') && (ch <= 'F'))) { clean[n++] = ch; }
    }
    clean[n] = '\0';
    if ((n == 0U) || ((n & 1U) != 0U) || (n > 128U))
    {
        bt_message_screen("Write", "Enter whole bytes", "(even hex, <=64B)");
        return;
    }

    gatt_chr_label(c, nm, sizeof(nm));

    /* Confirmation screen (deliberate second step). CENTER = Send, BACK = Cancel (nothing staged). */
    for (;;)
    {
        uint8_t btn;
        char l[36];
        char v[22];

        m1_u8g2_firstpage();
        bt_draw_title("Confirm Write");
        bt_draw_str_clipped(2, 26, nm, 124);
        (void)snprintf(l, sizeof(l), "%s  %u byte%s", with_response ? "With Rsp" : "No Rsp",
                       (unsigned)(n / 2U), ((n / 2U) == 1U) ? "" : "s");
        u8g2_DrawStr(&m1_u8g2, 2, 37, l);
        strncpy(v, clean, 20); v[20] = '\0';
        u8g2_DrawStr(&m1_u8g2, 2, 48, v);
        bt_draw_actionbar("Send", NULL);
        m1_u8g2_nextpage();

        btn = bt_gatt_wait_button();
        if (btn == BUTTON_BACK_KP_ID) { return; }
        if (btn == BUTTON_OK_KP_ID)   { break; }
    }

    /* Do not confirm a stage unless Core acknowledged it. */
    (void)snprintf(cmd, sizeof(cmd), "%s %u %s", with_response ? "write" : "writenr", (unsigned)c->val, clean);
    if (bt_gatt_command(cmd, 3000U) != SUCCESS || bt_gatt_disconnected(g_bt_scratch)) { return; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL || strstr(g_bt_scratch, "PENDING WRITE (") == NULL)
    {
        bt_gatt_abort_session();
        bt_message_screen("Write", "Write rejected", "Session closed");
        return;
    }

    if (bt_gatt_command("confirm", 6000U) != SUCCESS || bt_gatt_disconnected(g_bt_scratch)) { return; }
    if (strstr(g_bt_scratch, "[BLE:ERR]") != NULL) { bt_message_screen("Write", "Write failed", NULL); }
    else if (!with_response && strstr(g_bt_scratch, "[BLE:WRITE] sent (no response requested)") != NULL)
                                                        { bt_message_screen("Write", "Sent", "(no response)"); }
    else if (with_response && strstr(g_bt_scratch, "[BLE:WRITE] ok") != NULL)
                                                        { bt_message_screen("Write", "Write OK", NULL); }
    else                                                { bt_message_screen("Write", "No confirmation", NULL); }
}

/* Characteristic action menu, built only from the properties this characteristic advertises.
 * Details is always offered; Read/Write/Write-No-Rsp/Notify/Indicate/Descriptors appear only when
 * the characteristic actually supports them. */
static void bt_gatt_char_actions(uint8_t chr_idx)
{
    const gatt_chr_t *c = &g_gatt_chr[chr_idx];
    uint8_t has_dsc = 0U;
    uint16_t i;

    for (i = 0U; i < g_gatt_dsc_n; i++) { if (g_gatt_dsc[i].chr == chr_idx) { has_dsc = 1U; break; } }

    while (g_gatt_connected)
    {
        int sel;
        g_gatt_act_n = 0U;
        g_gatt_act[g_gatt_act_n++] = ACT_DETAILS;                                     /* always available */
        if (c->props & GATT_PROP_READ)   { g_gatt_act[g_gatt_act_n++] = ACT_READ; }
        if (c->props & GATT_PROP_WRITE)  { g_gatt_act[g_gatt_act_n++] = ACT_WRITE; }
        if (c->props & GATT_PROP_WNR)    { g_gatt_act[g_gatt_act_n++] = ACT_WRITENR; }
        if (c->props & GATT_PROP_NOTIFY) { g_gatt_act[g_gatt_act_n++] = ACT_NOTIFY; }
        if (c->props & GATT_PROP_INDIC)  { g_gatt_act[g_gatt_act_n++] = ACT_INDICATE; }
        if (has_dsc)                     { g_gatt_act[g_gatt_act_n++] = ACT_DESC; }

        sel = bt_list_pick("Actions", g_gatt_act_n, "Select", gatt_act_row);
        if (sel < 0) { return; }

        switch (g_gatt_act[sel])
        {
        case ACT_DETAILS:  bt_gatt_char_details(chr_idx); break;
        case ACT_READ:     bt_gatt_read(c->val); break;
        case ACT_WRITE:    bt_gatt_write(c, 1U); break;
        case ACT_WRITENR:  bt_gatt_write(c, 0U); break;
        case ACT_NOTIFY:   bt_gatt_notify(c->val, 0U); break;
        case ACT_INDICATE: bt_gatt_notify(c->val, 1U); break;
        default:
        {
            uint16_t k;
            g_gatt_map_n = 0U;
            for (k = 0U; k < g_gatt_dsc_n; k++) { if ((g_gatt_dsc[k].chr == chr_idx) && (g_gatt_map_n < GATT_MAX_DSC)) { g_gatt_map[g_gatt_map_n++] = (uint8_t)k; } }
            while (bt_list_pick("Descriptors", g_gatt_map_n, NULL, gatt_dsc_row) >= 0) { ; }
            break;
        }
        }
    }
}

/* Characteristic list for one service. */
static void bt_gatt_chars_list(uint8_t svc_idx)
{
    while (g_gatt_connected)
    {
        uint16_t k;
        int sel;
        g_gatt_map_n = 0U;
        for (k = 0U; k < g_gatt_chr_n; k++) { if ((g_gatt_chr[k].svc == svc_idx) && (g_gatt_map_n < GATT_MAX_CHR)) { g_gatt_map[g_gatt_map_n++] = (uint8_t)k; } }
        sel = bt_list_pick("Characteristics", g_gatt_map_n, "Select", gatt_chr_row);
        if (sel < 0) { return; }
        bt_gatt_char_actions(g_gatt_map[sel]);
    }
}

/* Service list. */
static void bt_gatt_services_list(void)
{
    const char *title = g_gatt_truncated ? "GATT Truncated" : "GATT Services";
    while (g_gatt_connected)
    {
        int sel = bt_list_pick(title, g_gatt_svc_n, "Select", gatt_svc_row);
        if (sel < 0) { return; }
        bt_gatt_chars_list((uint8_t)sel);
    }
}

/* One GATT session: connect, discover, explore, disconnect. */
static void bt_gatt_session(uint16_t index)
{
    g_gatt_remote_disconnected = 0U;
    if (bt_gatt_connect(index))
    {
        if (bt_gatt_command("services", GATT_DISCOVERY_COMMAND_MS) == SUCCESS &&
            !bt_gatt_disconnected(g_bt_scratch))
        {
            /* A discovery reply can carry a usable partial service tree AND a
             * trailing "[BLE:ERR] discovery incomplete" when the peripheral
             * exposes more than Core's bounded tree can hold (hardware-observed:
             * 8 svc / 13 chr / 10 dsc returned alongside the incomplete error).
             * Parse first, then decide: an "incomplete" error WITH a non-empty
             * tree is a TRUNCATED success -- open the list, clearly marked -- not
             * a total failure. Any other [BLE:ERR], or an empty tree, is a
             * genuine discovery failure and still shows "Discovery failed". */
            uint8_t has_err    = (uint8_t)(strstr(g_bt_scratch, "[BLE:ERR]") != NULL);
            uint8_t incomplete = (uint8_t)(strstr(g_bt_scratch, "discovery incomplete") != NULL);

            bt_gatt_parse_services(g_bt_scratch);

            if (has_err && !(incomplete && g_gatt_svc_n > 0U))
            {
                /* Genuine failure: a non-"incomplete" error (e.g. "discovery
                 * output truncated", where the reply text itself was cut and is
                 * unreliable) or an empty tree. Drop any partial parse so no
                 * stale/unreliable service data is left for the rest of the UI. */
                g_gatt_svc_n = 0U; g_gatt_chr_n = 0U; g_gatt_dsc_n = 0U;
                bt_message_screen("GATT Explorer", "Discovery failed", NULL);
            }
            else if (g_gatt_svc_n == 0U)
            {
                bt_message_screen("GATT Explorer", "No services", NULL);
            }
            else
            {
                g_gatt_truncated = (uint8_t)(has_err && incomplete);
                bt_gatt_services_list();
            }
        }
    }

    /* A command abort already retired the session. All other paths, including
     * failed connect and peer disconnect, converge on explicit cleanup. */
    g_gatt_connected = 0U;
    if (g_ble_ses_active)
    {
        uint8_t disconnected = g_gatt_remote_disconnected;
        if (disconnected || bt_gatt_command("disconnect", 3000U) == SUCCESS)
        {
            if (!disconnected && strstr(g_bt_scratch, "[BLE:CONN] disconnected") == NULL &&
                strstr(g_bt_scratch, "Not connected") == NULL)
            {
                bt_gatt_abort_session();
                bt_message_screen("GATT Explorer", "Disconnect unconfirmed", "Session closed");
            }
            else if (bt_gatt_command("resume", 5000U) == SUCCESS &&
                     strstr(g_bt_scratch, "[BLE:SCAN:LIVE]") == NULL)
            {
                bt_gatt_abort_session();
                bt_message_screen("GATT Explorer", "Scan resume failed", "Session closed");
            }
        }
    }
    xQueueReset(main_q_hdl);
}

/*
 * GATT Explorer (self-contained, like Signal Meter): always opens the device list, connects
 * to the chosen device (CENTER Connect), then browses services/characteristics/actions.
 * BACK from the service list disconnects and returns to the device list; BACK there exits.
 */
void bluetooth_gatt_explorer(void)
{
    uint8_t exit_ge = 0U;

    if (bt_session_ensure() != SUCCESS)
    {
        bt_message_screen("GATT Explorer", "BLE not ready", "Try again");
        return;
    }

    while (!exit_ge)
    {
        int idx;
        if (bt_session_ensure() != SUCCESS) { break; }
        idx = bt_device_picker("Select Device", "Connect");
        if (idx < 0) { exit_ge = 1U; }
        else { ble_sel_set_from_item((uint16_t)idx); bt_gatt_session(g_ble_sel.index); }
    }
    /* Session owns discovery; bt_gatt_session resumed the scan after disconnect. */
} // void bluetooth_gatt_explorer(void)
/* Device Details is no longer a top-level menu entry. The inspector (bt_detail_view)
 * is reached by pressing CENTER on a scanned device inside Scan Devices; the shared
 * selection (g_ble_sel) is populated there before the inspector opens. */
