/* Host-test stub: the FreeRTOS semaphore API m1_rf_spi.c actually calls. */
#ifndef STUB_SEMPHR_H_
#define STUB_SEMPHR_H_

#include "app_freertos.h"

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, uint32_t timeout);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);
void              vSemaphoreDelete(SemaphoreHandle_t s);

#endif /* STUB_SEMPHR_H_ */
