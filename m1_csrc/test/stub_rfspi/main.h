/* Host-test stub for the project-wide main.h, as far as m1_rf_spi.c needs it.
 * The chip-select port/pin symbols must be compile-time constant pointers so
 * the file-scope `static const spi_nss_port[]` initializer stays a constant
 * expression (fake but distinct non-NULL addresses -- the test only cares that
 * they are distinguishable and non-NULL). M1_LOG_E is normally reached via
 * main.h -> m1_log_debug.h. */
#ifndef STUB_MAIN_H_
#define STUB_MAIN_H_

#include <assert.h>

#define NFC_CS_GPIO_Port     ((GPIO_TypeDef *)0x1000)
#define NFC_CS_Pin           ((uint16_t)0x0001)
#define SI4463_CS_GPIO_Port  ((GPIO_TypeDef *)0x2000)
#define SI4463_CS_Pin        ((uint16_t)0x0400)

#define SPI2_IRQn            ((IRQn_Type)0)

#define M1_LOG_E(tag, ...)   do { (void)(tag); } while (0)

#endif /* STUB_MAIN_H_ */
