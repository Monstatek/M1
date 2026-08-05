# M1 Engineering Findings Register

This register contains sanitized engineering findings suitable for a public
repository.

Sensitive reproduction details, unpublished security findings, private
correspondence, and exploit material must remain under the ignored
`review-private/` directory.

## Finding identifiers

Use sequential identifiers by category:

- `M1-BUILD-NNN` — build and reproducibility
- `M1-FW-NNN` — firmware correctness
- `M1-HW-NNN` — hardware behavior
- `M1-SEC-NNN` — security findings
- `M1-DOC-NNN` — documentation and repository management

## Status values

- Observed
- Needs validation
- Confirmed
- Upstream reported
- Fix in progress
- Fixed locally
- Fixed upstream
- Coordinated disclosure
- Closed
- Not reproducible

## Severity values

- Informational
- Low
- Medium
- High
- Critical
- Unassigned

Severity is not equivalent to CVSS and does not imply CVE eligibility.

## Register

| ID | Category | Summary | Status | Severity | Affected commit | Report reference |
|---|---|---|---|---|---|---|
| M1-BUILD-001 | Build | macOS setup may install overlapping Arm GNU toolchains | Observed | Low | `4c77bb8` | Not filed |
| M1-BUILD-002 | Build | macOS CMake toolchain variable handling requires validation | Needs validation | Low | `4c77bb8` | Not filed |
| M1-BUILD-003 | Quality | Native release build emits 1,076 compiler warnings | Observed | Unassigned | `4c77bb8` | Not filed |
| M1-BUILD-004 | Build | Linux/amd64 container is exact-commit byte reproducible | Confirmed | Informational | `8c5e4f2` | [Container baseline](../baselines/container-linux-amd64-gcc-14.2.md) |
| M1-BUILD-005 | Build | Native macOS and Linux container artifacts are not byte-identical | Confirmed | Informational | `8c5e4f2` | [Container baseline](../baselines/container-linux-amd64-gcc-14.2.md) |
| M1-BUILD-006 | Build | Release artifacts embed source and toolchain build paths | Confirmed | Low | `8c5e4f2` | [Container baseline](../baselines/container-linux-amd64-gcc-14.2.md) |
| M1-BUILD-007 | Build | Debian package inputs are not snapshot-pinned | Observed | Low | `8c5e4f2` | [Container baseline](../baselines/container-linux-amd64-gcc-14.2.md) |
| M1-DOC-001 | Documentation | Repository documentation is split between root and `documentation/` | Observed | Informational | `4c77bb8` | Not filed |
| M1-DOC-002 | Documentation | Contribution guidance contains placeholder repository names | Observed | Informational | `4c77bb8` | Not filed |
| M1-DOC-003 | Documentation | Security policy contains template version and response claims | Needs validation | Informational | `4c77bb8` | Not filed |

## Triage notes

### M1-BUILD-001

The macOS setup can make more than one `arm-none-eabi-*` toolchain available.
This creates command-selection ambiguity and reduces build reproducibility.

The validated native baseline explicitly used Arm GNU Toolchain 14.2.Rel1.

### M1-BUILD-002

The macOS CMake toolchain configuration should be reviewed for consistent use
of its toolchain-path and toolchain-prefix variables.

This remains a hypothesis until configuration tests demonstrate an incorrect
selection or override.

### M1-BUILD-003

The release build succeeds, but warning categories include:

- possibly uninitialized values
- format-type mismatches
- possible format overflow
- possible string overflow or truncation
- incompatible function signatures
- a non-void function potentially missing a return

Compiler warnings are not automatically defects or vulnerabilities. Each
candidate requires source review and controlled reproduction.

### M1-BUILD-004

The committed Linux/amd64 container build was executed twice from commit
`8c5e4f2401e37593281ffde490f657ba05b061a2`.

Both runs produced the same Docker image ID, BIN, CRC-bearing BIN, ELF, Intel
HEX, artifact manifest, build metadata, and firmware-warning count.

This confirms same-commit reproducibility within the captured container
dependency environment. It does not establish cross-platform byte identity or
fully hermetic future cold builds.

### M1-BUILD-005

The validated native macOS and Linux/amd64 container builds produce different
artifact hashes.

The container build reports 260 fewer combined text bytes, while data and BSS
sizes remain unchanged. Section and symbol analysis also identified changed
section content and 430 symbol-address differences.

Both outputs remain valid ARM firmware. Functional equivalence requires
separate runtime and hardware validation.

### M1-BUILD-006

Release artifacts contain source and toolchain build-path strings.

The native build exposes the local workspace and username. The canonical
container uses the stable `/workspace` source root, reducing host-specific
disclosure while retaining build-path strings.

Eliminating these paths requires compiler prefix mapping or source changes and
must be validated separately.

### M1-BUILD-007

The Debian base image digest and official Arm GNU archive checksum are pinned.

The packages installed through `apt-get` are not individually version-pinned
and are not obtained from a dated Debian snapshot. Future cold image builds may
therefore resolve different package revisions.

The current repeatability result remains valid for the captured dependency
environment, but fully hermetic future cold builds are not yet established.

### M1-DOC-001

Build documentation exists under `documentation/`, while architecture and
development guidance remain at the repository root.

Existing files will not be moved in WP1 because path changes would unnecessarily
broaden the reproducible-build pull request.

### M1-DOC-002

The contribution guide contains placeholder repository names and paths from an
earlier project template.

This should be corrected in a separate documentation-focused change.

### M1-DOC-003

The current security policy includes placeholder supported-version information
and an unverified response-time commitment.

The reporting route and upstream ownership must be confirmed before changing
the policy.
