/* See COPYING.txt for license details. */

/*
*
*  m1_ir_universal.c
*
*  Universal Remote with Flipper IRDB support
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ir_universal.h"
#include "flipper_file.h"
#include "m1_system.h"
#include "m1_display.h"
#include "m1_infrared.h"
#include "m1_tasks.h"
#include "m1_watchdog.h"
#include "m1_buzzer.h"
#include "m1_file_browser.h"
#include "m1_lcd.h"
#include "m1_led_indicator.h"
#include "irsnd.h"
#include "ff.h"

/*************************** D E F I N E S ************************************/

#define BROWSE_NAMES_MAX     16
#define BROWSE_NAME_MAX_LEN  64

#define DASHBOARD_ITEM_COUNT  4
#define DASHBOARD_ITEM_HEIGHT 12
#define DASHBOARD_START_Y     14

#define LIST_HEADER_HEIGHT    12
#define LIST_ITEM_HEIGHT      10
#define LIST_START_Y          (LIST_HEADER_HEIGHT + 2)
#define LIST_VISIBLE_ITEMS    5

#define FAV_FILE_PATH         "0:/System/ir_favorites.txt"
#define RECENT_FILE_PATH      "0:/System/ir_recent.txt"

#define IR_FILE_EXTENSION     ".ir"

//************************** S T R U C T U R E S *******************************

//****************************** V A R I A B L E S *****************************/

static ir_universal_cmd_t s_commands[IR_UNIVERSAL_MAX_CMDS];
static uint16_t s_cmd_count;
static char s_browse_names[BROWSE_NAMES_MAX][BROWSE_NAME_MAX_LEN];
static uint16_t s_browse_count;
static uint16_t s_browse_page;
static uint16_t s_browse_selection;
static ir_universal_mode_t s_mode;
static char s_current_path[IR_UNIVERSAL_PATH_MAX_LEN];

/* Favorites and recent tracking */
static char s_favorites[IR_UNIVERSAL_MAX_FAVORITES][IR_UNIVERSAL_PATH_MAX_LEN];
static uint8_t s_favorite_count;
static char s_recent[IR_UNIVERSAL_MAX_RECENT][IR_UNIVERSAL_PATH_MAX_LEN];
static uint8_t s_recent_count;

/* Transmit state */
static IRMP_DATA s_tx_irmp_data;

/* Dashboard menu text */
static const char *s_dashboard_items[DASHBOARD_ITEM_COUNT] = {
	"Browse IRDB",
	"Search",
	"Favorites",
	"Recent"
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void dashboard_screen(void);
static void browse_directory(const char *path);
static void show_commands(const char *ir_file_path);
static void transmit_command(const ir_universal_cmd_t *cmd);
static void load_favorites(void);
static void save_favorites(void);
static void add_to_recent(const char *path);
static void load_recent(void);
static void save_recent(void);
static uint16_t scan_directory_page(const char *path, uint16_t page, uint16_t page_size);
static void draw_list_screen(const char *title, uint16_t count, uint16_t selection);
static void draw_dashboard(uint8_t selection);
static void show_favorites_screen(void);
static void show_recent_screen(void);
static bool is_ir_file(const char *fname);
static void path_append(char *base, const char *item);
static void path_go_up(char *path);
static uint16_t parse_ir_file(const char *filepath);
static bool parse_ir_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd);
static uint8_t map_flipper_protocol(const char *name);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/



/*============================================================================*/
/*
 * Initialize the universal remote module
 */
/*============================================================================*/
void ir_universal_init(void)
{
	s_cmd_count = 0;
	s_browse_count = 0;
	s_browse_page = 0;
	s_browse_selection = 0;
	s_mode = IR_UNIVERSAL_MODE_DASHBOARD;
	memset(s_current_path, 0, sizeof(s_current_path));
	s_favorite_count = 0;
	s_recent_count = 0;

	load_favorites();
	load_recent();
} // void ir_universal_init(void)



/*============================================================================*/
/*
 * Deinitialize the universal remote module
 */
/*============================================================================*/
void ir_universal_deinit(void)
{
	save_favorites();
	save_recent();
} // void ir_universal_deinit(void)



/*============================================================================*/
/*
 * Main entry point for the universal remote feature.
 * Called from infrared_universal_remotes() in m1_infrared.c
 */
/*============================================================================*/
void ir_universal_run(void)
{
	ir_universal_init();
	dashboard_screen();
	ir_universal_deinit();
} // void ir_universal_run(void)



/*============================================================================*/
/*
 * Draw the dashboard menu on the 128x64 display
 */
/*============================================================================*/
static void draw_dashboard(uint8_t selection)
{
	uint8_t i;

	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Title bar */
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Universal Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, 12, 128);

	/* Menu items */
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < DASHBOARD_ITEM_COUNT; i++)
	{
		uint8_t y = DASHBOARD_START_Y + (i * DASHBOARD_ITEM_HEIGHT);

		if (i == selection)
		{
			/* Draw selection highlight */
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, DASHBOARD_ITEM_HEIGHT);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 10, s_dashboard_items[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 10, s_dashboard_items[i]);
		}
	}

	/* Bottom bar hint */
	u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 2, 63, "OK:Select");
	u8g2_DrawStr(&m1_u8g2, 80, 63, "Back:Exit");

	m1_u8g2_nextpage();
} // static void draw_dashboard(uint8_t selection)



/*============================================================================*/
/*
 * Dashboard screen - top-level menu for the universal remote
 */
/*============================================================================*/
static void dashboard_screen(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t selection = 0;

	draw_dashboard(selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					break; /* Exit to caller */
				}
				else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection > 0)
						selection--;
					else
						selection = DASHBOARD_ITEM_COUNT - 1;
					draw_dashboard(selection);
				}
				else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < DASHBOARD_ITEM_COUNT - 1)
						selection++;
					else
						selection = 0;
					draw_dashboard(selection);
				}
				else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					switch (selection)
					{
						case 0: /* Browse IRDB */
							strncpy(s_current_path, IR_UNIVERSAL_IRDB_ROOT, IR_UNIVERSAL_PATH_MAX_LEN - 1);
							s_current_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
							browse_directory(s_current_path);
							break;
						case 1: /* Search - placeholder, browse root for now */
							strncpy(s_current_path, IR_UNIVERSAL_IRDB_ROOT, IR_UNIVERSAL_PATH_MAX_LEN - 1);
							s_current_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
							browse_directory(s_current_path);
							break;
						case 2: /* Favorites */
							show_favorites_screen();
							break;
						case 3: /* Recent */
							show_recent_screen();
							break;
						default:
							break;
					}
					/* Redraw dashboard after returning from sub-screen */
					draw_dashboard(selection);
				}
			} /* if (q_item.q_evt_type == Q_EVENT_KEYPAD) */
		} /* if (ret == pdTRUE) */
	} /* while (1) */
} // static void dashboard_screen(void)



/*============================================================================*/
/*
 * Draw a generic list screen with title, items, and selection highlight
 */
/*============================================================================*/
static void draw_list_screen(const char *title, uint16_t count, uint16_t selection)
{
	uint16_t i;
	uint16_t start_idx;
	uint16_t visible;
	uint8_t y;

	/* Calculate which items are visible in the scroll window */
	if (selection < LIST_VISIBLE_ITEMS)
		start_idx = 0;
	else
		start_idx = selection - LIST_VISIBLE_ITEMS + 1;

	visible = count - start_idx;
	if (visible > LIST_VISIBLE_ITEMS)
		visible = LIST_VISIBLE_ITEMS;

	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Title bar */
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, title);
	u8g2_DrawHLine(&m1_u8g2, 0, LIST_HEADER_HEIGHT, 128);

	/* List items */
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < visible; i++)
	{
		uint16_t idx = start_idx + i;
		y = LIST_START_Y + (i * LIST_ITEM_HEIGHT);

		if (idx == selection)
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, LIST_ITEM_HEIGHT);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 9, s_browse_names[idx]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 9, s_browse_names[idx]);
		}
	}

	/* Scroll indicator */
	if (count > LIST_VISIBLE_ITEMS)
	{
		uint8_t bar_height = (LIST_VISIBLE_ITEMS * LIST_ITEM_HEIGHT * LIST_VISIBLE_ITEMS) / count;
		uint8_t bar_y;

		if (bar_height < 4)
			bar_height = 4;
		bar_y = LIST_START_Y + (start_idx * (LIST_VISIBLE_ITEMS * LIST_ITEM_HEIGHT - bar_height)) / (count - LIST_VISIBLE_ITEMS);
		u8g2_DrawBox(&m1_u8g2, 126, bar_y, 2, bar_height);
	}

	/* Page info / bottom bar */
	{
		char page_info[24];
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		snprintf(page_info, sizeof(page_info), "%u/%u", (unsigned)(selection + 1), (unsigned)count);
		u8g2_DrawStr(&m1_u8g2, 2, 63, page_info);

		if (s_browse_page > 0)
			u8g2_DrawStr(&m1_u8g2, 50, 63, "<");
		u8g2_DrawStr(&m1_u8g2, 60, 63, "More>");
	}

	m1_u8g2_nextpage();
} // static void draw_list_screen(...)



/*============================================================================*/
/*
 * Scan a directory page and populate s_browse_names[]
 * Returns the number of entries found on this page
 */
/*============================================================================*/
static uint16_t scan_directory_page(const char *path, uint16_t page, uint16_t page_size)
{
	DIR dir;
	FILINFO fno;
	FRESULT res;
	uint16_t skip_count;
	uint16_t found = 0;

	res = f_opendir(&dir, path);
	if (res != FR_OK)
		return 0;

	/* Skip entries from previous pages */
	skip_count = page * page_size;
	while (skip_count > 0)
	{
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == '\0')
		{
			f_closedir(&dir);
			return 0;
		}
		/* Skip hidden files and . entries */
		if (fno.fname[0] == '.')
			continue;
		skip_count--;
	}

	/* Read entries for this page */
	while (found < page_size && found < BROWSE_NAMES_MAX)
	{
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == '\0')
			break;

		/* Skip hidden files */
		if (fno.fname[0] == '.')
			continue;

		strncpy(s_browse_names[found], fno.fname, BROWSE_NAME_MAX_LEN - 1);
		s_browse_names[found][BROWSE_NAME_MAX_LEN - 1] = '\0';
		found++;
	}

	f_closedir(&dir);
	return found;
} // static uint16_t scan_directory_page(...)



/*============================================================================*/
/*
 * Check if a filename has the .ir extension
 */
/*============================================================================*/
static bool is_ir_file(const char *fname)
{
	size_t len;

	if (fname == NULL)
		return false;

	len = strlen(fname);
	if (len < 4)
		return false;

	return (strcmp(&fname[len - 3], IR_FILE_EXTENSION) == 0);
} // static bool is_ir_file(...)



/*============================================================================*/
/*
 * Append a path component to a base path
 */
/*============================================================================*/
static void path_append(char *base, const char *item)
{
	size_t len = strlen(base);

	if (len > 0 && base[len - 1] != '/')
	{
		if (len + 1 < IR_UNIVERSAL_PATH_MAX_LEN)
		{
			base[len] = '/';
			base[len + 1] = '\0';
			len++;
		}
	}

	strncat(base, item, IR_UNIVERSAL_PATH_MAX_LEN - len - 1);
	base[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
} // static void path_append(...)



/*============================================================================*/
/*
 * Navigate one level up in the path
 */
/*============================================================================*/
static void path_go_up(char *path)
{
	size_t len = strlen(path);
	int i;

	if (len == 0)
		return;

	/* Remove trailing slash */
	if (path[len - 1] == '/')
	{
		path[len - 1] = '\0';
		len--;
	}

	/* Find the last slash and truncate */
	for (i = (int)len - 1; i >= 0; i--)
	{
		if (path[i] == '/')
		{
			path[i] = '\0';
			return;
		}
	}

	/* No slash found - clear the path */
	path[0] = '\0';
} // static void path_go_up(...)



/*============================================================================*/
/*
 * Browse a directory in the IRDB hierarchy.
 * Handles pagination with LEFT/RIGHT, selection with UP/DOWN/OK,
 * and BACK to navigate up.
 */
/*============================================================================*/
static void browse_directory(const char *path)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	char saved_path[IR_UNIVERSAL_PATH_MAX_LEN];
	char child_path[IR_UNIVERSAL_PATH_MAX_LEN];
	FILINFO fno;
	uint8_t is_directory;

	strncpy(s_current_path, path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
	s_current_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';

	s_browse_page = 0;
	s_browse_selection = 0;
	s_browse_count = scan_directory_page(s_current_path, s_browse_page, BROWSE_NAMES_MAX);

	if (s_browse_count == 0)
	{
		/* Empty directory - show message */
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 32, "No files found");
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 50, "Back to return");
		m1_u8g2_nextpage();

		/* Wait for BACK press */
		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
			}
		}
	}

	/* Extract a short title from the path */
	{
		const char *title = s_current_path;
		const char *p = s_current_path;
		while (*p)
		{
			if (*p == '/')
				title = p + 1;
			p++;
		}
		if (*title == '\0')
			title = "IRDB";
		draw_list_screen(title, s_browse_count, s_browse_selection);
	}

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return; /* Exit to caller */
				}
				else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (s_browse_selection > 0)
						s_browse_selection--;
					else
						s_browse_selection = s_browse_count - 1;
				}
				else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (s_browse_selection < s_browse_count - 1)
						s_browse_selection++;
					else
						s_browse_selection = 0;
				}
				else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
				{
					/* Next page */
					uint16_t next_count;
					next_count = scan_directory_page(s_current_path, s_browse_page + 1, BROWSE_NAMES_MAX);
					if (next_count > 0)
					{
						s_browse_page++;
						s_browse_count = next_count;
						s_browse_selection = 0;
					}
				}
				else if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
				{
					/* Previous page */
					if (s_browse_page > 0)
					{
						s_browse_page--;
						s_browse_count = scan_directory_page(s_current_path, s_browse_page, BROWSE_NAMES_MAX);
						s_browse_selection = 0;
					}
				}
				else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (s_browse_count > 0 && s_browse_selection < s_browse_count)
					{
						/* Build child path */
						strncpy(child_path, s_current_path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
						child_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
						path_append(child_path, s_browse_names[s_browse_selection]);

						/* Check if it's a directory or file */
						if (f_stat(child_path, &fno) == FR_OK)
						{
							is_directory = (fno.fattrib & AM_DIR) ? 1 : 0;
						}
						else
						{
							is_directory = 0;
						}

						if (is_directory)
						{
							/* Save current state and recurse into subdirectory */
							strncpy(saved_path, s_current_path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
							saved_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';

							browse_directory(child_path);

							/* Restore state after returning */
							strncpy(s_current_path, saved_path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
							s_current_path[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
							s_browse_count = scan_directory_page(s_current_path, s_browse_page, BROWSE_NAMES_MAX);
							if (s_browse_selection >= s_browse_count && s_browse_count > 0)
								s_browse_selection = s_browse_count - 1;
						}
						else if (is_ir_file(s_browse_names[s_browse_selection]))
						{
							/* Open .ir file and show commands */
							add_to_recent(child_path);
							show_commands(child_path);

							/* Restore browse state after returning */
							s_browse_count = scan_directory_page(s_current_path, s_browse_page, BROWSE_NAMES_MAX);
							if (s_browse_selection >= s_browse_count && s_browse_count > 0)
								s_browse_selection = s_browse_count - 1;
						}
					}
				}

				/* Redraw list */
				{
					const char *title = s_current_path;
					const char *p = s_current_path;
					while (*p)
					{
						if (*p == '/')
							title = p + 1;
						p++;
					}
					if (*title == '\0')
						title = "IRDB";
					draw_list_screen(title, s_browse_count, s_browse_selection);
				}
			} /* if (q_item.q_evt_type == Q_EVENT_KEYPAD) */
		} /* if (ret == pdTRUE) */
	} /* while (1) */
} // static void browse_directory(...)



/*============================================================================*/
/*
 * Map a Flipper-format protocol name to an IRMP protocol ID.
 * Returns 0 if not found.
 */
/*============================================================================*/
static uint8_t map_flipper_protocol(const char *name)
{
	/* Common Flipper protocol names mapped to IRMP IDs */
	if (strcmp(name, "NEC") == 0 || strcmp(name, "NECext") == 0)
		return IRMP_NEC_PROTOCOL;
	if (strcmp(name, "Samsung32") == 0 || strcmp(name, "Samsung") == 0)
		return IRMP_SAMSUNG32_PROTOCOL;
	if (strcmp(name, "RC5") == 0 || strcmp(name, "RC5X") == 0)
		return IRMP_RC5_PROTOCOL;
	if (strcmp(name, "RC6") == 0)
		return IRMP_RC6_PROTOCOL;
	if (strcmp(name, "Sony12") == 0 || strcmp(name, "Sony15") == 0 || strcmp(name, "Sony20") == 0 ||
	    strcmp(name, "SIRC") == 0 || strcmp(name, "SIRC15") == 0 || strcmp(name, "SIRC20") == 0)
		return IRMP_SIRCS_PROTOCOL;
	if (strcmp(name, "Kaseikyo") == 0 || strcmp(name, "Panasonic") == 0)
		return IRMP_KASEIKYO_PROTOCOL;
	if (strcmp(name, "NEC42") == 0 || strcmp(name, "NEC42ext") == 0)
		return IRMP_NEC42_PROTOCOL;
	if (strcmp(name, "RCMM") == 0)
		return IRMP_RCMM32_PROTOCOL;
#if IRMP_SUPPORT_DENON_PROTOCOL == 1
	if (strcmp(name, "Denon") == 0)
		return IRMP_DENON_PROTOCOL;
#endif
#if IRMP_SUPPORT_JVC_PROTOCOL == 1
	if (strcmp(name, "JVC") == 0)
		return IRMP_JVC_PROTOCOL;
#endif
#if IRMP_SUPPORT_LG_PROTOCOL == 1
	if (strcmp(name, "LG") == 0)
		return IRMP_LG_PROTOCOL;
#endif
#if IRMP_SUPPORT_SHARP_PROTOCOL == 1
	if (strcmp(name, "Sharp") == 0)
		return IRMP_SHARP_PROTOCOL;
#endif

	return 0; /* Unknown protocol */
} // static uint8_t map_flipper_protocol(...)



/*============================================================================*/
/*
 * Parse a single IR signal block from a Flipper .ir file.
 * The flipper_file cursor should be positioned at the start of a signal block.
 * Returns true if a valid signal was parsed.
 */
/*============================================================================*/
static bool parse_ir_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd)
{
	bool got_name = false;
	bool got_type = false;
	bool is_parsed = false;
	bool is_raw_type = false;

	memset(cmd, 0, sizeof(ir_universal_cmd_t));

	/* Read key-value pairs until we hit a separator or EOF */
	while (ff_read_line(ff))
	{
		if (ff_is_separator(ff))
			break; /* End of this signal block */

		if (!ff_parse_kv(ff))
			continue;

		if (strcmp(ff_get_key(ff), "name") == 0)
		{
			strncpy(cmd->name, ff_get_value(ff), IR_UNIVERSAL_NAME_MAX_LEN - 1);
			cmd->name[IR_UNIVERSAL_NAME_MAX_LEN - 1] = '\0';
			got_name = true;
		}
		else if (strcmp(ff_get_key(ff), "type") == 0)
		{
			got_type = true;
			if (strcmp(ff_get_value(ff), "parsed") == 0)
			{
				is_parsed = true;
				is_raw_type = false;
			}
			else if (strcmp(ff_get_value(ff), "raw") == 0)
			{
				is_parsed = false;
				is_raw_type = true;
			}
		}
		else if (strcmp(ff_get_key(ff), "protocol") == 0 && is_parsed)
		{
			cmd->protocol = map_flipper_protocol(ff_get_value(ff));
		}
		else if (strcmp(ff_get_key(ff), "address") == 0 && is_parsed)
		{
			cmd->address = (uint16_t)strtoul(ff_get_value(ff), NULL, 16);
		}
		else if (strcmp(ff_get_key(ff), "command") == 0 && is_parsed)
		{
			cmd->command = (uint16_t)strtoul(ff_get_value(ff), NULL, 16);
		}
		else if (strcmp(ff_get_key(ff), "frequency") == 0 && is_raw_type)
		{
			cmd->raw_freq = (uint32_t)strtoul(ff_get_value(ff), NULL, 10);
		}
		else if (strcmp(ff_get_key(ff), "data") == 0 && is_raw_type)
		{
			/* Count the raw timing values (space-separated integers) */
			const char *p = ff_get_value(ff);
			uint16_t count = 0;
			while (*p)
			{
				/* Skip whitespace */
				while (*p == ' ')
					p++;
				if (*p == '\0')
					break;
				count++;
				/* Skip non-whitespace */
				while (*p && *p != ' ')
					p++;
			}
			cmd->raw_count = count;
		}
	}

	if (got_name && got_type)
	{
		if (is_parsed && cmd->protocol != 0)
		{
			cmd->is_raw = false;
			cmd->flags = 1; /* No repeat */
			cmd->valid = true;
			return true;
		}
		else if (is_raw_type && cmd->raw_freq > 0)
		{
			cmd->is_raw = true;
			cmd->valid = true;
			return true;
		}
	}

	return false;
} // static bool parse_ir_signal_block(...)



/*============================================================================*/
/*
 * Parse a Flipper .ir file and load signals into s_commands[].
 * Returns the number of commands loaded.
 */
/*============================================================================*/
static uint16_t parse_ir_file(const char *filepath)
{
	flipper_file_t ff;
	uint16_t count = 0;

	if (!ff_open(&ff, filepath))
		return 0;

	/* Validate header: "Filetype: IR signals file" with version >= 1 */
	if (!ff_validate_header(&ff, "IR signals file", 1))
	{
		ff_close(&ff);
		return 0;
	}

	/* Parse signal blocks */
	while (count < IR_UNIVERSAL_MAX_CMDS)
	{
		if (!parse_ir_signal_block(&ff, &s_commands[count]))
		{
			/* Check if we've reached EOF */
			if (ff.eof)
				break;
			continue; /* Skip invalid blocks */
		}
		count++;
	}

	ff_close(&ff);
	return count;
} // static uint16_t parse_ir_file(...)



/*============================================================================*/
/*
 * Display the command list from a .ir file and allow the user
 * to select and transmit commands.
 */
/*============================================================================*/
static void show_commands(const char *ir_file_path)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint16_t selection = 0;
	uint16_t i;
	const char *short_name;
	const char *p;
	char title[BROWSE_NAME_MAX_LEN];

	/* Parse the .ir file */
	s_cmd_count = parse_ir_file(ir_file_path);

	if (s_cmd_count == 0)
	{
		/* No commands found */
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 32, "No IR signals");
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 50, "Back to return");
		m1_u8g2_nextpage();

		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
			}
		}
	}

	/* Copy command names into s_browse_names for the list renderer */
	for (i = 0; i < s_cmd_count && i < BROWSE_NAMES_MAX; i++)
	{
		strncpy(s_browse_names[i], s_commands[i].name, BROWSE_NAME_MAX_LEN - 1);
		s_browse_names[i][BROWSE_NAME_MAX_LEN - 1] = '\0';
	}
	s_browse_count = (s_cmd_count < BROWSE_NAMES_MAX) ? s_cmd_count : BROWSE_NAMES_MAX;

	/* Extract filename for title */
	short_name = ir_file_path;
	p = ir_file_path;
	while (*p)
	{
		if (*p == '/')
			short_name = p + 1;
		p++;
	}
	strncpy(title, short_name, BROWSE_NAME_MAX_LEN - 1);
	title[BROWSE_NAME_MAX_LEN - 1] = '\0';

	draw_list_screen(title, s_browse_count, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return; /* Exit to caller */
				}
				else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection > 0)
						selection--;
					else
						selection = s_browse_count - 1;
				}
				else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_browse_count - 1)
						selection++;
					else
						selection = 0;
				}
				else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_cmd_count && s_commands[selection].valid)
					{
						transmit_command(&s_commands[selection]);
					}
				}

				draw_list_screen(title, s_browse_count, selection);
			} /* if (q_item.q_evt_type == Q_EVENT_KEYPAD) */
			else if (q_item.q_evt_type == Q_EVENT_IRRED_TX)
			{
				/* IR TX completed notification */
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				draw_list_screen(title, s_browse_count, selection);
			}
		} /* if (ret == pdTRUE) */
	} /* while (1) */
} // static void show_commands(...)



/*============================================================================*/
/*
 * Transmit a parsed IR command using the existing IRSND infrastructure.
 * For parsed signals: sets up IRMP_DATA and uses irsnd_generate_tx_data.
 * Raw signals are not yet supported (placeholder for future implementation).
 */
/*============================================================================*/
static void transmit_command(const ir_universal_cmd_t *cmd)
{
	char tx_info[32];

	if (cmd == NULL || !cmd->valid)
		return;

	if (cmd->is_raw)
	{
		/* Raw signal transmission - show unsupported message for now */
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 25, "Raw TX");
		u8g2_DrawStr(&m1_u8g2, 10, 38, "Not supported yet");
		snprintf(tx_info, sizeof(tx_info), "Freq: %luHz", (unsigned long)cmd->raw_freq);
		u8g2_DrawStr(&m1_u8g2, 10, 51, tx_info);
		m1_u8g2_nextpage();
		vTaskDelay(pdMS_TO_TICKS(1500));
		return;
	}

	/* Parsed signal: use IRSND to transmit */
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

	/* Show transmitting screen */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 10, 15, "Transmitting...");
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 10, 30, cmd->name);
	snprintf(tx_info, sizeof(tx_info), "Addr:0x%04X Cmd:0x%04X", cmd->address, cmd->command);
	u8g2_DrawStr(&m1_u8g2, 4, 45, tx_info);
	m1_u8g2_nextpage();

	/* Set up IRMP_DATA for transmission */
	s_tx_irmp_data.protocol = cmd->protocol;
	s_tx_irmp_data.address = cmd->address;
	s_tx_irmp_data.command = cmd->command;
	s_tx_irmp_data.flags = cmd->flags;

	/* Initialize the IR encoder system */
	infrared_encode_sys_init();

	/* Generate OTA data from the IRMP_DATA structure */
	irsnd_generate_tx_data(s_tx_irmp_data);

	/* Start the transmit process */
	infrared_transmit(1); /* 1 = initialize */

	m1_buzzer_notification();

	/* The transmit is asynchronous. The caller's event loop will receive
	 * Q_EVENT_IRRED_TX when transmission completes, and should then call
	 * infrared_encode_sys_deinit() or let the next transmit re-init. */
} // static void transmit_command(...)



/*============================================================================*/
/*
 * Load favorite paths from SD card file
 */
/*============================================================================*/
static void load_favorites(void)
{
	FIL file;
	FRESULT res;
	UINT br;
	char line[IR_UNIVERSAL_PATH_MAX_LEN];
	uint8_t count = 0;

	s_favorite_count = 0;

	res = f_open(&file, FAV_FILE_PATH, FA_READ);
	if (res != FR_OK)
		return;

	while (count < IR_UNIVERSAL_MAX_FAVORITES)
	{
		/* Read a line manually */
		uint16_t i = 0;
		char ch;

		while (i < IR_UNIVERSAL_PATH_MAX_LEN - 1)
		{
			res = f_read(&file, &ch, 1, &br);
			if (res != FR_OK || br == 0)
			{
				if (i > 0) /* Partial line without newline at EOF */
				{
					line[i] = '\0';
					strncpy(s_favorites[count], line, IR_UNIVERSAL_PATH_MAX_LEN - 1);
					s_favorites[count][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
					count++;
				}
				goto done_fav;
			}

			if (ch == '\n' || ch == '\r')
			{
				if (i > 0)
				{
					line[i] = '\0';
					strncpy(s_favorites[count], line, IR_UNIVERSAL_PATH_MAX_LEN - 1);
					s_favorites[count][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
					count++;
				}
				break;
			}
			line[i++] = ch;
		}
	}

done_fav:
	s_favorite_count = count;
	f_close(&file);
} // static void load_favorites(void)



/*============================================================================*/
/*
 * Save favorites to SD card file
 */
/*============================================================================*/
static void save_favorites(void)
{
	FIL file;
	FRESULT res;
	UINT bw;
	uint8_t i;

	/* Ensure the System directory exists */
	f_mkdir("0:/System");

	res = f_open(&file, FAV_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return;

	for (i = 0; i < s_favorite_count; i++)
	{
		f_write(&file, s_favorites[i], strlen(s_favorites[i]), &bw);
		f_write(&file, "\n", 1, &bw);
	}

	f_close(&file);
} // static void save_favorites(void)



/*============================================================================*/
/*
 * Add a path to the recent list (most recent first, de-duplicated)
 */
/*============================================================================*/
static void add_to_recent(const char *path)
{
	uint8_t i;
	int existing = -1;

	/* Check if it already exists */
	for (i = 0; i < s_recent_count; i++)
	{
		if (strcmp(s_recent[i], path) == 0)
		{
			existing = i;
			break;
		}
	}

	if (existing >= 0)
	{
		/* Move it to the front */
		char tmp[IR_UNIVERSAL_PATH_MAX_LEN];
		strncpy(tmp, s_recent[existing], IR_UNIVERSAL_PATH_MAX_LEN - 1);
		tmp[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
		for (i = existing; i > 0; i--)
		{
			strncpy(s_recent[i], s_recent[i - 1], IR_UNIVERSAL_PATH_MAX_LEN - 1);
			s_recent[i][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
		}
		strncpy(s_recent[0], tmp, IR_UNIVERSAL_PATH_MAX_LEN - 1);
		s_recent[0][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
	}
	else
	{
		/* Shift everything down and insert at front */
		if (s_recent_count < IR_UNIVERSAL_MAX_RECENT)
			s_recent_count++;
		for (i = s_recent_count - 1; i > 0; i--)
		{
			strncpy(s_recent[i], s_recent[i - 1], IR_UNIVERSAL_PATH_MAX_LEN - 1);
			s_recent[i][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
		}
		strncpy(s_recent[0], path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
		s_recent[0][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
	}
} // static void add_to_recent(...)



/*============================================================================*/
/*
 * Load recent paths from SD card file
 */
/*============================================================================*/
static void load_recent(void)
{
	FIL file;
	FRESULT res;
	UINT br;
	char line[IR_UNIVERSAL_PATH_MAX_LEN];
	uint8_t count = 0;

	s_recent_count = 0;

	res = f_open(&file, RECENT_FILE_PATH, FA_READ);
	if (res != FR_OK)
		return;

	while (count < IR_UNIVERSAL_MAX_RECENT)
	{
		uint16_t i = 0;
		char ch;

		while (i < IR_UNIVERSAL_PATH_MAX_LEN - 1)
		{
			res = f_read(&file, &ch, 1, &br);
			if (res != FR_OK || br == 0)
			{
				if (i > 0)
				{
					line[i] = '\0';
					strncpy(s_recent[count], line, IR_UNIVERSAL_PATH_MAX_LEN - 1);
					s_recent[count][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
					count++;
				}
				goto done_recent;
			}

			if (ch == '\n' || ch == '\r')
			{
				if (i > 0)
				{
					line[i] = '\0';
					strncpy(s_recent[count], line, IR_UNIVERSAL_PATH_MAX_LEN - 1);
					s_recent[count][IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';
					count++;
				}
				break;
			}
			line[i++] = ch;
		}
	}

done_recent:
	s_recent_count = count;
	f_close(&file);
} // static void load_recent(void)



/*============================================================================*/
/*
 * Save recent list to SD card file
 */
/*============================================================================*/
static void save_recent(void)
{
	FIL file;
	FRESULT res;
	UINT bw;
	uint8_t i;

	/* Ensure the System directory exists */
	f_mkdir("0:/System");

	res = f_open(&file, RECENT_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return;

	for (i = 0; i < s_recent_count; i++)
	{
		f_write(&file, s_recent[i], strlen(s_recent[i]), &bw);
		f_write(&file, "\n", 1, &bw);
	}

	f_close(&file);
} // static void save_recent(void)



/*============================================================================*/
/*
 * Show the favorites screen
 */
/*============================================================================*/
static void show_favorites_screen(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint16_t selection = 0;
	uint8_t i;

	if (s_favorite_count == 0)
	{
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 32, "No favorites");
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 50, "Back to return");
		m1_u8g2_nextpage();

		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
			}
		}
	}

	/* Copy favorites to browse names, extracting just the filename */
	for (i = 0; i < s_favorite_count && i < BROWSE_NAMES_MAX; i++)
	{
		const char *fname = s_favorites[i];
		const char *p = s_favorites[i];
		while (*p)
		{
			if (*p == '/')
				fname = p + 1;
			p++;
		}
		strncpy(s_browse_names[i], fname, BROWSE_NAME_MAX_LEN - 1);
		s_browse_names[i][BROWSE_NAME_MAX_LEN - 1] = '\0';
	}
	s_browse_count = (s_favorite_count < BROWSE_NAMES_MAX) ? s_favorite_count : BROWSE_NAMES_MAX;

	draw_list_screen("Favorites", s_browse_count, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
				else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection > 0)
						selection--;
					else
						selection = s_browse_count - 1;
				}
				else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_browse_count - 1)
						selection++;
					else
						selection = 0;
				}
				else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_favorite_count)
					{
						show_commands(s_favorites[selection]);
						/* Refresh display after returning */
					}
				}

				draw_list_screen("Favorites", s_browse_count, selection);
			} /* if (q_item.q_evt_type == Q_EVENT_KEYPAD) */
		} /* if (ret == pdTRUE) */
	} /* while (1) */
} // static void show_favorites_screen(void)



/*============================================================================*/
/*
 * Show the recent files screen
 */
/*============================================================================*/
static void show_recent_screen(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint16_t selection = 0;
	uint8_t i;

	if (s_recent_count == 0)
	{
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 32, "No recent files");
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 50, "Back to return");
		m1_u8g2_nextpage();

		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
			}
		}
	}

	/* Copy recent paths to browse names, extracting just the filename */
	for (i = 0; i < s_recent_count && i < BROWSE_NAMES_MAX; i++)
	{
		const char *fname = s_recent[i];
		const char *p = s_recent[i];
		while (*p)
		{
			if (*p == '/')
				fname = p + 1;
			p++;
		}
		strncpy(s_browse_names[i], fname, BROWSE_NAME_MAX_LEN - 1);
		s_browse_names[i][BROWSE_NAME_MAX_LEN - 1] = '\0';
	}
	s_browse_count = (s_recent_count < BROWSE_NAMES_MAX) ? s_recent_count : BROWSE_NAMES_MAX;

	draw_list_screen("Recent", s_browse_count, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE)
		{
			if (q_item.q_evt_type == Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

				if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					xQueueReset(main_q_hdl);
					return;
				}
				else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection > 0)
						selection--;
					else
						selection = s_browse_count - 1;
				}
				else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_browse_count - 1)
						selection++;
					else
						selection = 0;
				}
				else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					if (selection < s_recent_count)
					{
						show_commands(s_recent[selection]);
					}
				}

				draw_list_screen("Recent", s_browse_count, selection);
			} /* if (q_item.q_evt_type == Q_EVENT_KEYPAD) */
		} /* if (ret == pdTRUE) */
	} /* while (1) */
} // static void show_recent_screen(void)
