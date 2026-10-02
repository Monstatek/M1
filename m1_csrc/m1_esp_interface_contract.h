/* See COPYING.txt for license details. */

/*
 *  m1_esp_interface_contract.h
 *
 *  Tracked STM32 <-> ESP32-C6 SPI-AT interface contract.
 *
 *  PURPOSE
 *  -------
 *  This header replaces the STM32 firmware's former build-time dependency on
 *  ESP-IDF's GENERATED, git-ignored sdkconfig.h (which lived under
 *  Esp_spi_at/build/include/ and is absent from a clean checkout, so a clean
 *  checkout could not configure/build). It codifies -- explicitly, tracked,
 *  and reviewable -- the small set of interface constants the STM32 side needs
 *  when talking to the ESP32 over the SPI-AT link.
 *
 *  PROVENANCE OF THE VALUES
 *  ------------------------
 *  The values below are the currently-accepted interface, taken verbatim from
 *  the tracked ESP32 configuration in documentation/ESP32/sdkconfig (the ESP32
 *  Core team's authoritative Kconfig .config):
 *      CONFIG_ESP_SPI_MODE=1
 *      CONFIG_ESP_SPI_TX_Q_SIZE=20
 *      CONFIG_ESP_SPI_RX_Q_SIZE=20
 *      CONFIG_MMU_PAGE_SIZE=0x10000   (CONFIG_MMU_PAGE_SIZE_64KB=y)
 *  They are NOT invented and they match the values the working candidate used.
 *
 *  MEASURED FACT (not assumed): the STM32 firmware consumes NO value from
 *  sdkconfig for code generation today. Every sdkconfig-including STM32
 *  translation unit (m1_cli.c and the four compiled Esp_spi_at TUs:
 *  spi_master.c, esp_app_main.c, esp_queue.c, m1_at_response_parser.c) compiles
 *  BYTE-IDENTICALLY whether the included sdkconfig defines these macros or is
 *  entirely empty. These constants are therefore defined here to make the
 *  interface explicit, range-checked and reviewable -- not because a value
 *  currently changes the binary.
 *
 *  SYNCHRONIZATION RESPONSIBILITY
 *  ------------------------------
 *  MUST STAY SYNCHRONIZED WITH THE ESP32 CORE TEAM. If the ESP32 firmware
 *  changes the SPI mode, the SPI TX/RX queue depths, or the flash MMU page
 *  size, those changes MUST be mirrored here and re-verified against real
 *  hardware. This header is the single tracked source of truth for those
 *  interface values on the STM32 side; do not reintroduce a dependency on a
 *  generated ESP-IDF sdkconfig.h to obtain them.
 */

#ifndef M1_ESP_INTERFACE_CONTRACT_H_
#define M1_ESP_INTERFACE_CONTRACT_H_

/* ---- Canonical M1 names (authoritative definitions) ---- */

/* SPI transfer mode used on the STM32<->ESP32 link (CPOL/CPHA). Mode 1. */
#define M1_ESP_SPI_MODE          1

/* SPI-AT software queue depths (number of buffered transfers). */
#define M1_ESP_SPI_TX_Q_SIZE     20   /* host -> slave */
#define M1_ESP_SPI_RX_Q_SIZE     20   /* slave -> host */

/* ESP32-C6 flash MMU page size, in bytes (64 KiB). */
#define M1_ESP_MMU_PAGE_SIZE     0x10000

/* ---- Compile-time range / compatibility checks ---- */

#if (M1_ESP_SPI_MODE < 0) || (M1_ESP_SPI_MODE > 3)
#error "m1_esp_interface_contract.h: M1_ESP_SPI_MODE must be a valid SPI mode (0..3)"
#endif

#if (M1_ESP_SPI_TX_Q_SIZE < 1) || (M1_ESP_SPI_TX_Q_SIZE > 255)
#error "m1_esp_interface_contract.h: M1_ESP_SPI_TX_Q_SIZE out of range (1..255)"
#endif

#if (M1_ESP_SPI_RX_Q_SIZE < 1) || (M1_ESP_SPI_RX_Q_SIZE > 255)
#error "m1_esp_interface_contract.h: M1_ESP_SPI_RX_Q_SIZE out of range (1..255)"
#endif

/* ESP32-C6 supports a configurable MMU page size of 8/16/32/64 KiB. */
#if (M1_ESP_MMU_PAGE_SIZE != 0x2000) && (M1_ESP_MMU_PAGE_SIZE != 0x4000) && \
    (M1_ESP_MMU_PAGE_SIZE != 0x8000) && (M1_ESP_MMU_PAGE_SIZE != 0x10000)
#error "m1_esp_interface_contract.h: M1_ESP_MMU_PAGE_SIZE must be 8/16/32/64 KiB"
#endif

/* ---- Legacy CONFIG_* aliases ----
 * Defined ONLY because the existing ESP-hosted config header
 * (m1_esp_hosted_config.h) already references these ESP-IDF spellings. Use the
 * M1_* names above for any new code; do not add new uses of the CONFIG_* forms.
 * Guarded with #ifndef so that if a real ESP-IDF sdkconfig.h is ever present in
 * a combined build it, not this contract, provides the value (no redefinition).
 */
#ifndef CONFIG_ESP_SPI_MODE
#define CONFIG_ESP_SPI_MODE        M1_ESP_SPI_MODE
#endif
#ifndef CONFIG_ESP_SPI_TX_Q_SIZE
#define CONFIG_ESP_SPI_TX_Q_SIZE   M1_ESP_SPI_TX_Q_SIZE
#endif
#ifndef CONFIG_ESP_SPI_RX_Q_SIZE
#define CONFIG_ESP_SPI_RX_Q_SIZE   M1_ESP_SPI_RX_Q_SIZE
#endif
#ifndef CONFIG_MMU_PAGE_SIZE
#define CONFIG_MMU_PAGE_SIZE       M1_ESP_MMU_PAGE_SIZE
#endif

#endif /* M1_ESP_INTERFACE_CONTRACT_H_ */
