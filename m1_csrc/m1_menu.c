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

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

/************************** S T R U C T U R E S *******************************/

/*----------------------------- > Sub-GHz ------------------------------------*/

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

S_M1_Menu_t menu_Sub_GHz_Regional_Information =
{
    "Regional Information", sub_ghz_regional_information, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Radio_Settings =
{
    "Radio Settings", sub_ghz_radio_settings, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz =
{
    "Sub-GHz", NULL, NULL, NULL, 4, 0, menu_m1_icon_wave, NULL,
    {&menu_Sub_GHz_Record, &menu_Sub_GHz_Replay, &menu_Sub_GHz_Frequency_Reader, &menu_Sub_GHz_Regional_Information}
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

S_M1_Menu_t menu_NFC_Tools =
{
    "Tools", nfc_tools, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC =
{
    "NFC", &menu_nfc_init, menu_nfc_deinit, NULL, 3, 0, menu_m1_icon_nfc, NULL,
    {&menu_NFC_Read, &menu_NFC_Saved, &menu_NFC_Tools }
};

/*----------------------------- > Infrared -----------------------------------*/

S_M1_Menu_t menu_Infrared_Universal_Remotes =
{
    "Universal Remotes", infrared_universal_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Learn_New_Remote =
{
    "Learn", infrared_learn_new_remote, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Saved_Remotes =
{
    "Replay", infrared_saved_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared =
{
    "Infrared", menu_infrared_init, NULL, NULL, 3, 0, menu_m1_icon_infrared, NULL,
    {&menu_Infrared_Universal_Remotes, &menu_Infrared_Learn_New_Remote, &menu_Infrared_Saved_Remotes}
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

S_M1_Menu_t menu_Setting_Storage_Explore =
{
    "Explore SD Card", storage_explore, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Mount =
{
    "Mount SD Card", storage_mount, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Unmount =
{
    "Unmount SD Card", storage_unmount, NULL, NULL, 0, 0, NULL, NULL, NULL
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

S_M1_Menu_t menu_Setting_Firmware_Update_USB_DFU =
{
    "USB DFU mode", firmware_update_usb_dfu_mode, NULL, NULL, 0, 0, NULL, NULL, NULL
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

S_M1_Menu_t menu_Settings_LCD_and_Notifications =
{
    "LCD and Notifications", settings_lcd_and_notifications, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Settings_Storage =
{
    "Storage", menu_setting_storage_init, NULL, NULL, 5, 0, NULL, NULL, {&menu_Setting_Storage_About, &menu_Setting_Storage_Explore, &menu_Setting_Storage_Mount, &menu_Setting_Storage_Unmount, &menu_Setting_Storage_Format}
};

S_M1_Menu_t menu_Settings_Power =
{
    "Power", menu_setting_power_init, NULL, NULL, 3, 0, NULL, NULL, {&menu_Setting_Power_Info, &menu_Setting_Power_Reboot, &menu_Setting_Power_Off}
};

S_M1_Menu_t menu_Settings_System =
{
    "System", settings_system, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Firmware_Update =
{
    "Firmware update", firmware_update_init, firmware_update_exit, NULL, 3, 0, NULL, firmware_update_gui_update, {&menu_Setting_Firmware_Update_Image_File, &menu_Setting_Firmware_Update_Start, &menu_Setting_Firmware_Update_USB_DFU}
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
    /*{&menu_Settings_LCD_and_Notifications,*/ {&menu_Settings_Storage, &menu_Settings_Power,/* &menu_Settings_System,*/ &menu_Setting_Firmware_Update, &menu_Setting_ESP32, &menu_Settings_About}
};

/*--------------------------------- > Wifi -----------------------------------*/

S_M1_Menu_t menu_Wifi_Config =
{
    "Config", wifi_config, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Scan_AP =
{
    "Scan AP", wifi_scan_ap, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi =
{
    "Wifi", menu_wifi_init, NULL, NULL, 2, 0, menu_m1_icon_wifi, NULL,
    {&menu_Wifi_Config, &menu_Wifi_Scan_AP}
};

/*--------------------------------- > Wifi -----------------------------------*/
S_M1_Menu_t menu_Bluetooth_Config =
{
    "Config", bluetooth_config, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Scan =
{
    "Scan", bluetooth_scan, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Advertise =
{
    "Advertise", bluetooth_advertise, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth =
{
    "Bluetooth", menu_bluetooth_init, NULL, NULL, 3, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_Config, &menu_Bluetooth_Scan, &menu_Bluetooth_Advertise}
};

/*------------------------------- > MAIN MENU --------------------------------*/

const S_M1_Menu_t menu_Main =
{
    "Main Menu", NULL, NULL, NULL, 8, 0, NULL, NULL,
    {&menu_Sub_GHz, &menu_125KHz_RFID, &menu_NFC, &menu_Infrared, &menu_GPIO, &menu_Wifi, &menu_Bluetooth, &menu_Settings}
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
							storage_explore();
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
						else
						{
							; // Do something here if necessary. This case may never happen!
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
