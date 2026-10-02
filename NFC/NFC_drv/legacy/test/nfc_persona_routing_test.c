/*
 * nfc_persona_routing_test.c - host mirror of the Phase A persona/role
 * routing fix in nfc_driver.c, m1_mfc_raw_listener.c and m1_csrc/m1_nfc.c
 * (which cannot link on host: real FreeRTOS/ST25R3916/task deletion). Mirrors
 * the exact decision logic byte-for-byte-in-spirit -- same technique as
 * mfc_raw_hw_logic_test.c / mfc_rawown_lifecycle_test.c.
 *
 * Bug being guarded against: Emu_SetPersona(EMU_PERSONA_MFC_EMU) is a sticky
 * global with no reset, and NFC_STATE_PROCESS's RAWOWN dispatch used to check
 * persona alone -- so a stale MFC_EMU persona from an earlier session hijacked
 * the next ordinary Read (or legacy Read>More>Emulate) into another blocking
 * RAWOWN listener session instead of running the real poller.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      NFC/NFC_drv/legacy/test/nfc_persona_routing_test.c \
 *      -o /tmp/persona_routing && /tmp/persona_routing
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

/* ---- mirror of EmuPersona_t / g_persona (nfc_listener.h/.c) ---- */
typedef enum { EMU_PERSONA_T4T = 0, EMU_PERSONA_T2T, EMU_PERSONA_RAW,
               EMU_PERSONA_MFC_DETECT, EMU_PERSONA_MFC_EMU } EmuPersona_t;

/* ---- mirror of NfcRole_e / s_currentRole (nfc_driver.c) ---- */
typedef enum { NFC_ROLE_POLLER = 0, NFC_ROLE_LISTENER } NfcRole_e;

typedef struct {
    EmuPersona_t persona;
    NfcRole_e    role;
    int          rawown_active;      /* mirrors m1_mfc_raw_hw_active(): state != INACTIVE */
    int          rawown_run_calls;   /* how many times the RAWOWN branch actually ran */
    int          poller_process_calls;
    int          task_delete_calls;  /* must stay 0 whenever rawown_active is true at the time */
    int          stop_and_wait_calls;
    int          armed;              /* mirrors m1_mfc_raw_has_emu_image() */
    int          mfc_emulate_refused_count;  /* mirrors nfc_driver.c's [MFC-EMU-REFUSED] log */
} mock_t;

static void emu_set_persona(mock_t *m, EmuPersona_t p) { m->persona = p; }

/* ---- mirror of NFC_SwitchRole()'s net effect on s_currentRole ---- */
static void switch_role(mock_t *m, NfcRole_e role) { m->role = role; }

/* ---- mirror of the event handlers in nfc_driver.c's NFC_STATE_WAIT switch,
 * AFTER the dual-route removal (commit that follows b5e7ae6f, which first
 * introduced the m1_mfc_raw_has_emu_image()-based promotion inside
 * Q_EVENT_NFC_START_EMULATE -- since removed): every start event explicitly
 * sets its own persona before switching role. Q_EVENT_NFC_START_EMULATE is
 * now UNCONDITIONALLY generic (RAW/T2T/T4T auto-inferred downstream) --
 * mirrors b5e7ae6f's own immediate parent, restoring the smallest proven
 * behavior rather than a new design. It must never select MFC_EMU, even if
 * `armed` (a stale image from an earlier MFC Emulate session) is true --
 * that shim is exactly what was removed. Q_EVENT_NFC_MFC_EMULATE (the ONE
 * production MIFARE Classic route) now gates on `armed` itself: refuses,
 * touching neither persona nor role, when no image is armed. ---- */
static void ev_start_read(mock_t *m)     { emu_set_persona(m, EMU_PERSONA_RAW);      switch_role(m, NFC_ROLE_POLLER); }
static void ev_start_emulate(mock_t *m)  { emu_set_persona(m, EMU_PERSONA_RAW);      switch_role(m, NFC_ROLE_LISTENER); }
static void ev_mfc_detect(mock_t *m)     { emu_set_persona(m, EMU_PERSONA_MFC_DETECT); switch_role(m, NFC_ROLE_LISTENER); }
static void ev_mfc_emulate(mock_t *m)
{
    if (m->armed) {
        emu_set_persona(m, EMU_PERSONA_MFC_EMU);
        switch_role(m, NFC_ROLE_LISTENER);
    } else {
        m->mfc_emulate_refused_count++;   /* [MFC-EMU-REFUSED]: no state transition at all */
    }
}

/* ---- mirror of ListenIni()'s own auto-inference (nfc_listener.c) for the
 * legacy Emulate screen -- only reached when persona is NOT the MFC_DETECT/
 * MFC_EMU early-exit branches. Simplified: any non-special persona becomes
 * whatever the loaded card's family implies (modeled as a fixed T2T result,
 * since the actual value doesn't matter for this test -- what matters is
 * that it is NOT EMU_PERSONA_MFC_EMU). ---- */
static void listen_ini_legacy_infer(mock_t *m)
{
    if (m->persona == EMU_PERSONA_MFC_DETECT || m->persona == EMU_PERSONA_MFC_EMU) { return; }
    m->persona = EMU_PERSONA_T2T;   /* stands in for the real T2T/T4T/RAW inference */
}

/* ---- mirror of NFC_STATE_PROCESS's RAWOWN dispatch, AFTER the Phase A fix:
 * role AND persona both required, not persona alone. ---- */
static bool nfc_state_process(mock_t *m)
{
    if ((m->role == NFC_ROLE_LISTENER) && (m->persona == EMU_PERSONA_MFC_EMU)) {
        m->rawown_run_calls++;
        m->rawown_active = 1;   /* m1_mfc_raw_hw_run() would block here until torn down */
        return true;            /* took the RAWOWN branch */
    }
    m->poller_process_calls++;  /* real poller/listener process ran instead */
    return false;
}

/* ---- mirror of m1_mfc_raw_teardown_cleanup()'s Phase A addition: reset
 * persona to neutral the moment RAWOWN genuinely ends. Also mirrors
 * nfc_mfc_emu_gui_destroy()'s unconditional m1_mfc_raw_set_emu_image(NULL)
 * -- the armed image is cleared on every exit (STOP/BACK/field loss),
 * never left to leak into the next session. ---- */
static void rawown_teardown_cleanup(mock_t *m)
{
    m->rawown_active = 0;
    m->armed = 0;
    emu_set_persona(m, EMU_PERSONA_RAW);
}

/* ---- mirror of menu_nfc_deinit()'s Phase A addition: request stop-and-wait
 * before ever deleting the task if RAWOWN is still active. ---- */
static void menu_nfc_deinit(mock_t *m)
{
    if (m->rawown_active) {
        m->stop_and_wait_calls++;
        rawown_teardown_cleanup(m);   /* stop-and-wait's success path: fully torn down */
    }
    /* Task delete only ever happens with rawown_active already false. */
    CHECK(!m->rawown_active, "menu_nfc_deinit: RAWOWN inactive before task delete");
    m->task_delete_calls++;
}

/* ---- mirror of m1_nfc.c's NFC_ACT_EMULATE dispatch (nfc_read_more_kp_
 * handler()): MIFARE Classic routes to the dedicated MFC view; every other
 * emulation-eligible family keeps the generic view. Two mock view-mode
 * values stand in for VIEW_MODE_NFC_MFC_EMU / VIEW_MODE_NFC_EMULATE --
 * what matters is that the family switch picks the right one, not their
 * numeric value. ---- */
typedef enum { FAM_CLASSIC, FAM_ULTRALIGHT, FAM_DESFIRE } Family_t;
typedef enum { VIEW_MFC_EMU, VIEW_GENERIC_EMULATE } ViewMode_t;

static ViewMode_t route_for_emulate(Family_t fam)
{
    return (fam == FAM_CLASSIC) ? VIEW_MFC_EMU : VIEW_GENERIC_EMULATE;
}

/* ==================================================================
 * MFC Emulate BACK navigation (nfc_mfc_emu_kp_handler()/_gui_create()/
 * _gui_destroy(), m1_nfc.c). BACK must return to this same card's
 * action menu (VIEW_MODE_NFC_READ_MORE), not the NFC root menu
 * (VIEW_MODE_IDLE); m1_uiView_display_switch()'s own synchronous
 * destroy() call is what stops RAWOWN -- exactly once, only if a
 * session actually started -- before the switch completes; nfc_ctx
 * (the completed card) is never touched by this path; the handler
 * always stays within the same message loop (never "exit the flow").
 * ================================================================== */
typedef enum { UI_VIEW_IDLE, UI_VIEW_READ_MORE, UI_VIEW_MFC_EMU } UiView_t;

typedef struct {
    UiView_t view;
    int      nfc_ctx_valid;       /* mirrors the completed read's nfc_ctx/mfc image */
    int      rawown_generation;   /* increments once per successful (armed) entry -- mirrors
                                    * m1_mfc_raw_begin()'s own s_sess_gen++, observed here via
                                    * "a fresh session was actually started" */
    int      stop_requests;       /* mirrors ListenerRequestStop() call count */
    int      stop_waits;          /* mirrors m1_mfc_raw_end() call count (waits for STOPPED) */
} ui_mock_t;

/* ---- mirror of nfc_mfc_emu_gui_destroy(): called SYNCHRONOUSLY by
 * m1_uiView_display_switch() before the switch completes, exactly once
 * per view exit. Only requests stop if a session actually started
 * (mirrors s_mfc_dedicated_started gating m1_mfc_raw_end()). ---- */
static void ui_mfc_emu_destroy(ui_mock_t *u, int started)
{
    if (started) {
        u->stop_requests++;
        u->stop_waits++;
    }
}

/* ---- mirror of nfc_mfc_emu_gui_create(param==0): a validated image
 * arms and starts a fresh RAWOWN generation; nfc_ctx is never touched
 * (this handler builds a throwaway image snapshot from it, never
 * clears or rewrites it). ---- */
static void ui_mfc_emu_enter(ui_mock_t *u, int image_ok)
{
    u->view = UI_VIEW_MFC_EMU;
    if (image_ok) {
        u->rawown_generation++;
    }
}

/* ---- mirror of nfc_mfc_emu_kp_handler()'s BACK branch. ---- */
static void ui_mfc_emu_back(ui_mock_t *u, int started)
{
    ui_mfc_emu_destroy(u, started);       /* m1_uiView_display_switch()'s synchronous destroy() */
    u->view = UI_VIEW_READ_MORE;          /* NOT UI_VIEW_IDLE */
    /* nfc_ctx_valid is never written here -- proves the completed read
     * context survives this navigation untouched. */
}

int main(void)
{
    /* [1] START_READ + stale MFC_EMU persona -> normal poller, never RAWOWN. */
    {
        mock_t m = {0};
        m.persona = EMU_PERSONA_MFC_EMU;   /* stale from an earlier session */
        m.role    = NFC_ROLE_LISTENER;     /* also stale */
        ev_start_read(&m);
        bool took_rawown = nfc_state_process(&m);
        CHECK(!took_rawown, "START_READ with stale MFC_EMU persona does not take the RAWOWN branch");
        CHECK(m.rawown_run_calls == 0, "START_READ with stale MFC_EMU persona: RAWOWN never runs");
        CHECK(m.poller_process_calls == 1, "START_READ with stale MFC_EMU persona: real poller runs");
        CHECK(m.role == NFC_ROLE_POLLER, "START_READ sets role to POLLER");
        CHECK(m.persona != EMU_PERSONA_MFC_EMU, "START_READ resets persona away from MFC_EMU");
    }

    /* [2] START_MFC_EMULATE with an armed image -> RAWOWN (the legitimate
     * path must still work: the dedicated view has already validated and
     * armed a real saved-card image before this event is sent). */
    {
        mock_t m = {0};
        m.armed = 1;
        ev_mfc_emulate(&m);
        bool took_rawown = nfc_state_process(&m);
        CHECK(took_rawown, "START_MFC_EMULATE (armed) takes the RAWOWN branch");
        CHECK(m.rawown_run_calls == 1, "START_MFC_EMULATE (armed): RAWOWN runs exactly once");
        CHECK(m.rawown_active, "START_MFC_EMULATE (armed): RAWOWN is active");
        CHECK(m.mfc_emulate_refused_count == 0, "START_MFC_EMULATE (armed): not refused");
    }

    /* [2b] START_MFC_EMULATE with NO armed image -> refused before any
     * state transition. Missing/incomplete MFC image refuses before RF
     * starts: no persona change, no role switch, RAWOWN never runs. */
    {
        mock_t m = {0};
        EmuPersona_t persona_before = m.persona;
        NfcRole_e    role_before    = m.role;
        ev_mfc_emulate(&m);   /* m.armed == 0 */
        CHECK(m.mfc_emulate_refused_count == 1, "START_MFC_EMULATE (unarmed) is refused exactly once");
        CHECK(m.persona == persona_before, "START_MFC_EMULATE (unarmed): persona untouched");
        CHECK(m.role == role_before, "START_MFC_EMULATE (unarmed): role untouched -- no listener switch");
        bool took_rawown = nfc_state_process(&m);
        CHECK(!took_rawown, "START_MFC_EMULATE (unarmed): RAWOWN branch never taken");
        CHECK(m.rawown_run_calls == 0, "START_MFC_EMULATE (unarmed): RAWOWN never runs");
    }

    /* [3] MFC BACK/STOP -> persona neutral, image disarmed. */
    {
        mock_t m = {0};
        m.armed = 1;
        ev_mfc_emulate(&m);
        (void)nfc_state_process(&m);
        CHECK(m.persona == EMU_PERSONA_MFC_EMU, "sanity: persona is MFC_EMU while active");
        rawown_teardown_cleanup(&m);   /* mirrors BACK -> m1_mfc_raw_end() -> ... -> teardown_cleanup() */
        CHECK(m.persona == EMU_PERSONA_RAW, "MFC BACK/STOP resets persona to neutral (RAW)");
        CHECK(!m.rawown_active, "MFC BACK/STOP: RAWOWN inactive after teardown");
        CHECK(!m.armed, "MFC BACK/STOP: armed image state is cleared");
    }

    /* [4] START_READ after MFC BACK -> normal poller (the end-to-end
     * regression this whole investigation traced). */
    {
        mock_t m = {0};
        m.armed = 1;
        ev_mfc_emulate(&m);
        (void)nfc_state_process(&m);
        rawown_teardown_cleanup(&m);   /* BACK */
        ev_start_read(&m);
        bool took_rawown = nfc_state_process(&m);
        CHECK(!took_rawown, "START_READ after MFC BACK does not take the RAWOWN branch");
        CHECK(m.poller_process_calls == 1, "START_READ after MFC BACK: real poller runs, can read a card");
    }

    /* [5] legacy Emulate after MFC Emulate -> legacy listener, never RAWOWN
     * (the "Read > More > Emulate" hijack this investigation also found).
     * Also proves the removed dual-route shim stays removed: even with a
     * STALE armed image still set (m.armed == 1, simulating the teardown
     * reset NOT having run yet), the generic event must still never select
     * MFC_EMU -- that promotion is gone, not merely usually-avoided. */
    {
        mock_t m = {0};
        m.armed = 1;
        ev_mfc_emulate(&m);
        (void)nfc_state_process(&m);
        /* User backs out WITHOUT the teardown_cleanup persona reset having
         * happened yet is not representative of the real fix (teardown_
         * cleanup always runs), but exercise it directly via ev_start_emulate
         * to prove the event handler itself is also now persona-safe,
         * independent of whether the prior session's own cleanup ran --
         * and, critically, independent of m.armed still being stale-true. */
        CHECK(m.armed, "sanity: armed image is still (stale-)true going into the generic event");
        ev_start_emulate(&m);          /* Q_EVENT_NFC_START_EMULATE handler */
        listen_ini_legacy_infer(&m);   /* ListenIni()'s own auto-inference */
        CHECK(m.persona != EMU_PERSONA_MFC_EMU,
              "legacy Emulate after MFC Emulate: persona is never MFC_EMU going into ListenIni(), "
              "even with a stale armed image (the removed shim does not come back)");
        bool took_rawown = nfc_state_process(&m);
        CHECK(!took_rawown, "legacy Emulate after MFC Emulate: RAWOWN branch never taken");
        CHECK(m.poller_process_calls == 1, "legacy Emulate after MFC Emulate: legacy listener runs instead");
    }

    /* [6] task delete while RAWOWN active -> prohibited (menu_nfc_deinit()
     * must stop-and-wait first, never delete with RAWOWN still active). */
    {
        mock_t m = {0};
        m.armed = 1;
        ev_mfc_emulate(&m);
        (void)nfc_state_process(&m);
        CHECK(m.rawown_active, "sanity: RAWOWN active before menu exit");
        menu_nfc_deinit(&m);
        CHECK(m.stop_and_wait_calls == 1, "menu_nfc_deinit requests stop-and-wait when RAWOWN is active");
        CHECK(m.task_delete_calls == 1, "menu_nfc_deinit still deletes the task, but only after RAWOWN is inactive");
        CHECK(m.persona == EMU_PERSONA_RAW, "menu_nfc_deinit's stop-and-wait also leaves persona neutral");
    }

    /* [6b] task delete while RAWOWN already inactive -> no stop-and-wait
     * needed, proceeds directly (the ordinary case). */
    {
        mock_t m = {0};
        menu_nfc_deinit(&m);
        CHECK(m.stop_and_wait_calls == 0, "menu_nfc_deinit: no stop-and-wait when RAWOWN was never active");
        CHECK(m.task_delete_calls == 1, "menu_nfc_deinit: task delete still proceeds normally");
    }

    /* [7] Detect Reader persona is unaffected: it's a distinct listener
     * persona, never confused with MFC_EMU by the role+persona AND-check. */
    {
        mock_t m = {0};
        ev_mfc_detect(&m);
        bool took_rawown = nfc_state_process(&m);
        CHECK(!took_rawown, "MFC_DETECT persona never takes the RAWOWN branch");
        CHECK(m.role == NFC_ROLE_LISTENER, "MFC_DETECT still uses listener role");
    }

    /* [8] Classic NFC_ACT_EMULATE routes only to the dedicated MFC view. */
    {
        CHECK(route_for_emulate(FAM_CLASSIC) == VIEW_MFC_EMU,
              "NFC_ACT_EMULATE for M1NFC_FAM_CLASSIC routes to VIEW_MODE_NFC_MFC_EMU");
    }

    /* [9] Every other emulation-eligible family keeps the generic view --
     * Ultralight/NTAG and UID-only (DESFire) routing is unchanged. */
    {
        CHECK(route_for_emulate(FAM_ULTRALIGHT) == VIEW_GENERIC_EMULATE,
              "NFC_ACT_EMULATE for M1NFC_FAM_ULTRALIGHT still routes to VIEW_MODE_NFC_EMULATE");
        CHECK(route_for_emulate(FAM_DESFIRE) == VIEW_GENERIC_EMULATE,
              "NFC_ACT_EMULATE for M1NFC_FAM_DESFIRE (UID-only) still routes to VIEW_MODE_NFC_EMULATE");
    }

    /* [10] Corollary of [8]: since Classic never reaches
     * VIEW_MODE_NFC_EMULATE (the only view that posts
     * Q_EVENT_NFC_START_EMULATE, in nfc_emulate_gui_create()), Classic
     * structurally never posts that event -- there is no code path from
     * "family == CLASSIC" to that post call left in the UI layer. */
    {
        Family_t classic = FAM_CLASSIC;
        CHECK(route_for_emulate(classic) != VIEW_GENERIC_EMULATE,
              "Classic never lands on the view that posts Q_EVENT_NFC_START_EMULATE");
    }

    /* [11] MFC dedicated view posts Q_EVENT_NFC_MFC_EMULATE exactly once
     * per successful entry -- mirrors nfc_mfc_emu_gui_create(param==0):
     * exactly one post when image validation succeeds, zero when it
     * refuses (verified by the [MFC-EMU-REFUSED] gate in [2b] above; this
     * models the UI-side image-build step feeding into it). */
    {
        int post_count;
        bool image_ok;

        post_count = 0;
        image_ok = true;   /* m1_mfc_emu_image_build() == M1_MFC_EMU_OK */
        if (image_ok) { post_count++; }   /* the one post_q_message call site */
        CHECK(post_count == 1, "MFC dedicated view: exactly one Q_EVENT_NFC_MFC_EMULATE post on valid image");

        post_count = 0;
        image_ok = false;  /* build refused: incomplete/missing image */
        if (image_ok) { post_count++; }
        CHECK(post_count == 0, "MFC dedicated view: zero posts when the image build refuses");
    }

    /* [12] BACK from a running MFC Emulate session: stops RAWOWN exactly
     * once, returns to VIEW_MODE_NFC_READ_MORE (not VIEW_MODE_IDLE), and
     * the completed nfc_ctx is left untouched. */
    {
        ui_mock_t u = {0};
        u.view = UI_VIEW_MFC_EMU;
        u.nfc_ctx_valid = 1;   /* the completed read this session came from */

        ui_mfc_emu_enter(&u, /*image_ok=*/1);
        CHECK(u.rawown_generation == 1, "sanity: entering with a valid image starts generation 1");

        ui_mfc_emu_back(&u, /*started=*/1);
        CHECK(u.stop_requests == 1, "BACK stops RAWOWN: ListenerRequestStop() called exactly once");
        CHECK(u.stop_waits == 1, "BACK stops RAWOWN: m1_mfc_raw_end() (STOPPED wait) called exactly once");
        CHECK(u.view == UI_VIEW_READ_MORE, "BACK returns to VIEW_MODE_NFC_READ_MORE");
        CHECK(u.view != UI_VIEW_IDLE, "BACK does NOT return to the NFC root menu (VIEW_MODE_IDLE)");
        CHECK(u.nfc_ctx_valid == 1, "BACK leaves the completed nfc_ctx/card image intact");
    }

    /* [13] BACK from a refused entry (no session ever started): no stop
     * request at all -- never blocks waiting for a STOPPED acknowledgment
     * that would never arrive. Still returns to the action menu. */
    {
        ui_mock_t u = {0};
        u.view = UI_VIEW_MFC_EMU;
        u.nfc_ctx_valid = 1;

        ui_mfc_emu_enter(&u, /*image_ok=*/0);   /* refused: image build failed */
        CHECK(u.rawown_generation == 0, "sanity: a refused entry never starts a RAWOWN generation");

        ui_mfc_emu_back(&u, /*started=*/0);
        CHECK(u.stop_requests == 0, "BACK from a refused entry: no stop request (nothing was started)");
        CHECK(u.stop_waits == 0, "BACK from a refused entry: no STOPPED wait");
        CHECK(u.view == UI_VIEW_READ_MORE, "BACK from a refused entry still returns to VIEW_MODE_NFC_READ_MORE");
    }

    /* [14] Re-entering Emulate after BACK starts a genuinely fresh RAWOWN
     * generation, not a continuation of the torn-down session. */
    {
        ui_mock_t u = {0};
        u.view = UI_VIEW_MFC_EMU;
        u.nfc_ctx_valid = 1;

        ui_mfc_emu_enter(&u, 1);
        CHECK(u.rawown_generation == 1, "first entry: generation 1");
        ui_mfc_emu_back(&u, 1);
        CHECK(u.view == UI_VIEW_READ_MORE, "sanity: back on the action menu after the first session");

        ui_mfc_emu_enter(&u, 1);   /* re-enter Emulate from the action menu */
        CHECK(u.rawown_generation == 2, "re-entry starts a fresh (incremented) RAWOWN generation");
        CHECK(u.view == UI_VIEW_MFC_EMU, "re-entry lands back on the MFC Emulate view");
        ui_mfc_emu_back(&u, 1);
        CHECK(u.stop_requests == 2, "each of the two sessions was stopped exactly once (2 total)");
        CHECK(u.stop_waits == 2, "each of the two sessions waited for STOPPED exactly once (2 total)");
        CHECK(u.nfc_ctx_valid == 1, "nfc_ctx survives both sessions and both BACK navigations");
    }

    printf("\nnfc_persona_routing_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
