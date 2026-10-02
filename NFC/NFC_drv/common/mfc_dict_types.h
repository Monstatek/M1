/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_dict_types.h
 * @brief   MIFARE Classic full-card model + dictionary-scan progress types.
 *
 * Relocated verbatim out of nfc_ctx.h (which is not host-includable: it
 * pulls in rfal_nfc.h/main.h/mf_desfire_parse.h/nfc_transit_clipper.h for
 * OTHER card families' types) so that mfc_dict_phase.c -- the canonical
 * key-major acquisition orchestration -- and its host tests can use the
 * REAL nfc_mfc_info_t/nfc_mfc_sector_t/nfc_mfc_scan_t types directly,
 * rather than a separately-maintained mirror that risks drifting from the
 * production layout. nfc_ctx.h includes this header and re-exposes these
 * types exactly as before; every other consumer is unaffected. Depends only
 * on stdint/stdbool/mfc_result.h, all three already host-portable.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_DICT_TYPES_H_
#define NFC_DRV_MFC_DICT_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include "mfc_result.h"   /* mfc_outcome_t (nfc_mfc_info_t.outcome) */

#ifdef __cplusplus
extern "C" {
#endif

/* ======================= MIFARE Classic (full-card 1K/4K) ======================= */
/* Full-card model for MIFARE Classic 1K/4K. Per-sector key store lives here;
 * the block DATA lives in the shared dump buffer (g_nfc_dump_buf, unit_size=16,
 * bound via nfc_ctx_set_dump) indexed by ABSOLUTE block number, with the dump
 * valid-bits bitmap marking which blocks were actually read. This keeps the ctx
 * model tiny and reuses the already-allocated 4096-byte (256-block) buffer for
 * both 1K (64 blocks) and 4K (256 blocks). Unread blocks stay unread. */
#define M1NFC_MFCTYPE_UNKNOWN  0
#define M1NFC_MFCTYPE_MINI     1
#define M1NFC_MFCTYPE_1K       2
#define M1NFC_MFCTYPE_4K       3

#define M1NFC_MFC_SECTORS_1K    16    /* 1K = 16 sectors                        */
#define M1NFC_MFC_SECTORS_4K    40    /* 4K = 40 sectors                        */
#define M1NFC_MFC_SECTORS_MAX   40    /* max sectors across supported types     */
#define M1NFC_MFC_BLOCKS_1K     64    /* 1K = 64 blocks                         */
#define M1NFC_MFC_BLOCKS_4K    256    /* 4K = 256 blocks                        */
#define M1NFC_MFC_SEC_BLOCKS     4    /* small-sector block count (1K; 4K 0..31) */
#define M1NFC_MFC_BIG_BLOCKS    16    /* big-sector block count (4K sectors 32..39) */
#define M1NFC_MFC_BLOCK_SZ      16    /* 16 bytes per block                     */

/* Sector geometry (1K + 4K). Sectors 0..31 have 4 blocks; 32..39 have 16. */
static inline uint8_t m1nfc_mfc_sector_blocks(uint8_t sector)
{
    return (sector < 32U) ? (uint8_t)M1NFC_MFC_SEC_BLOCKS : (uint8_t)M1NFC_MFC_BIG_BLOCKS;
}
static inline uint16_t m1nfc_mfc_sector_first_block(uint8_t sector)
{
    return (sector < 32U) ? (uint16_t)(sector * 4U)
                          : (uint16_t)(128U + (uint16_t)(sector - 32U) * 16U);
}
static inline uint8_t m1nfc_mfc_sectors_for_type(uint8_t type)
{
    return (type == M1NFC_MFCTYPE_4K) ? (uint8_t)M1NFC_MFC_SECTORS_4K
                                      : (uint8_t)M1NFC_MFC_SECTORS_1K;
}
static inline uint16_t m1nfc_mfc_blocks_for_type(uint8_t type)
{
    return (type == M1NFC_MFCTYPE_4K) ? (uint16_t)M1NFC_MFC_BLOCKS_4K
                                      : (uint16_t)M1NFC_MFC_BLOCKS_1K;
}
static inline uint8_t m1nfc_mfc_type_from_sak(uint8_t sak)
{
    return ((sak & 0x1FU) == 0x18U) ? (uint8_t)M1NFC_MFCTYPE_4K
                                    : (uint8_t)M1NFC_MFCTYPE_1K;
}

typedef struct {
    bool     key_a_found;                        /* Key A authenticated for this sector */
    uint8_t  key_a[6];
    bool     key_b_found;                        /* Key B authenticated or recovered from trailer */
    uint8_t  key_b[6];
} nfc_mfc_sector_t;

typedef struct {
    bool     valid;                              /* a full-card read was attempted */
    uint8_t  type;                               /* M1NFC_MFCTYPE_1K / _4K          */
    uint8_t  sectors_total;                      /* 16 (1K) or 40 (4K)             */
    uint8_t  keys_total;                         /* 32 (1K) or 80 (4K)             */
    uint8_t  keys_found;                         /* X in "Keys found: X/N"         */
    uint8_t  sectors_read;                       /* X in "Sectors read: X/N" (block-coverage) */
    mfc_outcome_t     outcome;                   /* set exactly once, by whichever acquisition
                                                    * (normal Read or Dictionary Scan) last ran;
                                                    * see mfc_result.h -- MFC_OUTCOME_NONE until then */
    nfc_mfc_sector_t sec[M1NFC_MFC_SECTORS_MAX];
} nfc_mfc_info_t;

/* ---- MIFARE Classic dictionary-scan status (Stage C; transient UI state).
 * Found keys themselves live in nfc_mfc_info_t/nfc_mfc_sector_t (key_a/key_b);
 * this is only the scan-progress bookkeeping. ---- */
typedef enum {
    NFC_SCAN_IDLE = 0,
    NFC_SCAN_WAIT_CARD,   /* activating a MIFARE Classic 1K              */
    NFC_SCAN_RUNNING,     /* testing candidate keys                      */
    NFC_SCAN_DONE,        /* finished naturally                         */
    NFC_SCAN_STOPPED,     /* user-aborted (partial results kept)         */
    NFC_SCAN_FAIL,        /* not a Classic 1K (wrong/absent card at start) */
    NFC_SCAN_CARD_LOST,   /* tag removed mid-scan (partial results kept)  */
    NFC_SCAN_IDENTITY_MISMATCH  /* Find Missing Keys only: the presented card's
                                  * UID does not match the card the partial read
                                  * came from -- continuation refused, existing
                                  * mfc/sc/resume state left completely untouched */
} nfc_mfc_scan_state_t;

/* Origin of the candidate key currently under test (live activity only).
 * Mirrors mfc_key_candidate_t's (kind, from_builtin) pair exactly, so the
 * live progress screen can name its three real origins -- compiled
 * built-ins, the SD user dictionary, and the SD system dictionary file --
 * without collapsing built-ins into whichever dictionary role carries them. */
typedef enum {
    NFC_SCAN_SRC_BUILTIN = 0,   /* compiled default keys (from_builtin == true) */
    NFC_SCAN_SRC_USER,          /* SD user dictionary file                      */
    NFC_SCAN_SRC_SYSTEM         /* SD system dictionary file                    */
} nfc_mfc_scan_src_t;

typedef struct {
    uint8_t  state;        /* nfc_mfc_scan_state_t                        */
    uint8_t  cur_sector;   /* sector currently targeted (0..15)           */
    uint8_t  cur_keytype;  /* 0 = Key A, 1 = Key B (current auth target)  */
    uint8_t  cur_source;   /* nfc_mfc_scan_src_t of the current candidate */
    uint8_t  sectors_acc;  /* sectors with >=1 proven key (0..16, live)   */
    uint32_t keys_tried;   /* candidate keys consumed from the iterator, total */
    uint8_t  found;        /* sector-key slots proven found (0..32)       */
    uint32_t src_tried;    /* candidates tried WITHIN cur_source only -- resets
                             * to 1 whenever cur_source changes from the
                             * previous candidate, mirroring how sectors_acc
                             * is already incremented conditionally. Powers
                             * the Find Missing Keys progress line's
                             * "<source> <current>/<total>" (paired with a
                             * one-shot mfc_key_source_count() per source,
                             * computed by the UI, not this struct). */
} nfc_mfc_scan_t;

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_DICT_TYPES_H_ */
