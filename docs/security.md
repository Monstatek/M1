# M1 security architecture

## Purpose

This page separates controls evidenced in the repository from security goals
that still require validation. It does not replace the vulnerability-reporting
instructions in [`.github/SECURITY.md`](../.github/SECURITY.md).

## Trust boundaries

The primary boundaries are:

- firmware image and STM32 internal flash;
- removable SD-card content;
- STM32 ↔ ESP32 communication;
- Wi-Fi and Bluetooth radio interfaces;
- NFC, LF RFID, Sub-GHz, and infrared input;
- USB CDC/MSC host connection;
- UART/SWD/JTAG-style development access;
- external GPIO;
- manufacturing and firmware-programming tools.

All external and removable-media input must be treated as untrusted.

## Implementation evidence

| Control | Evidence | Security interpretation |
| --- | --- | --- |
| Firmware CRC | STM32 CRC generation and validation | Detects corruption; does not authenticate publisher |
| Dual-bank update | Inactive-bank programming and bank swap | Supports the update flow; recovery uses the documented SD/service paths |
| Firmware metadata magic values | Two constants in image metadata | Detects some malformed images; not cryptographic |
| Version and region checks | Update result codes and metadata | Policy hooks exist; exact enforced policy needs tests |
| FreeRTOS stack/malloc hooks | Fatal hooks in `m1_tasks.c` | Fail-stop behavior; not memory isolation |
| Bus mutexes | I2C, RF SPI, logging and subsystem locks | Protects concurrency when consistently used |

## Unverified or undocumented claims

The existing security policy mentions secure boot, authenticated firmware
updates, MPU configuration, secure storage, tamper detection, and secure debug.
The documentation reviewed does not establish that these controls are enabled
in production.

Owners must classify each item as implemented, planned, not applicable, or
explicitly accepted risk.

## Required security decisions

1. Is every production STM32 image authenticated before boot and update?
2. Where are signing keys held, and how are public keys provisioned?
3. Is downgrade allowed, and how is the minimum version enforced?
4. Are SWD and other debug interfaces disabled or access-controlled in
   production?
5. What secrets or personal data can exist on the SD card or ESP32?
6. How are Wi-Fi credentials stored, cleared, and protected?
7. What is the trust model for ESP32 firmware and its update binary?
8. What parser fuzzing or malformed-input testing covers radio and storage
   formats?
9. Are MCU isolation features such as MPU/TrustZone used?
10. What is the manufacturing provisioning and recovery process?

## Minimum release security gate

- No credentials, private keys, or production secrets in source or artifacts.
- Update integrity and failure-path tests pass.
- Debug-port state is recorded for the target hardware.
- Dependency/license inventory is reviewed.
- New external parsers have bounds and malformed-input tests.
- Security-relevant changes receive an identified reviewer.
- Known exploitable issues are either fixed or explicitly accepted by an owner.
