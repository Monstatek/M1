/* See COPYING.txt for license details. */

#include <stdbool.h>
#include "m1_t2t_emu_image.h"


/* NFC-A short commands */
#define NFCA_CMD_REQA   0x26  /* 7-bit */
#define NFCA_CMD_WUPA   0x52  /* 7-bit */

/* Type 2 Tag commands */
#define T2T_CMD_READ        0x30
#define T2T_CMD_FAST_READ   0x3A
#define T2T_CMD_WRITE       0xA2  /* 4 bytes */
#define T2T_CMD_SECTOR_SEL  0xC2
#define T2T_CMD_GET_VERSION 0x60
#define T2T_CMD_COMPAT_WRITE 0xA0 /* optional / legacy */


typedef enum {
    EMU_PERSONA_T4T = 0,
    EMU_PERSONA_T2T,
    EMU_PERSONA_RAW,    // Use ATQA/SAK from read card as-is (e.g., SAK=08)
    EMU_PERSONA_MFC_DETECT, // MIFARE Classic Detect Reader: forced 1K identity + auth capture
#if defined(M1_MFC_RAW_EMULATION)
    EMU_PERSONA_MFC_EMU,    /* MonstaTek: full raw MIFARE Classic emulation (Scope B) */
#endif
} EmuPersona_t;

/**
 * @brief Emu_SetPersona - Set emulation persona
 * 
 * @param[in] p Persona type
 * @retval None
 */
void Emu_SetPersona(EmuPersona_t p);

/**
 * @brief Emu_GetPersona - Get emulation persona
 * 
 * @retval Current persona type
 */
EmuPersona_t Emu_GetPersona(void);

/**
 * @brief ListenIni - Initialize NFC listener
 * 
 * @retval true Initialization successful
 * @retval false Initialization failed
 */
bool ListenIni(void);

/**
 * @brief ListenerCycle - Main listener processing loop
 * 
 * @retval None
 */
void ListenerCycle(void);

/**
 * @brief ListenerRequestStop - Request listener stop
 * 
 * @retval None
 */
void ListenerRequestStop(void);

/**
 * @brief ListenerGetLastRx - Get last received data
 * 
 * @param[out] lenBits Pointer to store received length in bits
 * @retval Pointer to last received data buffer, or NULL if no data
 */
const uint8_t* ListenerGetLastRx(uint16_t *lenBits);

/**
 * @brief nfc_listener_set_t2t_emu_image - Arm the T2T RF listener with a
 * self-contained saved-card image (see m1_t2t_emu_image.h). Copies *img;
 * once armed, READ/FAST_READ/GET_VERSION/WRITE/COMPATIBILITY_WRITE are
 * served from this image exclusively, never nfc_ctx.
 *
 * @param[in] img Eligible image (M1_T2T_EMU_OK from m1_t2t_emu_image_build())
 * @retval None
 */
void nfc_listener_set_t2t_emu_image(const m1_t2t_emu_image_t *img);

/**
 * @brief nfc_listener_clear_t2t_emu_image - Disarm the T2T image. Call on
 * emulation stop/teardown so a stale image never survives into the next
 * session. T2T command handling reverts to the legacy nfc_ctx-direct
 * fallback path while unarmed.
 *
 * @retval None
 */
void nfc_listener_clear_t2t_emu_image(void);

/**
 * @brief nfc_listener_get_t2t_emu_image - Read back the armed image,
 * possibly mutated by reader WRITEs during this session (see ->dirty), so
 * the UI can offer Save Changes / Discard after emulation stops.
 *
 * @retval Pointer to the armed image, or NULL if not armed
 */
const m1_t2t_emu_image_t* nfc_listener_get_t2t_emu_image(void);

/**
 * @brief NFC_T2TTransportIsActive - Is the dedicated Type-2 transport
 * (m1_t2t_transport.c) currently owning the radio? Checked by nfc_driver.c
 * BEFORE nfc_process_func(), exactly mirroring m1_mfc_raw_hw_active()'s
 * existing role/ordering so MFC and Type-2 ownership can never overlap.
 *
 * @retval true Transport owns the session; nfc_process_func()/ListenerCycle()
 *              must not be called this tick
 */
bool NFC_T2TTransportIsActive(void);

/**
 * @brief NFC_T2TTransportProcess - Service one worker-task tick of the
 * dedicated Type-2 transport in place of nfc_process_func(). Services the
 * existing ListenerRequestStop() flag first (stopping the transport, on the
 * worker task, exactly where every other RFAL/SPI access in this driver is
 * required to happen) before ticking it.
 *
 * @retval None
 */
void NFC_T2TTransportProcess(void);
