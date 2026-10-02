/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fwupdate.c
*
*  M1CP streamed STM32 firmware-update command dispatch. See
*  m1_manager_fwupdate.h and m1_manager_update_coordinator.h.
*
*  This file is now a THIN shim over the shared m1_manager_update_coordinator:
*  it owns one m1cp_update_session_t, parses/builds the STM32-specific wire
*  payloads (BEGIN's manifest layout, ACTIVATE's confirm token), and forwards
*  everything else to the coordinator. All state-machine logic lives there.
*
* M1 Project
*
*/

#include <string.h>
#include "m1_manager_fwupdate.h"
#include "m1_manager_protocol.h"   /* error codes, status-publish API, op ids */

static m1cp_update_session_t s_session;

/* MD5 of exactly the bytes streamed to the flash backend this attempt --
 * see m1cp_fwupdate_last_md5()'s own doc comment. Populated by the firmware
 * backend's finish() only when a client hash was actually supplied. */
static uint8_t s_last_md5[16];
static bool    s_last_md5_valid = false;

bool m1cp_fwupdate_last_md5(uint8_t out[16])
{
    if (!s_last_md5_valid) { return false; }
    memcpy(out, s_last_md5, 16U);
    return true;
}

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*************************** C O M M A N D S ********************************/

/* FW_UPDATE_BEGIN: image_size(4)|hash_algo(1)|hash(16)|confirm(2) -> max_chunk(2).
 * See M1CP_FW_BEGIN_CONFIRM's own doc comment (m1_manager_fwupdate.h) for the
 * exact wire shape. Erases the inactive bank. */
static uint8_t fwu_begin(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    uint32_t image_size;
    uint8_t  hash_algo, hash_len;
    const uint8_t *hash_ptr;

    if (plen < 23U) { return M1CP_ERR_BAD_PAYLOAD; }  /* 4+1+16+2 */
    image_size = rd_u32(&payload[0]);
    hash_algo  = payload[4];
    if (rd_u16(&payload[21]) != M1CP_FW_BEGIN_CONFIRM) { return M1CP_ERR_BAD_ARG; }

    hash_len = (hash_algo == M1CP_UPDATE_HASH_NONE) ? 0U : M1CP_FW_BEGIN_HASH_LEN;
    hash_ptr = (hash_len != 0U) ? &payload[5] : NULL;

    return m1cp_update_begin(&s_session, 0U, image_size, hash_algo, hash_ptr, hash_len,
                              M1CP_FW_CHUNK_MAX, out, olen);
}

/* FW_UPDATE_DATA: offset(4)|data(N) -> next_offset(4). Programs one chunk. */
static uint8_t fwu_data(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    return m1cp_update_data(&s_session, payload, plen, out, olen);
}

/* FW_UPDATE_FINISH: verify the programmed image (hardware CRC32 [+ MD5]). */
static uint8_t fwu_finish(void)
{
    return m1cp_update_finish(&s_session);
}

/* FW_UPDATE_ACTIVATE: confirm(2). Defers the swap+reset until after the ACK. */
static uint8_t fwu_activate(const uint8_t *payload, uint16_t plen)
{
    if (plen < 2U) { return M1CP_ERR_BAD_PAYLOAD; }
    return m1cp_update_activate(&s_session, rd_u16(&payload[0]), M1CP_FW_ACTIVATE_CONFIRM);
}

static uint8_t fwu_abort(void)
{
    return m1cp_update_abort(&s_session);
}

/*************************** P U B L I C ************************************/

void m1cp_fwupdate_init(const m1cp_update_backend_t *backend)
{
    static const m1cp_update_ops_t ops =
    {
        M1CP_DOMAIN_STM32, M1CP_OP_PROGRAMMING, M1CP_OP_VERIFYING_PROG,
        M1CP_OP_REBOOT_REQUIRED, M1CP_OP_COMPLETE
    };
    m1cp_update_session_init(&s_session, backend, &ops);
}

bool m1cp_fwupdate_busy(void)
{
    return m1cp_update_busy(&s_session);
}

void m1cp_fwupdate_reset(void)
{
    m1cp_update_reset(&s_session);
}

bool m1cp_fwupdate_activate_pending(void)
{
    return m1cp_update_activate_pending(&s_session);
}

void m1cp_fwupdate_do_activate(void)
{
    m1cp_update_do_activate(&s_session);
}

uint16_t m1cp_fwupdate_last_diag(void)
{
    return m1cp_update_last_diag(&s_session);
}

uint8_t m1cp_fwupdate_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                             uint8_t *out, uint16_t *out_len)
{
    uint8_t err;
    *out_len = 0U;

    switch (msg_type)
    {
        case M1CP_CMD_FW_UPDATE_BEGIN:    err = fwu_begin(payload, plen, out, out_len); break;
        case M1CP_CMD_FW_UPDATE_DATA:     err = fwu_data(payload, plen, out, out_len);  break;
        case M1CP_CMD_FW_UPDATE_FINISH:   err = fwu_finish();                           break;
        case M1CP_CMD_FW_UPDATE_ACTIVATE: err = fwu_activate(payload, plen);            break;
        case M1CP_CMD_FW_UPDATE_ABORT:    err = fwu_abort();                            break;
        default:                          err = M1CP_ERR_NOT_IMPLEMENTED;               break;
    }
    if (err != M1CP_ERR_NONE) { *out_len = 0U; }
    return err;
}

/*************************** F I R M W A R E   B A C K E N D ****************/

#ifndef M1CP_HOST_TEST
#include "m1_fw_update_bl.h"   /* bl_flash_stream_*, bl_flash_stream_reset, bl_swap_banks */
#include "m1_power_ctl.h"      /* m1_check_battery_level                                  */
#include "m1_md5_hash.h"       /* mh_md5_init/update/final -- optional additive MD5 check */

static bool fwu_be_ready(void)
{
    return (m1_check_battery_level(50) != 0U);  /* >= 50%, matching the SD path */
}

static uint8_t fwu_be_begin(uint32_t flash_offset, uint32_t image_size)
{
    (void)flash_offset;   /* STM32 always programs the fixed bank-2 alias; not caller-selectable */
    s_last_md5_valid = false;
    mh_md5_init(0U, 0U);
    return bl_flash_stream_begin(image_size);
}

static uint8_t fwu_be_write(const uint8_t *data, uint32_t size)
{
    /* bl_flash_if_write() (m1_fw_update_bl.c) casts its source pointer
     * straight to (uint32_t *) for a QUADWORD HAL_FLASH_Program() call. The
     * M1CP RX frame buffer this `data` pointer lands in (s_frame,
     * m1_manager_protocol.c) places every DATA command's payload at a fixed
     * +15-byte offset from s_frame's own base (11-byte M1CP header + this
     * command's own 4-byte offset field) -- not a multiple of 4 regardless
     * of s_frame's own alignment, so `data` here is guaranteed misaligned
     * for a 32-bit/128-bit access every single call. Stage into a properly
     * 4-aligned local buffer first, exactly as the ESP32 backend already
     * does with its own s_flash_stage[] (m1_manager_espupdate.c). This only
     * changes this M1CP dispatch shim -- bl_flash_if_write()/
     * bl_flash_binary() themselves are untouched, so the proven SD-card
     * update path (bl_flash_app(), which calls the same functions with its
     * own naturally-aligned local buffer) is unaffected. */
    static uint8_t s_stage[M1CP_FW_CHUNK_MAX] __attribute__((aligned(4)));
    uint8_t rc;

    memcpy(s_stage, data, size);
    rc = bl_flash_stream_write(s_stage, size);
    if (rc == BL_CODE_OK) { mh_md5_update(data, size); }
    return rc;
}

static uint8_t fwu_be_finish(const uint8_t *expected_hash, uint8_t hash_len)
{
    /* The device's own hardware-CRC32 self-check (bl_crc_check, reached via
     * bl_flash_stream_finish) is UNCONDITIONAL and authoritative -- never
     * skipped or weakened regardless of whether the client supplied an MD5.
     * An MD5, when supplied, is an ADDITIONAL, independent check on top,
     * exactly like the ESP path's own model. */
    uint8_t raw_md5[16];
    uint8_t crc_rc = bl_flash_stream_finish();
    mh_md5_final(raw_md5);
    if (hash_len == 16U) { memcpy(s_last_md5, raw_md5, 16U); s_last_md5_valid = true; }

    if (crc_rc != BL_CODE_OK) { return 2U; /* hard error -- CRC32 mismatch or a HAL failure */ }
    if ((hash_len == 16U) && (memcmp(raw_md5, expected_hash, 16U) != 0U)) { return 1U; /* client MD5 mismatch */ }
    return 0U;
}

static void fwu_be_activate(void)
{
    bl_swap_banks();   /* delays, then resets; does not return on hardware */
}

static void fwu_be_abort(void)
{
    /* Before this hook existed, NOTHING called bl_flash_stream_reset() or
     * re-locked flash on an abort/cancel/begin-failure -- a cancelled or
     * failed-before-FINISH transfer left flash unlocked indefinitely and
     * the write cursor stale for the next BEGIN (see bl_flash_stream_reset's
     * own doc comment in m1_fw_update_bl.h for exactly what that corrupted). */
    bl_flash_stream_abort();
}

static const m1cp_update_backend_t s_stm32_backend =
{
    "stm32", M1CP_FW_MAX_IMAGE, 16U, true, M1CP_UPDATE_HASH_NONE, M1CP_ERR_FLASH,
    fwu_be_ready, fwu_be_begin, fwu_be_write, fwu_be_finish, fwu_be_activate, fwu_be_abort
};

void m1cp_fwupdate_install_backend(void)
{
    m1cp_fwupdate_init(&s_stm32_backend);
}
#endif /* !M1CP_HOST_TEST */
