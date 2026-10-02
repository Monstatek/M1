/* See COPYING.txt for license details. */

/*
*
*  m1_manager_update_coordinator.c
*
*  See m1_manager_update_coordinator.h for the design rationale.
*
* M1 Project
*
*/

#include <string.h>
#include "m1_manager_update_coordinator.h"
#include "m1_manager_protocol.h"   /* M1CP_ERR_*, M1CP_OP_*, M1CP_STF_*, status-publish API */

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The one place that knows how big a digest is for a given algorithm --
 * adding SHA-256 later means one new case here (and in the backend that
 * chooses to require/accept it), nothing else in this file. */
static uint8_t hash_len_for_algo(uint8_t algo)
{
    switch (algo)
    {
        case M1CP_UPDATE_HASH_NONE: return 0U;
        case M1CP_UPDATE_HASH_MD5:  return 16U;
        default:                    return 0xFFU;   /* unknown algorithm */
    }
}

static void publish_reboot_flag(const m1cp_update_session_t *s, bool set)
{
    if (s->ops.op_reboot_required != 0xFFU) { m1cp_status_set_flag(M1CP_STF_REBOOT_REQUIRED, set); }
}

/* Common failure path: reset session state to IDLE, publish the error, and
 * -- unless the backend already self-cleaned (a successful call into
 * finish() always self-cleans on every outcome, per that hook's own
 * contract) -- release whatever the backend is holding. */
static void session_fail(m1cp_update_session_t *s, uint8_t err, bool call_backend_abort)
{
    if (call_backend_abort && (s->be != NULL) && (s->be->abort != NULL)) { s->be->abort(); }
    s->state    = M1CP_UPD_IDLE;
    s->total    = 0U;
    s->received = 0U;
    s->chunk_max = 0U;
    publish_reboot_flag(s, false);
    m1cp_status_set_error(err);
    m1cp_status_set(s->ops.domain, M1CP_OP_FAILED, 0U);
}

void m1cp_update_session_init(m1cp_update_session_t *s, const m1cp_update_backend_t *be,
                               const m1cp_update_ops_t *ops)
{
    memset(s, 0, sizeof(*s));
    s->be    = be;
    s->ops   = *ops;
    s->state = M1CP_UPD_IDLE;
}

uint8_t m1cp_update_begin(m1cp_update_session_t *s, uint32_t flash_offset, uint32_t image_size,
                           uint8_t hash_algo, const uint8_t *hash, uint8_t hash_len,
                           uint16_t chunk_max, uint8_t *out, uint16_t *olen)
{
    uint8_t want_len, rc;

    if ((s == NULL) || (s->be == NULL)) { return M1CP_ERR_BAD_STATE; }
    if ((s->state == M1CP_UPD_RECEIVING) || (s->state == M1CP_UPD_VERIFIED) ||
        (s->state == M1CP_UPD_ACTIVATING)) { return M1CP_ERR_BUSY; }

    if ((image_size == 0U) || (image_size > s->be->max_image_size)) { return M1CP_ERR_BAD_ARG; }
    if (chunk_max == 0U) { return M1CP_ERR_BAD_ARG; }

    want_len = hash_len_for_algo(hash_algo);
    if (want_len == 0xFFU)   { return M1CP_ERR_BAD_ARG; }      /* unknown hash algorithm */
    if (hash_len != want_len) { return M1CP_ERR_BAD_ARG; }     /* digest length doesn't match the declared algorithm */
    if (hash_algo < s->be->min_hash_algo) { return M1CP_ERR_INCOMPATIBLE; } /* this target requires a stronger manifest than supplied */

    if (!s->be->ready()) { return M1CP_ERR_BAD_STATE; } /* e.g. low battery */

    s->diag_step = 0U; s->diag_code = 0U;

    rc = s->be->begin(flash_offset, image_size);
    if (rc != 0U)
    {
        uint8_t err = (rc == 1U) ? M1CP_ERR_BUSY : s->be->hard_err_code;
        s->diag_step = 1U; s->diag_code = rc;
        /* Defensive and idempotent-safe regardless of whether this
         * backend's own begin() already released on failure (ESP's does;
         * matches its own s_be_owned guard) or never had to (STM32's now
         * does too, via bl_flash_stream_reset()+re-lock in its abort()). */
        if ((s->be->abort != NULL)) { s->be->abort(); }
        m1cp_status_set_error(err);
        m1cp_status_set(s->ops.domain, M1CP_OP_FAILED, 0U);
        return err;
    }

    s->total       = image_size;
    s->received    = 0U;
    s->flash_offset = flash_offset;
    s->chunk_max   = chunk_max;
    s->hash_algo   = hash_algo;
    s->hash_len    = hash_len;
    if (hash_len != 0U) { memcpy(s->expected_hash, hash, hash_len); }
    s->stat_commands   = 1U;   /* this BEGIN */
    s->stat_writes_ok  = 0U;
    s->stat_resends_ok = 0U;
    s->stat_bad_arg    = 0U;
    s->state = M1CP_UPD_RECEIVING;

    m1cp_status_set_error(M1CP_ERR_NONE);
    publish_reboot_flag(s, false);
    m1cp_status_set(s->ops.domain, s->ops.op_receiving, 0U);

    out[0] = (uint8_t)(chunk_max & 0xFFU);
    out[1] = (uint8_t)((chunk_max >> 8) & 0xFFU);
    *olen = 2U;
    return M1CP_ERR_NONE;
}

uint8_t m1cp_update_data(m1cp_update_session_t *s, const uint8_t *payload, uint16_t plen,
                          uint8_t *out, uint16_t *olen)
{
    uint32_t offset;
    uint16_t n;
    bool     last;

    if ((s == NULL) || (s->state != M1CP_UPD_RECEIVING)) { return M1CP_ERR_BAD_STATE; }
    if (plen < 4U) { return M1CP_ERR_BAD_PAYLOAD; }
    offset = rd_u32(&payload[0]);
    n      = (uint16_t)(plen - 4U);

    s->stat_commands++;

    if (n == 0U)               { s->stat_bad_arg++; session_fail(s, M1CP_ERR_BAD_ARG, true); return M1CP_ERR_BAD_ARG; }
    if (n > s->chunk_max)      { s->stat_bad_arg++; session_fail(s, M1CP_ERR_BAD_ARG, true); return M1CP_ERR_BAD_ARG; }

    if (offset != s->received)
    {
        /* Not necessarily a protocol violation: a client's own per-chunk
         * retry re-sends a DATA chunk verbatim, at the SAME offset,
         * whenever ITS copy of the previous response went missing -- a
         * real timeout, or a response that arrived but failed the M1CP
         * frame's own weak 8-bit XOR checksum. Either way the device may
         * already have accepted and advanced past that exact byte range
         * even though the client never learned that. If the ENTIRE
         * resent range is already-accepted history (offset + n <=
         * received), this is exactly that case: re-ACK with the current
         * `received` and touch nothing -- those bytes are already
         * written/superseded, so reprocessing them would double-write or
         * desync the backend's own cursor. Anything else (offset ahead of
         * received -- a real gap; or a resend that only partially
         * overlaps already-accepted bytes -- not a shape any real client
         * retry produces) is a genuine desync, not a benign duplicate. */
        if ((offset < s->received) && (((uint64_t)offset + n) <= s->received))
        {
            s->stat_resends_ok++;
            wr_u32(&out[0], s->received);
            *olen = 4U;
            return M1CP_ERR_NONE;
        }
        s->stat_bad_arg++;
        session_fail(s, M1CP_ERR_BAD_ARG, true);
        return M1CP_ERR_BAD_ARG;
    }
    if (((uint64_t)s->received + n) > s->total)
    {
        s->stat_bad_arg++; session_fail(s, M1CP_ERR_BAD_ARG, true); return M1CP_ERR_BAD_ARG;
    }

    last = (((uint64_t)s->received + n) == s->total);
    if (!last && ((n % s->be->chunk_alignment) != 0U))
    {
        s->stat_bad_arg++; session_fail(s, M1CP_ERR_BAD_ARG, true); return M1CP_ERR_BAD_ARG;
    }

    {
        uint8_t wrc = s->be->write(&payload[4], n);
        if (wrc != 0U)
        {
            s->diag_step = 2U; s->diag_code = wrc;
            session_fail(s, s->be->hard_err_code, true);
            return s->be->hard_err_code;
        }
    }
    s->stat_writes_ok++;

    s->received += n;
    {
        uint8_t pct = (s->total != 0U) ? (uint8_t)(((uint64_t)s->received * 100U) / s->total) : 0U;
        m1cp_status_set(s->ops.domain, s->ops.op_receiving, pct);
    }
    wr_u32(&out[0], s->received);
    *olen = 4U;
    return M1CP_ERR_NONE;
}

uint8_t m1cp_update_finish(m1cp_update_session_t *s)
{
    uint8_t rc;

    if ((s == NULL) || (s->state != M1CP_UPD_RECEIVING)) { return M1CP_ERR_BAD_STATE; }
    if (s->received != s->total)
    {
        session_fail(s, M1CP_ERR_BAD_STATE, true);   /* incomplete: backend never got a chance to self-clean via finish() */
        return M1CP_ERR_BAD_STATE;
    }

    s->stat_commands++;
    m1cp_status_set(s->ops.domain, s->ops.op_verifying, 100U);

    rc = s->be->finish((s->hash_len != 0U) ? s->expected_hash : NULL, s->hash_len);
    /* finish() always leaves the backend released/re-locked on every
     * outcome (its own contract) -- the coordinator never calls abort()
     * after this point. */
    if (rc == 1U)
    {
        s->diag_step = 3U; s->diag_code = rc;
        s->state = M1CP_UPD_IDLE; s->total = 0U; s->received = 0U; s->chunk_max = 0U;
        publish_reboot_flag(s, false);
        m1cp_status_set_error(M1CP_ERR_INTEGRITY);
        m1cp_status_set(s->ops.domain, M1CP_OP_FAILED, 0U);
        return M1CP_ERR_INTEGRITY;
    }
    if (rc == 2U)
    {
        s->diag_step = 3U; s->diag_code = rc;
        s->state = M1CP_UPD_IDLE; s->total = 0U; s->received = 0U; s->chunk_max = 0U;
        publish_reboot_flag(s, false);
        m1cp_status_set_error(s->be->hard_err_code);
        m1cp_status_set(s->ops.domain, M1CP_OP_FAILED, 0U);
        return s->be->hard_err_code;
    }

    /* rc == 0 (verified) or rc == 3 (soft success: the whole-image verify
     * command itself couldn't run, but every block was already
     * individually acked during write()) -- both are success. */
    if (s->be->supports_activate)
    {
        s->state = M1CP_UPD_VERIFIED;
        publish_reboot_flag(s, true);
        m1cp_status_set(s->ops.domain, s->ops.op_reboot_required, 100U);
    }
    else
    {
        s->state = M1CP_UPD_IDLE;
        s->total = 0U; s->received = 0U; s->chunk_max = 0U;
        m1cp_status_set_error(M1CP_ERR_NONE);
        m1cp_status_set(s->ops.domain, s->ops.op_complete, 100U);
    }
    return M1CP_ERR_NONE;
}

uint8_t m1cp_update_activate(m1cp_update_session_t *s, uint16_t confirm_got, uint16_t confirm_expected)
{
    if ((s == NULL) || (!s->be->supports_activate)) { return M1CP_ERR_BAD_STATE; }
    if (s->state != M1CP_UPD_VERIFIED) { return M1CP_ERR_BAD_STATE; }
    if (confirm_got != confirm_expected) { return M1CP_ERR_BAD_ARG; }

    s->state = M1CP_UPD_ACTIVATING;   /* caller's task loop performs activate() after the ACK is sent */
    return M1CP_ERR_NONE;
}

uint8_t m1cp_update_abort(m1cp_update_session_t *s)
{
    if (s->state == M1CP_UPD_ACTIVATING) { return M1CP_ERR_NONE; /* already committed; nothing left to abort */ }
    if (s->state != M1CP_UPD_IDLE)
    {
        if ((s->be != NULL) && (s->be->abort != NULL)) { s->be->abort(); }
        s->state = M1CP_UPD_IDLE;
        s->total = 0U; s->received = 0U; s->chunk_max = 0U;
    }
    publish_reboot_flag(s, false);
    m1cp_status_set_error(M1CP_ERR_ABORTED);
    m1cp_status_set(s->ops.domain, M1CP_OP_IDLE, 0U);
    return M1CP_ERR_NONE;   /* ACK */
}

bool m1cp_update_busy(const m1cp_update_session_t *s)
{
    return (s->state == M1CP_UPD_RECEIVING) || (s->state == M1CP_UPD_VERIFIED) || (s->state == M1CP_UPD_ACTIVATING);
}

void m1cp_update_reset(m1cp_update_session_t *s)
{
    if (s->state == M1CP_UPD_ACTIVATING) { return; }   /* never interrupt a committed activation */
    if (s->state != M1CP_UPD_IDLE)
    {
        if ((s->be != NULL) && (s->be->abort != NULL)) { s->be->abort(); }
    }
    s->state = M1CP_UPD_IDLE;
    s->total = 0U; s->received = 0U; s->chunk_max = 0U;
    publish_reboot_flag(s, false);
}

bool m1cp_update_activate_pending(const m1cp_update_session_t *s)
{
    return (s->state == M1CP_UPD_ACTIVATING);
}

void m1cp_update_do_activate(m1cp_update_session_t *s)
{
    if ((s->state == M1CP_UPD_ACTIVATING) && (s->be != NULL) && (s->be->activate != NULL))
    {
        s->be->activate();   /* may not return (a device reset) */
    }
    /* Reached only in host tests where activate() does not reset. */
    s->state = M1CP_UPD_IDLE;
}

uint16_t m1cp_update_last_diag(const m1cp_update_session_t *s)
{
    return (uint16_t)(((uint16_t)s->diag_step << 8) | s->diag_code);
}
