/*
 * fuzz_payload.c - libFuzzer harness for rcm_parse_payload()
 *
 * Exercises the payload decoder with arbitrary data and lengths.
 *
 * Build:
 *   cmake .. -DENABLE_FUZZING=ON && make
 *
 * Run with corpus and dictionary:
 *   ./fuzz_payload corpus_payload -dict=../test/fuzz.dict -max_len=32
 */

#include "rc_monitor.h"
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    rc_state_t state;
    if (rcm_parse_payload(data, size, &state) == 0) {
        /* Touch key output fields via volatile reads so sanitizers detect issues */
        volatile bool b;
        b = state.pause;
        b = state.gohome;
        b = state.shutter;
        b = state.record;
        b = state.custom1;
        b = state.custom2;
        b = state.custom3;
        b = state.five_d.up;
        b = state.five_d.down;
        b = state.five_d.left;
        b = state.five_d.right;
        b = state.five_d.center;
        (void)b;
        volatile rc_flight_mode_t m = state.flight_mode;
        (void)m;
        volatile int16_t v;
        v = state.stick_right.horizontal;
        v = state.stick_right.vertical;
        v = state.stick_left.horizontal;
        v = state.stick_left.vertical;
        v = state.left_wheel;
        v = state.right_wheel;
        (void)v;
        volatile int8_t d = state.right_wheel_delta;
        (void)d;
    }
    return 0;
}
