/* See COPYING.txt for license details. */

/*
*
* m1_esp32_flash_session.h
*
* One shared ESP32 ROM-loader session: connect, flash_start, per-block
* write, verify, teardown. This is the exact sequence the SD-card updater
* (m1_esp32_fw_update.c) has always used and that is hardware-proven to
* flash a ~1.5MB image in minutes. The M1CP streamed backend
* (m1_manager_espupdate.c) previously carried its own, independently
* evolved copy of this same logic (retry loops, baud fallbacks, extra
* ring-buffer resets) that never matched the SD path's real timing despite
* several rounds of parameter tuning (block size, then baud rate). Both
* callers now go through this single implementation of the load-bearing
* transport primitives, so a fix or a proven-good property here reaches
* both instead of having to be independently rediscovered and re-applied.
*
* What stays OUTSIDE this module, intentionally, because the two callers'
* real needs differ:
*   - EN/power-cycle before connecting: the M1CP path must exit the ESP's
*     normal esp-hosted SPI-AT mode first (a real, distinct requirement);
*     the SD menu flow's ESP is already idle (only esp32_UART_deinit() is
*     needed). Callers do whatever pre-step they need, then call
*     m1_esp32_flash_session_connect().
*   - Any caller-side retry-of-a-primitive policy: this module exposes
*     single-attempt primitives, matching exactly what the SD path's own
*     m1_fw_app()/m1_fw_flash_binary() call (also single-attempt, at the
*     esp_loader level -- esp_loader_flash_write() already retries
*     internally per SERIAL_FLASHER_WRITE_BLOCK_RETRIES). A caller that
*     wants an outer retry wraps these calls; the primitives themselves
*     never differ between callers, so a caller's retry policy can never
*     silently change what "one write" or "one connect" actually does.
*
* M1 Project
*
*/

#ifndef M1_ESP32_FLASH_SESSION_H_
#define M1_ESP32_FLASH_SESSION_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "stm32_port.h"
#include "esp_loader.h"

typedef struct
{
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *io0_port;
    uint16_t     io0_pin;
    GPIO_TypeDef *rst_port;
    uint16_t     rst_pin;
} m1_esp32_flash_session_pins_t;

/* Configure the STM32 port layer (loader_port_stm32_init), bring up the
 * ESP UART (esp32_UART_init), and drive IO0 high -- exactly
 * m1_fw_app()'s own setup sequence, byte-for-byte. Does not touch EN/reset;
 * callers that need a power-cycle do it first, exactly as before. */
void m1_esp32_flash_session_setup_io(const m1_esp32_flash_session_pins_t *pins);

/* Exactly one connect_to_target(ESP32_UART_HIGH_BAUDRATE) call -- no retry,
 * no baud fallback. This single call, at this single baud, IS the proven
 * behavior: m1_fw_app()'s own connect loop only ever evaluates it once
 * (the loop body unconditionally breaks). A caller wanting a retry wraps
 * this call in its own loop rather than this function growing one, so
 * every caller is always retrying (or not) the identical primitive. */
esp_loader_error_t m1_esp32_flash_session_connect(void);

/* esp_loader_flash_start() passthrough -- erases the target region. */
esp_loader_error_t m1_esp32_flash_session_start(uint32_t offset, uint32_t image_size, uint32_t block_size);

/* Exactly one esp_loader_flash_write() call. No outer retry beyond what
 * esp_loader itself already does internally, no extra ring-buffer reset
 * beyond what stm32_port.c's loader_port_write() already performs
 * unconditionally before every single command (both callers get that for
 * free, identically, since it lives in the shared vendor port layer, not
 * here). Matches m1_fw_flash_binary()'s own write call exactly. */
esp_loader_error_t m1_esp32_flash_session_write(const uint8_t *data, size_t size);

/* esp_loader_flash_verify() -- verifies against esp_loader's own running
 * digest of everything written this session (the SD path's own check;
 * MD5_ENABLED-gated, matching m1_fw_flash_binary() exactly). */
esp_loader_error_t m1_esp32_flash_session_verify_self(void);

/* esp_loader_flash_verify_known_md5() -- verifies against a caller-supplied
 * expected digest, 32 lowercase hex chars (the M1CP path's check: the
 * Manager already knows the source image's own MD5 independent of what
 * got streamed, so it can check that leg too, not just internal
 * write-consistency). */
esp_loader_error_t m1_esp32_flash_session_verify_known_md5(uint32_t offset, uint32_t image_size,
                                                             const char hex_md5_32[32]);

/* Restore IO0/BUTTON_RIGHT to input only -- nothing else. m1_fw_app() does
 * this itself right before returning, before its caller separately does
 * the rest of teardown (baud/reset/deinit, below) -- kept as its own call
 * so that ordering is preserved exactly. */
void m1_esp32_flash_session_restore_io0_input(GPIO_TypeDef *io0_port, uint16_t io0_pin);

/* Full teardown, matching setting_esp32_firmware_update()'s own sequence
 * exactly: restore the default ESP baud, reset the RX ring buffer, reset
 * the ESP target (boots the new image), a short delay to skip its boot
 * banner, then deinit the UART. `restore_io0_input` additionally restores
 * IO0/BUTTON_RIGHT to input first (the SD path does this inline in
 * m1_fw_app() before returning; M1CP does it as part of this same
 * teardown) -- pass true unless a caller already did it separately. */
void m1_esp32_flash_session_teardown(bool restore_io0_input,
                                      GPIO_TypeDef *io0_port, uint16_t io0_pin);

#endif /* M1_ESP32_FLASH_SESSION_H_ */
