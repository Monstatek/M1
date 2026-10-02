/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    m1_isr_drop_counters.h
 * @brief   Purely additive diagnostic counters for main_q_hdl sends that an
 *          ISR could not deliver (xQueueSendFromISR() returned something
 *          other than pdPASS -- the queue was full).
 *
 * Every xQueueSendFromISR(main_q_hdl, ...) call site in m1_int_hdl.c
 * previously ignored its return value entirely, so a dropped IR/Sub-GHz
 * sample was silent and unobservable. These counters change nothing about
 * event delivery semantics (a drop is still a drop -- nothing is retried,
 * queued elsewhere, or blocks the ISR); they only make drops visible via
 * the existing "m1_mtest 5" debug telemetry command.
 *
 * One counter per call site (not one shared counter): each is written from
 * exactly one place, so a plain aligned uint32_t increment is safe without
 * disabling interrupts or a real atomic RMW -- no context ever needs to
 * race itself, and a naturally-aligned 32-bit load/store is a single bus
 * transaction on this MCU, so the m1_mtest 5 task-context reader can't see
 * a torn value either. Each counter saturates at UINT32_MAX instead of
 * wrapping silently.
 */
/*============================================================================*/
#ifndef M1_ISR_DROP_COUNTERS_H_
#define M1_ISR_DROP_COUNTERS_H_

#include <stdint.h>

/* Call only from the matching ISR call site in m1_int_hdl.c, only when
 * xQueueSendFromISR(main_q_hdl, ...) did not return pdPASS. */
void m1_isr_drop_note_ir_tx(void);
void m1_isr_drop_note_ir_rx_start(void);
void m1_isr_drop_note_ir_rx_edge(void);
void m1_isr_drop_note_subghz_tx(void);
void m1_isr_drop_note_subghz_rx(void);

/* Task-context reads (m1_mtest 5). Safe at any time -- see file header. */
uint32_t m1_isr_drop_get_ir_tx(void);
uint32_t m1_isr_drop_get_ir_rx_start(void);
uint32_t m1_isr_drop_get_ir_rx_edge(void);
uint32_t m1_isr_drop_get_subghz_tx(void);
uint32_t m1_isr_drop_get_subghz_rx(void);

#endif /* M1_ISR_DROP_COUNTERS_H_ */
