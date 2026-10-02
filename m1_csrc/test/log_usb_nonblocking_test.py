#!/usr/bin/env python3
"""Compile actual logger functions and ring-buffer implementation with host stubs."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
source = (ROOT / 'm1_csrc/m1_log_debug.c').read_text()

def function(signature):
    start = source.index(signature + '\n{')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "m1_ring_buffer.h"
#define M1_LOGDB_TX_BUFFER_SIZE 2048
#define USB_FS_CHUNK_SIZE 64
#define pdTRUE 1
#define taskSCHEDULER_RUNNING 1
#define USBD_OK 0
#define USBD_BUSY 1
#define UNUSED(x) (void)(x)
typedef int enCdcMode;
enum { CDC_MODE_LOG_CLI, CDC_MODE_ESP32, CDC_MODE_VCP };
typedef struct { unsigned TxState; } USBD_CDC_HandleTypeDef;
static USBD_CDC_HandleTypeDef usb;
static int ready=1, scheduler=1, in_isr, locked, critical, fail_tx, sends;
static void *mutex_log_write_trans=(void*)1, *log_q_hdl=(void*)1;
static enCdcMode m1_usbcdc_mode, cdc_tx_owner_mode;
static uint16_t logdb_dma_tx_len;
static S_M1_RingBuffer ring, *plogdb_tx_rb=&ring;
static uint8_t storage[M1_LOGDB_TX_BUFFER_SIZE], *inflight;
static uint16_t flight_len;
static unsigned __get_IPSR(void) {return in_isr;}
static int xTaskGetSchedulerState(void) {return scheduler;}
static int xSemaphoreTake(void *p, unsigned ticks) {
    (void)p; assert(ticks==0); if(locked) return 0; locked=1; return 1;
}
static int xSemaphoreGive(void *p) {(void)p; locked=0; return 1;}
static int xQueueSend(void *p, void *v, unsigned ticks) {
    (void)p; (void)v; assert(ticks==0); return 1;
}
#define taskENTER_CRITICAL() (++critical)
#define taskEXIT_CRITICAL() (--critical)
static uint8_t m1_logdb_usbcdc_tx_ready(void) {return ready;}
static USBD_CDC_HandleTypeDef *m1_logdb_get_usbcdc_handle(void) {return &usb;}
static uint8_t CDC_Transmit_FS(uint8_t *p, uint16_t len) {
    assert(critical==1); assert(cdc_tx_owner_mode==CDC_MODE_LOG_CLI);
    ++sends; if(fail_tx) return USBD_BUSY;
    assert(!usb.TxState); usb.TxState=1; inflight=p; flight_len=len; return USBD_OK;
}
'''
tests = r'''
static void reset(void) {
    m1_ringbuffer_init(&ring,storage,sizeof(storage),1);
    logdb_dma_tx_len=0; usb.TxState=0; ready=1; locked=0;
    in_isr=0; scheduler=1; fail_tx=0; sends=0; m1_usbcdc_mode=CDC_MODE_LOG_CLI;
}

/* --- 9a: no host connected, sustained across many iterations. Producer and
 * consumer are both driven repeatedly with the device never configured
 * (ready==0). No transmit attempt is ever made and every producer call
 * returns instantly (no stub here can block -- vTaskDelay/HAL_Delay are not
 * even linked into this binary, so a reintroduced wait would fail to link,
 * not just fail an assertion). --- */
static void stress_no_host(void) {
    uint8_t a[32]; memset(a,'N',sizeof(a));
    reset(); ready=0;
    int saw_drop=0;
    for (int i = 0; i < 10000; i++) {
        /* _write() reports the full length "consumed" either way (its
         * caller -- printf() -- must never see a short write and retry);
         * m1_logdb_enqueue() itself is agnostic to USB readiness and keeps
         * queueing into the bounded ring buffer until full, then drops --
         * ready==0 only ever gates the CONSUMER (nothing will ever drain
         * it), never the producer. */
        assert(_write(1,(char*)a,sizeof(a))==(int)sizeof(a));
        int w = m1_logdb_enqueue(a,sizeof(a));
        assert((w>=0) && (w<=(int)sizeof(a))); /* never negative, never more than requested */
        if (w==0) saw_drop=1;
        m1_logdb_start_usbcdc_tx(); /* not ready: must always be a pure no-op */
        assert(sends==0);
        assert(critical==0);
        assert(usb.TxState==0);
    }
    assert(saw_drop); /* the bounded ring buffer fills and starts dropping; it never grows or waits */
}

/* --- 9b: USB configured but no terminal is reading, i.e. every transmit
 * attempt the device makes is refused (CDC_Transmit_FS returns USBD_BUSY
 * forever, exactly as it does when the host-side driver never drains the
 * endpoint). Producer keeps queueing into the bounded ring buffer; once
 * full it starts dropping (return 0) rather than growing or waiting. The
 * consumer keeps retrying the SAME candidate chunk without ever blocking. --- */
static void stress_configured_unread(void) {
    uint8_t a[32]; memset(a,'U',sizeof(a));
    reset(); ready=1; fail_tx=1;
    int wrote_total=0, dropped=0;
    for (int i = 0; i < 10000; i++) {
        int w = m1_logdb_enqueue(a,sizeof(a));
        if (w) wrote_total+=w; else dropped++;
        m1_logdb_start_usbcdc_tx();
        assert(usb.TxState==0); /* CDC_Transmit_FS never reports success while fail_tx */
        assert(critical==0);
    }
    assert(sends>0);          /* the device DID keep trying, non-blockingly */
    assert(dropped>0);        /* the bounded ring buffer eventually refuses new writes */
    assert(wrote_total<=(int)sizeof(storage)); /* never grows past its fixed capacity */
}

/* --- 9c: the endpoint remains busy (TxState stuck at 1 by a transfer that
 * genuinely started but has not yet been ACKed), as opposed to 9b's
 * "never even accepted" case. Same non-blocking guarantee, different guard
 * clause (the TxState check short-circuits before CDC_Transmit_FS is even
 * called again). --- */
static void stress_endpoint_busy(void) {
    uint8_t a[100]; memset(a,'B',sizeof(a));
    reset(); ready=1;
    assert(m1_logdb_enqueue(a,sizeof(a))==(int)sizeof(a));
    m1_logdb_start_usbcdc_tx();
    assert(usb.TxState==1); /* one real transfer is now in flight */
    int before=sends;
    for (int i = 0; i < 10000; i++) {
        assert(m1_logdb_enqueue(a,sizeof(a))>=0); /* producer never blocks regardless */
        m1_logdb_start_usbcdc_tx(); /* must no-op: endpoint still busy */
        assert(sends==before);      /* CDC_Transmit_FS is never re-invoked while busy */
        assert(critical==0);
    }
}

/* --- 9d/9e: the host disconnects mid-transmission (TxState cancelled to 0
 * without the normal completion callback ever running -- indistinguishable
 * from the endpoint's own point of view, which is exactly why
 * m1_logdb_start_usbcdc_tx() must treat "idle with a nonzero in-flight
 * length" as reclaimable either way), and then the host reconnects and
 * normal operation must resume correctly -- not just "not crash", but with
 * byte-correct data reaching CDC_Transmit_FS afterwards. --- */
static void disconnect_then_reconnect(void) {
    uint8_t a[200], saved[64]; reset(); ready=1;
    memset(a,'X',sizeof(a));
    assert(m1_logdb_enqueue(a,sizeof(a))==(int)sizeof(a));
    m1_logdb_start_usbcdc_tx();
    assert(usb.TxState==1); assert(flight_len==64);
    uint32_t tail_before = ring.tail;

    /* Disconnect cancels the in-flight transfer: the endpoint goes idle
     * with no completion callback ever having advanced the ring buffer. */
    usb.TxState=0; ready=0;
    m1_logdb_start_usbcdc_tx(); /* not ready: must be a pure no-op */
    assert(ring.tail==tail_before);
    assert(logdb_dma_tx_len==64); /* candidate bytes are still just "pending", not lost or double-counted */

    /* Host reconnects. */
    ready=1;
    m1_logdb_start_usbcdc_tx();
    /* The orphaned 64 bytes are reclaimed (ring advances past them, since
     * they are no longer USB-owned once the endpoint went idle) and a
     * fresh chunk of the REMAINING queued data is sent immediately --
     * no stall, no gap, no re-send of bytes already given up as lost. */
    assert(ring.tail==tail_before+64);
    assert(usb.TxState==1);
    assert(flight_len==64);
    memcpy(saved, inflight, 64);
    assert(memcmp(saved, a+64, 64)==0); /* exactly the NEXT 64 queued bytes, byte-correct */

    /* A genuine completion now arrives (host really received this one) and
     * the cycle continues correctly. */
    usb.TxState=0;
    m1_logdb_start_usbcdc_tx();
    assert(ring.tail==tail_before+128);
    assert(memcmp(inflight, a+128, 64)==0);
    assert(critical==0);
}

/* --- m1_logdb_write(): the second producer entry point (used by
 * M1_LOG_x()/m1_logdb_printf()), same no-wait contract as _write(). --- */
static void logdb_write_direct(void) {
    const char *a = "WWWWWWWWWW"; /* 10 chars, NUL-terminated: m1_logdb_write() takes a C string */
    reset();
    ready=0; m1_logdb_write(a); /* not ready: dropped, no crash, no wait */
    assert(ring.head==0);
    ready=1; m1_logdb_write(a); /* queued like any other producer call */
    assert(ring.head==10);
    assert(sends==0); /* write() only queues; it never itself calls CDC_Transmit_FS */
}

int main(void) {
    uint8_t a[2048], saved[64]; memset(a,'A',sizeof(a)); reset();
    assert(_write(1,NULL,4)==0); assert(_write(1,(char*)a,-1)==0);
    ready=0; assert(_write(1,(char*)a,20)==20); assert(ring.head==0); ready=1;
    locked=1; assert(m1_logdb_enqueue(a,20)==0); locked=0;
    in_isr=1; assert(m1_logdb_enqueue(a,20)==0); in_isr=0;
    scheduler=0; assert(m1_logdb_enqueue(a,20)==0); scheduler=1;
    assert(m1_logdb_enqueue(a,100)==100); assert(sends==0);
    m1_logdb_start_usbcdc_tx(); assert(flight_len==64); assert(ring.tail==0);
    memcpy(saved,inflight,64); memset(a,'B',sizeof(a));
    assert(m1_logdb_enqueue(a,sizeof(a))==1947);
    assert(memcmp(saved,inflight,64)==0); assert(m1_logdb_enqueue(a,1)==0);
    int before=sends; m1_logdb_start_usbcdc_tx(); assert(sends==before);
    usb.TxState=0; m1_logdb_update_tx_buffer(); assert(ring.tail==64);
    assert(m1_logdb_enqueue(a,64)==64); /* wrap while preserving queued bytes */
    fail_tx=1; cdc_tx_owner_mode=CDC_MODE_VCP;
    m1_logdb_start_usbcdc_tx(); assert(logdb_dma_tx_len==0);
    assert(ring.tail==64); assert(cdc_tx_owner_mode==CDC_MODE_VCP);
    fail_tx=0; m1_logdb_start_usbcdc_tx(); assert(logdb_dma_tx_len==64);
    ready=0; before=sends; m1_logdb_start_usbcdc_tx(); assert(sends==before);
    /* Simulate disconnect cancelling the in-flight USB transfer. */
    usb.TxState=0; ready=1; m1_logdb_start_usbcdc_tx(); assert(ring.tail==128);
    assert(logdb_dma_tx_len==64); assert(critical==0);
    puts("USB logging nonblocking/buffer lifetime tests PASS");

    stress_no_host();
    puts("9a: 10000 no-host-connected iterations PASS (zero sends, no wait)");
    stress_configured_unread();
    puts("9b: 10000 configured-but-unread iterations PASS (bounded, no wait)");
    stress_endpoint_busy();
    puts("9c: 10000 endpoint-remains-busy iterations PASS (no re-send, no wait)");
    disconnect_then_reconnect();
    puts("9d/9e: disconnect-during-transmission then reconnect PASS (byte-correct recovery)");
    logdb_write_direct();
    puts("m1_logdb_write() producer no-wait PASS");
}
'''

with tempfile.TemporaryDirectory(prefix='m1-log-test-') as directory:
    d = pathlib.Path(directory)
    # Use the production ring-buffer code, replacing only platform includes.
    ring_source = (ROOT / 'm1_csrc/m1_ring_buffer.c').read_text()
    ring_source = '\n'.join(line for line in ring_source.splitlines()
                            if not line.startswith('#include'))
    code = prefix + ring_source + '\n' + '\n'.join(function(s) for s in (
        'void m1_logdb_update_tx_buffer(void)',
        'static void m1_logdb_start_usbcdc_tx(void)',
        'static int m1_logdb_enqueue(const uint8_t *data, size_t len)',
        'int _write(int file, char *data, int len)',
        'void m1_logdb_write(const char* data)',
    )) + tests
    (d / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-g', '-fsanitize=address,undefined',
                    '-I'+str(ROOT / 'm1_csrc'), str(d / 'test.c'), '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True)

# Enforce absence of synchronous delays in both producer entry points.
for signature in ('static int m1_logdb_enqueue(const uint8_t *data, size_t len)',
                  'int _write(int file, char *data, int len)',
                  'void m1_logdb_write(const char* data)'):
    body = function(signature)
    assert all(token not in body for token in ('portMAX_DELAY', 'vTaskDelay(', 'HAL_Delay(', 'CDC_Transmit_FS('))
assert 'm1_logdb_write_direct_usbcdc' not in source
print('Producer no-wait structural checks PASS')
