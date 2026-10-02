/* See COPYING.txt for license details. */

/*
*
*  m1_sub_ghz_api.h
*
*  M1 sub-ghz API
*
* M1 Project
*
*/

#ifndef M1_SUB_GHZ_API_H_
#define M1_SUB_GHZ_API_H_

#include "si446x_cmd.h"
#include "m1_sub_ghz.h"

#define SI446X_GRP_ID_0x22                      0x22
#define SI446X_GRP_ID_0x22_PA_PWR_LVL           0x01 // PA output power level.
#define SI446X_GRP_ID_0x22_PA_BIAS_CLKDUTY      0x02 // PA bias and duty cycle of the Tx clock source.

#define RADIO_SETTING_MODE_IDLE         0x00
#define RADIO_SETTING_MODE_PAPOWER      0x01
#define RADIO_SETTING_MODE_TX     		0x02
#define RADIO_SETTING_MODE_CLEAR_INT    0x03

#define RADIO_POWER_SETTING_LVL1      	0x00
#define RADIO_POWER_SETTING_LVL2      	0x01
#define RADIO_POWER_SETTING_LVL3      	0x02
#define RADIO_POWER_SETTING_LVL4      	0x03

#define RADIO_STATE_IDLE          		0x00
#define RADIO_STATE_RX       			0x01
#define RADIO_STATE_TX  				0x02
#define RADIO_STATE_TX_CW      			0x04

#define SI446X_STATE_NOCHANGE           0 // No change, remain in current state.
#define SI446X_STATE_SLEEP              1 // SLEEP or STANDBY state

#define START_TX_COMPLETE_STATE_MASK			0b11110000
#define START_TX_COMPLETE_STATE_NOCHANGE		0b00000000 // Do not change from previously sent START_TX_COMPLETE_STATE.
#define START_TX_COMPLETE_STATE_SLEEP         	0b00010000 // SLEEP or STANDBY state
#define START_TX_COMPLETE_STATE_READY         	0b00110000 // READY state.
#define START_TX_COMPLETE_STATE_TX            	0b01110000 // TX state
#define START_TX_COMPLETE_STATE_RX            	0b10000000 // RX state

#define MODEM_MOD_TYPE_MASK				0b00000111
#define MODEM_MOD_TYPE_CW				0b00000000
#define MODEM_MOD_TYPE_OOK				0b00000001
#define MODEM_MOD_TYPE_FSK				0b00000010
#define MODEM_MOD_TYPE_GFSK				0b00000011
#define MODEM_MOD_TYPE_2FSK				0b00000100
#define MODEM_MOD_TYPE_2GFSK			0b00000101

#define RADIO_PACKET_LEN_MAX			25 // This number must match the one set in WDS!
#define RF_PACKET_LEN_MAX				40
#define RADIO_XX_CONFIGURATION_DATA_RADIO_PACKET_LENGTH		RADIO_PACKET_LEN_MAX

typedef struct
{
    const uint8_t   *Radio_ConfigurationArray;
    uint8_t   Radio_ChannelNumber;
    uint8_t   Radio_PacketLength;
    uint8_t   Radio_State_After_Power_Up;
    uint16_t   Radio_Delay_Cnt_After_Reset;
    uint8_t   Radio_CustomPayload[RADIO_PACKET_LEN_MAX];
} tRadioConfiguration;

typedef enum
{
    SI446X_SUCCESS = 0,
    SI446X_NO_PATCH,
    SI446X_CTS_TIMEOUT,
    SI446X_PATCH_FAIL,
    SI446X_COMMAND_ERROR
} tRadioResult;

typedef enum
{
	RADIO_ANTENNA_MODE_ISOLATED = 0,
	RADIO_ANTENNA_MODE_RX,
	RADIO_ANTENNA_MODE_TX,
} tRadioAntennaMode;

void radio_init_rx_tx(S_M1_SubGHz_Band, uint8_t mod_type, bool do_reset);

/* Exact-frequency tuning (Record RAW extended frequency list, Phase 14).
 * radio_tune_exact_hz() programs the SI4463 synthesizer to an EXACT integer-Hz
 * frequency by loading the correct base band configuration (outdiv/modem) and
 * overriding FC_INTE/FC_FRAC. It does NOT silently substitute a nearby preset:
 * a non-zero return is an explicit failure the caller must surface.
 * NOTE: this validates the RECEIVE tuning path only. TX output power / RF
 * matching and regional TX enablement are SEPARATE and remain factory-pending. */
#define RADIO_TUNE_OK          0   /* exact frequency programmed                     */
#define RADIO_TUNE_ERR_RANGE   1   /* Hz outside the SI4463 synthesizer coverage     */
#define RADIO_TUNE_ERR_CALC    2   /* tuning calc rejected the value (FC not representable) */
#define RADIO_TUNE_ERR_APPLY   3   /* radio would not accept the config (CTS/apply fault)   */
bool    radio_freq_in_range(uint32_t hz);
uint8_t radio_tune_exact_hz(uint32_t hz, uint8_t mod_type, bool do_reset);

void radio_patch_reinit(void);
uint8_t radio_get_init_state(void);
void radio_set_antenna_mode(tRadioAntennaMode mode);
void Radio_Start_Tx(uint8_t CHANNEL, uint8_t CONDITION, uint16_t TX_LEN);
void SI446x_PowerUp(void);
uint8_t SI446x_Get_Reset_Stat(void);
void SI446x_Change_ModType(uint8_t NEW_MOD_TYPE);
void SI446x_Change_Modem_OOK_PDTC(uint8_t NEW_PDTC);
void SI446x_Get_IntStatus(uint8_t PH_CLR_PEND, uint8_t MODEM_CLR_PEND, uint8_t CHIP_CLR_PEND);
void SI446x_Change_State(uint8_t NEXT_STATE1);
void SI446x_Start_Rx(uint8_t channel);
void SI446x_Change_Radio_Setting(uint8_t mode, uint8_t pa_power);
void SI446x_Set_Tx_Power(uint8_t power);
void SI446x_Select_Frontend(S_M1_SubGHz_Band network);
void SI446x_Start_Tx_CW(uint16_t channel, uint8_t radio_mod_type);
struct si446x_reply_PART_INFO_map *SI446x_PartInfo(void);
struct si446x_reply_REQUEST_DEVICE_STATE_map *SI446x_Request_DeviceState(void);
struct si446x_reply_GET_MODEM_STATUS_map *SI446x_Get_ModemStatus( uint8_t MODEM_CLR_PEND);

extern volatile uint8_t radio_state_flag;
extern volatile uint8_t si446x_nIRQ_active;

#endif // #define M1_SUB_GHZ_API_H_
