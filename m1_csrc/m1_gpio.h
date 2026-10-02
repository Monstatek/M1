/* See COPYING.txt for license details. */

/*
*
*  m1_gpio.h
*
*  M1 GPIO functions
*
* M1 Project
*
*/

#ifndef M1_GPIO_H_
#define M1_GPIO_H_

//#define DEBUG_SWLINK_USED
#undef DEBUG_SWLINK_USED

#ifndef DEBUG_SWLINK_USED
#define M1_EXT_GPIO_LIST_N                      15 // Exclude PA9 (UART1_TX), PA10 (UART1_RX)
#else
#define M1_EXT_GPIO_LIST_N                      13 // Exclude PA13 (SWCLK_Pin), PA14 (SWDIO_Pin)
                                                   // Exclude PA9 (UART1_TX), PA10 (UART1_RX)
#endif
// (PA14 (SWCLK) and PA13 (SWDIO))
#define M1_EXT_GPIO_FIRST_ID					3 // First GPIO pin id in the GPIO buffer

extern S_GPIO_IO_t m1_ext_gpio[];

void menu_gpio_init(void);
void menu_gpio_exit(void);

void gpio_manual_control(void);
void gpio_3_3v_on_gpio(void);
void gpio_5v_on_gpio(void);
void gpio_usb_uart_bridge(void);

void ext_power_5V_set(uint8_t set_mode);
void ext_power_3V_set(uint8_t set_mode);
void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);

#endif /* M1_GPIO_H_ */
