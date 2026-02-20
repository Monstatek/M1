/* See COPYING.txt for license details. */

/*
 * lfrfid.h
 *
 *      Author: thomas
 */

#ifndef LFRFID_H_
#define LFRFID_H_

#include <math.h>   
#include <stdlib.h>  
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "stream_buffer.h"
#include "lfrfid_hal.h"
#include "t5577.h"

extern QueueHandle_t	lfrfid_q_hdl;
extern StreamBufferHandle_t lfrfid_sb_hdl;


#define LFR_BATCH_ITEMS   60	//60	// 10
#define LFR_ITEM_SIZE     sizeof(lfrfid_evt_t)
#define LFR_TRIGGER_BYTES (LFR_BATCH_ITEMS * LFR_ITEM_SIZE)   
#define LFR_RX_TIMEOUT    portMAX_DELAY //pdMS_TO_TICKS(50)//pdMS_TO_TICKS(10)                   // 10 ms

#define LFR_SBUF_BYTES 	  (LFR_TRIGGER_BYTES<<1)	//1024

#define FRAME_CHUNK_SIZE (LFR_BATCH_ITEMS)

typedef enum {
	RFID_IDLE = 0,
	RFID_READ_READING,
	RFID_READ_DONE,
	//ID_RECORD_REPLAY,
	RFID_UNKNOWN
} S_M1_RFID_Record_t;
#if 0
typedef enum
{
  RFIN_PREAMBLE = 0,
  RFIN_RISING_EDGE,
  RFIN_FALLING_EDGE,
  RFIN_FINISH,
  RFIN_DONE
}RfidReadTypeDef;
#endif

#if 1
#ifndef max
 #define max(a,b)        (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
 #define min(a,b)        (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAKEWORD
 #define MAKEWORD(a, b)  ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#endif

#ifndef MAKELONG
 #define MAKELONG(a, b)  ((DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#endif

#ifndef LOWORD
 #define LOWORD(l)       ((WORD)(l))
#endif

#ifndef HIWORD
 #define HIWORD(l)       ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#endif

#ifndef LOBYTE
 #define LOBYTE(w)       ((BYTE)(w))
#endif

#ifndef HIBYTE
 #define HIBYTE(w)       ((BYTE)(((WORD)(w) >> 8) & 0xFF))
#endif

#ifndef HINIBBLE
#define HINIBBLE(b) 	(((b)&0xF0) >> 4)
#endif

#ifndef LONIBBLE
#define LONIBBLE(b) 	((b)&0x0F)
#endif

#ifndef MAKEBYTE
#define MAKEBYTE(a,b)	((BYTE)(a<<4) | ((BYTE)(b) & 0x0F))
#endif
#endif

typedef enum {
    LFRFID_EDGE_UNKNOWN = 0,
    LFRFID_EDGE_RISE    = 1,
    LFRFID_EDGE_FALL    = 2,
} lfrfid_edge_t;

typedef struct {
    uint16_t t_us;     
    uint16_t  edge;    
    //uint8_t  level;   
} lfrfid_evt_t;

typedef struct
{
	uint8_t 	uid[5];
	uint8_t     protocol;
	uint16_t	bitrate;
	uint8_t		modulation;
	uint8_t		encoding;
	uint8_t		card_format;
	uint8_t     status;
	char	filename[32];
	char    filepath[128];
} LFRFID_TAG_INFO, *PLFRFID_TAG_INFO;

typedef enum {
    BIT_ORDER_MSB_FIRST,
    BIT_ORDER_LSB_FIRST,
} BitOrder;

typedef enum {
    LFRFID_STATE_IDLE,
    LFRFID_STATE_READ,
    LFRFID_STATE_WRITE,
    LFRFID_STATE_EMULATE,
    LFRFID_STATE_ERROR
} lfrfid_state_t;

#define LFRFID_WRITE_ERROR_COUNT	(10)

extern lfrfid_state_t lfrfid_state;

extern LFRFID_TAG_INFO lfrfid_tag_info;
extern LFRFID_TAG_INFO *lfrfid_tag_info_back;
extern LFRFIDProgram *lfrfid_program;
extern uint32_t lfrfid_write_count;

void safe_free(void **pp);

//void lfrfidThread(void *param);
void lfrfid_Init(void);
void lfrfid_DeInit(void);

void bytes_to_u32_array(BitOrder order, const uint8_t in_data[], uint32_t out_data[], int size);

#include "lfrfid_protocol.h"
//#include "lfrfid_protocol_detect.h"
#include "lfrfid_protocol_em4100.h"
#include "lfrfid_protocol_h10301.h"

#endif /* LFRFID_H_ */
