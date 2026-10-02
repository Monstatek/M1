/* See COPYING.txt for license details. */

/*
*
* m1_lib.h
*
* Header for common lib
*
* M1 Project
*
*/

#ifndef M1_LIB_H_
#define M1_LIB_H_

#include "m1_tasks.h"

void m1_test_gpio_pull_high(void);
void m1_test_gpio_pull_low(void);
void m1_test_gpio_toggle(void);
void m1_test2_gpio_pull_high(void);
void m1_test2_gpio_pull_low(void);
void m1_test2_gpio_toggle(void);
void m1_strtolower(char *str);
void m1_strtoupper(char *str);
void m1_float_to_string(char *out_str, float in_val, uint8_t in_fraction);
float m1_float_convert(uint8_t value, float factor, float offset);
void m1_text_enable(char* out, uint8_t value, uint8_t mask);
int m1_strtob_with_base(const char *str, uint8_t *out, int max_len, int base);
void m1_byte_to_hextext(const uint8_t *src, int len, char *out);
void m1_vkb_set_initial_text(int len, char *out);
/* Bounded queue send. Returns pdTRUE on delivery and pdFALSE for a NULL,
 * full, or otherwise unavailable queue. */
BaseType_t m1_app_send_q_message(QueueHandle_t Handle, S_M1_Q_Event_Type_t cmd);
uint8_t m1_datfile_keywords_check(char *instr, char *keyword, char delimiter);
void m1_hard_delay(uint32_t x);

#endif /* M1_LIB_H_ */
