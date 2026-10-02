/* See COPYING.txt for license details. */
#include "m1_heap_stack_report.h"
#include <stdio.h>

int m1_heap_stack_format_task_line(char *buf, size_t bufsz,
                                   const m1_task_stack_entry_t *e,
                                   size_t stack_word_bytes)
{
    if (e == NULL || e->name == NULL) {
        return snprintf(buf, bufsz, "(null): unavailable");
    }
    if (!e->available) {
        return snprintf(buf, bufsz, "%s: unavailable (not created, or one-shot task already exited)", e->name);
    }
    /* words -> bytes conversion shown explicitly, never silently assumed. */
    long bytes = e->stack_words * (long)stack_word_bytes;
    /* The target's reduced printf implementation does not support the C99
     * 'z' length modifier: %zu was emitted literally on hardware.  Cast to
     * unsigned long, which is supported by both the target and host libc. */
    return snprintf(buf, bufsz, "%s: %ld words (%ld B, word=%lu B) free stack (high-water mark)",
                    e->name, e->stack_words, bytes, (unsigned long)stack_word_bytes);
}

int m1_heap_stack_format_heap_line(char *buf, size_t bufsz,
                                   size_t free_heap_bytes,
                                   size_t min_ever_free_heap_bytes)
{
    return snprintf(buf, bufsz, "Heap: free=%lu B  min-ever-free=%lu B",
                    (unsigned long)free_heap_bytes,
                    (unsigned long)min_ever_free_heap_bytes);
}
