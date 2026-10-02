/* See COPYING.txt for license details. */

/*
*
 *m1_file_browser.c
*
 *Library for sd card file browsing
*
 * M1 Project
*
*/

/************************** *I N C L U D E S **********************************/

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_sdcard.h"
#include "res_string.h"
#include "m1_virtual_kb.h"

/************************** *D E F I N E S ************************************/

#define DISABLE_IRQ			__disable_irq(); __DSB(); __ISB();
#define ENABLE_IRQ			__enable_irq();

#define FILE_BROWSER_MAX_FILES			96
#define DIRECTORY_MAX_DEPTH_LEVEL		32

#define GUI_SCROLLBAR_WIDTH				4 // pixel

#define FILENAME_LEN_ON_CLI_MAX			80 // Max filename length to display on console

#define M1_LOGDB_TAG	"SD-BROWSER"

//************************* *C O N S T A N T **********************************/

const char m1_fb_data_types[]    = ".log.LOG.text.TEXT.txt.TXT";

//************************* *S T R U C T U R E S *******************************

/**************************** *V A R I A B L E S ******************************/

struct S_M1_file_browser_hdl
{
	S_M1_file_info info;
	uint16_t x, y, gui_width, gui_height;
	uint8_t font_w, font_h;
	uint8_t font_h_spacing;
	uint16_t *listing_index_buffer, *row_index_buffer;
	uint16_t listing_index, row_index;
	uint8_t dir_level;
};
static S_M1_file_browser_hdl *pfb_hdl = NULL;
static u8g2_t *plcd_hdl;
/* Shared directory-enumeration handle: file scope (not m1_fb_display()-local)
 * so m1_fb_navigate_back() can close it before truncating dir_name, exactly
 * as every other path-changing branch inside m1_fb_display() already does. */
static DIR directory;

static bool fb_gui_check;
/* Optional one-shot start directory for the NEXT m1_fb_init only. Default (unset) keeps the SD-root default, so RFID / Sub-GHz / firmware-update browsing are unaffected. NFC uses it to reopen the Saved Files list inside the /NFC folder after a delete. */
static char s_fb_start_dir[128];
static bool s_fb_start_dir_set = false;

bool m1_fb_copy_selection(char *directory, size_t directory_size,
                          char *filename, size_t filename_size)
{
    if (!pfb_hdl || !directory || !filename) return false;
    const S_M1_file_info *info = &pfb_hdl->info;
    if (info->status != FB_OK || !info->file_is_selected ||
        !info->dir_name || !info->file_name) return false;
    size_t dir_length = strlen(info->dir_name);
    size_t file_length = strlen(info->file_name);
    if (dir_length >= directory_size || file_length >= filename_size) return false;
    memcpy(directory, info->dir_name, dir_length + 1);
    memcpy(filename, info->file_name, file_length + 1);
    return true;
}

void m1_fb_set_start_dir(const char *dir)
{
	if (dir && dir[0]) {
		strncpy(s_fb_start_dir, dir, sizeof(s_fb_start_dir) - 1);
		s_fb_start_dir[sizeof(s_fb_start_dir) - 1] = '\0';
		s_fb_start_dir_set = true;
	} else {
		s_fb_start_dir_set = false;
	}
}
FIL m1_log_file;

/******************** *F U N C T I O N   P R O T O T Y P E S ******************/

extern void m1_u8g2_firstpage(void);
extern uint8_t m1_u8g2_nextpage(void);
const S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status);
S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl);
void m1_fb_deinit(void);
uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...);
FRESULT m1_fb_open_new_file(FIL *file, const char *filename);
FRESULT m1_fb_open_file(FIL *file, const char *filename);
uint8_t m1_fb_close_file(FIL *file);
uint8_t m1_fb_open_log_file(const char *filename);
uint8_t m1_fb_close_log_file(void);
uint8_t m1_fb_delete_file(const char *filename);
uint8_t m1_fb_rename_file(char *full_pathname_in, char *new_filename_out, uint8_t new_name);
uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size);
uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size);
uint8_t m1_fb_check_low_freespace(void);

/************** *F U N C T I O N   I M P L E M E N T A T I O N ****************/



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/

/* --- Directory-entry cache, with optional alphabetical sort ---------------
 * m1_fb_display reads the directory once (on entry/descend/ascend, not on
 * every redraw) into a heap array of paired {name, attribs} records and
 * drives BOTH the display and the selection from that array for every
 * caller -- so the highlighted row and the Open/Rename/Delete target are
 * always the same record, and no caller re-walks the FatFs directory stream
 * on a plain UP/DOWN redraw. Sorting on top of that cache (dir-before-file,
 * case-insensitive, then case-sensitive tiebreak) is enabled only for the
 * generic SD-card explorer and the two firmware-image pickers (storage_explore
 * / firmware_update_get_image_file / setting_esp32_image_file, all via
 * m1_fb_set_sort_enabled); NFC-saved, LF-RFID-saved, Sub-GHz Replay, and
 * BLE-saved keep the raw FatFs order. A sort-enabled browser also suppresses
 * the inert ".." row at the SD-card root; every other caller/depth keeps it.
 * Cache freed/cleared in m1_fb_deinit. ".." is never stored (drawn
 * separately; it always sorts first when sorting is active). */
typedef struct {
	TCHAR name[FF_MAX_LFN + 1];   /* full LFN name, paired with its attribs */
	BYTE  fattrib;
} fb_sorted_entry_t;

static bool               s_fb_sort_enabled = false;
static fb_sorted_entry_t *s_fb_sorted       = NULL;   /* heap, sized to the actual entry count */
static uint16_t           s_fb_sorted_count = 0;

/* Purely-additive performance counters (same pattern as
 * m1_isr_drop_counters.h/.c, surfaced via "m1_mtest 5"): let a hardware
 * tester measure directory (re)scans and file opens across a browsing
 * session, before/after a change, without needing a wall-clock trace. Never
 * read in any decision path -- snapshot-only, safe to read at any time. */
static uint32_t s_fb_perf_scan_count = 0;
static uint32_t s_fb_perf_file_open_count = 0;

uint32_t m1_fb_get_perf_scan_count(void) { return s_fb_perf_scan_count; }
uint32_t m1_fb_get_perf_file_open_count(void) { return s_fb_perf_file_open_count; }
void m1_fb_reset_perf_counters(void) { s_fb_perf_scan_count = 0; s_fb_perf_file_open_count = 0; }

void m1_fb_set_sort_enabled(bool enable)
{
	s_fb_sort_enabled = enable;
}

/* Display-only friendly names for a small set of known top-level SD folders,
 * generic SD-card explorer only. Raw FatFs name -> on-screen text; the real
 * name (used for traversal/selection/rename/delete/dispatch) is untouched.
 * Any name not in this list displays exactly as FatFs reports it. */
static bool s_fb_friendly_names = false;

void m1_fb_set_friendly_names(bool enable)
{
	s_fb_friendly_names = enable;
}

static bool s_fb_explorer_nav_enabled = false;

void m1_fb_set_explorer_nav_enabled(bool enable)
{
	s_fb_explorer_nav_enabled = enable;
}

bool m1_fb_explorer_nav_active(void)
{
	return s_fb_explorer_nav_enabled;
}

/* True iff path names the SD root.  New navigation always stores the
 * canonical SDCARD_DEFAULT_DRIVE_PATH form; the bare drive form remains
 * accepted defensively for state created by older code. Used instead of
 * dir_level == 0 so a session reopened at a non-root start directory is
 * never mistaken for the root. */
static bool fb_is_root_path(const char *path)
{
	if (!path)
		return true; /* defensive: never ascend past an unknown path */
	if (!strcmp(path, "0:"))
		return true;
	if (!strcmp(path, SDCARD_DEFAULT_DRIVE_PATH))
		return true;
	return false;
}

/* Depth of `path` below the canonical SD root (0 at the root itself), used
 * only to correctly initialize dir_level when a generic-Explorer session
 * starts at a non-root directory (m1_fb_set_start_dir). Every other caller
 * leaves dir_level at its default 0, exactly as before. */
static uint8_t fb_dir_level_from_path(const char *path)
{
	const char *root = SDCARD_DEFAULT_DRIVE_PATH;
	size_t root_len = strlen(root);
	const char *p;
	uint8_t depth;

	if (!path)
		return 0;

	if (!strncmp(path, root, root_len))
		p = path + root_len;
	else if (!strncmp(path, "0:", 2))
	{
		p = path + 2;
		if (*p == '/')
			p++;
	}
	else
		p = path; /* defensive: not a recognized SD path */

	if (*p == '\0')
		return 0; /* exactly the root */

	depth = 1;
	for (; *p; p++)
	{
		if (*p == '/')
			depth++;
	}
	return depth;
}

static int fb_name_casecmp(const char *a, const char *b);

static const char *fb_friendly_dir_name(const char *raw_name)
{
	static const char * const managed_root_names[] = {
		"nfc", "rfid", "subghz", "wifi", "ble",
		"infrared", "data", "monsta", "apps"
	};

	/* Canonical display labels are lowercase even when an existing card keeps
	 * an older uppercase or mixed-case FAT name. This changes presentation
	 * only: traversal and file operations continue using the raw name. */
	for (size_t i = 0; i < sizeof(managed_root_names) / sizeof(managed_root_names[0]); i++)
	{
		if (!fb_name_casecmp(raw_name, managed_root_names[i]))
			return managed_root_names[i];
	}
	return raw_name;
}

static void fb_sorted_free(void)
{
	if (s_fb_sorted)
	{
		free(s_fb_sorted);
		s_fb_sorted = NULL;
	}
	s_fb_sorted_count = 0;
}

/* Case-insensitive ASCII name compare (no suitable exported project helper:
 * the only case-insensitive strcmp is static in privateprofilestring.c). */
static int fb_name_casecmp(const char *a, const char *b)
{
	for (;;)
	{
		unsigned char ca = (unsigned char)*a;
		unsigned char cb = (unsigned char)*b;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
		if (ca != cb) return (int)ca - (int)cb;
		if (ca == 0)  return 0;
		a++; b++;
	}
}

/* Sort order: directories before files; case-insensitive alphabetical; then a
 * deterministic case-sensitive tiebreak. (".." is handled outside the array.) */
static int fb_entry_cmp(const void *pa, const void *pb)
{
	const fb_sorted_entry_t *a = (const fb_sorted_entry_t *)pa;
	const fb_sorted_entry_t *b = (const fb_sorted_entry_t *)pb;
	bool ad = (a->fattrib & AM_DIR) != 0;
	bool bd = (b->fattrib & AM_DIR) != 0;
	int  r;
	if (ad != bd)
		return ad ? -1 : 1;
	r = fb_name_casecmp(a->name, b->name);
	if (r == 0)
		r = strcmp(a->name, b->name);
	return r;
}

S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)
{
	/* assert()/assert_param() compile out under NDEBUG (ARM release builds),
	 * so they were never a real safety net here -- every allocation below is
	 * checked with a real runtime branch. On any failure, m1_fb_deinit() (its
	 * existing cleanup, reused rather than duplicated) frees whatever
	 * succeeded so far and this returns NULL instead of leaving pfb_hdl
	 * half-initialized for some other m1_fb_* call to crash on later. */
	pfb_hdl = (S_M1_file_browser_hdl*)calloc(1, sizeof(S_M1_file_browser_hdl));
	if (pfb_hdl == NULL)
		return NULL; /* nothing else allocated yet */

	pfb_hdl->listing_index_buffer = (uint16_t *)calloc(1, sizeof(uint16_t));
	pfb_hdl->row_index_buffer = (uint16_t *)calloc(1, sizeof(uint16_t));
	if ((pfb_hdl->listing_index_buffer == NULL) || (pfb_hdl->row_index_buffer == NULL))
	{
		m1_fb_deinit();
		return NULL;
	}
	*pfb_hdl->listing_index_buffer = 0;
	*pfb_hdl->row_index_buffer = 0;
	{
		const char *start = s_fb_start_dir_set ? s_fb_start_dir : SDCARD_DEFAULT_DRIVE_PATH;
		pfb_hdl->info.dir_name = malloc(strlen(start) + 1);
		if (pfb_hdl->info.dir_name == NULL)
		{
			m1_fb_deinit();
			return NULL;
		}
		strcpy(pfb_hdl->info.dir_name, start);
		s_fb_start_dir_set = false;   /* one-shot: next init returns to SD-root default */

		if (s_fb_explorer_nav_enabled)
		{
			/* Correctly initialize navigation state for a generic-Explorer
			 * session that starts at a non-root directory (the reopen-after-
			 * native-dispatch flow in browse_and_dispatch()), so it is never
			 * mistaken for a false root: size dir_level and its per-level
			 * buffers to the START path's real depth, exactly matching the
			 * invariant a normal step-by-step descend from root would have
			 * built (dir_level N -> buffers hold N+1 entries). Every other
			 * caller of m1_fb_set_start_dir (NFC/RFID/Sub-GHz Saved browsers)
			 * never sets s_fb_explorer_nav_enabled, so dir_level stays 0 for
			 * them exactly as before -- unchanged. */
			uint8_t start_depth = fb_dir_level_from_path(start);
			if (start_depth > 0)
			{
				uint16_t *new_listing_buf = (uint16_t *)calloc((size_t)start_depth + 1U, sizeof(uint16_t));
				uint16_t *new_row_buf = (uint16_t *)calloc((size_t)start_depth + 1U, sizeof(uint16_t));
				if ((new_listing_buf == NULL) || (new_row_buf == NULL))
				{
					free(new_listing_buf);
					free(new_row_buf);
					m1_fb_deinit();
					return NULL;
				}
				free(pfb_hdl->listing_index_buffer);
				free(pfb_hdl->row_index_buffer);
				pfb_hdl->listing_index_buffer = new_listing_buf;
				pfb_hdl->row_index_buffer = new_row_buf;
				pfb_hdl->dir_level = start_depth;
			}
		}
	}
	pfb_hdl->info.file_name = NULL;
	pfb_hdl->font_w = M1_GUI_FONT_WIDTH;
	pfb_hdl->font_h = M1_GUI_FONT_HEIGHT;
	pfb_hdl->font_h_spacing = M1_GUI_FONT_HEIGHT_SPACING;
	pfb_hdl->info.file_is_selected = FALSE;

	assert(lcd_hdl!=NULL);
	plcd_hdl = lcd_hdl;

	pfb_hdl->x = 0;
	pfb_hdl->y = 0;
	pfb_hdl->gui_width = plcd_hdl->width;
	pfb_hdl->gui_height = plcd_hdl->height;

	fb_gui_check = FALSE;

	m1_u8g2_firstpage(); // Reset display RAM

	return pfb_hdl;
} // S_M1_file_browser_hdl *m1_fb_init(u8g2_t *lcd_hdl)



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
static uint8_t m1_fb_find_ext(char *file_name, const char *file_ext)
{
	int k, f;

	f = 0;
	k = strlen(file_name);
	while (k)
	{
		if (file_name[k - 1]=='.')
		{
			f = 1;
	   		break;
	   	}
	   	k--;
	}
	if (!f || k==1)
		return 0;
	if (strstr(file_ext, &file_name[k - 1]))
		return 1;

	return 0;
} // static uint8_t m1_fb_find_ext(char *file_name, const char *file_ext)




/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
static S_M1_file_browser_ext m1_fb_get_file_type(char *filename)
{
	if (m1_fb_find_ext(filename, m1_fb_data_types))
		return F_EXT_DATA;

	return F_EXT_OTHER;
} // static S_M1_file_browser_ext m1_fb_get_file_type(char *filename)






/******************************************************************************/
/**
  * @brief  Create new file
  * @param  filename: name of the new file
  * 		file: pointer to the FIL object
  * @retval FR_OK on success, otherwise the FatFs/argument error
  */
/******************************************************************************/
FRESULT m1_fb_open_new_file(FIL *file, const char *filename)
{
	if (!file) return FR_INVALID_OBJECT;
	if (!filename) return FR_INVALID_PARAMETER;
	return f_open(file, filename, FA_CREATE_ALWAYS | FA_WRITE);
} // FRESULT m1_fb_open_new_file(FIL *file, const char *filename)



/******************************************************************************/
/**
  * @brief  read from a file
  * @param  file: pointer to the FIL object
  * 		buffer: pointer to the data buffer
  * 		size: number of bytes to read
  * @retval number of read bytes
  */
/******************************************************************************/
FRESULT m1_fb_read_file(FIL *file, void *buffer, UINT size, UINT *bytes_read)
{
    if (!bytes_read) return FR_INVALID_PARAMETER;
    *bytes_read = 0;
    if (!file) return FR_INVALID_OBJECT;
    if (!buffer && size) return FR_INVALID_PARAMETER;
    return f_read(file, buffer, size, bytes_read);
}

uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size)
{
    UINT count;
    return m1_fb_read_file(pfile, buffer, size, &count) == FR_OK ? count : 0;
} // uint16_t m1_fb_read_from_file(FIL *pfile, char *buffer, uint16_t size)




/******************************************************************************/
/**
  * @brief  write to a file
  * @param  file: pointer to the FIL object
  * 		buffer: pointer to the data buffer
  * 		size: number of bytes to write
  * @retval number of written bytes
  */
/******************************************************************************/
FRESULT m1_fb_write_file(FIL *file, const void *buffer, UINT size, UINT *bytes_written)
{
    if (!bytes_written) return FR_INVALID_PARAMETER;
    *bytes_written = 0;
    if (!file) return FR_INVALID_OBJECT;
    if (!buffer && size) return FR_INVALID_PARAMETER;
    return f_write(file, buffer, size, bytes_written);
}

uint16_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size)
{
    UINT count;
    return m1_fb_write_file(pfile, buffer, size, &count) == FR_OK ? count : 0;
} // uint8_t m1_fb_write_to_file(FIL *pfile, const char *buffer, uint16_t size)




/******************************************************************************/
/**
  * @brief  Open an existing file
  * @param  filename: name of the new file
  * 		file: pointer to the FIL object*
  * @retval FR_OK on success, otherwise the FatFs/argument error
  */
/******************************************************************************/
FRESULT m1_fb_open_file(FIL *file, const char *filename)
{
	if (!file) return FR_INVALID_OBJECT;
	if (!filename) return FR_INVALID_PARAMETER;

	s_fb_perf_file_open_count++; // every caller (NFC/RFID/Sub-GHz/BLE/Explore) opens saved files through here

	return f_open(file, filename, FA_OPEN_EXISTING | FA_READ);
} // FRESULT m1_fb_open_file(FIL *file, const char *filename)



/******************************************************************************/
/**
  * @brief  Open a directory
  * @param  directory: name of the new directory
  * 		dir: pointer to the DIR object
  * @retval FR_OK on success, otherwise the FatFs/argument error
  */
/******************************************************************************/
FRESULT m1_fb_open_dir(DIR *dir, const char *directory)
{
	if (!dir) return FR_INVALID_OBJECT;
	if (!directory) return FR_INVALID_PARAMETER;
	return f_opendir(dir, directory);
} // FRESULT m1_fb_open_dir(DIR *dir, const char *directory)


/******************************************************************************/
/**
  * @brief  Make a sub-directory
  * @param  directory: name of the new directory
  *
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_make_dir(const char *directory)
{
	if ( directory==NULL )
		return 1;

	if (f_mkdir(directory) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_make_dir(const char *directory)



/******************************************************************************/
/**
  * @brief  Check the existence of a file or director
  * @param  filedir: name of the directory or file for checking
  * @retval 1 for existence, else 0
  */
/******************************************************************************/
uint8_t m1_fb_check_existence(const char *filedir)
{
	FRESULT fr;

	if ( filedir==NULL )
		return 0;

	fr = f_stat(filedir, NULL);
	if ( fr==FR_OK )
		return 1;

	return 0;
} // uint8_t m1_fb_check_existence(const char *filedir)



/******************************************************************************/
/**
  * @brief  Close a file
  * @param  file: pointer of the FIL object
  * @retval 1 for f_close error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_close_file(FIL *file)
{
	if ( file==NULL )
		return 0;

	return f_close(file);
} // uint8_t m1_fb_close_file(FIL *file)




/******************************************************************************/
/**
  * @brief  Open log file
  * @param  filename: name of the log file
  * @retval 1 for f_write error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_open_log_file(const char *filename)
{
	uint32_t byteswritten;

	if ( m1_fb_open_new_file(&m1_log_file, filename) )
	{
		return 1;
	}
	if (f_write(&m1_log_file, "MonstaTek M1 log file.\r\n", 24, (void *)&byteswritten) != FR_OK)
	{
		return 1;
	}

	return 0;
} // uint8_t m1_fb_open_log_file(const char *filename)



/******************************************************************************/
/**
  * @brief  Close log file
  * @param  none
  * @retval 1 for f_close error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_close_log_file(void)
{
	return m1_fb_close_file(&m1_log_file);
} // uint8_t m1_fb_close_log_file(void)



/******************************************************************************/
/**
  * @brief  Delete a file or folder
  * @param  filename: name of the file or folder
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_delete_file(const char *filename)
{
	FRESULT  ret;
	if ( filename==NULL )
		return 1;

	ret = f_unlink(filename);

	return ret;
} // uint8_t m1_fb_delete_file(const char *filename)



/******************************************************************************/
/**
  * @brief  Rename an existing file on SD card
  * 		The full pathname length is limited to 64 to work properly!
  * @param  full_pathname_in full filename to be renamed
  * 		new_filename_out new filename
  * 		new_name if true, the default filename is given in new_filename_out
  * @retval 1 for error, else 0
  */
/******************************************************************************/
uint8_t m1_fb_rename_file(char *full_pathname_in, char *new_filename_out, uint8_t new_name)
{
	uint8_t ret_code, path_len;
	char *filename, *next_str, new_pathname[64];

	filename = full_pathname_in;
	next_str = NULL;
	ret_code = 1;
	while ( true ) // Search for the filename part only
	{
		next_str = strstr(filename, "/"); // Search for the next / sign
		if ( next_str!=NULL )
		{
			next_str += 1; // Move to next character after the /
			filename = next_str;
		}
		else
			break;
	} // while ( true )
	if ( !filename )
		return ret_code;
	path_len = (uint32_t)filename - (uint32_t)full_pathname_in;
	if ( new_name )
		filename = new_filename_out;

	while (true)
	{
		ret_code = m1_vkb_get_filename((char*)res_string(IDS_ENTER_FILENAME), filename, new_filename_out, 0 /* existing name: normal delete */);
		if ( !ret_code )
		{
			ret_code = 1;
			break;
		}
		//ret_code = strcmp(filename, new_filename_out);
		//if ( !ret_code ) // Same new filename?
		//	break;
		strncpy(new_pathname, full_pathname_in, path_len);
		new_pathname[path_len] = '\0';
		strcat(new_pathname, new_filename_out);
		if( m1_fb_check_existence(new_pathname) )
		{
			filename = new_filename_out; // Update new default filename
			m1_message_box(&m1_u8g2, res_string(IDS_DUPLICATE_FILE),NULL," ", res_string(IDS_BACK));
		}
		else
		{
			ret_code = f_rename(full_pathname_in, new_pathname);
			break;
		}
	} // while (true)

	return ret_code;
} // uint8_t m1_fb_rename_file(char *full_pathname_in, char *new_filename_out, uint8_t new_name)



/******************************************************************************/
/**
  * @brief  Check for low free space on an SD card
  * @param  None
  * @retval 1 for low free space, else 0
  */
/******************************************************************************/
uint8_t m1_fb_check_low_freespace(void)
{
	S_M1_SDCard_Access_Status stat;
	uint32_t free_cap, total_cap;

	if ( m1_sd_detected() )
	{
		stat = m1_sdcard_get_status();
		if ( stat==SD_access_NotReady )
		{
			M1_LOG_I(M1_LOGDB_TAG, "SD_access_NotReady.\r\n");
			m1_sdcard_init_ex();
		} // if ( stat==SD_access_NotReady )
		else
		{
			M1_LOG_I(M1_LOGDB_TAG, "SD_access_NotOK.\r\n");
			m1_sdcard_unmount();
			m1_sdcard_mount();
		} // else

		stat = m1_sdcard_get_status(); // Get latest status
		m1_sdcard_get_info();
	    if ( (stat != SD_access_OK) || (m1_sdcard_get_error_code() != FR_OK) )
	    {
	    	m1_sdcard_unmount();
		m1_sdcard_invalidate();
	    	return 1;
	    } // if ( (stat != SD_access_OK) || (m1_sdcard_get_error_code() != FR_OK) )

	    total_cap = m1_sdcard_get_total_capacity();
	    free_cap = m1_sdcard_get_free_capacity();
	    if ( free_cap < (total_cap/100)*10 ) // 10%
	    {
	    	return 1;
	    }
	    else
	    {
	    	return 0;
	    }
	} // if ( m1_sd_detected() )

	return 1;
} // uint8_t m1_fb_check_low_freespace(void)




/******************************************************************************/
/*
*	This function displays files and folders on the LCD
*
*/
/******************************************************************************/
const S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status)
{
	char name[FF_MAX_LFN + 1];
	FRESULT res;
	FILINFO file_info;
	/* Static: the highlighted entry is captured by the render walk and read by
	 * the OK/descend handler on the NEXT call, so it must persist across calls
	 * (the original relied on stack-slot persistence). Zero-init => no warning. */
	static FILINFO this_file = {0};
	const S_M1_menu_icon_data *fb_icon;
	S_M1_file_browser_ext f_ext;
	static uint16_t num_of_files;
	static uint16_t gui_max_column, gui_max_row;
	static uint16_t gui_width, gui_height;
	static uint16_t scroll_h;
	uint16_t scroll_y, count, len;
	uint16_t l, k;
	uint8_t y_offset, disp_max_column, ext_len;
	static uint8_t spacing;
	static bool scroll_ok;
	bool flag;

	if (!fb_gui_check)
	{
		gui_max_column = pfb_hdl->gui_width / pfb_hdl->font_w;
		gui_max_row = pfb_hdl->gui_height / pfb_hdl->font_h;
		spacing = pfb_hdl->font_h_spacing;

		if (pfb_hdl->x < 0 || pfb_hdl->y < 0 ||
			pfb_hdl->x + pfb_hdl->gui_width > M1_LCD_DISPLAY_WIDTH ||
			pfb_hdl->y + pfb_hdl->gui_height > M1_LCD_DISPLAY_HEIGHT||
			gui_max_column < 3 ||
			gui_max_row < 1  )
		{
			pfb_hdl->info.status = FB_ERR_GUI;
			return &pfb_hdl->info;
		}

		gui_width = gui_max_column*pfb_hdl->font_w;
		gui_height = gui_max_row*pfb_hdl->font_h;
		gui_max_row -= 1; // Need room for row spacing
		fb_gui_check = TRUE;
	} // if (!fb_gui_check)

	while (1) // Not an endless loop
	{
		if (button_status==NULL)
		{
			/* A new session/directory enumeration owns a new selection
			 * generation.  Clear the render-to-OK handoff before touching the
			 * directory so neither an empty listing nor an error can reuse the
			 * prior session's FILINFO.  The OK path also checks num_of_files as
			 * defense in depth. */
			memset(&this_file, 0, sizeof(this_file));
			res = f_opendir(&directory, pfb_hdl->info.dir_name);

			if (res != FR_OK)
			{
				fb_sorted_free(); // Card/media gone: don't keep a stale listing for it
				pfb_hdl->info.status = FB_ERR_SDCARD;
				return &pfb_hdl->info;
			}
			if (!pfb_hdl->info.file_is_selected)
			{
				pfb_hdl->listing_index = pfb_hdl->listing_index_buffer[pfb_hdl->dir_level];
				pfb_hdl->row_index = pfb_hdl->row_index_buffer[pfb_hdl->dir_level];
			}

		    /* Re-enumerate; drop any stale cached array first. The listing is
		     * cached for EVERY caller (not just the sort-enabled one) so the
		     * render loop below never re-walks the FatFs directory stream
		     * from scratch on every redraw -- only the qsort() step itself
		     * stays conditional on s_fb_sort_enabled.
		     *
		     * Sized to the ACTUAL entry count, not a fixed worst-case
		     * allocation: pass 1 counts real entries (bounded by the same
		     * FILE_BROWSER_MAX_FILES-1 cap as before) and distinguishes a
		     * genuine enumeration error (f_readdir returning non-FR_OK
		     * mid-walk) from the clean FR_OK-with-empty-name end-of-directory
		     * marker; pass 2 (after a rewind) populates an array sized to
		     * exactly that count. A folder with 3 files no longer pays for
		     * FILE_BROWSER_MAX_FILES slots every session. */
		    fb_sorted_free();
		    s_fb_perf_scan_count++;
		    {
		        uint16_t entry_count = 0;
		        bool enum_error = false;

		        while (entry_count < (FILE_BROWSER_MAX_FILES - 1))
		        {
		            res = f_readdir(&directory, &file_info);
		            if (res != FR_OK) { enum_error = true; break; }
		            if (!file_info.fname[0]) break; // clean end of directory
		            if (file_info.fattrib & (AM_HID | AM_SYS)) continue;
		            entry_count++;
		        }

		        if (enum_error)
		        {
		            /* Enumeration failed partway (e.g. media pulled mid-read):
		             * don't present a partial listing as complete, or as a
		             * legitimately empty folder -- surface the same SD-error
		             * path a failed f_opendir would. */
		            f_closedir(&directory);
		            pfb_hdl->info.status = FB_ERR_SDCARD;
		            return &pfb_hdl->info;
		        }

		        f_readdir(&directory, 0); // rewind for pass 2

		        if (entry_count > 0)
		            s_fb_sorted = (fb_sorted_entry_t *)calloc(entry_count, sizeof(fb_sorted_entry_t));

		        if (s_fb_sorted)
		        {
		            bool populate_error = false;
		            while (s_fb_sorted_count < entry_count)
		            {
		                res = f_readdir(&directory, &file_info);
		                if (res != FR_OK) { populate_error = true; break; }
		                if (!file_info.fname[0]) { populate_error = true; break; }
		                if (file_info.fattrib & (AM_HID | AM_SYS))
		                    continue;
		                strncpy(s_fb_sorted[s_fb_sorted_count].name, file_info.fname, FF_MAX_LFN);
		                s_fb_sorted[s_fb_sorted_count].name[FF_MAX_LFN] = 0;
		                s_fb_sorted[s_fb_sorted_count].fattrib = file_info.fattrib;
		                s_fb_sorted_count++;
		            }
		            if (populate_error || s_fb_sorted_count != entry_count)
		            {
		                /* Media or directory contents changed between the
		                 * count and populate passes.  Never expose a partial
		                 * cache as an authoritative selectable listing. */
		                fb_sorted_free();
		                f_closedir(&directory);
		                pfb_hdl->info.status = FB_ERR_SDCARD;
		                return &pfb_hdl->info;
		            }
		            if (s_fb_sort_enabled)
		                qsort(s_fb_sorted, s_fb_sorted_count, sizeof(fb_sorted_entry_t), fb_entry_cmp);
		            /* Sorted-browser root shows no ".."; every other case (nested
		             * sorted folder, or any unsorted browser at any depth) keeps
		             * the one ".." row -- identical to the previous per-mode math. */
		            num_of_files = (uint16_t)(s_fb_sorted_count +
		                ((s_fb_sort_enabled && pfb_hdl->dir_level == 0) ? 0 : 1));
		        }
		        else
		        {
		            /* Covers BOTH a genuinely empty directory (entry_count==0,
		             * nothing to allocate) AND the rare calloc() failure with
		             * entries present -- num_of_files is already known from
		             * pass 1 either way, so no live re-walk is needed; a
		             * failed cache just means this caller's redraws fall back
		             * to the live per-row f_readdir path (cache_active==false
		             * below), same degraded-but-working behavior as before. */
		            num_of_files = (uint16_t)(entry_count +
		                ((s_fb_sort_enabled && pfb_hdl->dir_level == 0) ? 0 : 1));
		        }
		    }

			scroll_h = (num_of_files > 0) ? (gui_height / num_of_files) : gui_height;

			if (scroll_h < pfb_hdl->font_h)
				scroll_h = pfb_hdl->font_h;

			scroll_ok = FALSE;
			if ((num_of_files > gui_max_row) && (gui_max_column > 3))
				scroll_ok = TRUE;
		} // if (button_status==NULL)

		// Reading keys from user
		else
		{
	       	if ( button_status->event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
	       		if ( num_of_files > 0 && pfb_hdl->listing_index < (num_of_files - 1) )
	       		{
	       			pfb_hdl->row_index++;
	       			if ((pfb_hdl->row_index >= gui_max_row) || (pfb_hdl->row_index >= num_of_files) )
	       				pfb_hdl->row_index--;
	       			pfb_hdl->listing_index++;
	       		} // if ( pfb_hdl->listing_index < (num_of_files - 1) )
	       		else if ( num_of_files > 0 ) // at last item -> wrap to first (viewport to top)
	       		{
	       			pfb_hdl->listing_index = 0;
	       			pfb_hdl->row_index = 0;
	       		}
	       	} // if ( button_status->event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )

	       	else if ( button_status->event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
	       		if ( pfb_hdl->listing_index > 0 )
	       		{
	       			pfb_hdl->listing_index--;
	       			if (pfb_hdl->row_index > 0)
	       				pfb_hdl->row_index--;

	       		} // if ( pfb_hdl->listing_index > 0 )
	       		else if ( num_of_files > 0 ) // at first item -> wrap to last (viewport to bottom window)
	       		{
	       			pfb_hdl->listing_index = (uint16_t)(num_of_files - 1);
	       			pfb_hdl->row_index = (num_of_files <= gui_max_row) ? (uint16_t)(num_of_files - 1) : (uint16_t)(gui_max_row ? (gui_max_row - 1) : 0);
	       		}
	       	} // else if ( button_status->event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )

	       	else if ( button_status->event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	       	{
			if (!num_of_files) // Nothing listed: no row was ever rendered into this_file this
				break;         // session, so there is nothing safe to select or descend into.

	       		if (((pfb_hdl->dir_level > 0) || !(s_fb_sort_enabled && s_fb_sorted)) && !pfb_hdl->listing_index) // ".." row (suppressed at generic-browser root)
	       		{
	       			pfb_hdl->info.file_is_selected = FALSE;
	       			if (pfb_hdl->dir_level) // Being at sub-directory
	       			{
	       				l = strlen(pfb_hdl->info.dir_name) - 1;
	       				k = 0;
	       				while (l >= 0 && !k)
	       				{
	       					if (pfb_hdl->info.dir_name[l]=='/')
	       						k++;
	       					pfb_hdl->info.dir_name[l] = 0;
	       					l--;
	       				} // while (l >= 0 && !k)
	       				/* Shrinking realloc: on the rare implementation where this can
	       				 * still fail, the original (larger, but already-truncated-in-
	       				 * place above and therefore still valid) block is untouched --
	       				 * a temporary pointer means a NULL result never overwrites the
	       				 * live one and loses it. */
	       				{
	       					char *new_dir_name = (char *)realloc(pfb_hdl->info.dir_name, (size_t)(l + 2));
	       					if (new_dir_name != NULL)
	       						pfb_hdl->info.dir_name = new_dir_name;

	       					uint16_t *new_listing_buf = (uint16_t *)realloc(pfb_hdl->listing_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));
	       					if (new_listing_buf != NULL)
	       						pfb_hdl->listing_index_buffer = new_listing_buf;

	       					uint16_t *new_row_buf = (uint16_t *)realloc(pfb_hdl->row_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));
	       					if (new_row_buf != NULL)
	       						pfb_hdl->row_index_buffer = new_row_buf;
	       				}
	       				pfb_hdl->dir_level--;
	       			}
	       			else // Being at root directory
	       			{
	       				//pfb_hdl->listing_index_buffer[0] = 0;
	       				//pfb_hdl->row_index_buffer[0] = 0;
	       				break; // Do nothing, just stay at where it is
	       			}
	       			pfb_hdl->info.file_is_selected = FALSE;

	       			f_closedir(&directory);
	       			button_status = NULL; // Reset so that the conditional loop will be executed one more time
	       			continue;
	       		} // if (!pfb_hdl->listing_index)

	       		if (this_file.fattrib & AM_DIR)
	       		{
	       			if ( pfb_hdl->dir_level >= DIRECTORY_MAX_DEPTH_LEVEL )
	       				break; // Do nothing if it goes too deep

	       			/* Growing reallocs: every one is attempted into a temporary and
	       			 * only committed to pfb_hdl once it succeeds (realloc already
	       			 * consumed the old block on success, so a successful temporary
	       			 * must still be committed even if a later one fails). Nothing
	       			 * that changes dir_name's *content* (strcat) or dir_level runs
	       			 * until all three have succeeded, so a failure partway through
	       			 * can never leave pfb_hdl referencing a stale size or a lost
	       			 * pointer -- it just falls through to the same defined "stay
	       			 * where it is" outcome as the max-depth check above. */
	       			{
	       				uint16_t new_dir_level = (uint16_t)(pfb_hdl->dir_level + 1);

	       				char *new_dir_name = (char *)realloc(pfb_hdl->info.dir_name,
	       					strlen(pfb_hdl->info.dir_name) + 1 + 1 + strlen(this_file.fname));
	       				if (new_dir_name != NULL)
	       					pfb_hdl->info.dir_name = new_dir_name;

	       				uint16_t *new_listing_buf = (new_dir_name != NULL)
	       					? (uint16_t *)realloc(pfb_hdl->listing_index_buffer, (size_t)(new_dir_level + 1) * sizeof(uint16_t))
	       					: NULL;
	       				if (new_listing_buf != NULL)
	       					pfb_hdl->listing_index_buffer = new_listing_buf;

	       				uint16_t *new_row_buf = (new_listing_buf != NULL)
	       					? (uint16_t *)realloc(pfb_hdl->row_index_buffer, (size_t)(new_dir_level + 1) * sizeof(uint16_t))
	       					: NULL;
	       				if (new_row_buf != NULL)
	       					pfb_hdl->row_index_buffer = new_row_buf;

	       				if ((new_dir_name == NULL) || (new_listing_buf == NULL) || (new_row_buf == NULL))
	       					break; // growth failed -- stay in the current directory, nothing crashed

	       				strcat(pfb_hdl->info.dir_name, "/");
	       				strcat(pfb_hdl->info.dir_name, this_file.fname);
	       				pfb_hdl->listing_index_buffer[pfb_hdl->dir_level] = pfb_hdl->listing_index;
	       				pfb_hdl->row_index_buffer[pfb_hdl->dir_level] = pfb_hdl->row_index;
	       				pfb_hdl->dir_level = new_dir_level;
	       				pfb_hdl->listing_index_buffer[pfb_hdl->dir_level] = 0;
	       				pfb_hdl->row_index_buffer[pfb_hdl->dir_level] = 0;
	       			}
	       			pfb_hdl->info.file_is_selected = FALSE;

	       			f_closedir(&directory);
	       			button_status = NULL; // Reset so that the conditional loop will be executed one more time
	       			continue;
	       		} // if (this_file.fattrib & AM_DIR)

	       		else
	       		{
	       		    if (pfb_hdl->info.file_name)
	       		    	free(pfb_hdl->info.file_name);
	       		    pfb_hdl->info.file_name = (char *)calloc(strlen(this_file.fname) + 1, 1);
	       		    assert_param(pfb_hdl->info.file_name!=NULL);
	       		    if ( pfb_hdl->info.file_name )
	       		    {
	       		    	strcpy(pfb_hdl->info.file_name, this_file.fname);
	       		    	pfb_hdl->info.status = FB_OK;
	       		    	pfb_hdl->info.file_is_selected = TRUE;
	       		    } // if ( pfb_hdl->info.file_name )

	       		    f_closedir(&directory);
	       			break;
	       		}
	       	} // else if ( button_status->event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
		} // else
		// if (button_status==NULL)

    	res = f_readdir(&directory, 0);
    	if ( res != FR_OK)
    	{
    		pfb_hdl->info.status = FB_ERR_SDCARD;
    		break;
    	} // if ( res != FR_OK)

    	// Clear GUI
    	m1_u8g2_firstpage();
    	//m1_lcd_cleardisplay();

	bool sort_root_no_dotdot = (s_fb_sort_enabled && s_fb_sorted != NULL && pfb_hdl->dir_level == 0);
	bool show_dotdot    = !sort_root_no_dotdot;
	bool cache_active   = (s_fb_sorted != NULL);
    	count = 0;
    	y_offset = 0;
       	while (count < num_of_files)
       	{
       		name[0] = 0;
   			bool is_dotdot = (show_dotdot && count == 0);
   			if (!is_dotdot)
   			{
				if (cache_active)
   				{
   					uint16_t sidx = show_dotdot ? (uint16_t)(count - 1) : count;
   					if (sidx >= s_fb_sorted_count)
   						break;
   					memset(&file_info, 0, sizeof(file_info));
   					strcpy(file_info.fname, s_fb_sorted[sidx].name);
   					file_info.fattrib = s_fb_sorted[sidx].fattrib;
   				}
   				else
   				{
   					res = f_readdir(&directory, &file_info);
   					if (res || !file_info.fname[0])
   						break;
   					if ((file_info.fattrib & (AM_HID | AM_SYS)))
   						continue;
   				}
   			}

   			if ((count >= pfb_hdl->listing_index - pfb_hdl->row_index) &&
   				(count < pfb_hdl->listing_index - pfb_hdl->row_index + gui_max_row))
   			{
   				if (is_dotdot)
   				{
   					strcpy(name, "..");
   					fb_icon = &menu_fb_icon_prev;
   				}
   				else
  				{
					/* Display-only substitution for a small set of known
					 * top-level folders (generic explorer only); the real
					 * file_info.fname below and this_file (selection target)
					 * are never touched by this. */
					const char *disp_fname = (s_fb_friendly_names && pfb_hdl->dir_level == 0 &&
						(file_info.fattrib & AM_DIR))
						? fb_friendly_dir_name(file_info.fname) : file_info.fname;

					len = strlen(disp_fname);
   					disp_max_column = gui_max_column - 1 - scroll_ok;
   					if (len <= disp_max_column)
   					{
						strcpy(name, disp_fname);
   					}
   					else
   					{
   						if (file_info.fattrib & AM_DIR)
   						{
							strncpy(name, disp_fname, disp_max_column - 2);
   							name[disp_max_column - 2] = 0;
   							strcat(name, "..");
   						}
   						else
   						{
   							flag = FALSE;
   							while (len)
   							{
   								if (file_info.fname[len-1]=='.')
   								{
   									flag = TRUE;
   									break;
   								}
   								len--;
   							}
   							if (!flag) // filename without extension
   							{
   								strncpy(name, file_info.fname, disp_max_column - 2);
   								name[disp_max_column - 2] = 0;
   								strcat(name, "..");
   							}
   							else
   							{
   								ext_len = strlen(&file_info.fname[len - 1]);
   								if ( ext_len > 4 ) // the dot (.) + extension
   									ext_len = 4;
   								if ( len > disp_max_column )
   								{
   	   								strncpy(name, file_info.fname, disp_max_column - 2 - ext_len);
   	   								name[disp_max_column - 2 - ext_len] = 0;
   	   								strcat(name, "..");
   	   								strncat(name, &file_info.fname[len - 1], ext_len);
   								}
   								else
   								{
   	   								strncpy(name, file_info.fname, disp_max_column);
   	   								name[disp_max_column] = 0;
   								}
   							} // else
   						} // else
   					} // else

       				if (file_info.fattrib & AM_DIR)
       				{
       					fb_icon = &menu_fb_icon_dir;
       				}
       				else
       				{
       					f_ext = m1_fb_get_file_type(file_info.fname);
       					if (f_ext==F_EXT_DATA)
       						fb_icon = &menu_fb_icon_data;
       					else
       						fb_icon = &menu_fb_icon_other;
       				}

       				if (count==pfb_hdl->listing_index)
       				{
       					memcpy(&this_file, &file_info, sizeof(FILINFO));
       				}
       			} // else
   					// if (!count)

   				y_offset += spacing;
   				// Draw icon of folder or file
   				u8g2_DrawXBMP(plcd_hdl, pfb_hdl->x, pfb_hdl->y + y_offset + (pfb_hdl->font_h - fb_icon->icon_h), fb_icon->icon_w, fb_icon->icon_h, fb_icon->pdata);

    			y_offset += pfb_hdl->font_h;
    			// Draw text of file name or folder name
   				u8g2_DrawStr(plcd_hdl, pfb_hdl->x + fb_icon->icon_w + 2, pfb_hdl->y + y_offset, name);
       		} // if ((count >= pfb_hdl->listing_index - pfb_hdl->row_index) &&
				// (count < pfb_hdl->listing_index - pfb_hdl->row_index + gui_max_row))
   			count++;
       	} // while (count < num_of_files)

	if (num_of_files == 0)
	{
		/* Nothing to list and nothing to frame -- say so plainly instead
		 * of leaving a blank list with a selection box floating on
		 * nothing (matches the wording already used for an empty list
		 * elsewhere in the UI, e.g. nfc_desfire_files_draw()'s "No files"). */
		u8g2_DrawStr(plcd_hdl, pfb_hdl->x, pfb_hdl->y + pfb_hdl->font_h, "No files");
	}
	else
       	{
		// Draw a frame around the selected file/sub-directory
		u8g2_DrawFrame(plcd_hdl, pfb_hdl->x, pfb_hdl->y + pfb_hdl->row_index * (pfb_hdl->font_h + spacing) + spacing/2,
							      pfb_hdl->x + gui_width - scroll_ok*GUI_SCROLLBAR_WIDTH + 2,
								  pfb_hdl->font_h + spacing + 2
						);

		// Scroll bar is active if the number of files/directories > maximum number of rows
		if (scroll_ok)
		{
			// Scroll bar position
			scroll_y = (pfb_hdl->listing_index - pfb_hdl->row_index) * (M1_LCD_DISPLAY_HEIGHT - scroll_h) / (num_of_files - gui_max_row);
			// Scroll bar
			u8g2_DrawFrame(plcd_hdl, pfb_hdl->x + M1_LCD_DISPLAY_WIDTH - GUI_SCROLLBAR_WIDTH, pfb_hdl->y, GUI_SCROLLBAR_WIDTH, M1_LCD_DISPLAY_HEIGHT);
			// Scroll bar slider
			u8g2_DrawBox(plcd_hdl, pfb_hdl->x + M1_LCD_DISPLAY_WIDTH - GUI_SCROLLBAR_WIDTH, pfb_hdl->y + scroll_y, GUI_SCROLLBAR_WIDTH, scroll_h);
		} // if (scroll_ok)
	} // else

       	m1_u8g2_nextpage(); // Now let update display RAM with contents written above

       	break; // End this loop
	} // while (1) // Not an endless loop

	return &pfb_hdl->info;

} // S_M1_file_info *m1_fb_display(S_M1_Buttons_Status *button_status)




/******************************************************************************/
/*
*	This function displays files and folders to the console for CLI
*
*/
/******************************************************************************/
FRESULT m1_fb_listing(const char *dir_name)
{
	char name[FF_MAX_LFN + 1];
	FRESULT res;
	DIR directory;
	FILINFO file_info, this_file;
	uint16_t num_of_files;
	uint16_t count, len;
	bool flag;

	while (1) // Not an endless loop
	{
		res = f_opendir(&directory, dir_name);
		if (res != FR_OK)
		{
			break;
		}
	    num_of_files = 1;
	    while (num_of_files < FILE_BROWSER_MAX_FILES)
	    {
	    	res = f_readdir(&directory, &file_info);
	    	if (res || !file_info.fname[0])
	    		break;
	    	if (!(file_info.fattrib & (AM_HID | AM_SYS))) // Not a system or hidden file?
	    		num_of_files++;
	    } // while (num_of_files < FILE_BROWSER_MAX_FILES)

    	res = f_readdir(&directory, 0);
    	if ( res != FR_OK)
    	{
    		break;
    	} // if ( res != FR_OK)

    	count = 0;
       	while (count < num_of_files)
       	{
       		name[0] = 0;
   			if (count)
   			{
   				res = f_readdir(&directory, &file_info);
   				if (res || !file_info.fname[0])
   					break;

   				if ((file_info.fattrib & (AM_HID | AM_SYS))) // Hidden and System file?
   					continue;
   			} // if (count)

			if (!count)
			{
				strcpy(name, "..");
			}
			else
			{
				if (strlen(file_info.fname) <= FILENAME_LEN_ON_CLI_MAX)
				{
					strcpy(name, file_info.fname);
				}
				else
				{
					if (file_info.fattrib & AM_DIR)
					{
						strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX);
						name[FILENAME_LEN_ON_CLI_MAX] = 0;
						strcat(name, "..");
					}
					else
					{
						len = strlen(file_info.fname);
						flag = FALSE;
						while (len)
						{
							if (file_info.fname[len-1]=='.')
							{
								flag = TRUE;
								break;
							}
							len--;
						}
						if (!flag || len==1)
						{
							strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX);
							name[FILENAME_LEN_ON_CLI_MAX] = 0;
							strcat(name, "..");
						}
						else
						{
							strncpy(name, file_info.fname, FILENAME_LEN_ON_CLI_MAX - strlen(file_info.fname + len));
							strncat(name, "..", FILENAME_LEN_ON_CLI_MAX - (strlen(name) + strlen(file_info.fname + len)));
							strncat(name, file_info.fname + len, FILENAME_LEN_ON_CLI_MAX - strlen(name));
						}
					} // else
				} // else
   			} // else
					// if (!count)
			M1_LOG_N(M1_LOGDB_TAG, "%s\r\n", name);
   			count++;
       	} // while (count < num_of_files)
       	break; // End this loop
	} // while (1) // Not an endless loop

	return res;
} // FRESULT m1_fb_listing(const char *dir_name)



/******************************************************************************/
/*
*	This function...
*
*/
/******************************************************************************/
void m1_fb_deinit(void)
{
	fb_sorted_free();
	s_fb_sort_enabled = false;   /* clear the session sort flag on deinit */
	s_fb_friendly_names = false; /* clear the session friendly-names flag on deinit */
	s_fb_explorer_nav_enabled = false; /* clear the session explorer-nav flag on deinit */

	if (pfb_hdl)
	{
		if (pfb_hdl->listing_index_buffer)
			free(pfb_hdl->listing_index_buffer);

		if (pfb_hdl->row_index_buffer)
			free(pfb_hdl->row_index_buffer);

		if (pfb_hdl->info.dir_name)
			free(pfb_hdl->info.dir_name);

		if (pfb_hdl->info.file_name)
			free(pfb_hdl->info.file_name);

		free(pfb_hdl);
		pfb_hdl = NULL;
	} // if (pfb_hdl)
} // void m1_fb_deinit(void)

/* Ascend exactly one directory level toward the canonical SD root, for the
 * generic Explorer's own BACK button. See m1_fb_back_result_t in the
 * header for the return contract. Determines "already at root" from the
 * canonical current path (fb_is_root_path), not from dir_level alone, so a
 * session reopened at a non-root start directory still ascends correctly.
 * The path-truncation/buffer-shrink logic below is intentionally the same
 * shape as the ".." row's own ascend inside m1_fb_display() -- kept as a
 * separate implementation (not refactored into a shared helper) so that
 * proven, hardware-accepted path is left completely untouched by this
 * separately-reviewed addition. */
m1_fb_back_result_t m1_fb_navigate_back(void)
{
	int l;
	int found_sep;

	if (!pfb_hdl || !pfb_hdl->info.dir_name)
		return M1_FB_BACK_ERROR;

	if (fb_is_root_path(pfb_hdl->info.dir_name))
		return M1_FB_BACK_AT_ROOT;

	if (pfb_hdl->dir_level == 0)
	{
		/* Defensive: the canonical-path check above already covers the
		 * normal case; dir_level cannot be decremented below 0. */
		return M1_FB_BACK_AT_ROOT;
	}

	pfb_hdl->info.file_is_selected = FALSE;
	f_closedir(&directory);

	l = (int)strlen(pfb_hdl->info.dir_name) - 1;
	found_sep = 0;
	while ((l >= 0) && !found_sep)
	{
		if (pfb_hdl->info.dir_name[l] == '/')
			found_sep = 1;
		pfb_hdl->info.dir_name[l] = 0;
		l--;
	}

	/* The legacy truncation shape above yields "0:" when the parent is the
	 * drive root.  Keep the browser's authoritative path canonical ("0:/")
	 * so callers never observe two spellings for the same location.  Every
	 * non-root path allocation is large enough for these four bytes. */
	if (!strcmp(pfb_hdl->info.dir_name, "0:"))
		strcpy(pfb_hdl->info.dir_name, SDCARD_DEFAULT_DRIVE_PATH);

	/* Shrinking reallocs: same "keep the still-valid truncated block on
	 * failure" pattern as the ".." row's own ascend. */
	{
		char *new_dir_name = (char *)realloc(pfb_hdl->info.dir_name,
			strlen(pfb_hdl->info.dir_name) + 1U);
		if (new_dir_name != NULL)
			pfb_hdl->info.dir_name = new_dir_name;

		uint16_t *new_listing_buf = (uint16_t *)realloc(pfb_hdl->listing_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));
		if (new_listing_buf != NULL)
			pfb_hdl->listing_index_buffer = new_listing_buf;

		uint16_t *new_row_buf = (uint16_t *)realloc(pfb_hdl->row_index_buffer, (size_t)pfb_hdl->dir_level * sizeof(uint16_t));
		if (new_row_buf != NULL)
			pfb_hdl->row_index_buffer = new_row_buf;
	}
	pfb_hdl->dir_level--;

	return M1_FB_BACK_PARENT_OPENED;
}



/******************************************************************************/
/*
*	This function dynamically concatenates number of strings given by num
*	Each string is separated by the separator symbol given in separators
*	Return: length of the new string
*/
/******************************************************************************/
uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...)
{
	va_list pargs;
	uint8_t len, k;
	char *tmp_buffer;

	assert(num >= 1);

	va_start(pargs, format);
	len = 0;
	k = num;
	while (k)
	{
		tmp_buffer = va_arg(pargs, char *);
		len += strlen(tmp_buffer);
		k--;
	} // while (k)

	if ( num > 1 )
		len += num - 1 ; // Add separator symbol to the end of each string

	if ( !len )
		return 0;

	va_start(pargs, format);
	k = num;
	strcpy(buffer, "");
	while (k)
	{
		tmp_buffer = va_arg(pargs, char *);
		if (strlen(tmp_buffer))
		{
			strcat(buffer, tmp_buffer);
			if (k != 1) // Not the last string?
				strcat(buffer, "/"); // Add separator symbol to the end of each string
		}
		k--;
	} // while (k)
	va_end(pargs);

	return strlen(buffer);
} // uint8_t m1_fb_dyn_strcat(char *buffer, uint8_t num, const char *format, ...)

