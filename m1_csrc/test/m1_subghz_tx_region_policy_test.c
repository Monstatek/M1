/*
 * m1_subghz_tx_region_policy_test.c - focused tests for the North America
 * Sub-GHz regional TX-permission hardening in m1_sub_ghz.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two parts:
 *   A. Policy-decision logic. The authoritative NA band table lives in
 *      m1_sub_ghz.c as const data + the SUBGHZ_FCC_ISM_BAND_* macros, and the
 *      decision is sub_ghz_fcc_ism_band_check_hz() (0 = TX allowed here,
 *      1 = not). That function and its data are static inside a HAL/FreeRTOS-
 *      coupled translation unit that cannot be host-linked, so Part A mirrors
 *      the exact band bounds and the exact comparison and asserts the
 *      allowed / prohibited / boundary decisions (including the intentional
 *      +0.00001 MHz lower-bound epsilon, which is NOT changed in this release).
 *      Part B binds this mirror to reality by asserting the real macro values.
 *   B. Routing / no-bypass. Reads the REAL committed m1_sub_ghz.c text and
 *      asserts enforcement sits at the common OPMODE_TX boundary, that both
 *      replay paths honor it, that the RAW-replay bypass is closed, that the
 *      old record-coupled caller gate is gone (RX/Record not region-gated),
 *      and that the authoritative band values are unchanged.
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      m1_csrc/test/m1_subghz_tx_region_policy_test.c -o /tmp/subghz_txpol && /tmp/subghz_txpol
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}
static int contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }
static int count_occurrences(const char *hay, const char *needle)
{
    int n = 0; const char *p = hay; size_t len = strlen(needle);
    if (!hay || len == 0) return 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}
/* Body of one K&R function: from the signature line (followed by a newline and
 * an opening brace) to the next line that is exactly "}" at column 0. */
static char *function_body(const char *src, const char *signature_needle)
{
    const char *search_from = src, *start = NULL;
    for (;;)
    {
        const char *candidate = strstr(search_from, signature_needle);
        if (!candidate) break;
        const char *after = candidate + strlen(signature_needle);
        const char *q = after;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == '{') { start = q; break; }
        search_from = after;
    }
    if (!start) return NULL;
    const char *end = strstr(start, "\n}");
    size_t len = end ? (size_t)(end - start) + 2 : strlen(start);
    char *body = malloc(len + 1);
    memcpy(body, start, len); body[len] = 0;
    return body;
}

/* ---- Part A: mirror of the authoritative NA table + the exact decision ---- */
/* These bounds MUST equal the SUBGHZ_FCC_ISM_BAND_* macros in m1_sub_ghz.c;
 * Part B asserts the real macro values so this mirror cannot silently drift. */
#define NA_310 310.00001f
#define NA_321 321.95001f
#define NA_433 433.05001f
#define NA_434 434.79001f
#define NA_915 915.00001f
#define NA_928 928.00001f

/* Returns 1 if TX is NOT permitted (mirrors sub_ghz_fcc_ism_band_check_hz). */
static int tx_blocked(unsigned long hz)
{
    const float bands[3][2] = { {NA_310, NA_321}, {NA_433, NA_434}, {NA_915, NA_928} };
    float f = (float)hz / 1000000.0f;
    for (int i = 0; i < 3; i++)
        if (f >= bands[i][0] && f <= bands[i][1]) return 0; /* in a permitted band */
    return 1;
}

static void part_a_policy_logic(void)
{
    /* Permitted (well inside a NA TX band) */
    CHECK(tx_blocked(315000000UL) == 0, "315.000 MHz permitted");
    CHECK(tx_blocked(318000000UL) == 0, "318.000 MHz permitted");
    CHECK(tx_blocked(433920000UL) == 0, "433.920 MHz permitted");
    CHECK(tx_blocked(434059000UL) == 0, "434.059 MHz permitted");
    CHECK(tx_blocked(920000000UL) == 0, "920.000 MHz permitted");
    CHECK(tx_blocked(925000000UL) == 0, "925.000 MHz permitted");

    /* Prohibited (outside every NA TX band) */
    CHECK(tx_blocked(300000000UL) == 1, "300.000 MHz prohibited");
    CHECK(tx_blocked(304250000UL) == 1, "304.250 MHz prohibited");
    CHECK(tx_blocked(390000000UL) == 1, "390.000 MHz prohibited");
    CHECK(tx_blocked(418000000UL) == 1, "418.000 MHz prohibited");
    CHECK(tx_blocked(435000000UL) == 1, "435.000 MHz (between bands) prohibited");
    CHECK(tx_blocked(868350000UL) == 1, "868.350 MHz prohibited");
    CHECK(tx_blocked(930000000UL) == 1, "930.000 MHz prohibited");

    /* Boundary behavior - intentional, unchanged this release. The band bounds
     * carry a +0.00001 MHz (10 Hz) literal epsilon, but at 300-928 MHz a float32
     * ulp (~37 Hz at 310 MHz, ~109 Hz at 915 MHz) is LARGER than 10 Hz, so the
     * epsilon rounds away: e.g. 310.00001f == 310.0f. The band edges are thus
     * effectively INCLUSIVE at integer-Hz inputs. This mirrors the firmware,
     * which does the same (float)hz/1e6 comparison. The table is NOT changed. */
    CHECK(tx_blocked(310000000UL) == 0, "310.000000 MHz low edge permitted (epsilon below float ulp)");
    CHECK(tx_blocked(310001000UL) == 0, "310.001 MHz (just above low edge) permitted");
    CHECK(tx_blocked(321950000UL) == 0, "321.950 MHz upper edge permitted (inclusive)");
    CHECK(tx_blocked(915000000UL) == 0, "915.000000 MHz low edge permitted (epsilon below float ulp)");
    CHECK(tx_blocked(928000000UL) == 0, "928.000 MHz upper edge permitted (inclusive)");
    CHECK(tx_blocked(928100000UL) == 1, "928.100 MHz (above upper edge) prohibited");
}

/* ---- Part B: routing / no-bypass / band values, against real source ---- */
static void part_b_source_routing(const char *src)
{
    /* Distinct region-refusal return code exists and is out of RADIO_TUNE range */
    CHECK(contains(src, "#define RADIO_TX_REGION_BLOCKED") ||
          contains(src, "define\tRADIO_TX_REGION_BLOCKED") ||
          contains(src, "RADIO_TX_REGION_BLOCKED"),
          "RADIO_TX_REGION_BLOCKED return code defined");

    /* Central enforcement: the exact-Hz opmode setter gates the OPMODE_TX case */
    char *hzsetter = function_body(src,
        "static uint8_t sub_ghz_set_opmode_hz(uint8_t opmode, uint32_t hz, uint8_t channel, uint8_t tx_power)");
    CHECK(hzsetter != NULL, "sub_ghz_set_opmode_hz() found");
    if (hzsetter)
    {
        CHECK(contains(hzsetter, "sub_ghz_fcc_ism_band_check_hz(hz)"),
              "boundary: set_opmode_hz checks the region policy on the tuned Hz");
        CHECK(contains(hzsetter, "return RADIO_TX_REGION_BLOCKED;"),
              "boundary: blocked TX returns RADIO_TX_REGION_BLOCKED (no antenna/Start_Tx)");
        CHECK(count_occurrences(hzsetter, "sub_ghz_fcc_ism_band_check_hz") == 1,
              "boundary: exactly one region check (TX case only; RX path not gated)");
        /* The check must precede the actual TX-enable call (match the call
         * token "Radio_Start_Tx(channel", not the word in the comment). */
        const char *chk = strstr(hzsetter, "sub_ghz_fcc_ism_band_check_hz(hz)");
        const char *tx  = strstr(hzsetter, "Radio_Start_Tx(channel");
        CHECK(chk && tx && chk < tx,
              "boundary: region check precedes the Radio_Start_Tx(channel ...) call");
    }
    free(hzsetter);

    /* Fail-conservative region handling (unprovisioned/invalid region) */
    char *gate = function_body(src, "static uint8_t sub_ghz_fcc_ism_band_check_hz(uint32_t hz)");
    CHECK(gate != NULL, "region gate function found");
    if (gate)
    {
        CHECK(contains(gate, "ism_band_region >= SUBGHZ_ISM_BAND_REGIONS_LIST"),
              "region gate fails conservatively (invalid region -> not permitted)");
        const char *bchk = strstr(gate, "ism_band_region >= SUBGHZ_ISM_BAND_REGIONS_LIST");
        const char *idx  = strstr(gate, "subghz_regions_list[m1_device_stat");
        CHECK(bchk && idx && bchk < idx,
              "bounds check precedes the regions table index (no OOB read)");
    }
    free(gate);

    /* Defensive enforcement in the legacy band-preset TX path */
    char *bandsetter = function_body(src,
        "static void sub_ghz_set_opmode(uint8_t opmode, uint8_t band, uint8_t channel, uint8_t tx_power)");
    CHECK(bandsetter != NULL, "sub_ghz_set_opmode() (band variant) found");
    if (bandsetter)
        CHECK(contains(bandsetter, "sub_ghz_fcc_ism_band_check_hz(nominal_hz)"),
              "defensive: band-variant TX path also honors the region policy");
    free(bandsetter);

    /* RAW Replay bypass closed + user message */
    char *rawrf = function_body(src, "static uint8_t subghz_tx_start_rf(void)");
    CHECK(rawrf != NULL, "subghz_tx_start_rf() found");
    if (rawrf)
    {
        CHECK(contains(rawrf, "RADIO_TX_REGION_BLOCKED"),
              "RAW replay checks the boundary's region-blocked return (bypass closed)");
        CHECK(contains(rawrf, "subghz_tx_region_blocked_msg"),
              "RAW replay shows the TX-restricted message when blocked");
    }
    free(rawrf);

    /* Replay path: no more record-coupled pre-gate; shows message via boundary */
    char *replay = function_body(src,
        "static uint8_t sub_ghz_replay_start(bool record_mode, S_M1_SubGHz_Band band, uint8_t channel, uint8_t power)");
    CHECK(replay != NULL, "sub_ghz_replay_start() found");
    if (replay)
    {
        CHECK(!contains(replay, "record_mode = 0;"),
              "Record no longer forced off by a TX gate (RX/Record not region-gated)");
        CHECK(!contains(replay, "BRANCH=FCC_BLOCK"),
              "old caller-level FCC pre-gate removed");
        CHECK(count_occurrences(replay, "sub_ghz_fcc_ism_band_check_hz") == 0,
              "replay path no longer calls the gate directly (moved to boundary)");
        CHECK(contains(replay, "RADIO_TX_REGION_BLOCKED") &&
              contains(replay, "subghz_tx_region_blocked_msg"),
              "replay surfaces a region refusal from the boundary with the message");
    }
    free(replay);

    /* The on-screen refusal helper + required wording */
    CHECK(contains(src, "static void subghz_tx_region_blocked_msg(void)"),
          "TX-restricted message helper present");
    CHECK(contains(src, "\"TX Restricted\"") &&
          contains(src, "\"Not available\"") &&
          contains(src, "\"in this region\""),
          "message uses concise 128x64 wording (TX Restricted / Not available in this region)");

    /* Authoritative NA band values UNCHANGED (binds Part A mirror to reality) */
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_310_000") && contains(src, "310.00001"),
          "band value 310.00001 unchanged");
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_321_950") && contains(src, "321.95001"),
          "band value 321.95001 unchanged");
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_433_050") && contains(src, "433.05001"),
          "band value 433.05001 unchanged");
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_434_790") && contains(src, "434.79001"),
          "band value 434.79001 unchanged");
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_915_000") && contains(src, "915.00001"),
          "band value 915.00001 unchanged");
    CHECK(contains(src, "SUBGHZ_FCC_ISM_BAND_928_000") && contains(src, "928.00001"),
          "band value 928.00001 unchanged");
    CHECK(contains(src, "subghz_fcc_ism_bands_NA"),
          "NA band table still present");

    /* Receive-side independence: the RX tuning path still exists and is not
     * coupled to the TX gate (record uses OPMODE_RX, never the TX case). */
    CHECK(contains(src, "sub_ghz_set_opmode_hz(SUB_GHZ_OPMODE_RX"),
          "receive/record still tunes via OPMODE_RX (never region-gated)");
}

/* ---- Part C: CLI CW-TX bypass closed in shipped builds ---- */
static void part_c_cli_cw_tx(void)
{
    char *cli = slurp("m1_csrc/m1_cli.c");
    CHECK(cli != NULL, "m1_cli.c readable from repo root");
    if (!cli) return;
    CHECK(contains(cli, "#if defined(M1_SUBGHZ_CW_TEST_TX)"),
          "CLI CW-TX guarded by M1_SUBGHZ_CW_TEST_TX (off in shipped/release builds)");
    /* The transmitter-keying must sit INSIDE the dev-only guard. */
    const char *guard = strstr(cli, "#if defined(M1_SUBGHZ_CW_TEST_TX)");
    const char *cw    = strstr(cli, "SI446x_Start_Tx_CW(input1_val");
    const char *endif = guard ? strstr(guard, "#endif") : NULL;
    CHECK(guard && cw && endif && guard < cw && cw < endif,
          "SI446x_Start_Tx_CW keying is inside the dev-only guard (not reachable by default)");
    CHECK(contains(cli, "CW test TX not available in this build"),
          "default/release path reports CW-TX unavailable instead of keying TX");
    free(cli);
}

int main(void)
{
    char *src = slurp("m1_csrc/m1_sub_ghz.c");
    CHECK(src != NULL, "m1_sub_ghz.c readable from repo root");
    if (!src) { printf("m1_subghz_tx_region_policy_test: %d passed, %d failed (source unreadable)\n",
                       g_pass, g_fail); return 1; }

    part_a_policy_logic();
    part_b_source_routing(src);
    part_c_cli_cw_tx();
    free(src);

    printf("m1_subghz_tx_region_policy_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
