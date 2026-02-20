/* See COPYING.txt for license details. */

/*
 * T5577 LF RFID implementation
 *
 * This file is derived from the Flipper Zero firmware project.
 * The original implementation has been modified to support
 * Monstatek hardware by adapting the hardware abstraction layer.
 *
 * Original project:
 * https://github.com/flipperdevices/flipperzero-firmware
 *
 * Copyright (C) Flipper Devices Inc.
 *
 * Licensed under the GNU General Public License v3.0 (GPLv3).
 *
 * Modifications:
 *   - Hardware interface adaptation for Monstatek platform
 *   - Integration into Monstatek firmware framework
 *
 * Copyright (C) 2026 Monstatek
 */
#ifndef T5577_H_
#define T5577_H_

#ifdef __cplusplus
extern "C" {
#endif

#define T5577_BLOCK_COUNT 8
#define T5577_BLOCKS_IN_PAGE_0   8
#define T5577_BLOCKS_IN_PAGE_1   4

// T5577 block 0 definitions
#define T5577_POR_DELAY         (1<<0)
#define T5577_INVERSE_DATA		(1<<1)
#define T5577_FAST_WRITE		(1<<2)
#define T5577_SST_TERM			(1<<3)
#define T5577_PWD				(1<<4)

#define T5577_MAXBLOCK_SHIFT    5
#define T5577_TRANS_BL_0		(0<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1		(1<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_2		(2<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_3		(3<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_4		(4<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_5		(5<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_6		(6<<T5577_MAXBLOCK_SHIFT)
#define T5577_TRANS_BL_1_7		(7<<T5577_MAXBLOCK_SHIFT)

#define T5577_OTP               (1<<8)
#define T5577_AOR               (1<<9)

#define T5577_PSKCF_SHIFT    	10
#define T5577_PSKCF_RF_2        (0<<T5577_PSKCF_SHIFT)
#define T5577_PSKCF_RF_4        (1<<T5577_PSKCF_SHIFT)
#define T5577_PSKCF_RF_8        (2<<T5577_PSKCF_SHIFT)

#define T5577_MOD_SHIFT    		12
#define T5577_MOD_DIRECT     	(0<<T5577_MOD_SHIFT)
#define T5577_MOD_PSK1       	(1<<T5577_MOD_SHIFT)
#define T5577_MOD_PSK2       	(2<<T5577_MOD_SHIFT)
#define T5577_MOD_PSK3       	(3<<T5577_MOD_SHIFT)
#define T5577_MOD_FSK1       	(4<<T5577_MOD_SHIFT)
#define T5577_MOD_FSK2       	(5<<T5577_MOD_SHIFT)
#define T5577_MOD_FSK1a      	(6<<T5577_MOD_SHIFT)
#define T5577_MOD_FSK2a      	(7<<T5577_MOD_SHIFT)
#define T5577_MOD_MANCHESTER 	(8<<T5577_MOD_SHIFT)
#define T5577_MOD_BIPHASE    	(9<<T5577_MOD_SHIFT)

#define T5577_X_MODE            (1<<17)

#define T5577_BITRATE_SHIFT    	18
#define T5577_BITRATE_RF_8      (0<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_16     (1<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_32     (2<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_40     (3<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_50     (4<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_64     (5<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_100    (6<<T5577_BITRATE_SHIFT)
#define T5577_BITRATE_RF_128    (7<<T5577_BITRATE_SHIFT)

typedef struct {
    uint16_t ctrl;
    uint16_t time;
} LFRFIDT5577WriteCtrl;

typedef struct {
    uint32_t block_data[T5577_BLOCK_COUNT];
    uint32_t max_blocks;
    LFRFIDT5577WriteCtrl write_frame[85];
} LFRFIDT5577;

typedef enum {
    LFRFIDProgramTypeT5577,
} LFRFIDProgramType;

typedef struct {
    LFRFIDProgramType type;
    union {
        LFRFIDT5577 t5577;
    };
} LFRFIDProgram;

/**
 * @brief Write T5577 tag data to tag
 * 
 * @param data 
 */
void t5577_execute_write(LFRFIDProgram* data, int block);

#ifdef __cplusplus
}
#endif
#endif /* T5577_H_ */
