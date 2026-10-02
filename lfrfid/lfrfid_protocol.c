/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) implementation
 *
 * Licensed under the GNU General Public License v3.0 (GPLv3).
 *
 * Modifications and additional implementation:
 * Copyright (C) 2026 Monstatek
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 */
/*************************** I N C L U D E S **********************************/
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"

#include "m1_log_debug.h"
#include "m1_sdcard.h"

#include "lfrfid.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//LFRFIDProtocol ProtocolID = -1;

/*
 * Explicitly sized [LFRFIDProtocolMax], NOT compiler-inferred from the
 * highest designated initializer present. This is a hard requirement, not
 * style: LFRFIDProtocolNexwatch exists unconditionally in the enum (its
 * numeric ID must never shift between build configurations), but its
 * registry entry below is conditionally compiled out when the NexWatch
 * decoder isn't built. Without an explicit size, the compiler would infer
 * the array's length from the highest initializer index ACTUALLY PRESENT
 * -- one element short of LFRFIDProtocolMax whenever the last enum slot's
 * entry is disabled -- while every LFRFIDProtocolMax-bounded loop in this
 * codebase (lfrfid_decoder_begin() foremost) still iterates the full
 * count. That mismatch was a real, hardware-reproduced bug: the phantom
 * final iteration read whatever global happened to be linked immediately
 * after this array, misinterpreted it as a protocol descriptor, and
 * branched through it. See fix/lfrfid-psk-entry-crash for the full
 * root-cause trace. The explicit size below guarantees every slot from
 * 0..LFRFIDProtocolMax-1 exists and is zero/NULL-initialized unless a
 * registry entry explicitly populates it -- exactly the guarantee every
 * consumer in this file, lfrfid.c and lfrfid_file.c already assumes.
 */
const LFRFIDProtocolBase* lfrfid_protocols[LFRFIDProtocolMax] = {
    [LFRFIDProtocolEM4100] = &protocol_em4100,
    [LFRFIDProtocolEM4100_32] = &protocol_em4100_32,
    [LFRFIDProtocolEM4100_16] = &protocol_em4100_16,
    [LFRFIDProtocolH10301] = &protocol_h10301,
    [LFRFIDProtocolPyramid] = &protocol_pyramid,
    [LFRFIDProtocolIoProxXSF] = &protocol_ioprox,
    [LFRFIDProtocolAWID] = &protocol_awid,
    [LFRFIDProtocolRadioKey] = &protocol_radiokey,
    [LFRFIDProtocolJablotron] = &protocol_jablotron,
    [LFRFIDProtocolFDXB] = &protocol_fdx_b,
    [LFRFIDProtocolHIDProx] = &protocol_hid_generic,
    [LFRFIDProtocolHIDExt] = &protocol_hid_ex_generic,
    [LFRFIDProtocolKeri] = &protocol_keri,
#if defined(LFRFID_NEXWATCH_ENABLED)
    [LFRFIDProtocolNexwatch] = &protocol_nexwatch,
#endif
};

/* Compile-time invariant: catches any future regression back to an
 * implicitly-sized array (or a new enum entry added without a
 * corresponding registry slot) at build time instead of at runtime on
 * hardware. */
_Static_assert(
    (sizeof(lfrfid_protocols) / sizeof(lfrfid_protocols[0])) == LFRFIDProtocolMax,
    "lfrfid_protocols[] length must equal LFRFIDProtocolMax"
);

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_decoder_begin(void)
{
	for (int i = 0; i < LFRFIDProtocolMax; i++)
	{
		if(lfrfid_protocols[i])
			if(lfrfid_protocols[i]->decoder.begin)
				lfrfid_protocols[i]->decoder.begin(NULL);
	}
}


/*============================================================================*/
/**
  * @brief Reset a single decoder's state via its begin() hook. Clears all
  *        frame/state buffers so the next successful decode must consume a new
  *        complete frame. The begin() hooks ignore their argument and touch
  *        only static decoder state (no RF hardware, no allocation).
  */
/*============================================================================*/
void lfrfid_decoder_reset(uint16_t protocol_index)
{
	if(protocol_index < LFRFIDProtocolMax)
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->decoder.begin)
				lfrfid_protocols[protocol_index]->decoder.begin(NULL);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool lfrfid_decoder_execute(uint16_t protocol_index, const lfrfid_evt_t* new_stream, uint8_t stream_count)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->decoder.execute)
				return lfrfid_protocols[protocol_index]->decoder.execute((void*)new_stream, stream_count);
	}
	return false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool lfrfid_encoder_begin(uint16_t protocol_index, void* proto)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->encoder.begin)
				return lfrfid_protocols[protocol_index]->encoder.begin(proto);
	}
	return false;

}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_encoder_send(uint16_t protocol_index, void* proto)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->encoder.send)
				lfrfid_protocols[protocol_index]->encoder.send(proto);
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_write_begin(uint16_t protocol_index, void* proto, void *data)
{

	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->write.begin)
				lfrfid_protocols[protocol_index]->write.begin(proto, data);
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_write_send(uint16_t protocol_index, void* proto)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->write.send)
				lfrfid_protocols[protocol_index]->write.send(proto);
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_get_data(uint16_t protocol_index, uint8_t* data, size_t data_size)
{

	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
		{
			uint8_t* protocol_data = lfrfid_protocols[protocol_index]->get_data(NULL);
			size_t protocol_data_size = lfrfid_protocols[protocol_index]->data_size;
			if(data_size >= protocol_data_size)
				memcpy(data, protocol_data, protocol_data_size);
		}
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint16_t protocol_get_data_size(uint16_t protocol_index)
{

	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->data_size)
				return lfrfid_protocols[protocol_index]->data_size;
	}
	return 0;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
const char* protocol_get_name(uint16_t protocol_index)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->name)
				return lfrfid_protocols[protocol_index]->name;
	}
	return NULL;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
const char* protocol_get_manufacturer(uint16_t protocol_index)
{
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->manufacturer)
				return lfrfid_protocols[protocol_index]->manufacturer;
	}
	return NULL;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void protocol_render_data(uint16_t protocol_index, char* szstring)
{
	if(!szstring)
		return;
	szstring[0] = '\0';
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->render_data)
				return lfrfid_protocols[protocol_index]->render_data(NULL,szstring);
	}
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_GetTagInfo(PLFRFID_TAG_INFO pTaginfo)
{
	memcpy(pTaginfo->uid, lfrfid_tag_info.uid, sizeof(pTaginfo->uid));
	pTaginfo->bitrate = lfrfid_tag_info.bitrate;
	pTaginfo->protocol = lfrfid_tag_info.protocol;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void lfrfid_tag_info_init(void)
{
	memset(&lfrfid_tag_info,0,sizeof(lfrfid_tag_info));
	lfrfid_tag_info.protocol = -1;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool lfrfid_write_verify(LFRFID_TAG_INFO* write, LFRFID_TAG_INFO* readback)
{
	bool result = false;

	if(write->protocol == readback->protocol)
	{
		uint32_t data_size = protocol_get_data_size(write->protocol);
		if(memcmp(write->uid, readback->uid, data_size) == 0)
		{
			result = true;
		}
	}

	return result;
}
