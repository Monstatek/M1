/* See COPYING.txt for license details. */

/*
*
* m1_wifi.h
*
* Library for M1 Wifi
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_wifi.h"
#include "m1_esp32_hal.h"
#include "m1_esp_uart_transport.h"
#include "esp_at_list.h"
#include "m1_usb_cdc_msc.h"
#include "usbd_cdc_if.h"
#include "m1_sdcard.h"
#include "m1_sdcard_man.h"
#include "m1_file_util.h"
#include "m1_file_browser.h"
#include "m1_sdcard_provision.h"
#include "m1_storage.h"
#include "m1_virtual_kb.h"
#include "m1_save_filename.h"
#include "ctrl_api.h"
#include "res_string.h"
#include "m1_wifi_session_cache.h"
#include "m1_wifi_client_id.h"

/*************************** D E F I N E S ************************************/

#define MAX_SCAN_CMD_TIMEOUT_MS     10000
#define ESP32UART_BUFFER_SIZE       1024*4 // byte
#define SEC_TO_MILLISEC(x) (1000*(x))

#define M1_LOGDB_TAG                "Wifi"

#define MAX_BEACON_SSIDS            11  /* Must match ESP32 MAX_BEACON_SSIDS */

#define M1_WIFI_AP_SCANNING_TIME    30 // seconds
#define M1_WIFI_SCAN_RETRY_MAX      3
/* Deauth Client device-picker live-RSSI refresh interval (Part 1). A full
 * refresh round is a blocking "scan -s"+"list -s" UART round trip (same
 * calls wifi_scan_devices() already makes, just without re-sending
 * "select -a" -- the AP selection persists across repeated scan -s calls).
 *
 * FIX (hardware report: "cannot scroll through devices"): this was
 * previously 1000ms. The ESP32's station-scan handler holds a fixed
 * duration_ms=5000 sniff window per call -- a hard ~5s floor with the
 * current ESP32 firmware, not a timeout ceiling -- and this call is
 * synchronous, so button input is only ever read *between* scans, never
 * during one (see wifi_dc_pick_device()'s xQueueReceive(...,
 * pdMS_TO_TICKS(150)) above its refresh check). With a 1000ms gate against
 * a ~5000ms floor, the elapsed-check was satisfied again the instant each
 * scan returned, chaining scans back-to-back with only that ~150ms
 * xQueueReceive gap between them for a keypress to land in -- on real
 * hardware that reads as the device list simply not scrolling, since the
 * odds of a click landing in a ~150ms window every ~5s are low.
 *
 * This is set well above the ~5s scan floor so a real several-second idle
 * window opens up after each refresh, during which this loop's normal
 * 150ms polling is fully responsive. This is a deliberate trade-off
 * (RSSI/identity refresh every ~8s instead of every ~1s) given the ESP32
 * scan duration is fixed in the current firmware; making input responsive
 * *during* an in-flight scan would need an async/background-task redesign
 * of the UART transport, which is out of scope for this fix. */
#define M1_WIFI_DC_PICKER_REFRESH_MS 8000u

#define M1_GUI_ROW_SPACING			1


#define MENU_INDEX_BEACON           3
#define MENU_INDEX_SURVEY           4
/* Wi-Fi menu scrolling viewport: at most 4 rows fit above the bottom info box
 * (info box frame starts at y=42; rows are at y=11,21,31,41). A 5th row would
 * draw over the info box, so the list scrolls within a 4-row window. */
#define WIFI_MENU_VISIBLE_ROWS      4

/* Max EAPOL storage: 10 packets × 512 bytes each (matches ESP32 MAX_HANDSHAKE_PACKETS × captured_packet_t.data) */
#define M1_EAPOL_MAX_BYTES			(10U * 512U)

#define DRIVE0_WIFI                 M1_SD_DIR_WIFI
#define WIFI_FILE_PREFIX            "handshake_"
#define WIFI_FILE_EXTENSION         ".txt"

#define FILE_AP_LIST                "/ap_lists.txt"

/* Saved Networks (SD) — reconnaissance target list, CSV, BSSID = identity. */
#define WIFI_SAVED_PATH             DRIVE0_WIFI "/saved_networks.csv"
#define WIFI_SAVED_TMP_PATH         DRIVE0_WIFI "/saved_networks.tmp"
#define WIFI_SAVED_BAK_PATH         DRIVE0_WIFI "/saved_networks.bak"
#define WIFI_SAVED_MAX              64
#define WIFI_SAVED_FMT_VER          1
/* Line buffer for bounded line-by-line load: must comfortably exceed the
 * writer's own longest possible record (line[160] in
 * wifi_saved_write_recoverable()) so a genuine record is never mistaken
 * for an overlong one. */
#define WIFI_SAVED_LINE_MAX         192
//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

typedef struct {
    //uint8_t ap_count;
    wifi_scanlist_t ap_inf;
    //uint8_t sta_count;
    wifi_connected_stations_list_t sta_inf;
} wifi_target_t;

typedef enum {
    WIFI_SCAN_MODE_AP_LIST = 0,
    WIFI_SCAN_MODE_STA_LIST = 1
} wifi_scan_mode_t;

typedef enum {
    WIFI_SCAN_UI_LIST = 0,
    WIFI_SCAN_UI_STA_SCAN_CONFIRM = 1,
    WIFI_SCAN_UI_TARGET_CONFIRM = 2
} wifi_scan_ui_mode_t;

/* wifi_deauth() outcome, used by wifi_deauth_client() to pick the right
 * post-run screen (Part 2j/2k) without wifi_deauth() itself owning any
 * guided-workflow UI beyond its own inline status messages. */
typedef enum {
    WIFI_DEAUTH_EXIT_NO_TARGET = 0, /* low-level safety guard refused (no AP/client selected) */
    WIFI_DEAUTH_EXIT_SEND_FAIL,     /* the initial "deauth" command failed to send */
    WIFI_DEAUTH_EXIT_STOPPED,       /* user pressed OK/BACK to stop a running attack */
    WIFI_DEAUTH_EXIT_REJECTED       /* ESP32 sent "[!]" (rejected) while running */
} wifi_deauth_exit_t;

static wifi_target_t wifi_target_storage;
wifi_target_t *wifitarget;

/* Scratch for converting to/from the canonical Wi-Fi AP cache
 * (wifi_session_cache_get(), m1_wifi_session_cache.c) in
 * wifi_ap_scan_list_cached() below -- static, not a stack local:
 * WIFI_SESSION_CACHE_MAX_APS (48) * sizeof(wifi_session_ap_t) is a few KB,
 * too much to put on a menu task's own stack. Reused for both directions
 * (cache->wifi_scanlist_t and wifi_scanlist_t->cache); the conversion is
 * sequential within one wifi_ap_scan_list_cached() call, never reentrant. */
static wifi_session_ap_t s_wifi_ap_cache_scratch[WIFI_SESSION_CACHE_MAX_APS];

/* Raw EAPOL byte storage – populated by wifi_handshake() via 'list -h'.  */
/* Content is ready for SD card write after wifi_handshake() returns.     */
static uint8_t  received_eapol[M1_EAPOL_MAX_BYTES];
static uint16_t received_eapol_len = 0;

static int8_t m1_beacon_menu_id = 0;

/* Optional heading overrides for the reused AP/STA paged list screens.
 * NULL => original behavior (used by the independent Networks scan). The guided
 * Deauth Client sets these to label the "Choose Network"/"Choose Client" screens. */
static const char *g_wifi_list_title = NULL;
static const char *g_wifi_list_subtitle = NULL;

extern uint8_t DEBUG_esp32_reset_pin;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
void wifi_scan_ap(void);
void wifi_config(void);

static void wifi_free_mem( char **buf_ptr);
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir);
static uint16_t wifi_station_list_print(ctrl_cmd_t *app_resp, bool up_dir);
static const char *wifi_auth_mode_to_str(int mode);
static void wifi_scan_free_lists(ctrl_cmd_t *ap_req, ctrl_cmd_t *sta_req);
static void wifi_scan_reset_ui_lists(void);
static void wifi_scan_reset_nav_state(wifi_scan_mode_t *scan_mode, wifi_scan_ui_mode_t *ui_mode, uint16_t list_count[2], uint16_t selected_id[2]);
static void wifi_scan_common_exit_cleanup(ctrl_cmd_t *ap_req, ctrl_cmd_t *sta_req);
static void wifi_scan_restore_usbcdc_mode_if_needed(enCdcMode prev_usbcdc_mode, uint8_t restore_usbcdc_mode);
static void wifi_scan_show_scanning_sta(void);
static uint8_t wifi_scan_run_sta_scan(ctrl_cmd_t *ap_req,
                                      ctrl_cmd_t *sta_req,
                                      uint16_t list_count[2],
                                      uint16_t selected_id[2],
                                      wifi_scan_mode_t *scan_mode,
                                      wifi_scan_ui_mode_t *ui_mode);
static void wifi_deauth_draw_running_screen(const wifi_target_t *target, const char *rate_msg);
static void wifi_deauth_show_status_message(const char *line1, const char *line2, uint32_t delay_ms);
static wifi_deauth_exit_t wifi_deauth(void);
static void wifi_footer_bar(const char *left_hint, const char *label);
static void wifi_footer_bar_ex(const char *left_hint, const char *label, uint8_t show_lr_arrows);
static void wifi_card_draw_ex(const char *heading, uint16_t sel, uint16_t count,
                              const char *l1, const char *l2, const char *l3,
                              const char *left_foot, const char *ok_label,
                              const char *foot_left_hint, uint8_t show_lr_arrows);
static void wifi_network_details(const wifi_scanlist_t *ap);
static uint8_t wifi_dc_pick_network(const wifi_scanlist_t *list, uint16_t count, uint16_t *sel);
static uint8_t wifi_dc_pick_device(ctrl_cmd_t *sta_req, char *out_mac, size_t out_mac_cap, int *out_rssi);
static uint8_t wifi_dc_selected_network_screen(const wifi_scanlist_t *ap);
static uint8_t wifi_hs_popcount4(uint8_t mask);
static void wifi_hs_draw_meter(uint8_t y, int count);
static void wifi_handshake_draw_capturing(const char *ssid, int channel, uint8_t phase);
static void wifi_handshake_draw_stages(const char *ssid, int channel, uint8_t mask, int count);
static void wifi_handshake_draw_captured(const char *ssid, int count, uint8_t complete);
static void wifi_handshake_draw_saved_to_sd(const char *saved_name);
static void wifi_process_show_message(const char *msg);
static void wifi_process_show_OkRetry(const char *msg);
static uint8_t wifi_handshake_ask_retry_sd_save(void);
static uint8_t wifi_handshake_ask_retry_filename_input(void);
static uint8_t wifi_read_more_options_save(void);

/* Guided single-client Deauth workflow (self-contained targeting). */
void wifi_deauth_client(void);
/* Guided active-Handshake workflow (self-contained network selection). */
void wifi_handshake_flow(void);
/* Networks: reconnaissance scan/save + Saved Networks browser (SD-backed). */
void wifi_scan_networks(void);
void wifi_saved_browser(void);
static int wifi_saved_find(const char *bssid_upper);
static void wifi_bssid_to_upper(char *dst, const char *src);
static uint8_t wifi_survey_prepare_esp32(enCdcMode *prev_usbcdc_mode, uint8_t *restore_usbcdc_mode);
static uint8_t wifi_dc_menu(const char *title, const char *info1, const char *info2,
                            const char *const *opts, uint8_t nopt, uint8_t start_sel);

//static uint8_t target_status(void);


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_wifi_init(void)
{
    ;
    DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);
} // void menu_wifi_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void  menu_wifi_exit(void)
{
    memset(&wifi_target_storage, 0, sizeof(wifi_target_storage));
    wifitarget = NULL;

    /* Leaving the Wi-Fi area: deliberately KEEP the shared AP cache alive so a
     * scan done here is reused when the user moves to a sibling tool (notably
     * Capture -> Network, a separate top-level menu) without a redundant
     * `scan -a`. The cache is display-only and TTL-bounded
     * (WIFI_SESSION_CACHE_TTL_MS); it does not feed attacks -- deauth/handshake
     * always take a fresh foreground AP scan (target 0) + station scan
     * (target 1) at attack time, so surviving-cache display can never target a
     * stale client. An explicit "Rescan" still force-refreshes. Only the
     * selected-AP storage above is cleared. (Previously this reset the cache
     * here, which forced every tool to rescan.) */

    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET);    // esp32 Reset

    osDelay(20); DEBUG_esp32_reset_pin = HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin);
} // void  menu_wifi_exit(void)

#if 1

static void wifi_free_mem( char **buf_ptr)
{
    if ( *buf_ptr != NULL )
    {
        free (*buf_ptr);
        *buf_ptr = NULL;
    }
} // static void wifi_free_mem( char **buf_ptr)


static int auth_string_to_mode(const char *s, size_t len, bool *matched)
{
    char buf[32];
    size_t copy_len = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    size_t i;

    if (matched)
    {
        *matched = false;
    }

    memcpy(buf, s, copy_len);
    buf[copy_len] = '\0';
    for (i = 0; i < copy_len; i++)
    {
        buf[i] = (char)toupper((unsigned char)buf[i]);
        if ((buf[i] == '-') || (buf[i] == '/') || (buf[i] == ' '))
        {
            buf[i] = '_';
        }
    }

    if (strcmp(buf, "OPEN") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_OPEN;
    }
    if (strcmp(buf, "WEP") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WEP;
    }
    if (strcmp(buf, "WPA") == 0  || strcmp(buf, "WPA_PSK") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WPA_PSK;
    }
    if (strcmp(buf, "WPA2") == 0 || strcmp(buf, "WPA2_PSK") == 0 || strcmp(buf, "WPA2PSK") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WPA2_PSK;
    }
    /* at auth_str[]: "WPA/2" */
    if (strcmp(buf, "WPA_WPA2") == 0 || strcmp(buf, "WPA_WPA2_PSK") == 0 || strcmp(buf, "WPA_2") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WPA_WPA2_PSK;
    }
    /* at auth_str[]: "EAP" */
    if (strcmp(buf, "WPA2_ENTERPRISE") == 0 || strcmp(buf, "EAP") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA2_ENTERPRISE
        return WIFI_AUTH_WPA2_ENTERPRISE;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }
    if (strcmp(buf, "WPA3") == 0 || strcmp(buf, "WPA3_PSK") == 0 || strcmp(buf, "WPA3_SAE") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WPA3_PSK;
    }
    /* at auth_str[]: "WPA2/3" */
    if (strcmp(buf, "WPA2_WPA3") == 0 || strcmp(buf, "WPA2_WPA3_PSK") == 0 || strcmp(buf, "WPA2_WPA3_SAE") == 0)
    {
        if (matched) { *matched = true; }
        return WIFI_AUTH_WPA2_WPA3_PSK;
    }

    /* at auth_str[]: "WAPI" */
    if (strcmp(buf, "WAPI") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WAPI_PSK
        return WIFI_AUTH_WAPI_PSK;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "OWE" */
    if (strcmp(buf, "OWE") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_OWE
        return WIFI_AUTH_OWE;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "WPA3-ENT-192" */
    if (strcmp(buf, "WPA3_ENT_192") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA3_ENT_192
        return WIFI_AUTH_WPA3_ENT_192;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "WPA3-EXT-PSK" */
    if (strcmp(buf, "WPA3_EXT_PSK") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA3_EXT_PSK
        return WIFI_AUTH_WPA3_EXT_PSK;
    #else
        return WIFI_AUTH_WPA3_PSK;
    #endif
    }

    /* at auth_str[]: "WPA3-EXT-MIXED" */
    if (strcmp(buf, "WPA3_EXT_MIXED") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE
        return WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE;
    #elif defined(WIFI_AUTH_WPA3_EXT_MIXED_MODE)
        return WIFI_AUTH_WPA3_EXT_MIXED_MODE;
    #else
        return WIFI_AUTH_WPA2_WPA3_PSK;
    #endif
    }

    /* at auth_str[]: "DPP" */
    if (strcmp(buf, "DPP") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_DPP
        return WIFI_AUTH_DPP;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "WPA3-ENT" */
    if (strcmp(buf, "WPA3_ENT") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA3_ENTERPRISE
        return WIFI_AUTH_WPA3_ENTERPRISE;
    #elif defined(WIFI_AUTH_WPA3_ENT)
        return WIFI_AUTH_WPA3_ENT;
    #elif defined(WIFI_AUTH_WPA2_ENTERPRISE)
        return WIFI_AUTH_WPA2_ENTERPRISE;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "WPA2/WPA3-ENT" */
    if (strcmp(buf, "WPA2_WPA3_ENT") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA2_WPA3_ENTERPRISE
        return WIFI_AUTH_WPA2_WPA3_ENTERPRISE;
    #elif defined(WIFI_AUTH_WPA3_ENTERPRISE)
        return WIFI_AUTH_WPA3_ENTERPRISE;
    #elif defined(WIFI_AUTH_WPA2_ENTERPRISE)
        return WIFI_AUTH_WPA2_ENTERPRISE;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    /* at auth_str[]: "WPA-ENT" */
    if (strcmp(buf, "WPA_ENT") == 0)
    {
        if (matched) { *matched = true; }
    #ifdef WIFI_AUTH_WPA_ENTERPRISE
        return WIFI_AUTH_WPA_ENTERPRISE;
    #elif defined(WIFI_AUTH_WPA2_ENTERPRISE)
        return WIFI_AUTH_WPA2_ENTERPRISE;
    #else
        return WIFI_AUTH_OPEN;
    #endif
    }

    return WIFI_AUTH_OPEN; /* unknown → default open */
}

static int is_mac_addr_text(const char *s)
{
    int i;

    if (!s)
    {
        return 0;
    }

    for (i = 0; i < 17; i++)
    {
        if ((i % 3) == 2)
        {
            if (s[i] != ':')
            {
                return 0;
            }
        }
        else if (!isxdigit((unsigned char)s[i]))
        {
            return 0;
        }
    }

    return 1;
}

static char *split_line_terminator(char *line)
{
    char *p;

    if (!line)
    {
        return NULL;
    }

    for (p = line; *p; p++)
    {
        if ((*p == '\r') || (*p == '\n'))
        {
            char *next = p + 1;

            if (((*p == '\r') && (*next == '\n')) || ((*p == '\n') && (*next == '\r')))
            {
                next++;
            }

            *p = '\0';
            return next;
        }
    }

    return NULL;
}


static void parse_wifi_list_a_lines(char *resp, ctrl_cmd_t *app_req)
{
    char *line;
    char *next;
    wifi_scanlist_t *out_list;

    if (!resp || !app_req)
    {
        return;
    }

    line = resp;
    while (line && *line)
    {
        char *id_end;
        char *mac_p = NULL;
        char *ch_end = NULL;
        char *ch_start = NULL;
        char *ssid_start;
        char *ssid_end;
        char *scan_p;
        char ssid[SSID_LENGTH] = {0};
        char bssid[BSSID_STR_SIZE] = {0};
        size_t ssid_len;
        int channel = 0;
        int rssi = 0;
        int encryption_mode = 0;

        next = split_line_terminator(line);

        if ((line[0] != '[') || !isdigit((unsigned char)line[1]))
        {
            line = next;
            continue;
        }

        id_end = strchr(line, ']');
        if (!id_end)
        {
            line = next;
            continue;
        }

        for (scan_p = id_end + 1; *scan_p; scan_p++)
        {
            if (is_mac_addr_text(scan_p))
            {
                mac_p = scan_p;
                break;
            }
        }
        if (!mac_p)
        {
            line = next;
            continue;
        }

        memcpy(bssid, mac_p, 17);
        bssid[17] = '\0';

        /* Parse AUTH token after BSSID */
        {
            char *auth_start = mac_p + 17;
            char *auth_end;
            bool auth_matched = false;
            while (*auth_start && isspace((unsigned char)*auth_start))
            {
                auth_start++;
            }
            auth_end = auth_start;
            while (*auth_end && !isspace((unsigned char)*auth_end))
            {
                auth_end++;
            }
            if (auth_end > auth_start)
            {
                size_t auth_len = (size_t)(auth_end - auth_start);
                encryption_mode = auth_string_to_mode(auth_start, auth_len, &auth_matched);
                //M1_LOG_I(TAG, \
                         "AUTH parse: raw='%.*s' -> mode=%d (matched=%d)\r\n", (int)auth_len,auth_start, encryption_mode, auth_matched ? 1 : 0);
                if (!auth_matched)
                {
                    //M1_LOG_E(TAG, "Unknown AUTH token from list -a: %.*s\r\n", (int)auth_len, auth_start);
                }
            }
        }

        /* Working backwards from MAC: first find RSSI (may be negative), then channel */
        ch_end = mac_p;
        while ((ch_end > id_end) && isspace((unsigned char)*(ch_end - 1)))
        {
            ch_end--;
        }

        /* ch_end now points past the RSSI token; scan back digits and optional leading '-' */
        ch_start = ch_end;
        while ((ch_start > id_end) && isdigit((unsigned char)*(ch_start - 1)))
        {
            ch_start--;
        }
        if ((ch_start > id_end) && (*(ch_start - 1) == '-'))
        {
            ch_start--;
        }
        if (ch_start == ch_end)
        {
            line = next;
            continue;
        }
        rssi = (int)strtol(ch_start, NULL, 10);

        /* Now find channel before RSSI */
        ch_end = ch_start;
        while ((ch_end > id_end) && isspace((unsigned char)*(ch_end - 1)))
        {
            ch_end--;
        }
        ch_start = ch_end;
        while ((ch_start > id_end) && isdigit((unsigned char)*(ch_start - 1)))
        {
            ch_start--;
        }
        if (ch_start == ch_end)
        {
            line = next;
            continue;
        }
        channel = (int)strtol(ch_start, NULL, 10);

        ssid_start = id_end + 1;
        while (*ssid_start && isspace((unsigned char)*ssid_start))
        {
            ssid_start++;
        }

        ssid_end = ch_start;
        while ((ssid_end > ssid_start) && isspace((unsigned char)*(ssid_end - 1)))
        {
            ssid_end--;
        }

        /* New list-a format wraps SSID in quotes: "SSID" */
        if ((ssid_end > ssid_start) && (*ssid_start == '"'))
        {
            ssid_start++;
        }
        if ((ssid_end > ssid_start) && (*(ssid_end - 1) == '"'))
        {
            ssid_end--;
        }

        ssid_len = (size_t)(ssid_end - ssid_start);
        if (ssid_len >= SSID_LENGTH)
        {
            ssid_len = SSID_LENGTH - 1;
        }
        if (ssid_len > 0)
        {
            memcpy(ssid, ssid_start, ssid_len);
        }
        ssid[ssid_len] = '\0';

        out_list = realloc(app_req->u.wifi_ap_scan.out_list,
                           sizeof(wifi_scanlist_t) * (app_req->u.wifi_ap_scan.count + 1));
        if (out_list == NULL)
        {
            return;
        }

        app_req->u.wifi_ap_scan.out_list = out_list;
        memset(&out_list[app_req->u.wifi_ap_scan.count], 0, sizeof(wifi_scanlist_t));
        strncpy((char *)out_list[app_req->u.wifi_ap_scan.count].ssid, ssid, SSID_LENGTH - 1);
        strncpy((char *)out_list[app_req->u.wifi_ap_scan.count].bssid, bssid, BSSID_STR_SIZE - 1);
        out_list[app_req->u.wifi_ap_scan.count].rssi = rssi;
        out_list[app_req->u.wifi_ap_scan.count].channel = channel;
        out_list[app_req->u.wifi_ap_scan.count].encryption_mode = encryption_mode;
        app_req->u.wifi_ap_scan.count++;

        line = next;
    }
}

static void parse_wifi_list_s_lines(char *resp, ctrl_cmd_t *app_req)
{
    char *line;
    char *next;
    wifi_scanlist_t *out_list;

    if (!resp || !app_req)
    {
        return;
    }

    line = resp;
    while (line && *line)
    {
        char *id_end;
        char *mac_p;
        char bssid[BSSID_STR_SIZE] = {0};
        int rssi;

        next = split_line_terminator(line);

        if ((line[0] != '[') || !isdigit((unsigned char)line[1]))
        {
            line = next;
            continue;
        }

        id_end = strchr(line, ']');
        if (!id_end)
        {
            line = next;
            continue;
        }

        mac_p = id_end + 1;
        while (*mac_p && isspace((unsigned char)*mac_p))
        {
            mac_p++;
        }

        if (!is_mac_addr_text(mac_p))
        {
            line = next;
            continue;
        }

        memcpy(bssid, mac_p, 17);
        bssid[17] = '\0';

        rssi = m1_wifi_parse_sta_rssi(mac_p + 17);

        out_list = realloc(app_req->u.wifi_ap_scan.out_list,
                           sizeof(wifi_scanlist_t) * (app_req->u.wifi_ap_scan.count + 1));
        if (out_list == NULL)
        {
            return;
        }

        app_req->u.wifi_ap_scan.out_list = out_list;
        memset(&out_list[app_req->u.wifi_ap_scan.count], 0, sizeof(wifi_scanlist_t));
        strncpy((char *)out_list[app_req->u.wifi_ap_scan.count].bssid, bssid, BSSID_STR_SIZE - 1);
        out_list[app_req->u.wifi_ap_scan.count].rssi = rssi;
        out_list[app_req->u.wifi_ap_scan.count].channel = 0;
        out_list[app_req->u.wifi_ap_scan.count].encryption_mode = WIFI_AUTH_OPEN;
        app_req->u.wifi_ap_scan.count++;

        line = next;
    }
}

uint8_t wifi_ap_sta_scan_list(ctrl_cmd_t *app_req, uint8_t target)
{
    char *resp_buf = NULL;
    uint16_t resp_len;
    uint8_t ret = ERROR;
    uint32_t timeout_ms;
    uint32_t scan_cmd_timeout_ms;
    uint32_t dropped_cnt;
    uint32_t ore_cnt;

    if (!app_req)
    {
        return ERROR;
    }

    if (app_req->cmd_timeout_sec <= 0)
    {
        app_req->cmd_timeout_sec = DEFAULT_CTRL_RESP_TIMEOUT;
    }
    timeout_ms = SEC_TO_MILLISEC(app_req->cmd_timeout_sec);
    scan_cmd_timeout_ms = timeout_ms;
    if (scan_cmd_timeout_ms > MAX_SCAN_CMD_TIMEOUT_MS)
    {
        scan_cmd_timeout_ms = MAX_SCAN_CMD_TIMEOUT_MS;
    }
    (void)m1_esp32_get_and_clear_rx_drop_count();
    (void)m1_esp32_get_and_clear_ore_count();

    if (app_req->u.wifi_ap_scan.out_list)
    {
        free(app_req->u.wifi_ap_scan.out_list);
        app_req->u.wifi_ap_scan.out_list = NULL;
    }
    app_req->u.wifi_ap_scan.count = 0;

    resp_buf = (char *)malloc(ESP32UART_BUFFER_SIZE);

    if (!resp_buf)
    {
        //M1_LOG_E(TAG, "Failed to alloc response buffer\r\n");
        return ERROR;
    }
    memset(resp_buf, 0x00, ESP32UART_BUFFER_SIZE);

    do
    {
        wifi_free_mem(&app_req->at_cmd);
        m1_ringbuffer_reset(&esp32_rb_hdl);

        if (target == 0)    // ap
        {
            app_req->at_cmd = strdup(CONCAT_CMD_PARAM("scan -a", ""));
        }
        else                // station
        {
            app_req->at_cmd = strdup(CONCAT_CMD_PARAM("scan -s", ""));
        }

        if (!app_req->at_cmd)
        {
            break;
        }
        app_req->cmd_len = strlen(app_req->at_cmd);

        if (!esp32_uart_write((uint8_t *)app_req->at_cmd, app_req->cmd_len, 100))
        {
            //M1_LOG_E(TAG, "Command send failed\r\n");
            break;
        }

        resp_len = esp32_uart_read_until_prompt(resp_buf, ESP32UART_BUFFER_SIZE, scan_cmd_timeout_ms, ">> ");
        dropped_cnt = m1_esp32_get_and_clear_rx_drop_count();
        ore_cnt = m1_esp32_get_and_clear_ore_count();
        if (dropped_cnt || ore_cnt)
        {
            //M1_LOG_E(TAG, "UART RX warn after scan -%c: drop=%lu ore=%lu\r\n", (target == 0) ? 'a' : 's', (unsigned long)dropped_cnt, (unsigned long)ore_cnt);
        }
        if ((resp_len == 0) || (strstr(resp_buf, ">> ") == NULL))
        {
            //M1_LOG_E(TAG, "scan -%c prompt timeout\r\n", (target == 0) ? 'a' : 's');
            break;
        }

        wifi_free_mem(&app_req->at_cmd);

        m1_ringbuffer_reset(&esp32_rb_hdl);

        if (target == 0)    // ap
        {
            app_req->at_cmd = strdup(CONCAT_CMD_PARAM("list -a", ""));
        }
        else
        {
            app_req->at_cmd = strdup(CONCAT_CMD_PARAM("list -s", ""));
        }

        if (!app_req->at_cmd)
        {
            break;
        }
        app_req->cmd_len = strlen(app_req->at_cmd);

        if (!esp32_uart_write((uint8_t *)app_req->at_cmd, app_req->cmd_len, 100))
        {
            //M1_LOG_E(TAG, "Command send failed\r\n");
            break;
        }

        resp_len = esp32_uart_read_until_prompt(resp_buf, ESP32UART_BUFFER_SIZE, timeout_ms, ">> ");
        dropped_cnt = m1_esp32_get_and_clear_rx_drop_count();
        ore_cnt = m1_esp32_get_and_clear_ore_count();
        if (dropped_cnt || ore_cnt)
        {
            //M1_LOG_E(TAG, "UART RX warn after list -%c: drop=%lu ore=%lu\r\n", (target == 0) ? 'a' : 's', (unsigned long)dropped_cnt, (unsigned long)ore_cnt);
        }
        if ((resp_len == 0) || (strstr(resp_buf, ">> ") == NULL))
        {
            //M1_LOG_E(TAG, "list -%c prompt timeout\r\n", (target == 0) ? 'a' : 's');
            break;
        }

        if (target == 0)
        {
            parse_wifi_list_a_lines(resp_buf, app_req);
        }
        else
        {
            parse_wifi_list_s_lines(resp_buf, app_req);
        }

        ret = SUCCESS;
    } while (0);

    wifi_free_mem(&app_req->at_cmd);
    wifi_free_mem(&app_req->cmd_resp);
    free(resp_buf);

    app_req->msg_type = CTRL_RESP;
    app_req->resp_event_status = (ret == SUCCESS) ? SUCCESS : ERROR;

    if (ret != SUCCESS)
    {
        //M1_LOG_E(TAG, "wifi_ap_sta_scan_list failed\r\n");
    }

    return ret;
}

#endif

/*
 * AP-scan (target==0 in wifi_ap_sta_scan_list() terms) through the shared,
 * canonical Wi-Fi AP cache (m1_wifi_session_cache.c / wifi_session_cache_get()).
 * Networks, Handshake, and Deauth Client all route their AP-list step
 * through this single function, so a scan taken in one tool is reused by
 * the others while it's still fresh instead of every tool re-issuing its
 * own "scan -a" -- switching between those three does not rescan. Station
 * (client) scans (target==1) are NEVER routed through this -- always call
 * wifi_ap_sta_scan_list(req, 1) directly, see wifi_scan_devices(); the
 * cache only ever holds AP data, and the targeted client scan must stay a
 * fresh foreground scan every time regardless of the AP cache's state.
 *
 * force_refresh=true: an explicit user "Rescan"/"Scan Networks" action --
 * invalidates the cache first, then always performs a real scan -a +
 * list -a, replacing the cache on success (matches
 * wifi_session_cache_invalidate()'s documented "invalidate before a forced
 * rescan" contract: a failed/cancelled scan below leaves the cache
 * honestly stale, never stamped fresh over data that was never actually
 * refreshed).
 * force_refresh=false: reuse the cache if it's still valid and within
 * WIFI_SESSION_CACHE_TTL_MS -- no scan -a round trip at all in that case.
 * Missing/expired cache falls through to the exact same real-scan path as
 * force_refresh=true.
 *
 * Either way, on SUCCESS app_req->u.wifi_ap_scan.out_list/.count end up
 * populated exactly as a direct wifi_ap_sta_scan_list(app_req, 0) call
 * would populate them -- every existing consumer (wifi_browse(), the BSSID
 * re-match loops in *_USE_SELECTED, etc.) needs no changes. Caller still
 * owns/frees out_list via wifi_scan_free_lists(), same as before.
 */
static uint8_t wifi_ap_scan_list_cached(ctrl_cmd_t *app_req, bool force_refresh)
{
    wifi_session_cache_t *cache = wifi_session_cache_get();
    uint32_t now = HAL_GetTick();

    if (!force_refresh && wifi_session_cache_is_fresh(cache, now, WIFI_SESSION_CACHE_TTL_MS))
    {
        uint16_t n = wifi_session_cache_snapshot(cache, s_wifi_ap_cache_scratch, WIFI_SESSION_CACHE_MAX_APS);
        wifi_scanlist_t *list = NULL;
        uint16_t i;

        if (app_req->u.wifi_ap_scan.out_list != NULL)
        {
            free(app_req->u.wifi_ap_scan.out_list);
            app_req->u.wifi_ap_scan.out_list = NULL;
        }
        app_req->u.wifi_ap_scan.count = 0;

        if (n > 0u)
        {
            list = (wifi_scanlist_t *)malloc((size_t)n * sizeof(wifi_scanlist_t));
            if (list == NULL)
            {
                app_req->msg_type = CTRL_RESP;
                app_req->resp_event_status = ERROR;
                return ERROR;
            }
            for (i = 0; i < n; i++)
            {
                memcpy(list[i].ssid, s_wifi_ap_cache_scratch[i].ssid, sizeof(list[i].ssid));
                memcpy(list[i].bssid, s_wifi_ap_cache_scratch[i].bssid, sizeof(list[i].bssid));
                list[i].rssi = s_wifi_ap_cache_scratch[i].rssi;
                list[i].channel = s_wifi_ap_cache_scratch[i].channel;
                list[i].encryption_mode = s_wifi_ap_cache_scratch[i].encryption_mode;
            }
        }
        app_req->u.wifi_ap_scan.out_list = list;
        app_req->u.wifi_ap_scan.count = (int)n;
        app_req->msg_type = CTRL_RESP;
        app_req->resp_event_status = SUCCESS;
        return SUCCESS;
    }

    wifi_session_cache_invalidate(cache);
    {
        uint8_t ret = wifi_ap_sta_scan_list(app_req, 0);
        if (ret == SUCCESS)
        {
            uint16_t n = (uint16_t)app_req->u.wifi_ap_scan.count;
            uint16_t i;

            if (n > WIFI_SESSION_CACHE_MAX_APS) { n = WIFI_SESSION_CACHE_MAX_APS; }
            for (i = 0; i < n; i++)
            {
                memcpy(s_wifi_ap_cache_scratch[i].ssid, app_req->u.wifi_ap_scan.out_list[i].ssid,
                       sizeof(s_wifi_ap_cache_scratch[i].ssid));
                memcpy(s_wifi_ap_cache_scratch[i].bssid, app_req->u.wifi_ap_scan.out_list[i].bssid,
                       sizeof(s_wifi_ap_cache_scratch[i].bssid));
                s_wifi_ap_cache_scratch[i].rssi = app_req->u.wifi_ap_scan.out_list[i].rssi;
                s_wifi_ap_cache_scratch[i].channel = app_req->u.wifi_ap_scan.out_list[i].channel;
                s_wifi_ap_cache_scratch[i].encryption_mode = app_req->u.wifi_ap_scan.out_list[i].encryption_mode;
            }
            wifi_session_cache_publish(cache, s_wifi_ap_cache_scratch, n, HAL_GetTick());
        }
        return ret;
    }
}

static void wifi_process_show_message(const char *msg)
{
    m1_u8g2_firstpage();
    u8g2_DrawStr(&m1_u8g2, 2, 15, msg);
    m1_u8g2_nextpage();
}


static void wifi_process_show_OkRetry(const char *msg)
{
    const char *line1 = (msg != NULL) ? msg : "Operation failed";

    m1_u8g2_firstpage();
    u8g2_DrawStr(&m1_u8g2, 2, 15, line1);
    u8g2_DrawStr(&m1_u8g2,
                 2,
                 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING,
                 "OK:Retry BACK:Exit");
    m1_u8g2_nextpage();
}


static const char *wifi_auth_mode_to_str(int mode)
{
	switch (mode)
	{
		case WIFI_AUTH_OPEN:
			return "OPEN";
		case WIFI_AUTH_WEP:
			return "WEP";
		case WIFI_AUTH_WPA_PSK:
			return "WPA";
		case WIFI_AUTH_WPA2_PSK:
			return "WPA2";
		case WIFI_AUTH_WPA_WPA2_PSK:
			return "WPA/WPA2";
		case WIFI_AUTH_WPA2_ENTERPRISE:
			return "WPA2-ENT";
		case WIFI_AUTH_WPA3_PSK:
			return "WPA3";
		case WIFI_AUTH_WPA2_WPA3_PSK:
			return "WPA2/WPA3";
		default:
			return "UNKNOWN";
	}
}

static void wifi_scan_free_lists(ctrl_cmd_t *ap_req, ctrl_cmd_t *sta_req)
{
    if ((ap_req != NULL) && (ap_req->u.wifi_ap_scan.out_list != NULL))
    {
        free(ap_req->u.wifi_ap_scan.out_list);
        ap_req->u.wifi_ap_scan.out_list = NULL;
    }

    if ((sta_req != NULL) && (sta_req->u.wifi_ap_scan.out_list != NULL))
    {
        free(sta_req->u.wifi_ap_scan.out_list);
        sta_req->u.wifi_ap_scan.out_list = NULL;
    }
}


static void wifi_scan_reset_ui_lists(void)
{
    wifi_ap_list_print(NULL, false);
    wifi_station_list_print(NULL, false);
}


static void wifi_scan_reset_nav_state(wifi_scan_mode_t *scan_mode, wifi_scan_ui_mode_t *ui_mode, uint16_t list_count[2], uint16_t selected_id[2])
{
    *scan_mode = WIFI_SCAN_MODE_AP_LIST;
    *ui_mode = WIFI_SCAN_UI_LIST;
    list_count[0] = 0;
    list_count[1] = 0;
    selected_id[0] = 0;
    selected_id[1] = 0;
}


static void wifi_scan_common_exit_cleanup(ctrl_cmd_t *ap_req, ctrl_cmd_t *sta_req)
{
    wifi_scan_free_lists(ap_req, sta_req);
    wifi_scan_reset_ui_lists();
    xQueueReset(main_q_hdl); // Reset main q before return
    m1_esp32_deinit();
}


static void wifi_scan_restore_usbcdc_mode_if_needed(enCdcMode prev_usbcdc_mode, uint8_t restore_usbcdc_mode)
{
    if (restore_usbcdc_mode && (m1_usbcdc_mode != prev_usbcdc_mode))
    {
        m1_logdb_deinit();
        m1_usbcdc_mode = prev_usbcdc_mode;
        m1_logdb_init();

        //M1_LOG_D(M1_LOGDB_TAG, "wifi_scan_ap exit: restore usbcdc_mode=%d", (int)m1_usbcdc_mode);
    }
}


static void wifi_scan_show_scanning_sta(void)
{
    m1_u8g2_firstpage();
    u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning STA...");
    u8g2_DrawXBMP(&m1_u8g2,
                  M1_LCD_DISPLAY_WIDTH / 2 - 18 / 2,
                  M1_LCD_DISPLAY_HEIGHT / 2 - 2,
                  18,
                  32,
                  hourglass_18x32);
    m1_u8g2_nextpage();
}


static uint8_t wifi_scan_run_sta_scan(ctrl_cmd_t *ap_req,
                                      ctrl_cmd_t *sta_req,
                                      uint16_t list_count[2],
                                      uint16_t selected_id[2],
                                      wifi_scan_mode_t *scan_mode,
                                      wifi_scan_ui_mode_t *ui_mode)
{
    BaseType_t ret;

    wifi_scan_show_scanning_sta();

    sta_req->cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
    sta_req->msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
    ret = wifi_ap_sta_scan_list(sta_req, 1);
    if (ret == SUCCESS)
    {
        *scan_mode = WIFI_SCAN_MODE_STA_LIST;
        *ui_mode = WIFI_SCAN_UI_LIST;
        list_count[1] = wifi_station_list_print(sta_req, true);
        selected_id[1] = 0;
        return 1U;
    }

    *ui_mode = WIFI_SCAN_UI_LIST;
    wifi_ap_list_print(ap_req, false);
    wifi_ap_list_print(ap_req, true);
    return 0U;
}


static void wifi_deauth_draw_running_screen(const wifi_target_t *tg, const char *rate_msg)
{
    char ui_msg[32];
    uint8_t y_offset;

    m1_u8g2_firstpage();
    y_offset = M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT;
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, "Deauth Running");

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    snprintf(ui_msg, sizeof(ui_msg), "Network: %.20s",
             (tg->ap_inf.ssid[0] != '\0') ? (const char *)tg->ap_inf.ssid : "Hidden Network");
    y_offset += M1_GUI_FONT_HEIGHT;
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);

    /* Short identity label (vendor from OUI + MAC suffix, or the neutral
     * "Device" fallback) alongside the SSID above -- no invented hostname or
     * product name (see m1_wifi_client_id.h). */
    if (tg->sta_inf.bssid[0] != '\0')
    {
        char id_label[24];
        (void)m1_wifi_format_client_label((const char *)tg->sta_inf.bssid, id_label, sizeof(id_label));
        snprintf(ui_msg, sizeof(ui_msg), "Device: %s", id_label);
    }
    else
    {
        snprintf(ui_msg, sizeof(ui_msg), "Device: not selected");
    }
    y_offset += M1_GUI_FONT_HEIGHT;
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);

    if ((rate_msg != NULL) && (rate_msg[0] != '\0'))
    {
        y_offset += M1_GUI_FONT_HEIGHT;
        u8g2_DrawStr(&m1_u8g2, 2, y_offset, rate_msg);
    }

    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}


static void wifi_deauth_show_status_message(const char *line1, const char *line2, uint32_t delay_ms)
{
    m1_u8g2_firstpage();
    if (line1 != NULL)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 15, line1);
    }
    if ((line2 != NULL) && (line2[0] != '\0'))
    {
        u8g2_DrawStr(&m1_u8g2,
                     2,
                     15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING,
                     line2);
    }
    m1_u8g2_nextpage();

    if (delay_ms > 0U)
    {
        osDelay(delay_ms);
    }
}


/* Count set bits in the low nibble (0..4). The ESP32 stores at most one copy of
 * each 4-way message, so the captured count equals popcount of the mask. */
static uint8_t wifi_hs_popcount4(uint8_t mask)
{
    uint8_t n = 0U;
    uint8_t i;
    for (i = 0U; i < 4U; i++) { if (mask & (uint8_t)(1U << i)) { n++; } }
    return n;
}

/* 10-slot capture-buffer meter (buffer usage, NOT completion). One filled
 * segment per stored packet; clamped 0..10. Drawn with u8g2 rectangle
 * primitives (filled = DrawBox, empty = DrawFrame) — no bitmap assets, no
 * Unicode blocks. MAX stored packets is 10 (ESP32 MAX_HANDSHAKE_PACKETS). */
static void wifi_hs_draw_meter(uint8_t y, int count)
{
    const uint8_t seg_w = 9U, seg_h = 6U, gap = 2U, x0 = 3U;
    uint8_t i;
    int c = count;
    if (c < 0)  { c = 0; }
    if (c > 10) { c = 10; }              /* clamp safely to the 10-slot buffer */
    for (i = 0U; i < 10U; i++)
    {
        u8g2_uint_t x = (u8g2_uint_t)(x0 + i * (seg_w + gap));
        if ((int)i < c) { u8g2_DrawBox(&m1_u8g2, x, y, seg_w, seg_h); }
        else            { u8g2_DrawFrame(&m1_u8g2, x, y, seg_w, seg_h); }
    }
}

/* Fallback live view (used until real mask data is seen): target network + a
 * low-frequency activity marker that cycles WITHOUT implying progress percent.
 * 'phase' advances only on the animation tick from the capture loop. */
static void wifi_handshake_draw_capturing(const char *ssid, int channel, uint8_t phase)
{
    char buf[28];
    uint8_t i;

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Capturing Handshake");
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    snprintf(buf, sizeof(buf), "%.16s", (ssid && ssid[0]) ? ssid : "Hidden Network");
    u8g2_DrawStr(&m1_u8g2, 2, 24, buf);
    if ((channel >= 1) && (channel <= 196))
    {
        snprintf(buf, sizeof(buf), "Ch %d", channel);
        u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 4 - u8g2_GetStrWidth(&m1_u8g2, buf)), 24, buf);
    }
    u8g2_DrawStr(&m1_u8g2, 2, 37, "Waiting for handshake");
    /* Moving single marker across 12 cells: cycles, never a progress bar. */
    for (i = 0U; i < 12U; i++)
    {
        u8g2_uint_t x = (u8g2_uint_t)(4 + i * 10);
        if (i == (phase % 12U)) { u8g2_DrawBox(&m1_u8g2, x, 44, 6, 4); }
        else                    { u8g2_DrawPixel(&m1_u8g2, (u8g2_uint_t)(x + 2), 46); }
    }
    wifi_footer_bar(NULL, "Stop");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

/* Primary live visualizer (used once real mask data arrives): M1-M4 stage
 * circles driven ONLY by the actual completion-mask bits (bit0=M1..bit3=M4),
 * plus the 10-slot buffer meter and packet count. Filled disc = detected,
 * empty circle = not detected. No time-based filling. */
static void wifi_handshake_draw_stages(const char *ssid, int channel, uint8_t mask, int count)
{
    char buf[24];
    uint8_t i;

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 8, "Capturing Handshake");
    snprintf(buf, sizeof(buf), "%.14s", (ssid && ssid[0]) ? ssid : "Hidden Network");
    u8g2_DrawStr(&m1_u8g2, 2, 19, buf);
    if ((channel >= 1) && (channel <= 196))
    {
        snprintf(buf, sizeof(buf), "Ch %d", channel);
        u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 4 - u8g2_GetStrWidth(&m1_u8g2, buf)), 19, buf);
    }
    /* Stage row: "M1 M2 M3 M4" each with a circle; filled iff its bit is set. */
    for (i = 0U; i < 4U; i++)
    {
        u8g2_uint_t lx = (u8g2_uint_t)(3 + i * 31);
        u8g2_uint_t cx = (u8g2_uint_t)(lx + 15);
        snprintf(buf, sizeof(buf), "M%u", (unsigned int)(i + 1U));
        u8g2_DrawStr(&m1_u8g2, lx, 31, buf);
        if (mask & (uint8_t)(1U << i)) { u8g2_DrawDisc(&m1_u8g2, cx, 28, 3, U8G2_DRAW_ALL); }
        else                           { u8g2_DrawCircle(&m1_u8g2, cx, 28, 3, U8G2_DRAW_ALL); }
    }
    snprintf(buf, sizeof(buf), "Packets %d/10", (count < 0) ? 0 : (count > 10 ? 10 : count));
    u8g2_DrawStr(&m1_u8g2, 2, 42, buf);
    wifi_hs_draw_meter(45, count);       /* meter bottom at y=50, clears footer (y>=52) */
    wifi_footer_bar(NULL, "Stop");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}


/* Result screen for a finished attempt that returned data. 'complete' is true
 * only when the final mask shows all four messages (0x0F); otherwise the capture
 * is a partial handshake. Packet count is the ESP32 'Count'. CENTER = Save. */
static void wifi_handshake_draw_captured(const char *ssid, int count, uint8_t complete)
{
    char buf[28];

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, complete ? "Handshake Captured" : "Capture Incomplete");
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    snprintf(buf, sizeof(buf), "%.20s", (ssid && ssid[0]) ? ssid : "Hidden Network");
    u8g2_DrawStr(&m1_u8g2, 2, 24, buf);
    if (count > 0) { snprintf(buf, sizeof(buf), "Packets: %d", count); }
    else           { snprintf(buf, sizeof(buf), "Packets: -"); }
    u8g2_DrawStr(&m1_u8g2, 2, 36, buf);
    u8g2_DrawStr(&m1_u8g2, 2, 48, complete ? "Complete capture" : "Handshake incomplete");
    wifi_footer_bar(NULL, "Save");                 /* CENTER proceeds to the save workflow */
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}


static void wifi_handshake_draw_saved_to_sd(const char *saved_name)
{
    (void)saved_name;   /* unified save-success screen shows no filename */
    m1_draw_file_saved_screen();
}



static uint8_t wifi_handshake_ask_retry_sd_save(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    wifi_process_show_OkRetry("Save Failed");

    xQueueReset(main_q_hdl);
    while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE)
    {
        ;
    }

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
        {
            ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
            if (ret == pdTRUE)
            {
                if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    return 1U;
                }
                if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    return 0U;
                }
            }
        }
    }
}


static uint8_t wifi_handshake_ask_retry_filename_input(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    wifi_process_show_OkRetry("File name failed");

    xQueueReset(main_q_hdl);
    while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE)
    {
        ;
    }

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
        {
            ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
            if (ret == pdTRUE)
            {
                if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    return 1U;
                }
                if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    return 0U;
                }
            }
        }
    }
}



/* wifi_xkey_handler removed: the top-level Wi-Fi status/info box and the Beacon
 * LEFT/RIGHT mode toggle that lived on it are gone. Menu navigation uses UP/DOWN
 * (unaffected); Beacon source is now chosen inside wifi_beacon(). The WiFi menu's
 * xkey_handler slot is set to NULL in m1_menu.c. */



/*============================================================================*/
/**
  * @brief Scans for wifi access point list
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_scan_ap(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    enCdcMode prev_usbcdc_mode;
    uint8_t restore_usbcdc_mode = 0;
    ctrl_cmd_t ap_req = CTRL_CMD_DEFAULT_REQ();
    ctrl_cmd_t sta_req = CTRL_CMD_DEFAULT_REQ();
    uint16_t list_count[2] = {0, 0};
    uint16_t selected_id[2] = {0, 0};
    char select_cmd[32];
    char ui_msg[32];
    wifi_scan_mode_t scan_mode = WIFI_SCAN_MODE_AP_LIST;
    wifi_scan_ui_mode_t ui_mode = WIFI_SCAN_UI_LIST;
    uint8_t scan_retry = 0;
    uint8_t need_esp32_reinit = 0;
    uint8_t esp32_prompt_ready = 0;
    uint8_t ready_try;
    uint32_t prompt_timeout_ms;
    static const char mode_wifi_cmd[] = "mode -w\r\n";
    char uart_resp[512] = {0};
    static const char esp32_prompt_ping[] = "\r\n";
    wifi_scanlist_t *ap_list;
    wifi_scanlist_t *sta_list;
    uint8_t y_offset;
    uint8_t request_restart_scan = 1U;
    uint8_t exit_scan = 0U;

    wifitarget = &wifi_target_storage;
    memset(wifitarget, 0, sizeof(*wifitarget));

    /* Graphic work starts here */
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    prev_usbcdc_mode = m1_usbcdc_mode;

    if (prev_usbcdc_mode == CDC_MODE_ESP32)
    {
        restore_usbcdc_mode = 1;
        m1_logdb_deinit();

        m1_usbcdc_mode = CDC_MODE_LOG_CLI;
        m1_logdb_init();
    }

    // Validate both SW init status and HW EN pin state.
    // After bridge mode exit, EN can be low while init flag is still true,
    // which makes scan commands fail with prompt timeout.
    if (!m1_esp32_get_init_status())
    {
        need_esp32_reinit = 1;
    }

    if (HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin) == GPIO_PIN_RESET)
    {
        need_esp32_reinit = 1;
    }

    if (need_esp32_reinit)
    {
        if (m1_esp32_get_init_status())
        {
            m1_esp32_deinit();
        }
        m1_esp32_init();

        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
        u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
        m1_u8g2_nextpage();
    }

    /* Guard scan path with prompt readiness check.
     * Bridge-mode transitions can leave ESP32 powered but shell not ready yet. */
    for (ready_try = 0; ready_try < 3U; ready_try++)
    {
        /* Adaptive timeout: start short for already-running module,
         * longer after reinit, and increase on each retry. */
        if (need_esp32_reinit)
        {
            prompt_timeout_ms = 1200U + ((uint32_t)ready_try * 700U);
        }
        else
        {
            prompt_timeout_ms = 300U + ((uint32_t)ready_try * 400U);
        }

        memset(uart_resp, 0, sizeof(uart_resp));
        (void)esp32_uart_write((const uint8_t *)esp32_prompt_ping,
                               (uint16_t)(sizeof(esp32_prompt_ping) - 1U),
                               100U);

        if (esp32_uart_read_until_prompt(uart_resp,
                                         (uint16_t)sizeof(uart_resp),
                                         prompt_timeout_ms,
                                         ">> ") > 0U)
        {
            if (strstr(uart_resp, ">> ") != NULL)
            {
                esp32_prompt_ready = 1U;
                break;
            }
        }

        //M1_LOG_D(M1_LOGDB_TAG, "wifi_scan_ap: ESP32 prompt not ready (try %u)", (unsigned int)(ready_try + 1U));
        m1_esp32_deinit();
        osDelay(20);
        m1_esp32_init();
        need_esp32_reinit = 1U;
        osDelay(150);
    }


    /* Ensure shell is in BLE mode before advertise command. */
    if (!esp32_uart_write((const uint8_t *)mode_wifi_cmd, (uint16_t)(sizeof(mode_wifi_cmd) - 1U), 100U))
    {
        ret = ERROR;
    }
    else if (esp32_uart_read_until_prompt(uart_resp, (uint16_t)sizeof(uart_resp), 1500U, ">> ") == 0U)
    {
        ret = ERROR;
    }

    if (!esp32_prompt_ready)
    {
        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 2, 15, "ESP32 not ready");
        u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING, "Try bridge reset");
        m1_u8g2_nextpage();
        osDelay(1200);
        exit_scan = 1U;
    }

    while (!exit_scan) // Main loop of this task
    {
        if (request_restart_scan)
        {
            request_restart_scan = 0U;

            wifi_scan_reset_nav_state(&scan_mode, &ui_mode, list_count, selected_id);
            wifi_scan_free_lists(&ap_req, &sta_req);
            wifi_scan_reset_ui_lists();

            m1_u8g2_firstpage();
            u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
            m1_u8g2_nextpage();

            // implemented synchronous
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;

            /* Route Networks through the shared AP cache: reuse a fresh scan
             * from another Wi-Fi tool if available, otherwise scan and publish
             * so the others reuse this one. Populates ap_req identically to the
             * former direct wifi_ap_sta_scan_list(&ap_req, 0). */
            ret = wifi_ap_scan_list_cached(&ap_req, false);

            if (ret == SUCCESS)
            {
                scan_retry = 0;
                list_count[0] = wifi_ap_list_print(&ap_req, true);
            }
            else
            {
                scan_retry++;
                m1_u8g2_firstpage();
                u8g2_DrawStr(&m1_u8g2, 6, 15, "Scan AP failed");
                m1_u8g2_nextpage();
                osDelay(1000);

                //M1_LOG_E(M1_LOGDB_TAG, "wifi_scan_ap failed (retry %u/%u)\r\n", (unsigned int)scan_retry, (unsigned int)M1_WIFI_SCAN_RETRY_MAX);
                if (scan_retry >= M1_WIFI_SCAN_RETRY_MAX)
                {
                    m1_u8g2_firstpage();
                    u8g2_DrawStr(&m1_u8g2, 2, 15, "Scan AP failed");
                    u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING, "Check ESP32 mode");
                    m1_u8g2_nextpage();
                    osDelay(1200);
                    exit_scan = 1U;
                }
                else
                {
                    request_restart_scan = 1U;
                }
                continue;
            }
        }

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

                    if (scan_mode == WIFI_SCAN_MODE_AP_LIST)
                    {
                        if (ui_mode != WIFI_SCAN_UI_LIST)
                        {
                            ui_mode = WIFI_SCAN_UI_LIST;
                            wifi_ap_list_print(&ap_req, false);
                            wifi_ap_list_print(&ap_req, true);
                            continue;
                        }

                        wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
                        exit_scan = 1U;
                        continue;
                    }
                    else
                    {
                        /* If STA list is empty (e.g. "No STA found"), BACK exits scan
                         * while keeping the currently selected AP information. */
                        if ((ui_mode == WIFI_SCAN_UI_LIST) &&
                            (list_count[WIFI_SCAN_MODE_STA_LIST] == 0U))
                        {
                            ap_list = ap_req.u.wifi_ap_scan.out_list;
                            if ((ap_list != NULL) && (selected_id[0] < list_count[WIFI_SCAN_MODE_AP_LIST]))
                            {
                                memcpy(&wifitarget->ap_inf, &ap_list[selected_id[0]], sizeof(wifi_scanlist_t));
                                memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf));
                            }

                            wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
                            exit_scan = 1U;
                            continue;
                        }

                        if (ui_mode == WIFI_SCAN_UI_TARGET_CONFIRM)
                        {
                            request_restart_scan = 1U;
                            continue;
                        }

                        scan_mode = WIFI_SCAN_MODE_AP_LIST;
                        ui_mode = WIFI_SCAN_UI_LIST;
                        wifi_station_list_print(NULL, false);
                        wifi_ap_list_print(&ap_req, false);
                        wifi_ap_list_print(&ap_req, true);
                        continue;
                    }

                } // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
                else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // go up?
                {
                    if (scan_mode == WIFI_SCAN_MODE_AP_LIST)
                    {
                        if (ui_mode != WIFI_SCAN_UI_LIST)
                        {
                            continue;
                        }

                        if ( list_count[scan_mode] )
                        {
                            if (selected_id[scan_mode] == 0)
                                selected_id[scan_mode] = (uint16_t)(list_count[scan_mode] - 1);
                            else
                                selected_id[scan_mode]--;

                            wifi_ap_list_print(&ap_req, true);
                        }
                    }
                    else
                    {
                        if (ui_mode == WIFI_SCAN_UI_TARGET_CONFIRM)
                        {
                            continue;
                        }

                        if ( list_count[scan_mode] )
                        {
                            if (selected_id[scan_mode] == 0)
                                selected_id[scan_mode] = (uint16_t)(list_count[scan_mode] - 1);
                            else
                                selected_id[scan_mode]--;

                            wifi_station_list_print(&sta_req, true);
                        }
                    }
                }
                else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // go down?
                {
                    if (scan_mode == WIFI_SCAN_MODE_AP_LIST)
                    {
                        if (ui_mode != WIFI_SCAN_UI_LIST)
                        {
                            continue;
                        }

                        if ( list_count[scan_mode] )
                        {
                            selected_id[scan_mode]++;
                            if (selected_id[scan_mode] >= list_count[scan_mode])
                                selected_id[scan_mode] = 0;

                            wifi_ap_list_print(&ap_req, false);
                        }
                    }
                    else
                    {
                        if (ui_mode == WIFI_SCAN_UI_TARGET_CONFIRM)
                        {
                            continue;
                        }

                        if ( list_count[scan_mode] )
                        {
                            selected_id[scan_mode]++;
                            if (selected_id[scan_mode] >= list_count[scan_mode])
                                selected_id[scan_mode] = 0;

                            wifi_station_list_print(&sta_req, false);
                        }
                    }
                }
                else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // Select?
                {
                    if (scan_mode == WIFI_SCAN_MODE_AP_LIST)
                    {
                        if (ui_mode != WIFI_SCAN_UI_LIST)
                        {
                            (void)wifi_scan_run_sta_scan(&ap_req,
                                                         &sta_req,
                                                         list_count,
                                                         selected_id,
                                                         &scan_mode,
                                                         &ui_mode);
                            continue;
                        }

                        if (list_count[scan_mode])
                        {
                            char sel_resp[64] = {0};
                            snprintf(select_cmd, sizeof(select_cmd), "select -a %u\r\n", (unsigned int)selected_id[scan_mode]);
                            if (!esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100))
                            {
                                //M1_LOG_E(M1_LOGDB_TAG, "Failed to send cmd to ESP32: %s\r\n", select_cmd);
                            }
                            else
                            {
                                (void)esp32_uart_read_until_prompt(sel_resp, sizeof(sel_resp), 1000, ">> ");
                                if (!m1_wifi_select_response_ok(sel_resp))
                                {
                                    //M1_LOG_W(M1_LOGDB_TAG, "select -a rejected: '%s'\r\n", sel_resp);
                                    m1_u8g2_firstpage();
                                    u8g2_DrawStr(&m1_u8g2, 2, 15, "Select failed");
                                    u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING,
                                                 (sel_resp[0] != '\0') ? sel_resp : "No reply");
                                    m1_u8g2_nextpage();
                                    osDelay(1200);
                                    wifi_ap_list_print(&ap_req, false);
                                    wifi_ap_list_print(&ap_req, true);
                                    continue;
                                }

                                m1_u8g2_firstpage();
                                snprintf(ui_msg, sizeof(ui_msg), "Select AP: %02u", (unsigned int)selected_id[scan_mode]);
                                u8g2_DrawStr(&m1_u8g2, 6, 15, ui_msg);
                                u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
                                m1_u8g2_nextpage();
                                osDelay(1000);

                                (void)wifi_scan_run_sta_scan(&ap_req,
                                                             &sta_req,
                                                             list_count,
                                                             selected_id,
                                                             &scan_mode,
                                                             &ui_mode);
                                continue;
                            }
                        }
                    }
                    else
                    {
                        if (!list_count[scan_mode])
                        {
                            scan_mode = WIFI_SCAN_MODE_AP_LIST;
                            ui_mode = WIFI_SCAN_UI_LIST;
                            wifi_station_list_print(NULL, false);
                            wifi_ap_list_print(&ap_req, false);
                            wifi_ap_list_print(&ap_req, true);
                            continue;
                        }

                        if (ui_mode == WIFI_SCAN_UI_TARGET_CONFIRM)
                        {
                            ap_list = ap_req.u.wifi_ap_scan.out_list;
                            sta_list = sta_req.u.wifi_ap_scan.out_list;
                            if (!ap_list || !sta_list)
                            {
                                request_restart_scan = 1U;
                                continue;
                            }

                            memcpy(&wifitarget->ap_inf, &ap_list[selected_id[0]], sizeof(wifi_scanlist_t));
                            memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf));
                            strncpy((char *)wifitarget->sta_inf.bssid,
                                    (const char *)sta_list[selected_id[1]].bssid,
                                    BSSID_STR_SIZE - 1);
                            wifitarget->sta_inf.rssi = sta_list[selected_id[1]].rssi;

                            wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
                            exit_scan = 1U;
                            continue;
                        }

                        if ( list_count[scan_mode] )
                        {
                            selected_id[1] = selected_id[scan_mode];
                            ap_list = ap_req.u.wifi_ap_scan.out_list;
                            sta_list = sta_req.u.wifi_ap_scan.out_list;
                            if (!ap_list || !sta_list)
                            {
                                request_restart_scan = 1U;
                                continue;
                            }

                            snprintf(select_cmd, sizeof(select_cmd), "select -s %u\r\n", (unsigned int)selected_id[1]);
                            if (!esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100))
                            {
                                //M1_LOG_E(M1_LOGDB_TAG, "Failed to send cmd to ESP32: %s\r\n", select_cmd);
                                request_restart_scan = 1U;
                                continue;
                            }
                            {
                                char sel_resp[64] = {0};
                                (void)esp32_uart_read_until_prompt(sel_resp, sizeof(sel_resp), 1000, ">> ");
                                if (!m1_wifi_select_response_ok(sel_resp))
                                {
                                    //M1_LOG_W(M1_LOGDB_TAG, "select -s rejected: '%s'\r\n", sel_resp);
                                    m1_u8g2_firstpage();
                                    u8g2_DrawStr(&m1_u8g2, 2, 15, "Select failed");
                                    u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING,
                                                 (sel_resp[0] != '\0') ? sel_resp : "No reply");
                                    m1_u8g2_nextpage();
                                    osDelay(1200);
                                    wifi_station_list_print(&sta_req, false);
                                    wifi_station_list_print(&sta_req, true);
                                    continue;
                                }
                            }

                            y_offset = M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT;

                            m1_u8g2_firstpage();
                            u8g2_DrawStr(&m1_u8g2, 2, y_offset, "Target Confirmed?");

                            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

                            snprintf(ui_msg, sizeof(ui_msg), "AP: %s", (const char *)ap_list[selected_id[0]].ssid);
                            y_offset += M1_GUI_FONT_HEIGHT;
                            u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);

                            snprintf(ui_msg, sizeof(ui_msg), "    %s", (const char *)ap_list[selected_id[0]].bssid);
                            y_offset += M1_GUI_FONT_HEIGHT;
                            u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);

                            snprintf(ui_msg, sizeof(ui_msg), "STA: %s", (const char *)sta_list[selected_id[1]].bssid);
                            y_offset += M1_GUI_FONT_HEIGHT;
                            u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);

                            /* Identification-hierarchy label: vendor (from OUI) +
                             * MAC suffix, or the neutral "Device" fallback. Never
                             * an invented hostname/product name. */
                            (void)m1_wifi_format_client_label((const char *)sta_list[selected_id[1]].bssid,
                                                              ui_msg, sizeof(ui_msg));
                            y_offset += M1_GUI_FONT_HEIGHT;
                            u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);
                            m1_u8g2_nextpage();

                            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

                            ui_mode = WIFI_SCAN_UI_TARGET_CONFIRM;
                        }
                    }
                }

            } // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
            else
            {
                ; // Do other things for this task
            }
        } // if (ret==pdTRUE)
    } // while (1 ) // Main loop of this task

    wifi_scan_restore_usbcdc_mode_if_needed(prev_usbcdc_mode, restore_usbcdc_mode);

} // void wifi_scan_ap(void)



/*============================================================================*/
/**
  * @brief Displays all scanned AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)
{
	static uint16_t i;
	static wifi_ap_scan_list_t *w_scan_p;
	static wifi_scanlist_t *list;
	static bool init_done = false;
    char prn_msg[26];
	uint8_t y_offset;

	if ( !app_resp && !up_dir ) // reset condition?
	{
		init_done = false;
		return 0;
	} // if ( !app_resp && !up_dir )

	if ( !init_done )
	{
		init_done = true;
		w_scan_p = &app_resp->u.wifi_ap_scan;
		list = w_scan_p->out_list;

		if (!w_scan_p->count)
		{
			strcpy(prn_msg, "No AP. Try again?");
			//M1_LOG_I(M1_LOGDB_TAG, "No AP found\r\n");
			init_done = false;
		}
		else if (!list)
		{
			strcpy(prn_msg, "Try again!");
			//M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned AP list\r\n");
			init_done = false;
		}
		else
		{
			//M1_LOG_I(M1_LOGDB_TAG, "Number of available APs is %d\r\n", w_scan_p->count);
		}

		if ( !init_done )
		{
			u8g2_DrawStr(&m1_u8g2, 6, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
			m1_u8g2_nextpage(); // Update display RAM
			return 0;
		}
		// Display first AP in the list
		i = 1;
		up_dir = true; // Overwrite the up_dir for the AP to be displayed for the first time
	} // if ( !init_done )

	if ( up_dir )
	{
		if ( i )
			i--;
		else
			i = w_scan_p->count-1; // roll over
	}
	else
	{
		i++;
		if ( i >= w_scan_p->count )
			i = 0; // roll over
	}

	m1_u8g2_firstpage();
	if (g_wifi_list_title)
	{
		/* Guided "Choose Network" heading: proven readable submenu font so the
		 * whole word fits (the wider default font overran into the counter). */
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, g_wifi_list_title);
	}
	else
	{
		u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Total AP:");
	}

    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 30, M1_GUI_ROW_SPACING*2, 8, 8, arrowleft_8x8);
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 15, M1_GUI_ROW_SPACING*2, 8, 8, arrowright_8x8);

	sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
	if (!g_wifi_list_title)
	{
		u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 70, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
	}
	else
	{
		/* Right-align the position counter just left of the arrows, and only if
		 * it clears the heading (measured) so the heading is never overlapped. */
		u8g2_uint_t hw = u8g2_GetStrWidth(&m1_u8g2, g_wifi_list_title);
		u8g2_uint_t cw = u8g2_GetStrWidth(&m1_u8g2, prn_msg);
		u8g2_uint_t cx = (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 32 - cw);
		if ((u8g2_uint_t)(2 + hw + 6) <= cx)
		{
			u8g2_DrawStr(&m1_u8g2, cx, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
		}
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

	/* Even 10px rhythm; start at 22 so the 5th line (Auth) ends at y=62 and does
	 * not clip the bottom edge (was y=64). */
	y_offset = 14 + M1_GUI_FONT_HEIGHT - 2;
	// Draw text
	if ( list[i].ssid[0]==0x00 ) // Hidden SSID?
        strcpy(prn_msg, "SSID: *hidden*");
	else
	{
	    strcpy(prn_msg, "SSID: ");
	    strncpy(&prn_msg[6], list[i].ssid, M1_LCD_DISPLAY_WIDTH/M1_SUB_MENU_FONT_WIDTH-6);
	}
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	y_offset += M1_GUI_FONT_HEIGHT;
    sprintf(prn_msg, "BSSID: %s", list[i].bssid);
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "RSSI: %ddBm", list[i].rssi);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "Channel: %d", list[i].channel);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "Auth mode: %s", wifi_auth_mode_to_str(list[i].encryption_mode));
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	m1_u8g2_nextpage(); // Update display RAM

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	//M1_LOG_D(M1_LOGDB_TAG, "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode \"%s\" \r\n",\
						i, list[i].ssid, list[i].bssid, list[i].rssi, list[i].channel, wifi_auth_mode_to_str(list[i].encryption_mode));

	return w_scan_p->count;
} // static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)



/*============================================================================*/
/**
  * @brief Displays all scanned Station list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t wifi_station_list_print(ctrl_cmd_t *app_resp, bool up_dir)
{
    static uint16_t i;
    static wifi_ap_scan_list_t *w_scan_p;
    static wifi_scanlist_t *list;
    static bool init_done = false;
    char prn_msg[26];
    uint8_t y_offset;

    if ( !app_resp && !up_dir ) // reset condition?
    {
        init_done = false;
        return 0;
    } // if ( !app_resp && !up_dir )

    if ( !init_done )
    {
        init_done = true;
        w_scan_p = &app_resp->u.wifi_ap_scan;
        list = w_scan_p->out_list;

        if (!w_scan_p->count)
        {
            strcpy(prn_msg, "No STA. Try again?");
            //M1_LOG_I(M1_LOGDB_TAG, "No STA found\r\n");
            init_done = false;
        }
        else if (!list)
        {
            strcpy(prn_msg, "Try again!");
            //M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned STA list\r\n");
            init_done = false;
        }
        else
        {
            //M1_LOG_I(M1_LOGDB_TAG, "Number of available STAs is %d\r\n", w_scan_p->count);
        }

        if ( !init_done )
        {
            m1_u8g2_firstpage();
            u8g2_DrawStr(&m1_u8g2, 5, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
            m1_u8g2_nextpage(); // Update display RAM
            return 0;
        }
        // Display first AP in the list
        i = 1;
        up_dir = true; // Overwrite the up_dir for the AP to be displayed for the first time
    } // if ( !init_done )

    if ( up_dir )
    {
        if ( i )
            i--;
        else
            i = w_scan_p->count-1; // roll over
    }
    else
    {
        i++;
        if ( i >= w_scan_p->count )
            i = 0; // roll over
    }

    m1_u8g2_firstpage();
    if (g_wifi_list_title)
    {
        /* Guided "Choose Client" heading: proven readable submenu font. */
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, g_wifi_list_title);
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Total STA:");
    }

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    if (!g_wifi_list_title)
    {
        /* default Networks view: total-count number after the "Total STA: " label */
        sprintf(prn_msg, "%d", w_scan_p->count);
        u8g2_DrawStr(&m1_u8g2, 2 + strlen("Total STA: ")*M1_GUI_FONT_WIDTH + 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
    }

    sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current STA
    if (!g_wifi_list_title)
    {
        u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6*M1_GUI_FONT_WIDTH, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
    }
    else
    {
        /* position counter right-aligned, drawn only if it clears the heading */
        u8g2_uint_t hw = u8g2_GetStrWidth(&m1_u8g2, g_wifi_list_title);
        u8g2_uint_t cw = u8g2_GetStrWidth(&m1_u8g2, prn_msg);
        u8g2_uint_t cx = (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 4 - cw);
        if ((u8g2_uint_t)(2 + hw + 6) <= cx)
        {
            u8g2_DrawStr(&m1_u8g2, cx, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
        }
    }

    y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
    // Draw text (network SSID when set by the guided client-select screen)
    if (g_wifi_list_subtitle)
    {
        snprintf(prn_msg, sizeof(prn_msg), "%.24s", g_wifi_list_subtitle);
    }
    else
    {
        strcpy(prn_msg, "Station info");
    }
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

    /* Primary identification line: vendor (from OUI) + MAC suffix, or the
     * neutral "Device" fallback -- never a bare full MAC as the headline,
     * and never an invented hostname/product name (see m1_wifi_client_id.h). */
    y_offset += M1_GUI_FONT_HEIGHT;
    (void)m1_wifi_format_client_label((const char *)list[i].bssid, prn_msg, sizeof(prn_msg));
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

    /* Secondary detail: full MAC, for unambiguous identification. */
    y_offset += M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
    sprintf(prn_msg, "MAC: %s", list[i].bssid);
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

    /* Secondary detail: real parsed RSSI (previously always 0 -- see
     * parse_wifi_list_s_lines()/parse_wifi_sta_rssi() above). */
    y_offset += M1_GUI_FONT_HEIGHT;
    sprintf(prn_msg, "RSSI: %ddBm", list[i].rssi);
    u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

    m1_u8g2_nextpage(); // Update display RAM

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    //M1_LOG_D(M1_LOGDB_TAG, "STA ID %02d) sta mac \"%s\" rssi \"%d\"\r\n",\
                        i, list[i].bssid, list[i].rssi);

    return w_scan_p->count;
} // static uint16_t wifi_station_list_print(ctrl_cmd_t *app_resp, bool up_dir)



/*============================================================================*/
/**
  * @brief Starts deauth attack
  * @param
  * @retval
  */
/*============================================================================*/
static wifi_deauth_exit_t wifi_deauth(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    const char *deauth_cmd = "deauth\r"; /* \r only: \r\n sends two terminators; the \n arrives as an empty line while deauth_running=true and immediately stops the attack */
    const char *stop_cmd = "stop\r\n";
    char rx_accum[128] = {0};
    uint16_t rx_accum_len = 0;
    char last_rate_msg[64] = {0};
    char reject_msg[64] = {0};
    uint8_t rejected = 0U;

    /* Graphic work starts here */
    //M1_LOG_W(M1_LOGDB_TAG, ">>> wifi_deauth START <<<\r\n");
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    wifitarget = &wifi_target_storage;
    /* Low-level safety: never transmit unless a valid AP and exactly one client are
     * selected. The guided Deauth Client flow guarantees this before calling here;
     * this guard also blocks transmission if this helper is entered in any other state. */
    if ((wifitarget->ap_inf.ssid[0] == '\0') || (wifitarget->sta_inf.bssid[0] == '\0'))
    {
        wifi_deauth_show_status_message("No target selected", "Pick network+client", 1200U);
        return WIFI_DEAUTH_EXIT_NO_TARGET;
    }

    if ( !m1_esp32_get_init_status() )
    {
        m1_esp32_init();

        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
        u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
        m1_u8g2_nextpage();

        osDelay(3000);
    }

    /* Pre-stop any ongoing attack so 'deauth' always starts fresh (not toggles off). */
    //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth: sending stop to reset ESP32 state\r\n");
    (void)esp32_uart_write((const uint8_t *)stop_cmd, (uint16_t)strlen(stop_cmd), 100);
    {
        char pre_stop_resp[64] = {0};
        (void)esp32_uart_read_until_prompt(pre_stop_resp, sizeof(pre_stop_resp), 1000, ">> ");
        //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth: pre-stop resp='%s'\r\n", pre_stop_resp);
    }

    //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth: tx cmd='deauth' ap_ssid='%s' sta_bssid='%s'\r\n",
    //         (const char *)wifitarget->ap_inf.ssid, (const char *)wifitarget->sta_inf.bssid);
    if (!esp32_uart_write((const uint8_t *)deauth_cmd, (uint16_t)strlen(deauth_cmd), 100))
    {
        //M1_LOG_E(M1_LOGDB_TAG, "Failed to send cmd to ESP32: %s\r\n", deauth_cmd);
        wifi_deauth_show_status_message("deauth send fail", NULL, 1000U);
        return WIFI_DEAUTH_EXIT_SEND_FAIL;
    }

    wifi_deauth_draw_running_screen(wifitarget, NULL);

    /* Drop the launch key event and any stale UART echo before the live loop starts. */
    xQueueReset(main_q_hdl);
    while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE)
    {
        ;
    }
    m1_esp32_reset_buffer();

    /* Visual activity heartbeat (hardware report: "deauth doesn't work at
     * all -- is this real or a stub?"). Root issue: the screen previously
     * only redrew when a UART line containing "pkts/s" was parsed out of
     * the ESP32's console echo. If that text never arrives -- wrong/absent
     * format, no console echo during raw-frame injection, timing -- the
     * screen sits on its very first draw forever, which is visually
     * identical to a hang or a no-op stub, whether or not real frames are
     * actually going out over the air. This spinner/elapsed-time line
     * redraws on every loop pass (~100-200ms) regardless of what (if
     * anything) the ESP32 has echoed back, so the user can always tell the
     * STM32 loop itself is alive and still issuing the attack -- it proves
     * loop liveness, not radio activity (that still needs the actual
     * hardware test / an RF capture to confirm), but it removes "is this
     * frozen or just quiet" as an open question. */
    {
        uint32_t start_tick = HAL_GetTick();
        uint32_t last_draw_tick = start_tick;
        uint8_t spin_idx = 0U;
        static const char spin_chars[4] = { '|', '/', '-', '\\' };

    while (1)
    {
        char uart_buf[64] = {0};
        char *line_start;
        char *p;
        uint16_t uart_len = 0;

        ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(100));
        if (ret == pdTRUE)
        {
            if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            {
                ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                if (ret == pdTRUE)
                {
                    if ((this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) ||
                        (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK))
                    {
                        //M1_LOG_W(M1_LOGDB_TAG, "wifi_deauth stop_tx: key event detected (OK/BACK)\r\n");
                        if (!esp32_uart_write((const uint8_t *)stop_cmd, (uint16_t)strlen(stop_cmd), 100))
                        {
                            //M1_LOG_E(M1_LOGDB_TAG, "Failed to send cmd to ESP32: %s\r\n", stop_cmd);
                        }

                        wifi_deauth_show_status_message("Stopping...", NULL, 300U);

                        xQueueReset(main_q_hdl);
                        break;
                    }
                }
            }
        }

        uart_len = esp32_uart_read_until_prompt(uart_buf, sizeof(uart_buf), 100, "\n");
        if (uart_len > 0)
        {
            if (uart_len >= sizeof(uart_buf))
            {
                uart_len = (uint16_t)(sizeof(uart_buf) - 1);
            }
            uart_buf[uart_len] = '\0';

            /* Debug: log raw received data for analysis */
            //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth rx_len=%u buf='%s'\r\n", (unsigned int)uart_len, uart_buf);

            if (uart_len >= (uint16_t)(sizeof(rx_accum) - 1))
            {
                memcpy(rx_accum,
                       &uart_buf[uart_len - (uint16_t)(sizeof(rx_accum) - 1)],
                       sizeof(rx_accum) - 1);
                rx_accum_len = (uint16_t)(sizeof(rx_accum) - 1);
                rx_accum[rx_accum_len] = '\0';
            }
            else
            {
                uint16_t free_len = (uint16_t)(sizeof(rx_accum) - 1) - rx_accum_len;
                if (uart_len > free_len)
                {
                    uint16_t drop_len = uart_len - free_len;
                    memmove(rx_accum, rx_accum + drop_len, rx_accum_len - drop_len);
                    rx_accum_len -= drop_len;
                }

                memcpy(rx_accum + rx_accum_len, uart_buf, uart_len);
                rx_accum_len += uart_len;
                rx_accum[rx_accum_len] = '\0';
            }

            line_start = rx_accum;
            for (p = rx_accum; ; p++)
            {
                if ((*p == '\r') || (*p == '\n') || (*p == '\0'))
                {
                    char saved = *p;
                    *p = '\0';

                    if (line_start[0] != '\0')
                    {
                        //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth parse line='%s'\r\n", line_start);
                        if (strstr(line_start, "pkts/s") != NULL)
                        {
                            snprintf(last_rate_msg, sizeof(last_rate_msg), "%s", line_start);
                            //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth rate_found='%s'\r\n", last_rate_msg);
                        }
                        /* ESP32-side rejection line (e.g. a stale/mismatched target
                         * table after an intervening rescan): "[!] Invalid deauth
                         * target selection. Scan/select targets first." Previously
                         * this was never recognized, so a rejected deauth just sat
                         * in the running screen forever with no packets ever sent
                         * and no error shown -- indistinguishable from a hang. */
                        else if (strstr(line_start, "[!]") != NULL)
                        {
                            //M1_LOG_W(M1_LOGDB_TAG, "wifi_deauth: ESP32 rejected deauth: '%s'\r\n", line_start);
                            snprintf(reject_msg, sizeof(reject_msg), "%s", line_start);
                            rejected = 1U;
                        }
                    }

                    if (saved == '\0')
                    {
                        break;
                    }

                    line_start = p + 1;
                }
            }

            if (line_start != rx_accum)
            {
                rx_accum_len = (uint16_t)strlen(line_start);
                memmove(rx_accum, line_start, rx_accum_len + 1);
            }

            if (rejected)
            {
                (void)esp32_uart_write((const uint8_t *)stop_cmd, (uint16_t)strlen(stop_cmd), 100);
                wifi_deauth_show_status_message("Deauth rejected", reject_msg, 1500U);
                xQueueReset(main_q_hdl);
                break;
            }

            if (last_rate_msg[0] != '\0')
            {
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth screen_update msg='%s'\r\n", last_rate_msg);
                wifi_deauth_draw_running_screen(wifitarget, last_rate_msg);
                last_draw_tick = HAL_GetTick();
            }
        }

        /* Heartbeat redraw: independent of whether this pass received any
         * UART data at all, so the spinner keeps visibly moving even during
         * total ESP32 console silence -- proof the loop is alive. Gated to
         * ~5Hz (not every ~100ms pass) so it doesn't fight the real
         * pkts/s-triggered redraw above for screen-refresh bandwidth. */
        {
            uint32_t now = HAL_GetTick();
            if ((now - last_draw_tick) >= 200U)
            {
                char activity[40];
                spin_idx = (uint8_t)((spin_idx + 1U) & 0x03U);
                if (last_rate_msg[0] != '\0')
                {
                    snprintf(activity, sizeof(activity), "%c %s", spin_chars[spin_idx], last_rate_msg);
                }
                else
                {
                    snprintf(activity, sizeof(activity), "%c Sending...  %lus",
                             spin_chars[spin_idx],
                             (unsigned long)((now - start_tick) / 1000U));
                }
                wifi_deauth_draw_running_screen(wifitarget, activity);
                last_draw_tick = now;
            }
        }
    }
    } /* heartbeat scope */

    return rejected ? WIFI_DEAUTH_EXIT_REJECTED : WIFI_DEAUTH_EXIT_STOPPED;
}


/*============================================================================*/
/* Guided single-client Deauth Client workflow.                               */
/* Self-contained targeting: performs its own network + client selection with */
/* the existing scan/list/select primitives, then runs the proven deauth loop */
/* (wifi_deauth). No new ESP32 commands and no change to the deauth protocol.  */
/*============================================================================*/

/* Vertical option menu with up to two info lines. LEFT/RIGHT move the cursor,
 * OK confirms (returns 0..nopt-1), BACK returns 0xFF. */
static uint8_t wifi_dc_menu(const char *title, const char *info1, const char *info2,
                            const char *const *opts, uint8_t nopt, uint8_t start_sel)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    uint8_t sel = start_sel;
    char buf[28];
    uint8_t k, y;

    /* Even vertical rhythm sized to the line count so lines are clearly separated
     * (no scrunched 3rd line) while the densest 7-line confirm still fits 128x64:
     *   <=6 lines -> 10px pitch + a 2px gap between target info and the action list
     *    7 lines  ->  9px pitch (no extra gap) so the last line clears the bottom. */
    {
        /* A trailing "Back" option is never rendered or selectable: the device
         * has a physical BACK button (wifi_dc_menu returns 0xFF, which every
         * caller's else-branch already treats as the back/cancel action). nvis =
         * the count of real, on-screen options. */
        uint8_t nvis = (uint8_t)((nopt > 0U && opts != NULL && strcmp(opts[nopt - 1U], "Back") == 0)
                                 ? (nopt - 1U) : nopt);
        uint8_t nlines = (uint8_t)((title ? 1 : 0) + (info1 ? 1 : 0) + (info2 ? 1 : 0) + nvis);
        uint8_t step = (nlines <= 6) ? 10U : 9U;
        uint8_t sep  = (nlines <= 6) ? 2U : 0U;
        if (nvis > 0U && sel >= nvis) { sel = 0U; }

    for (;;)
    {
        m1_u8g2_firstpage();
        y = step;
        if (title)
        {
            /* heading in the main-menu font, matching the other Wi-Fi feature screens */
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            snprintf(buf, sizeof(buf), "%.20s", title);
            u8g2_DrawStr(&m1_u8g2, 2, y, buf);
            y = (uint8_t)(y + step);
        }
        /* info + option lines stay in the small readable font (fits the dense
         * confirm screens; keeps SSID/BSSID/MAC technical detail legible) */
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        if (info1) { snprintf(buf, sizeof(buf), "%.26s", info1); u8g2_DrawStr(&m1_u8g2, 2, y, buf); y = (uint8_t)(y + step); }
        if (info2) { snprintf(buf, sizeof(buf), "%.26s", info2); u8g2_DrawStr(&m1_u8g2, 2, y, buf); y = (uint8_t)(y + step); }
        if ((info1 || info2) && (nvis > 0U)) { y = (uint8_t)(y + sep); } /* separate target info from actions */
        for (k = 0; k < nvis; k++)
        {
            snprintf(buf, sizeof(buf), "%c %s", (k == sel) ? '>' : ' ', opts[k]);
            u8g2_DrawStr(&m1_u8g2, 2, y, buf);
            y = (uint8_t)(y + step);
        }
        m1_u8g2_nextpage();
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            return 0xFFU;
        }
        else if ((nvis > 0U) && (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK))
        {
            /* previous item; wrap first -> last */
            sel = (sel == 0U) ? (uint8_t)(nvis - 1U) : (uint8_t)(sel - 1U);
        }
        else if ((nvis > 0U) && (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
        {
            /* next item; wrap last -> first */
            sel = (uint8_t)(((sel + 1U) >= nvis) ? 0U : (sel + 1U));
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            return (nvis == 0U) ? 0xFFU : sel;   /* message-only dialog: OK also dismisses */
        }
        /* LEFT/RIGHT intentionally do nothing on these vertical choice screens */
    }
    }
}

void wifi_ui_draw_scanning(const char *msg)
{
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 6, 15, msg);
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
    m1_u8g2_nextpage();
}

/*----- Unified network/device result rendering + browsing (Networks & Deauth) ----*/

/* Wi-Fi CENTER-action footer bar. Replicates the proven Sub-GHz Start/Stop
 * treatment (rr_draw_bottom_bar): a solid full-width bar with the action word +
 * CENTER (target) icon rendered inverted (punched out of the bar). Geometry is
 * copied from the Sub-GHz reference: bar y=52, h=12; courB08 bold font; 10x10
 * target icon at the bar top; text baseline at bar_y+9 = 61. Word+icon are kept
 * grouped and right-aligned (approved "word + CENTER icon" order). Wi-Fi-local:
 * uses the shared u8g2 font macro and the existing icon, with no Sub-GHz include
 * and no new shared framework. */
#define WIFI_FOOTER_Y   52
#define WIFI_FOOTER_H   12
static void wifi_footer_bar(const char *left_hint, const char *label)
{
    wifi_footer_bar_ex(left_hint, label, 0U);
}

/* Same black MonstaTek footer bar as wifi_footer_bar() (identical box/color-
 * invert/font handling -- this is the one, only footer implementation; the
 * plain wifi_footer_bar() above is just a thin wrapper preserving every
 * existing call site's exact prior rendering), extended with an optional
 * LEFT/RIGHT paging hint for one-item-per-screen pickers that browse with
 * LEFT/RIGHT (Deauth's Select Network / Select Device). Reuses the same
 * triangle primitive already established here for the DOWN-button "Rescan"
 * hint (u8g2_DrawTriangle, same size/baseline) -- just rotated to point left
 * and right instead of down, so this stays visually one family of icon, not
 * a new footer design. Two small arrows sit together at the LEFT of the
 * bar (left_hint's usual slot); the existing right-side CENTER label+target-
 * icon slot is unchanged. show_lr_arrows is the caller's responsibility to
 * set only when there is more than one item to browse -- see wifi_dc_pick_
 * network()/wifi_dc_pick_device(), which pass 0 whenever count<=1 so a
 * single-result screen never implies LEFT/RIGHT will browse anything. */
static void wifi_footer_bar_ex(const char *left_hint, const char *label, uint8_t show_lr_arrows)
{
    u8g2_uint_t ix = (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 2 - 10); /* icon far right, 2px right pad */
    u8g2_uint_t lw;
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);           /* FG: draw the solid bar */
    u8g2_DrawBox(&m1_u8g2, 0, WIFI_FOOTER_Y, M1_LCD_DISPLAY_WIDTH, WIFI_FOOTER_H);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);            /* BG: content punched out (inverted) */
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);            /* match the NFC menu black-bar font */
    if (label)
    {
        u8g2_DrawXBMP(&m1_u8g2, ix, WIFI_FOOTER_Y, 10, 10, target_10x10);
        lw = u8g2_GetStrWidth(&m1_u8g2, label);
        u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(ix - 2 - lw), WIFI_FOOTER_Y + 9, label); /* 2px word->icon pad */
    }
    if (show_lr_arrows)
    {
        /* Left-pointing triangle (flat right edge, point left) then a
         * right-pointing triangle (flat left edge, point right) right next
         * to it -- same 6px-wide/5px-tall footprint as the existing down-
         * chevron, just rotated. Placed in the left_hint slot; left_hint
         * itself is never used together with the arrows by any current
         * caller (Deauth's pickers don't pass both), so there's no overlap. */
        u8g2_DrawTriangle(&m1_u8g2, 8, WIFI_FOOTER_Y + 1, 8, WIFI_FOOTER_Y + 7, 2, WIFI_FOOTER_Y + 4);  /* left */
        u8g2_DrawTriangle(&m1_u8g2, 12, WIFI_FOOTER_Y + 1, 12, WIFI_FOOTER_Y + 7, 18, WIFI_FOOTER_Y + 4); /* right */
    }
    /* Optional lower-LEFT control hint (the DOWN-button Rescan on AP lists):
     * a small down-chevron glyph then the label. */
    if (left_hint)
    {
        u8g2_DrawTriangle(&m1_u8g2, 2, WIFI_FOOTER_Y + 2, 8, WIFI_FOOTER_Y + 2, 5, WIFI_FOOTER_Y + 7); /* down */
        u8g2_DrawStr(&m1_u8g2, 11, WIFI_FOOTER_Y + 9, left_hint);
    }
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);           /* restore default draw color */
}

/* One result per screen: heading + right-aligned "X/Y", 3 content lines, an
 * optional SAVED tag (right-aligned above the footer), and the CENTER-action
 * footer bar. Content baselines (<=50) sit above the reserved footer (y>=52). */
static void wifi_card_draw(const char *heading, uint16_t sel, uint16_t count,
                           const char *l1, const char *l2, const char *l3,
                           const char *left_foot, const char *ok_label,
                           const char *foot_left_hint)
{
    wifi_card_draw_ex(heading, sel, count, l1, l2, l3, left_foot, ok_label, foot_left_hint, 0U);
}

/* Same as wifi_card_draw() (thin wrapper above preserves every existing call
 * site's exact prior rendering), extended with show_lr_arrows for Deauth's
 * one-item-per-screen LEFT/RIGHT pickers -- see wifi_footer_bar_ex(). */
static void wifi_card_draw_ex(const char *heading, uint16_t sel, uint16_t count,
                              const char *l1, const char *l2, const char *l3,
                              const char *left_foot, const char *ok_label,
                              const char *foot_left_hint, uint8_t show_lr_arrows)
{
    char nn[12];
    u8g2_uint_t cw;
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, heading);
    snprintf(nn, sizeof(nn), "%u/%u", (unsigned int)(sel + 1U), (unsigned int)count);
    cw = u8g2_GetStrWidth(&m1_u8g2, nn);
    u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 4 - cw), 11, nn);
    if (l1) { u8g2_DrawStr(&m1_u8g2, 2, 26, l1); }
    if (l2) { u8g2_DrawStr(&m1_u8g2, 2, 38, l2); }
    if (l3) { u8g2_DrawStr(&m1_u8g2, 2, 50, l3); }
    /* SAVED indicator: right-aligned on the l3 row, above the footer bar. */
    if (left_foot)
    {
        u8g2_uint_t fw = u8g2_GetStrWidth(&m1_u8g2, left_foot);
        u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 2 - fw), 50, left_foot);
    }
    if (ok_label || foot_left_hint || show_lr_arrows) { wifi_footer_bar_ex(foot_left_hint, ok_label, show_lr_arrows); }
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

void wifi_ui_draw_ap_card(const char *heading, uint16_t sel, uint16_t count,
                          const char *ssid, const char *bssid, int rssi,
                          int channel, const char *security,
                          const char *status, const char *ok_label,
                          const char *refresh_hint)
{
    char l1[28], l2[28], l3[28];

    snprintf(l1, sizeof(l1), "%.20s", (ssid && ssid[0]) ? ssid : "Hidden Network");
    snprintf(l2, sizeof(l2), "%d dBm", rssi);
    snprintf(l3, sizeof(l3), "Ch %d  %.6s", channel,
             (security && security[0]) ? security : "Unknown");
    (void)bssid; /* The caller retains BSSID as the stable AP identity. */
    wifi_card_draw(heading, sel, count, l1, l2, l3, status, ok_label, refresh_hint);
}

/* Browse a scan list one result per screen. is_device=0 -> network card
 * (SSID / RSSI / Ch+Sec); is_device=1 -> device card (network SSID / MAC / RSSI).
 * LEFT/RIGHT page (wrap), CENTER performs the action, BACK returns. The
 * CENTER/OK button icon is shown bottom-right. *sel is in/out so callers
 * keep position across actions. Returns 0 = CENTER, 1 = BACK.
 *
 * is_device=0 (AP lists) only: UP/DOWN is the visible manual-refresh
 * control for the shared AP cache (wifi_session_cache_get()) -- returns 2,
 * which the caller must treat as "force a real rescan and reload the list"
 * (wifi_ap_scan_list_cached(req, true) then redraw), not as a selection.
 * The list screens show a lower-LEFT "Rescan" hint (LEFT button) and the
 * control is actually visible, not just live. Device/client lists
 * (is_device=1) never return 2 -- UP/DOWN does nothing there, since the
 * client scan is always fresh already and has nothing to "refresh". */
static uint8_t wifi_browse(const wifi_scanlist_t *list, uint16_t count, uint8_t is_device,
                           const char *net_ssid, const char *heading, uint8_t mark_saved,
                           const char *ok_label, uint16_t *sel)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    char l1[28], l2[28], l3[28];

    if (*sel >= count) { *sel = 0; }
    for (;;)
    {
        const char *left_foot = NULL;
        const wifi_scanlist_t *e = &list[*sel];
        if (mark_saved)
        {
            char up[18];
            wifi_bssid_to_upper(up, (const char *)e->bssid);
            if (wifi_saved_find(up) >= 0) { left_foot = "SAVED"; } /* saved-network indicator */
        }
        if (is_device)
        {
            /* Identification hierarchy (primary line): known OUI vendor + MAC
             * suffix, else the neutral "Device" fallback -- never a bare full
             * MAC as the headline, never an invented hostname/product name.
             * Full MAC and real RSSI remain visible as secondary detail. */
            (void)m1_wifi_format_client_label((const char *)e->bssid, l1, sizeof(l1));
            snprintf(l2, sizeof(l2), "%.20s", (net_ssid && net_ssid[0]) ? net_ssid : "Hidden Network");
            snprintf(l3, sizeof(l3), "%s  %ddBm", (const char *)e->bssid, e->rssi);
        }
        else
        {
            wifi_ui_draw_ap_card(heading, *sel, count,
                                 (const char *)e->ssid, (const char *)e->bssid,
                                 e->rssi, e->channel,
                                 wifi_auth_mode_to_str(e->encryption_mode),
                                 left_foot, ok_label, "Rescan");
        }
        if (is_device)
        {
            wifi_card_draw(heading, *sel, count, l1, l2, l3, left_foot, ok_label, NULL);
        }

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);
        /* Button map: LEFT/RIGHT page the list (horizontal scroll), OK = action
         * (Actions/Select), DOWN = Rescan on AP lists, physical BACK returns.
         * Rescan sits on DOWN so LEFT/RIGHT stay free for scrolling. */
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)       { return 1U; }
        else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)  { *sel = (*sel == 0U) ? (uint16_t)(count - 1U) : (uint16_t)(*sel - 1U); } /* previous, wrap */
        else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) { *sel = (uint16_t)(((*sel + 1U) >= count) ? 0U : (*sel + 1U)); }         /* next, wrap */
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)    { return 0U; }
        else if ((!is_device) && (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
        {
            return 2U; /* AP list: DOWN = Rescan */
        }
        /* UP unused here; on device lists DOWN is a no-op */
    }
}

/* select -a <net_idx> then scan/list the devices on that network into sta_req.
 * Shared by Networks (recon) and Deauth (targeting). Uses the existing STA scan
 * (scan -s / list -s) unchanged.
 *
 * Returns:
 *   device count (0..N)  on success -- sta_req is populated.
 *   0xFFFFU               scan -s/list -s itself failed (prompt timeout etc).
 *   0xFFFEU               the ESP32 rejected "select -a" (stale index /
 *                          AP-table mismatch -- see m1_wifi_select_response_ok()).
 *                          sel_err/sel_err_cap, if given, receive a short copy
 *                          of the ESP32's actual reply text for on-screen display.
 * A rejected select must never fall through to populating sta_req as if a
 * network were actually selected (root cause of the silent-hang Deauth bug). */
static uint16_t wifi_scan_devices(uint16_t net_idx, ctrl_cmd_t *sta_req, char *sel_err, size_t sel_err_cap)
{
    char cmd[32];
    char resp[64] = {0};

    wifi_ui_draw_scanning("Searching Devices...");
    snprintf(cmd, sizeof(cmd), "select -a %u\r\n", (unsigned int)net_idx);
    //M1_LOG_I(M1_LOGDB_TAG, "wifi_scan_devices: select cmd='%s'\r\n", cmd);
    (void)esp32_uart_write((const uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 1000, ">> ");
    //M1_LOG_I(M1_LOGDB_TAG, "wifi_scan_devices: select resp='%s'\r\n", resp);

    if (!m1_wifi_select_response_ok(resp))
    {
        //M1_LOG_W(M1_LOGDB_TAG, "wifi_scan_devices: select -a rejected: '%s'\r\n", resp);
        if ((sel_err != NULL) && (sel_err_cap > 0U))
        {
            snprintf(sel_err, sel_err_cap, "%s", (resp[0] != '\0') ? resp : "No reply");
        }
        return 0xFFFEU;
    }

    wifi_scan_free_lists(NULL, sta_req);
    sta_req->cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
    sta_req->msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
    if (wifi_ap_sta_scan_list(sta_req, 1) != SUCCESS) { return 0xFFFFU; }
    return (uint16_t)sta_req->u.wifi_ap_scan.count;
}

/* Detected-device details (Networks recon): network + full MAC + RSSI. BACK returns.
 * Recon only: never selects the device or sets any Deauth target. */
static void wifi_device_details(const char *net_ssid, const wifi_scanlist_t *dev)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    char buf[28];

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Device Details");
    snprintf(buf, sizeof(buf), "Network:%.14s", (net_ssid && net_ssid[0]) ? net_ssid : "Hidden Network");
    u8g2_DrawStr(&m1_u8g2, 2, 26, buf);
    snprintf(buf, sizeof(buf), "Device:%s", (const char *)dev->bssid);
    u8g2_DrawStr(&m1_u8g2, 2, 38, buf);
    snprintf(buf, sizeof(buf), "RSSI:%d dBm", dev->rssi);
    u8g2_DrawStr(&m1_u8g2, 2, 50, buf);
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    for (;;)
    {
        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return; }
    }
}

/* Deauth Client's own network picker (Part 2b): UP/DOWN navigate (wrap),
 * CENTER selects, BACK exits -- deliberately NOT the shared wifi_browse()
 * LEFT/RIGHT-paging control scheme, so Networks/Handshake (which still use
 * wifi_browse()) are untouched. One card per network:
 *   Select Network            i/n
 *   <SSID>
 *   <RSSI> dBm   CH <channel>
 * Returns 0 = CENTER (selection is *sel), 1 = BACK. */
static uint8_t wifi_dc_pick_network(const wifi_scanlist_t *list, uint16_t count, uint16_t *sel)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;

    if (*sel >= count) { *sel = 0; }
    for (;;)
    {
        const wifi_scanlist_t *e = &list[*sel];
        char l1[24];
        char l2[28];

        snprintf(l1, sizeof(l1), "%.20s", (e->ssid[0] != '\0') ? (const char *)e->ssid : "Hidden Network");
        snprintf(l2, sizeof(l2), "%d dBm   CH %d", e->rssi, e->channel);
        /* LEFT/RIGHT browse (one network per screen, matching wifi_browse()'s
         * established LEFT/RIGHT-page convention used elsewhere in this file);
         * UP/DOWN no longer move the selection here. Arrows only shown when
         * there's actually more than one network to browse. */
        wifi_card_draw_ex("Select Network", *sel, count, l1, l2, NULL, NULL, "Select", NULL,
                          (uint8_t)(count > 1U));

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return 1U; }
        else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)  /* previous, wrap */
        {
            *sel = (*sel == 0U) ? (uint16_t)(count - 1U) : (uint16_t)(*sel - 1U);
        }
        else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) /* next, wrap */
        {
            *sel = (uint16_t)(((*sel + 1U) >= count) ? 0U : (*sel + 1U));
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { return 0U; }
        /* UP/DOWN intentionally do nothing here -- no hidden controls. */
    }
}

/* Converts a raw wifi_scanlist_t array (MAC + RSSI is all this carries for
 * stations) into the parallel mac-string/RSSI arrays the pure picker module
 * (m1_wifi_dc_picker_apply_scan(), m1_wifi_client_id.h) consumes. Bounded by
 * M1_WIFI_DC_PICKER_MAX_ROWS -- same cap the picker itself enforces. */
static uint16_t wifi_dc_picker_snapshot(const wifi_scanlist_t *list, uint16_t count,
                                         char macs[][18], int *rssis, uint16_t cap)
{
    uint16_t n = (count > cap) ? cap : count;
    uint16_t i;

    for (i = 0; i < n; i++)
    {
        snprintf(macs[i], 18, "%s", (const char *)list[i].bssid);
        rssis[i] = list[i].rssi;
    }
    return n;
}

/* Deauth Client's own device picker (Part 2d/2e/2g, extended for Part 1 live
 * RSSI). Same UP/DOWN/CENTER/BACK scheme as wifi_dc_pick_network(). One card
 * per client:
 *   Select Device              i/n
 *   <vendor or "Device"> - XX:XX
 *   <RSSI> dBm   <category>   (or "(gone)  last <RSSI> dBm" -- Part 1)
 *   <full MAC>   (kept reachable here -- not repeated on the Confirm screen)
 *
 * Live refresh (Part 1): while idle in this loop, periodically re-issues
 * "scan -s"+"list -s" (via wifi_ap_sta_scan_list(sta_req, 1) -- no re-select,
 * the AP selection from wifi_scan_devices()/"select -a" persists across
 * repeated scan -s calls), gated by a plain HAL_GetTick() elapsed check each
 * loop pass -- the same "check elapsed ticks on every iteration" idiom
 * wifi_deauth()'s own button-poll loop already uses, not a new RTOS task/
 * timer. IMPORTANT: wifi_ap_sta_scan_list(sta_req, 1) is a SYNCHRONOUS call
 * that blocks this whole task for the ESP32's fixed station-scan sniff
 * window (esp32-side handle_scan_s sets duration_ms=5000 -- a hard ~5s
 * floor with the current ESP32 firmware, not a timeout ceiling) -- button
 * input is only read *between* scans (the xQueueReceive above), never
 * during one. M1_WIFI_DC_PICKER_REFRESH_MS therefore MUST stay meaningfully
 * larger than that ~5s floor: too small (an earlier build shipped 1000ms)
 * and the elapsed-check is satisfied again the instant a scan returns,
 * chaining scans back-to-back with only the ~150ms xQueueReceive gap
 * between them for a button press to land in -- which read as "can't
 * scroll" on hardware, since the odds of a click landing in that gap are
 * low. At M1_WIFI_DC_PICKER_REFRESH_MS below, there's a real several-
 * second idle window after each scan where this loop is fully responsive
 * at its normal 150ms polling granularity, at the cost of slower RSSI
 * refresh cadence -- an explicit, documented trade-off given the ESP32
 * scan floor is fixed in the current firmware (an async/background-task
 * redesign to poll input *during* a scan is a materially larger change,
 * out of scope here). Row identity/order is owned by the pure
 * m1_wifi_dc_picker_t module (MAC-keyed, freeze-on-first-input,
 * append-new-at-end, never-delete-on-disappear -- see m1_wifi_client_id.h);
 * this function only draws the picker's current rows and feeds it fresh
 * snapshots. sta_req is refreshed in place (its out_list/count reflect the
 * most recent "list -s" round) so the caller can re-locate the picked MAC's
 * *current* index for "select -s" after CENTER.
 * Returns 0 = CENTER (out_mac/out_rssi hold the selected client's identity,
 * captured by value -- never re-derived from a row/array index afterward),
 * 1 = BACK. */
static uint8_t wifi_dc_pick_device(ctrl_cmd_t *sta_req, char *out_mac, size_t out_mac_cap, int *out_rssi)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    m1_wifi_dc_picker_t picker;
    char macs[M1_WIFI_DC_PICKER_MAX_ROWS][18];
    int rssis[M1_WIFI_DC_PICKER_MAX_ROWS];
    uint16_t n;
    uint16_t vis_sel = 0U;
    uint32_t last_refresh;

    m1_wifi_dc_picker_init(&picker);
    n = wifi_dc_picker_snapshot(sta_req->u.wifi_ap_scan.out_list,
                                (uint16_t)sta_req->u.wifi_ap_scan.count,
                                macs, rssis, (uint16_t)M1_WIFI_DC_PICKER_MAX_ROWS);
    m1_wifi_dc_picker_apply_scan(&picker, macs, rssis, n); /* first population: sorted strongest-first */
    last_refresh = HAL_GetTick();

    for (;;)
    {
        BaseType_t ret;

        if (picker.count == 0U)
        {
            /* Caller only enters here with a non-empty initial scan -- this
             * only guards against every client disappearing while open. */
            wifi_card_draw("Select Device", 0, 0, "No Devices", "Not Seen", NULL, NULL, NULL, NULL);
        }
        else
        {
            const m1_wifi_dc_row_t *row;
            char l1[24];
            char l2[28];
            char l3[28];

            if (vis_sel >= picker.count) { vis_sel = (uint16_t)(picker.count - 1U); }
            row = &picker.rows[vis_sel];

            /* Identification hierarchy: l1 = vendor name or "Unknown Device"
             * (never a guessed product like "Samsung TV"). l2 = the FULL MAC
             * address (per explicit follow-up request: after searching
             * devices, the whole MAC must be visible on this card, not just
             * a shortened suffix -- row->mac is already the exact colon-
             * separated string, shown verbatim). l3 = live RSSI category
             * then dBm, e.g. "Strong  -51 dBm". */
            (void)m1_wifi_format_client_name(row->mac, l1, sizeof(l1));
            snprintf(l2, sizeof(l2), "%s", row->mac);
            if (row->present)
            {
                m1_wifi_rssi_category_t cat = m1_wifi_rssi_category(row->rssi);
                snprintf(l3, sizeof(l3), "%s  %d dBm", m1_wifi_rssi_category_str(cat), row->rssi);
            }
            else
            {
                /* Disappeared from the most recent refresh round -- kept on
                 * screen (never silently dropped/replaced) and visibly
                 * marked stale, in plain user-facing language rather than
                 * internal scan terminology. */
                snprintf(l3, sizeof(l3), "Not Seen  last %d dBm", row->rssi);
            }
            /* LEFT/RIGHT browse (one device per screen, matching wifi_browse()'s
             * established LEFT/RIGHT-page convention); UP/DOWN no longer move
             * the selection here. "Deauth" as the CENTER label since selecting
             * a device IS the final action on this screen -- no separate
             * confirm/start step. Arrows only shown when there's more than
             * one device to browse. */
            wifi_card_draw_ex("Select Device", vis_sel, picker.count, l1, l2, l3, NULL, "Deauth", NULL,
                              (uint8_t)(picker.count > 1U));
        }

        ret = xQueueReceive(main_q_hdl, &q, pdMS_TO_TICKS(150));
        if ((ret == pdTRUE) && (q.q_evt_type == Q_EVENT_KEYPAD) &&
            (xQueueReceive(button_events_q_hdl, &btn, 0) == pdTRUE))
        {
            if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return 1U; }
            else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)  /* previous, wrap */
            {
                m1_wifi_dc_picker_freeze(&picker);
                if (picker.count > 0U)
                {
                    vis_sel = (vis_sel == 0U) ? (uint16_t)(picker.count - 1U) : (uint16_t)(vis_sel - 1U);
                }
            }
            else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) /* next, wrap */
            {
                m1_wifi_dc_picker_freeze(&picker);
                if (picker.count > 0U)
                {
                    vis_sel = (uint16_t)(((vis_sel + 1U) >= picker.count) ? 0U : (vis_sel + 1U));
                }
            }
            else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                m1_wifi_dc_picker_freeze(&picker);
                if (picker.count > 0U)
                {
                    const m1_wifi_dc_row_t *sel_row = &picker.rows[vis_sel];
                    snprintf(out_mac, out_mac_cap, "%s", sel_row->mac);
                    *out_rssi = sel_row->rssi;
                    return 0U;
                }
            }
            /* UP/DOWN intentionally do nothing here -- no hidden controls. */
            continue; /* redraw immediately on a key event, don't wait out the refresh gate */
        }

        {
            uint32_t now = HAL_GetTick();
            if ((now - last_refresh) >= M1_WIFI_DC_PICKER_REFRESH_MS)
            {
                last_refresh = now;
                /* scan -s/list -s only -- no "select -a" re-issue. A failed
                 * round (UART hiccup/timeout) leaves the picker's existing
                 * rows exactly as they were; it never blanks the list or
                 * disturbs the highlight. */
                if (wifi_ap_sta_scan_list(sta_req, 1) == SUCCESS)
                {
                    n = wifi_dc_picker_snapshot(sta_req->u.wifi_ap_scan.out_list,
                                                (uint16_t)sta_req->u.wifi_ap_scan.count,
                                                macs, rssis, (uint16_t)M1_WIFI_DC_PICKER_MAX_ROWS);
                    m1_wifi_dc_picker_apply_scan(&picker, macs, rssis, n);
                }
            }
        }
    }
}

/* Selected Network screen (new linear flow, replaces the old PMF/
 * "compatibility" screen entirely -- there is no PMF/compatibility screen in
 * the new flow). Shows only what's genuinely known about the chosen AP
 * (SSID, channel, RSSI) and a single CENTER action to proceed to the device
 * scan. BACK returns to Select Network (no rescan -- the AP list already
 * loaded is still valid; only DC_NET_CHOOSE's own explicit Rescan control
 * forces a fresh scan).
 *
 * The prior PMF-inference line was removed in an earlier pass (verified:
 * ESP-IDF's wifi_ap_record_t, the ESP32 scan result struct, carries no PMF/
 * MFPC/MFPR/RSNXE field at all -- wifi_pmf_config_t only applies to a
 * *connection* config, never a scan result -- so there was never a real bit
 * to show). That finding still holds; this screen continues to omit PMF
 * rather than invent or infer a value.
 * Returns 1 = BACK, 0 = CENTER ("Scan Devices"). */
static uint8_t wifi_dc_selected_network_screen(const wifi_scanlist_t *ap)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    char buf[28];

    for (;;)
    {
        m1_u8g2_firstpage();
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
        snprintf(buf, sizeof(buf), "%.20s", (ap->ssid[0] != '\0') ? (const char *)ap->ssid : "Hidden Network");
        u8g2_DrawStr(&m1_u8g2, 2, 11, buf);

        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        snprintf(buf, sizeof(buf), "CH %d   %d dBm", ap->channel, ap->rssi);
        u8g2_DrawStr(&m1_u8g2, 2, 26, buf);
        wifi_footer_bar(NULL, "Scan Devices");
        m1_u8g2_nextpage();
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return 1U; }
        if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)   { return 0U; }
    }
}

void wifi_deauth_client(void)
{
    ctrl_cmd_t ap_req = CTRL_CMD_DEFAULT_REQ();
    ctrl_cmd_t sta_req = CTRL_CMD_DEFAULT_REQ();
    enCdcMode prev_usbcdc_mode;
    uint8_t restore_usbcdc_mode = 0;
    uint8_t ready;
    char select_cmd[32];
    char net_ssid[34];
    uint16_t dc_net_sel = 0;
    uint8_t opt;

    static const char *const opt_rescan[2]    = { "Rescan", "Back" };
    static const char *const opt_nodev_single[1] = { "Rescan" };

    /* New linear flow (full replacement pass): no DC_PRESELECT/"Use Selected
     * Network" gate, no DC_COMPAT/PMF screen (folded into DC_NET_SELECTED,
     * which shows only genuinely-known AP facts), and no DC_CONFIRM screen --
     * CENTER on a device in DC_CLIENT_CHOOSE is itself the final action:
     * validate (AP still valid, client MAC still resolvable, select-response
     * ok) then transmit directly, going straight to DC_RUN. Stopping (CENTER
     * or BACK) from DC_RUN returns straight to DC_CLIENT_CHOOSE -- there is
     * no separate "Deauth Stopped" screen.
     * DC_NET_SCAN is always the entry state below. The shared AP-cache reuse
     * (wifi_ap_scan_list_cached()) still applies internally, so switching
     * here from Networks/Handshake right after a scan there doesn't force a
     * redundant rescan; the user just never sees an extra menu screen for it. */
    enum {
        DC_NET_SCAN, DC_NET_CHOOSE, DC_NET_SELECTED,
        DC_CLIENT_SCAN, DC_CLIENT_CHOOSE, DC_RUN, DC_DONE
    } state;

    wifitarget = &wifi_target_storage;
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    net_ssid[0] = '\0';

    ready = wifi_survey_prepare_esp32(&prev_usbcdc_mode, &restore_usbcdc_mode);
    if (!ready)
    {
        wifi_deauth_show_status_message("ESP32 not ready", "Try bridge reset", 1500U);
        state = DC_DONE;
    }
    else
    {
        state = DC_NET_SCAN;
    }

    while (state != DC_DONE)
    {
        switch (state)
        {
        case DC_NET_SCAN:
        {
            wifi_ui_draw_scanning("Scanning Networks...");
            wifi_scan_free_lists(&ap_req, NULL);
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
            /* Entry AP-list load: REUSE the shared AP cache if it's fresh (a
             * scan just done in another Wi-Fi tool), otherwise fall through to
             * a real scan. The genuinely-explicit "refresh" control in
             * DC_NET_CHOOSE invalidates the cache first (below), so it still
             * forces a fresh scan. This is what lets one scan serve every
             * Wi-Fi tool instead of a redundant scan per tool. */
            if (wifi_ap_scan_list_cached(&ap_req, false) != SUCCESS)
            {
                opt = wifi_dc_menu(NULL, "Network scan failed", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? DC_NET_SCAN : DC_DONE;
            }
            else
            {
                dc_net_sel = 0;
                state = DC_NET_CHOOSE;
            }
            break;
        }

        case DC_NET_CHOOSE:
        {
            uint16_t ncount = (uint16_t)ap_req.u.wifi_ap_scan.count;
            if (ncount == 0U)
            {
                opt = wifi_dc_menu(NULL, "No networks found", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? DC_NET_SCAN : DC_DONE;
                break;
            }
            /* Deauth's own picker (Part 2b): UP/DOWN/CENTER/BACK, SSID +
             * "RSSI  CH n" per entry -- no LEFT/RIGHT paging, no intermediate
             * Actions submenu; CENTER selects the highlighted network directly. */
            if (wifi_dc_pick_network(ap_req.u.wifi_ap_scan.out_list, ncount, &dc_net_sel) == 1U)
            {
                state = DC_DONE; /* BACK -> exit Deauth Client (no entry menu to fall back to) */
                break;
            }
            {
                const wifi_scanlist_t *ap = &ap_req.u.wifi_ap_scan.out_list[dc_net_sel];
                char sel_resp[64] = {0};

                snprintf(select_cmd, sizeof(select_cmd), "select -a %u\r\n", (unsigned int)dc_net_sel);
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth_client: select cmd='%s'\r\n", select_cmd);
                (void)esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100);
                (void)esp32_uart_read_until_prompt(sel_resp, sizeof(sel_resp), 1000, ">> ");
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth_client: select resp='%s'\r\n", sel_resp);

                /* Part 1 fix: the ESP32's select reply is now actually
                 * inspected. A rejected/timed-out select (e.g. a stale index
                 * after an intervening rescan, or an ESP32-side AP table that
                 * no longer matches this list) must never populate ap_inf as
                 * if selection succeeded -- that silent-success bug is the
                 * root cause of Deauth appearing to "hang" with no packets
                 * ever sent and no error shown. */
                if (!m1_wifi_select_response_ok(sel_resp))
                {
                    //M1_LOG_W(M1_LOGDB_TAG, "wifi_deauth_client: select -a rejected: '%s'\r\n", sel_resp);
                    opt = wifi_dc_menu(NULL, "Select failed",
                                       (sel_resp[0] != '\0') ? sel_resp : "No reply", opt_rescan, 2, 0);
                    if (opt == 0U)
                    {
                        /* Rescan both sides: the STM32 list and the ESP32's own
                         * AP table must agree on indices again before retrying. */
                        wifi_session_cache_invalidate(wifi_session_cache_get());
                        state = DC_NET_SCAN;
                    }
                    else
                    {
                        state = DC_DONE;
                    }
                    break;
                }

                memcpy(&wifitarget->ap_inf, ap, sizeof(wifi_scanlist_t));
                memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); /* new network clears device */
                snprintf(net_ssid, sizeof(net_ssid), "%s",
                         (wifitarget->ap_inf.ssid[0] != '\0') ? (const char *)wifitarget->ap_inf.ssid : "Hidden Network");
            }
            state = DC_NET_SELECTED;
            break;
        }

        case DC_NET_SELECTED:
        {
            /* "Home WiFi" / "CH 6   -42 dBm" / CENTER "Scan Devices". No
             * PMF/compatibility screen, no confirm step -- CENTER goes
             * straight to the device scan. */
            if (wifi_dc_selected_network_screen(&wifitarget->ap_inf) == 1U)
            {
                state = DC_NET_CHOOSE; /* BACK -> reselect network, no rescan */
            }
            else
            {
                state = DC_CLIENT_SCAN;
            }
            break;
        }

        case DC_CLIENT_SCAN:
        {
            char sel_err[40] = {0};
            uint16_t dcount;
            memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); /* never leave a stale device on rescan */
            dcount = wifi_scan_devices(dc_net_sel, &sta_req, sel_err, sizeof(sel_err)); /* "Scanning Devices..." (2d) */
            if (dcount == 0xFFFEU) /* ESP32 rejected the re-select before the device scan */
            {
                opt = wifi_dc_menu(net_ssid, "Select failed", sel_err, opt_rescan, 2, 0);
                state = (opt == 0U) ? DC_CLIENT_SCAN : DC_NET_CHOOSE;
            }
            else if (dcount == 0xFFFFU)
            {
                opt = wifi_dc_menu(net_ssid, "Device scan failed", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? DC_CLIENT_SCAN : DC_NET_CHOOSE;
            }
            else
            {
                /* Part 1: initial strongest-RSSI-first ordering is now owned
                 * by the pure picker module (m1_wifi_dc_picker_apply_scan()'s
                 * pre-freeze sort, applied to its own first population
                 * inside wifi_dc_pick_device()) rather than a qsort() here --
                 * that module also owns every subsequent live-refresh round
                 * while the picker is open, so no separate one-shot sort of
                 * the raw sta_req list is needed anymore. */
                state = DC_CLIENT_CHOOSE;
            }
            break;
        }

        case DC_CLIENT_CHOOSE:
        {
            uint16_t dcount = (uint16_t)sta_req.u.wifi_ap_scan.count;
            char picked_mac[18];
            int picked_rssi = 0;
            if (dcount == 0U)
            {
                /* Part 2h: exact wording, CENTER=Rescan (device-only, no
                 * network rescan), BACK=Network. */
                opt = wifi_dc_menu("No Devices Found", NULL, NULL, opt_nodev_single, 1, 0);
                state = (opt == 0U) ? DC_CLIENT_SCAN : DC_NET_CHOOSE;
                break;
            }
            /* Deauth's own picker (Part 2d/2e, live-refreshed per Part 1):
             * UP/DOWN/CENTER/BACK, identity label + live RSSI/category, full
             * MAC still visible per row. sta_req is refreshed IN PLACE by
             * wifi_dc_pick_device() itself (repeated "scan -s"+"list -s", no
             * re-select) while the picker is open. */
            if (wifi_dc_pick_device(&sta_req, picked_mac, sizeof(picked_mac), &picked_rssi) == 1U)
            {
                state = DC_NET_CHOOSE; /* BACK -> back to network selection (device stays cleared) */
                break;
            }
            {
                char sel_resp[64] = {0};
                uint16_t cur_count = (uint16_t)sta_req.u.wifi_ap_scan.count;
                char cur_macs[M1_WIFI_DC_PICKER_MAX_ROWS][18];
                uint16_t k;
                int sel_idx = -1;

                /* CENTER on a device is now itself the final action: validate
                 * (AP still valid, client MAC still resolvable in the picker's
                 * CURRENT snapshot -- never a stale index), issue "select -s"
                 * for that MAC's current index, check the response, and only
                 * then fall through to DC_RUN, which transmits. The picker may
                 * have refreshed sta_req's underlying list (and thus the
                 * ESP32's own "scan -s" station-table order) since dcount was
                 * read above; snapshot the current MACs and re-locate
                 * picked_mac with the shared pure helper
                 * (m1_wifi_dc_locate_client_index(), m1_wifi_client_id.h) so
                 * "select -s <idx>" addresses the ESP32's live table, not a
                 * stale position. The picked identity itself (picked_mac/
                 * picked_rssi) was already captured by value inside
                 * wifi_dc_pick_device() and is never re-derived from any
                 * index. */
                if (cur_count > (uint16_t)M1_WIFI_DC_PICKER_MAX_ROWS)
                {
                    cur_count = (uint16_t)M1_WIFI_DC_PICKER_MAX_ROWS;
                }
                for (k = 0; k < cur_count; k++)
                {
                    snprintf(cur_macs[k], sizeof(cur_macs[k]), "%s",
                             (const char *)sta_req.u.wifi_ap_scan.out_list[k].bssid);
                }

                sel_idx = m1_wifi_dc_locate_client_index(cur_macs, cur_count, picked_mac);

                if ((wifitarget->ap_inf.ssid[0] == '\0') || (sel_idx < 0))
                {
                    /* AP was cleared, or the picked client vanished from the
                     * live table between CENTER and this point -- never
                     * select a substitute MAC/AP on its behalf. */
                    opt = wifi_dc_menu(net_ssid,
                                       (wifitarget->ap_inf.ssid[0] == '\0') ? "Network no longer valid"
                                                                            : "Device no longer visible",
                                       NULL, opt_rescan, 2, 0);
                    state = (opt == 0U) ? DC_CLIENT_SCAN : DC_NET_CHOOSE;
                    break;
                }

                snprintf(select_cmd, sizeof(select_cmd), "select -s %u\r\n", (unsigned int)sel_idx);
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth_client: select cmd='%s'\r\n", select_cmd);
                (void)esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100);
                (void)esp32_uart_read_until_prompt(sel_resp, sizeof(sel_resp), 1000, ">> ");
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_deauth_client: select resp='%s'\r\n", sel_resp);

                if (!m1_wifi_select_response_ok(sel_resp))
                {
                    //M1_LOG_W(M1_LOGDB_TAG, "wifi_deauth_client: select -s rejected: '%s'\r\n", sel_resp);
                    opt = wifi_dc_menu(net_ssid, "Select failed",
                                       (sel_resp[0] != '\0') ? sel_resp : "No reply", opt_rescan, 2, 0);
                    state = (opt == 0U) ? DC_CLIENT_SCAN : DC_NET_CHOOSE;
                    break; /* never populate sta_inf on a rejected select */
                }

                /* Captured by value (MAC string + RSSI) the instant selection
                 * succeeds -- never re-derived from any row/array index again
                 * after this point, so a later rescan/reorder can't silently
                 * repoint the target at a different device. */
                strncpy((char *)wifitarget->sta_inf.bssid, picked_mac, BSSID_STR_SIZE - 1);
                wifitarget->sta_inf.bssid[BSSID_STR_SIZE - 1] = '\0';
                wifitarget->sta_inf.rssi = picked_rssi;
            }
            /* No confirm screen: CENTER-on-device transmits directly. */
            state = DC_RUN;
            break;
        }

        case DC_RUN:
        {
            /* Proven single-client deauth loop (validates AP+client, deauth/
             * stop). Every exit path (user-stopped, ESP32-rejected, or the
             * low-level guard refusing to run at all) returns straight to
             * Select Device with the same network/client-list context --
             * there is no separate "Deauth Stopped" screen in the new flow,
             * and BACK from the running screen behaves the same as CENTER
             * (wifi_deauth() itself already treats OK and BACK identically
             * as "stop", see its own button-poll loop above). */
            wifi_deauth_exit_t exit_reason = wifi_deauth();
            (void)exit_reason; /* wifi_deauth() already shows its own inline
                                 * status message (Stopping.../Deauth rejected/
                                 * no target/send fail) before returning here. */
            state = DC_CLIENT_CHOOSE;
            break;
        }

        default:
            state = DC_DONE;
            break;
        }
    }

    g_wifi_list_title = NULL;
    g_wifi_list_subtitle = NULL;
    wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
    wifi_scan_restore_usbcdc_mode_if_needed(prev_usbcdc_mode, restore_usbcdc_mode);
}



/*============================================================================*/
/**
  * @brief Runs one active-Handshake capture attempt with the live visualizer.
  * @param None
  * @retval 1 if the user requested Retry on the result screen, else 0.
  */
/*============================================================================*/
uint8_t wifi_handshake(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    const char *handshake_cmd = "handshake\r";
    const char *stop_cmd = "\r";
    char rx_accum[192] = {0};
    uint16_t rx_accum_len = 0;
    int done_count = 0;
    uint8_t final_mask = 0;          /* [DONE] mask (bit0..3 = M1..M4): 0x0F = complete */
    uint8_t live_mask = 0;           /* mask from live capture lines, when the FW emits them */
    uint8_t have_live = 0;           /* set once any live "Mask:" line is parsed this attempt */
    uint8_t last_drawn_mask = 0xFFU; /* forces the first stage redraw */
    uint8_t anim_phase = 0;
    TickType_t last_anim_tick = 0;
    const char *hs_ssid;
    int hs_ch;
    uint8_t done_seen = 0;
    uint8_t no_ap_seen = 0;
    uint8_t prompt_seen = 0;
    uint8_t user_stopped = 0;

    static const char *const opt_retry_back[2]   = { "Retry", "Back" };
    static const char *const opt_incomplete[3]   = { "Save Capture", "Retry", "Back" };
    static const char *const opt_save_discard[2] = { "Save Capture", "Discard" };

    /* Graphic work starts here */
    //M1_LOG_W(M1_LOGDB_TAG, ">>> wifi_handshake START <<<\r\n");
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    wifitarget = &wifi_target_storage;
    /* Low-level safety: the guided Handshake flow (wifi_handshake_flow) selects a
     * network before calling here. Refuse to capture without a selected network.
     * No client is required for active handshake capture. */
    if (wifitarget->ap_inf.ssid[0] == '\0')
    {
        wifi_deauth_show_status_message("No network selected", "Choose a network", 1200U);
        return 0U;
    }

    if ( !m1_esp32_get_init_status() )
    {
        m1_esp32_init();

        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
        u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
        m1_u8g2_nextpage();

        osDelay(3000);
    }

    /* Fresh attempt: clear any capture data left in RAM by a previous run so a
     * stopped, timed-out, or empty attempt can never surface stale success data.
     * (done_count and the *_seen flags are per-call locals, already fresh.) */
    received_eapol_len = 0;
    received_eapol[0] = '\0';

    if (!esp32_uart_write((const uint8_t *)handshake_cmd, (uint16_t)strlen(handshake_cmd), 100))
    {
        //M1_LOG_E(M1_LOGDB_TAG, "Failed to send cmd to ESP32: %s\r\n", handshake_cmd);
        wifi_deauth_show_status_message("handshake send fail", NULL, 1000U);
        return 0U;
    }

    hs_ssid = (const char *)wifitarget->ap_inf.ssid;
    hs_ch   = (int)wifitarget->ap_inf.channel;
    wifi_handshake_draw_capturing(hs_ssid, hs_ch, 0);   /* fallback activity view, phase 0 */

    xQueueReset(main_q_hdl);
    while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE)
    {
        ;
    }
    m1_esp32_reset_buffer();
    last_anim_tick = xTaskGetTickCount();

    while (1)
    {
        char uart_buf[96] = {0};
        char *line_start;
        char *p;
        uint16_t uart_len = 0;

        /* Live visualizer update. Real mask data drives the M1-M4 stage view;
         * until any arrives, a low-frequency activity marker cycles (no fake
         * progress). Redraw only on a mask change or on the animation tick — no
         * flicker, and never blocking UART reception (no delays here). */
        if (have_live)
        {
            if (live_mask != last_drawn_mask)
            {
                wifi_handshake_draw_stages(hs_ssid, hs_ch, live_mask, (int)wifi_hs_popcount4(live_mask));
                last_drawn_mask = live_mask;
            }
        }
        else
        {
            TickType_t now = xTaskGetTickCount();
            if ((TickType_t)(now - last_anim_tick) >= pdMS_TO_TICKS(400))
            {
                last_anim_tick = now;
                anim_phase++;
                wifi_handshake_draw_capturing(hs_ssid, hs_ch, anim_phase);
            }
        }

        ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(100));
        if (ret == pdTRUE)
        {
            if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            {
                ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                if (ret == pdTRUE)
                {
                    if ((this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) ||
                        (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK))
                    {
                        /* Safe stop for BOTH CENTER and physical BACK: send the proven
                         * stop (Enter), then keep consuming console output until the
                         * ESP32 returns to its ">> " prompt so the parser stays in sync.
                         * Bounded so a silent console can't hang the UI. A user stop is
                         * never treated as a successful handshake. */
                        //M1_LOG_W(M1_LOGDB_TAG, "wifi_handshake stop_tx: key event detected (OK/BACK)\r\n");
                        (void)esp32_uart_write((const uint8_t *)stop_cmd, (uint16_t)strlen(stop_cmd), 100);

                        wifi_deauth_show_status_message("Stopping...", NULL, 300U);

                        {
                            char drain_buf[96];
                            uint8_t drain_it;
                            for (drain_it = 0U; drain_it < 12U; drain_it++)
                            {
                                uint16_t dn = esp32_uart_read_until_prompt(drain_buf, sizeof(drain_buf), 300, ">> ");
                                if ((dn > 0U) && (strstr(drain_buf, ">>") != NULL))
                                {
                                    break; /* console back at a safe prompt */
                                }
                            }
                        }

                        user_stopped = 1U;
                        xQueueReset(main_q_hdl);
                        break;
                    }
                }
            }
        }

        uart_len = esp32_uart_read_until_prompt(uart_buf, sizeof(uart_buf), 100, "\n");
        if (uart_len == 0)
        {
            continue;
        }

        if (uart_len >= sizeof(uart_buf))
        {
            uart_len = (uint16_t)(sizeof(uart_buf) - 1);
        }
        uart_buf[uart_len] = '\0';

        //M1_LOG_I(M1_LOGDB_TAG, "wifi_handshake rx_len=%u buf='%s'\r\n", (unsigned int)uart_len, uart_buf);

        if (uart_len >= (uint16_t)(sizeof(rx_accum) - 1))
        {
            memcpy(rx_accum,
                   &uart_buf[uart_len - (uint16_t)(sizeof(rx_accum) - 1)],
                   sizeof(rx_accum) - 1);
            rx_accum_len = (uint16_t)(sizeof(rx_accum) - 1);
            rx_accum[rx_accum_len] = '\0';
        }
        else
        {
            uint16_t free_len = (uint16_t)(sizeof(rx_accum) - 1) - rx_accum_len;
            if (uart_len > free_len)
            {
                uint16_t drop_len = uart_len - free_len;
                memmove(rx_accum, rx_accum + drop_len, rx_accum_len - drop_len);
                rx_accum_len -= drop_len;
            }

            memcpy(rx_accum + rx_accum_len, uart_buf, uart_len);
            rx_accum_len += uart_len;
            rx_accum[rx_accum_len] = '\0';
        }

        line_start = rx_accum;
        for (p = rx_accum; ; p++)
        {
            if ((*p == '\r') || (*p == '\n') || (*p == '\0'))
            {
                char saved = *p;
                *p = '\0';

                if (line_start[0] != '\0')
                {
                    //M1_LOG_I(M1_LOGDB_TAG, "wifi_handshake parse line='%s'\r\n", line_start);

                    if (strstr(line_start, "Select an AP first") != NULL)
                    {
                        no_ap_seen = 1;
                    }

                    if (strstr(line_start, "[DONE] Capture finished.") != NULL)
                    {
                        unsigned int mask_tmp = 0;
                        int count_tmp = 0;
                        /* Success = the DONE line is seen (unchanged production condition).
                         * 'Count' is the captured-message count; the mask (bit0..3 =
                         * M1..M4) distinguishes complete (0x0F) from a partial capture. */
                        if (sscanf(line_start, "[DONE] Capture finished. Mask: 0x%X, Count: %d", &mask_tmp, &count_tmp) == 2)
                        {
                            done_count = count_tmp;
                            final_mask = (uint8_t)(mask_tmp & 0x0FU);
                        }
                        done_seen = 1;
                    }
                    else
                    {
                        /* Live progress: any non-DONE line carrying "Mask: 0x" reflects the
                         * current 4-way mask as messages are stored. Drives the live M1-M4
                         * view. Absent on firmware that only prints at DONE — then the
                         * activity animation is shown instead (no fabricated stages). */
                        char *mp = strstr(line_start, "Mask: 0x");
                        if (mp != NULL)
                        {
                            unsigned int lv = 0;
                            if (sscanf(mp, "Mask: 0x%X", &lv) == 1)
                            {
                                live_mask = (uint8_t)(lv & 0x0FU);
                                have_live = 1U;
                            }
                        }
                    }

                    if ((strcmp(line_start, ">>") == 0) || (strcmp(line_start, ">> ") == 0))
                    {
                        prompt_seen = 1;
                    }
                }

                if (saved == '\0')
                {
                    break;
                }

                line_start = p + 1;
            }
        }

        if (line_start != rx_accum)
        {
            rx_accum_len = (uint16_t)strlen(line_start);
            memmove(rx_accum, line_start, rx_accum_len + 1);
        }

        if (no_ap_seen && prompt_seen)
        {
            /* ESP32 reports no selected network (distinct from success/stop). */
            wifi_deauth_show_status_message("No network selected", "Scan networks first", 1200U);
            return 0U;
        }

        if (done_seen && prompt_seen)
        {
            //M1_LOG_I(M1_LOGDB_TAG, "wifi_handshake done mask=0x%02X count=%d\r\n", (unsigned int)done_mask, done_count);

            /* ---- Retrieve captured records via 'list -h' (unchanged path) ---- */
            {
                const char *list_h_cmd = "list -h\r\n";
                char lh_buf[128] = {0};
                char lh_accum[256] = {0};
                uint16_t lh_accum_len = 0;
                uint8_t lh_prompt_seen = 0;
                uint8_t lh_timeout_cnt = 0;

                received_eapol_len = 0;
                memset(received_eapol, 0, sizeof(received_eapol));

                wifi_process_show_message("Retrieving capture...");

                //M1_LOG_I(M1_LOGDB_TAG, "wifi_handshake: sending list -h\r\n");
                (void)esp32_uart_write((const uint8_t *)list_h_cmd, (uint16_t)strlen(list_h_cmd), 100);

                while (!lh_prompt_seen && (lh_timeout_cnt < 60U))
                {
                    uint16_t lh_len;
                    char *lh_line_start;
                    char *lh_p;

                    lh_len = esp32_uart_read_until_prompt(lh_buf, sizeof(lh_buf), 500, "\n");
                    if (lh_len == 0U)
                    {
                        lh_timeout_cnt++;
                        continue;
                    }
                    lh_timeout_cnt = 0;
                    if (lh_len >= (uint16_t)sizeof(lh_buf))
                    {
                        lh_len = (uint16_t)(sizeof(lh_buf) - 1U);
                    }
                    lh_buf[lh_len] = '\0';

                    //M1_LOG_I(M1_LOGDB_TAG, "list_h rx='%s'\r\n", lh_buf);

                    /* append to accumulator */
                    {
                        uint16_t lh_free = (uint16_t)(sizeof(lh_accum) - 1U) - lh_accum_len;
                        if (lh_len > lh_free)
                        {
                            uint16_t drop = lh_len - lh_free;
                            memmove(lh_accum, lh_accum + drop, lh_accum_len - drop);
                            lh_accum_len -= drop;
                        }
                        memcpy(lh_accum + lh_accum_len, lh_buf, lh_len);
                        lh_accum_len += lh_len;
                        lh_accum[lh_accum_len] = '\0';
                    }

                    /* parse lines: keep raw text (no hex conversion) */
                    lh_line_start = lh_accum;
                    for (lh_p = lh_accum; ; lh_p++)
                    {
                        if ((*lh_p == '\r') || (*lh_p == '\n') || (*lh_p == '\0'))
                        {
                            char lh_saved = *lh_p;
                            *lh_p = '\0';

                            if (lh_line_start[0] != '\0')
                            {
                                if ((strcmp(lh_line_start, ">>") == 0) || (strcmp(lh_line_start, ">> ") == 0))
                                {
                                    lh_prompt_seen = 1;
                                }
                                else if ((strstr(lh_line_start, "list -h") != NULL) ||
                                         (strstr(lh_line_start, "list-h") != NULL))
                                {
                                    /* Skip command echo line from UART output. */
                                }
                                else
                                {
                                    size_t lh_llen = strlen(lh_line_start);
                                    if (received_eapol_len < (uint16_t)(sizeof(received_eapol) - 1U))
                                    {
                                        uint16_t remain = (uint16_t)(sizeof(received_eapol) - 1U) - received_eapol_len;
                                        uint16_t copy_len = (lh_llen < remain) ? (uint16_t)lh_llen : remain;

                                        if (copy_len > 0U)
                                    {
                                            memcpy(&received_eapol[received_eapol_len], lh_line_start, copy_len);
                                            received_eapol_len += copy_len;
                                        }

                                        if ((copy_len == (uint16_t)lh_llen) &&
                                            (received_eapol_len < (uint16_t)(sizeof(received_eapol) - 1U)))
                                                {
                                            received_eapol[received_eapol_len++] = '\n';
                                                }

                                        received_eapol[received_eapol_len] = '\0';
                                    }
                                }
                            }

                            if (lh_saved == '\0') { break; }
                            lh_line_start = lh_p + 1;
                        }
                    }

                    if (lh_line_start != lh_accum)
                    {
                        lh_accum_len = (uint16_t)strlen(lh_line_start);
                        memmove(lh_accum, lh_line_start, lh_accum_len + 1U);
                    }
                }

                //M1_LOG_I(M1_LOGDB_TAG, "wifi_handshake: received_eapol_len=%u\r\n", (unsigned int)received_eapol_len);

                if (received_eapol_len == 0U)
                {
                    /* Finished but nothing retrieved: distinct, non-success. */
                    uint8_t r = wifi_dc_menu("No handshake data", "Packets: 0", NULL,
                                             opt_retry_back, 2, 0);
                    return (r == 0U) ? 1U : 0U;                 /* Retry / Back */
                }
                else if (final_mask == 0x0FU)
                {
                    /* Official success (all four messages). Handshake Captured + Save.
                     * CENTER = save; BACK requires an explicit discard confirm so an
                     * unsaved successful capture is never silently dropped. */
                    uint8_t decided = 0U;
                    wifi_handshake_draw_captured(hs_ssid, done_count, 1U);
                    xQueueReset(main_q_hdl);
                    while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE) { ; }
                    while (!decided)
                    {
                        S_M1_Main_Q_t sd_q;
                        if (xQueueReceive(main_q_hdl, &sd_q, portMAX_DELAY) != pdTRUE) { continue; }
                        if (sd_q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
                        (void)xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                        if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
                        {
                            (void)wifi_read_more_options_save();     /* existing save workflow */
                            osDelay(1500);
                            decided = 1U;
                        }
                        else if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
                        {
                            uint8_t d = wifi_dc_menu("Discard capture?", NULL, NULL,
                                                     opt_save_discard, 2, 0);
                            if (d == 0U)      { (void)wifi_read_more_options_save(); osDelay(1500); decided = 1U; }
                            else if (d == 1U) { decided = 1U; }      /* explicit discard */
                            else              { wifi_handshake_draw_captured(hs_ssid, done_count, 1U); } /* BACK: back to result */
                        }
                    }
                    return 0U;
                }
                else
                {
                    /* Partial handshake, but data was retrieved. Offer Save/Retry/Back;
                     * the existing save format already stores whatever was captured. */
                    char pk[20];
                    uint8_t r;
                    snprintf(pk, sizeof(pk), "Packets: %d", done_count);
                    r = wifi_dc_menu("Capture Incomplete", pk, "Handshake incomplete",
                                     opt_incomplete, 3, 0);
                    if (r == 0U) { (void)wifi_read_more_options_save(); osDelay(1500); return 0U; }
                    return (r == 1U) ? 1U : 0U;                 /* Retry / Back */
                }
            }
        }
    }

    /* The loop only reaches here via a user stop (done/no-data/error already
     * returned). Report it — never as success — and offer Retry. Partial count is
     * the live mask popcount; no stale data from an earlier attempt is used. */
    if (user_stopped)
    {
        char pk[20];
        uint8_t r;
        snprintf(pk, sizeof(pk), "Packets: %d", (int)wifi_hs_popcount4(live_mask));
        r = wifi_dc_menu("Capture Stopped", pk, "Nothing saved", opt_retry_back, 2, 0);
        return (r == 0U) ? 1U : 0U;                             /* Retry / Back */
    }
    return 0U;
}



/*============================================================================*/
/* Guided active-Handshake workflow.                                          */
/* Self-contained: performs its own network selection using the existing      */
/* scan/list/select primitives, then runs the official active handshake       */
/* capture (wifi_handshake) unchanged. Network-only (no client step).         */
/*============================================================================*/
void wifi_handshake_flow(void)
{
    ctrl_cmd_t ap_req = CTRL_CMD_DEFAULT_REQ();
    ctrl_cmd_t sta_req = CTRL_CMD_DEFAULT_REQ(); /* unused; kept for shared cleanup signature */
    enCdcMode prev_usbcdc_mode;
    uint8_t restore_usbcdc_mode = 0;
    uint8_t ready;
    char select_cmd[32];
    char resp_buf[64];
    char net_ssid[34];
    uint16_t hs_net_sel = 0;
    uint8_t opt;

    static const char *const opt_entry[2]     = { "Scan Networks", "Back" };
    static const char *const opt_preselect[3] = { "Use Selected", "Scan Networks", "Back" };
    static const char *const opt_confirm[3]   = { "Start Capture", "Change Network", "Back" };
    static const char *const opt_rescan[2]    = { "Rescan", "Back" };
    static const char *const opt_unavail[3]   = { "Rescan", "Choose Network", "Back" };

    enum {
        HS_ENTRY, HS_USE_SELECTED, HS_NET_SCAN, HS_NET_CHOOSE, HS_CONFIRM, HS_RUN, HS_DONE
    } state;

    wifitarget = &wifi_target_storage;
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    net_ssid[0] = '\0';

    ready = wifi_survey_prepare_esp32(&prev_usbcdc_mode, &restore_usbcdc_mode);
    if (!ready)
    {
        wifi_deauth_show_status_message("ESP32 not ready", "Try bridge reset", 1500U);
        state = HS_DONE;
    }
    else
    {
        /* Straight to the network list (LEFT = Rescan); no entry menu. */
        state = HS_NET_SCAN;
    }

    while (state != HS_DONE)
    {
        switch (state)
        {
        case HS_ENTRY:
        {
            /* A valid selection is one carried from the current live session
             * (SSID present). Only then is "Use Selected" offered; otherwise the
             * menu is Scan Networks (highlighted) / Back, and nothing scans yet. */
            if (wifitarget->ap_inf.ssid[0] != '\0')
            {
                opt = wifi_dc_menu("Handshake", "Selected:",
                                   (const char *)wifitarget->ap_inf.ssid,
                                   opt_preselect, 3, 0);
                if (opt == 0U)      { state = HS_USE_SELECTED; }
                else if (opt == 1U) { memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); state = HS_NET_SCAN; }
                else                { state = HS_DONE; }      /* Back / BACK -> Wi-Fi menu */
            }
            else
            {
                opt = wifi_dc_menu("Handshake", NULL, NULL, opt_entry, 2, 0);
                if (opt == 0U) { state = HS_NET_SCAN; }
                else           { state = HS_DONE; }           /* Back / BACK -> Wi-Fi menu */
            }
            break;
        }

        case HS_NET_SCAN:
        {
            wifi_ui_draw_scanning("Scanning Networks...");
            wifi_scan_free_lists(&ap_req, NULL);
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
            /* Entry AP-list load: REUSE the shared AP cache if fresh (a scan
             * just done in another Wi-Fi tool), else a real scan. The explicit
             * manual-refresh control below invalidates the cache first, so it
             * still forces fresh. One scan now serves every Wi-Fi tool. */
            if (wifi_ap_scan_list_cached(&ap_req, false) != SUCCESS)
            {
                opt = wifi_dc_menu(NULL, "Network scan failed", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? HS_NET_SCAN : HS_ENTRY;   /* Back -> entry */
            }
            else
            {
                hs_net_sel = 0;
                state = HS_NET_CHOOSE;
            }
            break;
        }

        case HS_USE_SELECTED:
        {
            uint16_t i;
            uint16_t found = 0xFFFFU;
            wifi_ui_draw_scanning("Scanning Networks...");
            wifi_scan_free_lists(&ap_req, NULL);
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
            /* Validating a previously selected AP is still present: reuse
             * the shared cache if it's fresh, otherwise a real scan -- the
             * BSSID re-match loop below always runs regardless, so a stale
             * BSSID is never silently trusted either way. */
            if (wifi_ap_scan_list_cached(&ap_req, false) != SUCCESS)
            {
                opt = wifi_dc_menu(NULL, "Network scan failed", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? HS_USE_SELECTED : HS_ENTRY;   /* Back -> entry */
                break;
            }
            for (i = 0; i < (uint16_t)ap_req.u.wifi_ap_scan.count; i++)
            {
                if (strncmp((const char *)ap_req.u.wifi_ap_scan.out_list[i].bssid,
                            (const char *)wifitarget->ap_inf.bssid, BSSID_STR_SIZE - 1) == 0)
                {
                    found = i;
                    break;
                }
            }
            if (found == 0xFFFFU)
            {
                opt = wifi_dc_menu(NULL, "Network unavailable", NULL, opt_unavail, 3, 0);
                state = (opt == 0U) ? HS_USE_SELECTED : (opt == 1U) ? HS_NET_CHOOSE : HS_ENTRY;
                break;
            }
            hs_net_sel = found; /* keep result index aligned with the matched BSSID */
            snprintf(select_cmd, sizeof(select_cmd), "select -a %u\r\n", (unsigned int)found);
            (void)esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100);
            (void)esp32_uart_read_until_prompt(resp_buf, sizeof(resp_buf), 1000, ">> ");
            /* Part 1 fix: don't trust an unread/failed select reply. */
            if (!m1_wifi_select_response_ok(resp_buf))
            {
                opt = wifi_dc_menu(NULL, "Select failed",
                                   (resp_buf[0] != '\0') ? resp_buf : "No reply", opt_rescan, 2, 0);
                if (opt == 0U) { wifi_session_cache_invalidate(wifi_session_cache_get()); state = HS_NET_SCAN; }
                else            { state = HS_ENTRY; }
                break;
            }
            memcpy(&wifitarget->ap_inf, &ap_req.u.wifi_ap_scan.out_list[found], sizeof(wifi_scanlist_t));
            memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); /* refreshed network clears stale client */
            snprintf(net_ssid, sizeof(net_ssid), "%s",
                     (wifitarget->ap_inf.ssid[0] != '\0') ? (const char *)wifitarget->ap_inf.ssid : "Hidden Network");
            state = HS_CONFIRM;
            break;
        }

        case HS_NET_CHOOSE:
        {
            uint16_t ncount = (uint16_t)ap_req.u.wifi_ap_scan.count;
            if (ncount == 0U)
            {
                opt = wifi_dc_menu(NULL, "No networks found", NULL, opt_rescan, 2, 0);
                state = (opt == 0U) ? HS_NET_SCAN : HS_ENTRY;   /* Back -> entry */
                break;
            }
            {
                uint8_t br = wifi_browse(ap_req.u.wifi_ap_scan.out_list, ncount, 0, NULL,
                                         "Choose Network", 0, "Select", &hs_net_sel);
                if (br == 1U)
                {
                    state = HS_DONE; /* BACK -> exit Handshake (no entry menu) */
                }
                else if (br == 2U)
                {
                    /* Explicit manual-refresh: invalidate the shared cache so
                     * HS_NET_SCAN (which now reuses a fresh cache) does a real
                     * scan. */
                    wifi_session_cache_invalidate(wifi_session_cache_get());
                    state = HS_NET_SCAN; /* -> real rescan (cache now invalid) */
                }
                else
                {
                    snprintf(select_cmd, sizeof(select_cmd), "select -a %u\r\n", (unsigned int)hs_net_sel);
                    (void)esp32_uart_write((const uint8_t *)select_cmd, (uint16_t)strlen(select_cmd), 100);
                    (void)esp32_uart_read_until_prompt(resp_buf, sizeof(resp_buf), 1000, ">> ");
                    /* Part 1 fix: don't trust an unread/failed select reply. */
                    if (!m1_wifi_select_response_ok(resp_buf))
                    {
                        uint8_t o2 = wifi_dc_menu(NULL, "Select failed",
                                                  (resp_buf[0] != '\0') ? resp_buf : "No reply", opt_rescan, 2, 0);
                        if (o2 == 0U) { wifi_session_cache_invalidate(wifi_session_cache_get()); state = HS_NET_SCAN; }
                        else          { state = HS_DONE; }
                        break;
                    }
                    memcpy(&wifitarget->ap_inf, &ap_req.u.wifi_ap_scan.out_list[hs_net_sel], sizeof(wifi_scanlist_t));
                    memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); /* selecting a network clears stale device */
                    snprintf(net_ssid, sizeof(net_ssid), "%s",
                             (wifitarget->ap_inf.ssid[0] != '\0') ? (const char *)wifitarget->ap_inf.ssid : "Hidden Network");
                    state = HS_CONFIRM;
                }
            }
            break;
        }

        case HS_CONFIRM:
        {
            char net_line[28];
            char ch_line[20];
            int ch = (int)wifitarget->ap_inf.channel;
            snprintf(net_line, sizeof(net_line), "Net: %.20s", net_ssid);
            if ((ch >= 1) && (ch <= 196)) { snprintf(ch_line, sizeof(ch_line), "Channel: %d", ch); }
            else                          { ch_line[0] = '\0'; }
            /* Start Capture / Change Network (rescan) / Back (return to selection). */
            opt = wifi_dc_menu("Handshake", net_line, (ch_line[0] != '\0') ? ch_line : NULL, opt_confirm, 3, 0);
            if (opt == 0U)      { state = HS_RUN; }
            else if (opt == 1U) { memset(&wifitarget->sta_inf, 0, sizeof(wifitarget->sta_inf)); state = HS_NET_SCAN; }
            else                { state = HS_NET_CHOOSE; }   /* Back -> network selection */
            break;
        }

        case HS_RUN:
            /* Official active capture + live visualizer. Returns 1 when the user
             * chose Retry on the result screen -> start a fresh attempt against the
             * same selected network; otherwise fall back to the confirmation. */
            state = (wifi_handshake() == 1U) ? HS_RUN : HS_CONFIRM;
            break;

        default:
            state = HS_DONE;
            break;
        }
    }

    g_wifi_list_title = NULL;
    g_wifi_list_subtitle = NULL;
    wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
    wifi_scan_restore_usbcdc_mode_if_needed(prev_usbcdc_mode, restore_usbcdc_mode);
}


/*============================================================================*/
/**
 * @brief Get filename from user and create full file path
 *
 * Prompts user for filename using virtual keyboard, validates
 * SD card space, creates directory if needed, and checks for
 * duplicate filenames.
 *
 * @param filepath Output buffer for full file path (can be NULL)
 * @return 0 on success
 * @return 1 Limited space available on SD card
 * @return 2 Error creating directory on SD card
 * @return 3 User escaped (cancelled)
 */
/*============================================================================*/
uint8_t wifi_save_file_keyboard(char *filepath, size_t filepath_size)
{
    return m1_save_filename(filepath, filepath_size, DRIVE0_WIFI, DRIVE0_WIFI "/", WIFI_FILE_PREFIX, WIFI_FILE_EXTENSION);
}



/* Small fixed margin added on top of the exact capture size so a card that
 * is only marginally short still fails the space check before file
 * creation, rather than during the write. */
#define WIFI_EAPOL_SPACE_MARGIN_BYTES   512U

/* True if the SD card has at least required_bytes free, checked at FatFs
 * sector precision. m1_sdcard_get_free_capacity() (used elsewhere for the
 * filename picker) is KB-granularity only, too coarse for a capture that
 * can reach M1_EAPOL_MAX_BYTES (5120 bytes) -- same f_getfree() computation
 * as m1_sdcard_get_info(), done locally in bytes instead of rounded KB. */
static bool wifi_sd_has_free_bytes(uint32_t required_bytes)
{
	FATFS *fatfs;
	DWORD free_clusters;
	uint64_t free_bytes;

	if (f_getfree(SDCARD_DEFAULT_DRIVE_PATH, &free_clusters, &fatfs) != FR_OK) {
		return false;
	}

	free_bytes = (uint64_t)free_clusters * (uint64_t)fatfs->csize * (uint64_t)FF_MAX_SS;
	return free_bytes >= ((uint64_t)required_bytes + WIFI_EAPOL_SPACE_MARGIN_BYTES);
}

/*============================================================================*/
/**
 * @brief Save EAPOL to file
 *
 * Saves WiFi EAPOL data to file
 *
 * @param fp Full file path to save to
 * @param buf Buffer containing EAPOL data
 * @param size Size of the buffer
 * @return true on success, false on failure
 */
/*============================================================================*/
bool wifi_eapol_save(const char *fp, const void *buf, uint32_t size)
{
	FIL eapol_file;
	FRESULT fr;
	UINT bw = 0;
	bool created = false;
	bool ok;

	if (!fp || !buf || (size == 0U)) {
		return false;
	}

	if (!wifi_sd_has_free_bytes(size)) {
		return false;
	}

	/* FA_CREATE_NEW: fails FR_EXIST if fp already exists, so an existing
	 * user file is never silently overwritten. */
	fr = f_open(&eapol_file, fp, FA_CREATE_NEW | FA_WRITE);
	if (fr != FR_OK) {
		//M1_LOG_D(M1_LOGDB_TAG, "wifi_eapol_save: Error creating file '%s'\r\n", fp);
		return false;
	}
	created = true;

	fr = f_write(&eapol_file, buf, (UINT)size, &bw);
	ok = (fr == FR_OK) && (bw == (UINT)size);

	if (ok) {
		ok = (f_sync(&eapol_file) == FR_OK);
	}

	if (f_close(&eapol_file) != FR_OK) {
		ok = false;
	}

	if (!ok && created) {
		/* Safe: FA_CREATE_NEW guarantees this file did not exist before
		 * this call, so removing an incomplete write cannot destroy an
		 * existing user file. */
		(void)f_unlink(fp);
	}

	return ok;
}



/*============================================================================*/
/**
 * @brief wifi_read_more_options_save - Save WiFi EAPOL data to SD card file
 *
 * Saves the WiFi EAPOL data to a file on the SD card in
 * the wifi directory. The function:
 * 1. Checks SD card free space (minimum 4KB required)
 * 2. Creates /wifi directory if it doesn't exist
 * 3. Prompts user for filename using virtual keyboard
 * 4. Validates filename doesn't already exist

 *
 * @retval 0 Success
 * @retval 1 Insufficient SD card space
 * @retval 2 Directory creation failed
 * @retval 3 User cancelled (escaped)
 * @retval 4 File creation failed
 */
/*============================================================================*/
static uint8_t wifi_read_more_options_save(void)
{
    char filepath[128];
    uint8_t error;
    const char *saved_name;
    uint8_t retry_filename = 1U;
    uint8_t retry_save = 1U;

    while (retry_filename)
    {
        // Get filename from user and create full path
        error = wifi_save_file_keyboard(filepath, sizeof(filepath));
        if (error == 0U)
        {
            break;
        }

        // User escape
        if (error == 3U)
        {
            return error;
        }

        retry_filename = wifi_handshake_ask_retry_filename_input();
    }

    if (error != 0U)
    {
        return error;
    }

    while (retry_save)
    {
        wifi_process_show_message("Saving to SD...");

        if (wifi_eapol_save(filepath, received_eapol, (uint32_t)received_eapol_len) == true)
        {
            saved_name = fu_get_filename(filepath);
            wifi_handshake_draw_saved_to_sd(saved_name);
            return 0; // Success
        }

        retry_save = wifi_handshake_ask_retry_sd_save();
    }

    return 4; // Error
}



/*============================================================================*/
/**
  * @brief Starts broadcasting fake beacon frames
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_beacon(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    /* id==0: "beacon \"<ssid>\"\r\n\0" = 7+32+4 = 43 bytes max
     * id==1: "beacon \"s1\" \"s2\"...\0" = 6 + MAX_BEACON_SSIDS*(1+1+32+1) + 1 bytes max */
    char beacon_cmd[6 + MAX_BEACON_SSIDS * 35 + 4];
    char resp_buf[96];
    char ui_msg[40]; /* "N:" (3) + SSID (32) + NUL (1) = 36, rounded up to 40 */
    uint8_t y_offset;
    /* SSID list shared between command builder and LCD render */
    char        ssid_buf[MAX_BEACON_SSIDS][33];
    const char *beacon_args[MAX_BEACON_SSIDS];
    uint8_t     beacon_argc = 0U;

    //M1_LOG_W(M1_LOGDB_TAG, ">>> wifi_beacon START <<<\r\n");
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    /* Beacon source-selection + run loop: after a session stops, control
     * returns here (not to the higher-level Wi-Fi menu) so another session
     * can be started immediately with either source. BACK at the
     * source-selection screen is the only path that exits this loop (and
     * the function) back to the caller's menu level. */
    for (;;)
    {
        /* Beacon source selection (relocated from the old top-level menu LEFT/RIGHT
         * arrows). Same two options and identical behavior; only the selection point
         * moved into the Beacon screen. BACK cancels. */
        {
            static const char *const opt_bsrc[2] = { "SD List", "Keypad SSID" };
            uint8_t bsel = wifi_dc_menu("Beacon Source", NULL, NULL, opt_bsrc, 2,
                                        (m1_beacon_menu_id == 1) ? 1U : 0U);
            if (bsel == 0xFFU)
            {
                return; /* BACK cancels beacon */
            }
            m1_beacon_menu_id = (bsel == 1U) ? 1 : 0;
        }

        beacon_argc = 0U; /* reset per-session: the SD-list branch below appends onto this */

        wifitarget = &wifi_target_storage;

        if ( !m1_esp32_get_init_status() )
        {
            m1_esp32_init();

            m1_u8g2_firstpage();
            u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
            m1_u8g2_nextpage();

            osDelay(3000);
        }

        if (m1_beacon_menu_id == 1)
        {
            /* Build: beacon "ssid"\r\n
             * SSID is entered via keyboard.  */
            char ssid_default[33];
            char ssid_input[33];

            ssid_default[0] = '\0';
            ssid_input[0]   = '\0';
            if (!m1_vkb_get_filename("Beacon SSID", ssid_default, ssid_input, 0 /* user text entry: normal delete */))
            {
                /* User cancelled */
                return;
            }
            ssid_input[32] = '\0'; /* safety null-terminate */
            strncpy(ssid_buf[0], ssid_input, 32);
            ssid_buf[0][32] = '\0';
            beacon_args[0]  = ssid_buf[0];
            beacon_argc     = 1U;
            snprintf(beacon_cmd, sizeof(beacon_cmd), "beacon \"%s\"\n", ssid_buf[0]);   /* /n: transmit delimter */
        }
        else    // m1_beacon_menu_id == 0
        {
            /* Build: beacon "s1" "s2" ... "sN"\r\n
             * SSID list is loaded from the fixed ap_lists.txt path (one SSID per
             * line, max 32 bytes each, up to 16 entries).  If the file cannot be
             * opened the current AP SSID is used as a single fallback entry.   */
            uint8_t     bi;
            size_t      pos = 0U;

            /* ---- Load SSID list from fixed path ---- */
            static const char ap_list_path[] = DRIVE0_WIFI FILE_AP_LIST;

            FIL   hfile;
            if (m1_fb_open_file(&hfile, ap_list_path) == 0U)
            {
                /* Read entire file into a flat buffer then parse lines */
                char   file_buf[MAX_BEACON_SSIDS * 34]; /* MAX_BEACON_SSIDS lines × (32 chars + CRLF) */
                uint16_t file_len;
                char  *lp;
                char  *ep;

                memset(file_buf, 0, sizeof(file_buf));
                file_len = m1_fb_read_from_file(&hfile,
                                                file_buf,
                                                (uint16_t)(sizeof(file_buf) - 1U));
                m1_fb_close_file(&hfile);
                file_buf[file_len] = '\0';

                lp = file_buf;
                while ((beacon_argc < (uint8_t)MAX_BEACON_SSIDS) && (*lp != '\0'))
                {
                    /* Find end of line (\n or \r\n or end-of-string) */
                    ep = lp;
                    while ((*ep != '\0') && (*ep != '\n') && (*ep != '\r'))
                    {
                        ep++;
                    }

                    /* Trim trailing whitespace */
                    char *trim = ep - 1;
                    while ((trim >= lp) && ((*trim == ' ') || (*trim == '\t')))
                    {
                        trim--;
                    }

                    size_t llen = (size_t)(trim - lp + 1);
                    if (llen > 0U)
                    {
                        size_t copy_len = (llen > 32U) ? 32U : llen;
                        memcpy(ssid_buf[beacon_argc], lp, copy_len);
                        ssid_buf[beacon_argc][copy_len] = '\0';
                        beacon_args[beacon_argc] = ssid_buf[beacon_argc];
                        beacon_argc++;
                    }

                    /* Advance past line terminator(s) */
                    if (*ep == '\r') { ep++; }
                    if (*ep == '\n') { ep++; }
                    lp = ep;
                }

                //M1_LOG_I(M1_LOGDB_TAG, "wifi_beacon: loaded %u SSIDs from '%s'\r\n", (unsigned int)beacon_argc, ap_list_path);
                {
                    uint8_t _si;
                    for (_si = 0U; _si < beacon_argc; _si++)
                    {
                        //M1_LOG_I(M1_LOGDB_TAG, "  [%u] '%s'\r\n", (unsigned int)_si, ssid_buf[_si]);
                    }
                }
            }
            else
            {
                //M1_LOG_W(M1_LOGDB_TAG, "wifi_beacon: failed to open '%s', using AP SSID\r\n", ap_list_path);
            }

            /* Fallback: use the currently selected AP SSID */
            if (beacon_argc == 0U)
            {
                strncpy(ssid_buf[0], (const char *)wifitarget->ap_inf.ssid, 32);
                ssid_buf[0][32] = '\0';
                beacon_args[0]  = ssid_buf[0];
                beacon_argc     = 1U;
                //M1_LOG_I(M1_LOGDB_TAG, "wifi_beacon: fallback to AP SSID '%s'\r\n", ssid_buf[0]);
            }

            /* ---- Assemble the command string ---- */
            pos += (size_t)snprintf(beacon_cmd + pos, sizeof(beacon_cmd) - pos, "beacon");
            for (bi = 0U; bi < beacon_argc; bi++)
            {
                if (pos < sizeof(beacon_cmd) - 1U)
                {
                    pos += (size_t)snprintf(beacon_cmd + pos,
                                            sizeof(beacon_cmd) - pos,
                                            " \"%s\"",
                                            beacon_args[bi]);
                }
            }
            if (pos + 3U < sizeof(beacon_cmd))
            {
                beacon_cmd[pos++] = '\n';   /* /n: transmit delimter */
                beacon_cmd[pos]   = '\0';
            }
        }

        ////M1_LOG_I(M1_LOGDB_TAG, "wifi_beacon: cmd='%s'", beacon_cmd);

        if (!esp32_uart_write((const uint8_t *)beacon_cmd, (uint16_t)strlen(beacon_cmd), 100))
        {
            //M1_LOG_E(M1_LOGDB_TAG, "wifi_beacon: Failed to send command\r\n");
            wifi_deauth_show_status_message("beacon send fail", NULL, 1000U);
            return;
        }

       /* Wait for prompt to confirm command was accepted */
       memset(resp_buf, 0, sizeof(resp_buf));
       if (esp32_uart_read_until_prompt(resp_buf, (uint16_t)sizeof(resp_buf), 3000U, ">> ") == 0U)
       {
           //M1_LOG_W(M1_LOGDB_TAG, "wifi_beacon: no prompt after send\r\n");
       }
       //M1_LOG_I(M1_LOGDB_TAG, "wifi_beacon: start resp='%s'\r\n", resp_buf);

        /* Show beacon running screen with SSID list */
        m1_u8g2_firstpage();
        do
        {
            uint8_t _si;
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            y_offset = M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT;
            snprintf(ui_msg, sizeof(ui_msg), "Beacon(%d) running...", beacon_argc);

            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);
            for (_si = 0U; _si < beacon_argc; _si++)
            {
                y_offset += M1_GUI_FONT_HEIGHT;
                snprintf(ui_msg, sizeof(ui_msg), "%s", ssid_buf[_si]);
                u8g2_DrawStr(&m1_u8g2, 2, y_offset, ui_msg);
            }
        } while (m1_u8g2_nextpage());
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        /* Flush stale key events before entering wait loop */
        xQueueReset(main_q_hdl);
        while (xQueueReceive(button_events_q_hdl, &this_button_status, 0) == pdTRUE)
        {
            ;
        }

        /* Wait for OK or BACK button press */
        while (1)
        {
            ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
            if ((ret == pdTRUE) && (q_item.q_evt_type == Q_EVENT_KEYPAD))
            {
                ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                if (ret == pdTRUE)
                {
                    if ((this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) ||
                        (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK))
                    {
                        //M1_LOG_W(M1_LOGDB_TAG, "wifi_beacon: stop key pressed, sending Enter\r\n");
                        break;
                    }
                }
            }
        }

        /* Send Enter to stop beacon, wait for prompt */
        (void)esp32_uart_write((const uint8_t *)"\r\n", 2U, 100U);

        wifi_deauth_show_status_message("Stopping...", NULL, 100U);

        memset(resp_buf, 0, sizeof(resp_buf));
        (void)esp32_uart_read_until_prompt(resp_buf, (uint16_t)sizeof(resp_buf), 2000U, ">> ");
        //M1_LOG_I(M1_LOGDB_TAG, "wifi_beacon: stop resp='%s'\r\n", resp_buf);

        xQueueReset(main_q_hdl);
        /* Falls through to the top of the loop: back to Beacon source
         * selection, not the higher-level Wi-Fi menu. */
    }
} //void wifi_beacon(void)


/*============================================================================*/
/* 2.4 GHz channel survey                                                     */
/*                                                                            */
/* Host-side only. Runs ONE AP scan through the existing scan function        */
/* (scan -a / list -a via wifi_ap_sta_scan_list), aggregates the already-     */
/* parsed AP results per 2.4 GHz channel, and shows channel number, AP count  */
/* and strongest RSSI. No new ESP32 command, no continuous background scan.   */
/* Does not touch the Scan/Deauth/Handshake/Beacon workflow.                  */
/*============================================================================*/
#define WIFI_SURVEY_CH_MIN      1
#define WIFI_SURVEY_CH_MAX      14
#define WIFI_SURVEY_CH_SPAN     (WIFI_SURVEY_CH_MAX - WIFI_SURVEY_CH_MIN + 1)
#define WIFI_SURVEY_ROWS        4   /* data rows visible under the header */
#define WIFI_SURVEY_RSSI_NONE   (-127)

/* Private readiness helper used ONLY by the survey. This intentionally
 * duplicates the readiness sequence of wifi_scan_ap() so that the
 * regression-protected wifi_scan_ap() is left completely unchanged. */
static uint8_t wifi_survey_prepare_esp32(enCdcMode *prev_usbcdc_mode, uint8_t *restore_usbcdc_mode)
{
    uint8_t need_esp32_reinit = 0;
    uint8_t esp32_prompt_ready = 0;
    uint8_t ready_try;
    uint32_t prompt_timeout_ms;
    static const char mode_wifi_cmd[] = "mode -w\r\n";
    static const char esp32_prompt_ping[] = "\r\n";
    char uart_resp[512] = {0};

    *restore_usbcdc_mode = 0;
    *prev_usbcdc_mode = m1_usbcdc_mode;

    if (*prev_usbcdc_mode == CDC_MODE_ESP32)
    {
        *restore_usbcdc_mode = 1;
        m1_logdb_deinit();
        m1_usbcdc_mode = CDC_MODE_LOG_CLI;
        m1_logdb_init();
    }

    if (!m1_esp32_get_init_status())
    {
        need_esp32_reinit = 1;
    }
    if (HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin) == GPIO_PIN_RESET)
    {
        need_esp32_reinit = 1;
    }

    if (need_esp32_reinit)
    {
        if (m1_esp32_get_init_status())
        {
            m1_esp32_deinit();
        }
        m1_esp32_init();

        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
        u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
        m1_u8g2_nextpage();
    }

    for (ready_try = 0; ready_try < 3U; ready_try++)
    {
        if (need_esp32_reinit)
        {
            prompt_timeout_ms = 1200U + ((uint32_t)ready_try * 700U);
        }
        else
        {
            prompt_timeout_ms = 300U + ((uint32_t)ready_try * 400U);
        }

        memset(uart_resp, 0, sizeof(uart_resp));
        (void)esp32_uart_write((const uint8_t *)esp32_prompt_ping,
                               (uint16_t)(sizeof(esp32_prompt_ping) - 1U), 100U);

        if (esp32_uart_read_until_prompt(uart_resp, (uint16_t)sizeof(uart_resp),
                                         prompt_timeout_ms, ">> ") > 0U)
        {
            if (strstr(uart_resp, ">> ") != NULL)
            {
                esp32_prompt_ready = 1U;
                break;
            }
        }

        m1_esp32_deinit();
        osDelay(20);
        m1_esp32_init();
        need_esp32_reinit = 1U;
        osDelay(150);
    }

    /* Put the shell into Wi-Fi mode, same command the scan path uses. */
    if (esp32_uart_write((const uint8_t *)mode_wifi_cmd, (uint16_t)(sizeof(mode_wifi_cmd) - 1U), 100U))
    {
        (void)esp32_uart_read_until_prompt(uart_resp, (uint16_t)sizeof(uart_resp), 1500U, ">> ");
    }

    return esp32_prompt_ready;
}

void wifi_survey(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    enCdcMode prev_usbcdc_mode;
    uint8_t restore_usbcdc_mode = 0;
    ctrl_cmd_t ap_req = CTRL_CMD_DEFAULT_REQ();
    ctrl_cmd_t sta_req = CTRL_CMD_DEFAULT_REQ(); /* unused; kept for shared cleanup signature */
    uint8_t esp32_prompt_ready;
    uint8_t exit_survey = 0U;
    uint8_t request_rescan = 1U;
    uint16_t ch_count[WIFI_SURVEY_CH_SPAN];
    int16_t  ch_best_rssi[WIFI_SURVEY_CH_SPAN];
    uint16_t total_aps = 0;
    uint8_t top_row = 0;   /* index (0-based over channels 1..14) of first visible row */
    char line[26];

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    esp32_prompt_ready = wifi_survey_prepare_esp32(&prev_usbcdc_mode, &restore_usbcdc_mode);

    if (!esp32_prompt_ready)
    {
        m1_u8g2_firstpage();
        u8g2_DrawStr(&m1_u8g2, 2, 15, "ESP32 not ready");
        u8g2_DrawStr(&m1_u8g2, 2, 15 + M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING, "Try bridge reset");
        m1_u8g2_nextpage();
        osDelay(1200);
        exit_survey = 1U;
    }

    while (!exit_survey)
    {
        if (request_rescan)
        {
            uint16_t i;
            wifi_scanlist_t *list;
            uint16_t count;

            request_rescan = 0U;

            for (i = 0; i < WIFI_SURVEY_CH_SPAN; i++)
            {
                ch_count[i] = 0;
                ch_best_rssi[i] = WIFI_SURVEY_RSSI_NONE;
            }
            total_aps = 0;
            top_row = 0;
            wifi_scan_free_lists(&ap_req, &sta_req);

            m1_u8g2_firstpage();
            u8g2_DrawStr(&m1_u8g2, 6, 15, "2.4G Survey...");
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
            m1_u8g2_nextpage();

            /* Reuse the exact production AP scan path (scan -a / list -a), now
             * via the shared AP cache: a scan from another Wi-Fi tool is reused
             * if fresh, otherwise this scans and publishes for the others. */
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;

            ret = wifi_ap_scan_list_cached(&ap_req, false);

            if (ret == SUCCESS)
            {
                list = ap_req.u.wifi_ap_scan.out_list;
                count = (uint16_t)ap_req.u.wifi_ap_scan.count;
                if (list != NULL)
                {
                    for (i = 0; i < count; i++)
                    {
                        int ch = list[i].channel;
                        if ((ch >= WIFI_SURVEY_CH_MIN) && (ch <= WIFI_SURVEY_CH_MAX))
                        {
                            uint8_t idx = (uint8_t)(ch - WIFI_SURVEY_CH_MIN);
                            ch_count[idx]++;
                            if (list[i].rssi > ch_best_rssi[idx])
                            {
                                ch_best_rssi[idx] = (int16_t)list[i].rssi;
                            }
                            total_aps++;
                        }
                    }
                }
            }
            else
            {
                m1_u8g2_firstpage();
                u8g2_DrawStr(&m1_u8g2, 6, 15, "Survey failed");
                m1_u8g2_nextpage();
                osDelay(1000);
            }
        }

        /* Draw survey screen */
        {
            uint8_t r;
            uint8_t y = 14 + M1_GUI_FONT_HEIGHT - 1;

            m1_u8g2_firstpage();
            /* heading in the proven readable submenu font (matches Choose Network/Client) */
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "2.4G Survey");

            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 30, M1_GUI_ROW_SPACING*2, 8, 8, arrowleft_8x8);
            u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 15, M1_GUI_ROW_SPACING*2, 8, 8, arrowright_8x8);

            snprintf(line, sizeof(line), "%uAP", (unsigned int)total_aps);
            {
                /* right-align the AP-total just left of the arrows so it never overlaps the heading */
                u8g2_uint_t cw = u8g2_GetStrWidth(&m1_u8g2, line);
                u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 32 - cw), M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, line);
            }

            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            for (r = 0; r < WIFI_SURVEY_ROWS; r++)
            {
                uint8_t idx = (uint8_t)(top_row + r);
                if (idx >= WIFI_SURVEY_CH_SPAN)
                {
                    break;
                }
                if (ch_count[idx] > 0)
                {
                    snprintf(line, sizeof(line), "Ch%2u: %2u AP %ddBm",
                             (unsigned int)(idx + WIFI_SURVEY_CH_MIN),
                             (unsigned int)ch_count[idx],
                             (int)ch_best_rssi[idx]);
                }
                else
                {
                    snprintf(line, sizeof(line), "Ch%2u: --",
                             (unsigned int)(idx + WIFI_SURVEY_CH_MIN));
                }
                u8g2_DrawStr(&m1_u8g2, 2, y, line);
                y += M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
            }
            m1_u8g2_nextpage();
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret == pdTRUE)
        {
            if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            {
                (void)xQueueReceive(button_events_q_hdl, &this_button_status, 0);

                if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    exit_survey = 1U;
                }
                else if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    if (top_row > 0)
                    {
                        top_row--;
                    }
                }
                else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    if ((uint8_t)(top_row + WIFI_SURVEY_ROWS) < WIFI_SURVEY_CH_SPAN)
                    {
                        top_row++;
                    }
                }
                else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    request_rescan = 1U;   /* one-shot re-scan on demand */
                }
            }
        }
    }

    wifi_scan_common_exit_cleanup(&ap_req, &sta_req);
    wifi_scan_restore_usbcdc_mode_if_needed(prev_usbcdc_mode, restore_usbcdc_mode);
} //void wifi_survey(void)


/*============================================================================*/
/* SAVED NETWORKS — SD-backed reconnaissance target list                       */
/* CSV format, BSSID = persistent identity. No client/password/handshake data. */
/* Columns: version,ssid,bssid,channel,rssi,security,timestamp                 */
/* (timestamp left empty until a valid wall-clock source is confirmed).        */
/*============================================================================*/

typedef struct {
    char ssid[33];       /* may be empty for a hidden network */
    char bssid[18];      /* canonical uppercase "XX:XX:XX:XX:XX:XX" */
    int  channel;
    int  rssi;
    char security[16];   /* text, e.g. "WPA2"; unknown values preserved */
} wifi_saved_entry_t;

static wifi_saved_entry_t g_saved[WIFI_SAVED_MAX];
static uint16_t g_saved_count = 0;

/* Copy up to 17 chars of a BSSID and upper-case A-F for a canonical form. */
static void wifi_bssid_to_upper(char *dst, const char *src)
{
    uint8_t i;
    for (i = 0; (i < 17U) && src[i]; i++)
    {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* True iff s is exactly a canonical "XX:XX:XX:XX:XX:XX" BSSID (17 chars:
 * hex digit pairs separated by ':' at positions 2,5,8,11,14, nothing more). */
static bool wifi_bssid_is_canonical(const char *s)
{
    int i;
    if (!s) { return false; }
    for (i = 0; i < 17; i++)
    {
        if ((i % 3) == 2) { if (s[i] != ':') { return false; } }
        else               { if (!isxdigit((unsigned char)s[i])) { return false; } }
    }
    return s[17] == '\0';
}

/* Bounded, checked decimal-integer parse: rejects empty input and any
 * trailing non-numeric characters (unlike atoi(), which silently accepts
 * and truncates malformed input). */
static bool wifi_parse_int_field(const char *s, int *out)
{
    char *endp = NULL;
    long v;

    if (!s || !s[0] || !out) { return false; }
    v = strtol(s, &endp, 10);
    if (!endp || (*endp != '\0') || (endp == s)) { return false; }
    *out = (int)v;
    return true;
}

/* Append a strictly-quoted CSV field to dst: wrap in "" and escape " as "". */
static void wifi_csv_quote(char *dst, size_t dstsz, const char *src)
{
    size_t di = 0;
    if (di < (dstsz - 1U)) { dst[di++] = '"'; }
    for (; *src && (di < (dstsz - 3U)); src++)
    {
        if (*src == '"') { dst[di++] = '"'; dst[di++] = '"'; }
        else             { dst[di++] = *src; }
    }
    if (di < (dstsz - 1U)) { dst[di++] = '"'; }
    dst[di] = '\0';
}

/* Read one CSV field starting at p (quoted or bare) into out; return next-field ptr. */
static char *wifi_csv_next_field(char *p, char *out, size_t outsz)
{
    size_t oi = 0;
    if (*p == '"')
    {
        p++;
        while (*p)
        {
            if (*p == '"')
            {
                if (*(p + 1) == '"') { if (oi < (outsz - 1U)) out[oi++] = '"'; p += 2; }
                else                 { p++; break; }
            }
            else { if (oi < (outsz - 1U)) out[oi++] = *p; p++; }
        }
        while (*p && (*p != ',')) { p++; }
    }
    else
    {
        while (*p && (*p != ',')) { if (oi < (outsz - 1U)) out[oi++] = *p; p++; }
    }
    if (*p == ',') { p++; }
    out[oi] = '\0';
    return p;
}

/* Find a saved entry by canonical (uppercase) BSSID. Returns index or -1. */
static int wifi_saved_find(const char *bssid_upper)
{
    uint16_t i;
    for (i = 0; i < g_saved_count; i++)
    {
        if (strcmp(g_saved[i].bssid, bssid_upper) == 0) { return (int)i; }
    }
    return -1;
}

/* Deterministically reconcile WIFI_SAVED_PATH/.tmp/.bak left behind by a
 * replacement interrupted at any point (power loss, SD removal). Must be
 * called before WIFI_SAVED_PATH is opened for reading.
 *
 * The writer only ever creates WIFI_SAVED_BAK_PATH by renaming an existing
 * WIFI_SAVED_PATH onto it, and that rename happens
 * strictly after the new WIFI_SAVED_TMP_PATH has been fully written, synced
 * and closed. So ".bak exists" is proof the current .tmp (if any) finished
 * writing cleanly before this cycle's promotion began -- the distinguishing
 * fact that makes forward recovery safe. */
static void wifi_saved_recover_replace(void)
{
    bool final_exists = (f_stat(WIFI_SAVED_PATH, NULL) == FR_OK);
    bool bak_exists   = (f_stat(WIFI_SAVED_BAK_PATH, NULL) == FR_OK);
    bool tmp_exists   = (f_stat(WIFI_SAVED_TMP_PATH, NULL) == FR_OK);

    if (bak_exists)
    {
        if (final_exists)
        {
            /* Promotion already completed; .bak is a superseded leftover. */
            (void)f_unlink(WIFI_SAVED_BAK_PATH);
        }
        else if (tmp_exists && (f_rename(WIFI_SAVED_TMP_PATH, WIFI_SAVED_PATH) == FR_OK))
        {
            /* Interrupted mid-promotion; .tmp is proven complete -> finish it. */
            (void)f_unlink(WIFI_SAVED_BAK_PATH);
        }
        else
        {
            /* Forward completion unavailable or failed -> restore the last
             * known-good database rather than leave none present. */
            (void)f_rename(WIFI_SAVED_BAK_PATH, WIFI_SAVED_PATH);
        }
        (void)f_unlink(WIFI_SAVED_TMP_PATH);
        return;
    }

    if (!final_exists && tmp_exists)
    {
        /* No prior database was ever at risk (either this is the first
         * save, or a previous cycle already completed and cleaned up) ->
         * recover forward. If .tmp turns out malformed, the bounded loader
         * below safely skips bad rows rather than misbehaving. */
        (void)f_rename(WIFI_SAVED_TMP_PATH, WIFI_SAVED_PATH);
        return;
    }

    if (final_exists && tmp_exists)
    {
        /* A valid final already exists; an orphaned .tmp from an attempt
         * that never reached promotion is disposable. */
        (void)f_unlink(WIFI_SAVED_TMP_PATH);
    }
}

/* Load g_saved[] from SD using a bounded line-by-line read (f_gets()), so a
 * database at the writer's full 64-entry/~160-byte-per-line capacity can
 * never exceed the read buffer the way a fixed whole-file buffer could.
 * Returns 0 on success (incl. missing file => empty), 2 on SD/read error.
 * Malformed, overlong, truncated, wrong-version or duplicate-BSSID rows are
 * skipped safely. */
static uint8_t wifi_saved_load(void)
{
    FIL f;
    FRESULT fr;
    char line[WIFI_SAVED_LINE_MAX];

    g_saved_count = 0;
    wifi_saved_recover_replace();

    fr = f_open(&f, WIFI_SAVED_PATH, FA_READ);
    if (fr != FR_OK)
    {
        if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH)) { return 0U; } /* empty list */
        return 2U; /* SD unavailable / error */
    }

    while ((g_saved_count < WIFI_SAVED_MAX) && (f_gets(line, (int)sizeof(line), &f) != NULL))
    {
        size_t len = strlen(line);
        bool complete = (len > 0U) && ((line[len - 1U] == '\n') || f_eof(&f));

        if (!complete)
        {
            /* Line did not fit in the buffer: it is overlong/truncated by
             * definition (the writer never emits a line this long) ->
             * reject the whole record and discard its remainder so the
             * next f_gets() resynchronizes at the following line. */
            char discard[WIFI_SAVED_LINE_MAX];
            while (f_gets(discard, (int)sizeof(discard), &f) != NULL)
            {
                size_t dl = strlen(discard);
                if ((dl > 0U) && ((discard[dl - 1U] == '\n') || f_eof(&f))) { break; }
            }
            continue;
        }

        while (len && ((line[len - 1U] == '\n') || (line[len - 1U] == '\r'))) { line[--len] = '\0'; }

        if ((len == 0U) || (line[0] == '#'))
        {
            continue; /* blank line or "#M1SAVEDNET" magic marker */
        }

        {
            wifi_saved_entry_t e;
            char ver[8], ch[12], rs[12], ts[24];
            char up[18];
            int ver_val, ch_val, rs_val;
            char *fp = line;

            memset(&e, 0, sizeof(e));
            fp = wifi_csv_next_field(fp, ver, sizeof(ver));
            fp = wifi_csv_next_field(fp, e.ssid, sizeof(e.ssid));
            fp = wifi_csv_next_field(fp, e.bssid, sizeof(e.bssid));
            fp = wifi_csv_next_field(fp, ch, sizeof(ch));
            fp = wifi_csv_next_field(fp, rs, sizeof(rs));
            fp = wifi_csv_next_field(fp, e.security, sizeof(e.security));
            (void)wifi_csv_next_field(fp, ts, sizeof(ts));

            /* Require the exact supported format version (not merely a
             * leading digit), a structurally-canonical BSSID, and bounded
             * checked numeric fields; skip the row (header line included)
             * on any failure. */
            if (!wifi_parse_int_field(ver, &ver_val) || (ver_val != WIFI_SAVED_FMT_VER)) { continue; }
            if (!wifi_bssid_is_canonical(e.bssid)) { continue; }
            if (!wifi_parse_int_field(ch, &ch_val) || !wifi_parse_int_field(rs, &rs_val)) { continue; }

            wifi_bssid_to_upper(up, e.bssid);
            if (wifi_saved_find(up) >= 0)
            {
                continue; /* duplicate BSSID: first-seen entry wins, deterministically */
            }

            strncpy(e.bssid, up, sizeof(e.bssid) - 1U);
            e.bssid[sizeof(e.bssid) - 1U] = '\0';
            e.channel = ch_val;
            e.rssi = rs_val;
            g_saved[g_saved_count++] = e;
        }
    }

    return (f_close(&f) == FR_OK) ? 0U : 2U;
}

/* Write g_saved[] to a temp file, then promote it over the final file via a
 * .bak-preserving rename sequence: the previous valid database is kept as
 * WIFI_SAVED_BAK_PATH until the new one is confirmed in place, so a failure
 * at any step leaves at least one complete, loadable database on the card.
 * This is a *recoverable* replacement, not an atomic one -- FatFs performs
 * it as two separate rename operations, not one -- and wifi_saved_load()'s
 * wifi_saved_recover_replace() deterministically finishes or rolls back an
 * interruption between them. Returns 0 on success, 1 on any failure. */
static uint8_t wifi_saved_write_recoverable(void)
{
    FIL f;
    FRESULT fr;
    UINT bw = 0;
    uint16_t i;
    char line[160];
    char qssid[70];
    char qsec[36];
    bool had_prior_final;
    static const char hdr[] = "#M1SAVEDNET\r\nversion,ssid,bssid,channel,rssi,security,timestamp\r\n";

    if (fs_directory_ensure(DRIVE0_WIFI) != FR_OK) { return 1U; }

    fr = f_open(&f, WIFI_SAVED_TMP_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { return 1U; }

    fr = f_write(&f, hdr, (UINT)strlen(hdr), &bw);
    if ((fr != FR_OK) || (bw != strlen(hdr))) { f_close(&f); f_unlink(WIFI_SAVED_TMP_PATH); return 1U; }

    for (i = 0; i < g_saved_count; i++)
    {
        int n;
        wifi_csv_quote(qssid, sizeof(qssid), g_saved[i].ssid);
        wifi_csv_quote(qsec, sizeof(qsec), g_saved[i].security);
        n = snprintf(line, sizeof(line), "%d,%s,%s,%d,%d,%s,\r\n",
                     WIFI_SAVED_FMT_VER, qssid, g_saved[i].bssid,
                     g_saved[i].channel, g_saved[i].rssi, qsec);
        if ((n <= 0) || (n >= (int)sizeof(line))) { f_close(&f); f_unlink(WIFI_SAVED_TMP_PATH); return 1U; }
        fr = f_write(&f, line, (UINT)n, &bw);
        if ((fr != FR_OK) || (bw != (UINT)n)) { f_close(&f); f_unlink(WIFI_SAVED_TMP_PATH); return 1U; }
    }

    fr = f_close(&f);
    if (fr != FR_OK) { f_unlink(WIFI_SAVED_TMP_PATH); return 1U; }

    /* Recoverable promotion: preserve the previous database as .bak before
     * touching it, so any failure below still leaves one complete database
     * present (either the restored previous one, or the newly-promoted one). */
    (void)f_unlink(WIFI_SAVED_BAK_PATH); /* clear a stale backup; ok if absent */

    fr = f_rename(WIFI_SAVED_PATH, WIFI_SAVED_BAK_PATH);
    had_prior_final = (fr == FR_OK);
    if (!had_prior_final && (fr != FR_NO_FILE) && (fr != FR_NO_PATH))
    {
        /* Previous database could not be safely preserved -> abort, leaving
         * it exactly where it was; nothing has been promoted. */
        (void)f_unlink(WIFI_SAVED_TMP_PATH);
        return 1U;
    }

    fr = f_rename(WIFI_SAVED_TMP_PATH, WIFI_SAVED_PATH);
    if (fr != FR_OK)
    {
        if (had_prior_final) { (void)f_rename(WIFI_SAVED_BAK_PATH, WIFI_SAVED_PATH); }
        (void)f_unlink(WIFI_SAVED_TMP_PATH);
        return 1U;
    }

    (void)f_unlink(WIFI_SAVED_BAK_PATH); /* promotion confirmed; backup no longer needed */
    return 0U;
}

/* Save or update the highlighted network by BSSID.
 * Returns: 0 updated, 1 added, 2 list full, 3 save failed. */
static uint8_t wifi_saved_add_or_update(const wifi_scanlist_t *ap)
{
    char up[18];
    int idx;
    wifi_saved_entry_t *e;
    wifi_saved_entry_t prev; /* snapshot for update-failure rollback */

    wifi_bssid_to_upper(up, (const char *)ap->bssid);
    idx = wifi_saved_find(up);

    if (idx >= 0)
    {
        e = &g_saved[idx]; /* update in place (dedup by BSSID) */
        prev = *e;
    }
    else
    {
        if (g_saved_count >= WIFI_SAVED_MAX) { return 2U; } /* full */
        e = &g_saved[g_saved_count];
        memset(e, 0, sizeof(*e));
        strncpy(e->bssid, up, sizeof(e->bssid) - 1U);
    }

    strncpy(e->ssid, (const char *)ap->ssid, sizeof(e->ssid) - 1U);
    e->ssid[sizeof(e->ssid) - 1U] = '\0';
    e->channel = ap->channel;
    e->rssi = ap->rssi;
    strncpy(e->security, wifi_auth_mode_to_str(ap->encryption_mode), sizeof(e->security) - 1U);
    e->security[sizeof(e->security) - 1U] = '\0';

    if (idx < 0) { g_saved_count++; }

    if (wifi_saved_write_recoverable() != 0U)
    {
        if (idx < 0) { g_saved_count--; }   /* roll back the in-memory add */
        else         { *e = prev; }         /* restore the previous field values */
        return 3U;
    }
    return (idx >= 0) ? 0U : 1U;
}

/* Delete a saved entry by index and persist. On save failure, the deleted
 * entry and its original position are restored so in-memory state always
 * matches what remains stored on the card. Returns 0 ok, 1 save failed. */
static uint8_t wifi_saved_delete_index(uint16_t idx)
{
    uint16_t i;
    wifi_saved_entry_t removed;

    if (idx >= g_saved_count) { return 0U; }

    removed = g_saved[idx];
    for (i = idx; (i + 1U) < g_saved_count; i++)
    {
        g_saved[i] = g_saved[i + 1U];
    }
    g_saved_count--;

    if (wifi_saved_write_recoverable() != 0U)
    {
        /* Restore the deleted entry at its original index and ordering. */
        for (i = g_saved_count; i > idx; i--)
        {
            g_saved[i] = g_saved[i - 1U];
        }
        g_saved[idx] = removed;
        g_saved_count++;
        return 1U;
    }
    return 0U;
}

/*========================= Scan Networks (browse + save) ====================*/

/* One network per screen: heading + SSID/BSSID/RSSI + saved indicator + hints. */
/* Full network details view (from the network action menu); returns on BACK. */
static void wifi_network_details(const wifi_scanlist_t *ap)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    char buf[28];
    char up[18];
    bool saved;

    wifi_bssid_to_upper(up, (const char *)ap->bssid);
    saved = (wifi_saved_find(up) >= 0);

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Network Details");
    if (ap->ssid[0] == '\0') { snprintf(buf, sizeof(buf), "SSID: Hidden Network"); }
    else { snprintf(buf, sizeof(buf), "SSID: %.20s", (const char *)ap->ssid); }
    u8g2_DrawStr(&m1_u8g2, 2, 23, buf);
    snprintf(buf, sizeof(buf), "BSSID: %s", (const char *)ap->bssid);
    u8g2_DrawStr(&m1_u8g2, 2, 34, buf);
    snprintf(buf, sizeof(buf), "Ch:%d  RSSI:%ddBm", ap->channel, ap->rssi);
    u8g2_DrawStr(&m1_u8g2, 2, 45, buf);
    snprintf(buf, sizeof(buf), "Sec:%s  %s", wifi_auth_mode_to_str(ap->encryption_mode),
             saved ? "[SAVED]" : "");
    u8g2_DrawStr(&m1_u8g2, 2, 56, buf);
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    for (;;)
    {
        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return; }
    }
}

/* Recon: browse the devices detected on the selected network (Networks only).
 * Uses the shared device scan/browse. Never selects a device or sets a Deauth target. */
static void wifi_networks_scan_devices(uint16_t net_idx, const char *net_ssid, ctrl_cmd_t *sta_req)
{
    static const char *const opt_nodev[2] = { "Rescan", "Back" };
    uint16_t dev_sel = 0;
    uint8_t looping = 1;

    while (looping)
    {
        char sel_err[40] = {0};
        uint16_t dcount = wifi_scan_devices(net_idx, sta_req, sel_err, sizeof(sel_err)); /* "Searching Devices..." */
        if (dcount == 0xFFFEU)
        {
            uint8_t o = wifi_dc_menu(net_ssid, "Select failed", sel_err, opt_nodev, 2, 0);
            if (o != 0U) { return; }
            continue;
        }
        if (dcount == 0xFFFFU)
        {
            uint8_t o = wifi_dc_menu(net_ssid, "Device scan failed", NULL, opt_nodev, 2, 0);
            if (o != 0U) { return; }
            continue;
        }
        if (dcount == 0U)
        {
            uint8_t o = wifi_dc_menu(net_ssid, "No devices detected", NULL, opt_nodev, 2, 0);
            if (o != 0U) { return; }
            continue;
        }
        for (;;)
        {
            if (wifi_browse(sta_req->u.wifi_ap_scan.out_list, dcount, 1, net_ssid,
                            "Detected Devices", 0, "Details", &dev_sel) == 1U)
            {
                return; /* BACK -> network action menu */
            }
            /* CENTER -> Device Details (recon only) */
            wifi_device_details(net_ssid, &sta_req->u.wifi_ap_scan.out_list[dev_sel]);
        }
    }
}

void wifi_scan_networks(void)
{
    ctrl_cmd_t ap_req = CTRL_CMD_DEFAULT_REQ();
    ctrl_cmd_t sta_req = CTRL_CMD_DEFAULT_REQ();
    enCdcMode prev_usbcdc_mode;
    uint8_t restore_usbcdc_mode = 0;
    uint8_t ready;
    uint16_t count = 0;
    uint16_t sel = 0;
    uint8_t exit_flag = 0;
    uint8_t rescan = 1;
    uint8_t force_scan = 0; /* set by the manual-refresh control (wifi_browse() returning 2) */
    static const char *const opt_rescan[2]  = { "Rescan", "Back" };
    static const char *const opt_actions[5] = { "Scan for Devices", "Save Network", "Network Details", "Saved Networks", "Back" };

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    ready = wifi_survey_prepare_esp32(&prev_usbcdc_mode, &restore_usbcdc_mode);
    if (!ready)
    {
        wifi_deauth_show_status_message("ESP32 not ready", "Try bridge reset", 1500U);
        exit_flag = 1;
    }
    else
    {
        (void)wifi_saved_load(); /* for the SAVED indicator (missing file => empty) */
    }

    while (!exit_flag)
    {
        uint8_t a;

        if (rescan)
        {
            rescan = 0;
            wifi_ui_draw_scanning("Scanning Networks...");
            wifi_scan_free_lists(&ap_req, NULL);
            ap_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
            ap_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
            /* Initial entry / failure retry: reuse the shared AP cache if
             * fresh (this is what makes switching Networks<->Handshake<->
             * Deauth Client not rescan). force_scan is only set by the
             * manual-refresh control below. */
            if (wifi_ap_scan_list_cached(&ap_req, force_scan != 0) != SUCCESS)
            {
                uint8_t o = wifi_dc_menu(NULL, "Network scan failed", NULL, opt_rescan, 2, 0);
                if (o == 0U) { rescan = 1; } else { exit_flag = 1; }
                force_scan = 0;
                continue;
            }
            force_scan = 0;
            count = (uint16_t)ap_req.u.wifi_ap_scan.count;
            sel = 0;
            if (count == 0U)
            {
                uint8_t o = wifi_dc_menu(NULL, "No networks found", NULL, opt_rescan, 2, 0);
                if (o == 0U) { rescan = 1; } else { exit_flag = 1; }
                continue;
            }
        }

        /* Browse networks (LEFT/RIGHT page, UP/DOWN = manual-refresh
         * control, SAVED indicator); CENTER opens the action menu. */
        {
            uint8_t br = wifi_browse(ap_req.u.wifi_ap_scan.out_list, count, 0, NULL,
                                     "Networks", 1, "Actions", &sel);
            if (br == 1U)
            {
                exit_flag = 1;
                continue;
            }
            if (br == 2U)
            {
                force_scan = 1;
                rescan = 1;
                continue;
            }
        }

        {
            const wifi_scanlist_t *net = &ap_req.u.wifi_ap_scan.out_list[sel];
            char net_ssid[34];
            uint8_t in_actions = 1;
            snprintf(net_ssid, sizeof(net_ssid), "%s",
                     (net->ssid[0] != '\0') ? (const char *)net->ssid : "Hidden Network");

            /* Network Actions loop: sub-actions (Scan for Devices, Network Details,
             * Saved Networks) return here to the actions menu; Back/BACK returns to
             * the network result (outer loop). */
            while (in_actions)
            {
                a = wifi_dc_menu(net_ssid, NULL, NULL, opt_actions, 5, 0);
                if (a == 0U) /* Scan for Devices (recon) -> back returns here */
                {
                    wifi_networks_scan_devices(sel, net_ssid, &sta_req);
                }
                else if (a == 1U) /* Save Network */
                {
                    uint8_t r = wifi_saved_add_or_update(net);
                    const char *msg = (r == 0U) ? "Updated" :
                                      (r == 1U) ? "Saved" :
                                      (r == 2U) ? "Saved list full" : "Save failed";
                    wifi_deauth_show_status_message(msg, NULL, 800U);
                }
                else if (a == 2U) /* Network Details -> back returns here */
                {
                    wifi_network_details(net);
                }
                else if (a == 3U) /* Saved Networks browser -> back returns here */
                {
                    wifi_saved_browser();
                }
                else /* a == 4 (Back) or 0xFF: return to the network result */
                {
                    in_actions = 0;
                }
            }
        }
    }

    wifi_scan_free_lists(&ap_req, &sta_req);
    xQueueReset(main_q_hdl);
    m1_esp32_deinit();
    wifi_scan_restore_usbcdc_mode_if_needed(prev_usbcdc_mode, restore_usbcdc_mode);
}

/*========================= Saved Networks browser ===========================*/

static void wifi_saved_browser_draw(uint16_t sel)
{
    char buf[28];
    u8g2_uint_t cw;
    const wifi_saved_entry_t *e = &g_saved[sel];

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 11, "Saved Networks");
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned int)(sel + 1U), (unsigned int)g_saved_count);
    cw = u8g2_GetStrWidth(&m1_u8g2, buf);
    u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 4 - cw), 11, buf);

    if (e->ssid[0] == '\0') { snprintf(buf, sizeof(buf), "SSID: Hidden Network"); }
    else { snprintf(buf, sizeof(buf), "SSID: %.18s", e->ssid); }
    u8g2_DrawStr(&m1_u8g2, 2, 23, buf);
    snprintf(buf, sizeof(buf), "BSSID: %s", e->bssid);
    u8g2_DrawStr(&m1_u8g2, 2, 34, buf);
    snprintf(buf, sizeof(buf), "Ch:%d RSSI:%d %.4s", e->channel, e->rssi, e->security);
    u8g2_DrawStr(&m1_u8g2, 2, 50, buf);
    /* Same Sub-GHz-style CENTER-action footer bar; this caller's action is Actions. */
    wifi_footer_bar(NULL, "Actions");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

void wifi_saved_browser(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;
    uint16_t sel = 0;
    uint8_t exit_flag = 0;
    uint8_t st;
    static const char *const opt_ok[1]     = { "Back" };
    static const char *const opt_actions[2] = { "Delete", "Back" };
    static const char *const opt_delcfm[2] = { "Delete", "Cancel" };

    st = wifi_saved_load();
    if (st == 2U)
    {
        (void)wifi_dc_menu("Saved Networks", "SD unavailable", NULL, opt_ok, 1, 0);
        return;
    }
    if (g_saved_count == 0U)
    {
        (void)wifi_dc_menu("Saved Networks", "No saved networks", NULL, opt_ok, 1, 0);
        return;
    }

    while (!exit_flag)
    {
        wifi_saved_browser_draw(sel);

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            exit_flag = 1;
        }
        else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) /* previous, wrap */
        {
            sel = (sel == 0U) ? (uint16_t)(g_saved_count - 1U) : (uint16_t)(sel - 1U);
        }
        else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) /* next, wrap */
        {
            sel = (uint16_t)(((sel + 1U) >= g_saved_count) ? 0U : (sel + 1U));
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            char ssid_line[28];
            char bssid_line[28];
            uint8_t a;
            snprintf(ssid_line, sizeof(ssid_line), "%.22s",
                     (g_saved[sel].ssid[0] == '\0') ? "Hidden Network" : g_saved[sel].ssid);
            snprintf(bssid_line, sizeof(bssid_line), "%s", g_saved[sel].bssid);
            /* The per-entry browse screen already shows full details (SSID/BSSID/
             * Ch/RSSI/Security), so the actions menu just offers Delete/Back. */
            a = wifi_dc_menu("Saved", ssid_line, bssid_line, opt_actions, 2, 0);
            if (a == 0U) /* Delete -> explicit confirm */
            {
                uint8_t c = wifi_dc_menu("Delete entry?", ssid_line, bssid_line, opt_delcfm, 2, 1);
                if (c == 0U) /* explicit Delete */
                {
                    uint8_t dr = wifi_saved_delete_index(sel);
                    wifi_deauth_show_status_message((dr == 0U) ? "Deleted" : "Save failed", NULL, 800U);
                    if (g_saved_count == 0U) { exit_flag = 1; }
                    else if (sel >= g_saved_count) { sel = (uint16_t)(g_saved_count - 1U); }
                }
            }
            /* a == 1 (Back) or 0xFF: return to list */
        }
        /* UP/DOWN: no action on this paged result screen */
    }

    xQueueReset(main_q_hdl);
}
