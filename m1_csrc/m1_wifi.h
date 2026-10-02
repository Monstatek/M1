/* See COPYING.txt for license details. */

/*
*
* m1_wifi.h
*
* Library for M1 Wifi
*
* M1 Project
*
*/


#ifndef M1_WIFI_H_
#define M1_WIFI_H_

#include "m1_system.h"
#include "ctrl_api.h"

void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
uint8_t wifi_handshake(void);
/* wifi_deauth() (the low-level single-client deauth loop) is internal to
 * m1_wifi.c -- called only from wifi_deauth_client(). */
void wifi_beacon(void);
void wifi_scan_ap(void);
void wifi_survey(void);
void wifi_deauth_client(void);
void wifi_handshake_flow(void);
void wifi_scan_networks(void);
void wifi_saved_browser(void);
void wifi_config(void);
uint8_t wifi_ap_sta_scan_list(ctrl_cmd_t *app_req, uint8_t target);

/* Shared Wi-Fi presentation used by the standard Wi-Fi tools and
 * MonstaShark. These functions draw only; they do not scan, select a radio
 * mode, send a command, or alter transport state. */
void wifi_ui_draw_scanning(const char *msg);
void wifi_ui_draw_ap_card(const char *heading, uint16_t sel, uint16_t count,
                          const char *ssid, const char *bssid, int rssi,
                          int channel, const char *security,
                          const char *status, const char *ok_label,
                          const char *refresh_hint);

#endif /* M1_WIFI_H_ */
