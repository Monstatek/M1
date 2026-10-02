/* Copyright 2020-2023 Espressif Systems (Shanghai) CO LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/param.h>
#include <stdio.h>
#include "stm32_port.h"

static UART_HandleTypeDef *uart;
static GPIO_TypeDef *gpio_port_io0, *gpio_port_rst;
static uint16_t gpio_num_io0, gpio_num_rst;

#if SERIAL_FLASHER_DEBUG_TRACE
static void transfer_debug_print(const uint8_t *data, uint16_t size, bool write)
{
    static bool write_prev = false;

    if (write_prev != write) {
        write_prev = write;
        printf("\n--- %s ---\n", write ? "WRITE" : "READ");
    }

    for (uint32_t i = 0; i < size; i++) {
        printf("%02x ", data[i]);
    }
}
#endif

static uint32_t s_time_end;

esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout)
{
#ifdef M1_RING_BUFFER_H_
    /* This protocol is strictly synchronous request/response (the ROM never
     * sends unsolicited bytes) -- see m1_manager_espupdate.c's espu_be_write()
     * for the full reasoning, which already applies this same defensive
     * drain once per outer (3x) retry. Every loader_port_write() call begins
     * a brand-new command (SLIP_send_delimiter() + this write is always the
     * first thing send_cmd() does), so NOTHING should legitimately be
     * sitting in the RX ring buffer at this point either -- extending the
     * drain to fire here, before every single command (not just every outer
     * retry), catches the same stale-byte/misframing failure mode at the
     * finest granularity actually available without modifying vendored
     * esp_loader.c's own internal retry loop. */
    m1_ringbuffer_reset(&esp32_rb_hdl);
#endif
    HAL_StatusTypeDef err = HAL_UART_Transmit(uart, (uint8_t *)data, size, timeout);

    if (err == HAL_OK) {
#if SERIAL_FLASHER_DEBUG_TRACE
        transfer_debug_print(data, size, true);
#endif
        return ESP_LOADER_SUCCESS;
    } else if (err == HAL_TIMEOUT) {
        return ESP_LOADER_ERROR_TIMEOUT;
    } else {
        return ESP_LOADER_ERROR_FAIL;
    }
}


esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout)
{
#ifndef M1_RING_BUFFER_H_
    HAL_StatusTypeDef err = HAL_UART_Receive(uart, data, size, timeout);

    if (err == HAL_OK) {
#if SERIAL_FLASHER_DEBUG_TRACE
        transfer_debug_print(data, size, false);
#endif
        return ESP_LOADER_SUCCESS;
    } else if (err == HAL_TIMEOUT) {
        return ESP_LOADER_ERROR_TIMEOUT;
    } else {
        return ESP_LOADER_ERROR_FAIL;
    }
#else
	/* m1_ringbuffer_read() is a PARTIAL read - it returns whatever is available
	 * right now, up to `size`, but the original loop here returned as soon as it
	 * got ANY nonzero amount, even less than `size`. esp_loader/slip.c currently
	 * only ever calls loader_port_read() with size=1 (byte-at-a-time SLIP framing),
	 * so that bug happened not to bite in practice - but the function's contract is
	 * "read exactly `size` bytes", and a future/other caller requesting size>1
	 * would silently get a truncated buffer. Accumulate across ring-buffer reads
	 * until `size` bytes are collected or the timeout elapses, so the function
	 * actually honors its contract. */
	size_t to = HAL_GetTick();
	uint16_t got = 0;
	while ( got < size )
	{
		uint16_t n = m1_ringbuffer_read(& esp32_rb_hdl, &data[got], (uint16_t)(size - got));
		got = (uint16_t)(got + n);
		if ( got >= size )
			break;
		if ( (HAL_GetTick() - to) > timeout )
			break;
		if ( !n )
			HAL_Delay(1); // no data yet; yield briefly before polling again
	} // while ( got < size )
	if ( got >= size )
	{
#if SERIAL_FLASHER_DEBUG_TRACE
        transfer_debug_print(data, size, false);
#endif
        return ESP_LOADER_SUCCESS;
	}
	else
	{
		return ESP_LOADER_ERROR_TIMEOUT;
	}
#endif // #ifndef M1_RING_BUFFER_H_
}

void loader_port_stm32_init(loader_stm32_config_t *config)

{
    uart = config->huart;
    gpio_port_io0 = config->port_io0;
    gpio_port_rst = config->port_rst;
    gpio_num_io0 = config->pin_num_io0;
    gpio_num_rst = config->pin_num_rst;
}

// Set GPIO0 LOW, then
// assert reset pin for 100 milliseconds.
void loader_port_enter_bootloader(void)
{
    HAL_GPIO_WritePin(gpio_port_io0, gpio_num_io0, SERIAL_FLASHER_BOOT_INVERT ? GPIO_PIN_SET : GPIO_PIN_RESET);
    loader_port_reset_target();
    HAL_Delay(SERIAL_FLASHER_BOOT_HOLD_TIME_MS);
    HAL_GPIO_WritePin(gpio_port_io0, gpio_num_io0, SERIAL_FLASHER_BOOT_INVERT ? GPIO_PIN_RESET : GPIO_PIN_SET);
}


void loader_port_reset_target(void)
{
    HAL_GPIO_WritePin(gpio_port_rst, gpio_num_rst, SERIAL_FLASHER_RESET_INVERT ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
    HAL_GPIO_WritePin(gpio_port_rst, gpio_num_rst, SERIAL_FLASHER_RESET_INVERT ? GPIO_PIN_RESET : GPIO_PIN_SET);
}


void loader_port_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}


void loader_port_start_timer(uint32_t ms)
{
    s_time_end = HAL_GetTick() + ms;
}


uint32_t loader_port_remaining_time(void)
{
    int32_t remaining = s_time_end - HAL_GetTick();
    return (remaining > 0) ? (uint32_t)remaining : 0;
}


void loader_port_debug_print(const char *str)
{
    printf("DEBUG: %s\n", str);
}

esp_loader_error_t loader_port_change_transmission_rate(uint32_t baudrate)
{
    uart->Init.BaudRate = baudrate;

    if ( HAL_UART_Init(uart) != HAL_OK ) {
        return ESP_LOADER_ERROR_FAIL;
    }

    return ESP_LOADER_SUCCESS;
}
