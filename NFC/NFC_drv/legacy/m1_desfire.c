/*============================================================================*/
/**
 * @file    m1_desfire.c
 * @brief   MIFARE DESFire read-only identification and deeper inspection.
 *
 * Tier 1 (unchanged): identify a Type 4A DESFire from a single read-only
 * GetVersion (0x60) exchange over the ISO-DEP link RFAL already established.
 * The DESFire replies across three frames chained by the "additional frame"
 * status byte (0xAF), accumulating a 28-byte version tuple (7 hw + 7 sw + 7
 * uid + 5 batch + week + year).
 *
 * Deeper read: see m1_desfire.h for the full description and licensing.
 * Every command below is unauthenticated; every file read is
 * gated on the file's own access rights already granting free read to an
 * unauthenticated reader -- specifically, the access-rights word's Read
 * nibble (bits 15-12) OR ReadWrite nibble (bits 7-4) equals 0xE.
 * Communication mode is NOT part of this gate. No authentication, no writes, no
 * emulation, no key recovery.
 */
/*============================================================================*/
#include "m1_desfire.h"
#include "rfal_nfc.h"
#include "rfal_rf.h"
#include "rfal_utils.h"
#include "cmsis_os2.h"
#include "common/nfc_ctx.h"
#include "common/mf_desfire_parse.h"
#include "common/nfc_transit_clipper.h"
#include <string.h>

/* DESFire native command / status codes (subset needed for GetVersion). */
#define DESF_CMD_GET_VERSION      0x60U
#define DESF_STATUS_OK            0x00U   /* OPERATION_OK: final frame           */
#define DESF_STATUS_ADD_FRAME     0xAFU   /* ADDITIONAL_FRAME: more data follows */

#define DESF_VERSION_LEN          28U     /* 7 hw + 7 sw + 7 uid + 5 batch + 2   */
#define DESF_NXP_VENDOR           0x04U

/* Chained-frame accumulation caps for the deeper-read commands. These are
 * raw-byte staging sizes for commands where the CARD (not us) decides how
 * much to send back (GetApplicationIDs, GetFileIDs, GetFileSettings) --
 * generous enough to cover realistic DESFire cards without truncation;
 * mf_desfire_app_ids_parse()/mf_desfire_file_ids_parse() still detect and
 * flag truncation explicitly if an unusually large card ever exceeds them.
 * Commands where WE control the response length (ReadData/ReadRecords) are
 * bounded directly by MF_DESFIRE_DEEP_FILE_DATA_CAP instead. */
#define DESF_RAW_SMALL_CAP        8U    /* GetFreeMemory/GetKeySettings/GetKeyVersion/GetValue/SelectApplication */
#define DESF_RAW_APPIDS_CAP       258U  /* up to 86 three-byte application IDs */
#define DESF_RAW_FILEIDS_CAP      64U   /* up to 64 one-byte file IDs */
#define DESF_RAW_FILESETTINGS_CAP 24U   /* largest real body (Value, 17B) + 2B tail, rounded up */
/* Safety cap on 0xAF continuation frames per command. The largest bounded read
 * in this codebase is the Clipper history
 * file's mandatory 512-byte ReadData (desf_interpret_clipper()), which at
 * 63 payload bytes/frame needs ceil(512/63) = 9 frames minimum -- one more
 * than the previous 8-frame cap allowed, so that read could never complete.
 * Raised to 32U: generous headroom above the proven 9-frame minimum (covers
 * per-frame payloads as small as ~16 bytes for a 512-byte read, or reads up
 * to ~2000 bytes at the normal ~60-63 byte/frame rate) while still being a
 * hard, bounded ceiling appropriate for constrained firmware. */
#define DESF_MAX_CHAIN_FRAMES     32U

/*============================================================================*/
/* Blocking one-shot ISO-DEP APDU exchange for the tiny DESFire frames. Because
 * the device is ISO-DEP activated, the RFAL data-exchange lengths are in BYTES.
 * Pumps the RFAL worker until the transceive completes or times out. The rx
 * pointer refers to RFAL's internal buffer and is valid only until the next
 * exchange, so the caller copies out immediately. */
/*============================================================================*/
static ReturnCode desf_xchg(uint8_t cmd, uint8_t **rx, uint16_t *rxLen)
{
    uint8_t    tx     = cmd;
    uint8_t   *rxData = NULL;
    uint16_t  *rcv    = NULL;
    ReturnCode err;
    uint32_t   guard  = 0U;

    *rx = NULL; *rxLen = 0U;

    err = rfalNfcDataExchangeStart(&tx, 1U, &rxData, &rcv, RFAL_FWT_NONE);
    if (err != RFAL_ERR_NONE) return err;

    do {
        rfalNfcWorker();
        err = rfalNfcDataExchangeGetStatus();
        if (err == RFAL_ERR_BUSY) {
            if (++guard > 500U) return RFAL_ERR_TIMEOUT;   /* ~500 ms safety cap */
            osDelay(1);
        }
    } while (err == RFAL_ERR_BUSY);

    /* AGAIN would signal ISO-DEP chaining; the GetVersion frames are tiny and
     * complete in a single block, so treat NONE/AGAIN as this frame received. */
    if ((err != RFAL_ERR_NONE) && (err != RFAL_ERR_AGAIN)) return err;

    *rx    = rxData;
    *rxLen = (rcv != NULL) ? *rcv : 0U;
    return RFAL_ERR_NONE;
}

/*============================================================================*/
/* Same as desf_xchg() but for an arbitrary multi-byte command buffer (needed
 * by every deeper-read command: SelectApplication, GetKeyVersion,
 * GetFileSettings, ReadData, GetValue, ReadRecords all send parameters after
 * the command byte, unlike the parameterless GetVersion/GetFreeMemory/
 * GetKeySettings/GetApplicationIDs/GetFileIDs). */
/*============================================================================*/
static ReturnCode desf_xchg_raw(uint8_t *tx, uint16_t txLen, uint8_t **rx, uint16_t *rxLen)
{
    uint8_t   *rxData = NULL;
    uint16_t  *rcv    = NULL;
    ReturnCode err;
    uint32_t   guard  = 0U;

    *rx = NULL; *rxLen = 0U;

    err = rfalNfcDataExchangeStart(tx, txLen, &rxData, &rcv, RFAL_FWT_NONE);
    if (err != RFAL_ERR_NONE) return err;

    do {
        rfalNfcWorker();
        err = rfalNfcDataExchangeGetStatus();
        if (err == RFAL_ERR_BUSY) {
            if (++guard > 500U) return RFAL_ERR_TIMEOUT;
            osDelay(1);
        }
    } while (err == RFAL_ERR_BUSY);

    if ((err != RFAL_ERR_NONE) && (err != RFAL_ERR_AGAIN)) return err;

    *rx    = rxData;
    *rxLen = (rcv != NULL) ? *rcv : 0U;
    return RFAL_ERR_NONE;
}

/*============================================================================*/
/* Sends one native DESFire command (tx_first[0..tx_first_len-1], already
 * including the command byte and any parameters) and follows any 0xAF
 * ("additional frame") continuation with parameterless 0xAF frames,
 * accumulating every frame's payload (status byte stripped) into out[] up
 * to max_out bytes. Stops the instant a non-0xAF status is seen -- that
 * status is always conclusive (OK or a real error) and is never retried or
 * treated as transient. *out_status is only meaningful when this function
 * returns RFAL_ERR_NONE; a returned transport error means the exchange
 * itself failed (e.g. a timeout) and no conclusive DESFire status was
 * obtained for this attempt -- callers must not substitute a stale or
 * partial status in that case. Payload bytes beyond max_out are dropped
 * (never written out of bounds); *out_len reports exactly how many bytes
 * were stored, which callers combine with each parser's own truncation
 * detection (mf_desfire_app_ids_parse et al.) rather than inferring
 * truncation from *out_len alone. */
/*============================================================================*/
static ReturnCode desf_cmd_chain(uint8_t *tx_first, uint16_t tx_first_len,
                                  uint8_t *out, uint16_t max_out,
                                  uint16_t *out_len, uint8_t *out_status)
{
    uint8_t  cmd_af = MF_DESFIRE_CMD_ADDITIONAL_FRAME;
    uint8_t *tx     = tx_first;
    uint16_t txLen  = tx_first_len;
    uint16_t accLen = 0U;

    *out_len    = 0U;
    *out_status = 0xFFU;

    for (uint32_t frame = 0U; frame < DESF_MAX_CHAIN_FRAMES; frame++) {
        uint8_t   *rx    = NULL;
        uint16_t   rxLen = 0U;
        ReturnCode err    = desf_xchg_raw(tx, txLen, &rx, &rxLen);
        if ((err != RFAL_ERR_NONE) || (rx == NULL) || (rxLen < 1U)) {
            return (err != RFAL_ERR_NONE) ? err : RFAL_ERR_PROTO;
        }

        uint8_t  status  = rx[0];
        uint16_t payload = (uint16_t)(rxLen - 1U);
        for (uint16_t k = 0U; (k < payload) && (accLen < max_out); k++) {
            out[accLen++] = rx[1U + k];
        }

        *out_status = status;
        if (status != MF_DESFIRE_STATUS_ADDITIONAL_FRAME) {
            *out_len = accLen;
            return RFAL_ERR_NONE; /* conclusive status -- stop, do not retry */
        }

        tx    = &cmd_af;
        txLen = 1U;
    }

    /* Exhausted the chain-frame safety cap without a conclusive status --
     * treat as a protocol failure rather than looping forever or guessing. */
    *out_status = 0xFFU;
    *out_len    = accLen;
    return RFAL_ERR_PROTO;
}

/*============================================================================*/
/* Deeper-read stage: GetFreeMemory (0x6E). Non-fatal -- older/EV0 cards do
 * not support this command; deep->free_memory_valid simply stays false. */
/*============================================================================*/
static void desf_deep_read_free_memory(mf_desfire_deep_t *deep)
{
    uint8_t  cmd = MF_DESFIRE_CMD_GET_FREE_MEMORY;
    uint8_t  raw[DESF_RAW_SMALL_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(&cmd, 1U, raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) return;
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) return;
    deep->free_memory_valid = mf_desfire_free_memory_parse(raw, rawLen, &deep->free_memory_bytes);
}

/*============================================================================*/
/* Deeper-read stage: GetKeySettings (0x45) at PICC (master application)
 * level. Non-fatal -- some cards require authentication even for this;
 * deep->master_key_settings_valid simply stays false rather than
 * synthesizing a fake default (the UI shows "not available" explicitly). */
/*============================================================================*/
static void desf_deep_read_master_key_settings(mf_desfire_deep_t *deep)
{
    uint8_t  cmd = MF_DESFIRE_CMD_GET_KEY_SETTINGS;
    uint8_t  raw[DESF_RAW_SMALL_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(&cmd, 1U, raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) return;
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) return;
    deep->master_key_settings_valid = mf_desfire_key_settings_parse(raw, rawLen, &deep->master_key_settings);
}

/*============================================================================*/
/* Deeper-read stage: GetApplicationIDs (0x6A). On an auth-required card the
 * directory listing itself is protected -- deep->apps_protected is set and
 * app_count stays 0 (this is an honest "protected", not a failure). Any
 * other non-OK conclusive status or transport error leaves app_count at 0
 * with apps_protected=false (an honest "failed"/"nothing enumerated"). */
/*============================================================================*/
static void desf_deep_read_application_ids(mf_desfire_deep_t *deep)
{
    uint8_t  cmd = MF_DESFIRE_CMD_GET_APPLICATION_IDS;
    uint8_t  raw[DESF_RAW_APPIDS_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(&cmd, 1U, raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) return;

    mf_desfire_status_t cls = mf_desfire_classify_status(status);
    if (cls == MfDesfireStatusOk) {
        mf_desfire_app_id_t ids[MF_DESFIRE_DEEP_MAX_APPS];
        size_t count = 0U;
        bool   trunc = false;
        if (mf_desfire_app_ids_parse(raw, rawLen, ids, MF_DESFIRE_DEEP_MAX_APPS, &count, &trunc)) {
            size_t stored = (count < (size_t)MF_DESFIRE_DEEP_MAX_APPS) ? count : (size_t)MF_DESFIRE_DEEP_MAX_APPS;
            for (size_t i = 0U; i < stored; i++) {
                deep->apps[i].id = ids[i];
            }
            deep->app_count      = (uint8_t)stored;
            deep->apps_truncated = trunc;
        }
    } else if (mf_desfire_status_is_non_fatal_for_enumeration(cls)) {
        deep->apps_protected = true;
    }
    /* any other conclusive status: leave app_count=0, apps_protected=false */
}

/*============================================================================*/
/* Deeper-read stage: SelectApplication (0x5A) + AID(3B). Returns true only
 * on OPERATION_OK. */
/*============================================================================*/
static bool desf_select_application(const mf_desfire_app_id_t *aid)
{
    uint8_t  tx[4] = { MF_DESFIRE_CMD_SELECT_APPLICATION, aid->id[0], aid->id[1], aid->id[2] };
    uint8_t  raw[DESF_RAW_SMALL_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(tx, sizeof(tx), raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) return false;
    return (mf_desfire_classify_status(status) == MfDesfireStatusOk);
}

/*============================================================================*/
/* Deeper-read stage: per-application GetKeySettings (0x45) + GetKeyVersion
 * (0x64 + key_no) for key_no in [0, min(max_keys, MF_DESFIRE_MAX_KEYS)).
 * Stops (but keeps whatever was already captured) at the first non-OK
 * conclusive status or transport error on an individual key -- an
 * unreadable key is independent per-index, not a chained response, so a
 * partial key-version list is meaningful and is kept rather than discarded. */
/*============================================================================*/
static void desf_deep_read_app_keys(mf_desfire_app_t *app)
{
    {
        uint8_t  cmd = MF_DESFIRE_CMD_GET_KEY_SETTINGS;
        uint8_t  raw[DESF_RAW_SMALL_CAP];
        uint16_t rawLen = 0U;
        uint8_t  status = 0xFFU;
        if ((desf_cmd_chain(&cmd, 1U, raw, sizeof(raw), &rawLen, &status) == RFAL_ERR_NONE) &&
            (mf_desfire_classify_status(status) == MfDesfireStatusOk)) {
            app->key_settings_valid = mf_desfire_key_settings_parse(raw, rawLen, &app->key_settings);
        }
    }

    if (!app->key_settings_valid || (app->key_settings.max_keys == 0U)) return;

    uint8_t max_keys = app->key_settings.max_keys;
    if (max_keys > MF_DESFIRE_MAX_KEYS) {
        max_keys = MF_DESFIRE_MAX_KEYS; /* card reports more than the DESFire spec's own cap -- clamp, don't trust blindly */
    }

    for (uint8_t k = 0U; k < max_keys; k++) {
        uint8_t  tx[2] = { MF_DESFIRE_CMD_GET_KEY_VERSION, k };
        uint8_t  raw[DESF_RAW_SMALL_CAP];
        uint16_t rawLen = 0U;
        uint8_t  status = 0xFFU;

        if (desf_cmd_chain(tx, sizeof(tx), raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) break;
        if (mf_desfire_classify_status(status) != MfDesfireStatusOk) break;
        if (rawLen < 1U) break;

        app->key_versions[app->key_version_count++] = raw[0];
    }

    app->key_versions_truncated = (app->key_settings.max_keys > MF_DESFIRE_MAX_KEYS);
}

/*============================================================================*/
/* Deeper-read stage: GetFileIDs (0x6F) for the currently selected
 * application. */
/*============================================================================*/
static void desf_deep_read_file_ids(mf_desfire_app_t *app)
{
    uint8_t  cmd = MF_DESFIRE_CMD_GET_FILE_IDS;
    uint8_t  raw[DESF_RAW_FILEIDS_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(&cmd, 1U, raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) return;
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) return;

    size_t count = 0U;
    bool   trunc = false;
    if (mf_desfire_file_ids_parse(raw, rawLen, app->file_ids, MF_DESFIRE_MAX_FILES_PER_APP, &count, &trunc)) {
        size_t stored = (count < (size_t)MF_DESFIRE_MAX_FILES_PER_APP) ? count : (size_t)MF_DESFIRE_MAX_FILES_PER_APP;
        app->file_count      = (uint8_t)stored;
        app->files_truncated = trunc;
        for (size_t i = 0U; i < stored; i++) {
            app->files[i].id = app->file_ids[i];
        }
    }
}

/*============================================================================*/
/* True iff a file's settings grant free (keyless) read access to an
 * unauthenticated reader: either the Read nibble (bits 15-12) OR the ReadWrite nibble (bits
 * 7-4) of the access-rights word equals 0xE -- NOT "the whole 16-bit word
 * equals exactly 0x000E", which is what an earlier version of this file
 * incorrectly checked (a real acquisition bug: it rejected files with a
 * genuinely free Read nibble like 0xE000, causing every field on a real,
 * hardware-tested Clipper card to come back unavailable). Communication mode
 * (Plain/MACed/Enciphered) is deliberately NOT part of this condition.
 *
 * Scope note: this checks only the primary access-rights word.
 * EV1+ cards can include "additional access rights" entries
 * (present only when (comm & 0x80) is set) applying the same check to
 * each; this project's mf_desfire_file_settings_parse() parses that tail
 * only for length-bounds correctness and does not store its values, so a
 * file that is free ONLY via an additional (non-primary) access-rights
 * entry will not be detected here. Disclosed, not silently assumed away --
 * see the delivery report. */
/*============================================================================*/
static bool desf_file_is_freely_plain_readable(const mf_desfire_file_settings_t *s)
{
    uint8_t read_access       = (uint8_t)((s->access_rights >> 12) & 0x0FU);
    uint8_t read_write_access = (uint8_t)((s->access_rights >> 4)  & 0x0FU);
    return (read_access == 0x0EU) || (read_write_access == 0x0EU);
}

/*============================================================================*/
/* Reads a Standard/Backup file's content (ReadData, 0xBD) starting at
 * offset 0, up to MF_DESFIRE_DEEP_FILE_DATA_CAP bytes. A file larger than
 * the cap is reported Partial (data[] holds a genuine prefix, never padded
 * or fabricated past what was actually read); a file at or under the cap
 * that reads back completely is reported Complete. */
/*============================================================================*/
static void desf_read_file_standard_backup(mf_desfire_file_t *file)
{
    uint32_t size = file->settings.size;
    bool     will_be_partial = (size > (uint32_t)MF_DESFIRE_DEEP_FILE_DATA_CAP);
    uint32_t read_len = will_be_partial ? (uint32_t)MF_DESFIRE_DEEP_FILE_DATA_CAP : size;

    if (read_len == 0U) {
        file->read_status = MfDesfireFileComplete;
        file->data_len = 0U;
        return;
    }

    uint8_t tx[8] = {
        MF_DESFIRE_CMD_READ_DATA, file->id,
        0U, 0U, 0U,                                             /* offset = 0, 3-byte LE */
        (uint8_t)(read_len & 0xFFU), (uint8_t)((read_len >> 8) & 0xFFU), (uint8_t)((read_len >> 16) & 0xFFU)
    };
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(tx, sizeof(tx), file->data, (uint16_t)MF_DESFIRE_DEEP_FILE_DATA_CAP, &rawLen, &status) != RFAL_ERR_NONE) {
        file->read_status = MfDesfireFileFailed;
        return;
    }
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) {
        file->read_status = MfDesfireFileFailed;
        return;
    }

    file->data_len = rawLen;
    file->read_status = will_be_partial ? MfDesfireFilePartial : MfDesfireFileComplete;
}

/*============================================================================*/
/* Reads a Value file's current value (GetValue, 0x6C) -- always exactly 4
 * bytes, so always Complete on success, never Partial. */
/*============================================================================*/
static void desf_read_file_value(mf_desfire_file_t *file)
{
    uint8_t  tx[2] = { MF_DESFIRE_CMD_GET_VALUE, file->id };
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(tx, sizeof(tx), file->data, (uint16_t)MF_DESFIRE_DEEP_FILE_DATA_CAP, &rawLen, &status) != RFAL_ERR_NONE) {
        file->read_status = MfDesfireFileFailed;
        return;
    }
    if ((mf_desfire_classify_status(status) != MfDesfireStatusOk) || (rawLen < 4U)) {
        file->read_status = MfDesfireFileFailed;
        return;
    }

    file->data_len = rawLen;
    file->read_status = MfDesfireFileComplete;
}

/*============================================================================*/
/* Reads a Linear/Cyclic-Record file's content (ReadRecords, 0xBB), starting
 * at record offset 0, capped to the largest whole number of records that
 * fits within MF_DESFIRE_DEEP_FILE_DATA_CAP. A record_size of 0 or larger
 * than the cap makes even one record un-capturable -- reported Unsupported
 * rather than a misleading empty Partial/Complete. */
/*============================================================================*/
static void desf_read_file_record(mf_desfire_file_t *file)
{
    uint32_t record_size = file->settings.record_size;
    uint32_t record_cur  = file->settings.record_cur;

    if ((record_size == 0U) || (record_size > (uint32_t)MF_DESFIRE_DEEP_FILE_DATA_CAP)) {
        file->read_status = MfDesfireFileUnsupported;
        return;
    }

    uint32_t max_records_by_cap = (uint32_t)MF_DESFIRE_DEEP_FILE_DATA_CAP / record_size;
    uint32_t records_to_read    = (record_cur < max_records_by_cap) ? record_cur : max_records_by_cap;
    bool     will_be_partial    = (record_cur > max_records_by_cap);

    if (records_to_read == 0U) {
        file->read_status = MfDesfireFileComplete;
        file->data_len = 0U;
        return;
    }

    uint32_t read_len = records_to_read * record_size; /* <= cap by construction */
    uint8_t tx[8] = {
        MF_DESFIRE_CMD_READ_RECORDS, file->id,
        0U, 0U, 0U,                                                                    /* offset = 0, 3-byte LE */
        (uint8_t)(records_to_read & 0xFFU), (uint8_t)((records_to_read >> 8) & 0xFFU), (uint8_t)((records_to_read >> 16) & 0xFFU)
    };
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(tx, sizeof(tx), file->data, (uint16_t)read_len, &rawLen, &status) != RFAL_ERR_NONE) {
        file->read_status = MfDesfireFileFailed;
        return;
    }
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) {
        file->read_status = MfDesfireFileFailed;
        return;
    }

    file->data_len = rawLen;
    file->read_status = will_be_partial ? MfDesfireFilePartial : MfDesfireFileComplete;
}

/*============================================================================*/
/* Deeper-read stage: for one file, GetFileSettings (0xF5) then -- only when
 * the settings grant free plaintext read -- the type-appropriate content
 * read. Every outcome is one of the explicit mf_desfire_file_read_status_t
 * values; nothing is ever silently skipped without recording why. */
/*============================================================================*/
static void desf_deep_read_file(mf_desfire_file_t *file)
{
    uint8_t  tx[2] = { MF_DESFIRE_CMD_GET_FILE_SETTINGS, file->id };
    uint8_t  raw[DESF_RAW_FILESETTINGS_CAP];
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if (desf_cmd_chain(tx, sizeof(tx), raw, sizeof(raw), &rawLen, &status) != RFAL_ERR_NONE) {
        file->read_status = MfDesfireFileFailed;
        return;
    }
    if (mf_desfire_classify_status(status) != MfDesfireStatusOk) {
        file->read_status = MfDesfireFileFailed;
        return;
    }

    file->settings_valid = mf_desfire_file_settings_parse(raw, rawLen, &file->settings);
    if (!file->settings_valid) {
        file->read_status = MfDesfireFileFailed;
        return;
    }

    if (file->settings.type == MfDesfireFileTypeTransactionMac) {
        /* No dedicated data-read command exists for this type; settings only. */
        file->read_status = MfDesfireFileUnsupported;
        return;
    }

    if (!desf_file_is_freely_plain_readable(&file->settings)) {
        /* Neither access-rights nibble grants free read -- a key is
         * genuinely required. (Comm mode no longer factors into this
         * decision at all -- see desf_file_is_freely_plain_readable()'s own
         * comment for why. A hardware test proved that adding a comm-mode
         * requirement on top of the access-rights check causes files
         * known-readable files to be wrongly skipped here.) */
        file->read_status = MfDesfireFileProtected;
        return;
    }

    switch (file->settings.type) {
    case MfDesfireFileTypeStandard:
    case MfDesfireFileTypeBackup:
        desf_read_file_standard_backup(file);
        break;
    case MfDesfireFileTypeValue:
        desf_read_file_value(file);
        break;
    case MfDesfireFileTypeLinearRecord:
    case MfDesfireFileTypeCyclicRecord:
        desf_read_file_record(file);
        break;
    default:
        file->read_status = MfDesfireFileUnsupported;
        break;
    }
}

/*============================================================================*/
/* Deeper-read stage: process one application fully -- SelectApplication,
 * then keys, then files. If SelectApplication itself fails, the AID is
 * still recorded (from GetApplicationIDs) but select_ok stays false and no
 * further commands are attempted for this application -- an honest
 * "identified but could not select" outcome distinct from "identified and
 * fully read". */
/*============================================================================*/
static void desf_deep_read_application(mf_desfire_app_t *app)
{
    app->select_ok = desf_select_application(&app->id);
    if (!app->select_ok) return;

    desf_deep_read_app_keys(app);
    desf_deep_read_file_ids(app);

    for (uint8_t i = 0U; i < app->file_count; i++) {
        desf_deep_read_file(&app->files[i]);
    }
}

/*============================================================================*/
/* Top-level deeper-read orchestration, called only after Tier-1 GetVersion
 * has already succeeded and while the card remains selected. Every stage is
 * independently non-fatal per the rules documented above each helper; a
 * stage that cannot complete simply leaves its own fields at their reset
 * (not-attempted/false/0) state rather than aborting the remaining stages. */
/*============================================================================*/
static void desf_deep_read(mf_desfire_deep_t *deep)
{
    mf_desfire_deep_reset(deep);

    desf_deep_read_free_memory(deep);
    desf_deep_read_master_key_settings(deep);
    desf_deep_read_application_ids(deep);

    for (uint8_t i = 0U; i < deep->app_count; i++) {
        desf_deep_read_application(&deep->apps[i]);
    }
}

/*============================================================================*/
/* Card-specific interpretation: Clipper (San Francisco Bay Area transit
 * card). Runs only after the generic deep read above has already captured
 * the application's files.
 *
 * ALL-OR-NOTHING: parsing requires
 * get_file_contents(identity, min 5B) -> get_file_contents(ecash, min 32B)
 * -> get_file_contents(histidx, min 16B) -> get_file_contents(history, min
 * 512B) -- if ANY of those four minimum-size checks fails, the whole
 * function returns false and the UI shows generic DESFire info
 * screen instead, never a Clipper-branded screen with some fields blank.
 * This function reproduces that exact contract: it returns false the
 * instant any required file is missing/undersized, and the caller
 * (desf_interpret_card) only marks the card recognized for display
 * purposes when this returns true.
 *
 * Identity (file 8) and ecash (file 2) are interpreted directly from bytes
 * the generic deep-read pass already captured -- both are well within
 * MF_DESFIRE_DEEP_FILE_DATA_CAP (identity needs 5 bytes, ecash needs 32),
 * so no additional RF exchange is needed for those, once
 * desf_file_is_freely_plain_readable()'s corrected gate lets the generic
 * pass actually fetch them (see that function's own comment for the exact
 * acquisition bug this replaces).
 *
 * Ride history (file 14) is a different story: Clipper's own history file
 * is a fixed 512 bytes (16 slots x 32-byte records), far beyond the
 * generic per-file cap. Rather than grow that cap for every file of every
 * application (a ~65KB static RAM cost this project's zero-heap,
 * bounded-RAM architecture cannot absorb), this issues ONE additional
 * targeted read -- using the exact same, unmodified
 * desf_cmd_chain()/MF_DESFIRE_CMD_READ_DATA construction the generic pass
 * already uses, re-checking the identical free-plaintext access-rights
 * gate before attempting it. This does not alter the proven transport,
 * command sequence, or access-control behavior in any way; it only
 * requests a longer length parameter for a file already proven, by the
 * generic pass's own settings capture, to grant free read to an
 * unauthenticated reader. */
static bool desf_interpret_clipper(const mf_desfire_app_t *app, nfc_transit_result_t *out)
{
    const mf_desfire_file_t *f_id = NULL, *f_cash = NULL, *f_histidx = NULL, *f_hist = NULL;
    for (uint8_t i = 0U; i < app->file_count; i++) {
        switch (app->files[i].id) {
        case NFC_TRANSIT_CLIPPER_FILE_IDENTITY: f_id      = &app->files[i]; break;
        case NFC_TRANSIT_CLIPPER_FILE_ECASH:     f_cash    = &app->files[i]; break;
        case NFC_TRANSIT_CLIPPER_FILE_HISTIDX:   f_histidx = &app->files[i]; break;
        case NFC_TRANSIT_CLIPPER_FILE_HISTORY:   f_hist    = &app->files[i]; break;
        default: break;
        }
    }

    /* get_file_contents(identity, MfDesfireFileTypeStandard, min 5) */
    if ((f_id == NULL) || !f_id->settings_valid ||
        (f_id->settings.type != MfDesfireFileTypeStandard) ||
        (f_id->data_len < 5U) ||
        !nfc_transit_clipper_parse_identity(f_id->data, f_id->data_len, &out->serial_number)) {
        return false;
    }

    /* get_file_contents(ecash, MfDesfireFileTypeBackup, min 32) */
    if ((f_cash == NULL) || !f_cash->settings_valid ||
        (f_cash->settings.type != MfDesfireFileTypeBackup) ||
        (f_cash->data_len < 32U) ||
        !nfc_transit_clipper_parse_cash(f_cash->data, f_cash->data_len,
                                         &out->counter, &out->last_update_1900,
                                         &out->last_terminal_id, &out->last_txn_id,
                                         &out->balance_cents)) {
        return false;
    }

    /* get_file_contents(histidx, MfDesfireFileTypeBackup, min 16) */
    if ((f_histidx == NULL) || !f_histidx->settings_valid ||
        (f_histidx->settings.type != MfDesfireFileTypeBackup) ||
        (f_histidx->data_len < NFC_TRANSIT_CLIPPER_HISTIDX_SIZE)) {
        return false;
    }

    /* get_file_contents(history, MfDesfireFileTypeStandard, min 512) --
     * the file must be readable AND declare at least the full 512 bytes
     * before even attempting the targeted read. */
    if ((f_hist == NULL) || !f_hist->settings_valid ||
        (f_hist->settings.type != MfDesfireFileTypeStandard) ||
        !desf_file_is_freely_plain_readable(&f_hist->settings) ||
        (f_hist->settings.size < NFC_TRANSIT_CLIPPER_HISTORY_CAP)) {
        return false;
    }

    static uint8_t s_clipper_history_scratch[NFC_TRANSIT_CLIPPER_HISTORY_CAP];
    uint16_t want = NFC_TRANSIT_CLIPPER_HISTORY_CAP;
    uint8_t  tx[8] = {
        MF_DESFIRE_CMD_READ_DATA, f_hist->id,
        0U, 0U, 0U,
        (uint8_t)(want & 0xFFU), (uint8_t)((want >> 8) & 0xFFU), (uint8_t)((want >> 16) & 0xFFU)
    };
    uint16_t rawLen = 0U;
    uint8_t  status = 0xFFU;

    if ((desf_cmd_chain(tx, sizeof(tx), s_clipper_history_scratch, sizeof(s_clipper_history_scratch),
                         &rawLen, &status) != RFAL_ERR_NONE) ||
        (mf_desfire_classify_status(status) != MfDesfireStatusOk) ||
        (rawLen < NFC_TRANSIT_CLIPPER_HISTORY_CAP)) {
        return false;
    }

    /* Every required file is present at its minimum size. Individual ride records
     * within the (now guaranteed-present) history file can still be
     * skipped on their own terms (bad marker byte, agency_id==0, etc.) --
     * that per-record leniency is intentional. */
    out->serial_valid        = true;
    out->balance_valid       = true;
    out->last_update_valid   = true;
    out->last_terminal_valid = true;
    out->last_txn_valid      = true;
    out->counter_valid       = true;

    nfc_transit_clipper_parse_rides(f_histidx->data, f_histidx->data_len,
                                     s_clipper_history_scratch, rawLen,
                                     out->rides, NFC_TRANSIT_MAX_RIDES,
                                     &out->ride_count, &out->rides_truncated);
    return true;
}

static void desf_interpret_card(const mf_desfire_deep_t *deep)
{
    nfc_transit_result_t transit;
    nfc_transit_result_reset(&transit);

    uint8_t app_idx = 0U;
    const char *type_label = NULL;
    if (nfc_transit_detect_clipper(deep, &app_idx, &type_label) && deep->apps[app_idx].select_ok) {
        nfc_transit_result_t candidate;
        nfc_transit_result_reset(&candidate);
        candidate.card_type_label = type_label;
        if (desf_interpret_clipper(&deep->apps[app_idx], &candidate)) {
            candidate.card_id = NfcTransitCardClipper;
            transit = candidate;
        }
        /* else: `transit` stays at its reset (Unknown) state when parsing
         * fails. No Clipper-branded screen with partial/blank fields is
         * ever shown; the caller's UI falls back to the existing generic
         * DESFire screens, unchanged. */
    }

    nfc_ctx_set_transit(&transit);
}

/*============================================================================*/
bool m1_desfire_read(const rfalNfcDevice *dev)
{
    nfc_ctx_clear_desfire();

    if (dev == NULL) return false;

    platformLog("[DESF] rfInterface=%d (ISODEP=%d)\r\n",
                (int)dev->rfInterface, (int)RFAL_NFC_INTERFACE_ISODEP);
    if (dev->rfInterface != RFAL_NFC_INTERFACE_ISODEP) {
        platformLog("[DESF] not ISO-DEP activated -> keep generic Type 4A\r\n");
        return false;
    }

    uint8_t  acc[DESF_VERSION_LEN];
    uint16_t accLen = 0U;
    uint8_t  cmd    = DESF_CMD_GET_VERSION;

    /* 0x60, then 0xAF, then 0xAF -> three frames, status byte leads each. */
    for (int frame = 0; frame < 4; frame++) {
        uint8_t   *rx    = NULL;
        uint16_t   rxLen = 0U;
        ReturnCode err   = desf_xchg(cmd, &rx, &rxLen);
        if ((err != RFAL_ERR_NONE) || (rx == NULL) || (rxLen < 1U)) {
            platformLog("[DESF] GETVER frame%d fail err=%d len=%u\r\n",
                        frame, (int)err, (unsigned)rxLen);
            return false;
        }

        uint8_t  status  = rx[0];
        uint16_t payload = (uint16_t)(rxLen - 1U);
        for (uint16_t k = 0U; (k < payload) && (accLen < (uint16_t)sizeof(acc)); k++) {
            acc[accLen++] = rx[1U + k];
        }

        if (status == DESF_STATUS_OK)        break;    /* final frame received  */
        if (status != DESF_STATUS_ADD_FRAME) {         /* a real DESFire error   */
            platformLog("[DESF] status=0x%02X abort\r\n", status);
            return false;
        }
        cmd = DESF_STATUS_ADD_FRAME;                   /* request the next frame */
    }

    if (accLen != DESF_VERSION_LEN) {
        platformLog("[DESF] version len=%u (want %u)\r\n",
                    (unsigned)accLen, (unsigned)DESF_VERSION_LEN);
        return false;
    }
    if (acc[0] != DESF_NXP_VENDOR) {   /* trust only the NXP hardware vendor */
        platformLog("[DESF] hw_vendor=0x%02X not NXP -> keep generic\r\n", acc[0]);
        return false;
    }

    platformLog("[DESF] GETVER OK hw=%02X %02X %02X maj=%02X sto=%02X\r\n",
                acc[0], acc[1], acc[2], acc[3], acc[5]);

    /* acc: [0..6] hw, [7..13] sw, [14..20] uid, [21..25] batch, [26] week, [27] year */
    nfc_ctx_set_desfire_version(acc);

    /* Deeper read, while the card is still selected. Its own per-item
     * outcome (complete/partial/protected/unsupported/failed) is tracked
     * entirely in the deep struct and never changes this function's return
     * value -- GetVersion already succeeded, which is what "true" means.
     * mf_desfire_deep_t is ~16KB (bounded app/file arrays) -- far too large
     * for an RTOS task stack, so it is populated directly in the shared NFC
     * context's static storage rather than as a local variable copied in
     * afterward (the same direct-write pattern nfc_poller.c/nfc_listener.c
     * already use via nfc_ctx_get()). */
    desf_deep_read(&nfc_ctx_get()->desfire_deep);

    /* Card-specific interpretation (transit cards): honest best-effort,
     * never changes the return value above -- an unrecognized or
     * partially-readable card simply leaves nfc_ctx_get_transit()'s
     * card_id at NfcTransitCardUnknown / individual fields at *_valid ==
     * false, and every consumer falls back to the generic DESFire screens. */
    desf_interpret_card(&nfc_ctx_get()->desfire_deep);

    return true;
}

/*============================================================================*/
const char *m1_desfire_gen_name(uint8_t hw_major)
{
    switch (hw_major) {
        case 0x00U: return "MF3ICD40";
        case 0x01U: return "EV1";
        case 0x12U: return "EV2";
        case 0x33U: return "EV3";
        default:    return "?";
    }
}

const char *m1_desfire_size_name(uint8_t hw_storage)
{
    switch (hw_storage) {
        case 0x16U: return "2K";
        case 0x18U: return "4K";
        case 0x1AU: return "8K";
        case 0x1CU: return "16K";
        case 0x1EU: return "32K";
        default:    return "?";
    }
}
