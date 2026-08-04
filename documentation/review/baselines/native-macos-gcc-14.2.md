# Native macOS Build Baseline

## Baseline status

**Result:** PASS

The unmodified M1 firmware was successfully configured, compiled, linked,
converted into release artifacts, and validated on macOS.

This baseline records the repository state before adding the reproducible
container build.

## Repository identity

| Field | Value |
|---|---|
| Repository | `Zartharas/Monstatek_M1` |
| Upstream repository | `Monstatek/M1` |
| Baseline commit | `4c77bb86b57ebb2120a8b3d94f76ccf37f3b1349` |
| Working branch | `wp1/reproducible-build-baseline` |
| Upstream parity | PASS |
| Working tree before build | Clean |
| Working tree after build | Clean |

At baseline capture, `HEAD`, `origin/main`, and `upstream/main` resolved to the
same commit.

## Host environment

| Component | Value |
|---|---|
| Operating system | macOS |
| Host architecture | `x86_64` |
| CMake | `4.4.2` |
| Ninja | `1.13.2` |
| SRecord | `1.65.0` |

## Arm toolchain

| Component | Value |
|---|---|
| Distribution | Arm GNU Toolchain 14.2.Rel1 |
| GCC | `14.2.1 20241119` |
| Binutils | `2.43.1.20241119` |
| Target | `arm-none-eabi` |

CMake recorded these compiler paths:

    /Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-gcc
    /Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-g++

The C compiler, C++ compiler, and assembler all came from the same Arm GNU
14.2.Rel1 installation.

## Build result

| Check | Result |
|---|---|
| CMake configuration | PASS |
| Ninja compilation | PASS |
| Completed Ninja actions | 319 |
| Firmware link | PASS |
| Artifact generation | PASS |
| ARM ELF validation | PASS |
| Fatal build-pattern scan | PASS |

No compiler error, linker failure, undefined reference, or Ninja build-stop
pattern was found.

## ELF identity

| Field | Value |
|---|---|
| Format | `elf32-littlearm` |
| Architecture | `armv8-m.main` |
| ABI | ARM EABI5 |
| Link type | Statically linked |
| Stripping | Not stripped |

## Memory report

| Section | Bytes |
|---|---:|
| Text | 470,796 |
| Data | 7,032 |
| BSS | 169,872 |
| Total reported by `arm-none-eabi-size` | 647,700 |

Build-system utilization report:

| Region | Used | Capacity | Utilization |
|---|---:|---:|---:|
| RAM | 176,888 bytes | 640 KiB | 26.99% |
| Flash | 477,812 bytes | 1,023 KiB | 45.61% |
| Firmware configuration reserve | 20 bytes | 1 KiB | 1.95% |

## Artifact manifest

| Artifact | Size in bytes | SHA-256 |
|---|---:|---|
| `MonstaTek_M1_v0800.bin` | 1,047,572 | `af039084a824140c920f5ce67a4c4043871360ab3494111879246fd15ee8ab0b` |
| `MonstaTek_M1_v0800_wCRC.bin` | 1,047,576 | `a51884e8965e441911277192004c59eac69c8a80bc6de3c28f31c08d66f1d65b` |
| `MonstaTek_M1_v0800.elf` | 669,592 | `6642247f89d2f4d3d0ef7f7e979e4ff46b00a94d908696a90d2d312c6548ac92` |
| `MonstaTek_M1_v0800.hex` | 1,344,140 | `0b25e97c4e2bff0d5c331e8a2b37dabfef2ff231a9a75fa28cf438bae5d0fd73` |

The CRC-bearing binary is exactly four bytes larger than the non-CRC binary.

## Compiler-warning baseline

The successful release build emitted **1,076 warnings**.

| Count | Warning category |
|---:|---|
| 544 | `-Woverlength-strings` |
| 169 | `-Wpointer-sign` |
| 119 | `-Wunused-parameter` |
| 41 | `-Wmissing-braces` |
| 27 | `-Wunused-variable` |
| 24 | `-Wsign-compare` |
| 24 | `-Wmaybe-uninitialized` |
| 18 | `-Wunused-but-set-variable` |
| 18 | `-Wdiscarded-qualifiers` |
| 16 | `-Wunused-function` |
| 13 | `-Wformat` |
| 8 | `-Wcast-function-type` |
| 5 | `-Wenum-int-mismatch` |
| 3 | `-Wformat-overflow` |
| 2 | `-Wstringop-truncation` |
| 1 | `-Wstringop-overflow` |
| 1 | `-Wreturn-type` |
| 1 | `-Woverflow` |

These warnings are baseline observations. They have not yet been individually
validated as defects or security vulnerabilities.

Generated font data accounts for much of the overlength-string warning volume.
Potentially safety-relevant categories require separate controlled review.

## Docker preflight

| Component | Value |
|---|---|
| Docker client | `29.6.2` |
| Docker server | `29.6.2` |
| Docker Buildx | `0.35.0-desktop.2` |
| Docker architecture | `x86_64` |
| Docker CPUs | 16 |
| Preflight status | PASS |

## Scope limitations

This baseline establishes build success and artifact identity only.

It does not establish:

- runtime correctness
- hardware functionality
- radio compliance
- firmware-update safety
- resistance to malformed inputs
- absence of memory-safety defects
- reproducibility across operating systems
- security-vulnerability status

## Next comparison

The container build must:

1. Use Arm GNU Toolchain 14.2.Rel1.
2. Build the same repository commit.
3. Preserve firmware behavior.
4. Capture toolchain and build metadata.
5. Generate an artifact SHA-256 manifest.
6. Compare section sizes and artifact hashes with this baseline.
7. Explain any native-versus-container differences.
