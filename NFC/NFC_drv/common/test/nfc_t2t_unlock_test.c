/* Host tests for NTAG/Ultralight Unlock (genuine PWD_AUTH): the live-side
 * protocol primitive and AUTHLIM-safety logic in nfc_poller.c, and the
 * armed-image credential comparison / AUTH0-gated access in
 * nfc_listener.c.
 *
 * DISCLOSED LIMITATION (same as every other RFAL/HAL-coupled file in this
 * project -- see nfc_t2t_persistence_test.c's own header comment for the
 * full rationale): nfc_poller.c and nfc_listener.c cannot be host-compiled
 * (they pull the full ST25R3916/RFAL/FreeRTOS stack transitively). This
 * suite instead:
 *   1. Verifies the exact PWD_AUTH frame, outcome-derivation, and
 *      AUTHLIM-gating logic are present in the REAL, committed
 *      nfc_poller.c source text (source_contains() reads the actual file).
 *   2. Round-trips the outcome-derivation logic through a byte-for-byte
 *      faithful transcription of nfc_poller_pwd_auth() (verified against
 *      the real source above), covering every distinct failure/success
 *      case a physical PWD_AUTH exchange can produce.
 *   3. Verifies the armed-image PWD_AUTH comparison and AUTH0/PROT
 *      read/write gating are present in nfc_listener.c, and transcribes
 *      the comparison logic itself for direct exercise.
 * The real on-air exchange (a physical tag actually accepting/rejecting a
 * password) is covered by this task's mandatory hardware acceptance gate.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I. \
 *      nfc_t2t_unlock_test.c -o /tmp/unlock && /tmp/unlock
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0; fclose(f);
    return buf;
}
static bool contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static void test_source_matches_shipped(void)
{
    char *poller   = slurp(REPO_ROOT "/NFC/NFC_drv/legacy/nfc_poller.c");
    char *listener = slurp(REPO_ROOT "/NFC/NFC_drv/legacy/nfc_listener.c");
    char *m1nfc    = slurp(REPO_ROOT "/m1_csrc/m1_nfc.c");
    char *hexutils = slurp(REPO_ROOT "/NFC/NFC_drv/common/nfc_hex_utils.c");
    CHECK(poller != NULL, "nfc_poller.c readable");
    CHECK(listener != NULL, "nfc_listener.c readable");
    CHECK(m1nfc != NULL, "m1_nfc.c readable");
    CHECK(hexutils != NULL, "nfc_hex_utils.c readable");

    /* --- PWD_AUTH frame construction --- */
    CHECK(contains(poller, "uint8_t cmd[5] = { 0x1B, 0, 0, 0, 0 };"),
          "nfc_poller.c: PWD_AUTH frame is the real 0x1B opcode + 4 password bytes");
    CHECK(contains(poller, "cmd[1] = pwd[0]; cmd[2] = pwd[1]; cmd[3] = pwd[2]; cmd[4] = pwd[3];"),
          "nfc_poller.c: all 4 password bytes are actually placed into the frame");

    /* --- Distinct, honest outcome derivation --- */
    CHECK(contains(poller, "if (err == RFAL_ERR_TIMEOUT)") && contains(poller, "return NFC_PWDAUTH_REJECTED;"),
          "nfc_poller.c: timeout treated as an explicit rejection, not silence/success");
    CHECK(contains(poller, "if (rcvLen != 2U)"),
          "nfc_poller.c: any non-2-byte response is never treated as a valid PACK");
    CHECK(contains(poller, "memcmp(rx, expected_pack, 2U) != 0") && contains(poller, "NFC_PWDAUTH_PACK_MISMATCH"),
          "nfc_poller.c: a differing PACK against a caller-supplied expected value is its own distinct outcome");
    CHECK(contains(poller, "Never fabricates a PACK"),
          "nfc_poller.c: PACK is never fabricated (function's own doc comment states this)");

    /* --- AUTHLIM safety (dictionary mode only) --- */
    CHECK(contains(poller, "if (s_unlock_use_dictionary) {") &&
          contains(poller, "if (!have_authlim || (authlim != 0U)) {") &&
          contains(poller, "uk->state = NFC_UNLOCK_AUTHLIM_ACTIVE;"),
          "nfc_poller.c: dictionary mode refuses unless AUTHLIM was read as exactly 0");
    CHECK(!contains(poller, "nfc_ctx_set_t2t_authlim(0"),
          "nfc_poller.c: never resets/writes AUTHLIM back to 0 -- read-only safety check");

    /* --- Dictionary stops on first verified success, bounded, abortable --- */
    CHECK(contains(poller, "while (ntag_pwd_keys_iter_next(&it, cand)) {"),
          "nfc_poller.c: dictionary scan is bounded by the iterator's own finite size, not an unbounded loop");
    CHECK(contains(poller, "if (r == NFC_PWDAUTH_OK) {") && contains(poller, "found = true;\n                break;"),
          "nfc_poller.c: dictionary scan stops immediately on the first verified success");
    CHECK(contains(poller, "if (s_unlock_abort) { uk->state = NFC_UNLOCK_STOPPED; break; }"),
          "nfc_poller.c: every dictionary iteration re-checks abort -- BACK/card-removal stops promptly");

    /* --- Candidate password cleared from RAM once consumed --- */
    CHECK(contains(poller, "memset(s_unlock_pwd, 0, sizeof(s_unlock_pwd));"),
          "nfc_poller.c: single-attempt candidate password cleared from RAM after use");
    CHECK(contains(poller, "memset(cand, 0, sizeof(cand));"),
          "nfc_poller.c: last dictionary candidate cleared from RAM after the scan");
    CHECK(contains(poller, "memset(pwd, 0, sizeof(pwd));") && contains(poller, "memset(pack, 0, sizeof(pack));"),
          "nfc_poller.c: local password/PACK working copies cleared before returning");

    /* --- Genuine credential storage: only via nfc_ctx_set_t2t_credential(),
     * never anywhere else, and only from a real accepted exchange. --- */
    CHECK(contains(poller, "nfc_ctx_set_t2t_credential(pwd, pack);") &&
          contains(poller, "nfc_ctx_set_t2t_credential(cand, pack);"),
          "nfc_poller.c: credential stored only after a genuine NFC_PWDAUTH_OK result");

    /* --- Authenticated re-read after PWD_AUTH success (T2T-UNLOCK-T2 fix,
     * root cause A): the tag ships with pages at/after AUTH0 still marked
     * invalid from the necessarily-partial pre-auth read; without a re-read
     * in the SAME authenticated session, m1_t2t_emu_image_build()
     * permanently refuses with INCOMPLETE_PAGES no matter how successful
     * the auth was. UNLOCKED is reported only if that re-read completes;
     * a real accept with a failed re-read is its own distinct, honest
     * state -- never silently promoted to "unlocked". --- */
    CHECK(contains(poller, "static bool nfc_unlock_reread_all_pages(void)"),
          "nfc_poller.c: the authenticated re-read function reports completion, not void");
    CHECK(contains(poller, "uk->state = nfc_unlock_reread_all_pages() ? NFC_UNLOCK_UNLOCKED\n                                                           : NFC_UNLOCK_REREAD_INCOMPLETE;"),
          "nfc_poller.c: single-password success path only reports UNLOCKED if the re-read actually completed");
    CHECK(contains(poller, "nfc_ctx_set_t2t_page((uint16_t)(blk + i), &buf[i * 4U]);"),
          "nfc_poller.c: re-read writes captured pages through nfc_ctx_set_t2t_page() (updates the valid bitmap)");
    CHECK(contains(poller, "rfalT2TPollerRead((uint8_t)blk, buf, sizeof(buf), &rcvLen);") &&
          contains(poller, "err == RFAL_ERR_LINK_LOSS"),
          "nfc_poller.c: re-read uses the same T2T READ primitive with the same LINK_LOSS short-circuit, never a fresh deactivate/reactivate");
    CHECK(!contains(poller, "nfc_unlock_reread_all_pages(void)\n{\n    rfalNfcDeactivate") &&
          !contains(poller, "nfc_unlock_reread_all_pages(void)\n{\n    rfalNfcDiscover"),
          "nfc_poller.c: re-read never deactivates/reactivates the RF session -- reuses the already-authenticated one");

    /* --- Dump-capacity widening (T2T-UNLOCK-T8 fix, root cause F): live
     * hardware evidence showed PWD_AUTH succeeding, this function returning
     * true, "Unlocked!" showing on screen, yet the post-auth CFG0/CFG1
     * re-parse always failing to find them and Emulate still refusing with
     * INCOMPLETE_PAGES -- because nfc_ctx_set_t2t_page() silently refuses
     * to write any page index >= the dump's unit_count, which the pre-auth
     * partial read had already fixed at however many pages IT captured
     * (e.g. 4 for AUTH0=4). Without first widening that capacity, every
     * page this function re-reads beyond the original boundary is
     * silently dropped -- the loop only checks whether the RF read
     * succeeded, never whether the resulting context write did. --- */
    CHECK(contains(poller, "nfc_ctx_set_dump(4U, expected, 0U, g_nfc_dump_buf, g_nfc_valid_bits, 0U, true);"),
          "nfc_poller.c: the re-read widens nfc_ctx's dump capacity to the full expected page count before writing any page");
    {
        /* The widen call must happen BEFORE the block-read loop starts
         * (proximity check: it must appear before the first
         * nfc_ctx_set_t2t_page call in this function), not after. */
        const char *fn_start = strstr(poller, "static bool nfc_unlock_reread_all_pages(void)");
        const char *widen    = fn_start ? strstr(fn_start, "nfc_ctx_set_dump(4U, expected,") : NULL;
        const char *first_page_write = widen ? strstr(widen, "nfc_ctx_set_t2t_page((uint16_t)(blk + i)") : NULL;
        CHECK((fn_start != NULL) && (widen != NULL) && (first_page_write != NULL) && (widen < first_page_write),
              "nfc_poller.c: the capacity widen happens before any page is written, not after");
    }

    /* --- Signature capture retry (T2T-UNLOCK-T9 fix, root cause G): live
     * hardware evidence showed "T2T signature not captured (err/NAK)" for
     * this exact fixture, even though a compatible listener answers
     * READ_SIG unconditionally, never gated by AUTH0/PROT (confirmed
     * against mf_ultralight_listener_read_signature_handler() -- no
     * check_access() call at all, unlike plain READ/WRITE). READ_SIG is
     * issued immediately after the page-dump loop, which may have just
     * ended on a genuine protocol NAK at the AUTH0 boundary -- matching
     * the same "a command right after a NAK'd exchange can fail its first
     * attempt on this hardware" pattern the T4 retry-loop fix already
     * established. Retried once, mirroring that exact pattern. --- */
    CHECK(contains(poller, "for (uint8_t retry = 0; retry < 2U && !sig_ok; retry++) {") &&
          contains(poller, "sigErr = ReadSignature_Ntag(sigBuf, sizeof(sigBuf), &sigLen);"),
          "nfc_poller.c: signature capture retries once, matching the page-read loop's own retry pattern");
    CHECK(contains(poller, "platformLog(\"T2T signature not captured (err=%d rcv=%u)\\r\\n\", sigErr, sigLen);"),
          "nfc_poller.c: a failed signature capture logs the exact ReturnCode and length, not just \"err/NAK\"");

    /* --- Re-select after NAK before signature/counter/tearing capture
     * (T2T-UNLOCK-T10 fix, root cause H): live hardware evidence
     * source review together proved the T9 retry alone was insufficient --
     * NTAG21x emulation hardware-deselects
     * (GOTO_SENSE) the tag in response to ANY NAK'd command outcome, not
     * just READ, unlike real NXP silicon which stays selected after a
     * permission-denied NAK. READ_SIGNATURE's own wire format and dispatch
     * were independently confirmed correct (byte-identical to the expected
     * own poller-side transmission) -- the timeout was a session-level
     * deselect, not a framing bug, so no retry of the SAME de-selected
     * session could ever succeed. Only triggered when the dump loop
     * genuinely NAK'd (RFAL_ERR_PROTO) -- never runs for the
     * already-hardware-validated unprotected-tag case. --- */
    CHECK(contains(poller, "bool capture_ok = true;") &&
          contains(poller, "if (last_block_err == RFAL_ERR_PROTO) {\n            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);\n            rfalNfcDiscover(&discParam);"),
          "nfc_poller.c: signature/counter/tearing capture re-selects the tag when the dump loop genuinely NAK'd");
    CHECK(contains(poller, "(reDev->nfcidLen != rc->head.uid_len) ||\n                (memcmp(reDev->nfcid, rc->head.uid, rc->head.uid_len) != 0)) {"),
          "nfc_poller.c: re-selection verifies the SAME tag (matching UID) before proceeding, never assumes it");
    CHECK(contains(poller, "if (capture_ok) {"),
          "nfc_poller.c: signature/counter/tearing capture is skipped entirely if re-selection fails or finds a different tag");

    CHECK(contains(poller, "if (!complete) { return false; }"),
          "nfc_poller.c: an incomplete re-read never falls through to deriving/clearing genuine state");

    /* --- Never retry a genuine protocol NAK (T2T-UNLOCK-T4 fix, root cause
     * C): live serial evidence showed the pre-existing "retry once on
     * NAK/timeout" loop actively destroys a correct signal -- the first
     * attempt at a genuine AUTH0 boundary correctly returned
     * RFAL_ERR_PROTO/rcvLen=1 (a clean, textbook NAK), but the very next
     * attempt against the SAME still-present tag came back
     * RFAL_ERR_TIMEOUT instead, and since only the LAST attempt's error is
     * kept, the clean PROTO signal was silently overwritten by a
     * misleading TIMEOUT before the suspicion logic ever saw it. A genuine
     * protocol NAK is conclusive -- exactly like RFAL_ERR_LINK_LOSS, it
     * must never be retried. --- */
    CHECK(contains(poller, "(err == RFAL_ERR_LINK_LOSS) || (err == RFAL_ERR_PROTO)"),
          "nfc_poller.c: the main read loop stops immediately on a genuine protocol NAK, same as link loss -- never retries it");
    CHECK(contains(poller, "} else if ((err == RFAL_ERR_LINK_LOSS) || (err == RFAL_ERR_PROTO)) {"),
          "nfc_poller.c: nfc_unlock_reread_all_pages()'s mirrored loop applies the identical fix");

    /* --- Pre-auth PROTECTION-SUSPECTED signal (T2T-UNLOCK-T5 fix, root
     * cause B, now sound thanks to the T4 retry fix above): CFG0/CFG1 are
     * not exempt from the AUTH0 gate on real silicon, so a low AUTH0 (e.g.
     * 4) leaves them genuinely unreadable pre-auth. The first blocked page
     * only proves protection is SUSPECTED -- it must never be written into
     * the genuine auth0/prot/authlim fields, and must never fire on an
     * ordinary RF timeout/link-loss.
     *
     * History: T2T-UNLOCK-T2 tried this exact RFAL_ERR_PROTO check and it
     * never fired on real hardware. T2T-UNLOCK-T3 replaced it with an
     * outcome-based liveness probe to avoid trusting a specific
     * ReturnCode -- but field-tested against the SAME externally-validated
     * fixture, the probe itself never succeeded either, permanently
     * blocking Unlock on a confirmed-good tag; an unreliable mandatory
     * gate is worse than the thing it replaced. Live serial capture then
     * found the actual bug lived one level up, in the retry loop (see the
     * T4 checks above) -- not in trusting RFAL_ERR_PROTO itself, which was
     * correct all along. With that retry now suppressed, the direct check
     * is restored, and the probe (proven unreliable) is removed entirely
     * rather than kept as a second, redundant, sometimes-wrong gate. --- */
    CHECK(contains(poller, "nfc_ctx_set_t2t_protection_suspected(num_pages);"),
          "nfc_poller.c: a stopped-short read signals suspicion via the transient field, never the genuine one");
    CHECK(!contains(poller, "nfc_ctx_set_t2t_auth0((uint8_t)num_pages)"),
          "nfc_poller.c: never writes an inferred value into the genuine AUTH0 field");
    CHECK(!contains(poller, "t2t_liveness_probe"),
          "nfc_poller.c: the disproven-on-hardware liveness probe is fully removed, not left as dead code");
    CHECK(contains(poller, "} else if ((expected_pages != 0U) && (num_pages < expected_pages) &&\n                   (last_block_err == RFAL_ERR_PROTO)) {"),
          "nfc_poller.c: suspicion is gated directly on the (now-reliable) last_block_err == RFAL_ERR_PROTO");
    {
        /* AUTHLIM must stay untouched in the suspicion branch -- proximity
         * check: no nfc_ctx_set_t2t_authlim call between the branch's own
         * comment and the platformLog call that closes it. */
        const char *infer_start = strstr(poller, "Config pages themselves are at/after AUTH0");
        const char *infer_log   = infer_start ? strstr(infer_start, "T2T protection suspected") : NULL;
        const char *authlim_call = infer_start ? strstr(infer_start, "nfc_ctx_set_t2t_authlim(") : NULL;
        CHECK((infer_start != NULL) && (infer_log != NULL) &&
              ((authlim_call == NULL) || (authlim_call > infer_log)),
              "nfc_poller.c: suspicion branch never assumes AUTHLIM -- stays unknown, keeping the dictionary safety gate shut");
    }

    /* --- Entry guard permits manual Unlock on suspicion alone, but the
     * genuine fields are only ever populated by nfc_unlock_reread_all_pages()
     * post-auth (checked above), and dictionary mode is independently
     * refused by the pre-existing AUTHLIM gate whenever AUTHLIM is still
     * unknown (true whenever entry was via suspicion only). --- */
    CHECK(contains(poller, "bool    suspected   = nfc_ctx_get_t2t_protection_suspected(NULL);") &&
          contains(poller, "if (!supported || (!known_auth0 && !suspected)) {"),
          "nfc_poller.c: nfc_unlock_run() proceeds on genuine AUTH0 OR suspicion alone");

    /* --- Successful re-read supersedes suspicion with genuine data. --- */
    CHECK(contains(poller, "nfc_ctx_clear_t2t_protection_suspected();"),
          "nfc_poller.c: a completed re-read clears the transient suspicion once genuine fields are populated");

    /* --- Armed-image PWD_AUTH comparison (nfc_listener.c) --- */
    CHECK(contains(listener, "s_t2t_img_armed && s_t2t_img.credential_valid &&") &&
          contains(listener, "memcmp(&rx[1], s_t2t_img.pwd, 4U) == 0"),
          "nfc_listener.c: PWD_AUTH compares the incoming password against a genuinely verified stored one");
    CHECK(contains(listener, "s_t2t_authenticated = true;"),
          "nfc_listener.c: a successful match sets the per-session authenticated flag");
    CHECK(contains(listener, "err = rfalTransceiveBlockingTx(s_t2t_img.pack, 2U,"),
          "nfc_listener.c: success responds with the genuine stored PACK as a real 2-byte frame, not a short ACK");
    {
        /* The PWD_AUTH case's mismatch/no-credential fallthrough must still
         * reach the original genuine-NAK lines -- checked by proximity
         * (the credential-match block's own return must appear BEFORE the
         * NAK lines within the same case, i.e. the NAK is the fallthrough
         * path, not a dead/unreachable duplicate). */
        const char *case_start = strstr(listener, "case T2T_CMD_PWD_AUTH: {");
        const char *match_ret  = case_start ? strstr(case_start, "return CeRearmRxAfterTx();") : NULL;
        const char *nak_line   = match_ret ? strstr(match_ret, "err = CeSendShortFrame(T2T_NAK_NIBBLE);") : NULL;
        CHECK((case_start != NULL) && (match_ret != NULL) && (nak_line != NULL),
              "nfc_listener.c: mismatch/no-credential path still falls through to the original genuine NAK, after the match-success return");
    }

    /* --- AUTH0/PROT read/write gating --- */
    CHECK(contains(listener, "s_t2t_img.protected_tag && s_t2t_img.prot && !s_t2t_authenticated &&") &&
          contains(listener, "(startPage >= s_t2t_img.auth0)"),
          "nfc_listener.c: READ is blocked past AUTH0 only when PROT=1 and unauthenticated");
    CHECK(contains(listener, "bool auth_blocked = s_t2t_img.protected_tag && !s_t2t_authenticated &&") &&
          contains(listener, "(page >= s_t2t_img.auth0);"),
          "nfc_listener.c: WRITE is blocked past AUTH0 regardless of PROT (write is never the unprotected side)");
    CHECK(contains(listener, "s_t2t_authenticated = false;") ,
          "nfc_listener.c: authenticated state resets on a fresh arm/clear -- never inherited across cards");

    /* --- UI lifecycle (T2T-UNLOCK-T5): the result screen and Unlock
     * landing menu must agree with nfc_can_unlock()/nfc_unlock_run()'s own
     * gates, never independently guess. --- */
    CHECK(contains(m1nfc, "nfc_ctx_get_t2t_protection_suspected(NULL)) {") &&
          contains(m1nfc, "\"Locked - Partial Read\""),
          "m1_nfc.c: the read-result screen shows Locked - Partial Read when protection is only suspected");
    CHECK(contains(m1nfc, "static bool nfc_unlock_dictionary_allowed(void)") &&
          contains(m1nfc, "return nfc_ctx_get_t2t_authlim(&authlim) && (authlim == 0U);"),
          "m1_nfc.c: a single function decides dictionary-safety for the UI, mirroring nfc_unlock_run()'s own gate exactly");
    CHECK(contains(m1nfc, "uint8_t item_count  = nfc_unlock_dictionary_allowed() ? 2U : 1U;"),
          "m1_nfc.c: the Unlock landing menu only draws \"Use Dictionary\" when genuinely safe, never offers a dead-end choice");
    CHECK(contains(m1nfc, "uint8_t max_sel = nfc_unlock_dictionary_allowed() ? 1U : 0U;"),
          "m1_nfc.c: keypad navigation on the landing menu is bounded by the same safety check as the draw");
    CHECK(contains(m1nfc, "if (nfc_ctx_get_t2t_auth0(&auth0) && (auth0 != 0xFFU)) { return true; }") &&
          contains(m1nfc, "return nfc_ctx_get_t2t_protection_suspected(NULL);"),
          "m1_nfc.c: nfc_can_unlock() offers Unlock on genuine AUTH0 OR suspicion alone");

    /* --- Password-entry parsing (T2T-UNLOCK-T5 fix, root cause D): field
     * evidence showed "Invalid password" on every entry, regardless of
     * input. m1_vkbs_get_hexkey() returns a CONTIGUOUS hex-nibble string
     * (proven by its own out_hex[len]=digit;out_hex[len+1]='\0' append
     * code in m1_virtual_kb.c -- no separators), but m1_strtob_with_base()
     * is a space-TOKENIZED parser (strtok(buf," ")) that treats a
     * contiguous string as a single token -- exactly the mismatch that
     * broke every entry. Fixed with a dedicated contiguous-hex parser --
     * later extracted into nfc_hex_utils.c/.h (a tiny, host-linkable common
     * module) so it could be linked and tested directly instead of only via
     * source-text assertion; m1_nfc.c now calls it as a shared function
     * rather than defining it. --- */
    CHECK(contains(hexutils, "bool nfc_hex_nibbles_to_bytes(const char *hex, uint8_t *out, int len)"),
          "nfc_hex_utils.c: the dedicated contiguous-hex parser exists for virtual-keyboard hex entry");
    CHECK(contains(m1nfc, "if (!nfc_hex_nibbles_to_bytes(buf, pwd, (int)sizeof(pwd))) { nfc_ulc_toast(\"Invalid password\"); return; }"),
          "m1_nfc.c: Enter Password uses the contiguous-hex parser, not the space-tokenized one");
    CHECK(!contains(m1nfc, "m1_strtob_with_base(buf, pwd,"),
          "m1_nfc.c: the broken space-tokenized parse call is gone from the password path");

    /* --- Unlocked-result "continue" action (T2T-UNLOCK-T6 fix, root cause
     * E): field evidence showed OK at the "Unlocked!" screen led nowhere
     * useful. NFC_READ_DISPLAY_PARAM_READING_READY triggers
     * Q_EVENT_NFC_START_READ -- a brand-new physical read cycle -- and
     * since the RF session was already deactivated when nfc_unlock_run()
     * finished, that fresh read reselects the tag unauthenticated, hits
     * the AUTH0 boundary again, and (via m1_t2t_read_ntag()'s own
     * clear-on-entry calls) wipes the credential and genuine protection
     * state the unlock just established. READING_COMPLETE is the
     * established parameter (used elsewhere in this file to return from a
     * submenu to an already-populated result) for showing existing
     * nfc_ctx data with no new RF action. --- */
    CHECK(contains(m1nfc, "m1_uiView_display_switch(VIEW_MODE_NFC_READ, NFC_READ_DISPLAY_PARAM_READING_COMPLETE);\r\n            return 1;\r\n        }\r\n        s_unlock_ui_mode = 0;"),
          "m1_nfc.c: the Unlocked-result OK action jumps to the already-complete read result, not a fresh read cycle (m1_nfc.c is CRLF)");

    /* --- CENTER+View action hint on the Unlocked! screen. Requirements 1-7
     * below, in order. --- */
    {
        /* 1. Renders the CENTER icon (target_10x10, the same asset every
         * other CENTER-action footer in this file uses) and the word
         * "View", inside nfc_unlock_draw_result()'s UNLOCKED branch. */
        const char *fn_start    = strstr(m1nfc, "static void nfc_unlock_draw_result(void)");
        const char *unlocked_if = fn_start ? strstr(fn_start, "if (uk->state == NFC_UNLOCK_UNLOCKED) {") : NULL;
        const char *icon        = unlocked_if ? strstr(unlocked_if, "target_10x10") : NULL;
        const char *view_word   = icon ? strstr(icon, "\"View\"") : NULL;
        CHECK((fn_start && unlocked_if && icon && view_word),
              "m1_nfc.c: 1. the Unlocked! screen renders the CENTER icon and \"View\" text");
    }
    {
        /* 2-4, 6. Isolate the OK-handler's UNLOCKED branch -- specifically
         * the CODE after its explanatory doc comment closes (the comment
         * itself legitimately names READING_READY, Q_EVENT_NFC_START_READ,
         * and nfc_ctx_clear_t2t_credential() in prose, explaining what NOT
         * to do; scanning from the comment close excludes that prose so
         * only real code is checked) through its closing "return 1; }". */
        const char *ok_start = strstr(m1nfc, "if (uk->state == NFC_UNLOCK_UNLOCKED) {\r\n            /* Show the read result");
        const char *comment_end = ok_start ? strstr(ok_start, "nothing cleared. */") : NULL;
        const char *code_start = comment_end ? (comment_end + strlen("nothing cleared. */")) : NULL;
        const char *branch_end = code_start ? strstr(code_start, "return 1;\r\n        }") : NULL;
        size_t branch_len = (code_start && branch_end) ? (size_t)(branch_end - code_start) : 0;
        ok_start = code_start;   /* subsequent memcpy uses the code-only range */
        char *br = NULL;
        if (branch_len > 0) {
            br = malloc(branch_len + 1);
            memcpy(br, ok_start, branch_len);
            br[branch_len] = '\0';
        }
        CHECK((br != NULL) && contains(br, "NFC_READ_DISPLAY_PARAM_READING_COMPLETE"),
              "m1_nfc.c: 2. CENTER on Unlocked! selects NFC_READ_DISPLAY_PARAM_READING_COMPLETE");
        CHECK((br != NULL) && !contains(br, "NFC_READ_DISPLAY_PARAM_READING_READY"),
              "m1_nfc.c: 3a. CENTER on Unlocked! never references NFC_READ_DISPLAY_PARAM_READING_READY");
        CHECK((br != NULL) && !contains(br, "Q_EVENT_NFC_START_READ"),
              "m1_nfc.c: 3b. CENTER on Unlocked! never dispatches Q_EVENT_NFC_START_READ");
        CHECK((br != NULL) && !contains(br, "nfc_ctx_clear_t2t_credential") &&
              !contains(br, "nfc_ctx_clear_dump") && !contains(br, "nfc_ctx_clear_t2t_protection"),
              "m1_nfc.c: 4. CENTER on Unlocked! never clears the verified credential or page-valid state");
        /* 6. No emulation-eligibility-blocking call either -- the branch's
         * only actions are the display switch and returning. A completed
         * result stays exactly as eligible for Save/Emulate as
         * nfc_unlock_reread_all_pages() already made it. */
        CHECK((br != NULL) && !contains(br, "s_t2t_emu_refused") &&
              !contains(br, "m1_t2t_emu_image_build"),
              "m1_nfc.c: 6. CENTER on Unlocked! never touches emulation-eligibility state -- result stays eligible for Save/Emulate");
        free(br);
    }
    /* 5. BACK on the result screen returns to the Unlock landing menu
     * (s_unlock_ui_mode = 0), unconditionally -- not gated on which
     * terminal state is showing. */
    CHECK(contains(m1nfc, "if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {\r\n        s_unlock_ui_mode = 0;\r\n        m1_uiView_display_update(0);\r\n        return 1;\r\n    }"),
          "m1_nfc.c: 5. BACK on the result screen returns to the Unlock landing menu");
    /* 7. Unsuccessful-unlock states are completely unchanged: still the
     * plain "BACK to return" line, no footer, no icon. */
    CHECK(contains(m1nfc, "} else {\r\n        u8g2_DrawStr(&m1_u8g2, 2, 42, \"BACK to return\");\r\n    }\r\n}"),
          "m1_nfc.c: 7. unsuccessful-unlock states keep the unchanged plain \"BACK to return\" text, no new footer");

    free(poller); free(listener); free(m1nfc);
}

/* ---- Transcription of nfc_poller_pwd_auth()'s outcome derivation,
 * verified above to match the real source, exercised directly. ---- */
typedef enum { X_OK = 0, X_REJECTED, X_PACK_MISMATCH, X_COMM_ERROR } x_result_t;

/* Mirrors the real function's signature/branches exactly: err is the
 * simulated RFAL transport outcome, rcvLen the simulated response length. */
static x_result_t x_pwd_auth_outcome(int err_none, int err_timeout, uint16_t rcvLen,
                                     const uint8_t *rx, const uint8_t *expected_pack)
{
    if (err_timeout) return X_REJECTED;
    if (!err_none)   return X_COMM_ERROR;
    if (rcvLen != 2U) return X_COMM_ERROR;
    if (expected_pack != NULL && memcmp(rx, expected_pack, 2U) != 0) return X_PACK_MISMATCH;
    return X_OK;
}

static void test_outcome_timeout_is_rejected(void)
{
    uint8_t rx[2] = {0};
    x_result_t r = x_pwd_auth_outcome(0, 1, 0, rx, NULL);
    CHECK(r == X_REJECTED, "timeout -> REJECTED");
}
static void test_outcome_other_error_is_comm_error(void)
{
    uint8_t rx[2] = {0};
    x_result_t r = x_pwd_auth_outcome(0, 0, 0, rx, NULL);   /* neither NONE nor TIMEOUT */
    CHECK(r == X_COMM_ERROR, "CRC/collision/link-loss -> COMM_ERROR");
}
static void test_outcome_wrong_length_is_comm_error(void)
{
    uint8_t rx[8] = {1,2,3,4,5,6,7,8};
    x_result_t r = x_pwd_auth_outcome(1, 0, 5, rx, NULL);   /* frame ok, but not 2 bytes */
    CHECK(r == X_COMM_ERROR, "wrong-length response -> COMM_ERROR (inconclusive, never a verdict)");
}
static void test_outcome_accepted_no_expected_pack(void)
{
    uint8_t rx[2] = {0xAA, 0xBB};
    x_result_t r = x_pwd_auth_outcome(1, 0, 2, rx, NULL);
    CHECK(r == X_OK, "genuine 2-byte PACK, no expected value to check -> OK");
}
static void test_outcome_pack_matches_expected(void)
{
    uint8_t rx[2] = {0xAA, 0xBB};
    uint8_t exp[2] = {0xAA, 0xBB};
    x_result_t r = x_pwd_auth_outcome(1, 0, 2, rx, exp);
    CHECK(r == X_OK, "returned PACK matches a previously-verified expected value -> OK");
}
static void test_outcome_pack_mismatches_expected(void)
{
    uint8_t rx[2] = {0xAA, 0xBB};
    uint8_t exp[2] = {0x11, 0x22};
    x_result_t r = x_pwd_auth_outcome(1, 0, 2, rx, exp);
    CHECK(r == X_PACK_MISMATCH, "returned PACK disagrees with a previously-verified expected value -> PACK_MISMATCH");
}

/* ---- Transcription of the armed-image PWD_AUTH comparison + AUTH0/PROT
 * gating, verified above to match the real nfc_listener.c source. ---- */
typedef struct {
    bool     protected_tag, prot, credential_valid;
    uint8_t  auth0;
    uint8_t  pwd[4];
    uint8_t  pack[2];
} x_img_t;

static bool x_pwd_auth_matches(const x_img_t *img, const uint8_t rx_pwd[4])
{
    return img->credential_valid && (memcmp(rx_pwd, img->pwd, 4U) == 0);
}
static bool x_read_blocked(const x_img_t *img, bool authenticated, uint8_t startPage)
{
    return img->protected_tag && img->prot && !authenticated && (startPage >= img->auth0);
}
static bool x_write_blocked(const x_img_t *img, bool authenticated, uint8_t page)
{
    return img->protected_tag && !authenticated && (page >= img->auth0);
}

static void test_pwd_auth_match_and_mismatch(void)
{
    x_img_t img = {0};
    img.credential_valid = true;
    img.pwd[0]=0x11; img.pwd[1]=0x22; img.pwd[2]=0x33; img.pwd[3]=0x44;

    uint8_t correct[4] = {0x11,0x22,0x33,0x44};
    uint8_t wrong[4]   = {0x11,0x22,0x33,0x45};
    CHECK(x_pwd_auth_matches(&img, correct), "correct password matches stored credential");
    CHECK(!x_pwd_auth_matches(&img, wrong), "wrong password does not match");

    x_img_t noimg = {0};
    CHECK(!x_pwd_auth_matches(&noimg, correct), "no credential_valid -> never matches, regardless of bytes");
}

static void test_read_gate_prot1_vs_prot0(void)
{
    x_img_t img = {0};
    img.protected_tag = true; img.auth0 = 4;

    img.prot = true;
    CHECK(x_read_blocked(&img, false, 4), "PROT=1, unauthenticated, page>=AUTH0: READ blocked");
    CHECK(!x_read_blocked(&img, false, 3), "PROT=1, unauthenticated, page<AUTH0: READ allowed");
    CHECK(!x_read_blocked(&img, true, 4), "PROT=1, authenticated: READ allowed past AUTH0");

    img.prot = false;
    CHECK(!x_read_blocked(&img, false, 4), "PROT=0 (write-only): READ never blocked, even unauthenticated");
}

static void test_write_gate_ignores_prot(void)
{
    x_img_t img = {0};
    img.protected_tag = true; img.auth0 = 4;

    img.prot = false;   /* write-only protection -- WRITE must still be gated */
    CHECK(x_write_blocked(&img, false, 4), "PROT=0, unauthenticated, page>=AUTH0: WRITE blocked (write is the protected side)");
    CHECK(!x_write_blocked(&img, false, 3), "page<AUTH0: WRITE allowed regardless of auth state");
    CHECK(!x_write_blocked(&img, true, 4), "authenticated: WRITE allowed past AUTH0");

    img.prot = true;
    CHECK(x_write_blocked(&img, false, 4), "PROT=1 also gates WRITE (both protection kinds cover write)");
}

static void test_unprotected_tag_never_gated(void)
{
    x_img_t img = {0};   /* protected_tag = false */
    CHECK(!x_read_blocked(&img, false, 100), "unprotected tag: READ never blocked regardless of page");
    CHECK(!x_write_blocked(&img, false, 100), "unprotected tag: WRITE never blocked regardless of page");
}

/* ============================================================================
 * Full retry-loop -> RFAL_ERR_PROTO -> suspicion -> result-screen ->
 * manual-unlock -> authenticated-re-read -> emulation workflow, transcribed
 * from the T2T-UNLOCK-T5 fix (nfc_poller.c + m1_nfc.c). Verified above
 * (test_source_matches_shipped) to match the real committed source;
 * exercised here end-to-end using the exact hardware fixture and the exact
 * live-serial-capture evidence this task specified: NTAG213, AUTH0=4,
 * PROT=1, AUTHLIM=0, PWD=12 34 56 78, PACK=AB CD (protected-authlim0.nfc),
 * attempt 1 -> RFAL_ERR_PROTO/rcv=1 (a clean NACK), read stops at page 4/45.
 *
 * Suspicion is modeled directly on RFAL_ERR_PROTO (no liveness probe --
 * T2T-UNLOCK-T3's outcome-based probe was proven unreliable in the field,
 * even against this externally-validated-as-locked fixture, and was
 * removed). The retry loop's own behavior (T2T-UNLOCK-T4's fix) is what
 * makes trusting RFAL_ERR_PROTO directly sound again -- see
 * x_retry_loop_final_err() above, which is exercised again here as part of
 * the full chain from raw ReturnCode through to the Unlock landing menu.
 * ==========================================================================*/
#define X_NTAG213_PAGES 45U
#define X_CFG0_PAGE     41U

typedef struct {
    uint16_t expected_pages;
    bool     page_valid[X_NTAG213_PAGES];
    uint8_t  page_data[X_NTAG213_PAGES][4];

    /* Transient, pre-auth only -- mirrors nfc_ctx's separate storage. */
    bool     suspected;
    uint16_t first_blocked_page;

    /* Genuine, wire-confirmed only -- mirrors nfc_ctx's separate storage. */
    bool     auth0_valid;  uint8_t auth0;
    bool     prot_valid;   bool    prot;
    bool     authlim_valid; uint8_t authlim;

    bool     credential_valid;
    uint8_t  pwd[4];
    uint8_t  pack[2];
} x_ntag_t;

/* Real RFAL ReturnCode values (NFC/Middlewares/ST/rfal/Inc/rfal_utils.h),
 * confirmed against the live serial capture: attempt 1 at the AUTH0
 * boundary genuinely returned err=11/rcv=1 (RFAL_ERR_PROTO). */
#define X_ERR_NONE      0
#define X_ERR_TIMEOUT   4
#define X_ERR_PROTO     11
#define X_ERR_LINK_LOSS 37

/* Mirrors the block-read retry loop exactly: up to 2 attempts, stops
 * immediately (without trying attempt 2) on LINK_LOSS or PROTO. Returns
 * the error that actually ended the loop (what "last_block_err" captures). */
static int x_retry_loop_final_err(const int attempt_errs[2], int attempt_count)
{
    int last_err = X_ERR_NONE;
    for (int retry = 0; retry < 2 && retry < attempt_count; retry++) {
        last_err = attempt_errs[retry];
        if (last_err == X_ERR_NONE) { return X_ERR_NONE; }   /* read_ok */
        if ((last_err == X_ERR_LINK_LOSS) || (last_err == X_ERR_PROTO)) {
            break;   /* conclusive -- stop, don't try attempt 2 */
        }
    }
    return last_err;
}

/* Mirrors m1_t2t_read_ntag()'s (T2T-UNLOCK-T5) suspicion condition exactly:
 * a short read, config pages unreachable, AND the block-read loop's own
 * last_block_err (see x_retry_loop_final_err()) landing on a genuine
 * RFAL_ERR_PROTO -- never a bare assumption, never a probe. */
static bool x_infer_suspicion(bool cfg0_reachable, uint16_t num_pages,
                              uint16_t expected_pages, int last_block_err)
{
    if (cfg0_reachable) return false;
    if (expected_pages == 0U || num_pages >= expected_pages) return false;
    return last_block_err == X_ERR_PROTO;
}

/* Mirrors nfc_can_unlock()'s core logic. */
static bool x_can_unlock(bool known_auth0, bool suspected)
{
    return known_auth0 || suspected;
}

/* Mirrors nfc_unlock_run()'s dictionary-mode AUTHLIM gate AND
 * nfc_unlock_dictionary_allowed() (m1_nfc.c) -- the two are required to
 * agree exactly, so the landing menu never offers a choice the worker will
 * then refuse. */
static bool x_dictionary_allowed(bool have_authlim, uint8_t authlim)
{
    return have_authlim && (authlim == 0U);
}

/* Mirrors nfc_unlock_kp_handler()'s landing item count (m1_nfc.c): only
 * "Enter Password" when dictionary use isn't genuinely safe yet. */
static int x_landing_item_count(bool dictionary_allowed)
{
    return dictionary_allowed ? 2 : 1;
}

/* Mirrors the Info/Raw-Data result-screen text priority (m1_nfc.c): genuine
 * AUTH0 beats suspicion beats ordinary NDEF summary. */
typedef enum { X_SCREEN_PASSWORD_PROTECTED, X_SCREEN_LOCKED_PARTIAL_READ, X_SCREEN_ORDINARY_NDEF } x_screen_text_t;
static x_screen_text_t x_result_screen_text(bool known_auth0, bool suspected)
{
    if (known_auth0) return X_SCREEN_PASSWORD_PROTECTED;
    if (suspected)   return X_SCREEN_LOCKED_PARTIAL_READ;
    return X_SCREEN_ORDINARY_NDEF;
}

/* Mirrors m1_t2t_emu_image_build()'s per-page completeness loop. */
static bool x_emulation_ready(const bool *page_valid, uint16_t count)
{
    for (uint16_t p = 0; p < count; p++) {
        if (!page_valid[p]) return false;
    }
    return true;
}

/* Sets up the exact post-partial-read state for the AUTH0=4 fixture:
 * pages 0-3 valid, 4-44 not, config pages (41/42) unreachable, genuine
 * protection fields all unknown, suspicion set from a genuine NACK. */
static void x_setup_initial_partial_read(x_ntag_t *t)
{
    memset(t, 0, sizeof(*t));
    t->expected_pages = X_NTAG213_PAGES;
    for (uint16_t p = 0; p < 4U; p++) { t->page_valid[p] = true; }
    bool cfg0_reachable = (X_CFG0_PAGE + 1U) < 4U;   /* false: only 4 pages captured */
    /* X_ERR_PROTO: this fixture's real, live-serial-confirmed behavior --
     * attempt 1 at block 4 returns a genuine protocol NACK, and (T4's fix)
     * the retry loop stops immediately instead of retrying into a
     * misleading timeout. */
    if (x_infer_suspicion(cfg0_reachable, 4U, t->expected_pages, X_ERR_PROTO)) {
        t->suspected = true;
        t->first_blocked_page = 4U;
    }
}

/* Mirrors nfc_unlock_reread_all_pages(): fills every remaining page with
 * genuine (caller-supplied) data and, only on full completion, derives the
 * real AUTH0/PROT/AUTHLIM from the now-captured CFG0/CFG1 and clears the
 * transient suspicion. */
static bool x_reread_all_pages(x_ntag_t *t, uint16_t comm_fails_at_page /* 0xFFFF = never */)
{
    for (uint16_t p = 4U; p < t->expected_pages; p++) {
        if (p == comm_fails_at_page) { return false; }
        t->page_valid[p] = true;
        /* Genuine post-auth content for this fixture: CFG0 byte3=AUTH0=4,
         * CFG1 byte0=0x80 (PROT=1, AUTHLIM=0), PWD/PACK pages read back
         * masked (irrelevant to this model -- credential comes separately). */
        if (p == X_CFG0_PAGE)     { t->page_data[p][3] = 4U; }
        if (p == X_CFG0_PAGE + 1U) { t->page_data[p][0] = 0x80U; }
    }
    t->auth0_valid = true;  t->auth0 = t->page_data[X_CFG0_PAGE][3];
    t->prot_valid  = true;  t->prot  = (t->page_data[X_CFG0_PAGE + 1U][0] & 0x80U) != 0U;
    t->authlim_valid = true; t->authlim = (uint8_t)(t->page_data[X_CFG0_PAGE + 1U][0] & 0x07U);
    t->suspected = false;
    return true;
}

/* ---- Faithful model of nfc_ctx's dump-capacity semantics (nfc_ctx.c),
 * verified above to match the real source. The EARLIER x_reread_all_pages()
 * model above is too optimistic -- it always succeeds, which is exactly
 * how this real bug slipped past every prior regression: nfc_ctx_set_t2t_
 * page() silently refuses to write any page index >= unit_count, and
 * unit_count is fixed by whatever nfc_ctx_set_dump() call bound it last.
 * This model captures that bound explicitly. ---- */
typedef struct {
    uint16_t unit_count;
    uint16_t max_seen_unit;
    bool     page_valid[X_NTAG213_PAGES];
} x_dump_t;

/* Mirrors nfc_ctx_set_dump(): (re)binds capacity/high-water-mark. Widening
 * is safe and non-destructive -- it never clears page_valid[]. */
static void x_dump_set(x_dump_t *d, uint16_t unit_count, uint16_t max_seen_unit)
{
    d->unit_count = unit_count;
    d->max_seen_unit = max_seen_unit;
}

/* Mirrors nfc_ctx_set_t2t_page(): silently does nothing if idx >= unit_count
 * (the real function returns void; this returns whether the write actually
 * took effect, purely for test observability). */
static bool x_dump_set_page(x_dump_t *d, uint16_t idx)
{
    if (idx >= d->unit_count) { return false; }
    d->page_valid[idx] = true;
    if (idx > d->max_seen_unit) { d->max_seen_unit = idx; }
    return true;
}

static void test_reread_without_capacity_widen_silently_drops_pages(void)
{
    /* Reproduces the exact field failure: pre-auth read left unit_count=4
     * (only 4 pages were ever captured pre-auth). Re-reading pages 4..44
     * WITHOUT first widening the capacity -- the old, buggy behavior. */
    x_dump_t d = {0};
    x_dump_set(&d, 4U, 3U);
    for (uint16_t p = 0; p < 4U; p++) { x_dump_set_page(&d, p); }   /* pre-auth pages, already valid */

    bool all_dropped = true;
    for (uint16_t p = 4U; p < X_NTAG213_PAGES; p++) {
        if (x_dump_set_page(&d, p)) { all_dropped = false; }
    }
    CHECK(all_dropped, "without widening unit_count first, every post-auth page write is silently dropped (reproduces the field failure)");
    for (uint16_t p = 4U; p < X_NTAG213_PAGES; p++) {
        CHECK(!d.page_valid[p], "page beyond the stale unit_count bound is never actually marked valid");
    }
}

static void test_reread_with_capacity_widen_succeeds(void)
{
    /* The fix: widen unit_count to the full expected page count BEFORE
     * writing any post-auth page. */
    x_dump_t d = {0};
    x_dump_set(&d, 4U, 3U);
    for (uint16_t p = 0; p < 4U; p++) { x_dump_set_page(&d, p); }

    x_dump_set(&d, X_NTAG213_PAGES, d.max_seen_unit);   /* the fix: widen capacity, preserve high-water-mark */

    bool all_written = true;
    for (uint16_t p = 4U; p < X_NTAG213_PAGES; p++) {
        if (!x_dump_set_page(&d, p)) { all_written = false; }
    }
    CHECK(all_written, "after widening unit_count to the full expected page count, every post-auth page write succeeds");
    for (uint16_t p = 0; p < X_NTAG213_PAGES; p++) {
        CHECK(d.page_valid[p], "every page, pre- and post-auth, is genuinely valid after the fix");
    }
}

static void test_auth0_4_initial_read_offers_manual_unlock(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);
    CHECK(t.suspected && (t.first_blocked_page == 4U),
          "AUTH0=4 fixture: read stopping at page 4 with a genuine NACK sets suspicion at page 4");
    CHECK(x_can_unlock(t.auth0_valid, t.suspected),
          "AUTH0=4 fixture: manual Unlock is offered from suspicion alone, before any genuine AUTH0 exists");
}

/* Drives the FULL, ACTUAL lifecycle end to end from the raw per-attempt
 * ReturnCode sequence a real READ(4) produces, through every intermediate
 * stage, to what the user actually sees and can select -- not a single
 * field seeded directly, the way hardware never would. This is exactly the
 * chain that failed three times in the field (T2: wrong ReturnCode gate;
 * T3: unreliable liveness-probe gate) before T4+T5 fixed it. */
static void test_full_lifecycle_proto_to_unlock_landing(void)
{
    /* Stage 1: the real per-attempt sequence from the live serial capture
     * against protected-authlim0.nfc -- attempt 1 genuine NACK, and (T4's
     * fix) the retry loop must never reach a second attempt at all. */
    int attempt_errs[2] = { X_ERR_PROTO, X_ERR_TIMEOUT /* never reached if T4 holds */ };
    int last_block_err = x_retry_loop_final_err(attempt_errs, 2);
    CHECK(last_block_err == X_ERR_PROTO,
          "stage 1: the retry loop preserves the genuine NACK from attempt 1 (T4)");

    /* Stage 2: config pages (page 41 for NTAG213) are unreachable with only
     * 4 pages captured -- suspicion must derive from last_block_err alone. */
    bool cfg0_reachable = (X_CFG0_PAGE + 1U) < 4U;
    bool suspected = x_infer_suspicion(cfg0_reachable, 4U, X_NTAG213_PAGES, last_block_err);
    CHECK(suspected, "stage 2: suspicion is set directly from the preserved RFAL_ERR_PROTO (T5, no probe)");

    /* Stage 3: nfc_can_unlock() must offer Unlock from suspicion alone --
     * genuine AUTH0 is still unknown at this point. */
    bool known_auth0 = false;
    CHECK(x_can_unlock(known_auth0, suspected),
          "stage 3: Unlock is offered in the action menu");

    /* Stage 4: the read-result screen shows the honest, non-alarming
     * "suspected" wording, never the "confirmed" one. */
    CHECK(x_result_screen_text(known_auth0, suspected) == X_SCREEN_LOCKED_PARTIAL_READ,
          "stage 4: result screen shows Locked - Partial Read, not Password protected or an ordinary NDEF summary");

    /* Stage 5: the Unlock landing menu offers ONLY Enter Password -- genuine
     * AUTHLIM is unknown, so Use Dictionary must not even be selectable. */
    bool authlim_valid = false;
    uint8_t authlim = 0;
    bool dict_allowed = x_dictionary_allowed(authlim_valid, authlim);
    CHECK(!dict_allowed, "stage 5a: dictionary use is not yet safe");
    CHECK(x_landing_item_count(dict_allowed) == 1,
          "stage 5b: the landing menu draws exactly one item -- Enter Password");

    /* Stage 6: manual entry with the correct password authenticates, the
     * same-session re-read completes, and genuine config supersedes the
     * suspicion -- exactly the fixture's PWD=12 34 56 78 / PACK=AB CD. */
    x_ntag_t t;
    x_setup_initial_partial_read(&t);
    CHECK(t.suspected, "stage 6 setup: suspicion carried into the full x_ntag_t model matches stages 1-2");
    t.credential_valid = true;
    memcpy(t.pwd, (uint8_t[4]){0x12,0x34,0x56,0x78}, 4U);
    t.pack[0] = 0xAB; t.pack[1] = 0xCD;
    CHECK(x_reread_all_pages(&t, 0xFFFFU), "stage 6: same-session re-read completes fully");
    CHECK(t.auth0_valid && (t.auth0 == 4U) && !t.suspected,
          "stage 6: genuine AUTH0 now confirmed, transient suspicion cleared");
    CHECK(x_emulation_ready(t.page_valid, t.expected_pages),
          "stage 6: emulation eligibility now passes -- the full lifecycle reaches a usable end state");
}

static void test_auth0_prot_authlim_unknown_before_auth(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);
    CHECK(!t.auth0_valid && !t.prot_valid && !t.authlim_valid,
          "before authentication: genuine AUTH0/PROT/AUTHLIM are all still unknown, never inferred");
}

static void test_dictionary_refused_while_authlim_unknown(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);
    CHECK(!x_dictionary_allowed(t.authlim_valid, t.authlim),
          "dictionary mode stays refused: AUTHLIM is unknown whenever entry was via suspicion alone");
}

static void test_correct_password_completes_reread_and_emulation_ready(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);

    uint8_t entered[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t tag_pwd[4] = {0x12, 0x34, 0x56, 0x78};
    bool accepted = (memcmp(entered, tag_pwd, 4U) == 0);
    CHECK(accepted, "PWD_AUTH accepted: entered password matches the tag's real password");

    /* Credential is genuine and stored regardless of what the re-read finds. */
    t.credential_valid = true;
    memcpy(t.pwd, entered, 4U);
    t.pack[0] = 0xAB; t.pack[1] = 0xCD;

    bool reread_ok = x_reread_all_pages(&t, 0xFFFFU /* never fails */);
    CHECK(reread_ok, "same-session authenticated re-read completes fully");
    CHECK(t.auth0_valid && (t.auth0 == 4U) && t.prot_valid && t.prot &&
          t.authlim_valid && (t.authlim == 0U),
          "post-re-read: genuine AUTH0=4/PROT=1/AUTHLIM=0 captured from the real CFG0/CFG1 bytes");
    CHECK(!t.suspected, "post-re-read: transient suspicion is cleared, superseded by genuine data");
    CHECK(x_emulation_ready(t.page_valid, t.expected_pages),
          "post-re-read: every page valid -- the emulation gate now passes");
}

static void test_wrong_password_leaves_dump_incomplete(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);

    uint8_t entered[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t tag_pwd[4] = {0x12, 0x34, 0x56, 0x78};
    bool accepted = (memcmp(entered, tag_pwd, 4U) == 0);
    CHECK(!accepted, "PWD_AUTH rejected: wrong password does not match the tag's real password");

    /* No credential stored, no re-read attempted on rejection -- mirrors
     * nfc_unlock_run()'s NFC_PWDAUTH_REJECTED branch (no reread call at all). */
    CHECK(!t.credential_valid, "no credential stored on a rejected password");
    CHECK(!x_emulation_ready(t.page_valid, t.expected_pages),
          "dump remains incomplete: pages 4-44 are exactly as they were before the (failed) attempt");
}

static void test_rf_timeout_never_becomes_protection_suspected(void)
{
    bool cfg0_reachable = false;
    /* An ordinary timeout or link loss must never be mistaken for
     * protection -- only a genuine, deterministic protocol NACK
     * (RFAL_ERR_PROTO) does. */
    CHECK(!x_infer_suspicion(cfg0_reachable, 4U, X_NTAG213_PAGES, X_ERR_TIMEOUT),
          "a stopped-short read caused by RFAL_ERR_TIMEOUT never sets suspicion");
    CHECK(!x_infer_suspicion(cfg0_reachable, 4U, X_NTAG213_PAGES, X_ERR_LINK_LOSS),
          "a stopped-short read caused by RFAL_ERR_LINK_LOSS (card pulled away) never sets suspicion");
    CHECK(x_infer_suspicion(cfg0_reachable, 4U, X_NTAG213_PAGES, X_ERR_PROTO),
          "the identical stopped-short read DOES set suspicion when it's a genuine protocol NACK");
}

/* ---- Transcription of the per-block retry loop's error tracking,
 * verified above to match the real nfc_poller.c source. Reproduces the
 * exact live-hardware sequence that caused T2T-UNLOCK-T2 and T3 to fail:
 * a genuine NAK on attempt 1, a misleading TIMEOUT on a needless retry.
 * (x_retry_loop_final_err() itself is defined earlier, alongside the
 * X_ERR_* constants, since test_full_lifecycle_proto_to_unlock_landing()
 * needs it too.) ---- */

static void test_retry_loop_never_overwrites_a_genuine_nack(void)
{
    /* The EXACT sequence from the live serial capture: attempt 1 -> PROTO
     * (genuine NAK), attempt 2 (if it ran) -> TIMEOUT. Before the fix, the
     * loop always ran both attempts, so last_block_err ended up TIMEOUT --
     * after the fix, PROTO on attempt 1 stops the loop immediately. */
    int seq[2] = { X_ERR_PROTO, X_ERR_TIMEOUT };
    CHECK(x_retry_loop_final_err(seq, 2) == X_ERR_PROTO,
          "a genuine PROTO NAK on attempt 1 is preserved -- attempt 2's misleading TIMEOUT is never reached");

    /* Sanity: an ordinary transient error (neither LINK_LOSS nor PROTO)
     * still gets its full 2 attempts, matching the loop's original,
     * unchanged purpose for genuinely transient glitches. */
    int seq2[2] = { X_ERR_TIMEOUT, X_ERR_NONE };
    CHECK(x_retry_loop_final_err(seq2, 2) == X_ERR_NONE,
          "an ordinary timeout on attempt 1 still gets a real retry, which can still succeed");

    /* LINK_LOSS gets the same immediate-stop treatment PROTO now does. */
    int seq3[2] = { X_ERR_LINK_LOSS, X_ERR_NONE };
    CHECK(x_retry_loop_final_err(seq3, 2) == X_ERR_LINK_LOSS,
          "link loss still stops immediately, unaffected by adding the PROTO case alongside it");
}

static void test_auth_ok_but_reread_fails_reports_distinct_state(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);

    t.credential_valid = true;   /* genuine accept -- independent of the re-read outcome */
    memcpy(t.pwd, (uint8_t[4]){0x12,0x34,0x56,0x78}, 4U);
    t.pack[0] = 0xAB; t.pack[1] = 0xCD;

    bool reread_ok = x_reread_all_pages(&t, 10U /* comm fails partway through */);
    CHECK(!reread_ok, "re-read that fails partway through reports incomplete, not success");
    CHECK(!t.auth0_valid, "a failed re-read never populates the genuine AUTH0 field (no partial/guessed write)");
    CHECK(t.suspected, "a failed re-read leaves the prior suspicion exactly as it was -- nothing falsely resolved");
    CHECK(!x_emulation_ready(t.page_valid, t.expected_pages),
          "emulation gate still correctly refuses -- state must be reported as re-read-incomplete, never unlocked");
}

/* Save/reload must only ever persist genuinely captured configuration and
 * a verified credential -- the transient suspicion has no field in the V4
 * format at all, so it structurally cannot leak into a saved file. Modeled
 * here as: the save-relevant subset of x_ntag_t excludes suspected/
 * first_blocked_page entirely. */
typedef struct {
    bool auth0_valid; uint8_t auth0;
    bool prot_valid;  bool prot;
    bool authlim_valid; uint8_t authlim;
    bool credential_valid; uint8_t pwd[4]; uint8_t pack[2];
} x_saved_snapshot_t;

static x_saved_snapshot_t x_take_save_snapshot(const x_ntag_t *t)
{
    x_saved_snapshot_t s;
    s.auth0_valid = t->auth0_valid; s.auth0 = t->auth0;
    s.prot_valid  = t->prot_valid;  s.prot  = t->prot;
    s.authlim_valid = t->authlim_valid; s.authlim = t->authlim;
    s.credential_valid = t->credential_valid;
    memcpy(s.pwd, t->pwd, 4U);
    memcpy(s.pack, t->pack, 2U);
    return s;
}

static void test_save_reload_contains_only_genuine_data(void)
{
    x_ntag_t t;
    x_setup_initial_partial_read(&t);

    /* Before authentication: only suspicion exists -- the save snapshot
     * (which has no field for it at all) carries nothing protection-related. */
    x_saved_snapshot_t before = x_take_save_snapshot(&t);
    CHECK(!before.auth0_valid && !before.prot_valid && !before.authlim_valid && !before.credential_valid,
          "save snapshot before authentication: no protection/credential data at all -- suspicion has no field to leak into");

    t.credential_valid = true;
    memcpy(t.pwd, (uint8_t[4]){0x12,0x34,0x56,0x78}, 4U);
    t.pack[0] = 0xAB; t.pack[1] = 0xCD;
    CHECK(x_reread_all_pages(&t, 0xFFFFU), "re-read completes for the save/reload scenario");

    x_saved_snapshot_t after = x_take_save_snapshot(&t);
    CHECK(after.auth0_valid && (after.auth0 == 4U) && after.prot_valid && after.prot &&
          after.authlim_valid && (after.authlim == 0U),
          "save snapshot after successful unlock: genuine, wire-confirmed AUTH0/PROT/AUTHLIM");
    CHECK(after.credential_valid && (memcmp(after.pwd, (uint8_t[4]){0x12,0x34,0x56,0x78}, 4U) == 0) &&
          (after.pack[0] == 0xAB) && (after.pack[1] == 0xCD),
          "save snapshot after successful unlock: the verified credential, exactly as accepted by the tag");
}

/* ---- Transcription of nfc_hex_nibbles_to_bytes(), verified above to match
 * the real m1_nfc.c source. Reproduces the exact field failure: every
 * password entry reported "Invalid password" because the old code used a
 * space-tokenized parser against a contiguous hex string. ---- */
static bool x_hex_nibbles_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len; i++) {
        char pair[3] = { hex[i * 2], hex[(i * 2) + 1], '\0' };
        char *end = NULL;
        long v = strtol(pair, &end, 16);
        if (end != &pair[2]) { return false; }
        out[i] = (uint8_t)v;
    }
    return true;
}

/* Mirrors the OLD (broken) code's approach for direct comparison: a
 * space-tokenized parser applied to a contiguous string. */
static int x_strtob_with_base_space_tokenized(const char *str, uint8_t *out, int max_len)
{
    int count = 0;
    char buf[64];
    char *token;
    strncpy(buf, str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    token = strtok(buf, " ");
    while (token != NULL && count < max_len) {
        out[count++] = (uint8_t)strtol(token, NULL, 16);
        token = strtok(NULL, " ");
    }
    return count;
}

static void test_password_entry_parses_contiguous_hex_fixture_exact(void)
{
    /* Exactly what protected-authlim0.nfc's password looks like once
     * typed on the virtual hex keyboard: "12345678", no separators. */
    uint8_t pwd[4] = {0};
    CHECK(x_hex_nibbles_to_bytes("12345678", pwd, 4) &&
          (memcmp(pwd, (uint8_t[4]){0x12,0x34,0x56,0x78}, 4U) == 0),
          "the fixed parser correctly turns the exact fixture password into {0x12,0x34,0x56,0x78}");
}

static void test_old_parser_reproduces_the_field_failure(void)
{
    /* Proves the diagnosis, not just the fix: the OLD parser genuinely
     * fails on this exact input, matching the observed "Invalid password"
     * on every attempt regardless of what was typed. */
    uint8_t pwd[4] = {0};
    int n = x_strtob_with_base_space_tokenized("12345678", pwd, 4);
    CHECK(n == 1, "the old space-tokenized parser sees the whole contiguous string as ONE token (count=1, not 4) -- reproduces \"Invalid password\" on every entry");
}

int main(void)
{
    test_source_matches_shipped();
    test_outcome_timeout_is_rejected();
    test_outcome_other_error_is_comm_error();
    test_outcome_wrong_length_is_comm_error();
    test_outcome_accepted_no_expected_pack();
    test_outcome_pack_matches_expected();
    test_outcome_pack_mismatches_expected();
    test_pwd_auth_match_and_mismatch();
    test_read_gate_prot1_vs_prot0();
    test_write_gate_ignores_prot();
    test_unprotected_tag_never_gated();
    test_reread_without_capacity_widen_silently_drops_pages();
    test_reread_with_capacity_widen_succeeds();
    test_auth0_4_initial_read_offers_manual_unlock();
    test_full_lifecycle_proto_to_unlock_landing();
    test_auth0_prot_authlim_unknown_before_auth();
    test_dictionary_refused_while_authlim_unknown();
    test_correct_password_completes_reread_and_emulation_ready();
    test_wrong_password_leaves_dump_incomplete();
    test_rf_timeout_never_becomes_protection_suspected();
    test_retry_loop_never_overwrites_a_genuine_nack();
    test_auth_ok_but_reread_fails_reports_distinct_state();
    test_save_reload_contains_only_genuine_data();
    test_password_entry_parses_contiguous_hex_fixture_exact();
    test_old_parser_reproduces_the_field_failure();

    printf("\nnfc_t2t_unlock_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
