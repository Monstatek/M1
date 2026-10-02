/* See COPYING.txt for license details. */

/*
 * m1_read_icon.h
 *
 * Shared, hardware-independent drawing code for the NFC and RFID
 * read/waiting screens.
 *
 * Contains ONLY U8g2 drawing logic (no STM32 HAL, no FreeRTOS, no radio/NFC
 * access) so it can be compiled into both the M1 firmware and a host preview
 * harness that renders exact 128x64 1-bit frames from the same code.
 *
 * The screen is a centered composition: a horizontal rounded "tag" with a bold
 * letter and a small chip dot, concentric signal waves on both sides, and the
 * caption "Hold Tag to M1" below. No top/bottom bar, no battery icon,
 * no extra labels.
 *
 * M1 Project
 */

#ifndef M1_READ_ICON_H
#define M1_READ_ICON_H

#include <stdint.h>
#include "u8g2.h"

/* Emulate-screen animation cadence (ms per frame). */
#define M1_READ_ICON_EMU_MS   250

/* Shared BOLD caption font for the Read / Emulate / Write status text.
 * One definition, referenced by m1_read_icon.c (Read+Emulate) and
 * m1_display.c (Write), so all three stay identical. */
#define M1_READ_ICON_CAPTION_FONT  u8g2_font_helvB08_tf

/*
 * Draw the whole 128x64 read/waiting screen.
 *   letter  : glyph shown inside the tag ('N' for NFC, 'R' for RFID).
 *   n_waves : number of concentric signal arcs on EACH side.
 * The caller owns page begin/flush (u8g2_FirstPage / m1_u8g2_nextpage).
 */
void m1_read_icon_draw(u8g2_t *u8g2, char letter, uint8_t n_waves);

/*
 * Draw the shared NFC/RFID emulate screen (same tag, NO letter, chip dot) with
 * the caption "EMULATING...". Only the RF waves animate: `frame` (0,1,2,...)
 * cycles the wave count outward and loops, so callers just pass an
 * ever-incrementing counter advanced ~every M1_READ_ICON_EMU_MS.
 */
void m1_read_icon_draw_emulate(u8g2_t *u8g2, uint8_t frame);

/*
 * Draw ONLY the shared rounded tag body + chip dot (no letter, no waves,
 * no caption), so other screens (e.g. the Write screen) can render a tag
 * pixel-identical to the Read/Emulate tag. Caller owns page begin/flush.
 */
void m1_read_icon_draw_tag(u8g2_t *u8g2);

#endif /* M1_READ_ICON_H */
