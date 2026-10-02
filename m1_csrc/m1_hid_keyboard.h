/* See COPYING.txt for license details. */

/*
*
*  m1_hid_keyboard.h
*
*  MonstaTek M1 HID boot-keyboard transport foundation.
*
*  Independently authored M1-facing wrapper over the ST-derived HID class
*  (USB/Class/HID, usbd_hid_m1). It owns the boot-keyboard 8-byte report format
*  and provides a small API intended for a future FreeRTOS BadUSB worker task.
*
*  This is a backend foundation only. Nothing here is wired into the boot path,
*  a menu, a button, or normal firmware operation, and no report is ever emitted
*  automatically. HID stays inert until a future dedicated-HID activation step
*  binds and enables it.
*
* M1 Project
*
*/

#ifndef M1_HID_KEYBOARD_H_
#define M1_HID_KEYBOARD_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot-keyboard report: modifier byte, reserved byte, six simultaneous keys. */
#define M1_HID_KBD_REPORT_LEN     8U
#define M1_HID_KBD_MAX_KEYS       6U

/* Clear success/failure return values. */
typedef enum
{
    M1_HID_KBD_OK = 0,        /* report accepted for transmission             */
    M1_HID_KBD_NOT_READY,     /* HID not configured/bound - nothing sent      */
    M1_HID_KBD_PARAM,         /* invalid argument                             */
    M1_HID_KBD_TX_ERROR       /* class transport reported a failure           */
} m1_hid_kbd_status_t;

/*
 * Backend lifecycle. None of these activate USB HID by themselves; a future
 * dedicated-HID personality is responsible for actually registering the class
 * and then calling m1_hid_kbd_bind()/m1_hid_kbd_set_ready().
 */
void  m1_hid_kbd_init(void);                 /* reset backend state (no USB change)      */
void  m1_hid_kbd_bind(uint8_t class_id);     /* record the HID composite class id        */
void  m1_hid_kbd_set_ready(bool ready);      /* future activation marks HID configured   */
bool  m1_hid_kbd_is_ready(void);             /* HID bound-ready AND device configured?   */

/* Send one boot-keyboard report: modifier bitmap + up to six keycodes. */
m1_hid_kbd_status_t m1_hid_kbd_send_report(uint8_t modifier,
                                           const uint8_t keys[M1_HID_KBD_MAX_KEYS]);

/* Send an all-keys-released report (all zero). Safe to call on stop, error,
 * disconnect, deinit, and future mode exit. */
m1_hid_kbd_status_t m1_hid_kbd_release_all(void);

/* Safe stop: release all keys (if ready) and clear backend state. */
void  m1_hid_kbd_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* M1_HID_KEYBOARD_H_ */
