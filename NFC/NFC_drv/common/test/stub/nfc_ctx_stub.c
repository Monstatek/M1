/* Host-test stub implementation of the nfc_ctx.h accessors used by
 * m1_mfc_emu_image.c. The block accessors mirror the REAL implementation in
 * NFC/NFC_drv/common/nfc_ctx.c (nfc_ctx_mfc_block_valid/nfc_ctx_mfc_block/
 * nfc_ctx_mfc_store_block) exactly -- verified line-for-line in
 * mfc_emu_image_test.c's source-content checks. */
#include "nfc_ctx.h"
#include <string.h>

static nfc_run_ctx_t  g_ctx;
static nfc_mfc_info_t g_mfc_info;
static uint8_t         g_blocks[64 * 16];
static uint8_t          g_valid[64 / 8];

void test_nfc_ctx_reset(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_mfc_info, 0, sizeof(g_mfc_info));
    memset(g_blocks, 0, sizeof(g_blocks));
    memset(g_valid, 0, sizeof(g_valid));
}

nfc_run_ctx_t *nfc_ctx_get(void) { return &g_ctx; }
nfc_mfc_info_t *nfc_ctx_get_mfc_info(void) { return &g_mfc_info; }

bool nfc_ctx_mfc_block_valid(uint16_t blk)
{
    if (blk >= 64U) return false;
    return ((g_valid[blk >> 3] >> (blk & 7U)) & 1U) != 0U;
}

const uint8_t *nfc_ctx_mfc_block(uint16_t blk)
{
    if (!nfc_ctx_mfc_block_valid(blk)) return NULL;
    return &g_blocks[(uint32_t)blk * 16U];
}

void nfc_ctx_mfc_store_block(uint16_t blk, const uint8_t data[16])
{
    if (blk >= 64U) return;
    memcpy(&g_blocks[(uint32_t)blk * 16U], data, 16);
    g_valid[blk >> 3] |= (uint8_t)(1U << (blk & 7U));
}
