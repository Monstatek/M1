/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
Architecture:

We can initialize a SPI driver, but we don't talk to the SPI driver itself, we address a device. A device essentially
is a combination of SPI port and CS pin, plus some information about the specifics of communication to the device
(timing, command/address length etc). The arbitration between tasks is also in conception of devices.

A device can work in interrupt mode and polling mode, and a third but
complicated mode which combines the two modes above:

1. Work in the ISR with a set of queues; one per device.

   The idea is that to send something to a SPI device, you allocate a
   transaction descriptor. It contains some information about the transfer
   like the length, address, command etc, plus pointers to transmit and
   receive buffer. The address of this block gets pushed into the transmit
   queue. The SPI driver does its magic, and sends and retrieves the data
   eventually. The data gets written to the receive buffers, if needed the
   transaction descriptor is modified to indicate returned parameters and
   the entire thing goes into the return queue, where whatever software
   initiated the transaction can retrieve it.

   The entire thing is run from the SPI interrupt handler. If SPI is done
   transmitting/receiving but nothing is in the queue, it will not clear the
   SPI interrupt but just disable it by esp_intr_disable. This way, when a
   new thing is sent, pushing the packet into the send queue and re-enabling
   the interrupt (by esp_intr_enable) will trigger the interrupt again, which
   can then take care of the sending.

2. Work in the polling mode in the task.

   In this mode we get rid of the ISR, FreeRTOS queue and task switching, the
   task is no longer blocked during a transaction. This increase the cpu
   load, but decrease the interval of SPI transactions. Each time only one
   device (in one task) can send polling transactions, transactions to
   other devices are blocked until the polling transaction of current device
   is done.

   In the polling mode, the queue is not used, all the operations are done
   in the task. The task calls ``spi_device_polling_start`` to setup and start
   a new transaction, then call ``spi_device_polling_end`` to handle the
   return value of the transaction.

   To handle the arbitration among devices, the device "temporarily" acquire
   a bus by the ``device_acquire_bus_internal`` function, which writes
   dev_request by CAS operation. Other devices which wants to send polling
   transactions but don't own the bus will block and wait until given the
   semaphore which indicates the ownership of bus.

   In case of the ISR is still sending transactions to other devices, the ISR
   should maintain an ``random_idle`` flag indicating that it's not doing
   transactions. When the bus is locked, the ISR can only send new
   transactions to the acquiring device. The ISR will automatically disable
   itself and send semaphore to the device if the ISR is free. If the device
   sees the random_idle flag, it can directly start its polling transaction.
   Otherwise it should block and wait for the semaphore from the ISR.

   After the polling transaction, the driver will release the bus. During the
   release of the bus, the driver search all other devices to see whether
   there is any device waiting to acquire the bus, if so, acquire for it and
   send it a semaphore if the device queue is empty, or invoke the ISR for
   it. If all other devices don't need to acquire the bus, but there are
   still transactions in the queues, the ISR will also be invoked.

   To get better polling efficiency, user can call ``spi_device_acquire_bus``
   function, which also calls the ``spi_bus_lock_acquire_core`` function,
   before a series of polling transactions to a device. The bus acquiring and
   task switching before and after the polling transaction will be escaped.

3. Mixed mode

   The driver is written under the assumption that polling and interrupt
   transactions are not happening simultaneously. When sending polling
   transactions, it will check whether the ISR is active, which includes the
   case the ISR is sending the interrupt transactions of the acquiring
   device. If the ISR is still working, the routine sending a polling
   transaction will get blocked and wait until the semaphore from the ISR
   which indicates the ISR is free now.

   A fatal case is, a polling transaction is in flight, but the ISR received
   an interrupt transaction. The behavior of the driver is unpredictable,
   which should be strictly forbidden.

We have two bits to control the interrupt:

1. The slave->trans_done bit, which is automatically asserted when a transaction is done.

   This bit is cleared during an interrupt transaction, so that the interrupt
   will be triggered when the transaction is done, or the SW can check the
   bit to see if the transaction is done for polling transactions.

   When no transaction is in-flight, the bit is kept active, so that the SW
   can easily invoke the ISR by enable the interrupt.

2. The system interrupt enable/disable, controlled by esp_intr_enable and esp_intr_disable.

   The interrupt is disabled (by the ISR itself) when no interrupt transaction
   is queued. When the bus is not occupied, any task, which queues a
   transaction into the queue, will enable the interrupt to invoke the ISR.
   When the bus is occupied by a device, other device will put off the
   invoking of ISR to the moment when the bus is released. The device
   acquiring the bus can still send interrupt transactions by enable the
   interrupt.

*/

#include <string.h>
#include <sys/param.h>
//#include "esp_private/periph_ctrl.h"
//#include "esp_private/spi_common_internal.h"
//#include "esp_private/spi_master_internal.h"
#include "spi_master.h"
//#include "esp_clk_tree.h"
//#include "clk_ctrl_os.h"
#include "m1_log_debug.h"
#include "esp_check.h"
//#include "esp_ipc.h"
#include "task.h"
#include "queue.h"
//#include "soc/soc_memory_layout.h"
//#include "driver/gpio.h"
#include "spi_hal.h"
//#include "hal/spi_ll.h"
//#include "hal/hal_utils.h"
//#include "esp_heap_caps.h"
#include "m1_io_defs.h"
#include "m1_esp32_hal.h"
#include "m1_compile_cfg.h"
#include "m1_log_debug.h"

#define DEV_NUM_MAX 1 // B.N @ MonstaTek 2025

#define SPI_TAG		"ESP_SPI"

/**
  * @brief  Full duplex transaction SPI transaction
  * @param  txbuff: TX SPI buffer
  * @retval STM_SPI_OK / STM_SPI_FAIL
  */
static HAL_StatusTypeDef spi_transaction_stm(spi_hal_trans_config_t *hal_trans)
{
	uint8_t cmd_buff[3] = {}; // CMD ADD DUMMY
	uint16_t cmd_len;
	uint16_t tx_data_len, rx_data_len;
	HAL_StatusTypeDef retval = HAL_ERROR;

	cmd_buff[0] = hal_trans->cmd; // CMD
	cmd_buff[1] = hal_trans->addr; // ADDRESS
	//cmd_buff[2] = 0x00; // DUMMY
	cmd_len = 3; // <CMD> <ADDR> <DUMMY>
	tx_data_len = hal_trans->tx_bitlen/8; // convert bits to bytes
	rx_data_len = hal_trans->rx_bitlen/8; // convert bits to bytes
	switch ( hal_trans->cmd )
	{
		case CMD_HD_RDBUF_REG: // Master reads the status of the slave
		case CMD_HD_WRBUF_REG: // The message used by the master to send a request to send data
		case CMD_HD_WRDMA_REG: // The message used by the master to send data to slave
		case CMD_HD_INT0_REG: // The message used by the master to inform the slave all data has been received
		case CMD_HD_RDDMA_REG: //The message used by the master to receive data from the slave
		case CMD_HD_WR_END_REG: // The message used by the master to inform the slave all data has been sent
			break;

		default:
			cmd_len = 0; // invalid command
			retval = HAL_ERROR;
			break;
	} // switch ( hal_trans->cmd )

	if ( cmd_len )
	{
		HAL_GPIO_WritePin(ESP32_SPI3_NSS_GPIO_Port, ESP32_SPI3_NSS_Pin, GPIO_PIN_RESET);
		retval = HAL_SPI_Transmit(&hspi_esp, cmd_buff, cmd_len, ESP32_SPI_TIMEOUT); // Send cmd data
		if ( tx_data_len )
			retval = HAL_SPI_Transmit(&hspi_esp, hal_trans->send_buffer, tx_data_len, ESP32_SPI_TIMEOUT); // Send data
		else if ( rx_data_len )
			retval= HAL_SPI_TransmitReceive(&hspi_esp, hal_trans->rcv_buffer, hal_trans->rcv_buffer, rx_data_len, ESP32_SPI_TIMEOUT); // Receive data
		HAL_GPIO_WritePin(ESP32_SPI3_NSS_GPIO_Port, ESP32_SPI3_NSS_Pin, GPIO_PIN_SET);
	} // if ( cmd_len )

	switch(retval)
	{
		case HAL_OK:
			M1_LOG_D(SPI_TAG, "OK...\r\n");
			break;

		default:
			M1_LOG_E(SPI_TAG, "Error...\r\n");
			break;
	} // switch(retval)

	return retval;
} // static HAL_StatusTypeDef spi_transaction_stm(spi_hal_trans_config_t *hal_trans)



// Debug only
// NOTE if the acquiring is not fully completed, `spi_bus_lock_get_acquiring_dev`
// may return a false `NULL` cause the function returning false `false`.
static inline bool spi_bus_device_is_polling(spi_device_t *dev)
{
    return dev->host->polling;
}

/*-----------------------------------------------------------------------------
    Working Functions
-----------------------------------------------------------------------------*/

static void uninstall_priv_desc(spi_trans_priv_t* trans_buf)
{
    spi_transaction_t *trans_desc = trans_buf->trans;
    if ((void *)trans_buf->buffer_to_send != &trans_desc->tx_data[0] &&
            trans_buf->buffer_to_send != trans_desc->tx_buffer) {
        free((void *)trans_buf->buffer_to_send); //force free, ignore const
    }
    // copy data from temporary DMA-capable buffer back to IRAM buffer and free the temporary one.
    if (trans_buf->buffer_to_rcv && (void *)trans_buf->buffer_to_rcv != &trans_desc->rx_data[0] && trans_buf->buffer_to_rcv != trans_desc->rx_buffer) { // NOLINT(clang-analyzer-unix.Malloc)
        if (trans_desc->flags & SPI_TRANS_USE_RXDATA) {
            memcpy((uint8_t *) & trans_desc->rx_data[0], trans_buf->buffer_to_rcv, (trans_desc->rxlength + 7) / 8);
        } else {
            memcpy(trans_desc->rx_buffer, trans_buf->buffer_to_rcv, (trans_desc->rxlength + 7) / 8);
        }
        free(trans_buf->buffer_to_rcv);
    }
}



void setup_priv_desc(spi_trans_priv_t* priv_desc)
{
    spi_transaction_t *trans_desc = priv_desc->trans;

    // rx memory assign
    uint32_t* rcv_ptr;
    // tx memory assign
    const uint32_t *send_ptr;

    if (trans_desc->flags & SPI_TRANS_USE_RXDATA)
    {
        rcv_ptr = (uint32_t *)&trans_desc->rx_data[0];
    }
    else
    {
        //if not use RXDATA neither rx_buffer, buffer_to_rcv assigned to NULL
        rcv_ptr = trans_desc->rx_buffer;
    }

    if (trans_desc->flags & SPI_TRANS_USE_TXDATA)
    {
        send_ptr = (uint32_t *)&trans_desc->tx_data[0];
    }
    else
    {
        //if not use TXDATA neither tx_buffer, tx data assigned to NULL
        send_ptr = trans_desc->tx_buffer ;
    }

    priv_desc->buffer_to_send = send_ptr;
    priv_desc->buffer_to_rcv = rcv_ptr;
}


HAL_StatusTypeDef spi_device_queue_trans(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait)
{
    if ( spi_bus_device_is_polling(handle) )
    {
    	M1_LOG_E(SPI_TAG, "Cannot queue new transaction while previous polling transaction is not terminated.\r\n");
    	return HAL_BUSY;
    }

    spi_trans_priv_t trans_buf = { .trans = trans_desc, };
    BaseType_t r = xQueueSend(handle->trans_queue, (void *)&trans_buf, ticks_to_wait);
    if ( r )
    	return HAL_OK;

    uninstall_priv_desc(&trans_buf);
    return HAL_TIMEOUT;
}


HAL_StatusTypeDef spi_device_get_trans_result(spi_device_handle_t handle, spi_transaction_t **trans_desc, TickType_t ticks_to_wait)
{
    BaseType_t r;
    spi_trans_priv_t trans_buf;

    //use the interrupt, block until return
    r = xQueueReceive(handle->ret_queue, (void*)&trans_buf, ticks_to_wait);
    if ( r )
    {
    	(*trans_desc) = trans_buf.trans;
    	return HAL_OK;
    }

    // The memory occupied by rx and tx DMA buffer destroyed only when receiving from the queue (transaction finished).
    // If timeout, wait and retry.
    // Every in-flight transaction request occupies internal memory as DMA buffer if needed.
    return HAL_TIMEOUT;
}


//Porcelain to do one blocking transmission.
HAL_StatusTypeDef spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc)
{
    HAL_StatusTypeDef ret;
    spi_transaction_t *ret_trans;
    //ToDo: check if any spi transfers in flight
    ret = spi_device_queue_trans(handle, trans_desc, portMAX_DELAY);
    if (ret != HAL_OK) {
        return ret;
    }

    ret = spi_device_get_trans_result(handle, &ret_trans, portMAX_DELAY);
    if (ret != HAL_OK) {
        return ret;
    }

    assert(ret_trans == trans_desc);
    return HAL_OK;
}


HAL_StatusTypeDef spi_device_polling_start(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait)
{
    spi_trans_priv_t priv_polling_trans = { .trans = trans_desc };
    setup_priv_desc(&priv_polling_trans);

    //Polling, no interrupt is used.
    handle->host->polling = true;
    handle->host->cur_trans_buf = priv_polling_trans;

    M1_LOG_D(SPI_TAG, "Polling trans\r\n");

    spi_hal_trans_config_t *hal = &(handle->host->hal);
    handle->host->cur_cs = handle->id;
    spi_hal_trans_config_t hal_trans = {};

    hal_trans.tx_bitlen = handle->host->cur_trans_buf.trans->length;
    hal_trans.rx_bitlen = handle->host->cur_trans_buf.trans->rxlength;
    hal_trans.rcv_buffer = (uint8_t*)handle->host->cur_trans_buf.buffer_to_rcv;
    hal_trans.send_buffer = (uint8_t*)handle->host->cur_trans_buf.buffer_to_send;
    hal_trans.cmd = handle->host->cur_trans_buf.trans->cmd;
    hal_trans.addr = handle->host->cur_trans_buf.trans->addr;

    if (handle->host->cur_trans_buf.trans->flags & SPI_TRANS_VARIABLE_CMD) {
        hal_trans.cmd_bits = ((spi_transaction_ext_t *)handle->host->cur_trans_buf.trans)->command_bits;
    } else {
        hal_trans.cmd_bits = handle->cfg.command_bits;
    }
    if (handle->host->cur_trans_buf.trans->flags & SPI_TRANS_VARIABLE_ADDR) {
        hal_trans.addr_bits = ((spi_transaction_ext_t *)handle->host->cur_trans_buf.trans)->address_bits;
    } else {
        hal_trans.addr_bits = handle->cfg.address_bits;
    }
    if (handle->host->cur_trans_buf.trans->flags & SPI_TRANS_VARIABLE_DUMMY) {
        hal_trans.dummy_bits = ((spi_transaction_ext_t *)handle->host->cur_trans_buf.trans)->dummy_bits;
    } else {
        hal_trans.dummy_bits = handle->cfg.dummy_bits;
    }
    hal_trans.cs_keep_active = (handle->host->cur_trans_buf.trans->flags & SPI_TRANS_CS_KEEP_ACTIVE) ? 1 : 0;
    //Save the transaction attributes for internal usage.
    memcpy(hal, &hal_trans, sizeof(spi_hal_trans_config_t));

    //Kick off transfer
    return spi_transaction_stm(&hal_trans);
}



HAL_StatusTypeDef spi_device_polling_end(spi_device_handle_t handle, TickType_t ticks_to_wait)
{
    spi_host_t *host = handle->host;

    assert(host->cur_cs == handle->id);

    M1_LOG_D(SPI_TAG, "Complete\r\n");
    host->cur_cs = DEV_NUM_MAX;
    //release temporary buffers
    uninstall_priv_desc(&host->cur_trans_buf);
    host->polling = false;

    return HAL_OK;
}

HAL_StatusTypeDef spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t* trans_desc)
{
	HAL_StatusTypeDef ret;
    ret = spi_device_polling_start(handle, trans_desc, portMAX_DELAY);
    if (ret != HAL_OK)
    {
        return ret;
    }

    return spi_device_polling_end(handle, portMAX_DELAY);
}



