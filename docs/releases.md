# Release and version policy

## Current state

The FW 0.8.0.5 integration uses the user-visible build label
`FW 0.8.0.5`. The image metadata fields in `m1_fw_update_bl.h` are major `0`,
minor `8`, build `0`, and RC `5`. Release approval applies only to an exact
artifact hash and its qualification record; neither the label nor metadata
fields alone establish approval.

## Version authority

A release must have one canonical version in source. Generated filenames,
display strings, metadata, changelog entries, and release manifests must derive
from or be verified against it.

The four image-metadata components are major, minor, build, and RC. Consumers
must preserve this existing wire and image format unless a separately reviewed
migration changes it.

## Artifact states

Use explicit states:

- **development** — local or CI output, not distributed;
- **test** — intended only for named validation;
- **release candidate** — full qualification candidate;
- **production** — approved for specified hardware and region;
- **withdrawn** — must not be installed;
- **superseded** — retained for traceability but replaced.

## Release manifest

Each production artifact requires:

```text
product:
firmware_version:
source_commit:
workspace_clean:
hardware_revisions:
region:
bootloader_compatibility:
esp32_firmware_compatibility:
build_preset:
toolchain_versions:
image_filename:
image_size:
crc32:
sha256:
signing_status:
qualification_record:
known_issues:
approved_by:
approved_at:
```

## Required release sequence

1. Freeze version, hardware targets, region, and compatibility.
2. Build from a clean, identified commit.
3. Generate manifest and hashes.
4. Run the qualification matrix.
5. Resolve or formally waive failures.
6. Approve the exact artifact hashes.
7. Publish artifacts and release notes together.
8. Mark superseded or withdrawn artifacts explicitly.

## Repository artifacts

Tracked build directories are not a release registry. Prefer immutable release
assets or controlled artifact storage with manifests. If binaries remain in the
repository, store only approved deliverables and their manifests, not compiler
objects, caches, or ambiguous test variants.
