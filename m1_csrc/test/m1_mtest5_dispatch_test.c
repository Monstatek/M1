/*
 * Source-seam regression test for the zero-argument mtest 5 diagnostic.
 * Run from the repository root:
 *   cc -std=c11 -Wall -Wextra m1_csrc/test/m1_mtest5_dispatch_test.c \
 *      -o /tmp/m1_mtest5_dispatch_test && /tmp/m1_mtest5_dispatch_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass;
static int g_fail;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = malloc((size_t)size + 1U);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1U, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    char *cli = read_all("m1_csrc/m1_cli.c");
    char *help = read_all("m1_csrc/m1_cli_help.c");
    char *fmt = read_all("m1_csrc/m1_heap_stack_report.c");

    CHECK(cli != NULL, "read m1_cli.c");
    CHECK(help != NULL, "read m1_cli_help.c");
    CHECK(fmt != NULL, "read m1_heap_stack_report.c");
    if (cli != NULL) {
        CHECK(strstr(cli, "(n_params < 2) && (cmd_type != 5)") != NULL,
              "mtest 5 bypasses the generic second-parameter help gate");
        CHECK(strstr(cli, "cmd_m1_mtest_basic_system(pconsole, input_params, n_params, cmd_type)") != NULL,
              "basic-system dispatcher still invokes the real handler");
    }
    if (help != NULL) {
        CHECK(strstr(help, "Syntax: mtest 5\\r\\n") != NULL,
              "CLI help documents the true zero-argument syntax");
    }
    if (fmt != NULL) {
        CHECK(strstr(fmt, "free=%zu") == NULL && strstr(fmt, "word=%zu") == NULL,
              "target telemetry format strings contain no unsupported %zu conversion");
        CHECK(strstr(fmt, "%lu") != NULL,
              "target telemetry formatter uses supported unsigned-long conversions");
    }

    free(cli);
    free(help);
    free(fmt);
    printf("m1_mtest5_dispatch_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
