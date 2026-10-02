/* See COPYING.txt for license details. */

/*
*
* m1_feedback_sequences.h
*
* The single-resource sequence catalogue (reference sections 7.1-7.4 and
* 8): resets, solid holds, one-shot flashes, continuous blinks, and audio
* sequences. Multi-resource identifiers (composite alerts, power/lifecycle,
* radio, USB, payload, settings previews -- reference sections 9-12) are
* orchestrations of these primitives plus their own private message
* arrays; see m1_feedback_orchestration.h.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_SEQUENCES_H_
#define M1_FEEDBACK_SEQUENCES_H_

#include "m1_feedback_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Looks up the message array, resource, and kind for any single-resource
 * catalogue identifier. Returns false for identifiers that are not simple
 * single-resource entries (composites, power/lifecycle, radio, USB,
 * payload, settings previews, SEQ_RESET_ALL, SEQ_BLINK_STOP) -- those are
 * driven through m1_feedback_orchestration.h / fb_stop_led_loop() /
 * fb_reset_all() instead. */
bool fb_catalogue_lookup(fb_seq_id_t seq_id, const fb_msg_t **prog_out,
                          fb_resource_t *resource_out, fb_kind_t *kind_out);

/* Exposed so the orchestration layer can splice individual primitives
 * (e.g. the flash/blink message arrays) into composite/power sequences
 * without duplicating the exact timing templates. */
const fb_msg_t *fb_seq_prog(fb_seq_id_t seq_id);

#ifdef __cplusplus
}
#endif

#endif /* M1_FEEDBACK_SEQUENCES_H_ */
