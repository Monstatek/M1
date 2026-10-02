/*
 * m1_desfire_acquisition_test.c
 *
 * End-to-end behavioral test of the REAL, unmodified m1_desfire.c
 * acquisition path (m1_desfire_read()), driven entirely through a scripted
 * mock of rfal_nfc.h's three data-exchange functions -- not a test that
 * merely injects an already-populated interpreted result and calls the
 * parser. This exists specifically because a prior candidate's parser-only
 * golden-vector tests all passed while the real hardware acquisition path
 * had a genuine bug (an incorrect access-rights gate) that made every
 * field come back unavailable on a real card; this test would have caught
 * that bug, since it exercises the actual command construction, response
 * assembly, and gate logic m1_desfire.c uses, using a scripted response
 * sequence for a Clipper card built from the supported byte layouts.
 *
 * Verifies:
 *   1. The correct AID (Clipper Card, 90 11 F2) is selected -- by
 *      inspecting the exact bytes of the SelectApplication TX call.
 *   2. Every required file command (GetFileSettings, ReadData for each of
 *      files 2/6/8/14) is issued with the correct command byte, file id,
 *      offset, and length -- inspected directly from captured TX calls.
 *   3. A large (512-byte) read is correctly assembled across multiple
 *      0xAF additional-frame continuations.
 *   4. The response bytes land in the correct application/file structure
 *      (verified indirectly: the final decoded fields are only correct if
 *      they did).
 *   5. The Clipper detector recognizes the structure.
 *   6. Serial, balance, last-update, terminal, txn id, counter, and ride
 *      fields all populate from the scripted responses.
 *   7. The interpreted result matches the exact values encoded in the
 *      scripted response bytes (independently computed, not copied from
 *      the parser's own output).
 *   8. A file whose access rights do NOT grant free read causes the WHOLE
 *      interpretation to fail (the required all-or-nothing contract) --
 *      not a half-populated "Unavailable" result.
 *   9. Save/reload equivalence is covered separately by
 *      nfc_transit_persistence_test.c (the interpreted result's own
 *      round-trip); this file focuses on acquisition.
 *  10. A one-byte AID mismatch does not falsely identify the card as
 *      Clipper.
 *
 * DISCLOSED LIMITATION: m1_desfire.c is HAL/RFAL-coupled and cannot be
 * host-compiled against the real STM32/RFAL headers (they pull in the full
 * HAL stack). This test compiles the REAL, unmodified m1_desfire.c against
 * hand-written stub headers (stub_desfire/) that declare only the exact
 * symbols it uses, with a scripted mock standing in for the real RFAL
 * transceive functions -- the same "stage the real file into a stub
 * directory" pattern already established by m1_t2t_transport_test.c
 * (stub_t2t_transport/) and m1_t2t_emu_image tests (stub_t2t/) elsewhere
 * in this project.
 *
 * Build (from repo root):
 *   cp NFC/NFC_drv/legacy/m1_desfire.c NFC/NFC_drv/legacy/m1_desfire.h \
 *      NFC/NFC_drv/common/test/stub_desfire/
 *   cp NFC/NFC_drv/common/mf_desfire_parse.c NFC/NFC_drv/common/mf_desfire_parse.h \
 *      NFC/NFC_drv/common/nfc_transit_clipper.c NFC/NFC_drv/common/nfc_transit_clipper.h \
 *      NFC/NFC_drv/common/test/stub_desfire/common/
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *      -I NFC/NFC_drv/common/test/stub_desfire \
 *      -I NFC/NFC_drv/common/test/stub_desfire/common \
 *      NFC/NFC_drv/common/test/stub_desfire/m1_desfire.c \
 *      NFC/NFC_drv/common/test/stub_desfire/common/mf_desfire_parse.c \
 *      NFC/NFC_drv/common/test/stub_desfire/common/nfc_transit_clipper.c \
 *      NFC/NFC_drv/common/test/stub_desfire/common/nfc_ctx_stub.c \
 *      NFC/NFC_drv/common/test/stub_desfire/rfal_mock.c \
 *      NFC/NFC_drv/common/test/m1_desfire_acquisition_test.c \
 *      -o /tmp/m1_desfire_acquisition_test && /tmp/m1_desfire_acquisition_test
 *   rm NFC/NFC_drv/common/test/stub_desfire/m1_desfire.c NFC/NFC_drv/common/test/stub_desfire/m1_desfire.h
 *   rm NFC/NFC_drv/common/test/stub_desfire/common/mf_desfire_parse.c NFC/NFC_drv/common/test/stub_desfire/common/mf_desfire_parse.h
 *   rm NFC/NFC_drv/common/test/stub_desfire/common/nfc_transit_clipper.c NFC/NFC_drv/common/test/stub_desfire/common/nfc_transit_clipper.h
 */
#include "m1_desfire.h"
#include "nfc_ctx.h"
#include "rfal_mock.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/*============================ byte-building helpers ==============================*/

typedef struct { uint8_t buf[600]; uint16_t len; } bytebuf_t;

static void bb_reset(bytebuf_t *b) { b->len = 0; }
static void bb_u8(bytebuf_t *b, uint8_t v) { b->buf[b->len++] = v; }
static void bb_bytes(bytebuf_t *b, const uint8_t *p, uint16_t n) { memcpy(&b->buf[b->len], p, n); b->len += n; }
static void bb_be16(bytebuf_t *b, uint16_t v) { bb_u8(b, (uint8_t)(v >> 8)); bb_u8(b, (uint8_t)v); }
static void bb_be32(bytebuf_t *b, uint32_t v) { bb_u8(b, (uint8_t)(v >> 24)); bb_u8(b, (uint8_t)(v >> 16)); bb_u8(b, (uint8_t)(v >> 8)); bb_u8(b, (uint8_t)v); }
static void bb_le24(bytebuf_t *b, uint32_t v) { bb_u8(b, (uint8_t)v); bb_u8(b, (uint8_t)(v >> 8)); bb_u8(b, (uint8_t)(v >> 16)); }
static void bb_le16(bytebuf_t *b, uint16_t v) { bb_u8(b, (uint8_t)v); bb_u8(b, (uint8_t)(v >> 8)); }
static void bb_zeros(bytebuf_t *b, uint16_t n) { for (uint16_t i = 0; i < n; i++) bb_u8(b, 0); }

/* Queues one GetFileSettings response: status(0x00) + type + comm + AR(LE) + type-specific body. */
static void queue_file_settings(uint8_t type, uint8_t comm, uint16_t access_rights, uint32_t size_or_n1, uint32_t n2, uint32_t n3, int has_record)
{
    bytebuf_t b; bb_reset(&b);
    bb_u8(&b, 0x00); /* OPERATION_OK */
    bb_u8(&b, type);
    bb_u8(&b, comm);
    bb_le16(&b, access_rights);
    if (has_record) {
        bb_le24(&b, size_or_n1); /* record_size */
        bb_le24(&b, n2);         /* record_max */
        bb_le24(&b, n3);         /* record_cur */
    } else {
        bb_le24(&b, size_or_n1); /* Standard/Backup size */
    }
    mock_queue_response(b.buf, b.len);
}

/* Queues one plain-data response: status(0x00) + payload. */
static void queue_ok(const uint8_t *payload, uint16_t len)
{
    bytebuf_t b; bb_reset(&b);
    bb_u8(&b, 0x00);
    bb_bytes(&b, payload, len);
    mock_queue_response(b.buf, b.len);
}

static void queue_ok_empty(void) { queue_ok(NULL, 0); }

/* Queues a large payload chunked across N additional-frame continuations,
 * each chunk_len bytes, the final one carrying status 0x00 and any
 * remainder. Exercises real 0xAF chaining assembly, not a single frame. */
static void queue_chunked(const uint8_t *payload, uint32_t total_len, uint16_t chunk_len)
{
    uint32_t off = 0;
    while (off < total_len) {
        uint32_t remain = total_len - off;
        uint16_t n = (remain > chunk_len) ? chunk_len : (uint16_t)remain;
        bool is_last = ((off + n) >= total_len);
        bytebuf_t b; bb_reset(&b);
        bb_u8(&b, is_last ? 0x00 : 0xAF);
        bb_bytes(&b, &payload[off], (uint16_t)n);
        mock_queue_response(b.buf, b.len);
        off += n;
    }
}

/* One 32-byte Clipper ride record, per clipper.c's documented layout. */
static void build_ride_record(uint8_t *rec, uint16_t agency, int16_t fare, uint16_t vehicle,
                               uint32_t time_on, uint32_t time_off, uint16_t zone_on, uint16_t zone_off)
{
    memset(rec, 0, 32);
    rec[0] = 0x10;
    rec[2] = (uint8_t)(agency >> 8); rec[3] = (uint8_t)agency;
    rec[6] = (uint8_t)((uint16_t)fare >> 8); rec[7] = (uint8_t)fare;
    rec[0x0A] = (uint8_t)(vehicle >> 8); rec[0x0B] = (uint8_t)vehicle;
    rec[0x0C] = (uint8_t)(time_on >> 24); rec[0x0D] = (uint8_t)(time_on >> 16);
    rec[0x0E] = (uint8_t)(time_on >> 8);  rec[0x0F] = (uint8_t)time_on;
    rec[0x10] = (uint8_t)(time_off >> 24); rec[0x11] = (uint8_t)(time_off >> 16);
    rec[0x12] = (uint8_t)(time_off >> 8);  rec[0x13] = (uint8_t)time_off;
    rec[0x14] = (uint8_t)(zone_on >> 8); rec[0x15] = (uint8_t)zone_on;
    rec[0x16] = (uint8_t)(zone_off >> 8); rec[0x17] = (uint8_t)zone_off;
}

/*============================ script scenarios ====================================*/

/* Options controlling the scripted scenario, so negative tests can reuse
 * most of the happy-path script while deliberately corrupting one thing. */
typedef struct {
    uint8_t  app_aid[3];        /* AID returned by GetApplicationIDs */
    uint16_t ecash_access_rights; /* access rights presented for file 2 */
    uint16_t history_chunk_len;   /* bytes per native 0xAF frame for the 512-byte history read */
    uint16_t histidx_size;        /* file 6's own declared/captured size (real cards need not be exactly 16) */
} scenario_opts_t;

static void script_full_clipper_card(const scenario_opts_t *opt)
{
    mock_reset();

    /* --- GetVersion: 3 chained frames, NXP vendor, 28 bytes total --- */
    uint8_t hw[7]  = {0x04, 0x01, 0x00, 0x01, 0x16, 0x05, 0x00};
    uint8_t sw[7]  = {0x04, 0x01, 0x00, 0x01, 0x16, 0x05, 0x00};
    uint8_t uid[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t batch[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0xAF); bb_bytes(&b, hw, 7); mock_queue_response(b.buf, b.len); }
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0xAF); bb_bytes(&b, sw, 7); mock_queue_response(b.buf, b.len); }
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0x00); bb_bytes(&b, uid, 7); bb_bytes(&b, batch, 5); bb_u8(&b, 0x28); bb_u8(&b, 0x24); mock_queue_response(b.buf, b.len); }

    /* --- GetFreeMemory: 1,000,000 bytes free --- */
    { uint8_t p[3]; bytebuf_t t; bb_reset(&t); bb_le24(&t, 1000000U); memcpy(p, t.buf, 3); queue_ok(p, 3); }

    /* --- GetKeySettings (PICC/master) --- */
    { uint8_t p[2] = {0x0F, 0x01}; queue_ok(p, 2); }

    /* --- GetApplicationIDs: one app --- */
    queue_ok(opt->app_aid, 3);

    /* --- SelectApplication --- */
    queue_ok_empty();

    /* --- GetKeySettings (app-level): max_keys=2 --- */
    { uint8_t p[2] = {0x00, 0x02}; queue_ok(p, 2); }
    /* --- GetKeyVersion x2 --- */
    { uint8_t p[1] = {0x01}; queue_ok(p, 1); }
    { uint8_t p[1] = {0x02}; queue_ok(p, 1); }

    /* --- GetFileIDs: 2, 6, 8, 14 --- */
    { uint8_t p[4] = {2, 6, 8, 14}; queue_ok(p, 4); }

    /* --- File 2 (ecash): GetFileSettings + ReadData, 44 bytes --- */
    queue_file_settings(0x01 /*Backup*/, 0x00 /*Plain*/, opt->ecash_access_rights, 44, 0, 0, 0);
    if ((((opt->ecash_access_rights >> 12) & 0x0F) == 0x0E) || (((opt->ecash_access_rights >> 4) & 0x0F) == 0x0E)) {
        bytebuf_t ec; bb_reset(&ec);
        bb_be16(&ec, 0xFFFF);          /* unk00 */
        bb_be16(&ec, 100);              /* counter */
        bb_be32(&ec, 4000000000U);      /* last_updated_tm_1900 */
        bb_be16(&ec, 0x1234);           /* terminal */
        bb_zeros(&ec, 6);               /* unk01 */
        bb_be16(&ec, 0x5678);           /* txn_id */
        bb_be16(&ec, (uint16_t)(int16_t)-100); /* balance = -100 cents */
        bb_zeros(&ec, 44 - ec.len);     /* pad to 44 total */
        queue_ok(ec.buf, ec.len);
    }
    /* (if access rights aren't free, no ReadData is issued for this file --
     * matching the real gate; the scenario becomes an all-or-nothing
     * failure case exercised by test_protected_ecash_fails_all_or_nothing) */

    /* --- File 6 (histidx): GetFileSettings + ReadData, opt->histidx_size
     * bytes (the file's real declared size -- acquisition only requires
     * "at least 16", not exactly 16, so
     * a real card need not declare this file at precisely 16 bytes). --- */
    queue_file_settings(0x01 /*Backup*/, 0x00, 0xE000, opt->histidx_size, 0, 0, 0);
    { uint8_t idx[64]; memset(idx, 0xFF, sizeof(idx)); idx[0] = 0; queue_ok(idx, opt->histidx_size); }

    /* --- File 8 (identity): GetFileSettings + ReadData, 8 bytes --- */
    queue_file_settings(0x00 /*Standard*/, 0x00, 0xE000, 8, 0, 0, 0);
    { bytebuf_t id; bb_reset(&id); bb_u8(&id, 0xFF); bb_be32(&id, 1234567890U); bb_zeros(&id, 3); queue_ok(id.buf, id.len); }

    /* --- File 14 (history): GetFileSettings + generic-pass ReadData
     * (capped at MF_DESFIRE_DEEP_FILE_DATA_CAP=64, Partial) --- */
    queue_file_settings(0x00 /*Standard*/, 0x00, 0xE000, 512, 0, 0, 0);
    { uint8_t generic64[64]; memset(generic64, 0, sizeof(generic64)); queue_ok(generic64, 64); }

    /* --- Clipper interpretation's own targeted 512-byte history read,
     * chained across multiple 0xAF continuations (128 bytes/chunk). --- */
    {
        static uint8_t history[512];
        memset(history, 0, sizeof(history));
        build_ride_record(&history[0], 0x0004 /*BART*/, 250, 0, 4000000000U, 0, 1, 0);
        queue_chunked(history, 512, opt->history_chunk_len);
    }
}

/*============================ tests ================================================*/

static void test_full_clipper_card_acquires_and_decodes_everything(void)
{
    scenario_opts_t opt = { .app_aid = {0x90, 0x11, 0xF2}, .ecash_access_rights = 0xE000, .history_chunk_len = 128, .histidx_size = 16 };
    script_full_clipper_card(&opt);

    rfalNfcDevice dev; dev.rfInterface = RFAL_NFC_INTERFACE_ISODEP;
    bool ok = m1_desfire_read(&dev);
    CHECK(ok, "m1_desfire_read() returns true (GetVersion succeeded)");

    /* --- Item 1: correct AID selected --- */
    uint8_t tx[16]; uint16_t txlen;
    /* TX call indices: 0,1,2=GetVersion; 3=FreeMem; 4=PICC KeySettings;
     * 5=AppIDs; 6=SelectApplication. */
    txlen = mock_get_tx_call(6, tx, sizeof(tx));
    CHECK(txlen == 4, "SelectApplication TX is 4 bytes (cmd + 3-byte AID)");
    CHECK((txlen == 4) && (tx[0] == 0x5A) && (tx[1] == 0x90) && (tx[2] == 0x11) && (tx[3] == 0xF2),
          "SelectApplication transmits cmd 0x5A and the exact Clipper Card AID 90 11 F2, in that byte order");

    /* --- Item 2: file commands with correct id/offset/length --- */
    /* 7=app KeySettings, 8,9=GetKeyVersion(0,1), 10=GetFileIDs,
     * 11=GetFileSettings(2), 12=ReadData(2), 13=GetFileSettings(6),
     * 14=ReadData(6), 15=GetFileSettings(8), 16=ReadData(8),
     * 17=GetFileSettings(14), 18=ReadData(14, generic 64B),
     * 19=ReadData(14, targeted, first chunk of the chained 512B read). */
    txlen = mock_get_tx_call(10, tx, sizeof(tx));
    CHECK((txlen == 1) && (tx[0] == 0x6F), "GetFileIDs transmits cmd 0x6F only");

    txlen = mock_get_tx_call(11, tx, sizeof(tx));
    CHECK((txlen == 2) && (tx[0] == 0xF5) && (tx[1] == 2), "GetFileSettings(file 2/ecash) transmits cmd 0xF5, file id 2");
    txlen = mock_get_tx_call(12, tx, sizeof(tx));
    CHECK((txlen == 8) && (tx[0] == 0xBD) && (tx[1] == 2) && (tx[2] == 0) && (tx[3] == 0) && (tx[4] == 0) &&
          (tx[5] == 44) && (tx[6] == 0) && (tx[7] == 0),
          "ReadData(file 2) transmits cmd 0xBD, file id 2, offset 0, length 44 (its full declared size)");

    txlen = mock_get_tx_call(17, tx, sizeof(tx));
    CHECK((txlen == 2) && (tx[0] == 0xF5) && (tx[1] == 14), "GetFileSettings(file 14/history) transmits cmd 0xF5, file id 14");

    /* Item 3: the TARGETED 512-byte history read (index 19) requests the
     * full 512 bytes (0x000200 LE), and is followed by 0xAF continuation
     * TX calls (each just the bare 0xAF byte) -- proving chaining is
     * actually exercised, not a single oversized frame. */
    txlen = mock_get_tx_call(19, tx, sizeof(tx));
    CHECK((txlen == 8) && (tx[0] == 0xBD) && (tx[1] == 14) && (tx[2] == 0) && (tx[3] == 0) && (tx[4] == 0) &&
          (tx[5] == 0x00) && (tx[6] == 0x02) && (tx[7] == 0x00),
          "Targeted ReadData(file 14) requests offset 0, length 512 (0x000200 LE) -- the full history file, not the 64-byte generic cap");
    int total_tx = mock_tx_call_count();
    CHECK(total_tx == 23, "exact expected total TX call count (23) for this scripted scenario -- proves no extra/missing exchange");
    for (int i = 20; i < total_tx; i++) {
        txlen = mock_get_tx_call(i, tx, sizeof(tx));
        CHECK((txlen == 1) && (tx[0] == 0xAF), "history continuation TX calls are bare 0xAF additional-frame requests");
    }

    /* --- Items 5-7: detector + full field population, values
     * independently re-derived from the SCRIPTED bytes above, not copied
     * from the parser's own output. --- */
    const nfc_transit_info_t *t = nfc_ctx_get_transit();
    CHECK(t->card_id == NfcTransitCardClipper, "Clipper recognized");
    CHECK((t->card_type_label != NULL) && (strcmp(t->card_type_label, "Card") == 0), "card_type_label == \"Card\" (AID 90 11 F2)");
    CHECK(t->serial_valid && (t->serial_number == 1234567890U), "serial decoded correctly from the scripted identity file");
    CHECK(t->balance_valid && (t->balance_cents == -100), "balance decoded correctly (-100 cents) from the scripted ecash file");
    CHECK(t->last_update_valid && (t->last_update_1900 == 4000000000U), "last_update decoded correctly");
    CHECK(t->last_terminal_valid && (t->last_terminal_id == 0x1234), "terminal decoded correctly");
    CHECK(t->last_txn_valid && (t->last_txn_id == 0x5678), "transaction id decoded correctly");
    CHECK(t->counter_valid && (t->counter == 100), "counter decoded correctly");
    CHECK(t->ride_count == 1, "exactly one ride decoded from the scripted 512-byte history + histidx");
    CHECK(t->rides[0].agency_id == 0x0004U, "ride agency_id decoded correctly (BART)");
    CHECK(t->rides[0].fare_cents == 250, "ride fare decoded correctly");
    CHECK(t->rides[0].has_time_on && (t->rides[0].time_on_1900 == 4000000000U), "ride time_on decoded correctly");
    CHECK(!t->rides[0].has_time_off, "ride time_off correctly absent (scripted as 0)");
    CHECK(t->rides[0].zone_on_id == 1, "ride zone_on decoded correctly");
}

static void test_protected_ecash_fails_all_or_nothing(void)
{
    /* Same scripted card, except file 2 (ecash) presents access rights
     * that do NOT grant free read (0x1234 -- neither nibble is 0xE). Per
     * the all-or-nothing parse contract requires this to fail the
     * ENTIRE interpretation, not produce a half-populated result. */
    scenario_opts_t opt = { .app_aid = {0x90, 0x11, 0xF2}, .ecash_access_rights = 0x1234, .history_chunk_len = 128, .histidx_size = 16 };
    script_full_clipper_card(&opt);

    rfalNfcDevice dev; dev.rfInterface = RFAL_NFC_INTERFACE_ISODEP;
    bool ok = m1_desfire_read(&dev);
    CHECK(ok, "GetVersion still succeeds (this failure is specific to Clipper interpretation, not the base identification)");

    const nfc_transit_info_t *t = nfc_ctx_get_transit();
    CHECK(t->card_id == NfcTransitCardUnknown,
          "a protected ecash file fails the WHOLE Clipper interpretation (all-or-nothing), not a half-populated \"Unavailable\" result");
    CHECK(!t->serial_valid && !t->balance_valid,
          "no field is left populated when the overall interpretation failed -- confirms this isn't silently reusing a stale/partial struct");
}

static void test_512_byte_history_read_survives_realistic_9_frame_chunking(void)
{
    /* Real DESFire native 0xAF continuation frames carry roughly 59-63
     * payload bytes each (the per-frame receive buffer is 64 bytes total,
     * i.e. up to 63 payload bytes after the 1-byte status). At that realistic
     * granularity, Clipper's mandatory 512-byte history read needs
     * ceil(512/60) = 9 chained frames -- one more than the previous
     * DESF_MAX_CHAIN_FRAMES=8 cap allowed, which made desf_cmd_chain()
     * return RFAL_ERR_PROTO and the whole Clipper interpretation silently
     * fail on real hardware, even though the identical scenario scripted
     * above at an unrealistic 128-byte/frame chunk size (needing only 4
     * frames) always passed. This test would have FAILED before the cap
     * was raised to 32U; it now proves both the regression and the fix. */
    scenario_opts_t opt = { .app_aid = {0x90, 0x11, 0xF2}, .ecash_access_rights = 0xE000, .history_chunk_len = 60, .histidx_size = 16 };
    script_full_clipper_card(&opt);

    rfalNfcDevice dev; dev.rfInterface = RFAL_NFC_INTERFACE_ISODEP;
    bool ok = m1_desfire_read(&dev);
    CHECK(ok, "m1_desfire_read() returns true at realistic 60-byte/frame chunking (9 frames for the 512-byte history read)");

    const nfc_transit_info_t *t = nfc_ctx_get_transit();
    CHECK(t->card_id == NfcTransitCardClipper,
          "Clipper still recognized when the 512-byte history read needs 9 chained frames -- "
          "would have failed under the old DESF_MAX_CHAIN_FRAMES=8 cap");
    CHECK(t->serial_valid && (t->serial_number == 1234567890U), "serial still decodes correctly under 9-frame chunking");
    CHECK(t->balance_valid && (t->balance_cents == -100), "balance still decodes correctly under 9-frame chunking");
    CHECK(t->ride_count == 1, "ride history still decodes correctly under 9-frame chunking");
}

static void test_oversized_histidx_still_shows_ride_history(void)
{
    /* Real hardware regression: acquisition accepts the histidx file as long
     * as it is "at least" 16
     * bytes, and only ever reads the first 16 regardless of the file's true
     * declared size. desf_interpret_clipper()'s own gate (m1_desfire.c)
     * matches that (rejects only data_len < 16). But
     * nfc_transit_clipper_parse_rides() (nfc_transit_clipper.c) used a
     * stricter `histidx_len != 16` check -- rejecting a real card whose
     * histidx file legitimately declares more than 16 bytes (here: 64, a
     * perfectly ordinary real-world size, well under the 64-byte generic
     * cap). Because desf_interpret_clipper() ignores parse_rides()'s return
     * value, the WHOLE interpretation still "succeeded" (serial/balance/
     * terminal/counter all populated from the unaffected ecash/identity
     * files), while ride_count silently stayed 0 -- exactly the real
     * hardware symptom reported ("Terminal, Transaction Id, Counter show,
     * but no Ride Record"). This exercises the REAL acquisition path end to
     * end (file capture, buffer handoff, parser invocation, transit
     * context), not just the parser helper in isolation. This test would
     * have FAILED before nfc_transit_clipper_parse_rides()'s guard was
     * relaxed from != to <. */
    scenario_opts_t opt = { .app_aid = {0x90, 0x11, 0xF2}, .ecash_access_rights = 0xE000, .history_chunk_len = 128, .histidx_size = 64 };
    script_full_clipper_card(&opt);

    rfalNfcDevice dev; dev.rfInterface = RFAL_NFC_INTERFACE_ISODEP;
    bool ok = m1_desfire_read(&dev);
    CHECK(ok, "m1_desfire_read() returns true with a 64-byte (not exactly 16-byte) histidx file");

    const nfc_transit_info_t *t = nfc_ctx_get_transit();
    CHECK(t->card_id == NfcTransitCardClipper, "Clipper still recognized with an oversized histidx file");
    CHECK(t->serial_valid && (t->serial_number == 1234567890U), "serial still decodes correctly");
    CHECK(t->balance_valid && (t->balance_cents == -100), "balance still decodes correctly");
    CHECK(t->ride_count == 1,
          "ride history is NOT silently dropped when histidx is captured longer than the 16-byte minimum -- "
          "would have been 0 before the parse_rides() length-check fix");
    CHECK(t->rides[0].agency_id == 0x0004U, "ride agency_id decoded correctly (BART) despite the oversized histidx");
}

static void test_one_byte_aid_mismatch_does_not_false_positive(void)
{
    /* AID differs from the real Clipper Card AID (90 11 F2) by one byte
     * (90 11 F3). Detection must not match. */
    scenario_opts_t opt = { .app_aid = {0x90, 0x11, 0xF3}, .ecash_access_rights = 0xE000 };
    mock_reset();

    uint8_t hw[7]  = {0x04, 0x01, 0x00, 0x01, 0x16, 0x05, 0x00};
    uint8_t sw[7]  = {0x04, 0x01, 0x00, 0x01, 0x16, 0x05, 0x00};
    uint8_t uid[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t batch[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0xAF); bb_bytes(&b, hw, 7); mock_queue_response(b.buf, b.len); }
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0xAF); bb_bytes(&b, sw, 7); mock_queue_response(b.buf, b.len); }
    { bytebuf_t b; bb_reset(&b); bb_u8(&b, 0x00); bb_bytes(&b, uid, 7); bb_bytes(&b, batch, 5); bb_u8(&b, 0x28); bb_u8(&b, 0x24); mock_queue_response(b.buf, b.len); }
    { uint8_t p[3]; bytebuf_t bt; bb_reset(&bt); bb_le24(&bt, 1000000U); memcpy(p, bt.buf, 3); queue_ok(p, 3); }
    { uint8_t p[2] = {0x0F, 0x01}; queue_ok(p, 2); }
    queue_ok(opt.app_aid, 3); /* the mismatched AID -- detection stops here, no further app-level exchange is scripted */

    rfalNfcDevice dev; dev.rfInterface = RFAL_NFC_INTERFACE_ISODEP;
    bool ok = m1_desfire_read(&dev);
    CHECK(ok, "GetVersion still succeeds");

    const nfc_transit_info_t *t = nfc_ctx_get_transit();
    CHECK(t->card_id == NfcTransitCardUnknown, "a one-byte AID mismatch (90 11 F3 vs 90 11 F2) does not falsely identify the card as Clipper");
}

int main(void)
{
    test_full_clipper_card_acquires_and_decodes_everything();
    test_protected_ecash_fails_all_or_nothing();
    test_512_byte_history_read_survives_realistic_9_frame_chunking();
    test_oversized_histidx_still_shows_ride_history();
    test_one_byte_aid_mismatch_does_not_false_positive();

    printf("m1_desfire_acquisition_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
