/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_key_source_sd.h
 * @brief   Production SD-card adapter for mfc_key_source.h's file-I/O seam.
 *
 * The only file in the MFC key-source stack that touches real FatFs/HAL
 * (nfc_fileio.h -> main.h). Compiled only into the firmware -- never into a
 * host test binary; see mfc_key_source_test.c for the mocked-I/O tests that
 * exercise mfc_key_source.c's actual production logic instead.
 *
 * LIFETIME: mfc_key_source_sd_open() uses one static nfcfio_t instance, not
 * the stack or the heap. It must not be called again for a NEW file while a
 * previously opened one is still in use -- close it first (via the returned
 * mfc_line_source_t's close()). This matches how mfc_key_source_iter_next()
 * already drives it: at most one source file open at a time, closed before
 * advancing to the next.
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_KEY_SOURCE_SD_H_
#define NFC_DRV_MFC_KEY_SOURCE_SD_H_

#include "mfc_key_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Matches mfc_path_prober_fn. `io_ctx` is unused (NULL is fine) -- probing
 * is a stateless f_open/f_close check. */
mfc_path_probe_t mfc_key_source_sd_probe(const char *path, void *io_ctx);

/* Matches mfc_line_source_opener_fn. `io_ctx` is unused (NULL is fine). The
 * returned mfc_line_source_t's tell()/seek() are populated (real SD resume
 * support, backed by nfcfio_tell_line_start()/nfcfio_seek()) -- a caller
 * that does not need resume simply never calls them. */
int mfc_key_source_sd_open(mfc_line_source_t *out, const char *path, void *io_ctx);

/* Resolves `path` (already known PRESENT, e.g. via mfc_key_source_sd_probe())
 * and reports its size and FatFs modification date/time -- the file-identity
 * snapshot mfc_dict_resume_t needs before ever trusting a persisted System
 * resume offset against it (see mfc_dict_resume_system_identity_matches()).
 * Returns 1 on success, 0 if the file could not be stat'd (out params
 * untouched). */
int mfc_key_source_sd_stat(const char *path, uint32_t *size, uint16_t *fdate, uint16_t *ftime);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_KEY_SOURCE_SD_H_ */
