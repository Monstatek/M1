/* See COPYING.txt for license details. */

/*
 *  m1_csrc/esp_stub/sdkconfig.h
 *
 *  Tracked, minimal stand-in for ESP-IDF's generated sdkconfig.h, used by the
 *  STM32 firmware build ONLY.
 *
 *  WHY THIS EXISTS
 *  ---------------
 *  Vendored ESP-hosted headers that are pulled into the STM32 build -- e.g.
 *  Esp_spi_at/esp-idf/components/esp_common/include/esp_attr.h and
 *  .../log/include/esp_log.h, reached from the four compiled Esp_spi_at TUs
 *  (spi_master.c, esp_app_main.c, esp_queue.c, m1_at_response_parser.c) -- do
 *  `#include "sdkconfig.h"`. Those vendored headers must not be modified. This
 *  tracked file satisfies that include in a clean checkout, so the STM32 build
 *  no longer depends on the GENERATED, git-ignored Esp_spi_at/build/include/
 *  directory (which is absent from a fresh clone/worktree).
 *
 *  WHAT IT IS NOT
 *  --------------
 *  This is NOT the complete generated ESP-IDF sdkconfig.h (that file defines
 *  hundreds of CONFIG_* options and is a build product of the ESP32 side). It
 *  intentionally forwards only the tracked STM32<->ESP32 interface contract.
 *  This is safe because it is a MEASURED FACT that the STM32 firmware consumes
 *  no value from sdkconfig for code generation: every sdkconfig-including STM32
 *  translation unit compiles byte-identically with this stand-in, with the full
 *  generated header, or with an entirely empty header.
 */

#ifndef M1_STM32_SDKCONFIG_STUB_H_
#define M1_STM32_SDKCONFIG_STUB_H_

#include "m1_esp_interface_contract.h"

#endif /* M1_STM32_SDKCONFIG_STUB_H_ */
