# Testing and release qualification

## Current test evidence

The repository currently provides:

- a manufacturing I/O spreadsheet;
- 112 infrared capture/test-vector files;
- feature-specific test statements in `Changes log.txt`;
- build outputs and compiler-generated reports;
- contribution templates that request hardware and test details.

This evidence is useful but is not a unified, repeatable qualification system.

## Required test layers

| Layer | Scope | Minimum evidence |
| --- | --- | --- |
| Build | Supported toolchains and presets | Clean build log, warnings policy, artifact hashes |
| Static checks | C/C++ and configuration | Compiler warnings, formatting/lint result, targeted analysis |
| Unit | Pure parsers, codecs, CRC, utilities | Automated pass/fail results and vectors |
| Integration | Storage, buses, ESP32, USB, update | Versioned hardware setup and logs |
| Hardware-in-loop | Full device features | Board revision, firmware, peripherals, result record |
| Manufacturing | Every production I/O | Fixture version, limits, unit serial, pass/fail record |
| Compliance | RF/EMC and regional behavior | Approved configuration and report reference |
| Security | Update, malformed input, debug policy | Review record and negative tests |

## Core regression matrix

Every production candidate should cover:

- power-on, long-press behavior, shutdown, low battery, and watchdog recovery;
- display, buttons, LED, buzzer, and external GPIO;
- SD insertion/removal, filesystem errors, capacity reporting, and USB MSC;
- firmware update success, invalid CRC, invalid metadata, interruption, and supported recovery;
- NFC read/write/emulation for every advertised card family;
- LF RFID read/emulate/write for supported protocols;
- Sub-GHz capture/decode/replay at permitted regional bands;
- infrared learn/replay and representative protocol fixtures;
- ESP32 reset, Wi-Fi scan, Bluetooth operation, and post-use power state;
- USB CDC/CLI and serial bridging;
- idle and active current consumption.

## High-risk targeted tests

1. NFC SPI concurrency and lock-order stress.
2. Sub-GHz capture under sustained noise at 300 MHz.
3. SD-card removal during device I/O and USB MSC access.
4. Power loss during inactive-bank erase/program/swap.
5. Recovery with corrupt, interrupted, and region-mismatched update images.
6. Wi-Fi scan power consumption and ESP32 shutdown.
7. Backup-register and watchdog behavior after update/reset.

## Release qualification record

Record:

- candidate version and commit;
- hardware revision and region;
- bootloader and ESP32 firmware versions;
- build manifest and artifact hashes;
- test fixture/software versions;
- pass/fail/waived tests;
- linked defects and accepted risks;
- approvers and approval date.

No binary should be classified as production solely from its filename.
