/* See COPYING.txt for license details. */

/*
 * flipper_subghz.h
 *
 * Flipper Zero .sub file format parser for Sub-GHz signals
 *
 * M1 Project
 */

#ifndef FLIPPER_SUBGHZ_H_
#define FLIPPER_SUBGHZ_H_

#include "flipper_file.h"

#define FLIPPER_SUBGHZ_RAW_MAX_SAMPLES   2048
#define FLIPPER_SUBGHZ_PROTO_MAX_LEN     32
#define FLIPPER_SUBGHZ_PRESET_MAX_LEN    48

typedef enum {
	FLIPPER_SUBGHZ_TYPE_RAW = 0,
	FLIPPER_SUBGHZ_TYPE_PARSED
} flipper_subghz_type_t;

typedef struct {
	flipper_subghz_type_t type;
	uint32_t frequency;                 /* Hz, e.g. 433920000 */
	char     preset[FLIPPER_SUBGHZ_PRESET_MAX_LEN];
	char     protocol[FLIPPER_SUBGHZ_PROTO_MAX_LEN];
	/* For parsed protocols */
	uint32_t bit_count;
	uint64_t key;
	uint32_t te;                        /* Timing element */
	/* For raw data */
	int16_t  raw_data[FLIPPER_SUBGHZ_RAW_MAX_SAMPLES];
	uint16_t raw_count;
} flipper_subghz_signal_t;

/* Load a .sub file */
bool flipper_subghz_load(const char *path, flipper_subghz_signal_t *out);

/* Save a .sub file */
bool flipper_subghz_save(const char *path, const flipper_subghz_signal_t *sig);

/* Map Flipper preset name to M1 modulation type */
uint8_t flipper_subghz_preset_to_modulation(const char *preset);

/* Convert frequency in Hz to closest M1 SI4463 band */
uint8_t flipper_subghz_freq_to_band(uint32_t freq_hz);

#endif /* FLIPPER_SUBGHZ_H_ */
