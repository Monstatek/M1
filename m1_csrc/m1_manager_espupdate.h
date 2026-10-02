/* See COPYING.txt for license details. */

/*
*
*  m1_manager_espupdate.h
*
*  M1CP streamed ESP32-C6 firmware-update state machine (Gate D). See
*  documentation/M1CP_gateD_esp32_design.md.
*
*  Model: the Manager reads manifest.json, compares the STM32-reported installed
*  ESP32 version (m1_esp_version.{c,h}) against the manifest, and only when stale
*  fetches the ESP `.bin` + MD5 and streams the raw bytes over M1CP straight into
*  the ESP32 flash via the esp_loader (Esp32_serial_flasher). Integrity is the
*  esp_loader MD5 verify against the client-supplied MD5.
*
*  Flow:  ESP_UPDATE_BEGIN (take exclusive ESP ownership, enter download mode,
*  connect, flash_start) -> ESP_UPDATE_DATA* (flash_write chunks) ->
*  ESP_UPDATE_FINISH (MD5 verify, reset the C6, PERSIST the version via
*  m1cp_esp_version_set, restore normal ESP operation). The ESP is single-app
*  (no dual-bank swap) so there is NO separate ACTIVATE - it reboots into the new
*  image at FINISH. ESP_UPDATE_ABORT releases ownership and restores normal
*  operation. A failed/interrupted flash leaves a possibly-inoperative radio, but
*  the STM32 host is untouched and the Manager can re-flash (degrade, not brick).
*
*  The state machine is backend-abstracted (m1cp_espupdate_backend_t) so it is
*  exercised on the host without hardware. The firmware backend reuses the proven
*  SD-card ESP flash sequence (m1_esp32_fw_update.c: loader_port_stm32_init +
*  connect_to_target + esp_loader_flash_start/write + verify_known_md5).
*
*  M1CP_ESP_FLASH_IMPLEMENTED is currently DEFINED (see its own doc comment
*  below, near the #define) -- ESP32_UPDATE is presently advertised, for
*  on-device local testing of the streamed path, exactly as STM32_UPDATE is
*  currently advertised for the same reason.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_ESPUPDATE_H_
#define M1_MANAGER_ESPUPDATE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* streamed ESP32 firmware-update message types (namespace 0x30..0x3F) */
#define M1CP_CMD_ESP_GET_VERSION     0x30U   /* optional; also in device-info/status */
#define M1CP_CMD_ESP_UPDATE_BEGIN    0x31U
#define M1CP_CMD_ESP_UPDATE_DATA     0x32U
#define M1CP_CMD_ESP_UPDATE_FINISH   0x33U
#define M1CP_CMD_ESP_UPDATE_ABORT    0x34U

#define M1CP_ESP_MAX_IMAGE     0x00400000UL  /* 4 MiB (ESP32_IMAGE_SIZE_MAX)         */
#define M1CP_ESP_MAX_OFFSET    0x00100000UL  /* 1 MiB (ESP32_START_ADDRESS_MAX)      */
#define M1CP_ESP_CHUNK_MAX     480U          /* data bytes per ESP_UPDATE_DATA (x16) */
/* ESP ROM write size: one esp_loader_flash_write() (and one SLIP send_cmd()
 * round trip, including protocol_uart.c's own fixed 100ms per-command delay)
 * per this many bytes. 4096 (a real NOR flash sector) rather than 1024
 * quarters the number of ROM write round trips for the same image -- see
 * the perf investigation in m1_manager_espupdate.c's own comment above
 * espu_be_write() for the measured ~2-hour transfer this was sized against.
 * No longer matches the separate SD updater's own block size (that path is
 * independent and untouched by this constant -- confirmed by source grep). */
#define M1CP_ESP_FLASH_BLOCK    4096U
#define M1CP_ESP_MD5_LEN       16U
#define M1CP_ESP_BEGIN_CONFIRM 0xE5C3U       /* required ESP_UPDATE_BEGIN token      */

/*
 * Defining this advertises the ESP32_UPDATE capability (see m1_manager_protocol.c)
 * so the Web Manager reveals the ESP update UI, mirroring how STM32_UPDATE was
 * gated in Gate C. Enabled for on-device local testing of the streamed esp_loader
 * flash path.
 */
#define M1CP_ESP_FLASH_IMPLEMENTED 1

/*
 * Backend struct: m1cp_update_backend_t (m1_manager_update_coordinator.h),
 * shared with every other M1CP update target (including the STM32
 * self-update path, m1_manager_fwupdate.c). For this target specifically:
 * ready():  preconditions satisfied (e.g. battery >= 50%).
 * begin():  take exclusive ESP ownership, enter download mode, connect, and
 *           esp_loader_flash_start(flash_offset, image_size). Returns
 *           0 ok / 1 radio busy (-> ERR_BUSY) / 2 bootloader/connect fail (-> hard_err_code = ERR_ESP).
 * write():  stage into 4096-byte ROM blocks and esp_loader_flash_write()
 *           each full one (0 ok / non-zero -> hard_err_code = ERR_ESP).
 * finish(): flush any remaining partial block, esp_loader MD5 verify vs the
 *           client-supplied hash (mandatory: min_hash_algo ==
 *           M1CP_UPDATE_HASH_MD5), reset the C6, wait for boot, restore
 *           normal ESP operation. Returns 0 verified / 1 MD5 MISMATCH (->
 *           INTEGRITY) / 2 hard error (-> ERR_ESP) / 3 verify COMMAND failed
 *           but every block was checksum-acked (soft success, completes).
 * activate: NULL (supports_activate = false -- single-app, FINISH itself
 *           completes the update, no separate reboot step).
 * abort():  release ownership and restore normal ESP operation (idempotent).
 */
#include "m1_manager_update_coordinator.h"

void m1cp_espupdate_init(const m1cp_update_backend_t *backend);

/*
 * Handle an ESP32 command (0x30..0x3F). On success writes any response payload
 * into `out` (capacity >= 5) and sets *out_len, returning M1CP_ERR_NONE (caller
 * sends RESPONSE, or RESPONSE|ACK when *out_len == 0); otherwise a non-zero M1CP
 * error code. Task context only. ESP_GET_VERSION is always available; the flash
 * opcodes require the backend. */
uint8_t m1cp_espupdate_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                              uint8_t *out, uint16_t *out_len);

/* Last low-level flash diagnostic, as (step << 8 | esp_loader_code), so a NACK
 * can carry exactly which esp_loader step failed and why. step: 0 none, 1
 * connect, 2 flash_start, 3 write, 4 verify. code: raw esp_loader_error_t. */
uint16_t m1cp_espupdate_last_diag(void);
void     m1cp_espupdate_set_diag(uint8_t step, uint8_t code);

/* MD5 of exactly the bytes the STM32 received over M1CP and streamed to the ESP,
 * computed independently of the ESP's own flash verify. Lets an INTEGRITY
 * failure be diagnosed without a debug UART: if this matches the client's
 * expected MD5, corruption happened on the STM32<->ESP leg (or in the ESP's
 * flash verify itself); if it does NOT match, corruption happened before the
 * STM32 (the client<->STM32 leg). Valid once ESP_UPDATE_FINISH has run;
 * returns false before that (including on the host build). */
bool m1cp_espupdate_last_md5(uint8_t out[16]);

/* True while an ESP update is in progress (owns the radio). */
bool m1cp_espupdate_busy(void);

/* Abort any in-progress ESP update (session end / link-down): release ownership
 * and restore normal ESP operation. */
void m1cp_espupdate_reset(void);

/* Install the firmware ESP flash backend (esp_loader over UART4). */
#ifndef M1CP_HOST_TEST
void m1cp_espupdate_install_backend(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_ESPUPDATE_H_ */
