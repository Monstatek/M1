/* Run a .m1pkg file through the REAL firmware validator (m1cp_pkg_validate) on
 * the host, for the H573 target, and print the resulting M1CP error name. Used
 * to prove that the generated fixtures yield the documented FW_VALIDATE results
 * before physical-M1 testing.  Build via the Makefile: `make validate_file`.
 *   ./validate_file <file.m1pkg>
 * Exit 0 = validated (NONE); exit 2 = a validation error (name printed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "m1_manager_fw.h"
#include "m1_manager_protocol.h"

uint32_t g_test_tick_ms = 0;

#define DEVID 0x0484u
#define BANK  0x00100000u

static uint8_t *g_buf; static uint32_t g_len;
static m1cp_fsb_rc_t r_size(void *c, uint32_t *o){ (void)c; *o = g_len; return M1CP_FSB_OK; }
static m1cp_fsb_rc_t r_read(void *c, uint32_t off, void *b, uint32_t len, uint32_t *rd){
    (void)c; uint32_t avail = (off <= g_len) ? (g_len - off) : 0; uint32_t n = len < avail ? len : avail;
    memcpy(b, g_buf + off, n); if (rd) *rd = n; return M1CP_FSB_OK;
}

static const char *errname(uint8_t e){
    switch(e){
        case M1CP_ERR_NONE: return "NONE";
        case M1CP_ERR_BAD_PAYLOAD: return "BAD_PAYLOAD";
        case M1CP_ERR_INCOMPATIBLE: return "INCOMPATIBLE";
        case M1CP_ERR_INTEGRITY: return "INTEGRITY";
        case M1CP_ERR_NO_SPACE: return "NO_SPACE";
        case M1CP_ERR_NOT_FOUND: return "NOT_FOUND";
        case M1CP_ERR_IO: return "IO";
        case M1CP_ERR_BAD_ARG: return "BAD_ARG";
        default: { static char b[16]; snprintf(b,sizeof b,"0x%02X",e); return b; }
    }
}

int main(int argc, char **argv){
    if (argc < 2){ fprintf(stderr, "usage: %s <file.m1pkg>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f){ perror("open"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    g_buf = malloc(sz > 0 ? (size_t)sz : 1); g_len = (uint32_t)sz;
    if (sz > 0 && fread(g_buf, 1, (size_t)sz, f) != (size_t)sz){ fprintf(stderr,"read fail\n"); return 1; }
    fclose(f);

    m1cp_pkg_reader_t rd = { r_size, r_read, NULL };
    m1cp_fw_target_t  tgt = { DEVID, BANK };
    m1cp_pkg_info_t   info;
    uint8_t e = m1cp_pkg_validate(&rd, &tgt, &info);

    if (e == M1CP_ERR_NONE)
        printf("%-22s NONE  fw=%u.%u.%u.%u res=%u.%u.%u.%u fw_size=%u res_size=%u\n",
               argv[1], info.fw_version[0], info.fw_version[1], info.fw_version[2], info.fw_version[3],
               info.resource_version[0], info.resource_version[1], info.resource_version[2], info.resource_version[3],
               info.fw_size, info.res_size);
    else
        printf("%-22s %s\n", argv[1], errname(e));
    free(g_buf);
    return e == M1CP_ERR_NONE ? 0 : 2;
}
