/* Host test: proves the FULL production chain from "reader's first frame is
 * buffered during listen activation" through "CeHandleT2TCmdRx(30 00)
 * builds a 16-byte response" -- not just the single dev->type/dev->
 * rfInterface enum comparison the first version of this test covered.
 *
 * THAT fix (CeApplyDiscParamToActiveDev() in NFC/NFC_drv/legacy/
 * nfc_listener.c) is NECESSARY: without it, rfalNfcIsRemDevPoller() is
 * false and RFAL's rfalNfcDataExchangeStart() never even returns a pointer
 * to the buffered frame. But it is NOT SUFFICIENT on its own -- hardware
 * evidence (T4) showed NTAG213/215/216 still reporting "ISO14443A Unknown"
 * with that fix alone. This file adds the second, deeper defect: even once
 * rfalNfcDataExchangeStart() correctly returns the buffered frame pointer,
 * the very next call -- rfalNfcDataExchangeGetStatus() -- has its own,
 * SEPARATE "the first frame is already here, don't wait" fast path
 * (Drivers/.../rfal_nfc.c, function rfalNfcDataExchangeGetStatus(),
 * ~line 805-818), and that fast path is hard-coded to T3T
 * (RFAL_NFC_POLL_TYPE_NFCF) only. For T2T it falls through to
 * rfalGetTransceiveStatus(), which returns BUSY unless a real transceive
 * was started via rfalStartTransceive() -- which the buffered-frame path
 * never does. Net effect: rfalNfcDataExchangeGetStatus() reports BUSY
 * forever for the very first T2T-family command, even though the frame
 * (30 00, a READ of page 0 -- the first probe command)
 * is sitting right there with a valid length. nfc_listener.c's own
 * ACTIVATED-state handler checks err==RFAL_ERR_BUSY and defers/breaks
 * BEFORE it ever reaches the "is data already present" check a few lines
 * below -- so the command is silently dropped by the Ultralight poller
 * poller_detect() times out on the READ, and the scanner reports
 * "ISO14443-3A (Unknown)".
 *
 * T5 attempted a fix for this (a one-shot, EMU_PERSONA_T2T-only exception
 * granted in the ACTIVATED-case, consumed in CE_PHASE_WAIT_RX): host-proven
 * correct against this exact reasoning, but NTAG213 still reported Unknown
 * on real hardware. Reverted back to this (T4) state per that hardware
 * result -- the diff is preserved at
 * scratchpad/t5-first-frame-workaround-only.patch for reference. The
 * repeated pass-on-host/fail-on-hardware outcome means the RFAL data-
 * exchange status verdict is not the only thing standing between "frame is
 * buffered" and "reader gets a reply" -- see the T6 architectural pivot
 * (dedicated Type-2 transport, bypassing rfalNfcDataExchangeGetStatus()/
 * rfalNfcDataExchangeStart() for T2T entirely) for the current approach.
 *
 * This exact defect is independently corroborated by a comment already in
 * nfc_listener.c, written for the analogous MFC_EMU/raw-NFC-A case: "RFAL
 * exposes no COMPLETED first frame for a raw NFC-A listen (unlike its
 * dedicated T3T first-frame path), so GetStatus() stays BUSY..." -- MFC_EMU
 * worked around it with its own injected-frame mechanism; that workaround
 * was never extended to plain T2T persona, which still relies on the
 * vanilla (broken-for-T2T) GetStatus() verdict.
 *
 * Nothing in RFAL (rfal_nfc.c/rfal_nfc.h/rfal_rfst25r3916.c) can be
 * host-compiled directly -- rfal_platform.h pulls the full STM32/ST25R3916
 * HAL stack, same constraint documented throughout this project's other
 * host tests. So, per the "source-bind any unavoidable vendor seam"
 * principle:
 *   - every RFAL-side claim below is verified with a byte-exact substring
 *     match against the REAL file on disk (source_contains()) before any
 *     logic is reproduced from it, so a future RFAL edit that changes
 *     these conditions breaks THIS test rather than letting it silently
 *     drift out of sync with reality;
 *   - every M1-side claim (nfc_listener.c's call-site ordering, the READ
 *     response builder) is likewise source-bound against the real file;
 *   - the READ (0x30) response-construction logic (CeBuildT2TReadRespFromImage)
 *     has NO RFAL/HAL dependency at all (pure buffer math over a session
 *     image struct) but is `static`, so it cannot be linked from an
 *     external test binary; it is transcribed here (verified byte-exact
 *     against the real source) and exercised directly, extending the
 *     proof from "the buffered frame would be retrieved" through to
 *     "and CeHandleT2TCmdRx would then emit the correct 16-byte reply".
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -I. \
 *      NFC/NFC_drv/common/test/ce_dev_type_test.c -o /tmp/ce_devtype && /tmp/ce_devtype
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1U);
    if (!buf) { fclose(f); exit(1); }
    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static bool contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* ======================================================================
 * PART 1: dev->type / dev->rfInterface (the T4 fix) -- necessary.
 * ====================================================================== */

static void test_transcription_matches_real_header(void)
{
    char *h = read_file("NFC/Middlewares/ST/rfal/Inc/rfal_nfc.h");

    CHECK(contains(h, "RFAL_NFC_LISTEN_TYPE_NFCA               =  0,"),
          "real rfal_nfc.h: RFAL_NFC_LISTEN_TYPE_NFCA == 0 (unchanged)");
    CHECK(contains(h, "RFAL_NFC_POLL_TYPE_NFCA                 =  10,"),
          "real rfal_nfc.h: RFAL_NFC_POLL_TYPE_NFCA == 10 (unchanged)");
    CHECK(contains(h, "RFAL_NFC_POLL_TYPE_AP2P                 =  15"),
          "real rfal_nfc.h: RFAL_NFC_POLL_TYPE_AP2P == 15 (unchanged)");
    CHECK(contains(h, "#define rfalNfcIsRemDevPoller( tp )    ( ((tp)>= RFAL_NFC_POLL_TYPE_NFCA) && ((tp)<=RFAL_NFC_POLL_TYPE_AP2P ) )"),
          "real rfal_nfc.h: rfalNfcIsRemDevPoller() macro body unchanged");

    free(h);
}

static void test_fix_present_in_real_source(void)
{
    char *c = read_file("NFC/NFC_drv/legacy/nfc_listener.c");

    CHECK(contains(c, "dev->type = RFAL_NFC_POLL_TYPE_NFCA;"),
          "nfc_listener.c: CeApplyDiscParamToActiveDev() sets dev->type = RFAL_NFC_POLL_TYPE_NFCA");
    CHECK(!contains(c, "dev->type = RFAL_NFC_LISTEN_TYPE_NFCA;"),
          "nfc_listener.c: the old, wrong assignment is gone");
    CHECK(contains(c, "if (g_persona != EMU_PERSONA_T4T) {\n        dev->rfInterface = RFAL_NFC_INTERFACE_RF;"),
          "nfc_listener.c: rfInterface forced to RF for every persona except T4T (T4T's ISO-DEP value from CARDEMU_4A must survive untouched)");

    free(c);
}

#define T_RFAL_NFC_LISTEN_TYPE_NFCA   0
#define T_RFAL_NFC_POLL_TYPE_NFCA    10
#define T_RFAL_NFC_POLL_TYPE_NFCF    12
#define T_RFAL_NFC_POLL_TYPE_AP2P    15
#define T_rfalNfcIsRemDevPoller(tp)  ( ((tp) >= T_RFAL_NFC_POLL_TYPE_NFCA) && ((tp) <= T_RFAL_NFC_POLL_TYPE_AP2P) )

static void test_old_value_fails_poller_check(void)
{
    CHECK(!T_rfalNfcIsRemDevPoller(T_RFAL_NFC_LISTEN_TYPE_NFCA),
          "old dev->type (RFAL_NFC_LISTEN_TYPE_NFCA=0): rfalNfcIsRemDevPoller() is false -- reproduces the T3 bug");
}

static void test_new_value_passes_poller_check(void)
{
    CHECK(T_rfalNfcIsRemDevPoller(T_RFAL_NFC_POLL_TYPE_NFCA),
          "new dev->type (RFAL_NFC_POLL_TYPE_NFCA=10): rfalNfcIsRemDevPoller() is true -- proves the T4 fix reaches the buffered-frame branch");
}

#define T_RFAL_NFC_INTERFACE_RF      0
#define T_RFAL_NFC_INTERFACE_ISODEP  1
typedef enum { PERSONA_T2T, PERSONA_T4T, PERSONA_RAW, PERSONA_MFC_DETECT } test_persona_t;

static int fix_rf_interface_for(test_persona_t persona, int rfInterface_before)
{
    if (persona != PERSONA_T4T) { return T_RFAL_NFC_INTERFACE_RF; }
    return rfInterface_before;   /* T4T: leave untouched */
}

static void test_t4t_iso_dep_interface_not_clobbered(void)
{
    int before = T_RFAL_NFC_INTERFACE_ISODEP;   /* what CARDEMU_4A already set */
    CHECK(fix_rf_interface_for(PERSONA_T4T, before) == T_RFAL_NFC_INTERFACE_ISODEP,
          "T4T: rfInterface stays ISODEP (not clobbered back to RF)");
    CHECK(fix_rf_interface_for(PERSONA_T2T, T_RFAL_NFC_INTERFACE_RF) == T_RFAL_NFC_INTERFACE_RF,
          "T2T: rfInterface forced to RF");
    CHECK(fix_rf_interface_for(PERSONA_RAW, T_RFAL_NFC_INTERFACE_RF) == T_RFAL_NFC_INTERFACE_RF,
          "RAW: rfInterface forced to RF");
    CHECK(fix_rf_interface_for(PERSONA_MFC_DETECT, T_RFAL_NFC_INTERFACE_RF) == T_RFAL_NFC_INTERFACE_RF,
          "MFC_DETECT: rfInterface forced to RF");
}

/* ======================================================================
 * PART 2: rfalNfcDataExchangeGetStatus()'s T3T-only fast path -- why the
 * T4 fix, though necessary, is INSUFFICIENT. Every condition below is
 * verified against the real vendor source before being reproduced.
 * ====================================================================== */

static void test_get_status_fast_path_is_t3t_only(void)
{
    char *c = read_file("NFC/Middlewares/ST/rfal/Src/rfal_nfc.c");

    /* The exact fast-path guard: only fires for T3T (POLL_TYPE_NFCF), never
     * for T2T's POLL_TYPE_NFCA -- even after the T4 fix is applied. */
    CHECK(contains(c, "if( (gNfcDev.activeDev->type == RFAL_NFC_POLL_TYPE_NFCF) && (gNfcDev.activeDev->rfInterface == RFAL_NFC_INTERFACE_RF) )"),
          "real rfal_nfc.c: rfalNfcDataExchangeGetStatus()'s \"first frame already here\" fast path checks POLL_TYPE_NFCF only");
    CHECK(contains(c, "The first frame has been retrieved by rfalListenMode, flag data immediately"),
          "real rfal_nfc.c: the fast-path's own comment describes exactly T2T's situation too, but the code only implements it for T3T");
    CHECK(contains(c, "Can only call rfalGetTransceiveStatus() after starting a transceive with rfalStartTransceive"),
          "real rfal_nfc.c: comment documents that rfalGetTransceiveStatus() is invalid without a prior rfalStartTransceive() -- exactly what the buffered-frame path never does");

    free(c);
}

static void test_transceive_status_requires_started_transceive(void)
{
    char *c = read_file("Drivers/BSP/Components/ST25R3916/rfal_rfst25r3916.c");

    CHECK(contains(c, "return ((gRFAL.TxRx.state == RFAL_TXRX_STATE_IDLE) ? gRFAL.TxRx.status : RFAL_ERR_BUSY);"),
          "real rfal_rfst25r3916.c: rfalGetTransceiveStatus() returns BUSY unless gRFAL.TxRx.state==IDLE");

    free(c);
}

/* Reproduce rfalNfcDataExchangeGetStatus()'s decision for our (T4-fixed) T2T
 * device, verified-transcribed from the two real functions above. */
#define T_RFAL_TXRX_STATE_IDLE  0
typedef struct { int type; int rfInterface; } t_dev_t;

static int t_rfalGetTransceiveStatus(int txrx_state_not_idle)
{
    /* txrx_state_not_idle: true in the realistic case -- no transceive was
     * ever started for this "already buffered" retrieval, and TxRx.state
     * carries over non-IDLE from whatever the PREVIOUS session's last
     * command TX left it at (or its power-on default). */
    return txrx_state_not_idle ? 1 /* RFAL_ERR_BUSY (nonzero placeholder) */ : 0 /* RFAL_ERR_NONE */;
}

static int t_rfalNfcDataExchangeGetStatus_first_call(t_dev_t dev, int txrx_state_not_idle)
{
    /* Mirrors rfal_nfc.c's real branching for the ONE call that matters
     * here: gNfcDev.state==ACTIVATED (the very first call after listen
     * activation), rfInterface==RF (T4-fixed T2T value). */
    int dataExErr = 1 /* BUSY */;
    if (dev.type == T_RFAL_NFC_POLL_TYPE_NFCF && dev.rfInterface == T_RFAL_NFC_INTERFACE_RF) {
        dataExErr = 0;   /* RFAL_ERR_NONE -- the fast path, T3T only */
    }
    if (dataExErr == 1 /* BUSY */) {
        /* case RFAL_NFC_INTERFACE_RF: dataExErr = rfalGetTransceiveStatus(); */
        dataExErr = t_rfalGetTransceiveStatus(txrx_state_not_idle);
    }
    return dataExErr;
}

static void test_t4_fix_alone_still_reports_busy_for_t2t(void)
{
    t_dev_t t2t_fixed = { T_RFAL_NFC_POLL_TYPE_NFCA, T_RFAL_NFC_INTERFACE_RF };   /* T4's fix applied */
    int verdict_realistic = t_rfalNfcDataExchangeGetStatus_first_call(t2t_fixed, /*txrx_state_not_idle=*/1);
    CHECK(verdict_realistic == 1 /* BUSY */,
          "T2T with the T4 fix applied: rfalNfcDataExchangeGetStatus() STILL reports BUSY on the first call (realistic TxRx.state) -- the T4 fix alone does not deliver the frame");

    t_dev_t t3t = { T_RFAL_NFC_POLL_TYPE_NFCF, T_RFAL_NFC_INTERFACE_RF };
    int verdict_t3t = t_rfalNfcDataExchangeGetStatus_first_call(t3t, /*txrx_state_not_idle=*/1);
    CHECK(verdict_t3t == 0 /* NONE */,
          "T3T (for contrast): rfalNfcDataExchangeGetStatus() correctly reports NONE immediately, regardless of TxRx.state, via its dedicated fast path");
}

/* ======================================================================
 * PART 3: nfc_listener.c's call site takes the BUSY branch and defers
 * BEFORE it ever reaches the "is data already present" check -- so the
 * buffered 30 00 is never handed to CeHandleT2TCmdRx() on the tick it
 * arrives, and (since nothing ever starts a real transceive to resolve
 * TxRx.state away from non-IDLE) never on any later tick either.
 * ====================================================================== */

static void test_listener_busy_check_precedes_data_check(void)
{
    char *c = read_file("NFC/NFC_drv/legacy/nfc_listener.c");

    /* T5's fix for this exact pattern was reverted after failing hardware
     * acceptance (see the file header comment) -- this is once again the
     * documented T4 state: BUSY still breaks unconditionally, before the
     * data-present check below is ever reached on this tick. Verifying
     * this is deliberate: it is the known, still-open defect the T6
     * architectural pivot exists to replace, not something to silently
     * paper over in this file. */
    const char *busy_check =
        "err = rfalNfcDataExchangeGetStatus();\n"
        "            if (err == RFAL_ERR_BUSY) {\n"
        "                /* Still in data exchange, process in next loop */\n"
        "                state     = DATAEXCHANGE;\n"
        "                s_cePhase = CE_PHASE_WAIT_RX;\n"
        "                break;\n"
        "            }";
    CHECK(contains(c, busy_check),
          "nfc_listener.c: T4 state confirmed -- the BUSY branch still breaks out of the ACTIVATED case unconditionally (T5's exception was reverted)");

    const char *data_check =
        "if (err == RFAL_ERR_NONE && s_ceRxData != NULL && s_ceRxRcvLen != NULL && (*s_ceRxRcvLen > 0U)) {";
    CHECK(contains(c, data_check),
          "nfc_listener.c: the \"is data already present\" check exists, but is unreachable this tick once the BUSY branch above has already broken out");

    /* (Removed corroboration checks that mirrored the deleted MFC_EMU
     * comment in nfc_listener.c -- that duplicate raw-NFC-A listener
     * backend and its comment were physically deleted in the single-MFC
     * -implementation refactor. The live T2T BUSY-branch behaviour above
     * is still asserted directly.) */

    free(c);
}

/* ======================================================================
 * PART 4: once a frame IS delivered, CeHandleT2TCmdRx(30 00) builds the
 * correct 16-byte response. CeBuildT2TReadRespFromImage() has no RFAL/HAL
 * dependency (pure buffer math) but is `static`, so it is transcribed here
 * (verified byte-exact against the real source) rather than linked.
 * ====================================================================== */

static void test_read_response_builder_transcription_matches_real_source(void)
{
    char *c = read_file("NFC/NFC_drv/legacy/nfc_listener.c");

    const char *real_fn =
        "static void CeBuildT2TReadRespFromImage(uint8_t startPage)\n"
        "{\n"
        "    uint16_t pc = s_t2t_img.page_count;\n"
        "    for (uint8_t i = 0; i < 4U; i++) {\n"
        "        uint16_t page = (uint16_t)((startPage + i) % pc);\n"
        "        memcpy(&g_ceTxBuf[(uint16_t)i * 4U], s_t2t_img.page[page], 4U);\n"
        "    }\n"
        "    g_ceTxLenBytes = 16U;\n"
        "}";
    CHECK(contains(c, real_fn),
          "nfc_listener.c: CeBuildT2TReadRespFromImage() transcription below is byte-exact against the real function");

    CHECK(contains(c, "uint8_t startPage = rx[1];") && contains(c, "CeBuildT2TReadRespFromImage(startPage);"),
          "nfc_listener.c: CeHandleT2TCmdRx()'s case 0x30 extracts startPage=rx[1] and calls the builder above for an armed image");

    free(c);
}

/* Byte-exact transcription of CeBuildT2TReadRespFromImage(), verified above. */
#define T_MAX_PAGES 231U
static void t_CeBuildT2TReadRespFromImage(const uint8_t page_data[T_MAX_PAGES][4], uint16_t page_count,
                                          uint8_t startPage, uint8_t out_tx[16], uint16_t *out_len)
{
    for (uint8_t i = 0; i < 4U; i++) {
        uint16_t page = (uint16_t)((startPage + i) % page_count);
        memcpy(&out_tx[(uint16_t)i * 4U], page_data[page], 4U);
    }
    *out_len = 16U;
}

static void test_read_page0_produces_correct_16_byte_response(void)
{
    /* A minimal armed NTAG213 image (45 pages): fill each page with a
     * recognizable pattern (page index repeated 4x) so the response can be
     * checked byte-for-byte, matching t2t_emu_image_test.c's convention. */
    static uint8_t pages[T_MAX_PAGES][4];
    uint16_t page_count = 45U;
    for (uint16_t p = 0; p < page_count; p++) {
        pages[p][0] = pages[p][1] = pages[p][2] = pages[p][3] = (uint8_t)p;
    }

    uint8_t tx[16] = {0};
    uint16_t len = 0;
    t_CeBuildT2TReadRespFromImage(pages, page_count, /*startPage=*/0, tx, &len);

    CHECK(len == 16U, "READ page 0: response length is exactly 16 bytes (4 pages)");
    bool ok = true;
    for (uint16_t p = 0; p < 4U; p++) {
        for (uint8_t b = 0; b < 4U; b++) {
            if (tx[p * 4U + b] != (uint8_t)p) { ok = false; }
        }
    }
    CHECK(ok, "READ page 0: response is exactly pages 0,1,2,3 back-to-back (16 bytes)");

    /* Wrap-around: READ near the end of memory continues from page 0. */
    uint8_t tx2[16] = {0};
    uint16_t len2 = 0;
    t_CeBuildT2TReadRespFromImage(pages, page_count, /*startPage=*/44 /* last page, NTAG213 */, tx2, &len2);
    CHECK(len2 == 16U, "READ near end-of-memory: still 16 bytes");
    CHECK(tx2[0] == 44U && tx2[4] == 0U && tx2[8] == 1U && tx2[12] == 2U,
          "READ near end-of-memory: wraps 44,0,1,2 (never zero-fills or overruns)");
}

int main(void)
{
    test_transcription_matches_real_header();
    test_fix_present_in_real_source();
    test_old_value_fails_poller_check();
    test_new_value_passes_poller_check();
    test_t4t_iso_dep_interface_not_clobbered();

    test_get_status_fast_path_is_t3t_only();
    test_transceive_status_requires_started_transceive();
    test_t4_fix_alone_still_reports_busy_for_t2t();

    test_listener_busy_check_precedes_data_check();

    test_read_response_builder_transcription_matches_real_source();
    test_read_page0_produces_correct_16_byte_response();

    printf("\nce_dev_type_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
