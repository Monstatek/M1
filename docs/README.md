# M1 technical documentation

This directory is the entry point for M1 firmware, hardware, interface, test,
release, and operational documentation.

> **Documentation status:** These pages were reconciled for the FW 0.8.0.5
> integration. Items marked **Needs validation** are not
> established product requirements and must be confirmed by the responsible
> firmware, hardware, security, or manufacturing owner.

## Start here

| Reader | Recommended path |
| --- | --- |
| New firmware developer | [Build and debug](build-and-debug.md) → [Architecture](architecture.md) → [Known issues](known-issues.md) |
| Hardware or test engineer | [Hardware](hardware.md) → [Manufacturing and test](testing.md) |
| Integration developer | [Interfaces and protocols](protocols/README.md) → [Firmware image and update](protocols/firmware-update.md) |
| Release owner | [Release policy](releases.md) → [Testing](testing.md) → [Known issues](known-issues.md) |
| Security reviewer | [Security architecture](security.md) → [Firmware update](protocols/firmware-update.md) |
| Legal or compliance reviewer | [Legal notice](../LEGAL.md) → [Responsible use](responsible-use.md) → [Regulatory status](regulatory.md) |

## Documentation map

### Product and system

- [Architecture](architecture.md) — subsystem boundaries, runtime model, tasks,
  shared resources, and startup flow.
- [Hardware](hardware.md) — MCU, revisions, interfaces, source-of-truth files,
  and known hardware concerns.
- [Interfaces and protocols](protocols/README.md) — protocol inventory and the
  authoritative implementation for each interface.
- [Firmware image and update](protocols/firmware-update.md) — flash layout,
  metadata, CRC, bank swapping, update, and rollback.

### Engineering operations

- [Build and debug](build-and-debug.md) — canonical entry point for toolchains,
  builds, programming, and debugging.
- [Testing and release qualification](testing.md) — current evidence, required
  test layers, and release gates.
- [Release and version policy](releases.md) — version authority, artifact
  provenance, regions, and release manifest requirements.
- [Known issues and engineering validation](known-issues.md) — unresolved risks
  recovered from the engineering log and source comments.
- [Security architecture](security.md) — implemented evidence, unverified
  claims, trust boundaries, and required security decisions.
- [Legal notice and responsible use](../LEGAL.md) — repository-level lawful-use,
  modification, privacy, warranty, and third-party notice.
- [Responsible-use guide](responsible-use.md) — practical guidance by feature.
- [Regulatory status](regulatory.md) — radio-region controls, required product
  records, and certification claims that remain unverified.
- [Safety notice](safety.md) — battery, GPIO, radio, infrared, and critical-use
  precautions requiring product-owner validation.

## Existing source documents

These files remain useful and are not replaced silently:

| Existing file | Role going forward |
| --- | --- |
| [`../README.md`](../README.md) | Repository landing page |
| [`../documentation/mbt.md`](../documentation/mbt.md) | Detailed build-tool and CRC commands |
| [`../documentation/usingVSCode.md`](../documentation/usingVSCode.md) | VS Code walkthrough |
| [`../Changes log.txt`](../Changes%20log.txt) | Historical engineering journal; not authoritative for current requirements |
| [`../CHANGELOG.md`](../CHANGELOG.md) | Historical changelog pending version reconciliation |
| [`../documentation/MP Multitool M1 IO test.ods`](../documentation/MP%20Multitool%20M1%20IO%20test.ods) | Manufacturing I/O source sheet |
| [`../documentation/Multitool M1 External GPIO Connection Terminal.ods`](../documentation/Multitool%20M1%20External%20GPIO%20Connection%20Terminal.ods) | External connector source sheet |
| [`../documentation/Multitool M1 Function.ods`](../documentation/Multitool%20M1%20Function.ods) | Button/function source sheet |

## Documentation rules

1. Link to an authoritative page instead of copying the same instructions.
2. State the source file, code symbol, hardware revision, or release that supports
   a technical claim.
3. Use **Needs validation** when repository evidence is incomplete.
4. Do not treat an artifact filename as release approval or provenance.
5. Update documentation in the same change as an interface, memory layout,
   hardware mapping, or supported-behavior change.
6. Keep third-party documentation with its vendored component and link to it
   rather than presenting it as M1-authored design documentation.
