/* See COPYING.txt for license details. */

/*
 * m1_subghz_scan_draw.h
 *
 * Shared, hardware-independent drawing code for the active Sub-GHz Scan screen
 * (the "RF lock-on" design). U8g2-only (no STM32 HAL / radio) so it can be
 * compiled into both the M1 firmware and a host preview harness.
 *
 * Layout: preserved M1 frequency + modulation text, a "SCANNING..." label, a
 * centered concentric-circle crosshair (RF lock-on) target with RF arcs on both
 * sides, and the existing M1 target-icon + "Stop" option bar. 128x64, 1-bit.
 *
 * M1 Project
 */

#ifndef M1_SUBGHZ_SCAN_DRAW_H
#define M1_SUBGHZ_SCAN_DRAW_H

#include <stdint.h>
#include "u8g2.h"

/* Draw the whole 128x64 active Scan screen. band/mod are the existing M1
 * strings (e.g. "433.920", "OOK"). Caller flushes (m1_u8g2_nextpage). */
void subghz_scan_draw(u8g2_t *u8g2, const char *band, const char *mod);

#endif /* M1_SUBGHZ_SCAN_DRAW_H */
