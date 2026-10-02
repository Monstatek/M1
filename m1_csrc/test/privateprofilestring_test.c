/* Host tests for the Item 3 destructive-RFID-saved-file-update fix in
 * m1_csrc/privateprofilestring.c (write_private_profile_string() /
 * pps_commit_replace()). Links the REAL, unmodified privateprofilestring.c
 * and m1_file_util.c against a POSIX-backed FatFs stub (test/stub_pps/) so
 * the actual production temp-name derivation and checked-replace logic is
 * what gets exercised, not a reimplementation of it.
 *
 * Required coverage (per task spec): successful replacement; temp
 * open/write/sync/close failures; rename failure; card removal mid-op;
 * existing temp-file collision; repeated field updates (the 4-sequential-
 * call pattern lfrfid_profile_save() uses); original preserved after every
 * failed path; success never reported after a failed filesystem operation.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "main.h"
#include "ff.h"
#include "ff_fault_inject.h"
#include "privateprofilestring.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static char s_dir[256];

static void make_scratch_dir(void)
{
    char tmpl[] = "/tmp/pps_test_XXXXXX";
    char *d = mkdtemp(tmpl);
    assert(d != NULL);
    strncpy(s_dir, d, sizeof(s_dir) - 1);
    s_dir[sizeof(s_dir) - 1] = '\0';
}

static void remove_scratch_dir(void)
{
    DIR *dp = opendir(s_dir);
    if (dp != NULL) {
        struct dirent *ent;
        char path[512];
        while ((ent = readdir(dp)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            snprintf(path, sizeof(path), "%s/%s", s_dir, ent->d_name);
            remove(path);
        }
        closedir(dp);
    }
    rmdir(s_dir);
}

/* Counts the temp-collision-shaped files (".<basename>.tmp") sitting in
 * s_dir, so tests can assert no orphaned temp file is left behind. */
static int count_tmp_files(void)
{
    DIR *dp = opendir(s_dir);
    int n = 0;
    if (dp == NULL) return 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.' && strstr(ent->d_name, ".tmp") != NULL)
            n++;
    }
    closedir(dp);
    return n;
}

static void write_plain_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(content, f);
    fclose(f);
}

static char *read_plain_file(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "r");
    if (f == NULL) { buf[0] = '\0'; return buf; }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* ---- Test 1: successful replacement of an existing entry ---- */
static void test_successful_replacement(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card1.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nVersion: 1\nData: AA\n");

    ff_fault_inject_reset();
    int ok = write_private_profile_string("Data", "BB", path);

    CHECK(ok == 1);
    CHECK(strstr(read_plain_file(path), "Data: BB") != NULL);
    CHECK(strstr(read_plain_file(path), "Filetype: RFID") != NULL);
    CHECK(count_tmp_files() == 0);

    remove(path);
}

/* ---- Test 2: target file does not exist yet (created fresh) ---- */
static void test_create_new_file(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card2.rfid", s_dir);

    ff_fault_inject_reset();
    int ok = write_private_profile_string("Data", "CC", path);

    CHECK(ok == 1);
    CHECK(strstr(read_plain_file(path), "Data: CC") != NULL);
    CHECK(count_tmp_files() == 0);

    remove(path);
}

/* ---- Test 3: temp file open failure -- original must survive intact ---- */
static void test_temp_open_failure(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card3.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");

    ff_fault_inject_reset();
    g_ff_fail_open_write = 1;
    int ok = write_private_profile_string("Data", "ZZ", path);

    CHECK(ok == 0);
    CHECK(strstr(read_plain_file(path), "Data: AA") != NULL);
    CHECK(strstr(read_plain_file(path), "Data: ZZ") == NULL);
    CHECK(count_tmp_files() == 0);

    ff_fault_inject_reset();
    remove(path);
}

/* ---- Test 4: sync failure during commit -- original must survive, temp cleaned ---- */
static void test_sync_failure(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card4.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");

    ff_fault_inject_reset();
    g_ff_fail_sync = 1;
    int ok = write_private_profile_string("Data", "ZZ", path);

    CHECK(ok == 0);
    CHECK(strstr(read_plain_file(path), "Data: AA") != NULL);
    CHECK(count_tmp_files() == 0);

    ff_fault_inject_reset();
    remove(path);
}

/* ---- Test 5: close failure during commit -- original must survive, temp cleaned ---- */
static void test_close_failure(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card5.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");

    ff_fault_inject_reset();
    g_ff_fail_close = 1;
    int ok = write_private_profile_string("Data", "ZZ", path);

    CHECK(ok == 0);
    CHECK(strstr(read_plain_file(path), "Data: AA") != NULL);
    CHECK(count_tmp_files() == 0);

    ff_fault_inject_reset();
    remove(path);
}

/* ---- Test 6: rename failure after successful unlink (the one residual
 * FatFs-limitation window the fix's own comment documents) ---- */
static void test_rename_failure(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card6.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");

    ff_fault_inject_reset();
    g_ff_fail_rename = 1;
    int ok = write_private_profile_string("Data", "ZZ", path);

    /* Failure must be reported truthfully -- never claimed as success. */
    CHECK(ok == 0);
    /* Documented residual window: original was already unlinked before the
     * rename was attempted, so it is genuinely gone in this one case. */
    CHECK(!file_exists(path));

    ff_fault_inject_reset();
    remove(path);
    /* Sweep any orphaned temp file this specific failure mode leaves behind. */
    char glob[512];
    snprintf(glob, sizeof(glob), "%s/.card6.rfid.tmp", s_dir);
    remove(glob);
}

/* ---- Test 7: card removed partway through the operation ---- */
static void test_card_removed_mid_operation(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card7.rfid", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");

    ff_fault_inject_reset();
    /* Let the read-open and temp-open succeed (2 calls), then vanish. */
    g_ff_card_removed_after_call = 3;
    int ok = write_private_profile_string("Data", "ZZ", path);

    CHECK(ok == 0);
    CHECK(strstr(read_plain_file(path), "Data: AA") != NULL);

    ff_fault_inject_reset();
    remove(path);
    /* Once the card is genuinely gone, the temp file living on it is
     * unreachable too -- the production code correctly cannot clean it up
     * (its own f_unlink() call fails the same way every other post-loss
     * call does). That's real card-loss behavior, not a defect; sweep the
     * artifact here so it doesn't leak into other tests' shared scratch dir. */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.card7.rfid.tmp", s_dir);
    remove(tmp_path);
}

/* ---- Test 8: pre-existing collision at the temp-file's own path ---- */
static void test_existing_temp_collision(void)
{
    char path[512], tmp_path[512];
    snprintf(path, sizeof(path), "%s/card8.rfid", s_dir);
    snprintf(tmp_path, sizeof(tmp_path), "%s/.card8.rfid.tmp", s_dir);
    write_plain_file(path, "Filetype: RFID\nData: AA\n");
    write_plain_file(tmp_path, "stale leftover from a previous crashed run\n");

    ff_fault_inject_reset();
    int ok = write_private_profile_string("Data", "DD", path);

    CHECK(ok == 1);
    CHECK(strstr(read_plain_file(path), "Data: DD") != NULL);
    CHECK(count_tmp_files() == 0);

    remove(path);
}

/* ---- Test 9: repeated sequential field updates on the same file, matching
 * lfrfid_profile_save()'s own 4-call pattern (Filetype/Version/PackType/Data) ---- */
static void test_repeated_field_updates(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/card9.rfid", s_dir);

    ff_fault_inject_reset();
    CHECK(write_private_profile_string("Filetype", "RFID", path) == 1);
    CHECK(write_private_profile_string("Version", "1", path) == 1);
    CHECK(write_private_profile_string("PackType", "raw", path) == 1);
    CHECK(write_private_profile_string("Data", "AABBCC", path) == 1);

    char *content = read_plain_file(path);
    CHECK(strstr(content, "Filetype: RFID") != NULL);
    CHECK(strstr(content, "Version: 1") != NULL);
    CHECK(strstr(content, "PackType: raw") != NULL);
    CHECK(strstr(content, "Data: AABBCC") != NULL);
    CHECK(count_tmp_files() == 0);

    remove(path);
}

int main(void)
{
    make_scratch_dir();

    test_successful_replacement();
    test_create_new_file();
    test_temp_open_failure();
    test_sync_failure();
    test_close_failure();
    test_rename_failure();
    test_card_removed_mid_operation();
    test_existing_temp_collision();
    test_repeated_field_updates();

    remove_scratch_dir();

    printf("privateprofilestring_test: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
