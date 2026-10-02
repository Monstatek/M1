# Interfaces and protocols

## Status legend

- **Specified:** a human-readable format document exists.
- **Code-defined:** implementation is the present source of truth.
- **Needs validation:** behavior or compatibility is not fully established.

## Inventory

| Interface or format | Status | Current authority | Missing specification work |
| --- | --- | --- | --- |
| M1 NFC storage file | Partially specified | `NFC/NFC_drv/common/nfc_file_form.txt`, `nfc_file.c` | Formal grammar, limits, compatibility and negative cases |
| STM32 firmware image | Code-defined | `m1_fw_update*.{c,h}`, linker scripts, CMake | See [Firmware update](firmware-update.md) |
| STM32 ↔ ESP32 hosted SPI | Code-defined | `Esp_spi_at/`, `m1_esp32_hal.*` | Framing, handshake, state machine, errors, compatibility |
| ESP ROM serial flashing | Code-defined; upstream-derived | `Esp32_serial_flasher/private_include/protocol*.h` | M1 wiring/reset flow and supported target matrix |
| LF RFID EM4100 | Code-defined | `lfrfid_protocol_em4100.*` | Bit/timing/parity/file format and test vectors |
| LF RFID H10301 | Code-defined | `lfrfid_protocol_h10301.*` | Bit/timing/parity/file format and test vectors |
| T5577 operations | Code-defined | `lfrfid/` implementation | Commands, block mapping, verification, failure modes |
| Sub-GHz Princeton | Code-defined | `Sub_Ghz/protocols/m1_princeton_decode.c` | Timing, representation, supported variants |
| Sub-GHz Security+ 2.0 | Partial/code-defined | `m1_secplus_v2_decode.c`, engineering log | Supported subset and interoperability limits |
| Sub-GHz raw capture/replay | Code-defined | `Sub_Ghz/`, storage code | On-disk schema, units, sample limits, migration |
| Infrared capture/replay | Code-defined plus fixtures | `Infrared/`, 112 `IR-Data` samples | Supported matrix and stored representation |
| USB CDC | Code-defined | `USB/Class/CDC/`, `m1_usb_cdc_msc.c` | Host behavior, baud semantics, buffering, errors |
| USB MSC | Code-defined | `USB/Class/MSC/`, storage code | Media ownership, eject/removal, corruption recovery |
| CLI over UART/USB | Code-defined | `m1_cli.c`, `cli_app.c` | Command reference, permissions, output/error contract |
| External GPIO | Spreadsheet-defined | external GPIO ODS, `m1_gpio.*` | Electrical limits and application-level behavior |

## Protocol-document template

Each protocol page should include:

1. scope and supported versions;
2. physical/link-layer parameters;
3. framing or persistent schema;
4. byte and bit ordering;
5. commands, responses, and error codes;
6. timing, retries, and timeout behavior;
7. state machine;
8. resource and length limits;
9. compatibility and migration rules;
10. security considerations;
11. normative examples and negative test vectors;
12. source files and owning team.

## Compatibility rule

Do not change a persistent format or inter-processor message incompatibly without
a version field, migration plan, compatibility test, and release note.
