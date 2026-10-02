/* See COPYING.txt for license details. */

/*
*
*  bq27421_golden_image.h
*
*  M1 bq27421 header
*
* M1 Project
*
*/

#ifndef BQ27421_GOLDEN_IMAGE_H_
#define BQ27421_GOLDEN_IMAGE_H_

#define BQ27421_GI_DATA_SIZE   32U

#define BIE_EN 		(1<<5)
#define BIE_DIS		(0<<5)
#define BI_PU_EN	(1<<4)
#define BI_PU_DIS	(0<<4)

#define OPCONFIG_HIGH	(BIE_DIS | BI_PU_DIS | 0x5)

// TAPER_CURRENT = TERMINATE_CURRENT + (TERMINATE_CURRENT * 0.1)
// TAPER_RATE = DESIGN_CAPACITY / (0.1*TAPER_CURRENT)

#define M1_BATT_DESIGN_CAPACITY		(uint16_t)(2100) // mAh
#define M1_BATT_DESIGN_ENERGY		(uint16_t)(7770) // mWh, Design capacity * 3.7V
#define M1_STATE_QMAX_CELL			(uint16_t)(0x4C50) // 19536
#define M1_STATE_QMAX_CELL_DEFAULT	(uint16_t)(0x4000) // Default 16384
#define M1_STATE_TAPER_RATE         (uint16_t)(150)
#endif // BQ27421_GOLDEN_IMAGE_H_

