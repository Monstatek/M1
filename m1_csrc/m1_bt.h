/* See COPYING.txt for license details. */

/*
*
* m1_bt.h
*
* Header for M1 bluetooth
*
* M1 Project
*
*/

#ifndef M1_BT_H_
#define M1_BT_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Attributes State Machine */
enum
{
    IDX_SVC,
    IDX_CHAR_A,
    IDX_CHAR_VAL_A,
    IDX_CHAR_CFG_A,

    IDX_CHAR_B,
    IDX_CHAR_VAL_B,
    IDX_CHAR_CFG_B,

    IDX_CHAR_C,
    IDX_CHAR_VAL_C,
    IDX_CHAR_CFG_C,
    IDX_CHAR_CFG_C_2,

    HRS_IDX_NB,
};

void menu_bluetooth_init(void);
void menu_bluetooth_exit(void);
void bluetooth_scan_devices(void);
void bluetooth_signal_meter(void);
void bluetooth_gatt_explorer(void);
void bluetooth_saved(void);

#endif /* M1_BT_H_ */
