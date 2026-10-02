/*
 * mf_desfire_parse_test.c
 *
 * Host-compiled behavioral tests for mf_desfire_parse.c. Every expected
 * value below is computed independently from the documented byte
 * layout (mf_desfire_i.c/.h at the pinned 1.4.3 commit) -- none are derived
 * by running the C parser itself and recording its output.
 *
 * Self-contained. Build (from repo root):
 *   cc -std=c11 -Wall -Wextra -Werror \
 *     -I NFC/NFC_drv/common \
 *     NFC/NFC_drv/common/test/mf_desfire_parse_test.c \
 *     NFC/NFC_drv/common/mf_desfire_parse.c \
 *     -o /tmp/mf_desfire_parse_test && /tmp/mf_desfire_parse_test
 */

#include "mf_desfire_parse.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

/*============================ classify_status =================================*/

static void test_classify_status_named_codes(void)
{
    CHECK(mf_desfire_classify_status(0x00) == MfDesfireStatusOk, "OK");
    CHECK(mf_desfire_classify_status(0xAF) == MfDesfireStatusAdditionalFrame, "ADDITIONAL_FRAME");
    CHECK(mf_desfire_classify_status(0xAE) == MfDesfireStatusAuthRequired, "AUTHENTICATION_ERROR");
    CHECK(mf_desfire_classify_status(0x9D) == MfDesfireStatusPermissionDenied, "PERMISSION_DENIED");
    CHECK(mf_desfire_classify_status(0xA0) == MfDesfireStatusAppNotFound, "APPLICATION_NOT_FOUND");
    CHECK(mf_desfire_classify_status(0xF0) == MfDesfireStatusFileNotFound, "FILE_NOT_FOUND");
    CHECK(mf_desfire_classify_status(0x1C) == MfDesfireStatusIllegalCommand, "ILLEGAL_COMMAND_CODE");
    CHECK(mf_desfire_classify_status(0xBE) == MfDesfireStatusBoundaryError, "BOUNDARY_ERROR");
    CHECK(mf_desfire_classify_status(0x7E) == MfDesfireStatusLengthError, "LENGTH_ERROR");
}

static void test_classify_status_other_named_codes_are_protocol_error(void)
{
    static const uint8_t others[] = {
        0x0C, 0x0E, 0x1E, 0x40, 0x9E, 0xA1, 0xC1, 0xCA, 0xCD, 0xCE, 0xDE, 0xEE, 0xF1
    };
    for (size_t i = 0; i < sizeof(others); i++) {
        CHECK(mf_desfire_classify_status(others[i]) == MfDesfireStatusProtocolError,
              "named-but-uncollapsed code maps to generic protocol error");
    }
}

static void test_classify_status_undefined_byte_is_protocol_error(void)
{
    CHECK(mf_desfire_classify_status(0x77) == MfDesfireStatusProtocolError, "undefined byte 0x77");
    CHECK(mf_desfire_classify_status(0xFF) == MfDesfireStatusProtocolError, "undefined byte 0xFF");
}

static void test_non_fatal_for_enumeration(void)
{
    CHECK(mf_desfire_status_is_non_fatal_for_enumeration(MfDesfireStatusAuthRequired), "auth required is non-fatal");
    CHECK(mf_desfire_status_is_non_fatal_for_enumeration(MfDesfireStatusIllegalCommand), "illegal command is non-fatal");
    CHECK(!mf_desfire_status_is_non_fatal_for_enumeration(MfDesfireStatusOk), "OK is not in the non-fatal set");
    CHECK(!mf_desfire_status_is_non_fatal_for_enumeration(MfDesfireStatusPermissionDenied), "permission denied is fatal-for-enum");
    CHECK(!mf_desfire_status_is_non_fatal_for_enumeration(MfDesfireStatusProtocolError), "generic protocol error is fatal-for-enum");
}

/*============================ free_memory =====================================*/

static void test_free_memory_parse_valid(void)
{
    uint8_t buf[3] = { 0x40, 0x42, 0x0F }; /* 0x0F4240 = 1,000,000 */
    uint32_t out = 0;
    CHECK(mf_desfire_free_memory_parse(buf, sizeof(buf), &out), "free memory parse succeeds");
    CHECK(out == 1000000U, "free memory value == 1,000,000");
}

static void test_free_memory_parse_too_short(void)
{
    uint8_t buf[2] = { 0x01, 0x02 };
    uint32_t out = 0xDEADBEEF;
    CHECK(!mf_desfire_free_memory_parse(buf, sizeof(buf), &out), "2-byte body rejected, not silently accepted");
}

/*============================ key_settings ====================================*/

static void test_key_settings_parse_all_bits_set(void)
{
    uint8_t buf[2] = { 0x0F, 0x0E }; /* change_key_id=0, all 4 flag bits set; max_keys=14, flags=0 */
    mf_desfire_key_settings_t ks;
    CHECK(mf_desfire_key_settings_parse(buf, sizeof(buf), &ks), "key settings parse succeeds");
    CHECK(ks.valid, "valid flag set");
    CHECK(ks.config_changeable, "config_changeable");
    CHECK(ks.free_create_delete, "free_create_delete");
    CHECK(ks.free_directory_list, "free_directory_list");
    CHECK(ks.key_changeable, "key_changeable");
    CHECK(ks.change_key_id == 0U, "change_key_id == 0");
    CHECK(ks.max_keys == 14U, "max_keys == 14");
    CHECK(ks.flags == 0U, "flags == 0");
}

static void test_key_settings_parse_change_key_id_and_no_bits(void)
{
    uint8_t buf[2] = { 0xD0, 0x35 }; /* change_key_id=0xD, no flag bits; max_keys=5, flags=3 */
    mf_desfire_key_settings_t ks;
    CHECK(mf_desfire_key_settings_parse(buf, sizeof(buf), &ks), "parse succeeds");
    CHECK(!ks.config_changeable && !ks.free_create_delete && !ks.free_directory_list && !ks.key_changeable,
          "all flag bits clear");
    CHECK(ks.change_key_id == 0x0DU, "change_key_id == 0xD");
    CHECK(ks.max_keys == 5U, "max_keys == 5");
    CHECK(ks.flags == 3U, "flags == 3");
}

static void test_key_settings_parse_zero_keys_is_valid_not_a_failure(void)
{
    uint8_t buf[2] = { 0x00, 0x00 };
    mf_desfire_key_settings_t ks;
    CHECK(mf_desfire_key_settings_parse(buf, sizeof(buf), &ks), "zero-key card parses successfully (hardening case)");
    CHECK(ks.valid, "valid flag still set for zero-key card");
    CHECK(ks.max_keys == 0U, "max_keys == 0 is a real parsed value, not an error sentinel");
}

static void test_key_settings_parse_too_short(void)
{
    uint8_t buf[1] = { 0x0F };
    mf_desfire_key_settings_t ks;
    CHECK(!mf_desfire_key_settings_parse(buf, sizeof(buf), &ks), "1-byte body rejected");
}

/*============================ app_ids =========================================*/

static void test_app_ids_parse_exact_fit(void)
{
    uint8_t buf[6] = { 0x01, 0x02, 0x03, 0xAA, 0xBB, 0xCC };
    mf_desfire_app_id_t out[2];
    size_t count = 0; bool trunc = true;
    CHECK(mf_desfire_app_ids_parse(buf, sizeof(buf), out, 2, &count, &trunc), "parse succeeds");
    CHECK(count == 2U, "count == 2");
    CHECK(!trunc, "not truncated when capacity matches exactly");
    CHECK(memcmp(out[0].id, "\x01\x02\x03", 3) == 0, "first id bytes");
    CHECK(memcmp(out[1].id, "\xAA\xBB\xCC", 3) == 0, "second id bytes");
}

static void test_app_ids_parse_truncated(void)
{
    uint8_t buf[9] = { 0x01,0x02,0x03, 0x04,0x05,0x06, 0x07,0x08,0x09 };
    mf_desfire_app_id_t out[2];
    size_t count = 0; bool trunc = false;
    CHECK(mf_desfire_app_ids_parse(buf, sizeof(buf), out, 2, &count, &trunc), "parse succeeds even when truncating");
    CHECK(count == 3U, "true count reported as 3 even though only 2 fit");
    CHECK(trunc, "truncated flag set");
    CHECK(memcmp(out[0].id, "\x01\x02\x03", 3) == 0, "first id still captured");
    CHECK(memcmp(out[1].id, "\x04\x05\x06", 3) == 0, "second id still captured");
}

static void test_app_ids_parse_malformed_length(void)
{
    uint8_t buf[4] = { 0x01, 0x02, 0x03, 0x04 }; /* not a multiple of 3 */
    mf_desfire_app_id_t out[4];
    size_t count = 123; bool trunc = true;
    CHECK(!mf_desfire_app_ids_parse(buf, sizeof(buf), out, 4, &count, &trunc), "malformed length rejected, not rounded down");
    CHECK(count == 0U, "count reset to 0 on failure");
}

static void test_app_ids_parse_empty(void)
{
    size_t count = 999; bool trunc = true;
    CHECK(mf_desfire_app_ids_parse(NULL, 0, NULL, 4, &count, &trunc), "empty body parses as zero applications");
    CHECK(count == 0U, "count == 0");
    CHECK(!trunc, "not truncated");
}

/*============================ file_ids ========================================*/

static void test_file_ids_parse_exact_fit(void)
{
    uint8_t buf[3] = { 0x01, 0x02, 0x03 };
    uint8_t out[3];
    size_t count = 0; bool trunc = true;
    CHECK(mf_desfire_file_ids_parse(buf, sizeof(buf), out, 3, &count, &trunc), "parse succeeds");
    CHECK(count == 3U, "count == 3");
    CHECK(!trunc, "not truncated");
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3, "file ids captured in order");
}

static void test_file_ids_parse_truncated(void)
{
    uint8_t buf[5] = { 0x10, 0x11, 0x12, 0x13, 0x14 };
    uint8_t out[3];
    size_t count = 0; bool trunc = false;
    CHECK(mf_desfire_file_ids_parse(buf, sizeof(buf), out, 3, &count, &trunc), "parse succeeds even when truncating");
    CHECK(count == 5U, "true count reported as 5");
    CHECK(trunc, "truncated flag set");
    CHECK(out[0] == 0x10 && out[1] == 0x11 && out[2] == 0x12, "first 3 ids captured");
}

static void test_file_ids_parse_empty(void)
{
    size_t count = 999; bool trunc = true;
    CHECK(mf_desfire_file_ids_parse(NULL, 0, NULL, 8, &count, &trunc), "empty body parses as zero files");
    CHECK(count == 0U, "count == 0");
}

/*============================ file_settings ===================================*/

static void test_file_settings_parse_standard(void)
{
    uint8_t buf[7] = { 0x00, 0x00, 0xEE, 0x0E, 0x64, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "standard file parse succeeds");
    CHECK(fs.valid, "valid");
    CHECK(fs.type == MfDesfireFileTypeStandard, "type == Standard");
    CHECK(fs.comm == MfDesfireCommPlaintext, "comm == Plaintext");
    CHECK(fs.access_rights == 0x0EEEU, "access_rights == 0x0EEE");
    CHECK(fs.size == 100U, "size == 100");
}

static void test_file_settings_parse_backup(void)
{
    uint8_t buf[7] = { 0x01, 0x01, 0x34, 0x12, 0x00, 0x10, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "backup file parse succeeds");
    CHECK(fs.type == MfDesfireFileTypeBackup, "type == Backup");
    CHECK(fs.comm == MfDesfireCommAuthenticated, "comm == Authenticated");
    CHECK(fs.access_rights == 0x1234U, "access_rights == 0x1234");
    CHECK(fs.size == 4096U, "size == 4096");
}

static void test_file_settings_parse_value(void)
{
    uint8_t buf[17] = {
        0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,             /* lo_limit = 0 */
        0x40, 0x42, 0x0F, 0x00,             /* hi_limit = 1,000,000 */
        0xF4, 0x01, 0x00, 0x00,             /* limited_credit = 500 */
        0x01                                 /* enabled = true */
    };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "value file parse succeeds");
    CHECK(fs.type == MfDesfireFileTypeValue, "type == Value");
    CHECK(fs.value_lo_limit == 0, "lo_limit == 0");
    CHECK(fs.value_hi_limit == 1000000, "hi_limit == 1,000,000");
    CHECK(fs.value_limited_credit == 500, "limited_credit == 500");
    CHECK(fs.value_limited_credit_enabled, "limited_credit_enabled == true");
}

static void test_file_settings_parse_linear_record(void)
{
    uint8_t buf[13] = {
        0x03, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00,   /* record_size = 16 */
        0x0A, 0x00, 0x00,   /* record_max = 10 */
        0x03, 0x00, 0x00    /* record_cur = 3 */
    };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "linear record parse succeeds");
    CHECK(fs.type == MfDesfireFileTypeLinearRecord, "type == LinearRecord");
    CHECK(fs.record_size == 16U && fs.record_max == 10U && fs.record_cur == 3U, "record fields correct");
}

static void test_file_settings_parse_cyclic_record(void)
{
    uint8_t buf[13] = {
        0x04, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00,   /* record_size = 32 */
        0x05, 0x00, 0x00,   /* record_max = 5 */
        0x05, 0x00, 0x00    /* record_cur = 5 */
    };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "cyclic record parse succeeds");
    CHECK(fs.type == MfDesfireFileTypeCyclicRecord, "type == CyclicRecord");
    CHECK(fs.record_size == 32U && fs.record_max == 5U && fs.record_cur == 5U, "record fields correct");
}

static void test_file_settings_parse_transaction_mac_lrp(void)
{
    /* key_option bit0x02 clear -> LRP -> 2-byte counter limit */
    uint8_t buf[8] = { 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "transaction mac (LRP) parse succeeds");
    CHECK(fs.type == MfDesfireFileTypeTransactionMac, "type == TransactionMac");
    CHECK(fs.tmac_key_option == 0U && fs.tmac_key_version == 1U, "key option/version correct");
}

static void test_file_settings_parse_transaction_mac_aes(void)
{
    /* key_option bit0x02 set -> AES -> 4-byte counter limit */
    uint8_t buf[10] = { 0x05, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "transaction mac (AES) parse succeeds with the longer counter limit");
    CHECK(fs.tmac_key_option == 0x02U, "key_option == 0x02");
}

static void test_file_settings_parse_transaction_mac_aes_too_short_is_rejected(void)
{
    /* Same as AES case but missing the 4th counter-limit byte -- must NOT
     * be accepted as if it were the 2-byte LRP form. */
    uint8_t buf[9] = { 0x05, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "truncated AES counter limit rejected, not reinterpreted as LRP");
}

static void test_file_settings_parse_additional_access_rights_tail(void)
{
    uint8_t buf[9] = { 0x00, 0x80, 0x00, 0x00, 0x0A, 0x00, 0x00, 0xAB, 0xCD };
    mf_desfire_file_settings_t fs;
    CHECK(mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "standard file with additional access rights tail parses");
    CHECK(fs.size == 10U, "size == 10 despite trailing bytes");
}

static void test_file_settings_parse_additional_access_rights_tail_truncated(void)
{
    uint8_t buf[8] = { 0x00, 0x80, 0x00, 0x00, 0x0A, 0x00, 0x00, 0xAB }; /* missing 2nd tail byte */
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "declared-but-truncated tail rejected");
}

static void test_file_settings_parse_truncated_standard(void)
{
    uint8_t buf[6] = { 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00 }; /* missing 3rd size byte */
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "truncated standard-file size rejected");
}

static void test_file_settings_parse_truncated_value(void)
{
    uint8_t buf[16]; /* one byte short of the required 17 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "truncated value-file body rejected");
}

static void test_file_settings_parse_unknown_type(void)
{
    uint8_t buf[4] = { 0x06, 0x00, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "unrecognized type byte rejected, not guessed at");
    CHECK(fs.type == MfDesfireFileTypeUnknown, "type recorded as Unknown");
}

static void test_file_settings_parse_too_short_header(void)
{
    uint8_t buf[3] = { 0x00, 0x00, 0x00 };
    mf_desfire_file_settings_t fs;
    CHECK(!mf_desfire_file_settings_parse(buf, sizeof(buf), &fs), "3-byte header rejected, below the 4-byte minimum");
}

/*============================ display names ===================================*/

static void test_file_type_name_never_null(void)
{
    CHECK(strcmp(mf_desfire_file_type_name(MfDesfireFileTypeStandard), "Standard") == 0, "Standard name");
    CHECK(strcmp(mf_desfire_file_type_name(MfDesfireFileTypeTransactionMac), "Transaction MAC") == 0, "TransactionMac name");
    CHECK(strcmp(mf_desfire_file_type_name(MfDesfireFileTypeUnknown), "Unknown") == 0, "Unknown name for Unknown type");
    CHECK(strcmp(mf_desfire_file_type_name((mf_desfire_file_type_t)0x7F), "Unknown") == 0, "out-of-range value falls back to Unknown, not garbage");
}

static void test_comm_name_never_null(void)
{
    CHECK(strcmp(mf_desfire_comm_name(MfDesfireCommPlaintext), "Plain") == 0, "Plain name");
    CHECK(strcmp(mf_desfire_comm_name(MfDesfireCommEnciphered), "Enciphered") == 0, "Enciphered name");
}

static void test_read_status_name_never_null(void)
{
    CHECK(strcmp(mf_desfire_file_read_status_name(MfDesfireFileComplete), "Complete") == 0, "Complete name");
    CHECK(strcmp(mf_desfire_file_read_status_name(MfDesfireFileProtected), "Protected") == 0, "Protected name");
    CHECK(strcmp(mf_desfire_file_read_status_name((mf_desfire_file_read_status_t)0x7F), "Unknown") == 0, "out-of-range value falls back to Unknown");
}

/*============================ deep_reset ======================================*/

static void test_deep_reset_clears_everything(void)
{
    mf_desfire_deep_t deep;
    memset(&deep, 0xAA, sizeof(deep));
    mf_desfire_deep_reset(&deep);
    CHECK(!deep.free_memory_valid, "free_memory_valid cleared");
    CHECK(!deep.master_key_settings_valid, "master_key_settings_valid cleared");
    CHECK(deep.app_count == 0U, "app_count cleared");
    CHECK(!deep.apps_truncated, "apps_truncated cleared");
    CHECK(!deep.apps_protected, "apps_protected cleared");
}

static void test_deep_reset_null_is_safe(void)
{
    mf_desfire_deep_reset(NULL); /* must not crash */
    CHECK(1, "deep_reset(NULL) did not crash");
}

int main(void)
{
    test_classify_status_named_codes();
    test_classify_status_other_named_codes_are_protocol_error();
    test_classify_status_undefined_byte_is_protocol_error();
    test_non_fatal_for_enumeration();

    test_free_memory_parse_valid();
    test_free_memory_parse_too_short();

    test_key_settings_parse_all_bits_set();
    test_key_settings_parse_change_key_id_and_no_bits();
    test_key_settings_parse_zero_keys_is_valid_not_a_failure();
    test_key_settings_parse_too_short();

    test_app_ids_parse_exact_fit();
    test_app_ids_parse_truncated();
    test_app_ids_parse_malformed_length();
    test_app_ids_parse_empty();

    test_file_ids_parse_exact_fit();
    test_file_ids_parse_truncated();
    test_file_ids_parse_empty();

    test_file_settings_parse_standard();
    test_file_settings_parse_backup();
    test_file_settings_parse_value();
    test_file_settings_parse_linear_record();
    test_file_settings_parse_cyclic_record();
    test_file_settings_parse_transaction_mac_lrp();
    test_file_settings_parse_transaction_mac_aes();
    test_file_settings_parse_transaction_mac_aes_too_short_is_rejected();
    test_file_settings_parse_additional_access_rights_tail();
    test_file_settings_parse_additional_access_rights_tail_truncated();
    test_file_settings_parse_truncated_standard();
    test_file_settings_parse_truncated_value();
    test_file_settings_parse_unknown_type();
    test_file_settings_parse_too_short_header();

    test_file_type_name_never_null();
    test_comm_name_never_null();
    test_read_status_name_never_null();

    test_deep_reset_clears_everything();
    test_deep_reset_null_is_safe();

    printf("mf_desfire_parse_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
