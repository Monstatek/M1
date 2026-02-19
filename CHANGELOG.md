<!-- See COPYING.txt for license details. -->

# Changelog

All notable changes to the M1 project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.2-ChrisUFO] - 2026-02-18

### Added

- USB DFU mode entry in **Settings -> Firmware update -> USB DFU mode** with explicit confirmation/cancel flow
- `dfu` CLI command to reboot directly into STM32 ROM USB DFU mode
- Startup operation flag `DEV_OP_STATUS_USB_DFU_REQUEST` for reliable DFU handoff across reset
- DFU transition hardening:
  - confirmation timeout path (no infinite wait)
  - USB stack deinit status checks before ROM jump
  - pre-reset user feedback screen when preparing DFU reboot
- Architecture and user docs updated for DFU flow, troubleshooting, tested matrix, and limitations

### Changed

- Firmware version build bumped to `0.8.2` (`FW_VERSION_BUILD 2`)
- `ARCHITECTURE.md` rewritten to match actual repository structure and current firmware update pathways
- CMake post-build pipeline now gracefully skips `.bin/.hex/CRC` generation when `arm-none-eabi-objcopy` is not found, instead of hard failing link completion

### Validation Notes

- Firmware compiles and links with CMake target for this branch (`MonstaTek_M1_v0802-ChrisUFO.elf`).
- When `arm-none-eabi-objcopy` is unavailable, CMake emits a warning and skips `.bin/.hex/CRC` generation instead of failing the build.

## [1.0.1-ChrisUFO] - 2026-02-17

### Added

- **Universal Remote** feature (`m1_csrc/m1_ir_universal.c/.h`):
  - Flipper Zero-compatible `.ir` file support (parsed signal format)
  - SD card IR database layout: `0:/IR/<Category>/<Brand>/<Device>.ir`
  - 3-level SD card browser (Category → Brand → Device → Commands)
  - 20+ IR protocol mappings: NEC, NECext, NEC42, RC5, RC5X, RC6, RC6A,
    Samsung32, Samsung48, SIRC, SIRC15, SIRC20, Kaseikyo, Denon, Sharp,
    JVC, Panasonic, NEC16, LGAIR
  - Scrollable command list UI on 128×64 OLED (u8g2, 5×8 font, 6 rows)
  - IR transmit via IRSND with on-screen "Sending…" feedback
  - Graceful error display when SD card is absent or directory is empty
- Firmware version bumped to build 1 (`FW_VERSION_BUILD 1`)
- Fork tag added (`FW_VERSION_FORK_TAG "ChrisUFO"`)

### Changed

- `m1_csrc/m1_infrared.c`: `infrared_universal_remotes()` now delegates to
  `ir_universal_run()` instead of being a no-op stub
- `cmake/m1_01/CMakeLists.txt`: added `m1_ir_universal.c` to build

## [1.0.0] - 2026-02-05

### Added

- Initial open source release
- STM32H573VIT6 support (2MB Flash, 100LQFP)
- Hardware revision 2.x support
