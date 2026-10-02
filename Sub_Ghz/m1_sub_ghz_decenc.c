/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz_decenc.c
*
*  M1 sub-ghz decoding encoding
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "stm32h5xx_hal.h"
#include <m1_sub_ghz_decenc.h>
#include "m1_sub_ghz.h"
#include "m1_sub_ghz_api.h"
#include "si446x_cmd.h"
#include "m1_io_defs.h" // Test only
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"SUBGHZ_DECENC"

//************************** C O N S T A N T **********************************/

const SubGHz_protocol_t subghz_protocols_list[] =
{
	{370, 1140, PACKET_PULSE_TIME_TOLERANCE20, 0, 24}, // [0] Princeton (3:1)
	{320, 640, 30, 0, 24},   // [1] CAME 24  (2:1, te~320)
	{320, 640, 30, 0, 12},   // [2] CAME 12  (2:1, te~320)
	{700, 1400, 25, 0, 24},  // [3] Nice FLO 24 (2:1, te~700)
	{700, 1400, 25, 0, 12},  // [4] Nice FLO 12 (2:1, te~700)
	{350, 700, 30, 0, 24}    // [5] Gate TX 24 (2:1, te~350)
};

/* Decoder registry: index-aligned with subghz_protocols_list[] (params) */
const SubGHz_Decoder_t subghz_decoder_registry[] =
{
	{ "Princeton", subghz_decode_princeton },  /* [0] */
	{ "CAME",      subghz_decode_pwm },         /* [1] 24-bit */
	{ "CAME",      subghz_decode_pwm },         /* [2] 12-bit */
	{ "Nice FLO",  subghz_decode_pwm },         /* [3] 24-bit */
	{ "Nice FLO",  subghz_decode_pwm },         /* [4] 12-bit */
	{ "Gate TX",   subghz_decode_pwm }          /* [5] 24-bit */
};
const uint16_t subghz_n_decoders = sizeof(subghz_decoder_registry)/sizeof(subghz_decoder_registry[0]);

const char *subghz_protocol_name(uint16_t proto)
{
    if (proto < subghz_n_decoders) return subghz_decoder_registry[proto].name;
    return "Unknown";
}


enum {
   n_protocol = sizeof(subghz_protocols_list) / sizeof(subghz_protocols_list[0])
};


//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

SubGHz_DecEnc_t subghz_decenc_ctl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

inline uint16_t get_diff(uint16_t n_a, uint16_t n_b);
uint8_t subghz_pulse_handler(uint16_t duration);
bool subghz_decenc_read(SubGHz_Dec_Info_t *received, bool raw);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
inline uint16_t get_diff(uint16_t n_a, uint16_t n_b)
{
	return abs(n_a - n_b);
}



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
/* route2 diagnostics + explicit decode-ready flag (a valid fixed-code key can be 0) */
volatile uint32_t subghz_diag_isr_pulses = 0;
volatile uint32_t subghz_diag_insert_fail = 0;
volatile uint32_t subghz_diag_drained = 0;
volatile uint32_t subghz_diag_submitted = 0;
volatile uint32_t subghz_diag_attempts = 0;
volatile uint32_t subghz_diag_decoded = 0;
static volatile uint8_t subghz_decode_ready = 0;
volatile uint16_t subghz_seg_guard = 0; /* inter-packet guard captured at segmentation */

void subghz_scan_diag_reset(void)
{
  subghz_diag_isr_pulses=0; subghz_diag_insert_fail=0; subghz_diag_drained=0;
  subghz_diag_submitted=0; subghz_diag_attempts=0; subghz_diag_decoded=0;
}

bool subghz_data_ready()
{
  return (subghz_decode_ready != 0);
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_raw_data_ready()
{
  return (subghz_decenc_ctl.pulse_times[0] != 0);
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void subghz_reset_data()
{
	subghz_decode_ready = 0;
	subghz_decenc_ctl.n64_decodedvalue = 0;
	subghz_decenc_ctl.ndecodedbitlength = 0;
	memset(subghz_decenc_ctl.pulse_times, 0, sizeof(subghz_decenc_ctl.pulse_times));
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint64_t subghz_get_decoded_value()
{
	return subghz_decenc_ctl.n64_decodedvalue;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_bitlength()
{
	return subghz_decenc_ctl.ndecodedbitlength;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_delay()
{
	return subghz_decenc_ctl.ndecodeddelay;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t subghz_get_decoded_protocol()
{
	return subghz_decenc_ctl.ndecodedprotocol;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
int16_t subghz_get_decoded_rssi()
{
	return subghz_decenc_ctl.ndecodedrssi;
}


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint16_t *subghz_get_rawdata()
{
	return subghz_decenc_ctl.pulse_times;
}



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
/* subghz_decode_protocol() removed - replaced by subghz_decoder_registry[] dispatch */


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t subghz_pulse_handler(uint16_t duration)
{
	  static uint32_t interpacket_gap = 0;
	  uint8_t i;
	  int16_t rssi;
	  struct si446x_reply_GET_MODEM_STATUS_map *pmodemstat;

	  if (duration >= PACKET_PULSE_TIME_MIN)
	  {
		  if (duration >= INTERPACKET_GAP_MIN) // Possible gap between packets?
		  {
			  subghz_decenc_ctl.pulse_times[subghz_decenc_ctl.npulsecount++] = duration; // End bit

			  M1_LOG_D(M1_LOGDB_TAG, "Valid gap: %d, pulses:%d\r\n", duration, subghz_decenc_ctl.npulsecount);
			  subghz_seg_guard = (duration > 65535u) ? 65535u : (uint16_t)duration;
			  { uint16_t *_pt=subghz_decenc_ctl.pulse_times; uint16_t _mn=0xFFFF,_j; for(_j=0;_j<subghz_decenc_ctl.npulsecount;_j++) if(_pt[_j]>120&&_pt[_j]<_mn)_mn=_pt[_j];
			    M1_LOG_E(M1_LOGDB_TAG, "PKT n=%d min=%d guard=%u rssi=%d: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
			      subghz_decenc_ctl.npulsecount, _mn, subghz_seg_guard, (int)subghz_decenc_ctl.ndecodedrssi,
			      _pt[0],_pt[1],_pt[2],_pt[3],_pt[4],_pt[5],_pt[6],_pt[7],_pt[8],_pt[9],_pt[10],_pt[11],_pt[12],_pt[13],_pt[14],_pt[15]); }
			  if ( subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MIN ) // Potential packet received?
			  {
				  subghz_diag_attempts++;
				  {
				    uint8_t _bb=0; uint16_t _berr=0xFFFF; uint64_t _bval=0; uint16_t _bbits=0,_bproto=0,_bte=0; uint8_t _bf=0;
				    for(i = 0; i < subghz_n_decoders; i++)
				    {
				      if ( !subghz_decoder_registry[i].decode(i, subghz_decenc_ctl.npulsecount) )
				      {
				        uint16_t _bits=subghz_decenc_ctl.ndecodedbitlength, _mte=subghz_decenc_ctl.ndecodeddelay, _tte=subghz_protocols_list[i].te_short;
				        uint16_t _err=(_mte>_tte)?(_mte-_tte):(_tte-_mte);
				        if ( _bits > _bb || (_bits==_bb && _err<_berr) )
				        { _bb=_bits; _berr=_err; _bval=subghz_decenc_ctl.n64_decodedvalue; _bbits=_bits; _bproto=subghz_decenc_ctl.ndecodedprotocol; _bte=_mte; _bf=1; }
				      }
				    }
				    if ( _bf ) {
				      /* 24-bit CAME vs Gate TX by measured te (real remotes: CAME~263-296, GateTX~318-328; split 307) */
				      if ( _bbits==24 && _bte>=220 && _bte<550 ) _bproto = (_bte < 307) ? CAME_24 : GATE_TX;
				      subghz_decenc_ctl.n64_decodedvalue=_bval; subghz_decenc_ctl.ndecodedbitlength=_bbits; subghz_decenc_ctl.ndecodedprotocol=_bproto; subghz_decenc_ctl.ndecodeddelay=_bte; subghz_decode_ready=1; subghz_diag_decoded++; }
				  } // pick longest bit-length, tie-break closest te
			  } // if ( subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MIN )
			  interpacket_gap = duration; // update
			  subghz_decenc_ctl.npulsecount = 0;
			  // A potential interpacket gap has been detected, so it's not required to check for this condition for the next packet, if any.
			  return PULSE_DET_EOP; // error or end of packet has been met
		  } // if (duration >= INTERPACKET_GAP_MIN)
	  } // if (duration >= PACKET_PULSE_TIME_MIN)
	  else
	  {
		  subghz_decenc_ctl.npulsecount = 0; // reset
		  interpacket_gap += duration;
		  // Interpacket gap has been timeout for a potential packet
		  if ( interpacket_gap > INTERPACKET_GAP_MAX )
		  {
			  interpacket_gap = 0; // reset
			  return PULSE_DET_IDLE; // error
		  }
		  else
		  {
			  return PULSE_DET_NORMAL;
		  }
	  } // else
	  // detect overflow
	  if (subghz_decenc_ctl.npulsecount >= PACKET_PULSE_COUNT_MAX)
	  {
		  subghz_decenc_ctl.npulsecount = 0; // Reset rx buffer
		  return PULSE_DET_IDLE; // error
	  }
	  subghz_decenc_ctl.pulse_times[subghz_decenc_ctl.npulsecount++] = duration;
	  // Read RSSI when half of this potential packet has been received
	  if ( subghz_decenc_ctl.npulsecount==PACKET_PULSE_COUNT_MIN/2 )
	  {
		  // Read INTs, clear pending ones
		  SI446x_Get_IntStatus(0, 0, 0);
		  pmodemstat = SI446x_Get_ModemStatus(0x00);
		  // RF_Input_Level_dBm = (RSSI_value / 2) – MODEM_RSSI_COMP – 70
		  rssi = pmodemstat->CURR_RSSI/2 - MODEM_RSSI_COMP - 70;
		  subghz_decenc_ctl.ndecodedrssi = rssi;
	  } // if ( subghz_decenc_ctl.npulsecount==PACKET_PULSE_COUNT_MIN/2 )

	  return PULSE_DET_NORMAL;
} // uint8_t subghz_pulse_handler(uint16_t duration)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
bool subghz_decenc_read(SubGHz_Dec_Info_t *received, bool raw)
{
    bool ret = false;

	if ( subghz_decenc_ctl.subghz_data_ready() )
    {
        received->frequency = 0;
        received->key = subghz_decenc_ctl.subghz_get_decoded_value();
        received->protocol = subghz_decenc_ctl.subghz_get_decoded_protocol();
        received->rssi = subghz_decenc_ctl.subghz_get_decoded_rssi();
        received->te = subghz_decenc_ctl.subghz_get_decoded_delay();
        received->bit_len = subghz_decenc_ctl.subghz_get_decoded_bitlength();
        subghz_decenc_ctl.subghz_reset_data();
        ret = true;
    } // if ( subghz_decenc_ctl.subghz_data_ready() )

    if (raw && subghz_decenc_ctl.subghz_raw_data_ready())
    {
    	received->raw = true;
    	received->raw_data = subghz_decenc_ctl.subghz_get_rawdata();
        subghz_decenc_ctl.subghz_reset_data();
        ret = true;
    } // if (raw && subghz_decenc_ctl.subghz_raw_data_ready())

    return ret;
} // bool subghz_decenc_read(bool raw)


/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void subghz_decenc_init(void)
{
    subghz_decenc_ctl.subghz_data_ready = subghz_data_ready;
    subghz_decenc_ctl.subghz_raw_data_ready = subghz_raw_data_ready;
    subghz_decenc_ctl.subghz_reset_data = subghz_reset_data;

    subghz_decenc_ctl.subghz_get_decoded_value = subghz_get_decoded_value;
    subghz_decenc_ctl.subghz_get_decoded_bitlength = subghz_get_decoded_bitlength;
    subghz_decenc_ctl.subghz_get_decoded_delay = subghz_get_decoded_delay;
    subghz_decenc_ctl.subghz_get_decoded_protocol = subghz_get_decoded_protocol;
    subghz_decenc_ctl.subghz_get_decoded_rssi = subghz_get_decoded_rssi;
    subghz_decenc_ctl.subghz_get_rawdata = subghz_get_rawdata;
    subghz_decenc_ctl.subghz_pulse_handler = subghz_pulse_handler;

	subghz_decenc_ctl.n64_decodedvalue = 0;
	subghz_decenc_ctl.ndecodedbitlength = 0;
	subghz_decenc_ctl.ndecodedrssi = 0;
	subghz_decenc_ctl.ndecodeddelay = 0;
	subghz_decenc_ctl.ndecodedprotocol = 0;
	subghz_decenc_ctl.npulsecount = 0;
	subghz_decenc_ctl.pulse_det_stat = PULSE_DET_IDLE;
	memset(subghz_decenc_ctl.pulse_times, 0, sizeof(subghz_decenc_ctl.pulse_times));
	subghz_decenc_ctl.n64_decodedvalue = 0;
} // void subghz_decenc_init(void)
