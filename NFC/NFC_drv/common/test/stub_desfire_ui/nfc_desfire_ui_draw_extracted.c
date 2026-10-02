/* GENERATED FILE -- DO NOT HAND-EDIT.
 *
 * nfc_desfire_ui_draw_extracted.c
 *
 * A byte-exact, verbatim concatenation of specific line ranges copied
 * directly out of the REAL, currently-checked-out m1_csrc/m1_nfc.c by
 * extract_desfire_ui.py. Every line below is production source text,
 * character-for-character identical to what ships in m1_nfc.c today --
 * nothing here was retyped, paraphrased, or hand-adjusted. m1_csrc/m1_nfc.c
 * itself was never opened for writing; this file is a NEW, separate file.
 *
 * WHY A SLICE, NOT THE WHOLE FILE: m1_csrc/m1_nfc.c is ~7100 lines and,
 * across its full body (menu create/destroy handlers, keypad handlers,
 * MFC raw capture/detect, harvest, T2T unlock, virtual keyboard, file
 * browser, storage, NDEF, RFAL driver/listener/poller glue), references
 * on the order of 150 distinct externs from FreeRTOS/CMSIS-RTOS2, the
 * STM32 HAL, and M1's uiView menu framework -- none of which the six
 * drawing functions below actually call. Stubbing that whole surface to
 * satisfy the compiler for code paths irrelevant to on-screen pixel
 * geometry would be a disproportionate, error-prone undertaking distinct
 * from this task's actual question (does the real drawing code overlap
 * pixels). Extracting the exact, unmodified function bodies that DO matter
 * and linking them against the REAL u8g2 library and REAL nfc_ctx/
 * mf_desfire_parse/nfc_transit_clipper implementations (all genuinely
 * portable, HAL-free C, per the existing stub_desfire/common/nfc_ctx.h
 * precedent) gets real, unmodified drawing logic onto a real pixel buffer
 * without inventing a schematic re-implementation of it.
 *
 * Extracted from: m1_csrc/m1_nfc.c
 *       and (for the 4 M1_DISP_* macros only): m1_csrc/m1_display.h
 * Ranges (1-based, inclusive, as of extraction time):
 *   M1_DISP_DRAW_COLOR_BG/TXT define   : lines 19, 20 (in m1_display.h)
 *   M1_DISP_MAIN_MENU_FONT_N/B define  : lines 22, 23 (in m1_display.h)
 *   NFC_INFO_LINES_PER_SCREEN define   : line 90
 *   NFC_DESF_LIST_VISIBLE_ROWS define  : line 95
 *   NFC_FONT_STD / NFC_FONT_EMPH defines: lines 102-103
 *   file-scope static nav/scroll state : lines 193-211
 *   nfc_desf_draw_str_fit           : lines 4246-4271
 *   nfc_desfire_summary_draw        : lines 4296-4441
 *   nfc_desfire_data_draw           : lines 4444-4524
 *   nfc_desfire_transit_draw        : lines 4556-4690
 *   nfc_desfire_apps_draw           : lines 4706-4781
 *   nfc_desfire_app_detail_draw     : lines 4794-4841
 *   nfc_desfire_files_draw          : lines 4855-4923
 *   nfc_desfire_file_detail_draw    : lines 4939-5054
 *
 * If a future edit to m1_nfc.c changes these functions, re-run
 * extract_desfire_ui.py to regenerate this file with the new text -- do not
 * hand-patch this file to "keep it working"; that would defeat its purpose
 * as proof of the REAL shipped code.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u8g2.h"
#include "mf_desfire_parse.h"
#include "nfc_transit_clipper.h"
#include "nfc_ctx.h"          /* the existing stub_desfire/common structurally-faithful stub */
/* m1_u8g2 / arrowright_8x8 / target_10x10 are declared+defined by whichever
 * driver .c #includes this generated file into its own translation unit
 * (see nfc_desfire_ui_render_test.c) -- done that way, rather than via a
 * header here, because this file is only ever consumed as an #include,
 * never compiled standalone. */

/* ---- macros copied verbatim from m1_nfc.c (see header comment above) ---- */
#define M1_DISP_DRAW_COLOR_BG		0
#define M1_DISP_DRAW_COLOR_TXT		1
#define M1_DISP_MAIN_MENU_FONT_N		u8g2_font_resoledmedium_tr // 5x10 //u8g2_font_6x10_tf //, , u8g2_font_medsans_tr: clear font
#define M1_DISP_MAIN_MENU_FONT_B		u8g2_font_helvB08_tf
#define NFC_INFO_LINES_PER_SCREEN   	5
#define NFC_DESF_LIST_VISIBLE_ROWS   	(NFC_INFO_LINES_PER_SCREEN - 1)
#define NFC_FONT_STD    M1_DISP_MAIN_MENU_FONT_N   /* u8g2_font_resoledmedium_tr */
#define NFC_FONT_EMPH   M1_DISP_MAIN_MENU_FONT_B   /* u8g2_font_helvB08_tf       */

/* ---- file-scope static nav/scroll state, copied verbatim from m1_nfc.c ---- */
static uint8_t s_desf_scroll = 0;        /* DESFire Data view scroll (in lines) */
static uint8_t s_desf_line_count = 0;    /* total lines in the DESFire Data view */
static uint8_t s_desf_visible_rows = NFC_INFO_LINES_PER_SCREEN; /* shrinks by 1 when a "More" footer is shown */
/* DESFire deep-read Applications/App-Detail/Files/File-Detail navigation
 * state. Selection indices are absolute (into nfc_ctx_get_desfire_deep()'s
 * bounded arrays); *_scroll is the viewport's top row, kept in sync with
 * the selection by nfc_desfire_apps_kp_move()/nfc_desfire_files_kp_move()
 * so the highlighted row is always visible. */
static uint8_t s_desf_app_sel     = 0;
static uint8_t s_desf_apps_scroll = 0;
static uint8_t s_desf_file_sel     = 0;
static uint8_t s_desf_files_scroll = 0;
static uint8_t s_desf_sel_app_idx  = 0;  /* which application the Files/File-Detail screens are drilled into */
static uint8_t s_desf_filedetail_scroll = 0;
static uint8_t s_desf_filedetail_line_count = 0;
static uint8_t s_desf_transit_scroll = 0;
static uint8_t s_desf_transit_line_count = 0;
static uint8_t s_desf_summary_scroll = 0;
static uint8_t s_desf_summary_line_count = 0;

/* ---- nfc_desf_draw_str_fit(), copied verbatim from m1_nfc.c ---- */
static void nfc_desf_draw_str_fit(uint8_t x, uint8_t y, uint8_t max_width, const char *s)
{
    if (u8g2_GetStrWidth(&m1_u8g2, s) <= max_width) {
        u8g2_DrawStr(&m1_u8g2, x, y, s);
        return;
    }

    char buf[40];
    size_t len = strlen(s);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';

    while (len > 0) {
        len--;
        buf[len] = '\0';
        char tmp[42];
        snprintf(tmp, sizeof(tmp), "%s..", buf);
        if (u8g2_GetStrWidth(&m1_u8g2, tmp) <= max_width) {
            u8g2_DrawStr(&m1_u8g2, x, y, tmp);
            return;
        }
    }
    /* Even ".." alone doesn't fit in max_width -- draw nothing rather than
     * overflow; this only happens for a pathologically tiny max_width. */
}


/* ---- the six draw functions under test, copied verbatim from m1_nfc.c ---- */
static void nfc_desfire_summary_draw(void)
{
    const nfc_desfire_info_t *df = nfc_ctx_get_desfire_info();
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

    if (!df->present)
    {
        u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
        u8g2_DrawStr(&m1_u8g2, 2, 9, "MIFARE DESFire");
        u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);
        u8g2_DrawStr(&m1_u8g2, 2, 24, "No version data");
        return;
    }

    const uint8_t *v = df->v;
    const uint8_t hw_major = v[3], hw_storage = v[5], sw_storage = v[12];

    const char *type_str;
    switch (hw_major) {
        case 0x01: type_str = "EV1";    break;
        case 0x12: type_str = "EV2";    break;
        case 0x22: type_str = "EV2 XL"; break;
        case 0x33: type_str = "EV3";    break;
        default:   type_str = "UNK";    break;
    }
    const char *size_str;
    switch (hw_storage) {
        case 0x16: size_str = "2K";  break;
        case 0x18: size_str = "4K";  break;
        case 0x1A: size_str = "8K";  break;
        case 0x1C: size_str = "16K"; break;
        case 0x1E: size_str = "32K"; break;
        default:   size_str = "";    break;
    }

    /* Scrollable multi-line list, same architecture as
     * nfc_desfire_transit_draw() (TLINE-style accumulation + UP/DOWN scroll
     * + right-edge scrollbar) -- adopted here specifically because a real
     * production-font render (this file's own render test, not a photo)
     * proved every content line at the old fixed-baseline layout truncated
     * with "..": "Tech: ISO 14443-4 (..", "UID: 04 1F 66 D2 DA..",
     * "1 Application, 8 Fi..", "4096 bytes, 2016 by..". Real measured
     * widths (u8g2_GetStrWidth against the production font) showed the
     * combined strings exceed even a generous budget ("Tech: ISO 14443-4
     * (NFC-A)"=148px, "UID: <7 hex bytes>"=119px, "N Application(s), M
     * File(s)"=132px, "<total> bytes, <free> bytes free"=~184px, all over
     * the 118px safe width) while each HALF, split onto its own line,
     * measures well under budget (101/41, 21/~98, 95/52, 101/89
     * respectively) -- so long values are split across two lines below
     * instead of being shortened with an ellipsis, guaranteeing no
     * information is ever dropped regardless of scroll position. */
    #define NFC_DESF_SUMMARY_MAX_LINES 12
    static char s_summary_lines[NFC_DESF_SUMMARY_MAX_LINES][40];
    static bool s_summary_is_header[NFC_DESF_SUMMARY_MAX_LINES];
    uint8_t n = 0;

    #define SLINE_HDR(fmt, ...) do { if (n < NFC_DESF_SUMMARY_MAX_LINES) { \
        snprintf(s_summary_lines[n], sizeof(s_summary_lines[0]), fmt, ##__VA_ARGS__); \
        s_summary_is_header[n] = true; n++; } } while (0)
    #define SLINE(fmt, ...) do { if (n < NFC_DESF_SUMMARY_MAX_LINES) { \
        snprintf(s_summary_lines[n], sizeof(s_summary_lines[0]), fmt, ##__VA_ARGS__); \
        s_summary_is_header[n] = false; n++; } } while (0)

    SLINE_HDR("MIFARE DESFire");
    SLINE("%s %s", type_str, size_str);
    SLINE("Tech: ISO 14443-4");
    SLINE("(NFC-A)");
    SLINE("UID:");
    /* Split 4+3 bytes across two lines, not one 7-byte line: a real
     * production-font measurement (u8g2_GetStrWidth) showed the full 7-byte
     * hex string alone is 119px -- one pixel over the 118px safe budget,
     * still enough to trigger truncation even after moving the "UID:"
     * label to its own line. Two shorter lines both measure well under
     * budget. */
    SLINE("%02X %02X %02X %02X", v[14], v[15], v[16], v[17]);
    SLINE("%02X %02X %02X", v[18], v[19], v[20]);

    if (deep->apps_protected)
    {
        SLINE("Auth required to");
        SLINE("read apps!");
    }
    else
    {
        uint32_t file_total = 0;
        for (uint8_t i = 0; i < deep->app_count; i++)
        {
            const mf_desfire_app_t *app = &deep->apps[i];
            if (app->key_settings_valid && app->key_settings.free_directory_list)
                file_total += app->file_count;
        }
        SLINE("%u Application%s,", (unsigned)deep->app_count, (deep->app_count != 1) ? "s" : "");
        SLINE("%lu File%s", (unsigned long)file_total, (file_total != 1) ? "s" : "");

        uint32_t bytes_total = 1UL << (sw_storage >> 1);
        uint32_t bytes_free = deep->free_memory_valid ? deep->free_memory_bytes : 0;
        SLINE("%lu%s bytes total", (unsigned long)bytes_total, (sw_storage & 1U) ? "+" : "");
        SLINE("%lu bytes free", (unsigned long)bytes_free);
    }

    #undef SLINE_HDR
    #undef SLINE

    s_desf_summary_line_count = n;
    uint8_t max_start = (n > NFC_DESF_LIST_VISIBLE_ROWS) ? (uint8_t)(n - NFC_DESF_LIST_VISIBLE_ROWS) : 0;
    if (s_desf_summary_scroll > max_start) s_desf_summary_scroll = max_start;

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++) {
        uint8_t idx = (uint8_t)(s_desf_summary_scroll + r);
        if (idx >= n) break;
        u8g2_SetFont(&m1_u8g2, s_summary_is_header[idx] ? NFC_FONT_EMPH : NFC_FONT_STD);
        /* 118, not 124: leaves room for the scrollbar thumb (columns
         * 123-126) drawn below whenever n>NFC_DESF_LIST_VISIBLE_ROWS --
         * same budget and same reasoning as nfc_desfire_transit_draw(). */
        nfc_desf_draw_str_fit(2, (uint8_t)(9 + r * 11), 118, s_summary_lines[idx]);
    }
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (n > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 2, track_bot = 49;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_summary_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Full-width footer bar (matching nfc_desfire_apps_draw()/files_draw()'s
     * own "Select" footer convention) -- unconditionally overwrites rows
     * 52-63, so no body line above can ever visually collide with the
     * "More" icon or text regardless of its rendered width. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    #undef NFC_DESF_SUMMARY_MAX_LINES
}

static void nfc_desfire_data_draw(void)
{
    const nfc_desfire_info_t *df = nfc_ctx_get_desfire_info();
    const uint8_t *v = df->v;
    char lines[9][24];
    uint8_t n = 0;

    if (df->present)
    {
        snprintf(lines[n++], sizeof(lines[0]), "%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 v[14], v[15], v[16], v[17], v[18], v[19], v[20]);            /* UID */
        snprintf(lines[n++], sizeof(lines[0]), "hw %02X type %02X sub %02X", v[0], v[1], v[2]);
        snprintf(lines[n++], sizeof(lines[0]), " maj %02X min %02X",         v[3], v[4]);
        snprintf(lines[n++], sizeof(lines[0]), " size %02X proto %02X",      v[5], v[6]);
        snprintf(lines[n++], sizeof(lines[0]), "sw %02X type %02X sub %02X", v[7], v[8], v[9]);
        snprintf(lines[n++], sizeof(lines[0]), " maj %02X min %02X",         v[10], v[11]);
        snprintf(lines[n++], sizeof(lines[0]), " size %02X proto %02X",      v[12], v[13]);
        snprintf(lines[n++], sizeof(lines[0]), "batch %02X%02X%02X%02X%02X",
                 v[21], v[22], v[23], v[24], v[25]);
        snprintf(lines[n++], sizeof(lines[0]), "prod wk %02X yr %02X", v[26], v[27]);
    }
    else
    {
        snprintf(lines[n++], sizeof(lines[0]), "No version data");
    }

    s_desf_line_count = n;   /* used by the keypad handler for scroll bounds */

    /* "More" (RIGHT) drills into the deep-read Applications list -- only
     * offered when there is something to show there, matching every other
     * conditional "More" footer in this file (e.g. MFC Sector Keys). Shown
     * as a small corner hint (no reserved black bar), same convention as
     * nfc_info_preview_draw()'s own "More" cue; visible rows/track shrink
     * by exactly one line to make room, so the existing no-deep-data layout
     * (still the common case for older/basic DESFire cards) is completely
     * unchanged from before this milestone. */
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    bool has_more = (deep->app_count > 0) || deep->apps_protected;
    s_desf_visible_rows = has_more ? (uint8_t)(NFC_INFO_LINES_PER_SCREEN - 1) : NFC_INFO_LINES_PER_SCREEN;

    uint8_t max_start = (n > s_desf_visible_rows)
                        ? (uint8_t)(n - s_desf_visible_rows) : 0;
    if (s_desf_scroll > max_start) s_desf_scroll = max_start;

    /* The shared nfc_read_gui_update owns FirstPage/NextPage; this only draws. */
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    for (uint8_t r = 0; r < s_desf_visible_rows; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_scroll + r);
        if (idx >= n) break;
        u8g2_DrawStr(&m1_u8g2, 2, (uint8_t)(10 + r * 11), lines[idx]);
    }

    /* Vertical scroll indicator (only when the list overflows one screen). */
    if (n > s_desf_visible_rows)
    {
        const uint8_t track_top = 2;
        const uint8_t track_bot = has_more ? 49 : 61;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);

        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * s_desf_visible_rows) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;

        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_scroll) / max_start);

        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    if (has_more)
    {
        u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
        u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    }
}

static void nfc_desfire_transit_draw(void)
{
    const nfc_transit_info_t *t = nfc_ctx_get_transit();

    if (t->card_id != NfcTransitCardClipper)
    {
        /* Should be unreachable (the caller only switches to this screen
         * when card_id==Clipper) -- defensive fallback that draws nothing
         * rather than ever showing a half-populated screen. */
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
        u8g2_DrawStr(&m1_u8g2, 2, 9, "Transit Card");
        return;
    }

    #define NFC_DESF_TRANSIT_MAX_LINES 180
    static char s_transit_lines[NFC_DESF_TRANSIT_MAX_LINES][48];
    static bool s_transit_is_header[NFC_DESF_TRANSIT_MAX_LINES];
    uint8_t n = 0;

    #define TLINE_HDR(fmt, ...) do { if (n < NFC_DESF_TRANSIT_MAX_LINES) { \
        snprintf(s_transit_lines[n], sizeof(s_transit_lines[0]), fmt, ##__VA_ARGS__); \
        s_transit_is_header[n] = true; n++; } } while (0)
    #define TLINE(fmt, ...) do { if (n < NFC_DESF_TRANSIT_MAX_LINES) { \
        snprintf(s_transit_lines[n], sizeof(s_transit_lines[0]), fmt, ##__VA_ARGS__); \
        s_transit_is_header[n] = false; n++; } } while (0)

    /* \e#Clipper / Serial / Balance / Type / \e#Last Update / date+time-or-
     * "Never" / Terminal / Transaction Id / Counter -- clipper.c:272-295. */
    TLINE_HDR("Clipper");
    TLINE("Serial: %lu", (unsigned long)t->serial_number);
    {
        bool neg; int16_t dollars; uint16_t cents;
        nfc_transit_usd_split(t->balance_cents, &neg, &dollars, &cents);
        (void)neg; /* Sign state is not used directly for display. */
        TLINE("Balance: $%d.%02u", (int)dollars, (unsigned)cents);
    }
    TLINE("Type: %s", (t->card_type_label != NULL) ? t->card_type_label : "Card");

    TLINE_HDR("Last Update");
    if (t->last_update_1900 == 0U) {
        TLINE("Never"); /* clipper.c:287 -- literal, no Date:/Time: lines in this case */
    } else {
        uint16_t y; uint8_t mo, d, h, mi, s;
        nfc_transit_epoch1900_to_ymdhms(t->last_update_1900, &y, &mo, &d, &h, &mi, &s);
        TLINE("Date: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
        TLINE("Time: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
    }
    TLINE("Terminal: 0x%04x", (unsigned)t->last_terminal_id);
    TLINE("Transaction Id: %u", (unsigned)t->last_txn_id);
    TLINE("Counter: %u", (unsigned)t->counter);

    /* \e#Ride Record per entry, in histidx order -- clipper.c:479-500. */
    for (uint8_t ri = 0; ri < t->ride_count; ri++) {
        const nfc_transit_ride_t *ride = &t->rides[ri];
        TLINE_HDR("Ride Record");

        if (ride->has_time_on) {
            uint16_t y; uint8_t mo, d, h, mi, s;
            nfc_transit_epoch1900_to_ymdhms(ride->time_on_1900, &y, &mo, &d, &h, &mi, &s);
            TLINE("Date: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
            TLINE("Time: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
        }
        {
            bool neg; int16_t dollars; uint16_t cents;
            nfc_transit_usd_split(ride->fare_cents, &neg, &dollars, &cents);
            (void)neg;
            TLINE("Fare: $%d.%02u", (int)dollars, (unsigned)cents);
        }
        {
            const char *agency = nfc_transit_clipper_agency_name(ride->agency_id);
            TLINE("Agency: %s (%04x)", (agency != NULL) ? agency : "Unknown", (unsigned)ride->agency_id);
        }
        {
            const char *zone_on = nfc_transit_clipper_zone_name(ride->agency_id, ride->zone_on_id);
            TLINE("On: %s (%04x)", (zone_on != NULL) ? zone_on : "Unknown", (unsigned)ride->zone_on_id);
        }
        if (ride->vehicle_id != 0U) {
            TLINE("Vehicle id: %u", (unsigned)ride->vehicle_id);
        }
        if (ride->has_time_off) {
            const char *zone_off = nfc_transit_clipper_zone_name(ride->agency_id, ride->zone_off_id);
            TLINE("Off: %s (%04x)", (zone_off != NULL) ? zone_off : "Unknown", (unsigned)ride->zone_off_id);
            uint16_t y; uint8_t mo, d, h, mi, s;
            nfc_transit_epoch1900_to_ymdhms(ride->time_off_1900, &y, &mo, &d, &h, &mi, &s);
            TLINE("Date Off: %02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
            TLINE("Time Off: %02u:%02u:%02u (UTC)", (unsigned)h, (unsigned)mi, (unsigned)s);
        }
    }

    #undef TLINE_HDR
    #undef TLINE

    s_desf_transit_line_count = n;
    uint8_t max_start = (n > NFC_DESF_LIST_VISIBLE_ROWS) ? (uint8_t)(n - NFC_DESF_LIST_VISIBLE_ROWS) : 0;
    if (s_desf_transit_scroll > max_start) s_desf_transit_scroll = max_start;

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++) {
        uint8_t idx = (uint8_t)(s_desf_transit_scroll + r);
        if (idx >= n) break;
        u8g2_SetFont(&m1_u8g2, s_transit_is_header[idx] ? NFC_FONT_EMPH : NFC_FONT_STD);
        /* 118, not 124: the scrollbar thumb drawn below occupies columns
         * 123-126 whenever n>NFC_DESF_LIST_VISIBLE_ROWS, which is true for
         * nearly every real Clipper card (the unconditional base field set
         * alone is 8-9 lines) -- the old 124 budget let truncated text reach
         * column 123, directly under the thumb (hardware-proven overlap). */
        nfc_desf_draw_str_fit(2, (uint8_t)(9 + r * 11), 118, s_transit_lines[idx]);
    }
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (n > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 2, track_bot = 49;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_transit_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* "Info", not "More": Level 1 (this screen) -> Level 2 must be visibly
     * labelled "Info" per the required hierarchy -- Level 2 is the
     * technical DESFire Summary screen (UID/protocol/version/app count/
     * memory), reached via the identical RIGHT-button action as before,
     * just relabelled. */
    u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "Info"), 61, "Info");
    u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    #undef NFC_DESF_TRANSIT_MAX_LINES
}

static void nfc_desfire_apps_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Applications");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (deep->app_count == 0)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24,
                     deep->apps_protected ? "Auth required to" : "No applications");
        if (deep->apps_protected)
            u8g2_DrawStr(&m1_u8g2, 2, 35, "list applications");
        return;
    }

    if (s_desf_app_sel >= deep->app_count) s_desf_app_sel = (uint8_t)(deep->app_count - 1);

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_apps_scroll + r);
        if (idx >= deep->app_count) break;
        const mf_desfire_app_t *app = &deep->apps[idx];
        char row[28];
        /* "App %02x%02x%02x", lowercase, AID bytes reversed (data[2],[1],[0])
         * -- the submenu label. The
         * list never annotates a non-selectable app inline -- that failure is
         * only revealed on the App Detail screen after selecting it. */
        snprintf(row, sizeof(row), "App %02x%02x%02x", app->id.id[2], app->id.id[1], app->id.id[0]);
        /* Baselines 20,30,40,50 -- box top (y-8) gives rows 12,22,32,42, a
         * full row of real clearance below the header's measured ink-bottom
         * row 11 (verified by real u8g2 rendering + pixel scan, not font
         * metrics alone -- a baseline of 19 left zero margin, touching the
         * header at row 11 exactly). Box bottom for the last row (42+9=51)
         * stays a full row clear of the y=52 footer bar. Replaces the old
         * 12+r*11/y-9,h11 formula, whose row 0 box (y=3..14) directly
         * overlapped the header (hardware-proven). */
        uint8_t y = (uint8_t)(20 + r * 10);
        if (idx == s_desf_app_sel)
        {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(y - 8), 121, 10);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            nfc_desf_draw_str_fit(2, y, 116, row);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        }
        else
        {
            nfc_desf_draw_str_fit(2, y, 116, row);
        }
    }

    if (deep->app_count > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 12, track_bot = 50;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        uint8_t max_start = (uint8_t)(deep->app_count - NFC_DESF_LIST_VISIBLE_ROWS);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / deep->app_count);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_apps_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Footer: CENTER + Select -> the highlighted application's detail. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_desfire_app_detail_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
    char line[28];

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    snprintf(line, sizeof(line), "App %02X%02X%02X", app->id.id[0], app->id.id[1], app->id.id[2]);
    nfc_desf_draw_str_fit(2, 9, 124, line);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (!app->select_ok)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "Could not select");
        u8g2_DrawStr(&m1_u8g2, 2, 35, "this application");
        return;
    }

    uint8_t y = 22;
    if (app->key_settings_valid)
    {
        snprintf(line, sizeof(line), "Max keys: %u", (unsigned)app->key_settings.max_keys);
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
        snprintf(line, sizeof(line), "Free dir list: %s", app->key_settings.free_directory_list ? "Yes" : "No");
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, y, "Key settings: n/a"); y = (uint8_t)(y + 11);
    }

    if (app->key_version_count > 0)
    {
        snprintf(line, sizeof(line), "Key versions: %u%s", (unsigned)app->key_version_count,
                 app->key_versions_truncated ? "+" : "");
        u8g2_DrawStr(&m1_u8g2, 2, y, line); y = (uint8_t)(y + 11);
    }

    snprintf(line, sizeof(line), "Files: %u%s", (unsigned)app->file_count, app->files_truncated ? "+" : "");
    u8g2_DrawStr(&m1_u8g2, 2, y, line);

    if (app->file_count > 0)
    {
        u8g2_DrawStr(&m1_u8g2, 117 - (uint8_t)u8g2_GetStrWidth(&m1_u8g2, "More"), 61, "More");
        u8g2_DrawXBMP(&m1_u8g2, 119, 53, 8, 8, arrowright_8x8);
    }
}

static void nfc_desfire_files_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "Files");
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (app->file_count == 0)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "No files");
        return;
    }

    if (s_desf_file_sel >= app->file_count) s_desf_file_sel = (uint8_t)(app->file_count - 1);

    for (uint8_t r = 0; r < NFC_DESF_LIST_VISIBLE_ROWS; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_files_scroll + r);
        if (idx >= app->file_count) break;
        const mf_desfire_file_t *file = &app->files[idx];
        char row[28];
        /* "File %d", decimal, nothing else. Type/comm/access-rights/read-status
         * are File Detail (raw hex TextBox) content, never
         * shown in its list -- they remain available on M1's own File
         * Detail screen after selecting, just no longer crammed into this
         * row abbreviated to fit. */
        snprintf(row, sizeof(row), "File %u", (unsigned)file->id);
        /* Same corrected geometry as nfc_desfire_apps_draw() -- see the
         * comment there for the header/footer clearance derivation. */
        uint8_t y = (uint8_t)(20 + r * 10);
        if (idx == s_desf_file_sel)
        {
            u8g2_DrawBox(&m1_u8g2, 0, (uint8_t)(y - 8), 121, 10);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            nfc_desf_draw_str_fit(2, y, 116, row);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        }
        else
        {
            nfc_desf_draw_str_fit(2, y, 116, row);
        }
    }

    if (app->file_count > NFC_DESF_LIST_VISIBLE_ROWS)
    {
        const uint8_t track_top = 12, track_bot = 50;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        uint8_t max_start = (uint8_t)(app->file_count - NFC_DESF_LIST_VISIBLE_ROWS);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_DESF_LIST_VISIBLE_ROWS) / app->file_count);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_files_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }

    /* Footer: CENTER + Select -> the highlighted file's detail. */
    u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, 2, 53, 10, 10, target_10x10);
    u8g2_DrawStr(&m1_u8g2, 15, 61, "Select");
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

static void nfc_desfire_file_detail_draw(void)
{
    const nfc_desfire_deep_info_t *deep = nfc_ctx_get_desfire_deep();
    const mf_desfire_app_t *app = &deep->apps[s_desf_sel_app_idx];
    const mf_desfire_file_t *file = &app->files[s_desf_file_sel];
    const mf_desfire_file_settings_t *s = &file->settings;

    /* Worst case: 6 header lines (Comm, AR, Status, plus up to 3 more for a
     * Linear/Cyclic Record file's size/cur/max, each on its own line so no
     * single line can ever combine multiple large 24-bit DESFire fields --
     * see the hardware finding this replaced: "Rec size, cur/max" as one
     * combined line measured up to 197px wide against a 128px display)
     * plus a hex dump at 6 bytes/line (measured 108px, safely under 128;
     * 8 bytes/line measured 144px -- the confirmed hardware overflow).
     * Every write below is still bounds-checked against this capacity
     * regardless -- the arithmetic here only sizes the buffer. */
    #define NFC_DESF_HEXBYTES_PER_LINE 6U
    #define NFC_DESF_FILEDETAIL_MAX_LINES (6 + (MF_DESFIRE_DEEP_FILE_DATA_CAP + NFC_DESF_HEXBYTES_PER_LINE - 1) / NFC_DESF_HEXBYTES_PER_LINE)
    char lines[NFC_DESF_FILEDETAIL_MAX_LINES][24];
    uint8_t n = 0;

    char hdr[28];
    snprintf(hdr, sizeof(hdr), "File %02X %s", file->id, mf_desfire_file_type_name(s->type));
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_EMPH);
    nfc_desf_draw_str_fit(2, 9, 124, hdr);
    u8g2_SetFont(&m1_u8g2, NFC_FONT_STD);

    if (file->settings_valid && (n < NFC_DESF_FILEDETAIL_MAX_LINES))
    {
        snprintf(lines[n++], sizeof(lines[0]), "Comm: %s", mf_desfire_comm_name(s->comm));
        if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
        {
            snprintf(lines[n++], sizeof(lines[0]), "AR: %04X", s->access_rights);
        }
        switch (s->type)
        {
        case MfDesfireFileTypeStandard:
        case MfDesfireFileTypeBackup:
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Size: %lu bytes", (unsigned long)s->size);
            break;
        case MfDesfireFileTypeValue:
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Lo:%ld Hi:%ld", (long)s->value_lo_limit, (long)s->value_hi_limit);
            break;
        case MfDesfireFileTypeLinearRecord:
        case MfDesfireFileTypeCyclicRecord:
            /* Each field on its own line -- a 24-bit DESFire field can be
             * up to 16,777,215; combining even two of these on one line
             * was the exact combination that overflowed on hardware. */
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Rec size: %lu B", (unsigned long)s->record_size);
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Cur: %lu", (unsigned long)s->record_cur);
            if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
                snprintf(lines[n++], sizeof(lines[0]), "Max: %lu", (unsigned long)s->record_max);
            break;
        default:
            break;
        }
    }

    if (n < NFC_DESF_FILEDETAIL_MAX_LINES)
    {
        snprintf(lines[n++], sizeof(lines[0]), "Status: %s", mf_desfire_file_read_status_name(file->read_status));
    }

    if ((file->read_status == MfDesfireFileComplete || file->read_status == MfDesfireFilePartial) &&
        (file->data_len > 0U))
    {
        for (uint16_t i = 0; i < file->data_len; i += NFC_DESF_HEXBYTES_PER_LINE)
        {
            if (n >= NFC_DESF_FILEDETAIL_MAX_LINES) break; /* checked BEFORE writing, not after */
            uint16_t chunk = (uint16_t)((file->data_len - i < NFC_DESF_HEXBYTES_PER_LINE) ? (file->data_len - i) : NFC_DESF_HEXBYTES_PER_LINE);
            char *dst = lines[n];
            size_t pos = 0;
            for (uint16_t k = 0; k < chunk && pos < sizeof(lines[0]) - 3U; k++)
            {
                pos += (size_t)snprintf(dst + pos, sizeof(lines[0]) - pos, "%02X ", file->data[i + k]);
            }
            n++;
        }
    }

    s_desf_filedetail_line_count = n;
    uint8_t max_start = (n > NFC_INFO_LINES_PER_SCREEN) ? (uint8_t)(n - NFC_INFO_LINES_PER_SCREEN) : 0;
    if (s_desf_filedetail_scroll > max_start) s_desf_filedetail_scroll = max_start;

    for (uint8_t r = 0; r < NFC_INFO_LINES_PER_SCREEN; r++)
    {
        uint8_t idx = (uint8_t)(s_desf_filedetail_scroll + r);
        if (idx >= n) break;
        /* 118, not 124: same scrollbar-gutter reservation as the Transit
         * screen -- latent here today (no reachable line gets within 14px of
         * the thumb) but the same root defect, fixed proactively. */
        nfc_desf_draw_str_fit(2, (uint8_t)(21 + r * 10), 118, lines[idx]);
    }

    if (n > NFC_INFO_LINES_PER_SCREEN)
    {
        const uint8_t track_top = 12, track_bot = 61;
        const uint8_t track_h   = (uint8_t)(track_bot - track_top);
        for (uint8_t yy = track_top; yy <= track_bot; yy = (uint8_t)(yy + 3))
            u8g2_DrawPixel(&m1_u8g2, 125, yy);
        uint8_t thumb_h = (uint8_t)(((uint32_t)track_h * NFC_INFO_LINES_PER_SCREEN) / n);
        if (thumb_h < 4)       thumb_h = 4;
        if (thumb_h > track_h) thumb_h = track_h;
        uint8_t thumb_y = track_top;
        if (max_start > 0)
            thumb_y = (uint8_t)(track_top + ((uint32_t)(track_h - thumb_h) * s_desf_filedetail_scroll) / max_start);
        u8g2_DrawBox(&m1_u8g2, 123, thumb_y, 4, thumb_h);
    }
    #undef NFC_DESF_FILEDETAIL_MAX_LINES
    #undef NFC_DESF_HEXBYTES_PER_LINE
}

