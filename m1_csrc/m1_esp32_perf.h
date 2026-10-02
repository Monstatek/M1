/* See COPYING.txt for license details. */

/*
*
* m1_esp32_perf.h
*
* Measured, additive-only instrumentation for the ESP32 UART transport
* (Esp32_serial_flasher/src/esp_loader.c + protocol_uart.c), added to
* answer -- with real numbers from real hardware, not assumptions --
* exactly what a long ESP32 flash actually spends its time on: total
* low-level ROM commands sent (every SLIP round trip, including every
* internal esp_loader retry, which is normally opaque above that layer),
* how many of those failed specifically with a timeout versus some other
* error, the running total of the mandatory fixed per-command delay
* (protocol_uart.c's own 100ms HAL_Delay -- needed, per that file's own
* comment, to avoid losing response bytes to ISR latency), and how many
* times a connect was attempted this session.
*
* Purely additive: every call site this header's functions are called from
* already existed and already ran exactly this way; nothing here changes
* any timing, retry count, delay value, or other behavior -- it only
* counts. Reset once per M1CP ESP update attempt (m1_manager_espupdate.c's
* espu_be_begin(), alongside its existing perf-counter resets), then
* logged once at the end (that same file's existing "flash perf" M1_LOG_I
* line, extended). m1_esp32_perf_note_connect() itself is called from the
* shared m1_esp32_flash_session_connect() primitive, so it also counts the
* SD-card updater's own connect calls -- harmless, since nothing reads or
* logs these counters outside an M1CP ESP update attempt.
*
* M1 Project
*
*/

#ifndef M1_ESP32_PERF_H_
#define M1_ESP32_PERF_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void m1_esp32_perf_reset(void);

/* One send_cmd() call completed, any outcome (protocol_uart.c). */
void m1_esp32_perf_note_command(void);
/* Time spent in that same call's mandatory post-command delay. */
void m1_esp32_perf_note_fixed_delay(uint32_t ms);
/* One esp_loader_flash_write() internal attempt completed
 * (Esp32_serial_flasher/src/esp_loader.c's own retry loop, up to
 * SERIAL_FLASHER_WRITE_BLOCK_RETRIES per block). `timed_out` non-zero if
 * this specific attempt failed with ESP_LOADER_ERROR_TIMEOUT. */
void m1_esp32_perf_note_write_attempt(int timed_out);
/* One connect_to_target()/esp_loader_connect() attempt -- called from
 * m1_esp32_flash_session_connect() (m1_esp32_flash_session.c), not from
 * vendor code. */
void m1_esp32_perf_note_connect(void);

uint32_t m1_esp32_perf_commands(void);
uint32_t m1_esp32_perf_fixed_delay_ms(void);
uint32_t m1_esp32_perf_write_attempts(void);
uint32_t m1_esp32_perf_write_timeouts(void);
uint32_t m1_esp32_perf_connects(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_ESP32_PERF_H_ */
