/* See COPYING.txt for license details. */

/*
*
* m1_capture_link.c
*
* STM32 capture-mode UART4 DMA receive + Gate 0 transport soak driver
* (see m1_capture_link.h).
*
* The circular GPDMA receive is modeled on the proven logdb RX path in
* m1_log_debug.c (GPDMA1_REQUEST_UART4_RX, &UART4->RDR, linked-list circular),
* using the otherwise-unused GPDMA1_Channel4. The DMA write position is polled
* via __HAL_DMA_GET_COUNTER (as m1_usb_cdc_msc.c does for logdb), so no channel
* IRQ handler is needed. The ordinary interrupt-RX path and console baud are
* restored on exit.
*
* M1 Project
*
*/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_capture_link.h"
#include "m1_esp32_hal.h"
#include "m1_ring_buffer.h"
#include "m1_esp_uart_transport.h"
#include "m1_usb_cdc_msc.h"
#include "m1_log_debug.h"
#include "m1_display.h"
#include "m1_system.h"
#include "ff.h"
#include "m1_fault_report.h"
#include "m1_watchdog.h"
#include "m1_capture_protocol.h"
#include "m1_capture_transport.h"
#include "m1_capture_frame.h"
#include "m1_capture_pcap.h"
#include "m1_capture_scan_parse.h"
#include "m1_wifi.h"
#include "m1_wifi_session_cache.h"
#include "m1_file_util.h"
#include "m1_sdcard_provision.h"

/*************************** D E F I N E S ************************************/

#define CAP_SESSION_ID     0xCA07u
#define CAPBUF_SIZE        4096u              /* circular DMA RX buffer      */
#define CAP_GRANT_CHUNK    32768u             /* bytes granted per CAP_CREDIT */
#define CAP_GRANT_LOW      16384u             /* top up when outstanding < this */
#define CAP_SOAK_MS        3000u              /* per-run soak duration       */

/***************************** V A R I A B L E S ******************************/

static DMA_NodeTypeDef  s_cap_node;
static DMA_QListTypeDef s_cap_list;
static DMA_HandleTypeDef s_cap_dma;
static uint8_t s_capbuf[CAPBUF_SIZE];

/* Progress checkpoint shown by the fault reporter if the soak crashes. Values:
 *  0 idle | 10 entered | 12 ESP ready | 15 ACK | 20 baud switch |
 *  30 DMA setup begin | 34 DMA running | 40 loop | 50 stop/drain |
 *  60 DMA stopped | 61 console restored | 64 returned | 70 result screen */
volatile uint32_t g_cap_checkpoint = 0;
#define CP(n) do { g_cap_checkpoint = (uint32_t)(n); } while (0)

/*************************** D M A   S E T U P ********************************/

/* Build + start the circular GPDMA RX on UART4 (Channel4). Mirrors the logdb
 * UART4-RX node config. Returns 0 on success. */
static int cap_dma_start(void)
{
    DMA_NodeConfTypeDef nc;

    CP(30);
    __HAL_RCC_GPDMA1_CLK_ENABLE(); /* defensive: normally enabled by TX DMA init */

    memset(&nc, 0, sizeof(nc));
    nc.NodeType = DMA_GPDMA_LINEAR_NODE;
    nc.Init.Request = GPDMA1_REQUEST_UART4_RX;
    nc.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    nc.Init.Direction = DMA_PERIPH_TO_MEMORY;
    nc.Init.SrcInc = DMA_SINC_FIXED;
    nc.Init.DestInc = DMA_DINC_INCREMENTED;
    nc.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    nc.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    nc.Init.SrcBurstLength = 1;
    nc.Init.DestBurstLength = 1;
    nc.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT1;
    nc.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    nc.Init.Mode = DMA_NORMAL;
    nc.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    nc.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    nc.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    nc.SrcAddress = (uint32_t)&UART4->RDR;
    nc.DstAddress = (uint32_t)s_capbuf;   /* real dest; Receive_DMA re-sets it */
    nc.DataSize = CAPBUF_SIZE;

    memset(&s_cap_list, 0, sizeof(s_cap_list));
    memset(&s_cap_node, 0, sizeof(s_cap_node));
    if (HAL_DMAEx_List_BuildNode(&nc, &s_cap_node) != HAL_OK) { return -1; }
    if (HAL_DMAEx_List_InsertNode(&s_cap_list, NULL, &s_cap_node) != HAL_OK) { return -1; }
    if (HAL_DMAEx_List_SetCircularMode(&s_cap_list) != HAL_OK) { return -1; }

    s_cap_dma.Instance = GPDMA1_Channel4;
    s_cap_dma.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
    s_cap_dma.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
    s_cap_dma.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT1;
    s_cap_dma.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    s_cap_dma.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
    if (HAL_DMAEx_List_Init(&s_cap_dma) != HAL_OK) { return -1; }
    if (HAL_DMAEx_List_LinkQ(&s_cap_dma, &s_cap_list) != HAL_OK) { return -1; }

    __HAL_LINKDMA(&huart_esp, hdmarx, s_cap_dma);

    if (HAL_DMA_ConfigChannelAttributes(&s_cap_dma, DMA_CHANNEL_NPRIV) != HAL_OK) { return -1; }

    if (HAL_UART_Receive_DMA(&huart_esp, s_capbuf, CAPBUF_SIZE) != HAL_OK) { return -1; }
    CP(34);
    return 0;
}

static void cap_dma_stop(void)
{
    HAL_UART_DMAStop(&huart_esp);
    (void)HAL_DMAEx_List_DeInit(&s_cap_dma);
    huart_esp.hdmarx = NULL;
}

/* Current circular write head from the DMA counter. */
static uint32_t cap_dma_head(void)
{
    return (CAPBUF_SIZE - (uint32_t)__HAL_DMA_GET_COUNTER(huart_esp.hdmarx)) % CAPBUF_SIZE;
}

/* Poll hardware error flags: a UART overrun (ORE) means the UART FIFO overran
 * before DMA drained it; a GPDMA error flag means the DMA transfer faulted.
 * Both are "the link outran us" signals. Read-and-clear into the two counters. */
static void cap_poll_error_counts(uint32_t *uart_ore, uint32_t *dma_errors)
{
    if (__HAL_UART_GET_FLAG(&huart_esp, UART_FLAG_ORE))
    {
        (*uart_ore)++;
        __HAL_UART_CLEAR_OREFLAG(&huart_esp);
    }
    if (__HAL_DMA_GET_FLAG(&s_cap_dma, DMA_FLAG_DTE) ||
        __HAL_DMA_GET_FLAG(&s_cap_dma, DMA_FLAG_ULE) ||
        __HAL_DMA_GET_FLAG(&s_cap_dma, DMA_FLAG_USE))
    {
        (*dma_errors)++;
        __HAL_DMA_CLEAR_FLAG(&s_cap_dma, DMA_FLAG_DTE | DMA_FLAG_ULE | DMA_FLAG_USE);
    }
}

static void cap_poll_errors(m1_capture_soak_result_t *out)
{
    cap_poll_error_counts(&out->uart_ore, &out->dma_errors);
}

/* Scratch buffer for the SD-write load variant (4 KiB, static — not on stack). */
static uint8_t s_load_buf[4096];

/*************************** T R A N S P O R T ********************************/

static uint32_t cap_grant(uint32_t amount)
{
    uint8_t wire[M1_CAP_MAX_WIRE];
    uint8_t p[4];
    size_t n;
    p[0] = (uint8_t)amount; p[1] = (uint8_t)(amount >> 8);
    p[2] = (uint8_t)(amount >> 16); p[3] = (uint8_t)(amount >> 24);
    n = m1_cap_frame_encode(M1_CAP_CREDIT, CAP_SESSION_ID, 0, p, 4, wire, sizeof(wire));
    if (n > 0) { (void)esp32_uart_write(wire, (uint16_t)n, 100); }
    return amount;
}

static void cap_send_stop(void)
{
    uint8_t wire[M1_CAP_MAX_WIRE];
    size_t n = m1_cap_frame_encode(M1_CAP_STOP, CAP_SESSION_ID, 0, NULL, 0, wire, sizeof(wire));
    if (n > 0) { (void)esp32_uart_write(wire, (uint16_t)n, 100); }
}

/* Drain the DMA ring from *tail to the current head, decoding frames. */
static void cap_drain(uint32_t *tail, m1_cap_rx_stream_t *rx, m1_cap_seq_t *seq,
                      m1_capture_soak_result_t *out)
{
    static uint8_t block[M1_CAP_MAX_WIRE];
    static uint8_t dbuf[M1_CAP_MAX_ENVELOPE];
    uint32_t head = cap_dma_head();

    /* Peak unconsumed depth in the circular ring. If this reaches CAPBUF_SIZE
     * the DMA has lapped the reader — a true RX overrun. */
    {
        uint32_t backlog = (head + CAPBUF_SIZE - *tail) % CAPBUF_SIZE;
        if (backlog > out->max_backlog) { out->max_backlog = backlog; }
    }

    while (*tail != head)
    {
        uint8_t b = s_capbuf[*tail];
        size_t bl;
        *tail = (*tail + 1u) % CAPBUF_SIZE;

        bl = m1_cap_rx_push(rx, b, block, sizeof(block));
        if (bl == 0u) { continue; }
        out->blocks_seen++; /* a complete delimiter-bounded block arrived */

        {
            m1_cap_msg_t msg;
            m1_cap_status_t st = m1_cap_frame_decode(block, bl, dbuf, sizeof(dbuf), &msg);
            if (st == M1_CAP_OK)
            {
                if (msg.msg_type == M1_CAP_FRAME_BATCH)
                {
                    out->frames_ok++;
                    out->bytes_rx += bl;
                    (void)m1_cap_seq_observe(seq, msg.sequence);
                }
                else if (msg.msg_type == M1_CAP_READY)
                {
                    out->got_ready = true; /* binary sync established */
                }
                else if (msg.msg_type == M1_CAP_STOPPED)
                {
                    out->got_stopped = true;
                    if (msg.payload_len >= 8u && msg.payload != NULL)
                    {
                        out->esp_seq = (uint32_t)msg.payload[0] | ((uint32_t)msg.payload[1] << 8) |
                                       ((uint32_t)msg.payload[2] << 16) | ((uint32_t)msg.payload[3] << 24);
                        out->esp_frames = (uint32_t)msg.payload[4] | ((uint32_t)msg.payload[5] << 8) |
                                          ((uint32_t)msg.payload[6] << 16) | ((uint32_t)msg.payload[7] << 24);
                    }
                    if (msg.payload_len >= 16u && msg.payload != NULL)
                    {
                        out->esp_starved = (uint32_t)msg.payload[12] | ((uint32_t)msg.payload[13] << 8) |
                                           ((uint32_t)msg.payload[14] << 16) | ((uint32_t)msg.payload[15] << 24);
                    }
                    if (msg.payload_len >= 20u && msg.payload != NULL)
                    {
                        out->esp_grants = (uint32_t)msg.payload[16] | ((uint32_t)msg.payload[17] << 8) |
                                          ((uint32_t)msg.payload[18] << 16) | ((uint32_t)msg.payload[19] << 24);
                    }
                }
            }
            else if (st == M1_CAP_ERR_CRC) { out->crc_errors++; }
            else { out->format_errors++; }
        }
    }
}

/*************************** S O A K   D R I V E R ****************************/

/* Forward decl: defined further down (cap_esp32_link_ready(), "Proven ESP32
 * link bring-up"). m1_capture_soak_run() below needs it too -- see the fix
 * at its own ESP-ready check for why. */
static bool cap_esp32_link_ready(void);

bool m1_capture_soak_run(uint32_t baud, uint32_t duration_ms, bool load,
                         m1_capture_soak_result_t *out)
{
    enCdcMode prev_cdc;
    uint8_t restore_cdc = 0;
    char cmd[48];
    char resp[160];
    /* Large buffers kept OFF the task stack (single-instance soak): the rx
     * stream carries a ~1 KiB COBS block, and FatFs FIL embeds a sector buffer. */
    static m1_cap_rx_stream_t rx;
    static FIL lf;
    m1_cap_seq_t seq;
    uint32_t tail = 0;
    uint32_t granted = 0;
    uint32_t capture_start;
    uint8_t  sd_open = 0;
    uint32_t last_load;
    uint32_t load_writes = 0;

    CP(10);
    memset(out, 0, sizeof(*out));
    out->baud = baud;
    out->duration_ms = duration_ms;
    out->load = load;

    /* Ensure logdb is off UART4 (it shares UART4 in ESP32 CDC mode). */
    prev_cdc = m1_usbcdc_mode;
    if (prev_cdc == CDC_MODE_ESP32)
    {
        restore_cdc = 1;
        m1_logdb_deinit();
        m1_usbcdc_mode = CDC_MODE_LOG_CLI;
        m1_logdb_init();
    }

    /* m1_esp32_get_init_status() alone is not a reliable "is the ESP32 ready"
     * signal -- see cap_esp32_link_ready()'s own comment below: after the
     * GPIO USB-UART bridge (ESP32RUN) or the ESP32 FW-update path is used
     * and exited, the EN pin can be left low while this software flag is
     * still true, so this naive check skips re-init and the subsequent
     * CAPTEST ACK wait times out against an ESP32 that was never actually
     * brought back up. cap_esp32_link_ready() checks the EN pin too, does a
     * clean deinit-before-reinit when needed, and confirms a real ">> "
     * prompt with retries -- the same shared bring-up m1_capture_stream_run()
     * and the Capture Network scan glue already use. This soak driver was
     * the one remaining caller still hand-rolling the weaker check. */
    if (!cap_esp32_link_ready())
    {
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }

    /* Wake the shell and ensure Wi-Fi mode (captest is a WIFI-mode command). */
    (void)esp32_uart_write((const uint8_t *)"\r\n", 2, 100);
    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 800, ">> ");
    (void)esp32_uart_write((const uint8_t *)"mode -w\r\n", 9, 100);
    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 1200, ">> ");

    /* Trigger the ESP soak endpoint and wait for its ACK at the console rate. */
    m1_ringbuffer_reset(&esp32_rb_hdl);
    snprintf(cmd, sizeof(cmd), "captest %lu %lu\r\n", (unsigned long)baud, (unsigned long)duration_ms);
    (void)esp32_uart_write((const uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
    if (esp32_uart_read_until_prompt(resp, sizeof(resp), 1500, "CAPTEST ACK") > 0 &&
        strstr(resp, "CAPTEST ACK") != NULL)
    {
        out->got_ack = true;
    }
    if (!out->got_ack)
    {
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }

    CP(15); /* ACK received */

    /* Switch to capture mode: disable interrupt RX, raise baud, start DMA RX.
     * Also mask the UART4 NVIC IRQ: HAL_UART_Receive_DMA re-enables the UART
     * error interrupt, which would otherwise vector into the byte-at-a-time
     * console ISR mid-DMA. Errors are polled here instead (cap_poll_errors). */
    CP(20);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_RXFNE);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_ORE);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_ERR);
    HAL_NVIC_DisableIRQ(UART4_IRQn);
    esp32_UART_change_baudrate(baud);

    if (cap_dma_start() != 0)
    {
        esp32_UART_change_baudrate(ESP32_UART_BAUDRATE);
        HAL_NVIC_EnableIRQ(UART4_IRQn);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_RXFNE);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ORE);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ERR);
        m1_ringbuffer_reset(&esp32_rb_hdl);
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }

    m1_cap_rx_init(&rx);
    m1_cap_seq_init(&seq);

    /* Binary sync: wait for the ESP's CAP_READY at the new baud before granting
     * credit. This proves the link is aligned at the target rate; if it never
     * arrives, the baud/link is bad -> abort cleanly (the counters show why:
     * blocks_seen with no got_ready => garbage/baud; got_ready => link good). */
    CP(35);
    {
        uint32_t ready_start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - ready_start) < 1000u && !out->got_ready)
        {
            cap_drain(&tail, &rx, &seq, out);
            cap_poll_errors(out);
            m1_wdt_kick();
        }
    }
    if (!out->got_ready)
    {
        goto restore; /* no binary sync at this baud */
    }

    CP(36);
    granted += cap_grant(CAP_GRANT_CHUNK);
    out->grants++;

    /* Combined-load variant: representative buffered SD writes + display
     * refresh, interleaved with the transport, so the soak stresses the real
     * MonstaShark mix (UART DMA + framing + SD + UI). */
    if (load)
    {
        memset(s_load_buf, 0xA5, sizeof(s_load_buf));
        if (f_open(&lf, "0:/captest.tmp", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) { sd_open = 1; }
    }
    last_load = HAL_GetTick();

    CP(40); /* entering main soak loop */
    {
        uint32_t last_service = HAL_GetTick();
        capture_start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - capture_start) < duration_ms)
        {
        cap_drain(&tail, &rx, &seq, out);
        cap_poll_errors(out);
        if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
        {
            granted += cap_grant(CAP_GRANT_CHUNK);
            out->grants++;
        }
        /* This is a tight, no-block loop at normal priority; without this it
         * would starve the low-priority IWDG task -> watchdog reset. Feed the
         * IWDG and briefly yield so lower-priority tasks (WDT, system periodic)
         * can run. The DMA keeps filling the ring during the yield. */
        if ((HAL_GetTick() - last_service) >= 50u)
        {
            last_service = HAL_GetTick();
            m1_wdt_kick();
            osDelay(1);
        }
        if (load && ((HAL_GetTick() - last_load) >= 40u))
        {
            last_load = HAL_GetTick();
            if (sd_open)
            {
                UINT bw = 0;
                uint32_t off;

                /* Keep the UART DMA ring serviced while exercising SD I/O.
                 * A full 4 KiB FatFs write can block long enough for the
                 * 4 KiB UART ring to lap under the combined-load soak. Split
                 * the representative write into bounded pieces and service
                 * both RX and credit flow on either side of each one. */
                for (off = 0u; off < sizeof(s_load_buf); off += 1024u)
                {
                    UINT chunk = (UINT)(((sizeof(s_load_buf) - off) > 1024u)
                                         ? 1024u : (sizeof(s_load_buf) - off));

                    cap_drain(&tail, &rx, &seq, out);
                    cap_poll_errors(out);
                    if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
                    {
                        granted += cap_grant(CAP_GRANT_CHUNK);
                        out->grants++;
                    }

                    (void)f_write(&lf, &s_load_buf[off], chunk, &bw);

                    cap_drain(&tail, &rx, &seq, out);
                    cap_poll_errors(out);
                    if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
                    {
                        granted += cap_grant(CAP_GRANT_CHUNK);
                        out->grants++;
                    }
                }
                load_writes++;
                if ((load_writes & 0x0Fu) == 0u)
                {
                    /* f_sync() itself is indivisible, so service the stream
                     * immediately before and after the periodic flush. */
                    cap_drain(&tail, &rx, &seq, out);
                    cap_poll_errors(out);
                    if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
                    {
                        granted += cap_grant(CAP_GRANT_CHUNK);
                        out->grants++;
                    }
                    (void)f_sync(&lf);
                    cap_drain(&tail, &rx, &seq, out);
                    cap_poll_errors(out);
                    if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
                    {
                        granted += cap_grant(CAP_GRANT_CHUNK);
                        out->grants++;
                    }
                }
            }
            /* Representative UI activity: a full 128x64 SPI refresh. */
            cap_drain(&tail, &rx, &seq, out);
            cap_poll_errors(out);
            if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
            {
                granted += cap_grant(CAP_GRANT_CHUNK);
                out->grants++;
            }
            m1_u8g2_firstpage();
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, 20, "Soak + SD/UI load");
            {
                char pl[24];
                snprintf(pl, sizeof(pl), "rx %lu", (unsigned long)out->frames_ok);
                u8g2_DrawStr(&m1_u8g2, 2, 40, pl);
            }
            m1_u8g2_nextpage();
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            cap_drain(&tail, &rx, &seq, out);
            cap_poll_errors(out);
            if ((granted - (uint32_t)out->bytes_rx) < CAP_GRANT_LOW)
            {
                granted += cap_grant(CAP_GRANT_CHUNK);
                out->grants++;
            }
        }
        } /* while */
    } /* soak-loop scope */

    if (load && sd_open)
    {
        (void)f_close(&lf);
        (void)f_unlink("0:/captest.tmp");
    }

    /* Ask the ESP to stop and drain until CAP_STOPPED (bounded). */
    CP(50);
    cap_send_stop();
    {
        uint32_t drain_start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - drain_start) < 400u && !out->got_stopped)
        {
            cap_drain(&tail, &rx, &seq, out);
            cap_poll_errors(out);
            m1_wdt_kick();
        }
    }

    out->seq_gaps = seq.gaps;
    out->seq_dups = seq.dups;
    out->rx_overruns = rx.dropped_frames;

restore:
    /* Restore the ordinary console link. */
    CP(60);
    cap_dma_stop();
    esp32_UART_change_baudrate(ESP32_UART_BAUDRATE);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_RXFNE);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ORE);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ERR);
    m1_ringbuffer_reset(&esp32_rb_hdl);
    CP(61);

    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 800, "CAPTEST DONE");

    if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
    {
        m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
    }
    CP(64);
    return true;
}

/*********************** L I V E   C A P T U R E   S T R E A M ****************/

#define CAP_STREAM_BAUD    3000000u   /* must match the ESP PCAP_START baud   */
#define CAP_PCAP_BUF       8192u      /* SD flush staging buffer              */
#define CAP_PCAP_FLUSH_AT  (CAP_PCAP_BUF - 2600u) /* leave room for one EPB   */

/* SD flush staging + EPB scratch, kept static (single capture session). */
static uint8_t s_pcap_buf[CAP_PCAP_BUF];
static uint8_t s_pcap_scratch[M1_CAPTURE_PCAP_SCRATCH_MIN];

typedef struct {
    m1_pcapng_writer_t          w;
    FIL                        *fp;
    uint8_t                     file_open;
    uint32_t                    iface_id;
    m1_capture_pcap_counts_t    pc;
    m1_cap_seq_t                seq;
    m1_capture_stream_result_t *res;
} cap_stream_ctx_t;

/* HW-error poll for the stream result struct (mirrors cap_poll_errors). */
static void cap_poll_errors_stream(m1_capture_stream_result_t *out)
{
    cap_poll_error_counts(&out->uart_ore, &out->dma_errors);
}

/* Write the staged pcapng bytes to SD and reset the writer. Only ever called at
 * a block boundary, so the file stays a valid pcapng even if a capture aborts. */
static int cap_pcap_flush(cap_stream_ctx_t *c)
{
    UINT bw = 0;
    if (c->w.len == 0u) { return 0; }
    if (!c->file_open) { return -1; }
    if (f_write(c->fp, c->w.buf, (UINT)c->w.len, &bw) != FR_OK || bw != c->w.len)
    {
        return -1;
    }
    c->res->cap_bytes += c->w.len;
    m1_pcapng_writer_init(&c->w, s_pcap_buf, CAP_PCAP_BUF);
    return 0;
}

/* Drain the DMA ring, decoding CAP_FRAME_BATCH envelopes into pcapng EPBs on SD.
 * Also tracks CAP_READY (binary sync) and CAP_STOPPED (ESP final counters). */
static void cap_drain_stream(uint32_t *tail, m1_cap_rx_stream_t *rx, cap_stream_ctx_t *c)
{
    static uint8_t block[M1_CAP_MAX_WIRE];
    static uint8_t dbuf[M1_CAP_MAX_ENVELOPE];
    m1_capture_stream_result_t *out = c->res;
    uint32_t head = cap_dma_head();

    {
        uint32_t backlog = (head + CAPBUF_SIZE - *tail) % CAPBUF_SIZE;
        if (backlog > out->max_backlog) { out->max_backlog = backlog; }
    }

    while (*tail != head)
    {
        uint8_t b = s_capbuf[*tail];
        size_t bl;
        m1_cap_msg_t msg;
        m1_cap_status_t st;

        *tail = (*tail + 1u) % CAPBUF_SIZE;
        bl = m1_cap_rx_push(rx, b, block, sizeof(block));
        if (bl == 0u) { continue; }

        st = m1_cap_frame_decode(block, bl, dbuf, sizeof(dbuf), &msg);
        if (st == M1_CAP_ERR_CRC) { out->crc_errors++; continue; }
        if (st != M1_CAP_OK)      { out->format_errors++; continue; }

        if (msg.msg_type == M1_CAP_FRAME_BATCH)
        {
            out->batches++;
            out->wire_rx += bl;
            (void)m1_cap_seq_observe(&c->seq, msg.sequence);
            /* Flush before appending if the next EPBs might not fit. */
            if (c->w.len >= CAP_PCAP_FLUSH_AT)
            {
                if (cap_pcap_flush(c) != 0) { out->sd_ok = false; }
            }
            (void)m1_capture_pcap_batch(&c->w, c->iface_id, msg.payload, msg.payload_len,
                                        s_pcap_scratch, sizeof(s_pcap_scratch), &c->pc);
        }
        else if (msg.msg_type == M1_CAP_READY)
        {
            out->got_ready = true;
        }
        else if (msg.msg_type == M1_CAP_STOPPED)
        {
            out->got_stopped = true;
            /* ESP stream CAP_STOPPED payload: seq,batches,bytes,capframes,drops,... */
            if (msg.payload_len >= 16u && msg.payload != NULL)
            {
                out->esp_frames = (uint32_t)msg.payload[12] | ((uint32_t)msg.payload[13] << 8) |
                                  ((uint32_t)msg.payload[14] << 16) | ((uint32_t)msg.payload[15] << 24);
            }
            if (msg.payload_len >= 20u && msg.payload != NULL)
            {
                out->esp_drops = (uint32_t)msg.payload[16] | ((uint32_t)msg.payload[17] << 8) |
                                 ((uint32_t)msg.payload[18] << 16) | ((uint32_t)msg.payload[19] << 24);
            }
        }
    }
}

/*
 * Proven ESP32 link bring-up, ported from m1_wifi.c's own scan/command entry
 * guard (e.g. its beacon/advertise prelude around line 1457) rather than
 * reinvented: checks BOTH the software init flag AND the physical ESP32_EN
 * pin. Per that code's own comment -- "After bridge mode exit, EN can be low
 * while init flag is still true, which makes scan commands fail with prompt
 * timeout" -- m1_esp32_get_init_status() alone is not a reliable signal that
 * the ESP32 will actually respond. Does a clean deinit-before-reinit and
 * retries up to 3 times with escalating timeouts, confirming a real ">> "
 * prompt each try rather than assuming success after m1_esp32_init().
 *
 * Both m1_capture_stream_run() and the Capture Network scan glue previously
 * only checked the software flag once, with no EN-pin check and no retry --
 * the same class of bug m1_wifi.c's comment already documents. This closes
 * that gap for both by sharing one bring-up path instead of each hand-rolling
 * a weaker version of it.
 */
static bool cap_esp32_link_ready(void)
{
    uint8_t need_reinit = 0u;
    uint8_t ready_try;
    uint32_t prompt_timeout_ms;
    char resp[96];

    if (!m1_esp32_get_init_status()) { need_reinit = 1u; }
    if (HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin) == GPIO_PIN_RESET) { need_reinit = 1u; }

    if (need_reinit)
    {
        if (m1_esp32_get_init_status()) { m1_esp32_deinit(); }
        m1_esp32_init();
    }

    for (ready_try = 0u; ready_try < 3u; ready_try++)
    {
        prompt_timeout_ms = need_reinit ? (1200u + (uint32_t)ready_try * 700u)
                                         : (300u + (uint32_t)ready_try * 400u);

        memset(resp, 0, sizeof(resp));
        (void)esp32_uart_write((const uint8_t *)"\r\n", 2, 100);
        if ((esp32_uart_read_until_prompt(resp, sizeof(resp), prompt_timeout_ms, ">> ") > 0u) &&
            (strstr(resp, ">> ") != NULL))
        {
            return true;
        }

        m1_esp32_deinit();
        osDelay(20);
        m1_esp32_init();
        need_reinit = 1u;
        osDelay(150);
    }

    return false;
}

/* Outcome of a cap_pick_path() search. */
typedef enum {
    CAP_PICK_OK = 0,     /* path holds a confirmed-free name                */
    CAP_PICK_EXHAUSTED,  /* all 1000 names in use; no name available        */
    CAP_PICK_SD_ERROR,   /* directory could not be ensured, or f_stat()     */
                         /* returned a real error rather than "missing"     */
} cap_pick_result_t;

/* Choose the next free 0:/wifi/mcap_NNN.pcapng path. New captures are never
 * placed at the SD root and never in a separate MonstaShark folder; legacy
 * root-level 0:/mcap_NNN.pcapng files from earlier firmware are left
 * completely untouched and remain browsable via the generic file browser.
 * Only FR_NO_FILE/FR_NO_PATH count as "name available" -- any other
 * f_stat() outcome is a real SD error, not a free slot. Never falls back to
 * overwriting mcap_000.pcapng; returns CAP_PICK_EXHAUSTED instead. */
static cap_pick_result_t cap_pick_path(char *path, size_t cap)
{
    FILINFO fi;
    FRESULT fr;
    unsigned i;

    /* Lazy self-healing: recreate 0:/wifi if it was removed after boot. */
    if (fs_directory_ensure(M1_SD_DIR_WIFI) != FR_OK)
    {
        return CAP_PICK_SD_ERROR;
    }

    for (i = 0; i < 1000u; i++)
    {
        snprintf(path, cap, M1_SD_DIR_WIFI "/mcap_%03u.pcapng", i);
        fr = f_stat(path, &fi);
        if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH)) { return CAP_PICK_OK; }  /* free slot */
        if (fr != FR_OK) { return CAP_PICK_SD_ERROR; }  /* real SD error */
        /* fr == FR_OK: name already taken, try the next one */
    }
    return CAP_PICK_EXHAUSTED;
}

/* Live capture progress screen. Title + channel and an m:ss/m:ss countdown on
 * the top row, a progress bar in the middle, and a footer carrying live
 * "<frames>fr <KB>KB" (lower-left) plus "Stop" + CENTER(OK) icon (lower-right).
 * Pressing CENTER ends the capture early; BACK is ignored on this screen. */
static void capnet_draw_capturing(uint8_t channel, uint32_t elapsed_ms,
                                  uint32_t total_ms, uint32_t frames,
                                  uint32_t bytes)
{
    char l[26];
    char foot[24];
    uint32_t es;
    uint32_t ts = total_ms / 1000u;
    u8g2_uint_t w;
    u8g2_uint_t fillw;

    if (elapsed_ms > total_ms) { elapsed_ms = total_ms; }
    es = elapsed_ms / 1000u;
    fillw = (total_ms > 0u)
            ? (u8g2_uint_t)(((uint32_t)102 * elapsed_ms) / total_ms) : 0u;

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    snprintf(l, sizeof(l), "Capturing ch %u", (unsigned)channel);
    u8g2_DrawStr(&m1_u8g2, 2, 10, l);
    snprintf(l, sizeof(l), "%lu:%02lu/%lu:%02lu",
             (unsigned long)(es / 60u), (unsigned long)(es % 60u),
             (unsigned long)(ts / 60u), (unsigned long)(ts % 60u));
    w = (u8g2_uint_t)u8g2_GetStrWidth(&m1_u8g2, l);
    u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 2 - w), 10, l);

    u8g2_DrawFrame(&m1_u8g2, 12, 30, 104, 12);
    if (fillw > 0u) { u8g2_DrawBox(&m1_u8g2, 13, 31, fillw, 10); }

    snprintf(foot, sizeof(foot), "%lufr %luKB",
             (unsigned long)frames, (unsigned long)(bytes / 1024u));
    /* Footer styled like the Sub-GHz scan Stop bar (CENTER icon then "Stop"),
     * but in the NFC/black-bar font. Live counters sit lower-left. */
    {
        u8g2_uint_t sw;
        u8g2_uint_t tx;
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_DrawBox(&m1_u8g2, 0, 52, M1_LCD_DISPLAY_WIDTH, 12);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);   /* NFC bar font */
        u8g2_DrawStr(&m1_u8g2, 2, 61, foot);
        sw = (u8g2_uint_t)u8g2_GetStrWidth(&m1_u8g2, "Stop");
        tx = (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 2 - sw);
        u8g2_DrawXBMP(&m1_u8g2, (u8g2_uint_t)(tx - 12), 52, 10, 10, target_10x10);
        u8g2_DrawStr(&m1_u8g2, tx, 61, "Stop");
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    }
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

bool m1_capture_stream_run(uint8_t channel, uint32_t duration_ms,
                           m1_capture_stream_result_t *out)
{
    enCdcMode prev_cdc;
    uint8_t restore_cdc = 0;
    char cmd[48];
    char resp[160];
    static m1_cap_rx_stream_t rx;
    static FIL cf;
    cap_stream_ctx_t ctx;
    uint32_t tail = 0;
    uint32_t granted = 0;

    CP(10);
    memset(out, 0, sizeof(*out));
    out->channel = channel;
    out->duration_ms = duration_ms;
    out->sd_ok = true;

    prev_cdc = m1_usbcdc_mode;
    if (prev_cdc == CDC_MODE_ESP32)
    {
        restore_cdc = 1;
        m1_logdb_deinit();
        m1_usbcdc_mode = CDC_MODE_LOG_CLI;
        m1_logdb_init();
    }

    if (!cap_esp32_link_ready())
    {
        /* ESP32 never answered a prompt after retrying the full bring-up
         * (software flag + EN pin + reinit) -- no link, don't even attempt
         * PCAP_START. Same failure shape as a busy-radio no-ack below. */
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }

    (void)esp32_uart_write((const uint8_t *)"mode -w\r\n", 9, 100);
    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 1200, ">> ");

    m1_ringbuffer_reset(&esp32_rb_hdl);
    /* Give the ESP a longer safety timeout than the STM's own capture window so
     * the STM (t_end = duration_ms) always reaches its stop first, sends
     * CAP_STOP, and the ESP replies CAP_STOPPED while both sides are still at the
     * capture baud -- otherwise the ESP self-terminates on an equal timer, sends
     * CAP_STOPPED, and reverts to console baud before the STM's CAP_STOP, so the
     * STM misses CAP_STOPPED (no-stop). The ESP timer stays a genuine safety net
     * (STM dies -> ESP still self-terminates ~5s later). */
    snprintf(cmd, sizeof(cmd), "PCAP_START %u %lu\r\n",
             (unsigned)channel, (unsigned long)(duration_ms + 5000u));
    (void)esp32_uart_write((const uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
    if (esp32_uart_read_until_prompt(resp, sizeof(resp), 1500, "PCAP ACK") > 0 &&
        strstr(resp, "PCAP ACK") != NULL)
    {
        out->got_ack = true;
    }
    if (!out->got_ack)
    {
        /* Distinguish "BLE currently owns the radio" (ESP's wifi_take_radio()
         * gate declined before wifi_capture_run_stream() ever printed
         * "PCAP ACK", so this wait just times out on the busy text instead)
         * from a generic no-ack, so the caller can offer a reason-specific
         * retry instead of a dead-end failure screen. */
        out->radio_busy = (strstr(resp, "Radio busy") != NULL);
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }
    CP(15);

    CP(20);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_RXFNE);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_ORE);
    __HAL_UART_DISABLE_IT(&huart_esp, UART_IT_ERR);
    HAL_NVIC_DisableIRQ(UART4_IRQn);
    esp32_UART_change_baudrate(CAP_STREAM_BAUD);

    if (cap_dma_start() != 0)
    {
        esp32_UART_change_baudrate(ESP32_UART_BAUDRATE);
        HAL_NVIC_EnableIRQ(UART4_IRQn);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_RXFNE);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ORE);
        __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ERR);
        m1_ringbuffer_reset(&esp32_rb_hdl);
        if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
        {
            m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
        }
        return false;
    }

    m1_cap_rx_init(&rx);
    memset(&ctx, 0, sizeof(ctx));
    ctx.fp = &cf;
    ctx.iface_id = 0;
    ctx.res = out;
    m1_cap_seq_init(&ctx.seq);
    m1_pcapng_writer_init(&ctx.w, s_pcap_buf, CAP_PCAP_BUF);

    /* Binary sync: wait for CAP_READY at the capture baud before crediting. */
    CP(35);
    {
        uint32_t ready_start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - ready_start) < 1000u && !out->got_ready)
        {
            cap_drain_stream(&tail, &rx, &ctx);
            cap_poll_errors_stream(out);
            m1_wdt_kick();
        }
    }
    if (!out->got_ready) { goto restore; }

    /* Open the pcapng file and write SHB + IDB before any credit is granted. */
    out->path[0] = '\0';
    {
        cap_pick_result_t pick = CAP_PICK_SD_ERROR;
        FRESULT open_fr = FR_DISK_ERR;
        unsigned open_tries;

        for (open_tries = 0; open_tries < 3u; open_tries++)
        {
            pick = cap_pick_path(out->path, sizeof(out->path));
            if (pick != CAP_PICK_OK) { break; }

            /* FA_CREATE_NEW: exclusive create, fails FR_EXIST if this name
             * was taken by another writer between the f_stat() check inside
             * cap_pick_path() and this open -- retry the next free name
             * rather than overwrite it. */
            open_fr = f_open(&cf, out->path, FA_CREATE_NEW | FA_WRITE);
            if (open_fr == FR_OK) { break; }
            if (open_fr != FR_EXIST) { pick = CAP_PICK_SD_ERROR; break; }
        }

        if ((pick == CAP_PICK_OK) && (open_fr == FR_OK))
        {
            m1_pcapng_section_info_t si = {
                "MonstaTek M1 / ESP32-C6", "MonstaTek M1 Wi-Fi Capture", "MonstaShark", NULL
            };
            m1_pcapng_iface_info_t ii = { 0, "esp32c6-monitor", NULL, 6 };
            ctx.file_open = 1;
            if (m1_pcapng_write_shb(&ctx.w, &si) != 0) { out->sd_ok = false; }
            if (m1_pcapng_write_idb(&ctx.w, &ii) != 0) { out->sd_ok = false; }
            if (cap_pcap_flush(&ctx) != 0) { out->sd_ok = false; }
        }
        else
        {
            out->path[0] = '\0';
            out->sd_ok = false;
        }
    }

    CP(36);
    granted += cap_grant(CAP_GRANT_CHUNK);

    CP(40);
    {
        uint32_t now = HAL_GetTick();
        uint32_t last_service = now;
        uint32_t t_start = now;
        uint32_t last_draw = now - 1000u; /* force an immediate first draw */
        S_M1_Main_Q_t q;
        S_M1_Buttons_Status btn;

        while ((uint32_t)(HAL_GetTick() - t_start) < duration_ms && !out->got_stopped)
        {
            cap_drain_stream(&tail, &rx, &ctx);
            cap_poll_errors_stream(out);
            if ((granted - (uint32_t)out->wire_rx) < CAP_GRANT_LOW)
            {
                granted += cap_grant(CAP_GRANT_CHUNK);
            }
            /* CENTER/OK ends the capture early; the clean-stop path below
             * (cap_send_stop + drain) then runs exactly as on timer expiry.
             * Non-blocking poll -- BACK is intentionally ignored here. */
            if (xQueueReceive(main_q_hdl, &q, 0) == pdTRUE)
            {
                if (q.q_evt_type == Q_EVENT_KEYPAD &&
                    xQueueReceive(button_events_q_hdl, &btn, 0) == pdTRUE &&
                    btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    break;
                }
            }
            now = HAL_GetTick();
            if ((now - last_service) >= 50u)
            {
                last_service = now;
                m1_wdt_kick();
                osDelay(1);
            }
            /* ~4x/sec progress redraw (UI only; transport untouched). */
            if ((now - last_draw) >= 250u)
            {
                last_draw = now;
                capnet_draw_capturing(channel, now - t_start, duration_ms,
                                      ctx.pc.frames, (uint32_t)out->wire_rx);
            }
        }
    }
    xQueueReset(main_q_hdl); /* flush any keypresses made during capture */

    /* Ask the ESP to stop and drain until CAP_STOPPED (bounded). */
    CP(50);
    cap_send_stop();
    {
        uint32_t drain_start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - drain_start) < 400u && !out->got_stopped)
        {
            cap_drain_stream(&tail, &rx, &ctx);
            cap_poll_errors_stream(out);
            m1_wdt_kick();
        }
    }

    /* Interface Statistics Block, final flush, close. */
    if (ctx.file_open)
    {
        m1_pcapng_stats_t stx;
        memset(&stx, 0, sizeof(stx));
        stx.has_ifrecv = true;   stx.isb_ifrecv = out->esp_frames;
        stx.has_ifdrop = true;   stx.isb_ifdrop = out->esp_drops;
        stx.has_usrdeliv = true; stx.isb_usrdeliv = ctx.pc.frames;
        if (m1_pcapng_write_isb(&ctx.w, ctx.iface_id, 0, &stx) != 0) { out->sd_ok = false; }
        if (cap_pcap_flush(&ctx) != 0) { out->sd_ok = false; }
        if (f_sync(&cf) != FR_OK) { out->sd_ok = false; }
        if (f_close(&cf) != FR_OK) { out->sd_ok = false; }
    }

    out->frames = ctx.pc.frames;
    out->decode_errors = ctx.pc.decode_errors;
    out->seq_gaps = ctx.seq.gaps;
    out->rx_overruns = rx.dropped_frames;

restore:
    CP(60);
    if (!out->got_ready && ctx.file_open)
    {
        (void)f_close(&cf); /* opened but sync never established (defensive) */
    }
    cap_dma_stop();
    esp32_UART_change_baudrate(ESP32_UART_BAUDRATE);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_RXFNE);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ORE);
    __HAL_UART_ENABLE_IT(&huart_esp, UART_IT_ERR);
    m1_ringbuffer_reset(&esp32_rb_hdl);
    CP(61);

    (void)esp32_uart_read_until_prompt(resp, sizeof(resp), 800, "PCAP DONE");

    if (restore_cdc && (m1_usbcdc_mode != prev_cdc))
    {
        m1_logdb_deinit(); m1_usbcdc_mode = prev_cdc; m1_logdb_init();
    }
    CP(64);
    return true;
}

/*************************** U I   S C R E E N ********************************/

static void cap_draw_select(uint32_t baud, uint8_t load)
{
    char l[26];
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 10, "Capture Test (Gate 0)");
    snprintf(l, sizeof(l), "Baud: %lu", (unsigned long)baud);
    u8g2_DrawStr(&m1_u8g2, 2, 24, l);
    snprintf(l, sizeof(l), "Load: %s (SD+UI)", load ? "ON" : "off");
    u8g2_DrawStr(&m1_u8g2, 2, 36, l);
    u8g2_DrawStr(&m1_u8g2, 2, 48, "L/R baud  U/D load");
    u8g2_DrawStr(&m1_u8g2, 2, 58, "OK Run");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

/* Two-page result view. page 0 = throughput/integrity, page 1 = overruns/hw. */
static void cap_draw_result(const m1_capture_soak_result_t *r, uint8_t page)
{
    char l[26];
    uint32_t kbps = (r->duration_ms > 0u)
                    ? (uint32_t)((r->bytes_rx * 1000u) / (r->duration_ms * 1024u)) : 0u;
    long loss = (long)r->esp_frames - (long)r->frames_ok;

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    if (!r->got_ack)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 11, "Capture link failed");
        u8g2_DrawStr(&m1_u8g2, 2, 28, "No ACK / rate-switch");
            }
    else if (page == 0u)
    {
        snprintf(l, sizeof(l), "%lu Bd %luKB/s%s", (unsigned long)r->baud,
                 (unsigned long)kbps, r->load ? " L" : "");
        u8g2_DrawStr(&m1_u8g2, 2, 10, l);
        snprintf(l, sizeof(l), "tot %luKB  rx %lu",
                 (unsigned long)(r->bytes_rx / 1024u), (unsigned long)r->frames_ok);
        u8g2_DrawStr(&m1_u8g2, 2, 22, l);
        snprintf(l, sizeof(l), "crc %lu  gap %lu", (unsigned long)r->crc_errors, (unsigned long)r->seq_gaps);
        u8g2_DrawStr(&m1_u8g2, 2, 34, l);
        snprintf(l, sizeof(l), "dup %lu  fmt %lu", (unsigned long)r->seq_dups, (unsigned long)r->format_errors);
        u8g2_DrawStr(&m1_u8g2, 2, 46, l);
        u8g2_DrawStr(&m1_u8g2, 2, 58, "1/2  L/R page");
    }
    else
    {
        snprintf(l, sizeof(l), "RDY:%s  blk %lu", r->got_ready ? "Y" : "N", (unsigned long)r->blocks_seen);
        u8g2_DrawStr(&m1_u8g2, 2, 8, l);
        snprintf(l, sizeof(l), "esg %lu  esp %lu", (unsigned long)r->esp_grants, (unsigned long)r->esp_frames);
        u8g2_DrawStr(&m1_u8g2, 2, 18, l);
        snprintf(l, sizeof(l), "str %lu  ovr %lu", (unsigned long)r->esp_starved, (unsigned long)r->rx_overruns);
        u8g2_DrawStr(&m1_u8g2, 2, 28, l);
        snprintf(l, sizeof(l), "ore %lu dma %lu bk%lu",
                 (unsigned long)r->uart_ore, (unsigned long)r->dma_errors, (unsigned long)r->max_backlog);
        u8g2_DrawStr(&m1_u8g2, 2, 38, l);
        snprintf(l, sizeof(l), "loss %ld  %s", loss, r->got_stopped ? "stop-ok" : "no-stop");
        u8g2_DrawStr(&m1_u8g2, 2, 48, l);
        u8g2_DrawStr(&m1_u8g2, 2, 58, "2/2  L/R page");
    }
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

void m1_capture_test_screen(void)
{
    static const uint32_t bauds[4] = { 2000000u, 3000000u, 4000000u, 5000000u };
    uint8_t sel = 1;   /* default 3 Mbaud */
    uint8_t load = 0;  /* SD+UI load off by default */
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;

    CP(5); /* in Capture Test selector (before any soak) */
    cap_draw_select(bauds[sel], load);
    for (;;)
    {
        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return; }
        else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel == 0u) ? 3u : (uint8_t)(sel - 1u);
            cap_draw_select(bauds[sel], load);
        }
        else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (uint8_t)((sel + 1u) & 3u);
            cap_draw_select(bauds[sel], load);
        }
        else if ((btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) ||
                 (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
        {
            load = (uint8_t)(load ? 0u : 1u);
            cap_draw_select(bauds[sel], load);
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            m1_capture_soak_result_t res;
            uint8_t page = 0;

            m1_u8g2_firstpage();
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
            u8g2_DrawStr(&m1_u8g2, 2, 30, load ? "Soak + SD/UI..." : "Running soak...");
            m1_u8g2_nextpage();
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

            (void)m1_capture_soak_run(bauds[sel], CAP_SOAK_MS, load ? true : false, &res);
            xQueueReset(main_q_hdl);

            CP(70); /* soak returned; drawing result */
            cap_draw_result(&res, page);
            /* LEFT/RIGHT flip result pages; BACK returns to the selector. */
            for (;;)
            {
                if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
                if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
                (void)xQueueReceive(button_events_q_hdl, &btn, 0);
                if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { break; }
                if ((btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) ||
                    (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK))
                {
                    page = (uint8_t)(page ? 0u : 1u);
                    cap_draw_result(&res, page);
                }
            }
            cap_draw_select(bauds[sel], load);
        }
    }
}

/*********************** A P   S C A N   ( o n e   p e r   w o r k f l o w ) **/

#define CAP_SCAN_CMD             "scan -a\r\n"
#define CAP_SCAN_REPLY_BUF       4096u  /* matches m1_wifi.c's ESP32UART_BUFFER_SIZE */
#define CAP_SCAN_CMD_TIMEOUT_MS  10000u /* matches m1_wifi.c's MAX_SCAN_CMD_TIMEOUT_MS */

typedef enum {
    M1_CAP_SCAN_OK = 0,
    M1_CAP_SCAN_RADIO_BUSY,  /* ESP declined: BLE currently owns the radio */
    M1_CAP_SCAN_NO_REPLY,    /* UART write failed, or the ">> " prompt never came back */
} m1_cap_scan_status_t;

/* Reply text lands here, not on the menu task's stack -- same reasoning as
 * the wi007 fix (f41b224): keep multi-KB scratch off subfunc_handler_task's
 * frame. Scoped to this module, overwritten on every scan; not read outside
 * a scan call, so it carries no state between screen visits. */
static char s_cap_scan_reply[CAP_SCAN_REPLY_BUF];

/* Selected-AP context (m1_wifi_session_cache.c) -- capture-only, per the
 * header: Networks/Handshake/Deauth Client have their own existing
 * wifitarget/wifi_target_storage selection (m1_wifi.c) and don't use this.
 * The AP-LIST cache itself, by contrast, IS shared with those tools now --
 * see wifi_session_cache_get() below, the one canonical instance every
 * consumer (this file and m1_wifi.c) reads/writes. */
static wifi_session_selected_t s_wifi_selected;
static bool s_wifi_selected_inited = false;

/* Scratch for reading a cache snapshot back out -- static, not a stack
 * local, same reasoning as s_cap_scan_reply above: WIFI_SESSION_CACHE_MAX_APS
 * (48) * sizeof(wifi_session_ap_t) is a few KB, too much for
 * subfunc_handler_task's own stack (see the wi007 fix, f41b224). Scoped to
 * this module, overwritten on every cache read; not read outside that one
 * call, so it carries no state between screen visits. */
static wifi_session_ap_t s_cap_wifi_snap[WIFI_SESSION_CACHE_MAX_APS];

static void cap_wifi_cache_ensure_init(void)
{
    (void)wifi_session_cache_get(); /* lazy-inits the canonical instance */
    if (!s_wifi_selected_inited)
    {
        wifi_session_selected_clear(&s_wifi_selected);
        s_wifi_selected_inited = true;
    }
}

/* Coarse open/secured signal only -- the ESP's auth string ("OPEN", "WPA2",
 * "WPA/WPA2", ...) doesn't need the full WIFI_AUTH_* enum here. That mapping
 * (auth_string_to_mode()) is file-static to the frozen m1_wifi.c and out of
 * scope to touch or export; wifi_session_ap_t.encryption_mode is deliberately
 * just a plain int (see m1_wifi_session_cache.h), not tied to that enum. */
static int cap_auth_str_to_enc_mode(const char *auth)
{
    return (strcmp(auth, "OPEN") == 0) ? 0 : 1;
}

/* Converts a parsed scan result into the session cache's AP shape, clamped
 * to the cache's own bound (WIFI_SESSION_CACHE_MAX_APS != M1_CAP_SCAN_MAX_APS,
 * so this can't be a memcpy). */
static uint16_t cap_scan_to_session_aps(const m1_cap_scan_result_t *scan,
                                        wifi_session_ap_t *out, uint16_t out_cap)
{
    uint16_t n = (uint16_t)scan->count;
    uint16_t i;

    if (n > out_cap) { n = out_cap; }
    for (i = 0; i < n; i++)
    {
        const m1_cap_scan_ap_t *ap = &scan->aps[i];
        memset(&out[i], 0, sizeof(out[i]));
        strncpy((char *)out[i].ssid, ap->ssid, sizeof(out[i].ssid) - 1u);
        out[i].ssid[sizeof(out[i].ssid) - 1u] = '\0';
        strncpy((char *)out[i].bssid, ap->bssid, sizeof(out[i].bssid) - 1u);
        out[i].bssid[sizeof(out[i].bssid) - 1u] = '\0';
        out[i].rssi = ap->rssi;
        out[i].channel = ap->channel;
        out[i].encryption_mode = cap_auth_str_to_enc_mode(ap->auth);
    }
    return n;
}

/*
 * Reverse of cap_scan_to_session_aps(): rebuilds an m1_cap_scan_result_t from
 * a cache snapshot, so the AP-pick/manual-entry code below (all written
 * against m1_cap_scan_result_t) works identically whether the list just came
 * from a real scan or was read back from the cache -- no parallel "cache
 * flavor" of that code. `auth` is a coarse placeholder ("OPEN"/"WPA2")
 * derived from encryption_mode: capnet_draw_ap_pick()/capnet_draw_ap_confirm()
 * never display it (only ssid/bssid/channel/rssi), so full fidelity through
 * the round trip isn't needed. Clamped to M1_CAP_SCAN_MAX_APS (32), which is
 * smaller than the cache's own WIFI_SESSION_CACHE_MAX_APS (48) -- in
 * practice a publish() can never exceed 32 anyway, since the only producer
 * (cap_scan_to_session_aps(), fed by m1_cap_scan_parse()) is itself bounded
 * to 32, but the clamp is kept here too since this module doesn't assume
 * that invariant holds for every future cache producer. */
/* Bounded copy of a possibly-not-NUL-terminated fixed-size source (the
 * cache's uint8_t ssid[]/bssid[] arrays) into a NUL-terminated char dest.
 * Not strncpy(): GCC's -Wstringop-truncation can't see through the
 * uint8_t*->char* cast a plain strncpy(dst, (const char *)src, ...) call
 * would need here, so it (falsely) flags a truncation risk. This reads no
 * further than min(src_size, dst_size-1) either way and always terminates. */
static void cap_bounded_str_copy(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    size_t max = (dst_size - 1u < src_size) ? (dst_size - 1u) : src_size;
    size_t len = strnlen((const char *)src, max);

    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void cap_session_aps_to_scan(const wifi_session_ap_t *aps, uint16_t n,
                                    m1_cap_scan_result_t *out)
{
    uint16_t cap = n;
    uint16_t i;

    if (cap > M1_CAP_SCAN_MAX_APS) { cap = M1_CAP_SCAN_MAX_APS; }

    memset(out, 0, sizeof(*out));
    for (i = 0; i < cap; i++)
    {
        m1_cap_scan_ap_t *ap = &out->aps[i];
        cap_bounded_str_copy(ap->ssid, sizeof(ap->ssid), aps[i].ssid, sizeof(aps[i].ssid));
        cap_bounded_str_copy(ap->bssid, sizeof(ap->bssid), aps[i].bssid, sizeof(aps[i].bssid));
        strncpy(ap->auth, (aps[i].encryption_mode == 0) ? "OPEN" : "WPA2", sizeof(ap->auth) - 1u);
        ap->auth[sizeof(ap->auth) - 1u] = '\0';
        ap->channel = aps[i].channel;
        ap->rssi = aps[i].rssi;
    }
    out->count = cap;
    out->reported = cap;
    out->valid = true;
}

/* Returns the index of `bssid` in scan->aps[], or 0 if not found, bssid is
 * empty, or the list is empty. Used to re-locate the session's selected AP
 * (if any) in a freshly (re)loaded list, per "preserve selection/list
 * position" -- BSSID is the only stable identity across scans (SSID can
 * collide, list order/content isn't guaranteed stable scan to scan). */
static uint32_t cap_find_ap_index(const m1_cap_scan_result_t *scan, const char *bssid)
{
    uint32_t i;

    if (bssid[0] == '\0') { return 0u; }
    for (i = 0; i < scan->count; i++)
    {
        if (strcmp(scan->aps[i].bssid, bssid) == 0) { return i; }
    }
    return 0u;
}

/*
 * One `scan -a` round trip: send, wait for the ESP's own reply, hand it to
 * the host-tested parser (m1_capture_scan_parse.c). The ESP's reply already
 * contains the "N AP(s) found" sentinel plus the full table (esp32c6_wifi.c:
 * scan() prints both before returning) -- unlike the frozen Networks tool's
 * scan -a + list -a two-command fetch, one command is enough here, so this
 * is one scan AND one round trip, not just one scan.
 *
 * Radio ownership: the ESP's wifi_take_radio() gate acquires RADIO_OWNER_WIFI
 * for the scan and releases it (components/m1_wifi/m1_wifi_cmds.c: h_scan_a)
 * before print_ap_list() ever runs -- release happens before this reply is
 * even sent, let alone parsed. Nothing on this side needs to acquire/release
 * anything. If BLE currently owns the radio, the ESP replies with its
 * "[!] Radio busy" text instead of a table; that is reported as
 * M1_CAP_SCAN_RADIO_BUSY, never asserted on.
 */
static m1_cap_scan_status_t cap_scan_networks(m1_cap_scan_result_t *out)
{
    uint16_t resp_len;
    char pre[160]; /* matches m1_capture_soak_run()/m1_capture_stream_run()'s resp[160] */

    memset(out, 0, sizeof(*out));
    memset(s_cap_scan_reply, 0, sizeof(s_cap_scan_reply));

    /* Full proven ESP32 bring-up (software flag + physical EN pin + retry),
     * not just an init-status check -- see cap_esp32_link_ready() above for
     * why the flag alone isn't sufficient. This is the same class of failure
     * m1_wifi.c's own scan path already had to guard against. */
    if (!cap_esp32_link_ready())
    {
        return M1_CAP_SCAN_NO_REPLY;
    }

    /* Every call here is a forced rescan (this screen has no separate
     * "manual refresh" trigger -- one scan per visit, see cap_scan_or_cancel()
     * below) -- invalidate the cache before attempting it so a failed or
     * cancelled scan leaves the cache honestly stale rather than stamped
     * fresh over data that was never actually refreshed, per
     * wifi_session_cache_invalidate()'s documented contract. */
    cap_wifi_cache_ensure_init();
    wifi_session_cache_invalidate(wifi_session_cache_get());

    /* "mode -w" so this doesn't depend on whatever mode the console was last
     * left in, then a ring-buffer reset immediately before the real command
     * so nothing queued during the mode switch bleeds into the scan reply --
     * same shape every scan caller in m1_wifi.c follows before its own
     * command. */
    (void)esp32_uart_write((const uint8_t *)"mode -w\r\n", 9, 100);
    (void)esp32_uart_read_until_prompt(pre, sizeof(pre), 1200, ">> ");

    m1_ringbuffer_reset(&esp32_rb_hdl);
    if (!esp32_uart_write((const uint8_t *)CAP_SCAN_CMD, (uint16_t)(sizeof(CAP_SCAN_CMD) - 1u), 100))
    {
        return M1_CAP_SCAN_NO_REPLY;
    }

    resp_len = esp32_uart_read_until_prompt(s_cap_scan_reply, sizeof(s_cap_scan_reply),
                                            CAP_SCAN_CMD_TIMEOUT_MS, ">> ");
    if ((resp_len == 0u) || (strstr(s_cap_scan_reply, ">> ") == NULL))
    {
        return M1_CAP_SCAN_NO_REPLY;
    }
    if (strstr(s_cap_scan_reply, "Radio busy") != NULL)
    {
        return M1_CAP_SCAN_RADIO_BUSY;
    }

    /* A clean prompt with no sentinel (m1_cap_scan_parse returns -1, e.g. a
     * genuine "no APs" reply shape) folds into "0 APs found" rather than a
     * distinct error -- the link answered fine, it just found nothing. */
    (void)m1_cap_scan_parse(s_cap_scan_reply, (uint32_t)resp_len, out);

    /* Scan just completed (successfully, even if 0 APs found) -- the one
     * well-defined publish() moment per m1_wifi_session_cache.h. Uses the
     * same static scratch as the cache-read path (s_cap_wifi_snap); this
     * screen never has a read and a publish in flight at the same time. */
    {
        uint16_t n = cap_scan_to_session_aps(out, s_cap_wifi_snap, WIFI_SESSION_CACHE_MAX_APS);
        wifi_session_cache_publish(wifi_session_cache_get(), s_cap_wifi_snap, n, HAL_GetTick());
    }
    return M1_CAP_SCAN_OK;
}

static void capnet_draw_scanning(void)
{
    wifi_ui_draw_scanning("Scanning Networks...");
}

static void capnet_draw_scan_retry(m1_cap_scan_status_t st)
{
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 10, "Capture Network");
    if (st == M1_CAP_SCAN_RADIO_BUSY)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "Radio busy (BLE)");
        u8g2_DrawStr(&m1_u8g2, 2, 36, "Stop BLE first");
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, 24, "No reply from ESP");
        u8g2_DrawStr(&m1_u8g2, 2, 36, "Check link");
    }
    u8g2_DrawStr(&m1_u8g2, 2, 58, "OK Retry");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

/*
 * Runs the one scan for this screen visit, retrying on failure until it
 * succeeds or the user backs out. `out` is populated by this call alone: no
 * background task, no cache, nothing that outlives this screen visit. Callers
 * must treat a false return as "exit the screen", not "proceed with a stale
 * or empty result".
 */
static bool cap_scan_or_cancel(m1_cap_scan_result_t *out)
{
    for (;;)
    {
        m1_cap_scan_status_t st;

        capnet_draw_scanning();
        st = cap_scan_networks(out);
        if (st == M1_CAP_SCAN_OK) { return true; }

        capnet_draw_scan_retry(st);
        for (;;)
        {
            S_M1_Buttons_Status btn;
            S_M1_Main_Q_t q;

            if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
            if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
            (void)xQueueReceive(button_events_q_hdl, &btn, 0);

            if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return false; }
            if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { break; } /* retry scan */
        }
    }
}

/*********************** L I V E   C A P T U R E   S C R E E N ****************/

static const uint16_t k_cap_secs[3] = { 10u, 30u, 60u };

static void capnet_draw_select(uint8_t ch, uint8_t dur_idx, uint32_t ap_count)
{
    char l[26];
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 8, "Capture Network");
    snprintf(l, sizeof(l), "Channel: %u", (unsigned)ch);
    u8g2_DrawStr(&m1_u8g2, 2, 18, l);
    snprintf(l, sizeof(l), "Duration: %us", (unsigned)k_cap_secs[dur_idx]);
    u8g2_DrawStr(&m1_u8g2, 2, 28, l);
    snprintf(l, sizeof(l), "Nets seen: %lu", (unsigned long)ap_count);
    u8g2_DrawStr(&m1_u8g2, 2, 38, l);
    u8g2_DrawStr(&m1_u8g2, 2, 48, "L/R ch   U/D time");
    u8g2_DrawStr(&m1_u8g2, 2, 58, "OK Run");
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

static void capnet_draw_result(const m1_capture_stream_result_t *r, uint8_t page)
{
    char l[26];
    long loss = (long)r->esp_frames - (long)r->frames;

    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    if (!r->got_ack)
    {
        u8g2_DrawStr(&m1_u8g2, 2, 11, "Capture link failed");
        u8g2_DrawStr(&m1_u8g2, 2, 28, "No ACK / rate-switch");
            }
    else if (page == 0u)
    {
        /* page indicator top-left; status shares the top row */
        u8g2_DrawStr(&m1_u8g2, 2, 10, "1/2");
        snprintf(l, sizeof(l), "CH%u  %s", (unsigned)r->channel,
                 r->sd_ok ? "SD ok" : "SD ERR");
        u8g2_DrawStr(&m1_u8g2, 28, 10, l);
        snprintf(l, sizeof(l), "frames %lu", (unsigned long)r->frames);
        u8g2_DrawStr(&m1_u8g2, 2, 26, l);
        snprintf(l, sizeof(l), "file %luKB", (unsigned long)(r->cap_bytes / 1024u));
        u8g2_DrawStr(&m1_u8g2, 2, 40, l);
        u8g2_DrawStr(&m1_u8g2, 2, 54, r->path);
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, 10, "2/2");
        snprintf(l, sizeof(l), "RDY:%s bat %lu", r->got_ready ? "Y" : "N",
                 (unsigned long)r->batches);
        u8g2_DrawStr(&m1_u8g2, 28, 10, l);
        snprintf(l, sizeof(l), "esp %lu drop %lu",
                 (unsigned long)r->esp_frames, (unsigned long)r->esp_drops);
        u8g2_DrawStr(&m1_u8g2, 2, 22, l);
        snprintf(l, sizeof(l), "crc %lu fmt %lu dec %lu", (unsigned long)r->crc_errors,
                 (unsigned long)r->format_errors, (unsigned long)r->decode_errors);
        u8g2_DrawStr(&m1_u8g2, 2, 33, l);
        snprintf(l, sizeof(l), "ore %lu dma %lu bk%lu", (unsigned long)r->uart_ore,
                 (unsigned long)r->dma_errors, (unsigned long)r->max_backlog);
        u8g2_DrawStr(&m1_u8g2, 2, 44, l);
        snprintf(l, sizeof(l), "loss %ld  %s", loss, r->got_stopped ? "stop-ok" : "no-stop");
        u8g2_DrawStr(&m1_u8g2, 2, 55, l);
    }
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

/*
 * Runs one capture on `ch` for `dur_secs` and shows the paged result screen.
 * Shared by the AP-select and manual entry paths so OK behaves identically
 * either way.
 *
 * PCAP_START re-acquires RADIO_OWNER_WIFI on the ESP side (h_pcap_start ->
 * wifi_take_radio()), independently of whatever the scan step acquired and
 * released earlier -- if BLE has since grabbed the radio, m1_capture_stream_run()
 * comes back with got_ack=false, radio_busy=true instead of the usual
 * "PCAP ACK". That is reported here with the same reason-specific retry
 * screen as the scan step's cap_scan_or_cancel() (capnet_draw_scan_retry(),
 * OK retries the acquire, BACK gives up on this capture) rather than falling
 * through to the generic "Capture link failed" dead end -- no assert either
 * way.
 */
static void cap_run_and_show_result(uint8_t ch, uint16_t dur_secs)
{
    m1_capture_stream_result_t res;
    uint8_t page = 0;
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;

    for (;;)
    {
        m1_u8g2_firstpage();
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        u8g2_DrawStr(&m1_u8g2, 2, 30, "Capturing...");
        m1_u8g2_nextpage();
        u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

        (void)m1_capture_stream_run(ch, (uint32_t)dur_secs * 1000u, &res);
        xQueueReset(main_q_hdl);

        if (!res.got_ack && res.radio_busy)
        {
            capnet_draw_scan_retry(M1_CAP_SCAN_RADIO_BUSY);
            for (;;)
            {
                if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
                if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
                (void)xQueueReceive(button_events_q_hdl, &btn, 0);
                if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return; } /* give up on this capture */
                if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { break; }    /* retry PCAP_START */
            }
            continue;
        }
        break;
    }

    CP(70);
    capnet_draw_result(&res, page);
    for (;;)
    {
        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { break; }
        if ((btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) ||
            (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK))
        {
            page = (uint8_t)(page ? 0u : 1u);
            capnet_draw_result(&res, page);
        }
    }
}

/*********** A P - S E L E C T   ( u s e d   o n l y   w h e n   A P s   e x i s t ) */

/* One AP per screen; LEFT/RIGHT page (wrap), OK selects, BACK exits the whole
 * screen. The card itself is drawn by the shared Wi-Fi renderer; this local
 * loop retains MonstaShark's existing input outcomes. UP/DOWN are free in this
 * screen (unlike the AP-confirm screen below, which uses them for duration),
 * so either forces a real rescan and bypasses a still-fresh cache. */
static void capnet_draw_ap_pick(const m1_cap_scan_result_t *scan, uint32_t sel)
{
    const m1_cap_scan_ap_t *ap = &scan->aps[sel];
    wifi_ui_draw_ap_card("Networks", (uint16_t)sel, (uint16_t)scan->count,
                         ap->ssid, ap->bssid, ap->rssi, ap->channel, ap->auth,
                         NULL, "Select", "Rescan");
}

static void capnet_draw_ap_confirm(const m1_cap_scan_ap_t *ap, uint8_t dur_idx)
{
    char l[26];
    char v[10];
    u8g2_uint_t vx, vw;
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 2, 9, "MonstaShark");
    snprintf(l, sizeof(l), "%.20s", (ap->ssid[0] != '\0') ? ap->ssid : "(hidden)");
    u8g2_DrawStr(&m1_u8g2, 2, 21, l);
    snprintf(l, sizeof(l), "Channel %d", ap->channel);
    u8g2_DrawStr(&m1_u8g2, 2, 32, l);
    /* Capture time as an obviously-adjustable field: label + boxed value with an
     * up/down spinner, so it reads as "UP/DOWN changes this". */
    u8g2_DrawStr(&m1_u8g2, 2, 45, "Capture time");
    snprintf(v, sizeof(v), "%us", (unsigned)k_cap_secs[dur_idx]);
    vw = (u8g2_uint_t)u8g2_GetStrWidth(&m1_u8g2, v);
    vx = 78;
    u8g2_DrawFrame(&m1_u8g2, (u8g2_uint_t)(vx - 3), 36, (u8g2_uint_t)(vw + 6), 12); /* box around value */
    u8g2_DrawStr(&m1_u8g2, vx, 45, v);
    /* Footer bar: up/down triangle glyphs + "time" (lower-left), "Run" + CENTER
     * button icon (lower-right), punched out of a solid bar. */
    {
        u8g2_uint_t ix = (u8g2_uint_t)(M1_LCD_DISPLAY_WIDTH - 2 - 10);
        u8g2_uint_t rw;
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_DrawBox(&m1_u8g2, 0, 52, M1_LCD_DISPLAY_WIDTH, 12);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);    /* match the NFC menu black-bar font */
        u8g2_DrawTriangle(&m1_u8g2, 2, 60, 8, 60, 5, 55);    /* up triangle   */
        u8g2_DrawTriangle(&m1_u8g2, 10, 55, 16, 55, 13, 60); /* down triangle (side by side) */
        u8g2_DrawStr(&m1_u8g2, 19, 61, "time");
        u8g2_DrawXBMP(&m1_u8g2, ix, 52, 10, 10, target_10x10);
        rw = (u8g2_uint_t)u8g2_GetStrWidth(&m1_u8g2, "Run");
        u8g2_DrawStr(&m1_u8g2, (u8g2_uint_t)(ix - 2 - rw), 61, "Run");
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    }
    m1_u8g2_nextpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
}

typedef enum {
    CAP_AP_PICK_SELECT = 0, /* OK: *sel is the picked index                */
    CAP_AP_PICK_BACK,       /* BACK: exit the whole screen                 */
    CAP_AP_PICK_REFRESH,    /* UP/DOWN: visible manual-refresh control --  */
                             /* caller must force a real rescan (step 4)   */
} cap_ap_pick_result_t;

/* Browse scan->aps[]. Returns CAP_AP_PICK_SELECT with *sel set to the picked
 * index on OK, CAP_AP_PICK_BACK (leaving *sel wherever it was) on BACK --
 * caller treats that as "exit the screen", same top-level BACK semantics as
 * the manual path -- or CAP_AP_PICK_REFRESH on UP/DOWN, the manual-refresh
 * control (leaves *sel untouched; the caller reloads the list). */
static cap_ap_pick_result_t cap_ap_pick_or_back(const m1_cap_scan_result_t *scan, uint32_t *sel)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q;

    if (*sel >= scan->count) { *sel = 0u; }
    for (;;)
    {
        capnet_draw_ap_pick(scan, *sel);

        if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
        if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
        (void)xQueueReceive(button_events_q_hdl, &btn, 0);

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return CAP_AP_PICK_BACK; }
        if ((btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) ||
            (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
        {
            return CAP_AP_PICK_REFRESH;
        }
        if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            *sel = (*sel == 0u) ? (scan->count - 1u) : (*sel - 1u);
        }
        else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            *sel = ((*sel + 1u) >= scan->count) ? 0u : (*sel + 1u);
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) { return CAP_AP_PICK_SELECT; }
    }
}

typedef enum {
    CAP_NET_AP_EXIT = 0,  /* BACK out of the AP list: exit the whole screen */
    CAP_NET_AP_REFRESH,   /* manual-refresh control: caller must force a    */
                           /* real rescan (invalidate + cap_scan_or_cancel) */
} cap_net_ap_mode_result_t;

/*
 * AP-select path: pick a network from the (scanned or cache-supplied) list,
 * adjust only the duration (channel is fixed to the picked AP), run, show
 * the result, then return to the network list. Only entered when
 * scan->count > 0 -- an empty list falls back to manual entry instead (see
 * m1_capture_network_screen()). `*sel` is owned by the caller so list
 * position survives a manual-refresh reload (step 3's "preserve
 * selection/list position").
 */
static cap_net_ap_mode_result_t cap_network_screen_ap_mode(const m1_cap_scan_result_t *scan,
                                                            uint32_t *sel)
{
    uint8_t dur_idx = 0;

    for (;;)
    {
        cap_ap_pick_result_t pick = cap_ap_pick_or_back(scan, sel);
        if (pick == CAP_AP_PICK_BACK) { return CAP_NET_AP_EXIT; }
        if (pick == CAP_AP_PICK_REFRESH) { return CAP_NET_AP_REFRESH; }

        for (;;)
        {
            S_M1_Buttons_Status btn;
            S_M1_Main_Q_t q;

            capnet_draw_ap_confirm(&scan->aps[*sel], dur_idx);

            if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
            if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
            (void)xQueueReceive(button_events_q_hdl, &btn, 0);

            if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { break; } /* back to AP list */
            if ((btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) ||
                (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
            {
                dur_idx = (uint8_t)((dur_idx + 1u) % 3u);
            }
            else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                /* The AP's channel was frozen at scan time. Re-read it fresh
                 * here, at confirm, and bounds-check before it ever reaches
                 * PCAP_START <ch> -- don't trust that a stored row is still
                 * in range just because m1_cap_scan_parse() validated it
                 * once at parse time. This can't fail today (the parser
                 * already rejects out-of-range channels when storing a row),
                 * so this is a defensive re-check against that invariant
                 * changing later, not a response to an observed failure. */
                int cur_ch = scan->aps[*sel].channel;
                if (cur_ch < 1 || cur_ch > 14)
                {
                    break; /* treat as if the AP vanished; back to the list */
                }

                /* Persist the confirmed AP as this session's selected capture
                 * target (m1_wifi_session_cache.h's capture-only selected-AP
                 * context) before running -- independent of scan->aps[]'s
                 * own lifetime, which ends when this screen exits. Converted
                 * directly against the picked index (not via
                 * cap_scan_to_session_aps(), which always starts at aps[0]). */
                {
                    wifi_session_ap_t sel_ap;
                    memset(&sel_ap, 0, sizeof(sel_ap));
                    strncpy((char *)sel_ap.ssid, scan->aps[*sel].ssid, sizeof(sel_ap.ssid) - 1u);
                    sel_ap.ssid[sizeof(sel_ap.ssid) - 1u] = '\0';
                    strncpy((char *)sel_ap.bssid, scan->aps[*sel].bssid, sizeof(sel_ap.bssid) - 1u);
                    sel_ap.bssid[sizeof(sel_ap.bssid) - 1u] = '\0';
                    sel_ap.rssi = scan->aps[*sel].rssi;
                    sel_ap.channel = cur_ch;
                    sel_ap.encryption_mode = cap_auth_str_to_enc_mode(scan->aps[*sel].auth);
                    wifi_session_selected_set(&s_wifi_selected, &sel_ap);
                }

                cap_run_and_show_result((uint8_t)cur_ch, k_cap_secs[dur_idx]);
                break; /* back to AP list after showing the result */
            }
        }
    }
}

void m1_capture_network_screen(void)
{
    /* Local to this call frame: gone the moment the screen is exited, never
     * touched by any other tool or re-entry of this same screen, never
     * promoted to static/global. Reloaded each time around the outer loop
     * below -- either from the cache (no scan) or from a real scan. */
    static m1_cap_scan_result_t scan_res;
    uint32_t sel = 0;
    wifi_session_ap_t prev_sel;
    bool have_prev_sel;

    /* First entry since boot: give this screen a fresh (empty, invalid)
     * cache instance to publish into. cap_scan_networks() invalidates it
     * again before every rescan anyway, but that's a no-op on a cache that
     * is already invalid -- this is the "screen opened" moment, distinct
     * from "a rescan is about to run". */
    cap_wifi_cache_ensure_init();

    /* Read path for wifi_session_selected_get(): recover whichever AP was
     * confirmed last (this boot session, any prior visit to this screen) so
     * its list position can be restored below instead of always starting at
     * index 0. Resolved by BSSID against whichever list gets loaded on each
     * pass of the loop, since list order/content isn't stable scan to scan. */
    have_prev_sel = wifi_session_selected_get(&s_wifi_selected, &prev_sel);

    for (;;)
    {
        uint32_t now = HAL_GetTick();

        if (wifi_session_cache_is_fresh(wifi_session_cache_get(), now, WIFI_SESSION_CACHE_TTL_MS))
        {
            /* Step 3: valid, within TTL -- render the cached snapshot
             * immediately. No scan -a round trip on this path at all. */
            uint16_t n = wifi_session_cache_snapshot(wifi_session_cache_get(), s_cap_wifi_snap,
                                                     WIFI_SESSION_CACHE_MAX_APS);
            cap_session_aps_to_scan(s_cap_wifi_snap, n, &scan_res);
        }
        else
        {
            /* Step 4: invalid or expired -- the exact existing scan path.
             * cap_scan_networks() (called from cap_scan_or_cancel()) does
             * the invalidate-before/publish-after itself. */
            if (!cap_scan_or_cancel(&scan_res)) { return; }
        }

        if (have_prev_sel)
        {
            sel = cap_find_ap_index(&scan_res, (const char *)prev_sel.bssid);
        }

        if (scan_res.count > 0u)
        {
            cap_net_ap_mode_result_t r = cap_network_screen_ap_mode(&scan_res, &sel);
            if (r == CAP_NET_AP_EXIT) { return; }

            /* CAP_NET_AP_REFRESH: the visible manual-refresh control (step
             * 5). Force step 4 on the next pass regardless of TTL -- without
             * this, is_fresh() would still be true immediately after a
             * publish() and the loop would just re-serve the same cached
             * snapshot instead of actually rescanning. */
            wifi_session_cache_invalidate(wifi_session_cache_get());
            continue;
        }

        break; /* empty list: fall through to manual entry below */
    }

    /* Manual entry: the floor this screen falls back to whenever the list
     * (cached or freshly scanned) came back with zero APs -- unchanged from
     * before the scan step was added, so the screen stays usable either way.
     * No manual-refresh control here: every button (L/R/U/D/OK/BACK) is
     * already spoken for by channel/duration/run/exit, unlike the AP-pick
     * screen where UP/DOWN were free. */
    {
        uint8_t ch = 6;       /* default channel 6 */
        uint8_t dur_idx = 0;  /* default 10 s       */
        S_M1_Buttons_Status btn;
        S_M1_Main_Q_t q;

        capnet_draw_select(ch, dur_idx, scan_res.count);
        for (;;)
        {
            if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) { continue; }
            if (q.q_evt_type != Q_EVENT_KEYPAD) { continue; }
            (void)xQueueReceive(button_events_q_hdl, &btn, 0);

            if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { return; }
            else if (btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
            {
                ch = (ch <= 1u) ? 14u : (uint8_t)(ch - 1u);
                capnet_draw_select(ch, dur_idx, scan_res.count);
            }
            else if (btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
            {
                ch = (ch >= 14u) ? 1u : (uint8_t)(ch + 1u);
                capnet_draw_select(ch, dur_idx, scan_res.count);
            }
            else if ((btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) ||
                     (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK))
            {
                dur_idx = (uint8_t)((dur_idx + 1u) % 3u);
                capnet_draw_select(ch, dur_idx, scan_res.count);
            }
            else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                cap_run_and_show_result(ch, k_cap_secs[dur_idx]);
                capnet_draw_select(ch, dur_idx, scan_res.count);
            }
        }
    }
}
