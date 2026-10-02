/*
 * m1_isr_drop_counters_test.c - host tests for the main_q_hdl ISR-send drop
 * counters (m1_isr_drop_counters.c).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I m1_csrc \
 *      m1_csrc/m1_isr_drop_counters.c m1_csrc/test/m1_isr_drop_counters_test.c \
 *      -o /tmp/isr_drop_test && /tmp/isr_drop_test
 */
#include "m1_isr_drop_counters.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s\n", (m)); } } while (0)

int main(void)
{
    CHECK(m1_isr_drop_get_ir_tx() == 0, "ir_tx starts at 0");
    CHECK(m1_isr_drop_get_ir_rx_start() == 0, "ir_rx_start starts at 0");
    CHECK(m1_isr_drop_get_ir_rx_edge() == 0, "ir_rx_edge starts at 0");
    CHECK(m1_isr_drop_get_subghz_tx() == 0, "subghz_tx starts at 0");
    CHECK(m1_isr_drop_get_subghz_rx() == 0, "subghz_rx starts at 0");

    m1_isr_drop_note_ir_tx();
    m1_isr_drop_note_ir_tx();
    m1_isr_drop_note_ir_tx();
    CHECK(m1_isr_drop_get_ir_tx() == 3, "ir_tx increments correctly");
    CHECK(m1_isr_drop_get_ir_rx_start() == 0, "noting ir_tx doesn't touch other counters");
    CHECK(m1_isr_drop_get_ir_rx_edge() == 0, "noting ir_tx doesn't touch other counters (2)");
    CHECK(m1_isr_drop_get_subghz_tx() == 0, "noting ir_tx doesn't touch other counters (3)");
    CHECK(m1_isr_drop_get_subghz_rx() == 0, "noting ir_tx doesn't touch other counters (4)");

    m1_isr_drop_note_ir_rx_start();
    m1_isr_drop_note_ir_rx_edge();
    m1_isr_drop_note_ir_rx_edge();
    m1_isr_drop_note_subghz_tx();
    m1_isr_drop_note_subghz_rx();
    m1_isr_drop_note_subghz_rx();
    m1_isr_drop_note_subghz_rx();
    CHECK(m1_isr_drop_get_ir_rx_start() == 1, "ir_rx_start counts independently");
    CHECK(m1_isr_drop_get_ir_rx_edge() == 2, "ir_rx_edge counts independently");
    CHECK(m1_isr_drop_get_subghz_tx() == 1, "subghz_tx counts independently");
    CHECK(m1_isr_drop_get_subghz_rx() == 3, "subghz_rx counts independently");

    printf("m1_isr_drop_counters_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
