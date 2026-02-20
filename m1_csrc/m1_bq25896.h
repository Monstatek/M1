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
#ifndef M1_BQ25896_H_
#define M1_BQ25896_H_

#include <stdint.h>
#include "stm32h5xx_hal.h"
#include <stdbool.h>


#define BQ2589X_VREG_MIN      	3840 // 3840mV Offset
#define BQ2589X_VREG_MAX      	4608 // 4608mV Max range
#define BQ2589X_VREG_DEF      	4208 // 4608mV Default

#define BQ2589X_IREG_MAX      	3008 // 3008mA Max range

#define BQ2589X_IINLIM_MAX     	3250 // 3250mA Max range

#define BQ2589X_IPRECHG_MAX     1024 // 1024mA Max range

#define BQ2589X_BOOSTV_MIN      4550 // 4550mV Offset
#define BQ2589X_BOOSTV_MAX      5510 // 4550mV Max range
#define BQ2589X_BOOSTV_DEF      4998 // 4998mV Default

#define BQ2589X_BOOSTI_500 		0x00 // 500mA
#define BQ2589X_BOOSTI_750  	0x01 // 750mA
#define BQ2589X_BOOSTI_1200 	0x02 // 1200mA
#define BQ2589X_BOOSTI_1400 	0x03 // 1400mA (default)
#define BQ2589X_BOOSTI_1650 	0x04 // 1650mA
#define BQ2589X_BOOSTI_1875 	0x05 // 1875mA
#define BQ2589X_BOOSTI_2150 	0x06 // 2150mA

#define BQ2589X_I2C_WR_TIMEOUT	200 // Timeout for I2C Write/Read access in ms


#define BQ25896_I2C_ADDR	0xD6 // 8-bit address, 0x6B is 7-bit address


#define REG00_ADDR 			0x00
#define REG00_EN_HIZ 		0x80
#define REG00_EN_ILIM 		0x40
#define REG00_IINLIM 		0x3F

#define REG01_ADDR 			0x01
#define REG01_BHOT 			0xC0
#define REG01_BCOLD 		0x20
#define REG01_VINDPM_OS 	0x1F

#define REG02_ADDR 			0x02
#define REG02_CONV_START 	0x80
#define REG02_CONV_RATE 	0x40
#define REG02_BOOST_FREQ 	0x20
#define REG02_ICO_EN 		0x10
#define REG02_FORCE_DPDM 	0x02
#define REG02_AUTO_DPDM_EN 	0x01

#define REG03_ADDR 			0x03
#define REG03_BAT_LOADEN 	0x80
#define REG03_WD_RST 		0x40
#define REG03_OTG_CONFIG 	0x20
#define REG03_CHG_CONFIG 	0x10
#define REG03_SYS_MIN 		0x0E
#define REG03_MIN_VBAT_SEL 	0x01

#define REG04_ADDR 			0x04
#define REG04_EN_PUMPX 		0x80
#define REG04_ICHG 			0x7F

#define REG05_ADDR 			0x05
#define REG05_IPRECHG 		0xF0
#define REG05_ITERM 		0x0F

#define REG06_ADDR 			0x06
#define REG06_VREG 			0xFC
#define REG06_BATLOWV 		0x02
#define REG06_VRECHG 		0x01

#define REG07_ADDR 			0x07
#define REG07_EN_TERM 		0x80
#define REG07_STAT_DIS 		0x40
#define REG07_WATCHDOG 		0x30
#define REG07_EN_TIMER 		0x08
#define REG07_CHG_TIMER 	0x06
#define REG07_JEITA_ISET 	0x01

#define REG08_ADDR 			0x08

#define REG09_ADDR 			0x09
#define REG09_BATFET_DIS 	(1<<5)
#define REG09_BATFET_DLY 	(1<<3)
#define REG09_BATFET_RST_EN (1<<2)
#define REG09_BATFET_MASK	(REG09_BATFET_DIS|REG09_BATFET_DLY|REG09_BATFET_RST_EN)

#define REG0A_ADDR 			0x0A
#define REG0A_BOOSTV 		0xF0
#define REG0A_PFM_OTG_DIS 	0x08
#define REG0A_BOOST_LIM 	0x07

#define REGISTER0B_ADDR 	0x0B // REG0B_ADDR defined in other header file!
#define REG0B_VBUS_STAT 0xE0
#define REG0B_CHRG_STAT 0x18
#define REG0B_PG_STAT 0x04
#define REG0B_VSYS_STAT 0x01

#define REG0C_ADDR 				0x0C
#define REG0C_WATCHDOG_FAULT 	0x80
//Status 0 – Normal
//1- Watchdog timer expiration
#define REG0C_BOOST_FAULT 	0x40
//0 – Normal
//1 – VBUS overloaded in OTG, or VBUS OVP, or battery is too low in boost mode
#define REG0C_CHRG_FAULT 	0x30
//00 – Normal
//01 – Input fault (VBUS > VACOV or VBAT < VBUS < VVBUSMIN(typical 3.8V) )
//10 - Thermal shutdown
//11 – Charge Safety Timer Expiration
#define REG0C_BAT_FAULT 	0x08
//0 – Normal
//1 – BATOVP (VBAT > VBATOVP)
#define REG0C_NTC_FAULT 	0x07
//Buck Mode:
//000 – Normal 010 – TS Warm 011 – TS Cool 101 – TS Cold 110 – TS Hot
//Boost Mode:
//000 – Normal 101 – TS Cold 110 – TS Hot

#define REG0D_ADDR 			0x0D
#define REG0D_FORCE_VINDPM 	0x80
#define REG0D_VINDPM 		0x7F

#define REG0E_ADDR 			0x0E

#define REG0F_ADDR 			0x0F

#define REG10_ADDR			0x10

#define REG11_ADDR			0x11
#define REG11_VBUS_GD 		0x80
#define REG11_VBUSV 		0x7F

#define REG12_ADDR 			0x12

#define REG13_ADDR 			0x13
#define REG13_VDPM_STAT 	0x80
#define REG13_IDPM_STAT 	0x40
#define REG13_IDPM_LIM 		0x3F

#define REG14_ADDR 			0x14
#define REG14_REG_RST 		0x80

uint8_t bq_enableHIZ(void);
uint8_t bq_disableHIZ(void);
uint8_t bq_enableILIMPin(void);
uint8_t bq_disableILIMPin(void);
uint8_t bq_setMaxILIM(void);
uint8_t bq_setIINLIM(int i);
uint8_t bq_setVINDPM_OS(int val);
uint8_t bq_startADC(void);
uint8_t bq_oneShotADC(void);
uint8_t bq_oneSecADC(void);
uint8_t bq_stopADC(void);
uint8_t bq_setFORCE_DPDM(void);
uint8_t bq_unsetFORCE_DPDM(void);
uint8_t bq_enableAUTO_DPDM(void);
uint8_t bq_disableAUTO_DPDM(void);
uint8_t bq_enableIBATLOAD(void);
uint8_t bq_disableIBATLOAD(void);
uint8_t bq_enableOTG(void);
uint8_t bq_disableOTG(void);
bool bq_getOTG(void);
uint8_t bq_enableCHRG(void);
uint8_t bq_disableCHRG(void);
bool bq_getCHRG(void);
uint8_t bq_enablePUMPX(void);
uint8_t bq_disablePUMPX(void);
uint8_t bq_setICHG(int val);
int bq_getICHG(void);
uint8_t bq_setIPRECHG(int val);
uint8_t bq_setITERM(int val);
uint8_t bq_setVREG(int val);
uint8_t bq_disableWATCHDOG(void);
uint8_t bq_WATCHDOG40s(void);
uint8_t bq_WATCHDOG80s(void);
uint8_t bq_WATCHDOG160s(void);
uint8_t bq_setBOOSTV(int val);
uint8_t bq_setBOOST_LIM(int val);
int bq_getVBUS_STAT(void);
int bq_getCHRG_STAT(void);
int bq_getPG_STAT(void);
int bq_getVSYS_STAT(void);
int bq_getFAULT(void);
int bq_getWATCHDOG_FAULT(void);
int bq_getBOOST_FAULT(void);
int bq_getBAT_FAULT(void);
int bq_getCHRG_FAULT(void);
int bq_getNTC_FAULT(void);
uint8_t bq_enableFORCE_VINDPM(void);
uint8_t bq_disableFORCE_VINDPM(void);
uint8_t bq_setVINDPM(int val);

//uint8_t bq_enableBATFET(void);
//uint8_t bq_disableBATFET(void);
uint8_t bq_shipMode(uint8_t mode);

int bq_getVINDPM(void);
int bq_getBATV(void);
int bq_getSYSV(void);
float bq_getTSPCT(void);
float bq_getVBUSV(void);
int bq_getVBUS_GD(void);
int bq_getICHGR(void);
int bq_getVDPM_STAT(void);
int bq_getIDPM_STAT(void);
int bq_getIDPM_LIM(void);


int bq_reset(void);
int bq_setRegister(uint8_t reg, uint8_t value);
int bq_readRegister(uint8_t reg);
void bq_setBit(uint8_t *reg, uint8_t mask);
void bq_unsetBit(uint8_t *reg, uint8_t mask);
void bq_25896_init(void);
void bq_cli_status(void);

#endif /* M1_BQ25896_H_ */
