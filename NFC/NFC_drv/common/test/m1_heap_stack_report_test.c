/*
 * m1_heap_stack_report_test.c - host tests for the pure formatting behind
 * the heap/task-stack telemetry diagnostic (m1_cli.c mtest 5).
 *
 * Build & run (from repo root):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     m1_csrc/m1_heap_stack_report.c \
 *     NFC/NFC_drv/common/test/m1_heap_stack_report_test.c \
 *     -I m1_csrc -o /tmp/hsrt && /tmp/hsrt
 */
#include "m1_heap_stack_report.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; \
    printf("  FAIL: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

int main(void)
{
    printf("=== m1_heap_stack_report host tests ===\n");
    char buf[160];

    /* Heap line: exact byte units, no conversion. */
    {
        int n = m1_heap_stack_format_heap_line(buf, sizeof(buf), 45000, 38000);
        CHECK(n > 0 && (size_t)n < sizeof(buf), "heap line fits and reports a positive length");
        CHECK(strstr(buf, "45000") != NULL, "heap line reports the exact free-heap value");
        CHECK(strstr(buf, "38000") != NULL, "heap line reports the exact minimum-ever-free value");
        CHECK(strstr(buf, "B") != NULL, "heap line states bytes explicitly");
        CHECK(strcmp(buf, "Heap: free=45000 B  min-ever-free=38000 B") == 0,
              "heap line uses target-supported integer formatting with no literal %zu");
    }

    /* Task line: available, words->bytes conversion shown explicitly. */
    {
        m1_task_stack_entry_t e = { "nfc_worker", true, 500 };
        int n = m1_heap_stack_format_task_line(buf, sizeof(buf), &e, 4);
        CHECK(n > 0 && (size_t)n < sizeof(buf), "task line fits");
        CHECK(strstr(buf, "nfc_worker") != NULL, "task line names the task");
        CHECK(strstr(buf, "500") != NULL, "task line reports the raw word count");
        CHECK(strstr(buf, "2000") != NULL, "task line reports the correctly-converted byte count (500 words * 4 B/word = 2000 B)");
        CHECK(strstr(buf, "word=4 B") != NULL,
              "task line prints the word size with target-supported formatting");
    }

    /* Task line: a different word size produces a different, still-correct
     * byte conversion -- proves the conversion is never hardcoded. */
    {
        m1_task_stack_entry_t e = { "some_task", true, 100 };
        int n = m1_heap_stack_format_task_line(buf, sizeof(buf), &e, 8);
        CHECK(n > 0, "task line with 8-byte words formats");
        CHECK(strstr(buf, "800") != NULL, "8-byte-word conversion is correct (100 * 8 = 800), not hardcoded to 4");
    }

    /* Task line: unavailable (NULL handle or one-shot task) is reported
     * honestly, never as a fabricated 0 or garbage value. */
    {
        m1_task_stack_entry_t e = { "sys_init", false, 12345 /* must be ignored */ };
        int n = m1_heap_stack_format_task_line(buf, sizeof(buf), &e, 4);
        CHECK(n > 0, "unavailable task line formats");
        CHECK(strstr(buf, "sys_init") != NULL, "unavailable task line still names the task");
        CHECK(strstr(buf, "unavailable") != NULL, "unavailable task line honestly says so");
        CHECK(strstr(buf, "12345") == NULL, "unavailable task line never leaks the ignored stack_words value");
    }

    /* NULL entry / NULL name: does not crash, reports unavailable. */
    {
        int n = m1_heap_stack_format_task_line(buf, sizeof(buf), NULL, 4);
        CHECK(n > 0, "NULL entry pointer handled without crashing");
        m1_task_stack_entry_t e2 = { NULL, true, 10 };
        n = m1_heap_stack_format_task_line(buf, sizeof(buf), &e2, 4);
        CHECK(n > 0, "NULL name handled without crashing");
    }

    /* Truncation safety: a too-small buffer never overflows (snprintf
     * contract) -- exercised directly under ASan/UBSan. */
    {
        char tiny[4];
        m1_task_stack_entry_t e = { "a_very_long_task_name_here", true, 999999 };
        int n = m1_heap_stack_format_task_line(tiny, sizeof(tiny), &e, 4);
        CHECK(n >= 0, "oversized content into a tiny buffer never crashes or returns negative");
        CHECK(tiny[sizeof(tiny) - 1] == '\0', "tiny buffer is still NUL-terminated after truncation");
    }

    printf("=== m1_heap_stack_report: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
