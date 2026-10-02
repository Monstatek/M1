/* See COPYING.txt for license details. */

/*
*
* m1_fault_report.h
*
* Best-effort on-screen Cortex-M fault reporter. Called from the fault handlers
* to render the captured fault status (CFSR/HFSR/MMFAR/BFAR + stacked PC/LR) and
* the MonstaShark capture checkpoint to the display, so a crash is diagnosable
* on the device instead of appearing as a freeze.
*
* M1 Project
*
*/

#ifndef M1_FAULT_REPORT_H_
#define M1_FAULT_REPORT_H_

#include <stdint.h>

/* MonstaShark capture-soak checkpoint. Updated as the soak progresses so a fault
 * screen shows the last step reached. 0 = idle. See m1_capture_link.c for the
 * value map. */
extern volatile uint32_t g_cap_checkpoint;

/* Render the captured fault registers + checkpoint to the display. Safe to call
 * from fault context (blocking SPI; no RTOS). `tag` labels the fault type. */
void m1_fault_report(const char *tag);

#endif /* M1_FAULT_REPORT_H_ */
