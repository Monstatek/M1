/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    m1_isr_drop_counters.c
 * @brief   See m1_isr_drop_counters.h.
 */
/*============================================================================*/
#include "m1_isr_drop_counters.h"

static volatile uint32_t s_drop_ir_tx = 0;
static volatile uint32_t s_drop_ir_rx_start = 0;
static volatile uint32_t s_drop_ir_rx_edge = 0;
static volatile uint32_t s_drop_subghz_tx = 0;
static volatile uint32_t s_drop_subghz_rx = 0;

#define M1_ISR_DROP_NOTE(counter) do { if ((counter) < UINT32_MAX) { (counter)++; } } while (0)

void m1_isr_drop_note_ir_tx(void)       { M1_ISR_DROP_NOTE(s_drop_ir_tx); }
void m1_isr_drop_note_ir_rx_start(void) { M1_ISR_DROP_NOTE(s_drop_ir_rx_start); }
void m1_isr_drop_note_ir_rx_edge(void)  { M1_ISR_DROP_NOTE(s_drop_ir_rx_edge); }
void m1_isr_drop_note_subghz_tx(void)   { M1_ISR_DROP_NOTE(s_drop_subghz_tx); }
void m1_isr_drop_note_subghz_rx(void)   { M1_ISR_DROP_NOTE(s_drop_subghz_rx); }

uint32_t m1_isr_drop_get_ir_tx(void)       { return s_drop_ir_tx; }
uint32_t m1_isr_drop_get_ir_rx_start(void) { return s_drop_ir_rx_start; }
uint32_t m1_isr_drop_get_ir_rx_edge(void)  { return s_drop_ir_rx_edge; }
uint32_t m1_isr_drop_get_subghz_tx(void)   { return s_drop_subghz_tx; }
uint32_t m1_isr_drop_get_subghz_rx(void)   { return s_drop_subghz_rx; }
