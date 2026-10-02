/* Host-test stub of nfc_ctx.h -- structurally faithful for the fields
 * m1_mfc_emu_image.c actually reads. NOT the real header: the real one
 * transitively pulls rfal_nfc.h/main.h (the full ST25R3916/HAL vendor type
 * tree), which is impractical to host-compile for testing pure snapshot
 * logic that never touches RF hardware. Field names/types/semantics below
 * are copied verbatim from the real NFC/NFC_drv/common/nfc_ctx.h (M1NFC_
 * MFCTYPE_*, M1NFC_MFC_SECTORS_MAX, nfc_mfc_sector_t, nfc_mfc_info_t, the
 * head.uid/uid_len/a.{atqa,has_atqa,sak,has_sak} subset), verified against
 * the real file in mfc_emu_image_test.c's source-content checks.
 * Not used by the firmware build -- host analysis only, for
 * feat/mfc-saved-card-emulation host tests. */
#ifndef NFC_DRV_NFC_CTX_H_
#define NFC_DRV_NFC_CTX_H_

#include <stdint.h>
#include <stdbool.h>

#define M1NFC_FAM_CLASSIC      1
#define M1NFC_MFCTYPE_UNKNOWN  0
#define M1NFC_MFCTYPE_MINI     1
#define M1NFC_MFCTYPE_1K       2
#define M1NFC_MFCTYPE_4K       3
#define M1NFC_MFC_SECTORS_MAX  40

typedef struct {
    bool     key_a_found;
    uint8_t  key_a[6];
    bool     key_b_found;
    uint8_t  key_b[6];
} nfc_mfc_sector_t;

typedef struct {
    bool     valid;
    uint8_t  type;
    uint8_t  sectors_total;
    uint8_t  keys_total;
    uint8_t  keys_found;
    uint8_t  sectors_read;
    nfc_mfc_sector_t sec[M1NFC_MFC_SECTORS_MAX];
} nfc_mfc_info_t;

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
    nfc_header_t head;
} nfc_run_ctx_t;

nfc_run_ctx_t  *nfc_ctx_get(void);
nfc_mfc_info_t *nfc_ctx_get_mfc_info(void);
bool            nfc_ctx_mfc_block_valid(uint16_t blk);
const uint8_t  *nfc_ctx_mfc_block(uint16_t blk);
void            nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16]);

/* Test control (not part of the real API): reset all stub state. */
void test_nfc_ctx_reset(void);

#endif
