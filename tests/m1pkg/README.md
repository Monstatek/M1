<!-- See COPYING.txt for license details. -->

# `.m1pkg` Gate C validation fixtures

Deterministic firmware-package fixtures for validating the **read-only**
`FW_VALIDATE` (M1CP `0x20`) parser/validator. Committed binaries so the exact
bytes and SHA-256 hashes are fixed and reviewable.

**Scope of validation.** These fixtures + the cross-check exercise the validator
**logic on the host only** (`m1cp_pkg_validate`, H573 target). They do **not**
validate FatFs, SD-card ownership, USB-MSC interaction, or any physical-device
behaviour — those require the on-device procedure in
[`documentation/M1CP_gateC_physical_validation.md`](../../documentation/M1CP_gateC_physical_validation.md).

## Contents

| file | role |
| --- | --- |
| `gen_fixtures.js` | deterministic generator (fixed byte patterns, no randomness) |
| `verify.sh` | cross-check every fixture against the real validator + re-check hashes |
| `fixtures/*.m1pkg` | 1 valid + 13 malformed packages |
| `fixtures/MANIFEST.json` | per-fixture: `expect` (result), `versions` (on NONE), `sha256`; plus `format`, `target` |

Package format: 48-byte LE header + firmware image + resource blob, tiled exactly
(see [`documentation/M1CP_web_manager.md`](../../documentation/M1CP_web_manager.md)
section 5). All checksums are CRC-32/ISO-HDLC.

## Fixtures and expected `FW_VALIDATE` result

| fixture | expected | note |
| --- | --- | --- |
| `valid` | **NONE** → fw `0.8.0.4`, resource `0.8.1.0` | canonical valid package |
| `bad_magic` | BAD_PAYLOAD | container magic corrupted |
| `bad_format` | INCOMPATIBLE | unsupported `format_version` |
| `bad_header_crc` | INTEGRITY | header CRC-32 mismatch |
| `truncated` | BAD_PAYLOAD | file shorter than declared components |
| `trailing_data` | BAD_PAYLOAD | extra bytes after last component |
| `unaligned_fw` | BAD_PAYLOAD | `fw_size` not 4-byte aligned |
| `overlap` | BAD_PAYLOAD | components overlap (duplicate/aliased) |
| `leading_gap` | BAD_PAYLOAD | first component not at offset 48 |
| `missing_fw` | BAD_PAYLOAD | `fw_size == 0` |
| `wrong_target` | INCOMPATIBLE | `target_devid` != H573 |
| `incompatible_res` | INCOMPATIBLE | resource major.minor != firmware |
| `bad_fw_crc` | INTEGRITY | firmware component CRC-32 mismatch |
| `bad_res_crc` | INTEGRITY | resource component CRC-32 mismatch |

`NO_SPACE` (image > flash bank) and `IO` (staged-file read failure) are covered by
the host unit tests (`tools/m1cp_host_test/test_m1cp_fw.c`); they are not file
fixtures (a > 1 MiB fixture and a real read fault, respectively).

## Regenerate

```sh
node tests/m1pkg/gen_fixtures.js            # rewrites fixtures/ + MANIFEST.json
```

Output is byte-for-byte reproducible; regenerating must not change any hash.

## Verify (fixture cross-check against the real validator)

```sh
sh tests/m1pkg/verify.sh
```

Builds the harness that compiles the real `m1_manager_fw.c`, runs every fixture
through `m1cp_pkg_validate`, and asserts each result matches `MANIFEST.json` and
each SHA-256 still matches. Expected: `14 matched, 0 mismatched`.
