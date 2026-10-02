# Build and debug

This is the canonical entry point. Detailed commands remain in
[`documentation/mbt.md`](../documentation/mbt.md), and the graphical VS Code
walkthrough remains in
[`documentation/usingVSCode.md`](../documentation/usingVSCode.md).

## Supported build paths

| Environment | Toolchain | Build definition | Output |
| --- | --- | --- | --- |
| Linux/macOS shell | Arm GNU toolchain, CMake, Ninja | `Makefile`, CMake presets | `artifacts/` |
| VS Code | Arm GNU 14.2.Rel1 | CMake presets | `out/build/gcc-14_2_build-{debug,release}` |
| STM32CubeIDE | IDE-bundled GNU tools | Eclipse project files | `Debug/` or `Release/` |

STM32CubeIDE 1.17.0 with GNU Tools for STM32 12.3.rel1 and standalone Arm GNU
14.2.Rel1 are documented toolchains. Cross-toolchain binary identity is not
currently guaranteed.

## Command-line build

```sh
./build.sh --release
```

The script requires a clean Git tree and records the source revision and build
tools. Expected release artifacts include ELF, BIN, HEX, CRC-appended BIN, and
a text manifest under `artifacts/`.

## CRC-appended image

The build invokes `srec_cat` to crop the application image at `0x000FFC14` and
write the STM32 little-endian CRC at that address. This address is coupled to
`S_M1_FW_CONFIG_t`; see [Firmware image and update](protocols/firmware-update.md).

Do not change the metadata structure, linker section, crop address, or CRC
command independently.

## Debug probes

The repository includes VS Code configurations for ST-Link and J-Link. Verify
local executable paths before launching. The external connector sheet assigns:

| External GPIO | Function |
| --- | --- |
| 10 | SWCLK |
| 11 | SWDIO |
| 12 | UART1 TX |
| 13 | UART1 RX |

Confirm ground, target voltage, connector orientation, and hardware revision
before attaching a probe.

## Reproducibility record

Every production build should record:

- source commit SHA;
- clean/dirty workspace state;
- build preset and build type;
- compiler, linker, CMake, Ninja, and SRecord versions;
- firmware version and region constants;
- hardware target;
- artifact SHA-256 values;
- release qualification reference.

The release build emits a generated manifest alongside its artifacts.
