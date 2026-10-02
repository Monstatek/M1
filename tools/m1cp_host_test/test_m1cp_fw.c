/* Host unit test for m1_manager_fw.c (Gate C: .m1pkg parser + FW_VALIDATE).
 * Read-only: exercises the REAL validator with a buffer-backed package reader,
 * over valid and every malformed/corrupt case. No hardware, no flash. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "m1_manager_fw.h"
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;   /* referenced by linked protocol core */

#define DEVID 0x0484u
#define BANK  0x00100000u       /* 1 MiB */

/* ---- buffer-backed package reader (with injectable I/O failure) ---- */
static uint8_t  g_pkg[8192];
static uint32_t g_pkg_len = 0;
static int      g_fail_read_at = -1;   /* offset threshold to start failing, -1 = never */
static uint32_t g_short_size = 0;      /* if nonzero, size() reports this (I/O instability) */

static m1cp_fsb_rc_t rd_size(void *ctx, uint32_t *out) {
    (void)ctx; *out = g_short_size ? g_short_size : g_pkg_len; return M1CP_FSB_OK;
}
static m1cp_fsb_rc_t rd_read(void *ctx, uint32_t off, void *buf, uint32_t len, uint32_t *rd) {
    (void)ctx;
    if (g_fail_read_at >= 0 && (int)off >= g_fail_read_at) return M1CP_FSB_ERR_IO;
    uint32_t avail = (off <= g_pkg_len) ? (g_pkg_len - off) : 0;
    uint32_t n = (len < avail) ? len : avail;
    memcpy(buf, g_pkg + off, n);
    if (rd) *rd = n;
    return M1CP_FSB_OK;
}
static m1cp_pkg_reader_t reader(void) {
    m1cp_pkg_reader_t r = { rd_size, rd_read, NULL }; return r;
}
static m1cp_fw_target_t target(void) {
    m1cp_fw_target_t t = { DEVID, BANK }; return t;
}

static void wr16(uint8_t *p, uint16_t v){ p[0]=v; p[1]=v>>8; }
static void wr32(uint8_t *p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* Build a canonical valid package: header(48) | fw | res, fw first & 4-aligned.
 * Returns total length. Versions default 0.8.0.4 fw / 0.8.1.0 res (compatible). */
static uint32_t build_valid(uint16_t devid,
                            const uint8_t fwv[4], const uint8_t rsv[4],
                            uint32_t fwlen, uint32_t reslen) {
    memset(g_pkg, 0, sizeof(g_pkg));
    uint32_t fw_off = M1PKG_HEADER_LEN;
    uint32_t res_off = fw_off + fwlen;
    uint32_t total = res_off + reslen;
    /* fill component bytes deterministically */
    for (uint32_t i = 0; i < fwlen; i++)  g_pkg[fw_off + i]  = (uint8_t)(0x11 + i);
    for (uint32_t i = 0; i < reslen; i++) g_pkg[res_off + i] = (uint8_t)(0xA0 + i);
    uint32_t fwcrc  = m1cp_fs_crc32(0, g_pkg + fw_off, fwlen);
    uint32_t rescrc = m1cp_fs_crc32(0, g_pkg + res_off, reslen);
    g_pkg[0]=M1PKG_MAGIC0; g_pkg[1]=M1PKG_MAGIC1; g_pkg[2]=M1PKG_MAGIC2; g_pkg[3]=M1PKG_MAGIC3;
    g_pkg[4]=M1PKG_FORMAT_VERSION;
    wr16(&g_pkg[8], devid);
    memcpy(&g_pkg[12], fwv, 4);
    memcpy(&g_pkg[16], rsv, 4);
    wr32(&g_pkg[20], fw_off);  wr32(&g_pkg[24], fwlen);  wr32(&g_pkg[28], fwcrc);
    wr32(&g_pkg[32], res_off); wr32(&g_pkg[36], reslen); wr32(&g_pkg[40], rescrc);
    uint32_t hcrc = m1cp_fs_crc32(0, g_pkg, M1PKG_HEADER_CRC_COVER);
    wr32(&g_pkg[44], hcrc);
    g_pkg_len = total;
    g_fail_read_at = -1; g_short_size = 0;
    return total;
}
/* recompute header CRC after mutating header fields */
static void reseal_header(void) { wr32(&g_pkg[44], m1cp_fs_crc32(0, g_pkg, M1PKG_HEADER_CRC_COVER)); }

static int fails = 0, count = 0;
static void ok(int cond, const char *msg) {
    count++;
    if (cond) printf("  ok  : %s\n", msg);
    else { printf("  FAIL: %s\n", msg); fails++; }
}
static uint8_t validate(void) {
    m1cp_pkg_reader_t r = reader(); m1cp_fw_target_t t = target(); m1cp_pkg_info_t info;
    return m1cp_pkg_validate(&r, &t, &info);
}

static const uint8_t FWV[4] = {0,8,0,4};
static const uint8_t RSV[4] = {0,8,1,0};   /* same major.minor -> compatible */

int main(void) {
    /* ---- happy path ---- */
    build_valid(DEVID, FWV, RSV, 64, 40);
    { m1cp_pkg_reader_t r = reader(); m1cp_fw_target_t t = target(); m1cp_pkg_info_t info;
      uint8_t e = m1cp_pkg_validate(&r, &t, &info);
      ok(e == M1CP_ERR_NONE, "valid package validates");
      ok(info.fw_size==64 && info.res_size==40, "descriptor sizes correct");
      ok(memcmp(info.fw_version,FWV,4)==0 && memcmp(info.resource_version,RSV,4)==0, "descriptor versions correct");
      ok(info.total_size==M1PKG_HEADER_LEN+64+40, "descriptor total_size correct"); }

    /* smallest valid: fw=4, res=1 */
    build_valid(DEVID, FWV, RSV, 4, 1);
    ok(validate()==M1CP_ERR_NONE, "minimal valid package (fw=4,res=1)");

    /* ---- container magic ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg[0]='X'; reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "corrupt magic -> BAD_PAYLOAD");

    /* ---- unsupported format version ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg[4]=0x02; reseal_header();
    ok(validate()==M1CP_ERR_INCOMPATIBLE, "unsupported format version -> INCOMPATIBLE");

    /* ---- header CRC (tamper a field without resealing) ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg[8] ^= 0xFF; /* devid, no reseal */
    ok(validate()==M1CP_ERR_INTEGRITY, "header CRC mismatch -> INTEGRITY");

    /* ---- truncation: file shorter than declared components ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg_len -= 4; /* drop 4 bytes of res */
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "truncated file -> BAD_PAYLOAD");

    /* ---- header-only / incomplete header ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg_len = M1PKG_HEADER_LEN - 1;
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "file smaller than header -> BAD_PAYLOAD");

    /* ---- trailing data after last component ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg_len += 8; /* declared ends before EOF */
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "trailing data -> BAD_PAYLOAD");

    /* ---- oversized fw_size (declares more than the file holds) ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[24], 64+4); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "oversized fw_size -> BAD_PAYLOAD");

    /* ---- integer-overflow attempt: fw_size near UINT32_MAX ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[24], 0xFFFFFFFCu); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "overflow fw_size -> BAD_PAYLOAD (no wrap)");
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[20], 0xFFFFFFFCu); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "overflow fw_offset -> BAD_PAYLOAD");

    /* ---- alignment: fw_size not multiple of 4 ---- */
    build_valid(DEVID, FWV, RSV, 66, 40); /* 66 not /4 */
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "unaligned fw_size -> BAD_PAYLOAD");

    /* ---- overlap / duplicate: point res at fw's region ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[32], M1PKG_HEADER_LEN); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "overlapping components -> BAD_PAYLOAD");

    /* ---- leading gap: fw_offset past header ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[20], M1PKG_HEADER_LEN+8); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "leading gap -> BAD_PAYLOAD");

    /* ---- internal gap between components ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[32], M1PKG_HEADER_LEN+64+8); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "internal gap -> BAD_PAYLOAD");

    /* ---- missing component: fw_size == 0 ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[24], 0); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "missing firmware (size 0) -> BAD_PAYLOAD");
    /* missing resource: res_size == 0 */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[36], 0); reseal_header();
    ok(validate()==M1CP_ERR_BAD_PAYLOAD, "missing resource (size 0) -> BAD_PAYLOAD");

    /* ---- wrong target devid ---- */
    build_valid(0x0450, FWV, RSV, 64, 40);
    ok(validate()==M1CP_ERR_INCOMPATIBLE, "wrong target_devid -> INCOMPATIBLE");

    /* ---- fw image exceeds flash bank (header-only: NO_SPACE fires before any
     * component body is read, so no need to materialise a >1MB buffer) ---- */
    build_valid(DEVID, FWV, RSV, 64, 40);
    { uint32_t big = BANK + 4;
      wr32(&g_pkg[24], big);                            /* fw_size            */
      wr32(&g_pkg[32], M1PKG_HEADER_LEN + big);         /* res_off = 48 + big */
      wr32(&g_pkg[36], 4);                              /* res_size           */
      g_pkg_len = M1PKG_HEADER_LEN + big + 4;           /* declared file size  */
      reseal_header();
      ok(validate()==M1CP_ERR_NO_SPACE, "fw_size > bank -> NO_SPACE"); }

    /* ---- incompatible resource version (different minor) ---- */
    { uint8_t bad_rsv[4] = {0,9,0,0};
      build_valid(DEVID, FWV, bad_rsv, 64, 40);
      ok(validate()==M1CP_ERR_INCOMPATIBLE, "fw/resource minor mismatch -> INCOMPATIBLE"); }
    ok(m1cp_fw_versions_compatible(FWV, RSV), "compat rule: same major.minor ok");
    { uint8_t r2[4]={0,9,0,0}; ok(!m1cp_fw_versions_compatible(FWV,r2), "compat rule: minor mismatch not ok"); }

    /* ---- bad fw CRC ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[28], 0xDEADBEEF); reseal_header();
    ok(validate()==M1CP_ERR_INTEGRITY, "bad fw_crc32 -> INTEGRITY");
    /* ---- bad res CRC ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); wr32(&g_pkg[40], 0x12345678); reseal_header();
    ok(validate()==M1CP_ERR_INTEGRITY, "bad res_crc32 -> INTEGRITY");
    /* ---- corrupt fw body (CRC now mismatches) ---- */
    build_valid(DEVID, FWV, RSV, 64, 40); g_pkg[M1PKG_HEADER_LEN] ^= 0xFF;
    ok(validate()==M1CP_ERR_INTEGRITY, "corrupt fw body -> INTEGRITY");

    /* ---- staged-file I/O failure while reading a component ---- */
    build_valid(DEVID, FWV, RSV, 128, 40); g_fail_read_at = M1PKG_HEADER_LEN + 64;
    ok(validate()==M1CP_ERR_IO, "read failure mid-component -> IO");
    /* size() instability: reports larger than readable -> short read -> IO */
    build_valid(DEVID, FWV, RSV, 64, 40); g_short_size = g_pkg_len + 100;
    { uint8_t e = validate(); ok(e==M1CP_ERR_BAD_PAYLOAD || e==M1CP_ERR_IO, "size instability -> BAD_PAYLOAD/IO"); }

    /* ---- FW_VALIDATE handler via injected source: success + flags ---- */
    /* set a source that opens the in-memory package */
    extern void set_test_fw_source(void);
    set_test_fw_source();
    build_valid(DEVID, FWV, RSV, 64, 40);
    { uint8_t pl[16]; uint8_t out[16]; uint16_t olen=0;
      uint8_t nl = (uint8_t)strlen("fw.m1pkg"); pl[0]=nl; memcpy(pl+1,"fw.m1pkg",nl);
      uint8_t e = m1cp_fw_handle(M1CP_CMD_FW_VALIDATE, pl, (uint16_t)(1+nl), out, &olen);
      ok(e==M1CP_ERR_NONE && olen==8, "FW_VALIDATE handler ok, 8-byte resp");
      ok(memcmp(out,FWV,4)==0 && memcmp(out+4,RSV,4)==0, "FW_VALIDATE returns fw+resource versions"); }

    /* ---- the VALIDATE handler owns only 0x20; other 0x2x opcodes are handled
     *      elsewhere (streamed update) or unimplemented ---- */
    { uint8_t out[16]; uint16_t olen=0;
      ok(m1cp_fw_handle(0x2FU, (const uint8_t*)"", 0, out, &olen)==M1CP_ERR_NOT_IMPLEMENTED,
         "m1cp_fw_handle only implements FW_VALIDATE (0x2F -> NOT_IMPLEMENTED)"); }

    /* ---- FW_VALIDATE with invalid leaf name rejected ---- */
    { uint8_t pl[16]; uint8_t out[16]; uint16_t olen=0;
      pl[0]=6; memcpy(pl+1,"../x.y",6);
      ok(m1cp_fw_handle(M1CP_CMD_FW_VALIDATE, pl, 7, out, &olen)==M1CP_ERR_BAD_ARG,
         "FW_VALIDATE bad name -> BAD_ARG"); }

    printf("\n%s (%d/%d)\n", fails==0 ? "ALL PASS" : "FAILURES", count-fails, count);
    fflush(stdout);
    return fails ? 1 : 0;
}

/* test source: resolve any name to the in-memory package + fixed target */
static uint8_t test_src_open(const char *name, m1cp_pkg_reader_t *rd, m1cp_fw_target_t *tgt) {
    (void)name; *rd = reader(); *tgt = target(); return M1CP_ERR_NONE;
}
static void test_src_close(void) {}
void set_test_fw_source(void) { m1cp_fw_set_source(test_src_open, test_src_close); }
