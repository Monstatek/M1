/* Host unit test for m1_manager_fs.c (Gate B: Add Files transfer + FS safety).
 * Compiles the REAL m1_manager_fs.c with a real-file storage backend rooted at
 * ./fs_sandbox so writes, CRC-32 verify, atomic rename and temp cleanup are
 * genuinely exercised. Scratchpad/repo tool; no hardware. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "m1_manager_fs.h"
#include "m1_manager_protocol.h"

/* status API is provided by the linked m1_manager_protocol.c (host build). */
uint32_t g_test_tick_ms = 0;   /* referenced by protocol core stubs */

/* ---- configurable host backend rooted at ./fs_sandbox ---- */
#define SANDBOX "fs_sandbox"
static bool     g_ready = true;
static uint8_t  g_host_owned = 0;
static uint64_t g_free = 0;          /* 0 => unknown */
static FILE    *g_fp = NULL;

static void hostpath(const char *in, char *out) {
    /* "0:/foo" -> "fs_sandbox/foo"; bare "0:" (the root) -> "fs_sandbox" */
    if (strcmp(in, "0:") == 0) { sprintf(out, "%s", SANDBOX); return; }
    const char *rest = in;
    if (strncmp(in, "0:/", 3) == 0) rest = in + 3;
    sprintf(out, "%s/%s", SANDBOX, rest);
}
static bool be_ready(uint8_t *ho) { if (ho) *ho = g_host_owned; return g_ready; }
static uint64_t be_free(void) { return g_free; }
/* Backend-agnostic regression guard: a real FatFs backend's f_stat() on the
 * BARE root (no name component beneath it) returns FR_INVALID_NAME, not
 * FR_OK - fs_directory_ensure doesn't special-case that, so calling
 * ensure_dir() on the bare M1CP_FS_DIR root is a guaranteed IO error on
 * hardware even though this host backend's plain mkdir/stat wouldn't catch
 * it. Recording every call lets a test assert the bare root is never one of
 * them, regardless of what a specific backend's root-stat quirks are. */
static int g_ensure_dir_root_calls = 0;
static m1cp_fsb_rc_t be_ensure_dir(const char *dir_path) {
    if (strcmp(dir_path, "0:") == 0) g_ensure_dir_root_calls++;
    char p[128]; hostpath(dir_path, p); mkdir(p, 0777); return M1CP_FSB_OK;
}
static m1cp_fsb_rc_t be_open_w(const char *path) {
    char p[128]; hostpath(path, p);
    if (g_fp) fclose(g_fp);
    g_fp = fopen(p, "wb");
    return g_fp ? M1CP_FSB_OK : M1CP_FSB_ERR_IO;
}
static m1cp_fsb_rc_t be_write(const void *d, uint32_t len, uint32_t *w) {
    if (!g_fp) return M1CP_FSB_ERR_IO;
    size_t n = fwrite(d, 1, len, g_fp); if (w) *w = (uint32_t)n;
    return (n == len) ? M1CP_FSB_OK : M1CP_FSB_ERR_NO_SPACE;
}
static m1cp_fsb_rc_t be_sync(void) { if (g_fp) fflush(g_fp); return M1CP_FSB_OK; }
static m1cp_fsb_rc_t be_close(void) { if (g_fp) { fclose(g_fp); g_fp = NULL; } return M1CP_FSB_OK; }
static m1cp_fsb_rc_t be_crc32(const char *path, uint32_t *crc) {
    char p[128]; hostpath(path, p);
    FILE *f = fopen(p, "rb"); if (!f) return M1CP_FSB_ERR_NOT_FOUND;
    uint8_t buf[512]; size_t br; uint32_t acc = 0;
    while ((br = fread(buf, 1, sizeof(buf), f)) > 0) acc = m1cp_fs_crc32(acc, buf, (uint32_t)br);
    fclose(f); if (crc) *crc = acc; return M1CP_FSB_OK;
}
static m1cp_fsb_rc_t be_stat(const char *path, uint8_t *ex, uint32_t *sz) {
    char p[128]; hostpath(path, p); struct stat st;
    if (stat(p, &st) == 0) { if (ex) *ex = 1; if (sz) *sz = (uint32_t)st.st_size; }
    else { if (ex) *ex = 0; if (sz) *sz = 0; }
    return M1CP_FSB_OK;
}
static m1cp_fsb_rc_t be_remove(const char *path) { char p[128]; hostpath(path, p); remove(p); return M1CP_FSB_OK; }
static m1cp_fsb_rc_t be_rename(const char *a, const char *b) {
    char pa[128], pb[128]; hostpath(a, pa); hostpath(b, pb);
    return (rename(pa, pb) == 0) ? M1CP_FSB_OK : M1CP_FSB_ERR_IO;
}
static const m1cp_fs_backend_t BE = {
    be_ready, be_free, be_ensure_dir, be_open_w, be_write, be_sync, be_close,
    be_crc32, be_stat, be_remove, be_rename
};

static int fails = 0, count = 0;
static void ok(int cond, const char *msg) {
    count++;
    if (cond) printf("  ok  : %s\n", msg);
    else { printf("  FAIL: %s\n", msg); fails++; }
}

static uint8_t g_out[64]; static uint16_t g_olen;

/* build a FS_BEGIN payload */
static uint16_t mk_begin(uint8_t *p, const char *name, uint32_t total, uint32_t crc, uint8_t flags) {
    uint8_t nl = (uint8_t)strlen(name); uint16_t o = 0;
    p[o++] = nl; memcpy(p + o, name, nl); o += nl;
    p[o++] = total & 0xff; p[o++] = (total>>8)&0xff; p[o++] = (total>>16)&0xff; p[o++] = (total>>24)&0xff;
    p[o++] = crc & 0xff; p[o++] = (crc>>8)&0xff; p[o++] = (crc>>16)&0xff; p[o++] = (crc>>24)&0xff;
    p[o++] = flags; return o;
}
static uint16_t mk_data(uint8_t *p, uint16_t session, uint32_t off, const uint8_t *d, uint16_t n) {
    uint16_t o = 0; p[o++] = session & 0xff; p[o++] = (session>>8)&0xff;
    p[o++] = off & 0xff; p[o++] = (off>>8)&0xff; p[o++] = (off>>16)&0xff; p[o++] = (off>>24)&0xff;
    memcpy(p + o, d, n); o += n; return o;
}
static uint16_t mk_name(uint8_t *p, const char *name) {
    uint8_t nl = (uint8_t)strlen(name); p[0] = nl; memcpy(p + 1, name, nl); return (uint16_t)(1 + nl);
}
static int host_exists(const char *rel) { char p[128]; sprintf(p, "%s/%s", SANDBOX, rel); struct stat st; return stat(p, &st) == 0; }

int main(void) {
    system("rm -rf " SANDBOX);
    /* The sandbox scratch dir itself is a HOST TEST fixture, not part of what
     * production code creates - a real FatFs backend's root always exists
     * (it's the mounted card), which is exactly why fs_ensure_dirs_for() no
     * longer calls ensure_dir() for a flat leaf name with no subdirectory.
     * Previously this mkdir happened as an incidental side effect of
     * be_ensure_dir() always being called once for the bare root; do it
     * explicitly now instead of depending on that. */
    mkdir(SANDBOX, 0777);
    m1cp_fs_init(&BE);

    /* ---- name validation (escaping 0:/ is structurally impossible) ---- */
    ok(m1cp_fs_name_valid((const uint8_t*)"firmware.m1pkg", 14), "valid name accepted");
    ok(m1cp_fs_name_valid((const uint8_t*)"a", 1), "single char accepted");
    ok(!m1cp_fs_name_valid((const uint8_t*)"../etc", 6), "traversal '../etc' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a\\b", 3), "backslash rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"0:evil", 6), "colon rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"..", 2), "'..' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)".", 1), "'.' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"...", 3), "all-dots rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a b", 3), "space rejected");
    { uint8_t big[70]; memset(big, 'a', 70); ok(!m1cp_fs_name_valid(big, 65), ">64 rejected"); }

    /* ---- subdirectory support: one or more '/'-separated components ---- */
    ok(m1cp_fs_name_valid((const uint8_t*)"a/b", 3), "single-level subdir accepted");
    ok(m1cp_fs_name_valid((const uint8_t*)"db/tv.ir", 8), "'db/tv.ir' accepted");
    ok(m1cp_fs_name_valid((const uint8_t*)"a/b/c.txt", 9), "multi-level subdir accepted");
    ok(!m1cp_fs_name_valid((const uint8_t*)"/etc/passwd", 11), "leading '/' (absolute) rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a/", 2), "trailing '/' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a//b", 4), "empty component '//' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"../../etc/passwd", 16), "'../../etc/passwd' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a/../b", 6), "'a/../b' traversal component rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a/..", 4), "trailing '..' component rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"../a", 4), "leading '..' component rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"a/./b", 5), "'.' component rejected");

    /* ---- reserved top-level name: WIFI is the M1's own capture storage ---- */
    ok(!m1cp_fs_name_valid((const uint8_t*)"WIFI/scan.pcapng", 16), "top-level 'WIFI' rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"wifi/scan.pcapng", 16), "top-level 'wifi' (any case) rejected");
    ok(!m1cp_fs_name_valid((const uint8_t*)"WiFi", 4), "bare 'WiFi' leaf rejected");
    ok(m1cp_fs_name_valid((const uint8_t*)"WIFICONFIG.txt", 14), "a name merely STARTING with 'WIFI' is fine");
    ok(m1cp_fs_name_valid((const uint8_t*)"a/WIFI/b.txt", 12), "'WIFI' is only reserved as the TOP-level component");

    /* ---- happy path: BEGIN -> DATA -> COMMIT writes verified file ---- */
    uint8_t payload[600];
    const char *content = "hello monsta add-files payload!";
    uint32_t clen = (uint32_t)strlen(content);
    uint32_t ccrc = m1cp_fs_crc32(0, (const uint8_t*)content, clen);

    uint16_t n = mk_begin(payload, "greet.txt", clen, ccrc, 0);
    uint8_t e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE && g_olen == 4, "FS_BEGIN ok, 4-byte resp");
    uint16_t sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    ok(sess != 0, "session id nonzero");
    ok(m1cp_fs_busy(), "transfer active after BEGIN");
    ok(host_exists("greet.txt.part"), "temp .part created");

    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    e = m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    uint32_t nxt = (uint32_t)(g_out[0] | (g_out[1]<<8) | (g_out[2]<<16) | ((uint32_t)g_out[3]<<24));
    ok(e == M1CP_ERR_NONE && nxt == clen, "FS_DATA ok, next_offset == len");

    n = 2; payload[0] = sess & 0xff; payload[1] = (sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_COMMIT ok");
    ok(host_exists("greet.txt"), "final file present after commit");
    ok(!host_exists("greet.txt.part"), "temp gone after commit");
    ok(!m1cp_fs_busy(), "idle after commit");
    { char p[128]; sprintf(p, "%s/greet.txt", SANDBOX); FILE*f=fopen(p,"rb");
      if (f) { char rb[64]; size_t r=fread(rb,1,sizeof(rb),f); fclose(f);
        ok(r==clen && memcmp(rb,content,clen)==0, "final file content matches"); }
      else { ok(0, "final file content matches (file missing)"); } }

    /* ---- subdirectory transfer: BEGIN "db/tv.ir" creates the subdir and
     * lands the file at db/tv.ir, exactly like a bare leaf name ---- */
    n = mk_begin(payload, "db/tv.ir", clen, ccrc, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_BEGIN 'db/tv.ir' ok (subdir created)");
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    ok(host_exists("db/tv.ir.part"), "temp created under new 'db' subdir");
    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    payload[0] = sess & 0xff; payload[1] = (sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_COMMIT 'db/tv.ir' ok");
    ok(host_exists("db/tv.ir"), "final file present under 'db' subdir");

    /* A second file in the SAME subdir must not need to recreate it. */
    n = mk_begin(payload, "db/ac.ir", clen, ccrc, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_BEGIN second file in existing 'db' subdir ok");
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    payload[0] = sess & 0xff; payload[1] = (sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_COMMIT second file in existing 'db' subdir ok");
    ok(host_exists("db/ac.ir"), "second final file present under 'db' subdir");

    /* Two-level nested path (e.g. uploading a folder "INFRARED" containing a
     * "db" subfolder): NEITHER "INFRARED" nor "INFRARED/db" exist yet on the
     * SD card, so both levels must be created in one BEGIN, not just one. */
    n = mk_begin(payload, "INFRARED/db/tv.ir", clen, ccrc, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_BEGIN 'INFRARED/db/tv.ir' ok (both levels created, neither existed)");
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    ok(host_exists("INFRARED/db/tv.ir.part"), "temp created two levels deep");
    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    payload[0] = sess & 0xff; payload[1] = (sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "FS_COMMIT 'INFRARED/db/tv.ir' ok");
    ok(host_exists("INFRARED/db/tv.ir"), "final file present two levels deep");

    /* Traversal payloads are rejected by BEGIN itself (BAD_ARG), never reach
     * the filesystem. */
    n = mk_begin(payload, "../escape.bin", clen, ccrc, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_BAD_ARG, "FS_BEGIN '../escape.bin' -> BAD_ARG");
    ok(!host_exists("escape.bin") && !host_exists("../escape.bin"), "no file escaped the SD root");

    /* ---- integrity mismatch -> INTEGRITY, temp cleaned ---- */
    n = mk_begin(payload, "bad.bin", clen, ccrc ^ 0xFFFF, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    payload[0] = sess & 0xff; payload[1] = (sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_INTEGRITY, "wrong CRC -> ERR_INTEGRITY");
    ok(!host_exists("bad.bin"), "no final file on integrity failure");
    ok(!host_exists("bad.bin.part"), "temp cleaned on integrity failure");
    ok(!m1cp_fs_busy(), "idle after failed commit");

    /* ---- ordering: out-of-order / duplicate offset rejected ---- */
    n = mk_begin(payload, "ord.bin", 10, 0, 0);
    m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    { uint8_t d[5] = {1,2,3,4,5};
      n = mk_data(payload, sess, 0, d, 5); m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
      n = mk_data(payload, sess, 0, d, 5); /* wrong offset (dup) */
      e = m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
      ok(e == M1CP_ERR_BAD_ARG, "out-of-order/dup chunk -> ERR_BAD_ARG");
      n = mk_data(payload, sess, 5, d, 5); /* overrun by 0? received=5,total=10, +5=10 ok */
      e = m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
      ok(e == M1CP_ERR_NONE, "in-order chunk after dup still accepted");
      n = mk_data(payload, sess, 10, d, 1); /* overrun beyond total */
      e = m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
      ok(e == M1CP_ERR_BAD_ARG, "overrun beyond declared size -> ERR_BAD_ARG"); }
    /* wrong session id */
    { uint8_t d[1]={9}; n = mk_data(payload, (uint16_t)(sess ^ 0xFFFF), 10, d, 1);
      e = m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
      ok(e == M1CP_ERR_BAD_ARG, "wrong session -> ERR_BAD_ARG"); }
    /* abort cleans temp */
    e = m1cp_fs_handle(M1CP_CMD_FS_ABORT, payload, 0, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE && !m1cp_fs_busy(), "FS_ABORT ok, idle");
    ok(!host_exists("ord.bin.part"), "temp cleaned on abort");

    /* ---- incomplete commit rejected ---- */
    n = mk_begin(payload, "inc.bin", 100, 0, 0);
    m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    { uint8_t d[10]={0}; n = mk_data(payload, sess, 0, d, 10); m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen); }
    payload[0]=sess&0xff; payload[1]=(sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_BAD_STATE, "incomplete commit -> ERR_BAD_STATE");

    /* ---- second concurrent BEGIN rejected (single active transfer) ---- */
    n = mk_begin(payload, "x.bin", 4, 0, 0);
    m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(m1cp_fs_busy(), "transfer active");
    n = mk_begin(payload, "y.bin", 4, 0, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_BUSY, "second BEGIN while active -> ERR_BUSY");
    m1cp_fs_handle(M1CP_CMD_FS_ABORT, payload, 0, g_out, &g_olen);

    /* ---- duplicate final name policy ---- */
    n = mk_begin(payload, "greet.txt", clen, ccrc, 0); /* exists from happy path, no overwrite */
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_BAD_STATE, "existing final without overwrite -> ERR_BAD_STATE");
    n = mk_begin(payload, "greet.txt", clen, ccrc, 1); /* overwrite */
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "overwrite flag allows existing final");
    sess = (uint16_t)(g_out[2] | (g_out[3] << 8));
    n = mk_data(payload, sess, 0, (const uint8_t*)content, (uint16_t)clen);
    m1cp_fs_handle(M1CP_CMD_FS_DATA, payload, n, g_out, &g_olen);
    payload[0]=sess&0xff; payload[1]=(sess>>8)&0xff;
    e = m1cp_fs_handle(M1CP_CMD_FS_COMMIT, payload, 2, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE, "overwrite commit ok");

    /* ---- SD unavailable / host-owned refuses ---- */
    g_ready = false;
    n = mk_begin(payload, "z.bin", 4, 0, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_SD_UNAVAILABLE, "SD not ready -> ERR_SD_UNAVAILABLE");
    g_ready = true;

    /* ---- insufficient space ---- */
    g_free = 3;
    n = mk_begin(payload, "z.bin", 100, 0, 0);
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NO_SPACE, "declared size > free -> ERR_NO_SPACE");
    g_free = 0;

    /* ---- STAT / DELETE ---- */
    n = mk_name(payload, "greet.txt");
    e = m1cp_fs_handle(M1CP_CMD_FS_STAT, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE && g_olen == 5 && g_out[0] == 1, "FS_STAT existing -> exists=1");
    n = mk_name(payload, "nope.bin");
    e = m1cp_fs_handle(M1CP_CMD_FS_STAT, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE && g_out[0] == 0, "FS_STAT missing -> exists=0");
    n = mk_name(payload, "greet.txt");
    e = m1cp_fs_handle(M1CP_CMD_FS_DELETE, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NONE && !host_exists("greet.txt"), "FS_DELETE removes file");
    n = mk_name(payload, "greet.txt");
    e = m1cp_fs_handle(M1CP_CMD_FS_DELETE, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_NOT_FOUND, "FS_DELETE missing -> ERR_NOT_FOUND");

    /* ---- invalid name via command path rejected ---- */
    n = mk_begin(payload, "ok.bin", 4, 0, 0); payload[1] = '/'; /* corrupt to contain slash */
    e = m1cp_fs_handle(M1CP_CMD_FS_BEGIN, payload, n, g_out, &g_olen);
    ok(e == M1CP_ERR_BAD_ARG, "invalid name in FS_BEGIN -> ERR_BAD_ARG");

    /* ---- regression: ensure_dir() must never be called on the bare root ----
     * (a real FatFs backend's f_stat("0:") returns FR_INVALID_NAME, not
     * FR_OK, turning this into a guaranteed IO error on hardware - this host
     * backend's plain mkdir/stat wouldn't have caught that, so the check is
     * against the call itself, not its outcome here). Every BEGIN above
     * (flat leaf names AND nested paths) already ran; if any of them asked
     * the backend to ensure the bare root, this fails. */
    ok(g_ensure_dir_root_calls == 0, "ensure_dir() never called with the bare M1CP_FS_DIR root");

    system("rm -rf " SANDBOX);
    printf("\n%s (%d/%d)\n", fails==0 ? "ALL PASS" : "FAILURES", count-fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}
