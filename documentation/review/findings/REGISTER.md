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
