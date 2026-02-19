/*
 * fuzz_feed.c - libFuzzer harness for rcm_feed()
 *
 * Exercises the DUML frame parser state machine with arbitrary data,
 * feeding in variable-sized chunks to cover partial-read reassembly paths.
 *
 * Build:
 *   cmake .. -DENABLE_FUZZING=ON && make
 *   ./fuzz_feed -max_total_time=60
 */

#include "rc_monitor.h"
#include <stdint.h>
#include <stddef.h>

static void noop_callback(const rc_state_t *state, void *userdata) {
    (void)state;
    (void)userdata;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    rcm_parser_t *p = rcm_create(noop_callback, NULL);
    if (!p) return 0;

    /* Feed data in variable-sized chunks to exercise partial-read paths */
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = 1;
        if (offset + 1 < size) {
            chunk = (data[offset] % 64) + 1; /* 1-64 byte chunks */
            if (chunk > size - offset) chunk = size - offset;
        }
        rcm_feed(p, data + offset, chunk);
        offset += chunk;
    }

    rcm_destroy(p);
    return 0;
}
