# M1 hardware reference

## Supported platform

| Item | Repository evidence |
| --- | --- |
| MCU | STM32H573VIT6, Arm Cortex-M33, 2 MiB flash, LQFP100 |
| Nominal hardware family | Revision 2.x in the existing hardware page |
| Historical production labels | T1 and T3-MP appear in `Changes log.txt` |
| Display | ST7586S ERC240160 |
| NFC front end | ST25R3916 component code |
| Wi-Fi/Bluetooth co-processor | ESP32-C6 |
| Filesystem media | SD card through FatFs |

The terms `2.x`, `T1`, and `T3-MP` are not reconciled in the repository.
Do not infer compatibility from these labels alone.

## Hardware sources of truth

| Subject | Current source |
| --- | --- |
| MCU pin/peripheral configuration | `MonstaTek_M1_v0800.ioc`, `Core/Inc/main.h` |
| External connector | `documentation/Multitool M1 External GPIO Connection Terminal.ods` |
| Manufacturing I/O | `documentation/MP Multitool M1 IO test.ods` |
| Product button behavior | `documentation/Multitool M1 Function.ods` |
| Block diagram | `documentation/Multitool_Block_diagram.xml` |
| Historical changes/errata | `Changes log.txt` |

The ODS files should be treated as editable sources until document ownership is
formally assigned. The corresponding large XML files appear to be exports and
should not independently define hardware behavior.

## External GPIO connector

The current spreadsheet documents power, ground, MCU pins, electrical I/O type,
and alternate functions including SPI, UART, I2C, timers, ADC, and SWD. Examples
verified from the sheet include:

- External pin 1: external +5 V
- External pin 2: PE2 / SPI4_SCK / USART10_RX
- External pin 3: PE4 / SPI4_NSS / TIM15_CH1N
- External pin 4: PE5 / SPI4_MISO / TIM15_CH1
- External pin 5: PE6 / SPI4_MOSI / TIM15_CH2
- External pins 6–7: PD12/PD13 with I2C4 and TIM4 alternatives
- External pin 8: ground
- External pin 9: external +3.3 V
- External pins 10–11: PA14 SWCLK and PA13 SWDIO
- External pins 12–15 include UART1 and SPI2 functions

For electrical limits and the complete mapping, consult the source spreadsheet
and STM32H573 datasheet. **Needs validation:** connector voltage/current limits,
5 V tolerance, power-source direction, ESD protection, and allowed simultaneous
alternate functions are not defined in repository documentation.

## Known hardware concerns recovered from engineering notes

- SD-card detect and fuel-gauge interrupt originally conflicted on EXTI3; the
  notes say SD detect moved to PD1/EXTI1.
- IR receive/transmit pins were reported swapped on one hardware revision.
- RF switch control pins and RF paths were reported incorrect on an earlier
  design.
- The Sub-GHz RF path changed from direct tie to TX/RX switching.
- I2C speed was reduced from 400 kbit/s to 100 kbit/s for reliability.
- Boot0 isolation was identified as required.
- SD-card power cannot currently be cycled by firmware according to the known
  issues list.

These are historical notes, not a validated current-board errata list.

## Required compatibility matrix

Before release approval, the hardware owner should populate this table:

| Board marking | Internal revision | Supported firmware | Region | Required bootloader | Known errata | Status |
| --- | --- | --- | --- | --- | --- | --- |
| **Needs validation** | | | | | | |

## Hardware change requirements

A hardware-affecting change should update:

1. the schematic/board revision reference;
2. the CubeMX `.ioc` and generated pin definitions;
3. external connector documentation;
4. manufacturing I/O tests;
5. compatibility and errata tables;
6. release qualification evidence.
