# M1 Security Review and Disclosure Workflow

## Purpose

This document defines how suspected security findings discovered during the M1
engineering review should be recorded, validated, reported, remediated, and
eventually disclosed.

The official project reporting policy remains in `.github/SECURITY.md`.

## Core principles

1. Do not publicly disclose an unpatched vulnerability.
2. Separate confirmed facts from hypotheses.
3. Use the smallest safe reproduction necessary.
4. Preserve exact commit, firmware, hardware, and toolchain details.
5. Give maintainers a reasonable opportunity to investigate and remediate.
6. Do not claim CVE status before an authorized CVE Numbering Authority assigns
   an identifier.
7. Do not publish credentials, keys, personal data, private correspondence, or
   weaponized proof-of-concept material.

## Storage locations

### Public material

Store sanitized material under:

    documentation/review/

Examples include:

- build baselines
- non-sensitive artifact manifests
- sanitized warning summaries
- confirmed non-security bugs
- public issue references
- fixed and disclosed security findings

### Private material

Store unpublished or sensitive material under:

    review-private/

This directory is intentionally ignored by Git.

Appropriate private content includes:

- unpublished security hypotheses
- detailed reproduction instructions
- crash dumps
- sensitive logs
- proof-of-concept material
- maintainer correspondence
- embargo details
- draft advisories

## Finding lifecycle

### 1. Observation

Record the initial behavior without assuming root cause or impact.

Status:

    Observed

Required minimum information:

- date observed
- exact commit
- toolchain
- firmware configuration
- hardware revision, when relevant
- expected behavior
- observed behavior

### 2. Validation

Repeat the test in a controlled environment.

Status:

    Needs validation

Validation should establish:

- reproducibility
- required configuration
- affected component
- whether the behavior exists on the unmodified baseline
- whether the observation is caused by the test environment
- whether the behavior affects confidentiality, integrity, or availability

### 3. Confirmation

A finding may be marked confirmed only when evidence supports the conclusion.

Status:

    Confirmed

A compiler warning alone is not confirmation of a defect or vulnerability.

### 4. Security classification

Classify the finding as one of:

- correctness defect
- reliability issue
- build or reproducibility issue
- hardening opportunity
- security weakness
- confirmed security vulnerability

A confirmed security vulnerability should have a demonstrated security boundary
or security property that can be violated.

### 5. Private reporting

Use the project reporting process described in `.github/SECURITY.md`.

Do not create a public GitHub issue for an undisclosed security vulnerability.

Status:

    Upstream reported

Record privately:

- report date
- reporting channel
- maintainer contact
- acknowledgement date
- report identifier
- requested disclosure timeline

### 6. Remediation

Track the proposed correction and regression test.

Possible statuses:

- Fix in progress
- Fixed locally
- Fixed upstream

The regression test should fail before the correction and pass afterward.

### 7. CVE consideration

CVE consideration is appropriate only when:

- a real security impact is demonstrated
- affected versions can be identified
- the behavior is reproducible
- the issue is not solely a hardening recommendation
- the maintainer or another suitable CNA can coordinate assignment

Do not describe a finding as a CVE merely because a request was submitted.

Possible states:

- CVE not applicable
- CVE eligibility under review
- CVE requested
- CVE assigned

### 8. Coordinated disclosure

Public disclosure should normally occur after:

- a correction is available
- affected versions are identified
- maintainers have reviewed the report
- disclosure timing has been coordinated
- sensitive material has been sanitized

Status:

    Coordinated disclosure

## Evidence requirements

A security report should record:

1. Finding identifier
2. Title
3. Affected repository and commit
4. Affected release or firmware version
5. Hardware revision
6. Required attacker access
7. Required user interaction
8. Reproduction steps
9. Reproduction reliability
10. Expected behavior
11. Observed behavior
12. Demonstrated security impact
13. Relevant logs or traces
14. Proposed correction
15. Regression test
16. Disclosure history

## Public issue guidance

A public GitHub issue is appropriate for:

- build failures
- documentation problems
- non-sensitive correctness defects
- reproducibility problems
- already-fixed and disclosed vulnerabilities

A public GitHub issue is not appropriate for:

- an unpatched exploitable vulnerability
- sensitive crash data
- credential exposure
- private maintainership correspondence
- detailed attack instructions
- unpublished proof-of-concept material

## Closure criteria

A finding may be closed when one of the following applies:

- the issue was fixed and validated
- the issue was fixed upstream
- the report was publicly disclosed
- the behavior could not be reproduced
- the behavior was expected and documented
- the finding was a false positive
- the affected component was removed or no longer supported

The closure record should explain the reason and preserve links to the relevant
commit, pull request, issue, advisory, release, or assigned CVE.
