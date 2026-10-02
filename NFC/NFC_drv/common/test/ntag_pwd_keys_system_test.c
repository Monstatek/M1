/* Host test for real ntag_pwd_keys.c owner/system separation.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -Istub_ulc -I.. ../nfc_dict_line.c ../ntag_pwd_keys.c ntag_pwd_keys_system_test.c \
 *      -o /tmp/ntag_pwd_keys_system_test && /tmp/ntag_pwd_keys_system_test
 */
#include "../ntag_pwd_keys.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("FAIL line %d: %s\n", __LINE__, (m)); } } while (0)

typedef struct {
    const char* path;
    const char* const* lines;
    size_t count;
    bool read_error;
} mock_file_t;

static mock_file_t g_files[8];
static size_t g_file_count;
static char g_write_path[96];
static char g_written[16][32];
static size_t g_written_count;

static void mock_reset(void)
{
    memset(g_files, 0, sizeof(g_files));
    g_file_count = 0;
    g_write_path[0] = '\0';
    g_written_count = 0;
}

static void mock_add(const char* path, const char* const* lines, size_t count)
{
    g_files[g_file_count++] = (mock_file_t){path, lines, count, false};
}

static int find_file(const char* path)
{
    for (size_t i = 0; i < g_file_count; i++) {
        if (strcmp(g_files[i].path, path) == 0) return (int)i;
    }
    return -1;
}

FRESULT f_open(FIL* fp, const char* path, BYTE mode)
{
    (void)mode;
    int slot = find_file(path);
    if (slot < 0) return FR_NO_FILE;
    if (g_files[slot].read_error) return FR_DISK_ERR;
    fp->slot = slot;
    fp->line = 0;
    return FR_OK;
}

FRESULT f_close(FIL* fp) { (void)fp; return FR_OK; }
int nfcfio_open_read(nfcfio_t* io, const char* path) { return f_open(&io->fh, path, FA_READ) == FR_OK; }
int nfcfio_open_write(nfcfio_t* io, const char* path)
{
    (void)io;
    snprintf(g_write_path, sizeof(g_write_path), "%s", path);
    g_written_count = 0;
    return 1;
}
void nfcfio_close(nfcfio_t* io) { (void)io; }
int nfcfio_getline(nfcfio_t* io, char* out, size_t outsz)
{
    if ((io->fh.slot < 0) || ((size_t)io->fh.slot >= g_file_count)) return -2;
    mock_file_t* f = &g_files[io->fh.slot];
    if (io->fh.line >= f->count) return -1;
    return snprintf(out, outsz, "%s", f->lines[io->fh.line++]);
}
long nfcfio_tell_line_start(nfcfio_t* io) { (void)io; return -1; }
int nfcfio_seek(nfcfio_t* io, uint32_t offset) { (void)io; (void)offset; return 0; }
int nfcfio_puts(nfcfio_t* io, const char* s) { (void)io; (void)s; return 1; }
int nfcfio_putline(nfcfio_t* io, const char* s)
{
    (void)io;
    if (g_written_count >= 16) return 0;
    snprintf(g_written[g_written_count++], sizeof(g_written[0]), "%s", s);
    return 1;
}

static bool hexpwd(const char* text, uint8_t out[NTAG_PWD_KEY_SIZE])
{
    return ntag_pwd_keys_parse_line(text, out) == NTAG_PWD_LINE_KEY;
}

static void test_snapshot_and_write_boundary(void)
{
    static const char* user[] = {"FFFFFFFF", "11111111", "bad", "11111111"};
    static const char* system[] = {"11111111", "22222222", "33333333"};
    mock_reset();
    mock_add("user", user, 4);
    mock_add("system", system, 3);

    ntag_pwd_keys_t keys;
    ntag_pwd_keys_reset(&keys);
    ntag_pwd_keys_load_status_t us = ntag_pwd_keys_load_user(&keys, "user");
    ntag_pwd_keys_load_status_t ss = ntag_pwd_keys_load_system(&keys, "system");

    CHECK(us.ok && us.loaded == 1 && us.skipped_malformed == 1,
          "owner file loads one unique password and reports malformed input");
    CHECK(ss.ok && ss.valid == 3 && ss.loaded == 2,
          "system valid count is distinct from post-dedup loaded count");
    CHECK(keys.builtin_count == 1 && keys.user_count == 1 && keys.system_count == 2,
          "built-in, owner and system counts remain separate");
    CHECK(ntag_pwd_keys_is_builtin(&keys, 0), "fallback classified built-in");
    CHECK(ntag_pwd_keys_is_user(&keys, 1), "owner password classified user");
    CHECK(ntag_pwd_keys_is_system(&keys, 2), "system password classified system");
    CHECK(!ntag_pwd_keys_remove_at(&keys, 0), "fallback cannot be deleted");
    CHECK(!ntag_pwd_keys_remove_at(&keys, 2), "system password cannot be deleted");

    uint8_t added[NTAG_PWD_KEY_SIZE];
    CHECK(hexpwd("44444444", added), "new owner password parses");
    CHECK(ntag_pwd_keys_add(&keys, added), "owner password adds with system loaded");
    CHECK(keys.user_count == 2 && ntag_pwd_keys_is_user(&keys, 2),
          "new owner password inserted before system data");
    CHECK(ntag_pwd_keys_save_user(&keys), "owner dictionary saves");
    CHECK(strcmp(g_write_path, NTAG_PWD_KEYS_USER_PATH) == 0,
          "save targets only canonical owner file");
    CHECK(g_written_count == 2, "save excludes fallback and system passwords");
    CHECK(strcmp(g_written[0], "11111111") == 0 && strcmp(g_written[1], "44444444") == 0,
          "only owner passwords persisted in deterministic order");
}

static void test_stream_order_and_fallback(void)
{
    static const char* user[] = {"AAAAAAAA", "AAAAAAAA"};
    static const char* system[] = {"AAAAAAAA", "not-a-key", "FFFFFFFF", "BBBBBBBB"};
    mock_reset();
    mock_add("user", user, 2);
    mock_add("system", system, 4);

    static ntag_pwd_keys_iter_t it;
    uint8_t got[NTAG_PWD_KEY_SIZE];
    uint8_t expected[3][NTAG_PWD_KEY_SIZE];
    CHECK(hexpwd("AAAAAAAA", expected[0]) && hexpwd("FFFFFFFF", expected[1]) &&
          hexpwd("BBBBBBBB", expected[2]), "stream fixtures parse");
    ntag_pwd_keys_iter_begin_paths(&it, "user", "system");
    for (size_t i = 0; i < 3; i++) {
        CHECK(ntag_pwd_keys_iter_next(&it, got), "expected streamed password exists");
        CHECK(memcmp(got, expected[i], NTAG_PWD_KEY_SIZE) == 0,
              "stream order is owner then system");
    }
    CHECK(!ntag_pwd_keys_iter_next(&it, got), "duplicates and malformed lines add no attempts");
    ntag_pwd_keys_iter_end(&it);

    mock_reset();
    ntag_pwd_keys_iter_begin_paths(&it, "missing-user", "missing-system");
    CHECK(ntag_pwd_keys_iter_next(&it, got) && memcmp(got, expected[1], NTAG_PWD_KEY_SIZE) == 0,
          "documented factory password remains missing-file fallback");
    CHECK(!ntag_pwd_keys_iter_next(&it, got), "fallback emitted once");
    ntag_pwd_keys_iter_end(&it);
}

int main(void)
{
    test_snapshot_and_write_boundary();
    test_stream_order_and_fallback();
    printf("ntag_pwd_keys_system_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
