/* See COPYING.txt for license details. */

/*
*
*  m1_esp_version.c
*
*  Persistent installed-ESP32-version record (Gate D, Option B). See
*  m1_esp_version.h and documentation/M1CP_gateD_esp32_design.md.
*
* M1 Project
*
*/

#include "m1_esp_version.h"
#include <stddef.h>   /* NULL */

/* RAM cache (authoritative for readers; mirrors the persisted value). */
static uint8_t s_version[4] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU };
static bool    s_valid = false;

static uint32_t pack(const uint8_t v[4])
{
    return (uint32_t)v[0] | ((uint32_t)v[1] << 8) |
           ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
}
static void unpack(uint32_t p, uint8_t out[4])
{
    out[0] = (uint8_t)(p & 0xFFU);
    out[1] = (uint8_t)((p >> 8) & 0xFFU);
    out[2] = (uint8_t)((p >> 16) & 0xFFU);
    out[3] = (uint8_t)((p >> 24) & 0xFFU);
}

/*************************** B A C K E N D **********************************/

#ifndef M1CP_HOST_TEST
#include "main.h"              /* hrtc */
#include "stm32h5xx_hal.h"     /* HAL_RTCEx_BKUPWrite/Read, RTC_BKP_DR*         */

/* Dedicated backup registers (not DR0/DR1, which startup_bu_registers_init uses,
 * and not the FW_CONFIG flash region, which STM32 updates rewrite). */
#define ESP_VER_REG        RTC_BKP_DR2
#define ESP_VER_MAGIC_REG  RTC_BKP_DR3
#define ESP_VER_MAGIC      0xE5C60001UL   /* marks a valid recorded version */

static void backend_persist(uint32_t packed)
{
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, ESP_VER_REG, packed);
    HAL_RTCEx_BKUPWrite(&hrtc, ESP_VER_MAGIC_REG, ESP_VER_MAGIC);
}
static bool backend_load(uint32_t *packed)
{
    HAL_PWR_EnableBkUpAccess();
    if (HAL_RTCEx_BKUPRead(&hrtc, ESP_VER_MAGIC_REG) != ESP_VER_MAGIC)
    {
        return false;   /* never written or VBAT lost -> unavailable */
    }
    *packed = HAL_RTCEx_BKUPRead(&hrtc, ESP_VER_REG);
    return true;
}
#else
/* Host test: RAM only, no persistence across "boots". */
static void backend_persist(uint32_t packed) { (void)packed; }
static bool backend_load(uint32_t *packed)   { (void)packed; return false; }
#endif

/*************************** P U B L I C ************************************/

void m1cp_esp_version_init(void)
{
    uint32_t packed;
    if (backend_load(&packed))
    {
        unpack(packed, s_version);
        s_valid = true;
    }
    else
    {
        s_version[0] = 0xFFU; s_version[1] = 0xFFU;
        s_version[2] = 0xFFU; s_version[3] = 0xFFU;
        s_valid = false;
    }
}

void m1cp_esp_version_set(const uint8_t version[4])
{
    if (version == NULL) { return; }
    s_version[0] = version[0]; s_version[1] = version[1];
    s_version[2] = version[2]; s_version[3] = version[3];
    s_valid = true;
    backend_persist(pack(s_version));
}

bool m1cp_esp_version_get(uint8_t out[4])
{
    if (!s_valid || (out == NULL)) { return false; }
    out[0] = s_version[0]; out[1] = s_version[1];
    out[2] = s_version[2]; out[3] = s_version[3];
    return true;
}
