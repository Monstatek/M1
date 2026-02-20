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

/*************************** I N C L U D E S **********************************/
#include <string.h>
#include "m1_bq27421.h"
#include "m1_i2c.h"

/*************************** D E F I N E S ************************************/

#define BQ27241_I2C_TIMEOUT	(2000)

// ==== BlockData offsets (Extended Data - "Gas Gauging" subclass ???? ????) ====
#define OFFS_DESIGN_CAP_MSB      10
#define OFFS_DESIGN_CAP_LSB      11
#define OFFS_DESIGN_EN_MSB       12
#define OFFS_DESIGN_EN_LSB       13
#define OFFS_TERM_VOLT_MSB       16
#define OFFS_TERM_VOLT_LSB       17
#define OFFS_TAPER_RATE_MSB      27
#define OFFS_TAPER_RATE_LSB      28

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


bool _sealFlag; // Global to identify that IC was previously sealed
bool _userConfigControl; // Global to identify that user has control over
                         // entering/exiting config
static uint8_t Blockdata[32];


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool bq27421_i2c_command_read1( uint8_t command, uint8_t *data );

static uint16_t bq27421_flags(void);
static uint16_t bq27421_status(void);
static bool bq27421_sealed(void);
static bool bq27421_seal(void);
static bool bq27421_unseal(void);
static bool bq27421_itporFlag(void);
//static bool Bbq27421_executeControlWord(uint16_t function);
static bool bq27421_softReset(void);

//static bool bq27421_reset(void);
static bool bq27421_enterConfig(bool userControl);
static bool bq27421_exitConfig(bool userControl);
static bool bq27421_blockDataControl(void);
static bool bq27421_blockDataClass(uint8_t id);
static bool bq27421_blockDataOffset(uint8_t offset);
static uint8_t bq27421_blockDataChecksum(void);
static uint8_t bq27421_readBlockData(uint8_t offset);
static uint8_t bq27421_computeBlockChecksum(void);
//static bool bq27421_writeBlockChecksum(uint8_t csum);
static uint8_t bq27421_readExtendedData(uint8_t classID, uint8_t offset);
//static bool bq27421_writeExtendedData(uint8_t classID, uint8_t offset, uint8_t * data, uint8_t len);
static uint8_t bq27421_checkExtendedData(uint8_t offset);
static bool bq27421_checkSohStatus(void);
static bool bq27421_wdreseted(void);
bool bq27421_applyConfigIfMatches(uint16_t designCapacity_mAh,
                                  uint16_t designEnergy_mWh,
                                  uint16_t terminateVoltage_mV,
                                  uint16_t taperRate);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
HAL_StatusTypeDef bq27421_i2c_read(uint16_t NumberOfBytes, uint16_t RegAddress , uint8_t *RxBuffer)
{
	S_M1_I2C_Trans_Inf i2c_inf;
	HAL_StatusTypeDef   stat;

	i2c_inf.dev_id = I2C_DEVICE_BQ27421;
	i2c_inf.timeout = I2C_READ_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_READ_REGISTER_MULTIPLE;
	i2c_inf.reg_address = RegAddress;
	i2c_inf.pdata = RxBuffer;
	i2c_inf.data_len = NumberOfBytes;
	stat = m1_i2c_hal_trans_req(&i2c_inf);
	HAL_Delay( BQ27421_DELAY );

	return stat;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
HAL_StatusTypeDef bq27421_i2c_write(uint16_t NumberOfBytes, uint16_t RegAddress , uint8_t *TxBuffer)
{
	S_M1_I2C_Trans_Inf	i2c_inf;
	HAL_StatusTypeDef   stat;

	i2c_inf.dev_id = I2C_DEVICE_BQ27421;
	i2c_inf.timeout = I2C_WRITE_TIMEOUT;
	i2c_inf.trans_type = I2C_TRANS_WRITE_REGISTER_MULTIPLE;
	i2c_inf.reg_address = RegAddress;
	i2c_inf.pdata = TxBuffer;
	i2c_inf.data_len = NumberOfBytes;
	stat = m1_i2c_hal_trans_req(&i2c_inf);

	HAL_Delay( BQ27421_DELAY );

	return stat;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_command_write( uint8_t command, uint8_t data )
{
    uint8_t i2c_data[2];

    i2c_data[0] = ( uint8_t )( data & 0x00FF );
    //i2c_data[1] = ( uint8_t )( ( data >> 8 ) & 0x00FF );

    return bq27421_i2c_write(1, command, i2c_data);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_command_read( uint8_t command, uint16_t *data )
{
    uint8_t i2c_data[2];

    if (bq27421_i2c_read(2, command, i2c_data) != HAL_OK)
    	return false;

    *data = ( i2c_data[1] << 8 ) | i2c_data[0];

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_command_read1( uint8_t command, uint8_t *data )
{
    uint8_t i2c_data;

    if (bq27421_i2c_read(1, command, &i2c_data) != HAL_OK)
    	return false;

    *data = i2c_data;

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_control_write( uint16_t subcommand )
{
    HAL_StatusTypeDef   stat;
    uint8_t i2c_data;

     i2c_data = (uint8_t)( ( subcommand ) & 0x00FF );
    stat = bq27421_i2c_write(1, BQ27421_CONTROL_LOW, &i2c_data);

    if (stat == HAL_OK) {
  		i2c_data = (uint8_t)( ( subcommand >> 8 ) & 0x00FF );
		stat = bq27421_i2c_write(1, BQ27421_CONTROL_HIGH, &i2c_data);
    }
    return (stat == HAL_OK);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_control_read( uint16_t subcommand, uint16_t *data )
{
	S_M1_I2C_Trans_Inf i2c_inf;
	HAL_StatusTypeDef   stat = HAL_ERROR;
    uint8_t i2c_data[2];

    if (bq27421_i2c_control_write(subcommand))
    {
    	i2c_inf.dev_id = I2C_DEVICE_BQ27421;
    	i2c_inf.timeout = I2C_READ_TIMEOUT;
    	i2c_inf.trans_type = I2C_TRANS_READ_DATA;
    	i2c_inf.pdata = i2c_data;
    	i2c_inf.data_len = 2;

    	stat = m1_i2c_hal_trans_req(&i2c_inf);
    	HAL_Delay( BQ27421_DELAY );

    	if(stat == HAL_OK )
        {
        	*data = ( i2c_data[1] << 8 ) | i2c_data[0];
        }
    }
    return (stat == HAL_OK );
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_write_data_block( uint8_t offset, uint8_t *data, uint8_t bytes )
{
    HAL_StatusTypeDef stat = HAL_ERROR;
    uint16_t subcommand;
    //uint8_t i2c_data[2], i;
    int i;

    for( i = 0; i < bytes; i++)
    {
    	subcommand = BQ27421_BLOCK_DATA_START + offset + i;
        //i2c_data[1] = data[i];
    	stat = bq27421_i2c_write(1, subcommand, &data[i]);
    	if(stat != HAL_OK)
    		break;
    }

     //stat = bq27421_i2c_write(bytes, BQ27421_BLOCK_DATA_START + offset, data);

    return (stat == HAL_OK);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_i2c_read_data_block( uint8_t offset, uint8_t *data, uint8_t bytes )
{
    HAL_StatusTypeDef stat;
    uint16_t subcommand = BQ27421_BLOCK_DATA_START + offset;

    //stat = bq27421_i2c_write(1, BQ27421_BLOCK_DATA_START + offset, &i2c_data);

    //if(stat == HAL_OK)
    //{
    	//HAL_Delay(5);

    	stat = bq27421_i2c_read(bytes, subcommand, data);
    //}
	return (stat == HAL_OK);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_init( uint16_t designCapacity_mAh, uint16_t terminateVoltage_mV, uint16_t taperCurrent_mA )
{
    uint16_t designEnergy_mWh, taperRate, flags, checksumOld, checksumRead;
    uint8_t checksumNew;
    //bool result = false;
    //if(!(bq27421_itporFlag() == 1 && bq27421_soh(SOH_STAT) != 3))
    //	return false;

    designEnergy_mWh = 3.7 * designCapacity_mAh;
    //taperRate = designCapacity_mAh / ( 0.1 * taperCurrent_mA );
    taperRate = 10 *designCapacity_mAh / taperCurrent_mA;


    // Unseal gauge
	if (bq27421_sealed())
	{
		_sealFlag = true;
		bq27421_unseal(); // Must be unsealed before making changes
	}

	//bq27421_clearHiberate();

    if(bq27421_applyConfigIfMatches(designCapacity_mAh,designEnergy_mWh,terminateVoltage_mV,taperRate))
    {
    	return true;
    }
    /*****************************************************************************
     ************************ bq27421 initial Functions **************************
     *****************************************************************************/
    // Send CFG_UPDATE
    bq27421_i2c_control_write( BQ27421_CONTROL_SET_CFGUPDATE );

    // Poll flags
    do
    {
        bq27421_i2c_command_read( BQ27421_FLAGS_LOW, &flags );
        if( !(flags & BQ27421_FLAG_CFGUPMODE) )	// CFGUPMODE ?
        {
            HAL_Delay( 1 );
        }
    }
    while( !(flags & BQ27421_FLAG_CFGUPMODE) );

    //
    // read 0x52
    //
    // Enable Block Data Memory Control
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CONTROL, 0x00 );

    HAL_Delay( BQ27421_DELAY );

    // Access State subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x52 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Read block checksum
    bq27421_i2c_command_read( BQ27421_BLOCK_DATA_CHECKSUM, &checksumOld );

    // Read 32-byte block of data
    uint8_t block[32];
    for(uint8_t i = 0; i < 32; i++ )
    {
        block[i] = 0x00;
    }

    bq27421_i2c_read_data_block( 0x00, block, 32 );

    // Calculate checksum
    uint8_t checksumCalc = 0x00;

    for(uint8_t i = 0; i < 32; i++ )
    {
        checksumCalc += block[i];
    }
    checksumCalc = 0xFF - checksumCalc;

    //
    // write 0x52
    //
    // Update design capacity
    block[BQ27421_STATE_OFFSET_DESIGN_CAPACITY] = (uint8_t)( designCapacity_mAh >> 8 );
    block[BQ27421_STATE_OFFSET_DESIGN_CAPACITY+1] = (uint8_t)( designCapacity_mAh & 0x00FF );
    // Update design energy
    block[BQ27421_STATE_OFFSET_DESIGN_ENERGY] = (uint8_t)( designEnergy_mWh >> 8 );
    block[BQ27421_STATE_OFFSET_DESIGN_ENERGY+1] = (uint8_t)( designEnergy_mWh & 0x00FF );
    // Update terminate voltage
    block[BQ27421_STATE_OFFSET_TERMINATE_VOLTAGE] = (uint8_t)( terminateVoltage_mV >> 8 );
    block[BQ27421_STATE_OFFSET_TERMINATE_VOLTAGE+1] = (uint8_t)( terminateVoltage_mV & 0x00FF );
    // Update taper rate
    block[BQ27421_STATE_OFFSET_TAPER_RATE] = (uint8_t)( taperRate >> 8 );
    block[BQ27421_STATE_OFFSET_TAPER_RATE+1] = (uint8_t)( taperRate & 0x00FF );

    // Calculate new checksum
    checksumNew = 0x00;
    for(int i = 0; i < 32; i++ )
    {
        checksumNew += block[i];
    }
    checksumNew = 0xFF - checksumNew;

    // Enable Block Data Memory Control
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CONTROL, 0x00 );

    HAL_Delay( BQ27421_DELAY );

    // Access State subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x52 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Write 32-byte block of updated data
    bq27421_i2c_write_data_block( 0x00, block, 32 );

    // Write new checksum
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CHECKSUM, checksumNew );

    // Access State subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x52 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Read block checksum
    bq27421_i2c_command_read( BQ27421_BLOCK_DATA_CHECKSUM, &checksumRead );

    // Verify
    if( checksumRead != (uint8_t)checksumNew )
    {
        return false;
    }

    //
    // read 0x40
    //
    // Enable Block Data Memory Control
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CONTROL, 0x00 );

    HAL_Delay( BQ27421_DELAY );

    // Access Registers subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x40 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Read block checksum
    bq27421_i2c_command_read( BQ27421_BLOCK_DATA_CHECKSUM, &checksumOld );

    // Read 32-byte block of data
    for(uint8_t i = 0; i < 32; i++ )
    {
        block[i] = 0x00;
    }

    bq27421_i2c_read_data_block( 0x00, block, 32 );

    // Calculate checksum
    checksumCalc = 0x00;

    for(uint8_t i = 0; i < 32; i++ )
    {
        checksumCalc += block[i];
    }
    checksumCalc = 0xFF - checksumCalc;

    // Update OpConfig
    block[BQ27421_REGISTERS_OFFSET_OPCONFIG] = (BQ27421_OPCONFIG_BATLOWEN | BQ27421_OPCONFIG_TEMPS); //  0x05;

    // Calculate new checksum
    checksumNew = 0x00;
    for(int i = 0; i < 32; i++ )
    {
        checksumNew += block[i];
    }
    checksumNew = 0xFF - checksumNew;

    //
    // write 0x40
    //
    // Enable Block Data Memory Control
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CONTROL, 0x00 );

    HAL_Delay( BQ27421_DELAY );

    // Access Registers subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x40 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Write 32-byte block of updated data
    bq27421_i2c_write_data_block( 0x00, block, 32 );

    // Write new checksum
    bq27421_i2c_command_write( BQ27421_BLOCK_DATA_CHECKSUM, checksumNew );

    // Access Registers subclass
    bq27421_i2c_command_write( BQ27421_DATA_CLASS, 0x40 );

    // Write the block offset
    bq27421_i2c_command_write( BQ27421_DATA_BLOCK, 0x00 );

    // Read block checksum
    bq27421_i2c_command_read( BQ27421_BLOCK_DATA_CHECKSUM, &checksumRead );

    // Verify
    if( checksumRead != (uint8_t)checksumNew )
    {
        return false;
    }

    //
    // Configure BAT_DET
    bq27421_i2c_control_write( BQ27421_CONTROL_BAT_INSERT );

    // Send Soft Reset
    bq27421_i2c_control_write( BQ27421_CONTROL_SOFT_RESET );

    // Poll flags
    do
    {
        bq27421_i2c_command_read( BQ27421_FLAGS_LOW, &flags );
        if( !(flags & BQ27421_FLAG_CFGUPMODE) )
        {
            HAL_Delay( 1 );
        }
    }
    while( (flags & BQ27421_FLAG_CFGUPMODE) );

    // Seal gauge
    bq27421_i2c_control_write( BQ27421_CONTROL_SEALED );

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_update( bq27421_info *battery )
{
    uint16_t temp;

    if( !bq27421_readVoltage_mV( &(battery->voltage_mV) ) )
    {
        return false;
    }
    if( !bq27421_readAvgCurrent_mA( &(battery->current_mA) ) )
    {
        return false;
    }
    if( !bq27421_readTemp_degK( &temp ) )
    {
        return false;
    }
    battery->temp_degC = ( (double)temp / 10 ) - 273.15;

#if 0
    if( !bq27421_readStateofCharge_percent( &(battery->soc_percent) ) )
    {
        return false;
    }
#endif

    battery->soc_percent = bq27421_soc(FILTERED);
    battery->soh_state = bq27421_soh(SOH_STAT);
    battery->soh_percent = bq27421_soh(PERCENT);
#if 0
    if(battery->soh_state)
    {
    	battery->soc_percent = bq27421_soc(FILTERED);

    	if(battery->soh_state == 3)
    		battery->soh_percent = bq27421_soh(PERCENT);
    }
#endif
    //if( !bq27421_readStateofHealth_percent( &(battery->soh_percent) ) )
    //{
    //    return false;
    //}
    if( !bq27421_readDesignCapacity_mAh( &(battery->designCapacity_mAh) ) )
    {
        return false;
    }
    if( !bq27421_readRemainingCapacity_mAh( &(battery->remainingCapacity_mAh) ) )
    {
        return false;
    }
    if( !bq27421_readFullChargeCapacity_mAh( &(battery->fullChargeCapacity_mAh) ) )
    {
        return false;
    }
    if( !bq27421_readFlagsReg( &temp ) )
    {
        return false;
    }

    battery->flags = temp;
    bq27421_readControlReg(&battery->status);

    battery->isCritical = temp & 0x0002;
    battery->isLow = temp & 0x0004;
    battery->isFull = temp & 0x0200;
    if( battery->current_mA <= 0 )
    {
        battery->isDischarging = 1;
        battery->isCharging = 0;
    }
    else
    {
        battery->isDischarging = 0;
        battery->isCharging = 1;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readDeviceType( uint16_t *deviceType )
{
    if( !bq27421_i2c_control_write( BQ27421_CONTROL_DEVICE_TYPE ) )
    {
        return false;
    }
    if( !bq27421_i2c_command_read( BQ27421_CONTROL_LOW, deviceType ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readDeviceFWver( uint16_t *deviceFWver )
{
    if( !bq27421_i2c_control_write( BQ27421_CONTROL_FW_VERSION ) )
    {
        return false;
    }
    if( !bq27421_i2c_command_read( BQ27421_CONTROL_LOW, deviceFWver ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readDesignCapacity_mAh( uint16_t *capacity_mAh )
{
    if( !bq27421_i2c_command_read( BQ27421_DESIGN_CAP_LOW, capacity_mAh ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readVoltage_mV( uint16_t *voltage_mV )
{
    if( !bq27421_i2c_command_read( BQ27421_VOLTAGE_LOW, voltage_mV ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readTemp_degK( uint16_t *temp_degKbyTen )
{
    if( !bq27421_i2c_command_read( BQ27421_TEMP_LOW, temp_degKbyTen ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readAvgCurrent_mA( int16_t *avgCurrent_mA )
{
    if( !bq27421_i2c_command_read( BQ27421_AVG_CURRENT_LOW, (uint16_t *)avgCurrent_mA ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readControlReg( uint16_t *control )
{
    if( !bq27421_i2c_control_write( BQ27421_CONTROL_STATUS ) )
    {
        return false;
    }
    if( !bq27421_i2c_command_read( BQ27421_CONTROL_LOW, control ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readFlagsReg( uint16_t *flags )
{
    if( !bq27421_i2c_command_read( BQ27421_FLAGS_LOW, flags ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readopConfig( uint16_t *opConfig )
{
    if( !bq27421_i2c_command_read( BQ27421_OPCONFIG_LOW, opConfig ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readRemainingCapacity_mAh( uint16_t *capacity_mAh )
{
    if( !bq27421_i2c_command_read( BQ27421_REMAINING_CAP_LOW, capacity_mAh ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readFullChargeCapacity_mAh( uint16_t *capacity_mAh )
{
    if( !bq27421_i2c_command_read( BQ27421_FULL_CHARGE_CAP_LOW, capacity_mAh ) )
    {
        return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_setHiberate(void)
{
	if( !bq27421_i2c_control_write( BQ27421_CONTROL_SET_HIBERNATE ))
	{
		return false;
	}
	return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_clearHiberate(void)
{
	if( !bq27421_i2c_control_write( BQ27421_CONTROL_CLEAR_HIBERNATE ))
	{
		return false;
	}
	return true;
}


#if 0
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_readStateofCharge_percent( uint16_t *soc_percent )
{
    if( !bq27421_i2c_command_read( BQ27421_STATE_OF_CHARGE_LOW, soc_percent ) )
    {
        return false;
    }

    return true;
}
#endif


/*============================================================================*/
/**
  * @brief Reads and returns specified state of charge measurement
  * @param
  * @retval
  */
/*============================================================================*/
uint16_t bq27421_soc(soc_measure type)
{
	uint16_t socRet = 0;

	switch (type)
	{
	case FILTERED:
		bq27421_i2c_command_read(BQ27421_STATE_OF_CHARGE_LOW, &socRet);
		break;
	case UNFILTERED:
		bq27421_i2c_command_read(BQ27421_STATE_OF_CHARGE_UNFILT_LOW, &socRet);
		break;
	}

	return socRet;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_soh(soh_measure type)
{
	uint8_t sohStatus;
	uint8_t sohPercent;
	uint16_t sohRaw;

    if( !bq27421_i2c_command_read( BQ27421_STATE_OF_HEALTH_LOW, &sohRaw ) )
    {
        return false;
    }

	sohStatus = sohRaw >> 8;
	sohPercent = sohRaw & 0x00FF;

	if (type == PERCENT)
		return sohPercent;
	else
		return sohStatus;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_checkSohStatus(void)
{
	for (int i = 0; i < 2; i++)
	{
		if (bq27421_soh(SOH_STAT) != 0)
		{
			return true;
		}

		if (i == 0)
		{
			if (!bq27421_softReset())
			{
				return false;
			}

			int16_t timeout = BQ27241_I2C_TIMEOUT;
			while ((timeout > 0) && (bq27421_flags() & BQ27421_FLAG_ITPOR))
			{
				HAL_Delay(1);
				timeout--;
			}

			if (timeout <= 0)
			{
				return false;
			}
		}
	}

	return false;
}



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static inline uint16_t rd16_be(const uint8_t *buf, int msb_offs, int lsb_offs)
{
    return ((uint16_t)buf[msb_offs] << 8) | (uint16_t)buf[lsb_offs];
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

bool bq27421_applyConfigIfMatches(uint16_t designCapacity_mAh,
                                  uint16_t designEnergy_mWh,
                                  uint16_t terminateVoltage_mV,
                                  uint16_t taperRate)
{
    if (!bq27421_checkSohStatus()) {
        return false;
    }

    const bool wasSealed = bq27421_sealed();
    if (wasSealed) {
        bq27421_unseal();
    }

    bool ok = false;

    // 3) Extended Data ???
    if (bq27421_checkExtendedData(0)) {
        // Blockdata
        const uint16_t tmp_designCapacity_mAh  = rd16_be(Blockdata, OFFS_DESIGN_CAP_MSB,  OFFS_DESIGN_CAP_LSB);
        const uint16_t tmp_designEnergy_mWh    = rd16_be(Blockdata, OFFS_DESIGN_EN_MSB,   OFFS_DESIGN_EN_LSB);
        const uint16_t tmp_terminateVoltage_mV = rd16_be(Blockdata, OFFS_TERM_VOLT_MSB,   OFFS_TERM_VOLT_LSB);
        const uint16_t tmp_taperRate           = rd16_be(Blockdata, OFFS_TAPER_RATE_MSB,  OFFS_TAPER_RATE_LSB);

        //
        const bool matches =
            (tmp_designCapacity_mAh  == designCapacity_mAh)  &&
            (tmp_designEnergy_mWh    == designEnergy_mWh)    &&
            (tmp_terminateVoltage_mV == terminateVoltage_mV) &&
            (tmp_taperRate           == taperRate);

        if (matches) {
            //  BAT_INSERT + SoftReset
            if (bq27421_itporFlag() || bq27421_wdreseted()) {

            	bq27421_i2c_control_write(BQ27421_CONTROL_BAT_INSERT);

                if (bq27421_softReset()) {

                    int16_t timeout = (int16_t)BQ27241_I2C_TIMEOUT;

                    while (timeout-- > 0 && (bq27421_flags() & BQ27421_FLAG_CFGUPMODE)) {
                        HAL_Delay(1);
                    }
                    if (timeout > 0) {
                        ok = true;
                    }
                }
            } else {
                ok = true; //  OK
            }
            ok = true;	//+add

            // 4) Sealed
            //if (wasSealed) {
            bq27421_seal();
            //}
        }
    }

    return ok;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t bq27421_flags(void)
{
	uint16_t flags;

	bq27421_i2c_command_read( BQ27421_FLAGS_LOW, &flags );

	return flags;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t bq27421_status(void)
{
	uint16_t status;

	bq27421_readControlReg(&status);

	return status;
}


/*============================================================================*/
/**
  * @brief Check if the BQ27427 is wdreset or not.
  * @param
  * @retval
  */
/*============================================================================*/
static bool bq27421_wdreseted(void)
{
	uint16_t stat = bq27421_status();
	//bq27421_readControlReg(&stat);
	return stat & BQ27421_STATUS_WDRESET;
}


/*============================================================================*/
/**
  * @brief Check if the BQ27427 is sealed or not.
  * @param
  * @retval
  */
/*============================================================================*/
static bool bq27421_sealed(void)
{
	uint16_t stat = bq27421_status();
	//bq27421_readControlReg(&stat);
	return stat & BQ27421_STATUS_SS;
}


/*============================================================================*/
/**
  * @brief Seal the BQ27421
  * @param
  * @retval
  */
/*============================================================================*/
static bool bq27421_seal(void)
{
	return bq27421_i2c_control_write( BQ27421_CONTROL_SEALED );
}


/*============================================================================*/
/**
  * @brief UnSeal the BQ27421
  * @param
  * @retval
  */
/*============================================================================*/
static bool bq27421_unseal(void)
{
	// To unseal the BQ27427, write the key to the control
	// command. Then immediately write the same key to control again.

    if(bq27421_i2c_control_write( BQ27421_CONTROL_UNSEAL ))
    {
    	return bq27421_i2c_control_write( BQ27421_CONTROL_UNSEAL );
    }
	return false;
}


/*============================================================================*/
/**
  * @brief Execute a subcommand() from the BQ27421's control()
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_executeControlWord(uint16_t function)
{
	uint8_t subCommandMSB = (function >> 8);
	uint8_t subCommandLSB = (function & 0x00FF);
	uint8_t command[2] = {subCommandLSB, subCommandMSB};
	HAL_StatusTypeDef   stat;
	//uint8_t data[2] = {0, 0};

    stat = bq27421_i2c_write(1, BQ27421_CONTROL_LOW, &command[0]);

    if (stat == HAL_OK) {
		stat = bq27421_i2c_write(1, BQ27421_CONTROL_HIGH, &command[1]);

		if (stat == HAL_OK)
			return true;
    }
	return false;
}

/*****************************************************************************
 ************************** GPOUT Control Functions **************************
 *****************************************************************************/

/*============================================================================*/
/**
  * @brief Check if the ITPOR flag is set
  * @param
  * @retval
  */
/*============================================================================*/
static bool bq27421_itporFlag(void)
{
	uint16_t flagState = bq27421_flags();

	return flagState & BQ27421_FLAG_ITPOR;
}

#if 0
/*============================================================================*/
/**
  * @brief Issue a factory reset to the BQ27427
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_reset(void)
{
	if (!_userConfigControl) bq27421_enterConfig(false); // Enter config mode if not already in it

	if (bq27421_executeControlWord(BQ27421_CONTROL_RESET))
	{
		if (!_userConfigControl) bq27421_exitConfig(false);
		return true;
	}
	else
	{
		return false;
	}
}
#endif


/*============================================================================*/
/**
  * @brief Issue a soft-reset to the BQ27427
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_softReset(void)
{
	return bq27421_executeControlWord(BQ27421_CONTROL_SOFT_RESET);
}


/*============================================================================*/
/**
  * @brief Enter configuration mode - set userControl if calling from an Arduino sketch
  * and you want control over when to exitConfig
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_enterConfig(bool userControl)
{
	if (userControl) _userConfigControl = true;

	if (bq27421_sealed())
	{
		_sealFlag = true;
		bq27421_unseal(); // Must be unsealed before making changes
	}

	if (bq27421_executeControlWord(BQ27421_CONTROL_SET_CFGUPDATE))
	{
		int16_t timeout = BQ27241_I2C_TIMEOUT;
		while ((timeout--) && (!(bq27421_flags() & BQ27421_FLAG_CFGUPMODE)))
			HAL_Delay(1);

		if (timeout > 0)
			return true;
	}

	return false;
}


/*============================================================================*/
/**
  * @brief Exit configuration mode
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_exitConfig(bool userControl)
{
	if (userControl) _userConfigControl = false;

	if (bq27421_softReset())
	{
		int16_t timeout = BQ27241_I2C_TIMEOUT;
		while ((timeout--) && ((bq27421_flags() & BQ27421_FLAG_CFGUPMODE)))
			HAL_Delay(1);
		if (timeout > 0)
		{
			if (_sealFlag) bq27421_seal(); // Seal back up if we IC was sealed coming in
			return true;
		}
	}
	return false;
}

/*****************************************************************************
 ************************** Extended Data Commands ***************************
 *****************************************************************************/

/*============================================================================*/
/**
  * @brief Issue a BlockDataControl() command to enable BlockData access
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_blockDataControl(void)
{
	uint8_t enableByte = 0x00;
	return (bq27421_i2c_command_write( BQ27421_EXTENDED_CONTROL, enableByte ) == HAL_OK)? true : false;
	//return i2cWriteBytes(BQ27421_EXTENDED_CONTROL, &enableByte, 1);
}


/*============================================================================*/
/**
  * @brief Issue a DataClass() command to set the data class to be accessed
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_blockDataClass(uint8_t id)
{
	return (bq27421_i2c_command_write( BQ27421_EXTENDED_DATACLASS, id ) == HAL_OK)? true : false;
	//return i2cWriteBytes(BQ27421_EXTENDED_DATACLASS, &id, 1);
}


/*============================================================================*/
/**
  * @brief Issue a DataBlock() command to set the data block to be accessed
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_blockDataOffset(uint8_t offset)
{
    // Write the block offset
    return (bq27421_i2c_command_write( BQ27421_EXTENDED_DATABLOCK, offset ) == HAL_OK)? true : false;
	//return i2cWriteBytes(BQ27421_EXTENDED_DATABLOCK, &offset, 1);
}


/*============================================================================*/
/**
  * @brief Read the current checksum using BlockDataCheckSum()
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_blockDataChecksum(void)
{
	uint8_t csum;
	bq27421_i2c_command_read1( BQ27421_EXTENDED_CHECKSUM, &csum );
	//i2cReadBytes(BQ27421_EXTENDED_CHECKSUM, &csum, 1);
	return csum;
}


/*============================================================================*/
/**
  * @brief Use BlockData() to read a byte from the loaded extended data
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_readBlockData(uint8_t offset)
{
	uint8_t ret;
	uint8_t address = offset + BQ27421_EXTENDED_BLOCKDATA;
	bq27421_i2c_command_read1(address, &ret);
	return ret;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_readBlockDataAll(uint8_t offset, uint8_t *block)
{
	//uint8_t ret;
	//uint8_t address = offset + BQ27421_EXTENDED_BLOCKDATA;
	return (bq27421_i2c_read_data_block( offset, block, 32 ) == HAL_OK)? true : false;
}

#if 0
/*============================================================================*/
/**
  * @brief Use BlockData() to write a byte to an offset of the loaded data
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_writeBlockData(uint8_t offset, uint8_t data)
{
	uint8_t address = offset + BQ27421_EXTENDED_BLOCKDATA;
	return i2cWriteBytes(address, &data, 1);
}
#endif


/*============================================================================*/
/**
  * @brief Read all 32 bytes of the loaded extended data and compute a
  * checksum based on the values.
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_computeBlockChecksum(void)
{
	//uint8_t Blockdata[32];
	memset(Blockdata,0,32);
	bq27421_i2c_read_data_block( 0, Blockdata, 32 );
	//i2cReadBytes(BQ27421_EXTENDED_BLOCKDATA, Blockdata, 32);

	uint8_t csum = 0;
	for (int i=0; i<32; i++)
	{
		csum += Blockdata[i];
	}
	csum = 255 - csum;

	return csum;
}


#if 0
/*============================================================================*/
/**
  * @brief Use the BlockDataCheckSum() command to write a checksum value
  * @param
  * @retval
  */
/*============================================================================*/
bool bq27421_writeBlockChecksum(uint8_t csum)
{
	return i2cWriteBytes(BQ27421_EXTENDED_CHECKSUM, &csum, 1);
}
#endif


/*============================================================================*/
/**
  * @brief Read a byte from extended data specifying a class ID and position offset
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_checkExtendedData(uint8_t offset)
{
	uint8_t chksum = 0;
	uint8_t classID = 0x52;
	//uint8_t retData = 0;
	//uint16_t
	//if (!_userConfigControl) enterConfig(false);

	if (!bq27421_blockDataControl()) // // enable block data memory control
		return false; // Return false if enable fails
	if (!bq27421_blockDataClass(classID)) // Write class ID using DataBlockClass()
		return false;

	bq27421_blockDataOffset(offset / 32); // Write 32-bit block offset (usually 0)

	chksum = bq27421_computeBlockChecksum(); // Compute checksum going in
	uint8_t oldCsum = bq27421_blockDataChecksum();

	if(chksum == oldCsum)
	{
		return true;
	}

	return false;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t bq27421_readExtendedData(uint8_t classID, uint8_t offset)
{
	uint8_t retData = 0;
	if (!_userConfigControl) bq27421_enterConfig(false);

	if (!bq27421_blockDataControl()) // // enable block data memory control
		return false; // Return false if enable fails
	if (!bq27421_blockDataClass(classID)) // Write class ID using DataBlockClass()
		return false;

	bq27421_blockDataOffset(offset / 32); // Write 32-bit block offset (usually 0)

	bq27421_computeBlockChecksum(); // Compute checksum going in
	uint8_t oldCsum = bq27421_blockDataChecksum();
	/*for (int i=0; i<32; i++)
		Serial.print(String(readBlockData(i)) + " ");*/
	retData = bq27421_readBlockData(offset % 32); // Read from offset (limit to 0-31)

	if (!_userConfigControl) bq27421_exitConfig(false);

	return retData;
}

