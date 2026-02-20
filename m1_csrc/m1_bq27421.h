/* See COPYING.txt for license details. */

/*
 * m1_bq27421.c
 *
 * Driver for Texas Instruments BQ27421
 * 
 * Portions of this implementation are based on:
 * https://github.com/svcguy/lib-BQ27421
 *
 * Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 *
 * Modifications:
 * Copyright (C) 2026 Monstatek
 */

#ifndef __BQ27421_H
#define __BQ27421_H
#ifdef __cplusplus
 extern "C" {
#endif

#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "m1_types_def.h"

#define HAL_I2C_INSTANCE                    hi2c1
#define BQ27421_DELAY                       1

#define BQ27421_I2C_ADDRESS                 0xAA

#define BQ27421_CONTROL_LOW                 0x00
#define BQ27421_CONTROL_HIGH                0x01
#define BQ27421_TEMP_LOW                    0x02
#define BQ27421_TEMP_HIGH                   0x03
#define BQ27421_VOLTAGE_LOW                 0x04
#define BQ27421_VOLTAGE_HIGH                0x05
#define BQ27421_FLAGS_LOW                   0x06
#define BQ27421_FLAGS_HIGH                  0x07
#define BQ27421_NOM_AVAILABLE_CAP_LOW       0x08
#define BQ27421_NOM_AVAILABLE_CAP_HIGH      0x09
#define BQ27421_FULL_AVAILABLE_CAP_LOW      0x0A
#define BQ27421_FULL_AVAILABLE_CAP_HIGH     0x0B
#define BQ27421_REMAINING_CAP_LOW           0x0C
#define BQ27421_REMAINING_CAP_HIGH          0x0D
#define BQ27421_FULL_CHARGE_CAP_LOW         0x0E
#define BQ27421_FULL_CHARGE_CAP_HIGH        0x0F
#define BQ27421_AVG_CURRENT_LOW             0x10
#define BQ27421_AVG_CURRENT_HIGH            0x11
#define BQ27421_STANDBY_CURRENT_LOW         0x12
#define BQ27421_STANDBY_CURRENT_HIGH        0x13
#define BQ27421_MAX_LOAD_CURRENT_LOW        0x14
#define BQ27421_MAX_LOAD_CURRENT_HIGH       0x15
#define BQ27421_AVG_POWER_LOW               0x18
#define BQ27421_AVG_POWER_HIGH              0x19
#define BQ27421_STATE_OF_CHARGE_LOW         0x1C
#define BQ27421_STATE_OF_CHARGE_HIGH        0x1D
#define BQ27421_INT_TEMP_LOW                0x1E
#define BQ27421_INT_TEMP_HIGH               0x1F
#define BQ27421_STATE_OF_HEALTH_LOW         0x20
#define BQ27421_STATE_OF_HEALTH_HIGH        0x21
#define BQ27421_REMAINING_CAP_UNFILT_LOW    0x28
#define BQ27421_REMAINING_CAP_UNFILT_HIGH   0x29
#define BQ27421_REMAINING_CAP_FILT_LOW      0x2A
#define BQ27421_REMAINING_CAP_FILT_HIGH     0x2B
#define BQ27421_FULL_CHARGE_UNFILT_CAP_LOW  0x2C
#define BQ27421_FULL_CHARGE_UNFILT_CAP_HIGH 0x2D
#define BQ27421_FULL_CHARGE_FILT_CAP_LOW    0x2E
#define BQ27421_FULL_CHARGE_FILT_CAP_HIGH   0x2F
#define BQ27421_STATE_OF_CHARGE_UNFILT_LOW  0x30
#define BQ27421_STATE_OF_CHARGE_UNFILT_HIGH 0x31
#define BQ27421_OPCONFIG_LOW                0x3A
#define BQ27421_OPCONFIG_HIGH               0x3B
#define BQ27421_DESIGN_CAP_LOW              0x3C
#define BQ27421_DESIGN_CAP_HIGH             0x3D
#define BQ27421_DATA_CLASS                  0x3E
#define BQ27421_DATA_BLOCK                  0x3F
#define BQ27421_BLOCK_DATA_START            0x40
#define BQ27421_BLOCK_DATA_END              0x5F
#define BQ27421_BLOCK_DATA_CHECKSUM         0x60
#define BQ27421_BLOCK_DATA_CONTROL          0x61

#define BQ27421_CONTROL_STATUS              0x0000
#define BQ27421_CONTROL_DEVICE_TYPE         0x0001
#define BQ27421_CONTROL_FW_VERSION          0x0002
#define BQ27421_CONTROL_DM_CODE             0x0004
#define BQ27421_CONTROL_PREV_MACWRITE       0x0007
#define BQ27421_CONTROL_CHEM_ID             0x0008
#define BQ27421_CONTROL_BAT_INSERT          0x000C
#define BQ27421_CONTROL_BAT_REMOVE          0x000D
#define BQ27421_CONTROL_SET_HIBERNATE       0x0011
#define BQ27421_CONTROL_CLEAR_HIBERNATE     0x0012
#define BQ27421_CONTROL_SET_CFGUPDATE       0x0013
#define BQ27421_CONTROL_SHUTDOWN_ENABLE     0x001B
#define BQ27421_CONTROL_SHUTDOWN            0x001C
#define BQ27421_CONTROL_SEALED              0x0020
#define BQ27421_CONTROL_TOGGLE_GPOUT        0x0023
#define BQ27421_CONTROL_RESET               0x0041
#define BQ27421_CONTROL_SOFT_RESET          0x0042
#define BQ27421_CONTROL_EXIT_CFGUPDATE      0x0043
#define BQ27421_CONTROL_EXIT_RESIM          0x0044
#define BQ27421_CONTROL_UNSEAL              0x8000

 ///////////////////////////////////////////
 // Control Status Word - Bit Definitions //
 ///////////////////////////////////////////
 // Bit positions for the 16-bit data of CONTROL_STATUS.
 // CONTROL_STATUS instructs the fuel gauge to return status information to
 // Control() addresses 0x00 and 0x01. The read-only status word contains status
 // bits that are set or cleared either automatically as conditions warrant or
 // through using specified subcommands.
 #define BQ27421_STATUS_SHUTDOWNEN	(1<<15)
 #define BQ27421_STATUS_WDRESET		(1<<14)
 #define BQ27421_STATUS_SS			(1<<13)
 #define BQ27421_STATUS_CALMODE		(1<<12)
 #define BQ27421_STATUS_CCA			(1<<11)
 #define BQ27421_STATUS_BCA			(1<<10)
 #define BQ27421_STATUS_QMAX_UP		(1<<9)
 #define BQ27421_STATUS_RES_UP		(1<<8)
 #define BQ27421_STATUS_INITCOMP	(1<<7)
 #define BQ27421_STATUS_SLEEP		(1<<4)
 #define BQ27421_STATUS_LDMD		(1<<3)
 #define BQ27421_STATUS_RUP_DIS		(1<<2)
 #define BQ27421_STATUS_VOK			(1<<1)

 ////////////////////////////////////
 // Flag Command - Bit Definitions //
 ////////////////////////////////////
 // Bit positions for the 16-bit data of Flags()
 // This read-word function returns the contents of the fuel gauging status
 // register, depicting the current operating status.
 #define BQ27421_FLAG_OT			(1<<15)
 #define BQ27421_FLAG_UT			(1<<14)
 //#define BQ27421_FLAG_RSVD		(1<<13)
 //#define BQ27421_FLAG_RSVD		(1<<12)
 //#define BQ27421_FLAG_RSVD		(1<<11)
 //#define BQ27421_FLAG_RSVD		(1<<10)
 #define BQ27421_FLAG_FC			(1<<9)
 #define BQ27421_FLAG_CHG			(1<<8)
 #define BQ27421_FLAG_OCVTAKEN		(1<<7)
 #define BQ27421_FLAG_DOD_CRRCT		(1<<6)
 #define BQ27421_FLAG_ITPOR			(1<<5)
 #define BQ27421_FLAG_CFGUPMODE		(1<<4)
 #define BQ27421_FLAG_BAT_DET		(1<<3)
 #define BQ27421_FLAG_SOC1			(1<<2)
 #define BQ27421_FLAG_SOCF			(1<<1)
 #define BQ27421_FLAG_DSG			(1<<0)

 ////////////////////////////
 // Extended Data Commands //
 ////////////////////////////
 // Extended data commands offer additional functionality beyond the standard
 // set of commands. They are used in the same manner; however, unlike standard
 // commands, extended commands are not limited to 2-byte words.
 #define BQ27421_EXTENDED_DATACLASS	0x3E // DataClass()
 #define BQ27421_EXTENDED_DATABLOCK	0x3F // DataBlock()
 #define BQ27421_EXTENDED_BLOCKDATA	0x40 // BlockData()
 #define BQ27421_EXTENDED_CHECKSUM	0x60 // BlockDataCheckSum()
 #define BQ27421_EXTENDED_CONTROL	0x61 // BlockDataControl()

 ////////////////////////////////////////
 // Configuration Class, Subclass ID's //
 ////////////////////////////////////////
 // To access a subclass of the extended data, set the DataClass() function
 // with one of these values.
 // Configuration Classes
 #define BQ27421_ID_SAFETY			2   // Safety
 #define BQ27421_ID_CHG_TERMINATION	36  // Charge Termination
 #define BQ27421_ID_CONFIG_DATA		48  // Data
 #define BQ27421_ID_DISCHARGE		49  // Discharge
 #define BQ27421_ID_REGISTERS		64  // Registers
 #define BQ27421_ID_POWER			68  // Power
 // Gas Gauging Classes
 #define BQ27421_ID_IT_CFG			80  // IT Cfg
 #define BQ27421_ID_CURRENT_THRESH	81  // Current Thresholds
 #define BQ27421_ID_STATE			82  // State
 // Ra Tables Classes
 #define BQ27421_ID_R_A_RAM			89  // R_a RAM
 // Chemistry Info Classes
// #define BQ27421_ID_CHEM_DATA     109 // Chem Data
 // Calibration Classes
 #define BQ27421_ID_CALIB_DATA		104 // Data
 #define BQ27421_ID_CC_CAL			105 // CC Cal
 #define BQ27421_ID_CURRENT			107 // Current
 // Security Classes
 #define BQ27421_ID_CODES			112 // Codes


 /////////////////////////////////////////
 // OpConfig State - Bit Definitions //
 /////////////////////////////////////////
 #define BQ27421_STATE_OFFSET_QMAXCELL0	 		(0)
 #define BQ27421_STATE_OFFSET_UPDATE_STATUS		(2)
 #define BQ27421_STATE_OFFSET_RESERVE_CAP_mAH	(3)
 #define BQ27421_STATE_OFFSET_LOAD_SELET_MODE	(4)
 #define BQ27421_STATE_OFFSET_Q_INVALID_MAXV	(6)
 #define BQ27421_STATE_OFFSET_Q_INVALID_MINV	(8)
 #define BQ27421_STATE_OFFSET_DESIGN_CAPACITY	(10)
 #define BQ27421_STATE_OFFSET_DESIGN_ENERGY		(12)
 #define BQ27421_STATE_OFFSET_DEFAULT_DESIGN_CAP	(14)
 #define BQ27421_STATE_OFFSET_TERMINATE_VOLTAGE	(16)
 #define BQ27421_STATE_OFFSET_T_RISE			(22)
 #define BQ27421_STATE_OFFSET_T_TIME_CONST		(24)
 #define BQ27421_STATE_OFFSET_SOCI_DELTA		(26)
 #define BQ27421_STATE_OFFSET_TAPER_RATE		(27)
 #define BQ27421_STATE_OFFSET_TAPER_VOLTAGE		(29)
 #define BQ27421_STATE_OFFSET_SLEEP_CURRENT		(31)
 #define BQ27421_STATE_OFFSET_V_AT_CHG_TERM		(33)
 #define BQ27421_STATE_OFFSET_AVG_I_LAST_RUN	(35)
 #define BQ27421_STATE_OFFSET_AVG_P_LAST_RUN	(37)
 #define BQ27421_STATE_OFFSET_DELTA_VOLTAGE		(39)
 /////////////////////////////////////////
 // OpConfig Register - Bit Definitions //
 /////////////////////////////////////////
 #define BQ27421_REGISTERS_OFFSET_OPCONFIG	 (0)
 #define BQ27421_REGISTERS_OFFSET_OPCONFIGB	 (2)
 // Bit positions of the OpConfig Register
 #define BQ27421_OPCONFIG_BIE        (1<<13)
 #define BQ27421_OPCONFIG_BI_PU_EN	 (1<<12)
 #define BQ27421_OPCONFIG_GPIOPOL    (1<<11)
 //#define BQ27421_OPCONFIG_RS_FCT_STP (1<<6)
 #define BQ27421_OPCONFIG_SLEEP      (1<<5)
 #define BQ27421_OPCONFIG_RMFCC      (1<<4)
 //#define BQ27421_OPCONFIG_FASTCNV_EN (1<<3)
 #define BQ27421_OPCONFIG_BATLOWEN   (1<<2)
 #define BQ27421_OPCONFIG_TEMPS      (1<<0)

typedef struct
{
    uint16_t    voltage_mV;
    int16_t     current_mA;
    double      temp_degC;
    uint16_t    soc_percent;
    uint16_t    soh_percent;
    uint16_t    designCapacity_mAh;
    uint16_t    remainingCapacity_mAh;
    uint16_t    fullChargeCapacity_mAh;

    uint8_t     soh_state;
    uint16_t    flags;
    uint16_t    status;

    bool        isCritical;
    bool        isLow;
    bool        isFull;
    bool        isCharging;
    bool        isDischarging;
} bq27421_info;

// Parameters for the soc() function
typedef enum {
	FILTERED,  // State of Charge Filtered (DEFAULT)
	UNFILTERED // State of Charge Unfiltered
} soc_measure;

// Parameters for the soh() function
typedef enum {
	PERCENT,  // State of Health Percentage (DEFAULT)
	SOH_STAT  // State of Health Status Bits
} soh_measure;

bool bq27421_init( uint16_t designCapacity_mAh, uint16_t terminateVoltage_mV, uint16_t taperCurrent_mA );
bool bq27421_update( bq27421_info *battery );
bool bq27421_readDeviceType( uint16_t *deviceType );
bool bq27421_readDeviceFWver( uint16_t *deviceFWver );
bool bq27421_readDesignCapacity_mAh( uint16_t *capacity_mAh );

bool bq27421_readVoltage_mV( uint16_t *voltage_mV );
bool bq27421_readTemp_degK( uint16_t *temp_degKbyTen );
bool bq27421_readAvgCurrent_mA( int16_t *avgCurrent_mA );
//bool bq27421_readStateofCharge_percent( uint16_t *soc_percent );

bool bq27421_readControlReg( uint16_t *control );
bool bq27421_readFlagsReg( uint16_t *flags );
bool bq27421_readopConfig( uint16_t *opConfig );
bool bq27421_readRemainingCapacity_mAh( uint16_t *capacity_mAh );
bool bq27421_readFullChargeCapacity_mAh( uint16_t *capacity_mAh );
//bool bq27421_readStateofHealth_percent( uint16_t *soh_percent );
bool bq27421_setHiberate(void);
bool bq27421_clearHiberate(void);

uint8_t bq27421_soh(soh_measure type);
uint16_t bq27421_soc(soc_measure type);

bool bq27421_i2c_command_write( uint8_t command, uint8_t data );
bool bq27421_i2c_command_read( uint8_t command, uint16_t *data );

bool bq27421_i2c_control_write( uint16_t subcommand );
bool bq27421_i2c_control_read( uint16_t subcommand, uint16_t *data );
bool bq27421_i2c_write_data_block( uint8_t offset, uint8_t *data, uint8_t bytes );
bool bq27421_i2c_read_data_block( uint8_t offset, uint8_t *data, uint8_t bytes );

#ifdef __cplusplus
}
#endif
#endif /* __BQ27421_H */
