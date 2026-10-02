/* Host-test stub of nfc_ctx.h for m1_t2t_emu_image.c -- structurally
 * faithful for the fields/functions that file actually reads, mirroring
 * ../stub/nfc_ctx.h's role for the MFC image module (see that file's header
 * comment for why a stub is used instead of the real, HAL-heavy header).
 * Kept separate from ../stub so the already-validated MFC host tests are
 * never touched by T2T-specific changes.
 * Not used by the firmware build -- host analysis only. */
#ifndef NFC_DRV_NFC_CTX_H_
#define NFC_DRV_NFC_CTX_H_

#include <stdint.h>
#include <stdbool.h>

#define M1NFC_TECH_A             0
#define M1NFC_FAM_ULTRALIGHT     1

#define M1NFC_T2TVAR_UNKNOWN     0
#define M1NFC_T2TVAR_UL          1
#define M1NFC_T2TVAR_ULC         2
#define M1NFC_T2TVAR_UL11        3
#define M1NFC_T2TVAR_UL21        4
#define M1NFC_T2TVAR_NTAG203     5
#define M1NFC_T2TVAR_NTAG213     6
#define M1NFC_T2TVAR_NTAG215     7
#define M1NFC_T2TVAR_NTAG216     8

typedef struct {
    uint8_t       tech;
    uint8_t       family;
    uint8_t       uid[10];
    uint8_t       uid_len;
    struct {
        uint8_t   atqa[2];  bool has_atqa;
        uint8_t   sak;      bool has_sak;
        uint8_t   ats[32];
        uint8_t   ats_len;
    } a;
} nfc_header_t;

typedef struct {
    char path[128];
} nfc_file_state_t;

typedef struct {
    nfc_header_t      head;
    nfc_file_state_t  file;
} nfc_run_ctx_t;

nfc_run_ctx_t *nfc_ctx_get(void);

uint8_t  nfc_ctx_get_t2t_variant(void);
void     nfc_ctx_set_t2t_variant(uint8_t variant);
uint16_t nfc_ctx_get_t2t_page_count(void);
bool     nfc_ctx_get_t2t_page(uint16_t pageIndex, uint8_t out[4]);
bool     nfc_ctx_t2t_page_valid(uint16_t pageIndex);

void     nfc_ctx_set_t2t_expected_pages(uint16_t pages);
uint16_t nfc_ctx_get_t2t_expected_pages(void);

void nfc_ctx_set_t2t_geometry_corrupt(bool corrupt);
bool nfc_ctx_t2t_geometry_corrupt(void);

void nfc_ctx_clear_t2t_signature(void);
void nfc_ctx_set_t2t_signature(const uint8_t sig[32]);
bool nfc_ctx_get_t2t_signature(uint8_t out[32]);
bool nfc_ctx_t2t_signature_valid(void);

void nfc_ctx_clear_t2t_counters(void);
void nfc_ctx_set_t2t_counter(uint8_t idx, const uint8_t val[3]);
bool nfc_ctx_get_t2t_counter(uint8_t idx, uint8_t out[3]);
bool nfc_ctx_t2t_counter_valid(uint8_t idx);

void nfc_ctx_clear_t2t_tearing(void);
void nfc_ctx_set_t2t_tearing(uint8_t idx, uint8_t val);
bool nfc_ctx_get_t2t_tearing(uint8_t idx, uint8_t *out);
bool nfc_ctx_t2t_tearing_valid(uint8_t idx);

void nfc_ctx_clear_t2t_credential(void);
void nfc_ctx_set_t2t_credential(const uint8_t pwd[4], const uint8_t pack[2]);
bool nfc_ctx_get_t2t_pwd(uint8_t out[4]);
bool nfc_ctx_get_t2t_pack(uint8_t out[2]);
bool nfc_ctx_t2t_credential_valid(void);

/* Test control (not part of the real API). */
void test_nfc_ctx_reset(void);
/* Store a page's data and mark it valid, updating max_seen_unit -- mirrors
 * nfc_storage_store_unit()'s effect on the real dump metadata exactly. */
void test_t2t_store_page(uint16_t idx, const uint8_t data[4]);
/* Set the capacity that page_count/page_valid clamp against (mirrors
 * dump.unit_count / dump.has_dump). Must be called before storing pages.
 * Also defaults expected_pages to the same value (a single, internally-
 * consistent card) -- call test_t2t_set_expected_pages() afterward to
 * override for a deliberately-truncated-read scenario. */
void test_t2t_set_capacity(uint16_t unit_count);
/* Override the expected/declared total independently of actual capacity --
 * simulates nfc_ctx_get_t2t_expected_pages() disagreeing with how much data
 * is actually present (a genuinely truncated read). */
void test_t2t_set_expected_pages(uint16_t expected_pages);

#endif
