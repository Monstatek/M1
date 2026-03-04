/* See COPYING.txt for license details. */

/*
*
* m1_ir_universal.h
*
* Universal Remote with Flipper IRDB support
*
* M1 Project
*
*/

#ifndef M1_IR_UNIVERSAL_H_
#define M1_IR_UNIVERSAL_H_

#include <stdint.h>
#include <stdbool.h>
#include "irmp.h"

#define IR_UNIVERSAL_NAME_MAX_LEN    32
#define IR_UNIVERSAL_MAX_CMDS        64
#define IR_UNIVERSAL_BROWSE_PAGE_SIZE 6
#define IR_UNIVERSAL_MAX_FAVORITES   10
#define IR_UNIVERSAL_MAX_RECENT      10
#define IR_UNIVERSAL_PATH_MAX_LEN    256
#define IR_UNIVERSAL_IRDB_ROOT       "0:/IR"

typedef enum {
	IR_UNIVERSAL_MODE_DASHBOARD = 0,
	IR_UNIVERSAL_MODE_BROWSE_CATEGORY,
	IR_UNIVERSAL_MODE_BROWSE_BRAND,
	IR_UNIVERSAL_MODE_BROWSE_DEVICE,
	IR_UNIVERSAL_MODE_COMMANDS,
	IR_UNIVERSAL_MODE_SEARCH,
	IR_UNIVERSAL_MODE_FAVORITES,
	IR_UNIVERSAL_MODE_RECENT
} ir_universal_mode_t;

typedef struct {
	char name[IR_UNIVERSAL_NAME_MAX_LEN];
	uint8_t  protocol;     /* IRMP protocol ID */
	uint16_t address;
	uint16_t command;
	uint8_t  flags;
	bool     is_raw;
	uint32_t raw_freq;     /* For raw signals */
	uint16_t raw_count;
	bool     valid;
} ir_universal_cmd_t;

/* Main entry point - called from m1_infrared.c */
void ir_universal_run(void);

/* Initialize/cleanup */
void ir_universal_init(void);
void ir_universal_deinit(void);

#endif /* M1_IR_UNIVERSAL_H_ */
