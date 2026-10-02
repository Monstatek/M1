/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_block_render.h
 * @brief   Pure formatting of one MIFARE Classic 16-byte block into the two
 *          display rows shown by the Data screen.
 *
 * Each row holds four 2-byte groups (four uppercase hex characters each,
 * space-separated): "XXXX XXXX XXXX XXXX". An unread block (block16 == NULL)
 * renders both rows as "???? ???? ???? ????" -- the same four-group geometry,
 * not a single collapsed placeholder row. Byte order is preserved exactly;
 * sector trailer bytes are formatted the same as any other block (never
 * interpreted).
 *
 * Pure array-in/array-out logic, no HAL/RTOS/nfc_ctx dependency -- links and
 * runs unmodified in a host test binary, unlike the UI file (m1_nfc.c) that
 * calls it.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_BLOCK_RENDER_H_
#define NFC_DRV_MFC_BLOCK_RENDER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Formats one 16-byte MIFARE Classic block into two display rows of
 * "XXXX XXXX XXXX XXXX" (19 characters + NUL). Pass block16 == NULL for an
 * unread block; both rows are then "???? ???? ???? ????". Each rows[] entry
 * must be at least 24 bytes. */
void mfc_block_format_rows(const uint8_t *block16, char rows[2][24]);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_BLOCK_RENDER_H_ */
