# Known issues and engineering validation

This historical validation register was seeded from `Changes log.txt` and source
comments. Its rows are audit candidates, not confirmed defects in FW 8.0.5.
Status and severity require owner review against current source and hardware. It
is not a substitute for tracked GitHub issues.

| ID | Area | Observation | Risk | Validation or next action | Status |
| --- | --- | --- | --- | --- | --- |
| M1-DOC-001 | NFC | NFC SPI may be accessed concurrently; current mutex-wrapper substitution can deadlock | Data corruption or feature hang | Define bus ownership and lock ordering; add concurrency stress test | Needs validation |
| M1-DOC-002 | SD card | Card power cannot be cycled by GPIO after failure | Persistent storage failure until device reset | Confirm current hardware; document recovery and future revision | Needs validation |
| M1-DOC-003 | Power | PVD low-voltage detection is listed as TODO | Unsafe operation during brownout/update | Define thresholds and verify update inhibition | Needs validation |
| M1-DOC-004 | Storage/power | SD-card manager stop behavior during low battery is unresolved | Corruption or task-state failure | Specify shutdown sequence and test | Needs validation |
| M1-DOC-005 | Sub-GHz | Record mode near 300 MHz may crash in noisy environments | Device reset or denial of service | Reproduce with sustained-noise test; bound buffers/work | Needs validation |
| M1-DOC-006 | ESP32 | ESP32 may remain enabled after AP scan | Approximately +20 mA reported consumption | Measure and implement/verify shutdown policy | Needs validation |
| M1-DOC-007 | Watchdog | Disabling backup-register access was reported to trigger watchdog after about 60 seconds | Unexpected reset | Reproduce and document required register-access sequence | Needs validation |
| M1-DOC-008 | RTC | RTC enablement remains on TODO list | Missing time-dependent behavior | Confirm product requirement and ownership | Needs validation |
| M1-DOC-009 | Hardware | Historical IR pin swap and RF switch errors are not mapped to revisions | Incorrect initialization on affected boards | Build board-revision errata and compatibility matrix | Needs validation |
| M1-DOC-010 | Versioning | Changelog, code version, and release filenames conflict | Wrong image selection and weak traceability | Adopt release policy and manifest | Open documentation gap |
| M1-DOC-011 | Security | CRC exists, but authenticated update is not established | Unauthorized firmware may be accepted | Confirm secure boot/signature design and production state | Needs validation |
| M1-DOC-012 | USB/storage | SD media ownership between device code and USB MSC is undocumented | Filesystem corruption | Define arbitration/eject behavior and test removal races | Needs validation |

## Register rules

Each validated issue should move to the project issue tracker with:

- severity and affected versions;
- hardware revision and region;
- reproduction steps and logs;
- owner and target release;
- workaround;
- resolution PR and verification evidence.

Remove an item from this page only after linking its disposition.
