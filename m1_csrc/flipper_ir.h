/* See COPYING.txt for license details. */

/*
 * flipper_ir.h
 *
 * Flipper Zero .ir file format parser for IR signals
 *
 * M1 Project
 */

#ifndef FLIPPER_IR_H_
#define FLIPPER_IR_H_

#include "flipper_file.h"
#include "irmp.h"

#define FLIPPER_IR_RAW_MAX_SAMPLES  512
#define FLIPPER_IR_NAME_MAX_LEN     32

typedef enum {
	FLIPPER_IR_SIGNAL_PARSED = 0,
	FLIPPER_IR_SIGNAL_RAW
} flipper_ir_signal_type_t;

typedef struct {
	char name[FLIPPER_IR_NAME_MAX_LEN];
	flipper_ir_signal_type_t type;
	bool valid;
	union {
		struct {
			uint8_t  protocol;     /* IRMP protocol ID */
			uint16_t address;
			uint16_t command;
			uint8_t  flags;
		} parsed;
		struct {
			uint32_t frequency;    /* Hz, e.g. 38000 */
			float    duty_cycle;   /* e.g. 0.33 */
			int32_t  samples[FLIPPER_IR_RAW_MAX_SAMPLES];
			uint16_t sample_count;
		} raw;
	};
} flipper_ir_signal_t;

/* Open a .ir file and validate header */
bool flipper_ir_open(flipper_file_t *ctx, const char *path);

/* Read next signal from an open .ir file. Returns false at EOF */
bool flipper_ir_read_signal(flipper_file_t *ctx, flipper_ir_signal_t *out);

/* Write a .ir file header */
bool flipper_ir_write_header(flipper_file_t *ctx);

/* Write a signal to .ir file */
bool flipper_ir_write_signal(flipper_file_t *ctx, const flipper_ir_signal_t *sig);

/* Map Flipper protocol name string to IRMP protocol ID */
uint8_t flipper_ir_proto_to_irmp(const char *name);

/* Map IRMP protocol ID to Flipper protocol name string */
const char *flipper_ir_irmp_to_proto(uint8_t irmp_id);

/* Count signals in a .ir file without loading them all */
uint16_t flipper_ir_count_signals(const char *path);

#endif /* FLIPPER_IR_H_ */
