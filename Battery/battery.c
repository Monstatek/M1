/* See COPYING.txt for license details. */

/*
*
* battery.c
*
* Battery functions for M1
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "m1_lcd.h"
#include "m1_lp5814.h"
#include "m1_bq27421.h"
#include "m1_power_ctl.h"
#include "m1_bq25896.h"
#include "m1_log_debug.h"
#include "m1_i2c.h"
#include "m1_rf_spi.h"
#include "m1_sdcard.h"
#include "m1_esp32_hal.h"
#include "uiView.h"
#include "battery.h"
#include "m1_io_defs.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"BATT"

#define VCHG_3840mV		0
#define VCHG_4096mV		1
#define VCHG_4192mV		2
#define VCHG_4208mV		3

#define ICHG_512mA		0
#define ICHG_1024mA		1
#define ICHG_1536mA		2
#define ICHG_2048mA		3
#define ICHG_2560mA		4
#define ICHG_3072mA		5

#define IPRECHG_64mA	0
#define IPRECHG_128mA	1
#define IPRECHG_256mA	2
#define IPRECHG_512mA	3

#define ITERCHG_64mA	0
#define ITERCHG_128mA	1
#define ITERCHG_256mA	2
#define ITERCHG_512mA	3

#define IINLIM_500mA	0
#define IINLIM_1000mA	1
#define IINLIM_1500mA	2
#define IINLIM_2000mA	3
#define IINLIM_2500mA	4
#define IINLIM_3000mA	5

#define VOTG_4998mV		0
#define VOTG_5062mV		1
#define VOTG_5126mV		2

#define IOTG_500mA		0
#define IOTG_750mA		1
#define IOTG_1200mA		2
#define IOTG_1650mA		3
#define IOTG_2150mA		4

#define SLEEP_SHIP_TIMEOUT_60S	0
#define SLEEP_SHIP_TIMEOUT_120S	1
#define SLEEP_SHIP_TIMEOUT_300S	2

#define BQ2589X_VCHG_DEFAULT_IDX		 VCHG_4208mV // Default index of the Charge voltage array, 4208mV
#define BQ2589X_ICHG_DEFAULT_IDX		 ICHG_1024mA // Default index of the Charge current array, 1536mA
#define BQ2589X_IPRECHG_DEFAULT_IDX		 IPRECHG_128mA // Default index of the Pre-charge current array, 128mA
#define BQ2589X_ITERCHG_DEFAULT_IDX		 ITERCHG_128mA //ITERCHG_256mA // Default index of the Termination current array, 256mA
#define BQ2589X_IINLIM_DEFAULT_IDX		 IINLIM_1500mA // Default index of the Input current array, 1500mA
#define BQ2589X_VOTG_DEFAULT_IDX		 VOTG_5062mV // Default index of the Boost voltage array, 5062mV
#define BQ2589X_IOTG_DEFAULT_IDX		 IOTG_1200mA // Default index of the Boost current array, 1200mA
#define BQ2589X_SLPTO_DEFAULT_IDX		 SLEEP_SHIP_TIMEOUT_60S // Default index of the Sleep/Ship timeout array, 60 seconds

//************************** C O N S T A N T **********************************/
static const uint16_t bq2589x_VCHG[4] = {3840, 4096, 4192, 4208}; // Charge voltage
static const uint16_t bq2589x_ICHG[6] = {512, 1024, 1536, 2048, 2560, 3072};  // Charge current
static const uint16_t bq2589x_IPRECHG[4] = {64, 128, 256, 512};  // Pre-charge current
static const uint16_t bq2589x_ITERCHG[4] = {64, 128, 256, 512};  // Termination current
static const uint16_t bq2589x_IINLIM[6] = {500, 1000, 1500, 2000, 2500, 3000};  // Input current limit
static const uint16_t bq2589x_VOTG[3] = {4998, 5062, 5126}; // Boost voltage
static const uint16_t bq2589x_IOTG[5] = {500, 750, 1200, 1650, 2150}; // Boost current
//static const uint16_t bq2589x_Sleep_TO[3] = {60, 120,300 }; // Sleep/Ship Mode timeout in seconds
//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/
S_M1_Power_Status_t power_status;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

//static void batteryThread(void *param);
void battery_service_init(void);
uint32_t battery_power_status_get(S_M1_Power_Status_t *pSystemPowerStatus);
void battery_status_update(void);
static void bq25896_set_default_config(void);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief Initializes the battery
  * @param
  * @retval
  */
/*============================================================================*/
void battery_service_init(void)
{
	bq_25896_init();
	bq25896_set_default_config();
	bq27421_init(); //bq27421_init( 2100, 3200, 240); // Capacity(mA), terminate voltage(mV), taper current(mA, may be terminal current)

	app_soh_service_init();
	power_status.battery_health = 100;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void bq25896_set_default_config(void)
{
	//bq_disableWATCHDOG();
	//bq_oneShotADC();
	//bq_disableCHRG();

	// Set input current limit
	bq_setIINLIM(bq2589x_IINLIM[BQ2589X_IINLIM_DEFAULT_IDX]);
	// Set pre-charge current
	bq_setIPRECHG(bq2589x_IPRECHG[BQ2589X_IPRECHG_DEFAULT_IDX]);
	// Set termination current
	bq_setITERM(bq2589x_ITERCHG[BQ2589X_ITERCHG_DEFAULT_IDX]);
	// Set initial charge voltage
	bq_setVREG(bq2589x_VCHG[BQ2589X_VCHG_DEFAULT_IDX]);
	// Set initial charge current
	bq_setICHG(bq2589x_ICHG[BQ2589X_ICHG_DEFAULT_IDX]);
	// Set initial OTG voltage
	bq_setBOOSTV(bq2589x_VOTG[BQ2589X_VOTG_DEFAULT_IDX]);
	// Set initial OTG current
	bq_setBOOST_LIM(bq2589x_IOTG[BQ2589X_IOTG_DEFAULT_IDX]);

	bq_disableILIMPin();
} // void power_init(void)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint32_t battery_power_status_get(S_M1_Power_Status_t *pSystemPowerStatus)
{
	*pSystemPowerStatus = power_status; //data copy

	return TRUE;
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void battery_access_disable(void)
{
	power_status.enable = false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void battery_access_enable(void)
{
	power_status.enable = true;
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void battery_status_update(void)
{
	bq27421_info bat_info={0,};

	if (power_status.enable==false)
		return;

	bq27421_update(&bat_info);
	////////////////////////////////////////
	// Battery Charger fault
	// 0 Normal, 1: Input, 2: Thermal Shutdown, 3: Safety Timer Expiration
	power_status.fault = bq_getCHRG_FAULT();

	// 0: no charge 1: pre-charge 2: fast charge 3: charge complete
	power_status.stat = bq_getCHRG_STAT();

	power_status.consumption_current = bat_info.current_mA;
	//power_status.battery_voltage = bat_info.voltage_mV / 1000.0f + 0.05;
	power_status.battery_voltage = bat_info.voltage_mV;
	power_status.battery_temp = (uint8_t)bat_info.temp_degC;
	power_status.soh_state = bat_info.soh_state;

	power_status.flags = bat_info.flags;
	power_status.status = bat_info.status;

	power_status.isCritical = bat_info.isCritical;
	power_status.isLow = bat_info.isLow;
	power_status.isFull = bat_info.isFull;

	power_status.isCharging = bat_info.isCharging;
	power_status.isDischarging = bat_info.isDischarging;

	if (power_status.status & BQ27421_STATUS_INITCOMP)
		power_status.battery_level = bat_info.soc_percent;

	//if (power_status.soh_state==3)
	{
		power_status.battery_health = bat_info.soh_percent;
	}

	power_status.fullChargeCapacity_mAh = bat_info.fullChargeCapacity_mAh;
	power_status.remainingCapacity_mAh = bat_info.remainingCapacity_mAh;
	power_status.designCapacity_mAh = bat_info.designCapacity_mAh;
	////////////////////////////////////////
	// Battery Charger
	// Charge Voltage, Bus voltage
	bq_startADC();
	bq_oneShotADC();

	power_status.charge_voltage = bq_getVBUSV();

	// Charge Current
	power_status.charge_current = bq_getICHGR();

	// battery temperature
	//power_status.battery_temp = bq_getTSPCT();

	bq_stopADC();

	app_soh_service_process();

}
