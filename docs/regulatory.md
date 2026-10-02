# Regulatory status and radio operation

> **Draft for regulatory and counsel review.** This repository does not contain
> enough evidence to claim FCC, ISED, CE/RED, UKCA, MIC, KC, RCM, or other market
> authorization for a particular M1 unit. Certification statements and marks
> must be added only after the responsible compliance owner verifies them.

## What the repository establishes

- The firmware contains Sub-GHz configurations and features covering portions
  of approximately 300–928 MHz.
- Historical release names identify North America and Asia variants.
- Firmware metadata contains an `ism_band_region` field.
- FCC-oriented test firmware and radio configurations exist.

These facts do not establish that a production device was tested, approved,
properly labeled, or configured identically to the test unit.

## Required product record

Before a hardware/firmware combination is marketed or released, the responsible
compliance owner should record:

| Item | Required value |
| --- | --- |
| Product and model | **Needs validation** |
| PCB and assembly revision | **Needs validation** |
| Firmware and bootloader version | **Needs validation** |
| ESP32 firmware version | **Needs validation** |
| Market/region | **Needs validation** |
| Receive bands | **Needs validation** |
| Authorized transmit bands | **Needs validation** |
| Maximum conducted/EIRP power by band | **Needs validation** |
| Modulation, bandwidth, and duty-cycle limits | **Needs validation** |
| Approved antenna type/gain | **Needs validation** |
| Equipment authorization or report identifier | **Needs validation** |
| Required product/packaging/manual statements | **Needs validation** |
| Declaration-of-conformity location | **Needs validation** |

## United States

Do not state that the M1 complies with FCC rules or display an FCC identifier
unless the responsible party confirms that the exact product configuration is
properly authorized and labeled.

If applicable to the authorized device, the product and user instructions may
need the Part 15 operating-conditions statement and a warning that unauthorized
changes or modifications could void the user’s authority to operate the
equipment. Use only the exact, approved wording supplied by the compliance
owner; do not derive a certification statement from this repository.

Reference: [FCC enforcement discussion of 47 CFR §§ 15.19 and
15.21](https://docs.fcc.gov/public/attachments/DA-22-1371A1.pdf).

## European Union

For radio equipment placed on the EU market, the responsible manufacturer must
determine applicable Radio Equipment Directive obligations. Instructions for an
intentional transmitter include operating frequency bands and maximum
radio-frequency power. Market or geographical restrictions must be disclosed
where applicable, and the product must be accompanied by the required
declaration-of-conformity information.

References:

- [Directive 2014/53/EU](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32014L0053)
- [Implementing Regulation (EU) 2017/1354](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R1354)

## Region controls

Region controls are compliance-relevant safety controls. A production release
should:

1. identify the intended market;
2. load only the approved regional configuration;
3. block disallowed transmit frequencies and modes;
4. prevent ordinary UI or file operations from bypassing restrictions;
5. record the hardware, firmware, antenna, and radio configuration tested;
6. test region mismatch and modified-file failure paths; and
7. present a clear user-facing restriction when transmission is blocked.

Receive, scan, decode, record, and replay functions can be regulated
differently. A transmit block does not by itself establish that all other use is
permitted.

## Modified builds and test modes

FCC/EMC test images, continuous-wave modes, engineering menus, radio
configuration headers, and development firmware are not production approvals.
They should be access-controlled, clearly labeled, and tied to a test plan.

Do not distribute a modified build as production firmware unless the responsible
compliance owner confirms that the modification remains within the authorized
configuration or completes any required reassessment or authorization.
