/* See COPYING.txt for license details. */

/*
*
* m1_feedback_manager.h
*
* Non-blocking feedback manager: single owner of the RGB LED, display
* backlight and buzzer resources. Implements the architecture required by
* the "MonstaTek M1 Feedback and Notification Reference" (M1-FB-STD-001,
* v1.1) -- see documentation/M1_FEEDBACK_IMPLEMENTATION_BRIEF.md and
* documentation/M1_FEEDBACK_RECONCILIATION.md.
*
* Design summary (see m1_feedback_manager.c for the full rationale):
*  - Every sequence is a constant fb_msg_t[] terminated by FB_MSG_END.
*  - LED, backlight and buzzer are arbitrated independently, each with its
*    own fixed-size list of active "holds" plus at most one transient
*    (one-shot or loop) owner.
*  - Ownership recomputation is logical: releasing a resource re-evaluates
*    the highest-eligible remaining owner rather than restoring a saved
*    register value (reference section 6, rule 8).
*  - This module contains NO FreeRTOS calls and NO hardware register
*    access, so it can be linked and unit-tested on the host. The target
*    build supplies a fb_hw_adapter_t (m1_feedback_hw_adapter.c) and drives
*    fb_manager_tick() from a small dedicated task
*    (m1_feedback_task.c) -- see those files for task/ISR context rules.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_MANAGER_H_
#define M1_FEEDBACK_MANAGER_H_

#include "m1_feedback_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************** H A R D W A R E   A D A P T E R *******************/

/*
 * The manager calls these to apply state to hardware. They must be
 * non-blocking and safe to call from the feedback task's own context only
 * (the manager itself performs no locking around these calls -- see
 * "Task/ISR context" below). The target adapter is the ONLY application
 * code allowed to call into m1_lp5814.c / the buzzer timer for feedback
 * purposes; diagnostic/factory CLI bypasses are a documented exception
 * (see reconciliation doc).
 */
typedef struct
{
	/* Apply an LED colour mask (FB_LED_* bit combination) to hardware. */
	void (*led_write)(uint8_t mask);

	/* Apply a normalized backlight value, 0-100 percent. */
	void (*bl_write)(uint8_t percent);

	/* Start/replace the buzzer pitch for one note id (fb_note_t). */
	void (*snd_note)(uint8_t note_id);

	/* Stop the buzzer and drive it to its idle state. Must always be
	 * callable and must always take effect (reference section 5.1). */
	void (*snd_off)(void);
} fb_hw_adapter_t;

/*************************** T A S K / I S R   C O N T E X T ********************/
/*
 * fb_manager_init()      : call once at startup, before any other API, from
 *                          normal task context (not ISR).
 * fb_manager_tick()      : call periodically (nominally every 1 ms) from
 *                          ONE dedicated task only. This is the only
 *                          function that invokes the hw adapter, so it is
 *                          the only function that may block on I2C, etc.
 *                          Do not call from an ISR.
 * fb_request()/fb_release()/fb_cleanup_owner()/fb_reset_all()/settings
 *                        : safe to call from any FreeRTOS task context.
 *                          They only touch small fixed-size bookkeeping
 *                          arrays and are guarded internally by a short
 *                          critical section (interrupts/scheduler briefly
 *                          disabled, never while touching hardware) so
 *                          they never block the caller and are safe to
 *                          call concurrently from multiple tasks. They
 *                          must not be called from an ISR.
 */

void fb_manager_init(const fb_hw_adapter_t *hw);

/* Advance all running sequences by elapsed_ms and (re)apply any resulting
 * hardware state. Must be called from the dedicated feedback task only. */
void fb_manager_tick(uint32_t elapsed_ms);

/*************************** R E Q U E S T   A P I  *****************************/

typedef int16_t fb_handle_t;
#define FB_HANDLE_INVALID	((fb_handle_t)-1)

/* Request a catalogued sequence for `owner` at `priority`. The message
 * array and its kind (one-shot/hold/loop) are intrinsic to the sequence
 * and come from the built-in catalogue (m1_feedback_sequences.c); the
 * *priority* is supplied by the caller because the same LED/audio template
 * (e.g. a solid colour or a flash) is reused by subsystems in different
 * reference priority classes (section 6) -- it is not a property of the
 * colour itself. Callers should use the fb_priority_t class documented for
 * their event in reference sections 6-12.
 *
 * Returns FB_HANDLE_INVALID if the request was dropped (lower-priority
 * one-shot while a higher-priority owner holds the resource -- reference
 * rule 5). A HOLD-kind request is always accepted for bookkeeping purposes
 * even if not immediately visible (reference rule 8 / logical restore) --
 * such calls return a valid handle. SEQ_RESET_ALL is accepted here too and
 * simply calls fb_reset_all(). Not all other fb_seq_id_t values are valid
 * standalone requests; the orchestrated composite/power/net/usb/pay/ui
 * identifiers (which drive more than one resource) are issued through
 * m1_feedback_orchestration.h instead, and SEQ_BLINK_STOP is issued via
 * fb_stop_led_loop(). */
fb_handle_t fb_request(fb_seq_id_t seq_id, fb_owner_t owner, fb_priority_t priority);

/* Lower-level entry point used by the orchestration layer to run an
 * ad-hoc/private message array (e.g. one leg of a composite alert) under
 * the same arbitration rules as a catalogued sequence. */
fb_handle_t fb_request_raw(const fb_msg_t *prog, fb_resource_t resource,
                            fb_owner_t owner, fb_priority_t priority,
                            fb_kind_t kind, fb_seq_id_t tag);

/* Equivalent of MSG_LED_RELEASE / MSG_BL_RELEASE / stopping a buzzer hold:
 * releases whatever `owner` currently holds/runs on `resource` (hold entry,
 * active loop, or in-flight one-shot) and recomputes the next visible
 * owner. Safe to call when the owner holds nothing on that resource. */
void fb_release(fb_owner_t owner, fb_resource_t resource);

/* Stops the single visible continuous LED loop (SEQ_BLINK_STOP) regardless
 * of owner and recomputes the correct visible LED owner (reference 7.4). */
void fb_stop_led_loop(void);

/* Section 13 owner-scoped cleanup. Idempotent (V12): stops any buzzer note,
 * cancels loops, releases LED/backlight holds owned by `owner`, and
 * recomputes next eligible owners for all three resources. Safe to call
 * more than once and safe to call for an owner holding nothing. */
void fb_cleanup_owner(fb_owner_t owner);

/* SEQ_RESET_ALL: supervisor fallback when owner-specific cleanup cannot be
 * identified. Forces every resource to its administrative idle state
 * (LED off, backlight released to the sleep timer, sound off). */
void fb_reset_all(void);

/*************************** G L O B A L   S E T T I N G S **********************/

void fb_set_sound_enabled(bool enabled);
bool fb_get_sound_enabled(void);

/* 0-100 percent. 0 is equivalent to Sound Off for note suppression
 * purposes (reference section 4.3). */
void fb_set_alert_volume_pct(uint8_t pct);
uint8_t fb_get_alert_volume_pct(void);

/* 0-100 percent. Applied whenever a sequence requests MSG_BL_USER /
 * MSG_BL_HOLD_USER. */
void fb_set_brightness_pct(uint8_t pct);
uint8_t fb_get_brightness_pct(void);

/*************************** I N T R O S P E C T I O N ***************************/
/* For host unit tests and diagnostics only -- not part of the normative
 * request API. */

uint8_t fb_debug_visible_led(void);
uint8_t fb_debug_visible_bl(void);
bool    fb_debug_snd_idle(void);
bool    fb_debug_led_loop_active(void);
bool    fb_debug_led_loop_suspended(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_FEEDBACK_MANAGER_H_ */
