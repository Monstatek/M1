<!-- See COPYING.txt for license details. -->

# M1 Project Architecture

## Overview

The firmware is an STM32H573 + FreeRTOS application with menu-driven features for RF, IR, storage, and maintenance.

| Directory | Description |
|-----------|-------------|
| `Core/` | MCU startup, interrupt vectors, FreeRTOS integration, CLI transport task |
| `m1_csrc/` | Application layer: menu/UI flow, feature modules, system state, firmware update paths |
| `Battery/` | Battery service and power telemetry |
| `NFC/` | NFC stack and device-specific integration |
| `lfrfid/` | LF RFID logic and protocol handling |
| `Sub_Ghz/` | Sub-GHz radio APIs, protocol logic, and configs |
| `Infrared/` | IR send/receive and IRMP/IRSND integration |
| `Esp_spi_at/` | ESP32 SPI-AT transport and hosted interface examples |
| `Esp32_serial_flasher/` | ESP32-C6 serial flashing support used by ESP32 update flow |
| `USB/` | USB device stack (CDC/MSC composite support) |
| `Drivers/` | STM32 HAL/CMSIS, BSP drivers, u8g2, and hardware support code |
| `FatFs/` | SD-card filesystem stack |
| `Middlewares/` | FreeRTOS and middleware dependencies |
| `cmake/` | CMake target composition (`cmake/m1_01/CMakeLists.txt`) |
| `tools/` | Build/release helper scripts (e.g., CRC append) |
| `scripts/` | Repository maintenance scripts |
| `documentation/` | Developer and build documentation |
| `distribution/` | Packaged build artifacts for release/flash |

## Runtime Architecture

- **RTOS model:** FreeRTOS tasks + queues.
- **Global device state:** `m1_device_stat` in `m1_csrc/m1_system.c`.
- **Primary queues:**
  - `main_q_hdl` for system/menu events (`m1_csrc/m1_tasks.c`)
  - `button_events_q_hdl` for keypad events (`m1_csrc/m1_system.c`)
- **Task bootstrap path:**
  - `Core/Src/app_freertos.c` -> `m1_system_init()`
  - `m1_csrc/m1_sys_init.c` performs peripheral init, task init, and startup status handling.

## Menu and Feature Structure

Main menu graph is defined in `m1_csrc/m1_menu.c`.

Current Settings submenu:

| Menu Item | Status | Notes |
|-----------|--------|-------|
| Storage | Enabled | SD card about/explore/mount/unmount/format |
| Power | Enabled | Battery info, reboot, power off |
| Firmware update | Enabled | SD-card image update + USB DFU mode entry |
| ESP32 update | Enabled | ESP32 serial flashing workflow |
| About | Enabled | Firmware and device info |
| LCD & Notifications | Disabled in menu | Function exists, not linked in active menu |
| System | Disabled in menu | Function exists, not linked in active menu |

## Firmware Update Paths

### 1) SD-card firmware update

- Located in `m1_csrc/m1_fw_update.c` + `m1_csrc/m1_fw_update_bl.c`.
- Flow: select `.bin` from storage -> validate image + CRC + metadata -> flash/swap bank.
- Startup operation-state persistence uses backup registers (`startup_config_write()` / `startup_config_handler()`).

### 2) USB DFU mode (ROM bootloader)

- Entry points:
  - Settings -> Firmware update -> USB DFU mode (`m1_csrc/m1_menu.c`)
  - CLI `dfu` command (`Core/Src/cli_app.c`, `m1_csrc/m1_cli.c`)
- Control flow:
  - Request flag persisted in backup registers (`DEV_OP_STATUS_USB_DFU_REQUEST`).
  - Device resets.
  - Startup handler detects request and jumps to STM32 system-memory bootloader.
- System memory bootloader jump implementation: `firmware_update_enter_usb_dfu()` in `m1_csrc/m1_fw_update.c`.

## USB Architecture

- Application-mode USB stack is in `m1_csrc/m1_usb_cdc_msc.c` and `USB/`.
- Build-time mode selection supports CDC, MSC, or CDC+MSC composite.
- USB DFU transport is provided by STM32 ROM bootloader (not by application USB class runtime).

## Build System

- **STM32CubeIDE:** `.project`, `.cproject`, and `.ioc` driven workflow.
- **CMake:** top-level `CMakeLists.txt`, `CMakePresets.json`, and `cmake/m1_01/CMakeLists.txt`.
- **Helper pipeline:** `build` script + `tools/append_crc32.py` for release binary preparation.

## Versioning

Firmware version is defined in `m1_csrc/m1_fw_update_bl.h`:

```c
#define FW_VERSION_MAJOR    0
#define FW_VERSION_MINOR    8
#define FW_VERSION_BUILD    2
#define FW_VERSION_RC       0
#define FW_VERSION_FORK_TAG "ChrisUFO"
```

Output naming format: `M1_v{MAJOR}.{MINOR}.{BUILD}-{FORK_TAG}.bin`
