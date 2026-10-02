
/* See COPYING.txt for license details. */

/*
 * soh_filter.h
 *
 */

#ifndef SOH_FILTER_H_
#define SOH_FILTER_H_

void app_soh_service_init(void);
void app_soh_service_process(void);
uint8_t app_get_filtered_soh_ui(void);

#endif /* SOH_FILTER_H_ */
