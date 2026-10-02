/* See COPYING.txt for license details. */

/*
*
* m1_esp32_flash_session.c
*
* See m1_esp32_flash_session.h for the design rationale. Every function
* here is a direct extraction of m1_esp32_fw_update.c's own m1_fw_app()/
* m1_fw_flash_binary() sequence -- same calls, same order, same
* parameters -- so it carries over the SD-card updater's hardware-proven
* behavior unchanged rather than reimplementing it.
*
* M1 Project
*
*/

#include "m1_esp32_flash_session.h"
#include "m1_esp32_hal.h"   /* esp32_UART_init/deinit/change_baudrate, ESP32_UART_BAUDRATE */
#include "app_common.h"     /* connect_to_target */
#include "m1_esp32_perf.h"  /* measured (not assumed) connect-attempt counter -- see that header */

void m1_esp32_flash_session_setup_io(const m1_esp32_flash_session_pins_t *pins)
{
    loader_stm32_config_t config = {
        .huart       = pins->huart,
        .port_io0    = pins->io0_port,
        .pin_num_io0 = pins->io0_pin,
        .port_rst    = pins->rst_port,
        .pin_num_rst = pins->rst_pin,
    };
    GPIO_InitTypeDef gpio_init = {0};

    loader_port_stm32_init(&config);
    esp32_UART_init();

    /* BUTTON_RIGHT doubles as the ESP32 boot-mode (IO0) pin; drive it as an
     * output for the duration of the session, exactly as m1_fw_app(). */
    gpio_init.Pin   = pins->io0_pin;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pins->io0_port, &gpio_init);
    HAL_GPIO_WritePin(pins->io0_port, pins->io0_pin, GPIO_PIN_SET);
}

esp_loader_error_t m1_esp32_flash_session_connect(void)
{
    m1_esp32_perf_note_connect();   /* counted for both callers (SD updater and M1CP); only M1CP currently logs it */
    return connect_to_target(ESP32_UART_HIGH_BAUDRATE);
}

esp_loader_error_t m1_esp32_flash_session_start(uint32_t offset, uint32_t image_size, uint32_t block_size)
{
    return esp_loader_flash_start(offset, image_size, block_size);
}

esp_loader_error_t m1_esp32_flash_session_write(const uint8_t *data, size_t size)
{
    return esp_loader_flash_write((void *)data, (uint32_t)size);
}

esp_loader_error_t m1_esp32_flash_session_verify_self(void)
{
#ifdef MD5_ENABLED
    return esp_loader_flash_verify();
#else
    return ESP_LOADER_SUCCESS;
#endif
}

esp_loader_error_t m1_esp32_flash_session_verify_known_md5(uint32_t offset, uint32_t image_size,
                                                             const char hex_md5_32[32])
{
    return esp_loader_flash_verify_known_md5(offset, image_size, (const uint8_t *)hex_md5_32);
}

void m1_esp32_flash_session_restore_io0_input(GPIO_TypeDef *io0_port, uint16_t io0_pin)
{
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin   = io0_pin;
    gpio_init.Mode  = GPIO_MODE_INPUT;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(io0_port, &gpio_init);
}

void m1_esp32_flash_session_teardown(bool restore_io0_input,
                                      GPIO_TypeDef *io0_port, uint16_t io0_pin)
{
    if (restore_io0_input)
    {
        m1_esp32_flash_session_restore_io0_input(io0_port, io0_pin);
    }

    /* setting_esp32_firmware_update()'s own post-update sequence, exactly:
     * default baud, drop any stale RX bytes, reset the target (boots the
     * new image), a short delay to skip its boot banner, then release the
     * UART. */
    esp32_UART_change_baudrate(ESP32_UART_BAUDRATE);
    m1_ringbuffer_reset(&esp32_rb_hdl);
    esp_loader_reset_target();
    HAL_Delay(100);
    esp32_UART_deinit();
}
