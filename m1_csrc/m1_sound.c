/* See COPYING.txt for license details. */

/*
 * m1_sound.c
 *
 * See m1_sound.h.
 *
 * M1 Project
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_buzzer.h"
#include "m1_sound_policy.h"
#include "m1_sound.h"

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

void m1_sound_notify(m1_sound_event_t event)
{
	m1_sound_spec_t spec;
	uint32_t now_ms;
	uint8_t i;

	if ( xPortIsInsideInterrupt() ) // never call from ISR or timing-critical RF context
		return;

	now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
	if ( !m1_sound_policy_gate(event, now_ms) ) // muted is handled downstream by m1_buzzer_set itself
		return;

	spec = m1_sound_spec_for(event);
	if ( spec.repeat_count == 0 ) // invalid event
		return;

	for ( i = 0; i < spec.repeat_count; i++ )
	{
		m1_buzzer_set(spec.freq_hz, spec.duration_ms);
		if ( (uint8_t)(i + 1U) < spec.repeat_count ) // more repeats to come -- wait out this tone plus the gap
			vTaskDelay(pdMS_TO_TICKS(spec.duration_ms + spec.gap_ms));
	}
} // void m1_sound_notify(m1_sound_event_t event)
