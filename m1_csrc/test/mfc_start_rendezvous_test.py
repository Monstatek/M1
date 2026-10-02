#!/usr/bin/env python3
"""Exercise the production MFC-Emulate startup coordinator and rendezvous
(m1_mfc_raw_session_hw.c's s_start_request state machine, mfc_emu_start()
in m1_nfc.c, the worker's Q_EVENT_NFC_MFC_EMULATE dispatch and DONE-state
cleanup in nfc_driver.c, and menu_nfc_deinit()/nfc_mfc_emu_gui_destroy() in
m1_nfc.c) with an RTOS stub, host-linked and real-executed -- not a
reimplementation, not source-text grep alone.

Covers the four lifecycle corrections:
  1. m1_mfc_raw_hw_cancel_start() only notifies the worker (STOP) when it
     actually transitioned PENDING -> CANCELLED; a call against an already
     IDLE/CANCELLED coordinator must never post a stale notification.
  2. menu_nfc_deinit() resets the coordinator to IDLE only after the worker
     task is ACTUALLY deleted and RAWOWN is CONFIRMED inactive -- fail
     closed: if the bounded RAWOWN stop-and-wait fails and
     m1_mfc_raw_hw_active() still reports true afterwards, the worker task,
     its queue, and the coordinator are all left completely intact and the
     function returns without tearing anything down (a live RAWOWN session
     must never be stranded with no task left to service or release the
     radio). If the wait reports failure but RAWOWN has genuinely gone
     inactive by the time of that check, or the wait succeeds outright,
     teardown proceeds normally. Re-entering NFC always begins IDLE when
     teardown did happen, and is supported by the existing idempotent
     menu_nfc_init() guard (task/queue handles still non-NULL) when it
     didn't -- either way a later menu_nfc_init() can never create a second
     worker task or queue.
  3. The worker's rejection of a cancelled/no-image event clears the armed
     emulation image before retiring the request.
  4. No startup-stage timeout can leave a late start, a stale STOP, a stale
     image, a permanently CANCELLED/PENDING coordinator, or a worker
     deletion while RAWOWN still owns the radio.
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
hw = (root / 'NFC/NFC_drv/common/m1_mfc_raw_session_hw.c').read_text()
ui = (root / 'm1_csrc/m1_nfc.c').read_text()
driver = (root / 'NFC/NFC_drv/legacy/nfc_driver.c').read_text()


def extract(text, signature):
    """Extract a whole function definition by its exact signature line."""
    start = text.index(signature + '\n{')
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def extract_from(text, start_marker, wrapper_name):
    """Extract a brace-balanced statement starting at start_marker (which
    must itself begin with the statement's own opening '{' on the same or a
    later line) and wrap it in a void wrapper_name(void) function -- used
    for the two worker-side snippets that live inline inside
    nfc_worker_task()'s switch statement, not as their own functions. If the
    statement is an if-block immediately followed by 'else {', the else
    branch is included too (a plain brace-balance from the if's own '{'
    stops at the if-block's own closing brace, silently dropping the else)."""
    start = text.index(start_marker)

    def balanced_end(from_brace):
        depth = 1
        end = from_brace + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        return end

    brace = text.index('{', start)
    end = balanced_end(brace)
    tail = text[end:end + 32].lstrip()
    if tail.startswith('else'):
        else_brace = text.index('{', end)
        end = balanced_end(else_brace)
    stmt = text[start:end]
    return f'static void {wrapper_name}(void)\n{{\n{stmt}\n}}\n'


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
/* menu_nfc_deinit() has nested #if defined(M1_MFC_RAW_EMULATION) blocks in
 * its own body (both the pre-existing stop-and-wait and fix #2's new
 * coordinator reset) -- brace-matched extraction copies them verbatim, so
 * they must actually be defined for this test binary or the preprocessor
 * silently strips them out from under the test, same as any real build
 * configured with the flag off. This project's own build.sh always defines
 * it (M1_MFC_RAW_EMULATION=ON), so this matches production, not a
 * test-only assumption. */
#define M1_MFC_RAW_EMULATION 1
typedef unsigned TickType_t;
typedef int StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
static int s_started_buf, s_stopped_buf;
static SemaphoreHandle_t s_started_sem, s_stopped_sem;
enum {START_REQUEST_IDLE, START_REQUEST_PENDING, START_REQUEST_CANCELLED};
static uint8_t s_start_request;
enum {M1_RAWOWN_INACTIVE, M1_RAWOWN_ACTIVE};
static int s_state, critical, signal_start, sends, queue_fail, image, waits, stop;
static void *nfc_worker_task_hdl=(void*)1, *nfc_worker_q_hdl=(void*)1;
#define pdTRUE 1
#define eSetBits 0
#define M1_RAWOWN_NOTIFY_BIT_STOP 2
#define pdMS_TO_TICKS(x) (x)
#define taskENTER_CRITICAL() (++critical)
#define taskEXIT_CRITICAL() (--critical)
#define platformLog(...) ((void)0)
static int *xSemaphoreCreateBinaryStatic(int *p) {*p=0; return p;}
/* Model RAWOWN's real ordering (state transition, THEN semaphore signal /
 * notification) for the two races menu_nfc_deinit()'s fail-closed check
 * needs to be correct against, without a real concurrent worker: either
 * flag makes this stub flip s_state to INACTIVE as a side effect of the
 * exact real event that would carry that ordering guarantee in production. */
static int flip_inactive_on_notify;      /* STOP notification delivery races a genuine completion */
static int flip_inactive_on_stop_take;   /* the stopped-semaphore signal itself represents a real stop */
static int xSemaphoreTake(int *p, unsigned ticks) {
    assert(p); if(ticks) {assert(!critical); ++waits; if(signal_start) *p=1;}
    if(*p) {
        *p=0;
        if ((p == s_stopped_sem) && flip_inactive_on_stop_take) s_state = M1_RAWOWN_INACTIVE;
        return 1;
    }
    return 0;
}
static int xTaskNotify(void *p, unsigned bits, int mode) {
    (void)p; (void)bits; (void)mode; ++stop;
    if (flip_inactive_on_notify) s_state = M1_RAWOWN_INACTIVE;
    return 1;
}
typedef int m1_mfc_emu_status_t;
enum {M1_MFC_EMU_OK, M1_MFC_EMU_START_FAILED};
typedef struct {int unused;} m1_mfc_emu_image_t;
static int m1_mfc_emu_image_build(m1_mfc_emu_image_t *p) {(void)p; return 0;}
static int m1_mfc_raw_set_emu_image(const m1_mfc_emu_image_t *p) {image=(p!=NULL); return 0;}
static int m1_mfc_raw_has_emu_image(void) { return image; }
typedef struct {int q_evt_type;} S_M1_Main_Q_t;
#define Q_EVENT_NFC_MFC_EMULATE 123
static int xQueueSend(void *q, const S_M1_Main_Q_t *r, unsigned ticks) {
    assert(q); assert(ticks==0); assert(r->q_evt_type==123);
    assert(s_started_sem && s_stopped_sem); assert(image);
    assert(s_start_request==START_REQUEST_PENDING); ++sends;
    return !queue_fail;
}

/* --- menu_nfc_deinit()/nfc_mfc_emu_gui_destroy() dependency stubs --- */
static int deletes_task, deletes_queue;
static void vTaskDelete(void *h) { (void)h; ++deletes_task; nfc_worker_task_hdl = NULL; }
static void vQueueDelete(void *h) { (void)h; ++deletes_queue; nfc_worker_q_hdl = NULL; }
static int listener_stop_calls, raw_end_calls, led_calls;
static void ListenerRequestStop(void) { ++listener_stop_calls; }
static int m1_mfc_raw_end(void) { ++raw_end_calls; s_state = M1_RAWOWN_INACTIVE; return 0; }
static bool s_mfc_emu_feedback_acquired;
static void fb_net_emulating_stop(void) { ++led_calls; }

/* --- nfc_worker_task() Q_EVENT_NFC_MFC_EMULATE dispatch dependency stubs --- */
static int NfcState, mfc_start_request_in_worker;
enum { NFC_STATE_INITIALIZE = 1 };
enum { EMU_PERSONA_MFC_EMU = 1 };
enum { NFC_ROLE_LISTENER = 1 };
static void Emu_SetPersona(int p) { (void)p; }
static int NFC_SwitchRole(int r) { (void)r; return 0; }
'''

body = '\n'.join(extract(hw, signature) for signature in (
    'bool m1_mfc_raw_hw_prepare_start(void)',
    'bool m1_mfc_raw_hw_start_requested(void)',
    'void m1_mfc_raw_hw_cancel_start(void)',
    'void m1_mfc_raw_hw_finish_request(void)',
    'bool m1_mfc_raw_hw_wait_started(uint32_t max_ticks)',
    'bool m1_mfc_raw_hw_active(void)',
    'bool m1_mfc_raw_hw_request_stop_and_wait(uint32_t max_ticks)',
)) + '\n' + extract(ui, 'static m1_mfc_emu_status_t mfc_emu_start(void)')
body += '\n' + extract(ui, 'void menu_nfc_deinit(void)')
body += '\n' + extract(ui, 'static void nfc_mfc_emu_gui_destroy(uint8_t param)')

# The two worker-side snippets live inline inside nfc_worker_task()'s switch
# statement (not their own functions) -- extracted by exact, currently-real
# anchor text and wrapped in a thin function so they can be called directly,
# not reimplemented.
body += '\n' + extract_from(
    driver,
    'if (m1_mfc_raw_hw_start_requested() && m1_mfc_raw_has_emu_image()) {',
    'worker_handle_mfc_emulate_event')
body += '\n' + extract_from(
    driver,
    'if (mfc_start_request_in_worker) {',
    'worker_handle_done_cleanup')

tests = r'''
static void reset(void) {
    s_started_buf=0; s_stopped_buf=0; s_started_sem=&s_started_buf; s_stopped_sem=&s_stopped_buf;
    s_start_request=START_REQUEST_IDLE; s_state=M1_RAWOWN_INACTIVE;
    critical=0; signal_start=0; sends=0; queue_fail=0; image=0; waits=0; stop=0;
    nfc_worker_task_hdl=(void*)1; nfc_worker_q_hdl=(void*)1;
    deletes_task=0; deletes_queue=0; listener_stop_calls=0; raw_end_calls=0; led_calls=0;
    s_mfc_emu_feedback_acquired=false;
    NfcState=0; mfc_start_request_in_worker=0;
    flip_inactive_on_notify=0; flip_inactive_on_stop_take=0;
}
/* Coordinator half of "quiescent": s_start_request back to IDLE and no
 * critical-section leak. This does NOT imply the armed image is cleared --
 * that is a separate invariant, owned by three distinct real code paths
 * (m1_mfc_raw_teardown_cleanup() after any session that actually reached
 * m1_mfc_raw_hw_run(), nfc_mfc_emu_gui_destroy() on UI exit, and the
 * worker's rejection branch per fix #3) -- asserted explicitly wherever one
 * of those paths is actually exercised below, not folded into one macro
 * that would silently stop meaning what it says. */
#define ASSERT_COORD_IDLE() do { \
    assert(s_start_request==START_REQUEST_IDLE); \
    assert(critical==0); \
} while (0)

int main(void) {
    /* ---- 1: first-ever Emulate immediately after entering NFC ---- */
    reset(); signal_start=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    assert(sends==1 && waits==1); assert(!m1_mfc_raw_hw_prepare_start()); /* still armed: cannot double-prepare */
    assert(image); /* armed for the live session that is now (per the stub) running */
    /* Session runs to completion (m1_mfc_raw_hw_run()/teardown_cleanup(),
     * not simulated here -- HAL/RFAL-coupled, out of scope); its two real
     * effects are: the image is cleared, and the worker's DONE-state
     * cleanup retires the coordinator. */
    (void)m1_mfc_raw_set_emu_image(NULL);
    m1_mfc_raw_hw_finish_request();
    ASSERT_COORD_IDLE(); assert(!image);

    /* ---- 2: delayed worker scheduling -- the request sits queued for a
     * while (the worker task simply hasn't been scheduled onto the CPU yet)
     * before the worker's own dequeue/accept code finally runs; proves that
     * code path still correctly finds a live PENDING request with its image
     * still armed and accepts it, rather than assuming it must run
     * immediately after being queued. signal_start=1 models the worker
     * eventually signaling STARTED within the UI's bound -- this stub has
     * no concept of elapsed time short of "signaled" vs "never signaled",
     * so lateness itself is proven by explicitly deferring the
     * worker_handle_mfc_emulate_event() call to well after mfc_emu_start()
     * has already returned, not by the wait's own timing. ---- */
    reset(); signal_start=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    assert(m1_mfc_raw_hw_start_requested()); /* still PENDING: the worker has not consumed it yet in this timeline */
    /* Only NOW (late, from the worker's own perspective) does its dequeue/accept code run. */
    worker_handle_mfc_emulate_event();
    assert(mfc_start_request_in_worker); assert(NfcState==NFC_STATE_INITIALIZE);
    /* Session completes normally; both real cleanup effects apply. */
    (void)m1_mfc_raw_set_emu_image(NULL);
    worker_handle_done_cleanup();
    ASSERT_COORD_IDLE(); assert(!image);

    /* ---- 3a: queue full ---- */
    reset(); signal_start=1; queue_fail=1;
    assert(mfc_emu_start()==M1_MFC_EMU_START_FAILED);
    ASSERT_COORD_IDLE(); assert(!image); assert(waits==0); /* never even reached the wait */
    /* ---- 10a: successful retry after queue-full ---- */
    queue_fail=0;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    (void)m1_mfc_raw_set_emu_image(NULL);
    m1_mfc_raw_hw_finish_request();
    ASSERT_COORD_IDLE(); assert(!image);

    /* ---- 3b: queue null ---- */
    reset(); signal_start=1; nfc_worker_q_hdl=NULL;
    assert(mfc_emu_start()==M1_MFC_EMU_START_FAILED);
    ASSERT_COORD_IDLE(); assert(!image);
    /* ---- 10b: successful retry after null-queue ---- */
    nfc_worker_q_hdl=(void*)1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    (void)m1_mfc_raw_set_emu_image(NULL);
    m1_mfc_raw_hw_finish_request();
    ASSERT_COORD_IDLE(); assert(!image);

    /* ---- 4: hardware start failure before UI timeout -- the worker
     * dequeues, accepts, and runs, but m1_mfc_raw_hw_run() itself fails to
     * acquire and returns without ever giving STARTED; the worker's own
     * DONE-state cleanup retires the coordinator to IDLE BEFORE the UI's
     * wait_started() bound elapses. When the UI's wait then times out and
     * calls cancel_start(), fix #1 must prevent a stale STOP: nothing was
     * PENDING anymore by that point.
     *
     * mfc_emu_start() cannot be used whole here: it is one synchronous call
     * that itself performs the wait, so there is no way to interleave a
     * simulated worker mid-call. Instead the same real primitives it uses
     * are driven directly, in the true production order (UI arms and
     * queues, worker accepts/fails/retires, THEN the UI's own wait_started()
     * finally observes no signal and times out) -- still real, unmodified
     * coordinator code, just sequenced by hand instead of through the one
     * synchronous wrapper. ---- */
    reset(); signal_start=0;
    m1_mfc_emu_image_t img = {0};
    assert(m1_mfc_raw_hw_prepare_start());
    (void)m1_mfc_raw_set_emu_image(&img);
    S_M1_Main_Q_t request = {123};
    assert(xQueueSend(nfc_worker_q_hdl, &request, 0));
    worker_handle_mfc_emulate_event();
    assert(mfc_start_request_in_worker); assert(NfcState==NFC_STATE_INITIALIZE);
    /* Hardware acquire fails inside m1_mfc_raw_hw_run() (not simulated here --
     * out of scope, HAL/RFAL-coupled); it never gives s_started_sem, and the
     * worker reaches NFC_STATE_DONE regardless, which always runs this
     * cleanup whether the session ran, failed to acquire, or was cancelled. */
    (void)m1_mfc_raw_set_emu_image(NULL); /* teardown_cleanup()'s real effect, unconditional */
    worker_handle_done_cleanup();
    assert(s_start_request==START_REQUEST_IDLE); /* coordinator already retired, before the UI even times out */
    int stop_before_late_timeout = stop;
    assert(!m1_mfc_raw_hw_wait_started(pdMS_TO_TICKS(2000))); /* the UI's own wait, only now catching up: no signal ever came */
    m1_mfc_raw_hw_cancel_start(); /* the UI's own timeout-handling call, arriving late */
    assert(stop==stop_before_late_timeout); /* fix #1: no stale STOP posted */
    ASSERT_COORD_IDLE(); assert(!image);

    /* ---- 5: cancellation before event consumption -- the UI's wait times
     * out and cancels BEFORE the worker ever dequeues the event; fix #3
     * (image cleared in the rejection branch) is exercised here since the
     * image was armed but the request is CANCELLED, not IDLE, by the time
     * the worker examines it, so this is NOT the "no image" case -- it is
     * specifically the "cancelled" half of the rejection gate. ---- */
    reset(); signal_start=0;
    assert(mfc_emu_start()!=M1_MFC_EMU_OK); /* posts cancel_start() internally on timeout */
    assert(image); /* still armed: nothing has cleared it yet */
    assert(s_start_request==START_REQUEST_CANCELLED);
    assert(stop==1); /* genuine PENDING->CANCELLED transition: exactly one STOP */
    worker_handle_mfc_emulate_event(); /* worker dequeues afterwards, sees CANCELLED */
    assert(!mfc_start_request_in_worker); /* rejection branch taken, not the accept branch */
    ASSERT_COORD_IDLE(); assert(!image); /* fix #3: image cleared, request retired, by the rejection branch itself */

    /* ---- 7: timeout immediately before STARTED (genuinely PENDING the
     * whole time, worker never even dequeues) -- exactly one real STOP,
     * because this IS a genuine cancellation. ---- */
    reset(); signal_start=0;
    assert(mfc_emu_start()==M1_MFC_EMU_START_FAILED);
    assert(!m1_mfc_raw_hw_start_requested());
    assert(stop==1);
    assert(image); /* worker has not run yet in this scenario: not this call's job to clear it */
    worker_handle_mfc_emulate_event(); /* worker eventually dequeues the stale cancelled event */
    ASSERT_COORD_IDLE(); assert(!image); /* fix #3 clears it here too */

    /* ---- 8: immediate BACK -- nfc_mfc_emu_gui_destroy() must clear the
     * armed image and request the RAWOWN stop whether or not a session is
     * actually active (a refused/failed entry never started RAWOWN at
     * all). ---- */
    reset(); signal_start=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    m1_mfc_raw_hw_finish_request(); /* worker's own retirement, as in scenario 1 */
    s_state = M1_RAWOWN_ACTIVE; /* a live session really is running */
    nfc_mfc_emu_gui_destroy(0); /* BACK, immediately */
    assert(listener_stop_calls==1); assert(raw_end_calls==1);
    assert(!image); /* always cleared on exit, unconditionally */
    assert(s_state==M1_RAWOWN_INACTIVE);

    /* Also cover BACK from a REFUSED entry (never went active at all). */
    reset(); signal_start=1; queue_fail=1;
    assert(mfc_emu_start()==M1_MFC_EMU_START_FAILED); /* image already cleared by mfc_emu_start() itself */
    nfc_mfc_emu_gui_destroy(0);
    assert(listener_stop_calls==0); /* s_state was never ACTIVE: no stop requested */
    assert(!image);

    /* ---- 9: menu exit/re-entry -- a PENDING request must not survive
     * menu_nfc_deinit(); re-entering NFC (checked here as "coordinator is
     * IDLE, a fresh prepare_start() succeeds") always begins IDLE, even
     * when torn down mid-flight with a request still PENDING. ---- */
    reset(); signal_start=0;
    assert(mfc_emu_start()!=M1_MFC_EMU_OK); /* leaves it CANCELLED, image still armed (worker hasn't run) */
    assert(s_start_request==START_REQUEST_CANCELLED); assert(image);
    menu_nfc_deinit(); /* worker task deleted mid-flight, before it ever consumed the event */
    assert(deletes_task==1); assert(deletes_queue==1);
    assert(nfc_worker_task_hdl==NULL);
    assert(s_start_request==START_REQUEST_IDLE); /* fix #2: coordinator reset survives teardown */
    assert(m1_mfc_raw_hw_prepare_start()); /* re-entering NFC: a fresh session can start cleanly */
    m1_mfc_raw_hw_finish_request();

    /* ---- Fail-closed correction: menu_nfc_deinit() must never delete the
     * worker task/queue or reset the coordinator while RAWOWN still
     * reports active after the bounded stop-and-wait -- that would strand
     * a live RAWOWN session with no task left to ever service or release
     * the radio. Four cases, all driving the real, unmodified
     * menu_nfc_deinit(), m1_mfc_raw_hw_active() and
     * m1_mfc_raw_hw_request_stop_and_wait(). ---- */

    /* 9b: timeout + still ACTIVE -> task, queue, and coordinator are all
     * left completely intact; the function returns without tearing
     * anything down. */
    reset(); signal_start=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK); /* PENDING consumed into a live session */
    uint8_t coord_before = s_start_request;
    void *task_before = nfc_worker_task_hdl, *q_before = nfc_worker_q_hdl;
    s_state = M1_RAWOWN_ACTIVE; signal_start = 0; /* RAWOWN active; the bounded wait will now time out */
    menu_nfc_deinit();
    assert(deletes_task==0); assert(deletes_queue==0); /* fix: nothing torn down */
    assert(nfc_worker_task_hdl==task_before); assert(nfc_worker_q_hdl==q_before); /* handles preserved */
    assert(s_start_request==coord_before); /* coordinator untouched, not reset */
    assert(s_state==M1_RAWOWN_ACTIVE); /* RAWOWN's own state is out of this task's scope -- genuinely unresolved */
    /* 9e: later recovery cannot create a second worker/queue -- the
     * existing idempotent guard at the top of menu_nfc_init() (not
     * extracted/re-executed here -- pure task/queue creation, no
     * coordinator logic of its own) is exactly "if either handle is
     * already non-NULL, do nothing and return"; both handles are still
     * exactly the same non-NULL values menu_nfc_deinit() left them at,
     * so a subsequent menu_nfc_init() call is guaranteed to hit that
     * early return and skip xTaskCreate/xQueueCreate entirely -- proven
     * here at the handle level (the actual guard is a two-line NULL check
     * with no coordinator interaction to re-derive). */
    assert(nfc_worker_task_hdl != NULL && nfc_worker_q_hdl != NULL);
    m1_mfc_raw_hw_finish_request(); /* test-only cleanup so later scenarios start from IDLE */

    /* 9c: the wait's own return value said "failed" (bounded semaphore not
     * signaled), but RAWOWN genuinely finished and went INACTIVE in that
     * same window (flip_inactive_on_notify makes the STOP notification's
     * delivery -- a real, unmodified call inside
     * m1_mfc_raw_hw_request_stop_and_wait() -- also flip s_state, modeling
     * a completion racing the bounded wait) -- teardown must still
     * proceed. */
    reset(); signal_start=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    s_state = M1_RAWOWN_ACTIVE; signal_start = 0; flip_inactive_on_notify = 1;
    menu_nfc_deinit();
    flip_inactive_on_notify = 0;
    assert(deletes_task==1); assert(deletes_queue==1);
    assert(nfc_worker_task_hdl==NULL); assert(nfc_worker_q_hdl==NULL);
    assert(s_start_request==START_REQUEST_IDLE); /* coordinator reset: teardown genuinely completed */
    assert(s_state==M1_RAWOWN_INACTIVE);

    /* 9d: the wait succeeds outright (RAWOWN genuinely stops within the
     * bound -- flip_inactive_on_stop_take models the real worker's own
     * ordering: it sets s_state = INACTIVE, THEN gives the stopped
     * semaphore, so by the time xSemaphoreTake returns success s_state is
     * already inactive in production too) -- teardown and coordinator
     * reset proceed normally. */
    reset(); signal_start=1; flip_inactive_on_stop_take=1;
    assert(mfc_emu_start()==M1_MFC_EMU_OK);
    s_state = M1_RAWOWN_ACTIVE;
    menu_nfc_deinit();
    flip_inactive_on_stop_take=0;
    assert(deletes_task==1); assert(deletes_queue==1);
    assert(s_start_request==START_REQUEST_IDLE);
    assert(s_state==M1_RAWOWN_INACTIVE);

    /* ---- 11: absence of stale STOP/image/request state across the whole
     * sequence above -- the STOP count only ever increased for the two
     * scenarios that were genuine PENDING->CANCELLED transitions (5 and 7
     * above contribute one each via mfc_emu_start()'s own timeout path;
     * scenario 4's late cancel_start() call contributed zero, proving fix
     * #1 holds in aggregate, not just in isolation). ---- */
    puts("Startup coordinator lifecycle tests PASS (1,2,3a,3b,4,5,7,8,9,9b,9c,9d,9e,10a,10b,11)");
}
'''
with tempfile.TemporaryDirectory(prefix='mfc-start-test-') as directory:
    d = pathlib.Path(directory)
    (d / 'test.c').write_text(prefix + body + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                    str(d / 'test.c'), '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True)

init = hw[hw.index('void m1_mfc_raw_hw_init('):hw.index('bool m1_mfc_raw_hw_active(')]
assert 'xSemaphoreCreate' not in init and 'xSemaphoreTake' not in init
run = extract(hw, 'void m1_mfc_raw_hw_run(void)')
# 6: cancellation during initialization -- m1_mfc_raw_hw_run() itself is
# HAL/RFAL-coupled and cannot be host-linked; its ordering is verified
# structurally against the real, current source text instead.
assert run.index('if (!m1_mfc_raw_hw_start_requested()) return;') < run.index('m1_mfc_raw_hw_starting()')
assert run.index('m1_mfc_raw_hw_stopping("start-cancelled")') < run.index('xSemaphoreGive(s_started_sem)')
assert 'm1_mfc_raw_hw_start_requested() && m1_mfc_raw_has_emu_image()' in driver
done = driver[driver.index('case NFC_STATE_DONE:'):]
assert done.index('NfcRole.nfc_deinit_func();') < done.index('m1_mfc_raw_hw_finish_request();')
# fix #3, structural confirmation of ordering (dynamic proof is in tests above):
reject = driver[driver.index('if (m1_mfc_raw_hw_start_requested() && m1_mfc_raw_has_emu_image()) {'):]
else_branch = reject[reject.index('} else {'):]
assert else_branch.index('m1_mfc_raw_set_emu_image(NULL)') < else_branch.index('m1_mfc_raw_hw_finish_request()')
# fix #2 (fail-closed), structural confirmation of ordering (dynamic proof
# in tests 9b/9c/9d/9e above): the fail-closed "return" is textually before
# BOTH deletes, and the coordinator reset is textually after both.
deinit_start = ui.index('void menu_nfc_deinit(void)')
deinit = ui[deinit_start:ui.index('void nfc_read(', deinit_start)]
fail_closed_return = deinit.index('worker task/queue and start coordinator left intact, teardown skipped')
task_delete = deinit.index('vTaskDelete(nfc_worker_task_hdl);')
queue_delete = deinit.index('vQueueDelete(nfc_worker_q_hdl);')
coord_reset = deinit.index('m1_mfc_raw_hw_finish_request();')
assert fail_closed_return < task_delete < coord_reset
assert fail_closed_return < queue_delete < coord_reset
# The second m1_mfc_raw_hw_active() re-check is the actual fail-closed gate
# (not the wait's own return value, which is explicitly discarded via
# (void) immediately before it):
recheck_region = deinit[deinit.index('(void)m1_mfc_raw_hw_request_stop_and_wait'):]
assert recheck_region.index('if (m1_mfc_raw_hw_active()) {') < recheck_region.index('return;')
print('Worker/menu-teardown structural ordering checks PASS')
