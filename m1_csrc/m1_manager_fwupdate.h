/* See COPYING.txt for license details. */

/*
*
*  m1_manager_fwupdate.h
*
*  M1CP streamed STM32 firmware-update command dispatch. This file is now a
*  THIN shim: wire-payload parsing only. The state machine itself (BEGIN/
*  DATA/FINISH/ACTIVATE/ABORT, manifest validation, resumable/resend-
*  idempotent transfer, diagnostics, measured counters) is
*  m1_manager_update_coordinator.c, shared with the ESP32 path -- see that
*  file for the design rationale.
*
*  The Manager fetches the official firmware `_wCRC.bin` over HTTPS and
*  streams the raw bytes over M1CP straight into the INACTIVE bank. Integrity
*  is always the device's own hardware-CRC32 check (bl_crc_check, reached via
*  the backend's finish()) over the programmed image vs the embedded srec_cat
*  CRC -- unconditional, and never weakened by this file. A client MAY
*  additionally supply an MD5 (matching the ESP path's own, mandatory,
*  client-supplied-hash model) which is checked too, additively; see
*  M1CP_FW_BEGIN_CONFIRM's own doc comment for the exact wire shape.
*
*  Flow:  FW_UPDATE_BEGIN (erase inactive bank) -> FW_UPDATE_DATA* (program
*  chunks) -> FW_UPDATE_FINISH (CRC [+ MD5] verify) -> FW_UPDATE_ACTIVATE
*  (swap+reboot). FW_UPDATE_ABORT cancels before activation. The running bank
*  is never touched until activation, so a failed/interrupted update leaves
*  the device bootable.
*
*  The state machine is backend-abstracted (m1cp_update_backend_t, generic
*  across every M1CP update target) so it is exercised on the host without
*  flashing hardware. The firmware backend reuses the existing
*  bl_flash_stream_* engine and bl_swap_banks unchanged, plus
*  bl_flash_stream_reset() (a bug fix: the engine's own write cursor is now
*  actually reset on every begin()/abort(), where it previously wasn't --
*  see that function's own doc comment in m1_fw_update_bl.h) and an abort()
*  hook (new: releases/re-locks flash on cancel or a genuine DATA-phase
*  error, which nothing did before this change).
*
*  M1CP_FW_FLASH_IMPLEMENTED is currently DEFINED (see its own doc comment
*  below, near the #define) -- STM32_UPDATE is presently advertised, for
*  on-device testing of this dispatch layer. The underlying flash engine
*  itself was separately hardware-validated before this capability was
*  turned on; see that same comment for exactly what was and wasn't tested.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_FWUPDATE_H_
#define M1_MANAGER_FWUPDATE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* streamed firmware-update message types (0x21..0x25; 0x20 is FW_VALIDATE) */
#define M1CP_CMD_FW_UPDATE_BEGIN     0x21U
#define M1CP_CMD_FW_UPDATE_DATA      0x22U
#define M1CP_CMD_FW_UPDATE_FINISH    0x23U
#define M1CP_CMD_FW_UPDATE_ACTIVATE  0x24U
#define M1CP_CMD_FW_UPDATE_ABORT     0x25U

#define M1CP_FW_MAX_IMAGE   0x00100000UL  /* 1 MiB, matches FW_IMAGE_SIZE_MAX    */
#define M1CP_FW_CHUNK_MAX   480U          /* data bytes per FW_UPDATE_DATA (x16) */
#define M1CP_FW_ACTIVATE_CONFIRM 0xA5C3U  /* required FW_UPDATE_ACTIVATE token   */
/* FW_UPDATE_BEGIN payload (generic-coordinator manifest, mirrors the ESP
 * path's own BEGIN shape): image_size(4) | hash_algo(1) | hash(16, ignored
 * when hash_algo==M1CP_UPDATE_HASH_NONE but always reserved on the wire for
 * a fixed, simple payload layout) | confirm(2) = 23 bytes. hash_algo/hash
 * are m1cp_update_hash_algo_t / M1CP_UPDATE_HASH_MAX_LEN-shaped (see
 * m1_manager_update_coordinator.h) -- an MD5 is optional for this target
 * (min_hash_algo == M1CP_UPDATE_HASH_NONE: the device's own embedded
 * hardware-CRC32 self-check is authoritative either way), but a client
 * that supplies one gets it checked too, additively, exactly like the ESP
 * path's mandatory one. */
#define M1CP_FW_BEGIN_CONFIRM     0xB5C3U  /* required FW_UPDATE_BEGIN token       */
#define M1CP_FW_BEGIN_HASH_LEN    16U      /* wire-reserved hash field size (MD5) */

/*
 * The underlying program/verify/swap/reboot ENGINE (bl_flash_stream_*,
 * bl_swap_banks) was HARDWARE-VALIDATED on bare metal (dual-bank swap
 * confirmed: active bank flipped + version updated after reboot; verify-
 * before-activate rejected a corrupt image with INTEGRITY and did not swap)
 * and remains untouched by the generic-coordinator migration. The DISPATCH
 * layer around it (this file, the shared coordinator, the extended BEGIN
 * wire format, the new abort()/cursor-reset hooks) is NOT yet itself
 * hardware-validated -- do not claim end-to-end HWPASS for this candidate
 * without a fresh physical test. Defining M1CP_FW_FLASH_IMPLEMENTED
 * advertises the STM32_UPDATE capability (see m1_manager_protocol.c) so the
 * Web Manager reveals the customer firmware-update UI.
 */
#define M1CP_FW_FLASH_IMPLEMENTED 1

/* Backend struct: m1cp_update_backend_t (m1_manager_update_coordinator.h),
 * shared with every other M1CP update target. See that header for the
 * exact begin()/write()/finish()/activate()/abort() contract. */
#include "m1_manager_update_coordinator.h"

void m1cp_fwupdate_init(const m1cp_update_backend_t *backend);

/*
 * Handle a streamed firmware-update command (0x21..0x25). On success writes any
 * response payload into `out` (capacity >= 4) and sets *out_len, returning
 * M1CP_ERR_NONE (caller sends RESPONSE, or RESPONSE|ACK when *out_len == 0);
 * otherwise a non-zero M1CP error code. Task context only.
 */
uint8_t m1cp_fwupdate_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                             uint8_t *out, uint16_t *out_len);

/* True while an update is in progress (receiving or verified-pending-activate). */
bool m1cp_fwupdate_busy(void);

/* Abort any in-progress update (session end / link-down). Safe: only the
 * inactive bank was touched; the running bank is intact. */
void m1cp_fwupdate_reset(void);

/* Deferred activation: FW_UPDATE_ACTIVATE ACKs first, then the caller (task loop)
 * performs the swap+reset so the ACK is transmitted before the MCU resets. */
bool m1cp_fwupdate_activate_pending(void);
void m1cp_fwupdate_do_activate(void);

/* (step << 8) | code -- NEW: the STM32 path previously had no diagnostic
 * channel at all (a NACK carried only the bare error byte). Matches the ESP
 * path's own diag shape exactly (m1cp_espupdate_last_diag) so
 * m1_manager_protocol.c's NACK builder can use either session's diagnostic
 * without per-target branching. step: 0 none, 1 begin, 2 write, 3 finish. */
uint16_t m1cp_fwupdate_last_diag(void);

/* MD5 of exactly the bytes streamed to the flash backend this attempt, valid
 * once FW_UPDATE_FINISH has run AND a client hash was actually supplied
 * (M1CP_UPDATE_HASH_MD5) -- matches m1cp_espupdate_last_md5()'s shape and
 * same diagnostic purpose exactly (mismatch-leg diagnosis without a debug
 * UART). Returns false otherwise (including on the host build, or when this
 * target's own hardware-CRC32 self-check was the only check performed). */
bool m1cp_fwupdate_last_md5(uint8_t out[16]);

/* Install the firmware flash backend (bl_flash_stream_* + bl_swap_banks). */
#ifndef M1CP_HOST_TEST
void m1cp_fwupdate_install_backend(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_FWUPDATE_H_ */
