/* Host-test stub: the FreeRTOS surface m1_rf_spi.c actually uses. */
#ifndef STUB_APP_FREERTOS_H_
#define STUB_APP_FREERTOS_H_

#include <stdint.h>

typedef long          BaseType_t;
typedef void         *SemaphoreHandle_t;

#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define portMAX_DELAY  ((uint32_t)0xFFFFFFFFUL)

#endif /* STUB_APP_FREERTOS_H_ */
