# Legal notice and responsible use

> **Draft for counsel and regulatory review.** This notice is a repository-level
> statement for the M1 firmware project. It is not legal advice, a product
> warranty, a declaration of conformity, an equipment authorization, or a
> substitute for the instructions and notices that must accompany a product.

## Intended use

The M1 firmware and related technical material are intended for legitimate
development, interoperability, education, research, device administration, and
authorized security testing.

Use the M1 only with devices, credentials, radio systems, networks, and property
that you own or are expressly authorized to inspect or control. Possession of the
device or source code does not grant permission to access, copy, emulate,
interfere with, or control a third-party system.

## User responsibility

You are responsible for complying with all laws, regulations, licenses,
contracts, policies, and property rights applicable to your location and use.
Requirements vary by jurisdiction and may differ for receiving, recording,
storing, decoding, transmitting, emulating, or replaying a signal.

Do not use this project to:

- gain unauthorized access to a device, account, network, vehicle, building,
  payment system, alarm, credential, or other protected system;
- intercept communications or collect identifiers, credentials, or personal
  data without authorization;
- impersonate another person or clone, emulate, or replay their credential;
- jam, disrupt, flood, or cause harmful interference to radio or electronic
  communications;
- bypass access controls, rolling-code protections, encryption, safety
  interlocks, or regional transmission restrictions;
- operate on frequencies, power levels, duty cycles, or modulation modes that
  are not permitted for the device and location; or
- interfere with emergency, aviation, maritime, medical, automotive-safety,
  industrial-control, public-safety, or other critical systems.

“Research,” “educational,” or “testing” intent does not by itself make an
otherwise unauthorized or unlawful activity permissible.

## Radio operation

The presence of a frequency, radio configuration, test mode, or transmit
function in source code does not establish that its use is legal or certified in
any jurisdiction. Receive capability and transmit authorization may differ.

Use only the production hardware, antenna, firmware, region configuration,
frequency bands, transmit power, and accessories approved for the applicable
market. Stop operation if the device causes harmful interference.

See [Regulatory status and radio operation](docs/regulatory.md).

## Modified firmware and hardware

Modified firmware or hardware may alter frequency restrictions, output power,
duty cycle, protocol safeguards, update security, electrical limits, or other
behavior. Modification may invalidate test results, regulatory approvals,
warranties, support eligibility, or the operator’s authority to use the device.

Anyone modifying, building, distributing, installing, or operating modified
firmware or hardware is responsible for evaluating the resulting product and
its compliance. Do not represent a modified build as approved by Monstatek
unless Monstatek has expressly approved that exact hardware and software
configuration.

## Sensitive data

NFC/RFID dumps, radio captures, infrared data, Wi-Fi information, Bluetooth
identifiers, device identifiers, logs, and storage images may contain sensitive,
confidential, or personal data. Collect the minimum necessary information,
protect it from unauthorized access, and securely erase it when no longer
needed. Do not publish captured data unless you have the right to do so.

## Safety

Improper wiring, charging, external modules, antennas, firmware, or radio use
can damage equipment, corrupt stored data, cause interference, or create a risk
of injury or property damage. Follow the approved product instructions and
electrical limits. This project is not qualified for life-support, emergency,
safety-critical, or other uses where failure could cause serious harm unless
Monstatek expressly documents such qualification.

See [Safety notice](docs/safety.md).

## Software license and warranty

The software is licensed under the terms identified in [LICENSE](LICENSE),
[COPYING.txt](COPYING.txt), and [README_License.md](README_License.md). Those
licenses include applicable software warranty and liability provisions.

Open-source license language does not replace any warranty, consumer-protection,
safety, labeling, regulatory, or support obligations applicable to a physical
product. Product warranties and commercial terms, if any, must be provided
separately by the responsible seller.

## Third-party names and marks

Third-party product names, protocols, trademarks, and project names are the
property of their respective owners. References in this repository are for
identification, compatibility, interoperability, or attribution. They do not
imply sponsorship, endorsement, certification, or affiliation unless expressly
stated.

## Security reports

Report suspected security vulnerabilities using the private process in
[`.github/SECURITY.md`](.github/SECURITY.md). Do not include operational
credentials, private captures, or exploit details in a public issue.
