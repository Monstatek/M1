<!-- See COPYING.txt for license details. -->

# M1 Firmware

Firmware for the M1 NFC/RFID multi-protocol device, built on STM32H5.

> **Responsible use:** Use this project only with systems and signals you own or
> are authorized to test. Radio, credential, privacy, and access-control laws
> vary by jurisdiction. See [Legal notice and responsible use](LEGAL.md) and
> [Regulatory status](docs/regulatory.md).

## Overview

The M1 firmware provides support for:

- **NFC** (13.56 MHz)
- **LF RFID** (125 kHz)
- **Sub-GHz** (315–915 MHz)
- **Infrared** (IR transmit/receive)
- **Bluetooth** (BLE scan, advertise, GATT; via ESP32 co-processor)
- **WiFi** (AP scan, station/AP modes; via ESP32 co-processor)
- **Battery** monitoring
- **Display** (ST7586s ERC240160)
- **USB** (CDC, MSC)

## Hardware

- **MCU:** STM32H573VIT6 (32-bit, 2MB Flash, 100LQFP)
- **Hardware revision:** 2.x

See [HARDWARE.md](HARDWARE.md) for more details.

## Documentation

- [Technical documentation index](docs/README.md) – System, hardware, protocol,
  testing, release, security, and known-issue documentation
- [Build and debug](docs/build-and-debug.md) – Canonical command-line build
- [Build Tool (mbt)](documentation/mbt.md) – Build with STM32CubeIDE or VS Code
- [Architecture](ARCHITECTURE.md) – Project structure
- [Development](DEVELOPMENT.md) – Development guidelines

## Building

**See [documentation/mbt.md](documentation/mbt.md) for full build instructions** (STM32CubeIDE and VS Code setup, extensions, and optional post-build CRC).

### Prerequisites

- **STM32CubeIDE 1.17+** (recommended), or  
- **VS Code** with ARM GCC 14.2, CMake Tools, Cortex-Debug, and Ninja, or
- **Linux** with ARM GCC toolchain and Ninja

### Build steps

**macOS or Linux:**
```bash
./build.sh --release
```

This requires CMake, Ninja, GNU Arm Embedded Toolchain 14.2, and `srec_cat`.
Output is written to `./artifacts/` (`.elf`, `.bin`, `.hex`, and a release
manifest). A release build requires a clean Git working tree.

**STM32CubeIDE:**  
Open the project and build in the IDE.

**VS Code:**  
1. Configure the project (e.g. `gcc-14_2_build-release` or `gcc-14_2_build-debug`)  
2. Build via the Build icon  

Output: `./out/build/gcc-14_2_build-release` (VS Code) or `./Release` (STM32CubeIDE)

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](.github/CONTRIBUTING.md) and the [Code of Conduct](.github/CODE_OF_CONDUCT.md).

## License

See [LICENSE](LICENSE) for details.
