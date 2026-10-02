/* See COPYING.txt for license details. */
#ifndef M1_ESP_UART_TRANSPORT_H_
#define M1_ESP_UART_TRANSPORT_H_
#include <stdint.h>

/* Task-context UART helpers. The caller must own the existing ESP32 session;
 * these do not acquire ownership, reset the radio, or change baud/framing.
 * Legacy return conventions are preserved: write: 1 success / 0 failure;
 * read: 0 complete / 1 timeout. Timeouts are in milliseconds. */
uint8_t esp32_uart_write(const uint8_t *data, uint16_t size, uint32_t timeout);
uint8_t esp32_uart_read(uint8_t *data, uint16_t size, uint32_t timeout);
/* Returns retained bytes, excluding NUL. Partial text may be returned on
 * timeout; a nonzero count alone does not prove the prompt was received. */
/* Returns zero unless the complete nonempty prompt fits in the reply. */
uint16_t esp32_uart_read_complete_prompt(char *buf, uint16_t buf_size,
                                       uint32_t timeout_ms, const char *prompt);
uint16_t esp32_uart_read_until_prompt(char *buf, uint16_t buf_size,
                                    uint32_t timeout_ms, const char *prompt);
#endif
