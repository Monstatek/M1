/* See COPYING.txt for license details. */

/*
 * m1_bq25896.c
 *
 * Driver for Texas Instruments BQ25896
 *
 * Portions of this implementation were developed with reference to:
 * https://github.com/VRaktion/mbed-BQ25896
 *
 * Copyright (c) the respective authors of the original project.
 *
 * Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 *
 * Modifications and hardware adaptation:
 * Copyright (C) 2026 MonstaTek
 */
/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_bq25896.h"
#include "m1_i2c.h"
#include "m1_power_ctl.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*** REG00 ***/

/*============================================================================*/
/*
 * Description: enable high impedance mode (low power)
 *
 */
/*============================================================================*/
uint8_t bq_enableHIZ(void)
{
	uint8_t reg00 = bq_readRegister(REG00_ADDR);
	bq_setBit(&reg00, REG00_EN_HIZ);

	return bq_setRegister(REG00_ADDR, reg00);
} // uint8_t bq_enableHIZ(void)


/*============================================================================*/
/*
 * Description: disable high impedance mode (low power)
 *
 */
/*============================================================================*/
uint8_t bq_disableHIZ(void)
{
	uint8_t reg00 = bq_readRegister(REG00_ADDR);
	bq_unsetBit(&reg00, REG00_EN_HIZ);

	return bq_setRegister(REG00_ADDR, reg00);
} // uint8_t bq_disableHIZ(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_enableILIMPin(void)
{
	uint8_t reg00 = bq_readRegister(REG00_ADDR);
	bq_setBit(&reg00, REG00_EN_ILIM);

	return bq_setRegister(REG00_ADDR, reg00);
} // uint8_t bq_enableILIMPin(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableILIMPin(void)
{
	uint8_t reg00 = bq_readRegister(REG00_ADDR);
	bq_unsetBit(&reg00, REG00_EN_ILIM);
	return bq_setRegister(REG00_ADDR, reg00);
} // uint8_t bq_disableILIMPin(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_setMaxILIM(void)
{
	uint8_t reg00 = bq_readRegister(REG00_ADDR);
	bq_setBit(&reg00, REG00_IINLIM);

	return bq_setRegister(REG00_ADDR, reg00);
} // uint8_t bq_setMaxILIM(void)


/*============================================================================*/
/*
 * Description: set in mA
 *
 */
/*============================================================================*/
uint8_t bq_setIINLIM(int i)
{
	if (i <= BQ2589X_IINLIM_MAX)
	{
		uint8_t reg00 = bq_readRegister(REG00_ADDR);
		uint8_t mask = (uint8_t) ((i-100) / 50);
		bq_unsetBit(&reg00, REG00_IINLIM);
		bq_setBit(&reg00, mask);
		return bq_setRegister(REG00_ADDR, reg00);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setIINLIM(int i)


/*** REG01 ***/


/*============================================================================*/
/*
 * Description: Input Voltage Limit Offset in mV
 *
 */
/*============================================================================*/
uint8_t bq_setVINDPM_OS(int val)
{
	if(val <= 3100)
	{
		uint8_t reg = bq_readRegister(REG01_ADDR);
		bq_unsetBit(&reg, REG01_VINDPM_OS);
		bq_setBit(&reg, (uint8_t) (val / 100));
		return bq_setRegister(REG01_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setVINDPM_OS(int val)


/*** REG02 ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_startADC(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_setBit(&reg, REG02_CONV_START);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_startADC(void)



/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_oneShotADC(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_unsetBit(&reg, REG02_CONV_RATE);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_oneShotADC(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_oneSecADC(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_setBit(&reg, REG02_CONV_RATE);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_oneSecADC(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_stopADC(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_unsetBit(&reg, REG02_CONV_RATE);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_stopADC(void)


/*============================================================================*/
/*
 * Description: Force Input Detection
 *
 */
/*============================================================================*/
uint8_t bq_setFORCE_DPDM(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_setBit(&reg, REG02_FORCE_DPDM);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_setFORCE_DPDM(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_unsetFORCE_DPDM(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_unsetBit(&reg, REG02_FORCE_DPDM);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_unsetFORCE_DPDM(void)


/*============================================================================*/
/*
 * Description: Automatic Input Detection Enable
 *
 */
/*============================================================================*/
uint8_t bq_enableAUTO_DPDM(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_setBit(&reg, REG02_AUTO_DPDM_EN);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_enableAUTO_DPDM(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableAUTO_DPDM(void)
{
	uint8_t reg = bq_readRegister(REG02_ADDR);
	bq_unsetBit(&reg, REG02_AUTO_DPDM_EN);

	return bq_setRegister(REG02_ADDR, reg);
} // uint8_t bq_disableAUTO_DPDM(void)


/*** REG03 ***/


/*============================================================================*/
/*
 * Description: Battery Load enable
 *
 */
/*============================================================================*/
uint8_t bq_enableIBATLOAD(void)
{
	uint8_t reg = bq_readRegister(REG03_ADDR);
	bq_setBit(&reg, REG03_BAT_LOADEN);

	return bq_setRegister(REG03_ADDR, reg);
} // uint8_t bq_enableIBATLOAD(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableIBATLOAD(void)
{
	uint8_t reg = bq_readRegister(REG03_ADDR);
	bq_unsetBit(&reg, REG03_BAT_LOADEN);

	return bq_setRegister(REG03_ADDR, reg);
} // uint8_t bq_disableIBATLOAD(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_enableOTG(void)
{
	uint8_t reg03 = bq_readRegister(REG03_ADDR);
	bq_setBit(&reg03, REG03_OTG_CONFIG);

	return bq_setRegister(REG03_ADDR, reg03);
} // uint8_t bq_enableOTG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableOTG(void)
{
	uint8_t reg03 = bq_readRegister(REG03_ADDR);
	bq_unsetBit(&reg03, REG03_OTG_CONFIG);

	return bq_setRegister(REG03_ADDR, reg03);
} // uint8_t bq_disableOTG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
bool bq_getOTG(void)
{
	int reg = bq_readRegister(REG03_ADDR);

	return (bool) (reg & REG03_OTG_CONFIG);
} // bool bq_getOTG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_enableCHRG(void)
{
	uint8_t reg03 = bq_readRegister(REG03_ADDR);
	bq_setBit(&reg03, REG03_CHG_CONFIG);

	return bq_setRegister(REG03_ADDR, reg03);
} // uint8_t bq_enableCHRG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableCHRG(void)
{
	uint8_t reg03 = bq_readRegister(REG03_ADDR);
	bq_unsetBit(&reg03, REG03_CHG_CONFIG);

	return bq_setRegister(REG03_ADDR, reg03);
} // uint8_t bq_disableCHRG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
bool bq_getCHRG(void)
{
	int reg = bq_readRegister(REG03_ADDR);

	return (bool) (reg & REG03_CHG_CONFIG);
} // bool bq_getCHRG(void)


/*** REG04 ***/


/*============================================================================*/
/*
 * Description: Current pulse control Enable
 *
 */
/*============================================================================*/
uint8_t bq_enablePUMPX(void)
{
	uint8_t reg = bq_readRegister(REG04_ADDR);
	bq_setBit(&reg, REG04_EN_PUMPX);

	return bq_setRegister(REG04_ADDR, reg);
} // uint8_t bq_enablePUMPX(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disablePUMPX(void)
{
	uint8_t reg = bq_readRegister(REG04_ADDR);
	bq_unsetBit(&reg, REG04_EN_PUMPX);

	return bq_setRegister(REG04_ADDR, reg);
} // uint8_t bq_disablePUMPX(void)


/*============================================================================*/
/*
 * Description: Fast Charge Current Limit in mA
 *
 */
/*============================================================================*/
uint8_t bq_setICHG(int val)
{
	if(val <= BQ2589X_IREG_MAX)
	{
		uint8_t reg = bq_readRegister(REG04_ADDR);
		bq_unsetBit(&reg, REG04_ICHG);
		bq_setBit(&reg, (uint8_t) (val / 64));

		return bq_setRegister(REG04_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setICHG(int bq)



/*============================================================================*/
/*
 * Description: //charging current limit in mA
 *
 */
/*============================================================================*/
int bq_getICHG(void)
{
	return (bq_readRegister(REG04_ADDR) & REG04_ICHG) * 64;
} // int bq_bq_getICHG(void)


/*** REG05 ***/


/*============================================================================*/
/*
 * Description: Precharge Current Limit in mA
 *
 */
/*============================================================================*/
uint8_t bq_setIPRECHG(int val)
{
	if(val <= BQ2589X_IPRECHG_MAX)
	{
		uint8_t reg = bq_readRegister(REG05_ADDR);
		bq_unsetBit(&reg, REG05_IPRECHG);
		bq_setBit(&reg, (uint8_t) ((val - 64) / 64) << 4);
		return bq_setRegister(REG05_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setIPRECHG(int val)


/*============================================================================*/
/*
 * Description: Termination Current Limit
 *
 */
/*============================================================================*/
uint8_t bq_setITERM(int val)
{
	if(val <= 1024)
	{
		uint8_t reg = bq_readRegister(REG05_ADDR);
		bq_unsetBit(&reg, REG05_ITERM);
		bq_setBit(&reg, (uint8_t) ((val - 64) / 64));
		return bq_setRegister(REG05_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setITERM(int val)


/*** REG06 ***/


/*============================================================================*/
/*
 * Description: Charge Voltage Limit in mV
 *
 */
/*============================================================================*/
uint8_t bq_setVREG(int val)
{
	if(val >= BQ2589X_VREG_MIN && val <= BQ2589X_VREG_MAX)
	{
		uint8_t reg = bq_readRegister(REG06_ADDR);
		bq_unsetBit(&reg, REG06_VREG);
		bq_setBit(&reg, (uint8_t) (((val - BQ2589X_VREG_MIN) / 16)<<2));
		return bq_setRegister(REG06_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setVREG(int val)



/*** REG07 ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableWATCHDOG(void)
{
	uint8_t reg = bq_readRegister(REG07_ADDR);
	bq_unsetBit(&reg, REG07_WATCHDOG);

	return bq_setRegister(REG07_ADDR, reg);
} // uint8_t bq_disableWATCHDOG(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_WATCHDOG40s(void)
{
	uint8_t reg = bq_readRegister(REG07_ADDR);
	bq_unsetBit(&reg, REG07_WATCHDOG);
	bq_setBit(&reg, 0x01 << 4);

	return bq_setRegister(REG07_ADDR, reg);
} // uint8_t bq_WATCHDOG40s(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_WATCHDOG80s(void)
{
	uint8_t reg = bq_readRegister(REG07_ADDR);
	bq_unsetBit(&reg, REG07_WATCHDOG);
	bq_setBit(&reg, 0x02 << 4);

	return bq_setRegister(REG07_ADDR, reg);
} // uint8_t bq_WATCHDOG80s(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_WATCHDOG160s(void)
{
	uint8_t reg = bq_readRegister(REG07_ADDR);
	bq_unsetBit(&reg, REG07_WATCHDOG);
	bq_setBit(&reg, 0x03 << 4);

	return bq_setRegister(REG07_ADDR, reg);
} // uint8_t bq_WATCHDOG160s(void)


/*** REG0A ***/


/*============================================================================*/
/*
 * Description: Boost Mode Voltage Regulation in mV
 *
 */
/*============================================================================*/
uint8_t bq_setBOOSTV(int val)
{
	if(val >= BQ2589X_BOOSTV_MIN && val <= BQ2589X_BOOSTV_MAX)
	{
		uint8_t reg = bq_readRegister(REG0A_ADDR);
		bq_unsetBit(&reg, REG0A_BOOSTV);
		bq_setBit(&reg, (uint8_t) (((val - BQ2589X_BOOSTV_MIN) / 64)<<4));
		return bq_setRegister(REG0A_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setBOOSTV(int val)


/*============================================================================*/
/*
 * Description: Boost Mode Current Limit
 *
 */
/*============================================================================*/
uint8_t bq_setBOOST_LIM(int val)
{
    uint8_t ilim, reg;

    if (val==500)
        ilim = BQ2589X_BOOSTI_500;
    else if (val==750)
    	ilim = BQ2589X_BOOSTI_750;
    else if (val==1200)
    	ilim = BQ2589X_BOOSTI_1200;
    else if (val==1400)
    	ilim = BQ2589X_BOOSTI_1400;
    else if (val==1650)
    	ilim = BQ2589X_BOOSTI_1650;
    else if (val==1875)
    	ilim = BQ2589X_BOOSTI_1875;
    else if (val==2150)
    	ilim = BQ2589X_BOOSTI_2150;
    else
    	ilim = BQ2589X_BOOSTI_1400;

    reg = bq_readRegister(REG0A_ADDR);
    bq_unsetBit(&reg, REG0A_BOOST_LIM);
    bq_setBit(&reg, ilim);

    return bq_setRegister(REG0A_ADDR, reg);

} // uint8_t bq_setBOOST_LIM(int val)


/*** REG0B ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getVBUS_STAT(void)
{
	uint8_t reg = bq_readRegister(REGISTER0B_ADDR);

	return (reg & REG0B_VBUS_STAT) >> 5;
} // int bq_getVBUS_STAT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getCHRG_STAT(void)
{
	uint8_t reg = bq_readRegister(REGISTER0B_ADDR);

	return (reg & REG0B_CHRG_STAT) >> 3;
} // int bq_getCHRG_STAT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getPG_STAT(void)
{
	uint8_t reg = bq_readRegister(REGISTER0B_ADDR);

	return (reg & REG0B_PG_STAT) >> 2;
} // int bq_getPG_STAT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getVSYS_STAT(void)
{
	uint8_t reg = bq_readRegister(REGISTER0B_ADDR);

	return reg & REG0B_VSYS_STAT;
} // int bq_getVSYS_STAT(void)


/*** REG0C ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getFAULT(void)
{
	return bq_readRegister(REG0C_ADDR);
} // int bq_getFAULT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getWATCHDOG_FAULT(void)
{
	return bq_getFAULT() & REG0C_WATCHDOG_FAULT >> 7;
} // int bq_getWATCHDOG_FAULT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getBOOST_FAULT(void)
{
	return bq_getFAULT() & REG0C_BOOST_FAULT >> 6;
} // int bq_getBOOST_FAULT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getBAT_FAULT(void)
{
	return bq_getFAULT() & REG0C_BAT_FAULT >> 3;
} // int bq_getBAT_FAULT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getCHRG_FAULT(void)
{
	return bq_getFAULT() & REG0C_CHRG_FAULT >> 4;
} // int bq_getCHRG_FAULT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getNTC_FAULT(void)
{
	return bq_getFAULT() & REG0C_NTC_FAULT;
} // int bq_getNTC_FAULT(void)


/*** REG0D ***/


/*============================================================================*/
/*
 * Description: VINDPM Threshold Setting Method absolute
 *
 */
/*============================================================================*/
uint8_t bq_enableFORCE_VINDPM(void)
{
	uint8_t reg = bq_readRegister(REG0D_ADDR);
	bq_setBit(&reg, REG0D_FORCE_VINDPM);

	return bq_setRegister(REG0D_ADDR, reg);
} // uint8_t bq_enableFORCE_VINDPM(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_disableFORCE_VINDPM(void)
{
	uint8_t reg = bq_readRegister(REG0D_ADDR);
	bq_unsetBit(&reg, REG0D_FORCE_VINDPM);

	return bq_setRegister(REG0D_ADDR, reg);
} // uint8_t bq_disableFORCE_VINDPM(void)


/*============================================================================*/
/*
 * Description:
 * BATFET Disable Mode (Shipping Mode)
 * BATFET Enable (Exit Shipping Mode)
 */
/*============================================================================*/
static uint8_t bq_enableBATFET(void)
{
	uint8_t reg = bq_readRegister(REG09_ADDR);
	bq_setBit(&reg, REG09_BATFET_DIS | REG09_BATFET_DLY);
	bq_unsetBit(&reg,REG09_BATFET_RST_EN);

	return bq_setRegister(REG09_ADDR, reg);
} // uint8_t bq_enableFORCE_VINDPM(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
static uint8_t bq_disableBATFET(void)
{
	uint8_t reg = bq_readRegister(REG09_ADDR);
	bq_unsetBit(&reg, REG09_BATFET_DIS | REG09_BATFET_DLY | REG09_BATFET_RST_EN);

	return bq_setRegister(REG09_ADDR, reg);
} // uint8_t bq_disableFORCE_VINDPM(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
uint8_t bq_shipMode(uint8_t mode)
{
	if(mode == ENABLE)
		return bq_enableBATFET();
	else if(mode == DISABLE)
		return bq_disableBATFET();

	return HAL_ERROR;
}

/*============================================================================*/
/*
 * Description: Absolute VINDPM Threshold in mV
 *
 */
/*============================================================================*/
uint8_t bq_setVINDPM(int val)
{
	if(val >= 3900 && val <= 15300)
	{
		uint8_t reg = bq_readRegister(REG0D_ADDR);
		bq_unsetBit(&reg, REG0D_VINDPM);
		bq_setBit(&reg, (uint8_t) ((val - 2600) / 100));

		return bq_setRegister(REG0D_ADDR, reg);
	}
	else
	{
		return 1;
	}
} // uint8_t bq_setVINDPM(int val)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getVINDPM(void)
{
	uint8_t reg = bq_readRegister(REG0D_ADDR);

	return (int) ((reg & REG0D_VINDPM) * 100) + 2600;
} // int bq_getVINDPM(void)


/*** REG0E ***/


/*============================================================================*/
/*
 * Description: ADC conversion of Battery Voltage (VBAT)
 *
 */
/*============================================================================*/
int bq_getBATV(void)
{
	return ((int) ((bq_readRegister(REG0E_ADDR) & 0x7F))) * 20 + 2304;
} // int bq_getBATV(void)


/*** REG0F ***/


/*============================================================================*/
/*
 * Description: ADDC conversion of System Voltage (VSYS)
 *
 */
/*============================================================================*/
int bq_getSYSV(void)
{
	return ((int) bq_readRegister(REG0F_ADDR)) * 20 + 2304;
} // int bq_getSYSV(void)


/*** REG10 ***/


/*============================================================================*/
/*
 * Description: REGN in percentage
 *
 */
/*============================================================================*/
float bq_getTSPCT(void)
{
    float result = 0;

    // Linear approximation, +/- 5 C
    result = ((float) bq_readRegister(REG10_ADDR)) * 0.465 + 21;

    result = (71.0f - result) / 0.6f;
    return result;

	//return ((float) bq_readRegister(REG10_ADDR)) * 0.465 + 21;
} // float bq_getTSPCT(void)


/*** REG11 ***/


/*============================================================================*/
/*
 * Description: connected input Voltage in V .. -1 if no connection
 *
 */
/*============================================================================*/
float bq_getVBUSV(void)
{
    uint8_t reg = bq_readRegister(REG11_ADDR);
    float result = (float) (reg & REG11_VBUSV) * 0.1;

    if (result > 0)
        result += 2.6;
    return result;

} // float bq_getVBUSV(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getVBUS_GD(void)
{
	return bq_readRegister(REG11_ADDR) & REG11_VBUS_GD >> 7;
} // int bq_getVBUS_GD(void)



/*** REG12 ***/


/*============================================================================*/
/*
 * Description: charging current in mA
 *
 */
/*============================================================================*/
int bq_getICHGR(void)
{
	return ((int) bq_readRegister(REG12_ADDR)) * 50;
} // int bq_getICHGR(void)



/*** REG13 ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getVDPM_STAT(void)
{
	return bq_readRegister(REG13_ADDR) & REG13_VDPM_STAT >> 7;
} // int bq_getVDPM_STAT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getIDPM_STAT(void)
{
	return bq_readRegister(REG13_ADDR) & REG13_IDPM_STAT >> 6;
} // int bq_getIDPM_STAT(void)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_getIDPM_LIM(void)
{
	return (int)(bq_readRegister(REG13_ADDR) & REG13_IDPM_LIM) * 50 + 100;
} // int bq_getIDPM_LIM(void)


/*** REG14 ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_reset(void)
{
	uint8_t reg = bq_readRegister(REG14_ADDR);
	bq_setBit(&reg, REG14_REG_RST);

	return bq_setRegister(REG14_ADDR, reg);
} // int bq_reset(void)


/*** REG END ***/


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_setRegister(uint8_t reg, uint8_t value)
{
	S_M1_I2C_Trans_Inf i2c_inf;

	i2c_inf.dev_id = I2C_DEVICE_BQ25896;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER;
	i2c_inf.reg_address = reg;
	i2c_inf.reg_data = value;
	uint8_t stat = m1_i2c_hal_trans_req(&i2c_inf);

	return (stat==HAL_OK);
} // int bq_setRegister(uint8_t reg, uint8_t value)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
int bq_readRegister(uint8_t reg)
{
	S_M1_I2C_Trans_Inf i2c_inf;

	i2c_inf.dev_id = I2C_DEVICE_BQ25896;
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER;
	i2c_inf.reg_address = reg;
	m1_i2c_hal_trans_req(&i2c_inf);

	return i2c_inf.reg_data;
} // int bq_readRegister(uint8_t reg)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void bq_setBit(uint8_t *reg, uint8_t mask)
{
	*reg |= mask;
} // void bq_setBit(uint8_t *reg, uint8_t mask)


/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void bq_unsetBit(uint8_t *reg, uint8_t mask)
{
	*reg &= ~mask;
} // void bq_unsetBit(uint8_t *reg, uint8_t mask)



/*============================================================================*/
/*
 * Description:
 *
 */
/*============================================================================*/
void bq_25896_init(void)
{
    bq_reset();

    bq_setITERM(128);
    // bq_startADC();
    // bq_oneSecADC();
    bq_disableBATFET();
    bq_disableWATCHDOG();
} // void bq_25896_init(void)


/*============================================================================*/
/*
  * @brief
  * @param
  * @retval
 */
/*============================================================================*/
void bq_cli_status(void)
{
	char stat_msg[20], stat_msg2[10];
	float	f, f2;
	int		res;

	// Input Current Limit
	res = bq_readRegister(0x00);
	m1_text_enable(stat_msg, res, 0x40);
	f = m1_float_convert(res & 0x3f, 50, 100);
	M1_LOG_N("", "Input Current Limit: Pin(%s) %d mA\r\n", stat_msg, (int)f);

	// Configuration
	res = bq_readRegister(0x03);
	m1_text_enable(stat_msg, res, 0x20);
	m1_text_enable(stat_msg2, res, 0x10);
	M1_LOG_N("", "Boost Mode %s, Charge %s\r\n", stat_msg, stat_msg2);

	// Fast Charge Current Limit
	res = bq_readRegister(0x04);
	f = m1_float_convert(res & 0x7f, 64, 0);
	M1_LOG_N("", "Fast Charge Current Limit: %d mA\r\n", (int)f);

	// Pre-charge, Termination
	res = bq_readRegister(0x05);
	f = m1_float_convert((res & 0xf0)>>4, 64, 0);
	f2 = m1_float_convert(res & 0x0f, 64, 0);
	M1_LOG_N("", "Precharge (%d mA), Termination (%d mA)\r\n", (int)f, (int)f2);

	// Charge Voltage Limit
	res = bq_readRegister(0x06);
	f = m1_float_convert(res >> 2, 0.016f, 3.840f);
	m1_float_to_string(stat_msg, f, 3);
	M1_LOG_N("", "Charge Voltage Limit: %s V\r\n", stat_msg);
}
