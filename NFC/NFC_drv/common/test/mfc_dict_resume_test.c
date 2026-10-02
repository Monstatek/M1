/* Host test for mfc_dict_resume.c -- the real production module (compiled
 * directly into this binary below), not a transcribed copy.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      ../mfc_dict_resume.c mfc_dict_resume_test.c -o /tmp/mdr && /tmp/mdr
 */
#include "../mfc_dict_resume.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

static void fill_committed(mfc_dict_resume_t *r)
{
    mfc_dict_resume_reset(r);
    uint8_t k[MFC_KEY_SIZE] = {1, 2, 3, 4, 5, 6};
    memcpy(r->seen[0], k, MFC_KEY_SIZE);
    r->seen_n = 1;
    r->sys_valid       = true;
    strncpy(r->sys_path, "0:/mfc/system.txt", sizeof(r->sys_path) - 1);
    r->sys_file_size   = 12345;
    r->sys_file_date   = 0x5A21;
    r->sys_file_time   = 0x3C10;
    r->sys_byte_offset = 4096;
}

static void test_reset_is_fully_fresh(void)
{
    mfc_dict_resume_t r;
    fill_committed(&r);
    mfc_dict_resume_reset(&r);

    CHECK(r.seen_n == 0U, "reset clears seen_n");
    CHECK(!r.seen_overflowed, "reset clears seen_overflowed");
    CHECK(!r.sys_valid, "reset invalidates the System cursor");
    CHECK(r.sys_byte_offset == 0U, "reset zeroes the byte offset");
    CHECK(!mfc_dict_resume_system_identity_matches(&r, "0:/mfc/system.txt", 12345, 0x5A21, 0x3C10),
          "a freshly reset cursor never matches any file, even one that would have matched before reset");
}

static void test_system_only_reset_preserves_seen(void)
{
    mfc_dict_resume_t r;
    fill_committed(&r);

    mfc_dict_resume_reset_system_only(&r);

    CHECK(r.seen_n == 1U, "seen[]/seen_n survive a System-only reset");
    CHECK(memcmp(r.seen[0], (uint8_t[]){1, 2, 3, 4, 5, 6}, MFC_KEY_SIZE) == 0,
          "the surviving seen[] entry is byte-identical to what was committed");
    CHECK(!r.sys_valid, "the System cursor itself is invalidated");
    CHECK(r.sys_byte_offset == 0U, "the byte offset is zeroed -- a fresh System sweep from the top");
    CHECK(r.sys_path[0] == '\0', "the stored path is cleared");
}

static void test_identity_exact_match(void)
{
    mfc_dict_resume_t r;
    fill_committed(&r);

    CHECK(mfc_dict_resume_system_identity_matches(&r, "0:/mfc/system.txt", 12345, 0x5A21, 0x3C10),
          "identical path/size/date/time matches");
}

static void test_identity_mismatches(void)
{
    mfc_dict_resume_t r;

    fill_committed(&r);
    CHECK(!mfc_dict_resume_system_identity_matches(&r, "0:/mfc/OTHER.txt", 12345, 0x5A21, 0x3C10),
          "a different path never matches");

    fill_committed(&r);
    CHECK(!mfc_dict_resume_system_identity_matches(&r, "0:/mfc/system.txt", 99999, 0x5A21, 0x3C10),
          "a different size never matches -- the file was edited/replaced");

    fill_committed(&r);
    CHECK(!mfc_dict_resume_system_identity_matches(&r, "0:/mfc/system.txt", 12345, 0x0001, 0x3C10),
          "a different FAT date never matches");

    fill_committed(&r);
    CHECK(!mfc_dict_resume_system_identity_matches(&r, "0:/mfc/system.txt", 12345, 0x5A21, 0x0001),
          "a different FAT time never matches");

    fill_committed(&r);
    CHECK(!mfc_dict_resume_system_identity_matches(&r, NULL, 12345, 0x5A21, 0x3C10),
          "a NULL path candidate never matches");

    mfc_dict_resume_t fresh;
    mfc_dict_resume_reset(&fresh);
    CHECK(!mfc_dict_resume_system_identity_matches(&fresh, "0:/mfc/system.txt", 12345, 0x5A21, 0x3C10),
          "no committed cursor (sys_valid=false) never matches, regardless of the candidate values");
}

static void test_identity_null_resume(void)
{
    CHECK(!mfc_dict_resume_system_identity_matches(NULL, "0:/mfc/system.txt", 12345, 0x5A21, 0x3C10),
          "a NULL resume pointer never matches");
}

int main(void)
{
    test_reset_is_fully_fresh();
    test_system_only_reset_preserves_seen();
    test_identity_exact_match();
    test_identity_mismatches();
    test_identity_null_resume();

    printf("mfc_dict_resume_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
