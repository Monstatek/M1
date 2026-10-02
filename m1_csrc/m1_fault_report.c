/* See COPYING.txt for license details. */

/*
*
* m1_fault_report.c
*
* Best-effort on-screen Cortex-M fault reporter (see m1_fault_report.h).
*
* M1 Project
*
*/

#include <stdint.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "m1_display.h"
#include "m1_fault_report.h"

/* Fault status captured by the fault handlers (Core/Src/stm32h5xx_it.c). */
extern volatile uint32_t g_hardfault_pc;
extern volatile uint32_t g_hardfault_lr;
extern volatile uint32_t g_hardfault_sp;
extern volatile uint32_t g_hardfault_cfsr;
extern volatile uint32_t g_hardfault_hfsr;
extern volatile uint32_t g_hardfault_mmfar;
extern volatile uint32_t g_hardfault_bfar;

/* Defined in m1_capture_link.c. Weakly referenced default here so the reporter
 * links even if the capture module is absent. */
__attribute__((weak)) volatile uint32_t g_cap_checkpoint = 0;

void m1_fault_report(const char *tag)
{
    char l[26];

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_FirstPage(&m1_u8g2);
    do
    {
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        snprintf(l, sizeof(l), "%s  CP=%lu", (tag != NULL) ? tag : "FAULT",
                 (unsigned long)g_cap_checkpoint);
        u8g2_DrawStr(&m1_u8g2, 0, 8, l);
        snprintf(l, sizeof(l), "PC   %08lX", (unsigned long)g_hardfault_pc);
        u8g2_DrawStr(&m1_u8g2, 0, 18, l);
        snprintf(l, sizeof(l), "LR   %08lX", (unsigned long)g_hardfault_lr);
        u8g2_DrawStr(&m1_u8g2, 0, 28, l);
        snprintf(l, sizeof(l), "CFSR %08lX", (unsigned long)g_hardfault_cfsr);
        u8g2_DrawStr(&m1_u8g2, 0, 38, l);
        snprintf(l, sizeof(l), "HFSR %08lX", (unsigned long)g_hardfault_hfsr);
        u8g2_DrawStr(&m1_u8g2, 0, 48, l);
        snprintf(l, sizeof(l), "AR %08lX/%08lX",
                 (unsigned long)g_hardfault_mmfar, (unsigned long)g_hardfault_bfar);
        u8g2_DrawStr(&m1_u8g2, 0, 58, l);
    } while (u8g2_NextPage(&m1_u8g2));
}
