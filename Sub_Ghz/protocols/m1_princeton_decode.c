/* See COPYING.txt for license details. */

/*
*
*  m1_princeton_decode.c
*
*  M1 sub-ghz Princeton decoding
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "bit_util.h"
#include "m1_sub_ghz_decenc.h"
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"SUBGHZ_PRINCETON"



//************************** C O N S T A N T **********************************/



//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t subghz_decode_princeton(uint16_t p, uint16_t pulsecount);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t subghz_decode_princeton(uint16_t p, uint16_t pulsecount)
{
    uint32_t code;
    uint16_t te_long, te_short, tolerance_long, tolerance_short;
    uint16_t i;
    uint8_t tolerance, odd_bit, pre_bit_one, bits_count, max_bits;

    tolerance = subghz_protocols_list[p].te_tolerance;
    max_bits = subghz_protocols_list[p].data_bits;
    //te_short = subghz_protocols_list[p].te_short;
    //te_long = subghz_protocols_list[p].te_long;
    te_short = subghz_decenc_ctl.pulse_times[2];
    te_long = subghz_decenc_ctl.pulse_times[3];
    if ( te_long < te_short )
    {
    	tolerance_long = te_short;
    	te_short = te_long;
    	te_long = tolerance_long; // Swap bits
    } // if ( te_long < te_short )
    tolerance_short = te_short*3; // Assuming this is a valid long bit
    tolerance_long = (tolerance_short*tolerance)/100;
    if ( get_diff(te_long, tolerance_short) > tolerance_long ) // Not a valid bit for this protocol?
    {
    	return 1;
    }

    tolerance_short = (te_short*tolerance)/100;
    tolerance_long = (te_long*tolerance)/100;
    code = 0;
    odd_bit = 0;
    pre_bit_one = 0;
    bits_count = 0;

    for (i = 0; i<pulsecount; i++)
    {
    	if ( !odd_bit )
    	{
    		if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
    		{
    			pre_bit_one = 0;
    		}
    		else if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
    		{
    			pre_bit_one = 1;
    		}
    		else
    		{
    			break;
    		}
    		if ( i >= 2*max_bits )
    			break;
    	} // if ( !odd_bit )
    	else // odd bit
    	{
    		code <<= 1;
    		if ( pre_bit_one )
    		{
    			if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_short) < tolerance_short )
    			{
    				code |= 1; // Bit 1
    			}
    			else
    			{
    				break; // Error
    			}
    		} // if ( pre_bit_one )
    		else
    		{
        		if ( get_diff(subghz_decenc_ctl.pulse_times[i], te_long) < tolerance_long )
        		{
        			; // Bit 0
        		}
        		else
        		{
        			break; // Error
        		}
    		} // else
    		bits_count++;
    	} // else
        odd_bit ^= 1;
    } // for (i = 0; i<pulsecount; i++)

    if (bits_count >= max_bits ) // Let take this packet if all bits have been decoded
    {
        subghz_decenc_ctl.n64_decodedvalue = code;
        subghz_decenc_ctl.ndecodedbitlength = bits_count;
        subghz_decenc_ctl.ndecodeddelay = 0; //delay;
        subghz_decenc_ctl.ndecodedprotocol = p;
        return 0;
    }
    else
    {
        /* silenced: per-packet reject log flooded console */
    	for (i=0; i<10; i++)
    		M1_LOG_N(M1_LOGDB_TAG, "%d ", subghz_decenc_ctl.pulse_times[i]);
    	M1_LOG_N(M1_LOGDB_TAG, "\r\n");
    }

    return 1;
} // uint8_t subghz_decode_princeton(uint16_t p, uint16_t pulsecount)

