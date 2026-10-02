/* See COPYING.txt for license details. */

/*
*
* m1_fw_update_bl.h
*
* Firmware update support functions
*
* M1 Project
*
*/

#ifndef M1_FW_UPDATE_BL_H_
#define M1_FW_UPDATE_BL_H_

#include "ff.h"
#include "ff_gen_drv.h"

#define FW_CONFiG_ADDRESS				0x080FFC00 // FW config, range is 0x080FFC00-0x080FFFFF (1KB)
#define FW_CONFiG_SIZE					1024 // bytes
#define FLASH_MEM_LAST_SECTOR_ADDRESS	0x080FE000 // Sector 127, 8K

#define FW_START_ADDRESS 				0x08000000
#define FW_CONFIG_RESERVED_ADDRESS		0x080FFC00 // LENGTH = 1K, this address must match the one defined in the MEMORY section in the linker file
#define FW_CRC_ADDRESS 					FW_CONFIG_RESERVED_ADDRESS + sizeof(S_M1_FW_CONFIG_t)

#define M1_FLASH_BANK_SIZE				0x00100000

#define	FW_BOOT_MESSAGE_LF				0x0A
#define	FW_BOOT_MESSAGE_CR				0x0D
#define	FW_BOOT_MESSAGE_SEPARATOR		':'

#define FW_IMAGE_SIZE_MAX				(uint32_t)0x100000 // 1Mbytes
#define FW_IMAGE_CHUNK_SIZE				1024 // bytes
#define FW_IMAGE_CRC_SIZE				4 // 4 bytes = 32 bits

#define FW_VERSION_MAJOR   			0
#define FW_VERSION_MINOR   			8
#define FW_VERSION_BUILD   			1
#define FW_VERSION_RC   			0

/* About-box/version-log label, drawn directly with u8g2_DrawStr().
 * The label must fit the display width at ABOUT_BOX_Y_POS_ROW_4. */
#define FW_BUILD_LABEL				"FW 0.8.1.0"

#define FW_CONFIG_MAGIC_NUMBER_1	((uint32_t)0x4D493235)
#define FW_CONFIG_MAGIC_NUMBER_2    ((uint32_t)0x534A1F41)

#define BANK1_ACTIVE 				0x534A
#define BANK2_ACTIVE 				0x1F41

#define FW_UPDATE_SLIDER_WIDTH					5 // pixel
#define FW_UPDATE_SLIDER_OVERLAP				2 // pixel, each slider will be overlapped by 2 pixel when it's drawn on screen

#define FW_UPDATE_FULL_PROGRESS_COUNT			40 	// Calculated based on the sizes of the slider and the slide strip
												// N = (124 - x_0)/(slider_width - overlap)
												// x_0 = 3, start column; 124 = slide strip width - 2
#define FW_UPDATE_FULL_PROGRESS_FACTOR			(float)100/FW_UPDATE_FULL_PROGRESS_COUNT

#define GUI_DISP_LINE_LEN_MAX					M1_LCD_DISPLAY_WIDTH/M1_SUB_MENU_FONT_WIDTH

#define FW_UPDATE_PROGRESS_SLIDE_STRIP_ROW		48
#define FW_UPDATE_PROGRESS_SLIDE_STRIP_COL		1

#define FW_CFG_SECTION __attribute__((section(".FW_CONFIG_SECTION")))

// Size of this struct must be a multiple of 4
typedef struct {
	uint32_t magic_number_1;
	uint8_t fw_version_rc; // version number in little endian order
	uint8_t fw_version_build;
	uint8_t fw_version_minor;
	uint8_t fw_version_major;
	uint16_t user_option_1;
	uint16_t User_option_2;
	uint8_t ism_band_region;
	uint8_t reserve_1;
	uint16_t reserve_2;
	uint32_t magic_number_2;
} S_M1_FW_CONFIG_t;

typedef enum
{
	M1_FW_UPDATE_SUCCESS = 0,
	M1_FW_UPDATE_READY,	// 0x01
	M1_FW_CRC_FILE_ACCESS_ERROR,	// 0x02
	M1_FW_CRC_FILE_INVALID,
	M1_FW_IMAGE_FILE_ACCESS_ERROR,	// 0x04
	M1_FW_IMAGE_FILE_TYPE_ERROR,
	M1_FW_IMAGE_SIZE_INVALID,
	M1_FW_CRC_CHECKSUM_UNMATCHED,
	M1_FW_VERSION_ERROR,
	M1_FW_ISM_BAND_REGION_ERROR,
	M1_FW_UPDATE_FAILED,
	M1_FW_UPDATE_LOW_BATTERY,
	M1_FW_UPDATE_NOT_READY
} S_M1_M1_FW_CODES_t;

typedef enum
{
    BL_CODE_OK = 0,
	BL_CODE_APP_ERROR,
	BL_CODE_SIZE_ERROR,
	BL_CODE_CHK_ERROR,
	BL_CODE_ERASE_ERROR,
    BL_WRITE_ERROR,
	BL_CODE_OBP_ERROR
} S_M1_BL_CODES_t;

typedef enum
{
    BL_PROTECTION_NONE  = 0,
    BL_PROTECTION_WRP   = 0x1,
    BL_PROTECTION_RDP   = 0x2,
    BL_PROTECTION_PCROP = 0x4
} S_M1_BL_PROTECTION_t;

uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk);
uint8_t bl_flash_app(FIL *hfile);
uint16_t bl_get_active_bank(void);
uint8_t bl_crc_check(uint32_t image_size);
void bl_swap_banks(void);
void fw_gui_progress_update(size_t remainder);

/* Streamed programming wrappers (M1CP firmware-update path). These reuse the
 * exact SD-path engine (bl_flash_if_init/bl_flash_start/bl_flash_binary/
 * bl_flash_if_deinit) in the same order, driven by streamed chunks instead of a
 * file. Sequence: begin(image_size) [erases inactive bank] -> write(chunk)... ->
 * finish() [runs bl_crc_check over the programmed image vs the embedded CRC].
 * All chunks except the final one must be a multiple of 16 bytes (quadword).
 * Return BL_CODE_OK on success. No bank swap here; activation is separate
 * (bl_swap_banks). Task context only. */
uint8_t bl_flash_stream_begin(uint32_t image_size);
uint8_t bl_flash_stream_write(uint8_t *payload, uint32_t size);
uint8_t bl_flash_stream_finish(void);

/* Force bl_flash_binary()'s internal write cursor (previously a set of
 * function-local statics that only ever cleared themselves on a SUCCESSFUL
 * finish) back to its pre-BEGIN state. Call this from both
 * bl_flash_stream_begin() and any abort path -- without it, a BEGIN issued
 * after a transfer that was aborted or that failed before FINISH resumes
 * writing at the STALE mid-image cursor from the abandoned attempt, into a
 * freshly-erased bank, producing a corrupt image at the wrong offset. The
 * SD-card path (bl_flash_app) never hits this because it always runs one
 * uninterrupted begin->write*->finish sequence per call; only the M1CP
 * streamed path (which can BEGIN again after an abort) can. */
void bl_flash_stream_reset(void);

/* bl_flash_stream_reset() + re-lock flash (HAL_FLASH_Lock, via the same
 * private path bl_flash_stream_finish() already uses on success). Call this
 * on any exit that ISN'T a successful finish() -- an abort, a cancel, or a
 * begin()/write() failure -- so flash never stays unlocked and the cursor
 * never stays stale across a fresh begin(). Task context only. */
void bl_flash_stream_abort(void);

extern FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config;

#endif /* M1_FW_UPDATE_BL_H_ */
