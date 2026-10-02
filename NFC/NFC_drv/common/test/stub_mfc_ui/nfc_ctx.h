/* Host-test stub of nfc_ctx.h -- structurally faithful for ONLY the fields/
 * functions the MFC dictionary-UI draw code (see nfc_mfc_dict_ui_draw_extracted.c)
 * and m1_mfc_emu_image.c actually read/write, mirroring stub_desfire/common/
 * nfc_ctx.h's and test/stub/nfc_ctx.h's established role for their own
 * respective slices (see those files' own header comments): the real
 * NFC/NFC_drv/common/nfc_ctx.h pulls in rfal_nfc.h/main.h/the full STM32 HAL
 * vendor tree via fields unrelated to MFC UI rendering, which is impractical
 * to host-compile.
 *
 * nfc_mfc_info_t/nfc_mfc_sector_t/nfc_mfc_scan_t/nfc_mfc_scan_state_t/
 * nfc_mfc_scan_src_t and the M1NFC_MFCTYPE_* / M1NFC_MFC_SECTORS_MAX geometry
 * constants are NOT re-declared here -- they come straight from the REAL,
 * genuinely-portable NFC/NFC_drv/common/mfc_dict_types.h (no HAL, no RTOS;
 * already proven host-includable by mfc_key_source_test.c/mfc_dict_phase_test.c),
 * included directly below.
 *
 * Everything else (nfc_header_t, nfc_file_state_t's source_kind subset,
 * nfc_run_ctx_t, the M1NFC_FAM_* / LIVE_CARD / LOAD_FILE / M1NFC_T2TVAR_* macros)
 * is copied verbatim (field names/types/values) from the REAL, currently-
 * checked-out NFC/NFC_drv/common/nfc_ctx.h -- cross-checked line-for-line
 * against it, not guessed. Not used by the firmware build -- host analysis
 * (NFC2-005 hardware-review renders) only.
 */
#ifndef NFC_DRV_NFC_CTX_STUB_H_
#define NFC_DRV_NFC_CTX_STUB_H_

#include <stdint.h>
#include <stdbool.h>
#include "mfc_dict_types.h"   /* REAL, portable: nfc_mfc_info_t/nfc_mfc_sector_t/nfc_mfc_scan_t + geometry */
#include "mfc_key_source.h"   /* REAL, portable: mfc_key_source_cfg_t (m1_mfc_build_key_sources()'s own param type below) */

/* Real declaration lives in NFC/NFC_drv/legacy/nfc_poller.h (not host-
 * includable -- pulls FreeRTOS/HAL). Signature copied verbatim from there.
 * Unreachable at runtime for every render scenario in this test (see
 * link_stubs.c's own header comment) -- declared here only so the
 * verbatim-extracted nfc_mfc_dict_totals_ensure() compiles unchanged. */
void m1_mfc_build_key_sources(mfc_key_source_cfg_t cfgs[2],
                              const char *user_paths[2], const char *sys_paths[2]);

/* nfc_ctx.file.source_kind values -- verbatim from the real nfc_ctx.h. */
#define LIVE_CARD   0   /* Read card  */
#define LOAD_FILE   1   /* Load file  */

/* Family codes -- verbatim numeric values from the real nfc_ctx.h. */
#define M1NFC_FAM_CLASSIC        0
#define M1NFC_FAM_ULTRALIGHT     1
#define M1NFC_FAM_DESFIRE        2
#define M1NFC_FAM_15693          3   /* ISO15693 / NFC-V */
#define M1NFC_FAM_ST25TB         4   /* ST25TB / SRI / SRIX (ISO14443-B based) */

/* Type 2 Tag sub-variant codes -- verbatim from the real nfc_ctx.h. Only
 * referenced (never reached at runtime for any M1NFC_FAM_CLASSIC render
 * scenario in this test) inside the verbatim-extracted
 * nfc_can_write_ntag21x()/nfc_can_unlock()'s own switch statements --
 * present here purely so those functions compile unchanged. */
#define M1NFC_T2TVAR_UNKNOWN     0
#define M1NFC_T2TVAR_UL          1
#define M1NFC_T2TVAR_ULC         2
#define M1NFC_T2TVAR_UL11        3
#define M1NFC_T2TVAR_UL21        4
#define M1NFC_T2TVAR_NTAG203     5
#define M1NFC_T2TVAR_NTAG213     6
#define M1NFC_T2TVAR_NTAG215     7
#define M1NFC_T2TVAR_NTAG216     8

/* ======================= Header (Card Identity) -- verbatim subset ======= */
typedef struct {
    uint8_t       tech;                    /* M1NFC_TECH_A/B/F/V */
    uint8_t       family;                  /* M1NFC_FAM_CLASSIC/UL/... */

    uint8_t       uid[10];
    uint8_t       uid_len;

    struct {
        uint8_t   atqa[2];  bool has_atqa;
        uint8_t   sak;      bool has_sak;
        uint8_t   ats[32];
        uint8_t   ats_len;
    } a;
} nfc_header_t;

/* ======================= File/State -- verbatim subset =================== */
typedef struct {
    bool source_kind;   /* LIVE_CARD / LOAD_FILE -- the only field
                          * nfc_build_action_menu() reads */
} nfc_file_state_t;

/* ======================= Top-level Runtime Context -- verbatim subset ==== */
typedef struct {
    nfc_file_state_t  file;
    nfc_header_t      head;
} nfc_run_ctx_t;

nfc_run_ctx_t   *nfc_ctx_get(void);
nfc_mfc_info_t  *nfc_ctx_get_mfc_info(void);
nfc_mfc_scan_t  *nfc_ctx_get_mfc_scan(void);   /* single instance, never NULL -- matches the real signature */

/* MFC block-data accessors -- same names/signatures as the real nfc_ctx.h,
 * backed here by a plain array instead of the shared dump buffer (the real
 * implementation's own storage detail, irrelevant to what these renders
 * check). */
bool            nfc_ctx_mfc_block_valid(uint16_t blk);
const uint8_t  *nfc_ctx_mfc_block(uint16_t blk);
void            nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16]);

/* Unreachable at runtime for every render scenario in this test (guarded by
 * `c->head.family != M1NFC_FAM_ULTRALIGHT` inside the verbatim-extracted
 * nfc_can_write_ntag21x()/nfc_can_unlock(), and every scenario's family is
 * M1NFC_FAM_CLASSIC) -- declared here only so those functions compile
 * unchanged; see stub_mfc_ui/link_stubs.c for their (abort-if-actually-
 * called) bodies. */
uint8_t nfc_ctx_get_t2t_variant(void);
bool    nfc_ctx_get_t2t_auth0(uint8_t *out);
bool    nfc_ctx_get_t2t_protection_suspected(uint16_t *first_blocked_page_out);

/* Test control (not part of the real API): reset all stub state. */
void test_nfc_ctx_reset(void);

#endif
