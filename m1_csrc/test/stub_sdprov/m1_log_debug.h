/* Host-test stub for m1_log_debug.h, used only to host-compile the REAL,
 * unmodified m1_sdcard_provision.c. Routes M1_LOG_E/M1_LOG_I to the test's
 * own capture function instead of the real firmware logger, so a test can
 * assert exactly one log line was produced per failing path -- see
 * m1_sdcard_provision_test.c. */
#ifndef M1_LOG_DEBUG_H_
#define M1_LOG_DEBUG_H_

void test_sdprov_log_capture(const char *tag, const char *format, ...);

#define M1_LOG_E(tag, format, ...) test_sdprov_log_capture(tag, format, ##__VA_ARGS__)
#define M1_LOG_I(tag, format, ...) test_sdprov_log_capture(tag, format, ##__VA_ARGS__)

#endif /* M1_LOG_DEBUG_H_ */
