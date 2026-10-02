/* Host test for the REAL, unmodified m1_ring_buffer.c, linked directly
 * (its only non-portable dependencies -- stm32h5xx_hal.h/main.h for
 * taskENTER_CRITICAL/taskEXIT_CRITICAL/assert -- are satisfied by tiny
 * stub headers; the real .c file is compiled completely unmodified).
 *
 * Proves the wraparound-read unit-mismatch defect: m1_ringbuffer_read()'s
 * wrapped-read branch does `memcpy(&outdata[n_linear], padd_r, ...)` --
 * n_linear is a SLOT count, but outdata is uint8_t*, so for data_size > 1
 * this offsets by n_linear BYTES instead of n_linear*data_size bytes,
 * placing the second (post-wrap) part of the read n_linear*(data_size-1)
 * bytes too early, both corrupting bytes the first part already wrote and
 * leaving the true tail of the destination buffer un-written (stale/
 * uninitialized). Sub-GHz raw sample capture uses data_size=2, so any
 * wrapped read there corrupts pulse-width data on real hardware.
 *
 * Build/run recipe (stub headers created once in the test's own scratch
 * location, not committed -- see the two `cat > ... <<'EOF'` lines this
 * comment documents so the recipe is reproducible without relying on a
 * prior interactive step):
 *   mkdir -p /tmp/stub_ringbuf && cd /tmp/stub_ringbuf
 *   printf '#ifndef STUB_STM32H5XX_HAL_H_\n#define STUB_STM32H5XX_HAL_H_\n#include <stdint.h>\n#endif\n' > stm32h5xx_hal.h
 *   printf '#ifndef STUB_MAIN_H_\n#define STUB_MAIN_H_\n#define taskENTER_CRITICAL() do {} while (0)\n#define taskEXIT_CRITICAL() do {} while (0)\n#include <assert.h>\n#endif\n' > main.h
 *   cd <repo>
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
 *      -I /tmp/stub_ringbuf -I m1_csrc \
 *      m1_csrc/m1_ring_buffer.c m1_csrc/test/m1_ring_buffer_test.c \
 *      -o /tmp/ringbuf_test && /tmp/ringbuf_test
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "m1_ring_buffer.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) g_pass++; else { g_fail++; printf("  FAIL: %s (line %d)\n", (m), __LINE__); } } while (0)

#define CAP_SLOTS 16U   /* ring capacity in elements, for every data_size tested */

/* Fill `slot` (an element of `data_size` bytes) with a recognizable,
 * position-dependent pattern so any misplacement is detectable byte-for-
 * byte, not just "the read returned the right count". */
static void fill_slot(uint8_t *slot, uint8_t data_size, uint16_t slot_index)
{
    for (uint8_t b = 0; b < data_size; b++) {
        slot[b] = (uint8_t)(0x10U * (slot_index + 1U) + b);
    }
}

/* ---- generic exercise: wrap the buffer so a read straddles the ring end,
 * for a given data_size, and verify every byte of every returned element
 * lands at the correct position. ---- */
static void test_wrapped_read(uint8_t data_size)
{
    static uint8_t backing[CAP_SLOTS * 4];   /* big enough for data_size<=4 */
    S_M1_RingBuffer rb;
    uint8_t in[8 * 4];
    uint8_t out[8 * 4];
    uint16_t n;

    memset(backing, 0xEE, sizeof(backing));
    rb.pdata = backing; rb.len = CAP_SLOTS; rb.data_size = data_size;
    rb.end_index = (uint32_t)CAP_SLOTS * data_size;
    rb.head = 0; rb.tail = 0;

    /* Prime head/tail so the ring's "linear run to end" is short (forces
     * the very next read of several slots to wrap): write 12 slots (head
     * advances to 12*data_size), then advance tail to 10 so only 2 slots
     * of backlog remain queued before we ask for a 6-slot straddling read. */
    for (uint16_t i = 0; i < 12U; i++) { fill_slot(&in[0], data_size, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    (void)m1_ringbuffer_advance_read(&rb, 10U);   /* tail = 10*data_size; head = 12*data_size */
    /* Write 6 more (wraps head around the end of the 16-slot ring). */
    for (uint16_t i = 12; i < 18U; i++) { fill_slot(&in[0], data_size, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    /* Backlog is now slots [10..17] (8 slots), straddling the ring end
     * (end at slot 16). Read all 8 in one call -- must wrap internally. */
    memset(out, 0x00, sizeof(out));
    n = m1_ringbuffer_read(&rb, out, 8U);
    CHECK(n == 8U, "wrapped read (data_size varies): returns the full requested count");

    for (uint16_t i = 0; i < 8U; i++) {
        uint8_t expect[4];
        char msg[96];
        fill_slot(expect, data_size, (uint16_t)(10U + i));
        snprintf(msg, sizeof(msg),
                 "wrapped read (data_size=%u, elem %u): byte-exact output position", data_size, i);
        CHECK(memcmp(&out[(size_t)i * data_size], expect, data_size) == 0, msg);
    }
}

static void test_non_wrapped_read(uint8_t data_size)
{
    static uint8_t backing[CAP_SLOTS * 4];
    S_M1_RingBuffer rb;
    uint8_t in[4];
    uint8_t out[8 * 4];
    uint16_t n;

    memset(backing, 0xEE, sizeof(backing));
    rb.pdata = backing; rb.len = CAP_SLOTS; rb.data_size = data_size;
    rb.end_index = (uint32_t)CAP_SLOTS * data_size;
    rb.head = 0; rb.tail = 0;

    for (uint16_t i = 0; i < 5U; i++) { fill_slot(in, data_size, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    memset(out, 0, sizeof(out));
    n = m1_ringbuffer_read(&rb, out, 5U);
    CHECK(n == 5U, "non-wrapped read: returns the full requested count");
    for (uint16_t i = 0; i < 5U; i++) {
        uint8_t expect[4];
        fill_slot(expect, data_size, i);
        CHECK(memcmp(&out[(size_t)i * data_size], expect, data_size) == 0,
              "non-wrapped read: byte-exact at every position");
    }
}

static void test_partial_read(uint8_t data_size)
{
    static uint8_t backing[CAP_SLOTS * 4];
    S_M1_RingBuffer rb;
    uint8_t in[4];
    uint8_t out[8 * 4];
    uint16_t n;

    memset(backing, 0xEE, sizeof(backing));
    rb.pdata = backing; rb.len = CAP_SLOTS; rb.data_size = data_size;
    rb.end_index = (uint32_t)CAP_SLOTS * data_size;
    rb.head = 0; rb.tail = 0;

    for (uint16_t i = 0; i < 5U; i++) { fill_slot(in, data_size, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    memset(out, 0, sizeof(out));
    n = m1_ringbuffer_read(&rb, out, 3U);   /* ask for fewer than available */
    CHECK(n == 3U, "partial read: returns exactly the requested (smaller) count");
    for (uint16_t i = 0; i < 3U; i++) {
        uint8_t expect[4];
        fill_slot(expect, data_size, i);
        CHECK(memcmp(&out[(size_t)i * data_size], expect, data_size) == 0,
              "partial read: byte-exact for the entries actually returned");
    }
}

static void test_exact_end_read(uint8_t data_size)
{
    /* Fill to the ring's own USABLE capacity (len-1 slots -- this
     * implementation keeps one sentinel slot always empty so a full ring
     * is never indistinguishable from an empty one at head==tail; see
     * ringbuffer_get_empty_slots()'s own "minus 1" comment) with no wrap
     * at all -- the boundary case adjacent to (but not actually reaching)
     * wraparound. */
    static uint8_t backing[CAP_SLOTS * 4];
    S_M1_RingBuffer rb;
    uint8_t in[4];
    uint8_t out[CAP_SLOTS * 4];
    uint16_t n;
    const uint16_t usable = CAP_SLOTS - 1U;

    memset(backing, 0xEE, sizeof(backing));
    rb.pdata = backing; rb.len = CAP_SLOTS; rb.data_size = data_size;
    rb.end_index = (uint32_t)CAP_SLOTS * data_size;
    rb.head = 0; rb.tail = 0;

    for (uint16_t i = 0; i < usable; i++) { fill_slot(in, data_size, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    memset(out, 0, sizeof(out));
    n = m1_ringbuffer_read(&rb, out, usable);
    CHECK(n == usable, "exact-end read: returns the full usable ring capacity with no wrap needed");
    for (uint16_t i = 0; i < usable; i++) {
        uint8_t expect[4];
        fill_slot(expect, data_size, i);
        CHECK(memcmp(&out[(size_t)i * data_size], expect, data_size) == 0,
              "exact-end read: byte-exact at every position, including the very last usable slot");
    }
}

static void test_source_unchanged_after_wrapped_read(void)
{
    /* The ring's own backing storage must never be mutated by a read. */
    static uint8_t backing[CAP_SLOTS * 2];
    uint8_t backing_snapshot[CAP_SLOTS * 2];
    S_M1_RingBuffer rb;
    uint8_t in[2];
    uint8_t out[8 * 2];

    memset(backing, 0xEE, sizeof(backing));
    rb.pdata = backing; rb.len = CAP_SLOTS; rb.data_size = 2U;
    rb.end_index = (uint32_t)CAP_SLOTS * 2U;
    rb.head = 0; rb.tail = 0;

    for (uint16_t i = 0; i < 12U; i++) { fill_slot(in, 2U, i); (void)m1_ringbuffer_write(&rb, in, 1U); }
    (void)m1_ringbuffer_advance_read(&rb, 10U);
    for (uint16_t i = 12; i < 18U; i++) { fill_slot(in, 2U, i); (void)m1_ringbuffer_write(&rb, in, 1U); }

    memcpy(backing_snapshot, backing, sizeof(backing));
    (void)m1_ringbuffer_read(&rb, out, 8U);
    CHECK(memcmp(backing, backing_snapshot, sizeof(backing)) == 0,
          "read never mutates the ring's own backing storage");
}

int main(void)
{
    uint8_t sizes[3] = {1U, 2U, 4U};
    for (int i = 0; i < 3; i++) {
        test_non_wrapped_read(sizes[i]);
        test_wrapped_read(sizes[i]);
        test_partial_read(sizes[i]);
        test_exact_end_read(sizes[i]);
    }
    test_source_unchanged_after_wrapped_read();

    printf("m1_ring_buffer_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
