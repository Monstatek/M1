/* See COPYING.txt for license details. */

/*
 * m1_sound_policy.c
 *
 * See m1_sound_policy.h. Pure policy state -- no hardware access.
 *
 * M1 Project
 */

#include "m1_sound_policy.h"

/* Reuses the two already-verified baseline tones from the existing buzzer
 * driver (4 kHz/250 ms "standard", 2 kHz/250 ms "alternate") for
 * success/error so those don't need new hardware validation. Detected and
 * warning are new mappings onto frequencies the driver already defines
 * (6 kHz, 4 kHz) -- provisional until Phase 0's hardware pass. */
#define SOUND_DETECTED_FREQ_HZ		6000U
#define SOUND_DETECTED_DURATION_MS	60U
#define SOUND_DETECTED_COOLDOWN_MS	300U

#define SOUND_SUCCESS_FREQ_HZ		4000U
#define SOUND_SUCCESS_DURATION_MS	250U
#define SOUND_SUCCESS_COOLDOWN_MS	500U

#define SOUND_WARNING_FREQ_HZ		4000U
#define SOUND_WARNING_DURATION_MS	120U
#define SOUND_WARNING_GAP_MS		80U
#define SOUND_WARNING_COOLDOWN_MS	500U

#define SOUND_ERROR_FREQ_HZ			2000U
#define SOUND_ERROR_DURATION_MS		250U
#define SOUND_ERROR_COOLDOWN_MS		500U

static uint32_t s_last_played_ms[M1_SOUND_EVENT_COUNT];
static bool     s_has_played[M1_SOUND_EVENT_COUNT];

static bool event_valid(m1_sound_event_t event)
{
	return (int)event < (int)M1_SOUND_EVENT_COUNT; // enum is unsigned on this target; negatives can't occur
}

static uint32_t cooldown_for(m1_sound_event_t event)
{
	switch ( event )
	{
		case M1_SOUND_DETECTED: return SOUND_DETECTED_COOLDOWN_MS;
		case M1_SOUND_SUCCESS:  return SOUND_SUCCESS_COOLDOWN_MS;
		case M1_SOUND_WARNING:  return SOUND_WARNING_COOLDOWN_MS;
		case M1_SOUND_ERROR:    return SOUND_ERROR_COOLDOWN_MS;
		default:                return 0U;
	}
}

m1_sound_spec_t m1_sound_spec_for(m1_sound_event_t event)
{
	m1_sound_spec_t spec = { 0, 0, 0, 0 };

	switch ( event )
	{
		case M1_SOUND_DETECTED:
			spec.freq_hz = SOUND_DETECTED_FREQ_HZ;
			spec.duration_ms = SOUND_DETECTED_DURATION_MS;
			spec.repeat_count = 1U;
			break;
		case M1_SOUND_SUCCESS:
			spec.freq_hz = SOUND_SUCCESS_FREQ_HZ;
			spec.duration_ms = SOUND_SUCCESS_DURATION_MS;
			spec.repeat_count = 1U;
			break;
		case M1_SOUND_WARNING:
			spec.freq_hz = SOUND_WARNING_FREQ_HZ;
			spec.duration_ms = SOUND_WARNING_DURATION_MS;
			spec.repeat_count = 2U;
			spec.gap_ms = SOUND_WARNING_GAP_MS;
			break;
		case M1_SOUND_ERROR:
			spec.freq_hz = SOUND_ERROR_FREQ_HZ;
			spec.duration_ms = SOUND_ERROR_DURATION_MS;
			spec.repeat_count = 1U;
			break;
		default:
			break; // invalid event -> zeroed spec, repeat_count==0
	}
	return spec;
} // m1_sound_spec_t m1_sound_spec_for(...)

bool m1_sound_policy_gate(m1_sound_event_t event, uint32_t now_ms)
{
	uint32_t cooldown;

	if ( !event_valid(event) )
		return false;

	cooldown = cooldown_for(event);
	if ( s_has_played[event] && ((uint32_t)(now_ms - s_last_played_ms[event]) < cooldown) )
		return false; // still within this event's own cooldown window

	s_last_played_ms[event] = now_ms;
	s_has_played[event] = true;
	return true;
} // bool m1_sound_policy_gate(...)

void m1_sound_policy_reset(void)
{
	int i;
	for ( i = 0; i < (int)M1_SOUND_EVENT_COUNT; i++ )
	{
		s_last_played_ms[i] = 0U;
		s_has_played[i] = false;
	}
} // void m1_sound_policy_reset(void)
