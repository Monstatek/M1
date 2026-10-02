/* Host-test stub implementation -- see nfc_ctx.h in this directory. */
#include "nfc_ctx.h"
#include <string.h>

#define STUB_MAX_PAGES 231U

static nfc_run_ctx_t s_ctx;
static uint8_t  s_variant;
static uint8_t  s_page[STUB_MAX_PAGES][4];
static bool     s_page_valid[STUB_MAX_PAGES];
static uint32_t s_max_seen_unit;
static uint32_t s_unit_count;
static bool     s_has_dump;

nfc_run_ctx_t *nfc_ctx_get(void) { return &s_ctx; }

uint8_t nfc_ctx_get_t2t_variant(void) { return s_variant; }
void    nfc_ctx_set_t2t_variant(uint8_t variant) { s_variant = variant; }

static uint16_t s_expected_pages;
static bool     s_geometry_corrupt;

void     nfc_ctx_set_t2t_expected_pages(uint16_t pages) { s_expected_pages = pages; }
uint16_t nfc_ctx_get_t2t_expected_pages(void)            { return s_expected_pages; }

void nfc_ctx_set_t2t_geometry_corrupt(bool corrupt) { s_geometry_corrupt = corrupt; }
bool nfc_ctx_t2t_geometry_corrupt(void)             { return s_geometry_corrupt; }

static uint32_t stub_page_count(void)
{
    uint32_t pages = s_max_seen_unit + 1U;
    if ((pages == 0U) || (pages > s_unit_count)) {
        pages = s_unit_count;
    }
    return pages;
}

uint16_t nfc_ctx_get_t2t_page_count(void)
{
    if (!s_has_dump) { return 0U; }
    return (uint16_t)stub_page_count();
}

bool nfc_ctx_get_t2t_page(uint16_t pageIndex, uint8_t out[4])
{
    if (!s_has_dump || (out == NULL)) { return false; }
    if (pageIndex >= stub_page_count()) { return false; }
    memcpy(out, s_page[pageIndex], 4U);
    return true;
}

bool nfc_ctx_t2t_page_valid(uint16_t pageIndex)
{
    if (!s_has_dump) { return false; }
    if (pageIndex >= stub_page_count()) { return false; }
    return s_page_valid[pageIndex];
}

static uint8_t s_signature[32];
static bool    s_signature_valid;
static uint8_t s_counter[3][3];
static bool    s_counter_valid[3];
static uint8_t s_tearing[3];
static bool    s_tearing_valid[3];

void nfc_ctx_clear_t2t_signature(void) { s_signature_valid = false; memset(s_signature, 0, sizeof(s_signature)); }
void nfc_ctx_set_t2t_signature(const uint8_t sig[32]) { if (sig) { memcpy(s_signature, sig, sizeof(s_signature)); s_signature_valid = true; } }
bool nfc_ctx_get_t2t_signature(uint8_t out[32]) { if (!out || !s_signature_valid) { return false; } memcpy(out, s_signature, sizeof(s_signature)); return true; }
bool nfc_ctx_t2t_signature_valid(void) { return s_signature_valid; }

void nfc_ctx_clear_t2t_counters(void)
{
    for (uint8_t i = 0; i < 3U; i++) { s_counter_valid[i] = false; memset(s_counter[i], 0, sizeof(s_counter[i])); }
}
void nfc_ctx_set_t2t_counter(uint8_t idx, const uint8_t val[3])
{
    if (val && idx < 3U) { memcpy(s_counter[idx], val, 3U); s_counter_valid[idx] = true; }
}
bool nfc_ctx_get_t2t_counter(uint8_t idx, uint8_t out[3])
{
    if (!out || idx >= 3U || !s_counter_valid[idx]) { return false; }
    memcpy(out, s_counter[idx], 3U);
    return true;
}
bool nfc_ctx_t2t_counter_valid(uint8_t idx) { return (idx < 3U) && s_counter_valid[idx]; }

void nfc_ctx_clear_t2t_tearing(void)
{
    for (uint8_t i = 0; i < 3U; i++) { s_tearing_valid[i] = false; s_tearing[i] = 0U; }
}

static uint8_t s_pwd[4];
static uint8_t s_pack[2];
static bool    s_credential_valid;

void nfc_ctx_clear_t2t_credential(void)
{
    s_credential_valid = false;
    memset(s_pwd, 0, sizeof(s_pwd));
    memset(s_pack, 0, sizeof(s_pack));
}
void nfc_ctx_set_t2t_credential(const uint8_t pwd[4], const uint8_t pack[2])
{
    if (!pwd || !pack) return;
    memcpy(s_pwd, pwd, sizeof(s_pwd));
    memcpy(s_pack, pack, sizeof(s_pack));
    s_credential_valid = true;
}
bool nfc_ctx_get_t2t_pwd(uint8_t out[4])  { if (!out || !s_credential_valid) return false; memcpy(out, s_pwd, sizeof(s_pwd)); return true; }
bool nfc_ctx_get_t2t_pack(uint8_t out[2]) { if (!out || !s_credential_valid) return false; memcpy(out, s_pack, sizeof(s_pack)); return true; }
bool nfc_ctx_t2t_credential_valid(void)   { return s_credential_valid; }
void nfc_ctx_set_t2t_tearing(uint8_t idx, uint8_t val)
{
    if (idx < 3U) { s_tearing[idx] = val; s_tearing_valid[idx] = true; }
}
bool nfc_ctx_get_t2t_tearing(uint8_t idx, uint8_t *out)
{
    if (!out || idx >= 3U || !s_tearing_valid[idx]) { return false; }
    *out = s_tearing[idx];
    return true;
}
bool nfc_ctx_t2t_tearing_valid(uint8_t idx) { return (idx < 3U) && s_tearing_valid[idx]; }

void test_nfc_ctx_reset(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_variant = M1NFC_T2TVAR_UNKNOWN;
    memset(s_page, 0, sizeof(s_page));
    memset(s_page_valid, 0, sizeof(s_page_valid));
    s_max_seen_unit = 0;
    s_unit_count    = 0;
    s_has_dump      = false;
    s_expected_pages   = 0U;
    s_geometry_corrupt = false;
    nfc_ctx_clear_t2t_signature();
    nfc_ctx_clear_t2t_counters();
    nfc_ctx_clear_t2t_tearing();
    nfc_ctx_clear_t2t_credential();
}

void test_t2t_store_page(uint16_t idx, const uint8_t data[4])
{
    if (idx >= STUB_MAX_PAGES || idx >= s_unit_count) { return; }
    memcpy(s_page[idx], data, 4U);
    s_page_valid[idx] = true;
    if (idx > s_max_seen_unit) { s_max_seen_unit = idx; }
}

void test_t2t_set_capacity(uint16_t unit_count)
{
    s_unit_count = unit_count;
    s_has_dump   = true;
    /* A test that only calls set_capacity() is establishing a single,
     * internally-consistent card (variant geometry == actual dump size) --
     * exactly what nfc_storage.c's cross-check would have already
     * validated for a real reloaded file. Tests that specifically want to
     * simulate expected != actual (a genuinely truncated read) call
     * test_t2t_set_expected_pages() afterward to override this default. */
    s_expected_pages = unit_count;
}

void test_t2t_set_expected_pages(uint16_t expected_pages)
{
    s_expected_pages = expected_pages;
}
