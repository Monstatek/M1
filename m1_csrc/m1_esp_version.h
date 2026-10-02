/* See COPYING.txt for license details. */

/*
*
*  m1_esp_version.h
*
*  Persistent record of the installed ESP32-C6 firmware version (Gate D,
*  Option B). The STM32 is the authoritative record: after it flashes the ESP32
*  it stores the version it installed here, and reports it to the Web Manager via
*  GET_DEVICE_INFO / GET_STATUS so the app can skip the ESP flash when current.
*
*  Persistence: a dedicated RTC/TAMP backup register (DR2) + a validity magic
*  (DR3), in the VBAT/backup domain. This survives reset AND STM32 firmware
*  updates (flash programming and bl_swap_banks do not touch the backup domain,
*  and startup_bu_registers_init() only rewrites DR0/DR1) and battery-backed
*  power-off. A full VBAT loss clears it -> reported "unavailable" -> the Web
*  Manager re-flashes the ESP (safe fallback). NOT the FW_CONFIG flash region,
*  which is rewritten on every STM32 update.
*
*  Backend-abstracted: the firmware build uses the backup registers; the host
*  test build uses a RAM cache (no HAL), so reporting is exercised without
*  hardware.
*
* M1 Project
*
*/

#ifndef M1_ESP_VERSION_H_
#define M1_ESP_VERSION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load the persisted version into the RAM cache. Call once at init. */
void m1cp_esp_version_init(void);

/* Record the installed ESP32 firmware version (4 bytes: major, minor, patch,
 * rc) and persist it. Called by the ESP32 flash path after a verified flash. */
void m1cp_esp_version_set(const uint8_t version[4]);

/* Read the recorded version. Returns true and fills `out` (4 bytes) when a
 * valid version is known; false when unavailable (never flashed / VBAT lost). */
bool m1cp_esp_version_get(uint8_t out[4]);

#ifdef __cplusplus
}
#endif

#endif /* M1_ESP_VERSION_H_ */
