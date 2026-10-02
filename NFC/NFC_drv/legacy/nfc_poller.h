/* See COPYING.txt for license details. */

#include <stdbool.h>
#include <stdint.h>
#include "common/mfc_key_source.h"   /* mfc_key_source_cfg_t (m1_mfc_build_key_sources) */

/**
 * @brief ReadIni - Initialize NFC poller (READ-ONLY mode)
 *
 * @retval true Initialization successful
 * @retval false Initialization failed
 */
bool ReadIni(void);

/**
 * @brief ReadCycle - Main NFC poller processing loop
 *
 * @retval None
 */
void ReadCycle(void);

/**
 * @brief nfc_harvest_arm - Arm a one-shot MIFARE Classic nested-nonce harvest.
 *
 * The NFC poll loop (ReadCycle) services this on the next activated MIFARE
 * Classic card: it authenticates the SOURCE sector with known_key, then for
 * `samples` iterations issues a nested authenticate to the TARGET sector and
 * captures the encrypted nonce + parity (without completing the target auth,
 * whose key is unknown), and writes the samples as a Nested record to
 * 0:/NFC/recover/<uid>_<ticks>.m1h. Result is logged to the console.
 *
 * keytype: 0x60 = Key A, 0x61 = Key B. Block = sector * 4.
 * Cross-task safe (CLI task arms; NFC task services); one-shot.
 */
void nfc_harvest_arm(uint8_t src_block, uint8_t src_keytype,
                     uint8_t tgt_block, uint8_t tgt_keytype,
                     uint8_t samples, uint64_t known_key);

/* ---- UI-driven live harvest (Increment 3b): worker-run one-shot, abortable.
 * The UI sets the target sector + sample count, posts Q_EVENT_NFC_HARVEST, and
 * polls nfc_ctx_get_harvest() for progress. Source sector 0 / Key A / known key
 * FFFFFFFFFFFF is assumed (as for the CLI trigger). ---- */
void nfc_harvest_set_target(uint8_t tgt_sector, uint8_t samples);
bool nfc_poller_harvest_active(void);
void nfc_poller_harvest_begin(void);
void nfc_poller_harvest_end(void);
void nfc_poller_harvest_abort(void);
void nfc_harvest_scan_run(void);   /* worker-run: discover -> capture -> .m1h */

/* ---- MFC Recovery solve (nested-dictionary key recovery + RF verify). ---- */
bool nfc_poller_solve_active(void);
void nfc_poller_solve_begin(void);
void nfc_poller_solve_end(void);
void nfc_poller_solve_abort(void);
void nfc_solve_run(void);           /* worker-run: read .m1h -> solve -> verify */

/* Builds the canonical [USER, SYSTEM] MFC dictionary source list -- the
 * exact same path precedence and source order normal Read and Tools >
 * Dictionary Scan use. The MIFARE Classic Keys UI calls this to report an
 * honest system-dictionary count (mfc_key_source_count()/probe() over
 * cfgs[1]) without a second path-resolution/source-order implementation.
 * `user_paths`/`sys_paths` are caller-owned 2-entry storage that must
 * outlive however long `cfgs` is used. See nfc_poller.c for the full doc. */
void m1_mfc_build_key_sources(mfc_key_source_cfg_t cfgs[2],
                              const char *user_paths[2], const char *sys_paths[2]);

/* ---- MIFARE Classic 1K write (clone/restore of a loaded image). ---- */
bool nfc_poller_mfc_write_active(void);
void nfc_poller_mfc_write_begin(void);
void nfc_poller_mfc_write_end(void);
void nfc_poller_mfc_write_abort(void);
void nfc_mfc_write_run(void);       /* worker-run: activate 1K -> auth -> write+verify */

/* ---- NTAG/Ultralight Unlock (genuine PWD_AUTH). Set the mode/payload
 * BEFORE posting the begin event (mirrors nfc_harvest_set_target()). ---- */
bool nfc_poller_unlock_active(void);
void nfc_poller_unlock_begin(void);
void nfc_poller_unlock_end(void);
void nfc_poller_unlock_abort(void);
void nfc_unlock_set_single_password(const uint8_t pwd[4]);
void nfc_unlock_set_dictionary_mode(void);
void nfc_unlock_run(void);          /* worker-run: activate -> PWD_AUTH (single or dictionary) */