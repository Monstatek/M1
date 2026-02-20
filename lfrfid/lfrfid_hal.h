/* See COPYING.txt for license details. */

/*
 * lfrfid_hal.h
 */

#ifndef LFRFID_HAL_H_
#define LFRFID_HAL_H_

void rfid_read_handler(TIM_HandleTypeDef *htim);
void rfid_emul_handler(TIM_HandleTypeDef *htim);

void lfrfid_read_hw_init(void);
void lfrfid_read_hw_deinit(void);

void lfrfid_emul_hw_init(void);
void lfrfid_emul_hw_deinit(void);

void lfrfid_RFIDOut_Init(uint32_t freq);
void lfrfid_RFIDIn_Init(void);

extern TIM_HandleTypeDef   Timerhdl_RfIdTIM5;
extern TIM_HandleTypeDef   Timerhdl_RfIdTIM3;

extern uint8_t rfid_rxtx_is_taking_this_irq;
extern void lfrfid_isr_init(void);

//***************************************************************************
// emulation
//***************************************************************************
#define ENCODED_DATA_MAX 1200

typedef struct
{
    uint32_t bsrr;
    uint16_t time_us;
} Encoded_Data_t;

typedef struct
{
    Encoded_Data_t *data;
    uint16_t  length;
    uint16_t  index;
} EncodedTx_Data_t;


extern EncodedTx_Data_t lfrfid_encoded_data;

#endif /* LFRFID_HAL_H_ */
