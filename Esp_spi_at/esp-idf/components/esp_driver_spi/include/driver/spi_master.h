/*
 * SPDX-FileCopyrightText: 2010-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "spi_types.h"
#include "soc_caps.h"
#include "spi_hal.h"
#include "ctrl_api.h"

#define SPI_TRANS_MAX_LEN  		1024*4 // byte
#define ESP32_SPI_TIMEOUT		200 //ms

#define CMD_HD_WRBUF_REG      	0x01
#define CMD_HD_RDBUF_REG      	0x02
#define CMD_HD_WRDMA_REG      	0x03
#define CMD_HD_RDDMA_REG      	0x04
#define CMD_HD_WR_END_REG     	0x07
#define CMD_HD_INT0_REG       	0x08
#define WRBUF_START_ADDR      	0x0
#define RDBUF_START_ADDR      	0x4

/**
 * @brief SPI common used frequency (in Hz)
 * @note SPI peripheral only has an integer divider, and the default clock source can be different on other targets,
 *       so the actual frequency may be slightly different from the desired frequency.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* Moved from app_main.c */ // B.N @ MonstaTek 2025
typedef enum {
    SPI_NULL = 0,
    SPI_READ,         // slave -> master
    SPI_WRITE,             // maste -> slave
} spi_mode_t;

typedef struct {
    bool slave_notify_flag; // when slave recv done or slave notify master to recv, it will be true
} spi_master_msg_t;

typedef struct {
    uint32_t     magic    : 8;    // 0xFE
    uint32_t     send_seq : 8;
    uint32_t     send_len : 16;
} spi_send_opt_t;

typedef struct {
    uint32_t     direct : 8;
    uint32_t     seq_num : 8;
    uint32_t     transmit_len : 16;
} spi_recv_opt_t;

typedef struct {
    spi_mode_t direct;
} spi_msg_t;

extern QueueHandle_t esp_spi_msg_queue;
/* Move end */

typedef enum stm_spi_ret_s {
	STM_SPI_OK = 0,
	STM_SPI_FAIL,
	STM_SPI_TIMEOUT
} stm_spi_ret_t;

/** @cond */
//typedef struct spi_transaction_t spi_transaction_t;
/** @endcond */
//typedef void(*transaction_cb_t)(spi_transaction_t *trans);

/**
 * @brief This is a configuration for a SPI slave device that is connected to one of the SPI buses.
 */
typedef struct {
    uint8_t command_bits;           ///< Default amount of bits in command phase (0-16), used when ``SPI_TRANS_VARIABLE_CMD`` is not used, otherwise ignored.
    uint8_t address_bits;           ///< Default amount of bits in address phase (0-64), used when ``SPI_TRANS_VARIABLE_ADDR`` is not used, otherwise ignored.
    uint8_t dummy_bits;             ///< Amount of dummy bits to insert between address and data phase
    uint32_t flags;                 ///< Bitwise OR of SPI_DEVICE_* flags
} spi_device_interface_config_t;

/** @cond */
typedef struct spi_transaction_t spi_transaction_t;
/** @endcond */
typedef void(*transaction_cb_t)(spi_transaction_t *trans);

/* Moved from spi_master.c */
typedef struct spi_device_t spi_device_t;
/// struct to hold private transaction data (like tx and rx buffer for DMA).
typedef struct {
    spi_transaction_t   *trans;
    const uint32_t *buffer_to_send;    //equals to tx_data, if SPI_TRANS_USE_RXDATA is applied; otherwise if original buffer wasn't in DMA-capable memory, this gets the address of a temporary buffer that is;
    //otherwise sets to the original buffer or NULL if no buffer is assigned.
    uint32_t *buffer_to_rcv;           //similar to buffer_to_send
} spi_trans_priv_t;


typedef struct {
    int id;
    spi_hal_trans_config_t hal;
    spi_trans_priv_t cur_trans_buf;
    int cur_cs;     //current device doing transaction
//debug information
    bool polling;   //in process of a polling, avoid of queue new transactions into ISR
} spi_host_t;

struct spi_device_t {
    int id;
    QueueHandle_t trans_queue;
    QueueHandle_t ret_queue;
    spi_device_interface_config_t cfg;
    spi_host_t *host;
    //spi_bus_lock_dev_handle_t dev_lock;
};
/* Move end */

#define SPI_TRANS_MODE_DIO            (1<<0)  ///< Transmit/receive data in 2-bit mode
#define SPI_TRANS_MODE_QIO            (1<<1)  ///< Transmit/receive data in 4-bit mode
#define SPI_TRANS_USE_RXDATA          (1<<2)  ///< Receive into rx_data member of spi_transaction_t instead into memory at rx_buffer.
#define SPI_TRANS_USE_TXDATA          (1<<3)  ///< Transmit tx_data member of spi_transaction_t instead of data at tx_buffer. Do not set tx_buffer when using this.
#define SPI_TRANS_MODE_DIOQIO_ADDR    (1<<4)  ///< Also transmit address in mode selected by SPI_MODE_DIO/SPI_MODE_QIO
#define SPI_TRANS_VARIABLE_CMD        (1<<5)  ///< Use the ``command_bits`` in ``spi_transaction_ext_t`` rather than default value in ``spi_device_interface_config_t``.
#define SPI_TRANS_VARIABLE_ADDR       (1<<6)  ///< Use the ``address_bits`` in ``spi_transaction_ext_t`` rather than default value in ``spi_device_interface_config_t``.
#define SPI_TRANS_VARIABLE_DUMMY      (1<<7)  ///< Use the ``dummy_bits`` in ``spi_transaction_ext_t`` rather than default value in ``spi_device_interface_config_t``.
#define SPI_TRANS_CS_KEEP_ACTIVE      (1<<8)  ///< Keep CS active after data transfer
#define SPI_TRANS_MULTILINE_CMD       (1<<9)  ///< The data lines used at command phase is the same as data phase (otherwise, only one data line is used at command phase)
#define SPI_TRANS_MODE_OCT            (1<<10) ///< Transmit/receive data in 8-bit mode
#define SPI_TRANS_MULTILINE_ADDR      SPI_TRANS_MODE_DIOQIO_ADDR ///< The data lines used at address phase is the same as data phase (otherwise, only one data line is used at address phase)
#define SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL   (1<<11) ///< By default driver will automatically re-alloc dma buffer if it doesn't meet hardware alignment or dma_capable requirements, this flag is for you to disable this feature, you will need to take care of the alignment otherwise driver will return you error ESP_ERR_INVALID_ARG

/**
 * This structure describes one SPI transaction. The descriptor should not be modified until the transaction finishes.
 */
struct spi_transaction_t {
    uint32_t flags;                 ///< Bitwise OR of SPI_TRANS_* flags
    uint16_t cmd;                   /**< Command data, of which the length is set in the ``command_bits`` of spi_device_interface_config_t.
                                      *
                                      *  <b>NOTE: this field, used to be "command" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF 3.0.</b>
                                      *
                                      *  Example: write 0x0123 and command_bits=12 to send command 0x12, 0x3_ (in previous version, you may have to write 0x3_12).
                                      */
    uint64_t addr;                  /**< Address data, of which the length is set in the ``address_bits`` of spi_device_interface_config_t.
                                      *
                                      *  <b>NOTE: this field, used to be "address" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF3.0.</b>
                                      *
                                      *  Example: write 0x123400 and address_bits=24 to send address of 0x12, 0x34, 0x00 (in previous version, you may have to write 0x12340000).
                                      */
    size_t length;                  ///< Total data length, in bits
    size_t rxlength;                ///< Total data length received, should be not greater than ``length`` in full-duplex mode (0 defaults this to the value of ``length``).
    void *user;                     ///< User-defined variable. Can be used to store eg transaction ID.
    union {
        const void *tx_buffer;      ///< Pointer to transmit buffer, or NULL for no MOSI phase
        uint8_t tx_data[4];         ///< If SPI_TRANS_USE_TXDATA is set, data set here is sent directly from this variable.
    };
    union {
        void *rx_buffer;            ///< Pointer to receive buffer, or NULL for no MISO phase. Written by 4 bytes-unit if DMA is used.
        uint8_t rx_data[4];         ///< If SPI_TRANS_USE_RXDATA is set, data is received directly to this variable
    };
} ;        //the rx data should start from a 32-bit aligned address to get around dma issue.

/**
 * This struct is for SPI transactions which may change their address and command length.
 * Please do set the flags in base to ``SPI_TRANS_VARIABLE_CMD_ADR`` to use the bit length here.
 */
typedef struct {
    struct spi_transaction_t base;  ///< Transaction data, so that pointer to spi_transaction_t can be converted into spi_transaction_ext_t
    uint8_t command_bits;           ///< The command length in this transaction, in bits.
    uint8_t address_bits;           ///< The address length in this transaction, in bits.
    uint8_t dummy_bits;             ///< The dummy length in this transaction, in bits.
} spi_transaction_ext_t ;

typedef struct spi_device_t *spi_device_handle_t;  ///< Handle for a device on a SPI bus


/**
 * @brief Queue a SPI transaction for interrupt transaction execution. Get the result by ``spi_device_get_trans_result``.
 *
 * @note Normally a device cannot start (queue) polling and interrupt
 *      transactions simultaneously.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Description of transaction to execute
 * @param ticks_to_wait Ticks to wait until there's room in the queue; use portMAX_DELAY to
 *                      never time out.
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid. This can happen if SPI_TRANS_CS_KEEP_ACTIVE flag is specified while
 *                                 the bus was not acquired (`spi_device_acquire_bus()` should be called first)
 *                                 or set flag SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL but tx or rx buffer not DMA-capable, or addr&len not align to cache line size
 *         - ESP_ERR_TIMEOUT       if there was no room in the queue before ticks_to_wait expired
 *         - ESP_ERR_NO_MEM        if allocating DMA-capable temporary buffer failed
 *         - ESP_ERR_INVALID_STATE if previous transactions are not finished
 *         - ESP_OK                on success
 */
esp_err_t spi_device_queue_trans(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait);

/**
 * @brief Get the result of a SPI transaction queued earlier by ``spi_device_queue_trans``.
 *
 * This routine will wait until a transaction to the given device
 * successfully completed. It will then return the description of the
 * completed transaction so software can inspect the result and e.g. free the memory or
 * reuse the buffers.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Pointer to variable able to contain a pointer to the description of the transaction
        that is executed. The descriptor should not be modified until the descriptor is returned by
        spi_device_get_trans_result.
 * @param ticks_to_wait Ticks to wait until there's a returned item; use portMAX_DELAY to never time
                        out.
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_ERR_NOT_SUPPORTED if flag `SPI_DEVICE_NO_RETURN_RESULT` is set
 *         - ESP_ERR_TIMEOUT       if there was no completed transaction before ticks_to_wait expired
 *         - ESP_OK                on success
 */
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle, spi_transaction_t **trans_desc, TickType_t ticks_to_wait);

/**
 * @brief Send a SPI transaction, wait for it to complete, and return the result
 *
 * This function is the equivalent of calling spi_device_queue_trans() followed by spi_device_get_trans_result().
 * Do not use this when there is still a transaction separately queued (started) from spi_device_queue_trans() or polling_start/transmit that hasn't been finalized.
 *
 * @note This function is not thread safe when multiple tasks access the same SPI device.
 *      Normally a device cannot start (queue) polling and interrupt
 *      transactions simutanuously.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Description of transaction to execute
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);

/**
 * @brief Immediately start a polling transaction.
 *
 * @note Normally a device cannot start (queue) polling and interrupt
 *      transactions simutanuously. Moreover, a device cannot start a new polling
 *      transaction if another polling transaction is not finished.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Description of transaction to execute
 * @param ticks_to_wait Ticks to wait until there's room in the queue;
 *              currently only portMAX_DELAY is supported.
 *
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid. This can happen if SPI_TRANS_CS_KEEP_ACTIVE flag is specified while
 *                                 the bus was not acquired (`spi_device_acquire_bus()` should be called first)
 *                                 or set flag SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL but tx or rx buffer not DMA-capable, or addr&len not align to cache line size
 *         - ESP_ERR_TIMEOUT       if the device cannot get control of the bus before ``ticks_to_wait`` expired
 *         - ESP_ERR_NO_MEM        if allocating DMA-capable temporary buffer failed
 *         - ESP_ERR_INVALID_STATE if previous transactions are not finished
 *         - ESP_OK                on success
 */
HAL_StatusTypeDef spi_device_polling_start(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait);

/**
 * @brief Poll until the polling transaction ends.
 *
 * This routine will not return until the transaction to the given device has
 * successfully completed. The task is not blocked, but actively busy-spins for
 * the transaction to be completed.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param ticks_to_wait Ticks to wait until there's a returned item; use portMAX_DELAY to never time
                        out.
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_ERR_TIMEOUT       if the transaction cannot finish before ticks_to_wait expired
 *         - ESP_OK                on success
 */
esp_err_t spi_device_polling_end(spi_device_handle_t handle, TickType_t ticks_to_wait);

/**
 * @brief Send a polling transaction, wait for it to complete, and return the result
 *
 * This function is the equivalent of calling spi_device_polling_start() followed by spi_device_polling_end().
 * Do not use this when there is still a transaction that hasn't been finalized.
 *
 * @note This function is not thread safe when multiple tasks access the same SPI device.
 *      Normally a device cannot start (queue) polling and interrupt
 *      transactions simutanuously.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Description of transaction to execute
 * @return
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_ERR_TIMEOUT       if the device cannot get control of the bus
 *         - ESP_ERR_NO_MEM        if allocating DMA-capable temporary buffer failed
 *         - ESP_ERR_INVALID_STATE if previous transactions of same device are not finished
 *         - ESP_OK                on success
 */
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);

/**
 * @brief Send an AT command to slave.
 *
 **/
uint8_t spi_AT_app_send_command(ctrl_cmd_t *app_req);


/**
 * @brief Return the status of esp32 main init.
 *
 **/
bool get_esp32_main_init_status(void);

/**
 * @brief Initialize the ESP32C6 module and tasks to be ready for use by the app.
 *
 **/
void esp32_main_init(void);


#ifdef __cplusplus
}
#endif
