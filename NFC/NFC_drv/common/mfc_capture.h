/*============================================================================*/
/**
 * @file    mfc_capture.h
 * @brief   MIFARE Classic "Detect Reader" authentication-capture store.
 *
 * Phase 1A (capture ONLY): collects reader authentication contexts observed
 * while the M1 emulates a MIFARE Classic card, and pairs two contexts for the
 * same {cuid, sector, key_type} into one mfkey32-compatible parameter set.
 *
 * NO key recovery is performed here. This is a bounded, fixed-size, RAM-only
 * store with a read-only accessor API so a future mfkey32 recovery stage can
 * consume completed pairs without knowing anything about the UI or Crypto1.
 *
 * Captures are paired by sector and key type in fixed M1-native storage
 * with no heap growth.
 */
/*============================================================================*/
#ifndef MFC_CAPTURE_H
#define MFC_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Bounds (fixed storage, sized for the M1's RAM) ----------------------- */
/* Distinct in-flight {cuid,sector,key_type} records retained per session.
 * The M1 needs far fewer records to reach the pair target. */
#define MFC_CAPTURE_MAX_RECORDS   24U
/* Session completion target, counted in captured authentication attempts
 * (nonces). Two matching attempts = one mfkey32 pair, so 10 nonces => up to
 * 5 completed pairs (10 compared against 2*pairs). Displayed as
 * "Nonces: X/10". */
#define MFC_CAPTURE_TARGET_NONCES 10U

/* MIFARE Classic key type as carried by the AUTH command (0x60 / 0x61). */
typedef enum {
    MFC_KEY_A = 0,
    MFC_KEY_B = 1,
} mfc_key_type_t;

/* One authentication context captured after the reader's Nr+Ar are received.
 * All nonce values are the 32-bit big-endian view of the 4 on-wire bytes,
 * i.e. exactly the mfkey32 solver input form. now_ms is a caller-supplied
 * monotonic tick (e.g. HAL_GetTick()) used only for this record's own
 * timeout bookkeeping if it opens a new incomplete pair -- this module
 * itself makes no HAL/RTOS call, keeping it host-testable with an
 * explicit, deterministic clock. */
typedef struct {
    uint32_t       cuid;      /* card UID checksum (Crypto1 CUID) */
    uint8_t        block;     /* block the reader authenticated against */
    uint8_t        sector;    /* sector derived from block */
    mfc_key_type_t key_type;  /* Key A / Key B */
    uint32_t       nt;        /* card nonce (we generated + sent it) */
    uint32_t       nr;        /* reader challenge (encrypted on wire) */
    uint32_t       ar;        /* reader answer (encrypted on wire) */
    uint32_t       now_ms;    /* caller's monotonic tick at capture time */
} mfc_auth_ctx_t;

/* Paired mfkey32 parameter set: two contexts for one {cuid,sector,key_type}.
 * gen is the session generation this record was OPENED under (see
 * mfc_capture_invalidate_incomplete()/mfc_capture_check_timeout() below) --
 * a record whose gen no longer matches the store's current generation can
 * never be completed, even if it is still sitting in the array; this is
 * how field-loss/STOP/BACK/timeout invalidate an in-flight pair WITHOUT
 * discarding any already-completed pair sitting alongside it.
 * opened_ms is the first attempt's own now_ms, used only for timeout
 * expiry of a still-incomplete record. */
typedef struct {
    bool           is_filled; /* false = only nt0/nr0/ar0; true = usable pair */
    uint32_t       cuid;
    uint8_t        sector;
    mfc_key_type_t key_type;
    uint32_t       nt0, nr0, ar0;   /* first attempt  */
    uint32_t       nt1, nr1, ar1;   /* second attempt */
    uint32_t       gen;             /* session generation when opened */
    uint32_t       opened_ms;       /* first attempt's now_ms (timeout base) */
} mfc_pair_t;

/* Result of offering one captured context to the store. */
typedef enum {
    MFC_CAP_INVALID = 0,   /* rejected: malformed/out-of-range context        */
    MFC_CAP_DUPLICATE,     /* rejected: identical to an already-stored attempt*/
    MFC_CAP_ADDED_PARTIAL, /* stored as a new incomplete record (nt0/nr0/ar0) */
    MFC_CAP_COMPLETED_PAIR,/* completed an existing record (nt1/nr1/ar1)      */
    MFC_CAP_FULL,          /* rejected: record store is full                  */
} mfc_capture_result_t;

/* ---- Lifecycle ------------------------------------------------------------ */
/* Clear all records and counters for a new Detect Reader session. `cuid` is
 * the emulated card's CUID; contexts with a different cuid are rejected so a
 * new reader/card session never falsely pairs with a previous one. Also
 * bumps the session generation. */
void mfc_capture_reset(uint32_t cuid);

/* Offer one captured authentication context to the store (called from the RF
 * capture path only; no UI/Crypto1 knowledge). Applies validity + pairing +
 * duplicate rules and returns the outcome. Increments the nonce counter on
 * ADDED_PARTIAL / COMPLETED_PAIR only. A context can only COMPLETE an
 * existing partial record that was opened under the CURRENT generation --
 * see mfc_capture_invalidate_incomplete(). */
mfc_capture_result_t mfc_capture_add(const mfc_auth_ctx_t *ctx);

/* Invalidates every currently-incomplete record (bumps the session
 * generation so no future mfc_capture_add() can complete them) WITHOUT
 * touching any already-completed pair -- those remain fully readable via
 * mfc_capture_get_pair() exactly as before. Call this on field loss, STOP,
 * BACK, or any other event that must not let a later, causally-unrelated
 * reader attempt complete an in-flight pair. Idempotent; safe to call with
 * nothing pending. */
void mfc_capture_invalidate_incomplete(void);

/* Same effect as mfc_capture_invalidate_incomplete(), but only when at
 * least one still-incomplete record's opened_ms predates
 * (now_ms - timeout_ms) -- i.e. a bounded "how long may a partial pair
 * wait for its second attempt" policy, enforced by the caller polling this
 * with its own monotonic clock (this module makes no HAL/RTOS call
 * itself). Returns true iff it actually invalidated something (for
 * logging only -- callers must not depend on the return value for
 * correctness, since a false return legitimately means "nothing was
 * stale", not an error). */
bool mfc_capture_check_timeout(uint32_t now_ms, uint32_t timeout_ms);

/* ---- Read-only status (UI + future recovery interface) -------------------- */
uint32_t mfc_capture_nonce_count(void);  /* captured attempts (display X/10)   */
uint32_t mfc_capture_pair_count(void);   /* completed mfkey32 pairs            */
uint32_t mfc_capture_session_cuid(void); /* emulated CUID for this session     */
uint32_t mfc_capture_session_gen(void);  /* current generation (persistence/export tagging) */
bool     mfc_capture_target_reached(void);/* nonce_count >= target             */

/* Future mfkey32 recovery consumes completed pairs through this read-only API.
 * Returns false if `index` is out of range or the pair is not complete. */
uint32_t mfc_capture_get_completed_pairs(void);
bool     mfc_capture_get_pair(uint32_t index, mfc_pair_t *out);

/* Details of the most-recently accepted context (for the debug screen). */
bool     mfc_capture_last(uint8_t *sector, mfc_key_type_t *key_type);

#ifdef __cplusplus
}
#endif

#endif /* MFC_CAPTURE_H */
