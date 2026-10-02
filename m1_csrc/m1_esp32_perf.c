/* See COPYING.txt for license details. */

/*
*
* m1_esp32_perf.c
*
* See m1_esp32_perf.h for the design rationale.
*
* M1 Project
*
*/

#include "m1_esp32_perf.h"

static uint32_t s_commands;
static uint32_t s_fixed_delay_ms;
static uint32_t s_write_attempts;
static uint32_t s_write_timeouts;
static uint32_t s_connects;

void m1_esp32_perf_reset(void)
{
    s_commands = 0U;
    s_fixed_delay_ms = 0U;
    s_write_attempts = 0U;
    s_write_timeouts = 0U;
    s_connects = 0U;
}

void m1_esp32_perf_note_command(void)              { s_commands++; }
void m1_esp32_perf_note_fixed_delay(uint32_t ms)    { s_fixed_delay_ms += ms; }
void m1_esp32_perf_note_write_attempt(int timed_out)
{
    s_write_attempts++;
    if (timed_out) { s_write_timeouts++; }
}
void m1_esp32_perf_note_connect(void)               { s_connects++; }

uint32_t m1_esp32_perf_commands(void)         { return s_commands; }
uint32_t m1_esp32_perf_fixed_delay_ms(void)   { return s_fixed_delay_ms; }
uint32_t m1_esp32_perf_write_attempts(void)   { return s_write_attempts; }
uint32_t m1_esp32_perf_write_timeouts(void)   { return s_write_timeouts; }
uint32_t m1_esp32_perf_connects(void)         { return s_connects; }
