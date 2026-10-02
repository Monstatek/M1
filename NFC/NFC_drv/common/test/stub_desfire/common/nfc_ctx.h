/* Host-test stub of common/nfc_ctx.h -- structurally faithful for ONLY the
 * fields/functions m1_desfire.c actually reads/writes (desfire, desfire_deep,
 * transit), mirroring stub_t2t/nfc_ctx.h's established role for
 * m1_t2t_emu_image.c (see that file's own header comment for the rationale:
 * the real nfc_ctx.h pulls in main.h/the full STM32 HAL stack via other
 * unrelated fields, which cannot be host-compiled).
 *
 * mf_desfire_parse.h and nfc_transit_clipper.h are genuinely portable, pure
 * C with no HAL dependency -- this stub includes the REAL copies of those
 * two headers (staged into this same directory by the test's own build
 * command, exactly like m1_t2t_emu_image.c/.h are staged into stub_t2t/),
 * so mf_desfire_deep_t and nfc_transit_result_t here are the real,
 * production types, not a re-declared approximation.
 *
 * Not used by the firmware build -- host analysis only.
 */
#ifndef NFC_DRV_NFC_CTX_STUB_H_
#define NFC_DRV_NFC_CTX_STUB_H_

#include <stdint.h>
#include <stdbool.h>
#include "mf_desfire_parse.h"
#include "nfc_transit_clipper.h"

typedef struct {
    bool    present;
    uint8_t v[28];
} nfc_desfire_info_t;

typedef mf_desfire_deep_t nfc_desfire_deep_info_t;
typedef nfc_transit_result_t nfc_transit_info_t;

typedef struct {
    nfc_desfire_info_t      desfire;
    nfc_desfire_deep_info_t desfire_deep;
    nfc_transit_info_t      transit;
} nfc_run_ctx_t;

nfc_run_ctx_t *nfc_ctx_get(void);

void nfc_ctx_clear_desfire(void);
void nfc_ctx_set_desfire_version(const uint8_t *v28);
const nfc_desfire_info_t *nfc_ctx_get_desfire_info(void);

void nfc_ctx_set_transit(const nfc_transit_info_t *transit);
const nfc_transit_info_t *nfc_ctx_get_transit(void);

const nfc_desfire_deep_info_t *nfc_ctx_get_desfire_deep(void);

#endif
