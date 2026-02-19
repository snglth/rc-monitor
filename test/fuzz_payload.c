/*
 * fuzz_payload.c - libFuzzer harness for rcm_parse_payload()
 *
 * Exercises the payload decoder with arbitrary data and lengths.
 *
 * Build:
 *   cmake .. -DENABLE_FUZZING=ON && make
 *   ./fuzz_payload -max_total_time=60
 */

#include "rc_monitor.h"
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    rc_state_t state;
    rcm_parse_payload(data, size, &state);
    return 0;
}
