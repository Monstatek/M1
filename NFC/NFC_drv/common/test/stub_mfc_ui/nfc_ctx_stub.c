/* Host-test stub implementation of the nfc_ctx.h accessors this render test
 * (and the real m1_mfc_emu_image.c it links, see the render test's own
 * header comment) actually use. The MFC block accessors mirror the REAL
 * implementation in NFC/NFC_drv/common/nfc_ctx.c (nfc_ctx_mfc_block_valid/
 * nfc_ctx_mfc_block/nfc_ctx_mfc_store_block) -- same pattern already
 * established by NFC/NFC_drv/common/test/stub/nfc_ctx_stub.c for
 * mfc_emu_image_test.c, extended here with nfc_ctx_get_mfc_scan() (the
 * dictionary-progress screens' own live state) and a 256-block (4K-sized)
 * backing store instead of that file's 64-block (1K-only) one, since this
 * test's fixtures reference 4K-capable geometry via mfc_dict_types.h.
 */
#include "nfc_ctx.h"
#include <string.h>

static nfc_run_ctx_t  g_ctx;
static nfc_mfc_info_t g_mfc_info;
static nfc_mfc_scan_t g_mfc_scan;
static uint8_t         g_blocks[256 * 16];
static uint8_t         g_valid[256 / 8];

void test_nfc_ctx_reset(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_mfc_info, 0, sizeof(g_mfc_info));
    memset(&g_mfc_scan, 0, sizeof(g_mfc_scan));
    memset(g_blocks, 0, sizeof(g_blocks));
    memset(g_valid, 0, sizeof(g_valid));
}

nfc_run_ctx_t  *nfc_ctx_get(void) { return &g_ctx; }
nfc_mfc_info_t *nfc_ctx_get_mfc_info(void) { return &g_mfc_info; }
nfc_mfc_scan_t *nfc_ctx_get_mfc_scan(void) { return &g_mfc_scan; }

bool nfc_ctx_mfc_block_valid(uint16_t blk)
{
    if (blk >= 256U) return false;
    return ((g_valid[blk >> 3] >> (blk & 7U)) & 1U) != 0U;
}

const uint8_t *nfc_ctx_mfc_block(uint16_t blk)
{
    if (!nfc_ctx_mfc_block_valid(blk)) return NULL;
    return &g_blocks[(uint32_t)blk * 16U];
}

void nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16])
{
    if (blk >= 256U) return;
    memcpy(&g_blocks[(uint32_t)blk * 16U], data, 16);
    g_valid[blk >> 3] |= (uint8_t)(1U << (blk & 7U));
}
