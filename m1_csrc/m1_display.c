/* See COPYING.txt for license details. */

/*
*
* m1_display.c
*
* Functions to display objects on the M1 LCD
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
//#include "stm32h5xx_hal.h"
//#include "main.h"
#include "m1_lp5814.h"
#include "m1_io_defs.h"
#include "m1_compile_cfg.h"
#include "m1_display.h"
#include "m1_read_icon.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Display"

#define MENU_M1_SCR_ANI_TIMEOUT		1000 // animation timeout in millisecond

#define MENU_SCROLLBAR_POS_X				124
#define MENU_SCROLLBAR_POS_Y				0
#define MENU_SCROLLBAR_WIDTH				4

#define MAIN_MENU_TEXT_ITEMS				4
#define MAIN_MENU_TEXT_FRAME_TOP_POS_Y		0
#define MAIN_MENU_TXT_LEFT_POS_X			68
#define MAIN_MENU_TXT_TOP_POS_Y				12
#define MAIN_MENU_TXT_SPACING_VERT			16
#define MAIN_MENU_TEXT_FRAME_H				16
#define MAIN_MENU_TEXT_FRAME_W				75
#define MAIN_MENU_TEXT_FRAME_LEFT_POS_X		48

#define MAIN_MENU_ICON_LEFT_POS_X			50
#define MAIN_MENU_ICON_WIDTH				14
#define MAIN_MENU_ICON_HEIGHT				14

#define MAIN_MENU_LOGO_TOP_POS_Y			24
#define MAIN_MENU_LOGO_LEFT_POS_X			0
#define MAIN_MENU_LOGO_WIDTH				26
#define MAIN_MENU_LOGO_HEIGHT				14

#define MAIN_MENU_LOGO_FONT					M1_MAIN_LOGO_FONT_1B

/* Approved full-width main-menu presentation (main-menu drawing path only;
 * submenu rendering and the shared scrollbar constants are left unchanged). */
#define MAIN_MENU2_ROW_H					16
#define MAIN_MENU2_ICON_LEFT_POS_X			4
#define MAIN_MENU2_TXT_LEFT_POS_X			22
#define MAIN_MENU2_TXT_BASELINE_OFFSET		12
#define MAIN_MENU2_SEL_BAND_LEFT_POS_X		0
#define MAIN_MENU2_SEL_BAND_W				125
#define MAIN_MENU2_POSIND_POS_X				126
#define MAIN_MENU2_POSIND_W					2

#define SUB_MENU_TEXT_ITEMS					4 // rows
#define SUB_MENU_TEXT_FRAME_TOP_POS_Y		0
#define SUB_MENU_TXT_LEFT_POS_X				4
#define SUB_MENU_TEXT_TOP_POS_Y				12
#define SUB_MENU_TEXT_SPACING_VERT			16
#define SUB_MENU_TEXT_FRAME_H				16
#define SUB_MENU_TEXT_FRAME_W				(M1_LCD_DISPLAY_WIDTH - MENU_SCROLLBAR_WIDTH - 1)
#define SUB_MENU_TEXT_FRAME_LEFT_POS_X		0


//************************** C O N S T A N T **********************************/

const S_M1_menu_icon_data menu_fb_icon_prev = {fb_m1_icon_folder, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_dir = {fb_m1_icon_folder, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_text = {fb_m1_icon_file, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_data = {fb_m1_icon_file, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_other = {fb_m1_icon_file, 10, 8};


static const uint8_t menu_window_sizes[] = {MAIN_MENU_TEXT_ITEMS, SUB_MENU_TEXT_ITEMS};
static const uint8_t menu_text_frame_top_pos_y[] = {MAIN_MENU_TEXT_FRAME_TOP_POS_Y, SUB_MENU_TEXT_FRAME_TOP_POS_Y};
static const uint8_t menu_text_left_pos_x[] = {MAIN_MENU_TXT_LEFT_POS_X, SUB_MENU_TXT_LEFT_POS_X};
static const uint8_t menu_text_top_pos_y[] = {MAIN_MENU_TXT_TOP_POS_Y, SUB_MENU_TEXT_TOP_POS_Y};
static const uint8_t menu_text_spacing_vert[] = {MAIN_MENU_TXT_SPACING_VERT, SUB_MENU_TEXT_SPACING_VERT};
static const uint8_t menu_text_frame_h[] = {MAIN_MENU_TEXT_FRAME_H, SUB_MENU_TEXT_FRAME_H};
static const uint8_t menu_text_frame_w[] = {MAIN_MENU_TEXT_FRAME_W, SUB_MENU_TEXT_FRAME_W};
static const uint8_t menu_text_frame_left_pos_x[] = {MAIN_MENU_TEXT_FRAME_LEFT_POS_X, SUB_MENU_TEXT_FRAME_LEFT_POS_X};
static const uint8_t *menu_text_font_n[] = {M1_DISP_MAIN_MENU_FONT_N, M1_DISP_SUB_MENU_FONT_N};
static const uint8_t *menu_text_font_b[] = {M1_DISP_MAIN_MENU_FONT_B, M1_DISP_SUB_MENU_FONT_B};

const char sdcard_access_error_message[] = "Access error";
const char sdcard_db_error_message[] = "Database error";

//************************** S T R U C T U R E S *******************************

typedef struct
{
 	uint8_t active_disp_row; // selected row of the display window
	uint8_t disp_top_row; // first row of the moving display window
	uint8_t sel_item; // selected item of the menu list
} S_M1_Disp_Window_t;

/***************************** V A R I A B L E S ******************************/

static uint8_t disp_window_active_row;
static uint8_t disp_window_top_row; // moving display window
static uint8_t menu_level_id; // main menu (0) or sub menu
static bool info_box_high_box;
static uint8_t info_box_first_row = 0;
static const S_M1_Menu_t *this_gui_menu;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_gui_init(void);
void m1_gui_welcome_scr(void);
void m1_gui_scr_animation(void);
void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction);
uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction);

void m1_info_box_display_init(bool high_box);
void m1_info_box_display_clear(void);
void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext);
uint8_t m1_message_box(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons);
void m1_draw_bottom_bar(u8g2_t *u8g2, const uint8_t *lbitmap, const char *ltext, const char *rtext, const uint8_t *rbitmap);
void m1_draw_text(u8g2_t *u8g2,
                 int x, int y,
                 int max_width,
                 const char *text,
                 S_M1_text_align_t align);
void m1_draw_text_box(u8g2_t *u8g2,
                    int x, int y,
                    int max_width,          
                    int line_height,       
                    const char *text,
                    S_M1_text_align_t align);
void m1_image_message(const uint8_t *pimage, uint8_t image_w, uint8_t image_h, const char *message);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief This function sets up some settings for the menu display
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_gui_init(void)
{
	m1_lcd_cleardisplay();
	u8g2_SetBitmapMode(&m1_u8g2, 1);

	// used for scrolling text
	/*
	u8g2_SetFont(&m1_u8g2, u8g2_font_10x20_mr);
	width = u8g2_GetUTF8Width(&m1_u8g2, "text");
	u8g2_SetFontMode(&m1_u8g2, 0);		// enable transparent mode, which is faster
	*/
} // void m1_gui_init(void)



/*============================================================================*/
/**
  * @brief This function displays the M1 welcome screen
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_gui_welcome_scr(void)
{
	startup_info_screen_display("");
} // void m1_gui_welcome_scr(void)



/*============================================================================*/
/**
  * @brief This function updates the menu content on the display
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction)
{
	static S_M1_Disp_Window_t menu_display[SUB_MENU_LEVEL_MAX];
	static uint8_t menu_level = 0;
	uint8_t n_items, run;
	const char *psubmenu[SUB_MENU_ITEMS_MAX];

	if ( direction==MENU_UPDATE_INIT )
		menu_level = 0;
	this_gui_menu = phmenu;
	n_items = phmenu->num_submenu_items;

	if ( phmenu->gui_menu_update ) // This menu item has its own gui update function?
	{
		phmenu->gui_menu_update(phmenu, sel_item);
		if ( direction==MENU_UPDATE_RESET ) // Sub-menu changes to deeper level?
		{
			menu_display[menu_level].disp_top_row = disp_window_top_row;
			menu_display[menu_level].active_disp_row = disp_window_active_row; // back up
			menu_level++;
		}
		return;
	} // if ( phmenu->gui_menu_update )

	if ( direction==MENU_UPDATE_RESET ) // Sub-menu is changed
	{
		menu_level++;
	} // if ( direction==MENU_UPDATE_RESET )
	else if ( direction==MENU_UPDATE_RESTORE )
	{
		menu_level--;
		// Restore previous display values
		disp_window_top_row = menu_display[menu_level].disp_top_row;
		disp_window_active_row = menu_display[menu_level].active_disp_row;
	} // else if ( direction==MENU_UPDATE_RESTORE )
	for (run=0; run<n_items; run++)
	{
		psubmenu[run] = phmenu->submenu[run]->title;
	}
	menu_level_id = (menu_level==0)?0:1;
	m1_gui_submenu_update(psubmenu, n_items, sel_item, direction);

	// Back up
	menu_display[menu_level].disp_top_row = disp_window_top_row;
	menu_display[menu_level].active_disp_row = disp_window_active_row;
} // void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction)

/* Force the shared submenu renderer (m1_gui_submenu_update) onto its SUB-menu
 * presentation. Native saved-file action menus (NFC read-more, RFID saved
 * submenu) render through that shared path but key off the global menu_level_id.
 * Reached from the SD-card browser (storage_explore / HOME-LEFT) OUTSIDE the menu
 * system, menu_level_id is never updated and can be stale at 0 -- wrongly picking
 * the main-menu look (left icons + large font). The browser dispatch calls this
 * first so those menus render IDENTICALLY to their NFC/RFID -> Saved entry. No-op
 * for menu-system entries (menu_level_id already sub-level). */
void m1_gui_force_sub_menu_level(void)
{
	menu_level_id = 1;
}



/*============================================================================*/
/**
  * @brief This function updates the sub-menu content on the display
  * @param
  * @retval None
  */
/*============================================================================*/
uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction)
{
	static S_M1_Disp_Window_t x_menu_display[SUB_MENU_LEVEL_MAX];
	static uint8_t x_menu_level = 0, x_menu_update_init = 0;
	uint8_t menu_text_y, menu_frame_y;
	uint8_t disp_window_bottom_row;
	uint8_t n_items, active_item, run;

	n_items = num_items;

	switch ( direction )
	{
		case X_MENU_UPDATE_MOVE_DOWN:
			sel_item = x_menu_display[x_menu_level].sel_item;
			sel_item++;
			if ( sel_item >= n_items )
				sel_item = 0;
			// No break here!

		case MENU_UPDATE_MOVE_DOWN: // moving down
			if ( n_items > menu_window_sizes[menu_level_id] )
			{
				if ( disp_window_active_row < (menu_window_sizes[menu_level_id] - 1) )
				{
					disp_window_active_row++;
				}
				else if ( disp_window_active_row==(menu_window_sizes[menu_level_id] - 1) )
				{
					if ( sel_item==(n_items-1) ) // reach end of the menu items list?
					{
						disp_window_active_row++;
					} // if ( sel_item==(n_items-1) )
					else
					{
						// move display window down
						disp_window_top_row++;
					}
				} // else if ( disp_window_active_row==(menu_window_sizes[menu_level_id] - 1) )
				else // disp_window_active_row==menu_window_sizes[menu_level_id]
				{
					disp_window_active_row = 1; // reset to the first row
					disp_window_top_row = 1;
				} // else
			} // if ( n_items > menu_window_sizes[menu_level_id] )
			else // n_items <= menu_window_sizes[menu_level_id]
			{
				if ( disp_window_active_row < n_items )
				{
					disp_window_active_row++;
				} // if ( disp_window_active_row < n_items )
				else // disp_window_active_row==n_items
				{
					disp_window_active_row = 1; // reset to the first row
				} // else
			} // else
			break;

		case X_MENU_UPDATE_MOVE_UP:
			sel_item = x_menu_display[x_menu_level].sel_item;
			if ( sel_item )
				sel_item--;
			else
				sel_item = n_items - 1;
			// No break here!

		case MENU_UPDATE_MOVE_UP: // moving up
			if ( n_items >= menu_window_sizes[menu_level_id] )
			{
				if ( disp_window_active_row > 2 )
				{
					disp_window_active_row--;
				} // if ( disp_window_active_row > 2 )
				else if ( disp_window_active_row==2 )
				{
					if ( sel_item==0 ) // reach first item of the list?
					{
						disp_window_active_row = 1; // rest to the first row
					} // if ( sel_item==0 )
					else
					{
						// move display window up
						disp_window_top_row--;
					} // else
				} // else if ( disp_window_active_row==2 )
				else // being at the first row
				{
					disp_window_active_row = menu_window_sizes[menu_level_id]; // move to the last row
					disp_window_top_row = n_items - (menu_window_sizes[menu_level_id] - 1);
				} // else
			} // if ( n_items >= menu_window_sizes[menu_level_id] )
			else // n_items < menu_window_sizes[menu_level_id]
			{
				if ( disp_window_active_row > 1 )
				{
					disp_window_active_row--;
				} // if ( disp_window_active_row > 1 )
				else // being at the first row
				{
					disp_window_active_row = n_items; // move to the last item
				} // else
			} // else
			break;

		case MENU_UPDATE_INIT:
		case MENU_UPDATE_RESET:
			disp_window_active_row = 1; // reset to first row
    		disp_window_top_row = 1;
			break;

		case MENU_UPDATE_REFRESH:
			if ( x_menu_update_init )
			{
				x_menu_update_init = 0; // Reset
				x_menu_level = 0;
				disp_window_active_row = x_menu_display[0].active_disp_row; // Restore
				disp_window_top_row = x_menu_display[0].disp_top_row;
			} // if ( x_menu_update_init )
			break;

		case X_MENU_UPDATE_INIT:
			x_menu_update_init = 1; // Mark as initialized
    		x_menu_level = 0; // Reset
			x_menu_display[0].active_disp_row = disp_window_active_row; // back up
			x_menu_display[0].disp_top_row = disp_window_top_row;
			x_menu_display[0].sel_item = 0;
			return 0;

		case X_MENU_UPDATE_RESET:
			x_menu_level++;
			if ( x_menu_level >= SUB_MENU_LEVEL_MAX )
				x_menu_level = SUB_MENU_LEVEL_MAX - 1;
    		disp_window_top_row = 1;
			disp_window_active_row = 1; // reset to first row
    		sel_item = 0; // Overwrite input param
			break;

		case X_MENU_UPDATE_RESTORE:
			if ( x_menu_level )
				x_menu_level--;
			disp_window_active_row = x_menu_display[x_menu_level].active_disp_row; // previous values
			disp_window_top_row = x_menu_display[x_menu_level].disp_top_row;
			sel_item = x_menu_display[x_menu_level].sel_item;
			if ( sel_item >= n_items ) // Restore to an unexpected submenu?
			{
				sel_item = 0;
				disp_window_top_row = 1;
				disp_window_active_row = 1; // Reset to first row
			}
			if ( x_menu_level==0 )
				return 0; // Do nothing if the x_menu stays at its init level
			break;

		case X_MENU_UPDATE_REFRESH:
			sel_item = x_menu_display[x_menu_level].sel_item; // Restore from saved setting
			break;

		case MENU_UPDATE_NONE:
			if ( x_menu_update_init )
			{
				return x_menu_display[x_menu_level].sel_item;
			} // if ( x_menu_update_init )
			break;

		default:
			break;
	} // switch ( direction )

	/* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	if ( n_items >= menu_window_sizes[menu_level_id] )
		disp_window_bottom_row = disp_window_top_row + (menu_window_sizes[menu_level_id] - 1); // full window display
	else
		disp_window_bottom_row = disp_window_top_row + (n_items - 1); // partial window display
	active_item = disp_window_top_row;
	active_item += disp_window_active_row - 1;
	menu_frame_y = menu_text_frame_top_pos_y[menu_level_id];
	menu_text_y = menu_text_top_pos_y[menu_level_id];

	if ( menu_level_id == 0 )
	{
		/* ----- Approved full-width scrolling main-menu presentation -----
		 * 8-item scrollable list, 4 visible 16px rows; full-width selection band
		 * with white icon + title; a 2px selection-position indicator on the
		 * right. Consumes the existing scroll state (disp_window_top_row,
		 * disp_window_bottom_row, active_item, sel_item) WITHOUT modifying it.
		 * Submenu rendering (menu_level_id != 0) is left unchanged below. */
		uint8_t mm_row_y = 0;
		for ( run = disp_window_top_row; run <= disp_window_bottom_row; run++ )
		{
			if ( run == active_item )
			{
				/* full-width black selection band; icon + title drawn white */
				u8g2_DrawBox(&m1_u8g2, MAIN_MENU2_SEL_BAND_LEFT_POS_X, mm_row_y, MAIN_MENU2_SEL_BAND_W, MAIN_MENU2_ROW_H);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // white
				u8g2_DrawXBMP(&m1_u8g2, MAIN_MENU2_ICON_LEFT_POS_X, mm_row_y + 1, MAIN_MENU_ICON_WIDTH, MAIN_MENU_ICON_HEIGHT, this_gui_menu->submenu[run - 1]->icon_ptr);
				u8g2_SetFont(&m1_u8g2, menu_text_font_b[0]);  // selected row: helvB08
				u8g2_DrawStr(&m1_u8g2, MAIN_MENU2_TXT_LEFT_POS_X, mm_row_y + MAIN_MENU2_TXT_BASELINE_OFFSET, phmenu[run - 1]);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // restore black
			}
			else
			{
				/* black icon + title on white */
				u8g2_DrawXBMP(&m1_u8g2, MAIN_MENU2_ICON_LEFT_POS_X, mm_row_y + 1, MAIN_MENU_ICON_WIDTH, MAIN_MENU_ICON_HEIGHT, this_gui_menu->submenu[run - 1]->icon_ptr);
				u8g2_SetFont(&m1_u8g2, menu_text_font_n[0]);  // unselected: resoledmedium
				u8g2_DrawStr(&m1_u8g2, MAIN_MENU2_TXT_LEFT_POS_X, mm_row_y + MAIN_MENU2_TXT_BASELINE_OFFSET, phmenu[run - 1]);
			}
			mm_row_y += MAIN_MENU2_ROW_H;
		}

		/* Selection-position indicator: a ~ (64/num_items) px marker placed in one
		 * of num_items discrete positions (top = first item, bottom = last item).
		 * Endpoint-correct; drawn last so the selection band cannot cover it.
		 * Zero/one-item cases guarded before the division. */
		{
			uint8_t mm_mh, mm_my;
			if ( num_items <= 1 )
			{
				mm_mh = M1_LCD_DISPLAY_HEIGHT;
				mm_my = 0;
			}
			else
			{
				mm_mh = M1_LCD_DISPLAY_HEIGHT / num_items;
				if ( mm_mh == 0 )
					mm_mh = 1;
				mm_my = (uint8_t)( ((uint16_t)(M1_LCD_DISPLAY_HEIGHT - mm_mh) * sel_item) / (num_items - 1) );
			}
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, MAIN_MENU2_POSIND_POS_X, mm_my, MAIN_MENU2_POSIND_W, mm_mh);
		}

		u8g2_NextPage(&m1_u8g2); // Update display RAM

		// Back up (identical to the shared path below)
		if ( x_menu_update_init )
		{
			x_menu_display[x_menu_level].active_disp_row = disp_window_active_row;
			x_menu_display[x_menu_level].disp_top_row = disp_window_top_row;
			x_menu_display[x_menu_level].sel_item = sel_item;
		}
		return 0;
	} // if ( menu_level_id == 0 ) -- new full-width main-menu presentation

	u8g2_SetFont(&m1_u8g2, menu_text_font_n[menu_level_id]);
	for (run=disp_window_top_row; run<=disp_window_bottom_row; run++)
	{
		if ( run==active_item )
		{
			if ( menu_level_id==0 )
			{
		   		// Draw frame for selected menu item
				u8g2_DrawXBMP(&m1_u8g2, menu_text_frame_left_pos_x[menu_level_id], menu_frame_y, menu_text_frame_w[menu_level_id], menu_text_frame_h[menu_level_id], m1_frame_75x16);
			} // if ( menu_level_id==0 )
			else
			{
				u8g2_DrawBox(&m1_u8g2, menu_text_frame_left_pos_x[menu_level_id], menu_frame_y, menu_text_frame_w[menu_level_id], menu_text_frame_h[menu_level_id]);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set the color to White
			} // else
			u8g2_SetFont(&m1_u8g2, menu_text_font_b[menu_level_id]);
    		u8g2_DrawStr(&m1_u8g2, menu_text_left_pos_x[menu_level_id], menu_text_y, phmenu[run - 1]);
    		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return the color to Black
    		u8g2_SetFont(&m1_u8g2, menu_text_font_n[menu_level_id]);
		} // if ( run==active_item )
		else
		{
			u8g2_DrawStr(&m1_u8g2, menu_text_left_pos_x[menu_level_id], menu_text_y, phmenu[run - 1]);
		} // else
		if ( menu_level_id==0 )
		{
			// Draw icons for main menu items
			u8g2_DrawXBMP(&m1_u8g2, MAIN_MENU_ICON_LEFT_POS_X, menu_frame_y + 1, MAIN_MENU_ICON_WIDTH, MAIN_MENU_ICON_HEIGHT, this_gui_menu->submenu[run - 1]->icon_ptr);
		}
		menu_frame_y += menu_text_frame_h[menu_level_id];
		menu_text_y += menu_text_spacing_vert[menu_level_id];
	} // for (run=disp_window_top_row; run<=disp_window_bottom_row; run++)

	if ( menu_level_id==0 )
	{
		// Draw logo for main menu
		u8g2_DrawXBMP(&m1_u8g2, MAIN_MENU_LOGO_LEFT_POS_X, MAIN_MENU_LOGO_TOP_POS_Y, MAIN_MENU_LOGO_WIDTH, MAIN_MENU_LOGO_HEIGHT, m1_logo_26x14);
		u8g2_SetFont(&m1_u8g2, MAIN_MENU_LOGO_FONT);
		u8g2_DrawStr(&m1_u8g2, MAIN_MENU_LOGO_LEFT_POS_X + MAIN_MENU_LOGO_WIDTH + 1, MAIN_MENU_LOGO_TOP_POS_Y + MAIN_MENU_LOGO_HEIGHT, "M1");
	} // if ( menu_level_id==0 )

	// Draw the scroll bar
	u8g2_DrawXBMP(&m1_u8g2, MENU_SCROLLBAR_POS_X, MENU_SCROLLBAR_POS_Y, MENU_SCROLLBAR_WIDTH, M1_LCD_DISPLAY_HEIGHT, menu_scroll_bar_4x64);
	// Draw the scroll bar handle
	u8g2_DrawBox(&m1_u8g2, MENU_SCROLLBAR_POS_X, (M1_LCD_DISPLAY_HEIGHT*sel_item)/num_items, MENU_SCROLLBAR_WIDTH, M1_LCD_DISPLAY_HEIGHT/num_items);

	u8g2_NextPage(&m1_u8g2); // Update display RAM

	// Back up
	if ( x_menu_update_init )
	{
		x_menu_display[x_menu_level].active_disp_row = disp_window_active_row;
		x_menu_display[x_menu_level].disp_top_row = disp_window_top_row;
		x_menu_display[x_menu_level].sel_item = sel_item;
	} // if ( x_menu_update_init )

	return 0;
} // uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction)



/*============================================================================*/
/**
  * @brief This function updates welcome screen content periodically
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_gui_scr_animation(void)
{
	/* Boot/welcome M1 logo removed (menu_m1_icon_M1_logo_1 erased from the code).
	 * Nothing to animate; leave the panel blank if this is ever re-enabled. */
	if ( m1_device_stat.op_mode != M1_OPERATION_MODE_DISPLAY_ON ) // welcome screen only
		return;

	u8g2_FirstPage(&m1_u8g2);
	do
	{
		/* intentionally blank -- no logo */
	} while (u8g2_NextPage(&m1_u8g2));
} // void m1_gui_scr_animation(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_init(bool high_box)
{
	uint8_t y0 = 42, h0 = 22;
	const uint8_t *pframe = m1_frame_128x22;

	info_box_first_row = INFO_BOX_Y_POS_ROW_2; // low box
	info_box_high_box = high_box;
	if ( high_box )
	{
		info_box_first_row = INFO_BOX_Y_POS_ROW_1; // high box
		y0 = 32;
		h0 = 32;
		pframe = m1_frame_128x32;
	}

	// Draw info box at the bottom
	u8g2_DrawXBMP(&m1_u8g2, 0, y0, 128, h0, pframe);
} // void m1_info_box_display_init(bool high_box)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_clear(void)
{
	uint8_t y0 = 42, h0 = 22;

	if ( info_box_high_box )
	{
		y0 = 32;
		h0 = 32;
	}

	// Draw box to clear old content
	u8g2_DrawBox(&m1_u8g2, 1, y0 + 1, 126, h0 - 2);
} // void m1_info_box_display_clear(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext)
{
	uint8_t yn;

	if ( box_row >= INFO_BOX_ROW_EOL )
		return;

	yn = (box_row - INFO_BOX_ROW_1)*10;
	yn += info_box_first_row;

	u8g2_DrawStr(&m1_u8g2, 4, yn, ptext);
} // void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t m1_message_box(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons)
{
    S_M1_Main_Q_t q_item;
    S_M1_Buttons_Status this_button_status;
    BaseType_t ret;

    u8g2_UserInterfaceMessage(u8g2, title1, title2, title3, buttons);

    for(;;)
    {
  		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
  		if (ret==pdTRUE)
  		{
  			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
  			{
  				// Notification is only sent to this task when there's any button activity,
  				// so it doesn't need to wait when reading the event from the queue
  				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
  				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
  				{
  					; // Do extra tasks here if needed
  					xQueueReset(main_q_hdl); // Reset main q before return
  					break; // Exit and return to the calling task (subfunc_handler_task)
  				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
  				else
  				{
  					; // Do other things for this task, if needed
  				}
  			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
  			else
  			{
  				; // Do other things for this task
  			}
  		} // if (ret==pdTRUE)
    }

    /* never reached */
    return 0;
}

/*============================================================================*/
/**
 * @brief m1_draw_file_saved_screen - Universal "file saved" success splash.
 *
 * Shared success visual for EVERY save-to-file operation (NFC save / Edit-UID
 * save-back / Rename, RFID save / UID-edit / Rename / Add-manual, Sub-GHz
 * Record save, Wi-Fi handshake-to-SD, Bluetooth device export). Draws the
 * approved m1_saved_screen_128x64 asset pixel-for-pixel, full-screen -- the
 * same bitmap for every caller, not a per-feature duplicate. DRAW ONLY --
 * each caller keeps its own return/timeout/blocking behaviour; this function
 * has no effect on any workflow's existing dismiss timing. */
/*============================================================================*/
void m1_draw_file_saved_screen(void)
{
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 64, m1_saved_screen_128x64);
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief m1_wait_back_to_exit - Block until BACK is pressed, then return.
 *
 * Same main-queue/button wait m1_message_box uses, exposed so a caller can pair
 * it with a custom full-screen splash (the shared file-saved screen) while
 * keeping the existing "BACK to exit" blocking semantics. */
/*============================================================================*/
void m1_wait_back_to_exit(void)
{
	S_M1_Main_Q_t q_item;
	S_M1_Buttons_Status this_button_status;
	BaseType_t ret;

	for (;;)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					xQueueReset(main_q_hdl);
					break;
				}
			}
		}
	}
}



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_bottom_bar(u8g2_t *u8g2, const uint8_t *lbitmap, const char *ltext, const char *rtext, const uint8_t *rbitmap)
{
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(&m1_u8g2, 0, 52, 128, 12); // Draw an inverted bar at the bottom to display options

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // Write text in inverted color
	u8g2_DrawXBMP(&m1_u8g2, 1, 54, 8, 8, lbitmap); // draw arrowleft icon
	m1_draw_text(&m1_u8g2, 11, 61, 50,ltext, TEXT_ALIGN_LEFT);
	u8g2_DrawXBMP(&m1_u8g2, 119, 54, 8, 8, rbitmap); // draw arrowright icon
	m1_draw_text(&m1_u8g2, 67, 61, 50,rtext, TEXT_ALIGN_RIGHT);
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_text(u8g2_t *u8g2,
                 int x, int y,
                 int max_width,
                 const char *text,
                 S_M1_text_align_t align)
{
    if (text == NULL) return;

    uint16_t text_width = u8g2_GetStrWidth(u8g2, text);
    int draw_x = x;

    switch (align)
    {
        case TEXT_ALIGN_CENTER:
            draw_x = x + (max_width - text_width) / 2;
            break;

        case TEXT_ALIGN_RIGHT:
            draw_x = x + (max_width - text_width);
            break;

        default: // TEXT_ALIGN_LEFT
            draw_x = x;
            break;
    }

    u8g2_DrawStr(u8g2, draw_x, y, text);
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_text_box(u8g2_t *u8g2,
                    int x, int y,
                    int max_width,
                    int line_height,
                    const char *text,
                    S_M1_text_align_t align)
{
    const char *p = text;
    char line[128];
    int line_len = 0;

    while (*p)
    {
        if (*p == '\n')
        {
            if (line_len > 0)
            {
                line[line_len] = '\0';

                int draw_x = x;
                uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

                switch (align)
                {
                    case TEXT_ALIGN_CENTER:
                        draw_x = x + (max_width - line_width) / 2;
                        break;

                    case TEXT_ALIGN_RIGHT:
                        draw_x = x + (max_width - line_width);
                        break;

                    default: // LEFT
                        draw_x = x;
                        break;
                }

                u8g2_DrawStr(u8g2, draw_x, y, line);
                y += line_height;
            }

            line_len = 0;
            p++;
            continue;
        }


        line[line_len] = *p;
        line[line_len + 1] = '\0';

        uint16_t w = u8g2_GetStrWidth(u8g2, line);

        if (w > max_width)
        {
            line[line_len] = '\0';

            int draw_x = x;
            uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

            switch (align)
            {
                case TEXT_ALIGN_CENTER:
                    draw_x = x + (max_width - line_width) / 2;
                    break;

                case TEXT_ALIGN_RIGHT:
                    draw_x = x + (max_width - line_width);
                    break;

                default: // LEFT
                    draw_x = x;
                    break;
            }

            u8g2_DrawStr(u8g2, draw_x, y, line);
            y += line_height;

            line_len = 0;
            continue;
        }

        line_len++;
        p++;
    }

    if (line_len > 0)
    {
        line[line_len] = '\0';
        int draw_x = x;

        uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

        switch (align)
        {
            case TEXT_ALIGN_CENTER:
                draw_x = x + (max_width - line_width) / 2;
                break;

            case TEXT_ALIGN_RIGHT:
                draw_x = x + (max_width - line_width);
                break;

            default:
                draw_x = x;
                break;
        }

        u8g2_DrawStr(u8g2, draw_x, y, line);
    }
} // void m1_draw_text_box(...)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
#define M1_IMAGE_MESSAGE_FONT			M1_DISP_FUNC_MENU_FONT_N
#define M1_IMAGE_MESSAGE_FONT_WIDTH		5
#define M1_IMAGE_MESSAGE_FONT_HEIGHT	8
#define M1_IMAGE_MESSAGE_FONT_SPACE		3 // Space from image bottom to the top of the message

void m1_image_message(const uint8_t *pimage, uint8_t image_w, uint8_t image_h, const char *message)
{
	uint8_t msg_len, txt_width;
	uint8_t image_x0, image_y0, msg_x0, msg_y0;
	uint8_t clr_box_x, clr_box_y, clr_box_w, clr_box_h;

	msg_len = strlen(message);
	txt_width = msg_len*M1_IMAGE_MESSAGE_FONT_WIDTH;

	image_x0 = (M1_LCD_DISPLAY_WIDTH - image_w)/2;
	image_y0 = (M1_LCD_DISPLAY_HEIGHT - image_h)/2;
	msg_x0 = (M1_LCD_DISPLAY_WIDTH - txt_width)/2 + 1;
	msg_y0 = M1_LCD_DISPLAY_HEIGHT - (M1_LCD_DISPLAY_HEIGHT - image_h)/2 + M1_IMAGE_MESSAGE_FONT_HEIGHT + M1_IMAGE_MESSAGE_FONT_SPACE;
	if ( msg_y0 >= M1_LCD_DISPLAY_HEIGHT )
	{
		msg_y0 = M1_LCD_DISPLAY_HEIGHT - 1; // Move to the bottom of the LCD
	}
	clr_box_x = (msg_x0 < image_x0)?msg_x0:image_x0; // Get the left most position of x
	if ( clr_box_x )
		clr_box_x--; // Expand one pixel to the left
	clr_box_y = image_y0;
	if ( clr_box_y )
		clr_box_y--; // Expand one pixel to the top
	clr_box_w = (txt_width > image_w)?txt_width:image_w; // Get the max width
	clr_box_w += 2; // Expand the clear box width
	if ( clr_box_w >= M1_LCD_DISPLAY_WIDTH )
	{
		clr_box_w = M1_LCD_DISPLAY_WIDTH;
	}
	clr_box_h = msg_y0 - image_y0;
	clr_box_h += 2; // Expand the clear box height
	if ( clr_box_h >= M1_LCD_DISPLAY_HEIGHT )
	{
		clr_box_h = M1_LCD_DISPLAY_HEIGHT;
	}

	u8g2_FirstPage(&m1_u8g2);	//+2026_0513_01

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
	// Draw solid box to clear existing content
	u8g2_DrawBox(&m1_u8g2, clr_box_x, clr_box_y, clr_box_w, clr_box_h);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // Set to text color
	// Draw image
	u8g2_DrawXBMP(&m1_u8g2, image_x0, image_y0, image_w, image_h, pimage);
	u8g2_SetFont(&m1_u8g2, M1_IMAGE_MESSAGE_FONT);
	// Draw message
	u8g2_DrawStr(&m1_u8g2, msg_x0, msg_y0, message);

	m1_u8g2_nextpage();	 // Update display RAM

} // void m1_image_message(const uint8_t *pimage, uint8_t image_w, uint8_t image_h, const char *message)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_gui_let_update_fw(void)
{
    /* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
    do
    {
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_DrawXBMP(&m1_u8g2, 40, 2, 48, 48, fw_update_48x48); // draw firmware update icon
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 30, 62, "Coming soon");
    } while (u8g2_NextPage(&m1_u8g2));
} // void m1_gui_let_update_fw(void)

/*============================================================================*/
/* @brief m1_draw_write_screen - Shared NFC/RFID Write screen graphic.
 *
 * Renders the approved Write/Success screen: a centered horizontal card icon
 * (same visual family/weight as the NFC/RFID Read and Emulate screens) flanked
 * by three inward-pointing RF arrows on each side, with one centered status
 * line below.  Rendering only -- no polling/write/verify/timeout/buzzer/
 * navigation logic.  Callers pass "WRITING..." during the write and "SUCCESS"
 * on a verified success (identical graphic; only the status text changes).
 *
 * @param[in] status Centered status string ("WRITING..." or "SUCCESS")
 * @retval None
 */
/*============================================================================*/
void m1_draw_write_screen(const char *status)
{
	int i, t, w;

	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Same rounded tag (44x24) + chip dot as the Read/Emulate screens, drawn
	 * from the shared m1_read_icon geometry so it can never drift. */
	m1_read_icon_draw_tag(&m1_u8g2);

	/* three left arrows  >>>  pointing RIGHT toward the tag (compact) */
	for (i = 0; i < 3; i++) {
		int vx = 22 + i * 8;
		int ax = vx - 4;
		for (t = 0; t < 2; t++) {
			u8g2_DrawLine(&m1_u8g2, ax + t, 19, vx + t, 23);
			u8g2_DrawLine(&m1_u8g2, vx + t, 23, ax + t, 27);
		}
	}

	/* three right arrows  <<<  pointing LEFT toward the tag (compact) */
	for (i = 0; i < 3; i++) {
		int vx = 106 - i * 8;
		int ax = vx + 4;
		for (t = 0; t < 2; t++) {
			u8g2_DrawLine(&m1_u8g2, ax + t, 19, vx + t, 23);
			u8g2_DrawLine(&m1_u8g2, vx + t, 23, ax + t, 27);
		}
	}

	/* centered status line -- same caption font as the Read/Emulate screens */
	u8g2_SetFont(&m1_u8g2, M1_READ_ICON_CAPTION_FONT);
	w = (int)u8g2_GetStrWidth(&m1_u8g2, status);
	u8g2_DrawStr(&m1_u8g2, (128 - w) / 2, 58, status);
	m1_u8g2_nextpage();
}
