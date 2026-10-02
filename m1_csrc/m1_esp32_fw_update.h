/* See COPYING.txt for license details. */

/*
*
* m1_esp32_fw_update.h
*
* M1 header for ESP32 firmware update
*
* M1 Project
*
*/

#ifndef M1_ESP32_FW_UPDATE_H_
#define M1_ESP32_FW_UPDATE_H_

#include <stdint.h>
#include "m1_ring_buffer.h"
#include "m1_display.h"

// This defines the hardware interface to use.
#define SERIAL_FLASHER_INTERFACE_UART

//If enabled, `esp-serial-flasher` is capable of verifying flash integrity after writing to flash.
#define MD5_ENABLED

//This configures the amount of retries for writing blocks either to target flash or RAM.
/* A long streamed flash (thousands of DATA_CMD round trips at 115200) can hit a
 * rare transient UART glitch; 3 internal retries occasionally isn't enough (seen
 * on-device: a block failed all 3 and aborted a ~99%-complete 1.4MB transfer).
 * Raised for headroom; the M1CP backend adds a further outer retry on top. */
#define SERIAL_FLASHER_WRITE_BLOCK_RETRIES		10

//This is the time for which the reset pin is asserted when doing a hard reset in milliseconds.
#define SERIAL_FLASHER_RESET_HOLD_TIME_MS		100

//This is the time for which the boot pin is asserted when doing a hard reset in milliseconds.
#define SERIAL_FLASHER_BOOT_HOLD_TIME_MS		50

//This inverts the output of the reset gpio pin. Useful if the hardware has inverting connection
//between the host and the target reset pin. Implemented only for UART interface.
#define SERIAL_FLASHER_RESET_INVERT				0

//This inverts the output of the boot (IO0) gpio pin. Useful if the hardware has inverting connection
//between the host and the target boot pin. Implemented only for UART interface.
#define SERIAL_FLASHER_BOOT_INVERT				0

#define SERIAL_FLASHER_DEBUG_TRACE				0

void esp32_flasher_main(void);

void setting_esp32_init(void);
void setting_esp32_exit(void);
void setting_esp32_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void setting_esp32_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);
void setting_esp32_image_file(void);
void setting_esp32_start_address(void);
void setting_esp32_firmware_update(void);

#endif /* M1_ESP32_FW_UPDATE_H_ */
