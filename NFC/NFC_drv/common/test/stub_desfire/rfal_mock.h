/* Programmable mock backing rfal_nfc.h's data-exchange functions -- lets a
 * test script the exact sequence of DESFire responses a real card would
 * send, then verify both the decoded result AND the exact bytes m1_desfire.c
 * transmitted for each command. Not used by the firmware build -- host
 * analysis only. */
#ifndef RFAL_MOCK_H_
#define RFAL_MOCK_H_

#include <stdint.h>

void mock_reset(void);

/* Queue one response frame (status byte + payload) to be returned by the
 * NEXT rfalNfcDataExchangeStart call, in FIFO order. If more calls happen
 * than responses were queued, the mock returns RFAL_ERR_TIMEOUT so a test
 * with an incomplete script fails loudly instead of hanging/fabricating
 * data. */
void mock_queue_response(const uint8_t *bytes, uint16_t len);

/* How many rfalNfcDataExchangeStart calls have happened since mock_reset(). */
int mock_tx_call_count(void);

/* Copies the exact bytes transmitted on the Nth call (0-indexed) into out
 * (up to out_cap bytes); returns the TRUE transmitted length (which may
 * exceed out_cap -- callers should size out_cap generously and compare the
 * return value against the expected length explicitly). Returns 0 if index
 * is out of range. */
uint16_t mock_get_tx_call(int index, uint8_t *out, uint16_t out_cap);

#endif
