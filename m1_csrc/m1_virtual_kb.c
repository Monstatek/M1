/* See COPYING.txt for license details. */

/*
*
* m1_virtual_kb.c
*
* Library for the virtual keyboard
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "u8g2.h"
#include "mui.h"
#include "m1_virtual_kb.h"
#include "m1_kb_logic.h"   /* pure grid-wrap / edit-buffer logic (host-tested) */

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG				"VKB"

#define M1_VIRTUAL_KB_COLUMN_SIZE		14
#define M1_VIRTUAL_KB_ROW_SIZE			3

#define M1_VIRTUAL_KEY_BS				0x08 // Backspace
#define M1_VIRTUAL_KEY_ENTER			0x0A
#define M1_VIRTUAL_KEY_NONE				0x00 // NULL

#define M1_VIRTUAL_KB_FILENAME_MAX		20
//#define M1_VIRTUAL_KBS_DATA_MAX			14 // 10 + 4 spaces
uint8_t M1_VIRTUAL_KBS_DATA_MAX;

#define M1_VIRTUAL_KB_FONT_N			u8g2_font_resoledmedium_tr //u8g2_font_6x10_tf // u8g2_font_5x8_tf, u8g2_font_7x13_tf

#define M1_VIRTUAL_KB_FONT_SIZE_5x8		0
#define M1_VIRTUAL_KB_FONT_SIZE_6x10	1
#define M1_VIRTUAL_KB_FONT_SIZE_7x13	2

#define M1_VIRTUAL_KB_FONT_SIZE			M1_VIRTUAL_KB_FONT_SIZE_6x10

#if M1_VIRTUAL_KB_FONT_SIZE==M1_VIRTUAL_KB_FONT_SIZE_5x8

#define M1_VKB_GUI_FONT_WIDTH				5	// pixel
#define M1_VKB_GUI_FONT_HEIGHT				8	// pixel
#define M1_VKB_FONT_HEIGHT_SPACING			3	// pixel
#define M1_VKB_FONT_WIDTH_SPACING			4	// pixel

#elif M1_VIRTUAL_KB_FONT_SIZE==M1_VIRTUAL_KB_FONT_SIZE_6x10

#define M1_VKB_GUI_FONT_WIDTH				6	// pixel
#define M1_VKB_GUI_FONT_HEIGHT				10	// pixel
#define M1_VKB_FONT_HEIGHT_SPACING			2	// pixel
#define M1_VKB_FONT_WIDTH_SPACING			3	// pixel

#endif // #if M1_VIRTUAL_KB_FONT_SIZE==M1_VIRTUAL_KB_FONT_SIZE_5x8

// Defines for VKB
#define M1_VIRTUAL_KBS_COLUMN_SIZE			9
#define M1_VIRTUAL_KBS_ROW_SIZE				2

#define M1_VKB_FIRST_ROW_TOP_POS_Y			28
#define M1_VKB_LEFT_POS_X					M1_VKB_FONT_WIDTH_SPACING

#define M1_VKB_DESCRIPTION_POS_Y			12
#define M1_VKB_DESCRIPTION_POS_X			2

#define M1_VKB_FILENAME_POS_Y				25
#define M1_VKB_FILENAME_POS_X				2
#define M1_VKB_FILENAME_FRAME_POS_Y	14
#define M1_VKB_FILENAME_FRAME_POS_X	0
#define M1_VKB_FILENAME_FRAME_WIDTH			M1_LCD_DISPLAY_WIDTH
#define M1_VKB_FILENAME_FRAME_HEIGHT		14

#define M1_VKB_BACKSPACE_X					(M1_VKB_LEFT_POS_X + 9*(M1_VKB_GUI_FONT_WIDTH + M1_VKB_FONT_WIDTH_SPACING))
#define M1_VKB_BACKSPACE_Y					(M1_VKB_FIRST_ROW_TOP_POS_Y + 1*M1_VKB_GUI_FONT_HEIGHT + M1_VKB_FONT_HEIGHT_SPACING)
#define M1_VKB_BACKSPACE_ICON_W				15
#define M1_VKB_BACKSPACE_ICON_H				10

#define M1_VKB_ENTER_X						(M1_VKB_LEFT_POS_X + 9*(M1_VKB_GUI_FONT_WIDTH + M1_VKB_FONT_WIDTH_SPACING))
#define M1_VKB_ENTER_Y						(M1_VKB_FIRST_ROW_TOP_POS_Y + 2*M1_VKB_GUI_FONT_HEIGHT + 2*M1_VKB_FONT_HEIGHT_SPACING)
#define M1_VKB_ENTER_ICON_W					15
#define M1_VKB_ENTER_ICON_H					10

#define M1_VKB_DEFAULT_KEY_MAP_COL_ID		9
#define M1_VKB_DEFAULT_KEY_MAP_ROW_ID		2

#define M1_VKB_NUM_OF_FUNCTION_KEYS			2

// Defines for VKBS
#define M1_VKBS_FONT_HEIGHT_SPACING			6	// pixel
#define M1_VKBS_FONT_WIDTH_SPACING			7	// pixel

#define M1_VKBS_FIRST_ROW_TOP_POS_Y			34
#define M1_VKBS_LEFT_POS_X					3

#define M1_VKBS_DESCRIPTION_POS_Y			12
#define M1_VKBS_DESCRIPTION_POS_X			2

#define M1_VKBS_DATA_POS_Y					27//25
#define M1_VKBS_DATA_POS_X					3
#define M1_VKBS_DATA_FRAME_POS_Y			16//14
#define M1_VKBS_DATA_FRAME_POS_X			0
#define M1_VKBS_DATA_FRAME_WIDTH			M1_LCD_DISPLAY_WIDTH
#define M1_VKBS_DATA_FRAME_HEIGHT			14

#define M1_VKBS_BACKSPACE_X					(M1_LCD_DISPLAY_WIDTH - M1_VKBS_BACKSPACE_ICON_W - 3 )
#define M1_VKBS_BACKSPACE_Y					(M1_VKBS_FIRST_ROW_TOP_POS_Y)
#define M1_VKBS_BACKSPACE_ICON_W			15
#define M1_VKBS_BACKSPACE_ICON_H			10

#define M1_VKBS_ENTER_X						(M1_LCD_DISPLAY_WIDTH - M1_VKBS_ENTER_ICON_W - 3 )
#define M1_VKBS_ENTER_Y						(M1_VKBS_FIRST_ROW_TOP_POS_Y + M1_VKB_GUI_FONT_HEIGHT + M1_VKBS_FONT_HEIGHT_SPACING)
#define M1_VKBS_ENTER_ICON_W				15
#define M1_VKBS_ENTER_ICON_H				10

#define M1_VKBS_DEFAULT_KEY_MAP_COL_ID		0
#define M1_VKBS_DEFAULT_KEY_MAP_ROW_ID		0

#define M1_VKBS_NUM_OF_FUNCTION_KEYS		2

//************************** C O N S T A N T **********************************/

// 'backspace key', 15x10px
const uint8_t m1_virtual_kb_icon_backspace[] = {
/*
	0x00, 0x00, 0x10, 0x00, 0x18, 0x00, 0xfc, 0x3f, 0xfe, 0x3f, 0xfe, 0x3f, 0xfc, 0x3f, 0x18, 0x00,
	0x10, 0x00, 0x00, 0x00
*/
	0x00, 0x00, 0xf8, 0x7f, 0x1c, 0x60, 0xce, 0x66, 0x87, 0x63, 0x87, 0x63, 0xce, 0x66, 0x1c, 0x60,
	0xf8, 0x7f, 0x00, 0x00
};

// 'backspace key', 15x10px
const uint8_t m1_virtual_kb_icon_backspace_inv[] = {
/*
	0xff, 0x7f, 0xef, 0x7f, 0xe7, 0x7f, 0x03, 0x40, 0x01, 0x40, 0x01, 0x40, 0x03, 0x40, 0xe7, 0x7f,
	0xef, 0x7f, 0xff, 0x7f
*/
	0xff, 0x7f, 0x07, 0x00, 0xe3, 0x1f, 0x31, 0x19, 0x78, 0x1c, 0x78, 0x1c, 0x31, 0x19, 0xe3, 0x1f,
	0x07, 0x00, 0xff, 0x7f
};

// 'enter key', 15x10px
const uint8_t m1_virtual_kb_icon_enter[] = {
	0x10, 0x1e, 0x18, 0x1e, 0x1c, 0x1e, 0xfe, 0x1f, 0xff, 0x1f, 0xff, 0x1f, 0xfe, 0x1f, 0x1c, 0x00,
	0x18, 0x00, 0x10, 0x00
};

// 'enter key', 15x10px
const uint8_t m1_virtual_kb_icon_enter_inv[] = {
	0xef, 0x61, 0xe7, 0x61, 0xe3, 0x61, 0x01, 0x60, 0x00, 0x60, 0x00, 0x60, 0x01, 0x60, 0xe3, 0x7f,
	0xe7, 0x7f, 0xef, 0x7f
};

const uint8_t m1_vkb_map[M1_VIRTUAL_KB_ROW_SIZE][M1_VIRTUAL_KB_COLUMN_SIZE] =
{
		{'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 					'0', 					'1', '2', '3'},
		{'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', M1_VIRTUAL_KEY_BS, 	M1_VIRTUAL_KEY_BS,		'4', '5', '6'},
		{'z', 'x', 'c', 'v', 'b', 'n', 'm', '_', '.', M1_VIRTUAL_KEY_ENTER, M1_VIRTUAL_KEY_ENTER, 	'7', '8', '9'}
};

const uint8_t m1_vkbs_map[M1_VIRTUAL_KBS_ROW_SIZE][M1_VIRTUAL_KBS_COLUMN_SIZE] =
{
		{'1', '2', '3', '4', '5', 'A', 'B', 'C', M1_VIRTUAL_KEY_BS},
		{'6', '7', '8', '9', '0', 'D', 'E', 'F', M1_VIRTUAL_KEY_ENTER}
};


const uint8_t m1_vkb_func_key_codes[M1_VKB_NUM_OF_FUNCTION_KEYS] = {M1_VIRTUAL_KEY_BS, M1_VIRTUAL_KEY_ENTER};

//************************** S T R U C T U R E S *******************************

typedef struct
{
	const uint8_t *icon_reg; // icon in normal mode
	const uint8_t *icon_inv; // icon in inverted mode
	uint8_t icon_w, icon_h; // icon width and height
	uint8_t icon_x[2], icon_y[2]; // icon position x and y
	uint8_t col_factor; // column factor of this icon compared to that of a normal character
	uint8_t key_row_id, key_col_id;
} S_M1_VKB_X_Key;

typedef enum
{
	M1_VKB_FUNC_BS_KEY_ID = 0,
	M1_VKB_FUNC_ENTER_KEY_ID,
	M1_VKB_FUNC_UNDEFINED_KEY_ID
} S_M1_VKB_Func_Key_ID;

/***************************** V A R I A B L E S ******************************/
S_M1_VKB_X_Key m1_x_keys[M1_VKB_NUM_OF_FUNCTION_KEYS] =
{
		{	.icon_reg = m1_virtual_kb_icon_backspace,
			.icon_inv = m1_virtual_kb_icon_backspace_inv,
			.icon_w = M1_VKB_BACKSPACE_ICON_W,
			.icon_h = M1_VKB_BACKSPACE_ICON_H,
			.icon_x = {M1_VKB_BACKSPACE_X, M1_VKBS_BACKSPACE_X},
			.icon_y = {M1_VKB_BACKSPACE_Y, M1_VKBS_BACKSPACE_Y},
			.col_factor = 2,
			.key_col_id = 9,
			.key_row_id = 1
		},
		{	.icon_reg = m1_virtual_kb_icon_enter,
			.icon_inv = m1_virtual_kb_icon_enter_inv,
			.icon_w = M1_VKB_ENTER_ICON_W,
			.icon_h = M1_VKB_ENTER_ICON_H,
			.icon_x = {M1_VKB_ENTER_X, M1_VKBS_ENTER_X},
			.icon_y = {M1_VKB_ENTER_Y, M1_VKBS_ENTER_Y},
			.col_factor = 2,
			.key_col_id = 9,
			.key_row_id = 2
		}
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t m1_vkb_get_filename(char *description, char *default_name, char *new_name, uint8_t default_is_generated);
uint8_t m1_vkbs_get_data(char *description, char *data_buffer);
uint8_t m1_vkbs_get_hexkey(char *description, char *out_hex, uint8_t req_nibbles);
S_M1_VKB_Func_Key_ID m1_vkb_check_function_key(uint8_t kb_key);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/*
 * This function displays the virtual keyboard for user to enter the filename
 * It returns the filename length and the filename string is given in the pointer new_name
 *
 */
/*============================================================================*/
/* Horizontal move on the filename grid WITH WRAP (global rule). The two-column
 * function keys (Backspace / Enter at cols 9-10 of rows 1-2) count as one stop:
 * if a move lands on the second cell of a function-key pair, it steps once more
 * so the cursor never appears stuck. */
static uint8_t vkb_col_move(uint8_t row, uint8_t col, int dir)
{
	uint8_t nc = m1_kb_col_wrap(col, dir, M1_VIRTUAL_KB_COLUMN_SIZE);
	uint8_t v  = m1_vkb_map[row][nc];
	if (m1_vkb_check_function_key(v) != M1_VKB_FUNC_UNDEFINED_KEY_ID) {
		uint8_t came = m1_kb_col_wrap(nc, -dir, M1_VIRTUAL_KB_COLUMN_SIZE);
		if (m1_vkb_map[row][came] == v)   /* landed on the 2nd cell of the pair */
			nc = m1_kb_col_wrap(nc, dir, M1_VIRTUAL_KB_COLUMN_SIZE);
	}
	return nc;
}

/* Full redraw of the filename keyboard. A device-generated default name is drawn
 * inverted (selected-default look) so it is obvious that one Delete clears it or
 * the first typed character replaces it. */
static void vkb_render(const char *description, const char *filename,
                       uint8_t generated, uint8_t row_id, uint8_t col_id)
{
	char key[2] = {0, 0};
	uint8_t x, y;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	m1_u8g2_firstpage();
	do {
		u8g2_SetFont(&m1_u8g2, M1_VIRTUAL_KB_FONT_N);
		if (description && description[0])
			u8g2_DrawStr(&m1_u8g2, M1_VKB_DESCRIPTION_POS_X, M1_VKB_DESCRIPTION_POS_Y, description);
		u8g2_DrawFrame(&m1_u8g2, M1_VKB_FILENAME_FRAME_POS_X, M1_VKB_FILENAME_FRAME_POS_Y,
		               M1_VKB_FILENAME_FRAME_WIDTH, M1_VKB_FILENAME_FRAME_HEIGHT);

		if (filename && filename[0]) {
			/* Window the name so the caret (append point = end of string) stays
			 * visible: draw the longest trailing run that fits the frame. Parity
			 * with the hex/data keyboard's windowed field. */
			const char *vis = filename;
			{
				u8g2_uint_t maxw = (u8g2_uint_t)((M1_VKB_FILENAME_FRAME_POS_X + M1_VKB_FILENAME_FRAME_WIDTH)
				                                 - M1_VKB_FILENAME_POS_X - 2);
				while (vis[0] && ((u8g2_uint_t)u8g2_GetStrWidth(&m1_u8g2, vis) > maxw)) vis++;
			}
			if (generated) {
				uint8_t w = (uint8_t)u8g2_GetStrWidth(&m1_u8g2, vis);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_DrawBox(&m1_u8g2, M1_VKB_FILENAME_POS_X - 1, M1_VKB_FILENAME_FRAME_POS_Y + 1,
				             (u8g2_uint_t)(w + 2), M1_VKB_FILENAME_FRAME_HEIGHT - 2);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
				u8g2_DrawStr(&m1_u8g2, M1_VKB_FILENAME_POS_X, M1_VKB_FILENAME_POS_Y, vis);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			} else {
				u8g2_DrawStr(&m1_u8g2, M1_VKB_FILENAME_POS_X, M1_VKB_FILENAME_POS_Y, vis);
			}
		}

		for (uint8_t r = 0; r < M1_VIRTUAL_KB_ROW_SIZE; r++) {
			for (uint8_t c = 0; c < M1_VIRTUAL_KB_COLUMN_SIZE; c++) {
				uint8_t v = m1_vkb_map[r][c];
				S_M1_VKB_Func_Key_ID fk = m1_vkb_check_function_key(v);
				if (fk != M1_VKB_FUNC_UNDEFINED_KEY_ID) {
					uint8_t is_first = (c == 0) || (m1_vkb_map[r][c - 1] != v);
					if (!is_first) continue;   /* 2-wide icon drawn once */
					uint8_t sel = (row_id == r) &&
					              ((col_id == c) ||
					               ((c + 1 < M1_VIRTUAL_KB_COLUMN_SIZE) && (col_id == c + 1) && (m1_vkb_map[r][c + 1] == v)));
					u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
					u8g2_DrawXBMP(&m1_u8g2, m1_x_keys[fk].icon_x[0], m1_x_keys[fk].icon_y[0],
					              m1_x_keys[fk].icon_w, m1_x_keys[fk].icon_h,
					              sel ? m1_x_keys[fk].icon_inv : m1_x_keys[fk].icon_reg);
				} else {
					key[0] = v;
					x = M1_VKB_LEFT_POS_X + c * (M1_VKB_GUI_FONT_WIDTH + M1_VKB_FONT_WIDTH_SPACING);
					y = M1_VKB_FIRST_ROW_TOP_POS_Y + M1_VKB_GUI_FONT_HEIGHT +
					    r * (M1_VKB_GUI_FONT_HEIGHT + M1_VKB_FONT_HEIGHT_SPACING);
					if ((row_id == r) && (col_id == c)) {
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_DrawBox(&m1_u8g2, x - 1, y - M1_VKB_GUI_FONT_HEIGHT + 1,
						             M1_VKB_GUI_FONT_WIDTH + 2, M1_VKB_GUI_FONT_HEIGHT + 2);
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
						u8g2_DrawStr(&m1_u8g2, x, y, key);
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
					} else {
						u8g2_DrawStr(&m1_u8g2, x, y, key);
					}
				}
			}
		}
	} while (m1_u8g2_nextpage());
}

/*============================================================================*/
/*
 * Virtual filename keyboard. Returns the entered filename length (0 on cancel);
 * the name is written to new_name. `default_is_generated` marks default_name as
 * a device-generated default: while untouched it is treated as selected text --
 * one Backspace clears it entirely, and the first typed character replaces it.
 */
/*============================================================================*/
uint8_t m1_vkb_get_filename(char *description, char *default_name, char *new_name, uint8_t default_is_generated)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t row_id = M1_VKB_DEFAULT_KEY_MAP_ROW_ID, col_id = M1_VKB_DEFAULT_KEY_MAP_COL_ID;
	uint8_t exit_ok = 0, len, generated;
	S_M1_VKB_Func_Key_ID x_key_id;
	char filename[M1_VIRTUAL_KB_FILENAME_MAX + 1];

	filename[0] = '\0';
	if (default_name) {
		strncpy(filename, default_name, M1_VIRTUAL_KB_FILENAME_MAX);
		filename[M1_VIRTUAL_KB_FILENAME_MAX] = '\0';
	}
	len = (uint8_t)strlen(filename);
	generated = (default_is_generated && len) ? 1 : 0;

	vkb_render(description, filename, generated, row_id, col_id);

	while (1) {
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ((ret != pdTRUE) || (q_item.q_evt_type != Q_EVENT_KEYPAD)) continue;
		if (xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE) continue;

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl);
			return 0;   /* cancel */
		}
		if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			uint8_t v = m1_vkb_map[row_id][col_id];
			x_key_id = m1_vkb_check_function_key(v);
			if (x_key_id == M1_VKB_FUNC_ENTER_KEY_ID) {
				if (len) { exit_ok = 1; if (new_name) strcpy(new_name, filename); }
			} else if (x_key_id == M1_VKB_FUNC_BS_KEY_ID) {
				len = m1_kb_name_bs(filename, len, &generated);
			} else {
				len = m1_kb_name_put(filename, len, M1_VIRTUAL_KB_FILENAME_MAX, (char)v, &generated);
			}
		} else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = vkb_col_move(row_id, col_id, +1);
		} else if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = vkb_col_move(row_id, col_id, -1);
		} else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id < M1_VIRTUAL_KB_ROW_SIZE - 1) row_id++;
		} else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id > 0) row_id--;
		}

		if (exit_ok) { xQueueReset(main_q_hdl); return len; }
		vkb_render(description, filename, generated, row_id, col_id);
	}
}// uint8_t m1_vkb_get_filename(char *description, char *default_name, char *new_name, uint8_t default_is_generated)



/*============================================================================*/
/**
 * @brief: This function checks if a key at (col, row) is a function key or not
 * @param:
 * @retval Function key index if found
 */
/*============================================================================*/
S_M1_VKB_Func_Key_ID m1_vkb_check_function_key(uint8_t kb_key)
{
	uint8_t i;

	for ( i=0; i<M1_VKB_NUM_OF_FUNCTION_KEYS; i++)
	{
		if ( m1_vkb_func_key_codes[i]==kb_key )
			break;
	} // for ( for ( i=0; i<M1_VKB_NUM_OF_FUNCTION_KEYS; i++)

	return i;
} // S_M1_VKB_Func_Key_ID m1_vkb_check_function_key(uint8_t kb_key)




/*============================================================================*/
/**
 * @brief: This function displays the virtual hex keyboard for user to edit data
 * @param: None
 * @retval 0 if user cancels, otherwise data length and data buffer
 */
/*============================================================================*/
/* Full redraw of the hex/data keyboard. The data field is WINDOWED so an
 * arbitrarily long fixed-width buffer (e.g. a 32-hex Ultralight C key) scrolls to
 * keep the current position visible; the keyboard grid wraps horizontally. */
static void vkbs_render(const char *description, const char *data_buffer,
                        uint8_t L, uint8_t buffer_id, uint8_t row_id, uint8_t col_id)
{
	char key[2] = {0, 0};
	uint8_t x, y;
	const uint8_t WIN = 20;   /* visible chars in the data field (~120px) */
	uint8_t win_start = (buffer_id >= WIN) ? (uint8_t)(buffer_id - WIN + 1) : 0;

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	m1_u8g2_firstpage();
	do {
		u8g2_SetFont(&m1_u8g2, M1_VIRTUAL_KB_FONT_N);
		if (description && description[0])
			u8g2_DrawStr(&m1_u8g2, M1_VKBS_DESCRIPTION_POS_X, M1_VKBS_DESCRIPTION_POS_Y, description);
		u8g2_DrawFrame(&m1_u8g2, M1_VKBS_DATA_FRAME_POS_X, M1_VKBS_DATA_FRAME_POS_Y,
		               M1_VKBS_DATA_FRAME_WIDTH, M1_VKBS_DATA_FRAME_HEIGHT);

		/* data field (windowed), current position inverted */
		for (uint8_t i = 0; (i < WIN) && ((uint16_t)win_start + i < L); i++) {
			key[0] = data_buffer[win_start + i];
			x = M1_VKBS_DATA_POS_X + i * M1_VKB_GUI_FONT_WIDTH;
			y = M1_VKBS_DATA_POS_Y;
			if ((win_start + i) == buffer_id) {
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_DrawBox(&m1_u8g2, x - 1, y - M1_VKB_GUI_FONT_HEIGHT + 1,
				             M1_VKB_GUI_FONT_WIDTH + 1, M1_VKB_GUI_FONT_HEIGHT);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
				u8g2_DrawStr(&m1_u8g2, x, y, key);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			} else {
				u8g2_DrawStr(&m1_u8g2, x, y, key);
			}
		}

		/* keyboard grid: cols 0-7 hex chars, col 8 = function key (BS row0 / ENTER row1) */
		for (uint8_t r = 0; r < M1_VIRTUAL_KBS_ROW_SIZE; r++) {
			for (uint8_t c = 0; c < M1_VIRTUAL_KBS_COLUMN_SIZE; c++) {
				uint8_t v = m1_vkbs_map[r][c];
				S_M1_VKB_Func_Key_ID fk = m1_vkb_check_function_key(v);
				uint8_t sel = (r == row_id) && (c == col_id);
				if (fk != M1_VKB_FUNC_UNDEFINED_KEY_ID) {
					u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
					u8g2_DrawXBMP(&m1_u8g2, m1_x_keys[fk].icon_x[1], m1_x_keys[fk].icon_y[1],
					              m1_x_keys[fk].icon_w, m1_x_keys[fk].icon_h,
					              sel ? m1_x_keys[fk].icon_inv : m1_x_keys[fk].icon_reg);
				} else {
					key[0] = v;
					x = M1_VKBS_LEFT_POS_X + c * (M1_VKB_GUI_FONT_WIDTH + M1_VKBS_FONT_WIDTH_SPACING);
					y = M1_VKBS_FIRST_ROW_TOP_POS_Y + M1_VKB_GUI_FONT_HEIGHT +
					    r * (M1_VKB_GUI_FONT_HEIGHT + M1_VKBS_FONT_HEIGHT_SPACING);
					if (sel) {
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
						u8g2_DrawBox(&m1_u8g2, x - 1, y - M1_VKB_GUI_FONT_HEIGHT + 1,
						             M1_VKB_GUI_FONT_WIDTH + 2, M1_VKB_GUI_FONT_HEIGHT + 2);
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
						u8g2_DrawStr(&m1_u8g2, x, y, key);
						u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
					} else {
						u8g2_DrawStr(&m1_u8g2, x, y, key);
					}
				}
			}
		}
	} while (m1_u8g2_nextpage());
}

/*============================================================================*/
/*
 * Virtual hex/data keyboard. The caller seeds data_buffer with a fixed-width
 * field (its strlen sets the number of editable positions -- e.g. 32 '0's for a
 * 32-hex Ultralight C key). Each hex key overwrites the current position and
 * advances; Backspace clears/steps back; the keyboard wraps horizontally.
 * Returns the field length on Enter (nonzero), 0 on cancel.
 */
/*============================================================================*/
uint8_t m1_vkbs_get_data(char *description, char *data_buffer)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t row_id = M1_VKBS_DEFAULT_KEY_MAP_ROW_ID, col_id = M1_VKBS_DEFAULT_KEY_MAP_COL_ID;
	uint8_t buffer_id = 0, exit_ok = 0, L;
	S_M1_VKB_Func_Key_ID x_key_id;

	if (data_buffer == NULL) return 0;
	L = (uint8_t)strlen(data_buffer);
	M1_VIRTUAL_KBS_DATA_MAX = L;
	if (L == 0) return 0;   /* nothing to edit -- caller must seed a fixed-width field */

	vkbs_render(description, data_buffer, L, buffer_id, row_id, col_id);

	while (1) {
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ((ret != pdTRUE) || (q_item.q_evt_type != Q_EVENT_KEYPAD)) continue;
		if (xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE) continue;

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl);
			return 0;   /* cancel */
		}
		if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			uint8_t v = m1_vkbs_map[row_id][col_id];
			x_key_id = m1_vkb_check_function_key(v);
			if (x_key_id == M1_VKB_FUNC_ENTER_KEY_ID) {
				exit_ok = 1;
			} else if (x_key_id == M1_VKB_FUNC_BS_KEY_ID) {
				buffer_id = m1_kb_data_bs(data_buffer, buffer_id);
			} else {
				buffer_id = m1_kb_data_put(data_buffer, buffer_id, L, (char)v);
			}
		} else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = m1_kb_col_wrap(col_id, +1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = m1_kb_col_wrap(col_id, -1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id < M1_VIRTUAL_KBS_ROW_SIZE - 1) row_id++;
		} else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id > 0) row_id--;
		}

		if (exit_ok) { xQueueReset(main_q_hdl); return L; }
		vkbs_render(description, data_buffer, L, buffer_id, row_id, col_id);
	}
}// uint8_t m1_vkbs_get_data(char *description, char *data_buffer)

/*============================================================================*/
/*
 * Growing fixed-length hex-key editor. Reuses the hex keyboard's grid + windowed
 * data field (vkbs_render) but with APPEND semantics and IN-EDITOR validation:
 * an incomplete Save keeps the user on this screen with the entry preserved and
 * a "Need N hex (n/N)" message in place of the header. See the header comment.
 */
/*============================================================================*/
uint8_t m1_vkbs_get_hexkey(char *description, char *out_hex, uint8_t req_nibbles)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t row_id = M1_VKBS_DEFAULT_KEY_MAP_ROW_ID, col_id = M1_VKBS_DEFAULT_KEY_MAP_COL_ID;
	uint8_t len = 0, warn = 0;
	char hdr[32];
	S_M1_VKB_Func_Key_ID x_key_id;

	if ((out_hex == NULL) || (req_nibbles == 0)) return 0;
	out_hex[0] = '\0';

	for (;;) {
		/* Header shows the validation message while `warn` is set, else the label.
		 * The data field shows the entered digits plus an empty append slot (the
		 * cursor), windowed so it stays visible for long keys. */
		const char *top = description;
		if (warn) { snprintf(hdr, sizeof(hdr), "Need %u hex (%u/%u)", req_nibbles, len, req_nibbles); top = hdr; }
		uint8_t vlen = (len < req_nibbles) ? (uint8_t)(len + 1) : req_nibbles;
		uint8_t cur  = (len < req_nibbles) ? len : (uint8_t)(req_nibbles - 1);
		vkbs_render(top, out_hex, vlen, cur, row_id, col_id);

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ((ret != pdTRUE) || (q_item.q_evt_type != Q_EVENT_KEYPAD)) continue;
		if (xQueueReceive(button_events_q_hdl, &this_button_status, 0) != pdTRUE) continue;

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl);
			return 0;   /* cancel -- never shows the validation warning */
		}
		if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			uint8_t v = m1_vkbs_map[row_id][col_id];
			x_key_id = m1_vkb_check_function_key(v);
			if (x_key_id == M1_VKB_FUNC_ENTER_KEY_ID) {
				if (len == req_nibbles) { xQueueReset(main_q_hdl); return req_nibbles; }
				warn = 1;   /* incomplete: stay on this screen, keep input */
			} else if (x_key_id == M1_VKB_FUNC_BS_KEY_ID) {
				if (len > 0) { len--; out_hex[len] = '\0'; }
				warn = 0;
			} else {   /* hex digit -> append (uppercase from the map) */
				if (len < req_nibbles) { out_hex[len] = (char)v; out_hex[len + 1u] = '\0'; len++; }
				warn = 0;
			}
		} else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = m1_kb_col_wrap(col_id, +1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) {
			col_id = m1_kb_col_wrap(col_id, -1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id < M1_VIRTUAL_KBS_ROW_SIZE - 1) row_id++;
		} else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row_id > 0) row_id--;
		}
	}
}// uint8_t m1_vkbs_get_hexkey(char *description, char *out_hex, uint8_t req_nibbles)

/* Variable-length byte editor for GATT writes. The existing data editor needs
 * a prefilled field, and the hex-key editor requires an exact fixed length. */
uint8_t m1_vkbs_get_hex_bytes(char *description, char *out_hex, uint8_t max_bytes)
{
	S_M1_Buttons_Status buttons;
	S_M1_Main_Q_t q_item;
	uint8_t row = M1_VKBS_DEFAULT_KEY_MAP_ROW_ID;
	uint8_t col = M1_VKBS_DEFAULT_KEY_MAP_COL_ID;
	uint8_t len = 0, warn = 0;
	uint8_t max_nibbles;

	if ((out_hex == NULL) || (max_bytes == 0U) || (max_bytes > 64U)) return 0;
	max_nibbles = (uint8_t)(max_bytes * 2U);
	out_hex[0] = '\0';

	for (;;) {
		const char *top = warn ? "Enter whole bytes" : description;
		uint8_t visible = (len < max_nibbles) ? (uint8_t)(len + 1U) : len;
		uint8_t cursor = (len < max_nibbles) ? len : (uint8_t)(len - 1U);
		vkbs_render(top, out_hex, visible, cursor, row, col);

		if (xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY) != pdTRUE ||
		    q_item.q_evt_type != Q_EVENT_KEYPAD ||
		    xQueueReceive(button_events_q_hdl, &buttons, 0) != pdTRUE) continue;
		if (buttons.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl);
			return 0;
		}
		if (buttons.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			uint8_t key = m1_vkbs_map[row][col];
			S_M1_VKB_Func_Key_ID func = m1_vkb_check_function_key(key);
			if (func == M1_VKB_FUNC_ENTER_KEY_ID) {
				if (len != 0U && (len & 1U) == 0U) {
					xQueueReset(main_q_hdl);
					return len;
				}
				warn = 1;
			} else if (func == M1_VKB_FUNC_BS_KEY_ID) {
				if (len > 0U) out_hex[--len] = '\0';
				warn = 0;
			} else {
				if (len < max_nibbles) {
					out_hex[len++] = (char)key;
					out_hex[len] = '\0';
				}
				warn = 0;
			}
		} else if (buttons.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
			col = m1_kb_col_wrap(col, +1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (buttons.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK) {
			col = m1_kb_col_wrap(col, -1, M1_VIRTUAL_KBS_COLUMN_SIZE);
		} else if (buttons.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row < M1_VIRTUAL_KBS_ROW_SIZE - 1) row++;
		} else if (buttons.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (row > 0U) row--;
		}
	}
}
