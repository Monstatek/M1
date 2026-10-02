/* See COPYING.txt for license details. */

/*
*
*  m1_hid_keyboard.c
*
*  MonstaTek M1 HID boot-keyboard transport foundation (independently authored).
*
*  Thin M1-facing wrapper over the ST-derived HID class (usbd_hid_m1). It owns
*  the boot-keyboard 8-byte report format and forwards reports to the
*  report-agnostic class transport USBD_HID_SendReport(buffer, length).
*
*  Safety model:
*    - a non-zero report is only ever produced when the caller explicitly asks
*      for one via m1_hid_kbd_send_report(); init/release_all/deinit produce a
*      zero (all-keys-released) report only;
*    - nothing here runs at boot, on USB connect, or because a script exists;
*    - reports are only transmitted when HID has been bound and marked ready by
*      a future dedicated-HID activation step AND the device is configured;
*    - no blocking delays or filesystem access; USBD_HID_SendReport only queues
*      an interrupt-IN transfer, so this is safe to drive from a future FreeRTOS
*      BadUSB worker task.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <string.h>
#include "m1_hid_keyboard.h"
#include "usbd_core.h"
#include "usbd_hid_m1.h"

/***************************** V A R I A B L E S ******************************/

/* The project's single USB device handle (defined in m1_usb_cdc_msc.c). */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* Bound composite class id of the HID interface, recorded by future activation. */
static volatile uint8_t s_hid_class_id = 0U;

/* Set true by future activation once the HID interface is registered/configured. */
static volatile bool s_hid_ready = false;

/* Boot-keyboard report buffer: [0]=modifier, [1]=reserved, [2..7]=keycodes. */
static uint8_t s_report[M1_HID_KBD_REPORT_LEN];

/********************* F U N C T I O N   I M P L E M E N T A T I O N **********/

void m1_hid_kbd_init(void)
{
    s_hid_ready = false;
    s_hid_class_id = 0U;
    (void)memset(s_report, 0, sizeof(s_report));
}

void m1_hid_kbd_bind(uint8_t class_id)
{
    s_hid_class_id = class_id;
}

void m1_hid_kbd_set_ready(bool ready)
{
    s_hid_ready = ready;
}

bool m1_hid_kbd_is_ready(void)
{
    return (s_hid_ready && (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED));
}

/* Transmit the current s_report[] via the report-agnostic class transport. */
static m1_hid_kbd_status_t m1_hid_kbd_tx(void)
{
    uint8_t ret;

    if (!m1_hid_kbd_is_ready())
    {
        return M1_HID_KBD_NOT_READY;
    }

#ifdef USE_USBD_COMPOSITE
    ret = USBD_HID_SendReport(&hUsbDeviceFS, s_report, (uint16_t)M1_HID_KBD_REPORT_LEN, s_hid_class_id);
#else
    ret = USBD_HID_SendReport(&hUsbDeviceFS, s_report, (uint16_t)M1_HID_KBD_REPORT_LEN);
#endif /* USE_USBD_COMPOSITE */

    return (ret == (uint8_t)USBD_OK) ? M1_HID_KBD_OK : M1_HID_KBD_TX_ERROR;
}

m1_hid_kbd_status_t m1_hid_kbd_send_report(uint8_t modifier,
                                           const uint8_t keys[M1_HID_KBD_MAX_KEYS])
{
    uint8_t i;

    if (keys == NULL)
    {
        return M1_HID_KBD_PARAM;
    }

    s_report[0] = modifier;
    s_report[1] = 0U; /* reserved byte, always zero */
    for (i = 0U; i < M1_HID_KBD_MAX_KEYS; i++)
    {
        s_report[2U + i] = keys[i];
    }

    return m1_hid_kbd_tx();
}

m1_hid_kbd_status_t m1_hid_kbd_release_all(void)
{
    /* All modifiers and keycodes cleared. */
    (void)memset(s_report, 0, sizeof(s_report));
    return m1_hid_kbd_tx();
}

void m1_hid_kbd_deinit(void)
{
    /* Ensure no keys remain "stuck" on the host before tearing down. */
    if (m1_hid_kbd_is_ready())
    {
        (void)m1_hid_kbd_release_all();
    }

    s_hid_ready = false;
    (void)memset(s_report, 0, sizeof(s_report));
}
