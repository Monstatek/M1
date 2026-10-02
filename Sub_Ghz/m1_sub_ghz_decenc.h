/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz_decenc.h
*
*  M1 sub-ghz decoding encoding
*
* M1 Project
*
*/
#ifndef _M1_SUB_GHZ_DECENC_H
#define _M1_SUB_GHZ_DECENC_H

#include <stdint.h>
#include <stdbool.h>
#include "m1_sub_ghz.h"

#define SUBGHZ_RAW_DATA_PULSE_COUNT_MAX		20

#define INTERPACKET_GAP_MIN					4000 // uS (was 1500; collided with ~1500us bit-long of Linear etc.)
#define INTERPACKET_GAP_MAX					80000//5000 // uS
#define PACKET_PULSE_TIME_MIN				120 // uS
#define PACKET_PULSE_COUNT_MIN				16 // 8 bits min (was 48=24bits; blocked CAME/Linear/short protocols)
#define PACKET_PULSE_COUNT_MAX				128 // 64 bits

#define PACKET_PULSE_TIME_TOLERANCE20		20 // percentage
#define PACKET_PULSE_TIME_TOLERANCE25		25
#define PACKET_PULSE_TIME_TOLERANCE30		30

typedef struct SubGHz_protocol
{
    uint16_t te_short;
    uint16_t te_long;
    uint8_t te_tolerance;
    uint8_t preamble_bits;
    uint16_t data_bits;
} SubGHz_protocol_t;


typedef struct
{
    bool (*subghz_data_ready)(void);
    bool (*subghz_raw_data_ready)(void);
    void (*subghz_reset_data)(void);
    uint64_t (*subghz_get_decoded_value)(void);
    uint16_t (*subghz_get_decoded_bitlength)(void);
    uint16_t (*subghz_get_decoded_delay)(void);
    uint16_t (*subghz_get_decoded_protocol)(void);
    int16_t (*subghz_get_decoded_rssi)(void);
    uint16_t *(*subghz_get_rawdata)(void);
    uint8_t (*subghz_pulse_handler)(uint16_t duration);
    bool (*subghz_decode_protocol)(const uint16_t p, uint16_t changeCount);

    uint64_t n64_decodedvalue;
    uint32_t n32_serialnumber;
    uint32_t n32_rollingcode;
    uint8_t n8_buttonid;
    int16_t ndecodedrssi;
    uint16_t ndecodedbitlength;
    uint16_t ndecodeddelay;
    uint16_t ndecodedprotocol;
    uint16_t npulsecount;
    uint16_t ntx_raw_repeat;
    uint32_t ntx_raw_len;
    uint32_t ntx_raw_src;
    uint32_t ntx_raw_dest;
    volatile uint8_t pulse_det_stat; // Updated in interrupt
    volatile uint8_t pulse_det_pol; // Updated in interrupt
    uint16_t pulse_times[PACKET_PULSE_COUNT_MAX];
} SubGHz_DecEnc_t;

typedef struct
{
    uint32_t frequency;
    uint64_t key;
    uint16_t protocol;
    int16_t rssi;
    uint16_t *raw_data;
    uint16_t te;
    uint16_t bit_len;
    bool raw;
} SubGHz_Dec_Info_t;

enum {
	PRINCETON = 0,
	CAME_24, CAME_12, NICE_FLO_24, NICE_FLO_12, GATE_TX
	/* LINEAR is a subghz module (see modules/) */
};

extern SubGHz_DecEnc_t subghz_decenc_ctl;
extern const char *protocol_text[];
extern const SubGHz_protocol_t subghz_protocols_list[];

/* Decoder registry: one module per protocol { name, decode fn }, parallel to subghz_protocols_list[] */
typedef struct { const char *name; uint8_t (*decode)(uint16_t p, uint16_t pulsecount); } SubGHz_Decoder_t;
extern const SubGHz_Decoder_t subghz_decoder_registry[];
uint8_t subghz_decode_fixedcode(uint16_t p, uint16_t pulsecount);
const char *subghz_protocol_name(uint16_t proto);
extern const uint16_t subghz_n_decoders;
extern volatile uint32_t subghz_diag_isr_pulses;
extern volatile uint32_t subghz_diag_insert_fail;
extern volatile uint32_t subghz_diag_drained;
extern volatile uint32_t subghz_diag_submitted;
extern volatile uint32_t subghz_diag_attempts;
extern volatile uint32_t subghz_diag_decoded;
void subghz_scan_diag_reset(void);
extern volatile uint16_t subghz_seg_guard;

void subghz_decenc_init(void);
bool subghz_decenc_read(SubGHz_Dec_Info_t *received, bool raw);
uint16_t get_diff(uint16_t n_a, uint16_t n_b);
uint8_t subghz_decode_princeton(uint16_t p, uint16_t pulsecount);
uint8_t subghz_decode_security_plus_20(uint16_t p, uint16_t pulsecount);
uint8_t subghz_decode_pwm(uint16_t p, uint16_t pulsecount);
uint8_t m1_secplus_v2_decode(uint32_t fixed[], uint8_t half_codes[][10], uint32_t *rolling_code, uint64_t *out_bits);
uint8_t m1_secplus_v2_decode_half(uint64_t in_bits, uint8_t *half_code, uint32_t *out_bits);

#endif // #ifndef _M1_SUB_GHZ_DECENC_H
