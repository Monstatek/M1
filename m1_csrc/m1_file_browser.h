/* See COPYING.txt for license details. */

/*
*
* m1_file_browser.h
*
* Library for sd card file browsing
*
 * M1 Project
*
*/

#ifndef M1_FILE_BROWSER_H_
#define M1_FILE_BROWSER_H_

#include <stddef.h>

#include "u8g2.h"
#include "mui.h"
#include "ff.h"
#include "ff_gen_drv.h"

typedef enum
{
	F_EXT_DATA = 0,
	F_EXT_OTHER
} S_M1_file_browser_ext;

typedef enum
{
	FB_OK = 0,
	FB_ERR_SDCARD,
	FB_ERR_GUI
} S_M1_file_browser_code;

typedef struct
{
	char *dir_name;
	char *file_name;
	bool file_is_selected;
	S_M1_file_browser_code status;
} S_M1_file_info;

/* Opaque session: layout, allocations and cursor state belong to the browser. */
typedef struct S_M1_file_browser_hdl S_M1_file_browser_hdl;

S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl);
/* Set a one-shot start directory for the NEXT m1_fb_init only (then it reverts
 * to the SD-root default). Pass NULL/"" to clear. Callers that never call this
 * are unaffected. */
void m1_fb_set_start_dir(const char *dir);
/* Enable/disable alphabetical sorting for the generic SD-card explorer only.
 * Session-scoped: set true before browsing, auto-cleared on m1_fb_deinit. */
void m1_fb_set_sort_enabled(bool enable);
/* Enable/disable display-only friendly names for a small set of known
 * top-level folders (generic SD-card explorer only). Session-scoped: set
 * true before browsing, auto-cleared on m1_fb_deinit. Never affects the
 * real FatFs name used for traversal, selection, rename, delete, or
 * dispatch -- only the text drawn on screen. */
void m1_fb_set_friendly_names(bool enable);
/* Explicit generic-Explorer navigation mode. Session-scoped like
 * m1_fb_set_sort_enabled/m1_fb_set_friendly_names above: set true before
 * browsing, auto-cleared on m1_fb_deinit. Only the generic Home-LEFT SD
 * Explorer (storage_explore) sets this; every other caller of
 * storage_browse() -- the NFC/RFID/Sub-GHz Saved browsers and the
 * STM32/ESP32 firmware-image pickers -- never sets it and keeps its
 * existing BACK-always-exits behavior unchanged. */
void m1_fb_set_explorer_nav_enabled(bool enable);
bool m1_fb_explorer_nav_active(void);

typedef enum
{
	M1_FB_BACK_AT_ROOT = 0,   /* already at the canonical SD root (0:/); caller should exit */
	M1_FB_BACK_PARENT_OPENED, /* ascended one level; caller should redraw via m1_fb_display(NULL) */
	M1_FB_BACK_ERROR          /* no active browser session; caller should treat as exit */
} m1_fb_back_result_t;

/* Ascend exactly one directory level toward the canonical SD root, for the
 * generic Explorer's own BACK button (the ".." row keeps its own existing,
 * untouched selection-driven ascend path). Whether the session is already
 * at the root is determined from the canonical current path itself, not
 * from dir_level alone, so a session reopened at a non-root start
 * directory (m1_fb_set_start_dir) still ascends correctly instead of
 * treating that directory as a false root. Never traverses above 0:/. */
m1_fb_back_result_t m1_fb_navigate_back(void);
/* Purely-additive performance counters (surfaced via "m1_mtest 5"), same
 * pattern as m1_isr_drop_counters.h: measure directory (re)scans and saved-
 * file opens across a browsing session so a hardware tester can compare a
 * before/after without a wall-clock trace. Snapshot-only, safe at any time. */
uint32_t m1_fb_get_perf_scan_count(void);
uint32_t m1_fb_get_perf_file_open_count(void);
void m1_fb_reset_perf_counters(void);
void m1_fb_deinit(void);
void m1_fb_popup(void);
/* Borrowed read-only view (including strings), valid only until the next
 * browser call. Use copy_selection before retaining it or closing the browser. */
const S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status);
/* Copies the current selection into caller-owned buffers. On failure neither
 * buffer is changed; no allocation, truncation or browser-state mutation.
 * Buffers must be distinct and must not alias browser-owned strings. */
bool m1_fb_copy_selection(char *directory, size_t directory_size,
                          char *filename, size_t filename_size);
FRESULT m1_fb_listing(const char *dir_name);
uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...);
FRESULT m1_fb_open_new_file(FIL *file, const char *filename);
FRESULT m1_fb_open_file(FIL *file, const char *filename);
FRESULT m1_fb_open_dir(DIR *dir, const char *directory);
uint8_t m1_fb_make_dir(const char *directory);
uint8_t m1_fb_check_existence(const char *filedir);
uint8_t m1_fb_close_file(FIL *file);
uint8_t m1_fb_open_log_file(const char *filename);
uint8_t m1_fb_close_log_file(void);
uint8_t m1_fb_delete_file(const char *filename);
uint8_t m1_fb_rename_file(char *full_pathname_in, char *new_filename_out, uint8_t new_name);
/* Result-bearing I/O: EOF is FR_OK with zero bytes; partial counts remain
 * available on error. Legacy count-only adapters below retain old semantics. */
FRESULT m1_fb_read_file(FIL *file, void *buffer, UINT size, UINT *bytes_read);
FRESULT m1_fb_write_file(FIL *file, const void *buffer, UINT size, UINT *bytes_written);
uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size);
uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size);

#endif /* M1_FILE_BROWSER_H_ */
