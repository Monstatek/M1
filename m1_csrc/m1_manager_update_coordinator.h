/* See COPYING.txt for license details. */

/*
*
*  m1_manager_update_coordinator.h
*
*  Generic, transport- and firmware-content-agnostic package/transfer state
*  machine shared by every M1CP firmware-update target. Firmware CONTENTS are
*  opaque to this module: it validates a small MANIFEST (target image size
*  bound, an optional client-supplied hash algorithm + digest, a flash
*  offset) and moves opaque bytes from M1CP DATA frames to a per-target
*  BACKEND (m1cp_update_backend_t) -- it never interprets the image itself.
*  A future firmware release needs a new artifact + a new manifest (size,
*  hash, version) -- never a change to this file.
*
*  Design history: m1_csrc/m1_manager_fwupdate.c (STM32) and
*  m1_csrc/m1_manager_espupdate.c (ESP32) each independently implemented
*  their own copy of this exact shape -- BEGIN/DATA/FINISH/ABORT dispatch,
*  offset tracking, chunk-alignment validation, progress publishing, error
*  reporting -- and had DRIFTED: the ESP path had gained (through hardware-
*  driven fixes) a resend-idempotency guard for a lost-response retry and a
*  rich diagnostic NACK; the STM32 path had neither, plus its own backend
*  carried a genuine bug (a write cursor that was never reset on a fresh
*  BEGIN after an aborted transfer -- see m1_fw_update_bl.h's
*  bl_flash_stream_reset() doc comment). Extracting the shared shape into
*  this one module means a fix or a hardening made for one target is
*  structurally available to every other target, present or future,
*  instead of needing to be independently rediscovered per target the way
*  the resend-idempotency fix had to be.
*
*  A future target -- e.g. an ESP32 SPI transport instead of the current
*  UART one -- is a NEW m1cp_update_backend_t implementation. Nothing here,
*  and nothing in the M1CP command-dispatch shim that owns a
*  m1cp_update_session_t, changes.
*
* M1 Project
*
*/

#ifndef M1_MANAGER_UPDATE_COORDINATOR_H_
#define M1_MANAGER_UPDATE_COORDINATOR_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Room for a future SHA-256 (32 bytes) without a struct-layout change; only
 * MD5 (16 bytes) is actually implemented today -- see m1cp_update_hash_algo_t. */
#define M1CP_UPDATE_HASH_MAX_LEN 32U

typedef enum
{
    M1CP_UPDATE_HASH_NONE = 0U,  /* no client-supplied hash; a backend with its
                                  * own independent self-check (STM32's embedded
                                  * hardware CRC32) is still authoritative */
    M1CP_UPDATE_HASH_MD5  = 1U   /* 16 bytes -- the only algorithm implemented
                                  * on-device today (m1_md5_hash.c). Adding
                                  * SHA-256 later is a new enum value + a new
                                  * case in m1cp_update_hash_len_for_algo()/
                                  * m1cp_update_hash_verify() (m1_manager_update_coordinator.c)
                                  * -- not a wire-format or backend-interface change. */
} m1cp_update_hash_algo_t;

typedef enum
{
    M1CP_UPD_IDLE = 0,
    M1CP_UPD_RECEIVING,   /* backend prepared (erased/connected); streaming chunks   */
    M1CP_UPD_VERIFIED,    /* hash/CRC verified; awaiting activation (be->supports_activate only) */
    M1CP_UPD_ACTIVATING   /* ACK sent; be->activate() deferred to the caller's task loop */
} m1cp_update_state_t;

/* Small, REPLACEABLE per-target backend -- the only thing that differs
 * between updating the STM32 itself and updating the ESP32 radio (or any
 * future target). Every function here is a single-attempt primitive: no
 * internal retry loop belongs inside a backend beyond what its own
 * underlying driver already does (matching the proven SD-card ESP updater's
 * own single-attempt shape) -- a caller wanting a retry policy applies it
 * OUTSIDE, wrapping these calls, so no backend's own notion of "one write"
 * or "one begin" can silently drift from another's. */
typedef struct
{
    const char *name;              /* logging only */
    uint32_t    max_image_size;
    uint32_t    chunk_alignment;   /* every non-final DATA chunk's size must be a multiple of this */
    bool        supports_activate; /* true: FINISH only verifies, a separate ACTIVATE call commits it (STM32); false: FINISH itself completes the update (ESP32, single-app) */
    uint8_t     min_hash_algo;     /* M1CP_UPDATE_HASH_NONE if the backend accepts an unauthenticated image (self-check only); M1CP_UPDATE_HASH_MD5 if a client hash is mandatory (ESP, matching its proven behavior) */
    uint8_t     hard_err_code;     /* the M1CP_ERR_* to report for a begin()/finish() failure that isn't a plain "busy" (rc==1 from begin) or a hash mismatch (rc==1 from finish) -- M1CP_ERR_FLASH for STM32, M1CP_ERR_ESP for ESP32 */

    bool     (*ready)(void);
    /* Take ownership, erase/connect/prepare. Returns 0 ok / 1 busy (->
     * M1CP_ERR_BUSY, no abort() call needed) / other non-zero -> hard_err_code
     * (abort() IS called by the coordinator regardless, defensively -- a
     * backend whose begin() already self-cleans on failure, like the ESP
     * one, must make that release idempotent so this second call is a
     * harmless no-op; see espu_be_release()'s own s_be_owned guard).
     * MUST reset any of the backend's OWN internal write-cursor state
     * itself, unconditionally, on every call -- the coordinator does not
     * assume how (or whether) a backend tracks its own destination
     * address, only that begin() is safe to call again after a prior begin
     * that never reached a successful finish(). */
    uint8_t  (*begin)(uint32_t flash_offset, uint32_t image_size);
    /* Program exactly one contiguous chunk. */
    uint8_t  (*write)(const uint8_t *data, uint32_t size);
    /* Finalize + verify. expected_hash/hash_len are NULL/0 when the manifest
     * carried M1CP_UPDATE_HASH_NONE -- a backend with its own self-check
     * (STM32's embedded CRC32) still runs that regardless. Returns 0
     * verified / 1 genuine mismatch (-> M1CP_ERR_INTEGRITY) / 2 hard error
     * (-> hard_err_code) / 3 soft success (verification itself couldn't run,
     * but every block was already individually acked during write() --
     * treated as success, matching the ESP path's existing behavior). MUST
     * leave the backend in a clean, released state on EVERY outcome --
     * the coordinator never calls abort() after finish() returns. */
    uint8_t  (*finish)(const uint8_t *expected_hash, uint8_t hash_len);
    /* Only called when supports_activate && state == VERIFIED. May not
     * return (a device reset). NULL when !supports_activate. */
    void     (*activate)(void);
    /* Release/cleanup for every exit path that ISN'T finish() running to
     * completion: a genuine (non-resend) DATA-phase error, a begin()
     * failure, or an explicit abort/reset. NULL if there is truly nothing
     * to release for this backend. */
    void     (*abort)(void);
} m1cp_update_backend_t;

/* Per-instance status-publishing configuration -- lets the coordinator stay
 * ignorant of what M1CP_DOMAIN_* and M1CP_OP_* values actually MEAN for a given
 * target (existing wire values are preserved exactly; this is
 * configuration data, not per-target logic). op_reboot_required == 0xFF
 * means "this target has no separate reboot-required concept" (ESP: FINISH
 * itself completes the update; no M1cp_status_set_flag call is made). */
typedef struct
{
    uint8_t domain;              /* M1CP_DOMAIN_*                                    */
    uint8_t op_receiving;        /* published during DATA, with percent complete     */
    uint8_t op_verifying;        /* published at the start of FINISH                 */
    uint8_t op_reboot_required;  /* published after a successful FINISH when supports_activate; 0xFF = N/A */
    uint8_t op_complete;         /* published after a successful FINISH when !supports_activate            */
} m1cp_update_ops_t;

/* One coordinator instance per concurrently-trackable update session. Each
 * M1CP target dispatch shim owns exactly one of these (STM32's and ESP32's
 * stay independent sessions, matching their existing, already-correct
 * behavior of never interacting with each other -- an M1 has one host MCU
 * and one radio, not N of either). */
typedef struct
{
    const m1cp_update_backend_t *be;
    m1cp_update_ops_t ops;
    m1cp_update_state_t state;
    uint32_t total;
    uint32_t received;          /* resumable offset cursor -- coordinator-owned, not backend-owned */
    uint32_t flash_offset;
    uint16_t chunk_max;         /* negotiated at BEGIN; every DATA chunk's size must be <= this */
    uint8_t  expected_hash[M1CP_UPDATE_HASH_MAX_LEN];
    uint8_t  hash_len;
    uint8_t  hash_algo;         /* m1cp_update_hash_algo_t */
    uint8_t  diag_step;         /* 0 none, 1 begin, 2 write, 3 finish */
    uint8_t  diag_code;         /* backend-specific error/status code from that step */
    /* Measured, transport-independent counters for this session (reset at
     * every BEGIN). A backend with richer, transport-specific telemetry
     * (e.g. the ESP UART path's internal-retry/timeout/fixed-delay
     * counters) keeps that separately -- these four are the ones meaningful
     * for EVERY target regardless of transport. */
    uint32_t stat_commands;     /* BEGIN/DATA/FINISH/ACTIVATE/ABORT handled this session */
    uint32_t stat_writes_ok;    /* be->write() calls that returned success             */
    uint32_t stat_resends_ok;   /* DATA chunks re-ACKed as an idempotent resend, not reprocessed */
    uint32_t stat_bad_arg;      /* genuine (non-resend) BAD_ARG rejections                */
} m1cp_update_session_t;

void m1cp_update_session_init(m1cp_update_session_t *s, const m1cp_update_backend_t *be,
                               const m1cp_update_ops_t *ops);

/* BEGIN: validates the manifest (image_size vs be->max_image_size;
 * hash_algo known and hash_len exactly matching that algorithm's digest
 * size; hash_algo >= be->min_hash_algo) then calls be->begin(). On success
 * writes max_chunk(2, LE) to `out`/`olen`. */
uint8_t m1cp_update_begin(m1cp_update_session_t *s, uint32_t flash_offset, uint32_t image_size,
                           uint8_t hash_algo, const uint8_t *hash, uint8_t hash_len,
                           uint16_t chunk_max, uint8_t *out, uint16_t *olen);

/* DATA: payload = offset(4, LE) | data(N). Writes next_offset(4, LE) to
 * `out`/`olen` on success (including an idempotent resend -- see the .c
 * file for exactly which resends are safe and which remain rejected). */
uint8_t m1cp_update_data(m1cp_update_session_t *s, const uint8_t *payload, uint16_t plen,
                          uint8_t *out, uint16_t *olen);

/* FINISH: completeness (received == total) + be->finish(hash). Transitions
 * to VERIFIED (supports_activate) or IDLE-and-done (!supports_activate). */
uint8_t m1cp_update_finish(m1cp_update_session_t *s);

/* ACTIVATE: confirm(2, LE) must equal `confirm_expected`. Only valid when
 * be->supports_activate and state == VERIFIED; arms ACTIVATING and ACKs --
 * the caller's task loop must poll m1cp_update_activate_pending()/
 * m1cp_update_do_activate() afterward so the ACK is transmitted first. */
uint8_t m1cp_update_activate(m1cp_update_session_t *s, uint16_t confirm_got, uint16_t confirm_expected);

uint8_t m1cp_update_abort(m1cp_update_session_t *s);
bool    m1cp_update_busy(const m1cp_update_session_t *s);
/* Session end / link-down / inactivity timeout. Never interrupts ACTIVATING
 * (that swap/reset must complete once armed) -- matches both targets'
 * existing, already-correct behavior. */
void    m1cp_update_reset(m1cp_update_session_t *s);
bool    m1cp_update_activate_pending(const m1cp_update_session_t *s);
void    m1cp_update_do_activate(m1cp_update_session_t *s);

/* (diag_step << 8) | diag_code -- matches the existing ESP NACK wire format
 * exactly, so m1_manager_protocol.c's diagnostic-NACK builder needs no
 * per-target branching to use either session's diagnostic. */
uint16_t m1cp_update_last_diag(const m1cp_update_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* M1_MANAGER_UPDATE_COORDINATOR_H_ */
