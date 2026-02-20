/*
 * fuzz_feed.c - libFuzzer harness for rcm_feed()
 *
 * Exercises the DUML frame parser state machine with arbitrary data,
 * feeding in variable-sized chunks to cover partial-read reassembly paths.
 * Uses a persistent parser (created once in LLVMFuzzerInitialize) and
 * resets between inputs for performance.
 *
 * Build:
 *   cmake .. -DENABLE_FUZZING=ON && make
 *
 * Run with corpus and dictionary:
 *   ./fuzz_feed corpus_feed -dict=../test/fuzz.dict -max_len=8192
 */

#include "rc_monitor.h"
#include <stdint.h>
#include <stddef.h>

static rcm_parser_t *g_parser;

static void fuzz_callback(const rc_state_t *state, void *userdata) {
    (void)userdata;
    /* Touch all parsed fields via volatile reads so sanitizers detect issues */
    volatile bool b;
    b = state->pause;
    b = state->gohome;
    b = state->shutter;
    b = state->record;
    b = state->custom1;
    b = state->custom2;
    b = state->custom3;
    b = state->five_d.up;
    b = state->five_d.down;
    b = state->five_d.left;
    b = state->five_d.right;
    b = state->five_d.center;
    (void)b;
    volatile rc_flight_mode_t m = state->flight_mode;
    (void)m;
    volatile int16_t v;
    v = state->stick_right.horizontal;
    v = state->stick_right.vertical;
    v = state->stick_left.horizontal;
    v = state->stick_left.vertical;
    v = state->left_wheel;
    v = state->right_wheel;
    (void)v;
    volatile int8_t d = state->right_wheel_delta;
    (void)d;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    g_parser = rcm_create(fuzz_callback, NULL);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Feed data in variable-sized chunks to exercise partial-read paths */
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = 1;
        if (offset + 1 < size) {
            chunk = (data[offset] % 64) + 1; /* 1-64 byte chunks */
            if (chunk > size - offset) chunk = size - offset;
        }
        rcm_feed(g_parser, data + offset, chunk);
        offset += chunk;
    }

    rcm_reset(g_parser);
    return 0;
}
