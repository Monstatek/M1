/* See COPYING.txt for license details. */

/*
 * flipper_rfid.h
 *
 * Flipper Zero .rfid file format parser
 *
 * M1 Project
 */

#ifndef FLIPPER_RFID_H_
#define FLIPPER_RFID_H_

#include "flipper_file.h"
#include "lfrfid.h"

typedef struct {
	LFRFIDProtocol protocol;
	uint8_t  data[8];    /* Raw protocol data */
	uint8_t  data_len;
	char     protocol_name[32];
} flipper_rfid_tag_t;

/* Load a .rfid file */
bool flipper_rfid_load(const char *path, flipper_rfid_tag_t *out);

/* Save a .rfid file */
bool flipper_rfid_save(const char *path, const flipper_rfid_tag_t *tag);

/* Map Flipper RFID protocol name to M1 LFRFIDProtocol enum */
LFRFIDProtocol flipper_rfid_parse_protocol(const char *name);

#endif /* FLIPPER_RFID_H_ */
