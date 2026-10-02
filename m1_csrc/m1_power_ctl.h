/* See COPYING.txt for license details. */

/*
*
* m1_power_ctl.h
*
* Driver and library for battery charger
*
* M1 Project
*
*/

#ifndef M1_POWER_CTL_H_
#define M1_POWER_CTL_H_

void menu_setting_power_init(void);
void menu_setting_power_exit(void);

void power_battery_info(void);
void power_reboot(void);
void power_off(void);
void battery_golden_image(void);
//void power_init(void);
void m1_pre_power_down(void);
void m1_power_down(void);
void power_shutdown_gui_update(uint8_t param);

int power_test_charger(uint8_t reg);

extern float f_monitor;

void get_power_status(void);
uint8_t m1_check_battery_level(uint8_t remaining_charge);

#endif /* M1_POWER_CTL_H_ */
