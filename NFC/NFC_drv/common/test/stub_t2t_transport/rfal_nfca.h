/* Host-test stub of rfal_nfca.h -- see rfal_rf.h in this directory for the
 * stubbing rationale. Only rfalNfcaListenerIsSleepReq() is used by
 * m1_t2t_transport.c. Its mock implementation (rfal_mock.c) reproduces the
 * REAL function's logic exactly (2-byte HLTA payload, CRC already
 * stripped -- verified against the real rfal_nfca.c while designing the
 * production code), so this stub is behaviorally faithful, not a shortcut.
 *
 * The real rfal_nfca.h #includes "rfal_platform.h", which is where
 * platformTimerCreate()/platformTimerIsExpired() (macros over
 * timerCalculateTimer()/timerIsExpired()) come from -- m1_t2t_transport.c's
 * reactivation watchdog uses those same macro names so its source is
 * byte-identical between production and this host test. The mock versions
 * of the two underlying functions (rfal_mock.c) use a test-controllable
 * tick (mock_set_tick()/mock_advance_tick()) instead of HAL_GetTick(), so
 * a test can deterministically arm/expire the watchdog without a real
 * wall-clock wait. */
#ifndef RFAL_NFCA_STUB_H_
#define RFAL_NFCA_STUB_H_

#include <stdint.h>
#include <stdbool.h>

bool rfalNfcaListenerIsSleepReq(const uint8_t *buf, uint16_t bufLen);

uint32_t timerCalculateTimer(uint16_t time);
bool     timerIsExpired(uint32_t timer);
#define platformTimerCreate(t)      timerCalculateTimer(t)
#define platformTimerIsExpired(t)   timerIsExpired(t)

#endif
