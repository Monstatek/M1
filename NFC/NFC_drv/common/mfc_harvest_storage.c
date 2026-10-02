/*
 * mfc_harvest_storage.c - FatFs/SD binding for the .m1h harvester
 *                         (Harvester Increment 2).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See mfc_harvest_storage.h. Device-only; uses the project's FatFs wrappers.
 * No dynamic allocation. No RF/radio dependency.
 */
#include "mfc_harvest_storage.h"
#include "mfc_harvest.h"
#include "main.h"
#include "stm32h5xx_hal.h"     /* HAL_GetTick */
#include "m1_system.h"         /* S_M1_Buttons_Status (needed by m1_file_browser.h) */
#include "m1_file_browser.h"   /* m1_fb_open_new_file / _write_to_file / _close_file */
#include "m1_file_util.h"      /* fs_directory_ensure */
#include "m1_sdcard.h"         /* m1_sdcard_get_info / _get_free_capacity */
#include "m1_sdcard_provision.h"
#include <stdio.h>
#include <string.h>

#define HS_NFC_DIR      M1_SD_DIR_NFC
#define HS_RECOVER_DIR  M1_SD_DIR_NFC_RECOVER
#define HS_MIN_FREE     4096u     /* bytes of free space required to start */

static char hexnib(uint8_t v) {
    return (char)((v < 10) ? ('0' + v) : ('A' + (v - 10)));
}

int mfc_harvest_storage_start(mfc_harvest_storage_t *st, const uint8_t *uid,
                              uint8_t uid_len, uint32_t ticks) {
    if (st == NULL || uid == NULL ||
        (uid_len != 4 && uid_len != 7 && uid_len != 10)) {
        return MFC_HS_ERR_ARG;
    }
    st->open = 0;
    st->path[0] = '\0';

    /* SD presence / free space */
    (void)m1_sdcard_get_info();
    if (m1_sdcard_get_free_capacity() < HS_MIN_FREE) return MFC_HS_ERR_SD;

    /* ensure 0:/NFC then 0:/NFC/recover (each single-level) */
    if (fs_directory_ensure(HS_NFC_DIR) != FR_OK)     return MFC_HS_ERR_DIR;
    if (fs_directory_ensure(HS_RECOVER_DIR) != FR_OK) return MFC_HS_ERR_DIR;

    /* build "0:/nfc/recover/<uid-hex>_<ticks08X>.m1h" (max ~49 bytes < 64) */
    size_t p = 0;
    const char *dir = HS_RECOVER_DIR "/";
    for (const char *c = dir; *c != '\0'; c++) st->path[p++] = *c;
    for (uint8_t i = 0; i < uid_len; i++) {
        st->path[p++] = hexnib((uint8_t)(uid[i] >> 4));
        st->path[p++] = hexnib((uint8_t)(uid[i] & 0x0F));
    }
    st->path[p++] = '_';
    for (int s = 28; s >= 0; s -= 4) st->path[p++] = hexnib((uint8_t)((ticks >> s) & 0x0F));
    const char *ext = ".m1h";
    for (const char *c = ext; *c != '\0'; c++) st->path[p++] = *c;
    st->path[p] = '\0';

    /* m1_fb_open_new_file: 0 = success, non-0 = failure */
    if (m1_fb_open_new_file(&st->file, st->path) != 0) return MFC_HS_ERR_OPEN;
    st->open = 1;
    return MFC_HS_OK;
}

bool mfc_harvest_storage_flush(void *ctx, const uint8_t *bytes, size_t len) {
    mfc_harvest_storage_t *st = (mfc_harvest_storage_t *)ctx;
    if (st == NULL || st->open == 0 || bytes == NULL) return false;
    if (len == 0) return true;
    if (len > 0xFFFFu) return false;   /* m1_fb_write_to_file size is uint16_t */
    uint16_t w = m1_fb_write_to_file(&st->file, (const char *)bytes, (uint16_t)len);
    return (w == (uint16_t)len);       /* short write -> false -> FLUSH_FAILED */
}

int mfc_harvest_storage_close(mfc_harvest_storage_t *st) {
    if (st == NULL) return MFC_HS_ERR_ARG;
    if (st->open) {
        (void)m1_fb_close_file(&st->file);
        st->open = 0;
    }
    return MFC_HS_OK;
}

int mfc_harvest_mock_run(char *msg, size_t msglen) {
    /* static: FIL+path (~0.6 KiB) and the ~552 B serializer ctx are kept off the
     * 4 KiB CLI task stack. Only present because the debug trigger is linked. */
    static mfc_harvest_storage_t st;
    static mfc_harvest_t         h;
    const uint8_t uid[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint32_t ticks = HAL_GetTick();

    int r = mfc_harvest_storage_start(&st, uid, 4, ticks);
    if (r != MFC_HS_OK) {
        if (msg && msglen) snprintf(msg, msglen, "mock harvest: SD/open failed (%d)\r\n", r);
        return r;
    }

    mfc_harvest_init(&h, mfc_harvest_storage_flush, &st);

    mfc_harvest_file_info_t info;
    memset(&info, 0, sizeof info);
    info.uid_len = 4;
    info.uid[0] = uid[0]; info.uid[1] = uid[1]; info.uid[2] = uid[2]; info.uid[3] = uid[3];
    info.atqa = 0x0004; info.sak = 0x08;
    info.capture_ticks = ticks; info.capability_flags = 0;
    info.record_count = 2;
    mfc_harvest_begin_file(&h, &info);

    /* record 1: a mock MFKey64 transcript */
    mfc_mfkey64_record_t rk = { 0x3C, 0x60, 0xCE844261u, 0xF8049CCBu,
                                0x0525C84Fu, 0x9431CC40u };
    mfc_harvest_write_mfkey64(&h, &rk);

    /* record 2: a mock Nested sample-set (2 samples) */
    mfc_card_only_hdr_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.src_block = 0; hdr.src_keytype = 0x60;
    hdr.tgt_block = 8; hdr.tgt_keytype = 0x60; hdr.known_key_ref = 0;
    mfc_harvest_begin_nested(&h, &hdr, 0x11223344u, 2);
    mfc_nested_sample_t s0 = { 0xB0000000u, 0x00, 1000 };
    mfc_nested_sample_t s1 = { 0xB0000001u, 0x01, 1001 };
    mfc_harvest_add_nested_sample(&h, &s0);
    mfc_harvest_add_nested_sample(&h, &s1);
    mfc_harvest_end_record(&h);

    mfc_harvest_status_t hs = mfc_harvest_finalize(&h);
    (void)mfc_harvest_storage_close(&st);

    if (hs != MFC_HARVEST_OK) {
        if (msg && msglen) snprintf(msg, msglen, "mock harvest: serialize failed (%d)\r\n", (int)hs);
        return MFC_HS_ERR_SERIAL;
    }
    if (msg && msglen) snprintf(msg, msglen, "mock harvest: wrote %s\r\n", st.path);
    return MFC_HS_OK;
}
