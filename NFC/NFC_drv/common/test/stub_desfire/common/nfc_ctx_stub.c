/* Host-test stub implementation -- see nfc_ctx.h in this directory. */
#include "nfc_ctx.h"
#include <string.h>

static nfc_run_ctx_t s_ctx;

nfc_run_ctx_t *nfc_ctx_get(void)
{
    return &s_ctx;
}

void nfc_ctx_clear_desfire(void)
{
    memset(&s_ctx.desfire, 0, sizeof(s_ctx.desfire));
    mf_desfire_deep_reset(&s_ctx.desfire_deep);
    nfc_transit_result_reset(&s_ctx.transit);
}

void nfc_ctx_set_desfire_version(const uint8_t *v28)
{
    s_ctx.desfire.present = (v28 != NULL);
    if (v28 != NULL) memcpy(s_ctx.desfire.v, v28, sizeof(s_ctx.desfire.v));
}

const nfc_desfire_info_t *nfc_ctx_get_desfire_info(void)
{
    return &s_ctx.desfire;
}

void nfc_ctx_set_transit(const nfc_transit_info_t *transit)
{
    if (transit == NULL) {
        nfc_transit_result_reset(&s_ctx.transit);
    } else {
        s_ctx.transit = *transit;
    }
}

const nfc_transit_info_t *nfc_ctx_get_transit(void)
{
    return &s_ctx.transit;
}

const nfc_desfire_deep_info_t *nfc_ctx_get_desfire_deep(void)
{
    return &s_ctx.desfire_deep;
}
