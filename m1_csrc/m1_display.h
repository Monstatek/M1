/* See COPYING.txt for license details. */

/*
*
* m1_display.h
*
* Functions to display objects on the M1 LCD
*
* M1 Project
*
*/

#ifndef M1_DISPLAY_H_
#define M1_DISPLAY_H_

#include "m1_lcd.h"
#include "m1_menu.h"

#define M1_DISP_DRAW_COLOR_BG		0
#define M1_DISP_DRAW_COLOR_TXT		1

#define M1_DISP_MAIN_MENU_FONT_N		u8g2_font_resoledmedium_tr // 5x10 //u8g2_font_6x10_tf //, , u8g2_font_medsans_tr: clear font
#define M1_DISP_MAIN_MENU_FONT_B		u8g2_font_helvB08_tf
#define M1_DISP_SUB_MENU_FONT_N			u8g2_font_resoledmedium_tr // Nokia removed; unified with the main-menu font
#define M1_DISP_SUB_MENU_FONT_B			u8g2_font_squeezed_b7_tr //, u8g2_font_ncenB08_tf: slightly wide bold,
#define M1_DISP_FUNC_MENU_FONT_N		u8g2_font_spleen5x8_mf // u8g2_font_nine_by_five_nbp_tf // u8g2_font_miranda_nbp_tf // u8g2_font_5x8_tf
#define M1_DISP_FUNC_MENU_FONT_N2		u8g2_font_nine_by_five_nbp_tf // larger and clear font

#define M1_DISP_IR_REMOTES_FONT_N		u8g2_font_squeezed_r6_tr //u8g2_font_tom_thumb_4x6_tf
#define M1_DISP_IR_REMOTES_FONT_1		u8g2_font_tiny5_tf // + 4x5
#define M1_DISP_IR_REMOTES_FONT_2		u8g2_font_tinytim_tf // +, 5x5
#define M1_DISP_IR_REMOTES_FONT_3		u8g2_font_tinyunicode_tf // +
#define M1_DISP_IR_REMOTES_FONT_4		u8g2_font_fivepx_tr // + - round
#define M1_DISP_IR_REMOTES_FONT_5		u8g2_font_wedge_tr // + - bold
#define M1_DISP_IR_REMOTES_FONT_6		u8g2_font_baby_tf // + +
#define M1_DISP_IR_REMOTES_FONT_7		u8g2_font_chikita_tf // +, 5x5
#define M1_DISP_IR_REMOTES_FONT_B1		u8g2_font_squeezed_b7_tr // +, 5x7 bold
#define M1_DISP_IR_REMOTES_FONT_B2		u8g2_font_resoledbold_tr // +, 6x7
#define M1_DISP_IR_REMOTES_FONT_S1		u8g2_font_3x3basic_tr // +, 3x3
#define M1_DISP_IR_REMOTES_FONT_S2		u8g2_font_u8glib_4_tr // height 4

#define M1_DISP_RUN_MENU_FONT_B			u8g2_font_courB08_tf // small bold font, //u8g2_font_resoledbold_tr: small bold font

#define M1_DISP_RUN_ERROR_FONT_1B		u8g2_font_Terminal_tr // bold larger clear
#define M1_DISP_RUN_ERROR_FONT_2B		u8g2_font_lubB08_tf // Bold font, larger size, likely size 10

#define M1_DISP_RUN_WARNING_FONT_1B		u8g2_font_victoriabold8_8r // wide bold
#define M1_DISP_RUN_WARNING_FONT_2B		u8g2_font_pcsenior_8f // wide bold
#define M1_DISP_RUN_WARNING_FONT_3		u8g2_font_samim_10_t_all // likely larger font and clear

#define M1_DISP_RUN_WARNING_FONT_1B_W	7

#define M1_DISP_LARGE_FONT_1B			u8g2_font_profont17_tr
#define M1_DISP_LARGE_FONT_2B			u8g2_font_VCR_OSD_tu // 12x17
#define M1_DISP_LARGE_FONT_3B			u8g2_font_spleen8x16_mf // u8g2_font_fub14_tf//u8g2_font_timB14_tf//u8g2_font_ncenB14_tf

#define M1_MAIN_LOGO_FONT_1B			u8g2_font_Pixellari_tu // 10 pixel

#define M1_POWERUP_LOGO_TOP_POS_Y		15
#define M1_POWERUP_LOGO_LEFT_POS_X		0
#define M1_POWERUP_LOGO_WIDTH			40
#define M1_POWERUP_LOGO_HEIGHT			32

#define M1_POWERUP_LOGO_FONT			u8g2_font_tenthinnerguys_tu //u8g2_font_luBS08_tr //u8g2_font_courB10_tr // u8g2_font_fub17_tr

/*
	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 2, 12, "WMwmijqyekt%.,:");
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 2, 22, "WMwmijqyekt%.,:");
	u8g2_SetFont(&m1_u8g2, u8g2_font_resoledmedium_tr); // u8g2_font_resoledmedium_tr: normal clear, u8g2_font_simple1_tf: normal clear narrower
	u8g2_DrawStr(&m1_u8g2, 2, 32, "WMwmijqyekt%.,:"); // u8g2_font_8bitclassic_tf: large bold clear
	u8g2_SetFont(&m1_u8g2, u8g2_font_balthasar_regular_nbp_tf); // u8g2_font_frigidaire_mr: larger and clear, u8g2_font_finderskeepers_tf: small narrow clear
	u8g2_DrawStr(&m1_u8g2, 2, 42, "WMwmijqyekt%.,:"); // u8g2_font_NokiaSmallPlain_tf: small clear narrower
	u8g2_SetFont(&m1_u8g2, u8g2_font_balthasar_titling_nbp_tf); // u8g2_font_sticker100complete_tr: large bold shaded
	u8g2_DrawStr(&m1_u8g2, 2, 52, "WMwmijqyekt%.,:");
	u8g2_SetFont(&m1_u8g2, u8g2_font_synchronizer_nbp_tf); // u8g2_font_iconquadpix_m_all: icons collection, u8g2_font_fivepx_tr: tiny and clear
	u8g2_DrawStr(&m1_u8g2, 2, 62, "WMwmijqyekt%.,:");
	m1_u8g2_nextpage(); // Update display

 */

#define M1_GUI_FONT_WIDTH				6	// pixel
#define M1_GUI_FONT_HEIGHT				10	// pixel
#define M1_GUI_FONT_HEIGHT_SPACING		2	// pixel

#define M1_SUB_MENU_SFONT_WIDTH			4	// pixel
#define M1_SUB_MENU_FONT_WIDTH			5	// pixel
#define M1_SUB_MENU_FONT_HEIGHT			8	// pixel

#define M1_BACKLIGHT_BRIGHTNESS			255//80//128//255 // Set to maximum to minimize the flickering effect on the display
#define M1_BACKLIGHT_OFF				0

#define INFO_BOX_Y_POS_ROW_1			42
#define INFO_BOX_Y_POS_ROW_2			52
#define INFO_BOX_Y_POS_ROW_3			62

#define SDCARD_ERROR_IMAGE_WIDTH		46
#define SDCARD_ERROR_IMAGE_HEIGHT		36

typedef struct
{
	const uint8_t *pdata;
	uint8_t icon_w;
	uint8_t icon_h;
} S_M1_menu_icon_data;

typedef enum
{
	INFO_BOX_ROW_1 = 0,
	INFO_BOX_ROW_2,
	INFO_BOX_ROW_3,
	INFO_BOX_ROW_EOL
} S_M1_info_box_row;

typedef enum {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} S_M1_text_align_t;

extern const uint8_t menu_m1_icon_bluetooth[];
extern const uint8_t menu_m1_icon_gpio[];
extern const uint8_t menu_m1_icon_infrared[];
extern const uint8_t menu_m1_icon_nfc[];
extern const uint8_t menu_m1_icon_rfid[];
extern const uint8_t menu_m1_icon_wave[];
extern const uint8_t menu_m1_icon_setting[];
extern const uint8_t menu_m1_icon_wifi[];
extern const uint8_t menu_m1_icon_usb[];
extern const uint8_t usb_drive_icon_52x24[];   /* USB Drive screen icon */
extern const uint8_t m1_logo_26x14[];
extern const uint8_t m1_boot_logo_128x64[];   /* approved full-screen power-on boot artwork */
extern const uint8_t menu_scroll_bar_4x64[];
extern const uint8_t m1_frame_75x16[];
extern const uint8_t m1_frame_128x32[];
extern const uint8_t m1_frame_128x22[];
extern const uint8_t m1_frame_64x76[];
extern const uint8_t m1_progress_60x10[];
extern const uint8_t hourglass_18x32[];
extern const uint8_t hourglass_16x29[];
extern const uint8_t arrowleft_10x10[];
extern const uint8_t arrowright_10x10[];
extern const uint8_t arrowright_8x8[];
extern const uint8_t arrowleft_8x8[];
extern const uint8_t arrowdown_8x8[];
extern const uint8_t fb_m1_icon_folder[];
extern const uint8_t fb_m1_icon_file[];
extern const uint8_t ir_button_frame_21x21[];
extern const uint8_t button_power_18x18[];
extern const uint8_t button_mute_18x18[];
extern const uint8_t button_up_18x18[];
extern const uint8_t button_down_18x18[];
extern const uint8_t button_plus_18x18[];
extern const uint8_t button_minus_18x18[];
extern const uint8_t button_previous_18x18[];
extern const uint8_t button_next_18x18[];
extern const uint8_t button_play_18x18[];
extern const uint8_t button_pause_18x18[];
extern const uint8_t ir_button_frame_up_21x21[];
extern const uint8_t ir_button_frame_down_21x21[];
extern const uint8_t ir_button_frame_up_i_21x21[];
extern const uint8_t ir_button_frame_down_i_21x21[];
extern const uint8_t button_power_off_18x18[];
extern const uint8_t weather_dry_18x18[];
extern const uint8_t weather_cold_20x20[];
extern const uint8_t weather_hot_20x20[];
extern const uint8_t return_10x10[];
extern const uint8_t remote_48x25[];
extern const uint8_t fw_update_48x48[];
extern const uint8_t target_10x10[];
extern const uint8_t target_i_10x10[];
extern const uint8_t sd_card_error_46x36[];
extern const uint8_t micro_sd_card_error_format[];
extern const uint8_t m1_saved_screen_128x64[];   /* universal "Saved" confirmation screen, 128x64 1-bit */
extern const uint8_t nfc_emit_48x48[];
extern const uint8_t subghz_transmission_64_30[];
extern const uint8_t fw_update_slide_strip_126x14[];
extern const uint8_t fw_update_slider_5x8[];
extern const uint8_t m1_device_82x36[];
extern const uint8_t battery_meter_46_36[];
extern const uint8_t xtal_10x10[];
extern const uint8_t error_10x10[];
extern const uint8_t check_10x10[];

extern const S_M1_menu_icon_data menu_fb_icon_prev;
extern const S_M1_menu_icon_data menu_fb_icon_dir;
extern const S_M1_menu_icon_data menu_fb_icon_text;
extern const S_M1_menu_icon_data menu_fb_icon_data;
extern const S_M1_menu_icon_data menu_fb_icon_other;

extern const char sdcard_access_error_message[];
extern const char sdcard_db_error_message[];

void m1_gui_init(void);
void m1_gui_welcome_scr(void);
void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction);
uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction);
void m1_gui_force_sub_menu_level(void);
void m1_gui_scr_animation(void);
void m1_gui_let_update_fw(void);
void m1_info_box_display_init(bool high_box);
void m1_info_box_display_clear(void);
void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext);
uint8_t m1_message_box(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons);
void m1_draw_file_saved_screen(void);
void m1_wait_back_to_exit(void);
void m1_draw_bottom_bar(u8g2_t *u8g2, const uint8_t *lbitmap, const char *ltext, const char *rtext, const uint8_t *rbitmap);
void m1_draw_text(u8g2_t *u8g2,
                 int x, int y,
                 int max_width,
                 const char *text,
                 S_M1_text_align_t align);
void m1_draw_text_box(u8g2_t *u8g2,
                    int x, int y,
                    int max_width,          // 텍스트 박스 폭(px)
                    int line_height,        // 줄 간격(px)
                    const char *text,
                    S_M1_text_align_t align);
void m1_image_message(const uint8_t *pimage, uint8_t image_w, uint8_t image_h, const char *message);

/* Shared NFC/RFID Write screen graphic: centered card icon flanked by
 * inward-pointing RF arrows, with a centered status line ("WRITING..."
 * or "SUCCESS").  Rendering only.  Defined in m1_display.c. */
void m1_draw_write_screen(const char *status);

#endif /* M1_DISPLAY_H_ */
