/*
 * Espressif Systems Wireless LAN device driver
 *
 * Copyright (C) 2015-2022 Espressif Systems (Shanghai) PTE LTD
 * SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0
 */

/** prevent recursive inclusion **/
#ifndef __CTRL_API_H
#define __CTRL_API_H

#include <stdbool.h>

//#define SUCCESS                             0 // Defined as enum in stm32h5xx.h
#define SUCCESSOK							0
#define FAILURE                             -1

#define MAC_SIZE_BYTES                       6
#define SSID_LENGTH                          33
#define MAX_MAC_STR_SIZE                     18
#define BSSID_STR_SIZE                       MAX_MAC_STR_SIZE
#define PASSWORD_LENGTH                      65
#define BSSID_LENGTH						17
#define STATUS_LENGTH                        14
#define VENDOR_OUI_BUF                       3

#define CALLBACK_SET_SUCCESS                 0
#define CALLBACK_AVAILABLE                   0
#define CALLBACK_NOT_REGISTERED              -1
#define MSG_ID_OUT_OF_ORDER                  -2

#define COUNTRY_CODE_LEN                     3

/* If request is already being served and
 * another request is pending, time period for
 * which new request will wait in seconds
 * */
#define WAIT_TIME_B2B_CTRL_REQ               5
#define DEFAULT_CTRL_RESP_TIMEOUT            30
#define DEFAULT_CTRL_RESP_AP_SCAN_TIMEOUT    (60*3)
#define DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT (15*3)

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define SUCCESS_STR                          "success"
#define FAILURE_STR                          "failure"
#define NOT_CONNECTED_STR                    "not_connected"

#define CTRL_CMD_DEFAULT_REQ() {                          \
  .msg_type = CTRL_REQ,                                   \
  .cmd_timeout_sec = DEFAULT_CTRL_RESP_TIMEOUT /*30 sec*/ \
}


// Defined in esp_hosted_config.pb-c.h
/* --- enums --- */

/*
 * Enums similar to ESP IDF
 */
typedef enum _CtrlVendorIEType {
  CTRL__VENDOR_IETYPE__Beacon = 0,
  CTRL__VENDOR_IETYPE__Probe_req = 1,
  CTRL__VENDOR_IETYPE__Probe_resp = 2,
  CTRL__VENDOR_IETYPE__Assoc_req = 3,
  CTRL__VENDOR_IETYPE__Assoc_resp = 4
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__VENDOR_IETYPE)
} CtrlVendorIEType;
typedef enum _CtrlVendorIEID {
  CTRL__VENDOR_IEID__ID_0 = 0,
  CTRL__VENDOR_IEID__ID_1 = 1
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__VENDOR_IEID)
} CtrlVendorIEID;
typedef enum _CtrlWifiMode {
  CTRL__WIFI_MODE__NONE = 0,
  CTRL__WIFI_MODE__STA = 1,
  CTRL__WIFI_MODE__AP = 2,
  CTRL__WIFI_MODE__APSTA = 3
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__WIFI_MODE)
} CtrlWifiMode;
typedef enum _CtrlWifiBw {
  CTRL__WIFI_BW__BW_Invalid = 0,
  CTRL__WIFI_BW__HT20 = 1,
  CTRL__WIFI_BW__HT40 = 2
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__WIFI_BW)
} CtrlWifiBw;
typedef enum _CtrlWifiPowerSave {
  CTRL__WIFI_POWER_SAVE__PS_Invalid = 0,
  CTRL__WIFI_POWER_SAVE__MIN_MODEM = 1,
  CTRL__WIFI_POWER_SAVE__MAX_MODEM = 2
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__WIFI_POWER_SAVE)
} CtrlWifiPowerSave;
typedef enum _CtrlWifiSecProt {
  CTRL__WIFI_SEC_PROT__Open = 0,
  CTRL__WIFI_SEC_PROT__WEP = 1,
  CTRL__WIFI_SEC_PROT__WPA_PSK = 2,
  CTRL__WIFI_SEC_PROT__WPA2_PSK = 3,
  CTRL__WIFI_SEC_PROT__WPA_WPA2_PSK = 4,
  CTRL__WIFI_SEC_PROT__WPA2_ENTERPRISE = 5,
  CTRL__WIFI_SEC_PROT__WPA3_PSK = 6,
  CTRL__WIFI_SEC_PROT__WPA2_WPA3_PSK = 7
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__WIFI_SEC_PROT)
} CtrlWifiSecProt;
// Defined in esp_hosted_config.pb-c.h - End

/*
 * enums for Control path
 */
typedef enum _CtrlStatus {
  CTRL__STATUS__Connected = 0,
  CTRL__STATUS__Not_Connected = 1,
  CTRL__STATUS__No_AP_Found = 2,
  CTRL__STATUS__Connection_Fail = 3,
  CTRL__STATUS__Invalid_Argument = 4,
  CTRL__STATUS__Out_Of_Range = 5
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL__STATUS)
} CtrlStatus;
typedef enum _CtrlMsgType {
  CTRL_MSG_TYPE__MsgType_Invalid = 0,
  CTRL_MSG_TYPE__Req = 1,
  CTRL_MSG_TYPE__Resp = 2,
  CTRL_MSG_TYPE__Event = 3,
  CTRL_MSG_TYPE__MsgType_Max = 4
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL_MSG_TYPE)
} CtrlMsgType;
typedef enum _CtrlMsgId {
  CTRL_MSG_ID__MsgId_Invalid = 0,
  /*
   ** Request Msgs *
   */
  CTRL_MSG_ID__Req_Base = 100,
  CTRL_MSG_ID__Req_GetMACAddress = 101,
  CTRL_MSG_ID__Req_SetMacAddress = 102,
  CTRL_MSG_ID__Req_GetWifiMode = 103,
  CTRL_MSG_ID__Req_SetWifiMode = 104,
  CTRL_MSG_ID__Req_GetAPScanList = 105,
  CTRL_MSG_ID__Req_GetAPConfig = 106,
  CTRL_MSG_ID__Req_ConnectAP = 107,
  CTRL_MSG_ID__Req_DisconnectAP = 108,
  CTRL_MSG_ID__Req_GetSoftAPConfig = 109,
  CTRL_MSG_ID__Req_SetSoftAPVendorSpecificIE = 110,
  CTRL_MSG_ID__Req_StartSoftAP = 111,
  CTRL_MSG_ID__Req_GetSoftAPConnectedSTAList = 112,
  CTRL_MSG_ID__Req_StopSoftAP = 113,
  CTRL_MSG_ID__Req_SetPowerSaveMode = 114,
  CTRL_MSG_ID__Req_GetPowerSaveMode = 115,
  CTRL_MSG_ID__Req_OTABegin = 116,
  CTRL_MSG_ID__Req_OTAWrite = 117,
  CTRL_MSG_ID__Req_OTAEnd = 118,
  CTRL_MSG_ID__Req_SetWifiMaxTxPower = 119,
  CTRL_MSG_ID__Req_GetWifiCurrTxPower = 120,
  CTRL_MSG_ID__Req_ConfigHeartbeat = 121,
  CTRL_MSG_ID__Req_EnableDisable = 122,
  CTRL_MSG_ID__Req_GetFwVersion = 123,
  CTRL_MSG_ID__Req_SetCountryCode = 124,
  CTRL_MSG_ID__Req_GetCountryCode = 125,
  /* Added by B.N @ Monstatek 2025 */
  CTRL_MSG_ID__Req_BLEScanList = 126,
  CTRL_MSG_ID__Req_BLEAdvertise = 127,
  CTRL_MSG_ID__Req_BLEReset = 128,
  /* End - Added by B.N @ Monstatek 2025 */
  /*
   * Add new control path command response before Req_Max
   * and update Req_Max
   */
  CTRL_MSG_ID__Req_Max = 129,
  /*
   ** Response Msgs *
   */
  CTRL_MSG_ID__Resp_Base = 200,
  CTRL_MSG_ID__Resp_GetMACAddress = 201,
  CTRL_MSG_ID__Resp_SetMacAddress = 202,
  CTRL_MSG_ID__Resp_GetWifiMode = 203,
  CTRL_MSG_ID__Resp_SetWifiMode = 204,
  CTRL_MSG_ID__Resp_GetAPScanList = 205,
  CTRL_MSG_ID__Resp_GetAPConfig = 206,
  CTRL_MSG_ID__Resp_ConnectAP = 207,
  CTRL_MSG_ID__Resp_DisconnectAP = 208,
  CTRL_MSG_ID__Resp_GetSoftAPConfig = 209,
  CTRL_MSG_ID__Resp_SetSoftAPVendorSpecificIE = 210,
  CTRL_MSG_ID__Resp_StartSoftAP = 211,
  CTRL_MSG_ID__Resp_GetSoftAPConnectedSTAList = 212,
  CTRL_MSG_ID__Resp_StopSoftAP = 213,
  CTRL_MSG_ID__Resp_SetPowerSaveMode = 214,
  CTRL_MSG_ID__Resp_GetPowerSaveMode = 215,
  CTRL_MSG_ID__Resp_OTABegin = 216,
  CTRL_MSG_ID__Resp_OTAWrite = 217,
  CTRL_MSG_ID__Resp_OTAEnd = 218,
  CTRL_MSG_ID__Resp_SetWifiMaxTxPower = 219,
  CTRL_MSG_ID__Resp_GetWifiCurrTxPower = 220,
  CTRL_MSG_ID__Resp_ConfigHeartbeat = 221,
  CTRL_MSG_ID__Resp_EnableDisable = 222,
  CTRL_MSG_ID__Resp_GetFwVersion = 223,
  CTRL_MSG_ID__Resp_SetCountryCode = 224,
  CTRL_MSG_ID__Resp_GetCountryCode = 225,
  /* Added by B.N @ Monstatek 2025 */
  CTRL_MSG_ID__Resp_BLEScanList = 226,
  CTRL_MSG_ID__Resp_BLEAdvertise = 227,
  CTRL_MSG_ID__Resp_BLEReset = 228,
  /* End - Added by B.N @ Monstatek 2025 */
  /*
   * Add new control path command response before Resp_Max
   * and update Resp_Max
   */
  CTRL_MSG_ID__Resp_Max = 229,
  /*
   ** Event Msgs *
   */
  CTRL_MSG_ID__Event_Base = 300,
  CTRL_MSG_ID__Event_ESPInit = 301,
  CTRL_MSG_ID__Event_Heartbeat = 302,
  CTRL_MSG_ID__Event_StationDisconnectFromAP = 303,
  CTRL_MSG_ID__Event_StationDisconnectFromESPSoftAP = 304,
  CTRL_MSG_ID__Event_StationConnectedToAP = 305,
  CTRL_MSG_ID__Event_StationConnectedToESPSoftAP = 306,
  /*
   * Add new control path command notification before Event_Max
   * and update Event_Max
   */
  CTRL_MSG_ID__Event_Max = 307
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(CTRL_MSG_ID)
} CtrlMsgId;
typedef enum _HostedFeature {
  HOSTED_FEATURE__Hosted_InvalidFeature = 0,
  HOSTED_FEATURE__Hosted_Wifi = 1,
  HOSTED_FEATURE__Hosted_Bluetooth = 2
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(HOSTED_FEATURE)
} HostedFeature;


/*---- Control structures ----*/

typedef enum {

	CTRL_MSGTYPE_INVALID = CTRL_MSG_TYPE__MsgType_Invalid,
	CTRL_REQ = CTRL_MSG_TYPE__Req,
	CTRL_RESP = CTRL_MSG_TYPE__Resp,
	CTRL_EVENT = CTRL_MSG_TYPE__Event,
	CTRL_MSGTYPE_MAX = CTRL_MSG_TYPE__MsgType_Max,
//    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(HOSTED_FEATURE)
} AppMsgType_e;

typedef enum {

	CTRL_MSGID_INVALID = CTRL_MSG_ID__MsgId_Invalid,
	/*
	 ** Request Msgs *
	 */
	CTRL_REQ_BASE                      = CTRL_MSG_ID__Req_Base,
	CTRL_REQ_GET_MAC_ADDR              = CTRL_MSG_ID__Req_GetMACAddress, //0x65
	CTRL_REQ_SET_MAC_ADDR              = CTRL_MSG_ID__Req_SetMacAddress, //0x66
	CTRL_REQ_GET_WIFI_MODE             = CTRL_MSG_ID__Req_GetWifiMode,   //0x67
	CTRL_REQ_SET_WIFI_MODE             = CTRL_MSG_ID__Req_SetWifiMode,   //0x68

	CTRL_REQ_GET_AP_SCAN_LIST          = CTRL_MSG_ID__Req_GetAPScanList, //0x69
	CTRL_REQ_GET_AP_CONFIG             = CTRL_MSG_ID__Req_GetAPConfig,   //0x6a
	CTRL_REQ_CONNECT_AP                = CTRL_MSG_ID__Req_ConnectAP,     //0x6b
	CTRL_REQ_DISCONNECT_AP             = CTRL_MSG_ID__Req_DisconnectAP,  //0x6c

	CTRL_REQ_GET_SOFTAP_CONFIG         = CTRL_MSG_ID__Req_GetSoftAPConfig,            //0x6d
	CTRL_REQ_SET_SOFTAP_VND_IE         = CTRL_MSG_ID__Req_SetSoftAPVendorSpecificIE,  //0x6e
	CTRL_REQ_START_SOFTAP              = CTRL_MSG_ID__Req_StartSoftAP,                //0x6f
	CTRL_REQ_GET_SOFTAP_CONN_STA_LIST  = CTRL_MSG_ID__Req_GetSoftAPConnectedSTAList,  //0x70
	CTRL_REQ_STOP_SOFTAP               = CTRL_MSG_ID__Req_StopSoftAP,                 //0x71

	CTRL_REQ_SET_PS_MODE               = CTRL_MSG_ID__Req_SetPowerSaveMode,   //0x72
	CTRL_REQ_GET_PS_MODE               = CTRL_MSG_ID__Req_GetPowerSaveMode,   //0x73

	CTRL_REQ_OTA_BEGIN                 = CTRL_MSG_ID__Req_OTABegin,           //0x74
	CTRL_REQ_OTA_WRITE                 = CTRL_MSG_ID__Req_OTAWrite,           //0x75
	CTRL_REQ_OTA_END                   = CTRL_MSG_ID__Req_OTAEnd,             //0x76

	CTRL_REQ_SET_WIFI_MAX_TX_POWER     = CTRL_MSG_ID__Req_SetWifiMaxTxPower,  //0x77
	CTRL_REQ_GET_WIFI_CURR_TX_POWER    = CTRL_MSG_ID__Req_GetWifiCurrTxPower, //0x78

	CTRL_REQ_CONFIG_HEARTBEAT          = CTRL_MSG_ID__Req_ConfigHeartbeat,    //0x79
	CTRL_REQ_ENABLE_DISABLE            = CTRL_MSG_ID__Req_EnableDisable,      //0x7a

	CTRL_REQ_GET_FW_VERSION            = CTRL_MSG_ID__Req_GetFwVersion,       //0x7b

	CTRL_REQ_SET_COUNTRY_CODE          = CTRL_MSG_ID__Req_SetCountryCode,     //0x7c
	CTRL_REQ_GET_COUNTRY_CODE          = CTRL_MSG_ID__Req_GetCountryCode,     //0x7d

	/* Added by B.N @ Monstatek 2025 */
	CTRL_REQ_GET_BLE_SCAN_LIST		   = CTRL_MSG_ID__Req_BLEScanList,		//0x7e
	CTRL_REQ_SET_BLE_ADVERTISE		   = CTRL_MSG_ID__Req_BLEAdvertise,		//0x7f
	CTRL_REQ_SET_BLE_RESET		   	   = CTRL_MSG_ID__Req_BLEReset,			//0x80
	/* End - Added by B.N @ Monstatek 2025 */
	/*
	 * Add new control path command response before Req_Max
	 * and update Req_Max
	 */
	CTRL_REQ_MAX = CTRL_MSG_ID__Req_Max,
	/*
	 ** Response Msgs *
	 */
	CTRL_RESP_BASE                     = CTRL_MSG_ID__Resp_Base,
	CTRL_RESP_GET_MAC_ADDR             = CTRL_MSG_ID__Resp_GetMACAddress,  //0x65 -> 0xc9
	CTRL_RESP_SET_MAC_ADDRESS          = CTRL_MSG_ID__Resp_SetMacAddress,  //0x66 -> 0xca
	CTRL_RESP_GET_WIFI_MODE            = CTRL_MSG_ID__Resp_GetWifiMode,    //0x67 -> 0xcb
	CTRL_RESP_SET_WIFI_MODE            = CTRL_MSG_ID__Resp_SetWifiMode,    //0x68 -> 0xcc

	CTRL_RESP_GET_AP_SCAN_LIST         = CTRL_MSG_ID__Resp_GetAPScanList,  //0x69 -> 0xcd
	CTRL_RESP_GET_AP_CONFIG            = CTRL_MSG_ID__Resp_GetAPConfig,    //0x6a -> 0xce
	CTRL_RESP_CONNECT_AP               = CTRL_MSG_ID__Resp_ConnectAP,      //0x6b -> 0xdf
	CTRL_RESP_DISCONNECT_AP            = CTRL_MSG_ID__Resp_DisconnectAP,   //0x6c -> 0xd0

	CTRL_RESP_GET_SOFTAP_CONFIG        = CTRL_MSG_ID__Resp_GetSoftAPConfig,           //0x6d -> 0xd1
	CTRL_RESP_SET_SOFTAP_VND_IE        = CTRL_MSG_ID__Resp_SetSoftAPVendorSpecificIE, //0x6e -> 0xd2
	CTRL_RESP_START_SOFTAP             = CTRL_MSG_ID__Resp_StartSoftAP,               //0x6f -> 0xd3
	CTRL_RESP_GET_SOFTAP_CONN_STA_LIST = CTRL_MSG_ID__Resp_GetSoftAPConnectedSTAList, //0x70 -> 0xd4
	CTRL_RESP_STOP_SOFTAP              = CTRL_MSG_ID__Resp_StopSoftAP,                //0x71 -> 0xd5

	CTRL_RESP_SET_PS_MODE              = CTRL_MSG_ID__Resp_SetPowerSaveMode, //0x72 -> 0xd6
	CTRL_RESP_GET_PS_MODE              = CTRL_MSG_ID__Resp_GetPowerSaveMode, //0x73 -> 0xd7

	CTRL_RESP_OTA_BEGIN                = CTRL_MSG_ID__Resp_OTABegin,         //0x74 -> 0xd8
	CTRL_RESP_OTA_WRITE                = CTRL_MSG_ID__Resp_OTAWrite,         //0x75 -> 0xd9
	CTRL_RESP_OTA_END                  = CTRL_MSG_ID__Resp_OTAEnd,           //0x76 -> 0xda

	CTRL_RESP_SET_WIFI_MAX_TX_POWER     = CTRL_MSG_ID__Resp_SetWifiMaxTxPower,  //0x77 -> 0xdb
	CTRL_RESP_GET_WIFI_CURR_TX_POWER    = CTRL_MSG_ID__Resp_GetWifiCurrTxPower, //0x78 -> 0xdc

	CTRL_RESP_CONFIG_HEARTBEAT          = CTRL_MSG_ID__Resp_ConfigHeartbeat,    //0x79 -> 0xdd
	CTRL_RESP_ENABLE_DISABLE            = CTRL_MSG_ID__Resp_EnableDisable,      //0x7a -> 0xde

	CTRL_RESP_GET_FW_VERSION            = CTRL_MSG_ID__Resp_GetFwVersion,       //0x7b -> 0xdf

	CTRL_RESP_SET_COUNTRY_CODE          = CTRL_MSG_ID__Resp_SetCountryCode,     //0x7c -> 0xe0
	CTRL_RESP_GET_COUNTRY_CODE          = CTRL_MSG_ID__Resp_GetCountryCode,     //0x7d -> 0xe1

	/* Added by B.N @ Monstatek 2025 */
	CTRL_RESP_GET_BLE_SCAN_LIST        	= CTRL_MSG_ID__Resp_BLEScanList,		//0x7e -> 0xe2
	CTRL_RESP_SET_BLE_ADVERTISE        	= CTRL_MSG_ID__Resp_BLEAdvertise,		//0x7f -> 0xe3
	CTRL_RESP_SET_BLE_RESET        		= CTRL_MSG_ID__Resp_BLEReset,			//0x80 -> 0xe4
	/* End - Added by B.N @ Monstatek 2025 */
	/*
	 * Add new control path comm       and response before Resp_Max
	 * and update Resp_Max
	 */
	CTRL_RESP_MAX = CTRL_MSG_ID__Resp_Max,
	/*
	 ** Events
	 */
	CTRL_EVENT_BASE            = CTRL_MSG_ID__Event_Base,
	CTRL_EVENT_ESP_INIT        = CTRL_MSG_ID__Event_ESPInit,
	CTRL_EVENT_HEARTBEAT       = CTRL_MSG_ID__Event_Heartbeat,
	CTRL_EVENT_STATION_DISCONNECT_FROM_AP =
		CTRL_MSG_ID__Event_StationDisconnectFromAP,
	CTRL_EVENT_STATION_DISCONNECT_FROM_ESP_SOFTAP =
		CTRL_MSG_ID__Event_StationDisconnectFromESPSoftAP,
	CTRL_EVENT_STATION_CONNECTED_TO_AP =
		CTRL_MSG_ID__Event_StationConnectedToAP,
	CTRL_EVENT_STATION_CONNECTED_TO_ESP_SOFTAP =
		CTRL_MSG_ID__Event_StationConnectedToESPSoftAP,
	/*
	 * Add new control path command notification before Event_Max
	 * and update Event_Max
	 */
	CTRL_EVENT_MAX = CTRL_MSG_ID__Event_Max,
} AppMsgId_e;

typedef enum {
	WIFI_MODE_NONE = CTRL__WIFI_MODE__NONE,
	WIFI_MODE_STA = CTRL__WIFI_MODE__STA,
	WIFI_MODE_AP = CTRL__WIFI_MODE__AP,
	WIFI_MODE_APSTA = CTRL__WIFI_MODE__APSTA,
	WIFI_MODE_MAX
} wifi_mode_e;

typedef enum {
	WIFI_AUTH_OPEN = CTRL__WIFI_SEC_PROT__Open,
	WIFI_AUTH_WEP = CTRL__WIFI_SEC_PROT__WEP,
	WIFI_AUTH_WPA_PSK = CTRL__WIFI_SEC_PROT__WPA_PSK,
	WIFI_AUTH_WPA2_PSK = CTRL__WIFI_SEC_PROT__WPA2_PSK,
	WIFI_AUTH_WPA_WPA2_PSK = CTRL__WIFI_SEC_PROT__WPA_WPA2_PSK,
	WIFI_AUTH_WPA2_ENTERPRISE = CTRL__WIFI_SEC_PROT__WPA2_ENTERPRISE,
	WIFI_AUTH_WPA3_PSK = CTRL__WIFI_SEC_PROT__WPA3_PSK,
	WIFI_AUTH_WPA2_WPA3_PSK = CTRL__WIFI_SEC_PROT__WPA2_WPA3_PSK,
	WIFI_AUTH_MAX,
} wifi_auth_mode_e;

typedef enum {
	WIFI_BW_HT20 = CTRL__WIFI_BW__HT20,
	WIFI_BW_HT40 = CTRL__WIFI_BW__HT40,
} wifi_bandwidth_e;

typedef enum {
	WIFI_PS_MIN_MODEM = CTRL__WIFI_POWER_SAVE__MIN_MODEM,
	WIFI_PS_MAX_MODEM = CTRL__WIFI_POWER_SAVE__MAX_MODEM,
	WIFI_PS_INVALID,
} wifi_ps_type_e;

typedef enum {
	WIFI_VND_IE_TYPE_BEACON      = CTRL__VENDOR_IETYPE__Beacon,
	WIFI_VND_IE_TYPE_PROBE_REQ   = CTRL__VENDOR_IETYPE__Probe_req,
	WIFI_VND_IE_TYPE_PROBE_RESP  = CTRL__VENDOR_IETYPE__Probe_resp,
	WIFI_VND_IE_TYPE_ASSOC_REQ   = CTRL__VENDOR_IETYPE__Assoc_req,
	WIFI_VND_IE_TYPE_ASSOC_RESP  = CTRL__VENDOR_IETYPE__Assoc_resp,
} wifi_vendor_ie_type_e;

typedef enum {
	WIFI_VND_IE_ID_0 = CTRL__VENDOR_IEID__ID_0,
	WIFI_VND_IE_ID_1 = CTRL__VENDOR_IEID__ID_1,
} wifi_vendor_ie_id_e;


enum hosted_features_t {
	HOSTED_WIFI = HOSTED_FEATURE__Hosted_Wifi,
	HOSTED_BT = HOSTED_FEATURE__Hosted_Bluetooth,
};

enum {
	CTRL_ERR_NOT_CONNECTED = 1,
	CTRL_ERR_NO_AP_FOUND,
	CTRL_ERR_INVALID_PASSWORD,
	CTRL_ERR_INVALID_ARGUMENT,
	CTRL_ERR_OUT_OF_RANGE,
	CTRL_ERR_MEMORY_FAILURE,
	CTRL_ERR_UNSUPPORTED_MSG,
	CTRL_ERR_INCORRECT_ARG,
	CTRL_ERR_PROTOBUF_ENCODE,
	CTRL_ERR_PROTOBUF_DECODE,
	CTRL_ERR_SET_ASYNC_CB,
	CTRL_ERR_TRANSPORT_SEND,
	CTRL_ERR_REQUEST_TIMEOUT,
	CTRL_ERR_REQ_IN_PROG,
	OUT_OF_RANGE
};


typedef struct {
	/* Should be set to WIFI_VENDOR_IE_ELEMENT_ID (0xDD) */
	uint8_t element_id;
	/* Len of all bytes in the element data
	 * following this field. Minimum 4 */
	uint8_t length;
	/* Vendor identifier (OUI) */
	uint8_t vendor_oui[VENDOR_OUI_BUF];
	/* Vendor-specific OUI type */
	uint8_t vendor_oui_type;
	/*length of payload field*/
	uint16_t payload_len;
	/* Payload. Length is equal to value in 'length' field, minus 4 */
	uint8_t* payload;

} vendor_ie_data_t;

typedef struct {
	uint8_t ssid[SSID_LENGTH];
	uint8_t bssid[BSSID_STR_SIZE];
	int rssi;
	int channel;
	int encryption_mode;
} wifi_scanlist_t;

typedef struct {
	uint8_t bssid[BSSID_STR_SIZE];
	int rssi;
} wifi_connected_stations_list_t;

typedef struct {
	int mode;
	char mac[MAX_MAC_STR_SIZE];
} wifi_mac_t;

typedef struct {
	int mode;
} wifi_mode_t;

typedef struct {
	uint8_t ssid[SSID_LENGTH];
	uint8_t pwd[PASSWORD_LENGTH];
	uint8_t bssid[BSSID_STR_SIZE];
	bool is_wpa3_supported;
	int rssi;
	int channel;
	int encryption_mode;
	uint16_t listen_interval;
	char status[STATUS_LENGTH];
	char out_mac[MAX_MAC_STR_SIZE];
	int band_mode;
} wifi_ap_config_t;

typedef struct {
	uint8_t ssid[SSID_LENGTH];
	uint8_t pwd[PASSWORD_LENGTH];
	int channel;
	int encryption_mode;
	int max_connections;
	bool ssid_hidden;
	wifi_bandwidth_e bandwidth;
	char out_mac[MAX_MAC_STR_SIZE];
	int band_mode;
} softap_config_t;

typedef struct {
	int count;
	/* dynamic size */
	wifi_scanlist_t *out_list;
} wifi_ap_scan_list_t;

typedef struct {
	int count;
	/* dynamic list*/
	wifi_connected_stations_list_t *out_list;
} wifi_softap_conn_sta_list_t;

typedef struct {
	int ps_mode;
} wifi_power_save_t;

typedef struct {
	bool enable;
	wifi_vendor_ie_type_e type;
	wifi_vendor_ie_id_e idx;
	vendor_ie_data_t vnd_ie;
} wifi_softap_vendor_ie_t;

typedef struct {
	uint8_t *ota_data;
	uint32_t ota_data_len;
} ota_write_t;

typedef struct {
	int power;
} wifi_tx_power_t;

typedef struct {
	char project_name[3];
	uint8_t major_1;
	uint8_t major_2;
	uint8_t minor;
	uint8_t revision_patch_1;
	uint8_t revision_patch_2;
} fw_version_t;

typedef struct {
	char country[COUNTRY_CODE_LEN];
	bool ieee80211d_enabled;
} country_code_t;

typedef struct {
	HostedFeature feature;
	uint8_t enable;
} feature_enable_disable_t;

typedef struct {
	/* event */
	uint32_t hb_num;
	/* Req */
	uint8_t enable;
	uint32_t duration;
} event_heartbeat_t;

typedef struct {
	uint8_t ssid[SSID_LENGTH];
	uint32_t ssid_len;
	uint8_t bssid[BSSID_STR_SIZE];
	int channel;
	int authmode;
	int aid;
} event_sta_conn_t;

typedef struct {
	uint8_t ssid[SSID_LENGTH];
	uint32_t ssid_len;
	uint8_t bssid[BSSID_STR_SIZE];
	uint32_t reason;
	int32_t rssi;
} event_sta_disconn_t;

typedef struct {
	uint8_t mac[MAX_MAC_STR_SIZE];
	int32_t aid;
	int32_t is_mesh_child;
} event_softap_sta_conn_t;

typedef struct {
	uint8_t mac[MAX_MAC_STR_SIZE];
	int32_t aid;
	int32_t is_mesh_child;
	uint32_t reason;
} event_softap_sta_disconn_t;

typedef struct Ctrl_cmd_t {
	/* msg type could be 1. req 2. resp 3. notification */
	uint8_t msg_type;
	/* control path protobuf msg number */
	uint16_t msg_id;
	/* uid of request / response */
	int32_t uid;
	/* statusof response or notification */
	int32_t resp_event_status;
	/*AT command buffer*/
	char *at_cmd;
	/*AT command length*/
	uint16_t cmd_len;
	/*Expected response for the AT command*/
	char *cmd_resp;
	union {
		wifi_ap_scan_list_t         wifi_ap_scan;
		wifi_ap_config_t            wifi_ap_config;
	}u;
	/* Wait for timeout duration, if response not received,
	 * it will send timeout response.
	 * Default value for this time out is DEFAULT_CTRL_RESP_TIMEOUT */
	int cmd_timeout_sec;
} ctrl_cmd_t;

#endif
