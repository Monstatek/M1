/* See COPYING.txt for license details. */

/*
*
* m1_capture_link.h
*
* STM32 side of the MonstaShark (Wi-Fi Capture) Gate 0 transport harness:
* capture-mode UART4 DMA receive + soak driver. Reconfigures UART4 from the
* ordinary interrupt-RX path to a circular GPDMA receive at a negotiated high
* baud, drives the ESP32-C6 `captest` endpoint, grants byte credits, verifies
* framed test traffic (CRC-32C + sequence), and restores the console link.
*
* This is a transport soak, not the capture engine. Software byte-credit flow
* control is used (no RTS/CTS assumption).
*
* M1 Project
*
*/

#ifndef M1_CAPTURE_LINK_H_
#define M1_CAPTURE_LINK_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t baud;
    uint32_t duration_ms;
    bool     load;           /* SD-write + UI-load variant was run          */
    uint32_t frames_ok;      /* frames that decoded with valid CRC          */
    uint32_t crc_errors;     /* CRC-32C mismatches                          */
    uint32_t format_errors;  /* malformed/short frames / desync             */
    uint32_t seq_gaps;       /* missing sequence numbers                    */
    uint32_t seq_dups;       /* duplicate/old sequence numbers              */
    uint32_t rx_overruns;    /* COBS block-buffer overruns                  */
    uint32_t uart_ore;       /* UART4 overrun (ORE) events (FIFO overran)   */
    uint32_t dma_errors;     /* GPDMA transfer/link/user error flags        */
    uint32_t max_backlog;    /* peak unconsumed bytes in the DMA ring       */
    uint32_t grants;         /* CAP_CREDIT messages sent                    */
    uint64_t bytes_rx;       /* framed wire bytes received                  */
    uint32_t esp_frames;     /* frames the ESP reported sending (CAP_STOPPED)*/
    uint32_t esp_seq;        /* last sequence the ESP reported              */
    uint32_t esp_starved;    /* ESP credit-starvation events (CAP_STOPPED)  */
    uint32_t esp_grants;     /* credits the ESP reported receiving          */
    uint32_t blocks_seen;    /* delimiter-bounded blocks the STM received   */
    bool     got_ack;        /* saw "CAPTEST ACK"                           */
    bool     got_ready;      /* decoded a valid CAP_READY (binary sync ok)  */
    bool     got_stopped;    /* received CAP_STOPPED                        */
} m1_capture_soak_result_t;

/* Run one transport soak at `baud` for `duration_ms`. When `load` is true, the
 * STM32 also performs representative buffered SD writes + display refresh during
 * the soak (combined-load test). Blocks. Returns true if the link negotiated
 * (ACK seen) and the capture link was exercised; false if negotiation failed
 * (console rate restored, no harm). */
bool m1_capture_soak_run(uint32_t baud, uint32_t duration_ms, bool load,
                         m1_capture_soak_result_t *out);

/* Menu sub-function: pick baud + load, run the soak, show results. */
void m1_capture_test_screen(void);

/*--------------------------- live capture stream ---------------------------*/

typedef struct {
    uint8_t  channel;        /* fixed capture channel (1..14)               */
    uint32_t duration_ms;    /* requested capture window                    */
    bool     got_ack;        /* ESP acknowledged PCAP_START                 */
    bool     radio_busy;     /* PCAP_START declined: BLE currently owns the radio */
    bool     got_ready;      /* binary sync at capture baud established      */
    bool     got_stopped;    /* ESP reported CAP_STOPPED                     */
    bool     sd_ok;          /* pcapng file opened + all writes succeeded    */
    uint32_t batches;        /* CAP_FRAME_BATCH envelopes received          */
    uint32_t frames;         /* EPBs written to the pcapng file             */
    uint64_t cap_bytes;      /* pcapng bytes written to SD                  */
    uint64_t wire_rx;        /* framed batch wire bytes received            */
    uint32_t crc_errors;     /* CRC-32C mismatches                          */
    uint32_t format_errors;  /* malformed/short frames / desync             */
    uint32_t decode_errors;  /* malformed capframe records inside batches   */
    uint32_t seq_gaps;       /* missing transport sequence numbers          */
    uint32_t rx_overruns;    /* COBS block-buffer overruns                  */
    uint32_t uart_ore;       /* UART4 overrun events                        */
    uint32_t dma_errors;     /* GPDMA error flags                           */
    uint32_t max_backlog;    /* peak unconsumed bytes in the DMA ring       */
    uint32_t esp_frames;     /* frames the ESP captured (CAP_STOPPED)       */
    uint32_t esp_drops;      /* frames the ESP dropped at its ring          */
    char     path[24];       /* pcapng file path on SD                      */
} m1_capture_stream_result_t;

/* Run a live capture on `channel` for up to `duration_ms`, writing a PCAPNG
 * (radiotap + 802.11) file to the SD card via the Gate 0 binary transport.
 * Blocks. Returns true if the link negotiated and a capture ran; false if
 * negotiation failed (console rate restored, no harm). */
bool m1_capture_stream_run(uint8_t channel, uint32_t duration_ms,
                           m1_capture_stream_result_t *out);

/* Menu sub-function: pick channel, run a live capture, show results. */
void m1_capture_network_screen(void);

#endif /* M1_CAPTURE_LINK_H_ */
