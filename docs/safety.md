# Safety notice

> **Draft for hardware, product-safety, regulatory, and counsel review.** Product
> instructions must replace the unverified items in this page with limits and
> warnings validated for the released hardware.

## General precautions

- Use only approved hardware revisions, firmware, batteries, chargers,
  antennas, cables, and accessories.
- Stop using a device that is damaged, wet, swollen, unusually hot, emitting an
  odor, or behaving unpredictably.
- Do not open, repair, charge, or modify the device without an approved
  procedure.
- Back up removable media before firmware updates or storage tests.
- Keep the device away from environments and systems where unintended radio,
  infrared, USB, or GPIO activity could create a hazard.

## Battery and charging

The repository includes battery-charger and fuel-gauge firmware, but it does not
contain an approved end-user battery safety specification. The released product
manual should define:

- battery type, capacity, voltage, and approved replacement;
- charger/input ratings;
- permitted temperature ranges;
- damaged or swollen battery response;
- storage, transport, replacement, recycling, and disposal requirements; and
- behavior during low battery, ship mode, firmware update, and charging faults.

## External GPIO and debugging

Do not rely on alternate-function names alone as electrical limits. Before
connecting an external device, confirm connector orientation, ground,
voltage domain, input/output direction, reset state, pull configuration,
5 V tolerance, current limits, and whether the pin is shared internally.

Incorrect connections can damage the M1, the connected equipment, or both. Do
not connect external power to an output rail or attach a debugger to an
unverified pinout.

## Radio and infrared

- Use only approved antennas and radio configurations.
- Stop transmitting if harmful interference occurs.
- Do not operate near equipment where interference could affect safety.
- Avoid directing infrared transmitters toward eyes or optical sensors at close
  range until the hardware owner publishes validated optical-safety limits.

## Critical systems

The repository does not establish qualification for life-support, medical,
emergency, aviation, maritime, automotive-safety, industrial-control, security,
or other critical applications. Do not use the M1 as the sole means of access,
control, monitoring, authentication, or safety protection in such systems.

## Required validation

Before product release, owners should replace this section with approved values
for electrical limits, battery safety, RF exposure, environmental conditions,
optical output, EMC restrictions, cleaning, storage, transport, service, and
disposal.
