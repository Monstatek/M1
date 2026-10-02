/* See COPYING.txt for license details. */

/*
 * m1_led_priority.c
 *
 * See m1_led_priority.h. Pure arbitration state -- no hardware access.
 *
 * M1 Project
 */

#include <stddef.h>
#include "m1_led_priority.h"

typedef struct
{
	bool             active;
	uint8_t          token;	/* current generation for this owner; 0 = never begun */
	m1_led_effect_t  effect;
} m1_led_slot_t;

static m1_led_slot_t s_slots[M1_LED_OWNER_COUNT];

static bool owner_valid(m1_led_owner_t owner)
{
	return (owner > M1_LED_OWNER_NONE) && (owner < M1_LED_OWNER_COUNT);
}

m1_led_result_t m1_led_owner_begin(m1_led_owner_t owner, const m1_led_effect_t *effect, uint8_t *out_token)
{
	uint8_t next_token;

	if ( !owner_valid(owner) || effect == NULL )
		return M1_LED_REJECTED_INVALID;

	next_token = (uint8_t)(s_slots[owner].token + 1U);
	if ( next_token == 0U ) // skip the sentinel value on wraparound
		next_token = 1U;

	s_slots[owner].active = true;
	s_slots[owner].token = next_token;
	s_slots[owner].effect = *effect;

	if ( out_token != NULL )
		*out_token = next_token;

	return M1_LED_ACCEPTED;
} // m1_led_result_t m1_led_owner_begin(...)

m1_led_result_t m1_led_owner_end(m1_led_owner_t owner, uint8_t token)
{
	if ( !owner_valid(owner) )
		return M1_LED_REJECTED_INVALID;

	if ( !s_slots[owner].active )
		return M1_LED_ACCEPTED; // idempotent: nothing to end

	if ( s_slots[owner].token != token )
		return M1_LED_REJECTED_STALE; // a newer begin() has already superseded this token

	s_slots[owner].active = false;
	return M1_LED_ACCEPTED;
} // m1_led_result_t m1_led_owner_end(...)

bool m1_led_owner_is_active(m1_led_owner_t owner)
{
	if ( !owner_valid(owner) )
		return false;
	return s_slots[owner].active;
} // bool m1_led_owner_is_active(...)

m1_led_owner_t m1_led_topmost(m1_led_effect_t *effect_out)
{
	int i;

	for ( i = (int)M1_LED_OWNER_COUNT - 1; i > (int)M1_LED_OWNER_NONE; i-- )
	{
		if ( s_slots[i].active )
		{
			if ( effect_out != NULL )
				*effect_out = s_slots[i].effect;
			return (m1_led_owner_t)i;
		}
	}
	return M1_LED_OWNER_NONE;
} // m1_led_owner_t m1_led_topmost(...)

void m1_led_priority_reset(void)
{
	int i;
	for ( i = 0; i < (int)M1_LED_OWNER_COUNT; i++ )
	{
		s_slots[i].active = false;
		s_slots[i].token = 0U;
	}
} // void m1_led_priority_reset(void)
