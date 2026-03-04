/* See COPYING.txt for license details. */

/*
*
* m1_wifi_cred.h
*
* Encrypted WiFi credential storage on SD card
*
* M1 Project
*
*/

#ifndef M1_WIFI_CRED_H_
#define M1_WIFI_CRED_H_

#include <stdint.h>
#include <stdbool.h>

#define WIFI_CRED_SSID_MAX_LEN     33
#define WIFI_CRED_PASS_MAX_LEN     65
#define WIFI_CRED_MAX_STORED       8
#define WIFI_CRED_FILE_PATH        "0:/System/wifi_creds.bin"

typedef struct {
	char ssid[WIFI_CRED_SSID_MAX_LEN];
	char password[WIFI_CRED_PASS_MAX_LEN];
	bool valid;
} wifi_credential_t;

/* Load all saved credentials from SD card (decrypts automatically) */
uint8_t wifi_cred_load_all(wifi_credential_t *creds, uint8_t max_count);

/* Save a credential (encrypts before storing) */
bool wifi_cred_save(const char *ssid, const char *password);

/* Delete a credential by SSID */
bool wifi_cred_delete(const char *ssid);

/* Find a credential by SSID */
bool wifi_cred_find(const char *ssid, wifi_credential_t *out);

#endif /* M1_WIFI_CRED_H_ */
