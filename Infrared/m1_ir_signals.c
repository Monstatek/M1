/* See COPYING.txt for license details. */
/*
*
* ir_signals.c
*
* Infrared signals
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_storage.h"
#include "m1_sdcard_man.h"
#include "m1_ir_signals.h"
#include "m1_infrared.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"IR_SIG"

#define IR_DATAFILE_VERSION				1

#define IR_HEADER_SIGNALS  				"IR signals file"
#define IR_HEADER_LIBRARY				"IR library file"

#define IR_SIGNALS_KEYWORD_DELIMITER	' '
#define IR_SIGNALS_KEYWORD_COMMENT		'#'
#define IR_SIGNALS_KEYWORD_CRLF			"\n"//"\r\n"

#define IR_SIGNALS_KEYWORD_NAME			"name:"
#define IR_SIGNALS_KEYWORD_TYPE 		"type:"
#define IR_SIGNALS_KEYWORD_DATA       	"data:"
#define IR_SIGNALS_KEYWORD_FREQUENCY  	"frequency:"
#define IR_SIGNALS_KEYWORD_DUTY_CYCLE 	"duty_cycle:"
#define IR_SIGNALS_KEYWORD_PROTOCOL 	"protocol:"
#define IR_SIGNALS_KEYWORD_ADDRESS  	"address:"
#define IR_SIGNALS_KEYWORD_COMMAND  	"command:"
#define IR_SIGNALS_KEYWORD_TYPE_RAW		"raw"
#define IR_SIGNALS_KEYWORD_TYPE_PARSED	"parsed"

#define IR_REMOTE_UNKNOWN_FUNC_LIST		0
#define IR_REMOTE_TV_FUNC_LIST			6
#define IR_REMOTE_AUDIO_FUNC_LIST		8
#define IR_REMOTE_PROJECTOR_FUNC_LIST	4
#define IR_REMOTE_AC_FUNC_LIST			6
#define IR_REMOTE_FUNC_LIST_MAX			IR_REMOTE_AUDIO_FUNC_LIST // Take the longest function list defined above

#define IR_RAW_DATA_SAMPLES_MAX			64000

#define IR_SIGNALS_PROTOCOL_NAME_LEN_MAX	12

/*************************** C O N S T A N T **********************************/

static const char *ir_datfile_header_keywords[] =
{
	"Filetype:",
	"Version:"
};

static const char *ir_datfile_payload_keywords[] =
{
	IR_SIGNALS_KEYWORD_NAME,
	IR_SIGNALS_KEYWORD_TYPE,
	IR_SIGNALS_KEYWORD_PROTOCOL,
	IR_SIGNALS_KEYWORD_ADDRESS,
	IR_SIGNALS_KEYWORD_COMMAND,
	IR_SIGNALS_KEYWORD_FREQUENCY,
	IR_SIGNALS_KEYWORD_DUTY_CYCLE,
	IR_SIGNALS_KEYWORD_DATA
};

static const char *ir_remote_tv_functions[] =
{
	"Power",
	"Mute",
	"Vol_up",
	"Ch_next",
	"Vol_dn",
	"Ch_prev",
};

static const char *ir_remote_audio_functions[] =
{
	"Power",
	"Mute",
	"Play",
	"Vol_up",
	"Pause",
	"Vol_dn",
	"Prev",
	"Next"
};

static const char *ir_remote_projector_functions[] =
{
	"Power",
	"Mute",
	"Vol_up",
	"Vol_dn"
};

static const char *ir_remote_ac_functions[] =
{
	"Off",
	"Dh",
	"Cool_hi",
	"Cool_lo",
	"Heat_hi",
	"Heat_lo"
};

//************************** S T R U C T U R E S *******************************

typedef enum
{
	IR_FILETYPE_UNKNOWN = 0,
	IR_FILETYPE_SIGNALS,
	IR_FILETYPE_LIBRARY,
	IR_FILETYPE_EOL
} S_M1_IR_FileType_t;

typedef enum // The order must match that of ir_datfile_payload_keywords[]
{
	IR_PAYLOAD_NAME = 0,
	IR_PAYLOAD_TYPE,
	IR_PAYLOAD_PROTOCOL,
	IR_PAYLOAD_ADDRESS,
	IR_PAYLOAD_COMMAND,
	IR_PAYLOAD_FREQUENCY,
	IR_PAYLOAD_DUTY_CYCLE,
	IR_PAYLOAD_DATA
} S_M1_IR_PayLoad_t;

typedef struct
{
	const char **ir_remote_type_functions;
	const uint8_t ir_remote_func_list;
} S_M1_Ir_Remote_List_t;

/***************************** V A R I A B L E S ******************************/

static uint8_t *sdcard_dat_buffer = NULL;
static uint8_t ir_remote_type, ir_datfile_type, ir_datfile_version;
static uint8_t ir_data_type;
static uint8_t ir_big_raw_data;
static uint16_t sdcard_dat_read_size;
static uint16_t	ir_raw_data_buffer_size;
static uint16_t ir_raw_samples_count;
static uint8_t *sdcard_buffer_run_ptr = NULL;
static uint16_t *ir_raw_data_buffer = NULL;
static uint32_t ir_payload_value_address, ir_payload_value_command;
static uint32_t ir_payload_value_frequency, ir_payload_value_duty_cycle;
static uint8_t ir_payload_value_protocol_p[IR_SIGNALS_PROTOCOL_NAME_LEN_MAX + 1];
static S_M1_SDM_DatFileInfo_t datfile_info;
static uint32_t sdcard_dat_file_size, sdcard_dat_buffer_end_pos;
static uint16_t ir_remote_dev_func_counter[IR_REMOTE_FUNC_LIST_MAX];

/*************************** C O N S T A N T **********************************/

const S_M1_Ir_Remote_List_t ir_remote_list[] = // The list order must match that of S_M1_IR_RemoteType_t
{
		{	.ir_remote_type_functions = ir_remote_tv_functions,
			.ir_remote_func_list = IR_REMOTE_TV_FUNC_LIST
		},
		{	.ir_remote_type_functions = ir_remote_audio_functions,
			.ir_remote_func_list = IR_REMOTE_AUDIO_FUNC_LIST
		},
		{	.ir_remote_type_functions = ir_remote_projector_functions,
			.ir_remote_func_list = IR_REMOTE_PROJECTOR_FUNC_LIST
		},
		{	.ir_remote_type_functions = ir_remote_ac_functions,
			.ir_remote_func_list = IR_REMOTE_AC_FUNC_LIST
		}
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type);
uint8_t ir_remote_file_data_check(uint8_t remote_type);
static uint8_t ir_datfile_keyword_val_get(char *instr, uint32_t *out_val, uint8_t val_type, bool validation_req);
static uint8_t ir_datfile_keyword_payload_get(char *token, uint8_t payload_id, bool validation_req);
uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type);
uint8_t ir_datfile_end_of_file_check(void);
uint16_t ir_remote_dev_func_counter_get(uint8_t function_type);
uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload);
static uint8_t ir_remote_file_raw_data_parse(char *instr);
void ir_remote_file_deinit(void);
static uint32_t ir_bits_reverse32 (uint32_t in_val, uint8_t len);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief This function allocates memory for data buffers, reads data file header and checks its format
  * @param  None
  * @retval error code
  */
/*============================================================================*/
uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type)
{
	uint16_t sdcard_read_result;
	uint8_t error, *token, *str;
	char *end_ptr;
	uint8_t *psdcard_dat_buffer = NULL;

	strcpy(datfile_info.dat_filename, remotes_filename);
	ir_remote_type = remote_type;
	ir_big_raw_data = 0; // Reset

	do
	{
		error = m1_sdm_get_logging_error();
		if ( error )
			break;
		sdcard_dat_buffer = m1_malloc(M1_SDM_MIN_BUFFER_SIZE);
		if (sdcard_dat_buffer==NULL)
		{
			error = 1;
			break;
		}
		sdcard_dat_buffer += M1_SDM_MIN_BUFFER_SIZE/2; // Start at the middle of the buffer
		sdcard_dat_read_size = M1_SDM_MIN_BUFFER_SIZE/4; // Limit the reading size from SD card to avoid data error
		ir_raw_data_buffer_size = IR_RAW_DATA_SAMPLES_MAX;

		while ( true )
		{
			ir_raw_data_buffer = malloc(ir_raw_data_buffer_size*sizeof(uint16_t));
			if ( ir_raw_data_buffer )
				break;
			ir_raw_data_buffer_size /= 2;
		} // while ( true )

		error = m1_fb_open_file(&datfile_info.dat_file_hdl, datfile_info.dat_filename);
		if (error)
			break;

		sdcard_dat_file_size = f_size(&datfile_info.dat_file_hdl);
		if ( sdcard_dat_read_size > sdcard_dat_file_size )
			sdcard_dat_read_size = sdcard_dat_file_size;
		sdcard_read_result = m1_fb_read_from_file(&datfile_info.dat_file_hdl, sdcard_dat_buffer, sdcard_dat_read_size);
		if ( sdcard_read_result != sdcard_dat_read_size )
		{
			error = 1;
			break;
		}
		sdcard_dat_buffer[sdcard_dat_read_size] = '\0'; // Add end of string to the buffer
		sdcard_buffer_run_ptr = sdcard_dat_buffer;
		sdcard_dat_file_size -= sdcard_dat_read_size; // Update the remainder

		psdcard_dat_buffer = malloc(sdcard_dat_read_size + 1);
		if ( psdcard_dat_buffer==NULL )
		{
			error = 1;
			break;
		}
		memcpy(psdcard_dat_buffer, sdcard_dat_buffer, sdcard_dat_read_size + 1); // Duplicate this file header

		ir_datfile_type = IR_FILETYPE_UNKNOWN;
		token = strtok(psdcard_dat_buffer, IR_SIGNALS_KEYWORD_CRLF); // Tokenize this duplicated buffer
		if ( strstr(token, ir_datfile_header_keywords[0]) )
		{
			if ( strstr(token, IR_HEADER_SIGNALS) )
				ir_datfile_type = IR_FILETYPE_SIGNALS;
			else if ( strstr(token, IR_HEADER_LIBRARY) )
				ir_datfile_type = IR_FILETYPE_LIBRARY;
		} // if ( strstr(token, ir_datfile_header_keywords[0]) )

		ir_datfile_version = 0;
		while ( ir_datfile_type!=IR_FILETYPE_UNKNOWN )
		{
			sdcard_buffer_run_ptr += strlen(token) + strlen(IR_SIGNALS_KEYWORD_CRLF);
			token = strtok(NULL, IR_SIGNALS_KEYWORD_CRLF); // Version
			if ( strstr(token, ir_datfile_header_keywords[1])==NULL ) // Version
				break;
			str = strstr(token, ":");
			str += 1; // Move to the Version value
			ir_datfile_version = strtol(str, &end_ptr, 10);
			sdcard_buffer_run_ptr += strlen(token) + strlen(IR_SIGNALS_KEYWORD_CRLF);
			break;
		} // while ( ir_datfile_type!=IR_FILETYPE_UNKNOWN )
		if ( (ir_datfile_version==0) || (ir_datfile_version > IR_DATAFILE_VERSION) )
		{
			error = 1;
			break;
		} // if ( (ir_datfile_version==0) || (ir_datfile_version > IR_DATAFILE_VERSION) )

		sdcard_dat_buffer_end_pos = (uint32_t)sdcard_dat_buffer + sdcard_dat_read_size;

	} while(0); // while (0)

	if ( psdcard_dat_buffer!=NULL )
		free(psdcard_dat_buffer);

	if ( error )
	{
		if (sdcard_dat_buffer!=NULL)
		{
			sdcard_dat_buffer -= M1_SDM_MIN_BUFFER_SIZE/2; // Restore original ptr
			free(sdcard_dat_buffer);
			sdcard_dat_buffer = NULL;
		}
		if ( ir_raw_data_buffer != NULL )
		{
			free(ir_raw_data_buffer);
			ir_raw_data_buffer = NULL;
		}
	} // if ( error )

	return error;

} // uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t ir_remote_file_data_check(uint8_t remote_type)
{
	uint8_t i, error_code;

	error_code = 1;

	if ( remote_type >= IR_REMOTETYPE_UNKNOWN )
		return error_code;

	for (i=0; i<IR_REMOTE_FUNC_LIST_MAX; i++ )
		ir_remote_dev_func_counter[i] = 0;

	ir_raw_data_buffer_size = IR_RAW_DATA_SAMPLES_MAX;

	while ( sdcard_dat_file_size )
	{
		error_code = ir_remote_function_data_read(remote_type, IR_REMOTETYPE_ALL_DEVICES_NONE);
		if ( error_code )
			break;
	} // while ( sdcard_dat_file_size )

	if ( error_code==IR_ERROR_CODE_END_OF_FILE )
		error_code = 0;

	return error_code;
} // uint8_t ir_remote_file_data_check(uint8_t remote_type)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type)
{
	char *token, *runptr;
	uint8_t error_code, ret_val, eof, crlf_found, read_more;
	uint8_t function_id, payload_id, last_loop;
	uint16_t sdcard_read_result, token_len;
	const S_M1_Ir_Remote_List_t *this_ir_remote;

	error_code = 1;
	eof = 0;
	payload_id = 0;
	last_loop = 0;
	token = NULL;
	// Reset with default values
	ir_payload_value_address = 0x00;
	ir_payload_value_command = 0x00;
	ir_payload_value_frequency = IR_ENCODE_CARRIER_FREQ_38_KHZ;
	ir_payload_value_duty_cycle = 33;

	while ( true )
	{
		if ( token )
		{
			sdcard_buffer_run_ptr += strlen(token) + strlen(IR_SIGNALS_KEYWORD_CRLF);
		}
		crlf_found = 0;
		read_more = 1;
		token = strstr(sdcard_buffer_run_ptr, IR_SIGNALS_KEYWORD_CRLF);
		if ( (token != NULL) || (eof) || (ir_big_raw_data) ) // CRLF found or End Of File or High number of samples?
		{
			if ( token != NULL ) // CRLF found?
			{
				*token = '\0'; // Add NULL to the end of this string
				crlf_found = 1;
			} // if ( token != NULL )
			eof = 0;
			read_more = 0;
			token = sdcard_buffer_run_ptr; // Restore pointer to the beginning of the string
			token_len = strlen(ir_datfile_payload_keywords[payload_id]);
			ret_val = m1_datfile_keywords_check(token, ir_datfile_payload_keywords[payload_id], IR_SIGNALS_KEYWORD_DELIMITER);
			if ( ret_val ) // Not found, found at the wrong position, or being a comment?
			{
				if ( token[0]==(uint8_t)IR_SIGNALS_KEYWORD_COMMENT ) // A comment?
					continue; // Skip it and continue
				if ( !ir_big_raw_data )
					break;
			} // if ( ret_val )
			if ( crlf_found )
			{
				// This flag is always reset when a CRLF is found.
				ir_big_raw_data = 0;
			} // if ( crlf_found )
			if ( payload_id==0 ) // name
			{
				if ( remote_type!=IR_REMOTETYPE_UNKNOWN )
				{
					this_ir_remote = &ir_remote_list[remote_type];
					for (function_id=0; function_id<this_ir_remote->ir_remote_func_list; function_id++)

					{
						if ( strstr(token, this_ir_remote->ir_remote_type_functions[function_id]) ) // Search found?
							break;
					}
					if ( function_id >= this_ir_remote->ir_remote_func_list ) // Search not found
						break;
					if ( m1_datfile_keywords_check(&token[token_len + 1], this_ir_remote->ir_remote_type_functions[function_id], '\x0') )
						break;
					if ( function_type==IR_REMOTETYPE_ALL_DEVICES_NONE ) // All function types?
						ir_remote_dev_func_counter[function_id]++; // Add to counter
				} // if ( remote_type!=IR_REMOTETYPE_UNKNOWN )
				else
				{
					; // General remote
				}
				payload_id++;
			} // if ( payload_id==0 )
			else if ( payload_id==1 ) // type
			{
				ir_data_type = IR_DATATYPE_UNKNOWN;
				if ( strstr(token, IR_SIGNALS_KEYWORD_TYPE_PARSED) ) // Search found?
				{
					ir_data_type = IR_DATATYPE_PARSED;
					runptr = IR_SIGNALS_KEYWORD_TYPE_PARSED;
				}
				else if ( strstr(token, IR_SIGNALS_KEYWORD_TYPE_RAW) ) // Search found?
				{
					ir_data_type = IR_DATATYPE_RAW;
					runptr = IR_SIGNALS_KEYWORD_TYPE_RAW;
				}
				else
				{
					break;
				}
				if ( m1_datfile_keywords_check(&token[token_len + 1], runptr, '\0') )
					break;
				payload_id++;
				if ( ir_data_type==IR_DATATYPE_RAW )
				{
					payload_id += 3; // Move to the right buffer
				}
			} // else if ( payload_id==1 )
			else
			{
				token_len = strlen(token);
				ret_val = ir_datfile_keyword_payload_get(token, payload_id, true);
				if ( ret_val ) // Error?
					break;

				switch ( payload_id )
				{
					case IR_PAYLOAD_COMMAND: // command: 15 00 00 00
					case IR_PAYLOAD_DATA: // data: 175 7436 179 4913 175...
						if ( !ir_big_raw_data ) // Not being the high number of raw samples?
						{
							payload_id = 0; // Reset
						}
						break;

					//case IR_PAYLOAD_PROTOCOL: // protocol: SIRC
					//case IR_PAYLOAD_ADDRESS: // address: 01 00 00 00
					//case IR_PAYLOAD_FREQUENCY: // frequency: 38000
					//case IR_PAYLOAD_DUTY_CYCLE: // duty_cycle: 0.330000
					default:
						payload_id++; // Move to next keyword
						break;
				} // switch ( payload_id )

				if ( payload_id==0 ) // A complete record read?
				{
					sdcard_buffer_run_ptr += token_len + strlen(IR_SIGNALS_KEYWORD_CRLF); // Update running ptr
					if ( function_type < IR_REMOTETYPE_ALL_DEVICES_NONE ) // The caller function requests a specific function type to be read?
					{
						if ( function_id != function_type ) // Function type found not matched?
						{
							token = NULL; // Reset
							continue; // Continue to read data
						}
					} // if ( function_type < IR_REMOTETYPE_ALL_DEVICES_NONE )
					error_code = 0;
					break;
				} // if ( payload_id==0 )
				else if ( ir_big_raw_data )
				{
					// sdcard_buffer_run_ptr is updated by the function ir_remote_file_raw_data_parse() in this case!
					token = NULL;
					read_more = 1;
				} // else if ( ir_big_raw_data )
			} // else
		}  // if ( (token != NULL) || (eof) || (ir_big_raw_data) )
		else
		{
			if (  (sdcard_buffer_run_ptr < sdcard_dat_buffer) && (payload_id==IR_PAYLOAD_DATA) )
			{
				ir_big_raw_data = 1;
				continue;
			} // if (  (sdcard_buffer_run_ptr < sdcard_dat_buffer) && (payload_id==IR_PAYLOAD_DATA) )
		} // else

		if ( read_more )
		{
			if ( sdcard_dat_file_size==0 ) // End of file?
			{
				if ( (uint32_t)sdcard_buffer_run_ptr >= sdcard_dat_buffer_end_pos ) // No remaining data?
				{
					error_code = IR_ERROR_CODE_END_OF_FILE; // End of file has reached. The task is completed!
					break;
				} // if ( (uint32_t)sdcard_buffer_run_ptr >= sdcard_dat_buffer_end_pos )
				else // Remaining data with no CRLF?
				{
					if ( !last_loop )
					{
						last_loop = 1;
						token = NULL;
						eof = 1;
						continue; // Try the last time for the remaining data
					} // if ( !last_loop )
					else
					{
						break; // Error
					}
				} // else
			} // if ( sdcard_dat_file_size==0 )

			sdcard_dat_buffer_end_pos -= (uint32_t)sdcard_buffer_run_ptr; // Remaining data
			if ( sdcard_dat_buffer_end_pos ) // Is there remaining data?
			{
				// If this is the last chunk of data, this condition won't be met.
				// In that case, moving old data to the front in the buffer is not expected!
				if ( sdcard_buffer_run_ptr > sdcard_dat_buffer )
				{
					// If the write buffer address is not aligned, the data read from SDcard may be corrupted randomly when writing.
					// So if there're remaining characters, let copy those characters to before the beginning of the aligned buffer
					sdcard_dat_buffer -= sdcard_dat_buffer_end_pos; // Move pointer to the front
					memcpy(sdcard_dat_buffer, sdcard_buffer_run_ptr, sdcard_dat_buffer_end_pos); // Copy old data
					sdcard_buffer_run_ptr = sdcard_dat_buffer; // Update new running ptr
					sdcard_dat_buffer += sdcard_dat_buffer_end_pos; // Restore
				} // if ( sdcard_buffer_run_ptr > sdcard_dat_buffer )
			} // if ( sdcard_dat_buffer_end_pos )
			else
			{
				sdcard_buffer_run_ptr = sdcard_dat_buffer; // Reset the running ptr
			} // else

			if ( sdcard_dat_file_size!=0 )
			{
				if ( sdcard_dat_read_size > sdcard_dat_file_size ) // Last block to read from file?
					sdcard_dat_read_size = sdcard_dat_file_size; // Adjust the read size
				sdcard_read_result = m1_fb_read_from_file(&datfile_info.dat_file_hdl, sdcard_dat_buffer, sdcard_dat_read_size);
				if ( sdcard_read_result != sdcard_dat_read_size )
				{
					break;
				}
				sdcard_dat_buffer_end_pos = (uint32_t)sdcard_dat_buffer; // Update moving buffer pointer
				sdcard_dat_buffer_end_pos += sdcard_dat_read_size; // Update buffer end index for this data block
				sdcard_dat_buffer[sdcard_dat_read_size] = '\0'; // Add end of string to the buffer
				sdcard_dat_file_size -= sdcard_dat_read_size; // Update the remainder
				if ( sdcard_dat_file_size==0 ) // End of file reached?
				{
					ir_big_raw_data = 0; // Reset
					eof = 1; // End Of File flag
				} // if ( sdcard_dat_file_size==0 )
				continue;
			} // if ( sdcard_dat_file_size!=0 )
		} // if ( read_more )
	} // while (true)

	return error_code;
} // uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type)



/*============================================================================*/
/**
  * @brief	Convert text-format data (little endian) from "address: 01 00 00 00"
  * 		or "command: 15 00 00 00",
  * 		or "frequency: 38000" or "duty_cycle: 0.330000"
  * 		to a 32-bit value.
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t ir_datfile_keyword_val_get(char *instr, uint32_t *out_val, uint8_t val_type, bool validation_req)
{
	uint8_t val8, count, error_code;
	uint8_t *runptr;
	char *endptr;

	runptr = strstr(instr, " "); // First occurrence of space should always be found!
	count = strlen(runptr);
	*out_val = 0;
	error_code = 1;

	switch(val_type)
	{
		case IR_PAYLOAD_ADDRESS:
		case IR_PAYLOAD_COMMAND:
			// "address: 01 00 00 00" or "command: 15 00 00 00"
			if ( count != 12 ) // 4 2-character number plus 4 spaces
				return error_code;
			count = 0;
			while ( count < 4 )
			{
				if ( runptr[0] != IR_SIGNALS_KEYWORD_DELIMITER ) // The space before a number is not found?
					break;
				if ( !isxdigit(runptr[1]) || !isxdigit(runptr[2]) )
					break;
				val8 = (uint8_t)strtol(&runptr[1], NULL, 16);
				*out_val |= (uint32_t)val8 << count*8; // Small endian format
				runptr += 3; // Move to next space
				count++;
			} // while ( count < 4 )
			if ( count==4 )
				error_code = 0;
			if ( validation_req )
			{
				;
			}
			break;

		case IR_PAYLOAD_FREQUENCY:
			//"frequency: 38000"
			if ( count != 6 ) // space + length of the number
				return error_code;
			*out_val = strtol(&runptr[1], &endptr, 10);
			if ( *endptr == '\0' ) // Valid number?
				error_code = 0;
			if ( validation_req )
			{
/*
				for ( count=0; count<IR_ENCODE_CARRIER_FREQ_LIST_MAX; count++)
				{
					if ( *out_val==ir_carrier_frequency_list[count] )
						break;
				}
				if ( count >= IR_ENCODE_CARRIER_FREQ_LIST_MAX )
				{
					error_code = 1;
				}
*/
				if ( (*out_val < IR_ENCODE_CARRIER_FREQ_MIN_KHZ) || (*out_val > IR_ENCODE_CARRIER_FREQ_MAX_KHZ) )
					error_code = 1;
			} // if ( validation_req )
			break;

		case IR_PAYLOAD_DUTY_CYCLE:
			//"duty_cycle: 0.330000"
			if ( count > 9 ) // space + length of the number
				return error_code;
		    *out_val = (uint32_t)(100*strtof(&runptr[1], &endptr));
			if ( *endptr == '\0' ) // Valid number?
				error_code = 0;
			if ( validation_req )
			{
				if ( (*out_val < 10) || (*out_val > 100) ) // Duty cycle should be at least 10%!
					error_code = 1;
			} // if ( validation_req )
			break;

		case IR_PAYLOAD_PROTOCOL:
			// protocol: SIRC
			endptr = (uint8_t *)out_val;
			do
			{
				if ( !endptr )
					break;
				count = strlen(&runptr[1]);
				if ( count > IR_SIGNALS_PROTOCOL_NAME_LEN_MAX )
					break;
				strcpy(endptr, &runptr[1]);
				error_code = 0;
			} while (0);
			if ( validation_req )
			{
				// Supported protocols defined in ir_protocols_mapping_table[IR_PROTOCOLS_MAPPING_MAX]
				// in ir_remotes.c
				; // Validation will be handled in other process later!
			}
			break;

		default:
			break;
	} // switch(val_type)

	return error_code;
} // static uint8_t ir_datfile_keyword_val_get(char *instr, uint32_t *out_val, uint8_t val_type, bool validation_req)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t ir_datfile_keyword_payload_get(char *token, uint8_t payload_id, bool validation_req)
{
	uint8_t ret_val = 1;
	uint32_t *payload_ptr = NULL;

	switch ( payload_id )
	{
		case IR_PAYLOAD_PROTOCOL: // protocol: SIRC
			payload_ptr = (uint32_t *)ir_payload_value_protocol_p;
			break;

		case IR_PAYLOAD_ADDRESS: // address: 01 00 00 00
			payload_ptr = &ir_payload_value_address;
			break;

		case IR_PAYLOAD_COMMAND: // command: 15 00 00 00
			payload_ptr = &ir_payload_value_command;
			break;

		case IR_PAYLOAD_FREQUENCY: // frequency: 38000
			payload_ptr = &ir_payload_value_frequency;
			break;

		case IR_PAYLOAD_DUTY_CYCLE: // duty_cycle: 0.330000
			payload_ptr = &ir_payload_value_duty_cycle;
			break;

		case IR_PAYLOAD_DATA: // data: 175 7436 179 4913 175...
			if ( ir_data_type!=IR_DATATYPE_RAW )
			{
				ret_val = 1;
			}
			else
			{
				ret_val = ir_remote_file_raw_data_parse(token);
			}
			break;

		default:
			ret_val = 1;
			break;
	} // switch ( payload_id )

	if ( payload_ptr )
	{
		ret_val = ir_datfile_keyword_val_get(token, payload_ptr, payload_id, validation_req);
	}

	return ret_val;
} // static uint8_t ir_datfile_keyword_payload_get(char *token, uint8_t payload_id, bool validation_req)



/*============================================================================*/
/**
  * @brief  An extra pause time is added automatically to the start of the sample buffer
  * 		Maximum allowed bit time from raw samples is 65535x10 uS
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t ir_remote_file_raw_data_parse(char *instr)
{
	char *token, *endptr;
	uint8_t error_code, len, last_number;
	static uint8_t big_raw_data = 0;
	uint32_t number;

	last_number = 0;
	error_code = 1;
	if ( !big_raw_data )
	{
		ir_raw_samples_count = 0; // Reset
		// Extra delay time between raw frames
		ir_raw_data_buffer[ir_raw_samples_count++] = ((IR_REMOTE_PAUSE_TIME_BETWEEN_FRAME*1000)/IR_ENCODE_BASEBAND_PRESCALE_FACTOR);
		instr += strlen(IR_SIGNALS_KEYWORD_DATA) + 1; // Move pointer to the beginning of the numbers
	} // if ( !big_raw_data )

	while ( true )
	{
		token = strstr(instr, " "); // Find next numbers
		if ( token==NULL ) // Not found or last number?
		{
			big_raw_data = ir_big_raw_data; // Update
			if ( big_raw_data )
			{
				sdcard_buffer_run_ptr = instr; // Update
				error_code = 0;
				break; // return
			} // if ( big_raw_data )
			len = strlen(instr);
			if ( len ) // Last number?
			{
				token = instr + len;
				last_number = 1;
			} // if ( len )
			else // End of string
			{
				if ( ir_raw_samples_count > 1 ) // At least two samples read?
				{
					error_code = 0;
				}
				break;
			} // else
		} // if ( token==NULL )
		*token = '\0'; // Add end of string to replace the " " at the end
		token = instr;
		len = strlen(token);
		instr += len + 1; // Move read pointer to the next item, +1 for " " at the end
		if ( !len ) // This is just a space, not a number?
			continue;
		number = strtol(token, &endptr, 10);
		if ( *endptr != '\0' ) // Not a valid number?
		{
			break;
		}
		if ( number < IR_ENCODE_BASEBAND_PRESCALE_FACTOR ) // The value is less than IR_ENCODE_BASEBAND_PRESCALE_FACTOR?
		{
			if ( !number ) // Not a valid value?
			{
				M1_LOG_E(M1_LOGDB_TAG, "Raw data 0 -> Samples: %d\r\n", ir_raw_samples_count);
				break;
			} // if ( !number )
			//break;
			number = IR_ENCODE_BASEBAND_PRESCALE_FACTOR; // Bring it to the minimum value
		} // if ( number < IR_ENCODE_BASEBAND_PRESCALE_FACTOR )
		ir_raw_data_buffer[ir_raw_samples_count++] = (number/IR_ENCODE_BASEBAND_PRESCALE_FACTOR);
		if ( ir_raw_samples_count >= ir_raw_data_buffer_size ) // Buffer overflowed?
		{
			M1_LOG_E(M1_LOGDB_TAG, "Overflowed samples buffer -> Samples: %d\r\n", ir_raw_samples_count);
			break;
		} // if ( ir_raw_samples_count >= ir_raw_data_buffer_size )
		if ( last_number )
		{
			error_code = 0;
			break;
		}
	} // while ( true )

	return error_code;
} // static uint8_t ir_remote_file_raw_data_parse(char *instr)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void ir_remote_file_deinit(void)
{
	if (sdcard_dat_buffer!=NULL)
	{
		sdcard_dat_buffer -= M1_SDM_MIN_BUFFER_SIZE/2; // Restore original ptr
		free(sdcard_dat_buffer);
		sdcard_dat_buffer = NULL;
	}
	if ( ir_raw_data_buffer != NULL )
	{
		free(ir_raw_data_buffer);
		ir_raw_data_buffer = NULL;
	}

	m1_fb_close_file(&datfile_info.dat_file_hdl);

} // void ir_remote_file_deinit(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval 1 if End of File has been reached.
  */
/*============================================================================*/
uint8_t ir_datfile_end_of_file_check(void)
{
	return ( sdcard_dat_file_size==0 );
} // uint8_t ir_datfile_end_of_file_check(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint16_t ir_remote_dev_func_counter_get(uint8_t function_type)
{
	if ( function_type < IR_REMOTE_FUNC_LIST_MAX )
		return ir_remote_dev_func_counter[function_type];

	return 0;
} // uint16_t ir_remote_dev_func_counter_get(uint8_t function_type)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload)
{
	if ( payload==NULL )
		return 1;

	payload->address = ir_payload_value_address;
	payload->command = ir_payload_value_command;
	payload->frequency = ir_payload_value_frequency;
	payload->duty_cycle = ir_payload_value_duty_cycle;
	payload->rawdata_counter = ir_raw_samples_count;
	payload->pprotocol = ir_payload_value_protocol_p;
	payload->ir_data_type = ir_data_type;
	payload->pprawdata = ir_raw_data_buffer;

	return 0;
} // uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload)



/*============================================================================*/
/**
  * @brief Reverse bits order of a 32-bit value, i.e. MSB bit <--> LSB bit
  * @param
  * @retval
  */
/*============================================================================*/
static uint32_t ir_bits_reverse32 (uint32_t in_val, uint8_t len)
{
    uint32_t out_val = 0;

    while ( len-- )
    {
    	out_val <<= 1;
    	if (in_val & 0x01)
        {
            out_val |= 0x01;
        }
        in_val >>= 1;
    } // while ( len-- )

    return out_val;
} // static uint32_t ir_bits_reverse32 (uint32_t in_val, uint8_t len)
