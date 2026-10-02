# STM32 firmware image and update

## Status

This document describes the implementation visible in `m1_fw_update.c`,
`m1_fw_update_bl.c`, `m1_fw_update_bl.h`, the linker scripts, and
`CMakeLists.txt`. Security and product-policy assertions marked **Needs
validation** require owner approval.

## Flash layout

| Region | Address/size | Purpose |
| --- | --- | --- |
| Bank 1 base | `0x08000000` | Active or inactive application bank |
| Bank size | `0x00100000` (1 MiB) | One firmware image bank |
| Firmware configuration | `0x080FFC00`–`0x080FFFFF` | Reserved 1 KiB linker section |
| Application region | `0x000FFC00` (1,023 KiB) | Linker region below the reserved configuration section |
| Transfer chunk | 1024 bytes | File/update processing chunk |
| CRC | 4 bytes | Stored immediately after `S_M1_FW_CONFIG_t` |

Bank 2 is addressed by adding `M1_FLASH_BANK_SIZE` to the Bank 1 address.

## Firmware metadata

`S_M1_FW_CONFIG_t` is placed in `.FW_CONFIG_SECTION` and contains, in order:

| Field | Type | Meaning |
| --- | --- | --- |
| `magic_number_1` | `uint32_t` | `0x4D493235` |
| `fw_version_rc` | `uint8_t` | Least-significant version component |
| `fw_version_build` | `uint8_t` | Build component |
| `fw_version_minor` | `uint8_t` | Minor component |
| `fw_version_major` | `uint8_t` | Major component |
| `user_option_1` | `uint16_t` | Reserved/user option |
| `User_option_2` | `uint16_t` | Reserved/user option |
| `ism_band_region` | `uint8_t` | Compiled Sub-GHz region |
| reserved fields | mixed | Reserved |
| `magic_number_2` | `uint32_t` | `0x534A1F41` |

The current source constants are major `0`, minor `8`, build `0`, RC `5`.
The byte order in the struct is intentionally RC, build, minor, major.

## CRC generation and validation

The build command crops the binary through `0x000FFC14` and asks SRecord to
write an STM32 little-endian CRC at `0x000FFC14`. Runtime validation uses the
STM32 CRC peripheral with the default polynomial and initial value, no input
inversion, no output inversion, and 32-bit input words.

`0x000FFC14` is correct for the current 20-byte metadata structure beginning at
`0x000FFC00`. A metadata-size change moves the CRC address and therefore requires
coordinated changes to the linker layout, CMake command, update parser, tests,
and documentation.

## Update flow

The implementation performs the following high-level sequence:

1. Select a firmware image from storage.
2. Check file access, type, size, version, region, and battery readiness.
3. Read the image in 1024-byte chunks and calculate CRC.
4. Compare the calculated CRC with the CRC stored in the image.
5. Erase and program the inactive flash bank.
6. Validate the programmed bank.
7. Set operation state in backup registers.
8. change the bank-swap option byte and reset into the new bank.
9. Report completion or failure during the next startup.

The exact error enumeration is `S_M1_M1_FW_CODES_t` in
`m1_fw_update_bl.h`.

## Recovery and rollback status

The source contains a manual alternate-bank rollback path, but it is not part
of the supported FW 0.8.0.5 release claim and has not been hardware-qualified for
this release. Automatic failed-boot rollback is not implemented. Supported
recovery for this release is the SD-card update path and service DFU.

## Safety requirements

- Keep external power or adequate battery charge throughout programming.
- Never alter option bytes outside the update/rollback implementation.
- Do not ship an image whose metadata and CRC were produced from different
  binaries.
- Qualify update, interrupted update, CRC failure, invalid metadata, full/low
  battery, and the supported recovery paths on every supported hardware revision.

## Security limitations

Repository evidence shows integrity checking through CRC. CRC is not a digital
signature and does not establish publisher authenticity.

**Needs validation:** secure boot, authenticated update, anti-rollback policy,
key provisioning, and production debug-lock status are not established by the
current documentation.

## Required release evidence

Every released firmware image should have a manifest containing source commit,
version, hardware targets, region, image length, CRC value, SHA-256, toolchain,
build preset, signing status, and qualification result.
