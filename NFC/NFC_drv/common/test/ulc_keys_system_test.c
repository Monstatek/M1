/* Host test for the real ulc_keys.c system/owner separation.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -Istub_ulc -I.. ../nfc_dict_line.c ../ulc_keys.c ulc_keys_system_test.c \
 *      -o /tmp/ulc_keys_system_test && /tmp/ulc_keys_system_test
 */
#include "../ulc_keys.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fail;
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
static char g_written[16][96];
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

FRESULT f_close(FIL* fp)
{
    (void)fp;
    return FR_OK;
}

int nfcfio_open_read(nfcfio_t* io, const char* path)
{
    return f_open(&io->fh, path, FA_READ) == FR_OK;
}

int nfcfio_open_write(nfcfio_t* io, const char* path)
{
    (void)io;
    snprintf(g_write_path, sizeof(g_write_path), "%s", path);
    g_written_count = 0;
    return 1;
}

void nfcfio_close(nfcfio_t* io)
{
    (void)io;
}

int nfcfio_getline(nfcfio_t* io, char* out, size_t outsz)
{
    if ((io->fh.slot < 0) || ((size_t)io->fh.slot >= g_file_count)) return -2;
    mock_file_t* f = &g_files[io->fh.slot];
    if (io->fh.line >= f->count) return -1;
    int n = snprintf(out, outsz, "%s", f->lines[io->fh.line++]);
    return (n >= 0) ? n : -2;
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

static bool hexkey(const char* text, uint8_t out[ULC_KEY_SIZE])
{
    return ulc_keys_parse_line(text, out) == ULC_LINE_KEY;
}

static void test_snapshot_sources_and_write_boundary(void)
{
    static const char* user[] = {
        "49454D4B41455242214E4143554F5946", /* built-in duplicate */
        "11111111111111111111111111111111",
        "bad",
        "11111111111111111111111111111111", /* owner duplicate */
    };
    static const char* system[] = {
        "11111111111111111111111111111111", /* owner duplicate */
        "22222222222222222222222222222222",
        "33333333333333333333333333333333",
    };
    mock_reset();
    mock_add("user", user, 4);
    mock_add("system", system, 3);

    ulc_keys_t keys;
    ulc_keys_reset(&keys);
    ulc_keys_load_status_t us = ulc_keys_load_user(&keys, "user");
    ulc_keys_load_status_t ss = ulc_keys_load_system(&keys, "system");

    CHECK(us.ok && us.loaded == 1 && us.skipped_malformed == 1,
          "owner file loads one unique key and reports malformed input");
    CHECK(ss.ok && ss.valid == 3 && ss.loaded == 2,
          "system count retains valid lines while loading only new keys");
    CHECK(keys.builtin_count == 1 && keys.user_count == 1 && keys.system_count == 2,
          "source counts remain separate");
    CHECK(keys.count == 4, "snapshot total is built-in + owner + system");
    CHECK(ulc_keys_is_builtin(&keys, 0), "first key classified built-in");
    CHECK(ulc_keys_is_user(&keys, 1), "second key classified owner");
    CHECK(ulc_keys_is_system(&keys, 2) && ulc_keys_is_system(&keys, 3),
          "last keys classified system");
    CHECK(!ulc_keys_remove_at(&keys, 0), "built-in cannot be deleted");
    CHECK(!ulc_keys_remove_at(&keys, 2), "system key cannot be deleted");

    uint8_t added[ULC_KEY_SIZE];
    CHECK(hexkey("44444444444444444444444444444444", added), "new owner fixture parses");
    CHECK(ulc_keys_add(&keys, added), "owner key can be added with system keys loaded");
    CHECK(keys.user_count == 2 && ulc_keys_is_user(&keys, 2),
          "new owner key is inserted before system keys");
    CHECK(ulc_keys_save_user(&keys), "owner dictionary saves");
    CHECK(strcmp(g_write_path, ULC_KEYS_USER_PATH) == 0,
          "save always targets canonical owner path");
    CHECK(g_written_count == 2, "save writes owner keys only");
    CHECK(strcmp(g_written[0], "11111111111111111111111111111111") == 0,
          "existing owner key persisted");
    CHECK(strcmp(g_written[1], "44444444444444444444444444444444") == 0,
          "new owner key persisted");
}

static void test_stream_order_and_missing_files(void)
{
    static const char* user[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    };
    static const char* system[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "not-a-key",
        "49454D4B41455242214E4143554F5946",
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    };
    mock_reset();
    mock_add("user", user, 2);
    mock_add("system", system, 4);

    static ulc_keys_iter_t it;
    uint8_t got[ULC_KEY_SIZE];
    uint8_t expected[3][ULC_KEY_SIZE];
    CHECK(hexkey("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", expected[0]), "owner fixture parses");
    CHECK(hexkey("49454D4B41455242214E4143554F5946", expected[1]), "system default parses");
    CHECK(hexkey("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", expected[2]), "system fixture parses");

    ulc_keys_iter_begin_paths(&it, "user", "system");
    for (size_t i = 0; i < 3; i++) {
        CHECK(ulc_keys_iter_next(&it, got), "expected streamed candidate exists");
        CHECK(memcmp(got, expected[i], ULC_KEY_SIZE) == 0,
              "stream order is owner then system");
    }
    CHECK(!ulc_keys_iter_next(&it, got), "duplicates/malformed lines do not add attempts");
    ulc_keys_iter_end(&it);

    mock_reset();
    ulc_keys_iter_begin_paths(&it, "missing-user", "missing-system");
    CHECK(ulc_keys_iter_next(&it, got), "compiled default remains as missing-file fallback");
    CHECK(!ulc_keys_iter_next(&it, got), "missing optional files are nonfatal and empty");
    ulc_keys_iter_end(&it);
}

int main(void)
{
    test_snapshot_sources_and_write_boundary();
    test_stream_order_and_missing_files();
    printf("ulc_keys_system_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
