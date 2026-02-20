# Firmware Project

This repository contains firmware developed by Monstatek.

---

## Open Source License Notice

This project includes portions of code derived from third-party open source software.
The applicable components, licenses, and original sources are listed below.

---

## 1. LF RFID Implementation (GPLv3)

The LF RFID decoding and T5577 tag handling modules include structures and architectural
concepts developed with reference to the Flipper Zero firmware project.

### Related Files

- lfrfid_protocol.c / lfrfid_protocol.h
- lfrfid_protocol_em4100.c / lfrfid_protocol_em4100.h
- lfrfid_protocol_h10301.c / lfrfid_protocol_h10301.h
- t5577.c / t5577.h

### Original Project

https://github.com/flipperdevices/flipperzero-firmware

Copyright (C) Flipper Devices Inc.

### License

GNU General Public License v3.0 (GPLv3)

### Modifications

Hardware adaptation, platform integration, and functional implementation were independently
developed by Monstatek.

In accordance with GPLv3 requirements, the complete corresponding source code for the derived
portions is provided in this repository.

---

## 2. BQ25896 Charger Driver (LGPL-3.0)

This project includes a battery charger driver developed with reference to the mbed-BQ25896 project.

### Related Files

- m1_bq25896.c / m1_bq25896.h

### Reference Project

https://github.com/VRaktion/mbed-BQ25896

### License

GNU Lesser General Public License v3.0 (LGPL-3.0)

### Modifications

Hardware adaptation and integration were performed by Monstatek.

Source code for the LGPL-covered portions is available in this repository.

---

## 3. BQ27421 Fuel Gauge Driver (LGPL-3.0)

This project includes a fuel gauge driver based on the lib-BQ27421 project.

### Related Files

- m1_bq27421.c / m1_bq27421.h

### Original Project

https://github.com/svcguy/lib-BQ27421

### License

GNU Lesser General Public License v3.0 (LGPL-3.0)

### Modifications

Hardware adaptation and integration were performed by Monstatek.

Source code for the LGPL-covered portions is available in this repository.

---

## 4. OpenLogos / LogOSMaTrans Derived Component (GPL)

This project includes components derived from OpenLogos / LogOSMaTrans.

### Related Files

- privateprofilestring.c / privateprofilestring.h

### License

GNU General Public License (GPL)

---

## 5. LP5814 RGB LED Driver (MIT)

The LP5814 RGB LED driver implementation was partially developed with reference to the
LP5562 RGB LED driver library.

### Related Files

- m1_lp5814.c / m1_lp5814.h

### Reference Project

https://github.com/rickkas7/LP5562-RK

### License

MIT License

### Modifications

The implementation was adapted and modified for LP5814 hardware and system integration by Monstatek.

---

## 6. NFC Driver (STMicroelectronics Library)

This project includes NFC-related components implemented using
STMicroelectronics software libraries.

### Related Files

- nfc driver and interface modules

### Original Provider

STMicroelectronics

### License

STMicroelectronics Software License Agreement (SLA0048 or equivalent
permissive license provided with STM32 firmware packages).

### Modifications

Integration and platform adaptation for the target hardware were
performed by Monstatek.

The ST-provided library components remain under their original license terms.

---

## License Copies

Copies of applicable open source licenses are provided in the `LICENSES/` directory:

- GPL-3.0
- LGPL-3.0
- MIT

---

## License Scope Clarification

Only the LF RFID module (decoder and T5577-related implementation) and the OpenLogos-derived
component contain portions covered under GPL licenses.

The BQ25896 and BQ27421 drivers are licensed under LGPL-3.0 and apply only to their respective
driver implementations.

The LP5814 driver is based in part on an MIT-licensed reference project and applies only to the
LP5814 driver implementation.

All other firmware components in this repository were independently developed by Monstatek and are
not derived from GPL-licensed works.

---

## Source Code Availability

In accordance with applicable open source license requirements, complete corresponding source code
for GPL and LGPL covered portions is available within this repository.

---

## Contact

For questions regarding open source software usage:

Monstatek  

