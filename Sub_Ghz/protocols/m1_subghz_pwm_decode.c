/* See COPYING.txt for license details. */
/*
*  m1_subghz_pwm_decode.c
*  Generic fixed-code PWM decoder, driven by subghz_protocols_list[p].
*  Verified against real M1 300MHz OOK captures: te from the list, classify each
*  bit by its HIGH pulse (short=0, long=1) with the complementary LOW validated,
*  scan start offsets to skip a noise prefix, and accept the inter-packet guard
*  as the final bit's low. Discriminates by te values + exact bit count.
*  Clean-room. M1 Project.
*/
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "bit_util.h"
#include "m1_sub_ghz_decenc.h"
#include "m1_log_debug.h"

#define M1_LOGDB_TAG    "SUBGHZ_PWM"

uint8_t subghz_decode_pwm(uint16_t p, uint16_t pulsecount)
{
    uint16_t te_short = subghz_protocols_list[p].te_short;
    uint16_t te_long  = subghz_protocols_list[p].te_long;
    uint8_t  max_bits = subghz_protocols_list[p].data_bits;
    uint8_t  tolerance= subghz_protocols_list[p].te_tolerance;
    uint16_t tol_s = (te_short * tolerance) / 100;
    uint16_t tol_l = (te_long  * tolerance) / 100;
    uint16_t start, b;

    if (te_short == 0) return 1;
    uint16_t _meas = 0xFFFF, _mi2;
    for (_mi2 = 0; _mi2 < pulsecount; _mi2++) { uint16_t _d = subghz_decenc_ctl.pulse_times[_mi2]; if (_d > 120 && _d < _meas) _meas = _d; }

    /* slide over the packet; find max_bits consecutive valid PWM bit-pairs */
    for (start = 0; (uint32_t)start + 2u*max_bits <= pulsecount; start++)
    {
        uint32_t code = 0;
        uint8_t  ok = 1;
        for (b = 0; b < max_bits; b++)
        {
            uint16_t hi = subghz_decenc_ctl.pulse_times[start + 2*b];
            uint16_t lo = subghz_decenc_ctl.pulse_times[start + 2*b + 1];
            uint8_t  is_last = (b == (uint16_t)(max_bits - 1));

            if (get_diff(hi, te_short) < tol_s)          /* short high -> bit 0 */
            {
                if (get_diff(lo, te_long) < tol_l || (is_last && lo >= INTERPACKET_GAP_MIN))
                    code <<= 1;
                else { ok = 0; break; }
            }
            else if (get_diff(hi, te_long) < tol_l)       /* long high -> bit 1 */
            {
                if (get_diff(lo, te_short) < tol_s || (is_last && lo >= INTERPACKET_GAP_MIN))
                    code = (code << 1) | 1;
                else { ok = 0; break; }
            }
            else { ok = 0; break; }
        }
        if (ok)
        {
            subghz_decenc_ctl.n64_decodedvalue  = code;
            subghz_decenc_ctl.ndecodedbitlength = max_bits;
            subghz_decenc_ctl.ndecodeddelay     = _meas; /* measured te for pick-closest + log */
            subghz_decenc_ctl.ndecodedprotocol  = p;
            return 0;
        }
    }
    return 1;
} // subghz_decode_pwm

/* Fixed-code CLASSIFIER on the segmented packet (proven pulse_handler path).
 * Determines bit count from packet length, self-calibrates te from the shortest pulse,
 * decodes with the proven validation, then classifies CAME / Gate TX / Nice FLO by te.
 * Sets ndecodedprotocol to 10=CAME, 11=Nice FLO, 12=Gate TX. No decode-order dependence. */
uint8_t subghz_decode_fixedcode(uint16_t p, uint16_t pulsecount)
{
    uint8_t nbits;
    (void)p;
    if      (pulsecount >= 44 && pulsecount <= 54) nbits = 24;   /* 24-bit packet ~48 pulses */
    else if (pulsecount >= 20 && pulsecount <= 30) nbits = 12;   /* 12-bit packet ~24 pulses */
    else return 1;

    /* self-calibrate te_short = minimum plausible pulse (short high or short low) */
    uint16_t te = 0xFFFF, i;
    for (i = 0; i < pulsecount; i++) {
        uint16_t d = subghz_decenc_ctl.pulse_times[i];
        if (d > 120 && d < te) te = d;
    }
    if (te < 220 || te > 850) return 1;             /* outside CAME/GateTX/NiceFlo range */
    uint16_t te_long = (uint16_t)(te * 2);
    uint16_t tol_s = te / 3, tol_l = te_long / 3;   /* ~33% */

    uint16_t start, b;
    for (start = 0; (uint32_t)start + 2u*nbits <= pulsecount; start++) {
        uint32_t code = 0; uint8_t ok = 1;
        for (b = 0; b < nbits; b++) {
            uint16_t hi = subghz_decenc_ctl.pulse_times[start + 2*b];
            uint16_t lo = subghz_decenc_ctl.pulse_times[start + 2*b + 1];
            uint8_t is_last = (b == (uint16_t)(nbits - 1));
            if (get_diff(hi, te) < tol_s) {
                if (get_diff(lo, te_long) < tol_l || (is_last && lo >= INTERPACKET_GAP_MIN)) code <<= 1;
                else { ok = 0; break; }
            } else if (get_diff(hi, te_long) < tol_l) {
                if (get_diff(lo, te) < tol_s || (is_last && lo >= INTERPACKET_GAP_MIN)) code = (code << 1) | 1;
                else { ok = 0; break; }
            } else { ok = 0; break; }
        }
        if (ok) {
            uint16_t proto;
            if (te >= 550) proto = 11;              /* Nice FLO (te ~700) */
            else if (te < 335) proto = 10;          /* CAME (te ~320) */
            else proto = 12;                        /* Gate TX (te ~350) */
            if (proto == 12 && nbits == 12) proto = 10;  /* Gate TX is 24-bit only */
            subghz_decenc_ctl.n64_decodedvalue  = code;
            subghz_decenc_ctl.ndecodedbitlength = nbits;
            subghz_decenc_ctl.ndecodeddelay     = te;   /* report measured te for tuning */
            subghz_decenc_ctl.ndecodedprotocol  = proto;
            return 0;
        }
    }
    return 1;
} // subghz_decode_fixedcode
