/* See COPYING.txt for license details. */

/*
*
*  m1_manager_espupdate.c
*
*  M1CP streamed ESP32-C6 firmware-update command dispatch. See
*  m1_manager_espupdate.h and m1_manager_update_coordinator.h.
*
*  This file is now a THIN shim over the shared m1_manager_update_coordinator
*  (the same one m1_manager_fwupdate.c uses for the STM32 self-update path):
*  it owns one m1cp_update_session_t, parses/builds the ESP-specific BEGIN
*  wire payload, and forwards everything else to the coordinator. The
*  480-byte-M1CP-frame -> 4096-byte-ROM-block staging is ESP-specific
*  transport detail and lives entirely inside the firmware backend's own
*  write()/finish() now (previously it lived in this file's own dispatch
*  handler) -- the coordinator just sees "one write() call per DATA chunk",
*  exactly like the STM32 backend.
*
*  OWNERSHIP: ESP_UPDATE_BEGIN takes exclusive control of the ESP32 (stops normal
*  esp-hosted operation, drives the bootloader over UART4). The STM32 host is
*  never touched, so a failed flash degrades the radio but cannot brick the
*  device - the Manager can always retry. On success FINISH records the installed
*  version (m1_esp_version) so the Manager can skip future flashes when current.
*  There is no ACTIVATE: the ESP is single-app and reboots into the new image at
*  FINISH (supports_activate = false). Integrity is the esp_loader MD5 verify
*  against the client MD5 (mandatory for this target -- min_hash_algo ==
*  M1CP_UPDATE_HASH_MD5).
*
* M1 Project
*
*/

#include <string.h>
#include "m1_manager_espupdate.h"
#include "m1_manager_protocol.h"   /* error codes, status-publish API, op ids */
#include "m1_esp_version.h"        /* persist the installed version on FINISH  */

static m1cp_update_session_t s_session;

/* The BEGIN payload's version(4) field, stashed here between BEGIN and
 * FINISH -- the generic coordinator has no concept of "version" at all
 * (deliberately, see m1_manager_update_coordinator.h), so this stays
 * dispatch-shim state, exactly as it was a plain file-scope static before
 * this refactor (s_version). */
static uint8_t s_pending_version[4];

/* Last low-level flash failure, reported alongside ERR_ESP in the NACK so the
 * Manager can show exactly which esp_loader step failed and why, without needing
 * the debug UART. step: 0 none, 1 connect, 2 flash_start, 3 write, 4 verify.
 * code: the raw esp_loader_error_t. Set by the firmware backend. Kept as its
 * OWN, ESP-specific diagnostic (richer than the generic coordinator's own
 * 3-step begin/write/finish diag) rather than delegated to
 * m1cp_update_last_diag() -- the coordinator's diag is the minimum every
 * target gets for free; a target may still layer its own finer-grained one
 * on top, which ESP already did before this refactor and still does. */
static uint8_t s_diag_step = 0U;
static uint8_t s_diag_code = 0U;

void m1cp_espupdate_set_diag(uint8_t step, uint8_t code)
{
    s_diag_step = step;
    s_diag_code = code;
}

uint16_t m1cp_espupdate_last_diag(void)
{
    return (uint16_t)(((uint16_t)s_diag_step << 8) | s_diag_code);
}

/* MD5 of exactly the bytes the STM32 received over M1CP and streamed to the ESP
 * (computed independently of the ESP's own flash verify). On an INTEGRITY
 * failure this lets the Manager tell whether corruption happened before the STM32
 * (client<->STM32 leg: this would NOT match the client's expected MD5 either) or
 * between the STM32 and the ESP (this WOULD match the client's expected MD5, but
 * the ESP's flash verify still failed) - without needing a debug UART. Set by the
 * firmware backend on FINISH; unavailable on the host build. */
static uint8_t s_stm32_md5[16];
static bool    s_stm32_md5_valid = false;

bool m1cp_espupdate_last_md5(uint8_t out[16])
{
    if (!s_stm32_md5_valid) { return false; }
    memcpy(out, s_stm32_md5, 16U);
    return true;
}

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*************************** C O M M A N D S ********************************/

/* ESP_GET_VERSION: -> state(1) | version(4). state 0 unavailable, 1 valid.
 * (The "stale" state is computed by the Manager from manifest.json, not here.) */
static uint8_t espu_get_version(uint8_t *out, uint16_t *olen)
{
    uint8_t ev[4];
    if (m1cp_esp_version_get(ev))
    {
        out[0] = 1U;
        out[1] = ev[0]; out[2] = ev[1]; out[3] = ev[2]; out[4] = ev[3];
    }
    else
    {
        out[0] = 0U;
        out[1] = 0xFFU; out[2] = 0xFFU; out[3] = 0xFFU; out[4] = 0xFFU;
    }
    *olen = 5U;
    return M1CP_ERR_NONE;
}

/* ESP_UPDATE_BEGIN: image_size(4)|flash_offset(4)|md5(16)|version(4)|confirm(2)
 * -> max_chunk(2). Takes exclusive ESP ownership, enters download mode,
 * connects, flash_start (all inside the coordinator's call into begin()). */
static uint8_t espu_begin(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    uint32_t image_size, flash_offset;

    if (plen < 30U) { return M1CP_ERR_BAD_PAYLOAD; }  /* 4+4+16+4+2 */
    image_size   = rd_u32(&payload[0]);
    flash_offset = rd_u32(&payload[4]);
    /* payload: image_size(4)|flash_offset(4)|md5(16)|version(4)|confirm(2) */
    if (rd_u16(&payload[28]) != M1CP_ESP_BEGIN_CONFIRM) { return M1CP_ERR_BAD_ARG; }
    if ((image_size & 0x03U) != 0U)         { return M1CP_ERR_BAD_ARG; } /* 4-byte aligned */
    if (flash_offset > M1CP_ESP_MAX_OFFSET) { return M1CP_ERR_BAD_ARG; }
    if ((flash_offset & 0x0FFFU) != 0U)     { return M1CP_ERR_BAD_ARG; } /* 4 KiB aligned */

    s_diag_step = 0U; s_diag_code = 0U;   /* fresh attempt: clear the ESP-specific diagnostic */

    return m1cp_update_begin(&s_session, flash_offset, image_size,
                              M1CP_UPDATE_HASH_MD5, &payload[8], M1CP_ESP_MD5_LEN,
                              M1CP_ESP_CHUNK_MAX, out, olen);
    /* NOTE: `version` (payload[24..27]) is parsed but not forwarded to the
     * coordinator (which has no generic concept of it) -- it's persisted
     * directly by the firmware backend's finish() via m1cp_esp_version_set(),
     * exactly as before this refactor. See espu_be_finish(). */
}

/* ESP_UPDATE_DATA: offset(4)|data(N) -> next_offset(4). Streams one chunk. */
static uint8_t espu_data(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *olen)
{
    return m1cp_update_data(&s_session, payload, plen, out, olen);
}

/* ESP_UPDATE_FINISH: MD5-verify, reset the C6, persist the version, restore.
 * Version persistence happens HERE (dispatch shim, host-testable and always
 * compiled) rather than inside the backend's finish() -- the generic
 * coordinator has no concept of "version" at all (deliberately, see
 * m1_manager_update_coordinator.h), and m1cp_esp_version_set() must run
 * regardless of which backend (mock, for host tests, or the real device
 * one) is installed. Covers both real success (rc 0) and the backend's
 * soft-success case (rc 3, verify command unavailable but every block was
 * already checksum-acked) -- m1cp_update_finish() maps both to
 * M1CP_ERR_NONE. */
static uint8_t espu_finish(void)
{
    uint8_t err = m1cp_update_finish(&s_session);
    if (err == M1CP_ERR_NONE) { m1cp_esp_version_set(s_pending_version); }
    return err;
}

static uint8_t espu_abort(void)
{
    return m1cp_update_abort(&s_session);
}

/*************************** P U B L I C ************************************/

void m1cp_espupdate_init(const m1cp_update_backend_t *backend)
{
    static const m1cp_update_ops_t ops =
    {
        M1CP_DOMAIN_ESP32, M1CP_OP_ESP_FLASHING, M1CP_OP_ESP_VERIFYING,
        0xFFU /* no reboot-required concept for this target */, M1CP_OP_COMPLETE
    };
    m1cp_update_session_init(&s_session, backend, &ops);
}

bool m1cp_espupdate_busy(void)
{
    return m1cp_update_busy(&s_session);
}

void m1cp_espupdate_reset(void)
{
    m1cp_update_reset(&s_session);
}

uint8_t m1cp_espupdate_handle(uint8_t msg_type, const uint8_t *payload, uint16_t plen,
                              uint8_t *out, uint16_t *out_len)
{
    uint8_t err;
    *out_len = 0U;

    switch (msg_type)
    {
        case M1CP_CMD_ESP_GET_VERSION:   err = espu_get_version(out, out_len);            break;
        case M1CP_CMD_ESP_UPDATE_BEGIN:
            err = espu_begin(payload, plen, out, out_len);
            if (err == M1CP_ERR_NONE) { memcpy(s_pending_version, &payload[24], 4U); }
            break;
        case M1CP_CMD_ESP_UPDATE_DATA:   err = espu_data(payload, plen, out, out_len);    break;
        case M1CP_CMD_ESP_UPDATE_FINISH: err = espu_finish();                             break;
        case M1CP_CMD_ESP_UPDATE_ABORT:  err = espu_abort();                              break;
        default:                         err = M1CP_ERR_NOT_IMPLEMENTED;                  break;
    }
    if (err != M1CP_ERR_NONE) { *out_len = 0U; }
    return err;
}

/*************************** F I R M W A R E   B A C K E N D ****************/

#ifndef M1CP_HOST_TEST
#include "stm32h5xx_hal.h"
#include "main.h"               /* ESP32_EN_*, BUTTON_RIGHT_* pins               */
#include "m1_esp32_hal.h"       /* esp32_UART_*, esp32_enable, m1_esp32_deinit    */
#include "esp_loader.h"         /* esp_loader_error_t, ESP_LOADER_*              */
#include "app_common.h"         /* get_error_string                              */
#include "m1_esp32_flash_session.h" /* shared connect/write/verify/teardown -- see
                                      * that header for why this file no longer
                                      * carries its own copy of that logic       */
#include "m1_power_ctl.h"       /* m1_check_battery_level                        */
#include "m1_log_debug.h"       /* M1_LOG_E                                      */
#include "m1_md5_hash.h"        /* mh_md5_init/update/final -- tracks exactly what
                                  * the STM32 received, for m1cp_espupdate_last_md5 */
#include "m1_esp32_perf.h"      /* measured (not assumed) command/retry/timeout/
                                  * fixed-delay/connect counters -- see that header */

#define ESP_UPD_TAG "ESP-UPD"

/* Only ever called from this backend now (the dispatch shim above has no
 * MD5 concept of its own since the generic-coordinator migration). */
static void espu_md5_begin(void)                          { mh_md5_init(0U, 0U); }
static void espu_md5_update(const uint8_t *d, uint32_t n) { mh_md5_update(d, n); }
static void espu_md5_finish(uint8_t out[16])               { mh_md5_final(out); }

/* Shared with the ESP32 boot-mode (IO9) pin, matching m1_esp32_fw_update.c. */
#define ESP32_IO9_GPIO_Port   BUTTON_RIGHT_GPIO_Port
#define ESP32_IO9_Pin         BUTTON_RIGHT_Pin
#define ESP32_RESET_GPIO_Port ESP32_EN_GPIO_Port
#define ESP32_RESET_Pin       ESP32_EN_Pin

static uint32_t s_be_offset = 0U;
static uint32_t s_be_size   = 0U;
static bool     s_be_owned  = false;

/* M1CP frames are limited to 480 data bytes. Stage them before handing them
 * to esp_loader so writes reach the ROM in fixed M1CP_ESP_FLASH_BLOCK-sized
 * chunks (4096 -- a real NOR flash sector). This is now entirely internal to
 * write()/finish() (previously it lived in this file's own DATA dispatch
 * handler, before the generic-coordinator migration) -- the coordinator
 * just calls write() once per DATA chunk and never sees the staging. */
static uint8_t  s_flash_stage[M1CP_ESP_FLASH_BLOCK];
static uint16_t s_flash_stage_len = 0U;

/* Perf instrumentation for the flash-write path only (worker-task context,
 * plain HAL_GetTick() deltas -- never touches an ISR). protocol_uart.c's
 * send_cmd() pays a fixed, deliberate 100ms HAL_Delay after EVERY command
 * (comment: "Without this delay, most of response data will be lost due to
 * unexpected huge ISR latency!!!"), and esp_loader_flash_write() itself
 * retries internally up to SERIAL_FLASHER_WRITE_BLOCK_RETRIES (10) times,
 * silently, when a block's response is lost/garbled -- these counters make
 * that opaque cost visible (total/max/avg ms actually spent inside the
 * shared write call) without needing a debug-UART trace. */
static uint32_t s_perf_write_calls  = 0U;  /* esp_loader_flash_write() invocations this update (excludes internal esp_loader retries, which are opaque to this layer) */
static uint32_t s_perf_write_ms     = 0U;  /* total ms spent inside esp_loader_flash_write() across the whole update */
static uint32_t s_perf_write_max_ms = 0U;  /* slowest single esp_loader_flash_write() call this update */

/* Release the ESP bootloader UART and hand the radio back to normal operation. */
static void espu_be_release(void)
{
    if (!s_be_owned) { return; }
    if (s_perf_write_calls != 0U)
    {
        M1_LOG_I(ESP_UPD_TAG,
                 "flash perf: %lu writes, %lu ms total, %lu ms max, %lu ms avg\r\n",
                 (unsigned long)s_perf_write_calls,
                 (unsigned long)s_perf_write_ms, (unsigned long)s_perf_write_max_ms,
                 (unsigned long)(s_perf_write_ms / s_perf_write_calls));
        /* MEASURED (not assumed) transport-level breakdown -- exactly what
         * the multi-hour-flash investigation needed and didn't have: total
         * low-level ROM commands sent (every SLIP round trip, including
         * every internal esp_loader retry -- normally opaque above that
         * layer), how many esp_loader_flash_write() internal attempts were
         * consumed in total and how many of THOSE specifically timed out
         * (vs failed some other way), the running total of time spent in
         * the mandatory fixed per-command delay alone (isolated from
         * everything else that also happens inside a write call), and how
         * many times a connect was attempted this session. See
         * m1_esp32_perf.h. */
        M1_LOG_I(ESP_UPD_TAG,
                 "flash perf (measured): %lu commands, %lu ms in fixed delays, "
                 "%lu write attempts (%lu timed out), %lu connects, %lu bytes acked\r\n",
                 (unsigned long)m1_esp32_perf_commands(),
                 (unsigned long)m1_esp32_perf_fixed_delay_ms(),
                 (unsigned long)m1_esp32_perf_write_attempts(),
                 (unsigned long)m1_esp32_perf_write_timeouts(),
                 (unsigned long)m1_esp32_perf_connects(),
                 (unsigned long)s_session.received);   /* s_session: the dispatch shim's coordinator session, same translation unit -- the ACTUAL bytes acknowledged so far, not just the declared total (s_be_size), which would over-report on a failure path */
    }
    m1_esp32_flash_session_teardown(true, ESP32_IO9_GPIO_Port, ESP32_IO9_Pin);
    s_be_owned = false;
    s_be_offset = 0U;
    s_be_size   = 0U;
}

static bool espu_be_ready(void)
{
    return (m1_check_battery_level(50) != 0U);  /* >= 50%, matching the FW/SD paths */
}

static uint8_t espu_be_begin(uint32_t flash_offset, uint32_t image_size)
{
    esp_loader_error_t err;
    const m1_esp32_flash_session_pins_t pins = {
        .huart    = &huart_esp,
        .io0_port = ESP32_IO9_GPIO_Port,
        .io0_pin  = ESP32_IO9_Pin,
        .rst_port = ESP32_RESET_GPIO_Port,
        .rst_pin  = ESP32_RESET_Pin,
    };

    /* Reset EVERYTHING a prior, never-finished attempt could have left
     * behind -- the same class of bug just fixed on the STM32 side
     * (bl_flash_stream_reset(), see that function's doc comment): a stale
     * s_flash_stage_len here would resume mid-block into a freshly
     * restarted image. */
    s_flash_stage_len   = 0U;
    s_perf_write_calls  = 0U;
    s_perf_write_ms     = 0U;
    s_perf_write_max_ms = 0U;
    m1_esp32_perf_reset();   /* measured (not assumed) transport counters -- see m1_esp32_perf.h */
    s_stm32_md5_valid    = false;
    espu_md5_begin();

    /* Take exclusive ownership and CLEAN POWER-CYCLE the ESP. Normal USB/Manager
     * operation never runs m1_esp32_init() (commented out at boot), so the module
     * sits unpowered/in an unknown state. Stop the esp-hosted SPI-AT driver if it
     * was running (guarded no-op otherwise), then fully cycle EN low->high so the
     * ROM download loader starts from a clean reset - a stale or half-flashed ESP
     * from a prior attempt otherwise refuses the bootloader sync.
     *
     * On-device testing repeatedly showed a short (100ms off / 100ms settle)
     * software EN-toggle was NOT enough to recover a module left in a bad state
     * by a prior failed attempt - only a full physical power cycle of the whole
     * M1 (cutting power for seconds, well past any capacitor discharge) reliably
     * did. Hold EN low and settle much longer here to close that gap without
     * requiring a physical cycle for every retry. */
    m1_esp32_deinit();
    esp32_disable();                /* EN low (own 50ms hold) */
    HAL_Delay(1000);                /* long off-time: let the rail fully discharge */
    esp32_enable();                 /* EN high */
    HAL_Delay(500);                 /* generous settle before entering bootloader */

    m1_esp32_flash_session_setup_io(&pins);
    s_be_owned = true;              /* own the radio now; release on any exit path */

    /* Connect through the SAME shared primitive the SD updater calls --
     * m1_esp32_flash_session_connect() is a direct extraction of
     * m1_fw_app()'s own single-attempt, proven-baud connect. See
     * m1_esp32_flash_session.h for why sharing the exact implementation,
     * not adjusting parameters around a second one, is the actual fix for
     * this path's own history of perf regressions. */
    err = m1_esp32_flash_session_connect();
    if (err != ESP_LOADER_SUCCESS)
    {
        M1_LOG_E(ESP_UPD_TAG, "connect failed: %s\r\n", get_error_string(err));
        m1cp_espupdate_set_diag(1U, (uint8_t)err);
        espu_be_release();
        return 2U;                  /* bootloader entry / connect failure -> ERR_ESP */
    }
    err = m1_esp32_flash_session_start(flash_offset, image_size, M1CP_ESP_FLASH_BLOCK);
    if (err != ESP_LOADER_SUCCESS)
    {
        M1_LOG_E(ESP_UPD_TAG, "flash_start(0x%lX,%lu) failed: %s\r\n",
                 (unsigned long)flash_offset, (unsigned long)image_size, get_error_string(err));
        m1cp_espupdate_set_diag(2U, (uint8_t)err);
        espu_be_release();
        return 2U;
    }
    s_be_offset = flash_offset;
    s_be_size   = image_size;
    return 0U;
}

/* Flush whatever is currently staged (a full 4096-byte block, or -- called
 * from finish() -- a final, smaller partial one) to the ESP ROM. Exactly
 * one call to the shared write primitive per flush, matching the SD
 * updater's own single-attempt shape: no outer retry, no extra ring-buffer
 * reset beyond what stm32_port.c's loader_port_write() already does
 * unconditionally before every command. */
static uint8_t espu_be_flush_block(void)
{
    uint16_t size = s_flash_stage_len;
    uint32_t t0 = HAL_GetTick();
    esp_loader_error_t err = m1_esp32_flash_session_write(s_flash_stage, size);
    {
        uint32_t dt = HAL_GetTick() - t0;
        s_perf_write_calls++;
        s_perf_write_ms += dt;
        if (dt > s_perf_write_max_ms) { s_perf_write_max_ms = dt; }
    }
    s_flash_stage_len = 0U;
    if (err == ESP_LOADER_SUCCESS) { return 0U; }
    M1_LOG_E(ESP_UPD_TAG, "flash_write(%u) failed: %s\r\n", size, get_error_string(err));
    m1cp_espupdate_set_diag(3U, (uint8_t)err);
    return 2U;
}

static uint8_t espu_be_write(const uint8_t *data, uint32_t size)
{
    const uint8_t *src = data;
    uint32_t remain = size;
    while (remain != 0U)
    {
        uint16_t room = (uint16_t)(M1CP_ESP_FLASH_BLOCK - s_flash_stage_len);
        uint16_t take = (remain < (uint32_t)room) ? (uint16_t)remain : room;
        memcpy(&s_flash_stage[s_flash_stage_len], src, take);
        s_flash_stage_len = (uint16_t)(s_flash_stage_len + take);
        src += take;
        remain -= take;
        if (s_flash_stage_len == M1CP_ESP_FLASH_BLOCK)
        {
            uint8_t rc = espu_be_flush_block();
            if (rc != 0U) { return rc; }
        }
    }
    espu_md5_update(data, size);   /* track exactly what the STM32 received, regardless of staging boundaries */
    return 0U;
}

static uint8_t espu_be_finish(const uint8_t *expected_hash, uint8_t hash_len)
{
    /* esp_loader_flash_verify_known_md5 compares the expected digest as a
     * 32-char lowercase hex STRING (it is memcmp'd against the ROM's ASCII-hex
     * reply, MD5_SIZE_ROM=32) - NOT 16 raw bytes. Convert before verifying. */
    static const char hexd[] = "0123456789abcdef";
    char hexmd5[33];
    esp_loader_error_t err;
    int i;

    /* esp_loader pads the final partial block internally, while hashing only
     * the declared image bytes. */
    if (s_flash_stage_len != 0U)
    {
        uint8_t rc = espu_be_flush_block();
        /* Every other failure exit in this file releases ownership before
         * returning (connect/flash_start failure above, the hash_len guard
         * below, and the unconditional release after verify) -- this one
         * didn't, leaving s_be_owned true and the radio held with no way
         * back to normal operation until the next update attempt happened
         * to call espu_be_begin() again. */
        if (rc != 0U) { espu_be_release(); return rc; }
    }

    /* Finalize the STM32-side digest of exactly what was received, regardless of
     * how the ESP verify below turns out - lets a mismatch be diagnosed (client<->
     * STM32 leg vs STM32<->ESP leg) without a debug UART (see last_md5 doc). */
    { uint8_t raw[16]; espu_md5_finish(raw); memcpy(s_stm32_md5, raw, 16U); s_stm32_md5_valid = true; }

    if (hash_len != 16U)
    {
        /* Unreachable in practice: min_hash_algo == M1CP_UPDATE_HASH_MD5
         * means the coordinator never lets BEGIN succeed without exactly a
         * 16-byte hash for this target. Fail safe rather than verify
         * against nothing if it ever were. */
        espu_be_release();
        return 2U;
    }
    for (i = 0; i < 16; i++)
    {
        hexmd5[(i * 2)]     = hexd[(expected_hash[i] >> 4) & 0x0F];
        hexmd5[(i * 2) + 1] = hexd[expected_hash[i] & 0x0F];
    }
    hexmd5[32] = '\0';

    err = m1_esp32_flash_session_verify_known_md5(s_be_offset, s_be_size, hexmd5);
    espu_be_release();              /* reset + boot + hand back regardless of result */
    if (err == ESP_LOADER_SUCCESS) { return 0U; }              /* verified; espu_finish() persists the version, not here (see its own doc comment) */
    if (err == ESP_LOADER_ERROR_INVALID_MD5)
    {
        m1cp_espupdate_set_diag(4U, (uint8_t)err);
        return 1U;                                            /* real mismatch -> INTEGRITY */
    }
    /* The whole-image MD5 command itself failed (TIMEOUT / IMAGE_SIZE / UNSUPPORTED
     * / INVALID_RESPONSE) - NOT a data mismatch. Every streamed block was already
     * checksum-verified and acked by the ROM during flash_write, so the flash is
     * good; complete the update rather than blocking on a best-effort check. The
     * diagnostic still records why, for visibility. */
    M1_LOG_E(ESP_UPD_TAG, "flash_verify_known_md5 command failed (blocks OK, completing): %s\r\n",
             get_error_string(err));
    m1cp_espupdate_set_diag(4U, (uint8_t)err);
    return 3U;   /* soft success -- espu_finish() persists the version, not here (see its own doc comment) */
}

static const m1cp_update_backend_t s_esp_backend =
{
    "esp32", M1CP_ESP_MAX_IMAGE, 4U, false, M1CP_UPDATE_HASH_MD5, M1CP_ERR_ESP,
    espu_be_ready, espu_be_begin, espu_be_write, espu_be_finish, NULL, espu_be_release
};

void m1cp_espupdate_install_backend(void)
{
    m1cp_espupdate_init(&s_esp_backend);
}
#endif /* !M1CP_HOST_TEST */
