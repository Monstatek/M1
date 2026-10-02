#!/usr/bin/env python3
"""Exercise production failure-path functions with host HAL/RTOS stubs.

Run from the repository root. ASan/UBSan checks do not replace hardware
acceptance of DMA, capture, UART reconnect or user-interface behavior.
"""
import re
import sys
import tempfile
from pathlib import Path
sys.dont_write_bytecode = True
from m1_pass3_helpers_test import source, function, build_run


def prompt(tmp):
    text = source('m1_csrc/m1_esp_uart_transport.c')
    code = r'''
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
static uint32_t tick, pos; static const char *input;
static int esp32_rb_hdl;
static uint32_t xTaskGetTickCount(void) { return tick; }
static void vTaskDelay(unsigned n) { tick += n; }
static unsigned m1_ringbuffer_read(int *r, uint8_t *p, unsigned n) {
    assert(r == &esp32_rb_hdl && n == 1);
    if (!input[pos]) return 0;
    *p = input[pos++]; return 1;
}
'''
    code += function(text, 'esp32_uart_read_until_prompt')
    code += function(text, 'esp32_uart_read_complete_prompt')
    code += r'''
int main(void) {
    char buf[16];
    const char *bad[] = {"", "partial", "ack>>", "01234567890123456789>> "};
    for (unsigned i=0; i<4; ++i) {
        input=bad[i]; pos=0; tick=UINT32_MAX-3;
        memset(buf, 'X', sizeof(buf));
        assert(esp32_uart_read_complete_prompt(buf,15,10,">> ")==0);
        assert(buf[15]=='X');
    }
    input="ack>> tail"; pos=tick=0;
    assert(esp32_uart_read_complete_prompt(buf,7,10,">> ")==6);
    assert(!strcmp(buf,"ack>> ") && pos==6);
    input="partial"; pos=tick=0;
    assert(esp32_uart_read_until_prompt(buf,16,10,">> ")==7);
    assert(!esp32_uart_read_complete_prompt(NULL,0,10,">> "));
    assert(!esp32_uart_read_complete_prompt(buf,16,10,""));
    puts("Prompt: complete, partial, truncation, exact fit, rollover and legacy PASS");
}
'''
    build_run(tmp, 'prompt', code)
    bt = source('m1_csrc/m1_bt.c')
    assert 'esp32_uart_read_until_prompt(' not in bt
    assert bt.count('esp32_uart_read_complete_prompt(') == 3


def dma_cleanup(tmp):
    text = source('lfrfid/lfrfid_dma_tx.c')
    code = r'''
#define HAL_OK 0
typedef struct { void *Instance; unsigned state; } DMA_HandleTypeDef;
typedef struct { unsigned live; } DMA_QListTypeDef;
static DMA_HandleTypeDef s_hdma_bsrr, s_hdma_arr;
static DMA_QListTypeDef s_qlist_bsrr, s_qlist_arr;
static unsigned calls, fail_mask;
static int HAL_DMA_Abort(DMA_HandleTypeDef *h) {
    assert(h->Instance); ++calls; return -1; /* RESET/ERROR is allowed */
}
static int HAL_DMAEx_List_DeInit(DMA_HandleTypeDef *h) {
    assert(h->Instance); ++calls;
    return (fail_mask & (h==&s_hdma_bsrr ? 1U : 2U)) ? -1 : HAL_OK;
}
'''
    code += function(text, 'dma_release_all')
    code += r'''
int main(void) {
    assert(dma_release_all() && calls==0); /* stop before first start */
    for (unsigned mask=0; mask<4; ++mask) {
        for (fail_mask=0; fail_mask<4; ++fail_mask) {
            s_hdma_bsrr.Instance = (mask&1) ? &s_hdma_bsrr : NULL;
            s_hdma_arr.Instance = (mask&2) ? &s_hdma_arr : NULL;
            s_qlist_bsrr.live=s_qlist_arr.live=7;
            calls=0;
            assert(dma_release_all() == ((mask & fail_mask)==0));
            assert(calls == 2*((!!(mask&1))+(!!(mask&2))));
            assert(!!s_hdma_bsrr.Instance == !!(mask&fail_mask&1));
            assert(!!s_hdma_arr.Instance == !!(mask&fail_mask&2));
            if (mask&fail_mask&1) assert(s_qlist_bsrr.live==7);
            if (mask&fail_mask&2) assert(s_qlist_arr.live==7);
        }
    }
    fail_mask=0; assert(dma_release_all());
    calls=0; assert(dma_release_all() && calls==0);
    puts("DMA: uninitialized/partial init, deinit timeout, retry, repeated stop PASS");
}
'''
    build_run(tmp, 'dma_cleanup', code)
    start = function(text, 'lfrfid_dma_tx_start')
    assert start.index('if (!dma_release_all())') < start.index('dma_ensure_buffers()')
    lf = function(source('lfrfid/lfrfid.c'), 'lfrfidThread')
    assert 'lfrfid_dma_tx_check_health()' in lf and 'pdMS_TO_TICKS(100)' in lf
    assert '!lfrfid_dma_tx_active()' in lf and 'LFRFID_STATE_ERROR' in lf


def rollover(tmp):
    text = source('m1_csrc/m1_capture_link.c')
    clauses = re.findall(r'while \(\(uint32_t\)\(HAL_GetTick\(\) - (\w+)\) < (\w+)', text)
    assert len(clauses) == 6, clauses
    assert 'HAL_GetTick() < t_end' not in text
    code = r'''
static uint32_t tick;
static uint32_t HAL_GetTick(void) { return tick; }
int main(void) {
    for (uint32_t offset=0; offset<1200; offset+=17) {
      for (uint32_t duration_ms=0; duration_ms<=1100; duration_ms+=100) {
        uint32_t ready_start=UINT32_MAX-offset, drain_start=ready_start;
        uint32_t capture_start=ready_start, t_start=ready_start;
        for (uint32_t elapsed=0; elapsed<=1200; ++elapsed) {
          tick=ready_start+elapsed;
'''
    for start, limit in clauses:
        code += f'assert(((uint32_t)(HAL_GetTick() - {start}) < {limit}) == (elapsed < {limit}));\n'
    code += '}}} puts("Capture: all six production deadline expressions across tick wrap PASS");}\n'
    build_run(tmp, 'rollover', code)


def overflow_abort(tmp):
    text = source('m1_csrc/m1_sub_ghz.c')
    code = r'''
enum { SUBGHZ_RECORD_DISPLAY_PARAM_ACTIVE=1, SUBGHZ_RECORD_DISPLAY_PARAM_SYS_ERROR=2,
       SUB_GHZ_OPMODE_ISOLATED=0, LED_BLINK_ON_RGB=0, LED_FASTBLINK_PWM_OFF=0,
       LED_FASTBLINK_ONTIME_OFF=0 };
static unsigned subghz_capture_overflow, subghz_uiview_gui_latest_param;
static struct {int band;} subghz_scan_config;
static unsigned paused,stopped,deinit,discarded,isolated,led,display;
#define M1_LOG_E(...) ((void)0)
static void sub_ghz_rx_pause(void) { ++paused; }
static void m1_sdm_task_stop(void) { ++stopped; }
static void m1_sdm_task_deinit(void) { assert(stopped); ++deinit; }
static void sub_ghz_raw_samples_deinit(bool discard) { assert(deinit && discard); ++discarded; }
static void sub_ghz_set_opmode(int a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d;++isolated; }
static void fb_net_read_stop(void) { ++led; }
static void m1_uiView_display_update(int state) { display=state; subghz_uiview_gui_latest_param=state; }
'''
    code += function(text, 'subghz_record_abort_overflow')
    code += r'''
int main(void) {
    subghz_uiview_gui_latest_param=1;
    assert(!subghz_record_abort_overflow(false));
    subghz_capture_overflow=1;
    assert(subghz_record_abort_overflow(false));
    assert(paused==1 && stopped==1 && deinit==1 && discarded==1 && isolated==1 && led==1 && display==2);
    assert(!subghz_record_abort_overflow(false));
    subghz_uiview_gui_latest_param=1;
    assert(subghz_record_abort_overflow(true) && paused==1);
    puts("RAW overflow: abort, discard temporary capture, error, repeated handling PASS");
}
'''
    build_run(tmp, 'overflow_abort', code)
    irq = source('m1_csrc/m1_int_hdl.c')
    assert 'm1_ringbuffer_insert(&subghz_rx_rawdata_rb' not in irq
    assert 'm1_ringbuffer_write(&subghz_rx_rawdata_rb' in irq
    assert 'subghz_capture_overflow = 1' in irq
    assert text.count('subghz_record_abort_overflow(true)') == 2
    assert '_n = m1_ringbuffer_read(' in text
    assert 'n_samples_to_rw = m1_ringbuffer_read(' in text


def uart_order():
    text = source('m1_csrc/m1_esp32_hal.c')
    init = function(text, 'esp32_UART_init')
    end = function(text, 'esp32_UART_deinit')
    assert init.count('xSemaphoreCreateBinary()') == 1
    assert 'if (!sem_esp32_trans)' in init
    assert init.index('if (!pesp32_rx || !sem_esp32_trans)') < init.index('m1_ringbuffer_init(')
    assert init.index('m1_ringbuffer_init(') < init.index('esp32_UART_DMA_init()')
    assert init.index('esp32_UART_DMA_init()') < init.index('HAL_NVIC_EnableIRQ(ESP32_UART_IRQn)')
    assert end.index('HAL_NVIC_DisableIRQ(ESP32_UART_IRQn)') < end.index('HAL_UART_DeInit(')
    assert end.index('HAL_DMA_DeInit(') < end.index('free(pesp32_rx)')
    assert end.index('HAL_UART_DeInit(') < end.index('__HAL_RCC_UART4_CLK_DISABLE()')
    assert end.index('memset(&esp32_rb_hdl, 0,') < end.index('free(pesp32_rx)')
    print('UART lifecycle ordering and lifetime semaphore guards PASS (source assertions)')


def ring_interleaving(tmp):
    text = source('m1_csrc/m1_ring_buffer.c')
    code = source('m1_csrc/m1_ring_buffer.h')
    code += r'''
#define GET_MIN_NUM(a,b) ((a)<(b)?(a):(b))
#define IS_BUFFER_VALID(p) ((p) && (p)->pdata && (p)->len && (p)->data_size)
static S_M1_RingBuffer rb;
static uint16_t incoming;
static unsigned inject,attempted,accepted;
static void *interleaved_copy(void *dst,const void *src,size_t n) {
    if (inject) {
        inject=0; ++attempted;
        uint32_t tail=rb.tail;
        accepted=m1_ringbuffer_write(&rb,(uint8_t*)&incoming,1);
        assert(rb.tail==tail); /* producer must never move the consumer cursor */
    }
    return memcpy(dst,src,n);
}
'''
    names = ('m1_ringbuffer_init', 'ringbuffer_get_empty_slots', 'ringbuffer_get_data_slots',
             'm1_ringbuffer_write', 'm1_ringbuffer_get_read_len', 'm1_ringbuffer_get_read_address',
             'm1_ringbuffer_advance_read')
    code += '\n'.join(function(text, n) for n in names)
    code += '\n#define memcpy interleaved_copy\n' + function(text, 'm1_ringbuffer_read')
    code += r'''
#undef memcpy
int main(void) {
    uint16_t storage[8], out[8];
    for (unsigned wrap=0; wrap<8; ++wrap) {
      for (unsigned fill=1; fill<=7; ++fill) {
        m1_ringbuffer_init(&rb,(uint8_t*)storage,8,2);
        rb.head=rb.tail=wrap*2;
        for(uint16_t i=0;i<fill;++i) assert(m1_ringbuffer_write(&rb,(uint8_t*)&i,1)==1);
        incoming=1234; attempted=accepted=0; inject=1;
        assert(m1_ringbuffer_read(&rb,(uint8_t*)out,fill)==fill);
        for(unsigned i=0;i<fill;++i) assert(out[i]==i);
        assert(attempted==1 && accepted==(fill<7));
        if(accepted) assert(m1_ringbuffer_read(&rb,(uint8_t*)out,1)==1 && out[0]==1234);
        assert(m1_ringbuffer_read(&rb,(uint8_t*)out,1)==0);
      }
    }
    puts("Ring: ISR arrival during read, full rejection, all wrap positions PASS");
}
'''
    build_run(tmp, 'ring_interleaving', code)


def dma_health(tmp):
    text = source('lfrfid/lfrfid_dma_tx.c')
    code = r'''
enum { DMA_FLAG_DTE=1, DMA_FLAG_ULE=2, DMA_FLAG_USE=4 };
static unsigned s_hdma_bsrr,s_hdma_arr,stops;
static bool s_active;
#define __HAL_DMA_GET_FLAG(h,f) (*(h)&(f))
static void lfrfid_dma_tx_stop(void) { ++stops; s_active=false; }
'''
    code += function(text[text.index('bool lfrfid_dma_tx_check_health(void)'):],
                     'lfrfid_dma_tx_check_health')
    code += r'''
int main(void) {
    for(unsigned a=0;a<8;++a) for(unsigned b=0;b<8;++b) {
        s_hdma_bsrr=a;s_hdma_arr=b;stops=0;s_active=true;
        assert(lfrfid_dma_tx_check_health()==!(a|b));
        assert(stops==!!(a|b));
        if(a|b) assert(lfrfid_dma_tx_check_health() && stops==1);
    }
    puts("DMA health: all channel error combinations and idempotence PASS");
}
'''
    build_run(tmp, 'dma_health', code)


def uart_resources(tmp):
    # Execute the actual init prologue; register setup is covered separately
    # by the ARM build and the source ordering assertions, not simulated here.
    text = function(source('m1_csrc/m1_esp32_hal.c'), 'esp32_UART_init')
    prologue = text[:text.index('    GPIO_InitTypeDef')] + '}\n'
    code = r'''
enum { ESP32_UART_IRQn=1, ESP32_UART_DMA_Tx_IRQn=2, ESP32_RX_BUFFER_LEN=16 };
static int esp32_uart_init_done,fail_alloc,fail_sem,errors,created,disabled;
static uint8_t *pesp32_rx;
static int token; static int *sem_esp32_trans;
static struct { uint8_t *pdata; } esp32_rb_hdl;
static void HAL_NVIC_DisableIRQ(int irq) { disabled |= irq; }
static void *test_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
static int *xSemaphoreCreateBinary(void) { if(fail_sem) return NULL; ++created; return &token; }
static int xSemaphoreGive(int *p) { assert(p==&token); return 1; }
static void Error_Handler(void) { ++errors; }
#define m1_ringbuffer_init(r,p,n,s) do { assert(disabled==3 && (n)==16 && (s)==1); (r)->pdata=(p); } while(0)
#define malloc test_malloc
'''
    code += prologue
    code += r'''
#undef malloc
int main(void) {
    fail_alloc=1; esp32_UART_init(); assert(errors==1 && !esp32_rb_hdl.pdata);
    assert(created==1); fail_alloc=0;
    for(unsigned i=0;i<100;++i) {
        esp32_UART_init(); assert(esp32_rb_hdl.pdata==pesp32_rx && pesp32_rx);
        free(pesp32_rx); pesp32_rx=NULL; esp32_rb_hdl.pdata=NULL;
    }
    assert(created==1); /* no semaphore leak across reconnects */
    sem_esp32_trans=NULL; fail_sem=1; esp32_UART_init();
    assert(errors==2 && !esp32_rb_hdl.pdata);
    fail_sem=0; esp32_UART_init(); assert(created==2 && esp32_rb_hdl.pdata);
    free(pesp32_rx);
    puts("UART resources: allocation failures, retry and 100 reconnects PASS");
}
'''
    build_run(tmp, 'uart_resources', code)


def dma_start(tmp):
    text = source('lfrfid/lfrfid_dma_tx.c')
    code = r'''
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#endif
typedef int lfrfid_dma_tx_status_t;
typedef struct { int unused; } EncodedTx_Data_t;
typedef struct { void *Instance; } DMA_HandleTypeDef;
static DMA_HandleTypeDef s_hdma_bsrr,s_hdma_arr;
static int s_node_bsrr_prologue,s_node_bsrr_body,s_qlist_bsrr;
static int s_node_arr_prologue,s_node_arr_body,s_qlist_arr;
static uint32_t s_bsrr[2]={22,44},s_arr[2]={33,55};
static uint16_t s_nsteps;
static bool s_active;
static struct {uint32_t CR1,PSC,CNT,SR,DIER,ARR;} timer;
static struct {uint32_t BSRR;} gpio;
#define TIM5 (&timer)
#define GPIOA (&gpio)
#define GPDMA2_Channel0 (&s_hdma_bsrr)
#define GPDMA2_Channel1 (&s_hdma_arr)
#define GPDMA2_REQUEST_TIM5_UP 92
#define HAL_OK 0
#define LFRFID_DMA_TX_OK 0
#define LFRFID_DMA_TX_MAX_STEPS 2
#define TIM_DIER_UDE 1
#define TIM_CR1_CEN 1
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPDMA2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM5_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM5_CLK_DISABLE() ((void)0)
static unsigned failure,builds,starts,releases;
static bool dma_release_all(void) { ++releases; return failure!=1; }
static bool dma_ensure_buffers(void) { return failure!=2; }
static uint32_t dma_tim5_hz(void) { return 1000000; }
static void dma_pa2_force_inactive(void) { gpio.BSRR=0; }
static int lfrfid_dma_tx_prepare(const EncodedTx_Data_t *w,uint32_t hz,uint32_t *b,
                                uint32_t *a,unsigned cap,uint16_t *n) {
    (void)w;(void)hz;(void)b;(void)a;(void)cap;*n=2;return failure==3?-1:0;
}
static bool dma_build_channel(DMA_HandleTypeDef *h,void *inst,int *p,int *b,int *q,
                              const uint32_t *buf,uint16_t n,uint32_t req,uint32_t dst) {
    (void)p;(void)b;(void)q;(void)buf;(void)n;(void)req;(void)dst;
    h->Instance=inst; ++builds;return failure!=(builds==1?4U:5U);
}
static int HAL_DMAEx_List_Start(DMA_HandleTypeDef *h) {
    assert(h->Instance);++starts;return failure==(starts==1?6U:7U)?-1:0;
}
void lfrfid_dma_tx_stop(void);
'''
    code += function(text, 'lfrfid_dma_tx_start')
    code += function(text[text.index('void lfrfid_dma_tx_stop(void)'):], 'lfrfid_dma_tx_stop')
    code += r'''
int main(void) {
    EncodedTx_Data_t wave={0};
    for(failure=1;failure<=7;++failure) {
        builds=starts=releases=0;s_active=false;memset(&timer,0,sizeof(timer));gpio.BSRR=0;
        assert(!lfrfid_dma_tx_start(&wave));
        assert(!s_active && !timer.CR1 && !timer.DIER && !gpio.BSRR);
        if(failure>=4) assert(releases==2); /* complete stop on partial start */
    }
    failure=0;builds=starts=0;
    assert(lfrfid_dma_tx_start(&wave));
    assert(s_active && timer.CR1==1 && timer.DIER==1 && timer.ARR==33 && gpio.BSRR==22);
    lfrfid_dma_tx_stop(); assert(!s_active && !timer.CR1 && !timer.DIER && !gpio.BSRR && !s_nsteps);
    puts("DMA start: seven failure stages fail closed; successful prime/arm preserved PASS");
}
'''
    build_run(tmp, 'dma_start', code)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='m1-pass5-host-') as directory:
        for test in (prompt, dma_cleanup, dma_start, dma_health, rollover, overflow_abort,
                     ring_interleaving, uart_resources):
            test(Path(directory))
    uart_order()
    print('Pass 5 targeted reliability checks PASS; hardware acceptance not asserted.')
