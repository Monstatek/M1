/* See COPYING.txt for license details. */

/*
*
* m1_feedback_types.h
*
* Shared message/sequence types for the M1 feedback manager.
*
* Implements the message-array representation defined by the
* "MonstaTek M1 Feedback and Notification Reference" (document
* M1-FB-STD-001, v1.1). See documentation/M1_FEEDBACK_RECONCILIATION.md
* for the full identifier-to-implementation mapping.
*
* M1 Project
*
*/

#ifndef M1_FEEDBACK_TYPES_H_
#define M1_FEEDBACK_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

/*************************** M E S S A G E   O P S *****************************/

/* One atomic manager instruction. Mirrors reference section 4. */
typedef enum
{
	FB_MSG_END = 0,			/* MSG_END                       */
	FB_MSG_LED_SAVE,			/* MSG_LED_SAVE   (kept for fidelity; see note below) */
	FB_MSG_LED_SET,				/* MSG_LED_SET(mask)              arg = mask   */
	FB_MSG_LED_ADD,				/* MSG_LED_ADD(mask)               arg = mask  */
	FB_MSG_LED_REMOVE,			/* MSG_LED_REMOVE(mask)            arg = mask  */
	FB_MSG_LED_RESTORE,			/* MSG_LED_RESTORE                             */
	FB_MSG_LED_RELEASE,			/* MSG_LED_RELEASE                             */
	FB_MSG_BL_SAVE,				/* MSG_BL_SAVE    (kept for fidelity)          */
	FB_MSG_BL_SET,				/* MSG_BL_SET(value)               arg = 0-100 */
	FB_MSG_BL_USER,				/* MSG_BL_USER                                 */
	FB_MSG_BL_OFF,				/* MSG_BL_OFF                                  */
	FB_MSG_BL_RESTORE,			/* MSG_BL_RESTORE                              */
	FB_MSG_BL_HOLD_USER,		/* MSG_BL_HOLD_USER                            */
	FB_MSG_BL_RELEASE,			/* MSG_BL_RELEASE                              */
	FB_MSG_SND_NOTE,			/* MSG_SND_NOTE(note)              arg = note id */
	FB_MSG_SND_OFF,				/* MSG_SND_OFF                                 */
	FB_MSG_DELAY,				/* MSG_DELAY(ms)                   arg = ms      */
	FB_MSG_LOOP_BEGIN,			/* MSG_LOOP_BEGIN(count)           arg = count, 0 = continuous */
	FB_MSG_LOOP_END,			/* MSG_LOOP_END                                 */
} fb_msg_op_t;

/*
 * NOTE on FB_MSG_LED_SAVE / FB_MSG_BL_SAVE:
 * The reference notation includes SAVE/RESTORE pairs so a one-shot can put a
 * resource back the way it found it (section 4.1/4.2). This implementation
 * performs *logical* restoration instead of a blind register copy-back
 * (reference section 6, rule 8): ownership state for every resource is kept
 * as a small priority-ordered list (m1_feedback_manager.c), so "restore"
 * really means "recompute the current highest-eligible owner and apply it".
 * FB_MSG_LED_SAVE / FB_MSG_BL_SAVE are therefore accepted (for one-to-one
 * fidelity with the document's message notation) but are no-ops at
 * execution time; FB_MSG_LED_RESTORE / FB_MSG_BL_RESTORE perform the
 * recomputation. This is an intentional, documented deviation from a
 * literal "push/pop a register" implementation -- see
 * documentation/M1_FEEDBACK_RECONCILIATION.md.
 */

typedef struct
{
	uint8_t  op;	/* fb_msg_op_t */
	uint16_t arg;
} fb_msg_t;

#define FB_MSG(op_, arg_)	{ (uint8_t)(op_), (uint16_t)(arg_) }
#define FB_END				FB_MSG(FB_MSG_END, 0)

/*************************** R E S O U R C E S **********************************/

typedef enum
{
	FB_RES_LED = 0,
	FB_RES_BL,
	FB_RES_SND,
	FB_RES_COUNT
} fb_resource_t;

/* LED colour masks -- bit-compatible with LED_BLINK_ON_RED/GREEN/BLUE in
 * m1_lp5814.h so the hardware adapter can pass the mask straight through. */
#define FB_LED_OFF		0x00u
#define FB_LED_RED		0x01u
#define FB_LED_GREEN	0x02u
#define FB_LED_BLUE		0x04u
#define FB_LED_YELLOW	(FB_LED_RED | FB_LED_GREEN)
#define FB_LED_CYAN		(FB_LED_GREEN | FB_LED_BLUE)
#define FB_LED_MAGENTA	(FB_LED_RED | FB_LED_BLUE)
#define FB_LED_WHITE	(FB_LED_RED | FB_LED_GREEN | FB_LED_BLUE)

/*************************** P R I O R I T Y   /   K I N D  ********************/

/* Reference section 6. Lower numeric value = higher priority. */
typedef enum
{
	FB_PRIO_ADMIN         = 0,		/* Shutdown/cancel/reset cleanup            */
	FB_PRIO_CRITICAL_PWR  = 1,		/* Critical battery                         */
	FB_PRIO_PWR_STATE     = 2,		/* Charging, charged                        */
	FB_PRIO_SYSTEM_ALERT  = 3,		/* Low battery, boot, power off             */
	FB_PRIO_APP_ALERT     = 4,		/* Success, error, notify, target, USB chime*/
	FB_PRIO_PERSISTENT    = 5,		/* Emulation, USB connected, payload running*/
	FB_PRIO_BACKGROUND    = 6,		/* Scanning, receiving, channel hopping      */
	FB_PRIO_NONE          = 7,		/* Sentinel: resource idle                  */
} fb_priority_t;

/* How a sequence's ownership of a resource ends. */
typedef enum
{
	FB_KIND_ONESHOT = 0,	/* Runs once, then releases (auto-recompute).   */
	FB_KIND_HOLD,			/* Persistent; released only by explicit call.  */
	FB_KIND_LOOP,			/* Repeats until stopped/preempted.             */
} fb_kind_t;

/* Requesting subsystems. Used for "same owner replaces" (rule 6) and for
 * owner-scoped cleanup (section 13). Add new owners here as call sites are
 * migrated -- see documentation/M1_FEEDBACK_RECONCILIATION.md. */
typedef enum
{
	FB_OWNER_NONE = 0,
	FB_OWNER_ADMIN,			/* SEQ_RESET_*, supervisor fallback            */
	FB_OWNER_POWER,			/* Charging/charged/battery/boot/shutdown      */
	FB_OWNER_USB,
	FB_OWNER_RADIO,			/* NFC/RFID/Sub-GHz field/emulation/hopping    */
	FB_OWNER_PAYLOAD,
	FB_OWNER_UI_SETTINGS,
	FB_OWNER_NOTIFY,		/* Generic success/error/notify beeps          */
	FB_OWNER_SLEEP_TIMER,	/* LCD backlight sleep-timer background hold   */
	FB_OWNER_DIAGNOSTIC,	/* Factory/CLI bypass bookkeeping only         */
	FB_OWNER_FW_UPDATE,		/* Firmware-update in-progress indication      */
	FB_OWNER_COUNT
} fb_owner_t;

/*************************** N O T E S ******************************************/

typedef enum
{
	FB_NOTE_C4 = 0,
	FB_NOTE_A4,
	FB_NOTE_C5,
	FB_NOTE_E5,
	FB_NOTE_G5,
	FB_NOTE_C6,
	FB_NOTE_COUNT
} fb_note_t;

/*************************** S E Q U E N C E   I D S ****************************/

typedef enum
{
	/* 7.1 Immediate reset */
	SEQ_RESET_RGB = 0,
	SEQ_RESET_SOUND,
	SEQ_RESET_DISPLAY,
	SEQ_RESET_ALL,

	/* 7.2 Solid holds */
	SEQ_SOLID_RED,
	SEQ_SOLID_GREEN,
	SEQ_SOLID_BLUE,
	SEQ_SOLID_YELLOW,
	SEQ_SOLID_CYAN,
	SEQ_SOLID_MAGENTA,
	SEQ_SOLID_WHITE,

	/* 7.3 One-shot flashes: 10 ms family */
	SEQ_FLASH_RED_10,
	SEQ_FLASH_GREEN_10,
	SEQ_FLASH_BLUE_10,
	SEQ_FLASH_YELLOW_10,
	SEQ_FLASH_CYAN_10,
	SEQ_FLASH_MAGENTA_10,
	SEQ_FLASH_WHITE_10,

	/* 7.3 One-shot flashes: 100 ms family */
	SEQ_FLASH_RED_100,
	SEQ_FLASH_GREEN_100,
	SEQ_FLASH_BLUE_100,
	SEQ_FLASH_YELLOW_100,
	SEQ_FLASH_CYAN_100,
	SEQ_FLASH_MAGENTA_100,
	SEQ_FLASH_WHITE_100,

	/* 7.4 Continuous blinks: 10/100 family */
	SEQ_BLINK_RED_10,
	SEQ_BLINK_GREEN_10,
	SEQ_BLINK_BLUE_10,
	SEQ_BLINK_YELLOW_10,
	SEQ_BLINK_CYAN_10,
	SEQ_BLINK_MAGENTA_10,
	SEQ_BLINK_WHITE_10,

	/* 7.4 Continuous blinks: 100/100 family */
	SEQ_BLINK_RED_100,
	SEQ_BLINK_GREEN_100,
	SEQ_BLINK_BLUE_100,
	SEQ_BLINK_YELLOW_100,
	SEQ_BLINK_CYAN_100,
	SEQ_BLINK_MAGENTA_100,
	SEQ_BLINK_WHITE_100,

	/* Interaction palette (see documentation/M1_FEEDBACK_RECONCILIATION.md
	 * for palette rationale): slow-flash blue for
	 * Detect/Listen, an approximated pulse-blue for Emulate, a double-flash
	 * white for the new Write state, and two alternating-colour loops for
	 * Replay/Transmit and Firmware Update. Read reuses the existing
	 * SEQ_BLINK_WHITE_100 fast-flash entry directly -- no new id needed. */
	SEQ_BLINK_BLUE_SLOW,	/* Detect/Listen -- slow flashing blue, 500/500   */
	SEQ_PULSE_BLUE,			/* Emulate -- pulse-approximation blue, see doc   */
	SEQ_DBLFLASH_WHITE,		/* Write -- double-flash white, repeating         */
	SEQ_ALT_BLUE_WHITE,		/* Replay/Transmit -- alternating blue/white      */
	SEQ_ALT_CYAN_MAGENTA,	/* Firmware update -- alternating cyan/magenta    */

	SEQ_BLINK_STOP,			/* Not a message array -- see manager API */

	/* 8. Audio sequences */
	SEQ_SND_SUCCESS,
	SEQ_SND_ERROR,
	SEQ_SND_NOTIFY,
	SEQ_SND_BATT_LOW,
	SEQ_SND_BOOT,
	SEQ_SND_POWEROFF,
	SEQ_SND_BEEP,
	SEQ_SND_BEEP2,
	SEQ_SND_USB_IN,
	SEQ_SND_USB_OUT,
	SEQ_SND_TARGET,
	SEQ_SND_BATT_CRIT,

	/* 9. Composite alerts (orchestrated, see m1_feedback_orchestration.c) */
	SEQ_ALERT_SUCCESS,
	SEQ_ALERT_ERROR,
	SEQ_ALERT_NOTIFY,
	SEQ_ALERT_BEEP,
	SEQ_ALERT_BEEP2,

	/* 10. Power and lifecycle */
	SEQ_PWR_BOOT,
	SEQ_PWR_OFF,
	SEQ_PWR_CHARGING,
	SEQ_PWR_CHARGED,
	SEQ_PWR_NOT_CHARGING,
	SEQ_PWR_BATT_LOW,
	SEQ_PWR_BATT_CRIT,

	/* 11.1 Radio states -- see documentation/M1_FEEDBACK_RECONCILIATION.md,
	 * for the current mapping. */
	SEQ_NET_FIELD_DETECT,	/* Detect/Listen -- slow flashing blue  */
	SEQ_NET_READING,	/* Read -- fast flashing white */
	SEQ_NET_EMULATING,	/* Emulate -- pulse blue (backlight hold preserved) */
	SEQ_NET_REPLAY,		/* Replay/Transmit -- alternating blue/white */
	SEQ_NET_WRITE,		/* Write -- double-flash white */
	SEQ_NET_HOPPING,
	SEQ_NET_TARGET,

	/* 11.2 USB states */
	SEQ_USB_CONNECT,
	SEQ_USB_ACTIVITY,
	SEQ_USB_DISCONNECT,

	/* Firmware update: alternating cyan/magenta, FB_PRIO_SYSTEM_ALERT. */
	SEQ_FW_UPDATE,

	/* 11.3 Automated execution states */
	SEQ_PAY_START,
	SEQ_PAY_DONE,
	SEQ_PAY_FAIL,

	/* 12. Settings previews */
	SEQ_UI_VOL_PREVIEW,
	SEQ_UI_BRIGHT_PREVIEW,

	SEQ_ID_COUNT
} fb_seq_id_t;

#endif /* M1_FEEDBACK_TYPES_H_ */
