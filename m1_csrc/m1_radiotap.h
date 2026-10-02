/* See COPYING.txt for license details. */

/*
*
* m1_radiotap.h
*
* Minimal, standards-compliant Radiotap header builder for MonstaShark
* (Wi-Fi Capture). Little-endian, correctly aligned.
*
* Pure logic with NO RTOS/HAL/ESP dependencies so it is exercised by host
* golden tests (see m1_csrc/test/m1_pcapng_test.c).
*
* Scope: only metadata the ESP32-C6 can report reliably today — the channel
* (frequency + 2.4 GHz band flags) and dBm antenna signal (RSSI). FCS flags,
* legacy rate, MCS, A-MPDU, and HE fields are intentionally NOT emitted until
* they are validated on production hardware. Incorrect metadata is worse than
* omitted metadata; Wireshark still dissects the raw 802.11 frame without them.
*
* Reference: https://www.radiotap.org/  (little-endian, field order = bit order,
* each field naturally aligned from the start of the radiotap header).
*
* M1 Project
*
*/

#ifndef M1_RADIOTAP_H_
#define M1_RADIOTAP_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Radiotap "present" bit numbers (only the ones we emit). */
#define M1_RADIOTAP_BIT_CHANNEL       3
#define M1_RADIOTAP_BIT_DBM_ANTSIGNAL 5

/* Radiotap channel flags (u16, little-endian). */
#define M1_RADIOTAP_CHAN_TURBO   0x0010u
#define M1_RADIOTAP_CHAN_CCK     0x0020u
#define M1_RADIOTAP_CHAN_OFDM    0x0040u
#define M1_RADIOTAP_CHAN_2GHZ    0x0080u
#define M1_RADIOTAP_CHAN_5GHZ    0x0100u

/* Largest header this module can emit (version+pad+len+present + channel(4) +
 * signal(1), rounded up). */
#define M1_RADIOTAP_MAX_LEN  16

typedef struct {
    bool     has_channel;      /* emit the Channel field                    */
    uint16_t channel_freq_mhz; /* e.g. 2437 for ch 6                        */
    uint16_t channel_flags;    /* M1_RADIOTAP_CHAN_* bits                   */
    bool     has_dbm_signal;   /* emit the dBm Antenna Signal field         */
    int8_t   dbm_signal;       /* RSSI in dBm (signed)                      */
} m1_radiotap_info_t;

/* Build a Radiotap header into out[0..out_cap). Returns the number of bytes
 * written, or 0 if out_cap is too small or nothing to emit. */
size_t m1_radiotap_build(const m1_radiotap_info_t *info, uint8_t *out, size_t out_cap);

/* 2.4 GHz channel number (1..14) -> center frequency in MHz. Returns 0 for an
 * out-of-range channel. */
uint16_t m1_radiotap_chan_to_freq(uint8_t channel);

#endif /* M1_RADIOTAP_H_ */
