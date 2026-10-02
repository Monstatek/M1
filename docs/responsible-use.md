# Responsible use

> **Draft for counsel and compliance review.** This page provides practical
> guidance and does not determine whether a particular activity is lawful.

## Before using a feature

Confirm all of the following:

- You own the target system or have explicit permission from its owner.
- Your authorization covers the exact test, time, location, data, and equipment.
- The selected frequency and transmission settings are permitted locally.
- Testing will not affect other people, shared infrastructure, or critical
  systems.
- Captured data can be stored and handled lawfully and securely.
- You have a recovery plan if a write, replay, or update changes system state.

## Feature-specific guidance

### NFC and LF RFID

- Read, save, emulate, or write only tags and credentials you own or are
  authorized to test.
- Treat identifiers, dumps, keys, and access records as sensitive data.
- Do not test payment, identity, transit, hotel, workplace, medical, or physical
  access credentials without written authorization covering that activity.
- Verify tag type and target behavior before writing; some writes are permanent.

### Sub-GHz

- Receiving a signal does not necessarily authorize recording, decoding,
  storing, or replaying it.
- Transmit only in approved regional bands and within applicable power, antenna,
  bandwidth, modulation, and duty-cycle limits.
- Never use the device for jamming or intentional harmful interference.
- Do not replay signals to vehicles, gates, doors, alarms, industrial equipment,
  or other property without authorization.
- Replaying rolling-code or stateful protocols may desynchronize an authorized
  transmitter or create unsafe behavior.

### Infrared

- Use transmit functions only where unexpected commands cannot cause injury,
  property damage, data loss, or interruption.
- Take particular care with heaters, motors, doors, projectors, industrial
  equipment, and shared public devices.

### Wi-Fi and Bluetooth

- Scan and interact only as permitted by local law and network policy.
- Do not attempt unauthorized pairing, impersonation, credential collection,
  interference, tracking, or access.
- Protect captured identifiers and network information.

### USB, UART, and external GPIO

- Verify voltage, direction, grounding, and current limits before connecting
  hardware.
- Do not connect to safety-critical or production systems without an approved
  procedure and recovery plan.
- USB mass-storage access can corrupt media if the host and device write at the
  same time; follow the documented ownership/eject process when available.

## When to stop

Stop the activity if:

- authorization or legal status is uncertain;
- the device reports a region or frequency restriction;
- unexpected transmissions or interference occur;
- a target behaves unexpectedly;
- stored data may belong to another person;
- equipment becomes hot, damaged, unstable, or electrically unsafe; or
- continuing could affect a critical or shared service.

## Reporting misuse or vulnerabilities

Report product security issues privately through
[`.github/SECURITY.md`](../.github/SECURITY.md). Do not publish live
credentials, private communications, access codes, or instructions that create
an immediate risk to third parties.
