/*
 * fuzz_build_packet.c - libFuzzer harness for rcm_build_packet() round-trip
 *
 * Extracts build parameters from fuzz input, calls rcm_build_packet(),
 * then feeds the result through rcm_feed(). Tests builder + parser in
 * combination.
 *
 * Build:
 *   cmake .. -DENABLE_FUZZING=ON && make
 *   ./fuzz_build_packet corpus_feed -dict=../test/fuzz.dict -max_len=1450
 */

#include "rc_monitor.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static rcm_parser_t *g_parser;

static void fuzz_callback(const rc_state_t *state, void *userdata) {
    (void)userdata;
    /* Touch all fields so sanitizers can detect issues */
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

/*
 * Input layout (minimum 8 bytes for header, rest is payload):
 *   [0]    sender_type
 *   [1]    receiver_type
 *   [2-3]  seq_num (LE)
 *   [4]    pack_type (low nibble) | ack_type (high nibble)
 *   [5]    encrypt_type
 *   [6]    cmd_set
 *   [7]    cmd_id
 *   [8..]  payload bytes
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0;

    uint8_t sender_type   = data[0] & 0x1F;   /* 5-bit device type */
    uint8_t sender_index  = 0;
    uint8_t receiver_type = data[1] & 0x1F;
    uint8_t receiver_index = 0;
    uint16_t seq_num      = (uint16_t)(data[2] | (data[3] << 8));
    uint8_t pack_type     = data[4] & 0x0F;
    uint8_t ack_type      = (data[4] >> 4) & 0x0F;
    uint8_t encrypt_type  = data[5] & 0x07;    /* 3-bit field */
    uint8_t cmd_set       = data[6];
    uint8_t cmd_id        = data[7];

    const uint8_t *payload = (size > 8) ? data + 8 : NULL;
    size_t payload_len = (size > 8) ? size - 8 : 0;

    /* Cap payload to DUML max */
    if (payload_len > DUML_MAX_FRAME_LEN - DUML_MIN_FRAME_LEN)
        payload_len = DUML_MAX_FRAME_LEN - DUML_MIN_FRAME_LEN;

    uint8_t out[DUML_MAX_FRAME_LEN];
    int pkt_len = rcm_build_packet(out, sizeof(out),
                                   sender_type, sender_index,
                                   receiver_type, receiver_index,
                                   seq_num, pack_type, ack_type, encrypt_type,
                                   cmd_set, cmd_id,
                                   payload, payload_len);

    if (pkt_len > 0) {
        rcm_feed(g_parser, out, (size_t)pkt_len);
    }

    rcm_reset(g_parser);
    return 0;
}
