/* See COPYING.txt for license details. */

/*
 * m1_sound_policy.h
 *
 * Hardware-independent sound policy: semantic event -> tone spec, plus a
 * per-event rate-limit gate. No STM32 HAL / FreeRTOS dependency -- pure
 * state so it can be host-tested (see m1_csrc/test/m1_sound_policy_test.c).
 * The hardware adapter lives in m1_sound.c, which calls
 * m1_sound_policy_gate()/m1_sound_spec_for() and pushes the result to
 * m1_buzzer_set().
 *
 * Vocabulary and per-event behaviour are the sound half of the M1 Feedback
 * System plan's product-experience table:
 *   detected -> very short tone, rate-limited
 *   success  -> short tone (the existing 4 kHz/250 ms "standard" tone)
 *   warning  -> optional double tone
 *   error    -> single lower tone (the existing 2 kHz/250 ms "alternate" tone)
 * Listening/scanning/emulating, writing/firmware-update, charging, and
 * fully-charged are explicitly silent per that table and have no event
 * here -- only genuinely sound-producing events do.
 *
 * Exact frequencies/durations are provisional pending the plan's Phase 0
 * hardware audibility/comfort pass -- kept as named constants in one place
 * (m1_sound_policy.c) so that pass can retune them without touching this
 * API or any future caller.
 *
 * M1 Project
 */

#ifndef M1_SOUND_POLICY_H_
#define M1_SOUND_POLICY_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	M1_SOUND_DETECTED = 0,	/* meaningful reader/tag/signal discovery	*/
	M1_SOUND_SUCCESS,		/* completed user operation				*/
	M1_SOUND_WARNING,		/* incomplete / recoverable / attention	*/
	M1_SOUND_ERROR,			/* failed operation							*/
	M1_SOUND_EVENT_COUNT
} m1_sound_event_t;

typedef struct
{
	uint16_t freq_hz;
	uint16_t duration_ms;
	uint8_t  repeat_count;	/* 1 = single tone, 2 = double tone (warning)	*/
	uint16_t gap_ms;		/* silence between repeats; unused if repeat_count==1 */
} m1_sound_spec_t;

/* Pure lookup: the tone spec for an event. Always succeeds for a valid
 * event; returns a zeroed spec (repeat_count==0) for an invalid one. */
m1_sound_spec_t m1_sound_spec_for(m1_sound_event_t event);

/* Stateful rate-limit gate: true if this event is allowed to actually
 * sound at time now_ms (and records that it did), false if it's within
 * that event's own cooldown window since it last sounded. Each event type
 * is gated independently -- a rapid ERROR doesn't suppress a DETECTED.
 * now_ms is caller-supplied (not read internally) so this stays host-
 * testable without a real clock. */
bool m1_sound_policy_gate(m1_sound_event_t event, uint32_t now_ms);

/* Test-only: clear all per-event cooldown state. Never called from
 * production code paths. */
void m1_sound_policy_reset(void);

#endif /* M1_SOUND_POLICY_H_ */
