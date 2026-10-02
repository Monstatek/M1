/* See COPYING.txt for license details. */

/*
 * m1_sound.h
 *
 * Hardware adapter for m1_sound_policy: plays the semantic tone for an
 * event through the (already-hardened) m1_buzzer driver, honouring the
 * policy's per-event rate-limit gate and the buzzer's own mute state.
 *
 * Not yet called from any feature module -- available for the planned
 * cross-feature feedback service (m1_feedback) to use directly, or for a
 * feature to call ahead of that migration.
 *
 * M1 Project
 */

#ifndef M1_SOUND_H_
#define M1_SOUND_H_

#include "m1_sound_policy.h"

/* Play the semantic tone for `event` if the policy gate allows it right
 * now (muted, via the buzzer driver, or within that event's own
 * rate-limit cooldown both silently suppress it -- this never blocks
 * waiting for either). A WARNING's double tone is played as two
 * sequential m1_buzzer_set() calls with a task-context vTaskDelay for the
 * gap between them.
 *
 * Task-context only -- never call from an ISR or a timing-critical RF
 * path (Crypto1/DMA/transparent-mode/IRQ/FIFO), same restriction as
 * m1_buzzer_set() itself.
 */
void m1_sound_notify(m1_sound_event_t event);

#endif /* M1_SOUND_H_ */
