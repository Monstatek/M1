/*
*
*  m1_subghz_fcc.h
*
*  For Sub-GHz FCC test
*
*  B.N @ MonstaTek, Sep 2025
*
*/

#ifndef M1_SUBGHZ_FCC_H_
#define M1_SUBGHZ_FCC_H_

void sub_ghz_fcc_init(void);
void sub_ghz_fcc_exit(void);
void sub_ghz_fcc_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void sub_ghz_fcc_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);
void sub_ghz_fcc_frequency(void);
void sub_ghz_fcc_channel(void);
void sub_ghz_fcc_tx_power(void);

#endif /* M1_SUBGHZ_FCC_H_ */
