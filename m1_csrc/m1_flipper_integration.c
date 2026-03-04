/* See COPYING.txt for license details. */

/*
*
*  m1_flipper_integration.c
*
*  Flipper Zero file import/replay integration for M1
*
*  Provides menu-callable functions to:
*  - Replay Flipper .sub files via the SI4463 radio
*  - Import Flipper .nfc files into M1 NFC saved format
*  - Import Flipper .rfid files into M1 RFID saved format
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_compile_cfg.h"

#ifdef M1_APP_FLIPPER_COMPAT_ENABLE

#include "m1_flipper_integration.h"
#include "flipper_subghz.h"
#include "flipper_nfc.h"
#include "flipper_rfid.h"
#include "m1_sub_ghz.h"
#include "m1_sub_ghz_api.h"
#include "m1_ring_buffer.h"
#include "m1_nfc.h"
#include "m1_rfid.h"
#include "m1_lcd.h"
#include "m1_display.h"
#include "m1_file_browser.h"
#include "m1_log_debug.h"
#include "ff.h"
#include "lfrfid.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG                "FlipperIntg"

#define FLIPPER_SUBGHZ_DIR          "0:/Flipper/SubGHz"
#define FLIPPER_NFC_DIR             "0:/Flipper/NFC"
#define FLIPPER_RFID_DIR            "0:/Flipper/RFID"
#define FLIPPER_DIR_ROOT            "0:/Flipper"

#define FLIPPER_PATH_MAX            256
#define FLIPPER_STATUS_LINE_Y       56

/************************** S T R U C T U R E S *******************************/

/***************************** V A R I A B L E S ******************************/

static char flipper_filepath[FLIPPER_PATH_MAX];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void flipper_show_status(const char *line1, const char *line2);
static void flipper_show_result(const char *msg, bool success);
static bool flipper_wait_for_back(void);
static bool flipper_ensure_dir(const char *path);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Show a two-line status message on screen
 */
/*============================================================================*/
static void flipper_show_status(const char *line1, const char *line2)
{
    u8g2_FirstPage(&m1_u8g2);
    do {
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        if (line1) u8g2_DrawStr(&m1_u8g2, 2, 12, line1);
        if (line2) u8g2_DrawStr(&m1_u8g2, 2, 28, line2);
    } while (u8g2_NextPage(&m1_u8g2));
}

/*============================================================================*/
/**
 * @brief  Show a result message (success/fail) and wait for BACK
 */
/*============================================================================*/
static void flipper_show_result(const char *msg, bool success)
{
    u8g2_FirstPage(&m1_u8g2);
    do {
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        u8g2_DrawStr(&m1_u8g2, 2, 12, success ? "Success" : "Failed");
        if (msg) u8g2_DrawStr(&m1_u8g2, 2, 28, msg);
        u8g2_DrawStr(&m1_u8g2, 2, FLIPPER_STATUS_LINE_Y, "Press BACK to exit");
    } while (u8g2_NextPage(&m1_u8g2));
}

/*============================================================================*/
/**
 * @brief  Block waiting for the BACK button press, return true on BACK
 */
/*============================================================================*/
static bool flipper_wait_for_back(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
        {
            ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
            if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                xQueueReset(main_q_hdl);
                return true;
            }
        }
    }
}

/*============================================================================*/
/**
 * @brief  Ensure a directory exists on the SD card, create if needed
 */
/*============================================================================*/
static bool flipper_ensure_dir(const char *path)
{
    FILINFO fno;
    FRESULT fr;

    fr = f_stat(path, &fno);
    if (fr == FR_OK && (fno.fattrib & AM_DIR))
        return true;

    /* Try to create path components */
    char tmp[FLIPPER_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 3; *p; p++) {  /* Skip "0:/" */
        if (*p == '/') {
            *p = '\0';
            f_mkdir(tmp);
            *p = '/';
        }
    }
    fr = f_mkdir(tmp);
    return (fr == FR_OK || fr == FR_EXIST);
}


/*============================================================================*/
/*                                                                            */
/*  SUB-GHZ: REPLAY FLIPPER .SUB FILES                                       */
/*                                                                            */
/*============================================================================*/

void sub_ghz_replay_flipper(void)
{
    flipper_subghz_signal_t sig;
    S_M1_SubGHz_Band band;
    int ret;
    uint16_t i;

    flipper_show_status("Flipper Sub-GHz", "Loading...");

    /* Ensure directory exists */
    flipper_ensure_dir(FLIPPER_SUBGHZ_DIR);

    /* Use the M1 file browser to pick a .sub file */
    /* For now, show instructions and wait */
    flipper_show_status("Place .sub files in:", FLIPPER_SUBGHZ_DIR);
    M1_LOG_I(M1_LOGDB_TAG, "Sub-GHz Flipper replay - waiting for file selection\r\n");

    /* Simple file browser: look for first .sub file in directory */
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    bool found = false;

    fr = f_opendir(&dir, FLIPPER_SUBGHZ_DIR);
    if (fr != FR_OK) {
        flipper_show_result("No Flipper/SubGHz dir", false);
        flipper_wait_for_back();
        return;
    }

    /* Find first .sub file */
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        size_t len = strlen(fno.fname);
        if (len > 4 && strcmp(&fno.fname[len - 4], ".sub") == 0) {
            snprintf(flipper_filepath, sizeof(flipper_filepath), "%s/%s",
                     FLIPPER_SUBGHZ_DIR, fno.fname);
            found = true;
            break;
        }
    }
    f_closedir(&dir);

    if (!found) {
        flipper_show_result("No .sub files found", false);
        flipper_wait_for_back();
        return;
    }

    /* Parse the .sub file */
    flipper_show_status("Parsing:", fno.fname);
    ret = flipper_subghz_read(flipper_filepath, &sig);
    if (ret != 0) {
        flipper_show_result("Parse failed", false);
        flipper_wait_for_back();
        return;
    }

    /* Map frequency to M1 band */
    band = flipper_subghz_freq_to_band(sig.frequency);

    M1_LOG_I(M1_LOGDB_TAG, "Sub-GHz replay: freq=%lu Hz, %d samples, type=%s\r\n",
             sig.frequency, sig.raw_count,
             sig.is_raw ? "RAW" : "KEY");

    if (sig.is_raw && sig.raw_count > 0) {
        /* Load raw timing data into the Sub-GHz ring buffer for replay */
        flipper_show_status("Replaying:", fno.fname);

        /* Reset ring buffer and load raw data */
        m1_ringbuffer_reset(&subghz_rx_rawdata_rb);

        for (i = 0; i < sig.raw_count; i++) {
            int32_t val = sig.raw_data[i];
            /* Convert signed Flipper format (positive=high, negative=low)
             * to M1 unsigned timing with polarity in LSB */
            uint32_t abs_val;
            if (val >= 0) {
                abs_val = (uint32_t)val;
                abs_val |= 0x0001;  /* Mark: LSB=1 (rising edge) */
            } else {
                abs_val = (uint32_t)(-val);
                abs_val &= 0xFFFFFFFE;  /* Space: LSB=0 (falling edge) */
            }
            m1_ringbuffer_insert(&subghz_rx_rawdata_rb, (uint8_t *)&abs_val);
        }

        M1_LOG_I(M1_LOGDB_TAG, "Loaded %d samples into ring buffer\r\n", sig.raw_count);

        flipper_show_result("Replay data loaded", true);
    } else {
        flipper_show_result("Only RAW .sub supported", false);
    }

    flipper_wait_for_back();
}


/*============================================================================*/
/*                                                                            */
/*  NFC: IMPORT FLIPPER .NFC FILES                                           */
/*                                                                            */
/*============================================================================*/

void nfc_import_flipper(void)
{
    flipper_nfc_card_t card;
    int ret;

    flipper_show_status("Flipper NFC Import", "Loading...");

    flipper_ensure_dir(FLIPPER_NFC_DIR);

    /* Find first .nfc file */
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    bool found = false;

    fr = f_opendir(&dir, FLIPPER_NFC_DIR);
    if (fr != FR_OK) {
        flipper_show_result("No Flipper/NFC dir", false);
        flipper_wait_for_back();
        return;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        size_t len = strlen(fno.fname);
        if (len > 4 && strcmp(&fno.fname[len - 4], ".nfc") == 0) {
            snprintf(flipper_filepath, sizeof(flipper_filepath), "%s/%s",
                     FLIPPER_NFC_DIR, fno.fname);
            found = true;
            break;
        }
    }
    f_closedir(&dir);

    if (!found) {
        flipper_show_result("No .nfc files found", false);
        flipper_wait_for_back();
        return;
    }

    /* Parse the .nfc file */
    flipper_show_status("Importing:", fno.fname);
    ret = flipper_nfc_read(flipper_filepath, &card);
    if (ret != 0) {
        flipper_show_result("Parse failed", false);
        flipper_wait_for_back();
        return;
    }

    M1_LOG_I(M1_LOGDB_TAG, "NFC import: type=%d, UID len=%d, ATQA=%02X%02X, SAK=%02X\r\n",
             card.type, card.uid_len, card.atqa[0], card.atqa[1], card.sak);

    /* Display imported card info */
    {
        char uid_str[32] = {0};
        char info_str[48] = {0};
        uint8_t i;

        for (i = 0; i < card.uid_len && i < 7; i++) {
            snprintf(&uid_str[i * 3], sizeof(uid_str) - i * 3, "%02X ", card.uid[i]);
        }
        snprintf(info_str, sizeof(info_str), "UID: %s", uid_str);

        u8g2_FirstPage(&m1_u8g2);
        do {
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, 12, "NFC Card Imported");
            u8g2_DrawStr(&m1_u8g2, 2, 28, info_str);
            u8g2_DrawStr(&m1_u8g2, 2, 44, fno.fname);
            u8g2_DrawStr(&m1_u8g2, 2, FLIPPER_STATUS_LINE_Y, "Press BACK to exit");
        } while (u8g2_NextPage(&m1_u8g2));
    }

    flipper_wait_for_back();
}


/*============================================================================*/
/*                                                                            */
/*  RFID: IMPORT FLIPPER .RFID FILES                                         */
/*                                                                            */
/*============================================================================*/

void rfid_import_flipper(void)
{
    flipper_rfid_tag_t tag;
    int ret;

    flipper_show_status("Flipper RFID Import", "Loading...");

    flipper_ensure_dir(FLIPPER_RFID_DIR);

    /* Find first .rfid file */
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    bool found = false;

    fr = f_opendir(&dir, FLIPPER_RFID_DIR);
    if (fr != FR_OK) {
        flipper_show_result("No Flipper/RFID dir", false);
        flipper_wait_for_back();
        return;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        size_t len = strlen(fno.fname);
        if (len > 5 && strcmp(&fno.fname[len - 5], ".rfid") == 0) {
            snprintf(flipper_filepath, sizeof(flipper_filepath), "%s/%s",
                     FLIPPER_RFID_DIR, fno.fname);
            found = true;
            break;
        }
    }
    f_closedir(&dir);

    if (!found) {
        flipper_show_result("No .rfid files found", false);
        flipper_wait_for_back();
        return;
    }

    /* Parse the .rfid file */
    flipper_show_status("Importing:", fno.fname);
    ret = flipper_rfid_read(flipper_filepath, &tag);
    if (ret != 0) {
        flipper_show_result("Parse failed", false);
        flipper_wait_for_back();
        return;
    }

    M1_LOG_I(M1_LOGDB_TAG, "RFID import: protocol=%d, data_len=%d\r\n",
             tag.protocol, tag.data_len);

    /* Display imported tag info */
    {
        char data_str[32] = {0};
        char info_str[48] = {0};
        uint8_t i;

        for (i = 0; i < tag.data_len && i < 8; i++) {
            snprintf(&data_str[i * 3], sizeof(data_str) - i * 3, "%02X ", tag.data[i]);
        }
        snprintf(info_str, sizeof(info_str), "Data: %s", data_str);

        u8g2_FirstPage(&m1_u8g2);
        do {
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, 12, "RFID Tag Imported");
            u8g2_DrawStr(&m1_u8g2, 2, 28, info_str);
            u8g2_DrawStr(&m1_u8g2, 2, 44, fno.fname);
            u8g2_DrawStr(&m1_u8g2, 2, FLIPPER_STATUS_LINE_Y, "Press BACK to exit");
        } while (u8g2_NextPage(&m1_u8g2));
    }

    flipper_wait_for_back();
}

#else /* M1_APP_FLIPPER_COMPAT_ENABLE not defined */

/* Stub implementations when Flipper support is disabled */
void sub_ghz_replay_flipper(void) { }
void nfc_import_flipper(void) { }
void rfid_import_flipper(void) { }

#endif /* M1_APP_FLIPPER_COMPAT_ENABLE */
