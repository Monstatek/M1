/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    mfc_keys_ui_model.h
 * @brief   Pure decision rules for the MIFARE Classic Keys UI (m1_nfc.c):
 *          duplicate classification, list navigation/selection arithmetic,
 *          and system-dictionary label selection.
 *
 * Not a dictionary parser, path resolver, iterator, or counter -- those
 * remain solely mfc_key_source's job. This module only decides, from
 * ALREADY-COMPUTED values (a key array + counts, a probe result, a
 * selection/scroll pair), what the UI should show or do next. m1_nfc.c
 * calls these functions directly (not a transcription) so the drawn
 * screens, the button handlers, and this file's host tests can never
 * disagree about the rule.
 *
 * Header-only static inline, matching mfc_dict_types.h's own
 * m1nfc_mfc_sector_first_block()/m1nfc_mfc_sector_blocks() precedent: no
 * .c file, no HAL dependency, safe to include in multiple translation
 * units (including a host test binary).
 */
/*============================================================================*/
#ifndef NFC_DRV_MFC_KEYS_UI_MODEL_H_
#define NFC_DRV_MFC_KEYS_UI_MODEL_H_

#include <stdint.h>
#include <stdbool.h>
#include "mfc_key_source.h"   /* MFC_KEY_SIZE, mfc_path_probe_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the "List" control is shown at all. The key-management
 * scene only shows RIGHT/List when user keys exist; mirrored here as the
 * single source of truth the dashboard's drawn footer and the RIGHT-button
 * handler both consult, so they can never disagree. */
static inline bool mfc_keys_ui_list_visible(uint16_t user_key_count)
{
    return user_key_count > 0;
}

/* Duplicate classification against a bounded key array laid out exactly as
 * mfc_keys_t.keys[] (built-ins first, at indices [0, builtin_count), user
 * keys after): NONE if `key` is not present at all (a genuinely new key,
 * or one that exists only in the SD system dictionary -- which this array
 * never holds -- and so may be intentionally promoted into the user
 * dictionary), USER if it is already a saved user key, BUILTIN if it is
 * already a compiled default. Takes raw array+counts (not mfc_keys_t
 * itself, which pulls in FatFs via mfc_keys.h/nfc_fileio.h and cannot be
 * linked on host) -- m1_nfc.c calls this as
 * mfc_keys_ui_dup_kind(s_mfck.keys, s_mfck.count, s_mfck.builtin_count, key). */
typedef enum {
    MFC_KEYS_UI_DUP_NONE = 0,
    MFC_KEYS_UI_DUP_USER,
    MFC_KEYS_UI_DUP_BUILTIN,
} mfc_keys_ui_dup_kind_t;

static inline mfc_keys_ui_dup_kind_t mfc_keys_ui_dup_kind(const uint8_t (*keys)[MFC_KEY_SIZE], uint16_t count,
                                                           uint16_t builtin_count,
                                                           const uint8_t key[MFC_KEY_SIZE])
{
    for (uint16_t i = 0; i < count; i++) {
        bool eq = true;
        for (uint8_t b = 0; b < MFC_KEY_SIZE; b++) {
            if (keys[i][b] != key[b]) { eq = false; break; }
        }
        if (eq) return (i < builtin_count) ? MFC_KEYS_UI_DUP_BUILTIN : MFC_KEYS_UI_DUP_USER;
    }
    return MFC_KEYS_UI_DUP_NONE;
}

/* Which of the three honest system-dictionary states the dashboard shows,
 * from the same tri-state mfc_key_source_probe() result every other MFC
 * key consumer already uses (never a fourth, UI-invented state). */
typedef enum {
    MFC_KEYS_UI_SYS_INSTALLED = 0,   /* show the exact count */
    MFC_KEYS_UI_SYS_NOT_INSTALLED,   /* "System: Not installed" */
    MFC_KEYS_UI_SYS_READ_ERROR,      /* "System: Read error"    */
} mfc_keys_ui_sys_label_t;

static inline mfc_keys_ui_sys_label_t mfc_keys_ui_system_label(mfc_path_probe_t probe_state)
{
    if (probe_state == MFC_PATH_ABSENT)     return MFC_KEYS_UI_SYS_NOT_INSTALLED;
    if (probe_state == MFC_PATH_READ_ERROR) return MFC_KEYS_UI_SYS_READ_ERROR;
    return MFC_KEYS_UI_SYS_INSTALLED;
}

/* List navigation: UP moves the selection back one (if not already at the
 * top), pulling the scroll window up if the selection would otherwise
 * scroll off the top of the visible rows. */
static inline void mfc_keys_ui_nav_up(uint16_t *sel, uint16_t *scroll)
{
    if ((sel == NULL) || (scroll == NULL) || (*sel == 0)) return;
    (*sel)--;
    if (*sel < *scroll) *scroll = *sel;
}

/* DOWN moves the selection forward one (if not already at the last item),
 * pushing the scroll window down if the selection would otherwise scroll
 * off the bottom of `visible_rows`. */
static inline void mfc_keys_ui_nav_down(uint16_t *sel, uint16_t *scroll, uint16_t count, uint16_t visible_rows)
{
    if ((sel == NULL) || (scroll == NULL)) return;
    if ((uint16_t)(*sel + 1U) >= count) return;
    (*sel)++;
    if (*sel >= (uint16_t)(*scroll + visible_rows)) {
        *scroll = (uint16_t)(*sel - visible_rows + 1U);
    }
}

/* Clamp a selection after the underlying user-key count shrinks (e.g. a
 * delete just removed one): never past the last item, and an empty list
 * resets to index 0. */
static inline uint16_t mfc_keys_ui_clamp_selection(uint16_t sel, uint16_t count)
{
    if (count == 0U) return 0U;
    return (sel >= count) ? (uint16_t)(count - 1U) : sel;
}

/* The bounded-snapshot index a list selection targets: always past every
 * built-in (builtin_count + a user-list-space selection), so a built-in
 * can never be the target of Select/Delete -- structurally, not merely by
 * a runtime check that could be bypassed. */
static inline uint16_t mfc_keys_ui_target_index(uint16_t builtin_count, uint16_t user_sel)
{
    return (uint16_t)(builtin_count + user_sel);
}

/* Which mode to land on right after a delete completes: back to the list
 * if any user key remains, or `mode_dashboard` (which then also hides
 * List, since mfc_keys_ui_list_visible(0) is false) if that was the last
 * one. Mode constants are passed through unchanged so this stays
 * independent of the caller's own screen-mode enumeration. */
static inline uint8_t mfc_keys_ui_mode_after_delete(uint16_t user_count_after, uint8_t mode_list,
                                                     uint8_t mode_dashboard)
{
    return (user_count_after > 0U) ? mode_list : mode_dashboard;
}

#ifdef __cplusplus
}
#endif

#endif /* NFC_DRV_MFC_KEYS_UI_MODEL_H_ */
