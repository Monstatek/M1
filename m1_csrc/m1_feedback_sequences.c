/* See COPYING.txt for license details. */

/*
*
* m1_feedback_sequences.c
*
* Single-resource sequence catalogue: reference sections 7.1-7.4 (LED
* resets, solid holds, one-shot flashes, continuous blinks) and section 8
* (audio sequences). Except for the customized success cue noted below, the
* message arrays transcribe M1-FB-STD-001 v1.1 -- see
* documentation/M1_FEEDBACK_RECONCILIATION.md for the identifier-by-
* identifier mapping and the notation key (reference section 3.3):
*
*   SAVE_LED    = MSG_LED_SAVE          LED(x)   = MSG_LED_SET(x)
*   RESTORE_LED = MSG_LED_RESTORE       BL(x)    = MSG_BL_SET(x)
*   NOTE(x)     = MSG_SND_NOTE(x)       SND_OFF  = MSG_SND_OFF
*   D(x)        = MSG_DELAY(x)          LOOP(n)  = MSG_LOOP_BEGIN(n)
*   LOOP_END    = MSG_LOOP_END          END      = MSG_END
*
* M1 Project
*
*/

#include <stddef.h>
#include "m1_feedback_sequences.h"

/*************************** 7 . 1   I M M E D I A T E   R E S E T ***************/

static const fb_msg_t SEQ_PROG_RESET_RGB[] = {
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),
	FB_END,
};

static const fb_msg_t SEQ_PROG_RESET_SOUND[] = {
	FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_RESET_DISPLAY[] = {
	FB_MSG(FB_MSG_BL_OFF, 0),
	FB_END,
};

/* SEQ_RESET_ALL is handled specially by fb_request()/fb_reset_all() (the
 * supervisor fallback -- reference section 13), but the literal message
 * array is still provided here for one-to-one fidelity with reference
 * section 7.1 and so the orchestration layer can splice it if ever needed. */
static const fb_msg_t SEQ_PROG_RESET_ALL[] = {
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),
	FB_MSG(FB_MSG_BL_RELEASE, 0),
	FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

/*************************** 7 . 2   S O L I D   H O L D S ************************/

#define SOLID_PROG(name_, mask_) \
	static const fb_msg_t name_[] = { FB_MSG(FB_MSG_LED_SET, (mask_)), FB_END }

SOLID_PROG(SEQ_PROG_SOLID_RED,     FB_LED_RED);
SOLID_PROG(SEQ_PROG_SOLID_GREEN,   FB_LED_GREEN);
SOLID_PROG(SEQ_PROG_SOLID_BLUE,    FB_LED_BLUE);
SOLID_PROG(SEQ_PROG_SOLID_YELLOW,  FB_LED_YELLOW);
SOLID_PROG(SEQ_PROG_SOLID_CYAN,    FB_LED_CYAN);
SOLID_PROG(SEQ_PROG_SOLID_MAGENTA, FB_LED_MAGENTA);
SOLID_PROG(SEQ_PROG_SOLID_WHITE,   FB_LED_WHITE);

/*************************** 7 . 3   O N E - S H O T   F L A S H E S **************/

/* SAVE_LED, LED(colour), D(duration), RESTORE_LED, END */
#define FLASH_PROG(name_, mask_, dur_) \
	static const fb_msg_t name_[] = { \
		FB_MSG(FB_MSG_LED_SAVE, 0), \
		FB_MSG(FB_MSG_LED_SET, (mask_)), \
		FB_MSG(FB_MSG_DELAY, (dur_)), \
		FB_MSG(FB_MSG_LED_RESTORE, 0), \
		FB_END, \
	}

FLASH_PROG(SEQ_PROG_FLASH_RED_10,     FB_LED_RED,     10);
FLASH_PROG(SEQ_PROG_FLASH_GREEN_10,   FB_LED_GREEN,   10);
FLASH_PROG(SEQ_PROG_FLASH_BLUE_10,    FB_LED_BLUE,    10);
FLASH_PROG(SEQ_PROG_FLASH_YELLOW_10,  FB_LED_YELLOW,  10);
FLASH_PROG(SEQ_PROG_FLASH_CYAN_10,    FB_LED_CYAN,    10);
FLASH_PROG(SEQ_PROG_FLASH_MAGENTA_10, FB_LED_MAGENTA, 10);
FLASH_PROG(SEQ_PROG_FLASH_WHITE_10,   FB_LED_WHITE,   10);

FLASH_PROG(SEQ_PROG_FLASH_RED_100,     FB_LED_RED,     100);
FLASH_PROG(SEQ_PROG_FLASH_GREEN_100,   FB_LED_GREEN,   100);
FLASH_PROG(SEQ_PROG_FLASH_BLUE_100,    FB_LED_BLUE,    100);
FLASH_PROG(SEQ_PROG_FLASH_YELLOW_100,  FB_LED_YELLOW,  100);
FLASH_PROG(SEQ_PROG_FLASH_CYAN_100,    FB_LED_CYAN,    100);
FLASH_PROG(SEQ_PROG_FLASH_MAGENTA_100, FB_LED_MAGENTA, 100);
FLASH_PROG(SEQ_PROG_FLASH_WHITE_100,   FB_LED_WHITE,   100);

/*************************** 7 . 4   C O N T I N U O U S   B L I N K S ************/

/* LOOP(0), LED(colour), D(on_ms), LED(LED_OFF), D(off_ms), LOOP_END, END */
#define BLINK_PROG(name_, mask_, on_, off_) \
	static const fb_msg_t name_[] = { \
		FB_MSG(FB_MSG_LOOP_BEGIN, 0), \
		FB_MSG(FB_MSG_LED_SET, (mask_)), \
		FB_MSG(FB_MSG_DELAY, (on_)), \
		FB_MSG(FB_MSG_LED_SET, FB_LED_OFF), \
		FB_MSG(FB_MSG_DELAY, (off_)), \
		FB_MSG(FB_MSG_LOOP_END, 0), \
		FB_END, \
	}

BLINK_PROG(SEQ_PROG_BLINK_RED_10,     FB_LED_RED,     10, 100);
BLINK_PROG(SEQ_PROG_BLINK_GREEN_10,   FB_LED_GREEN,   10, 100);
BLINK_PROG(SEQ_PROG_BLINK_BLUE_10,    FB_LED_BLUE,    10, 100);
BLINK_PROG(SEQ_PROG_BLINK_YELLOW_10,  FB_LED_YELLOW,  10, 100);
BLINK_PROG(SEQ_PROG_BLINK_CYAN_10,    FB_LED_CYAN,    10, 100);
BLINK_PROG(SEQ_PROG_BLINK_MAGENTA_10, FB_LED_MAGENTA, 10, 100);
BLINK_PROG(SEQ_PROG_BLINK_WHITE_10,   FB_LED_WHITE,   10, 100);

BLINK_PROG(SEQ_PROG_BLINK_RED_100,     FB_LED_RED,     100, 100);
BLINK_PROG(SEQ_PROG_BLINK_GREEN_100,   FB_LED_GREEN,   100, 100);
BLINK_PROG(SEQ_PROG_BLINK_BLUE_100,    FB_LED_BLUE,    100, 100);
BLINK_PROG(SEQ_PROG_BLINK_YELLOW_100,  FB_LED_YELLOW,  100, 100);
BLINK_PROG(SEQ_PROG_BLINK_CYAN_100,    FB_LED_CYAN,    100, 100);
BLINK_PROG(SEQ_PROG_BLINK_MAGENTA_100, FB_LED_MAGENTA, 100, 100);
BLINK_PROG(SEQ_PROG_BLINK_WHITE_100,   FB_LED_WHITE,   100, 100);

/*********** S U P E R S E D I N G   P A L E T T E  *******************/
/*
 * See documentation/M1_FEEDBACK_RECONCILIATION.md for palette rationale
 * and timing constants. These
 * are ordinary single-resource LED-only message arrays, added to the
 * catalogue exactly like the 7.4 continuous blinks above; Read reuses the
 * existing SEQ_PROG_BLINK_WHITE_100 entry directly and needs no new array.
 */

/* Detect/Listen: slow flashing blue, 500 ms on / 500 ms off -- a clearly
 * slower cadence than any existing 10/100 ms family member, chosen to read
 * as an unhurried "listening" cue. */
BLINK_PROG(SEQ_PROG_BLINK_BLUE_SLOW, FB_LED_BLUE, 500, 500);

/* Emulate approximates a pulse with 1000 ms on / 400 ms off (~71% duty).
 * The LED message/adapter interface carries an on/off colour mask, and
 * ownership restoration tracks that mask rather than per-channel brightness.
 * A PWM ramp would require coordinated changes to both interfaces and restore
 * state. This cadence differs from Detect (500/500) and Read (100/100).
 * See documentation/M1_FEEDBACK_RECONCILIATION.md for design rationale. */
BLINK_PROG(SEQ_PROG_PULSE_BLUE, FB_LED_BLUE, 1000, 400);

/* Write: double-flash white -- two quick 80 ms flashes with an 80 ms gap
 * between them, then a 600 ms pause, repeating. Deliberately a different
 * shape (not just a different timing) from a simple flash, so it cannot be
 * mistaken for Read's fast flash even at a glance. */
static const fb_msg_t SEQ_PROG_DBLFLASH_WHITE[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_LED_SET, FB_LED_WHITE), FB_MSG(FB_MSG_DELAY, 80),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),   FB_MSG(FB_MSG_DELAY, 80),
	FB_MSG(FB_MSG_LED_SET, FB_LED_WHITE), FB_MSG(FB_MSG_DELAY, 80),
	FB_MSG(FB_MSG_LED_SET, FB_LED_OFF),   FB_MSG(FB_MSG_DELAY, 600),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

/* Replay/Transmit: alternating blue and white, 150 ms each colour (300 ms
 * period), equal dwell for both colours. */
static const fb_msg_t SEQ_PROG_ALT_BLUE_WHITE[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_LED_SET, FB_LED_BLUE),  FB_MSG(FB_MSG_DELAY, 150),
	FB_MSG(FB_MSG_LED_SET, FB_LED_WHITE), FB_MSG(FB_MSG_DELAY, 150),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

/* Firmware update: alternating cyan and magenta, 200 ms each colour (400 ms
 * period) -- a distinct cadence from Replay's 150/150 blue/white so the two
 * alternating states remain visually distinguishable by period as well as
 * colour. */
static const fb_msg_t SEQ_PROG_ALT_CYAN_MAGENTA[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_LED_SET, FB_LED_CYAN),    FB_MSG(FB_MSG_DELAY, 200),
	FB_MSG(FB_MSG_LED_SET, FB_LED_MAGENTA), FB_MSG(FB_MSG_DELAY, 200),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

/*************************** 8 .   A U D I O   S E Q U E N C E S ******************/

/* M1 success cue: a single flat beep at G5 (3136 Hz) ~= the piezo's ~3.2 kHz
 * mechanical resonance -- the loudest tone this fixed-drive piezo can make (no
 * amplitude control exists, so all energy is concentrated at resonance).
 * Single-beep, utilitarian convention (cf. the Proxmark PM5 BuzzerBeep(), one
 * ~2 kHz tone -- pitched here at the M1 piezo's resonance so it is loud rather
 * than weak). Flat, not rising (avoids a "cheery" feel) and not falling
 * (avoids a TikTok-like descending pair). 150 ms reads as a deliberate tone
 * rather than a click and does not grate on repeat. Distinct from the
 * error/notify BEEP2 (two short C5 beeps) by pitch and count. Perceived
 * loudness of this pitch/duration is pending on-hardware confirmation. Note
 * maps through the x4 NOTE_FREQ_HZ table: G5 = 3136 Hz. */
static const fb_msg_t SEQ_PROG_SND_SUCCESS[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 150), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_ERROR[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_A4), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_A4), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_NOTIFY[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_BATT_LOW[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C4), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C4), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_BOOT[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_E5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C6), FB_MSG(FB_MSG_DELAY, 250), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_POWEROFF[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C6), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_E5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 250), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_BEEP[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 50), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_BEEP2[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 50), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 50), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_USB_IN[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_USB_OUT[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_G5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 50),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C5), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

static const fb_msg_t SEQ_PROG_SND_TARGET[] = {
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C6), FB_MSG(FB_MSG_DELAY, 100), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_END,
};

/* LOOP(0), NOTE(C6), D(250), SND_OFF, D(100), NOTE(C4), D(250), SND_OFF, D(100), LOOP_END, END */
static const fb_msg_t SEQ_PROG_SND_BATT_CRIT[] = {
	FB_MSG(FB_MSG_LOOP_BEGIN, 0),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C6), FB_MSG(FB_MSG_DELAY, 250), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_SND_NOTE, FB_NOTE_C4), FB_MSG(FB_MSG_DELAY, 250), FB_MSG(FB_MSG_SND_OFF, 0),
	FB_MSG(FB_MSG_DELAY, 100),
	FB_MSG(FB_MSG_LOOP_END, 0),
	FB_END,
};

/*************************** C A T A L O G U E   T A B L E ************************/

typedef struct
{
	const fb_msg_t *prog;
	fb_resource_t   resource;
	fb_kind_t       kind;
	bool            valid;	/* false = not a simple single-resource entry */
} fb_cat_entry_t;

/* Indexed directly by fb_seq_id_t. Entries left {0} (valid=false) are the
 * orchestrated multi-resource identifiers (composites, power/lifecycle,
 * radio, USB, payload, settings previews), SEQ_RESET_ALL (handled specially
 * by fb_request()), and SEQ_BLINK_STOP (not a message array at all -- see
 * fb_stop_led_loop()). See m1_feedback_orchestration.c for those. */
static const fb_cat_entry_t g_catalogue[SEQ_ID_COUNT] =
{
	[SEQ_RESET_RGB]     = { SEQ_PROG_RESET_RGB,     FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_RESET_SOUND]   = { SEQ_PROG_RESET_SOUND,   FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_RESET_DISPLAY] = { SEQ_PROG_RESET_DISPLAY, FB_RES_BL,  FB_KIND_ONESHOT, true },
	/* SEQ_RESET_ALL: intentionally not listed here -- fb_request() special-
	 * cases it and calls fb_reset_all() directly (reference section 13). */

	[SEQ_SOLID_RED]     = { SEQ_PROG_SOLID_RED,     FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_GREEN]   = { SEQ_PROG_SOLID_GREEN,   FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_BLUE]    = { SEQ_PROG_SOLID_BLUE,    FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_YELLOW]  = { SEQ_PROG_SOLID_YELLOW,  FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_CYAN]    = { SEQ_PROG_SOLID_CYAN,    FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_MAGENTA] = { SEQ_PROG_SOLID_MAGENTA, FB_RES_LED, FB_KIND_HOLD, true },
	[SEQ_SOLID_WHITE]   = { SEQ_PROG_SOLID_WHITE,   FB_RES_LED, FB_KIND_HOLD, true },

	[SEQ_FLASH_RED_10]     = { SEQ_PROG_FLASH_RED_10,     FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_GREEN_10]   = { SEQ_PROG_FLASH_GREEN_10,   FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_BLUE_10]    = { SEQ_PROG_FLASH_BLUE_10,    FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_YELLOW_10]  = { SEQ_PROG_FLASH_YELLOW_10,  FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_CYAN_10]    = { SEQ_PROG_FLASH_CYAN_10,    FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_MAGENTA_10] = { SEQ_PROG_FLASH_MAGENTA_10, FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_WHITE_10]   = { SEQ_PROG_FLASH_WHITE_10,   FB_RES_LED, FB_KIND_ONESHOT, true },

	[SEQ_FLASH_RED_100]     = { SEQ_PROG_FLASH_RED_100,     FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_GREEN_100]   = { SEQ_PROG_FLASH_GREEN_100,   FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_BLUE_100]    = { SEQ_PROG_FLASH_BLUE_100,    FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_YELLOW_100]  = { SEQ_PROG_FLASH_YELLOW_100,  FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_CYAN_100]    = { SEQ_PROG_FLASH_CYAN_100,    FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_MAGENTA_100] = { SEQ_PROG_FLASH_MAGENTA_100, FB_RES_LED, FB_KIND_ONESHOT, true },
	[SEQ_FLASH_WHITE_100]   = { SEQ_PROG_FLASH_WHITE_100,   FB_RES_LED, FB_KIND_ONESHOT, true },

	[SEQ_BLINK_RED_10]     = { SEQ_PROG_BLINK_RED_10,     FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_GREEN_10]   = { SEQ_PROG_BLINK_GREEN_10,   FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_BLUE_10]    = { SEQ_PROG_BLINK_BLUE_10,    FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_YELLOW_10]  = { SEQ_PROG_BLINK_YELLOW_10,  FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_CYAN_10]    = { SEQ_PROG_BLINK_CYAN_10,    FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_MAGENTA_10] = { SEQ_PROG_BLINK_MAGENTA_10, FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_WHITE_10]   = { SEQ_PROG_BLINK_WHITE_10,   FB_RES_LED, FB_KIND_LOOP, true },

	[SEQ_BLINK_RED_100]     = { SEQ_PROG_BLINK_RED_100,     FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_GREEN_100]   = { SEQ_PROG_BLINK_GREEN_100,   FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_BLUE_100]    = { SEQ_PROG_BLINK_BLUE_100,    FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_YELLOW_100]  = { SEQ_PROG_BLINK_YELLOW_100,  FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_CYAN_100]    = { SEQ_PROG_BLINK_CYAN_100,    FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_MAGENTA_100] = { SEQ_PROG_BLINK_MAGENTA_100, FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_BLINK_WHITE_100]   = { SEQ_PROG_BLINK_WHITE_100,   FB_RES_LED, FB_KIND_LOOP, true },

	/* Interaction palette -- see the section above. */
	[SEQ_BLINK_BLUE_SLOW]  = { SEQ_PROG_BLINK_BLUE_SLOW,  FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_PULSE_BLUE]       = { SEQ_PROG_PULSE_BLUE,       FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_DBLFLASH_WHITE]   = { SEQ_PROG_DBLFLASH_WHITE,   FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_ALT_BLUE_WHITE]   = { SEQ_PROG_ALT_BLUE_WHITE,   FB_RES_LED, FB_KIND_LOOP, true },
	[SEQ_ALT_CYAN_MAGENTA] = { SEQ_PROG_ALT_CYAN_MAGENTA, FB_RES_LED, FB_KIND_LOOP, true },

	/* SEQ_BLINK_STOP: intentionally not listed -- see fb_stop_led_loop(). */

	[SEQ_SND_SUCCESS]   = { SEQ_PROG_SND_SUCCESS,   FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_ERROR]     = { SEQ_PROG_SND_ERROR,     FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_NOTIFY]    = { SEQ_PROG_SND_NOTIFY,    FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_BATT_LOW]  = { SEQ_PROG_SND_BATT_LOW,  FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_BOOT]      = { SEQ_PROG_SND_BOOT,      FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_POWEROFF]  = { SEQ_PROG_SND_POWEROFF,  FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_BEEP]      = { SEQ_PROG_SND_BEEP,      FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_BEEP2]     = { SEQ_PROG_SND_BEEP2,     FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_USB_IN]    = { SEQ_PROG_SND_USB_IN,    FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_USB_OUT]   = { SEQ_PROG_SND_USB_OUT,   FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_TARGET]    = { SEQ_PROG_SND_TARGET,    FB_RES_SND, FB_KIND_ONESHOT, true },
	[SEQ_SND_BATT_CRIT] = { SEQ_PROG_SND_BATT_CRIT, FB_RES_SND, FB_KIND_LOOP, true },
};

bool fb_catalogue_lookup(fb_seq_id_t seq_id, const fb_msg_t **prog_out,
                          fb_resource_t *resource_out, fb_kind_t *kind_out)
{
	const fb_cat_entry_t *e;

	if ( (uint32_t)seq_id >= (uint32_t)SEQ_ID_COUNT )
		return false;

	e = &g_catalogue[seq_id];
	if ( !e->valid )
		return false;

	if ( prog_out != NULL )
		*prog_out = e->prog;
	if ( resource_out != NULL )
		*resource_out = e->resource;
	if ( kind_out != NULL )
		*kind_out = e->kind;
	return true;
} // bool fb_catalogue_lookup(...)



const fb_msg_t *fb_seq_prog(fb_seq_id_t seq_id)
{
	/* Also exposes SEQ_RESET_ALL's literal array (not reachable through
	 * fb_catalogue_lookup, see above) for orchestration/documentation use. */
	if ( seq_id == SEQ_RESET_ALL )
		return SEQ_PROG_RESET_ALL;

	if ( (uint32_t)seq_id < (uint32_t)SEQ_ID_COUNT && g_catalogue[seq_id].valid )
		return g_catalogue[seq_id].prog;

	return NULL;
} // const fb_msg_t *fb_seq_prog(fb_seq_id_t seq_id)
