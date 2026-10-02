/* See COPYING.txt for license details. */

/*
 *   m1_esp_hosted_config.h
 *
 *  Extra config file for ESP-hosted solution
 *
 * M1 Project
 */

#ifndef M1_ESP_HOSTED_CONFIG_H_
#define M1_ESP_HOSTED_CONFIG_H_

#include "m1_io_defs.h"
/* STM32<->ESP32 interface constants come from the tracked interface contract,
 * NOT ESP-IDF's generated, git-ignored sdkconfig.h. See
 * m1_esp_interface_contract.h for the values, their provenance, and the
 * ESP32-Core synchronization responsibility. */
#include "m1_esp_interface_contract.h"

#define CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE

#define CONFIG_ESP_HOSTED_SPI_GPIO_HANDSHAKE		ESP32_HANDSHAKE_Pin
#define CONFIG_ESP_HOSTED_SPI_GPIO_HANDSHAKE_PORT	ESP32_HANDSHAKE_GPIO_Port

#define CONFIG_ESP_HOSTED_SPI_GPIO_DATA_READY		ESP32_DATAREADY_Pin
#define CONFIG_ESP_HOSTED_SPI_GPIO_DATA_READY_PORT	ESP32_DATAREADY_GPIO_Port

#define CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE		SI4463_ENA_Pin
#define CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE_PORT	SI4463_ENA_GPIO_Port

#define CONFIG_ESP_HOSTED_SPI_GPIO_MOSI_PORT        ESP32_SPI3_MOSI_GPIO_Port
#define CONFIG_ESP_HOSTED_SPI_GPIO_MOSI				ESP32_SPI3_MOSI_Pin
#define CONFIG_ESP_HOSTED_SPI_GPIO_MISO_PORT        ESP32_SPI3_MISO_GPIO_Port
#define CONFIG_ESP_HOSTED_SPI_GPIO_MISO				ESP32_SPI3_MISO_Pin
#define CONFIG_ESP_HOSTED_SPI_GPIO_CLK_PORT			ESP32_SPI3_SCK_GPIO_Port
#define CONFIG_ESP_HOSTED_SPI_GPIO_CLK				ESP32_SPI3_SCK_Pin
#define CONFIG_ESP_HOSTED_SPI_GPIO_CS_PORT			ESP32_SPI3_NSS_GPIO_Port
#define CONFIG_ESP_HOSTED_SPI_GPIO_CS				ESP32_SPI3_NSS_Pin


#define CONFIG_ESP_HOSTED_SPI_TX_Q_SIZE				CONFIG_ESP_SPI_TX_Q_SIZE
#define CONFIG_ESP_HOSTED_SPI_RX_Q_SIZE				CONFIG_ESP_SPI_RX_Q_SIZE

#define CONFIG_ESP_HOSTED_SPI_MODE                  CONFIG_ESP_SPI_MODE
#define CONFIG_ESP_HOSTED_SPI_CLK_FREQ				10


#define SOC_MMU_PAGE_SIZE							CONFIG_MMU_PAGE_SIZE

/* Max concurrent sync/async RPC requests the host tracks. Not consumed by any
 * STM32 code path today (no RPC layer is compiled into this firmware); kept for
 * source compatibility with the shared ESP-hosted headers. Confirm with the
 * ESP32 Core team before relying on these for an actual RPC implementation. */
#define CONFIG_ESP_HOSTED_MAX_SIMULTANEOUS_SYNC_RPC_REQUESTS	10
#define CONFIG_ESP_HOSTED_MAX_SIMULTANEOUS_ASYNC_RPC_REQUESTS	10


#define CONFIG_UNITY_FREERTOS_PRIORITY		5
#define CONFIG_UNITY_FREERTOS_CPU			0 // ( ( BaseType_t ) 0x7FFFFFFF ) //tskNO_AFFINITY
#define CONFIG_UNITY_FREERTOS_STACK_SIZE	8192
//#define UNITY_FREERTOS_PRIORITY				5
//#define UNITY_FREERTOS_CPU					0 // ( ( BaseType_t ) 0x7FFFFFFF ) //tskNO_AFFINITY
//#define UNITY_FREERTOS_STACK_SIZE			8192
#define UNITY_WARN_LEAK_LEVEL_GENERAL		255
#define UNITY_CRITICAL_LEAK_LEVEL_GENERAL	1200
#define UNITY_CRITICAL_LEAK_LEVEL_LWIP		4095
#define UNITY_IGNORE_PERFORMANCE_TESTS		0

#define CONFIG_UNITY_CRITICAL_LEAK_LEVEL_GENERAL	UNITY_CRITICAL_LEAK_LEVEL_GENERAL
#define CONFIG_UNITY_WARN_LEAK_LEVEL_GENERAL		UNITY_WARN_LEAK_LEVEL_GENERAL
#define CONFIG_UNITY_CRITICAL_LEAK_LEVEL_LWIP		UNITY_CRITICAL_LEAK_LEVEL_LWIP

#define WIFI_PROV_SCAN_MAX_ENTRIES			16 // range 1-255, defined in esp-idf-v5.3.1/components/wifi_provisioning/Kconfig
#define CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES	WIFI_PROV_SCAN_MAX_ENTRIES
#define WIFI_PROV_AUTOSTOP_TIMEOUT			30 // range 5-600, defined in esp-idf-v5.3.1/components/wifi_provisioning/Kconfig
#define CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT	WIFI_PROV_AUTOSTOP_TIMEOUT

bool get_esp32_ready_status(void);
void test_get_available_wifi(void);

#endif /* M1_ESP_HOSTED_CONFIG_H_ */

