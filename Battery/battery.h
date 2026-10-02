/* See COPYING.txt for license details. */

/*
 * battery.h
 *
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include "soh_filter.h"

#define DELAY_BEFORE_POWER_REBOOT		1000 // ms

typedef enum
{
	POWER_CTRL_OPT_SHUTDOWN = 0,
	POWER_CTRL_OPT_REBOOT
} S_M1_Power_Ctrl_t;

typedef struct
{
	uint8_t 	stat;					// Charge / Dis-Charge
	uint8_t     fault;					// 0 Normal, 1: Input, 2: Thermal Shutdown, 3: Safety Timer Expiration
	float 		charge_voltage;			// bus voltage
	uint16_t	charge_current;			// 0 ~ xxxx mA

	uint8_t		battery_level;			// 0 ~ 100%
	uint8_t		battery_temp;			// 0 ~ 80 deg
	float 		battery_voltage;		// battery voltage
	uint8_t		battery_health;			// 0 ~ 100 %
	int			consumption_current;	// 0 ~ xxx mA

    bool        isCritical;
    bool        isLow;
    bool        isFull;
    bool		isDischarging;
    bool		isCharging;

	uint8_t     soh_state;
	uint16_t    flags;
	uint16_t    status;

	bool		enable;

    uint16_t    designCapacity_mAh;
    uint16_t    remainingCapacity_mAh;
    uint16_t    fullChargeCapacity_mAh;
} S_M1_Power_Status_t;

//extern S_M1_Power_Status_t power_status;
uint32_t battery_power_status_get(S_M1_Power_Status_t *pSystemPowerStatus);
void battery_status_update(void);
void battery_service_init(void);

void battery_access_disable(void);
void battery_access_enable(void);

#endif /* BATTERY_H_ */
