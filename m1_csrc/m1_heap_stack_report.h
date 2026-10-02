/* See COPYING.txt for license details. */
/*============================================================================*/
/**
 * @file    m1_heap_stack_report.h
 * @brief   Pure, host-testable formatting for the heap/task-stack telemetry
 *          diagnostic (m1_cli.c mtest 5).
 *
 * This file has NO FreeRTOS dependency of its own -- the real heap/stack
 * numbers are read by the CLI command (xPortGetFreeHeapSize(),
 * xPortGetMinimumEverFreeHeapSize(), uxTaskGetStackHighWaterMark()) and
 * handed in here already as plain integers, so this formatting logic links
 * and runs unmodified in a host test binary. Units: heap values are always
 * bytes; task stack values are StackType_t WORDS as FreeRTOS itself reports
 * them, with the word-to-byte conversion (stack_word_bytes = sizeof(StackType_t)
 * on the real target) done explicitly and shown in the formatted line, never
 * silently assumed.
 */
/*============================================================================*/
#ifndef M1_HEAP_STACK_REPORT_H_
#define M1_HEAP_STACK_REPORT_H_

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    /* false: this task's handle was NULL (never created / creation failed),
     * or it is a known one-shot boot task expected to have already deleted
     * itself (m1_runonce_task_handler, m1_system_init_task) -- querying a
     * FreeRTOS handle after the task it names has been deleted is undefined
     * behavior (the TCB memory may already be reused), so this diagnostic
     * never calls uxTaskGetStackHighWaterMark() on one; it reports the
     * honest "unavailable" state below instead of risking a fault. */
    bool available;
    /* Meaningful only when available == true. Raw StackType_t words, exactly
     * as uxTaskGetStackHighWaterMark() returned them -- NOT bytes. */
    long stack_words;
} m1_task_stack_entry_t;

/*
 * Formats one task's stack-headroom line into buf (NUL-terminated; safely
 * truncated, never overflowing bufsz). stack_word_bytes is sizeof(StackType_t)
 * on the real target, passed in explicitly rather than assumed, so the byte
 * conversion shown is always correct for whatever port this runs on.
 * Returns the number of characters that would have been written (like
 * snprintf), which may exceed bufsz-1 if truncated.
 */
int m1_heap_stack_format_task_line(char *buf, size_t bufsz,
                                   const m1_task_stack_entry_t *e,
                                   size_t stack_word_bytes);

/*
 * Formats the heap summary line (both already in bytes -- the units
 * xPortGetFreeHeapSize()/xPortGetMinimumEverFreeHeapSize() already report
 * in, no conversion needed).
 */
int m1_heap_stack_format_heap_line(char *buf, size_t bufsz,
                                   size_t free_heap_bytes,
                                   size_t min_ever_free_heap_bytes);

#ifdef __cplusplus
}
#endif

#endif /* M1_HEAP_STACK_REPORT_H_ */
