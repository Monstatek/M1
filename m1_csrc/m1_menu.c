/* See COPYING.txt for license details. */

/*
*
*  m1_menu.c
*
*  M1 menu handler
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h"
//#include "mui.h"
//#include "u8x8.h"
//#include "U8g2lib.h"
#include "m1_gpio.h"
#include "m1_infrared.h"
#include "m1_nfc.h"
#include "m1_rfid.h"
#include "m1_settings.h"
#include "m1_sub_ghz.h"
#include "m1_power_ctl.h"
#include "m1_fw_update.h"
#include "m1_esp32_fw_update.h"
#include "m1_storage.h"
#include "m1_wifi.h"
#include "m1_bt.h"
#include "m1_ir_remotes.h"
#include "m1_capture_link.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

/************************** S T R U C T U R E S *******************************/

/*----------------------------- > Sub-GHz ------------------------------------*/

S_M1_Menu_t menu_Sub_GHz_Scan =
{
    "Scan", sub_ghz_scan, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Record =
{
    "Record", sub_ghz_record, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Replay =
{
    "Replay", sub_ghz_replay, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Frequency_Reader =
{
    "Frequency Reader", sub_ghz_frequency_reader, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_GPIO_Remap =
{
    "Remap Tx pin", sub_ghz_gpio_remap, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz =
{
    "Sub-GHz", NULL, NULL, NULL, 5, 0, menu_m1_icon_wave, NULL,
    {&menu_Sub_GHz_Scan, &menu_Sub_GHz_Record, &menu_Sub_GHz_Replay, &menu_Sub_GHz_Frequency_Reader, &menu_Sub_GHz_GPIO_Remap}
};

/*----------------------------- > 125KHz RFID --------------------------------*/

S_M1_Menu_t menu_125KHz_RFID_Read =
{
    "Read", rfid_125khz_read, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID_Saved =
{
    "Saved", rfid_125khz_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};


S_M1_Menu_t menu_125KHz_RFID_Add_Manually =
{
    "Add", rfid_125khz_add_manually, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID_Utilities =
{
    "Utilities", rfid_125khz_utilities, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID =
{
    "RFID", menu_125khz_rfid_init, menu_125khz_rfid_deinit, NULL, 4, 0, menu_m1_icon_rfid, NULL,
    {&menu_125KHz_RFID_Read, &menu_125KHz_RFID_Saved, &menu_125KHz_RFID_Add_Manually, &menu_125KHz_RFID_Utilities}
};

/*-------------------------------- > NFC -------------------------------------*/

S_M1_Menu_t menu_NFC_Read =
{
    "Read", nfc_read, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Saved =
{
    "Saved", nfc_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Detect_Reader =
{
    "Extract Keys", nfc_detect_reader, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_MFC_Keys =
{
    "MIFARE Classic Keys", nfc_mfc_keys, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_ULC_Keys =
{
    /* Hardware finding: "MIFARE Ultralight Keys" ran off the 128x64
     * display in the NFC Tools list. Label shortened to fit; the
     * internal struct name, launcher function, view mode, dictionary
     * paths and formats are all unchanged. */
    "Ultralight Keys", nfc_ulc_keys, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_PWD_Keys =
{
    "NTAG/UL Passwords", nfc_pwd_keys, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/* "MFC Emulate" (nfc_mfc_emulate(), m1_nfc.c) is intentionally not listed
 * here: it always presents the fixed 01020304 test identity regardless of
 * any loaded card (see m1_mfc_raw_begin()'s unarmed fallback), which
 * duplicates and is weaker than the real saved-card path (Read/Saved ->
 * More -> Emulate). The function itself, its view, and its dev-only
 * dispatch event are left entirely intact for bench testing -- only the
 * production Tools menu entry point to it is removed. */

S_M1_Menu_t menu_NFC_Tools =
{
    "Tools", NULL, NULL, NULL, 3, 0, NULL, NULL, {&menu_NFC_MFC_Keys, &menu_NFC_ULC_Keys, &menu_NFC_PWD_Keys}
};

/* NFC > Add: manual card creation for the six approved types. */
S_M1_Menu_t menu_NFC_Add_MFC1K   = { "MFC 1K",     nfc_add_mfc1k,      NULL, NULL, 0, 0, NULL, NULL, NULL };
S_M1_Menu_t menu_NFC_Add_MFC4K   = { "MFC 4K",     nfc_add_mfc4k,      NULL, NULL, 0, 0, NULL, NULL, NULL };
S_M1_Menu_t menu_NFC_Add_UL      = { "Ultralight", nfc_add_ultralight, NULL, NULL, 0, 0, NULL, NULL, NULL };
S_M1_Menu_t menu_NFC_Add_NTAG213 = { "NTAG213",    nfc_add_ntag213,    NULL, NULL, 0, 0, NULL, NULL, NULL };
S_M1_Menu_t menu_NFC_Add_NTAG215 = { "NTAG215",    nfc_add_ntag215,    NULL, NULL, 0, 0, NULL, NULL, NULL };
S_M1_Menu_t menu_NFC_Add_NTAG216 = { "NTAG216",    nfc_add_ntag216,    NULL, NULL, 0, 0, NULL, NULL, NULL };

S_M1_Menu_t menu_NFC_Add =
{
    "Add", NULL, NULL, NULL, 6, 0, NULL, NULL,
    {&menu_NFC_Add_MFC1K, &menu_NFC_Add_MFC4K, &menu_NFC_Add_UL,
     &menu_NFC_Add_NTAG213, &menu_NFC_Add_NTAG215, &menu_NFC_Add_NTAG216}
};

/* Release build: Extract Keys is not exposed in the user-facing NFC menu.
 * menu_NFC_Detect_Reader and nfc_detect_reader stay in the tree as internal
 * implementation; only the menu entry and the item count are withdrawn, so no
 * surrounding NFC entry changes position or meaning. */
S_M1_Menu_t menu_NFC =
{
    "NFC", &menu_nfc_init, menu_nfc_deinit, NULL, 4, 0, menu_m1_icon_nfc, NULL,
    {&menu_NFC_Read, &menu_NFC_Saved, &menu_NFC_Add, &menu_NFC_Tools }
};

/*----------------------------- > Infrared -----------------------------------*/

S_M1_Menu_t menu_Infrared_Universal_TVs =
{
    "TV remotes", infrared_universal_tv_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Universal_Audios =
{
    "Audio player remotes", infrared_universal_audio_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Universal_Projectors =
{
    "Projector remotes", infrared_universal_projector_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Universal_ACs =
{
    "AC remotes", infrared_universal_ac_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Universal_Remotes =
{
    "Universal Remotes", NULL, NULL, NULL, 4, 0, NULL, NULL,
	{&menu_Infrared_Universal_TVs, &menu_Infrared_Universal_Audios, &menu_Infrared_Universal_Projectors, &menu_Infrared_Universal_ACs}
};

S_M1_Menu_t menu_Infrared_Learn_New_Remote =
{
    "Learn", infrared_learn_new_remote, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Saved_Remotes =
{
    "Replay", infrared_saved_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Remap_GPIO =
{
    "Remap Tx pin", infrared_remap_gpio, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared =
{
    "Infrared", menu_infrared_init, NULL, NULL, 4, 0, menu_m1_icon_infrared, NULL,
    {&menu_Infrared_Universal_Remotes, &menu_Infrared_Learn_New_Remote, &menu_Infrared_Saved_Remotes, &menu_Infrared_Remap_GPIO}
};

/*------------------------------- > GPIO -------------------------------------*/

S_M1_Menu_t menu_GPIO_GPIO_Manual_Control =
{
    "GPIO Control", gpio_manual_control, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_3_3V_On_GPIO =
{
    "3.3V power", gpio_3_3v_on_gpio, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_5V_On_GPIO =
{
    "5V power", gpio_5v_on_gpio, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_USB_UART =
{
    "USB-UART bridge", gpio_usb_uart_bridge, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO =
{
    "GPIO", menu_gpio_init, menu_gpio_exit, gpio_xkey_handler, 4, 0, menu_m1_icon_gpio, gpio_gui_update,
    {&menu_GPIO_GPIO_Manual_Control, &menu_GPIO_3_3V_On_GPIO, &menu_GPIO_5V_On_GPIO, &menu_GPIO_USB_UART}
};

/*------------------------------- > Settings ---------------------------------*/

/*------------------------- > Settings-Storage -------------------------------*/

S_M1_Menu_t menu_Setting_Storage_About =
{
    "About SD Card", storage_about, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/* No Explore/Mount/Unmount items here: boot/home LEFT is the sole surviving
 * path to storage_explore(); Mount and Unmount had no caller anywhere else
 * in the tree, so storage_mount()/storage_unmount() were removed entirely. */

/* USB Drive = the USB Mass Storage (MSC) SD-card exposure, re-homed from
 * Settings > Storage into the new top-level USB category. storage_usbmsc is the
 * MSC lifecycle handler (safe SD ownership handoff); user-facing name = "USB Drive". */
S_M1_Menu_t menu_USB_Drive =
{
    "USB Drive", storage_usbmsc, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_USB =
{
    "USB", NULL, NULL, NULL, 1, 0, menu_m1_icon_usb, NULL, {&menu_USB_Drive}
};

S_M1_Menu_t menu_Setting_Storage_Format =
{
    "Format SD Card", storage_format, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*------------------------- > Settings-Storage-End ---------------------------*/

/*------------------------- > Settings-Power ---------------------------------*/

S_M1_Menu_t menu_Setting_Power_Info =
{
    "Battery Info", power_battery_info, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Power_Reboot =
{
    "Reboot", power_reboot, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Power_Off =
{
    "Power Off", power_off, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*----------------------- > Settings-Power-End -------------------------------*/


/*---------------------- > Settings-Firmware Update --------------------------*/

S_M1_Menu_t menu_Setting_Firmware_Update_Image_File =
{
    "Image File", firmware_update_get_image_file, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Firmware_Update_Start =
{
    "Firmware update", firmware_update_start, NULL, NULL, 0, 0, NULL, NULL, NULL
};


/*---------------------- > Settings-Firmware Update-End ----------------------*/


/*---------------------- > Settings-ESP32 Update --------------------------*/

S_M1_Menu_t menu_Setting_ESP32_Image_File =
{
    "Image File", setting_esp32_image_file, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_ESP32_Start_Address =
{
    "Start Address", setting_esp32_start_address, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_ESP32_Firmware_Update =
{
    "Firmware Update", setting_esp32_firmware_update, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*---------------------- > Settings-ESP32 Update-End ----------------------*/

S_M1_Menu_t menu_Settings_Storage =
{
    "Storage", menu_setting_storage_init, NULL, NULL, 2, 0, NULL, NULL, {&menu_Setting_Storage_About, &menu_Setting_Storage_Format}
};

S_M1_Menu_t menu_Settings_Power =
{
    "Power", menu_setting_power_init, NULL, NULL, 3, 0, NULL, NULL, {&menu_Setting_Power_Info, &menu_Setting_Power_Reboot, &menu_Setting_Power_Off}
};

S_M1_Menu_t menu_Setting_Firmware_Update =
{
    "Firmware update", firmware_update_init, firmware_update_exit, NULL, 2, 0, NULL, firmware_update_gui_update, {&menu_Setting_Firmware_Update_Image_File, &menu_Setting_Firmware_Update_Start}
};

S_M1_Menu_t menu_Setting_ESP32 =
{
    "ESP32 update", setting_esp32_init, setting_esp32_exit, setting_esp32_xkey_handler, 3, 0, NULL, setting_esp32_gui_update, {&menu_Setting_ESP32_Image_File, &menu_Setting_ESP32_Start_Address, &menu_Setting_ESP32_Firmware_Update}
};

S_M1_Menu_t menu_Settings_About =
{
    "About", settings_about, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Settings =
{
    "Settings", menu_settings_init, NULL, NULL, 5, 0, menu_m1_icon_setting, NULL,
    {&menu_Settings_Storage, &menu_Settings_Power, &menu_Setting_Firmware_Update, &menu_Setting_ESP32, &menu_Settings_About}
};

/*--------------------------------- > Wifi -----------------------------------*/

/* Networks is a leaf (like Deauth/Handshake/Beacon/Survey) so its BACK uses the
 * same proven parent-menu return path (-> Wi-Fi menu). Saved Networks is reached
 * from the in-function Network Actions menu, not a nested menu-system submenu. */
S_M1_Menu_t menu_Wifi_Networks =
{
    "Networks", wifi_scan_networks, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Deauth =
{
    "Deauth Client", wifi_deauth_client, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Handshake =
{
    "Handshake", wifi_handshake_flow, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Beacon =
{
    "Beacon", wifi_beacon, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Survey =
{
    "Channel Survey", wifi_survey, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/* MonstaShark stays under Wi-Fi so it shares the AP scan session with the
 * other Wi-Fi tools; navigation between them does not cross menu_wifi_exit. */
S_M1_Menu_t menu_Capture_Network =
{
    "MonstaShark", m1_capture_network_screen, NULL, NULL, 0, 0, menu_m1_icon_wifi, NULL, NULL
};

/* gui_menu_update = NULL uses the standard scrolling submenu renderer
 * (m1_gui_submenu_update). MonstaShark is third in the Wi-Fi menu. */
S_M1_Menu_t menu_Wifi =
{
    "Wi-Fi", menu_wifi_init, menu_wifi_exit, NULL, 6, 0, menu_m1_icon_wifi, NULL,
    {&menu_Wifi_Networks, &menu_Wifi_Deauth, &menu_Capture_Network, &menu_Wifi_Handshake,
     &menu_Wifi_Beacon, &menu_Wifi_Survey}
};

/*------------------------------ > Bluetooth ---------------------------------*/
S_M1_Menu_t menu_Bluetooth_ScanDevices =
{
    "Scan Devices", bluetooth_scan_devices, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/* Device Details is not a top-level function: it is the inspector reached by pressing
 * CENTER on a scanned device inside Scan Devices (bt_detail_view). */

S_M1_Menu_t menu_Bluetooth_SignalMeter =
{
    "Signal Meter", bluetooth_signal_meter, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_GattExplorer =
{
    "GATT Explorer", bluetooth_gatt_explorer, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Saved =
{
    "Saved", bluetooth_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};


S_M1_Menu_t menu_Bluetooth =
{
    "Bluetooth", menu_bluetooth_init, menu_bluetooth_exit, NULL, 4, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_ScanDevices, &menu_Bluetooth_SignalMeter, &menu_Bluetooth_GattExplorer, &menu_Bluetooth_Saved}
};

/* NOTE: MonstaShark capture moved INTO the Wi-Fi menu (menu_Capture_Network /
 * menu_Capture_Test are now children of menu_Wifi above) so it shares the AP
 * scan session with the other Wi-Fi tools. The former top-level "Capture" menu
 * and its main-menu slot were removed. */

/*------------------------------- > MAIN MENU --------------------------------*/

const S_M1_Menu_t menu_Main =
{
    "Main Menu", NULL, NULL, NULL, 9, 0, NULL, NULL,
    {&menu_Sub_GHz, &menu_125KHz_RFID, &menu_NFC, &menu_Infrared, &menu_USB, &menu_GPIO, &menu_Wifi, &menu_Bluetooth, &menu_Settings}
};


/***************************** V A R I A B L E S ******************************/

static S_M1_Menu_Control_t		menu_ctl;
static const S_M1_Menu_t 		*pthis_submenu;
TaskHandle_t					subfunc_handler_task_hdl;
TaskHandle_t					menu_main_handler_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void menu_main_init(void);
void menu_main_handler_task(void *param);
void subfunc_handler_task(void *param);
static void m1_quick_power_down(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes the main menu.
*/
/*============================================================================*/
static void menu_main_init(void)
{
    menu_ctl.menu_level = 0; // main menu
    menu_ctl.menu_item_active = 0; // first menu item
    menu_ctl.last_selected_items[0] = 0; // last selected item is the current active item
    menu_ctl.main_menu_ptr[0] = &menu_Main; // level 0 should be the main menu
    menu_ctl.num_menu_items = menu_ctl.main_menu_ptr[0]->num_submenu_items;

    assert(menu_ctl.num_menu_items >= 3);

	pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level];
    menu_ctl.this_func = pthis_submenu->sub_func;

    m1_gui_init();

} // static void menu_main_init(void)



/*============================================================================*/
/*
 * This function handles all tasks of the M1 main menu.
*/
/*============================================================================*/
void menu_main_handler_task(void *param)
{
	uint8_t key, sel_item, n_items;
	uint8_t menu_update_stat;
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	vTaskDelay(POWER_UP_SYS_CONFIG_WAIT_TIME); // Give some time to startup_config_handler() during power-up

	while(1)
	{
		menu_update_stat = MENU_UPDATE_NONE;
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret!=pdTRUE )
			continue;
		if ( q_item.q_evt_type!=Q_EVENT_KEYPAD )
			continue;
		// Notification is only sent to this task when there's any button activity,
		// so it doesn't need to wait when reading the event from the queue
		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if ( ret!=pdTRUE ) // This should never happen!
			continue; // Wait for a new notification when the attempt to read the button event fails

		for (key=0; key<NUM_BUTTONS_MAX; key++)
	    {
	    	if ( this_button_status.event[key]!=BUTTON_EVENT_IDLE )
	        {
	    		switch( key )
	    		{
	    			case BUTTON_OK_KP_ID:
	    				if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	    				{
	    					if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    					{
	                            n_items = pthis_submenu->submenu[menu_ctl.menu_item_active]->num_submenu_items;  // get the number of submenu items of the selected item
	                            menu_ctl.last_selected_items[menu_ctl.menu_level] = menu_ctl.menu_item_active; // save the selected item before going the next menu level
	                            if ( n_items != 0 ) // This menu item has another submenu?
	                            {
	                                menu_ctl.menu_level++; // go to next menu level
	                                menu_ctl.main_menu_ptr[menu_ctl.menu_level] = pthis_submenu->submenu[menu_ctl.menu_item_active];
	                                pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level];
	                                menu_ctl.this_func = pthis_submenu->sub_func;
	                            	menu_ctl.num_menu_items = n_items; // update this field
	                                menu_ctl.menu_item_active = 0; // default for new submenu
	                                sel_item = 0;
	                                if ( menu_ctl.this_func != NULL )
	                                {
	                                    menu_ctl.this_func(); // run the function of the selected submenu item to initialize it
	                                    // This function should complete quickly after initializing the display!!!
	                                } // if ( menu_ctl.this_func != NULL )
	                                menu_update_stat = MENU_UPDATE_RESET;
	                            } // if ( n_items != 0 )

	                            else if ( pthis_submenu->submenu[menu_ctl.menu_item_active]->sub_func != NULL ) // This menu item has no submenu. Does it have a function to run?
	                            {
	                                m1_device_stat.op_mode = M1_OPERATION_MODE_SUB_FUNC_RUNNING;
	                                m1_device_stat.sub_func = pthis_submenu->submenu[menu_ctl.menu_item_active]->sub_func; // let schedule to run the function of the selected submenu item
	                                //this_button_status.event[key].event = BUTTON_EVENT_IDLE; // clear before return
	                                // Notify the sub-function handler
	                                xTaskNotify(subfunc_handler_task_hdl, 0, eNoAction);
	                                // Wait for the sub-function to complete and notify this task from subfunc_handler_task
	                                xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
	                        		m1_device_stat.op_mode = M1_OPERATION_MODE_MENU_ON;
	                                // Return from sub-function. Let update GUI.
	                                sel_item = menu_ctl.menu_item_active;
	                                menu_update_stat = MENU_UPDATE_REFRESH; // The sub-function may have changed the GUI. It needs update.
	                            } // else if ( menu_ctl.this_function != NULL )

	                            else // This case should never happen. It doesn't exist!
	                            {
	                            	assert(("num_menu_items=0, this_func=NULL", FALSE));
	                            }
	                            key = NUM_BUTTONS_MAX; // Exit condition to stop checking other buttons!
	    					} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    					else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON )
	    					{
	    						menu_main_init();
	    						sel_item = 0;
	    						menu_update_stat = MENU_UPDATE_INIT;
	    						m1_device_stat.op_mode = M1_OPERATION_MODE_MENU_ON; // update new state
	    					} // else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON )
#ifdef BUTTON_REPEATED_PRESS_ENABLE
	    					else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_POWER_UP )
	    					{
	    						m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	    						m1_gui_welcome_scr();
	    						; // Change settings/config to exit out of shutdown/sleep state
	    					} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_POWER_UP )
#endif // #ifdef BUTTON_REPEATED_PRESS_ENABLE
	    				} // if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
#ifndef BUTTON_REPEATED_PRESS_ENABLE
	    				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_LCLICK )
	    				{
	    					if ( m1_device_stat.op_mode==M1_OPERATION_MODE_POWER_UP )
	    					{
	    						m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	    						m1_gui_welcome_scr();
	    						; // Change settings/config to exit out of shutdown/sleep state
	    					} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_POWER_UP )
	    					else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    					{
	    						//m1_device_stat.op_mode = OPERATION_MODE_SHUTDOWN; // force to sleep mode immediately
	    						//System_Shutdown();
	    					} // else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    				} // if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_LCLICK )
#endif // #ifndef BUTTON_REPEATED_PRESS_ENABLE
	                    break;

	    			case BUTTON_UP_KP_ID:
						if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						{
							if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // UP and DOWN pressed at the same time?
								break; // Do nothing
							sel_item = menu_ctl.menu_item_active; // take the current active menu item
							if ( sel_item==0 ) // first menu item?
							{
								sel_item = menu_ctl.num_menu_items - 1; // move to last menu item
								//menu_ctl.total_menu_items = menu_ctl.main_menu_ptr[menu_ctl.menu_level]->submenu_items;
							}
							else // not the first item
							{
								sel_item--;
							}
							menu_ctl.menu_item_active = sel_item; // update the active index
							menu_update_stat = MENU_UPDATE_MOVE_UP;
						} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						else
						{
							; // Do something here if necessary. This case may never happen!
						}
	    				break;

	    			case BUTTON_DOWN_KP_ID:
						if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						{
							if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // UP and DOWN pressed at the same time?
								break; // Do nothing
	                        sel_item = menu_ctl.menu_item_active; // take the current active menu item
	                        if ( sel_item==(menu_ctl.num_menu_items - 1) ) // last menu item?
	                        {
	                        	sel_item = 0; // move to first menu item
	                        }
	                        else // not the last item
	                        {
	                        	sel_item++;
	                        }
	                        menu_ctl.menu_item_active = sel_item; // update the active index
	                        menu_update_stat = MENU_UPDATE_MOVE_DOWN;
						}
						else
						{
							; // Do something here if necessary. This case may never happen!
							if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_LCLICK )
							{
								m1_buzzer_notification();
							}
						}
	    				break;

	    			case BUTTON_LEFT_KP_ID:
						if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						{
							if ( pthis_submenu->xkey_handler )
								pthis_submenu->xkey_handler(this_button_status.event[BUTTON_LEFT_KP_ID], BUTTON_LEFT_KP_ID, sel_item);
						}
						else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON )
						{
							/* HOME-LEFT opens the general SD-card browser. Route it through the
							 * generic sub-function dispatch (op_mode/sub_func/xTaskNotify) so
							 * storage_explore() runs on subfunc_handler_task (4096-byte stack)
							 * instead of inline on this menu_main_handler_task (1024-byte
							 * stack). Opening a recognized saved file enters the deep native view
							 * handler (nfc_saved/rfid_125khz_saved/sub_ghz_replay); on the small
							 * stack that overflowed and rebooted the device. This is now the
							 * sole entry point into storage_explore() -- the Settings > Storage
							 * "Explore SD Card" menu item that used to be the other one was
							 * removed as a confirmed duplicate. */
							m1_device_stat.op_mode  = M1_OPERATION_MODE_SUB_FUNC_RUNNING;
							m1_device_stat.sub_func = storage_explore;
							xTaskNotify(subfunc_handler_task_hdl, 0, eNoAction);
							xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
							m1_device_stat.op_mode  = M1_OPERATION_MODE_DISPLAY_ON;
							m1_gui_welcome_scr();
						}
	    				break;

	    			case BUTTON_RIGHT_KP_ID:
						if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						{
							if ( pthis_submenu->xkey_handler )
								pthis_submenu->xkey_handler(this_button_status.event[BUTTON_RIGHT_KP_ID], BUTTON_RIGHT_KP_ID, sel_item);
						}
						else
						{
							; // Do something here if necessary. This case may never happen!
						}
	    				break;

	    			case BUTTON_BACK_KP_ID:
						if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						{
							if ( menu_update_stat!=MENU_UPDATE_NONE ) // Other buttons pressed?
								break; // Do nothing, let other buttons take their higher priority!
							if ( menu_ctl.menu_level==0 ) // already at main menu screen?
							{
	    						m1_device_stat.op_mode = M1_OPERATION_MODE_DISPLAY_ON; // update new state
	    						m1_gui_welcome_scr();
								; // Do something before going to default home screen
								//
							} // if ( menu_ctl.menu_level==0 )
							else
							{
								if ( menu_ctl.num_menu_items ) // Submenu with active items?
								{
									if ( pthis_submenu->deinit_func )
										pthis_submenu->deinit_func(); // Run deinit function of this submenu before leaving
								} // if ( menu_ctl.num_menu_items )
								menu_ctl.menu_level--; // go back one level
								menu_ctl.menu_item_active = menu_ctl.last_selected_items[menu_ctl.menu_level]; // restore  previous selected item of the upper menu level
								pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level]; // save the current menu level index
								menu_ctl.this_func = pthis_submenu->sub_func;
								menu_ctl.num_menu_items = pthis_submenu->num_submenu_items;
								n_items = menu_ctl.num_menu_items;
								sel_item = menu_ctl.menu_item_active;
								if ( menu_ctl.this_func != NULL )
								{
									menu_ctl.this_func(); // run the function of the selected submenu item to initialize it
									// It's not necessary to set the flag sub_func_is_running here.
									// This function should complete quickly after initializing the display!!!
								} // if ( menu_ctl.this_func != NULL )
								menu_update_stat = MENU_UPDATE_RESTORE;
							} // else
						} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
						else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON)
						{
							if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_LCLICK )
							{
								m1_quick_power_down();
							}
						}
	    				break;

	    			default: // undefined buttons, or buttons do not exist.
	    				break;
	    		} // switch( key )

	        } // if ( this_button_status.event[key]!=BUTTON_EVENT_IDLE )
	    } // for (key=0; key<NUM_BUTTONS_MAX; key++)

	    if ( menu_update_stat!=MENU_UPDATE_NONE )
	    	m1_gui_menu_update(pthis_submenu, sel_item, menu_update_stat);
	} // while(1)

} // void menu_main_handler_task(void *param)



/*============================================================================*/
/*
 * This function handles the quick power-down option from the welcome screen
*/
/*============================================================================*/
static void m1_quick_power_down(void)
{
	S_M1_Main_Q_t q_item;
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	power_shutdown_gui_update(0);
	// Let wait here until user releases the BACK button.
	while ( true )
	{
		vTaskDelay(100); // Return some time to the system
		if ( m1_button_event_check(BUTTON_BACK_KP_ID, BUTTON_EVENT_IDLE) ) // Button released?
			break;
	} // while ( true )
	xQueueReceive(button_events_q_hdl, &this_button_status, 0); // Remove old events, if any, from the keypad queue
	xQueueReset(main_q_hdl); // Reset old events from main queue, if any

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( ret!=pdTRUE ) // This should never happen!
					continue; // Wait for a new notification when the attempt to read the button event fails
				if ( (this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) ||
					 (this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK) ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					m1_gui_welcome_scr();
					break;
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Power off?
				{
					m1_power_down();
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		} // if (ret==pdTRUE)
	} // while (1)

} // static void m1_quick_power_down(void)



/*============================================================================*/
/*
 * This task handles the execution of any sub-function
*/
/*============================================================================*/
void subfunc_handler_task(void *param)
{
	while(1)
	{
		// Waiting for notification from menu_main_handler_task,
		// or from button_event_handler_task
		xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
		assert(m1_device_stat.sub_func!=NULL);
		// Run the sub-function
		m1_device_stat.sub_func();
		// Sub-function completes, let notify menu_main_handler_task
		xTaskNotify(menu_main_handler_task_hdl, 0, eNoAction);
	} // while(1)
} // void subfunc_handler_task(void *param)
