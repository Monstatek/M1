/* See COPYING.txt for license details. */

#include "lfrfid_psk1_wave.h"

#include <stdbool.h>

static uint8_t lfrfid_psk1_get_bit(const uint8_t *data, size_t position)
{
    return (uint8_t)((data[position / 8U] >> (7U - (position % 8U))) & 1U);
}

int lfrfid_psk1_encoded_to_wave(const uint8_t *encoded,
                                size_t encoded_bits,
                                uint8_t gpio_pin,
                                Encoded_Data_t *steps,
                                size_t max_steps,
                                size_t *out_step_count)
{
    size_t idx = 0;

    if(out_step_count)
        *out_step_count = 0;
    if(!encoded || !steps || encoded_bits == 0U || gpio_pin >= 16U)
        return -1;
    if(encoded_bits > (max_steps / LFRFID_PSK1_STEPS_PER_BIT))
        return -1;

    const uint32_t bsrr_set = 1UL << gpio_pin;
    const uint32_t bsrr_reset = 1UL << (gpio_pin + 16U);

    /* The initial absolute phase is arbitrary.  Seeding last_bit from the
     * end of the frame makes the first/last boundary phase-continuous when
     * the completed step buffer is replayed circularly. */
    bool current_polarity = true;
    uint8_t last_bit = lfrfid_psk1_get_bit(encoded, encoded_bits - 1U);

    for(size_t bit = 0; bit < encoded_bits; bit++) {
        for(size_t cycle = 0; cycle < LFRFID_PSK1_CYCLES_PER_BIT; cycle++) {
            steps[idx].bsrr = current_polarity ? bsrr_set : bsrr_reset;
            steps[idx].time_us = LFRFID_PSK1_HALF_CYCLE_US;
            idx++;

            steps[idx].bsrr = current_polarity ? bsrr_reset : bsrr_set;
            steps[idx].time_us = LFRFID_PSK1_HALF_CYCLE_US;
            idx++;
        }

        uint8_t current_bit = lfrfid_psk1_get_bit(encoded, bit);
        if(current_bit != last_bit)
            current_polarity = !current_polarity;
        last_bit = current_bit;
    }

    if(out_step_count)
        *out_step_count = idx;
    return 0;
}
