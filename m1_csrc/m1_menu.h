/* See COPYING.txt for license details. */

/*
*
*  m1_menu.h
*
*  Data structure for M1 menu
*
* M1 Project
*
*/

#ifndef M1_MENU_H_
#define M1_MENU_H_

#include <stdint.h>
#include "app_freertos.h"
#include "m1_system.h"

#define MENU_TITLE_LEN          30
#define SUB_MENU_ITEMS_MAX      10
#define SUB_MENU_LEVEL_MAX      5
#define RUN_MENU_ITEMS_MAX      5

#define SUB_MENU_ITEM_INVALID	0xFF

typedef struct S_M1_Menu
{
	char title[MENU_TITLE_LEN];
    void (*sub_func)(void); // Main subfunction
    void (*deinit_func)(void); // This subfunction is called before this menu becomes inactive
    void (*xkey_handler)(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item); // Extra key handling subfunc, if any
    uint8_t num_submenu_items;
    uint8_t reserved;
    const uint8_t *icon_ptr; // Pointer to image buffer for the icon, if any
    // Pointer to the gui update function for this menu item if needed
    void (*gui_menu_update)(const struct S_M1_Menu *phmenu, uint8_t sel_item);
    struct S_M1_Menu *submenu[SUB_MENU_ITEMS_MAX];
} S_M1_Menu_t;

typedef struct
{
    void (*this_func)(void);
    const S_M1_Menu_t *main_menu_ptr[SUB_MENU_LEVEL_MAX];
    uint8_t last_selected_items[SUB_MENU_LEVEL_MAX];
    uint8_t num_menu_items;
    uint8_t menu_level;
    uint8_t menu_item_active; // index of selected menu item
} S_M1_Menu_Control_t;

typedef enum
{
	MENU_UPDATE_NONE = 0, // No display update
	MENU_UPDATE_MOVE_UP, // Menu moves up
	MENU_UPDATE_MOVE_DOWN, // Menu moves down
	MENU_UPDATE_RESET, // Display new menu or sub-menu
	MENU_UPDATE_RESTORE, // Display previous menu
	MENU_UPDATE_REFRESH, // Refresh current menu after a function completes
	MENU_UPDATE_INIT, // Menu initialized
	X_MENU_UPDATE_INIT, // Used for the extra menu of a menu item
	X_MENU_UPDATE_MOVE_UP,
	X_MENU_UPDATE_MOVE_DOWN,
	X_MENU_UPDATE_RESET,
	X_MENU_UPDATE_RESTORE,
	X_MENU_UPDATE_REFRESH
} S_M1_Menu_Update_t;

void menu_main_handler_task(void *param);
void subfunc_handler_task(void *param);
extern TaskHandle_t subfunc_handler_task_hdl;
extern TaskHandle_t menu_main_handler_task_hdl;

#endif /* M1_MENU_H_ */
