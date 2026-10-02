/* See COPYING.txt for license details. */

/*
*
*  m1_manager_protocol.c
*
*  M1 Manager Control Protocol (M1CP) v1 implementation. See m1_manager_protocol.h
*  and documentation/M1CP_v1.md for the wire format and rules.
*
*  Safety model:
*    - the receive path (m1cp_rx_from_isr) only copies bytes into a statically
*      allocated FreeRTOS stream buffer; it never parses and never allocates;
*    - all parsing, dispatch and responses run in task context (m1cp_process);
*    - the byte parser is a bounded state machine: every branch either advances a
*      fixed-size static buffer or resets to magic-sync, so malformed input,
*      split frames, oversized lengths, bad CRC and input noise can never cause
*      overflow, unbounded reads or parser lockup;
*    - before a validated HELLO handshake nothing is transmitted, so random
*      console bytes cannot enter manager mode or pollute the console.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <string.h>
#include "m1_manager_protocol.h"
#include "m1_manager_fs.h"
#include "m1_manager_fw.h"
#include "m1_manager_fwupdate.h"
#include "m1_manager_espupdate.h"
#include "m1_esp_version.h"

/* STM32 firmware update is advertised only once the streamed program/verify/
 * swap/reboot path has been validated on physical hardware, gated by defining
 * M1CP_FW_FLASH_IMPLEMENTED (m1_manager_fwupdate.h) -- when undefined, this
 * bit resolves to 0 and the customer update UI stays hidden. CURRENTLY
 * DEFINED (see m1_manager_fwupdate.h's own comment above that #define): this
 * capability IS presently advertised, for on-device testing of the streamed
 * path. Integrity for this path is CRC32 only (bl_crc_check), by design -
 * there is no cryptographic authentication. */
#if defined(M1CP_FW_FLASH_IMPLEMENTED)
#define M1CP_CAP_STM32_UPDATE_ADV   M1CP_CAP_STM32_UPDATE
#else
#define M1CP_CAP_STM32_UPDATE_ADV   0UL
#endif

/* ESP32 firmware update is advertised only once the streamed esp_loader flash
 * path has been validated on physical hardware, gated by defining
 * M1CP_ESP_FLASH_IMPLEMENTED (m1_manager_espupdate.h) -- when undefined, this
 * bit resolves to 0 and the ESP update UI stays hidden, exactly as
 * STM32_UPDATE is gated above. CURRENTLY DEFINED (see
 * m1_manager_espupdate.h's own comment above that #define): this capability
 * IS presently advertised, for on-device testing of the streamed path. */
#if defined(M1CP_ESP_FLASH_IMPLEMENTED)
#define M1CP_CAP_ESP32_UPDATE_ADV   M1CP_CAP_ESP32_UPDATE
#else
#define M1CP_CAP_ESP32_UPDATE_ADV   0UL
#endif

/* Capabilities advertised in HELLO_RESPONSE / GET_CAPABILITIES / device info.
 * A capability bit is set only when its handlers are built and safe; this single
 * definition keeps every advertisement site in sync. */
#define M1CP_CAPS_ADVERTISED  (M1CP_CAP_CORE | M1CP_CAP_DEVICE_INFO | \
                               M1CP_CAP_FILESYSTEM | M1CP_CAP_STM32_UPDATE_ADV | \
                               M1CP_CAP_ESP32_UPDATE_ADV)

#ifndef M1CP_HOST_TEST
/* ---- firmware build ---- */
#include "stream_buffer.h"
#include "stm32h5xx_hal.h"
#include "m1_fw_update_bl.h"   /* FW_VERSION_*, M1_FLASH_BANK_SIZE, bl_get_active_bank */
#include "m1_sdcard.h"         /* m1_sdcard_get_status, SD_access_*, m1_sd_detected   */
#include "m1_storage.h"        /* usbmsc_sd_enable                                    */
#define M1CP_TICK_MS()   ((uint32_t)HAL_GetTick())
/* m1_esp32_get_and_clear_rx_drop_count/ore_count declared directly (rather
 * than pulling in m1_esp32_hal.h, which drags in FreeRTOS/HAL headers the
 * host unit test doesn't have) - defined/implemented in m1_esp32_hal.c. */
uint32_t m1_esp32_get_and_clear_rx_drop_count(void);
uint32_t m1_esp32_get_and_clear_ore_count(void);
#else
/* ---- host test build: minimal stubs so the parser can be exercised ---- */
#include "m1cp_test_stubs.h"
#endif

/*************************** D E F I N E S ************************************/

#define M1CP_RX_STREAM_BYTES      1024U   /* ISR->task ring for received CDC bytes */

/* A partial (in-progress) frame must complete within this long, else the parser
 * discards it and resynchronizes. Bounds recovery after a truncated frame or a
 * disconnect mid-frame, and prevents a stuck partial frame from blocking. */
#define M1CP_FRAME_TIMEOUT_MS     500U

/* device family id */
#define M1CP_FAMILY_MONSTATEK_M1  0x01U

/* GET_DEVICE_INFO response is a fixed 56-byte little-endian structure. */
#define M1CP_DEVINFO_LEN          56U

/* GET_STATUS response: versioned, currently 43 bytes (struct_version 0x01).
 * See documentation/M1CP_web_manager.md section 3. */
#define M1CP_STATUS_LEN           43U
#define M1CP_STATUS_STRUCT_VER    0x01U

/* sd_mount_state values */
#define M1CP_SD_STATE_NOTREADY    0x00U
#define M1CP_SD_STATE_UNMOUNTED   0x01U
#define M1CP_SD_STATE_MOUNTED     0x02U
#define M1CP_SD_STATE_NOFS        0x03U
#define M1CP_SD_STATE_NOTOK       0x04U
#define M1CP_SD_STATE_UNKNOWN     0xFFU

/* parser states */
typedef enum
{
    M1CP_ST_MAGIC = 0,
    M1CP_ST_HEADER,
    M1CP_ST_PAYLOAD,
    M1CP_ST_CRC
} m1cp_pstate_t;

/*************************** V A R I A B L E S *******************************/

static const uint8_t s_magic[M1CP_MAGIC_LEN] =
{
    M1CP_MAGIC0, M1CP_MAGIC1, M1CP_MAGIC2, M1CP_MAGIC3
};

static m1cp_send_fn   s_send       = NULL;
static m1cp_legacy_fn s_legacy     = NULL;

/* parser state */
static uint8_t      s_frame[M1CP_MAX_FRAME];
static uint16_t     s_idx         = 0U;   /* bytes collected into s_frame          */
static uint8_t      s_magic_match = 0U;   /* magic bytes matched so far            */
static uint16_t     s_payload_len = 0U;
static m1cp_pstate_t s_pstate     = M1CP_ST_MAGIC;

/* session state */
static bool         s_session     = false;
static uint32_t     s_last_frame_ms = 0U;   /* last complete valid frame (session) */
static uint32_t     s_frame_start_ms = 0U;  /* start of the current partial frame  */

/* response scratch (task context only) */
static uint8_t      s_tx[M1CP_MAX_FRAME];

/* manager operation status (task context only; published via GET_STATUS) */
static uint8_t      s_op_domain      = M1CP_DOMAIN_NONE;
static uint8_t      s_op_state       = M1CP_OP_IDLE;
static uint8_t      s_op_progress    = 0U;
static uint8_t      s_last_error     = M1CP_ERR_NONE;
static uint8_t      s_status_flags   = 0U;
static uint8_t      s_resource_present = 0U;
static uint8_t      s_resource_ver[4]  = { 0xFFU, 0xFFU, 0xFFU, 0xFFU };
/* ESP32 installed-version: reported from the persistent record the STM32 keeps
 * when it flashes the ESP32 (Gate D, Option B; m1_esp_version.{c,h}). "state"
 * is 0 unavailable / 1 valid. */

#ifndef M1CP_HOST_TEST
static StreamBufferHandle_t s_rx_sb = NULL;
static StaticStreamBuffer_t s_rx_sb_struct;
static uint8_t              s_rx_sb_store[M1CP_RX_STREAM_BYTES + 1U];
#endif

/********************* F O R W A R D   D E C L S ******************************/

static void     m1cp_parse_byte(uint8_t b);
static void     m1cp_dispatch(uint8_t msg_type, uint8_t flags, uint16_t seq,
                              const uint8_t *payload, uint16_t plen);
static void     m1cp_send_frame(uint8_t msg_type, uint8_t flags, uint16_t seq,
                                const uint8_t *payload, uint16_t plen);
static void     m1cp_send_nack(uint8_t msg_type, uint16_t seq, uint8_t err);
static void     m1cp_send_update_nack(uint8_t msg_type, uint16_t seq, uint8_t err, uint16_t diag,
                                       bool (*last_md5)(uint8_t out[16]), bool include_esp_uart_counters);
static uint16_t m1cp_build_device_info(uint8_t *out);
static uint16_t m1cp_build_status(uint8_t *out);
static void     m1cp_parser_reset(void);
static void     m1cp_legacy_replay(const uint8_t *d, uint16_t n);
static uint16_t m1cp_held_count(void);
#ifndef M1CP_HOST_TEST
static void     m1cp_put_u64le(uint8_t *p, uint64_t v);
static bool     m1cp_sd_capacity(uint64_t *total, uint64_t *freeb);
#endif

/*************************** C R C - 1 6 *************************************/

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000. */
uint16_t m1cp_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t   i;
    uint8_t  bit;

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/*************************** L I F E C Y C L E *******************************/

void m1cp_init(m1cp_send_fn send, m1cp_legacy_fn legacy)
{
    s_send = send;
    s_legacy = legacy;
    m1cp_parser_reset();
    s_session = false;
    s_last_frame_ms = M1CP_TICK_MS();

    m1cp_esp_version_init();            /* load persisted ESP32 version (Gate D) */

#ifndef M1CP_HOST_TEST
    if (s_rx_sb == NULL)
    {
        /* trigger level 1: wake the task as soon as any byte is available */
        s_rx_sb = xStreamBufferCreateStatic(sizeof(s_rx_sb_store), 1U,
                                            s_rx_sb_store, &s_rx_sb_struct);
    }
    m1cp_fs_install_fatfs_backend();   /* Add Files: FatFs-backed storage */
    m1cp_fw_install_fatfs_source();    /* FW_VALIDATE: FatFs package source */
    m1cp_fwupdate_install_backend();   /* FW update: bl_flash_stream_* backend */
    m1cp_espupdate_install_backend();  /* ESP update: esp_loader UART backend */
#endif
}

static void m1cp_parser_reset(void)
{
    s_pstate      = M1CP_ST_MAGIC;
    s_idx         = 0U;
    s_magic_match = 0U;
    s_payload_len = 0U;
}

bool m1cp_session_active(void)
{
    return s_session;
}

void m1cp_notify_link_down(void)
{
    /* Restore legacy CDC behavior cleanly on disconnect/suspend. */
    s_session = false;
    m1cp_fs_reset();          /* discard any in-progress transfer + temp file */
    m1cp_fwupdate_reset();    /* abort in-progress fw update (inactive bank only) */
    m1cp_espupdate_reset();   /* abort in-progress ESP update, release the radio */
    m1cp_parser_reset();
}

/*************************** R X  (I S R  s a f e) ***************************/

bool m1cp_rx_from_isr(const uint8_t *data, uint16_t len, BaseType_t *hpw)
{
#ifndef M1CP_HOST_TEST
    if (s_rx_sb == NULL)
    {
        return false;   /* not initialized yet: caller preserves legacy */
    }
    if ((data != NULL) && (len != 0U))
    {
        /* Copy only; excess bytes when full are dropped (client retransmits). */
        (void)xStreamBufferSendFromISR(s_rx_sb, data, (size_t)len, hpw);
    }
    return true;        /* M1CP owns these bytes (CLI replay decided in task ctx) */
#else
    /* host test: feed directly */
    uint16_t i;
    (void)hpw;
    if (data != NULL)
    {
        for (i = 0U; i < len; i++)
        {
            m1cp_parse_byte(data[i]);
        }
    }
    return true;
#endif
}

/*************************** P R O C E S S  (task) ***************************/

void m1cp_process(void)
{
#ifndef M1CP_HOST_TEST
    uint8_t  tmp[64];
    size_t   n;

    if (s_rx_sb != NULL)
    {
        /* Drain all currently-available bytes and feed the parser. */
        for (;;)
        {
            n = xStreamBufferReceive(s_rx_sb, tmp, sizeof(tmp), 0);
            if (n == 0U)
            {
                break;
            }
            for (size_t i = 0U; i < n; i++)
            {
                m1cp_parse_byte(tmp[i]);
            }
        }
    }

    /* Deferred firmware activation: FW_UPDATE_ACTIVATE ACKs during dispatch
     * above; perform the swap+reset here so the ACK is transmitted first
     * (bl_swap_banks delays ~200 ms before reset, flushing the USB TX). */
    if (m1cp_fwupdate_activate_pending())
    {
        m1cp_fwupdate_do_activate();   /* swaps banks and resets the MCU */
    }
#endif

    {
        uint32_t now = M1CP_TICK_MS();

        /* Candidate-frame timeout: a held partial magic or in-progress frame that
         * does not complete in time is discarded and resynced. Pre-session the
         * held bytes are replayed to the legacy console so no console input is
         * lost while sniffing for a HELLO. */
        {
            uint16_t held = m1cp_held_count();
            if ((held > 0U) &&
                ((uint32_t)(now - s_frame_start_ms) > (uint32_t)M1CP_FRAME_TIMEOUT_MS))
            {
                if (!s_session) { m1cp_legacy_replay(s_frame, held); }
                m1cp_parser_reset();
            }
        }

        /* Session inactivity timeout -> clean return to legacy CDC behavior. */
        if (s_session &&
            ((uint32_t)(now - s_last_frame_ms) > (uint32_t)M1CP_SESSION_TIMEOUT_MS))
        {
            s_session = false;
            m1cp_fs_reset();      /* discard any in-progress transfer + temp */
            m1cp_fwupdate_reset();
            m1cp_espupdate_reset(); /* abort in-progress ESP update, release the radio --
                                     * m1cp_notify_link_down() (above) already resets all
                                     * three of fs/fwupdate/espupdate; this timeout path
                                     * had silently dropped this one, leaking ESP32 UART
                                     * ownership on an abandoned (not properly
                                     * finished/aborted) ESP update. */
            m1cp_parser_reset();
        }
    }
}

/*************************** P A R S E R ************************************/

/* Forward bytes to the legacy CLI (task context only). */
static void m1cp_legacy_replay(const uint8_t *d, uint16_t n)
{
    if ((s_legacy != NULL) && (d != NULL) && (n > 0U))
    {
        s_legacy(d, n);
    }
}

/* Number of bytes currently held as a candidate (partial magic or partial frame). */
static uint16_t m1cp_held_count(void)
{
    return (s_pstate == M1CP_ST_MAGIC) ? (uint16_t)s_magic_match : s_idx;
}

/*
 * Byte parser with pre-session hold-and-replay. A candidate frame (anything
 * starting with the magic) is held, never fed to the legacy CLI, until it is
 * resolved: a CRC-valid HELLO is consumed by M1CP only (no bytes reach the CLI);
 * anything else - a mismatching magic prefix, a bad-version/oversized/bad-CRC
 * frame, or a valid non-HELLO frame - is replayed to the legacy CLI in original
 * order so console behavior is preserved. During an active session the CLI is
 * gated off entirely, so nothing is replayed.
 */
static void m1cp_parse_byte(uint8_t b)
{
    switch (s_pstate)
    {
        case M1CP_ST_MAGIC:
            if (b == s_magic[s_magic_match])
            {
                s_frame[s_magic_match] = b;
                if (s_magic_match == 0U)
                {
                    s_frame_start_ms = M1CP_TICK_MS();  /* start candidate timer */
                }
                s_magic_match++;
                if (s_magic_match == M1CP_MAGIC_LEN)
                {
                    s_idx    = M1CP_MAGIC_LEN;
                    s_pstate = M1CP_ST_HEADER;
                }
            }
            else
            {
                /* The held partial magic was a false start: replay it (pre-session)
                 * to the legacy console, then reconsider the current byte. */
                if (s_magic_match > 0U)
                {
                    if (!s_session) { m1cp_legacy_replay(s_frame, s_magic_match); }
                    s_magic_match = 0U;
                }
                if (b == s_magic[0])
                {
                    s_frame[0] = b;
                    s_magic_match = 1U;
                    s_frame_start_ms = M1CP_TICK_MS();
                }
                else if (!s_session)
                {
                    m1cp_legacy_replay(&b, 1U);   /* ordinary console byte */
                }
                else
                {
                    /* in-session stray byte between frames: dropped */
                }
            }
            break;

        case M1CP_ST_HEADER:
            s_frame[s_idx] = b;
            s_idx++;
            if (s_idx == M1CP_HEADER_LEN)
            {
                uint8_t  ver = s_frame[4];
                uint16_t seq = (uint16_t)((uint16_t)s_frame[7] |
                                          ((uint16_t)s_frame[8] << 8));
                s_payload_len = (uint16_t)((uint16_t)s_frame[9] |
                                           ((uint16_t)s_frame[10] << 8));

                if ((ver != M1CP_VERSION) || (s_payload_len > M1CP_MAX_PAYLOAD))
                {
                    uint8_t err = (ver != M1CP_VERSION) ? M1CP_ERR_BAD_VERSION
                                                        : M1CP_ERR_BAD_LENGTH;
                    if (s_session) { m1cp_send_nack(s_frame[5], seq, err); }
                    else           { m1cp_legacy_replay(s_frame, s_idx); }
                    m1cp_parser_reset();
                }
                else if (s_payload_len == 0U)
                {
                    s_pstate = M1CP_ST_CRC;
                }
                else
                {
                    s_pstate = M1CP_ST_PAYLOAD;
                }
            }
            break;

        case M1CP_ST_PAYLOAD:
            s_frame[s_idx] = b;
            s_idx++;
            if (s_idx == (uint16_t)(M1CP_HEADER_LEN + s_payload_len))
            {
                s_pstate = M1CP_ST_CRC;
            }
            break;

        case M1CP_ST_CRC:
        default:
            s_frame[s_idx] = b;
            s_idx++;
            if (s_idx == (uint16_t)(M1CP_HEADER_LEN + s_payload_len + M1CP_CRC_LEN))
            {
                uint16_t crc_off = (uint16_t)(M1CP_HEADER_LEN + s_payload_len);
                uint16_t got = (uint16_t)((uint16_t)s_frame[crc_off] |
                                          ((uint16_t)s_frame[crc_off + 1U] << 8));
                /* CRC covers version..payload : bytes [4 .. crc_off-1]. */
                uint16_t calc = m1cp_crc16(&s_frame[M1CP_MAGIC_LEN],
                                           (size_t)(crc_off - M1CP_MAGIC_LEN));
                uint8_t  type  = s_frame[5];
                uint8_t  flags = s_frame[6];
                uint16_t seq   = (uint16_t)((uint16_t)s_frame[7] |
                                            ((uint16_t)s_frame[8] << 8));
                bool     is_cmd = ((flags & (M1CP_FLAG_RESPONSE | M1CP_FLAG_EVENT)) == 0U);

                if (got != calc)
                {
                    /* bad CRC */
                    if (s_session) { m1cp_send_nack(type, seq, M1CP_ERR_BAD_CRC); }
                    else           { m1cp_legacy_replay(s_frame, s_idx); }
                }
                else if (s_session)
                {
                    m1cp_dispatch(type, flags, seq,
                                  &s_frame[M1CP_HEADER_LEN], s_payload_len);
                }
                else if (is_cmd && (type == M1CP_CMD_HELLO))
                {
                    /* the only pre-session frame consumed by M1CP: opens the
                     * session and emits HELLO_RESPONSE; none of its bytes reach
                     * the legacy CLI. */
                    m1cp_dispatch(type, flags, seq,
                                  &s_frame[M1CP_HEADER_LEN], s_payload_len);
                }
                else
                {
                    /* CRC-valid but not a HELLO (or a response/event): preserve
                     * legacy behavior by replaying the held bytes to the CLI. */
                    m1cp_legacy_replay(s_frame, s_idx);
                }
                m1cp_parser_reset();
            }
            break;
    }
}

/*************************** D I S P A T C H ********************************/

static void m1cp_dispatch(uint8_t msg_type, uint8_t flags, uint16_t seq,
                          const uint8_t *payload, uint16_t plen)
{
    /* Ignore frames that are themselves responses/events from the peer. */
    if ((flags & (M1CP_FLAG_RESPONSE | M1CP_FLAG_EVENT)) != 0U)
    {
        return;
    }

    /* Any complete valid frame refreshes the session activity timer. */
    s_last_frame_ms = M1CP_TICK_MS();

    if (!s_session)
    {
        /* Pre-session: only an exact HELLO opens a session. Everything else is
         * silently ignored so random console traffic never enters manager mode
         * and no unsolicited frames are emitted. */
        if (msg_type == M1CP_CMD_HELLO)
        {
            uint8_t body[7];
            s_session = true;
            body[0] = M1CP_VERSION;
            body[1] = (uint8_t)(M1CP_MAX_PAYLOAD & 0xFFU);
            body[2] = (uint8_t)((M1CP_MAX_PAYLOAD >> 8) & 0xFFU);
            body[3] = (uint8_t)(M1CP_CAPS_ADVERTISED & 0xFFUL);
            body[4] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 8) & 0xFFUL);
            body[5] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 16) & 0xFFUL);
            body[6] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 24) & 0xFFUL);
            m1cp_send_frame(M1CP_CMD_HELLO, M1CP_FLAG_RESPONSE, seq, body, sizeof(body));
        }
        return;
    }

    /* In-session command handling. */
    switch (msg_type)
    {
        case M1CP_CMD_HELLO:
        {
            /* Re-HELLO within a session: answer, keep session. */
            uint8_t body[7];
            body[0] = M1CP_VERSION;
            body[1] = (uint8_t)(M1CP_MAX_PAYLOAD & 0xFFU);
            body[2] = (uint8_t)((M1CP_MAX_PAYLOAD >> 8) & 0xFFU);
            body[3] = (uint8_t)(M1CP_CAPS_ADVERTISED & 0xFFUL);
            body[4] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 8) & 0xFFUL);
            body[5] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 16) & 0xFFUL);
            body[6] = (uint8_t)((M1CP_CAPS_ADVERTISED >> 24) & 0xFFUL);
            m1cp_send_frame(M1CP_CMD_HELLO, M1CP_FLAG_RESPONSE, seq, body, sizeof(body));
            break;
        }

        case M1CP_CMD_PING:
            /* Echo the request payload (bounded) as pong. */
            m1cp_send_frame(M1CP_CMD_PING, M1CP_FLAG_RESPONSE, seq, payload, plen);
            break;

        case M1CP_CMD_GET_CAPABILITIES:
        {
            uint8_t body[7];
            uint32_t caps = M1CP_CAPS_ADVERTISED;
            body[0] = (uint8_t)(caps & 0xFFUL);
            body[1] = (uint8_t)((caps >> 8) & 0xFFUL);
            body[2] = (uint8_t)((caps >> 16) & 0xFFUL);
            body[3] = (uint8_t)((caps >> 24) & 0xFFUL);
            body[4] = (uint8_t)(M1CP_MAX_PAYLOAD & 0xFFU);
            body[5] = (uint8_t)((M1CP_MAX_PAYLOAD >> 8) & 0xFFU);
            body[6] = M1CP_VERSION;
            m1cp_send_frame(M1CP_CMD_GET_CAPABILITIES, M1CP_FLAG_RESPONSE, seq,
                            body, sizeof(body));
            break;
        }

        case M1CP_CMD_GET_DEVICE_INFO:
        {
            uint16_t n = m1cp_build_device_info(s_tx + M1CP_HEADER_LEN);
            /* build_device_info wrote into the payload area of s_tx; send it. */
            m1cp_send_frame(M1CP_CMD_GET_DEVICE_INFO, M1CP_FLAG_RESPONSE, seq,
                            s_tx + M1CP_HEADER_LEN, n);
            break;
        }

        case M1CP_CMD_GET_STATUS:
        {
            uint16_t n = m1cp_build_status(s_tx + M1CP_HEADER_LEN);
            m1cp_send_frame(M1CP_CMD_GET_STATUS, M1CP_FLAG_RESPONSE, seq,
                            s_tx + M1CP_HEADER_LEN, n);
            break;
        }

        case M1CP_CMD_END_SESSION:
            m1cp_send_frame(M1CP_CMD_END_SESSION,
                            (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_ACK), seq, NULL, 0U);
            s_session = false;
            m1cp_fs_reset();          /* no orphan .part across session end */
            m1cp_fwupdate_reset();
            m1cp_espupdate_reset();
            m1cp_parser_reset();
            break;

        default:
            if ((msg_type >= 0x40U) && (msg_type <= 0x4FU))
            {
                /* filesystem / content namespace -> Add Files module */
                uint16_t n   = 0U;
                /* Cross-domain exclusion: FS, STM32 update, and ESP32 update
                 * each ran with zero awareness of the other two -- nothing
                 * anywhere called any of the three _busy() query functions.
                 * A client could open an Add Files transfer while a flash
                 * update streams (or vice versa), contending for the same
                 * SD card / internal flash / UART resources. */
                uint8_t  err = (m1cp_fwupdate_busy() || m1cp_espupdate_busy())
                             ? M1CP_ERR_BUSY
                             : m1cp_fs_handle(msg_type, payload, plen,
                                              s_tx + M1CP_HEADER_LEN, &n);
                if (err == M1CP_ERR_NONE)
                {
                    uint8_t fl = (n == 0U)
                               ? (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_ACK)
                               : (uint8_t)M1CP_FLAG_RESPONSE;
                    m1cp_send_frame(msg_type, fl, seq, s_tx + M1CP_HEADER_LEN, n);
                }
                else
                {
                    m1cp_send_nack(msg_type, seq, err);
                }
            }
            else if ((msg_type >= 0x20U) && (msg_type <= 0x2FU))
            {
                /* STM32 firmware namespace: 0x20 = read-only FW_VALIDATE;
                 * 0x21..0x25 = the streamed CRC32-only update flow. Both build
                 * responses into s_tx; STM32_UPDATE is currently advertised
                 * (M1CP_FW_FLASH_IMPLEMENTED is defined -- see the capability
                 * gating comment near the top of this file). */
                uint16_t n   = 0U;
                /* Cross-domain exclusion (see the FS branch above for the
                 * full rationale) -- FW_VALIDATE itself is a read-only
                 * validator with no update session, left unguarded exactly
                 * as before. */
                uint8_t  err = (msg_type == M1CP_CMD_FW_VALIDATE)
                             ? m1cp_fw_handle(msg_type, payload, plen,
                                              s_tx + M1CP_HEADER_LEN, &n)
                             : (m1cp_fs_busy() || m1cp_espupdate_busy())
                             ? M1CP_ERR_BUSY
                             : m1cp_fwupdate_handle(msg_type, payload, plen,
                                              s_tx + M1CP_HEADER_LEN, &n);
                if (err == M1CP_ERR_NONE)
                {
                    uint8_t fl = (n == 0U)
                               ? (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_ACK)
                               : (uint8_t)M1CP_FLAG_RESPONSE;
                    m1cp_send_frame(msg_type, fl, seq, s_tx + M1CP_HEADER_LEN, n);
                }
                else if (msg_type == M1CP_CMD_FW_VALIDATE)
                {
                    m1cp_send_nack(msg_type, seq, err);   /* read-only validator: no update session, no diagnostic */
                }
                else
                {
                    /* NEW: previously a bare payload[0], nothing else -- the
                     * STM32 path had no diagnostic channel at all. Same
                     * shared shape the ESP path already used (step+code+MD5),
                     * minus the ESP-specific secondary-UART counters (this
                     * path runs over the same USB-CDC link as everything
                     * else -- no analogous secondary link to report on). */
                    m1cp_send_update_nack(msg_type, seq, err, m1cp_fwupdate_last_diag(),
                                          m1cp_fwupdate_last_md5, false);
                }
            }
            else if ((msg_type >= 0x30U) && (msg_type <= 0x3FU))
            {
                /* ESP32 namespace: 0x30 = ESP_GET_VERSION (always available);
                 * 0x31..0x34 = the streamed esp_loader flash flow. Builds the
                 * response into s_tx; ESP32_UPDATE is currently advertised
                 * (M1CP_ESP_FLASH_IMPLEMENTED is defined -- see the
                 * capability gating comment near the top of this file). */
                uint16_t n   = 0U;
                /* Cross-domain exclusion (see the FS branch above) --
                 * ESP_GET_VERSION is a read-only query, left unguarded. */
                uint8_t  err = ((msg_type != M1CP_CMD_ESP_GET_VERSION) &&
                                (m1cp_fs_busy() || m1cp_fwupdate_busy()))
                             ? M1CP_ERR_BUSY
                             : m1cp_espupdate_handle(msg_type, payload, plen,
                                              s_tx + M1CP_HEADER_LEN, &n);
                if (err == M1CP_ERR_NONE)
                {
                    uint8_t fl = (n == 0U)
                               ? (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_ACK)
                               : (uint8_t)M1CP_FLAG_RESPONSE;
                    m1cp_send_frame(msg_type, fl, seq, s_tx + M1CP_HEADER_LEN, n);
                }
                else
                {
                    m1cp_send_update_nack(msg_type, seq, err, m1cp_espupdate_last_diag(),
                                          m1cp_espupdate_last_md5, true);
                }
            }
            else if (msg_type >= 0xF0U)
            {
                /* reserved-but-known namespace (async events) */
                m1cp_send_nack(msg_type, seq, M1CP_ERR_NOT_IMPLEMENTED);
            }
            else
            {
                m1cp_send_nack(msg_type, seq, M1CP_ERR_UNKNOWN_CMD);
            }
            break;
    }
}

/*************************** T X   H E L P E R S *****************************/

static void m1cp_send_frame(uint8_t msg_type, uint8_t flags, uint16_t seq,
                            const uint8_t *payload, uint16_t plen)
{
    uint16_t crc;
    uint16_t total;

    if (s_send == NULL)
    {
        return;
    }
    if (plen > M1CP_MAX_PAYLOAD)
    {
        plen = M1CP_MAX_PAYLOAD;
    }

    s_tx[0] = M1CP_MAGIC0;
    s_tx[1] = M1CP_MAGIC1;
    s_tx[2] = M1CP_MAGIC2;
    s_tx[3] = M1CP_MAGIC3;
    s_tx[4] = M1CP_VERSION;
    s_tx[5] = msg_type;
    s_tx[6] = flags;
    s_tx[7] = (uint8_t)(seq & 0xFFU);
    s_tx[8] = (uint8_t)((seq >> 8) & 0xFFU);
    s_tx[9] = (uint8_t)(plen & 0xFFU);
    s_tx[10] = (uint8_t)((plen >> 8) & 0xFFU);

    /* payload may already reside in s_tx+M1CP_HEADER_LEN (device info path);
     * only copy when it is an external buffer. */
    if ((payload != NULL) && (plen > 0U) && (payload != (s_tx + M1CP_HEADER_LEN)))
    {
        (void)memmove(s_tx + M1CP_HEADER_LEN, payload, plen);
    }

    crc = m1cp_crc16(&s_tx[M1CP_MAGIC_LEN],
                     (size_t)((M1CP_HEADER_LEN - M1CP_MAGIC_LEN) + plen));
    s_tx[M1CP_HEADER_LEN + plen]      = (uint8_t)(crc & 0xFFU);
    s_tx[M1CP_HEADER_LEN + plen + 1U] = (uint8_t)((crc >> 8) & 0xFFU);

    total = (uint16_t)(M1CP_HEADER_LEN + plen + M1CP_CRC_LEN);
    (void)s_send(s_tx, total);
}

static void m1cp_send_nack(uint8_t msg_type, uint16_t seq, uint8_t err)
{
    uint8_t e = err;
    m1cp_send_frame(msg_type, (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_NACK),
                    seq, &e, 1U);
}

/* Shared, transport-independent diagnostic NACK builder for BOTH update
 * targets (STM32 self-update and ESP32 radio update) -- previously ESP had
 * a rich, hand-built diagnostic body and STM32 had none at all (bare
 * payload[0]). Every M1CP update target now gets the same body shape:
 * err(1)|step(1)|code(1)|md5(16, all-zero if `last_md5` is NULL or reports
 * nothing available yet -- i.e. any failure before that target's own
 * finish() computed one) -- and, only for a target that actually has a
 * secondary hardware UART link worth diagnosing (today: just the ESP32
 * path, over UART4 to the radio; the STM32 path runs over the same USB-CDC
 * link as every other M1CP command, so there is no analogous secondary
 * link to report), a further uartDropCount(2)|uartOreCount(2). Older
 * clients read only payload[0] and ignore the rest. */
static void m1cp_send_update_nack(uint8_t msg_type, uint16_t seq, uint8_t err, uint16_t diag,
                                   bool (*last_md5)(uint8_t out[16]), bool include_esp_uart_counters)
{
    uint8_t  body[23];
    uint16_t blen;

    body[0] = err;
    body[1] = (uint8_t)(diag >> 8);    /* step */
    body[2] = (uint8_t)(diag & 0xFFU); /* backend-specific code */
    if ((last_md5 == NULL) || (!last_md5(&body[3]))) { memset(&body[3], 0, 16U); }
    blen = 19U;

    if (include_esp_uart_counters)
    {
        /* Real evidence for a genuine STM32-side UART RX bottleneck (ISR
         * writes one byte/interrupt into a 4KB ring buffer; if the
         * consuming task is ever preempted long enough, bytes are either
         * dropped by the ring buffer or lost to a hardware overrun) - the
         * exact mechanism that would explain a fully deterministic
         * mid-stream SLIP corruption without needing a time-based/watchdog
         * explanation. Cleared-on-read. */
        uint32_t drop_cnt = m1_esp32_get_and_clear_rx_drop_count();
        uint32_t ore_cnt  = m1_esp32_get_and_clear_ore_count();
        body[19] = (uint8_t)(drop_cnt >> 8);
        body[20] = (uint8_t)(drop_cnt & 0xFFU);
        body[21] = (uint8_t)(ore_cnt >> 8);
        body[22] = (uint8_t)(ore_cnt & 0xFFU);
        blen = 23U;
    }

    m1cp_send_frame(msg_type, (uint8_t)(M1CP_FLAG_RESPONSE | M1CP_FLAG_NACK), seq, body, blen);
}

/*************************** S T A T U S   A P I *****************************/

/* Publish operation domain/state/progress. Task context only. */
void m1cp_status_set(uint8_t domain, uint8_t op_state, uint8_t progress)
{
    s_op_domain   = domain;
    s_op_state    = op_state;
    s_op_progress = progress;
}

void m1cp_status_set_error(uint8_t err)
{
    s_last_error = err;
}

void m1cp_status_set_flag(uint8_t flag_bit, bool on)
{
    if (on) { s_status_flags |= flag_bit; }
    else    { s_status_flags = (uint8_t)(s_status_flags & (uint8_t)~flag_bit); }
}

void m1cp_status_set_resource(uint8_t present, const uint8_t ver[4])
{
    if ((present != 0U) && (ver != NULL))
    {
        s_resource_present = 1U;
        s_resource_ver[0] = ver[0];
        s_resource_ver[1] = ver[1];
        s_resource_ver[2] = ver[2];
        s_resource_ver[3] = ver[3];
    }
    else
    {
        s_resource_present = 0U;
        s_resource_ver[0] = 0xFFU; s_resource_ver[1] = 0xFFU;
        s_resource_ver[2] = 0xFFU; s_resource_ver[3] = 0xFFU;
    }
}

#ifndef M1CP_HOST_TEST
/* Little-endian u64 store. */
static void m1cp_put_u64le(uint8_t *p, uint64_t v)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++)
    {
        p[i] = (uint8_t)(v & 0xFFU);
        v >>= 8;
    }
}

/* Report cached SD capacity in bytes, but only when the device (not the USB-MSC
 * host) owns a mounted, healthy card and the cache has been populated. Uses the
 * cached kB getters (no FatFs call from this path). Returns false -> caller
 * writes the all-0xFF "unavailable" sentinel. */
static bool m1cp_sd_capacity(uint64_t *total, uint64_t *freeb)
{
    uint8_t owner = (usbmsc_sd_enable != 0U) ? 1U : 0U;

    if ((m1_sdcard_get_status() == SD_access_OK) && (owner == 0U))
    {
        uint32_t tot_kb = m1_sdcard_get_total_capacity();
        if (tot_kb > 0U)   /* 0 => never measured -> treat as unavailable */
        {
            *total = (uint64_t)tot_kb * 1024ULL;
            *freeb = (uint64_t)m1_sdcard_get_free_capacity() * 1024ULL;
            return true;
        }
    }
    return false;
}
#endif

/* Build the variable-length GET_STATUS payload (struct_version 0x01) into `out`.
 * Never fabricates: resource/ESP version and SD capacity use explicit
 * unavailable sentinels when not truthfully known. */
static uint16_t m1cp_build_status(uint8_t *out)
{
    uint16_t o = 0U;
    uint8_t  dev;

    (void)memset(out, 0, M1CP_STATUS_LEN);

    out[o++] = M1CP_STATUS_STRUCT_VER;              /* 0  struct_version        */

    if (s_op_state == M1CP_OP_FAILED)               { dev = M1CP_DEV_FAULT; }
    else if ((s_op_state != M1CP_OP_IDLE) &&
             (s_op_state != M1CP_OP_COMPLETE))      { dev = M1CP_DEV_BUSY; }
    else                                            { dev = M1CP_DEV_OK; }
    out[o++] = dev;                                 /* 1  device_state          */
    out[o++] = s_op_domain;                         /* 2  op_domain             */
    out[o++] = s_op_state;                          /* 3  op_state              */
    out[o++] = s_op_progress;                       /* 4  op_progress           */
    out[o++] = s_last_error;                        /* 5  last_error            */
    out[o++] = s_status_flags;                      /* 6  flags                 */

#ifndef M1CP_HOST_TEST
    out[o++] = (uint8_t)FW_VERSION_MAJOR;           /* 7..10 stm32 fw version   */
    out[o++] = (uint8_t)FW_VERSION_MINOR;
    out[o++] = (uint8_t)FW_VERSION_BUILD;
    out[o++] = (uint8_t)FW_VERSION_RC;
#else
    out[o++] = 0U; out[o++] = 8U; out[o++] = 0U; out[o++] = 4U;
#endif

    out[o++] = s_resource_present;                  /* 11 resource_present      */
    out[o++] = s_resource_ver[0];                   /* 12..15 resource_version  */
    out[o++] = s_resource_ver[1];
    out[o++] = s_resource_ver[2];
    out[o++] = s_resource_ver[3];

    {
        uint8_t ev[4];
        out[o++] = 1U;                              /* 16 esp32_present         */
        if (m1cp_esp_version_get(ev))
        {
            out[o++] = 1U;                          /* 17 esp32_ver_state valid */
            out[o++] = ev[0]; out[o++] = ev[1];     /* 18..21 esp32_fw_version  */
            out[o++] = ev[2]; out[o++] = ev[3];
        }
        else
        {
            out[o++] = 0U;                          /* 17 unavailable           */
            out[o++] = 0xFFU; out[o++] = 0xFFU;     /* 18..21 sentinel          */
            out[o++] = 0xFFU; out[o++] = 0xFFU;
        }
    }

#ifndef M1CP_HOST_TEST
    {
        uint8_t  present = (m1_sd_detected() != 0U) ? 1U : 0U;
        uint8_t  state;
        uint64_t tb = 0U, fb = 0U;

        out[o++] = present;                         /* 22 sd_present            */
        switch (m1_sdcard_get_status())             /* 23 sd_state              */
        {
            case SD_access_OK:        state = M1CP_SD_STATE_MOUNTED;   break;
            case SD_access_UnMounted: state = M1CP_SD_STATE_UNMOUNTED; break;
            case SD_access_NotReady:  state = M1CP_SD_STATE_NOTREADY;  break;
            case SD_access_NoFS:      state = M1CP_SD_STATE_NOFS;      break;
            case SD_access_NotOK:     state = M1CP_SD_STATE_NOTOK;     break;
            default:                  state = M1CP_SD_STATE_UNKNOWN;   break;
        }
        out[o++] = state;
        out[o++] = (usbmsc_sd_enable != 0U) ? 1U : 0U;   /* 24 sd_owner         */

        if (m1cp_sd_capacity(&tb, &fb))             /* 25..32 total, 33..40 free */
        {
            m1cp_put_u64le(&out[o], tb); o += 8U;
            m1cp_put_u64le(&out[o], fb); o += 8U;
        }
        else
        {
            uint8_t k; for (k = 0U; k < 16U; k++) { out[o++] = 0xFFU; }
        }

        {
            uint16_t ab = bl_get_active_bank();     /* 41 active_bank           */
            if (ab == BANK1_ACTIVE)      { out[o++] = 1U; }
            else if (ab == BANK2_ACTIVE) { out[o++] = 2U; }
            else                         { out[o++] = 0xFFU; }
        }
    }
#else
    out[o++] = 1U;                                  /* 22 sd_present            */
    out[o++] = M1CP_SD_STATE_UNMOUNTED;             /* 23 sd_state              */
    out[o++] = 0U;                                  /* 24 sd_owner              */
    { uint8_t k; for (k = 0U; k < 16U; k++) { out[o++] = 0xFFU; } }
    out[o++] = 1U;                                  /* 41 active_bank           */
#endif

    out[o++] = 1U;                                  /* 42 max_concurrent_xfer   */

    return o;                                       /* == M1CP_STATUS_LEN       */
}

/*************************** D E V I C E   I N F O ***************************/

/* Build the fixed 56-byte little-endian device-info payload into `out`.
 * Uses explicit unavailable sentinels rather than inventing values. */
static uint16_t m1cp_build_device_info(uint8_t *out)
{
    uint16_t o = 0U;
    uint32_t caps = M1CP_CAPS_ADVERTISED;

    (void)memset(out, 0, M1CP_DEVINFO_LEN);

    out[o++] = M1CP_VERSION;                 /* 0  protocol version              */
    out[o++] = M1CP_FAMILY_MONSTATEK_M1;     /* 1  device family                 */

#ifndef M1CP_HOST_TEST
    out[o++] = (uint8_t)FW_VERSION_MAJOR;    /* 2  fw major                      */
    out[o++] = (uint8_t)FW_VERSION_MINOR;    /* 3  fw minor                      */
    out[o++] = (uint8_t)FW_VERSION_BUILD;    /* 4  fw build                      */
    out[o++] = (uint8_t)FW_VERSION_RC;       /* 5  fw rc                         */
    out[o++] = 0xFFU;                        /* 6  hw revision: unknown at runtime */

    {
        uint32_t devid = (uint32_t)HAL_GetDEVID();   /* 7..8  STM32 device id LE */
        out[o++] = (uint8_t)(devid & 0xFFU);
        out[o++] = (uint8_t)((devid >> 8) & 0xFFU);
    }
    {
        uint32_t uid[3];                              /* 9..20 96-bit unique id  */
        uint8_t  w;
        uid[0] = HAL_GetUIDw0();
        uid[1] = HAL_GetUIDw1();
        uid[2] = HAL_GetUIDw2();
        for (w = 0U; w < 3U; w++)
        {
            out[o++] = (uint8_t)(uid[w] & 0xFFU);
            out[o++] = (uint8_t)((uid[w] >> 8) & 0xFFU);
            out[o++] = (uint8_t)((uid[w] >> 16) & 0xFFU);
            out[o++] = (uint8_t)((uid[w] >> 24) & 0xFFU);
        }
    }
    {
        uint16_t ab = bl_get_active_bank();           /* 21 active bank          */
        if (ab == BANK1_ACTIVE)       { out[o++] = 1U; }
        else if (ab == BANK2_ACTIVE)  { out[o++] = 2U; }
        else                          { out[o++] = 0xFFU; }
    }
    {
        uint32_t cap = (uint32_t)M1_FLASH_BANK_SIZE;   /* 22..25 bank capacity LE */
        out[o++] = (uint8_t)(cap & 0xFFU);
        out[o++] = (uint8_t)((cap >> 8) & 0xFFU);
        out[o++] = (uint8_t)((cap >> 16) & 0xFFU);
        out[o++] = (uint8_t)((cap >> 24) & 0xFFU);
    }
    {
        uint8_t present = (m1_sd_detected() != 0U) ? 1U : 0U;  /* 26 sd present  */
        uint8_t state;
        uint8_t owner;
        out[o++] = present;
        switch (m1_sdcard_get_status())               /* 27 sd mount state       */
        {
            case SD_access_OK:        state = M1CP_SD_STATE_MOUNTED;   break;
            case SD_access_UnMounted: state = M1CP_SD_STATE_UNMOUNTED; break;
            case SD_access_NotReady:  state = M1CP_SD_STATE_NOTREADY;  break;
            case SD_access_NoFS:      state = M1CP_SD_STATE_NOFS;      break;
            case SD_access_NotOK:     state = M1CP_SD_STATE_NOTOK;     break;
            default:                  state = M1CP_SD_STATE_UNKNOWN;   break;
        }
        out[o++] = state;
        owner = (usbmsc_sd_enable != 0U) ? 1U : 0U;   /* 28 owner: 0=dev 1=host  */
        out[o++] = owner;
    }
#else
    out[o++] = 0U; out[o++] = 8U; out[o++] = 0U; out[o++] = 4U;  /* 2..5 fw     */
    out[o++] = 0xFFU;                                            /* 6 hw rev    */
    out[o++] = 0x84U; out[o++] = 0x04U;                          /* 7..8 devid  */
    { uint8_t k; for (k = 0U; k < 12U; k++) { out[o++] = (uint8_t)(0xA0U + k); } } /* 9..20 uid */
    out[o++] = 1U;                                               /* 21 bank     */
    out[o++] = 0x00U; out[o++] = 0x00U; out[o++] = 0x10U; out[o++] = 0x00U; /* 22..25 1MB */
    out[o++] = 1U;                                               /* 26 sd pres  */
    out[o++] = M1CP_SD_STATE_UNMOUNTED;                          /* 27 sd state */
    out[o++] = 0U;                                               /* 28 owner    */
#endif

    /* 29..36 sd total bytes, 37..44 sd free bytes. Real cached values when the
     * device owns a mounted, measured card; otherwise the all-0xFF sentinel
     * (decoder already treats that as "unavailable"). */
#ifndef M1CP_HOST_TEST
    {
        uint64_t tb = 0U, fb = 0U;
        if (m1cp_sd_capacity(&tb, &fb))
        {
            m1cp_put_u64le(&out[o], tb); o += 8U;
            m1cp_put_u64le(&out[o], fb); o += 8U;
        }
        else
        {
            uint8_t k; for (k = 0U; k < 16U; k++) { out[o++] = 0xFFU; }
        }
    }
#else
    { uint8_t k; for (k = 0U; k < 16U; k++) { out[o++] = 0xFFU; } }
#endif

    out[o++] = 1U;                            /* 45 esp32 present (onboard)       */
    {                                         /* 46..49 esp32 fw version          */
        uint8_t ev[4];
        if (m1cp_esp_version_get(ev))         /* persistent record (Gate D)       */
        {
            out[o++] = ev[0]; out[o++] = ev[1];
            out[o++] = ev[2]; out[o++] = ev[3];
        }
        else
        {
            out[o++] = 0xFFU; out[o++] = 0xFFU;   /* unavailable sentinel         */
            out[o++] = 0xFFU; out[o++] = 0xFFU;
        }
    }

    out[o++] = (uint8_t)(caps & 0xFFUL);      /* 50..53 capability flags LE       */
    out[o++] = (uint8_t)((caps >> 8) & 0xFFUL);
    out[o++] = (uint8_t)((caps >> 16) & 0xFFUL);
    out[o++] = (uint8_t)((caps >> 24) & 0xFFUL);

    out[o++] = (uint8_t)(M1CP_MAX_PAYLOAD & 0xFFU);       /* 54..55 max payload LE */
    out[o++] = (uint8_t)((M1CP_MAX_PAYLOAD >> 8) & 0xFFU);

    return o; /* == M1CP_DEVINFO_LEN */
}
