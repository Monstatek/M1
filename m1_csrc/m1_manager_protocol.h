/* See COPYING.txt for license details. */

/*
*
*  m1_manager_protocol.h
*
*  M1 Manager Control Protocol (M1CP) v1 - framing, session and non-destructive
*  device-information foundation over the existing USB CDC interface.
*
*  This milestone implements only core/session and non-destructive
*  device-information commands. Firmware writing, SD writing, ESP32 flashing and
*  HID activation are explicitly out of scope; their command namespaces are
*  reserved (see documentation/M1CP_v1.md).
*
*  Design constraints:
*    - deterministic, browser-JavaScript-friendly binary frame;
*    - no dynamic allocation in the USB receive path (static buffers only);
*    - the USB interrupt/callback only ingests bytes; all parsing, dispatch and
*      responses happen in task context via m1cp_process();
*    - legacy CDC console/bridge behavior remains the default: M1CP activates
*      only after an exact, CRC-validated HELLO handshake.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_PROTOCOL_H_
#define M1_MANAGER_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ frame ---
 * Wire layout (little-endian for multi-byte scalar fields):
 *   off size field
 *   0   4    magic       = 'M','1','C','P' (0x4D 0x31 0x43 0x50), byte order as written
 *   4   1    version     = M1CP_VERSION
 *   5   1    msg_type
 *   6   1    flags
 *   7   2    seq         (LE)  request sequence number; responses echo it
 *   9   2    payload_len (LE)  0 .. M1CP_MAX_PAYLOAD
 *   11  N    payload
 *   11+N 2   crc16       (LE)  CRC-16/CCITT-FALSE over bytes [4 .. 10+N] (version..payload)
 * ------------------------------------------------------------------------- */
#define M1CP_MAGIC0                 0x4DU  /* 'M' */
#define M1CP_MAGIC1                 0x31U  /* '1' */
#define M1CP_MAGIC2                 0x43U  /* 'C' */
#define M1CP_MAGIC3                 0x50U  /* 'P' */
#define M1CP_MAGIC_LEN              4U

#define M1CP_VERSION                0x01U

#define M1CP_HEADER_LEN             11U    /* magic..payload_len inclusive */
#define M1CP_CRC_LEN                2U
#define M1CP_MAX_PAYLOAD            512U
#define M1CP_MAX_FRAME              (M1CP_HEADER_LEN + M1CP_MAX_PAYLOAD + M1CP_CRC_LEN)

/* flags (bitmask) */
#define M1CP_FLAG_RESPONSE          0x01U
#define M1CP_FLAG_EVENT             0x02U
#define M1CP_FLAG_ACK               0x04U
#define M1CP_FLAG_NACK              0x08U

/* message types, namespaced (see doc). Only the ones below are implemented. */
/* core/session : 0x01..0x0F */
#define M1CP_CMD_HELLO              0x01U
#define M1CP_CMD_PING               0x02U
#define M1CP_CMD_GET_CAPABILITIES   0x03U
#define M1CP_CMD_END_SESSION        0x04U
/* device information : 0x10..0x1F */
#define M1CP_CMD_GET_DEVICE_INFO    0x10U
#define M1CP_CMD_GET_STATUS         0x11U  /* variable, versioned rich status  */
/* reserved (documented, NOT implemented): STM32 fw 0x20-0x2F, ESP32 fw
 * 0x30-0x3F, filesystem/content 0x40-0x4F, events/status 0xF0-0xFF */

/* stable error codes (carried in a NACK response payload, 1 byte) */
#define M1CP_ERR_NONE               0x00U
#define M1CP_ERR_BAD_CRC            0x01U
#define M1CP_ERR_BAD_VERSION        0x02U
#define M1CP_ERR_UNKNOWN_CMD        0x03U
#define M1CP_ERR_BAD_LENGTH         0x04U
#define M1CP_ERR_NO_SESSION         0x05U
#define M1CP_ERR_BUSY               0x06U
#define M1CP_ERR_NOT_IMPLEMENTED    0x07U
#define M1CP_ERR_BAD_PAYLOAD        0x08U
/* extended error model (Web Manager) - see documentation/M1CP_web_manager.md */
#define M1CP_ERR_BAD_STATE          0x09U  /* not valid in current op state     */
#define M1CP_ERR_BAD_ARG            0x0AU  /* argument out of range / invalid   */
#define M1CP_ERR_TIMEOUT            0x0BU  /* operation timed out               */
#define M1CP_ERR_INTEGRITY          0x0CU  /* hash/CRC verification failed      */
#define M1CP_ERR_SD_UNAVAILABLE     0x0DU  /* SD missing/unmounted/host-owned   */
#define M1CP_ERR_NO_SPACE           0x0EU  /* insufficient free space           */
#define M1CP_ERR_FLASH              0x0FU  /* STM32 flash erase/program failure */
#define M1CP_ERR_ESP                0x10U  /* ESP bootloader/flash/boot failure */
#define M1CP_ERR_NOT_FOUND          0x11U  /* named file/resource not found     */
#define M1CP_ERR_IO                 0x12U  /* filesystem I/O error              */
#define M1CP_ERR_ABORTED            0x13U  /* operation cancelled by client     */
#define M1CP_ERR_INCOMPATIBLE       0x14U  /* wrong target / fw<->resource mism.*/

/* capability flags (GET_CAPABILITIES / device info) - implemented groups only */
#define M1CP_CAP_CORE               0x00000001UL  /* HELLO/PING/CAPS/END_SESSION */
#define M1CP_CAP_DEVICE_INFO        0x00000002UL  /* GET_DEVICE_INFO             */
#define M1CP_CAP_STM32_UPDATE       0x00000004UL  /* STM32 firmware update       */
#define M1CP_CAP_ESP32_UPDATE       0x00000008UL  /* ESP32 firmware update       */
#define M1CP_CAP_FILESYSTEM         0x00000010UL  /* Add Files / content         */
#define M1CP_CAP_EVENTS             0x00000020UL  /* async progress events       */

/* Session inactivity timeout: no valid frame for this long -> end session and
 * restore legacy CDC behavior. */
#define M1CP_SESSION_TIMEOUT_MS     60000U

/* ---- Manager operation status (GET_STATUS payload + future 0xF0 events) ----
 * A single long-running operation is tracked at a time. Long ops are async:
 * the start command ACKs immediately and the client polls GET_STATUS. See
 * documentation/M1CP_web_manager.md sections 3 and 7. */

/* op_domain */
#define M1CP_DOMAIN_NONE            0x00U
#define M1CP_DOMAIN_STM32           0x01U
#define M1CP_DOMAIN_ESP32           0x02U
#define M1CP_DOMAIN_FS              0x03U

/* op_state ids (shared numbering for GET_STATUS and events) */
#define M1CP_OP_IDLE                0x00U
#define M1CP_OP_RECEIVING           0x01U
#define M1CP_OP_VERIFYING           0x02U
#define M1CP_OP_STAGING             0x03U
#define M1CP_OP_PROGRAMMING         0x05U
#define M1CP_OP_VERIFYING_PROG      0x06U
#define M1CP_OP_INSTALLING_RES      0x07U
#define M1CP_OP_REBOOT_REQUIRED     0x08U
#define M1CP_OP_ESP_OWNING          0x09U
#define M1CP_OP_ESP_FLASHING        0x0AU
#define M1CP_OP_ESP_VERIFYING       0x0BU
#define M1CP_OP_ESP_RESETTING       0x0CU
#define M1CP_OP_ESP_CONFIRMING      0x0DU
#define M1CP_OP_COMPLETE            0x0EU
#define M1CP_OP_FAILED              0x0FU

/* device_state (overall) */
#define M1CP_DEV_OK                 0x00U
#define M1CP_DEV_BUSY               0x01U
#define M1CP_DEV_FAULT              0x02U

/* GET_STATUS.flags bits */
#define M1CP_STF_REBOOT_REQUIRED    0x01U
#define M1CP_STF_FW_PKG_STAGED      0x02U
#define M1CP_STF_TRANSFER_ACTIVE    0x04U

#define M1CP_PROGRESS_INDETERMINATE 0xFFU

/* Publish operation progress/state so GET_STATUS reflects it. Called from task
 * context by the long-operation handlers (filesystem/STM32/ESP32 gates). */
void m1cp_status_set(uint8_t domain, uint8_t op_state, uint8_t progress);

/* Record the last error code (also surfaced in GET_STATUS.last_error). */
void m1cp_status_set_error(uint8_t err);

/* Set/clear one of the M1CP_STF_* status flag bits. */
void m1cp_status_set_flag(uint8_t flag_bit, bool on);

/* Publish the installed resource-package version. present=0 clears it (reports
 * "not installed"); ver is 4 bytes major,minor,build,rc. Called by the resource
 * installer (STM32 firmware gate). */
void m1cp_status_set_resource(uint8_t present, const uint8_t ver[4]);

/* -------------------------------------------------------------------- API --- */

/* Transmit callback: queue `len` bytes over CDC. Return >=0 on success. */
typedef int (*m1cp_send_fn)(const uint8_t *data, uint16_t len);

/* Legacy-console sink: forward `len` bytes to the legacy CDC CLI input path.
 * Called only from task context (m1cp_process), only before a session, and only
 * for bytes that are NOT part of a consumed M1CP HELLO handshake. */
typedef void (*m1cp_legacy_fn)(const uint8_t *data, uint16_t len);

/* Initialize the protocol layer (call once, from task context, at startup).
 * `send`   - transmit M1CP frames over CDC.
 * `legacy` - replay non-M1CP / rejected candidate bytes to the legacy CLI so
 *            console behavior is preserved when no valid HELLO occurs.
 * Creates the static RX stream buffer and resets parser/session state. */
void m1cp_init(m1cp_send_fn send, m1cp_legacy_fn legacy);

/* Ingest received CDC bytes. ISR/callback-safe: only copies into the static RX
 * stream buffer, never parses. Returns true if M1CP accepted the bytes (the
 * caller must NOT also feed them to the legacy CLI); false if M1CP is not ready
 * yet (the caller should preserve legacy behavior itself). `hpw` is the FreeRTOS
 * higher-priority-woken out flag (may be NULL if not called from an ISR). */
bool m1cp_rx_from_isr(const uint8_t *data, uint16_t len, BaseType_t *hpw);

/* Task-context worker: drain the RX stream buffer, parse complete frames,
 * dispatch commands and emit responses. Also enforces the session timeout.
 * Must NOT be called from interrupt context. */
void m1cp_process(void);

/* True while a validated manager session is active. Used by the CDC layer to
 * gate legacy console/log output and route RX to M1CP only. */
bool m1cp_session_active(void);

/* Notify the protocol layer that the USB link dropped (disconnect/suspend).
 * Ends any active session cleanly so legacy CDC behavior is restored. */
void m1cp_notify_link_down(void);

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0).
 * Exposed for the host test harness. */
uint16_t m1cp_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_PROTOCOL_H_ */
