# Linux/amd64 Container Build Baseline

## Baseline status

**Result:** PASS

The M1 firmware was successfully built twice from the same committed source
using the finalized Linux/amd64 container build definition. Both runs produced
the same Docker image identity, firmware artifacts, artifact manifest, and
build metadata.

This container is the canonical reproducible-build path established by WP1.

## Repository identity

| Field | Value |
|---|---|
| Repository | `Zartharas/Monstatek_M1` |
| Upstream repository | `Monstatek/M1` |
| Baseline commit | `8c5e4f2401e37593281ffde490f657ba05b061a2` |
| Working branch | `wp1/reproducible-build-baseline` |
| Build definition commit | `8c5e4f2401e37593281ffde490f657ba05b061a2` |
| Source date epoch | `1785879997` |
| Working tree before validation | Clean |
| Working tree after validation | Clean |

## Canonical build entry point

    scripts/reproducible/container-build.sh

The wrapper:

1. Resolves the current committed source identity.
2. Exports committed source through `git archive`.
3. Builds the Linux/amd64 toolchain image.
4. Disables BuildKit provenance generation for stable image identity.
5. Runs tool-version and firmware-build containers without network access.
6. Supplies `SOURCE_DATE_EPOCH` from the source commit timestamp.
7. Collects release artifacts, metadata, and a SHA-256 manifest.

Generated firmware artifacts remain ignored and are not committed.

## Container identity

| Field | Value |
|---|---|
| Platform | `linux/amd64` |
| Operating system | Linux |
| Architecture | `amd64` |
| Image tag | `monstatek-m1-build:arm-gnu-14.2-rel1-amd64` |
| Image ID | `sha256:127f9407e8f8bb9a2c5f811597a51dbfe9eeab558b82bd030ff1f3c086ac4bfa` |
| Base image digest | `sha256:63a496b5d3b99214b39f5ed70eb71a61e590a77979c79cbee4faf991f8c0783e` |
| Dockerfile SHA-256 | `03d68ac19b9cbd4086c85348b7ff4389394d803c4ebd48597191c6c31cdb4f4e` |
| Build-wrapper SHA-256 | `8a8e4c1af11a40e628d9f28d1dd3e922acdea4869594e08a2de2832f599217f6` |
| Build type | Release |

The image architecture and OCI revision label were validated after the build.
The revision label resolved to the complete baseline commit.

## Toolchain and build tools

| Component | Value |
|---|---|
| Arm GNU distribution | Arm GNU Toolchain 14.2.Rel1 |
| GCC | `14.2.1 20241119` |
| Binutils | `2.43.1.20241119` |
| Arm toolchain archive SHA-256 | `62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823` |
| Target | `arm-none-eabi` |
| CMake | `3.25.1` |
| Ninja | `1.11.1` |
| SRecord | `1.64.D001` |

The official Arm GNU archive is checksum-pinned before extraction.

## Build result

| Check | Result |
|---|---|
| Committed-source export | PASS |
| Toolchain image build | PASS |
| Tool-version validation | PASS |
| CMake configuration | PASS |
| Ninja compilation | PASS |
| Completed Ninja actions | 319 |
| Firmware link | PASS |
| Artifact generation | PASS |
| Artifact collection | PASS |
| SHA-256 manifest verification | PASS |
| ELF format validation | PASS |
| Exact-commit repeatability | PASS |

## ELF identity

| Field | Value |
|---|---|
| Format | `elf32-littlearm` |
| Architecture | `armv8-m.main` |
| Entry address | `0x08002545` |
| Flags | `EXEC_P`, `HAS_SYMS`, `D_PAGED` |

## Memory report

| Section | Bytes |
|---|---:|
| Text | 470,536 |
| Data | 7,032 |
| BSS | 169,872 |
| Total reported by `arm-none-eabi-size` | 647,440 |

Build-system utilization report:

| Region | Used |
|---|---:|
| RAM | 176,888 bytes |
| Flash | 477,556 bytes |

## Artifact manifest

| Artifact | Size in bytes | SHA-256 |
|---|---:|---|
| `MonstaTek_M1_v0800.bin` | 1,047,572 | `433790ee249a574a58d60281fc5da09e72debb05bd478283bd912e1a3434d176` |
| `MonstaTek_M1_v0800_wCRC.bin` | 1,047,576 | `7101bb1511dd63fcc54298f0befdc991a84f25bce53208a6c3bf8ebe7ac45202` |
| `MonstaTek_M1_v0800.elf` | 669,560 | `caa8c8b1882d49122fc5d4340c38b269a0fc230ef6500cb15f7f9838a0ccc64a` |
| `MonstaTek_M1_v0800.hex` | 1,343,412 | `22706a7ba1373a348d67fc1651bd76220c86ff4860923e3717799eed87aa57e4` |

The CRC-bearing binary is exactly four bytes larger than the non-CRC binary.

## Exact-commit repeatability result

Two clean firmware builds were performed from commit
`8c5e4f2401e37593281ffde490f657ba05b061a2`.

| Comparison | Result |
|---|---|
| Docker image ID | MATCH |
| Non-CRC binary | MATCH |
| CRC-bearing binary | MATCH |
| ELF | MATCH |
| Intel HEX | MATCH |
| `SHA256SUMS` | MATCH |
| `build-metadata.txt` | MATCH |
| Firmware warning count | MATCH |

Both runs used image ID:

    sha256:127f9407e8f8bb9a2c5f811597a51dbfe9eeab558b82bd030ff1f3c086ac4bfa

Each firmware build emitted 1,076 compiler warnings. Package-manager warnings
from image construction were excluded from the firmware-warning count.

## Native-versus-container comparison

The native macOS and Linux/amd64 container builds are both valid ARM firmware
builds, but they are not byte-identical.

| Measurement | Native macOS | Container | Delta |
|---|---:|---:|---:|
| Text | 470,796 | 470,536 | -260 |
| Data | 7,032 | 7,032 | 0 |
| BSS | 169,872 | 169,872 | 0 |
| Total | 647,700 | 647,440 | -260 |
| Flash utilization | 477,812 | 477,556 | -256 |
| RAM utilization | 176,888 | 176,888 | 0 |

All four native artifact hashes differ from their container equivalents.

The comparison audit also observed:

- a 12-byte increase in the container `.text` section
- a 272-byte decrease in the container `.rodata` section
- 430 symbols with changed addresses
- no symbols present exclusively in only one build
- substantial binary content differences
- embedded source and toolchain build paths

These observations establish non-equivalence at the byte level. They do not,
by themselves, establish a functional defect.

## Embedded build paths

Release artifacts contain build-path strings originating from active
`__FILE__` use and toolchain-library content.

The native build included host-specific paths containing the local workspace
and username. The canonical container build substitutes the stable
`/workspace` source root, reducing host-specific disclosure but not eliminating
embedded build paths.

Path-prefix normalization requires a separate source and compiler-flag change
with regression testing and is outside the WP1 implementation scope.

## Comparator correction

An early local helper reported zero differing sections despite confirmed
artifact and section differences. That result was a parser false negative and
was discarded.

The final conclusion uses direct artifact hashes, byte comparisons, section
sizes, symbol-address comparisons, and repeat builds instead.

## Reproducibility claim

The validated claim is:

> Repeated clean firmware builds from commit
> `8c5e4f2401e37593281ffde490f657ba05b061a2`, using the committed
> Linux/amd64 container definition, produce byte-identical release artifacts,
> an identical artifact manifest, identical build metadata, and a stable image
> identity within the captured dependency environment.

The result does not establish byte identity with the native macOS build.

## Dependency limitation

The Debian base image digest and Arm GNU archive checksum are pinned.

The Debian packages installed with `apt-get`, however, are not resolved through
a dated repository snapshot and are not individually version-pinned. A future
cold image build may therefore obtain different Debian package revisions even
when the base image digest and Dockerfile remain unchanged.

Accordingly, future cold-build hermeticity is not yet established.

## Scope limitations

This baseline establishes container-build success, artifact identity, and
same-commit repeatability only.

It does not establish:

- runtime correctness
- hardware functionality
- native-versus-container behavioral equivalence
- radio compliance
- firmware-update safety
- resistance to malformed inputs
- absence of memory-safety defects
- security-vulnerability status
- fully hermetic future cold builds

## Public evidence

The sanitized machine-readable evidence is recorded in:

    ../evidence/container-linux-amd64-gcc-14.2.txt

Raw compiler and Docker logs remain outside the repository because they contain
local paths and host-specific diagnostic data.
