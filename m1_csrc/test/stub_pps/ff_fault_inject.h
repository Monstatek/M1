/* Fault-injection controls for the POSIX-backed ff.h stub (ff_stub.c).
 * The test file sets these globals before calling into the real
 * privateprofilestring.c / m1_file_util.c to exercise their failure paths
 * without needing a real removable card. All flags default to 0 (no
 * injected fault, normal POSIX behavior). */
#ifndef STUB_FF_FAULT_INJECT_H_
#define STUB_FF_FAULT_INJECT_H_

extern int g_ff_fail_open_write;  /* f_open() fails whenever mode has FA_WRITE */
extern int g_ff_fail_sync;        /* f_sync() fails */
extern int g_ff_fail_close;       /* f_close() reports failure (fd is still
                                    * really closed underneath, so no fd leak
                                    * in the test process) */
extern int g_ff_fail_rename;      /* f_rename() fails and does NOT perform
                                    * the rename */
extern int g_ff_fail_unlink;      /* f_unlink() fails and does NOT remove
                                    * the file */
extern int g_ff_card_removed;     /* simulates media removal: every FatFs
                                    * call in this stub fails immediately,
                                    * as if the card vanished mid-operation */
extern int g_ff_card_removed_after_call; /* 0 = disabled. When >0, the stub
                                    * counts every FatFs entry point call
                                    * and sets g_ff_card_removed itself once
                                    * that many calls have completed -- lets
                                    * a test place the card's disappearance
                                    * at a precise point deep inside a
                                    * multi-call operation, rather than only
                                    * before it starts. */
extern int g_ff_call_count;       /* total FatFs entry-point calls made
                                    * since the last ff_fault_inject_reset() */

void ff_fault_inject_reset(void);

#endif /* STUB_FF_FAULT_INJECT_H_ */
