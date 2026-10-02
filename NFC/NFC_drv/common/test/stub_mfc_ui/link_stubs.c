/* Link-satisfying stubs for symbols the verbatim-extracted production code
 * (nfc_mfc_dict_ui_draw_extracted.c) REFERENCES but every render scenario in
 * nfc_mfc_dict_ui_render_test.c is designed to never actually reach at
 * runtime -- each is guarded out by a condition the test's fixtures always
 * satisfy before the real call site is reached (documented per-function
 * below, and cross-referenced from the render test's own header comment).
 * Every body aborts loudly if it IS ever actually invoked, so a scenario
 * that somehow does reach one fails immediately and visibly instead of
 * silently rendering wrong pixels or a wrong action-menu label.
 */
#include "nfc_ctx.h"
#include "mfc_key_source.h"
#include <stdio.h>
#include <stdlib.h>

static void unreachable(const char *fn)
{
    fprintf(stderr,
        "link_stubs.c: %s was actually called -- a render scenario reached "
        "code this test's fixtures were designed to make unreachable. This "
        "is a real bug in the test (or a real behavior change in the "
        "production code being tested), not something to silently paper "
        "over.\n", fn);
    abort();
}

/* Only reached from nfc_mfc_dict_totals_ensure() (m1_nfc.c, verbatim-
 * extracted) when its own s_mfc_dict_totals_computed guard is false. Every
 * dictionary-progress render scenario sets s_mfc_dict_totals_computed = true
 * directly before calling nfc_mfc_dict_progress_draw() (see the render
 * test's own header comment, "Faking totals"), so nfc_mfc_dict_totals_ensure()
 * itself always returns on its very first line and this is never invoked. */
void m1_mfc_build_key_sources(mfc_key_source_cfg_t cfgs[2], const char *user_paths[2], const char *sys_paths[2])
{
    (void)cfgs; (void)user_paths; (void)sys_paths;
    unreachable("m1_mfc_build_key_sources");
}

mfc_key_source_count_t mfc_key_source_count(const mfc_key_source_cfg_t *sources, size_t n_sources)
{
    (void)sources; (void)n_sources;
    unreachable("mfc_key_source_count");
    mfc_key_source_count_t z; z.count = 0; z.overflowed = false;
    return z;
}

/* Only reached from nfc_can_write_ntag21x()/nfc_can_unlock() (m1_nfc.c,
 * verbatim-extracted) past their own leading
 * `c->head.family != M1NFC_FAM_ULTRALIGHT` guard. Every render scenario's
 * nfc_ctx_get()->head.family is M1NFC_FAM_CLASSIC, so these three are never
 * actually invoked. */
uint8_t nfc_ctx_get_t2t_variant(void)
{
    unreachable("nfc_ctx_get_t2t_variant");
    return 0;
}

bool nfc_ctx_get_t2t_auth0(uint8_t *out)
{
    (void)out;
    unreachable("nfc_ctx_get_t2t_auth0");
    return false;
}

bool nfc_ctx_get_t2t_protection_suspected(uint16_t *first_blocked_page_out)
{
    (void)first_blocked_page_out;
    unreachable("nfc_ctx_get_t2t_protection_suspected");
    return false;
}
