/* Control API for rfal_mock.c -- see that file for the full explanation. */
#ifndef RFAL_MOCK_H_
#define RFAL_MOCK_H_

#include "rfal_rf.h"

void mock_reset(void);

/* Script the FIFO consumed by successive rfalListenGetState() calls; once
 * drained, the last-pushed value repeats ("sticky") so a test doesn't need
 * to push a value for every tick. */
void mock_push_lm_state(rfalLmState state, bool dataFlag);

/* Script the FIFO consumed by successive rfalGetTransceiveStatus() calls;
 * same sticky-repeat behavior once drained. */
void mock_push_transceive_status(ReturnCode status);

/* Force the NEXT rfalListenStart()/rfalStartTransceive() call to fail with
 * this error (auto-resets to RFAL_ERR_NONE after being consumed once). */
void mock_set_next_listen_start_err(ReturnCode err);
void mock_set_next_start_transceive_err(ReturnCode err);

/* Write bytes into whichever buffer is currently armed (the rxBuf most
 * recently supplied to rfalListenStart() or rfalStartTransceive()) and set
 * its length -- mirrors "hardware received a frame." Returns false if
 * nothing is currently armed to receive into. */
bool mock_inject_rx(const uint8_t *data, uint16_t lenBytes);

/* Test-controllable tick backing timerCalculateTimer()/timerIsExpired()
 * (the real functions behind platformTimerCreate()/platformTimerIsExpired(),
 * see rfal_nfca.h in this directory) -- lets a test arm/expire the
 * reactivation watchdog deterministically instead of waiting on a real
 * wall clock. Starts at 0, reset to 0 by mock_reset(). */
void     mock_set_tick(uint32_t ms);
void     mock_advance_tick(uint32_t ms);
uint32_t mock_get_tick(void);

int mock_listen_start_count(void);
int mock_listen_stop_count(void);
int mock_listen_sleep_count(void);
int mock_start_transceive_count(void);
int mock_rearm_count(void);     /* rfalStartTransceive() calls with txBuf==NULL */
int mock_tx_count(void);        /* rfalStartTransceive() calls with txBuf!=NULL (a real response) */
int mock_worker_count(void);

uint16_t       mock_last_tx_len_bits(void);
const uint8_t *mock_last_tx_buf(void);
const uint8_t *mock_last_listen_start_uid(void);
uint8_t        mock_last_listen_start_uid_len(void);
const uint8_t *mock_last_listen_start_atqa(void);
uint8_t        mock_last_listen_start_sak(void);

#endif
