/* See COPYING.txt for license details. */

/*
 * LF RFID (125 kHz) implementation
 *
 * Portions of the data structure definitions and table-driven
 * architecture were adapted from the Flipper Zero firmware project.
 *
 * Original project:
 * https://github.com/flipperdevices/flipperzero-firmware
 *
 * Copyright (C) Flipper Devices Inc.
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

const LFRFIDProtocolBase* lfrfid_protocols[] = {
    [LFRFIDProtocolEM4100] = &protocol_em4100,
    [LFRFIDProtocolEM4100_32] = &protocol_em4100_32,
    [LFRFIDProtocolEM4100_16] = &protocol_em4100_16,
    [LFRFIDProtocolH10301] = &protocol_h10301,
};

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
	if(protocol_index < LFRFIDProtocolMax)
	{
		if(lfrfid_protocols[protocol_index])
			if(lfrfid_protocols[protocol_index]->render_data)
				return lfrfid_protocols[protocol_index]->render_data(NULL,szstring);
	}
	(szstring = NULL);
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
	memcpy(pTaginfo->uid, lfrfid_tag_info.uid,5);
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

