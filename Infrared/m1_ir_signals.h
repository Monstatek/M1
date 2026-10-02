/* See COPYING.txt for license details. */
/*
*
* m1_ir_signals.h
*
* Header for infrared signals
*
* M1 Project
*
*/

#ifndef M1_IR_SIGNALS_H_
#define M1_IR_SIGNALS_H_

#define IR_REMOTE_TYPE_TVS_FILENAME			"0://infrared/db/tv.ir"
#define IR_REMOTE_TYPE_AUDIOS_FILENAME		"0://infrared/db/audio.ir"
#define IR_REMOTE_TYPE_PROJECTORS_FILENAME	"0://infrared/db/projector.ir"
#define IR_REMOTE_TYPE_ACS_FILENAME			"0://infrared/db/ac.ir"

#define IR_REMOTETYPE_ALL_DEVICES_NONE		0xFF // Undefined function for all IR devices

#define IR_REMOTE_PAUSE_TIME_BETWEEN_FRAME	100 //ms

typedef enum
{
	IR_ERROR_CODE_NONE = 0,
	IR_ERROR_CODE_SDCARD,
	IR_ERROR_CODE_DATABASE,
	IR_ERROR_CODE_END_OF_FILE,
	IR_ERROR_CODE_EOL
} S_M1_IR_Transmit_Error_t;

typedef enum
{
	IR_REMOTETYPE_TV = 0,
	IR_REMOTETYPE_AUDIO,
	IR_REMOTETYPE_PROJECTOR,
	IR_REMOTETYPE_AC,
	IR_REMOTETYPE_UNKNOWN
} S_M1_IR_RemoteType_t;

typedef enum
{
	IR_REMOTETYPE_TV_POWER,
	IR_REMOTETYPE_TV_MUTE,
	IR_REMOTETYPE_TV_VOL_UP,
	IR_REMOTETYPE_TV_CH_NEXT,
	IR_REMOTETYPE_TV_VOL_DN,
	IR_REMOTETYPE_TV_CH_PREV,
	IR_REMOTETYPE_TV_EOL
} S_M1_IR_TV_Functions_t;

typedef enum
{
	IR_REMOTETYPE_AUDIO_POWER,
	IR_REMOTETYPE_AUDIO_MUTE,
	IR_REMOTETYPE_AUDIO_PLAY,
	IR_REMOTETYPE_AUDIO_PAUSE,
	IR_REMOTETYPE_AUDIO_VOL_UP,
	IR_REMOTETYPE_AUDIO_VOL_DN,
	IR_REMOTETYPE_AUDIO_NEXT,
	IR_REMOTETYPE_AUDIO_PREV,
	IR_REMOTETYPE_AUDIO_EOL
} S_M1_IR_Audio_Functions_t;

typedef enum
{
	IR_REMOTETYPE_PROJECTOR_POWER,
	IR_REMOTETYPE_PROJECTOR_MUTE,
	IR_REMOTETYPE_PROJECTOR_VOL_UP,
	IR_REMOTETYPE_PROJECTOR_VOL_DN,
	IR_REMOTETYPE_PROJECTOR_EOL
} S_M1_IR_Projector_Functions_t;

typedef enum
{
	IR_REMOTETYPE_AC_OFF,
	IR_REMOTETYPE_AC_DH,
	IR_REMOTETYPE_AC_COOL_HI,
	IR_REMOTETYPE_AC_COOL_LO,
	IR_REMOTETYPE_AC_HEAT_HI,
	IR_REMOTETYPE_AC_HEAT_LO,
	IR_REMOTETYPE_AC_EOL
} S_M1_IR_AC_Functions_t;

typedef enum
{
	IR_DATATYPE_UNKNOWN = 0,
	IR_DATATYPE_PARSED,
	IR_DATATYPE_RAW,
	IR_DATATYPE_EOL
} S_M1_IR_DataType_t;

typedef struct
{
	uint32_t address;
	uint32_t command;
	uint32_t frequency;
	uint32_t duty_cycle;
	uint16_t rawdata_counter;
	uint8_t ir_data_type;
	uint8_t *pprotocol;
	uint16_t *pprawdata;
} S_M1_IR_Payload_t;


uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type);
uint8_t ir_remote_file_data_check(uint8_t remote_type);
uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type);
void ir_remote_file_deinit(void);
uint8_t ir_datfile_end_of_file_check(void);
uint16_t ir_remote_dev_func_counter_get(uint8_t function_type);
uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload);

extern S_M1_IR_Payload_t ir_universal_payload;

#endif /* M1_IR_SIGNALS_H_ */
