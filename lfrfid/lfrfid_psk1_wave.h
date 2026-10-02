/* See COPYING.txt for license details. */

/*
 * Host-testable PSK1 LF RFID waveform builder.
 *
 * Converts an MSB-first encoded protocol frame into GPIO BSRR/duration
 * entries for the shared DMA transmitter.  The waveform is RF/2: sixteen
 * complete 62.5 kHz load-modulation cycles per 256 us data bit, with the
 * carrier phase reversed whenever the encoded data changes.
 */
#ifndef LFRFID_PSK1_WAVE_H_
#define LFRFID_PSK1_WAVE_H_

#include <stddef.h>
#include <stdint.h>

#include "lfrfid_dma_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LFRFID_PSK1_HALF_CYCLE_US   (8U)
#define LFRFID_PSK1_CYCLES_PER_BIT  (16U)
#define LFRFID_PSK1_STEPS_PER_BIT   (LFRFID_PSK1_CYCLES_PER_BIT * 2U)

/* Returns 0 on success and -1 if the destination is too small or an input is
 * invalid.  *out_step_count is set to the number of entries written (or zero
 * on failure). */
int lfrfid_psk1_encoded_to_wave(const uint8_t *encoded,
                                size_t encoded_bits,
                                uint8_t gpio_pin,
                                Encoded_Data_t *steps,
                                size_t max_steps,
                                size_t *out_step_count);

#ifdef __cplusplus
}
#endif

#endif /* LFRFID_PSK1_WAVE_H_ */
