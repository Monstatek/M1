/* See COPYING.txt for license details. */
#include <string.h>
#include "main.h"
#include "m1_esp32_hal.h"
#include "m1_esp_uart_transport.h"

uint8_t esp32_uart_write(const uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart_esp, (uint8_t *)data, size, timeout);

    if (ret == HAL_OK)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

uint8_t esp32_uart_read(uint8_t *data, uint16_t size, uint32_t timeout)
{
    TickType_t xStartTick = xTaskGetTickCount();
    TickType_t xTimeoutTicks = pdMS_TO_TICKS(timeout);
    uint16_t read_n = 0;

    while (read_n < size)
    {
        uint16_t current_read = m1_ringbuffer_read(&esp32_rb_hdl, &data[read_n], size - read_n);
        read_n += current_read;

        if (read_n >= size)
        {
            return FALSE;
        }

        if ((xTaskGetTickCount() - xStartTick) >= xTimeoutTicks)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return (read_n >= size) ? FALSE : TRUE;
}

uint16_t esp32_uart_read_until_prompt(char *buf, uint16_t buf_size, uint32_t timeout_ms, const char *prompt)
{
    TickType_t start_tick;
    TickType_t timeout_tick;
    uint16_t read_len = 0;
    uint8_t ch;

    if (!buf || (buf_size == 0) || !prompt)
    {
        return 0;
    }

    start_tick = xTaskGetTickCount();
    timeout_tick = pdMS_TO_TICKS(timeout_ms);
    buf[0] = '\0';

    while ((xTaskGetTickCount() - start_tick) < timeout_tick)
    {
        if (m1_ringbuffer_read(&esp32_rb_hdl, &ch, 1) == 1)
        {
            if (read_len < (uint16_t)(buf_size - 1))
            {
                buf[read_len++] = (char)ch;
                buf[read_len] = '\0';
            }

            if (strstr(buf, prompt) != NULL)
            {
                break;
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return read_len;
}

/* Command callers must not mistake a partial reply for an acknowledgement.
 * Keep the partial-read API above for streaming/drain callers. */
uint16_t esp32_uart_read_complete_prompt(char *buf, uint16_t buf_size,
                                        uint32_t timeout_ms, const char *prompt)
{
    uint16_t n = esp32_uart_read_until_prompt(buf, buf_size, timeout_ms, prompt);
    return n && prompt && prompt[0] && strstr(buf, prompt) ? n : 0;
}
