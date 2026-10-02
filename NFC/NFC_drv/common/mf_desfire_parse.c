/* See COPYING.txt for license details. */

/*
 * mf_desfire_parse.c
 *
 * See mf_desfire_parse.h for scope and byte conventions.
 *
 * This file has no dependency on any HAL/RFAL/RTOS header -- it is pure,
 * host-testable C over caller-supplied byte buffers, matching the
 * lfrfid_fsk_osc.c decoupling pattern used earlier in this project.
 */

#include "mf_desfire_parse.h"
#include <string.h>

mf_desfire_status_t mf_desfire_classify_status(uint8_t status_byte)
{
    switch (status_byte) {
    case MF_DESFIRE_STATUS_OPERATION_OK:          return MfDesfireStatusOk;
    case MF_DESFIRE_STATUS_ADDITIONAL_FRAME:      return MfDesfireStatusAdditionalFrame;
    case MF_DESFIRE_STATUS_AUTHENTICATION_ERROR:  return MfDesfireStatusAuthRequired;
    case MF_DESFIRE_STATUS_PERMISSION_DENIED:     return MfDesfireStatusPermissionDenied;
    case MF_DESFIRE_STATUS_APPLICATION_NOT_FOUND: return MfDesfireStatusAppNotFound;
    case MF_DESFIRE_STATUS_FILE_NOT_FOUND:        return MfDesfireStatusFileNotFound;
    case MF_DESFIRE_STATUS_ILLEGAL_COMMAND_CODE:  return MfDesfireStatusIllegalCommand;
    case MF_DESFIRE_STATUS_BOUNDARY_ERROR:        return MfDesfireStatusBoundaryError;
    case MF_DESFIRE_STATUS_LENGTH_ERROR:          return MfDesfireStatusLengthError;
    /* Every other defined status byte (NO_CHANGES, OUT_OF_EEPROM_ERROR,
     * INTEGRITY_ERROR, NO_SUCH_KEY, PARAMETER_ERROR, APPL_INTEGRITY_ERROR,
     * PICC_INTEGRITY_ERROR, COMMAND_ABORTED, PICC_DISABLED_ERROR,
     * COUNT_ERROR, DUPLICATE_ERROR, EEPROM_ERROR, FILE_INTEGRITY_ERROR, and
     * any undefined byte) collapses to a generic conclusive protocol error. */
    default: return MfDesfireStatusProtocolError;
    }
}

bool mf_desfire_status_is_non_fatal_for_enumeration(mf_desfire_status_t s)
{
    return (s == MfDesfireStatusAuthRequired) || (s == MfDesfireStatusIllegalCommand);
}

void mf_desfire_deep_reset(mf_desfire_deep_t *deep)
{
    if (deep == NULL) return;
    memset(deep, 0, sizeof(*deep));
}

bool mf_desfire_free_memory_parse(const uint8_t *buf, size_t len, uint32_t *out_bytes)
{
    if ((buf == NULL) || (out_bytes == NULL) || (len < 3U)) return false;
    *out_bytes = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16);
    return true;
}

bool mf_desfire_key_settings_parse(const uint8_t *buf, size_t len, mf_desfire_key_settings_t *out)
{
    if ((buf == NULL) || (out == NULL) || (len < 2U)) return false;

    memset(out, 0, sizeof(*out));

    uint8_t settings = buf[0];
    uint8_t key_count = buf[1];

    out->config_changeable   = (settings & 0x08U) != 0U;
    out->free_create_delete  = (settings & 0x04U) != 0U;
    out->free_directory_list = (settings & 0x02U) != 0U;
    out->key_changeable      = (settings & 0x01U) != 0U;
    out->change_key_id_present = true;
    out->change_key_id       = (uint8_t)((settings >> 4) & 0x0FU);

    /* Zero-master-key hardening (adopted from the newer official checkout's
     * crash fix for cards that report zero keys): max_keys is stored raw,
     * but every consumer of key_versions[]/key_versions_truncated MUST
     * check max_keys > 0 before treating it as a loop bound, never divide
     * or index by it directly. This function only records the value. */
    out->max_keys = (uint8_t)(key_count & 0x0FU);
    out->flags    = (uint8_t)((key_count >> 4) & 0x0FU);
    out->valid = true;
    return true;
}

bool mf_desfire_app_ids_parse(const uint8_t *buf, size_t len,
                               mf_desfire_app_id_t *out, size_t max_out,
                               size_t *out_count, bool *truncated)
{
    if (out_count != NULL) *out_count = 0U;
    if (truncated != NULL) *truncated = false;
    if ((buf == NULL) && (len != 0U)) return false;
    if ((len % MF_DESFIRE_APP_ID_SIZE) != 0U) return false; /* malformed body, not silently rounded */

    size_t total = len / MF_DESFIRE_APP_ID_SIZE;
    if (out_count != NULL) *out_count = total;

    size_t stored = (total < max_out) ? total : max_out;
    for (size_t i = 0U; i < stored; i++) {
        if (out != NULL) {
            memcpy(out[i].id, &buf[i * MF_DESFIRE_APP_ID_SIZE], MF_DESFIRE_APP_ID_SIZE);
        }
    }
    if (truncated != NULL) *truncated = (total > max_out);
    return true;
}

bool mf_desfire_file_ids_parse(const uint8_t *buf, size_t len,
                                uint8_t *out, size_t max_out,
                                size_t *out_count, bool *truncated)
{
    if (out_count != NULL) *out_count = 0U;
    if (truncated != NULL) *truncated = false;
    if ((buf == NULL) && (len != 0U)) return false;

    if (out_count != NULL) *out_count = len;
    size_t stored = (len < max_out) ? len : max_out;
    for (size_t i = 0U; i < stored; i++) {
        if (out != NULL) out[i] = buf[i];
    }
    if (truncated != NULL) *truncated = (len > max_out);
    return true;
}

/* Full tagged-union file-settings layout. Body (status byte stripped):
 *   [0]      type
 *   [1]      comm settings (bit 0x80 => additional access rights tail present)
 *   [2..3]   access_rights, little-endian
 *   [4..]    type-specific body
 * Standard/Backup:      3-byte LE size
 * Value:                lo(4B LE) hi(4B LE) limited_credit(4B LE) enabled(1B)
 * Linear/CyclicRecord:  record_size(3B LE) record_max(3B LE) record_cur(3B LE)
 * TransactionMac:       key_option(1B) key_version(1B) [+ counter_limit,
 *                        2B if !(key_option&0x02) i.e. LRP, 4B if AES --
 *                        parsed for length-bounds correctness only, value
 *                        not surfaced (no M1 UI need beyond settings display)]
 * If (comm & 0x80): an additional-access-rights tail (2 bytes) follows the
 * type body; parsed for bounds-correctness only (not stored --  no M1 UI
 * consumes it yet, matching the current display scope). */
bool mf_desfire_file_settings_parse(const uint8_t *buf, size_t len, mf_desfire_file_settings_t *out)
{
    if ((buf == NULL) || (out == NULL) || (len < 4U)) return false;

    memset(out, 0, sizeof(*out));

    uint8_t type_byte = buf[0];
    uint8_t comm_byte = buf[1];
    size_t  off = 4U; /* past type/comm/access_rights */

    out->access_rights = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    out->comm = (mf_desfire_comm_t)(comm_byte & 0x03U);

    switch (type_byte) {
    case MfDesfireFileTypeStandard:
    case MfDesfireFileTypeBackup:
        out->type = (mf_desfire_file_type_t)type_byte;
        if (len < off + 3U) return false;
        out->size = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16);
        off += 3U;
        break;

    case MfDesfireFileTypeValue:
        out->type = MfDesfireFileTypeValue;
        if (len < off + 13U) return false;
        out->value_lo_limit = (int32_t)((uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                                         ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24));
        off += 4U;
        out->value_hi_limit = (int32_t)((uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                                         ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24));
        off += 4U;
        out->value_limited_credit = (int32_t)((uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                                               ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24));
        off += 4U;
        out->value_limited_credit_enabled = (buf[off] != 0U);
        off += 1U;
        break;

    case MfDesfireFileTypeLinearRecord:
    case MfDesfireFileTypeCyclicRecord:
        out->type = (mf_desfire_file_type_t)type_byte;
        if (len < off + 9U) return false;
        out->record_size = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16);
        off += 3U;
        out->record_max  = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16);
        off += 3U;
        out->record_cur  = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16);
        off += 3U;
        break;

    case MfDesfireFileTypeTransactionMac: {
        out->type = MfDesfireFileTypeTransactionMac;
        if (len < off + 2U) return false;
        out->tmac_key_option  = buf[off];
        out->tmac_key_version = buf[off + 1];
        off += 2U;
        size_t counter_limit_len = ((out->tmac_key_option & 0x02U) != 0U) ? 4U : 2U;
        if (len < off + counter_limit_len) return false;
        off += counter_limit_len;
        break;
    }

    default:
        out->type = MfDesfireFileTypeUnknown;
        return false; /* unrecognized type byte -- explicit parse failure, not a guess */
    }

    if ((comm_byte & 0x80U) != 0U) {
        if (len < off + 2U) return false; /* additional access rights tail declared but truncated */
        off += 2U;
    }

    out->valid = true;
    return true;
}

const char *mf_desfire_file_type_name(mf_desfire_file_type_t type)
{
    switch (type) {
    case MfDesfireFileTypeStandard:       return "Standard";
    case MfDesfireFileTypeBackup:         return "Backup";
    case MfDesfireFileTypeValue:          return "Value";
    case MfDesfireFileTypeLinearRecord:   return "Linear Record";
    case MfDesfireFileTypeCyclicRecord:   return "Cyclic Record";
    case MfDesfireFileTypeTransactionMac: return "Transaction MAC";
    default:                              return "Unknown";
    }
}

const char *mf_desfire_comm_name(mf_desfire_comm_t comm)
{
    switch (comm) {
    case MfDesfireCommPlaintext:     return "Plain";
    case MfDesfireCommAuthenticated: return "MACed";
    case MfDesfireCommEnciphered:    return "Enciphered";
    default:                         return "Reserved";
    }
}

const char *mf_desfire_file_read_status_name(mf_desfire_file_read_status_t status)
{
    switch (status) {
    case MfDesfireFileNotAttempted: return "Not read";
    case MfDesfireFileComplete:     return "Complete";
    case MfDesfireFilePartial:      return "Partial";
    case MfDesfireFileProtected:    return "Protected";
    case MfDesfireFileUnsupported:  return "Unsupported";
    case MfDesfireFileFailed:       return "Failed";
    default:                        return "Unknown";
    }
}
